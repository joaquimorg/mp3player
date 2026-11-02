// On-device tests: pio test -e watch_test -f test_device
#include <Arduino.h>
#include <LittleFS.h>
#include <unity.h>
#include "storage.h"
#include "settings.h"

using namespace storage;

static uint8_t buf[1100];

void setUp() {
  for (int s = 1; s <= 4; s++) { remove(s); }
  LittleFS.remove("/slot1.tmp");
}
void tearDown() {}

static bool put(int slot, size_t n, uint8_t fill) {
  memset(buf, fill, sizeof buf);
  SlotWriter w(slot);
  return w.write(buf, n) && w.commit();
}

void test_commit_creates_slot() {
  TEST_ASSERT_TRUE(put(1, 1003, 0));
  TEST_ASSERT_TRUE(hasFile(1));
  TEST_ASSERT_EQUAL_UINT32(1003, slotSize(1));
  TEST_ASSERT_FALSE(LittleFS.exists("/slot1.tmp"));
}

void test_commit_replaces_old() {
  put(1, 1003, 1);
  TEST_ASSERT_TRUE(put(1, 500, 2));
  TEST_ASSERT_EQUAL_UINT32(500, slotSize(1));
  File f = openRead(1);
  TEST_ASSERT_EQUAL_UINT8(2, f.read());
  f.close();
}

void test_abort_keeps_old() {
  put(1, 1003, 1);
  SlotWriter w(1);
  TEST_ASSERT_TRUE(w.write(buf, 10));
  w.abort();
  TEST_ASSERT_EQUAL_UINT32(1003, slotSize(1));
  TEST_ASSERT_FALSE(LittleFS.exists("/slot1.tmp"));
  File f = openRead(1);
  TEST_ASSERT_EQUAL_UINT8(1, f.read());
  f.close();
}

void test_write_and_commit_after_abort_fail() {
  SlotWriter w(1);
  w.write(buf, 10);
  w.abort();
  TEST_ASSERT_FALSE(w.write(buf, 1));
  TEST_ASSERT_FALSE(w.commit());
  TEST_ASSERT_FALSE(hasFile(1));
}

void test_commit_never_written_fails_keeps_old() {
  put(1, 1003, 1);
  {
    SlotWriter w(1);
    TEST_ASSERT_FALSE(w.commit());
  }
  TEST_ASSERT_EQUAL_UINT32(1003, slotSize(1));
}

void test_destructor_aborts() {
  put(1, 1003, 1);
  {
    SlotWriter w(1);
    w.write(buf, 10);
  }
  TEST_ASSERT_FALSE(LittleFS.exists("/slot1.tmp"));
  TEST_ASSERT_EQUAL_UINT32(1003, slotSize(1));
}

void test_stale_tmp_removed_by_writer_and_begin() {
  File f = LittleFS.open("/slot1.tmp", "w");
  f.print("junk");
  f.close();
  TEST_ASSERT_TRUE(begin());
  TEST_ASSERT_FALSE(LittleFS.exists("/slot1.tmp"));
  f = LittleFS.open("/slot1.tmp", "w");
  f.print("junk");
  f.close();
  { SlotWriter w(1); w.write(buf, 5); w.commit(); }
  TEST_ASSERT_EQUAL_UINT32(5, slotSize(1));
}

void test_bad_slot_writer() {
  SlotWriter w(0);
  TEST_ASSERT_FALSE(w.write(buf, 1));
  TEST_ASSERT_FALSE(w.commit());
}

void test_remove_clears_file_and_name() {
  put(2, 100, 0);
  setSlotName(2, "abc.mp3");
  TEST_ASSERT_EQUAL_STRING("abc.mp3", slotName(2).c_str());
  TEST_ASSERT_TRUE(remove(2));
  TEST_ASSERT_FALSE(hasFile(2));
  TEST_ASSERT_EQUAL_STRING("Faixa 2", slotName(2).c_str());
}

void test_name_roundtrip_utf8_quotes_persisted() {
  const char* n = "M\xC3\xBAsica \xC3\xA7\xC3\xA3o \"q\" \\ x.mp3";
  setSlotName(3, n);
  TEST_ASSERT_TRUE(begin());  // forces reload from /names.json
  TEST_ASSERT_EQUAL_STRING(n, slotName(3).c_str());
}

void test_name_cap_48_no_split_utf8() {
  String s;
  for (int i = 0; i < 30; i++) s += "\xC3\xA7";  // 60 bytes
  setSlotName(4, s);
  begin();
  String r = slotName(4);
  TEST_ASSERT_EQUAL_UINT32(48, r.length());
  TEST_ASSERT_EQUAL_UINT8(0xC3, (uint8_t)r[46]);
  TEST_ASSERT_EQUAL_UINT8(0xA7, (uint8_t)r[47]);
  String t = "";
  for (int i = 0; i < 49; i++) t += (i == 47 ? "\xC3\xA7" : "a");  // 2-byte char straddles byte 48
  setSlotName(4, t);
  TEST_ASSERT_TRUE(slotName(4).length() <= 48);
  TEST_ASSERT_EQUAL_UINT8('a', (uint8_t)slotName(4)[slotName(4).length() - 1]);
}

void test_hasFileAll() {
  put(1, 10, 0);
  put(4, 10, 0);
  bool h[4];
  hasFileAll(h);
  TEST_ASSERT_TRUE(h[0]); TEST_ASSERT_FALSE(h[1]); TEST_ASSERT_FALSE(h[2]); TEST_ASSERT_TRUE(h[3]);
  TEST_ASSERT_TRUE(freeBytes() > 1000000);
}

void test_settings_roundtrip() {
  settings::setVolume(77);
  TEST_ASSERT_EQUAL_UINT8(77, settings::volume());
  settings::setVolume(200);
  TEST_ASSERT_EQUAL_UINT8(100, settings::volume());
  settings::setLoopMode(slotlogic::LoopMode::AllSlots);
  TEST_ASSERT_TRUE(settings::loopMode() == slotlogic::LoopMode::AllSlots);
  settings::setLoopMode(slotlogic::LoopMode::RepeatOne);
  settings::setWifi("ssid \"x\"", "pw");
  TEST_ASSERT_EQUAL_STRING("ssid \"x\"", settings::wifiSsid().c_str());
  settings::setWifi("", "");
  settings::setVolume(50);
}

void setup() {
  delay(2000);
  begin();
  UNITY_BEGIN();
  RUN_TEST(test_commit_creates_slot);
  RUN_TEST(test_commit_replaces_old);
  RUN_TEST(test_abort_keeps_old);
  RUN_TEST(test_write_and_commit_after_abort_fail);
  RUN_TEST(test_commit_never_written_fails_keeps_old);
  RUN_TEST(test_destructor_aborts);
  RUN_TEST(test_stale_tmp_removed_by_writer_and_begin);
  RUN_TEST(test_bad_slot_writer);
  RUN_TEST(test_remove_clears_file_and_name);
  RUN_TEST(test_name_roundtrip_utf8_quotes_persisted);
  RUN_TEST(test_name_cap_48_no_split_utf8);
  RUN_TEST(test_hasFileAll);
  RUN_TEST(test_settings_roundtrip);
  UNITY_END();
}
void loop() {}
