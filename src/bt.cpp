#include "bt.h"

#include <atomic>
#include <cstring>
#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <freertos/semphr.h>

#include "BluetoothA2DPSource.h"
#include "player.h"
#include "settings.h"

namespace bt {
namespace {

constexpr uint8_t kInquirySeconds = 10;  // units of 1.28 s

struct Device {
  String name;
  esp_bd_addr_t addr;
  int rssi;
};

SemaphoreHandle_t s_lock = nullptr;  // guards s_devices and s_target
std::vector<Device> s_devices;
String s_target;                      // name we want to connect to ("" = none)
std::atomic<bool> s_started{false};
std::atomic<bool> s_scanWanted{false};
std::atomic<bool> s_connectPending{false};  // onSsid already dispatched a connect
String s_savedName;                          // last value written to settings
String s_connectedName;                      // name of the live peer ("" if none)
std::atomic<uint32_t> s_connects{0}, s_disconnects{0}, s_audioStarts{0};
SemaphoreHandle_t s_parked = nullptr;

struct Lock {
  Lock() { xSemaphoreTake(s_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(s_lock); }
};

// Subclass to fix library behaviour that conflicts with our needs (members are
// protected; the library's C trampolines dispatch through virtuals).
class WatchSource : public BluetoothA2DPSource {
 public:
  // Library restarts discovery forever when it stops without a match; we only
  // do that while a scan was requested (so stopScan() sticks).
  void bt_app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) override {
    if (event != ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
      BluetoothA2DPSource::bt_app_gap_callback(event, param);
      return;
    }
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
      discovery_active = true;
      log_i("bt: discovery started");
    } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
      discovery_active = false;
      log_i("bt: discovery stopped");
      if (s_scanWanted.load() && !is_connected() && s_a2d_state != APP_AV_STATE_CONNECTING) {
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquirySeconds, 0);
      }
    }
  }

  // Runs on the library task (BtAppT), serialised with its state machine.
  void connectAddr(const uint8_t* addr) {
    if (discovery_active) esp_bt_gap_cancel_discovery();
    // One inquiry reports the same device many times: ignore repeats while a
    // connect to that address is already in flight.
    if (s_a2d_state == APP_AV_STATE_CONNECTING && memcmp(addr, peer_bd_addr, ESP_BD_ADDR_LEN) == 0) return;
    if (is_connected()) {
      if (memcmp(addr, peer_bd_addr, ESP_BD_ADDR_LEN) == 0) return;
      // Switch device: drop the current link; the heartbeat in UNCONNECTED
      // state then connects to the new peer_bd_addr.
      esp_bd_addr_t old;
      memcpy(old, peer_bd_addr, ESP_BD_ADDR_LEN);
      memcpy(peer_bd_addr, addr, ESP_BD_ADDR_LEN);
      esp_a2d_source_disconnect(old);
      return;
    }
    // The address is NOT persisted here: only onConnState(CONNECTED) saves
    // address + name together, so a failed attempt never changes them.
    memcpy(peer_bd_addr, addr, ESP_BD_ADDR_LEN);
    s_a2d_state = APP_AV_STATE_CONNECTING;
    s_connecting_heatbeat_count = 0;
    connect_to(peer_bd_addr);
  }

  void startDiscovery() {
    if (!discovery_active) esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquirySeconds, 0);
  }
  void cancelDiscovery() {
    if (discovery_active) esp_bt_gap_cancel_discovery();
  }

  bool dispatch(bt_app_cb_t cb, const void* param, int len) {
    return bt_app_work_dispatch(cb, 0, const_cast<void*>(param), len, nullptr);
  }

  bool hasAddress() { return has_last_connection(); }
  void peer(uint8_t* out) { memcpy(out, peer_bd_addr, ESP_BD_ADDR_LEN); }
  bool isSavedAddress(const uint8_t* a) { return memcmp(a, last_connection, ESP_BD_ADDR_LEN) == 0; }
  // Writes the NVS address (library namespace "connected_bda"); no-op if equal.
  void saveAddress(uint8_t* a) { set_last_connection(a); }

  // Full teardown. The library's end() is not usable for a source in 1.8.5:
  // it clears the address before disconnecting, calls the *sink* disconnect,
  // never deletes the heartbeat timer / app task and never deinits a2dp.
  void teardown();
};

WatchSource s_a2dp;

// ---- work items executed on BtAppT ---------------------------------------
void workConnect(uint16_t, void* param) { s_a2dp.connectAddr((const uint8_t*)param); }
void workStartScan(uint16_t, void*) { s_a2dp.startDiscovery(); }
void workStopScan(uint16_t, void*) { s_a2dp.cancelDiscovery(); }
void workPark(uint16_t, void*) {
  xSemaphoreGive(s_parked);
  vTaskSuspend(nullptr);  // deleted by teardown()
}

