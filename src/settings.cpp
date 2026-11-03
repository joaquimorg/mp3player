#include "settings.h"
#include <Preferences.h>

namespace settings {
namespace {
const char* kNs = "mp3";

String getStr(const char* key) {
  Preferences p;
  if (!p.begin(kNs, true)) return String();
  String v = p.getString(key, "");
  p.end();
  return v;
}

void putStr(const char* key, const String& v) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putString(key, v);
  p.end();
}
}  // namespace

String wifiSsid() { return getStr("ssid"); }
String wifiPass() { return getStr("pass"); }

void setWifi(const String& ssid, const String& pass) {
  putStr("ssid", ssid);
  putStr("pass", pass);
}

String lastBtDevice() { return getStr("bt"); }
void setLastBtDevice(const String& name) { putStr("bt", name); }

slotlogic::LoopMode loopMode() {
  Preferences p;
  uint8_t v = 0;
  if (p.begin(kNs, true)) {
    v = p.getUChar("loop", 0);
    p.end();
  }
  return v == 1 ? slotlogic::LoopMode::AllSlots : slotlogic::LoopMode::RepeatOne;
}

void setLoopMode(slotlogic::LoopMode mode) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar("loop", mode == slotlogic::LoopMode::AllSlots ? 1 : 0);
  p.end();
}

uint8_t volume() {
  Preferences p;
  uint8_t v = 50;
  if (p.begin(kNs, true)) {
    v = p.getUChar("vol", 50);
    p.end();
  }
  return v > 100 ? 100 : v;
}

void setVolume(uint8_t vol) {
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar("vol", vol > 100 ? 100 : vol);
  p.end();
}

bool programFlag() {
  Preferences p;
  bool v = false;
  if (p.begin(kNs, true)) {
    v = p.getUChar("prog", 0) != 0;
    p.end();
  }
  return v;
}

void setProgramFlag(bool on) {
  if (programFlag() == on) return;
  Preferences p;
  if (!p.begin(kNs, false)) return;
  p.putUChar("prog", on ? 1 : 0);
  p.end();
}

}  // namespace settings
