#include "seriallink.h"

#include <string.h>

#include "config.h"
#include "netmode.h"
#include "serproto.h"
#include "settings.h"
#include "storage.h"
#include "transfer.h"
#ifdef UI_DEBUG
#include "ui.h"
#endif

namespace seriallink {
namespace {

Host s_host;
char s_line[serproto::kMaxLine + 1];
size_t s_len = 0;
bool s_overflow = false;  // current line is too long: discard until its terminator

void reply(const char* text) {
  // One write per reply so a concurrent debug print cannot split it.
  char out[256];
  int n = snprintf(out, sizeof out - 1, "%s\n", text);
  if (n > 0) Serial.write((const uint8_t*)out, (size_t)(n < (int)sizeof out - 1 ? n : sizeof out - 1));
}

void replyError(const char* err) {
  char out[48];
  snprintf(out, sizeof out, "ERRO %s", err ? err : "comando");
  reply(out);
}

// "192.168.4.1 (TWATCH-SETUP)" -> "192.168.4.1"; anything not starting with a digit ("A ligar...", "") -> "-".
void ipToken(char* out, size_t cap) {
  String t = s_host.ipText ? s_host.ipText() : String();
  size_t n = 0;
  if (t.length() && t[0] >= '0' && t[0] <= '9')
    while (n < t.length() && n + 1 < cap && t[n] != ' ') { out[n] = t[n]; ++n; }
  if (n == 0) { out[0] = '-'; n = 1; }
  out[n] = 0;
}

void replyHello() {
  String ssid = settings::wifiSsid();
  char ssidTxt[40];
  size_t n = 0;
  for (size_t i = 0; i < ssid.length() && n + 1 < sizeof ssidTxt; ++i) {
    char c = ssid[i];
    ssidTxt[n++] = (c < 0x20 || c == 0x7f) ? '?' : c;  // never let the SSID break the line
  }
  if (n == 0) ssidTxt[n++] = '-';
  ssidTxt[n] = 0;
  char ip[48];
  ipToken(ip, sizeof ip);
  char out[200];
  snprintf(out, sizeof out, "OK TWATCH %s modo=%s wifi=%s ip=%s livre=%u", FW_VERSION, s_host.modeName(), ssidTxt, ip,
           (unsigned)storage::freeBytes());
  reply(out);
}

void handle(const char* line, size_t len) {
  serproto::Parsed p;
  bool ok = serproto::parseLine(line, len, p);
  if (!ok) {
#ifdef UI_DEBUG
    if (p.err && strcmp(p.err, "comando") == 0) {  // not ours: the debug console
      ui::debugCommand(line);
      memset(&p, 0, sizeof p);
      return;
    }
#endif
    replyError(p.err);
    memset(&p, 0, sizeof p);
    return;
  }
  switch (p.cmd) {
    case serproto::Cmd::Hello:
      replyHello();
      break;
    case serproto::Cmd::WifiCheck:  // validated by the parser; saves nothing
      reply("OK");
      break;
    case serproto::Cmd::Wifi:
      settings::setWifi(String(p.ssid), String(p.pass));
      reply("OK");
      break;
    case serproto::Cmd::ModeProg:
    case serproto::Cmd::ModePlay: {
      bool program = p.cmd == serproto::Cmd::ModeProg;
      bool already = strcmp(s_host.modeName(), program ? "PROGRAMACAO" : "REPRODUCAO") == 0;
      if (s_host.busy() || transfer::active()) replyError("ocupado");
      else if (already) reply("OK");
      else if (s_host.requestMode(program)) reply("OK");
      else replyError("ocupado");
      break;
    }
    default:  // Baud, Put, Cancel, List: later tasks
      replyError("nao-suportado");
      break;
  }
  memset(&p, 0, sizeof p);  // the password must not linger on the stack
}

void feed(uint8_t b) {
  if (b == '\n' || b == '\r') {
    if (s_overflow) s_overflow = false;  // ERRO tamanho was already sent
    else if (s_len) handle(s_line, s_len);  // empty lines (CRLF, resync) are ignored
    memset(s_line, 0, sizeof s_line);
    s_len = 0;
    return;
  }
  if (s_overflow) return;
  if (s_len >= serproto::kMaxLine) {
    s_overflow = true;
    memset(s_line, 0, sizeof s_line);
    s_len = 0;
    replyError("tamanho");  // right away, so a never-ending line is still answered
    return;
  }
  s_line[s_len++] = (char)b;
}

void task(void*) {
  uint8_t buf[64];
  for (;;) {
    int n = Serial.available();
    if (n <= 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    n = Serial.readBytes(buf, n < (int)sizeof buf ? n : (int)sizeof buf);
    for (int i = 0; i < n; ++i) feed(buf[i]);
    memset(buf, 0, sizeof buf);
  }
}

}  // namespace

void begin(const Host& host) {
  s_host = host;
  xTaskCreatePinnedToCore(task, "serial", 5120, nullptr, 1, nullptr, 0);
}

}  // namespace seriallink