void WatchSource::teardown() {
  s_scanWanted.store(false);
  if (s_tmr) {
    xTimerStop(s_tmr, pdMS_TO_TICKS(100));
    xTimerDelete(s_tmr, pdMS_TO_TICKS(100));
    s_tmr = nullptr;
  }
  if (discovery_active) esp_bt_gap_cancel_discovery();

  // Deinit with a connect attempt still in flight (paging an absent speaker)
  // crashes: the late A2DP event is delivered to freed state in btc_av.c
  // (LoadProhibited in btc_a2dp_cb_handler, reproduced on the watch). So wait
  // for the link to be fully DISCONNECTED first. The heartbeat timer is
  // already gone, so no new attempt starts; a page attempt ends after ~5.1 s.
  vTaskDelay(pdMS_TO_TICKS(300));  // a connect queued just before the timer died
  auto busy = [this] {
    return connection_state != ESP_A2D_CONNECTION_STATE_DISCONNECTED ||
           s_a2d_state == APP_AV_STATE_CONNECTING;
  };
  if (busy()) {
    uint32_t t0 = millis();
    if (connection_state == ESP_A2D_CONNECTION_STATE_CONNECTED) esp_a2d_source_disconnect(peer_bd_addr);
    for (int i = 0; i < 80 && busy(); ++i) vTaskDelay(pdMS_TO_TICKS(100));
    if (busy())
      log_e("bt: link still busy (conn %d, sm %d) after 8 s, tearing down anyway", connection_state,
            s_a2d_state);
    else
      log_i("bt: link idle after %lu ms", millis() - t0);
    vTaskDelay(pdMS_TO_TICKS(200));  // let BTC deliver the final events
  }
  s_a2d_state = APP_AV_STATE_IDLE;
  s_media_state = 0;

  esp_err_t e;
  if ((e = esp_avrc_ct_deinit()) != ESP_OK) log_w("bt: avrc_ct_deinit %d", e);
  if ((e = esp_a2d_source_deinit()) != ESP_OK) log_w("bt: a2d_source_deinit %d", e);
  if ((e = esp_bluedroid_disable()) != ESP_OK) log_w("bt: bluedroid_disable %d", e);
  if ((e = esp_bluedroid_deinit()) != ESP_OK) log_w("bt: bluedroid_deinit %d", e);
  if (!btStop()) log_w("bt: btStop failed (controller status %d)", esp_bt_controller_get_status());

  // Park the library task at a safe point, drain its queue, then delete both.
  if (s_bt_app_task_handle && s_bt_app_task_queue) {
    if (!s_parked) s_parked = xSemaphoreCreateBinary();
    bool parked = dispatch(workPark, nullptr, 0) &&
                  xSemaphoreTake(s_parked, pdMS_TO_TICKS(1000)) == pdTRUE;
    if (!parked) log_w("bt: app task did not park, deleting anyway");
    bt_app_msg_t msg;
    while (xQueueReceive(s_bt_app_task_queue, &msg, 0) == pdTRUE) free(msg.param);
  }
  bt_app_task_shut_down();
  connection_state = ESP_A2D_CONNECTION_STATE_DISCONNECTED;
  discovery_active = false;
}

// ---- library callbacks ----------------------------------------------------
bool onSsid(const char* name, esp_bd_addr_t addr, int rssi) {
  bool match = false;
  {
    Lock l;
    bool found = false;
    for (auto& d : s_devices) {
      if (memcmp(d.addr, addr, ESP_BD_ADDR_LEN) == 0) {
        d.name = name;
        d.rssi = rssi;
        found = true;
        break;
      }
    }
    if (!found && s_devices.size() < 32) {
      Device d;
      d.name = name;
      memcpy(d.addr, addr, ESP_BD_ADDR_LEN);
      d.rssi = rssi;
      s_devices.push_back(d);
      log_i("bt: found '%s' rssi %d", name, rssi);
    }
    match = s_target.length() > 0 && s_target == name;
  }
  if (match && !s_a2dp.is_connected() && !s_connectPending.exchange(true)) {
    s_scanWanted.store(false);
    if (!s_a2dp.dispatch(workConnect, addr, ESP_BD_ADDR_LEN)) s_connectPending.store(false);
  }
  return false;  // never let the library hijack its state machine
}

