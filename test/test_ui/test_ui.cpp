// Pure UI logic tests. Run natively (pio test -e native -f test_ui) or on the
// watch (pio test -e watch_test -f test_ui).
#include <unity.h>
#include <string>
#include "ui_logic.h"

using namespace uilogic;

void setUp() {}
void tearDown() {}

// ---- layout -------------------------------------------------------------------
static bool inside(const Rect& r) { return r.x >= 0 && r.y >= 0 && r.x + r.w <= kScreenW && r.y + r.h <= kScreenH; }
static bool overlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void test_play_rects_inside_and_disjoint() {
  for (int i = 1; i <= kPlayTargets; ++i) {
    Rect a = playRect((PlayHit)i);
    TEST_ASSERT_TRUE_MESSAGE(inside(a), "rect outside screen");
    TEST_ASSERT_TRUE(a.w >= 40 && a.h >= 40);
    for (int j = i + 1; j <= kPlayTargets; ++j) TEST_ASSERT_FALSE(overlap(a, playRect((PlayHit)j)));
  }
}

void test_play_controls_at_least_40px() {
  for (int i = (int)PlayHit::Prev; i <= (int)PlayHit::Program; ++i) {
    Rect a = playRect((PlayHit)i);
    TEST_ASSERT_TRUE(a.w >= 40 && a.h >= 40);
  }
}

void test_play_targets_cover_whole_screen() {
  for (int y = 0; y < kScreenH; ++y)
    for (int x = 0; x < kScreenW; ++x) TEST_ASSERT_TRUE(playHitTest(x, y) != PlayHit::None);
  TEST_ASSERT_TRUE(playHitTest(100, -1) == PlayHit::None);
  TEST_ASSERT_TRUE(playHitTest(100, 240) == PlayHit::None);
  TEST_ASSERT_TRUE(playHitTest(-1, 100) == PlayHit::None);
  TEST_ASSERT_TRUE(playHitTest(240, 100) == PlayHit::None);
}

void test_play_hit_points() {
  TEST_ASSERT_TRUE(playHitTest(120, 0) == PlayHit::Slot1);
  TEST_ASSERT_TRUE(playHitTest(5, 39) == PlayHit::Slot1);
  TEST_ASSERT_TRUE(playHitTest(5, 40) == PlayHit::Slot2);
  TEST_ASSERT_TRUE(playHitTest(5, 119) == PlayHit::Slot3);
  TEST_ASSERT_TRUE(playHitTest(5, 120) == PlayHit::Slot4);
  TEST_ASSERT_TRUE(playHitTest(239, 159) == PlayHit::Slot4);
  TEST_ASSERT_TRUE(playHitTest(0, 160) == PlayHit::Prev);
  TEST_ASSERT_TRUE(playHitTest(72, 180) == PlayHit::PlayPause);
  TEST_ASSERT_TRUE(playHitTest(120, 180) == PlayHit::Next);
  TEST_ASSERT_TRUE(playHitTest(170, 180) == PlayHit::VolDown);
  TEST_ASSERT_TRUE(playHitTest(239, 199) == PlayHit::VolUp);
  TEST_ASSERT_TRUE(playHitTest(10, 220) == PlayHit::Loop);
  TEST_ASSERT_TRUE(playHitTest(120, 220) == PlayHit::Bluetooth);
  TEST_ASSERT_TRUE(playHitTest(239, 239) == PlayHit::Program);
  TEST_ASSERT_EQUAL_INT(3, slotOf(PlayHit::Slot3));
  TEST_ASSERT_EQUAL_INT(0, slotOf(PlayHit::Next));
}

void test_hash_mix() {
  uint32_t a = hashMix(hashMix(kHashSeed, 1), 2), b = hashMix(hashMix(kHashSeed, 2), 1);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_EQUAL_UINT32(a, hashMix(hashMix(kHashSeed, 1), 2));
  TEST_ASSERT_TRUE(hashMix(kHashSeed, 0) != hashMix(kHashSeed, 256));
}

