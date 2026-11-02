#pragma once
// TEMPORARY Task 5 test harness (Wi-Fi + web server). Only compiled with
// -DTASK5_HARNESS (pio env "watch_harness5"); remove once the UI exists.
// Serial console at 115200, one command per line:
//   wifi <ssid> <pass>   store credentials in NVS (settings::setWifi) - never in source
//   wifi                 show the stored SSID (password is never printed)
//   net | netend | ip | heap | slots | rm <slot> | ls (files on flash) | reboot
// Boot with TASK5_AUTONET defined to call netmode::begin() automatically.
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "netmode.h"
#include "settings.h"
#include "storage.h"

namespace harness5 {

static String s_line;
static bool s_last = false;
static String s_lastIp;

static void heapLine(const char* tag) {
  Serial.printf("[heap] %s int=%u intMin=%u largest=%u psram=%u\n", tag,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void printSlots() {
  for (int s = 1; s <= 4; ++s)
    Serial.printf("[slot] %d %s %u bytes name '%s'\n", s, storage::hasFile(s) ? "file" : "empty",
                  (unsigned)storage::slotSize(s), storage::hasFile(s) ? storage::slotName(s).c_str() : "");
  Serial.printf("[slot] free %u\n", (unsigned)storage::freeBytes());
}

static void listFiles() {
  fs::File root = LittleFS.open("/");
  for (fs::File f = root.openNextFile(); f; f = root.openNextFile())
    Serial.printf("[fs] %s %u\n", f.name(), (unsigned)f.size());
}

static void command(String c) {
  c.trim();
  if (c.isEmpty()) return;
  int sp = c.indexOf(' ');
  String cmd = sp < 0 ? c : c.substring(0, sp);
  String arg = sp < 0 ? String() : c.substring(sp + 1);
  arg.trim();
  // Do not echo the line: it may contain a password.
  if (cmd == "wifi") {
    if (arg.isEmpty()) {
      Serial.printf("[wifi] stored ssid '%s', password %s\n", settings::wifiSsid().c_str(),
                    settings::wifiPass().isEmpty() ? "empty" : "set");
    } else {
      int s2 = arg.indexOf(' ');
      String ssid = s2 < 0 ? arg : arg.substring(0, s2);
      String pass = s2 < 0 ? String() : arg.substring(s2 + 1);
      settings::setWifi(ssid, pass);
      Serial.printf("[wifi] stored ssid '%s', password %s\n", ssid.c_str(), pass.isEmpty() ? "empty" : "set");
    }
  } else if (cmd == "net") {
    heapLine("before netmode::begin");
    netmode::begin();
  } else if (cmd == "netend") {
    netmode::end();
    heapLine("after netmode::end");
  } else if (cmd == "ip") Serial.printf("[net] ip '%s' ap=%d connecting=%d\n", netmode::ipText().c_str(),
                                         netmode::inSetupAp(), netmode::connecting());
  else if (cmd == "heap") heapLine("now");
  else if (cmd == "slots") printSlots();
  else if (cmd == "rm") Serial.printf("[slot] rm %d -> %d\n", (int)arg.toInt(), storage::remove(arg.toInt()));
  else if (cmd == "ls") listFiles();
  else if (cmd == "reboot") ESP.restart();
  else Serial.println("? wifi <ssid> <pass> | net | netend | ip | heap | slots | rm <n> | ls | reboot");
}

inline void setup() {
  heapLine("boot");
  printSlots();
#ifdef TASK5_AUTONET
  netmode::begin();
#endif
  Serial.println("[harness5] ready (commands: wifi, net, netend, ip, heap, slots, rm, ls, reboot)");
}

inline void loop() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (s_line.length()) command(s_line);
      s_line = "";
    } else if (s_line.length() < 160) {
      s_line += ch;
    }
  }
  uint32_t t0 = millis();
  netmode::loop();
  uint32_t dt = millis() - t0;
  if (dt > 300) Serial.printf("[net] loop() blocked %u ms (t=%u)\n", (unsigned)dt, (unsigned)millis());
  String ip = netmode::ipText();
  if (ip != s_lastIp) {
    s_lastIp = ip;
    Serial.printf("[net] ip text now '%s'\n", ip.c_str());
  }
  delay(1);
}

}  // namespace harness5
