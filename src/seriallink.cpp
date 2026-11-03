#include "seriallink.h"

#include <atomic>
#include <memory>
#include <new>
#include <string>
#include <string.h>

#include <esp_heap_caps.h>

#include "config.h"
#include "net_logic.h"
#include "netmode.h"
#include "player.h"
#include "serproto.h"
#include "settings.h"
#include "slot_logic.h"
#include "storage.h"
#include "transfer.h"
#ifdef UI_DEBUG
#include "ui.h"
#endif

// Protocol rules (clients, see spec "Protocolo serie"):
//  - Send a lone LF (backslash-n) before the first command: it ends any partial/over-long line left in the buffer
//    (empty lines are ignored). Lines end at CR or LF. A line over 200 bytes is answered "ERRO tamanho"
//    at once and discarded up to its terminator.
//  - WIFI <ssid>[TAB]<pass>: the FIRST TAB separates SSID from password, so an SSID cannot contain TAB/CR/LF.
//    settings::setWifi writes ssid and password as two separate NVS writes (not atomic).
//  - TWATCH? reply: "wifi=" is the SSID verbatim (control chars -> '?', may contain spaces): parse it up
//    to the LAST " ip=".
//  - MODO replies OK when the switch is ACCEPTED, not when it is done: poll TWATCH? until modo= changes.
//  - Under UI_DEBUG, lines that are not protocol commands go to the debug console (ui::debugCommand).
//
// BAUD 921600|115200 -> "OK" (sent and flushed at the old speed), then the watch switches; the PC switches after
// reading OK. The watch goes back to 115200 by itself at the end of a PUT (success or failure) and after 10 s
// without any received byte outside a PUT, so a crashed PC app cannot leave the link at the wrong speed.
//
// PUT <slot> <size> <crc32 hex> <name hex> (any mode):
//  - Refused with "ERRO ocupado" while a mode switch is pending, a Wi-Fi upload or another transfer is running;
//    "ERRO argumentos" (parser: bad slot/size/crc/name), "ERRO espaco" (size + 16 KiB > free), "ERRO nao-mp3"
//    (size 0). Otherwise "OK PUT" and the link is in binary block mode.
//  - Blocks [seq][data <= 4096][crc16 BE] (serproto::BlockReceiver). Per block the watch answers 'K' (accepted,
//    or a duplicate of the last accepted block: not written again) or 'R' (bad CRC, bad seq, or 3 s silence
//    in the middle of a frame). Block 0's first 4 bytes are checked as MP3: "ERRO nao-mp3" and the PUT ends.
//  - After the last block, instead of 'K': "OK <crc32 8 hex>" (crc matches, committed, named) or "ERRO crc" /
//    "ERRO gravar". "ERRO espaco"/"ERRO gravar" can also replace a 'K' when a flash write fails.
//  - "CANCELAR" (line) at a frame boundary -> abort, "OK". Other valid command lines get "ERRO ocupado".
//    (If 'C' happens to be the expected seq byte, CANCELAR is only recognised after the 3 s frame timeout.)
//  - 5 s without any byte (or 30 s without an accepted block) -> abort ("ERRO tempo", usually unread).
//  - Any end: the SlotWriter is committed or aborted (old slot intact, no .tmp), the on-watch overlay shows
//    Done/Failed, playback resumes if it was playing, and the link returns to 115200 after the reply is flushed.
//  - Playback is paused during the PUT; uploading into the slot being played stops it (the file is replaced)
//    and plays that slot again from the start afterwards if it was playing.
constexpr int STACK = 8192;  // LittleFS writes/rename and names.json are done from this task

