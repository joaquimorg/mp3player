#include <unity.h>
#include <cstring>
#include "slot_logic.h"

using namespace slotlogic;

void setUp() {}
void tearDown() {}

static const bool ALL[4]   = {true, true, true, true};
static const bool NONE[4]  = {false, false, false, false};

void test_slotPath_valid() { TEST_ASSERT_EQUAL_STRING("/slot1.mp3", slotPath(1)); TEST_ASSERT_EQUAL_STRING("/slot4.mp3", slotPath(4)); }
void test_slotPath_zero_null() { TEST_ASSERT_NULL(slotPath(0)); }
void test_slotPath_five_null() { TEST_ASSERT_NULL(slotPath(5)); }
void test_next_repeatone() { TEST_ASSERT_EQUAL_INT(2, nextSlot(2, LoopMode::RepeatOne, ALL)); }
void test_next_all_wraps() { TEST_ASSERT_EQUAL_INT(1, nextSlot(4, LoopMode::AllSlots, ALL)); }
void test_next_all_skips_empty() { const bool h[4] = {1,0,0,1}; TEST_ASSERT_EQUAL_INT(4, nextSlot(1, LoopMode::AllSlots, h)); }
void test_next_all_only_current() { const bool h[4] = {1,0,0,0}; TEST_ASSERT_EQUAL_INT(1, nextSlot(1, LoopMode::AllSlots, h)); }
void test_next_all_empty_zero() { TEST_ASSERT_EQUAL_INT(0, nextSlot(1, LoopMode::AllSlots, NONE)); }
void test_next_repeatone_empty_zero() { TEST_ASSERT_EQUAL_INT(0, nextSlot(1, LoopMode::RepeatOne, NONE)); }
void test_next_repeatone_current_empty_falls_back() { const bool h[4] = {1,0,1,1}; TEST_ASSERT_EQUAL_INT(3, nextSlot(2, LoopMode::RepeatOne, h)); }
void test_prev_wraps() { const bool h[4] = {1,1,0,1}; TEST_ASSERT_EQUAL_INT(4, prevSlot(1, h)); }
void test_prev_all_empty_zero() { TEST_ASSERT_EQUAL_INT(0, prevSlot(3, NONE)); }

static const uint8_t ID3[4] = {'I','D','3',0x03};
static const uint8_t MPEG[4] = {0xFF,0xFB,0x90,0x00};
static const uint8_t JPG[4] = {0xFF,0xD8,0xFF,0xE0};
static const size_t MiB = 1024u*1024u;

void test_up_id3_ok() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::Ok, (int)validateUpload(1, 1000, 2000000, ID3)); }
void test_up_mpeg_ok() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::Ok, (int)validateUpload(4, 1000, 2000000, MPEG)); }
void test_up_jpeg_notmp3() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::NotMp3, (int)validateUpload(1, 1000, 2000000, JPG)); }
void test_up_ten_mb_in_twelve_ok() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::Ok, (int)validateUpload(1, 10*MiB, 12*MiB, ID3)); }
// A file needs size + kFsOverhead free bytes.
void test_up_space_margin_boundary() {
  TEST_ASSERT_EQUAL_INT((int)UploadCheck::Ok, (int)validateUpload(1, 1000000, 1000000 + kFsOverhead, ID3));
  TEST_ASSERT_EQUAL_INT((int)UploadCheck::NoSpace, (int)validateUpload(1, 1000000, 1000000 + kFsOverhead - 1, ID3));
}
void test_up_nospace() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::NoSpace, (int)validateUpload(1, 1024*1024, 500000, ID3)); }
void test_up_badslot() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::BadSlot, (int)validateUpload(0, 1000, 2000000, ID3)); TEST_ASSERT_EQUAL_INT((int)UploadCheck::BadSlot, (int)validateUpload(5, 1000, 2000000, ID3)); }
void test_up_order() {
  TEST_ASSERT_EQUAL_INT((int)UploadCheck::BadSlot, (int)validateUpload(0, 20*MiB, 0, JPG));
  TEST_ASSERT_EQUAL_INT((int)UploadCheck::NotMp3, (int)validateUpload(1, 20*MiB, 0, JPG));
  TEST_ASSERT_EQUAL_INT((int)UploadCheck::NoSpace, (int)validateUpload(1, 20*MiB, 0, ID3));
}
void test_up_zero_size_notok() { TEST_ASSERT_EQUAL_INT((int)UploadCheck::NotMp3, (int)validateUpload(1, 0, 2000000, ID3)); }
void test_up_ff_weak_sync_notmp3() { const uint8_t h[4] = {0xFF,0xC0,0,0}; TEST_ASSERT_EQUAL_INT((int)UploadCheck::NotMp3, (int)validateUpload(1, 1000, 2000000, h)); }

static void runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_slotPath_valid); RUN_TEST(test_slotPath_zero_null); RUN_TEST(test_slotPath_five_null);
  RUN_TEST(test_next_repeatone); RUN_TEST(test_next_all_wraps); RUN_TEST(test_next_all_skips_empty);
  RUN_TEST(test_next_all_only_current); RUN_TEST(test_next_all_empty_zero); RUN_TEST(test_next_repeatone_empty_zero);
  RUN_TEST(test_next_repeatone_current_empty_falls_back);
  RUN_TEST(test_prev_wraps); RUN_TEST(test_prev_all_empty_zero);
  RUN_TEST(test_up_id3_ok); RUN_TEST(test_up_mpeg_ok); RUN_TEST(test_up_jpeg_notmp3); 
  RUN_TEST(test_up_ten_mb_in_twelve_ok); RUN_TEST(test_up_space_margin_boundary); RUN_TEST(test_up_nospace); RUN_TEST(test_up_badslot); RUN_TEST(test_up_order);
  RUN_TEST(test_up_zero_size_notok); RUN_TEST(test_up_ff_weak_sync_notmp3);
  UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runAll(); }
void loop() {}
#else
int main() { runAll(); return 0; }
#endif
