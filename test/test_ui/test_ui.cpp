// Pure UI logic tests. Run natively (pio test -e native -f test_ui) or on the
// watch (pio test -e watch_test -f test_ui).
#include <unity.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "ui_logic.h"
#include "bt_logic.h"

using namespace uilogic;

void setUp() {}
void tearDown() {}

// ---- layout -------------------------------------------------------------------
static bool overlap(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
// Distance between two rects along the axis where they do not overlap; for
// rects that share a row or a column this is the visible gap between them.
static int gapBetween(const Rect& a, const Rect& b) {
  int dx = std::max(b.x - (a.x + a.w), a.x - (b.x + b.w));
  int dy = std::max(b.y - (a.y + a.h), a.y - (b.y + b.h));
  return std::max(dx, dy);
}

// Every target: >= kMinTarget px high (and wide), >= kMargin from every edge,
// no overlaps, >= kGap from every other target.
static void checkScreen(const std::vector<Rect>& rs, const char* name) {
  char msg[96];
  for (size_t i = 0; i < rs.size(); ++i) {
    const Rect& a = rs[i];
    snprintf(msg, sizeof msg, "%s target %u", name, (unsigned)i);
    TEST_ASSERT_TRUE_MESSAGE(a.h >= kMinTarget && a.w >= kMinTarget, msg);
    TEST_ASSERT_TRUE_MESSAGE(a.x >= kMargin && a.y >= kMargin, msg);
    TEST_ASSERT_TRUE_MESSAGE(a.x + a.w <= kScreenW - kMargin && a.y + a.h <= kScreenH - kMargin, msg);
    for (size_t j = i + 1; j < rs.size(); ++j) {
      snprintf(msg, sizeof msg, "%s targets %u/%u", name, (unsigned)i, (unsigned)j);
      TEST_ASSERT_FALSE_MESSAGE(overlap(a, rs[j]), msg);
      TEST_ASSERT_TRUE_MESSAGE(gapBetween(a, rs[j]) >= kGap, msg);
    }
  }
}

void test_layout_player() {
  std::vector<Rect> rs;
  for (int i = 1; i <= kPlayerTargets; ++i) rs.push_back(playerRect((PlayerHit)i));
  checkScreen(rs, "player");
  // ⏯ is the biggest transport button; volume label sits between − and +
  Rect pp = playerRect(PlayerHit::PlayPause), pv = playerRect(PlayerHit::Prev);
  TEST_ASSERT_TRUE(pp.h > pv.h && pp.w > pv.w);
  Rect vl = playerVolumeLabel();
  TEST_ASSERT_TRUE(vl.x >= playerRect(PlayerHit::VolDown).x + playerRect(PlayerHit::VolDown).w);
  TEST_ASSERT_TRUE(vl.x + vl.w <= playerRect(PlayerHit::VolUp).x);
  TEST_ASSERT_TRUE(playerRect(PlayerHit::VolDown).h >= 50 && playerRect(PlayerHit::Songs).h >= 50);
  TEST_ASSERT_EQUAL_INT(40, kTitleH);  // two lines: Bluetooth state + now playing
  TEST_ASSERT_TRUE(playerRect(PlayerHit::Prev).y >= kTitleH && pp.y >= kTitleH);  // below the title
}

void test_layout_songs_menu_bt_program() {
  std::vector<Rect> rs;
  for (int i = 1; i <= kSongsTargets; ++i) rs.push_back(songsRect((SongsHit)i));
  checkScreen(rs, "songs");
  rs.clear();
  for (int i = 1; i <= kMenuTargets; ++i) rs.push_back(menuRect((MenuHit)i));
  checkScreen(rs, "menu");
  rs.clear();
  for (int k = 0; k < kBtRowsPerPage; ++k) rs.push_back(btRowRect(k));
  for (int b = 0; b < 3; ++b) rs.push_back(btButtonRect((BtButton)b));
  checkScreen(rs, "bluetooth");
  rs.clear();
  rs.push_back(programBackRect());
  checkScreen(rs, "program");
}

void test_player_hits() {
  TEST_ASSERT_TRUE(playerHitTest(120, 10) == PlayerHit::None);   // title (Bluetooth line)
  TEST_ASSERT_TRUE(playerHitTest(120, 39) == PlayerHit::None);   // last title row
  TEST_ASSERT_TRUE(playerHitTest(8, 43) == PlayerHit::Prev);     // top-left corner
  TEST_ASSERT_TRUE(playerHitTest(7, 60) == PlayerHit::None);     // margin
  TEST_ASSERT_TRUE(playerHitTest(71, 112) == PlayerHit::Prev);   // bottom-right corner
  TEST_ASSERT_TRUE(playerHitTest(75, 60) == PlayerHit::None);    // gap
  TEST_ASSERT_TRUE(playerHitTest(80, 40) == PlayerHit::PlayPause);
  TEST_ASSERT_TRUE(playerHitTest(159, 115) == PlayerHit::PlayPause);
  TEST_ASSERT_TRUE(playerHitTest(160, 60) == PlayerHit::None);
  TEST_ASSERT_TRUE(playerHitTest(200, 78) == PlayerHit::Next);
  TEST_ASSERT_TRUE(playerHitTest(40, 150) == PlayerHit::VolDown);
  TEST_ASSERT_TRUE(playerHitTest(120, 150) == PlayerHit::None);  // "Vol NN" label
  TEST_ASSERT_TRUE(playerHitTest(200, 150) == PlayerHit::VolUp);
  TEST_ASSERT_TRUE(playerHitTest(60, 206) == PlayerHit::Songs);
  TEST_ASSERT_TRUE(playerHitTest(120, 206) == PlayerHit::None);  // gap 116..123
  TEST_ASSERT_TRUE(playerHitTest(180, 206) == PlayerHit::Menu);
  TEST_ASSERT_TRUE(playerHitTest(231, 231) == PlayerHit::Menu);
  TEST_ASSERT_TRUE(playerHitTest(232, 232) == PlayerHit::None);
  TEST_ASSERT_TRUE(playerHitTest(-1, -1) == PlayerHit::None);
}

// Músicas: 2 x 2 tiles (108x79) and a full-width "Voltar" bar at the bottom.
void test_songs_tiles() {
  Rect s1 = songsRect(SongsHit::Slot1), s2 = songsRect(SongsHit::Slot2);
  Rect s3 = songsRect(SongsHit::Slot3), s4 = songsRect(SongsHit::Slot4), b = songsRect(SongsHit::Back);
  TEST_ASSERT_TRUE(s1.x == 8 && s1.y == 8 && s1.w == 108 && s1.h == 79);
  TEST_ASSERT_TRUE(s2.x == 124 && s2.y == 8 && s3.x == 8 && s3.y == 95 && s4.x == 124 && s4.y == 95);
  TEST_ASSERT_TRUE(b.x == 8 && b.y == 182 && b.w == 224 && b.h == 50);
  TEST_ASSERT_TRUE(songsHitTest(8, 8) == SongsHit::Slot1);
  TEST_ASSERT_TRUE(songsHitTest(115, 86) == SongsHit::Slot1);
  TEST_ASSERT_TRUE(songsHitTest(116, 50) == SongsHit::None);   // gap between columns
  TEST_ASSERT_TRUE(songsHitTest(124, 8) == SongsHit::Slot2);
  TEST_ASSERT_TRUE(songsHitTest(231, 86) == SongsHit::Slot2);
  TEST_ASSERT_TRUE(songsHitTest(60, 90) == SongsHit::None);    // gap between rows
  TEST_ASSERT_TRUE(songsHitTest(8, 95) == SongsHit::Slot3);
  TEST_ASSERT_TRUE(songsHitTest(115, 173) == SongsHit::Slot3);
  TEST_ASSERT_TRUE(songsHitTest(124, 95) == SongsHit::Slot4);
  TEST_ASSERT_TRUE(songsHitTest(231, 173) == SongsHit::Slot4);
  TEST_ASSERT_TRUE(songsHitTest(100, 178) == SongsHit::None);  // gap above Voltar
  TEST_ASSERT_TRUE(songsHitTest(8, 182) == SongsHit::Back);
  TEST_ASSERT_TRUE(songsHitTest(231, 231) == SongsHit::Back);
  TEST_ASSERT_EQUAL_INT(3, slotOf(SongsHit::Slot3));
  TEST_ASSERT_EQUAL_INT(0, slotOf(SongsHit::Back));
}

// Bluetooth state shown at the top of the Player.
void test_bt_state_line() {
  TEST_ASSERT_TRUE(btState(true, false, true, 99999) == BtState::Connected);
  TEST_ASSERT_TRUE(btState(true, true, false, 0) == BtState::Connected);        // link wins over a pending tap
  TEST_ASSERT_TRUE(btState(false, true, false, 99999) == BtState::Connecting);  // device tapped, waiting
  TEST_ASSERT_TRUE(btState(false, false, true, 5000) == BtState::Connecting);   // saved device, just entered Reprodução
  TEST_ASSERT_TRUE(btState(false, false, true, 24999) == BtState::Connecting);
  TEST_ASSERT_TRUE(btState(false, false, true, 25000) == BtState::Off);         // gave up waiting
  TEST_ASSERT_TRUE(btState(false, false, false, 0) == BtState::Off);            // nothing saved
  TEST_ASSERT_EQUAL_STRING("Redmi Buds 6 Active", btStatusLine(BtState::Connected, "Redmi Buds 6 Active").c_str());
  TEST_ASSERT_EQUAL_STRING("Auscultadores ligados", btStatusLine(BtState::Connected, "").c_str());
  TEST_ASSERT_EQUAL_STRING("A ligar...", btStatusLine(BtState::Connecting, "x").c_str());
  TEST_ASSERT_EQUAL_STRING("Sem auscultadores", btStatusLine(BtState::Off, "x").c_str());
}

void test_songs_menu_bt_hits() {
  TEST_ASSERT_TRUE(menuHitTest(60, 60) == MenuHit::Bluetooth);
  TEST_ASSERT_TRUE(menuHitTest(180, 60) == MenuHit::Loop);
  TEST_ASSERT_TRUE(menuHitTest(60, 180) == MenuHit::Program);
  TEST_ASSERT_TRUE(menuHitTest(180, 180) == MenuHit::Back);
  TEST_ASSERT_TRUE(menuHitTest(120, 120) == MenuHit::None);  // centre gap

  BtHit h = btHitTest(100, 8, 3, false);
  TEST_ASSERT_TRUE(h.kind == BtHit::Kind::Row);
  TEST_ASSERT_EQUAL_INT(0, h.row);
  h = btHitTest(100, 173, 3, false);
  TEST_ASSERT_TRUE(h.kind == BtHit::Kind::Row);
  TEST_ASSERT_EQUAL_INT(2, h.row);
  TEST_ASSERT_TRUE(btHitTest(100, 173, 2, false).kind == BtHit::Kind::None);  // row not shown
  TEST_ASSERT_TRUE(btHitTest(100, 62, 3, false).kind == BtHit::Kind::None);   // gap
  TEST_ASSERT_TRUE(btHitTest(20, 200, 0, false).kind == BtHit::Kind::Scan);
  TEST_ASSERT_TRUE(btHitTest(120, 200, 0, false).kind == BtHit::Kind::None);  // Mais hidden
  TEST_ASSERT_TRUE(btHitTest(120, 200, 0, true).kind == BtHit::Kind::More);
  TEST_ASSERT_TRUE(btHitTest(230, 231, 0, true).kind == BtHit::Kind::Back);

  TEST_ASSERT_TRUE(programHitTest(120, 200));
  TEST_ASSERT_FALSE(programHitTest(120, 100));
}

void test_navigation() {
  const Action none{Action::Kind::None, 0}, play{Action::Kind::Play, 2}, pause{Action::Kind::Pause, 0},
      resume{Action::Kind::Resume, 0};
  TEST_ASSERT_TRUE(afterPlayerTap(PlayerHit::Songs) == View::Songs);
  TEST_ASSERT_TRUE(afterPlayerTap(PlayerHit::Menu) == View::Menu);
  TEST_ASSERT_TRUE(afterPlayerTap(PlayerHit::PlayPause) == View::Player);
  TEST_ASSERT_TRUE(afterPlayerTap(PlayerHit::VolUp) == View::Player);
  TEST_ASSERT_TRUE(afterSongsTap(SongsHit::Slot2, play) == View::Player);    // new track
  TEST_ASSERT_TRUE(afterSongsTap(SongsHit::Slot1, pause) == View::Songs);    // current: toggle, stay
  TEST_ASSERT_TRUE(afterSongsTap(SongsHit::Slot1, resume) == View::Songs);
  TEST_ASSERT_TRUE(afterSongsTap(SongsHit::Slot3, none) == View::Songs);     // empty
  TEST_ASSERT_TRUE(afterSongsTap(SongsHit::Back, none) == View::Player);
  TEST_ASSERT_TRUE(afterSongsTap(SongsHit::None, none) == View::Songs);
  TEST_ASSERT_TRUE(afterMenuTap(MenuHit::Bluetooth) == View::Bluetooth);
  TEST_ASSERT_TRUE(afterMenuTap(MenuHit::Loop) == View::Menu);
  TEST_ASSERT_TRUE(afterMenuTap(MenuHit::Program) == View::Menu);
  TEST_ASSERT_TRUE(afterMenuTap(MenuHit::Back) == View::Player);
  TEST_ASSERT_TRUE(afterBtTap(BtHit::Kind::Back) == View::Player);
  TEST_ASSERT_TRUE(afterBtTap(BtHit::Kind::Scan) == View::Bluetooth);
  TEST_ASSERT_TRUE(afterBtTap(BtHit::Kind::Row) == View::Bluetooth);
  TEST_ASSERT_TRUE(kViewOnEnterPlay == View::Player);
}

void test_player_title() {
  const bool has[4] = {true, false, true, false};
  const bool none[4] = {false, false, false, false};
  Title t = playerTitle(1, true, false, has, "Fado");
  TEST_ASSERT_TRUE(t.icon == Title::Icon::Play);
  TEST_ASSERT_EQUAL_STRING("1 \xC2\xB7 Fado", t.text.c_str());
  t = playerTitle(3, false, true, has, "X");
  TEST_ASSERT_TRUE(t.icon == Title::Icon::Pause);
  TEST_ASSERT_EQUAL_STRING("3 \xC2\xB7 X", t.text.c_str());
  t = playerTitle(3, false, false, has, "X");
  TEST_ASSERT_TRUE(t.icon == Title::Icon::None);
  TEST_ASSERT_EQUAL_STRING("Parado", t.text.c_str());
  t = playerTitle(0, false, false, none, "");
  TEST_ASSERT_EQUAL_STRING("Nenhuma m\xC3\xBAsica", t.text.c_str());
}

void test_hash_mix() {
  uint32_t a = hashMix(hashMix(kHashSeed, 1), 2), b = hashMix(hashMix(kHashSeed, 2), 1);
  TEST_ASSERT_TRUE(a != b);
  TEST_ASSERT_EQUAL_UINT32(a, hashMix(hashMix(kHashSeed, 1), 2));
  TEST_ASSERT_TRUE(hashMix(kHashSeed, 0) != hashMix(kHashSeed, 256));
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

// ---- Bluetooth device list (lib/btlogic) -----------------------------------
static const uint8_t kA[6] = {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x01};
static const uint8_t kB[6] = {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x02};
static const uint8_t kC[6] = {0xaa, 0xbb, 0xcc, 0x00, 0x00, 0x03};

void test_bt_cod_filter() {
  using namespace btlogic;
  TEST_ASSERT_TRUE(isListedCod(0x240404));   // headset, audio+rendering (typical earbuds)
  TEST_ASSERT_TRUE(isListedCod(0x000418));   // headphones, NO service bits: AV major is enough
  TEST_ASSERT_TRUE(isListedCod(0x000414));   // loudspeaker, no service bits
  TEST_ASSERT_TRUE(isListedCod(0x0C043C));   // Samsung TV seen nearby (video display+loudspeaker)
  TEST_ASSERT_TRUE(isListedCod(0x5A020C));   // phone with telephony/audio (library listed these too)
  TEST_ASSERT_FALSE(isListedCod(0x000104));  // computer, no audio service
  TEST_ASSERT_FALSE(isListedCod(0));         // unknown COD
  TEST_ASSERT_FALSE(isListedCod(0x240405));  // invalid format bits
  TEST_ASSERT_EQUAL_STRING("Audio/Video", majorName(0x240404));
  TEST_ASSERT_EQUAL_STRING("headset", avMinorName(0x240404));
  TEST_ASSERT_EQUAL_STRING("headphones", avMinorName(0x000418));
  TEST_ASSERT_EQUAL_STRING("rendering,audio", serviceNames(0x240404).c_str());
  TEST_ASSERT_EQUAL_STRING("aa:bb:cc:00:00:01", addrText(kA).c_str());
}

void test_bt_list_names_late_and_unnamed() {
  btlogic::DeviceList l;
  TEST_ASSERT_TRUE(l.upsert(kA, "", -60));            // unnamed: shown as its address
  TEST_ASSERT_EQUAL_STRING("aa:bb:cc:00:00:01", l.names()[0].c_str());
  TEST_ASSERT_FALSE(l.upsert(kA, "", -50));           // repeat result: no change
  TEST_ASSERT_TRUE(l.upsert(kA, "Redmi Buds 6 Active", -50));  // name arrives later (EIR/RNR)
  TEST_ASSERT_EQUAL_STRING("Redmi Buds 6 Active", l.names()[0].c_str());
  TEST_ASSERT_FALSE(l.upsert(kA, "", -40));           // empty never erases a known name
  TEST_ASSERT_EQUAL_STRING("Redmi Buds 6 Active", l.names()[0].c_str());
  TEST_ASSERT_TRUE(l.upsert(kB, "TV", -70));
  TEST_ASSERT_EQUAL(2, (int)l.size());
  TEST_ASSERT_TRUE(l.findByDisplay("TV") != nullptr);
  TEST_ASSERT_TRUE(l.findByDisplay("aa:bb:cc:00:00:01") == nullptr);  // now named
  TEST_ASSERT_TRUE(l.findByAddr(kC) == nullptr);
  btlogic::DeviceList small(2);
  small.upsert(kA, "a", 0);
  small.upsert(kB, "b", 0);
  TEST_ASSERT_FALSE(small.upsert(kC, "c", 0));  // capped
}

void test_bt_list_cleared_on_new_search() {
  btlogic::DeviceList l;
  l.upsert(kA, "Buds", -50);
  l.upsert(kB, "TV", -70);
  l.upsert(kC, "", -80);
  // new search while connected to B: only B stays (so it can show 'ligado')
  TEST_ASSERT_TRUE(l.clearExcept(kB));
  TEST_ASSERT_EQUAL(1, (int)l.size());
  TEST_ASSERT_EQUAL_STRING("TV", l.names()[0].c_str());
  // new search with nothing connected: empty
  TEST_ASSERT_TRUE(l.clearExcept(nullptr));
  TEST_ASSERT_EQUAL(0, (int)l.size());
  TEST_ASSERT_FALSE(l.clearExcept(nullptr));
  // repopulates, connected peer first
  l.upsert(kB, "TV", 0);
  l.upsert(kA, "Buds", 0);
  std::vector<std::string> ui = uilogic::btListNames(l.names());
  TEST_ASSERT_EQUAL(2, (int)ui.size());
  TEST_ASSERT_EQUAL_STRING("TV", ui[0].c_str());
}

void test_bt_search_policy() {
  using btlogic::SearchTrigger;
  // not connected: continuous search with name requests (screen and button)
  for (SearchTrigger t : {SearchTrigger::EnterScreen, SearchTrigger::Button}) {
    btlogic::SearchPolicy p = btlogic::searchPolicy(false, t);
    TEST_ASSERT_TRUE(p.start);
    TEST_ASSERT_TRUE(p.continuous);
    TEST_ASSERT_TRUE(p.nameRequests);
    TEST_ASSERT_FALSE(p.singleShot);
  }
  // connected, screen opened: no search at all
  btlogic::SearchPolicy p = btlogic::searchPolicy(true, SearchTrigger::EnterScreen);
  TEST_ASSERT_FALSE(p.start);
  TEST_ASSERT_FALSE(p.continuous);
  TEST_ASSERT_FALSE(p.nameRequests);
  // connected, "Procurar": one window, no name requests
  p = btlogic::searchPolicy(true, SearchTrigger::Button);
  TEST_ASSERT_TRUE(p.start);
  TEST_ASSERT_FALSE(p.continuous);
  TEST_ASSERT_FALSE(p.nameRequests);
  TEST_ASSERT_TRUE(p.singleShot);
}

void test_bt_name_requests_once_per_address() {
  btlogic::NameRequests q(2, 3);
  uint8_t out[6];
  TEST_ASSERT_TRUE(q.want(kA));
  TEST_ASSERT_FALSE(q.want(kA));  // already queued
  TEST_ASSERT_TRUE(q.next(out));
  TEST_ASSERT_EQUAL_MEMORY(kA, out, 6);
  // the request for A failed; A shows up unnamed again on the next inquiry:
  TEST_ASSERT_FALSE(q.want(kA));  // never asked twice in one search
  TEST_ASSERT_TRUE(q.want(kB));
  TEST_ASSERT_TRUE(q.want(kC));
  uint8_t kD[6] = {1, 2, 3, 4, 5, 6};
  TEST_ASSERT_FALSE(q.want(kD));  // queue cap (2) / total cap (3)
  TEST_ASSERT_TRUE(q.next(out));
  TEST_ASSERT_TRUE(q.next(out));
  TEST_ASSERT_FALSE(q.next(out));
  TEST_ASSERT_EQUAL(3, (int)q.asked());
  TEST_ASSERT_FALSE(q.want(kD));  // total cap: at most 3 attempts per search
  q.reset();                      // new search
  TEST_ASSERT_TRUE(q.want(kA));
}

void test_bt_connect_target_by_name_or_address() {
  btlogic::DeviceList l;
  l.upsert(kA, "", -50);
  TEST_ASSERT_TRUE(l.findTarget("aa:bb:cc:00:00:01") != nullptr);  // tapped while unnamed
  l.upsert(kA, "Buds", -50);                                        // name arrived meanwhile
  TEST_ASSERT_TRUE(l.findTarget("aa:bb:cc:00:00:01") != nullptr);  // still connectable
  TEST_ASSERT_TRUE(l.findTarget("Buds") != nullptr);
  TEST_ASSERT_TRUE(l.findTarget("Other") == nullptr);
  TEST_ASSERT_TRUE(l.findTarget("") == nullptr);
}

void test_transfer_helpers() {
  TEST_ASSERT_EQUAL_STRING("2,1 MB", uilogic::formatMB(2200000).c_str());
  TEST_ASSERT_EQUAL_STRING("4,3 MB", uilogic::formatMB(4430u * 1024u).c_str());
  TEST_ASSERT_EQUAL_STRING("48 KB", uilogic::formatMB(50000).c_str());
  TEST_ASSERT_EQUAL_INT(0, uilogic::percent(0, 0));
  TEST_ASSERT_EQUAL_INT(50, uilogic::percent(1, 2));
  TEST_ASSERT_EQUAL_INT(100, uilogic::percent(300, 200));
  TEST_ASSERT_EQUAL_INT(-1, uilogic::etaSeconds(0, 100, 1000));
  TEST_ASSERT_EQUAL_INT(10, uilogic::etaSeconds(50, 100, 10000));
  TEST_ASSERT_EQUAL_INT(0, uilogic::etaSeconds(100, 100, 10000));
  TEST_ASSERT_EQUAL_INT(-1, uilogic::etaSeconds(1, 4000000000u, 4000000000u));  // > 1 day: unknown
}

int runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_layout_player);
  RUN_TEST(test_layout_songs_menu_bt_program);
  RUN_TEST(test_player_hits);
  RUN_TEST(test_songs_tiles); RUN_TEST(test_bt_state_line);
  RUN_TEST(test_songs_menu_bt_hits);
  RUN_TEST(test_navigation);
  RUN_TEST(test_player_title);
  RUN_TEST(test_hash_mix);
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
  RUN_TEST(test_bt_cod_filter);
  RUN_TEST(test_bt_list_names_late_and_unnamed);
  RUN_TEST(test_bt_list_cleared_on_new_search);
  RUN_TEST(test_bt_search_policy);
  RUN_TEST(test_bt_name_requests_once_per_address);
  RUN_TEST(test_bt_connect_target_by_name_or_address);
  RUN_TEST(test_transfer_helpers);
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
