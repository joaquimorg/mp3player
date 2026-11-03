#include "transfer.h"

#include <string.h>

namespace transfer {
namespace {
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Info s_info = {State::Idle, Source::None, 0, {0}, 0, 0, 0, 0, {0}, 0};

// Copies at most cap-1 bytes without cutting a UTF-8 sequence in half.
void copyUtf8(char* dst, size_t cap, const char* src) {
  size_t n = src ? strlen(src) : 0;
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) --n;  // src[n] must start a character
  }
  if (n) memcpy(dst, src, n);
  dst[n] = 0;
}
}  // namespace

void begin(Source src, int slot, const char* name, uint32_t total) {
  uint32_t now = millis();
  portENTER_CRITICAL(&s_mux);
  s_info.state = State::Receiving;
  s_info.src = src;
  s_info.slot = slot;
  copyUtf8(s_info.name, sizeof s_info.name, name);
  s_info.received = 0;
  s_info.total = total;
  s_info.startMs = now;
  s_info.endMs = 0;
  s_info.msg[0] = 0;
  ++s_info.version;
  portEXIT_CRITICAL(&s_mux);
}

void progress(uint32_t received) {
  portENTER_CRITICAL(&s_mux);
  if (s_info.state == State::Receiving && received != s_info.received) {
    s_info.received = received;
    ++s_info.version;
  }
  portEXIT_CRITICAL(&s_mux);
}

void done() {
  uint32_t now = millis();
  portENTER_CRITICAL(&s_mux);
  if (s_info.state == State::Receiving) {
    s_info.state = State::Done;
    s_info.endMs = now;
    ++s_info.version;
  }
  portEXIT_CRITICAL(&s_mux);
}

void fail(const char* msg) {
  uint32_t now = millis();
  portENTER_CRITICAL(&s_mux);
  if (s_info.state == State::Receiving) {
    s_info.state = State::Failed;
    s_info.endMs = now;
    copyUtf8(s_info.msg, sizeof s_info.msg, msg);
    ++s_info.version;
  }
  portEXIT_CRITICAL(&s_mux);
}

Info snapshot() {
  Info i;
  portENTER_CRITICAL(&s_mux);
  i = s_info;
  portEXIT_CRITICAL(&s_mux);
  return i;
}

bool active() {
  portENTER_CRITICAL(&s_mux);
  bool a = s_info.state == State::Receiving;
  portEXIT_CRITICAL(&s_mux);
  return a;
}

}  // namespace transfer
