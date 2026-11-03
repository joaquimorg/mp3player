// On-device Unity tests for the audio core (ring buffer, resampler, volume,
// slot advance) and the player's empty-slot / lifecycle behaviour.
// Run: pio test -e watch_test -f test_audio
// Non-destructive: never writes or deletes a slot.
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <unity.h>

#include "audio_core.h"
#include "player.h"
#include "storage.h"

using audiocore::packFrame;
using slotlogic::LoopMode;

// ------------------------------------------------------------------ ring

static void test_ring_wraps_and_limits() {
  static uint32_t buf[8];
  audiocore::SpscRing r;
  TEST_ASSERT_TRUE(r.init(buf, 8));
  TEST_ASSERT_FALSE(r.init(buf, 6));  // not a power of two
  TEST_ASSERT_TRUE(r.init(buf, 8));
  uint32_t in[12], out[12];
  for (uint32_t i = 0; i < 12; ++i) in[i] = 100 + i;
  TEST_ASSERT_EQUAL_UINT32(5, r.write(in, 5));
  TEST_ASSERT_EQUAL_UINT32(3, r.read(out, 3));
  TEST_ASSERT_EQUAL_UINT32(100, out[0]);
  TEST_ASSERT_EQUAL_UINT32(102, out[2]);
  // 2 buffered, 6 free: a write of 12 is capped and wraps around the end.
  TEST_ASSERT_EQUAL_UINT32(6, r.freeSpace());
  TEST_ASSERT_EQUAL_UINT32(6, r.write(in + 5, 12));
  TEST_ASSERT_EQUAL_UINT32(0, r.freeSpace());
  TEST_ASSERT_EQUAL_UINT32(8, r.read(out, 12));
  for (uint32_t i = 0; i < 8; ++i) TEST_ASSERT_EQUAL_UINT32(103 + i, out[i]);
  TEST_ASSERT_EQUAL_UINT32(0, r.read(out, 4));  // empty
}

static void test_ring_flush_keeps_new_data() {
  static uint32_t buf[16];
  audiocore::SpscRing r;
  r.init(buf, 16);
  uint32_t a[6] = {1, 2, 3, 4, 5, 6}, b[3] = {7, 8, 9}, out[16];
  r.write(a, 6);
  r.requestFlush();  // old track
  r.write(b, 3);     // new track, written before the consumer noticed
  TEST_ASSERT_EQUAL_UINT32(3, r.read(out, 16));
  TEST_ASSERT_EQUAL_UINT32(7, out[0]);
  TEST_ASSERT_EQUAL_UINT32(9, out[2]);
}

static void test_ring_flush_never_rewinds() {
  static uint32_t buf[16];
  audiocore::SpscRing r;
  r.init(buf, 16);
  uint32_t a[4] = {1, 2, 3, 4}, out[16];
  r.write(a, 4);
  r.consumerSync();
  TEST_ASSERT_EQUAL_UINT32(4, r.read(out, 4));  // consumer already past...
  r.requestFlush();                             // ...the flush point
  uint32_t b[2] = {5, 6};
  r.write(b, 2);
  TEST_ASSERT_EQUAL_UINT32(2, r.read(out, 16));
  TEST_ASSERT_EQUAL_UINT32(5, out[0]);
}

// ------------------------------------------------------------- resampler

static void test_resampler_44k_mono_duplicates() {
  audiocore::Resampler rs;
  rs.reset(44100);
  int16_t in[4] = {10, -20, 30, -40};
  uint32_t out[8];
  size_t used = 0;
  TEST_ASSERT_EQUAL(4, (int)rs.process(in, 4, 1, out, 8, &used));
  TEST_ASSERT_EQUAL(4, (int)used);
  TEST_ASSERT_EQUAL_UINT32(packFrame(-20, -20), out[1]);
  TEST_ASSERT_EQUAL_UINT32(packFrame(-40, -40), out[3]);
}

static void test_resampler_44k_stereo_respects_cap() {
  audiocore::Resampler rs;
  rs.reset(44100);
  int16_t in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint32_t out[3];
  size_t used = 0;
  TEST_ASSERT_EQUAL(3, (int)rs.process(in, 4, 2, out, 3, &used));
  TEST_ASSERT_EQUAL(3, (int)used);
  TEST_ASSERT_EQUAL_UINT32(packFrame(5, 6), out[2]);
}

