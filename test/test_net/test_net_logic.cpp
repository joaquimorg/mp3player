// Pure web-API helpers. Run natively: pio test -e native -f test_net
#include <unity.h>
#include "net_logic.h"

using namespace netlogic;
using slotlogic::UploadCheck;

void setUp() {}
void tearDown() {}

void test_sanitize_basic() { TEST_ASSERT_EQUAL_STRING("Fado", sanitizeFileName("Fado.mp3").c_str()); }
void test_sanitize_ext_case() { TEST_ASSERT_EQUAL_STRING("A B", sanitizeFileName("A B.MP3").c_str()); }
void test_sanitize_path() { TEST_ASSERT_EQUAL_STRING("x", sanitizeFileName("C:\\music\\dir/x.mp3").c_str()); }
void test_sanitize_ctrl_and_tags() { TEST_ASSERT_EQUAL_STRING("ab", sanitizeFileName("a\x01<b>\n.mp3").c_str()); }
void test_sanitize_utf8_kept() { TEST_ASSERT_EQUAL_STRING("Coração", sanitizeFileName("Coração.mp3").c_str()); }
void test_sanitize_latin1_fallback() { TEST_ASSERT_EQUAL_STRING("Mús ção", sanitizeFileName("M\xFAs \xE7\xE3o.mp3").c_str()); }
void test_sanitize_cap_no_split() {
  std::string s;
  for (int i = 0; i < 30; i++) s += "ã";  // 2 bytes each = 60 bytes
  TEST_ASSERT_EQUAL_UINT32(48, sanitizeFileName(s).size());
  std::string s2 = "a";
  for (int i = 0; i < 30; i++) s2 += "ã";  // cutting at 48 would split a sequence
  TEST_ASSERT_EQUAL_UINT32(47, sanitizeFileName(s2).size());
}
void test_sanitize_empty() {
  TEST_ASSERT_EQUAL_STRING("", sanitizeFileName("  .mp3").c_str());
  TEST_ASSERT_EQUAL_STRING("", sanitizeFileName("").c_str());
}
void test_json_escape() { TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\u000a", jsonEscape("a\"b\\c\n").c_str()); }
void test_json_error() { TEST_ASSERT_EQUAL_STRING("{\"error\":\"x\\\"y\"}", jsonError("x\"y").c_str()); }
void test_slots_json() {
  SlotInfo s[4] = {{0, ""}, {1234, "Fado \"x\""}, {0, ""}, {5, "é"}};
  TEST_ASSERT_EQUAL_STRING(
      "{\"free\":99,\"slots\":[{\"n\":1,\"size\":0,\"name\":\"\"},{\"n\":2,\"size\":1234,\"name\":\"Fado \\\"x\\\"\"},"
      "{\"n\":3,\"size\":0,\"name\":\"\"},{\"n\":4,\"size\":5,\"name\":\"é\"}]}",
      buildSlotsJson(99, s).c_str());
}
void test_estimate() {
  TEST_ASSERT_EQUAL_UINT32(0, estimateFileSize(0));
  TEST_ASSERT_EQUAL_UINT32(0, estimateFileSize(-1));
  TEST_ASSERT_EQUAL_UINT32(600, estimateFileSize(1000));
  TEST_ASSERT_EQUAL_UINT32(1, estimateFileSize(300));
}
void test_messages() {
  TEST_ASSERT_EQUAL_STRING("Sem espaço: apaga primeiro este slot e volta a enviar.",
                           uploadErrorMessage(UploadCheck::NoSpace, true));
  TEST_ASSERT_TRUE(uploadErrorMessage(UploadCheck::NoSpace, false) != uploadErrorMessage(UploadCheck::NoSpace, true));
  TEST_ASSERT_EQUAL_STRING("", uploadErrorMessage(UploadCheck::Ok, false));
}
void test_slot_number() {
  TEST_ASSERT_EQUAL_INT(3, parseSlotNumber("3"));
  TEST_ASSERT_EQUAL_INT(0, parseSlotNumber("12"));
  TEST_ASSERT_EQUAL_INT(0, parseSlotNumber("x"));
  TEST_ASSERT_EQUAL_INT(0, parseSlotNumber(""));
}

static void runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_sanitize_basic); RUN_TEST(test_sanitize_ext_case); RUN_TEST(test_sanitize_path);
  RUN_TEST(test_sanitize_ctrl_and_tags); RUN_TEST(test_sanitize_utf8_kept); RUN_TEST(test_sanitize_latin1_fallback);
  RUN_TEST(test_sanitize_cap_no_split); RUN_TEST(test_sanitize_empty); RUN_TEST(test_json_escape);
  RUN_TEST(test_json_error); RUN_TEST(test_slots_json); RUN_TEST(test_estimate); RUN_TEST(test_messages);
  RUN_TEST(test_slot_number);
  UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runAll(); }
void loop() {}
#else
int main() { runAll(); return 0; }
#endif
