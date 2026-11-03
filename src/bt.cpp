#include "log.h"
#include "bt.h"

#include <atomic>
#include <cstring>
#include <esp_a2dp_api.h>
#include <esp_avrc_api.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <freertos/semphr.h>

#include <array>
#include <string>

#include "BluetoothA2DPSource.h"
#include "bt_logic.h"
#include "player.h"
#include "settings.h"

namespace bt {
namespace {

constexpr uint8_t kInquirySeconds = 10;  // units of 1.28 s

SemaphoreHandle_t s_lock = nullptr;  // guards s_list, s_names, s_target, s_connAddr*
btlogic::DeviceList s_list;          // devices of the current search (+ the connected peer)
btlogic::NameRequests s_names;       // one remote-name attempt per address per search
uint8_t s_connAddr[ESP_BD_ADDR_LEN];  // address of the connected peer
bool s_connAddrValid = false;
std::atomic<bool> s_rnrBusy{false};              // a remote-name request is outstanding
std::atomic<uint32_t> s_nameRequests{0};         // remote-name requests sent (stats)
std::atomic<uint32_t> s_rnrAt{0};
String s_target;                      // name we want to connect to ("" = none)
std::atomic<bool> s_started{false};
// Search state. s_scanWanted = a search is wanted (what the UI shows as
// "Parar"); it stays true in the gaps between inquiry windows. Policy (see
// btlogic::searchPolicy): while connected a search is ONE window without name
// requests (inquiry/paging cause audio dropouts); otherwise continuous.
std::atomic<bool> s_scanWanted{false};
std::atomic<bool> s_singleShot{false};    // stop after the current inquiry window
std::atomic<bool> s_namesAllowed{true};   // remote-name requests allowed in this search
std::atomic<uint32_t> s_searchGen{0};     // bumped by every user search action / connect
std::atomic<bool> s_ending{false};        // teardown in progress: never restart anything
std::atomic<bool> s_connectPending{false};  // onName() already dispatched a connect
String s_savedName;                          // last value written to settings
String s_connectedName;                      // name of the live peer ("" if none)
std::atomic<uint32_t> s_connects{0}, s_disconnects{0}, s_audioStarts{0};
std::atomic<uint32_t> s_devVersion{0};  // bumped on every change of the device list
SemaphoreHandle_t s_parked = nullptr;
#ifdef UI_DEBUG
std::atomic<bool> s_raw{false};        // "btraw": log every inquiry result
std::atomic<uint32_t> s_rawUntil{0}, s_rawResults{0}, s_rawListed{0};
bool s_rawPrevWanted = false;
uint32_t s_rawGen = 0;
#endif

void onInquiryResult(esp_bt_gap_cb_param_t* param);
void onName(const uint8_t* addr, const std::string& name, int rssi, bool fromInquiry);
void afterInquiry();

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
  // Runs in the Bluedroid BTC task (core 0).
  void bt_app_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) override {
    if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
      if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
        discovery_active = true;
        log_i("bt: discovery started");
      } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        discovery_active = false;
        log_i("bt: discovery stopped");
        afterInquiry();
      }
      return;
    }
    if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
      auto& r = param->read_rmt_name;
      std::string name = r.stat == ESP_BT_STATUS_SUCCESS ? std::string((const char*)r.rmt_name) : std::string();
#ifdef UI_DEBUG
      if (s_raw.load())
        DBG_PRINTF("[btraw] remote name %s: %s '%s'\n", btlogic::addrText(r.bda).c_str(),
                      r.stat == ESP_BT_STATUS_SUCCESS ? "ok" : "failed", name.c_str());
