#pragma once
// Pure UI helpers (no Arduino / TFT dependency, unit-testable natively and on
// the watch): screen layout + hit-testing, list paging, display-text
// sanitising/truncation, transport decisions, touch/backlight tracking and the
// boot-mode decision.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace uilogic {

constexpr int kScreenW = 240;
constexpr int kScreenH = 240;

struct Rect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

// ---- Layout rules -----------------------------------------------------------
// Every touch target is >= 50 px high, >= 8 px from the screen edges and >= 8 px
// from its neighbours (checked by test_ui for every screen).
constexpr int kMargin = 8;
constexpr int kGap = 8;
constexpr int kMinTarget = 50;

// ---- Views of Reprodução ------------------------------------------------------
// Player (main) <-> Músicas / Menu; Menu -> Bluetooth. "Voltar" always returns
// to Player, and so does every return from Programação.
enum class View { Player, Songs, Menu, Bluetooth };

// ---- Player -------------------------------------------------------------------
// y   0.. 39  two text lines (not targets): the Bluetooth state ("nome" / "A ligar..." /
//              "Sem auscultadores"), then the title "▶ N · nome" / "Parado" / "Nenhuma música"
// y  40..115  ⏮ (64x70, centred) | ⏯ (80x76) | ⏭ (64x70)
// y 124..173  − (64x50) | "Vol NN" label | + (64x50)
// y 182..231  Músicas (108x50) | Menu (108x50)
constexpr int kTitleH = 40;
enum class PlayerHit { None, Prev, PlayPause, Next, VolDown, VolUp, Songs, Menu };
constexpr int kPlayerTargets = 7;  // Prev..Menu
Rect playerRect(PlayerHit h);      // {0,0,0,0} for None
Rect playerVolumeLabel();          // between − and + (not a target)
PlayerHit playerHitTest(int x, int y);

// ---- Músicas --------------------------------------------------------------------
// 2 x 2 tiles of 108x79 (slots 1,2 on the first row, 3,4 on the second; x 8/124, y 8/95) and a
// full-width "Voltar" bar 224x50 at y 182.
enum class SongsHit { None, Slot1, Slot2, Slot3, Slot4, Back };
constexpr int kSongsTargets = 5;
Rect songsRect(SongsHit h);
SongsHit songsHitTest(int x, int y);
inline int slotOf(SongsHit h) {
  return (h >= SongsHit::Slot1 && h <= SongsHit::Slot4) ? (int)h - (int)SongsHit::Slot1 + 1 : 0;
}

// ---- Menu -----------------------------------------------------------------------
// 2 x 2 buttons of 108x108: Bluetooth | Repetir ; Programar | Voltar
enum class MenuHit { None, Bluetooth, Loop, Program, Back };
constexpr int kMenuTargets = 4;
Rect menuRect(MenuHit h);
MenuHit menuHitTest(int x, int y);

// ---- Bluetooth screen -------------------------------------------------------
// No title row (the scan state is on the Parar/Procurar button and in the
// empty-list text) so that 3 device rows of 50 px fit:
// y   8..173  3 device rows 224x50 (gaps 8)
// y 182..231  Parar/Procurar | Mais | Voltar (69x50 each)
constexpr int kBtRowH = 50;
constexpr int kBtRowsPerPage = 3;
constexpr int kBtButtonsY = 182;
enum class BtButton { Scan = 0, More = 1, Back = 2 };
Rect btRowRect(int row);           // row 0..2
Rect btButtonRect(BtButton b);
struct BtHit {
  enum class Kind { None, Row, Scan, More, Back } kind;
  int row;  // valid for Kind::Row
};
// rowsShown: device rows drawn on this page; moreShown: "Mais" visible.
BtHit btHitTest(int x, int y, int rowsShown, bool moreShown);

// ---- Programação screen -----------------------------------------------------
Rect programBackRect();  // "Reprodução" button, 224x50 at y 182
bool programHitTest(int x, int y);

// ---- Navigation -------------------------------------------------------------
struct Action;
View afterPlayerTap(PlayerHit h);
// a: what the tap did to the player (slotTapAction): a new track returns to
// Player; pause/resume of the current track or an empty slot stays.
View afterSongsTap(SongsHit h, const Action& a);
View afterMenuTap(MenuHit h);
View afterBtTap(BtHit::Kind k);
constexpr View kViewOnEnterPlay = View::Player;

// ---- Paging -----------------------------------------------------------------
int pageCount(int items, int perPage);           // >= 1
int clampPage(int page, int items, int perPage);  // into [0, pageCount)
int nextPage(int page, int items, int perPage);   // cyclic
int pageFirst(int page, int perPage);
int pageRows(int page, int items, int perPage);   // items on that page

// ---- Text -------------------------------------------------------------------
using GlyphFn = bool (*)(uint32_t codepoint, void* ctx);
using WidthFn = int (*)(const std::string& utf8, void* ctx);
extern const char* const kEllipsis;  // U+2026 in UTF-8

