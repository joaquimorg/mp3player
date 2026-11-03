#define LILYGO_WATCH_2020_V1
#include <LilyGoWatch.h>
#include <LittleFS.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include "log.h"
#include "bt.h"
#include "config.h"
#include "netmode.h"
#include "player.h"
#include "seriallink.h"
#include "settings.h"
#include "storage.h"
#include "transfer.h"
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
//
// Hang protection: a switch that has not finished 25 s after the system task
// started it (worst cases measured in tasks 4/5: bt::end() ~8.5 s + player::end()
// ~5.5 s), or that has not even started 180 s after it was requested (it waits
// for an upload in progress to end), is logged and the watch restarts with the
// Programação flag cleared, i.e. into Reprodução, instead of showing
// "A mudar de modo…" forever.
namespace {

constexpr uint32_t kSwitchDeadlineMs = 25000;
constexpr uint32_t kStartDeadlineMs = 180000;

TTGOClass* watch = nullptr;

QueueHandle_t s_cmds = nullptr;
std::atomic<ui::Mode> s_mode{ui::Mode::Play};
std::atomic<bool> s_busy{true};
SemaphoreHandle_t s_ipLock = nullptr;
String s_ip;  // snapshot of netmode::ipText(), written by the system task
std::atomic<uint32_t> s_ipVersion{0};
std::atomic<uint32_t> s_requestAt{0};  // millis() of the pending request (0: none)
std::atomic<uint32_t> s_startAt{0};    // millis() the system task began it (0: not yet)
uint32_t s_switches = 0;

void heapLine(const char* tag) {
  DBG_PRINTF("[heap] %s: free %u (internal %u, min internal %u, largest %u, psram %u)\n", tag,
                (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void publishIp(const String& ip) {
  xSemaphoreTake(s_ipLock, portMAX_DELAY);
  if (ip != s_ip) {
    s_ip = ip;
    s_ipVersion.fetch_add(1);
  }
  xSemaphoreGive(s_ipLock);
}

uint32_t nonZero(uint32_t t) { return t ? t : 1; }

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

}  // namespace
#ifdef UI_DEBUG
volatile bool g_debugStallSwitch = false;  // "stall" console command: next switch hangs 40 s
#endif
namespace {

// Reprodução -> Programação: Bluetooth and player fully off before Wi-Fi starts.
void enterProgram() {
#ifdef UI_DEBUG
  if (g_debugStallSwitch) {
    g_debugStallSwitch = false;
    DBG_PRINTLN("[mode] debug: stalling this switch for 40 s");
    vTaskDelay(pdMS_TO_TICKS(40000));
  }
#endif
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
      s_startAt = nonZero(t0);
      const char* name = want == ui::Mode::Play ? "Reproducao" : "Programacao";
      DBG_PRINTF("[mode] switch #%u -> %s\n", (unsigned)++s_switches, name);
      heapLine("before switch");
      if (want == ui::Mode::Play) enterPlay();
      else enterProgram();
      s_mode = want;
      DBG_PRINTF("[mode] now %s (switch took %u ms)\n", name, (unsigned)(millis() - t0));
      heapLine("after switch");
      s_requestAt = 0;
      s_startAt = 0;
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
  // Claim busy atomically, like serialRequestMode: a request that lost the race is dropped.
  bool expected = false;
  if (!s_busy.compare_exchange_strong(expected, true)) return;
  // Busy is claimed first, then the serial PUT is checked (a PUT claims first, then checks busy).
  if (seriallink::putActive() || transfer::active()) {
    s_busy = false;
    return;
  }
  s_startAt = 0;
  s_requestAt = nonZero(millis());
  xQueueSend(s_cmds, &m, portMAX_DELAY);
}
// Serial MODO request. Same path as the UI (system task, busy flag), refused while a switch is
// pending (s_busy is claimed with a compare-exchange), a web upload or a transfer is running.
bool serialRequestMode(bool program) {
  if (transfer::active() || netmode::uploading()) return false;
  bool expected = false;
  if (!s_busy.compare_exchange_strong(expected, true)) return false;
  if (seriallink::putActive() || transfer::active()) {
    s_busy = false;
    return false;
  }
  s_startAt = 0;
  s_requestAt = nonZero(millis());
  ui::Mode m = program ? ui::Mode::Program : ui::Mode::Play;
  xQueueSend(s_cmds, &m, portMAX_DELAY);
  return true;
}
const char* serialModeName() { return s_mode.load() == ui::Mode::Program ? "PROGRAMACAO" : "REPRODUCAO"; }
String hostIp() {
  xSemaphoreTake(s_ipLock, portMAX_DELAY);
  String ip = s_ip;
  xSemaphoreGive(s_ipLock);
  return ip;
}
// netmode::uploading() is atomic and meant to be read while the system task is
// blocked inside netmode::loop() receiving the file.
uint32_t hostIpVersion() { return s_ipVersion.load(); }
bool hostUploading() { return s_mode.load() == ui::Mode::Program && !s_busy.load() && netmode::uploading(); }

}  // namespace

void setup() {
  // Before begin(). Serial PUT is stop-and-wait (one <= 4099-byte frame in flight); 8 KB absorbs a whole frame
  // at 921600 while the serial task is stalled in a LittleFS write.
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  // The core's default FIFO-full threshold above 57600 baud is 120 of 128 bytes: at 921600 that leaves only ~87 us
  // of interrupt latency (the UART ISR is not in IRAM) before the FIFO overflows. 64 leaves ~0.7 ms.
  Serial.setRxFIFOFull(64);
#ifndef UI_DEBUG
  // Serial belongs to the PC protocol: silence library logs (A2DP etc. log through log_printf/ets_printf).
  Serial.setDebugOutput(false);
  esp_log_level_set("*", ESP_LOG_NONE);
#endif
  delay(100);

  watch = TTGOClass::getWatch();
  watch->begin();
  watch->tft->fillScreen(TFT_BLACK);

  if (!storage::begin()) DBG_PRINTLN("LittleFS mount FAILED");
  else
    DBG_PRINTF("LittleFS used %u of %u bytes\n", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());

  esp_reset_reason_t why = esp_reset_reason();
  bool program = uilogic::bootIntoProgram(settings::programFlag(), why == ESP_RST_SW);
  DBG_PRINTF("[mode] boot: reset reason %d, program flag %d -> %s\n", (int)why, settings::programFlag(),
                program ? "Programacao" : "Reproducao");
  heapLine("boot");

  s_cmds = xQueueCreate(4, sizeof(ui::Mode));
  s_ipLock = xSemaphoreCreateMutex();
  s_busy = true;
  s_requestAt = nonZero(millis());
  ui::Mode first = program ? ui::Mode::Program : ui::Mode::Play;
  xQueueSend(s_cmds, &first, 0);
  // Priority 1 on core 1, like the Arduino loop task (UI): the two time-slice, so
  // a long web upload in netmode::loop() never freezes the screen. In Reprodução
  // the task just sleeps on its queue and core 1 is left to the MP3 decoder.
  xTaskCreatePinnedToCore(systemTask, "system", 10240, nullptr, 1, nullptr, 1);

  ui::Host host{hostMode, hostBusy, hostRequest, hostIp, hostIpVersion, hostUploading};
  ui::begin(watch, host);
  seriallink::begin({serialRequestMode, serialModeName, hostBusy, hostIp});
}

// Runs in the UI task: the system task may be the one that is stuck.
void checkSwitchDeadline() {
  if (!s_busy.load()) return;
  uint32_t now = millis(), start = s_startAt.load(), req = s_requestAt.load();
  bool late = start ? now - start > kSwitchDeadlineMs : (req && now - req > kStartDeadlineMs);
  if (!late) return;
  DBG_PRINTF("[mode] ERROR: mode switch %s for %u s; restarting into Reproducao\n",
                start ? "not finished" : "not started", (unsigned)((now - (start ? start : req)) / 1000));
  settings::setProgramFlag(false);
  delay(200);
  ESP.restart();
}

void loop() {
  ui::loop();
  checkSwitchDeadline();
  netmode::checkStall();
  delay(10);
}

#endif  // UNIT_TEST
