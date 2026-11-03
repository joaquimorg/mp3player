#include "ui.h"

#include <LilyGoWatch.h>
#include <string>
#include <vector>

#include "bt.h"
#include "fonts/font_l.h"
#include "fonts/font_m.h"
#include "fonts/font_s.h"
#include "player.h"
#include "settings.h"
#include "storage.h"
#include "transfer.h"
#include "ui_logic.h"
#ifdef NET_DIAG
#include <esp_wifi.h>
#include "netmode.h"
#endif

namespace ui {
namespace {

using uilogic::Rect;
using uilogic::View;

// ---- look -------------------------------------------------------------------
constexpr uint8_t kBrightness = 110;  // of 255: modest, readable indoors
constexpr uint32_t kBlankMs = 15000;
constexpr uint32_t kPollMs = 150;      // model refresh while the screen is on
constexpr uint32_t kVolSaveMs = 2000;  // volume is written to NVS after this idle time
constexpr int kVolStep = 5;
constexpr uint32_t kTouchRetryMs = 5000;  // re-probe an absent touch controller
constexpr uint32_t kTouchHintMs = 5000;   // on-screen "touch unavailable" hint

constexpr uint16_t C_BG = 0x0000;
constexpr uint16_t C_TEXT = 0xFFFF;
constexpr uint16_t C_DIM = 0x8410;
constexpr uint16_t C_BTN = 0x2124;      // dark grey buttons
constexpr uint16_t C_BTN_HI = 0x2BFA;   // blue (primary buttons)
constexpr uint16_t C_PRESS = 0x54BF;    // light blue: button held down
constexpr uint16_t C_ROW_A = 0x0861;
constexpr uint16_t C_ROW_B = 0x10A2;
constexpr uint16_t C_ROW_CUR = 0x1148;  // dark blue: current slot
constexpr uint16_t C_ROW_OK = 0x0B04;   // dark green: connected device
constexpr uint16_t C_SCAN = 0x6A40;     // dark amber: scan running
constexpr uint16_t C_GREEN = 0x5E4F;
constexpr uint16_t C_YELLOW = 0xFEA0;
constexpr uint16_t C_CYAN = 0x5DDF;
constexpr uint16_t C_WARN = 0x8000;     // dark red banner
constexpr uint16_t C_ERR = 0xFAAA;      // light red text on black (transfer failed)
constexpr uint16_t C_BAR = 0x2BFA;      // progress bar fill

// Transfer overlay (any screen): receiving while transfer::active(), then a banner.
constexpr uint32_t kDoneBannerMs = 4000;
constexpr uint32_t kFailBannerMs = 6000;
constexpr uint32_t kOverlayRedrawMs = 500;  // at most 2 redraws per second while receiving

// ---- state ------------------------------------------------------------------
enum class Screen { Busy, Player, Songs, Menu, Bluetooth, Program };
const char* screenName(Screen s) {
  switch (s) {
    case Screen::Busy: return "Ocupado";
    case Screen::Player: return "Player";
    case Screen::Songs: return "Musicas";
    case Screen::Menu: return "Menu";
    case Screen::Bluetooth: return "Bluetooth";
    default: return "Programacao";
  }
}

TTGOClass* s_w = nullptr;
Host s_host{};
TFT_eSprite* s_spr = nullptr;
TFT_eSPI* g = nullptr;  // drawing target: the sprite, or the panel if no PSRAM
uilogic::TouchTracker s_touch(kBlankMs);

Screen s_screen = Screen::Busy;
View s_view = View::Player;  // view of Reprodução (when not busy / not in Programação)
bool s_dirty = true;
uint32_t s_lastPoll = 0;
uint32_t s_lastBtn = 0;
uint32_t s_lastSig = 0;

// Transfer overlay bookkeeping: what was drawn last (0: normal screens).
enum class Overlay : uint8_t { None, Receiving, Done, Failed };
Overlay s_ov = Overlay::None;
uint32_t s_ovVer = 0;
uint32_t s_ovAt = 0;

// Button held down (pressed-state feedback): id is the screen's hit enum value.
Screen s_pressScreen = Screen::Busy;
int s_pressId = 0;

// Slot cache: files only change in Programação, so this is reloaded on every
// entry to Reprodução (not polled: slotName() reads /names.json).
bool s_has[4] = {false, false, false, false};
String s_names[4];
slotlogic::LoopMode s_loop = slotlogic::LoopMode::RepeatOne;

bool s_volDirty = false;
uint32_t s_volAt = 0;

// Bluetooth peer name, re-read only when the link state or the connect count
// changes (bt::connectedName() takes a lock and builds a String).
bool s_btConn = false;
uint32_t s_btConnects = 0xFFFFFFFF;
String s_btName;

// Bluetooth screen
int s_btPage = 0;
String s_pending;  // device tapped, waiting for the link
uint32_t s_pendingAt = 0;
uint32_t s_pendingVer = 0;               // bumped when s_pending changes
uint32_t s_devVer = 0xFFFFFFFF;          // bt::devicesVersion() of s_btNames
std::vector<std::string> s_btNames;      // filtered device list (all pages)
std::vector<std::string> s_btList;       // rows as drawn (for hit -> name)

// Touch controller presence (see readTouch()).
bool s_touchOk = true;
uint32_t s_touchProbeAt = 0;
uint32_t s_touchHintUntil = 0;

#ifdef UI_DEBUG
}  // namespace
}  // namespace ui
extern volatile bool g_debugStallSwitch;  // main.cpp
namespace ui {
namespace {
String s_desc;  // text description of the last drawn frame
String s_line;
int s_inject = 0;
int16_t s_injX = 0, s_injY = 0;
uint32_t s_fontLoads = 0;
#define DESC(...)                          \
  do {                                     \
    char _b[160];                          \
    snprintf(_b, sizeof _b, __VA_ARGS__);  \
    s_desc += _b;                          \
    s_desc += '\n';                        \
  } while (0)
#else
#define DESC(...) \
  do {            \
  } while (0)
#endif

// ---- fonts / text -----------------------------------------------------------
// Loading a smooth font parses its glyph table, so text is not drawn as it is
// declared: queueText() records it and flushText() draws it grouped by font,
// starting with the font that is already loaded (at most one switch per font
// used by the frame, i.e. 1-2 per redraw).
enum class Font { None, S, M, L };
Font s_font = Font::None;

void useFont(Font f) {
  if (f == s_font) return;
  if (s_font != Font::None) g->unloadFont();
  g->loadFont(f == Font::S ? kFontS : f == Font::M ? kFontM : kFontL);
  s_font = f;
#ifdef UI_DEBUG
  ++s_fontLoads;
#endif
}

bool hasGlyph(uint32_t cp, void*) {
  uint16_t idx;
  return g->getUnicodeIndex((uint16_t)cp, &idx);
}
int widthOf(const std::string& s, void*) { return g->textWidth(s.c_str()); }

// Sanitised (unrenderable -> '?') and truncated with "…" to maxW px, in the current font.
String fit(const String& s, int maxW) {
  std::string t = uilogic::sanitize(s.c_str(), hasGlyph, nullptr);
  return String(uilogic::fitWidth(t, maxW, widthOf, nullptr).c_str());
}

struct TextOp {
  Font font;
  String s;
  int16_t x, y, maxW;  // maxW > 0: sanitise + truncate to that width
  uint8_t datum;
  uint16_t fg, bg;
  const char* tag;  // debug description label (nullptr: not described)
};
constexpr int kMaxOps = 32;
TextOp s_ops[kMaxOps];
int s_nOps = 0;

void queueText(Font f, const String& s, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg, int maxW = 0,
               const char* tag = nullptr) {
  if (s_nOps >= kMaxOps) return;
  s_ops[s_nOps++] = TextOp{f, s, (int16_t)x, (int16_t)y, (int16_t)maxW, datum, fg, bg, tag};
}

void flushText() {
  Font order[4] = {s_font, Font::S, Font::M, Font::L};
  for (int k = 0; k < 4; ++k) {
    Font f = order[k];
    if (f == Font::None || (k > 0 && f == order[0])) continue;
    bool loaded = false;
    for (int i = 0; i < s_nOps; ++i) {
      TextOp& op = s_ops[i];
      if (op.font != f) continue;
      if (!loaded) useFont(f), loaded = true;
      String shown = op.maxW > 0 ? fit(op.s, op.maxW) : op.s;
      g->setTextDatum(op.datum);
      g->setTextColor(op.fg, op.bg);
      g->drawString(shown, op.x, op.y);
      if (op.tag) DESC("%s '%s'", op.tag, shown.c_str());
    }
  }
  for (int i = 0; i < s_nOps; ++i) s_ops[i].s = String();  // release the strings
  s_nOps = 0;
}

// Copies one rectangle of the sprite to the panel (TFT_eSPI 2.x here has no
// windowed pushSprite): used to show a pressed/released button without
// pushing the whole 240x240 frame.
void pushRect(const Rect& r) {
  if (!s_spr) return;
  uint16_t* fb = (uint16_t*)s_spr->frameBuffer(1);
  bool old = s_w->tft->getSwapBytes();
  s_w->tft->setSwapBytes(false);
  for (int row = 0; row < r.h; ++row)
    s_w->tft->pushImage(r.x, r.y + row, r.w, 1, fb + (r.y + row) * uilogic::kScreenW + r.x);
  s_w->tft->setSwapBytes(old);
}

int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

// Clears the rect to the background, then draws a rounded button filling it.
void button(const Rect& r, uint16_t color) {
  g->fillRect(r.x, r.y, r.w, r.h, C_BG);
  g->fillRoundRect(r.x, r.y, r.w, r.h, 9, color);
}

bool isPressed(Screen s, int id) { return s_pressScreen == s && s_pressId == id; }

// ---- icons ------------------------------------------------------------------
void iconPlay(int x, int y, int s, uint16_t c) { g->fillTriangle(x - s / 2, y - s, x - s / 2, y + s, x + s, y, c); }
void iconPause(int x, int y, int s, uint16_t c) {
  g->fillRect(x - s, y - s, s * 2 / 3 + 1, 2 * s + 1, c);
  g->fillRect(x + s / 3, y - s, s * 2 / 3 + 1, 2 * s + 1, c);
}
void iconPrev(int x, int y, uint16_t c) {
  g->fillRect(x - 14, y - 12, 4, 25, c);
  g->fillTriangle(x + 12, y - 12, x + 12, y + 12, x - 9, y, c);
}
void iconNext(int x, int y, uint16_t c) {
  g->fillRect(x + 11, y - 12, 4, 25, c);
  g->fillTriangle(x - 12, y - 12, x - 12, y + 12, x + 9, y, c);
}

// ---- shared player state ------------------------------------------------------
void refreshBtName() {
  bool c = bt::connected();
  uint32_t n = bt::stats().connects;
  if (c == s_btConn && n == s_btConnects) return;
  s_btConn = c;
  s_btConnects = n;
  s_btName = c ? bt::connectedName() : String();
}

uint32_t playerStateHash(uint32_t h) {
  h = uilogic::hashMix(h, (uint32_t)player::currentSlot());
  h = uilogic::hashMix(h, (player::playing() ? 1u : 0u) | (player::paused() ? 2u : 0u) | (s_btConn ? 4u : 0u));
  h = uilogic::hashMix(h, player::volume() | ((uint32_t)s_loop << 8));
  return uilogic::hashMix(h, s_btConnects);
}

void apply(const uilogic::Action& a) {
  switch (a.kind) {
    case uilogic::Action::Kind::Play: player::play(a.slot); break;
    case uilogic::Action::Kind::Pause: player::pause(); break;
    case uilogic::Action::Kind::Resume: player::resume(); break;
    default: break;
  }
}

void saveVolume() {
  s_volDirty = false;
  settings::setVolume(player::volume());
}

// ---- Player (main) ----------------------------------------------------------
void drawPlayerWidget(uilogic::PlayerHit id, bool pressed) {
  using uilogic::PlayerHit;
  Rect r = uilogic::playerRect(id);
  bool playing = player::playing();
  switch (id) {
    case PlayerHit::Prev:
      button(r, pressed ? C_PRESS : C_BTN);
      iconPrev(cx(r), cy(r), C_TEXT);
      break;
    case PlayerHit::PlayPause:
      button(r, pressed ? C_PRESS : C_BTN_HI);
      if (playing) iconPause(cx(r), cy(r), 15, C_TEXT);
      else iconPlay(cx(r) - 2, cy(r), 15, C_TEXT);
      break;
    case PlayerHit::Next:
      button(r, pressed ? C_PRESS : C_BTN);
      iconNext(cx(r), cy(r), C_TEXT);
      break;
    case PlayerHit::VolDown:
      button(r, pressed ? C_PRESS : C_BTN);
      g->fillRect(cx(r) - 12, cy(r) - 2, 24, 5, C_TEXT);
      break;
    case PlayerHit::VolUp:
      button(r, pressed ? C_PRESS : C_BTN);
      g->fillRect(cx(r) - 12, cy(r) - 2, 24, 5, C_TEXT);
      g->fillRect(cx(r) - 2, cy(r) - 12, 5, 24, C_TEXT);
      break;
    case PlayerHit::Songs: {
      uint16_t c = pressed ? C_PRESS : C_BTN;
      button(r, c);
      queueText(Font::M, "Músicas", cx(r), cy(r), MC_DATUM, C_TEXT, c, r.w - 8, "button");
      break;
    }
    case PlayerHit::Menu: {
      uint16_t c = pressed ? C_PRESS : C_BTN;
      button(r, c);
      queueText(Font::M, "Menu", cx(r), cy(r), MC_DATUM, C_TEXT, c, r.w - 8, "button");
      break;
    }
    default: break;
  }
}

void drawPlayer() {
  using uilogic::PlayerHit;
  int cur = player::currentSlot();
  bool playing = player::playing(), paused = player::paused();
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);

