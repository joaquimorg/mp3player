#pragma once
#include <Arduino.h>

// Programação mode networking: Wi-Fi station + web server (page and upload API).
// If there are no saved credentials, or the connection is not up after
// netlogic::kConnectAttempts attempts of 15 s, a setup access point "TWATCH-SETUP" is started instead, serving a form
// (POST /wifi) that stores the credentials and reboots.
// Bluetooth/player must already be stopped (bt::end(), player::end()).
namespace netmode {

void begin();   // non-blocking: starts connecting; call loop() regularly
void loop();    // drives the connection state machine and serves HTTP
void end();     // stops the server and turns Wi-Fi off

// STA IP when connected, "192.168.4.1 (TWATCH-SETUP)" in setup AP mode,
// "A ligar..." while connecting, "" when not started.
String ipText();
bool inSetupAp();
bool connecting();   // true while joining the saved network (up to 3 x 15 s)
bool uploading();    // a file upload is being received; safe to call from another task
                     // while loop() is busy streaming it
// Call regularly from another task (the UI task) while loop() may be blocked in an
// upload: drops an upload whose connection stays open but has delivered no data for
// netlogic::kUploadStallMs, so the single-threaded server is never blocked forever.
void checkStall();
#ifdef NET_DIAG
void debugDropUpload();
#endif

}  // namespace netmode