#endif
      if (!name.empty()) onName(r.bda, name, -129, false);
      s_rnrBusy.store(false);
      afterInquiry();
      return;
    }
    BluetoothA2DPSource::bt_app_gap_callback(event, param);
  }

  // Replaces the library's filter (ESP32-A2DP 1.8.5), which: drops every device
  // whose result carries no EIR; takes the name only from EIR (ignores the
  // BDNAME property / a later remote-name request); and, when the EIR has no
  // name, reports the PREVIOUS device's name (s_peer_bdname is not cleared).
  void filter_inquiry_scan_result(esp_bt_gap_cb_param_t* param) override { onInquiryResult(param); }

  bool connecting() { return s_a2d_state == APP_AV_STATE_CONNECTING; }
  bool discoveryActive() { return discovery_active; }

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

  void startDiscovery() {  // BtAppT or BTC task
    if (!s_ending.load() && !discovery_active)
      esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, kInquirySeconds, 0);
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
// New search (Bluetooth screen opened / "Procurar"). Runs on BtAppT, like
// connectAddr() and the connection callbacks, so is_connected()/peer are a
// consistent snapshot when choosing which entry survives the clear.
void workNewSearch(uint16_t, void* param) {
  auto trigger = (btlogic::SearchTrigger)(*(uint8_t*)param);
  bool c = s_a2dp.is_connected();
  uint8_t peer[ESP_BD_ADDR_LEN];
  s_a2dp.peer(peer);
  {
    Lock l;
    s_list.clearExcept(c ? peer : nullptr);
    s_names.reset();
    s_devVersion.fetch_add(1);
  }
  btlogic::SearchPolicy pol = btlogic::searchPolicy(c, trigger);
  if (!pol.start) {
    s_scanWanted.store(false);
    return;
  }
  s_singleShot.store(pol.singleShot);
  s_namesAllowed.store(pol.nameRequests);
  if (s_scanWanted.load()) s_a2dp.startDiscovery();  // not if "Parar" came meanwhile
}
// The connected peer got its real name after connecting unnamed: save it.
void workSaveName(uint16_t, void*);
void workStopScan(uint16_t, void*) { s_a2dp.cancelDiscovery(); }
void workPark(uint16_t, void*) {
  xSemaphoreGive(s_parked);
  vTaskSuspend(nullptr);  // deleted by teardown()
}