  // title: ▶/❚❚ icon + "N · nome" / "Parado" / "Nenhuma música"
  std::string curName = (cur >= 1 && cur <= 4) ? std::string(s_names[cur - 1].c_str()) : std::string();
  uilogic::Title t = uilogic::playerTitle(cur, playing, paused, s_has, curName);
  int tx = 8;
  if (t.icon == uilogic::Title::Icon::Play) iconPlay(15, 14, 7, C_GREEN), tx = 28;
  if (t.icon == uilogic::Title::Icon::Pause) iconPause(15, 14, 7, C_YELLOW), tx = 28;
  queueText(Font::M, t.text.c_str(), tx, 14, ML_DATUM, t.icon == uilogic::Title::Icon::None ? C_DIM : C_TEXT, C_BG,
            232 - tx, "title");
  DESC("title icon %s", t.icon == uilogic::Title::Icon::Play    ? "play"
                        : t.icon == uilogic::Title::Icon::Pause ? "pause"
                                                                 : "none");

  for (int i = 1; i <= uilogic::kPlayerTargets; ++i)
    drawPlayerWidget((PlayerHit)i, isPressed(Screen::Player, i));
  Rect vl = uilogic::playerVolumeLabel();
  queueText(Font::M, "Vol " + String(player::volume()), cx(vl), cy(vl), MC_DATUM, C_CYAN, C_BG, 0, "volume");
  DESC("buttons [prev] [%s] [next] [-] [+]", playing ? "pause" : "play");
}

void tapPlayer(uilogic::PlayerHit h, uint32_t now) {
  using uilogic::PlayerHit;
  int cur = player::currentSlot();
  bool playing = player::playing(), paused = player::paused();
  switch (h) {
    case PlayerHit::PlayPause: apply(uilogic::playPauseAction(playing, paused, cur, s_has)); break;
    case PlayerHit::Prev: player::prev(); break;
    case PlayerHit::Next: player::next(); break;
    case PlayerHit::VolDown:
    case PlayerHit::VolUp:
      player::setVolume(uilogic::stepVolume(player::volume(), h == PlayerHit::VolUp ? kVolStep : -kVolStep));
      s_volDirty = true;
      s_volAt = now;
      break;
    default: break;
  }
  s_view = uilogic::afterPlayerTap(h);
}

// ---- Músicas ----------------------------------------------------------------
void drawSongsWidget(uilogic::SongsHit id, bool pressed) {
  using uilogic::SongsHit;
  Rect r = uilogic::songsRect(id);
  if (id == SongsHit::Back) {
    uint16_t c = pressed ? C_PRESS : C_BTN_HI;
    button(r, c);
    g->fillTriangle(cx(r) + 7, cy(r) - 30, cx(r) + 7, cy(r) - 14, cx(r) - 7, cy(r) - 22, C_TEXT);
    queueText(Font::S, "Voltar", cx(r), cy(r) + 8, MC_DATUM, C_TEXT, c, r.w - 4, "button");
    return;
  }
  int slot = uilogic::slotOf(id);
  if (!slot) return;
  int cur = player::currentSlot();
  bool playing = player::playing(), paused = player::paused();
  bool isCur = cur == slot && (playing || paused);
  uint16_t bg = pressed ? C_PRESS : isCur ? C_ROW_CUR : (slot % 2 ? C_ROW_A : C_ROW_B);
  button(r, bg);
  if (isCur) iconPlay(r.x + 12, cy(r), 7, playing ? C_GREEN : C_YELLOW);
  queueText(Font::S, String(slot), r.x + 26, cy(r), ML_DATUM, C_DIM, bg);
  static char tags[4][32];
  snprintf(tags[slot - 1], sizeof tags[slot - 1], "slot %d%s", slot,
           !isCur ? "" : playing ? " > (a tocar)" : " > (pausa)");
  if (s_has[slot - 1])
    queueText(Font::M, s_names[slot - 1], r.x + 40, cy(r), ML_DATUM, C_TEXT, bg, r.w - 40 - 6, tags[slot - 1]);
  else
    queueText(Font::M, "Vazio", r.x + 40, cy(r), ML_DATUM, C_DIM, bg, 0, tags[slot - 1]);
}

void drawSongs() {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  for (int i = 1; i <= uilogic::kSongsTargets; ++i)
    drawSongsWidget((uilogic::SongsHit)i, isPressed(Screen::Songs, i));
}

void tapSongs(uilogic::SongsHit h) {
  uilogic::Action a{uilogic::Action::Kind::None, 0};
  int slot = uilogic::slotOf(h);
  if (slot) {
    a = uilogic::slotTapAction(slot, player::playing(), player::paused(), player::currentSlot(), s_has);
    apply(a);
  }
  s_view = uilogic::afterSongsTap(h, a);
}

// ---- Menu -------------------------------------------------------------------
void drawMenuWidget(uilogic::MenuHit id, bool pressed) {
  using uilogic::MenuHit;
  Rect r = uilogic::menuRect(id);
  uint16_t c = pressed ? C_PRESS : id == MenuHit::Back ? C_BTN_HI : C_BTN;
  button(r, c);
  switch (id) {
    case MenuHit::Bluetooth:
      queueText(Font::M, "Bluetooth", cx(r), cy(r) - 12, MC_DATUM, C_TEXT, c, r.w - 8, "button");
      queueText(Font::S, s_btConn ? "BT: " + s_btName : String("desligado"), cx(r), cy(r) + 14, MC_DATUM,
                s_btConn ? C_CYAN : C_DIM, c, r.w - 8, "bluetooth state");
      break;
    case MenuHit::Loop:
      queueText(Font::M, "Repetir", cx(r), cy(r) - 12, MC_DATUM, C_TEXT, c, r.w - 8);
      queueText(Font::M, s_loop == slotlogic::LoopMode::AllSlots ? "todas" : "faixa", cx(r), cy(r) + 14, MC_DATUM,
                C_CYAN, c, r.w - 8, "loop button Repetir");
      break;
    case MenuHit::Program:
      queueText(Font::M, "Programar", cx(r), cy(r), MC_DATUM, C_TEXT, c, r.w - 6, "button");
      break;
    case MenuHit::Back:
      queueText(Font::M, "Voltar", cx(r), cy(r), MC_DATUM, C_TEXT, c, r.w - 8, "button");
      break;
    default: break;
  }
}

void drawMenu() {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  for (int i = 1; i <= uilogic::kMenuTargets; ++i) drawMenuWidget((uilogic::MenuHit)i, isPressed(Screen::Menu, i));
}

void setPending(const String& name, uint32_t now) {
  s_pending = name;
  s_pendingAt = now;
  ++s_pendingVer;
}

void tapMenu(uilogic::MenuHit h, uint32_t now) {
  using uilogic::MenuHit;
  switch (h) {
    case MenuHit::Loop:
      s_loop = s_loop == slotlogic::LoopMode::AllSlots ? slotlogic::LoopMode::RepeatOne
                                                       : slotlogic::LoopMode::AllSlots;
      settings::setLoopMode(s_loop);
      break;
    case MenuHit::Bluetooth:
      s_btPage = 0;
      setPending("", now);
      bt::screenOpened();  // fresh list; searches only when nothing is connected
      break;
    case MenuHit::Program:
      if (s_volDirty) saveVolume();  // the player is about to be torn down
      s_host.requestMode(Mode::Program);
      break;
    default: break;
  }
  s_view = uilogic::afterMenuTap(h);
}

// ---- Bluetooth --------------------------------------------------------------
uint32_t sigBt(uint32_t now) {
  uint32_t v = bt::devicesVersion();
  if (v != s_devVer) {  // rebuild the list only when bt reports a change
    s_devVer = v;
    std::vector<std::string> raw;
    for (auto& n : bt::devices()) raw.push_back(n.c_str());
    s_btNames = uilogic::btListNames(raw);
  }
  if (s_pending.length() && s_btConn && s_btName == s_pending) setPending("", now);
  if (s_pending.length() && now - s_pendingAt > 30000) setPending("", now);  // gave up
  s_btPage = uilogic::clampPage(s_btPage, (int)s_btNames.size(), uilogic::kBtRowsPerPage);
  uint32_t h = uilogic::hashMix(uilogic::kHashSeed, 2);
  h = uilogic::hashMix(h, v);
  h = uilogic::hashMix(h, (bt::scanning() ? 1u : 0u) | (s_btConn ? 2u : 0u) | ((uint32_t)s_btPage << 8));
  h = uilogic::hashMix(h, s_btConnects);
  return uilogic::hashMix(h, s_pendingVer);
}

// Widget ids on the Bluetooth screen: 1..3 = rows, 11 = Scan, 12 = More, 13 = Back.
constexpr int kBtIdRow0 = 1, kBtIdScan = 11, kBtIdMore = 12, kBtIdBack = 13;

void drawBtWidget(int id, bool pressed) {
  if (id >= kBtIdRow0 && id < kBtIdRow0 + uilogic::kBtRowsPerPage) {
    int k = id - kBtIdRow0;
    if (k >= (int)s_btList.size()) return;
    Rect rr = uilogic::btRowRect(k);
    String name = s_btList[k].c_str();
    bool isConn = s_btConn && name == s_btName;
    bool isPend = !isConn && s_pending.length() && name == s_pending;
    uint16_t bg = pressed ? C_PRESS : isConn ? C_ROW_OK : (k % 2 ? C_ROW_B : C_ROW_A);
    button(rr, bg);
    const char* tag = isConn ? "ligado" : isPend ? "a ligar\xE2\x80\xA6" : "";
    static char tags[uilogic::kBtRowsPerPage][24];
    snprintf(tags[k], sizeof tags[k], "row %d%s", k, isConn ? " (ligado)" : isPend ? " (a ligar)" : "");
    queueText(Font::M, name, rr.x + 8, cy(rr), ML_DATUM, C_TEXT, bg, tag[0] ? rr.w - 16 - 66 : rr.w - 16, tags[k]);
    if (tag[0]) queueText(Font::S, tag, rr.x + rr.w - 8, cy(rr), MR_DATUM, isConn ? C_GREEN : C_YELLOW, bg);
    return;
  }
  bool scanning = bt::scanning();
  int pages = uilogic::pageCount((int)s_btNames.size(), uilogic::kBtRowsPerPage);
  if (id == kBtIdScan) {
    Rect r = uilogic::btButtonRect(uilogic::BtButton::Scan);
    uint16_t c = pressed ? C_PRESS : scanning ? C_SCAN : C_BTN;
    button(r, c);
    queueText(Font::S, scanning ? "Parar" : "Procurar", cx(r), cy(r), MC_DATUM, C_TEXT, c, r.w - 4, "button");
  } else if (id == kBtIdMore && pages > 1) {
    Rect r = uilogic::btButtonRect(uilogic::BtButton::More);
    uint16_t c = pressed ? C_PRESS : C_BTN;
    button(r, c);
    queueText(Font::S, "Mais", cx(r), cy(r) - 8, MC_DATUM, C_TEXT, c, r.w - 4, "button");
    queueText(Font::S, String(s_btPage + 1) + "/" + String(pages), cx(r), cy(r) + 10, MC_DATUM, C_DIM, c);
  } else if (id == kBtIdBack) {
    Rect r = uilogic::btButtonRect(uilogic::BtButton::Back);
    uint16_t c = pressed ? C_PRESS : C_BTN_HI;
    button(r, c);
    queueText(Font::S, "Voltar", cx(r), cy(r), MC_DATUM, C_TEXT, c, r.w - 4, "button");
  }
}

void drawBt() {
  bool scanning = bt::scanning();
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  int n = (int)s_btNames.size();
  int pages = uilogic::pageCount(n, uilogic::kBtRowsPerPage);
  int first = uilogic::pageFirst(s_btPage, uilogic::kBtRowsPerPage);
  int rows = uilogic::pageRows(s_btPage, n, uilogic::kBtRowsPerPage);
  s_btList.assign(s_btNames.begin() + first, s_btNames.begin() + first + rows);
  for (int k = 0; k < rows; ++k) drawBtWidget(kBtIdRow0 + k, isPressed(Screen::Bluetooth, kBtIdRow0 + k));
  if (n == 0) {
    queueText(Font::M, "Bluetooth", 120, 30, MC_DATUM, C_TEXT, C_BG, 0, "title");
    queueText(Font::S, scanning ? "A procurar dispositivos\xE2\x80\xA6" : "Nenhum dispositivo encontrado.", 120, 75,
              MC_DATUM, scanning ? C_YELLOW : C_DIM, C_BG, 224, "empty");
    queueText(Font::S, "Põe o auricular ou a coluna", 120, 115, MC_DATUM, C_DIM, C_BG, 224);
    queueText(Font::S, "em modo de emparelhamento.", 120, 135, MC_DATUM, C_DIM, C_BG, 224);
  }
  for (int id : {kBtIdScan, kBtIdMore, kBtIdBack}) drawBtWidget(id, isPressed(Screen::Bluetooth, id));
  DESC("state %s, page %d/%d of %d devices", scanning ? "a procurar" : "parado", s_btPage + 1, pages, n);
}

int btWidgetAt(int x, int y) {
  int total = (int)s_btNames.size();
  bool more = uilogic::pageCount(total, uilogic::kBtRowsPerPage) > 1;
  uilogic::BtHit h = uilogic::btHitTest(x, y, (int)s_btList.size(), more);
  switch (h.kind) {
    case uilogic::BtHit::Kind::Row: return kBtIdRow0 + h.row;
    case uilogic::BtHit::Kind::Scan: return kBtIdScan;
    case uilogic::BtHit::Kind::More: return kBtIdMore;
    case uilogic::BtHit::Kind::Back: return kBtIdBack;
    default: return 0;
  }
}

Rect btWidgetRect(int id) {
  if (id >= kBtIdRow0 && id < kBtIdRow0 + uilogic::kBtRowsPerPage) return uilogic::btRowRect(id - kBtIdRow0);
  if (id == kBtIdScan) return uilogic::btButtonRect(uilogic::BtButton::Scan);
  if (id == kBtIdMore) return uilogic::btButtonRect(uilogic::BtButton::More);
  return uilogic::btButtonRect(uilogic::BtButton::Back);
}

void tapBt(int id, uint32_t now) {
  uilogic::BtHit::Kind kind = uilogic::BtHit::Kind::None;
  if (id >= kBtIdRow0 && id < kBtIdRow0 + uilogic::kBtRowsPerPage) {
    kind = uilogic::BtHit::Kind::Row;
    setPending(s_btList[id - kBtIdRow0].c_str(), now);
    Serial.printf("[ui] connect to '%s'\n", s_pending.c_str());
    bt::connectTo(s_pending);
  } else if (id == kBtIdScan) {
    kind = uilogic::BtHit::Kind::Scan;
    if (bt::scanning()) {
      bt::stopScan();
    } else {
      s_btPage = 0;  // a new search starts from an empty list
      bt::startScan();
    }
  } else if (id == kBtIdMore) {
    kind = uilogic::BtHit::Kind::More;
    s_btPage = uilogic::nextPage(s_btPage, (int)s_btNames.size(), uilogic::kBtRowsPerPage);
  } else if (id == kBtIdBack) {
    kind = uilogic::BtHit::Kind::Back;
    bt::stopScan();
  }
  s_view = uilogic::afterBtTap(kind);
}

// ---- Programação ------------------------------------------------------------
uint32_t sigProgram() {
  uint32_t h = uilogic::hashMix(uilogic::kHashSeed, 3);
  h = uilogic::hashMix(h, s_host.ipVersion());
  return uilogic::hashMix(h, s_host.uploading() ? 1u : 0u);
}

void drawProgramButton(bool pressed) {
  Rect b = uilogic::programBackRect();
  uint16_t c = pressed ? C_PRESS : C_BTN_HI;
  button(b, c);
  queueText(Font::M, "Reprodução", cx(b), cy(b), MC_DATUM, C_TEXT, c, 0, "button");
}

void drawProgram() {
  String ip = s_host.ipText();
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  queueText(Font::M, "Programação", 120, 16, MC_DATUM, C_TEXT, C_BG, 0, "title");
  int ap = ip.indexOf(" (");
  bool noLink = ip.startsWith("Sem liga");  // STA link dropped (netmode::ipText)
  if (ip.isEmpty() || noLink || ip.startsWith("A ligar")) {
    queueText(Font::M,
              ip.isEmpty() ? "A iniciar\xE2\x80\xA6" : noLink ? "Sem liga\xC3\xA7\xC3\xA3o Wi-Fi" : "A ligar ao Wi-Fi\xE2\x80\xA6",
              120, 60, MC_DATUM, C_YELLOW, C_BG, 0, "ip");
  } else {
    // 30 px digits: even "255.255.255.255" fits 236 px (truncation is only a safety net).
    queueText(Font::L, ap > 0 ? ip.substring(0, ap) : ip, 120, 60, MC_DATUM, C_CYAN, C_BG, 236, "ip");
    if (ap > 0)  // setup access point: tell which network to join
      queueText(Font::S, "Liga-te à rede Wi-Fi " + ip.substring(ap + 2, ip.length() - 1), 120, 90, MC_DATUM,
                C_TEXT, C_BG, 236, "ap");
  }
  queueText(Font::S, "Abre este endereço no browser", 120, 120, MC_DATUM, C_DIM, C_BG, 236, "hint");
  if (s_host.uploading()) queueText(Font::S, "A receber\xE2\x80\xA6", 120, 152, MC_DATUM, C_YELLOW, C_BG, 0, "upload");
  drawProgramButton(isPressed(Screen::Program, 1));
}

// ---- busy -------------------------------------------------------------------
void drawBusy() {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  queueText(Font::M, "A mudar de modo\xE2\x80\xA6", 120, 110, MC_DATUM, C_TEXT, C_BG, 0, "busy");
  queueText(Font::S, "Aguarda um momento", 120, 140, MC_DATUM, C_DIM, C_BG);
}

// Banner shown for kTouchHintMs when the touch controller is absent.
void drawTouchHint() {
  g->fillRoundRect(6, 80, 228, 70, 8, C_WARN);
  queueText(Font::S, "Toque indisponível \xE2\x80\x94", 120, 103, MC_DATUM, C_TEXT, C_WARN, 220, "touch hint");
  queueText(Font::S, "desliga e liga o relógio", 120, 125, MC_DATUM, C_TEXT, C_WARN, 220, "touch hint");
}

// ---- pressed-state feedback -------------------------------------------------
// Target id under (x, y) on the current screen (0 = none) and its rect.
int widgetAt(Screen s, int x, int y) {
  switch (s) {
    case Screen::Player: return (int)uilogic::playerHitTest(x, y);
    case Screen::Songs: return (int)uilogic::songsHitTest(x, y);
    case Screen::Menu: return (int)uilogic::menuHitTest(x, y);
    case Screen::Bluetooth: return btWidgetAt(x, y);
    case Screen::Program: return uilogic::programHitTest(x, y) ? 1 : 0;
    default: return 0;
  }
}

Rect widgetRect(Screen s, int id) {
  switch (s) {
    case Screen::Player: return uilogic::playerRect((uilogic::PlayerHit)id);
    case Screen::Songs: return uilogic::songsRect((uilogic::SongsHit)id);
    case Screen::Menu: return uilogic::menuRect((uilogic::MenuHit)id);
    case Screen::Bluetooth: return btWidgetRect(id);
    default: return uilogic::programBackRect();
  }
}

// Redraws one button into the sprite and pushes only its rectangle.
void redrawWidget(Screen s, int id, bool pressed) {
  if (!id || s != s_screen) return;
  switch (s) {
    case Screen::Player: drawPlayerWidget((uilogic::PlayerHit)id, pressed); break;
    case Screen::Songs: drawSongsWidget((uilogic::SongsHit)id, pressed); break;
    case Screen::Menu: drawMenuWidget((uilogic::MenuHit)id, pressed); break;
    case Screen::Bluetooth: drawBtWidget(id, pressed); break;
    case Screen::Program: drawProgramButton(pressed); break;
    default: return;
  }
#ifdef UI_DEBUG
  String keep = s_desc;  // a widget redraw must not replace the frame description
#endif
  flushText();
#ifdef UI_DEBUG
  s_desc = keep;
#endif
  pushRect(widgetRect(s, id));
}

// ---- transfer overlay ---------------------------------------------------------
Overlay overlayFor(const transfer::Info& t, uint32_t now) {
  switch (t.state) {
    case transfer::State::Receiving: return Overlay::Receiving;
    case transfer::State::Done: return now - t.endMs < kDoneBannerMs ? Overlay::Done : Overlay::None;
    case transfer::State::Failed: return now - t.endMs < kFailBannerMs ? Overlay::Failed : Overlay::None;
    default: return Overlay::None;
  }
}

void drawOverlay(Overlay ov, const transfer::Info& t, uint32_t now) {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  char b[64];
  if (ov == Overlay::Receiving) {
    queueText(Font::M, "A receber música", 120, 30, MC_DATUM, C_YELLOW, C_BG, 232, "title");
    String who = t.slot > 0 ? String("Slot ") + t.slot + " · " + t.name : String(t.name);
    queueText(Font::S, who, 120, 64, MC_DATUM, C_TEXT, C_BG, 224, "file");
    int pct = uilogic::percent(t.received, t.total);
    g->drawRoundRect(16, 88, 208, 22, 6, C_DIM);
    int fill = 200 * pct / 100;
    if (fill > 0) g->fillRoundRect(20, 92, fill, 14, 4, C_BAR);
    DESC("bar %d%%", pct);
    std::string got = uilogic::formatMB(t.received);
    if (t.total)
      snprintf(b, sizeof b, "%s de %s · %d%%", got.c_str(), uilogic::formatMB(t.total).c_str(), pct);
    else
      snprintf(b, sizeof b, "%s", got.c_str());
    queueText(Font::M, b, 120, 134, MC_DATUM, C_TEXT, C_BG, 232, "progress");
    int eta = uilogic::etaSeconds(t.received, t.total, now - t.startMs);
    if (eta >= 0) {
      if (eta < 120) snprintf(b, sizeof b, "falta cerca de %d s", eta);
      else snprintf(b, sizeof b, "falta cerca de %d min", (eta + 30) / 60);
      queueText(Font::S, b, 120, 164, MC_DATUM, C_DIM, C_BG, 224, "eta");
    }
    queueText(Font::S, "Não desligues o relógio", 120, 206, MC_DATUM, C_YELLOW, C_BG, 232, "warn");
  } else if (ov == Overlay::Done) {
    g->fillRoundRect(6, 70, 228, 100, 8, 0x0B04);
    // check mark (the fonts have no U+2713)
    for (int d = 0; d < 3; ++d) {
      g->drawLine(104, 92 + d, 114, 102 + d, C_TEXT);
      g->drawLine(114, 102 + d, 134, 80 + d, C_TEXT);
    }
    snprintf(b, sizeof b, "Recebido: %s (%s)", t.name, uilogic::formatMB(t.received).c_str());
    useFont(Font::S);
    std::vector<std::string> lines =
        uilogic::wrap(uilogic::sanitize(b, hasGlyph, nullptr), 214, 3, widthOf, nullptr);
    int y = 124 - (int)(lines.size() - 1) * 11;
    for (auto& l : lines) {
      queueText(Font::S, l.c_str(), 120, y, MC_DATUM, C_TEXT, 0x0B04, 0, "banner");
      y += 22;
    }
  } else {
    queueText(Font::M, "Falhou a receção", 120, 28, MC_DATUM, C_ERR, C_BG, 232, "title");
    useFont(Font::S);
    std::vector<std::string> lines = uilogic::wrap(uilogic::sanitize(t.msg, hasGlyph, nullptr), 224, 6, widthOf, nullptr);
    int y = 70;
    for (auto& l : lines) {
      queueText(Font::S, l.c_str(), 120, y, MC_DATUM, C_ERR, C_BG, 0, "error");
      y += 24;
    }
  }
  flushText();
  if (s_spr) s_spr->pushSprite(0, 0);
}

// Returns true when the overlay owns the screen this poll.
bool refreshOverlay(uint32_t now) {
  transfer::Info t = transfer::snapshot();
  Overlay ov = overlayFor(t, now);
  if (ov == Overlay::None) {
    if (s_ov != Overlay::None) {  // banner over: redraw the normal screen
      s_ov = Overlay::None;
      s_dirty = true;
    }
    return false;
  }
  if (ov != s_ov) s_pressId = 0;  // taps are ignored from now on
  if (ov != s_ov || (t.version != s_ovVer && now - s_ovAt >= kOverlayRedrawMs)) {
#ifdef UI_DEBUG
    s_desc = String("screen Transferencia\n");
#endif
    s_ov = ov;
    s_ovVer = t.version;
    s_ovAt = now;
    drawOverlay(ov, t, now);
  }
  return true;
}

// ---- frame ------------------------------------------------------------------
void loadSlots() {
  storage::hasFileAll(s_has);
  for (int i = 0; i < 4; ++i) s_names[i] = s_has[i] ? storage::slotName(i + 1) : String();
  s_loop = settings::loopMode();
}

Screen screenOf(View v) {
  switch (v) {
    case View::Songs: return Screen::Songs;
    case View::Menu: return Screen::Menu;
    case View::Bluetooth: return Screen::Bluetooth;
    default: return Screen::Player;
  }
}

void setScreen(Screen s) {
  if (s == s_screen) return;
  Serial.printf("[ui] screen %s -> %s\n", screenName(s_screen), screenName(s));
  bool wasPlayMode = s_screen == Screen::Player || s_screen == Screen::Songs || s_screen == Screen::Menu ||
                     s_screen == Screen::Bluetooth;
  bool isPlayMode = s == Screen::Player || s == Screen::Songs || s == Screen::Menu || s == Screen::Bluetooth;
  if (isPlayMode && !wasPlayMode) loadSlots();  // fresh from Programação/boot
  if (s == Screen::Bluetooth) s_devVer = 0xFFFFFFFF;  // rebuild the list
  s_pressId = 0;  // a held button belongs to the old screen
  s_screen = s;
  s_dirty = true;
}

void refresh(uint32_t now) {
  if (refreshOverlay(now)) return;
  Screen want;
  if (s_host.busy()) {
    want = Screen::Busy;
    s_view = uilogic::kViewOnEnterPlay;  // land on Player after any mode switch
  } else if (s_host.mode() == Mode::Program) {
    want = Screen::Program;
    s_view = uilogic::kViewOnEnterPlay;
  } else {
    want = screenOf(s_view);
  }
  setScreen(want);

  // Cheap signature of everything shown (no String/vector built unless a
  // list really changed); redraw only when it differs.
  bool hint = (int32_t)(s_touchHintUntil - now) > 0;
  uint32_t sig = uilogic::hashMix(uilogic::kHashSeed, (uint32_t)s_screen);
  switch (s_screen) {
    case Screen::Busy: break;
    case Screen::Player:
    case Screen::Songs:
    case Screen::Menu: refreshBtName(); sig = playerStateHash(sig); break;
    case Screen::Bluetooth: refreshBtName(); sig = uilogic::hashMix(sig, sigBt(now)); break;
    case Screen::Program: sig = uilogic::hashMix(sig, sigProgram()); break;
  }
  sig = uilogic::hashMix(sig, hint ? 1u : 0u);
  if (!s_dirty && sig == s_lastSig) return;
  s_lastSig = sig;
  s_dirty = false;
#ifdef UI_DEBUG
  uint32_t t0 = millis(), loads0 = s_fontLoads;
  s_desc = String("screen ") + screenName(s_screen) + "\n";
#endif
  switch (s_screen) {
    case Screen::Busy: drawBusy(); break;
    case Screen::Player: drawPlayer(); break;
    case Screen::Songs: drawSongs(); break;
    case Screen::Menu: drawMenu(); break;
    case Screen::Bluetooth: drawBt(); break;
    case Screen::Program: drawProgram(); break;
  }
  flushText();
  if (hint) {  // drawn on top of everything
    drawTouchHint();
    flushText();
  }
  if (s_spr) s_spr->pushSprite(0, 0);
#ifdef UI_DEBUG
  Serial.printf("[ui] redraw %s %u ms, %u font loads\n", screenName(s_screen), (unsigned)(millis() - t0),
                (unsigned)(s_fontLoads - loads0));
#endif
}

void onTap(int x, int y, uint32_t now) {
#ifdef UI_DEBUG
  Serial.printf("[ui] tap %d,%d on %s\n", x, y, screenName(s_screen));
#endif
  if (s_host.busy()) return;  // nothing is clickable while switching
  if (s_ov != Overlay::None || transfer::active()) return;  // taps are ignored during a transfer
  int id = widgetAt(s_screen, x, y);
  if (!id) return;
  // Immediate feedback: only this button is redrawn and pushed.
  s_pressScreen = s_screen;
  s_pressId = id;
  redrawWidget(s_screen, id, true);
  switch (s_screen) {
    case Screen::Player: tapPlayer((uilogic::PlayerHit)id, now); break;
    case Screen::Songs: tapSongs((uilogic::SongsHit)id); break;
    case Screen::Menu: tapMenu((uilogic::MenuHit)id, now); break;
    case Screen::Bluetooth: tapBt(id, now); break;
    case Screen::Program: s_host.requestMode(Mode::Play); break;
    default: break;
  }
  s_lastPoll = 0;  // re-check the signature right away
}

void onRelease() {
  if (!s_pressId) return;
  Screen s = s_pressScreen;
  int id = s_pressId;
  s_pressId = 0;
  redrawWidget(s, id, false);
}

void backlight(bool on) {
  if (on) {
    s_w->setBrightness(kBrightness);
    s_w->openBL();
    s_dirty = true;
  } else {
    s_w->closeBL();
  }
  Serial.printf("[ui] backlight %s\n", on ? "on" : "off");
}

// Side button (AXP202 PEK short press): toggles the screen.
void pollButton(uint32_t now) {
  if (now - s_lastBtn < 50) return;
  s_lastBtn = now;
  if (digitalRead(AXP202_INT) != LOW) return;
  s_w->power->readIRQ();
  bool pek = s_w->power->isPEKShortPressIRQ();
  s_w->power->clearIRQ();
  if (!pek) return;
  if (s_touch.screenOn()) {
    s_touch.sleep();
    backlight(false);
  } else {
    s_touch.wake(now);
    backlight(true);
  }
}

// FT6236 touch controller (I2C 0x38 on Wire1). When it does not answer (seen
// with the controller left in hibernate by a previous firmware: on the 2020 V1
// only a full power-off resets it), the library's read blocks for ~1 s. A
// register-pointer write NACKs within ~1 ms, so probe first; while the
// controller is absent, re-attach the driver and re-probe every 5 s only.
constexpr uint8_t kTouchAddr = 0x38;

bool readTouch(int16_t& x, int16_t& y, uint32_t now) {
  if (!s_touchOk) {
    if (now - s_touchProbeAt < kTouchRetryMs) return false;
    s_w->touch->begin(Wire1);  // same as the library's init (sets bus + address)
  }
  s_touchProbeAt = now;
  Wire1.beginTransmission(kTouchAddr);
  Wire1.write(0x02);  // TD_STATUS
  bool ok = Wire1.endTransmission() == 0;
  if (ok != s_touchOk) {
    s_touchOk = ok;
    Serial.println(ok ? "[ui] touch controller answering again"
                      : "[ui] touch controller not answering (turn the watch fully off and on)");
    if (!ok) {
      s_touchHintUntil = now + kTouchHintMs;
      if (!s_touchHintUntil) s_touchHintUntil = 1;
      s_dirty = true;
    }
  }
  return ok && s_w->getTouch(x, y);
}

#ifdef UI_DEBUG
// "soak": a fake A2DP consumer on core 0 (441 frames every 10 ms, like the real
// callback) plus a [stat] line every 10 s. Needs Reprodução with a track playing.
void soakTask(void*) {
  static Frame fr[441];
  uint32_t t0 = millis(), nextStat = t0 + 10000;
  for (;;) {
    player::pullFrames(fr, 441);
    vTaskDelay(pdMS_TO_TICKS(10));
    if ((int32_t)(millis() - nextStat) >= 0) {
      nextStat += 10000;
      player::Stats st = player::stats();
      Serial.printf("[stat] t=%us slot=%d prod=%u pull=%u mp3fr=%u underrun=%u err=%u tracks=%u int=%u psram=%u\n",
                    (unsigned)((millis() - t0) / 1000), player::currentSlot(), (unsigned)st.framesProduced,
                    (unsigned)st.framesPulled, (unsigned)st.mp3Frames, (unsigned)st.underrunFrames,
                    (unsigned)st.decodeErrors, (unsigned)st.tracksStarted,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
  }
}

void debugSerial() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch != '\n' && ch != '\r') {
      if (s_line.length() < 64) s_line += ch;
      continue;
    }
    String c = s_line;
    s_line = "";
    c.trim();
    if (c.startsWith("tap ")) {
      int sp = c.indexOf(' ', 4);
      s_injX = c.substring(4, sp).toInt();
      s_injY = c.substring(sp + 1).toInt();
      s_inject = 1;
    } else if (c == "screen") {
      Serial.printf("[ui] --- %s (backlight %s, busy %d)\n%s[ui] ---\n", screenName(s_screen),
                    s_touch.screenOn() ? "on" : "off", s_host.busy(), s_desc.c_str());
    } else if (c == "heap") {
      Serial.printf("[ui] heap free %u internal %u psram %u\n", (unsigned)ESP.getFreeHeap(),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    } else if (c == "soak") {
      static TaskHandle_t soak = nullptr;
      if (!soak) xTaskCreatePinnedToCore(soakTask, "soak", 4096, nullptr, 1, &soak, 0);
    } else if (c == "stall") {  // the next switch to Programação hangs (tests the 25 s deadline)
      g_debugStallSwitch = true;
    } else if (c == "reboot") {  // software reset, like the one after POST /wifi
      ESP.restart();
    } else if (c == "scan") {    // I2C devices on both buses (touch = 0x38 on bus 1)
      for (int bus = 0; bus < 2; ++bus) {
        TwoWire& w = bus ? Wire1 : Wire;
        String found;
        for (int a = 1; a < 127; ++a) {
          w.beginTransmission(a);
          if (w.endTransmission() == 0) found += " 0x" + String(a, HEX);
        }
        Serial.printf("[ui] i2c bus %d:%s\n", bus, found.c_str());
      }
    } else if (c == "bt") {  // search state + list
      bt::Stats st = bt::stats();
      auto d = bt::devices();
      Serial.printf("[bt] t=%us search %s, connected %d '%s', name requests %u, %u devices:",
                    (unsigned)(millis() / 1000), bt::scanning() ? "on" : "off", bt::connected(),
                    bt::connectedName().c_str(), (unsigned)st.nameRequests, (unsigned)d.size());
      for (auto& n : d) Serial.printf(" '%s'", n.c_str());
      Serial.println();
    } else if (c == "btraw" || c.startsWith("btraw ")) {  // log every BT inquiry result
      uint32_t secs = c.length() > 6 ? (uint32_t)c.substring(6).toInt() : 30;
      if (bt::debugRawScan(secs))
        Serial.printf("[btraw] logging every inquiry result for ~%u s (ends at the next inquiry stop)\n",
                      (unsigned)secs);
      else
        Serial.println("[btraw] Bluetooth is off: switch to Reproducao first");
#ifdef NET_DIAG
    } else if (c == "netdrop") {
      netmode::debugDropUpload();
    } else if (c.startsWith("txp ")) {  // Wi-Fi TX power in 0.25 dBm units (8..78): degrades the link on purpose
      int8_t v = (int8_t)c.substring(4).toInt();
      esp_err_t e = esp_wifi_set_max_tx_power(v);
      int8_t now = 0;
      esp_wifi_get_max_tx_power(&now);
      Serial.printf("[diag] txp %d -> err %d, now %d\n", (int)v, (int)e, (int)now);
#endif
    } else if (c.length()) {
      Serial.println("[ui] ? tap <x> <y> | screen | heap | scan | reboot | stall | soak | bt | btraw [secs]");
    }
  }
}
#endif

}  // namespace

