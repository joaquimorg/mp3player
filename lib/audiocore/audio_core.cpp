#include "audio_core.h"

#include <cstring>

namespace audiocore {

// ---------------------------------------------------------------- SpscRing

bool SpscRing::init(uint32_t* buf, uint32_t capacity) {
  if (!buf || capacity < 2 || (capacity & (capacity - 1)) != 0) return false;
  buf_ = buf;
  cap_ = capacity;
  mask_ = capacity - 1;
  w_.store(0);
  r_.store(0);
  flushAt_.store(0);
  flushReq_.store(0);
  flushAck_ = 0;
  return true;
}

uint32_t SpscRing::freeSpace() const {
  uint32_t w = w_.load(std::memory_order_relaxed);
  uint32_t r = r_.load(std::memory_order_acquire);
  return cap_ - (w - r);
}

uint32_t SpscRing::write(const uint32_t* src, uint32_t n) {
  if (!buf_) return 0;
  uint32_t space = freeSpace();
  if (n > space) n = space;
  uint32_t w = w_.load(std::memory_order_relaxed);
  uint32_t idx = w & mask_;
  uint32_t first = cap_ - idx;
  if (first > n) first = n;
  memcpy(buf_ + idx, src, first * sizeof(uint32_t));
  if (n > first) memcpy(buf_, src + first, (n - first) * sizeof(uint32_t));
  w_.store(w + n, std::memory_order_release);
  return n;
}

void SpscRing::requestFlush() {
  flushAt_.store(w_.load(std::memory_order_relaxed), std::memory_order_relaxed);
  flushReq_.fetch_add(1, std::memory_order_release);
}

void SpscRing::consumerSync() {
  uint32_t req = flushReq_.load(std::memory_order_acquire);
  if (req == flushAck_) return;
  flushAck_ = req;
  uint32_t at = flushAt_.load(std::memory_order_relaxed);
  uint32_t r = r_.load(std::memory_order_relaxed);
  // Never move backwards: the consumer may already have read past `at`.
  if ((int32_t)(at - r) > 0) r_.store(at, std::memory_order_release);
}

uint32_t SpscRing::available() {
  uint32_t w = w_.load(std::memory_order_acquire);
  uint32_t r = r_.load(std::memory_order_relaxed);
  return w - r;
}

uint32_t SpscRing::read(uint32_t* dst, uint32_t n) {
  if (!buf_) return 0;
  consumerSync();
  uint32_t avail = available();
  if (n > avail) n = avail;
  uint32_t r = r_.load(std::memory_order_relaxed);
  uint32_t idx = r & mask_;
  uint32_t first = cap_ - idx;
  if (first > n) first = n;
  memcpy(dst, buf_ + idx, first * sizeof(uint32_t));
  if (n > first) memcpy(dst + first, buf_, (n - first) * sizeof(uint32_t));
  r_.store(r + n, std::memory_order_release);
  return n;
}

// --------------------------------------------------------------- Resampler

void Resampler::reset(uint32_t inRate) {
  if (inRate == 0) inRate = kOutRate;
  inRate_ = inRate;
  step_ = (uint32_t)(((uint64_t)inRate << 16) / kOutRate);
  frac_ = 0;
  prevL_ = prevR_ = 0;
}

size_t Resampler::process(const int16_t* in, size_t inFrames, int channels,
                          uint32_t* out, size_t outCap, size_t* consumed) {
  const bool stereo = channels >= 2;
  const size_t stride = stereo ? 2 : 1;
  size_t i = 0, o = 0;

  if (inRate_ == kOutRate) {
    size_t n = inFrames < outCap ? inFrames : outCap;
    for (; o < n; ++o, ++i) {
      int16_t l = in[i * stride];
      int16_t r = stereo ? in[i * stride + 1] : l;
      out[o] = packFrame(l, r);
    }
    if (n > 0) {
      prevL_ = frameLeft(out[n - 1]);
      prevR_ = frameRight(out[n - 1]);
    }
    if (consumed) *consumed = i;
    return o;
  }

  const uint32_t kOne = 1u << 16;
  for (;;) {
    // Advance the input position while the phase passed the next sample.
    while (frac_ >= kOne) {
      if (i >= inFrames) goto done;
      frac_ -= kOne;
      prevL_ = in[i * stride];
      prevR_ = stereo ? in[i * stride + 1] : prevL_;
      ++i;
    }
    if (i >= inFrames || o >= outCap) break;
    {
      int32_t nl = in[i * stride];
      int32_t nr = stereo ? in[i * stride + 1] : nl;
      int32_t l = prevL_ + (int32_t)(((int64_t)(nl - prevL_) * frac_) >> 16);
      int32_t r = prevR_ + (int32_t)(((int64_t)(nr - prevR_) * frac_) >> 16);
      out[o++] = packFrame((int16_t)l, (int16_t)r);
      frac_ += step_;
    }
  }
done:
  if (consumed) *consumed = i;
  return o;
}

// ------------------------------------------------------------------ volume

void applyVolume(uint32_t* frames, size_t n, uint8_t volume) {
  if (volume >= 100) return;
  if (volume == 0) {
    memset(frames, 0, n * sizeof(uint32_t));
    return;
  }
  // gain in Q16: (v*v/10000) * 65536
  const int32_t gain = (int32_t)(((uint32_t)volume * volume * 65536u) / 10000u);
  for (size_t k = 0; k < n; ++k) {
    int32_t l = ((int32_t)frameLeft(frames[k]) * gain) >> 16;
    int32_t r = ((int32_t)frameRight(frames[k]) * gain) >> 16;
    frames[k] = packFrame((int16_t)l, (int16_t)r);
  }
}

// ------------------------------------------------------------ slot advance

int afterTrackEnd(int current, slotlogic::LoopMode mode, const bool hasFile[4],
                  bool producedAudio, int& badStreak) {
  if (producedAudio) {
    badStreak = 0;
  } else {
    ++badStreak;
    if (mode == slotlogic::LoopMode::RepeatOne) return 0;
    int files = 0;
    for (int k = 0; k < slotlogic::kNumSlots; ++k) files += hasFile[k] ? 1 : 0;
    if (badStreak >= files) return 0;
  }
  return slotlogic::nextSlot(current, mode, hasFile);
}

int resolvePlay(int slot, const bool hasFile[4]) {
  if (slot < 1 || slot > slotlogic::kNumSlots) return 0;
  return hasFile[slot - 1] ? slot : 0;
}

}  // namespace audiocore