void WatchSource::teardown() {
  s_ending.store(true);  // first: no callback may restart discovery / name requests
#ifdef UI_DEBUG
  s_raw.store(false);
#endif
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
           s_a2d_state == APP_AV_STATE_CONNECTING || s_rnrBusy.load();  // RNR pages too
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

// ---- inquiry results (BTC task) ---------------------------------------------
#ifdef UI_DEBUG
void rawLog(const uint8_t* a, uint32_t cod, int rssi, bool hasEir, const std::string& eirName,
            const std::string& bdName, bool listed) {
  if (!s_raw.load()) return;
  s_rawResults.fetch_add(1);
  if (listed) s_rawListed.fetch_add(1);
  // What ESP32-A2DP 1.8.5's own filter would have done (for comparison).
  bool lib = btlogic::codValid(cod) &&
             (cod & (btlogic::kSrvcRendering | btlogic::kSrvcAudio | btlogic::kSrvcTelephony)) && hasEir;
  std::string name = eirName.empty() ? bdName : eirName;
  std::string disp = btlogic::displayName(name, a);
  char buf[360];
  snprintf(buf, sizeof buf,
           "[btraw] %s rssi %d cod 0x%06x major %s(%u) minor %u%s%s srv [%s] eir %s eirname '%s' bdname '%s'"
           " -> %s%s%s (lib1.8.5: %s)",
           btlogic::addrText(a).c_str(), rssi, (unsigned)cod, btlogic::majorName(cod),
           (unsigned)btlogic::codMajor(cod), (unsigned)btlogic::codMinor(cod),
           *btlogic::avMinorName(cod) ? " " : "", btlogic::avMinorName(cod), btlogic::serviceNames(cod).c_str(),
           hasEir ? "yes" : "no", eirName.c_str(), bdName.c_str(), listed ? "listed as '" : cod == 0 ? "name-only result (no COD): only renames a listed device" : "NOT listed (COD)",
           listed ? disp.c_str() : "", listed ? "'" : "", lib ? "yes" : "no");
  DBG_PRINTLN(buf);
}
#endif

void onInquiryResult(esp_bt_gap_cb_param_t* param) {
  uint32_t cod = 0;
  int rssi = -129;
  uint8_t* eir = nullptr;
  std::string bdName, eirName;
  for (int i = 0; i < param->disc_res.num_prop; i++) {
    esp_bt_gap_dev_prop_t* p = param->disc_res.prop + i;
    switch (p->type) {
      case ESP_BT_GAP_DEV_PROP_COD: cod = *(uint32_t*)p->val; break;
      case ESP_BT_GAP_DEV_PROP_RSSI: rssi = *(int8_t*)p->val; break;
      case ESP_BT_GAP_DEV_PROP_EIR: eir = (uint8_t*)p->val; break;
      case ESP_BT_GAP_DEV_PROP_BDNAME:
        bdName.assign((const char*)p->val, strnlen((const char*)p->val, p->len));
        break;
      default: break;
    }
  }
  if (eir) {
    uint8_t len = 0;
    uint8_t* n = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
    if (!n) n = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
    if (n) eirName.assign((const char*)n, strnlen((const char*)n, len));
  }
  bool listed = btlogic::isListedCod(cod);
#ifdef UI_DEBUG
  rawLog(param->disc_res.bda, cod, rssi, eir != nullptr, eirName, bdName, listed);
#endif
  std::string name = eirName.empty() ? bdName : eirName;
  if (!listed) {
    // Bluedroid also reports name-only results (no COD, just BDNAME) for a
    // device it saw before: use them to name an already-listed device.
    bool known;
    {
      Lock l;
      known = s_list.findByAddr(param->disc_res.bda) != nullptr;
    }
    if (cod == 0 && known && !name.empty()) onName(param->disc_res.bda, name, rssi, false);
    return;
  }
  onName(param->disc_res.bda, name, rssi, true);
}

// Adds/updates a listed device (fromInquiry=false: name updates only, never
// inserts); connects if it is the requested target.
void onName(const uint8_t* addr, const std::string& name, int rssi, bool fromInquiry) {
  bool match = false, saveName = false;
  uint8_t a[ESP_BD_ADDR_LEN];
  memcpy(a, addr, ESP_BD_ADDR_LEN);
  {
    Lock l;
    if (!fromInquiry && !s_list.findByAddr(a)) return;
    if (s_list.upsert(a, name, rssi)) {
      s_devVersion.fetch_add(1);
      log_i("bt: listed '%s' rssi %d", btlogic::displayName(name, a).c_str(), rssi);
    }
    const btlogic::DeviceList::Entry* e = s_list.findByAddr(a);
    if (!e) return;  // list full
    if (fromInquiry && e->name.empty() && s_namesAllowed.load()) s_names.want(a);
    if (s_target.length() > 0) {
      std::string t = s_target.c_str();
      match = t == btlogic::displayName(e->name, e->addr) || t == e->name || t == btlogic::addrText(a);
    }
    saveName = s_connAddrValid && memcmp(s_connAddr, a, ESP_BD_ADDR_LEN) == 0 && !e->name.empty();
  }
  if (saveName) s_a2dp.dispatch(workSaveName, nullptr, 0);
  if (match && !s_a2dp.is_connected() && !s_connectPending.exchange(true)) {
    s_scanWanted.store(false);
    if (!s_a2dp.dispatch(workConnect, a, ESP_BD_ADDR_LEN)) s_connectPending.store(false);
  }
}

// After an inquiry window or a remote-name request ended (BTC task): ask the
// names of unnamed devices one at a time (once per address per search), then
// keep discovering while a continuous search is wanted.
void afterInquiry() {
#ifdef UI_DEBUG
  if (s_raw.load() && !s_rnrBusy.load()) {
    bool userActed = s_searchGen.load() != s_rawGen;
    if (userActed || (int32_t)(millis() - s_rawUntil.load()) >= 0) {
      s_raw.store(false);
      // Restore the previous search state only if nobody stopped/started a
      // search or connected meanwhile, and Bluetooth is not being torn down.
      if (!userActed && !s_ending.load()) s_scanWanted.store(s_rawPrevWanted);
      DBG_PRINTF("[btraw] done%s: %u results, %u listed, %u name requests so far; devices now:\n",
                    userActed ? " (search changed by the user)" : "", (unsigned)s_rawResults.load(),
                    (unsigned)s_rawListed.load(), (unsigned)s_nameRequests.load());
      for (auto& n : bt::devices()) DBG_PRINTF("[btraw]   '%s'\n", n.c_str());
    }
  }
#endif
  if (s_ending.load() || s_rnrBusy.load() || !s_scanWanted.load() || s_a2dp.connecting()) return;
  if (s_singleShot.load()) {  // connected: one inquiry window only
    s_singleShot.store(false);
    s_scanWanted.store(false);
    log_i("bt: single search window done");
    return;
  }
  if (s_namesAllowed.load() && !s_a2dp.is_connected()) {
    for (;;) {
      uint8_t a[ESP_BD_ADDR_LEN];
      {
        Lock l;
        if (!s_names.next(a)) break;  // marks it asked: never retried this search
        // A later inquiry result (EIR / name-only) may already have named it.
        const btlogic::DeviceList::Entry* e = s_list.findByAddr(a);
        if (!e || !e->name.empty()) continue;
      }
      s_rnrAt.store(millis());
      s_nameRequests.fetch_add(1);
      if (esp_bt_gap_read_remote_name(a) == ESP_OK) {
        s_rnrBusy.store(true);
        return;  // continues at ESP_BT_GAP_READ_REMOTE_NAME_EVT
      }
    }
  }
  s_a2dp.startDiscovery();
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
      const btlogic::DeviceList::Entry* e = s_list.findByAddr(addr);
      if (e) name = btlogic::displayName(e->name, e->addr).c_str();
      if (!name.length())  // boot auto-reconnect: no scan, the saved device
        name = s_a2dp.isSavedAddress(addr) ? s_savedName : s_target;
      s_connectedName = name;
      memcpy(s_connAddr, addr, ESP_BD_ADDR_LEN);
      s_connAddrValid = true;
      // Keep the connected peer visible in the list (it is usually no longer
      // discoverable once connected). An address-text name is stored as "no
      // name" so the real one can replace it when it arrives.
      if (!e) {
        std::string n = name.c_str();
        if (n == btlogic::addrText(addr)) n.clear();
        if (s_list.upsert(addr, n, -129)) s_devVersion.fetch_add(1);
      }
    }
    s_a2dp.saveAddress(addr);
    if (name.length() && name != s_savedName) {
      settings::setLastBtDevice(name);
      s_savedName = name;
    }
    // A link exists: stop any search (inquiry/paging would cause dropouts).
    if (s_scanWanted.load()) {
      s_scanWanted.store(false);
      s_singleShot.store(false);
      s_searchGen.fetch_add(1);
      s_a2dp.cancelDiscovery();
    }
  } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    {
      Lock l;
      s_connectedName = "";
      s_connAddrValid = false;
    }
    if (s_connects.load() > s_disconnects.load()) s_disconnects.fetch_add(1);
  }
  // A continuous search that was paused by a connect attempt resumes here.
  if (s_scanWanted.load() && !s_singleShot.load() && !s_a2dp.discoveryActive() && !s_rnrBusy.load() &&
      state == ESP_A2D_CONNECTION_STATE_DISCONNECTED)
    s_a2dp.startDiscovery();
}

