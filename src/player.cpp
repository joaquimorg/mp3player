#include "player.h"

#include <FS.h>
#include <atomic>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "audio_core.h"
#include "libhelix-mp3/mp3dec.h"
#include "settings.h"
#include "slot_logic.h"
#include "storage.h"

namespace player {
namespace {

// ---- tuning --------------------------------------------------------------
constexpr uint32_t kRingFrames = 8192;        // 32 KB, ~186 ms at 44.1 kHz
constexpr size_t kInBufBytes = 4096;          // compressed input block
constexpr size_t kPcmShorts = 1152 * 2;       // one MP3 frame, stereo
constexpr size_t kOutChunk = 512;             // frames per ring write
constexpr uint32_t kTaskStack = 8192;
constexpr UBaseType_t kTaskPrio = 3;
constexpr BaseType_t kTaskCore = 1;
constexpr int kMaxConsecutiveErrors = 64;     // then the file is treated as bad

enum class State : uint8_t { Idle, Playing, Paused };
enum class CmdType : uint8_t { Play, Next, Prev, Stop, Exit };
struct Cmd {
  CmdType type;
  int slot;
};

// ---- shared state (atomics are read from other tasks) --------------------
std::atomic<State> s_state{State::Idle};
std::atomic<int> s_slot{0};
std::atomic<uint8_t> s_volume{50};
std::atomic<bool> s_ringReady{false};
std::atomic<int> s_consumersInside{0};

std::atomic<uint32_t> st_produced{0}, st_pulled{0}, st_underrun{0}, st_mp3Frames{0},
    st_errors{0}, st_tracks{0}, st_rate{0}, st_bitrate{0};
std::atomic<uint8_t> st_channels{0};

audiocore::SpscRing s_ring;
uint32_t* s_ringBuf = nullptr;
QueueHandle_t s_cmdQueue = nullptr;
SemaphoreHandle_t s_exitDone = nullptr;
TaskHandle_t s_task = nullptr;

// ---- decode-task-only state ----------------------------------------------
struct Decoder {
  HMP3Decoder helix = nullptr;
  uint8_t* inBuf = nullptr;
  int16_t* pcm = nullptr;
  uint32_t* out = nullptr;
  fs::File file;
  uint8_t* readPtr = nullptr;
  int bytesLeft = 0;
  bool eof = false;
  int consecutiveErrors = 0;
  size_t pcmFrames = 0;   // decoded frames in pcm
  size_t pcmPos = 0;      // frames of pcm already resampled
  int pcmChannels = 2;
  size_t outCount = 0;    // frames in out not yet in the ring
  size_t outPos = 0;
  bool producedAudio = false;
  int badStreak = 0;
  audiocore::Resampler resampler;
};
Decoder d;

void freeDecoderBuffers() {
  if (d.file) d.file.close();
  if (d.helix) MP3FreeDecoder(d.helix);
  d.helix = nullptr;
  free(d.inBuf);
  free(d.pcm);
  free(d.out);
  d.inBuf = nullptr;
  d.pcm = nullptr;
  d.out = nullptr;
}

void hasFiles(bool has[4]) { storage::hasFileAll(has); }

void closeTrack() {
  if (d.file) d.file.close();
  d.bytesLeft = 0;
  d.readPtr = d.inBuf;
  d.pcmFrames = d.pcmPos = 0;
  d.outCount = d.outPos = 0;
}

// Skips an ID3v2 tag at the start of the file (album art can contain bytes
// that look like MP3 sync words).
void skipId3(fs::File& f) {
  uint8_t h[10];
  if (f.read(h, 10) == 10 && h[0] == 'I' && h[1] == 'D' && h[2] == '3') {
    uint32_t size = ((uint32_t)(h[6] & 0x7F) << 21) | ((uint32_t)(h[7] & 0x7F) << 14) |
                    ((uint32_t)(h[8] & 0x7F) << 7) | (h[9] & 0x7F);
    uint32_t skip = 10 + size + ((h[5] & 0x10) ? 10 : 0);
    if (!f.seek(skip)) f.seek(0);
  } else {
    f.seek(0);
  }
}

// Opens `slot` and starts playing it. Returns false if the file can't be opened.
bool openTrack(int slot) {
  closeTrack();
  d.file = storage::openRead(slot);
  s_slot.store(slot);
  if (!d.file) {
    log_w("player: slot %d: open failed", slot);
    return false;
  }
  skipId3(d.file);
  d.eof = false;
  d.consecutiveErrors = 0;
  d.producedAudio = false;
  st_rate.store(0);
  st_channels.store(0);
  d.resampler.reset(audiocore::kOutRate);
  st_tracks.fetch_add(1);
  log_i("player: slot %d opened (%u bytes)", slot, (unsigned)d.file.size());
  return true;
}

void goIdle() {
  closeTrack();
  s_state.store(State::Idle);
}

// Starts `slot` (validated by the caller), walking on through the loop logic
// if it can't be opened. Ends idle if nothing can be played.
void startFrom(int slot) {
  while (slot != 0) {
    if (openTrack(slot)) {
      if (s_state.load() == State::Idle) s_state.store(State::Playing);
      return;
    }
    bool has[4];
    hasFiles(has);
    slot = audiocore::afterTrackEnd(slot, settings::loopMode(), has, false, d.badStreak);
  }
  log_i("player: nothing playable, idle");
  goIdle();
}

// Track ended (EOF or unrecoverable error): pick the next one.
void onTrackEnd() {
  int cur = s_slot.load();
  bool produced = d.producedAudio;
  closeTrack();
  bool has[4];
  hasFiles(has);
  int next = audiocore::afterTrackEnd(cur, settings::loopMode(), has, produced, d.badStreak);
  if (!produced) log_w("player: slot %d produced no audio (bad streak %d)", cur, d.badStreak);
  if (next == 0) {
    log_i("player: end of playback after slot %d, idle", cur);
    goIdle();
    return;
  }
  startFrom(next);
}

// Decodes one MP3 frame into d.pcm. Returns 1 = got PCM, 0 = need to call
// again (no PCM yet), -1 = end of track.
int decodeOne() {
  // Refill the input buffer.
  if (d.bytesLeft < MAINBUF_SIZE && !d.eof) {
    if (d.bytesLeft > 0 && d.readPtr != d.inBuf) memmove(d.inBuf, d.readPtr, d.bytesLeft);
    d.readPtr = d.inBuf;
    int n = d.file.read(d.inBuf + d.bytesLeft, kInBufBytes - d.bytesLeft);
    if (n <= 0) d.eof = true;
    else d.bytesLeft += n;
  }
  if (d.bytesLeft <= 0) return -1;

  int off = MP3FindSyncWord(d.readPtr, d.bytesLeft);
  if (off < 0) {
    // No sync word: drop all but the last 3 bytes (a sync could straddle).
    if (d.eof) return -1;
    if (d.bytesLeft > 3) {
      d.readPtr += d.bytesLeft - 3;
      d.bytesLeft = 3;
    }
    return 0;
  }
  d.readPtr += off;
  d.bytesLeft -= off;

  int err = MP3Decode(d.helix, &d.readPtr, &d.bytesLeft, d.pcm, 0);
  if (err == ERR_MP3_NONE) {
    d.consecutiveErrors = 0;
    MP3FrameInfo info;
    MP3GetLastFrameInfo(d.helix, &info);
    if (info.nChans < 1 || info.outputSamps <= 0) return 0;
    if ((uint32_t)info.samprate != d.resampler.inRate() || st_rate.load() == 0) {
      d.resampler.reset(info.samprate);
      st_rate.store(info.samprate);
      log_i("player: %d Hz, %d ch, %d kbit/s%s", info.samprate, info.nChans,
            info.bitrate / 1000, info.samprate == 44100 ? "" : " (resampling to 44100)");
    }
    st_channels.store(info.nChans);
    st_bitrate.store(info.bitrate);
    st_mp3Frames.fetch_add(1);
    d.pcmChannels = info.nChans;
    d.pcmFrames = info.outputSamps / info.nChans;
    d.pcmPos = 0;
    return 1;
  }
  if (err == ERR_MP3_INDATA_UNDERFLOW) {
    if (d.eof) return -1;
    // Need more data: force a refill on the next call.
    if (d.bytesLeft >= MAINBUF_SIZE) {  // buffer is full but frame still incomplete
      d.readPtr++;
      d.bytesLeft--;
    }
    return 0;
  }
  if (err == ERR_MP3_MAINDATA_UNDERFLOW) return 0;  // normal right after sync/seek

  st_errors.fetch_add(1);
  if (++d.consecutiveErrors > kMaxConsecutiveErrors) {
    log_w("player: too many decode errors, skipping track");
    return -1;
  }
  // Skip this sync word and resync.
  if (d.bytesLeft > 0) {
    d.readPtr++;
    d.bytesLeft--;
  }
  return 0;
}

void handleCmd(const Cmd& c) {
  bool has[4];
  switch (c.type) {
    case CmdType::Play: {
      hasFiles(has);
      int slot = audiocore::resolvePlay(c.slot, has);
      if (slot == 0) {
        log_w("player: play(%d): slot empty or invalid, ignored", c.slot);
        return;
      }
      s_ring.requestFlush();
      d.badStreak = 0;
      s_state.store(State::Playing);
      startFrom(slot);
      return;
    }
    case CmdType::Next:
    case CmdType::Prev: {
      hasFiles(has);
      int cur = s_slot.load();
      int slot = c.type == CmdType::Next
                     ? slotlogic::nextSlot(cur, slotlogic::LoopMode::AllSlots, has)
                     : slotlogic::prevSlot(cur, has);
      if (slot == 0) {
        log_w("player: next/prev: every slot is empty");
        s_ring.requestFlush();
        goIdle();
        return;
      }
      s_ring.requestFlush();
      d.badStreak = 0;
      if (s_state.load() == State::Idle) s_state.store(State::Playing);
      startFrom(slot);  // keeps Paused if paused
      return;
    }
    case CmdType::Stop:
      s_ring.requestFlush();
      goIdle();
      return;
    case CmdType::Exit:
      return;
  }
}

void decodeTask(void*) {
  int noPcmSteps = 0;
  for (;;) {
    // Commands: block briefly when idle, poll otherwise.
    Cmd c;
    TickType_t wait = (s_state.load() == State::Idle || !d.file) ? pdMS_TO_TICKS(20) : 0;
    bool exitReq = false;
    while (xQueueReceive(s_cmdQueue, &c, wait) == pdTRUE) {
      if (c.type == CmdType::Exit) {
        exitReq = true;
        break;
      }
      handleCmd(c);
      wait = 0;
    }
    if (exitReq) break;
    if (!d.file || s_state.load() == State::Idle) continue;

    // 1) Push pending output into the ring.
    if (d.outPos < d.outCount) {
      uint32_t n = s_ring.write(d.out + d.outPos, d.outCount - d.outPos);
      d.outPos += n;
      st_produced.fetch_add(n);
      if (d.outPos < d.outCount) vTaskDelay(pdMS_TO_TICKS(5));  // ring full
      continue;
    }
    // 2) Convert pending PCM into output frames.
    if (d.pcmPos < d.pcmFrames) {
      size_t used = 0;
      d.outCount = d.resampler.process(d.pcm + d.pcmPos * d.pcmChannels, d.pcmFrames - d.pcmPos,
                                       d.pcmChannels, d.out, kOutChunk, &used);
      d.outPos = 0;
      d.pcmPos += used;
      if (d.outCount > 0) d.producedAudio = true;
      continue;
    }
    // 3) Decode the next MP3 frame.
    int r = decodeOne();
    if (r < 0) {
      onTrackEnd();
    } else if (r == 0 && ++noPcmSteps >= 16) {
      // Garbage/ID3 data produces no PCM: yield so core 1 isn't hogged.
      noPcmSteps = 0;
      vTaskDelay(1);
    } else if (r > 0) {
      noPcmSteps = 0;
    }
  }

  freeDecoderBuffers();
  xSemaphoreGive(s_exitDone);
  vTaskDelete(nullptr);
}

// The ring goes to PSRAM: internal RAM is scarce once Bluetooth is up (~46 KB
// free). It is only touched from tasks (decode task, A2DP data callback), never
// from an ISR, so PSRAM is fine. Falls back to internal RAM.
void* allocRing(size_t bytes) {
  void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p) {
    log_i("player: ring buffer %u B in PSRAM", (unsigned)bytes);
    return p;
  }
  p = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (p) log_w("player: PSRAM allocation failed, ring buffer %u B in internal RAM", (unsigned)bytes);
  return p;
}

void post(CmdType t, int slot = 0) {
  if (!s_cmdQueue) return;
  Cmd c{t, slot};
  if (xQueueSend(s_cmdQueue, &c, pdMS_TO_TICKS(50)) != pdTRUE) log_w("player: command dropped");
}

}  // namespace