// Runs on the library task (BtAppT, 4 KB stack).
void onConnState(esp_a2d_connection_state_t state, void*) {
  s_connectPending.store(false);
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
    s_connects.fetch_add(1);
    // Persist address + name together, and only once a link really exists, so
    // a mistyped/absent/unreachable device never replaces the saved one.
    uint8_t addr[ESP_BD_ADDR_LEN];
    s_a2dp.peer(addr);
    String name;
    {
      Lock l;
      for (auto& d : s_devices)
        if (memcmp(d.addr, addr, ESP_BD_ADDR_LEN) == 0) name = d.name;
      if (!name.length())  // boot auto-reconnect: no scan, the saved device
        name = s_a2dp.isSavedAddress(addr) ? s_savedName : s_target;
      s_connectedName = name;
    }
    s_a2dp.saveAddress(addr);
    if (name.length() && name != s_savedName) {
      settings::setLastBtDevice(name);
      s_savedName = name;
    }
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    {
      Lock l;
      s_connectedName = "";
    }
    if (s_connects.load() > s_disconnects.load()) s_disconnects.fetch_add(1);
  }
}

void onAudioState(esp_a2d_audio_state_t state, void*) {
  if (state == ESP_A2D_AUDIO_STATE_STARTED) s_audioStarts.fetch_add(1);
}

int32_t dataCallback(Frame* frames, int32_t count) { return player::pullFrames(frames, count); }

}  // namespace

void begin() {
  if (s_started.load()) return;
  if (!s_lock) s_lock = xSemaphoreCreateMutex();

  String last = settings::lastBtDevice();
  s_savedName = last;
  s_connectPending.store(false);
  {
    Lock l;
    s_target = last;
    s_connectedName = "";
    s_devices.clear();
  }
  s_a2dp.set_local_name("T-Watch MP3");
  s_a2dp.set_task_core(0);          // library default is core 1 (decode core)
  s_a2dp.set_event_stack_size(4096);
  s_a2dp.set_ssid_callback(onSsid);
  s_a2dp.set_on_connection_state_changed(onConnState);
  s_a2dp.set_on_audio_state_changed(onAudioState);
  // With a saved device the library connects straight to the address it kept
  // in NVS at stack-up; without one it starts a discovery.
  s_a2dp.set_auto_reconnect(last.length() > 0);
  s_a2dp.start(dataCallback);  // loads the NVS address; stack-up runs async
  // Saved name but no address yet: keep scanning until onSsid() finds it.
  s_scanWanted.store(last.length() > 0 && !s_a2dp.hasAddress());
  s_started.store(true);
  log_i("bt: started (saved device '%s', saved address %s)", last.c_str(),
        s_a2dp.hasAddress() ? "yes" : "no");
}

void startScan() {
  if (!s_started.load()) return;
  s_scanWanted.store(true);
  s_a2dp.dispatch(workStartScan, nullptr, 0);
}

void stopScan() {
  if (!s_started.load()) return;
  s_scanWanted.store(false);
  s_a2dp.dispatch(workStopScan, nullptr, 0);
}

bool scanning() { return s_started.load() && s_a2dp.is_discovery_active(); }

std::vector<String> devices() {
  std::vector<String> out;
  if (!s_lock) return out;
  Lock l;
  for (auto& d : s_devices) out.push_back(d.name);
  return out;
}

void connectTo(const String& name) {
  if (!s_started.load() || name.length() == 0) return;
  s_connectPending.store(false);
  bool known = false;
  esp_bd_addr_t addr;
  {
    Lock l;
    s_target = name;
    for (auto& d : s_devices) {
      if (d.name == name) {
        memcpy(addr, d.addr, ESP_BD_ADDR_LEN);
        known = true;
        break;
      }
    }
  }
  if (known) {
    s_scanWanted.store(false);
    if (!s_a2dp.dispatch(workConnect, addr, ESP_BD_ADDR_LEN)) log_w("bt: connect request dropped");
  } else {
    // onSsid() connects when the name shows up.
    startScan();
  }
}

bool connected() { return s_started.load() && s_a2dp.is_connected(); }

String connectedName() {
  if (!s_lock) return String();
  Lock l;
  return s_connectedName.length() ? s_connectedName : s_target;
}

void end() {
  if (!s_started.load()) return;
  s_a2dp.teardown();
  s_started.store(false);
  s_connectPending.store(false);
  {
    Lock l;
    s_devices.clear();
    s_connectedName = "";
  }
  log_i("bt: stopped, controller status %d", esp_bt_controller_get_status());
}

Stats stats() {
  return Stats{s_connects.load(), s_disconnects.load(), s_audioStarts.load(), s_started.load()};
}

}  // namespace bt