void workSaveName(uint16_t, void*) {
  String name;
  {
    Lock l;
    if (!s_connAddrValid) return;
    const btlogic::DeviceList::Entry* e = s_list.findByAddr(s_connAddr);
    if (!e || e->name.empty()) return;
    name = e->name.c_str();
    s_connectedName = name;
  }
  if (name != s_savedName) {
    settings::setLastBtDevice(name);
    s_savedName = name;
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
    s_list.clearExcept(nullptr);
    s_names.reset();
    s_connAddrValid = false;
    s_devVersion.fetch_add(1);
  }
  s_rnrBusy.store(false);
  s_ending.store(false);
  s_singleShot.store(false);
  s_namesAllowed.store(true);
  s_a2dp.set_local_name("T-Watch MP3");
  s_a2dp.set_task_core(0);          // library default is core 1 (decode core)
  s_a2dp.set_event_stack_size(4096);
  s_a2dp.set_on_connection_state_changed(onConnState);
  s_a2dp.set_on_audio_state_changed(onAudioState);
  // With a saved device the library connects straight to the address it kept
  // in NVS at stack-up; without one it starts a discovery.
  s_a2dp.set_auto_reconnect(last.length() > 0);
  s_a2dp.start(dataCallback);  // loads the NVS address; stack-up runs async
  // Saved name but no address yet: keep scanning until onName() finds it.
  s_scanWanted.store(last.length() > 0 && !s_a2dp.hasAddress());
  s_started.store(true);
  log_i("bt: started (saved device '%s', saved address %s)", last.c_str(),
        s_a2dp.hasAddress() ? "yes" : "no");
}