void begin() {
  if (s_task) return;
  s_volume.store(settings::volume());
  s_state.store(State::Idle);
  s_slot.store(0);

  s_ringBuf = (uint32_t*)allocRing(kRingFrames * sizeof(uint32_t));
  d.helix = MP3InitDecoder();
  d.inBuf = (uint8_t*)malloc(kInBufBytes);
  d.pcm = (int16_t*)malloc(kPcmShorts * sizeof(int16_t));
  d.out = (uint32_t*)malloc(kOutChunk * sizeof(uint32_t));
  s_cmdQueue = xQueueCreate(8, sizeof(Cmd));
  s_exitDone = xSemaphoreCreateBinary();
  if (!s_ringBuf || !d.helix || !d.inBuf || !d.pcm || !d.out || !s_cmdQueue || !s_exitDone) {
    log_e("player: out of memory");
    freeDecoderBuffers();
    free(s_ringBuf);
    s_ringBuf = nullptr;
    if (s_cmdQueue) vQueueDelete(s_cmdQueue);
    if (s_exitDone) vSemaphoreDelete(s_exitDone);
    s_cmdQueue = nullptr;
    s_exitDone = nullptr;
    return;
  }
  s_ring.init(s_ringBuf, kRingFrames);
  d.readPtr = d.inBuf;
  d.bytesLeft = 0;
  d.badStreak = 0;
  s_ringReady.store(true);

  if (xTaskCreatePinnedToCore(decodeTask, "mp3dec", kTaskStack, nullptr, kTaskPrio, &s_task,
                              kTaskCore) != pdPASS) {
    log_e("player: task create failed");
    s_task = nullptr;
    end();
  }
}