// Decodes UTF-8; invalid/overlong sequences, codepoints > 0xFFFF and codepoints
// the font lacks (hasGlyph false) become '?'; control chars become ' '.
// Leading/trailing blanks are trimmed.
std::string sanitize(const std::string& utf8, GlyphFn hasGlyph, void* ctx);
// s if it fits in maxW px, else the longest prefix (whole codepoints) + "…".
std::string fitWidth(const std::string& s, int maxW, WidthFn width, void* ctx);
// Word wrap into at most maxLines lines; the last line is fitWidth()-truncated.
std::vector<std::string> wrap(const std::string& s, int maxW, int maxLines, WidthFn width, void* ctx);

// ---- Player decisions -------------------------------------------------------
uint8_t stepVolume(uint8_t v, int delta);  // clamped 0..100
struct Action {
  enum class Kind { None, Play, Pause, Resume } kind;
  int slot;  // for Play
};

// Player title: icon + text. "N · nome" for the current slot (Play icon while
// playing, Pause icon while paused); "Parado" when stopped/idle;
// "Nenhuma música" when every slot is empty.
struct Title {
  enum class Icon { None, Play, Pause } icon;
  std::string text;
};
Title playerTitle(int current, bool playing, bool paused, const bool has[4], const std::string& currentName);
int firstNonEmpty(const bool has[4]);  // 1..4, 0 if none
// ⏯: playing -> Pause; paused -> Resume; idle -> Play(current if non-empty,
// else first non-empty); nothing to play -> None.
Action playPauseAction(bool playing, bool paused, int current, const bool has[4]);
// Slot row tap: empty slot -> None; the current slot while playing/paused ->
// toggle (Pause/Resume); anything else -> Play(slot).
Action slotTapAction(int slot, bool playing, bool paused, int current, const bool has[4]);

std::string btStatusText(bool connected, const std::string& name);  // "BT: <name>" / "BT: Desligado"

// Bluetooth state line at the top of the Player. `pending`: a device was tapped and the link is not up
// yet; `hasSaved` + `sinceEnterMs`: a saved device is being reconnected automatically after entering
// Reprodução (shown as connecting for kBtAutoConnectMs, then as off).
enum class BtState { Connected, Connecting, Off };
constexpr uint32_t kBtAutoConnectMs = 25000;
BtState btState(bool connected, bool pending, bool hasSaved, uint32_t sinceEnterMs);
std::string btStatusLine(BtState s, const std::string& name);

// FNV-1a style mixing, for cheap "did anything shown change?" signatures.
inline uint32_t hashMix(uint32_t h, uint32_t v) {
  for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (8 * i)) & 0xFF)) * 16777619u;
  return h;
}
constexpr uint32_t kHashSeed = 2166136261u;
// Drops empty names and duplicates (first occurrence kept, order preserved).
std::vector<std::string> btListNames(const std::vector<std::string>& raw);

// After POST /wifi the watch reboots with ESP.restart() (software reset) while
// the Programação flag is set: go back to Programação. A power-cycle (or any
// other reset) boots into Reprodução.
bool bootIntoProgram(bool programFlag, bool softwareReset);

// ---- Touch + backlight ------------------------------------------------------
// Feed update() with the raw touch state every loop. A press while the screen
// is off only wakes it (Event::Wake) and is swallowed until release. A press
// while on gives one Event::Tap (at the press position); nothing more until
// the finger is lifted. Event::Sleep once, after timeoutMs without touch.
class TouchTracker {
 public:
  enum class Event { None, Tap, Wake, Sleep };
  explicit TouchTracker(uint32_t timeoutMs = 15000) : timeout_(timeoutMs) {}
  Event update(uint32_t now, bool down, int x, int y);
  void wake(uint32_t now);   // external wake (side button)
  void sleep() { on_ = false; }  // external blank (side button)
  void activity(uint32_t now) { last_ = now; }
  bool screenOn() const { return on_; }
  bool down() const { return down_; }  // a press is in progress (for pressed-state feedback)
  int tapX() const { return tx_; }
  int tapY() const { return ty_; }

 private:
  uint32_t timeout_;
  uint32_t last_ = 0;
  bool on_ = true;
  bool down_ = false;
  int tx_ = 0, ty_ = 0;
};

// ---- Transfer progress ------------------------------------------------------
// "2,1 MB" (one decimal, comma); below 100 KB: "48 KB" (whole KB, rounded down).
std::string formatMB(uint32_t bytes);
int percent(uint32_t received, uint32_t total);  // 0..100; 0 when total is 0
// Seconds left at the average rate so far; -1 when it cannot be known yet.
int etaSeconds(uint32_t received, uint32_t total, uint32_t elapsedMs);

}  // namespace uilogic
