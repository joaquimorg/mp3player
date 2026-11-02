#pragma once
// TEMPORARY Task 4 test harness (Bluetooth + player). Only compiled with
// -DTASK4_HARNESS (pio env "watch_harness"); remove once the UI exists.
// Serial console at 115200, one command per line, "help" lists them.
// Nothing here runs a destructive operation on its own: a slot is only written
// by an explicit "up" command from tools/watch_serial.py, and "up" refuses a
// slot that already has a file (use "upf" to overwrite on purpose); "rm <slot>"
// deletes a slot only when typed explicitly.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include "bt.h"
#include "player.h"
#include "settings.h"
#include "storage.h"

// Set to the exact name of a speaker/headphones in pairing mode to run the
// brief's boot sequence: scan 10 s -> log names -> connect -> play slot 1.
#ifndef TEST_BT_NAME
#define TEST_BT_NAME ""
#endif

namespace harness {

static String s_line;
static uint32_t s_lastStat = 0;
static player::Stats s_prev{};
static TaskHandle_t s_sinkTask = nullptr;
static volatile bool s_sinkRun = false;
static uint32_t s_demoAt = 0;  // millis() when the demo scan ends (0 = no demo)
static String s_demoName;
static bool s_lastConn = false;

static uint32_t heapInt() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
static uint32_t heapIntMin() { return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }
static uint32_t heapPsram() { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }
static void heapLine(const char* tag) {
  Serial.printf("[heap] %s int=%u intMin=%u largest=%u psram=%u\n", tag, heapInt(), heapIntMin(),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT), heapPsram());
}

// Fake A2DP consumer: pulls 441 frames every 10 ms (= 44.1 kHz) on core 0.
static void sinkTask(void*) {
  static Frame buf[441];
  TickType_t last = xTaskGetTickCount();
  while (s_sinkRun) {
    player::pullFrames(buf, 441);
    vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
  }
  s_sinkTask = nullptr;
  vTaskDelete(nullptr);
}

static void printSlots() {
  for (int s = 1; s <= 4; ++s)
    Serial.printf("[slot] %d %s %u bytes\n", s, storage::hasFile(s) ? "file" : "empty",
                  (unsigned)storage::slotSize(s));
}

// "up <slot> <size>": PC sends the file in 1024-byte blocks, waiting for "K"
// after each one. Written through storage::SlotWriter (atomic commit).
static void upload(int slot, size_t size, bool force) {
  if (slot < 1 || slot > 4 || size == 0 || size > SLOT_MAX_BYTES) {
    Serial.println("UPLOAD ERR args");
    return;
  }
  if (storage::hasFile(slot) && !force) {
    Serial.println("UPLOAD ERR slot not empty (use upf to overwrite)");
    return;
  }
  storage::SlotWriter w(slot);
  static uint8_t blk[1024];
  size_t done = 0;
  Serial.println("READY");
  while (done < size) {
    size_t want = min((size_t)sizeof(blk), size - done);
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < want && millis() - t0 < 5000) {
      int n = Serial.read(blk + got, want - got);
      if (n > 0) got += n;
      else delay(1);
    }
    if (got < want || !w.write(blk, got)) {
      Serial.printf("UPLOAD ERR at %u\n", (unsigned)done);
      return;  // SlotWriter destructor aborts; the old slot content is kept
    }
    done += got;
    Serial.println("K");
  }
  Serial.println(w.commit() ? "UPLOAD OK" : "UPLOAD ERR commit");
}

static void wifiTest() {
  if (bt::stats().started) {
    Serial.println("[wifi] run btend first");
    return;
  }
  heapLine("before wifi");
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  Serial.printf("[wifi] STA started, scan found %d networks\n", n);
  WiFi.scanDelete();
  heapLine("wifi on");
  WiFi.mode(WIFI_OFF);
  delay(200);
  heapLine("wifi off");
}

static void help() {
  Serial.println(
      "cmds: slots | up <slot> <size> | upf <slot> <size> | rm <slot> | play <n> | pause | resume | next | prev"
      " | stop | vol <0-100> | loop one|all | sink on|off | btbegin | btend | scan | stopscan | devs"
      " | conn <name> | demo <name> | wifi | pend | pbegin | heap");
}

