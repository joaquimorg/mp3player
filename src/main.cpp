#define LILYGO_WATCH_2020_V1
#include <LilyGoWatch.h>
#include <LittleFS.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include "bt.h"
#include "config.h"
#include "netmode.h"
#include "player.h"
#include "settings.h"
#include "storage.h"
#include "ui.h"
#include "ui_logic.h"

#ifndef UNIT_TEST

// Mode state machine.
//
// Reprodução (Play): Bluetooth A2DP source + MP3 player. Programação (Program):
// Wi-Fi + web page. Wi-Fi and Bluetooth are never on together. Every transition
// and netmode::loop() run in one "system" task, so:
//  - the slow teardowns (bt::end() up to ~8.5 s, player::end() up to ~5.5 s) and
//    a web upload in progress (netmode::loop() blocks while it streams) never
//    block the UI task (Arduino loop), which keeps drawing "A mudar de modo…";
//  - netmode::end() can never run while netmode::loop() is inside a request: a
//    switch requested during an upload waits for that upload to finish.
// The UI only calls player::/bt:: while !busy(), and busy is set by the UI task
// itself (requestMode) before the system task starts tearing anything down.
//
// Boot mode: settings::programFlag() is set on entering Programação and cleared
// on entering Reprodução. The watch boots into Programação only if the flag is
// set AND the reset was a software reset (ESP.restart() after POST /wifi in the
// setup access point); a power-cycle always boots into Reprodução.
namespace {

TTGOClass* watch = nullptr;

QueueHandle_t s_cmds = nullptr;
std::atomic<ui::Mode> s_mode{ui::Mode::Play};
std::atomic<bool> s_busy{true};
SemaphoreHandle_t s_ipLock = nullptr;
String s_ip;  // snapshot of netmode::ipText(), written by the system task
uint32_t s_switches = 0;

void heapLine(const char* tag) {
  Serial.printf("[heap] %s: free %u (internal %u, min internal %u, largest %u, psram %u)\n", tag,
                (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void publishIp(const String& ip) {
  xSemaphoreTake(s_ipLock, portMAX_DELAY);
  s_ip = ip;
  xSemaphoreGive(s_ipLock);
}

// Programação -> Reprodução: Wi-Fi fully off before Bluetooth starts.
void enterPlay() {
  // A request handled just before the switch (e.g. the 200 answer to an upload)
  // is still in lwip's send buffer: give it time to leave before Wi-Fi goes off.
  vTaskDelay(pdMS_TO_TICKS(300));
  netmode::end();
  publishIp("");
  settings::setProgramFlag(false);
  bt::begin();     // reconnects by itself to the saved device (address in NVS)
  player::begin();
}

// Reprodução -> Programação: Bluetooth and player fully off before Wi-Fi starts.
void enterProgram() {
  player::pause();
  player::stop();
  bt::end();
  player::end();
  settings::setProgramFlag(true);
  netmode::begin();
}

void systemTask(void*) {
  uint32_t lastIp = 0;
  for (;;) {
    bool program = s_mode.load() == ui::Mode::Program && !s_busy.load();
    ui::Mode want;
    if (xQueueReceive(s_cmds, &want, program ? pdMS_TO_TICKS(2) : portMAX_DELAY) == pdTRUE) {
      uint32_t t0 = millis();
      const char* name = want == ui::Mode::Play ? "Reproducao" : "Programacao";
      Serial.printf("[mode] switch #%u -> %s\n", (unsigned)++s_switches, name);
      heapLine("before switch");
      if (want == ui::Mode::Play) enterPlay();
      else enterProgram();
      s_mode = want;
      Serial.printf("[mode] now %s (switch took %u ms)\n", name, (unsigned)(millis() - t0));
      heapLine("after switch");
      s_busy = false;
      continue;
    }
    if (program) {
      netmode::loop();
      uint32_t now = millis();
      if (now - lastIp >= 250) {
        lastIp = now;
        publishIp(netmode::ipText());
      }
    }
  }
}

// ---- host functions for the UI ----------------------------------------------
ui::Mode hostMode() { return s_mode.load(); }
bool hostBusy() { return s_busy.load(); }
void hostRequest(ui::Mode m) {
  s_busy = true;
  xQueueSend(s_cmds, &m, portMAX_DELAY);
}
String hostIp() {
  xSemaphoreTake(s_ipLock, portMAX_DELAY);
  String ip = s_ip;
  xSemaphoreGive(s_ipLock);
  return ip;
}
// netmode::uploading() is atomic and meant to be read while the system task is
// blocked inside netmode::loop() receiving the file.
bool hostUploading() { return s_mode.load() == ui::Mode::Program && !s_busy.load() && netmode::uploading(); }

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  watch = TTGOClass::getWatch();
  watch->begin();
  watch->tft->fillScreen(TFT_BLACK);

  if (!storage::begin()) Serial.println("LittleFS mount FAILED");
  else
    Serial.printf("LittleFS used %u of %u bytes\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());

  esp_reset_reason_t why = esp_reset_reason();
  bool program = uilogic::bootIntoProgram(settings::programFlag(), why == ESP_RST_SW);
  Serial.printf("[mode] boot: reset reason %d, program flag %d -> %s\n", (int)why, settings::programFlag(),
                program ? "Programacao" : "Reproducao");
  heapLine("boot");

  s_cmds = xQueueCreate(4, sizeof(ui::Mode));
  s_ipLock = xSemaphoreCreateMutex();
  s_busy = true;
  ui::Mode first = program ? ui::Mode::Program : ui::Mode::Play;
  xQueueSend(s_cmds, &first, 0);
  // Priority 1 on core 1, like the Arduino loop task (UI): the two time-slice, so
  // a long web upload in netmode::loop() never freezes the screen. In Reprodução
  // the task just sleeps on its queue and core 1 is left to the MP3 decoder.
  xTaskCreatePinnedToCore(systemTask, "system", 10240, nullptr, 1, nullptr, 1);

  ui::Host host{hostMode, hostBusy, hostRequest, hostIp, hostUploading};
  ui::begin(watch, host);
}

void loop() {
  ui::loop();
  delay(10);
}

#endif  // UNIT_TEST
