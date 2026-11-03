#pragma once
#include <Arduino.h>
#include <vector>

// Bluetooth A2DP source (ESP32-A2DP). The data callback pulls frames from
// player::pullFrames(). Library tasks run on core 0.
//
// Reconnect: after a link loss the library's 10 s heartbeat re-issues a connect
// to the last peer address (non-blocking). At begin() the address saved by the
// library in NVS is used if settings::lastBtDevice() is set; if no address is
// known yet, a scan looks for that name and connects when it is found.
namespace bt {

void begin();                       // idempotent
// Searching. Inquiry and name paging cause audio dropouts while streaming, so
// (btlogic::searchPolicy): when NOT connected a search is continuous (with one
// remote-name request per unnamed device); when connected "Procurar" runs ONE
// ~12.8 s inquiry window without name requests and stops by itself, and
// opening the screen does not search. A new connection stops any search.
void screenOpened();                // Bluetooth screen opened: new list, search if not connected
void startScan();                   // "Procurar": clears the list (keeps the connected peer)
void stopScan();                    // only ever stops
bool scanning();                    // a search is wanted (true between inquiry windows too)
std::vector<String> devices();      // audio devices of the current search (+ connected
                                    // peer); unnamed ones show as "aa:bb:cc:dd:ee:ff"
uint32_t devicesVersion();          // changes whenever devices() would return something new
void connectTo(const String& name); // saved as settings::lastBtDevice() once CONNECTED
bool connected();
String connectedName();             // live peer's name if connected, else the target name
void end();                         // full teardown (controller off) so Wi-Fi can start

struct Stats {
  uint32_t connects;     // times the link reached CONNECTED
  uint32_t disconnects;  // times it dropped to DISCONNECTED after a connect
  uint32_t audioStarts;  // ESP_A2D_AUDIO_STATE_STARTED events
  bool started;
  uint32_t nameRequests;  // remote-name requests sent since boot
};
Stats stats();

#ifdef UI_DEBUG
// Diagnostic ("btraw" console command): for `secs` seconds log EVERY inquiry
// result (address, COD decoded, RSSI, EIR/remote name, listed or not).
// Needs Reprodução (Bluetooth started). Returns false if Bluetooth is off.
bool debugRawScan(uint32_t secs);
#endif

}  // namespace bt