void end() {
  if (s_task) {
    // The task polls its queue at least every 20 ms, so it normally exits in
    // well under 100 ms. Retry the post (queue could be full) for up to 5 s.
    bool exited = false;
    for (int i = 0; i < 10 && !exited; ++i) {
      post(CmdType::Exit);
      exited = xSemaphoreTake(s_exitDone, pdMS_TO_TICKS(500)) == pdTRUE;
    }
    if (!exited) {
      // Deliberate choice: leak rather than crash. Deleting the task from here
      // could kill it inside LittleFS/heap code (holding locks), and freeing
      // buffers it may still use would corrupt memory. Leave the task, its
      // buffers, the ring and the queue alone; the player stays unusable
      // (begin() is a no-op while s_task is set) until a reboot.
      log_e("player: decode task did not exit within 5 s; leaving it (leak, no free)");
      s_ringReady.store(false);
      s_state.store(State::Idle);
      return;
    }
    s_task = nullptr;
  }
  // Keep pullFrames() (another core) away from the ring while it is freed.
  s_ringReady.store(false);
  while (s_consumersInside.load() != 0) vTaskDelay(1);
  free(s_ringBuf);  // s_ring keeps a stale pointer but is unused until begin()
  s_ringBuf = nullptr;
  if (s_cmdQueue) vQueueDelete(s_cmdQueue);
  if (s_exitDone) vSemaphoreDelete(s_exitDone);
  s_cmdQueue = nullptr;
  s_exitDone = nullptr;
  s_state.store(State::Idle);
}