void begin(TTGOClass* watch, const Host& host) {
  s_w = watch;
  s_host = host;
  s_spr = new TFT_eSprite(s_w->tft);
  s_spr->setColorDepth(16);
  if (s_spr->createSprite(uilogic::kScreenW, uilogic::kScreenH)) {
    g = s_spr;
  } else {
    Serial.println("[ui] no memory for the screen buffer: drawing directly");
    delete s_spr;
    s_spr = nullptr;
    g = s_w->tft;
  }
  s_w->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);
  s_w->power->clearIRQ();
  pinMode(AXP202_INT, INPUT_PULLUP);
  uint32_t now = millis();
  s_touch.activity(now);
  int16_t x, y;
  readTouch(x, y, now);  // detect an absent controller at boot (shows the hint)
  backlight(true);
  refresh(now);
}

void loop() {
  uint32_t now = millis();
  pollButton(now);
#ifdef UI_DEBUG
  uint32_t tb = millis() - now;
#endif
  int16_t x = 0, y = 0;
  bool down = readTouch(x, y, now);
#ifdef UI_DEBUG
  uint32_t tt = millis() - now - tb;
  if (tb > 20 || tt > 20) Serial.printf("[ui] slow: button %u ms, touch %u ms\n", (unsigned)tb, (unsigned)tt);
  debugSerial();
  if (s_inject > 0) {
    down = true;
    x = s_injX;
    y = s_injY;
    --s_inject;
  }
#endif
  if (transfer::active()) {  // the backlight never blanks while receiving
    if (!s_touch.screenOn()) {
      s_touch.wake(now);
      backlight(true);
    } else {
      s_touch.activity(now);
    }
  }
  switch (s_touch.update(now, down, x, y)) {
    case uilogic::TouchTracker::Event::Tap: onTap(s_touch.tapX(), s_touch.tapY(), now); break;
    case uilogic::TouchTracker::Event::Wake: backlight(true); break;
    case uilogic::TouchTracker::Event::Sleep:
      backlight(false);
      // Screen off on the Bluetooth screen: stop the search (it is not resumed
      // on wake; the button then shows "Procurar").
      if (s_screen == Screen::Bluetooth && bt::scanning()) bt::stopScan();
      break;
    default: break;
  }
  if (!s_touch.down()) onRelease();
  // Debounced volume save, whatever screen is shown (player::volume() stays
  // valid after player::end()).
  if (s_volDirty && now - s_volAt >= kVolSaveMs) saveVolume();
  if (s_touch.screenOn() && (s_dirty || now - s_lastPoll >= kPollMs)) {
    s_lastPoll = now;
    refresh(now);
  }
}

}  // namespace ui