void test_bt_layout_and_hits() {
  for (int r = 0; r < kBtRowsPerPage; ++r) {
    TEST_ASSERT_TRUE(inside(btRowRect(r)));
    TEST_ASSERT_TRUE(btRowRect(r).h >= 40);
  }
  for (int b = 0; b < 3; ++b) {
    Rect a = btButtonRect((BtButton)b);
    TEST_ASSERT_TRUE(inside(a) && a.h >= 40 && a.w >= 40);
    TEST_ASSERT_FALSE(overlap(a, btRowRect(kBtRowsPerPage - 1)));
  }
  BtHit h = btHitTest(100, 40, 4, false);
  TEST_ASSERT_TRUE(h.kind == BtHit::Kind::Row);
  TEST_ASSERT_EQUAL_INT(0, h.row);
  h = btHitTest(100, 191, 4, false);
  TEST_ASSERT_TRUE(h.kind == BtHit::Kind::Row);
  TEST_ASSERT_EQUAL_INT(3, h.row);
  // rows beyond the ones shown are dead
  TEST_ASSERT_TRUE(btHitTest(100, 191, 2, false).kind == BtHit::Kind::None);
  TEST_ASSERT_TRUE(btHitTest(10, 210, 0, false).kind == BtHit::Kind::Scan);
  TEST_ASSERT_TRUE(btHitTest(120, 210, 0, false).kind == BtHit::Kind::None);  // Mais hidden
  TEST_ASSERT_TRUE(btHitTest(120, 210, 0, true).kind == BtHit::Kind::More);
  TEST_ASSERT_TRUE(btHitTest(230, 239, 0, true).kind == BtHit::Kind::Back);
  TEST_ASSERT_TRUE(btHitTest(100, 10, 4, true).kind == BtHit::Kind::None);  // header
}

void test_program_button() {
  TEST_ASSERT_TRUE(inside(programBackRect()));
  TEST_ASSERT_TRUE(programHitTest(120, 200));
  TEST_ASSERT_FALSE(programHitTest(120, 100));
}

// ---- paging -------------------------------------------------------------------
void test_paging() {
  TEST_ASSERT_EQUAL_INT(1, pageCount(0, 4));
  TEST_ASSERT_EQUAL_INT(1, pageCount(4, 4));
  TEST_ASSERT_EQUAL_INT(2, pageCount(5, 4));
  TEST_ASSERT_EQUAL_INT(2, pageCount(6, 4));
  TEST_ASSERT_EQUAL_INT(1, nextPage(0, 6, 4));
  TEST_ASSERT_EQUAL_INT(0, nextPage(1, 6, 4));
  TEST_ASSERT_EQUAL_INT(0, nextPage(0, 3, 4));
  TEST_ASSERT_EQUAL_INT(1, clampPage(5, 6, 4));   // list shrank
  TEST_ASSERT_EQUAL_INT(0, clampPage(-1, 6, 4));
  TEST_ASSERT_EQUAL_INT(4, pageFirst(1, 4));
  TEST_ASSERT_EQUAL_INT(4, pageRows(0, 6, 4));
  TEST_ASSERT_EQUAL_INT(2, pageRows(1, 6, 4));
  TEST_ASSERT_EQUAL_INT(0, pageRows(0, 0, 4));
}

// ---- text ---------------------------------------------------------------------
// Fake font: Latin-1 + U+2026, every codepoint 10 px wide.
static bool latin1(uint32_t cp, void*) { return (cp >= 0x20 && cp <= 0xFF) || cp == 0x2026; }
static int cpCount(const std::string& s, void*) {
  int n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n * 10;
}

