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
void startScan();                   // keeps scanning until stopScan()/connectTo()
void stopScan();
bool scanning();
std::vector<String> devices();      // audio-capable names seen since begin()
void connectTo(const String& name); // saved as settings::lastBtDevice() once CONNECTED
bool connected();
String connectedName();             // name of the current/target peer
void end();                         // full teardown (controller off) so Wi-Fi can start

struct Stats {
  uint32_t connects;     // times the link reached CONNECTED
  uint32_t disconnects;  // times it dropped to DISCONNECTED after a connect
  uint32_t audioStarts;  // ESP_A2D_AUDIO_STATE_STARTED events
  bool started;
};
Stats stats();

}  // namespace bt
