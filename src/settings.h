#pragma once
#include <Arduino.h>
#include "slot_logic.h"

// Persistent settings in NVS namespace "mp3".
namespace settings {

String wifiSsid();
String wifiPass();
void setWifi(const String& ssid, const String& pass);

String lastBtDevice();
void setLastBtDevice(const String& name);

slotlogic::LoopMode loopMode();
void setLoopMode(slotlogic::LoopMode mode);

uint8_t volume();             // 0..100, default 50
void setVolume(uint8_t vol);  // clamped to 100

}  // namespace settings