// Feeds `frames` input frames in odd-sized pieces, returns output count.
static size_t runRate(uint32_t rate, size_t frames, int16_t value, bool* constant) {
  audiocore::Resampler rs;
  rs.reset(rate);
  static int16_t in[2 * 1152];
  for (auto& s : in) s = value;
  static uint32_t out[700];
  size_t total = 0, fed = 0;
  *constant = true;
  while (fed < frames) {
    size_t piece = frames - fed < 333 ? frames - fed : 333;
    size_t off = 0;
    while (off < piece) {
      size_t used = 0;
      size_t n = rs.process(in, piece - off, 2, out, 700, &used);
      for (size_t k = 0; k < n; ++k) {
        // skip the start-up ramp (interpolates from the initial zero over
        // the first input sample period: <= 3 output frames for >= 16 kHz)
        if (total + k > 3 && out[k] != packFrame(value, value)) *constant = false;
      }
      total += n;
      off += used;
    }
    fed += piece;
  }
  return total;
}

static void test_resampler_rates() {
  bool constant = false;
  // 22050 -> 44100: twice as many frames (+-2)
  size_t n = runRate(22050, 11025, 1000, &constant);
  TEST_ASSERT_INT_WITHIN(2, 22050, (int)n);
  TEST_ASSERT_TRUE(constant);
  // 48000 -> 44100: 0.91875x
  n = runRate(48000, 48000, -1234, &constant);
  TEST_ASSERT_INT_WITHIN(2, 44100, (int)n);
  TEST_ASSERT_TRUE(constant);
  // 32000 -> 44100
  n = runRate(32000, 32000, 77, &constant);
  TEST_ASSERT_INT_WITHIN(2, 44100, (int)n);
  TEST_ASSERT_TRUE(constant);
}

// ---------------------------------------------------------------- volume

static void test_volume() {
  uint32_t f[2] = {packFrame(1000, -1000), packFrame(32767, -32768)};
  audiocore::applyVolume(f, 2, 100);
  TEST_ASSERT_EQUAL_UINT32(packFrame(1000, -1000), f[0]);
  audiocore::applyVolume(f, 2, 50);  // gain 0.25
  TEST_ASSERT_EQUAL_UINT32(packFrame(250, -250), f[0]);
  TEST_ASSERT_EQUAL_UINT32(packFrame(8191, -8192), f[1]);
  audiocore::applyVolume(f, 2, 0);
  TEST_ASSERT_EQUAL_UINT32(0, f[0]);
  TEST_ASSERT_EQUAL_UINT32(0, f[1]);
}

// ----------------------------------------------------------- slot advance

static void test_after_track_end() {
  const bool none[4] = {false, false, false, false};
  const bool s13[4] = {true, false, true, false};
  int streak = 0;
  // every slot empty -> idle (Review Focus 5)
  TEST_ASSERT_EQUAL(0, audiocore::afterTrackEnd(1, LoopMode::AllSlots, none, true, streak));
  TEST_ASSERT_EQUAL(0, audiocore::afterTrackEnd(1, LoopMode::RepeatOne, none, true, streak));
  // RepeatOne replays a good track, AllSlots skips empty slots
  TEST_ASSERT_EQUAL(3, audiocore::afterTrackEnd(3, LoopMode::RepeatOne, s13, true, streak));
  TEST_ASSERT_EQUAL(3, audiocore::afterTrackEnd(1, LoopMode::AllSlots, s13, true, streak));
  TEST_ASSERT_EQUAL(1, audiocore::afterTrackEnd(3, LoopMode::AllSlots, s13, true, streak));
  // current slot became empty under RepeatOne -> next file
  TEST_ASSERT_EQUAL(3, audiocore::afterTrackEnd(2, LoopMode::RepeatOne, s13, true, streak));
  // silent/broken track: RepeatOne stops instead of spinning
  streak = 0;
  TEST_ASSERT_EQUAL(0, audiocore::afterTrackEnd(1, LoopMode::RepeatOne, s13, false, streak));
  // AllSlots: one bad file moves on, all files bad in a row -> idle
  streak = 0;
  TEST_ASSERT_EQUAL(3, audiocore::afterTrackEnd(1, LoopMode::AllSlots, s13, false, streak));
  TEST_ASSERT_EQUAL(0, audiocore::afterTrackEnd(3, LoopMode::AllSlots, s13, false, streak));
  // a good track resets the streak
  streak = 1;
  TEST_ASSERT_EQUAL(3, audiocore::afterTrackEnd(1, LoopMode::AllSlots, s13, true, streak));
  TEST_ASSERT_EQUAL(0, streak);
}

static void test_resolve_play() {
  const bool s13[4] = {true, false, true, false};
  TEST_ASSERT_EQUAL(1, audiocore::resolvePlay(1, s13));
  TEST_ASSERT_EQUAL(0, audiocore::resolvePlay(2, s13));
  TEST_ASSERT_EQUAL(0, audiocore::resolvePlay(0, s13));
  TEST_ASSERT_EQUAL(0, audiocore::resolvePlay(5, s13));
}

