#pragma once
#include <Arduino.h>

// Serial command link to the PC app (protocol in lib/serproto, spec "Protocolo serie").
// Runs in its own FreeRTOS task (core 0, priority 1): never blocks the UI or the player.
// Owns Serial.read(); in the UI_DEBUG builds the lines that are not protocol commands are
// forwarded to the debug console (ui::debugCommand). Nothing is ever echoed.
namespace seriallink {

struct Host {
  bool (*requestMode)(bool program);  // false when busy (switch pending, upload, transfer); true = accepted
  const char* (*modeName)();          // "REPRODUCAO" or "PROGRAMACAO"
  bool (*busy)();
  String (*ipText)();                 // snapshot of the IP text (same as the UI shows)
};

// Call after Serial.begin(). Starts the task.
void begin(const Host& host);

}  // namespace seriallink