namespace seriallink {
namespace {

constexpr uint32_t kBaudDefault = 115200;
constexpr uint32_t kSilenceAbortMs = 5000;   // PUT: no byte at all
constexpr uint32_t kFrameTimeoutMs = 3000;   // PUT: partial frame, then 'R'
constexpr uint32_t kDrainSilenceMs = 20;     // PUT: after a bad frame, wait for the line to go quiet
constexpr uint32_t kNoProgressMs = 30000;    // PUT: bytes keep coming but no block is ever accepted
constexpr uint32_t kHighBaudIdleMs = 10000;  // outside a PUT at 921600: back to 115200

Host s_host;
char s_line[serproto::kMaxLine + 1];
size_t s_len = 0;
bool s_overflow = false;  // current line is too long: discard until its terminator
uint32_t s_lastRx = 0;    // millis() of the last received byte (or of a baud switch)
bool s_highBaud = false;

std::atomic<bool> s_putClaim{false};  // see putActive()

struct Put {
  bool on = false;
  int slot = 0;
  uint32_t size = 0, crc = 0;
  char name[serproto::kMaxName + 1] = {0};  // sanitized UTF-8, "" -> default name
  serproto::BlockReceiver* rx = nullptr;    // ~4.1 KB, heap (PSRAM when available)
  std::unique_ptr<storage::SlotWriter> writer;
  bool wasPlaying = false;
  int replaySlot = 0;  // the playing slot was stopped because it is being replaced: play it again after
  bool inFrame = false;
  bool draining = false;
  uint8_t cap[24];  // first bytes of the current frame / drained junk, to spot a command line
  size_t capLen = 0;
  uint32_t lastProgress = 0;
};
Put s_put;

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

void setBaud(uint32_t baud) {
  Serial.flush();  // the reply must leave at the old speed
  Serial.updateBaudRate(baud);
  s_highBaud = baud != kBaudDefault;
  s_lastRx = millis();
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

// ---- PUT ---------------------------------------------------------------------------------------------

void freeReceiver() {
  if (!s_put.rx) return;
  s_put.rx->~BlockReceiver();
  heap_caps_free(s_put.rx);
  s_put.rx = nullptr;
}

// Ends the PUT. failMsg == nullptr: success (already committed). Sends `line`, then returns to 115200.
void endPut(const char* line, const char* failMsg) {
  s_put.writer.reset();  // uncommitted: aborts, removes /slotN.tmp, old slot untouched
  if (failMsg) transfer::fail(failMsg);
  freeReceiver();
  reply(line);
  if (s_highBaud) setBaud(kBaudDefault);
  if (s_put.replaySlot) player::play(s_put.replaySlot);
  else if (s_put.wasPlaying) player::resume();
  s_put.on = false;
  s_put.inFrame = s_put.draining = false;
  s_put.capLen = 0;
  s_putClaim = false;
  s_len = 0;
  s_overflow = false;
  memset(s_line, 0, sizeof s_line);
}

void startPut(const serproto::Parsed& p) {
  if (s_host.busy() || transfer::active()) { replyError("ocupado"); return; }
  // Claim first, then check the others (they set theirs first, then check ours): no window for both to start.
  s_putClaim = true;
  if (s_host.busy() || transfer::active() || netmode::uploading()) {
    s_putClaim = false;
    replyError("ocupado");
    return;
  }
  static const uint8_t kAnyMp3[4] = {0xFF, 0xFB, 0x90, 0x00};  // the real head is checked on block 0
  slotlogic::UploadCheck chk = slotlogic::validateUpload(p.slot, p.size, storage::freeBytes(), kAnyMp3);
  if (chk != slotlogic::UploadCheck::Ok) {
    s_putClaim = false;
    replyError(chk == slotlogic::UploadCheck::NoSpace ? "espaco"
               : chk == slotlogic::UploadCheck::NotMp3 ? "nao-mp3"
                                                         : "argumentos");
    return;
  }
  void* mem = heap_caps_malloc(sizeof(serproto::BlockReceiver), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) mem = heap_caps_malloc(sizeof(serproto::BlockReceiver), MALLOC_CAP_8BIT);
  if (!mem) {
    s_putClaim = false;
    replyError("memoria");
    return;
  }
  s_put.rx = new (mem) serproto::BlockReceiver(p.size);
  s_put.slot = p.slot;
  s_put.size = p.size;
  s_put.crc = p.crc32;
  std::string nm = netlogic::sanitizeFileName(std::string(p.name));
  if (nm.size() > serproto::kMaxName) nm.resize(serproto::kMaxName);  // never longer: the parser caps it
  memcpy(s_put.name, nm.c_str(), nm.size());
  s_put.name[nm.size()] = 0;

  // Playback: pause; the slot being played is about to be replaced, so that one is stopped (file closed).
  s_put.wasPlaying = player::playing();
  s_put.replaySlot = 0;
  if ((player::playing() || player::paused()) && player::currentSlot() == p.slot) {
    if (s_put.wasPlaying) s_put.replaySlot = p.slot;
    s_put.wasPlaying = false;
    player::stop();
  } else if (s_put.wasPlaying) {
    player::pause();
  }

  char shown[serproto::kMaxName + 12];
  if (s_put.name[0]) snprintf(shown, sizeof shown, "%s", s_put.name);
  else snprintf(shown, sizeof shown, "Faixa %d", p.slot);
  transfer::begin(transfer::Source::Usb, p.slot, shown, p.size);

  s_put.on = true;
  s_put.inFrame = s_put.draining = false;
  s_put.capLen = 0;
  s_put.lastProgress = millis();
  s_lastRx = millis();
  reply("OK PUT");
}

void finishPut() {
  if (s_put.rx->crc32() != s_put.crc) {
    endPut("ERRO crc", "O ficheiro chegou com erros (CRC). Tenta de novo.");
    return;
  }
  if (!s_put.writer || !s_put.writer->commit()) {
    endPut("ERRO gravar", "Erro ao gravar na memória do relógio.");
    return;
  }
  s_put.writer.reset();
  storage::setSlotName(s_put.slot, String(s_put.name));  // "" -> default "Faixa N"
  transfer::done();  // after commit + name: the file is complete
  char out[24];
  snprintf(out, sizeof out, "OK %08X", (unsigned)s_put.crc);
  endPut(out, nullptr);
}

// An accepted block (Ok or Done). Returns false if the PUT ended.
bool acceptBlock(bool last) {
  const uint8_t* d = s_put.rx->data();
  size_t n = s_put.rx->dataLen();
  if (!s_put.writer) {  // block 0: validate the real head before touching flash
    uint8_t head[4] = {0, 0, 0, 0};
    memcpy(head, d, n < 4 ? n : 4);
    slotlogic::UploadCheck chk = slotlogic::validateUpload(s_put.slot, s_put.size, storage::freeBytes(), head);
    if (chk == slotlogic::UploadCheck::NotMp3) {
      endPut("ERRO nao-mp3", "O ficheiro não é um MP3.");
      return false;
    }
    if (chk != slotlogic::UploadCheck::Ok) {
      endPut("ERRO espaco", "Não há espaço livre suficiente no relógio.");
      return false;
    }
    s_put.writer.reset(new storage::SlotWriter(s_put.slot));
  }
  if (!s_put.writer->write(d, n)) {
    bool full = storage::freeBytes() < slotlogic::kFsOverhead;  // the partial tmp still counts here
    endPut(full ? "ERRO espaco" : "ERRO gravar",
           full ? "Não há espaço livre suficiente no relógio." : "Erro ao gravar na memória do relógio.");
    return false;
  }
  transfer::progress(s_put.rx->received());
  s_put.lastProgress = millis();
  if (last) {
    finishPut();
    return false;
  }
  Serial.write('K');
  return true;
}

void capture(const uint8_t* b, size_t n) {
  size_t room = sizeof s_put.cap - s_put.capLen;
  if (n > room) n = room;
  memcpy(s_put.cap + s_put.capLen, b, n);
  s_put.capLen += n;
}

// The current frame was dropped (bad CRC/seq after draining, or timeout). If its bytes were a command line,
// answer it; otherwise ask for the block again.
void frameDropped() {
  s_put.rx->abortFrame();
  s_put.inFrame = s_put.draining = false;
  size_t end = 0;
  while (end < s_put.capLen && s_put.cap[end] != '\n' && s_put.cap[end] != '\r') ++end;
  bool isLine = end > 0 && end < s_put.capLen;
  serproto::Parsed p;
  bool cmd = isLine && serproto::parseLine((const char*)s_put.cap, end, p);
  bool cancel = cmd && p.cmd == serproto::Cmd::Cancel;
  memset(&p, 0, sizeof p);
  s_put.capLen = 0;
  if (cancel) endPut("OK", "Envio cancelado.");
  else if (cmd) replyError("ocupado");
  else Serial.write('R');
}

// Feeds received bytes to the block receiver. Returns how many were consumed (the PUT may end in between:
// the rest then goes to the line parser).
size_t putFeed(const uint8_t* b, size_t n) {
  if (s_put.draining) {
    capture(b, n);
    return n;
  }
  size_t done = 0;
  while (done < n && s_put.on) {
    if (!s_put.inFrame) {
      s_put.inFrame = true;
      s_put.capLen = 0;
    }
    size_t used = 0;
    serproto::BlockReceiver::Result r = s_put.rx->feed(b + done, n - done, used);
    capture(b + done, used);
    done += used;
    switch (r) {
      case serproto::BlockReceiver::Result::NeedMore:
        break;
      case serproto::BlockReceiver::Result::Ok:
      case serproto::BlockReceiver::Result::Done:
        s_put.inFrame = false;
        if (!acceptBlock(r == serproto::BlockReceiver::Result::Done)) return done;
        break;
      case serproto::BlockReceiver::Result::OkDuplicate:  // our 'K' was lost: confirm again, do not write
        s_put.inFrame = false;
        s_put.lastProgress = millis();
        Serial.write('K');
        break;
      case serproto::BlockReceiver::Result::BadCrc:
      case serproto::BlockReceiver::Result::BadSeq:
        s_put.draining = true;  // the rest of the chunk (and of the frame) is junk until the line is quiet
        capture(b + done, n - done);
        return n;
    }
  }
  return done;
}

void putTimers(uint32_t now) {
  if (!s_put.on) return;
  uint32_t quiet = now - s_lastRx;
  if (quiet >= kSilenceAbortMs) {
    endPut("ERRO tempo", "Envio interrompido: o computador deixou de enviar.");
  } else if (now - s_put.lastProgress >= kNoProgressMs) {
    endPut("ERRO tempo", "Envio interrompido: demasiados erros na ligação.");
  } else if (s_put.draining && quiet >= kDrainSilenceMs) {
    frameDropped();
  } else if (s_put.inFrame && quiet >= kFrameTimeoutMs) {
    frameDropped();
  }
}

// ---- lines ---------------------------------------------------------------------------------------------

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
    case serproto::Cmd::Baud:
      reply("OK");
      setBaud((uint32_t)p.baud);
      break;
    case serproto::Cmd::Put:
      startPut(p);
      break;
    case serproto::Cmd::Cancel:  // no PUT running: nothing to cancel
      reply("OK");
      break;
    default:  // List: a later task
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
  uint8_t buf[512];
  for (;;) {
    int n = Serial.available();
    if (n <= 0) {
      uint32_t now = millis();
      if (s_put.on) {
        putTimers(now);
      } else if (s_highBaud && now - s_lastRx >= kHighBaudIdleMs) {
        setBaud(kBaudDefault);  // the PC app vanished at 921600
        s_len = 0;
        s_overflow = false;
      }
      vTaskDelay(pdMS_TO_TICKS(s_put.on ? 2 : 5));
      continue;
    }
    n = Serial.readBytes(buf, n < (int)sizeof buf ? n : (int)sizeof buf);
    if (n <= 0) continue;
    s_lastRx = millis();
    size_t i = 0;
    while (i < (size_t)n) {
      if (s_put.on) i += putFeed(buf + i, (size_t)n - i);
      else feed(buf[i++]);
    }
    if (s_put.on) putTimers(millis());  // the no-progress limit also applies to a never-silent line
    memset(buf, 0, sizeof buf);
  }
}

}  // namespace

bool putActive() { return s_putClaim.load(); }

void begin(const Host& host) {
  s_host = host;
  s_lastRx = millis();
  xTaskCreatePinnedToCore(task, "serial", STACK, nullptr, 1, nullptr, 0);
}

}  // namespace seriallink