// ------------------------------------------------------------ player (device)

static uint32_t heapInt() { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); }

static Frame s_frames[512];

static bool allSilent(int n) {
  for (int i = 0; i < n; ++i)
    if (s_frames[i].channel1 || s_frames[i].channel2) return false;
  return true;
}

static void test_player_pull_before_begin_is_silence() {
  memset(s_frames, 0x55, sizeof(s_frames));
  TEST_ASSERT_EQUAL(512, player::pullFrames(s_frames, 512));
  TEST_ASSERT_TRUE(allSilent(512));
}

static void test_player_empty_slot_stays_idle() {
  bool has[4];
  storage::hasFileAll(has);
  int empty = 0;
  for (int i = 0; i < 4 && !empty; ++i)
    if (!has[i]) empty = i + 1;
  if (!empty) TEST_IGNORE_MESSAGE("no empty slot on this watch");
  player::begin();
  player::play(empty);
  delay(150);
  TEST_ASSERT_FALSE(player::playing());
  TEST_ASSERT_EQUAL(0, player::currentSlot());
  memset(s_frames, 0x55, sizeof(s_frames));
  TEST_ASSERT_EQUAL(256, player::pullFrames(s_frames, 256));
  TEST_ASSERT_TRUE(allSilent(256));
  player::play(0);
  player::play(9);
  delay(100);
  TEST_ASSERT_FALSE(player::playing());
  player::end();
}

static void test_player_plays_and_pauses_real_file() {
  bool has[4];
  storage::hasFileAll(has);
  int slot = 0;
  for (int i = 0; i < 4 && !slot; ++i)
    if (has[i]) slot = i + 1;
  if (!slot) TEST_IGNORE_MESSAGE("no MP3 in any slot (upload one via the web page)");
  player::begin();
  player::play(slot);
  delay(300);
  TEST_ASSERT_TRUE(player::playing());
  TEST_ASSERT_EQUAL(slot, player::currentSlot());
  // Pull ~2 s of audio in real time; ring must keep up after the first fill.
  uint32_t under0 = 0;
  bool sound = false;
  for (int i = 0; i < 200; ++i) {
    TEST_ASSERT_EQUAL(441, player::pullFrames(s_frames, 441));
    if (!allSilent(441)) sound = true;
    if (i == 20) under0 = player::stats().underrunFrames;
    delay(10);
  }
  TEST_ASSERT_TRUE(sound);
  TEST_ASSERT_EQUAL_UINT32(under0, player::stats().underrunFrames);  // no underrun once running
  TEST_ASSERT_NOT_EQUAL(0, (int)player::stats().sampleRate);
  TEST_ASSERT_EQUAL(0, (int)player::stats().decodeErrors);
  player::pause();
  TEST_ASSERT_FALSE(player::playing());
  TEST_ASSERT_TRUE(player::paused());
  memset(s_frames, 0x55, sizeof(s_frames));
  TEST_ASSERT_EQUAL(441, player::pullFrames(s_frames, 441));
  TEST_ASSERT_TRUE(allSilent(441));
  player::resume();
  TEST_ASSERT_TRUE(player::playing());
  player::end();  // while playing
  TEST_ASSERT_FALSE(player::playing());
}

static void test_player_begin_end_does_not_leak() {
  player::begin();
  player::end();
  delay(100);  // the idle task frees the self-deleted decode task's stack
  uint32_t before = heapInt();
  for (int i = 0; i < 5; ++i) {
    player::begin();
    player::play(1);
    delay(100);
    player::end();
  }
  delay(100);
  uint32_t after = heapInt();
  TEST_ASSERT_INT_WITHIN(512, (int)before, (int)after);
}

void setup() {
  delay(2000);
  storage::begin();
  UNITY_BEGIN();
  RUN_TEST(test_ring_wraps_and_limits);
  RUN_TEST(test_ring_flush_keeps_new_data);
  RUN_TEST(test_ring_flush_never_rewinds);
  RUN_TEST(test_resampler_44k_mono_duplicates);
  RUN_TEST(test_resampler_44k_stereo_respects_cap);
  RUN_TEST(test_resampler_rates);
  RUN_TEST(test_volume);
  RUN_TEST(test_after_track_end);
  RUN_TEST(test_resolve_play);
  RUN_TEST(test_player_pull_before_begin_is_silence);
  RUN_TEST(test_player_empty_slot_stays_idle);
  RUN_TEST(test_player_plays_and_pauses_real_file);
  RUN_TEST(test_player_begin_end_does_not_leak);
  UNITY_END();
}

void loop() {}