void play(int slot) { post(CmdType::Play, slot); }
void next() { post(CmdType::Next); }
void prev() { post(CmdType::Prev); }
void stop() { post(CmdType::Stop); }

void pause() {
  State e = State::Playing;
  s_state.compare_exchange_strong(e, State::Paused);
}

void resume() {
  State e = State::Paused;
  s_state.compare_exchange_strong(e, State::Playing);
}

void setVolume(uint8_t v) { s_volume.store(v > 100 ? 100 : v); }
uint8_t volume() { return s_volume.load(); }
int currentSlot() { return s_slot.load(); }
bool playing() { return s_state.load() == State::Playing; }
bool paused() { return s_state.load() == State::Paused; }

int32_t pullFrames(Frame* out, int32_t count) {
  if (!out || count <= 0) return 0;
  uint32_t* frames = reinterpret_cast<uint32_t*>(out);
  s_consumersInside.fetch_add(1);
  uint32_t got = 0;
  if (s_ringReady.load()) {
    s_ring.consumerSync();  // apply track-change flushes even while paused
    if (s_state.load() == State::Playing) {
      got = s_ring.read(frames, (uint32_t)count);
      audiocore::applyVolume(frames, got, s_volume.load());
      if (got < (uint32_t)count) st_underrun.fetch_add(count - got);
    }
  }
  s_consumersInside.fetch_sub(1);
  if (got < (uint32_t)count) memset(frames + got, 0, (count - got) * sizeof(uint32_t));
  st_pulled.fetch_add(count);
  return count;
}

Stats stats() {
  Stats s;
  s.framesProduced = st_produced.load();
  s.framesPulled = st_pulled.load();
  s.underrunFrames = st_underrun.load();
  s.mp3Frames = st_mp3Frames.load();
  s.decodeErrors = st_errors.load();
  s.tracksStarted = st_tracks.load();
  s.sampleRate = st_rate.load();
  s.bitrate = st_bitrate.load();
  s.channels = st_channels.load();
  s.ringCapacity = s_ringReady.load() ? s_ring.capacity() : 0;
  s.ringFill = s.ringCapacity ? s.ringCapacity - s_ring.freeSpace() : 0;
  return s;
}

}  // namespace player
