#pragma once
#include <Arduino.h>
#include "SoundData.h"  // ESP32-A2DP: struct Frame {int16_t channel1, channel2;}

// MP3 player: a decode task pinned to core 1 reads the slot file in blocks,
// decodes it with libhelix, converts to 44.1 kHz 16-bit stereo and fills a
// lock-free ring buffer. pullFrames() (called from the A2DP data callback on
// core 0) drains the ring and never blocks.
//
// Commands (play/next/prev/stop) are queued to the decode task and take effect
// within a few ms; currentSlot()/playing() reflect the task's state.
namespace player {

void begin();                // idempotent; reads settings::volume()
void play(int slot);         // empty/invalid slot: logged and ignored
void pause();
void resume();
void next();                 // next non-empty slot (cyclic), regardless of loop mode
void prev();                 // previous non-empty slot (cyclic)
void stop();                 // close the file and go idle
void setVolume(uint8_t v);   // 0..100, applied immediately (not persisted)
uint8_t volume();
int currentSlot();           // 0 = none yet
bool playing();              // true only while playing (not paused, not idle)
bool paused();

// A2DP data callback. Always returns `count` (silence when idle/paused/underrun).
int32_t pullFrames(Frame* out, int32_t count);

void end();                  // stops the task and frees every buffer

struct Stats {
  uint32_t framesProduced;   // 44.1 kHz frames pushed into the ring
  uint32_t framesPulled;     // frames requested by the consumer
  uint32_t underrunFrames;   // silence inserted while playing (ring empty)
  uint32_t mp3Frames;        // MP3 frames decoded
  uint32_t decodeErrors;     // MP3Decode errors (skipped)
  uint32_t tracksStarted;
  uint32_t sampleRate;       // of the current file (Hz)
  uint32_t bitrate;          // of the last decoded MP3 frame (bit/s)
  uint8_t channels;
  uint32_t ringFill;         // frames buffered
  uint32_t ringCapacity;
};
Stats stats();

}  // namespace player