void test_sanitize_cases() {
  TEST_ASSERT_EQUAL_STRING("Canção", sanitize("Canção", latin1, nullptr).c_str());
  // emoji (> U+FFFF) -> '?'
  TEST_ASSERT_EQUAL_STRING("a?b", sanitize("a\xF0\x9F\x8E\xB5" "b", latin1, nullptr).c_str());
  // CJK (BMP but not in font) -> '?'
  TEST_ASSERT_EQUAL_STRING("x?", sanitize("x\xE4\xB8\xAD", latin1, nullptr).c_str());
  // invalid bytes -> '?'
  TEST_ASSERT_EQUAL_STRING("a?b", sanitize("a\xFF" "b", latin1, nullptr).c_str());
  // truncated sequence at the end
  TEST_ASSERT_EQUAL_STRING("ab?", sanitize("ab\xC3", latin1, nullptr).c_str());
  // overlong encoding of '/'
  TEST_ASSERT_EQUAL_STRING("?", sanitize("\xC0\xAF", latin1, nullptr).c_str());
  // control chars -> space, trimmed
  TEST_ASSERT_EQUAL_STRING("a b", sanitize("\ta\nb\r", latin1, nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("", sanitize("", latin1, nullptr).c_str());
  // em dash U+2014 is not in the fake font
  TEST_ASSERT_EQUAL_STRING("a ? b", sanitize("a \xE2\x80\x94 b", latin1, nullptr).c_str());
}

void test_fit_width() {
  TEST_ASSERT_EQUAL_STRING("Curto", fitWidth("Curto", 100, cpCount, nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("0123456789", fitWidth("0123456789", 100, cpCount, nullptr).c_str());
  // 11 chars in 100 px -> 9 chars + "…"
  TEST_ASSERT_EQUAL_STRING("012345678\xE2\x80\xA6", fitWidth("0123456789A", 100, cpCount, nullptr).c_str());
  // multibyte prefix is never split
  TEST_ASSERT_EQUAL_STRING("ççç\xE2\x80\xA6", fitWidth("çççççç", 40, cpCount, nullptr).c_str());
  // a trailing blank before the ellipsis is trimmed
  TEST_ASSERT_EQUAL_STRING("ab\xE2\x80\xA6", fitWidth("ab cdef", 40, cpCount, nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("\xE2\x80\xA6", fitWidth("abc", 15, cpCount, nullptr).c_str());
  TEST_ASSERT_EQUAL_STRING("", fitWidth("abc", 5, cpCount, nullptr).c_str());
}

void test_wrap() {
  auto l = wrap("Abre este endereço no browser", 150, 2, cpCount, nullptr);
  TEST_ASSERT_EQUAL_INT(2, (int)l.size());
  TEST_ASSERT_EQUAL_STRING("Abre este", l[0].c_str());
  TEST_ASSERT_EQUAL_STRING(std::string("endereço no br").append(kEllipsis).c_str(), l[1].c_str());  // rest truncated
  l = wrap("Abre este endereço no browser", 400, 2, cpCount, nullptr);
  TEST_ASSERT_EQUAL_INT(1, (int)l.size());
  l = wrap("um dois tres quatro cinco", 60, 2, cpCount, nullptr);
  TEST_ASSERT_EQUAL_INT(2, (int)l.size());
  TEST_ASSERT_TRUE(cpCount(l[1], nullptr) <= 60);
  l = wrap("", 60, 2, cpCount, nullptr);
  TEST_ASSERT_EQUAL_INT(0, (int)l.size());
}

// ---- decisions ----------------------------------------------------------------
void test_volume_step() {
  TEST_ASSERT_EQUAL_UINT8(55, stepVolume(50, 5));
  TEST_ASSERT_EQUAL_UINT8(100, stepVolume(98, 5));
  TEST_ASSERT_EQUAL_UINT8(0, stepVolume(3, -5));
}

void test_play_pause_action() {
  const bool h[4] = {false, true, false, true};
  const bool none[4] = {false, false, false, false};
  TEST_ASSERT_TRUE(playPauseAction(true, false, 2, h).kind == Action::Kind::Pause);
  TEST_ASSERT_TRUE(playPauseAction(false, true, 2, h).kind == Action::Kind::Resume);
  Action a = playPauseAction(false, false, 0, h);
  TEST_ASSERT_TRUE(a.kind == Action::Kind::Play);
  TEST_ASSERT_EQUAL_INT(2, a.slot);
  a = playPauseAction(false, false, 4, h);
  TEST_ASSERT_EQUAL_INT(4, a.slot);
  a = playPauseAction(false, false, 1, h);  // current slot was deleted
  TEST_ASSERT_EQUAL_INT(2, a.slot);
  TEST_ASSERT_TRUE(playPauseAction(false, false, 0, none).kind == Action::Kind::None);
}

void test_slot_tap_action() {
  const bool h[4] = {true, true, false, true};
  TEST_ASSERT_TRUE(slotTapAction(3, false, false, 0, h).kind == Action::Kind::None);  // empty
  Action a = slotTapAction(2, true, false, 1, h);
  TEST_ASSERT_TRUE(a.kind == Action::Kind::Play);
  TEST_ASSERT_EQUAL_INT(2, a.slot);
  TEST_ASSERT_TRUE(slotTapAction(1, true, false, 1, h).kind == Action::Kind::Pause);
  TEST_ASSERT_TRUE(slotTapAction(1, false, true, 1, h).kind == Action::Kind::Resume);
  a = slotTapAction(1, false, false, 1, h);  // idle on the current slot: play again
  TEST_ASSERT_TRUE(a.kind == Action::Kind::Play);
  TEST_ASSERT_TRUE(slotTapAction(0, false, false, 0, h).kind == Action::Kind::None);
  TEST_ASSERT_TRUE(slotTapAction(5, false, false, 0, h).kind == Action::Kind::None);
}

void test_bt_status_and_list() {
  TEST_ASSERT_EQUAL_STRING("BT: Desligado", btStatusText(false, "Redmi Buds 6 Active").c_str());
  TEST_ASSERT_EQUAL_STRING("BT: Redmi Buds 6 Active", btStatusText(true, "Redmi Buds 6 Active").c_str());
  auto l = btListNames({"A", "", " ", "B", "A", " C "});
  TEST_ASSERT_EQUAL_INT(3, (int)l.size());
  TEST_ASSERT_EQUAL_STRING("A", l[0].c_str());
  TEST_ASSERT_EQUAL_STRING("B", l[1].c_str());
  TEST_ASSERT_EQUAL_STRING("C", l[2].c_str());
}

void test_boot_mode() {
  TEST_ASSERT_TRUE(bootIntoProgram(true, true));     // reboot after POST /wifi
  TEST_ASSERT_FALSE(bootIntoProgram(true, false));   // power-cycle while in Programação
  TEST_ASSERT_FALSE(bootIntoProgram(false, true));
  TEST_ASSERT_FALSE(bootIntoProgram(false, false));
}

// ---- touch tracker ------------------------------------------------------------
void test_touch_tap_once_per_press() {
  TouchTracker t(15000);
  TEST_ASSERT_TRUE(t.update(0, false, 0, 0) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.update(100, true, 10, 20) == TouchTracker::Event::Tap);
  TEST_ASSERT_EQUAL_INT(10, t.tapX());
  TEST_ASSERT_EQUAL_INT(20, t.tapY());
  TEST_ASSERT_TRUE(t.update(120, true, 50, 50) == TouchTracker::Event::None);  // held / dragged
  TEST_ASSERT_TRUE(t.update(140, false, 0, 0) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.update(200, true, 30, 40) == TouchTracker::Event::Tap);
}

void test_touch_sleep_and_wake_swallowed() {
  TouchTracker t(15000);
  t.update(0, false, 0, 0);
  TEST_ASSERT_TRUE(t.update(14999, false, 0, 0) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.update(15000, false, 0, 0) == TouchTracker::Event::Sleep);
  TEST_ASSERT_FALSE(t.screenOn());
  TEST_ASSERT_TRUE(t.update(16000, false, 0, 0) == TouchTracker::Event::None);  // only once
  TEST_ASSERT_TRUE(t.update(20000, true, 100, 100) == TouchTracker::Event::Wake);
  TEST_ASSERT_TRUE(t.screenOn());
  TEST_ASSERT_TRUE(t.update(20100, true, 100, 100) == TouchTracker::Event::None);  // swallowed
  TEST_ASSERT_TRUE(t.update(20200, false, 0, 0) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.update(20300, true, 5, 5) == TouchTracker::Event::Tap);       // next press works
}

void test_touch_hold_keeps_screen_on() {
  TouchTracker t(15000);
  t.update(0, true, 1, 1);
  TEST_ASSERT_TRUE(t.update(30000, true, 1, 1) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.update(30001, false, 0, 0) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.screenOn());
}

void test_touch_button_wake_and_wraparound() {
  TouchTracker t(15000);
  t.activity(0xFFFFF000u);
  TEST_ASSERT_TRUE(t.update(0xFFFFF000u, false, 0, 0) == TouchTracker::Event::None);
  TEST_ASSERT_TRUE(t.update(0x00001000u, false, 0, 0) == TouchTracker::Event::None);  // 8 s across wrap
  TEST_ASSERT_TRUE(t.update(0x00003000u + 15000, false, 0, 0) == TouchTracker::Event::Sleep);
  t.wake(100000);
  TEST_ASSERT_TRUE(t.screenOn());
  TEST_ASSERT_TRUE(t.update(100001, true, 7, 8) == TouchTracker::Event::Tap);
}

int runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_play_rects_inside_and_disjoint);
  RUN_TEST(test_play_controls_at_least_40px);
  RUN_TEST(test_play_targets_cover_whole_screen);
  RUN_TEST(test_hash_mix);
  RUN_TEST(test_play_hit_points);
  RUN_TEST(test_bt_layout_and_hits);
  RUN_TEST(test_program_button);
  RUN_TEST(test_paging);
  RUN_TEST(test_sanitize_cases);
  RUN_TEST(test_fit_width);
  RUN_TEST(test_wrap);
  RUN_TEST(test_volume_step);
  RUN_TEST(test_play_pause_action);
  RUN_TEST(test_slot_tap_action);
  RUN_TEST(test_bt_status_and_list);
  RUN_TEST(test_boot_mode);
  RUN_TEST(test_touch_tap_once_per_press);
  RUN_TEST(test_touch_sleep_and_wake_swallowed);
  RUN_TEST(test_touch_hold_keeps_screen_on);
  RUN_TEST(test_touch_button_wake_and_wraparound);
  return UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() {
  delay(2000);
  runAll();
}
void loop() {}
#else
int main() { return runAll(); }
#endif