namespace {
void newSearch(btlogic::SearchTrigger t, bool wantedNow) {
  s_searchGen.fetch_add(1);
  if (s_rnrBusy.load() && millis() - s_rnrAt.load() > 15000) s_rnrBusy.store(false);  // lost event
  s_singleShot.store(false);
  s_scanWanted.store(wantedNow);  // the UI shows "Parar" at once; corrected by workNewSearch
  uint8_t trig = (uint8_t)t;
  s_a2dp.dispatch(workNewSearch, &trig, 1);
}
}  // namespace

void startScan() {
  if (!s_started.load()) return;
  newSearch(btlogic::SearchTrigger::Button, true);
}

void screenOpened() {
  if (!s_started.load()) return;
  newSearch(btlogic::SearchTrigger::EnterScreen, !s_a2dp.is_connected());
}

void stopScan() {  // only ever stops (also in the gap between inquiry windows)
  if (!s_started.load()) return;
  s_searchGen.fetch_add(1);
  s_scanWanted.store(false);
  s_singleShot.store(false);
  s_a2dp.dispatch(workStopScan, nullptr, 0);
}

bool scanning() { return s_started.load() && s_scanWanted.load(); }

std::vector<String> devices() {
  std::vector<String> out;
  if (!s_lock) return out;
  Lock l;
  for (auto& n : s_list.names()) out.push_back(n.c_str());
  return out;
}

void connectTo(const String& name) {
  if (!s_started.load() || name.length() == 0) return;
  s_connectPending.store(false);
  s_searchGen.fetch_add(1);
  bool known = false;
  esp_bd_addr_t addr;
  {
    Lock l;
    s_target = name;
    const btlogic::DeviceList::Entry* e = s_list.findTarget(name.c_str());
    if (e) {
      memcpy(addr, e->addr, ESP_BD_ADDR_LEN);
      known = true;
    }
  }
  if (known) {
    s_scanWanted.store(false);
    s_singleShot.store(false);
    if (!s_a2dp.dispatch(workConnect, addr, ESP_BD_ADDR_LEN)) log_w("bt: connect request dropped");
  } else {
    // onName() connects when the name shows up.
    startScan();
  }
}

uint32_t devicesVersion() { return s_devVersion.load(); }

bool connected() { return s_started.load() && s_a2dp.is_connected(); }

String connectedName() {
  if (!s_lock) return String();
  Lock l;
  if (s_connAddrValid) {  // by address: follows a name that arrives after connecting
    const btlogic::DeviceList::Entry* e = s_list.findByAddr(s_connAddr);
    if (e) return btlogic::displayName(e->name, e->addr).c_str();
  }
  return s_connectedName.length() ? s_connectedName : s_target;
}

void end() {
  if (!s_started.load()) return;
  s_a2dp.teardown();
  s_started.store(false);
  s_connectPending.store(false);
  {
    Lock l;
    s_list.clearExcept(nullptr);
    s_names.reset();
    s_connAddrValid = false;
    s_devVersion.fetch_add(1);
    s_connectedName = "";
  }
  s_rnrBusy.store(false);
#ifdef UI_DEBUG
  s_raw.store(false);
#endif
  log_i("bt: stopped, controller status %d", esp_bt_controller_get_status());
}

#ifdef UI_DEBUG
bool debugRawScan(uint32_t secs) {
  if (!s_started.load()) return false;
  if (secs < 5) secs = 5;
  if (secs > 120) secs = 120;
  s_rawPrevWanted = s_scanWanted.load();
  s_rawGen = s_searchGen.load();
  s_singleShot.store(false);
  s_namesAllowed.store(!s_a2dp.is_connected());
  s_rawResults.store(0);
  s_rawListed.store(0);
  s_rawUntil.store(millis() + secs * 1000);
  s_raw.store(true);
  s_scanWanted.store(true);
  if (s_rnrBusy.load() && millis() - s_rnrAt.load() > 15000) s_rnrBusy.store(false);
  s_a2dp.dispatch(workStartScan, nullptr, 0);  // no-op if discovery is already running
  return true;
}
#endif

Stats stats() {
  return Stats{s_connects.load(), s_disconnects.load(), s_audioStarts.load(), s_started.load(),
               s_nameRequests.load()};
}

}  // namespace bt
