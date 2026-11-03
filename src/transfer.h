#pragma once
#include <Arduino.h>

// State of the file transfer in progress (or the last one), shared between the
// task that receives (netmode, later USB) and the UI task that shows it.
// All functions are thread safe and cheap (a short critical section).
namespace transfer {

enum class Source : uint8_t { None, Wifi, Usb };
enum class State : uint8_t { Idle, Receiving, Done, Failed };

struct Info {
  State state;
  Source src;
  int slot;
  char name[49];
  uint32_t received;
  uint32_t total;    // announced size (estimate), 0 when unknown
  uint32_t startMs;  // millis() at begin()
  uint32_t endMs;    // millis() at done()/fail()
  char msg[96];      // failure message
  uint32_t version;  // incremented on every change
};

void begin(Source src, int slot, const char* name, uint32_t total);
void progress(uint32_t received);
void done();                  // only from Receiving
void fail(const char* msg);   // only from Receiving
Info snapshot();
bool active();                // state == Receiving

}  // namespace transfer
