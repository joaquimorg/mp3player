#include "netmode.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <atomic>
#include <memory>
#include <string>
#include "net_logic.h"
#include "settings.h"
#include "slot_logic.h"
#include "storage.h"
#include "web_page.h"

namespace netmode {
namespace {

constexpr uint32_t kConnectTimeoutMs = 15000;
// Requests whose Content-Length is above this can never be a valid upload: they
// are refused immediately and the connection is dropped instead of being drained
// (draining costs ~6 s per MB on this hardware).
constexpr long kMaxDrainBytes = (long)slotlogic::kMaxFileSize + 4096;
const char* kApName = "TWATCH-SETUP";
const char* kSlotPrefix = "/api/slot/";

enum class State { Off, Connecting, Sta, Ap };
State s_state = State::Off;
uint32_t s_t0 = 0;
uint32_t s_rebootAt = 0;
std::unique_ptr<WebServer> s_server;

// ---- upload state (one upload at a time: WebServer is single threaded) ----
struct Upload {
  std::unique_ptr<storage::SlotWriter> writer;
  int slot = 0;
  bool active = false;     // START seen for field "file"
  bool dupFile = false;    // a second "file" part: ignored, answered with an error
  bool failed = false;     // rejected/aborted: remaining body is drained
  bool responded = false;  // an error response has already been sent (or nobody is left to get one)
  int errCode = 0;         // pending error answer, sent by the POST handler once the body is drained
  String errMsg;
  bool headChecked = false;
  bool committed = false;
  long contentLength = 0;
  size_t written = 0;      // bytes of file data received so far
};
Upload s_up;
// Mirrors "a file is being received" for other tasks (the UI) while loop() is
// busy inside the upload.
std::atomic<bool> s_receiving{false};

void resetUpload() {
  s_up.writer.reset();  // SlotWriter destructor aborts an uncommitted write
  s_up = Upload();
  s_receiving = false;
}

void sendJson(int code, const std::string& body) {
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(code, "application/json; charset=utf-8", body.c_str());
}

void sendError(int code, const char* msg) { sendJson(code, netlogic::jsonError(msg)); }

// Used inside the upload callback: nothing has been (or will be) written to
// flash for this upload, the partial tmp file is discarded, and the rest of the
// body is drained by the WebServer. The answer is sent by handlePostSlot() once
// the body has been consumed: answering early and then closing with unread data
// makes lwip reset the next connection (observed on hardware).
void failUpload(int code, const char* msg) {
  if (s_up.failed) return;
  s_up.failed = true;
  s_up.writer.reset();
  s_up.errCode = code;
  s_up.errMsg = msg;
  Serial.printf("[net] upload slot %d rejected: %s\n", s_up.slot, msg);
}

// A write/commit failure on a (nearly) full filesystem is answered like the
// up-front NoSpace check (same message, so the PC tool and web page react to it);
// anything else stays a generic 500. Must be called before failUpload() drops the
// writer, because the partial tmp file still counts as used space until then.
void failWrite() {
  if (storage::freeBytes() < slotlogic::kFsOverhead)
    failUpload(507, netlogic::uploadErrorMessage(slotlogic::UploadCheck::NoSpace, storage::hasFile(s_up.slot)));
  else
    failUpload(500, "Erro ao gravar na memória do relógio.");
}

int slotFromUri(const String& uri) {
  if (!uri.startsWith(kSlotPrefix)) return 0;
  return netlogic::parseSlotNumber(uri.substring(strlen(kSlotPrefix)).c_str());
}

void handleUpload() {
  HTTPUpload& up = s_server->upload();
  if (up.status == UPLOAD_FILE_START) {
    if (up.name != "file") return;  // ignore other file fields
    if (s_up.active) {              // second "file" part: keep the first outcome
      s_up.dupFile = true;
      return;
    }
    s_up.active = true;
    s_receiving = true;
    s_up.slot = slotFromUri(s_server->uri());
    s_up.contentLength = s_server->clientContentLength();
    // Detect a vanished peer (Wi-Fi dropped without FIN) in ~5 s instead of never.
    int on = 1, idle = 2, intvl = 1, cnt = 3;
    WiFiClient c = s_server->client();
    c.setSocketOption(SOL_SOCKET, SO_KEEPALIVE, &on, sizeof on);
    c.setSocketOption(IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    c.setSocketOption(IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof intvl);
    c.setSocketOption(IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof cnt);
    Serial.printf("[net] upload start slot %d name '%s' content-length %ld heap %u\n", s_up.slot,
                  up.filename.c_str(), s_up.contentLength, (unsigned)ESP.getFreeHeap());
    if (s_up.contentLength > kMaxDrainBytes) {  // checked first: whatever else is wrong, never drain this
      failUpload(400, netlogic::uploadErrorMessage(slotlogic::UploadCheck::TooBig, false));
      sendError(400, s_up.errMsg.c_str());
      s_up.responded = true;
      // Not worth reading megabytes just to discard them. client() returns a copy of
      // the handle, so stop() on it would not close anything: shut the socket down so
      // the parser's next read fails and the request is dropped at once.
      int fd = c.fd();
      if (fd >= 0) lwip_shutdown(fd, SHUT_RDWR);
    } else if (slotlogic::slotPath(s_up.slot) == nullptr) {
      failUpload(400, netlogic::uploadErrorMessage(slotlogic::UploadCheck::BadSlot, false));
    } else if (s_up.contentLength <= 0) {
      failUpload(400, "Pedido sem tamanho (Content-Length).");
    }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!s_up.active || s_up.dupFile || s_up.failed || up.currentSize == 0) return;
    if (!s_up.headChecked) {
      // First chunk: validate with the real first 4 bytes BEFORE touching flash.
      s_up.headChecked = true;
      uint8_t head[4] = {0, 0, 0, 0};
      memcpy(head, up.buf, up.currentSize < 4 ? up.currentSize : 4);
      size_t est = netlogic::estimateFileSize(s_up.contentLength);
      slotlogic::UploadCheck chk = slotlogic::validateUpload(s_up.slot, est, storage::freeBytes(), head);
      if (chk != slotlogic::UploadCheck::Ok) {
        failUpload(400, netlogic::uploadErrorMessage(chk, storage::hasFile(s_up.slot)));
        return;
      }
      s_up.writer.reset(new storage::SlotWriter(s_up.slot));
    }
    if (s_up.written + up.currentSize > slotlogic::kMaxFileSize) {
      failUpload(400, netlogic::uploadErrorMessage(slotlogic::UploadCheck::TooBig, false));
      return;
    }
    if (!s_up.writer || !s_up.writer->write(up.buf, up.currentSize)) {
      failWrite();
      return;
    }
    s_up.written += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (!s_up.active || s_up.dupFile || s_up.failed) return;
    if (!s_up.headChecked) {  // no data at all: empty file
      failUpload(400, netlogic::uploadErrorMessage(slotlogic::UploadCheck::NotMp3, false));
      return;
    }
    if (!s_up.writer || s_up.written == 0 || !s_up.writer->commit()) {
      failWrite();
      return;
    }
    s_up.writer.reset();
    s_up.committed = true;
    std::string name = netlogic::sanitizeFileName(up.filename.c_str());
    storage::setSlotName(s_up.slot, String(name.c_str()));  // "" -> default "Faixa N"
    Serial.printf("[net] upload slot %d committed %u bytes, name '%s', heap %u\n", s_up.slot,
                  (unsigned)s_up.written, name.c_str(), (unsigned)ESP.getFreeHeap());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Serial.printf("[net] upload slot %d ABORTED by client after %u bytes; old slot kept\n", s_up.slot,
                  (unsigned)s_up.written);
    resetUpload();  // abort + remove tmp, forget everything: nobody is left to answer
  }
}

void handlePostSlot() {
  if (s_up.responded) {  // already answered from the upload callback
    resetUpload();
    return;
  }
  if (s_up.dupFile) {
    resetUpload();
    sendError(400, "Só é permitido um ficheiro por pedido.");
    return;
  }
  if (s_up.failed) {
    int code = s_up.errCode;
    String msg = s_up.errMsg;
    resetUpload();
    sendError(code, msg.c_str());
    return;
  }
  if (s_up.committed) {
    char b[80];
    snprintf(b, sizeof b, "{\"ok\":true,\"slot\":%d,\"size\":%u}", s_up.slot, (unsigned)storage::slotSize(s_up.slot));
    resetUpload();
    sendJson(200, b);
    return;
  }
  resetUpload();
  sendError(400, "Nenhum ficheiro recebido (campo \"file\").");
}

void handleDeleteSlot() {
  int slot = slotFromUri(s_server->uri());
  if (!slotlogic::slotPath(slot)) {
    sendError(400, netlogic::uploadErrorMessage(slotlogic::UploadCheck::BadSlot, false));
    return;
  }
  if (!storage::remove(slot)) {
    sendError(500, "Não foi possível apagar o slot.");
    return;
  }
  Serial.printf("[net] slot %d deleted\n", slot);
  sendJson(200, "{\"ok\":true}");
}

void handleSlots() {
  netlogic::SlotInfo info[slotlogic::kNumSlots];
  for (int i = 0; i < slotlogic::kNumSlots; i++) {
    info[i].size = storage::slotSize(i + 1);
    if (info[i].size) info[i].name = storage::slotName(i + 1).c_str();
  }
  sendJson(200, netlogic::buildSlotsJson(storage::freeBytes(), info));
}

void handleNotFound() { sendError(404, "Não encontrado."); }

void handleWifiSave() {
  String ssid = s_server->arg("ssid");
  String pass = s_server->arg("pass");
  ssid.trim();
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 63 || (pass.length() > 0 && pass.length() < 8)) {
    s_server->send(400, "text/html; charset=utf-8",
                   "<meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                   "<p style='font-family:sans-serif'>Dados inválidos: o nome da rede é obrigatório "
                   "(máx. 32) e a palavra-passe deve ter entre 8 e 63 caracteres (ou ficar vazia numa rede aberta)."
                   "</p><p><a href='/'>Voltar</a></p>");
    return;
  }
  settings::setWifi(ssid, pass);
  Serial.printf("[net] wifi credentials saved; rebooting\n");
  s_server->send_P(200, "text/html; charset=utf-8", kSetupSavedPage);
  s_rebootAt = millis() + 1500;
}

void startServer(bool setupMode) {
  s_server.reset(new WebServer(80));
  if (setupMode) {
    s_server->on("/", HTTP_GET, [] { s_server->send_P(200, "text/html; charset=utf-8", kSetupPage); });
    s_server->on("/wifi", HTTP_POST, handleWifiSave);
    // Captive-portal probes and anything else: send the form.
    s_server->onNotFound([] { s_server->send_P(200, "text/html; charset=utf-8", kSetupPage); });
  } else {
    s_server->on("/", HTTP_GET, [] {
      s_server->sendHeader("Cache-Control", "no-store");
      s_server->send_P(200, "text/html; charset=utf-8", kMainPage);
    });
    s_server->on("/api/slots", HTTP_GET, handleSlots);
    s_server->on(UriBraces("/api/slot/{}"), HTTP_POST, handlePostSlot, handleUpload);
    s_server->on(UriBraces("/api/slot/{}"), HTTP_DELETE, handleDeleteSlot);
    s_server->onNotFound(handleNotFound);
  }
  s_server->begin();
}

void startAp() {
  if (s_server) s_server->stop();
  s_server.reset();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(kApName);
  delay(100);
  startServer(true);
  s_state = State::Ap;
  Serial.printf("[net] setup AP '%s' started, page at http://%s/\n", kApName, WiFi.softAPIP().toString().c_str());
}

}  // namespace

