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

// ---- Reprodução screen ------------------------------------------------------
// y   0.. 19  status "BT: ..." + volume (not a touch target)
// y  20..159  4 slot rows, 35 px each, full width
// y 160..199  ⏮ ⏯ ⏭ − +  (48 x 40 each)
// y 200..239  loop toggle | Bluetooth | Programar (80 x 40 each)
constexpr int kStatusH = 20;
constexpr int kSlotRowH = 35;
constexpr int kCtrlRowH = 40;
constexpr int kTransportY = kStatusH + 4 * kSlotRowH;  // 160
constexpr int kBottomY = kTransportY + kCtrlRowH;      // 200

enum class PlayHit { None, Slot1, Slot2, Slot3, Slot4, Prev, PlayPause, Next, VolDown, VolUp, Loop, Bluetooth, Program };
constexpr int kPlayTargets = 12;  // Slot1..Program
Rect playRect(PlayHit h);         // {0,0,0,0} for None
PlayHit playHitTest(int x, int y);
inline int slotOf(PlayHit h) {
  return (h >= PlayHit::Slot1 && h <= PlayHit::Slot4) ? (int)h - (int)PlayHit::Slot1 + 1 : 0;
}

// ---- Bluetooth screen -------------------------------------------------------
// y   0.. 31  title + scan state
// y  32..191  4 device rows, 40 px each
// y 192..239  Parar/Procurar | Mais | Voltar (80 x 48 each)
constexpr int kBtHeaderH = 32;
constexpr int kBtRowH = 40;
constexpr int kBtRowsPerPage = 4;
constexpr int kBtButtonsY = kBtHeaderH + kBtRowsPerPage * kBtRowH;  // 192
enum class BtButton { Scan = 0, More = 1, Back = 2 };
Rect btRowRect(int row);           // row 0..3
Rect btButtonRect(BtButton b);
struct BtHit {
  enum class Kind { None, Row, Scan, More, Back } kind;
  int row;  // valid for Kind::Row
};
// rowsShown: device rows drawn on this page; moreShown: "Mais" visible.
BtHit btHitTest(int x, int y, int rowsShown, bool moreShown);

// ---- Programação screen -----------------------------------------------------
Rect programBackRect();  // "Reprodução" button, y 184..239 full width
bool programHitTest(int x, int y);

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
int firstNonEmpty(const bool has[4]);  // 1..4, 0 if none
// ⏯: playing -> Pause; paused -> Resume; idle -> Play(current if non-empty,
// else first non-empty); nothing to play -> None.
Action playPauseAction(bool playing, bool paused, int current, const bool has[4]);
// Slot row tap: empty slot -> None; the current slot while playing/paused ->
// toggle (Pause/Resume); anything else -> Play(slot).
Action slotTapAction(int slot, bool playing, bool paused, int current, const bool has[4]);

std::string btStatusText(bool connected, const std::string& name);  // "BT: <name>" / "BT: Desligado"
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
  int tapX() const { return tx_; }
  int tapY() const { return ty_; }

 private:
  uint32_t timeout_;
  uint32_t last_ = 0;
  bool on_ = true;
  bool down_ = false;
  int tx_ = 0, ty_ = 0;
};

}  // namespace uilogic
