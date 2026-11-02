#pragma once
// Pure audio helpers shared by the player (no Arduino / FreeRTOS dependency so
// they can be unit-tested anywhere). Frames are 16-bit stereo packed into a
// uint32_t exactly like ESP32-A2DP's Frame {int16_t channel1; int16_t channel2;}
// (little endian: low half = left).
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "slot_logic.h"

namespace audiocore {

constexpr uint32_t kOutRate = 44100;

inline uint32_t packFrame(int16_t l, int16_t r) {
  return (uint32_t)(uint16_t)l | ((uint32_t)(uint16_t)r << 16);
}
inline int16_t frameLeft(uint32_t f) { return (int16_t)(f & 0xFFFF); }
inline int16_t frameRight(uint32_t f) { return (int16_t)(f >> 16); }

// Single-producer / single-consumer ring of stereo frames. Lock-free: the
// producer only writes w_, the consumer only writes r_. Indices are free-running
// 32-bit counters (capacity must be a power of two).
//
// Flush: the producer calls requestFlush() to discard everything it has written
// so far (e.g. on a user track change). The consumer applies it at its next
// read()/consumerSync(), so data written after the request is never lost.
class SpscRing {
 public:
  // buf must hold `capacity` frames; capacity must be a power of two (>= 2).
  bool init(uint32_t* buf, uint32_t capacity);
  uint32_t capacity() const { return cap_; }

  // Producer side.
  uint32_t freeSpace() const;
  uint32_t write(const uint32_t* src, uint32_t n);  // writes min(n, freeSpace())
  void requestFlush();

  // Consumer side.
  void consumerSync();                       // applies a pending flush
  uint32_t available();                      // after consumerSync()
  uint32_t read(uint32_t* dst, uint32_t n);  // reads min(n, available())

 private:
  uint32_t* buf_ = nullptr;
  uint32_t cap_ = 0;
  uint32_t mask_ = 0;
  std::atomic<uint32_t> w_{0};
  std::atomic<uint32_t> r_{0};
  std::atomic<uint32_t> flushAt_{0};
  std::atomic<uint32_t> flushReq_{0};
  uint32_t flushAck_ = 0;  // consumer-only
};

// Converts interleaved 16-bit PCM (1 or 2 channels, any rate) to 44.1 kHz
// stereo frames. Mono is duplicated to both channels. Rates other than 44.1 kHz
// use linear interpolation (Q16 phase); 44.1 kHz is a straight copy.
// Input may be fed in arbitrary pieces; state carries across calls.
class Resampler {
 public:
  void reset(uint32_t inRate);
  uint32_t inRate() const { return inRate_; }
  // Returns frames written to out (<= outCap); *consumed = input frames used.
  size_t process(const int16_t* in, size_t inFrames, int channels, uint32_t* out,
                 size_t outCap, size_t* consumed);

 private:
  uint32_t inRate_ = kOutRate;
  uint32_t step_ = 1u << 16;
  uint32_t frac_ = 0;
  int16_t prevL_ = 0, prevR_ = 0;
};

// Scales frames in place. volume 0..100 (clamped), quadratic curve
// (gain = (v/100)^2) which sounds more even than linear on a 0..100 slider.
void applyVolume(uint32_t* frames, size_t n, uint8_t volume);

// Decision after a track finished (EOF, decode failure or open failure).
// producedAudio: the finished track produced at least one PCM frame.
// badStreak: consecutive tracks that produced nothing (updated in place).
// Returns the slot to play next, or 0 to stop and stay idle:
//  - every slot empty -> 0 (nextSlot)
//  - silent/broken track in RepeatOne -> 0 (would replay it forever)
//  - silent/broken tracks for every non-empty slot in a row -> 0
int afterTrackEnd(int current, slotlogic::LoopMode mode, const bool hasFile[4],
                  bool producedAudio, int& badStreak);

// Slot to start for a user play(slot) request: slot if it has a file, else 0.
int resolvePlay(int slot, const bool hasFile[4]);

}  // namespace audiocore