void begin() {
  end();
  String ssid = settings::wifiSsid();
  if (ssid.isEmpty()) {
    Serial.println("[net] no saved Wi-Fi credentials");
    startAp();
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);  // modem sleep adds 100-300 ms per round trip and slows uploads
  WiFi.begin(ssid.c_str(), settings::wifiPass().c_str());
  s_t0 = millis();
  s_state = State::Connecting;
  Serial.printf("[net] connecting to saved network (timeout %u s)\n", (unsigned)(kConnectTimeoutMs / 1000));
}

void loop() {
  switch (s_state) {
    case State::Connecting:
      if (WiFi.status() == WL_CONNECTED) {
        startServer(false);
        s_state = State::Sta;
        Serial.printf("[net] connected, http://%s/ (heap %u)\n", WiFi.localIP().toString().c_str(),
                      (unsigned)ESP.getFreeHeap());
      } else if (millis() - s_t0 >= kConnectTimeoutMs) {
        Serial.println("[net] connection timed out");
        startAp();
      }
      break;
    case State::Sta:
    case State::Ap:
      if (s_server) {
        s_server->handleClient();
        resetUpload();  // a request is fully handled (or dropped) here: never carry state over
      }
      if (s_rebootAt && (int32_t)(millis() - s_rebootAt) >= 0) ESP.restart();
      break;
    case State::Off:
      break;
  }
}

void end() {
  resetUpload();
  if (s_server) s_server->stop();
  s_server.reset();
  s_rebootAt = 0;
  if (s_state != State::Off) {
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  s_state = State::Off;
}

String ipText() {
  switch (s_state) {
    case State::Sta:
      // Link dropped (auto-reconnect pending): never show 0.0.0.0.
      return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("Sem ligação Wi-Fi");
    case State::Ap: return String("192.168.4.1 (") + kApName + ")";
    case State::Connecting: return "A ligar...";
    default: return "";
  }
}

bool inSetupAp() { return s_state == State::Ap; }
bool connecting() { return s_state == State::Connecting; }
bool uploading() { return s_receiving.load(); }

}  // namespace netmode
