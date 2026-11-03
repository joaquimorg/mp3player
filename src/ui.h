#pragma once
#include <Arduino.h>

class TTGOClass;

// Touch UI (TFT_eSPI drawing into a full-screen PSRAM sprite, Portuguese text).
// Runs entirely in the caller's task (Arduino loop): ui::loop() polls touch and
// the side button, redraws when what is shown changes and blanks the backlight
// after 15 s without touch (the first touch then only wakes the screen).
//
// The UI never switches modes itself: it asks the host (main.cpp), which does
// the slow teardown/start-up in its own task and reports busy() meanwhile. While
// busy() the UI shows "A mudar de modo…" and calls no player::/bt:: function.
namespace ui {

enum class Mode { Play, Program };

struct Host {
  Mode (*mode)();
  bool (*busy)();
  void (*requestMode)(Mode m);  // sets busy() before returning
  String (*ipText)();           // snapshot of netmode::ipText() (Programação)
  uint32_t (*ipVersion)();      // changes whenever ipText() changes (cheap to poll)
  bool (*uploading)();          // snapshot of netmode::uploading()
};

void begin(TTGOClass* watch, const Host& host);
void loop();

}  // namespace ui