static void command(String c) {
  c.trim();
  if (c.isEmpty()) return;
  Serial.printf("> %s\n", c.c_str());
  int sp = c.indexOf(' ');
  String cmd = sp < 0 ? c : c.substring(0, sp);
  String arg = sp < 0 ? String() : c.substring(sp + 1);
  arg.trim();

  if (cmd == "help") help();
  else if (cmd == "slots") printSlots();
  else if (cmd == "up" || cmd == "upf") {
    int sp2 = arg.indexOf(' ');
    upload(arg.toInt(), (size_t)arg.substring(sp2 + 1).toInt(), cmd == "upf");
  } else if (cmd == "rm") Serial.printf("[slot] rm %d -> %d\n", (int)arg.toInt(), storage::remove(arg.toInt()));
  else if (cmd == "play") player::play(arg.toInt());
  else if (cmd == "pause") player::pause();
  else if (cmd == "resume") player::resume();
  else if (cmd == "next") player::next();
  else if (cmd == "prev") player::prev();
  else if (cmd == "stop") player::stop();
  else if (cmd == "vol") player::setVolume(arg.toInt());
  else if (cmd == "loop") settings::setLoopMode(arg == "all" ? slotlogic::LoopMode::AllSlots : slotlogic::LoopMode::RepeatOne);
  else if (cmd == "sink") {
    if (arg == "on") {
      if (bt::stats().started) Serial.println("[sink] refused: bluetooth is the consumer");
      else if (!s_sinkTask) {
        s_sinkRun = true;
        xTaskCreatePinnedToCore(sinkTask, "fakesink", 3072, nullptr, 4, &s_sinkTask, 0);
      }
    } else {
      s_sinkRun = false;
    }
  } else if (cmd == "btbegin") {
    if (s_sinkTask) Serial.println("[bt] refused: stop the fake sink first");
    else {
      heapLine("before bt::begin");
      bt::begin();
      delay(500);
      heapLine("after bt::begin");
    }
  } else if (cmd == "btend") {
    heapLine("before bt::end");
    uint32_t t0 = millis();
    bt::end();
    Serial.printf("[bt] end took %lu ms, controller status %d (0=idle)\n", millis() - t0,
                  (int)esp_bt_controller_get_status());
    heapLine("after bt::end");
  } else if (cmd == "scan") bt::startScan();
  else if (cmd == "stopscan") bt::stopScan();
  else if (cmd == "devs") {
    auto d = bt::devices();
    Serial.printf("[bt] %u devices:\n", (unsigned)d.size());
    for (auto& n : d) Serial.printf("[bt]   '%s'\n", n.c_str());
  } else if (cmd == "conn") bt::connectTo(arg);
  else if (cmd == "demo") {
    s_demoName = arg;
    if (!bt::stats().started) bt::begin();
    bt::startScan();
    s_demoAt = millis() + 10000;
    Serial.printf("[demo] scanning 10 s for '%s'\n", arg.c_str());
  } else if (cmd == "wifi") wifiTest();
  else if (cmd == "pend") {
    player::end();
    heapLine("after player::end");
  } else if (cmd == "pbegin") {
    player::begin();
    heapLine("after player::begin");
  } else if (cmd == "heap") heapLine("now");
  else Serial.println("? (help)");
}

static void stat() {
  player::Stats s = player::stats();
  bt::Stats b = bt::stats();
  float dt = 10.0f;
  Serial.printf(
      "[stat] t=%lus int=%u intMin=%u psram=%u | prod/s=%.0f pull/s=%.0f mp3fr/s=%.1f underrun=%u err=%u"
      " tracks=%u | %u Hz %u ch %u kbps ring %u/%u | slot=%d playing=%d paused=%d vol=%u | bt=%d conn=%d"
      " connects=%u drops=%u audioStarts=%u\n",
      millis() / 1000, heapInt(), heapIntMin(), heapPsram(), (s.framesProduced - s_prev.framesProduced) / dt,
      (s.framesPulled - s_prev.framesPulled) / dt, (s.mp3Frames - s_prev.mp3Frames) / dt, s.underrunFrames,
      s.decodeErrors, s.tracksStarted, s.sampleRate, s.channels, s.bitrate / 1000, s.ringFill, s.ringCapacity,
      player::currentSlot(), player::playing(), player::paused(), player::volume(), b.started, bt::connected(),
      b.connects, b.disconnects, b.audioStarts);
  s_prev = s;
}

inline void setup() {
  heapLine("boot");
  printSlots();
  player::begin();
  heapLine("after player::begin");
  if (strlen(TEST_BT_NAME) > 0) command(String("demo ") + TEST_BT_NAME);
  else Serial.println("[harness] TEST_BT_NAME empty: waiting for commands (help)");
  s_lastStat = millis();
}

inline void loop() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (s_line.length()) command(s_line);
      s_line = "";
    } else if (s_line.length() < 128) {
      s_line += ch;
    }
  }
  if (s_demoAt && (int32_t)(millis() - s_demoAt) >= 0) {
    s_demoAt = 0;
    command("devs");
    Serial.printf("[demo] connecting to '%s' and playing slot 1\n", s_demoName.c_str());
    bt::connectTo(s_demoName);
    player::play(1);
  }
  bool c = bt::connected();
  if (c != s_lastConn) {
    s_lastConn = c;
    Serial.printf("[bt] %s '%s'\n", c ? "CONNECTED" : "DISCONNECTED", bt::connectedName().c_str());
  }
  if (millis() - s_lastStat >= 10000) {
    s_lastStat += 10000;
    stat();
  }
  delay(5);
}

}  // namespace harness
