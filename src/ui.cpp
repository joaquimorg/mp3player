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
#include "ui_logic.h"

namespace ui {
namespace {

using uilogic::Rect;

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
constexpr uint16_t C_ROW_A = 0x0861;
constexpr uint16_t C_ROW_B = 0x10A2;
constexpr uint16_t C_ROW_CUR = 0x1148;  // dark blue: current slot
constexpr uint16_t C_ROW_OK = 0x0B04;   // dark green: connected device
constexpr uint16_t C_GREEN = 0x5E4F;
constexpr uint16_t C_YELLOW = 0xFEA0;
constexpr uint16_t C_CYAN = 0x5DDF;
constexpr uint16_t C_WARN = 0x8000;     // dark red banner

// ---- state ------------------------------------------------------------------
enum class Screen { Busy, Play, Bluetooth, Program };
const char* screenName(Screen s) {
  switch (s) {
    case Screen::Busy: return "Ocupado";
    case Screen::Play: return "Reproducao";
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
bool s_inPlayBt = false;  // in Reprodução: Bluetooth screen open
bool s_dirty = true;
uint32_t s_lastPoll = 0;
uint32_t s_lastBtn = 0;
uint32_t s_lastSig = 0;

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

int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

void button(const Rect& r, uint16_t color) { g->fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 7, color); }

void labelButton(const Rect& r, const String& label, uint16_t color, const char* tag) {
  button(r, color);
  queueText(Font::S, label, cx(r), cy(r), MC_DATUM, C_TEXT, color, r.w - 4, tag);
}

// Two-line label (where one line does not fit 80 px).
void label2Button(const Rect& r, const String& l1, const String& l2, uint16_t color, uint16_t fg2, const char* tag) {
  button(r, color);
  queueText(Font::S, l1, cx(r), cy(r) - 9, MC_DATUM, C_TEXT, color, r.w - 4);
  queueText(Font::S, l2, cx(r), cy(r) + 9, MC_DATUM, fg2, color, r.w - 4, tag);
}

// ---- Reprodução ---------------------------------------------------------------
void refreshBtName() {
  bool c = bt::connected();
  uint32_t n = bt::stats().connects;
  if (c == s_btConn && n == s_btConnects) return;
  s_btConn = c;
  s_btConnects = n;
  s_btName = c ? bt::connectedName() : String();
}

uint32_t sigPlay() {
  refreshBtName();
  uint32_t h = uilogic::hashMix(uilogic::kHashSeed, 1);
  h = uilogic::hashMix(h, (uint32_t)player::currentSlot());
  h = uilogic::hashMix(h, (player::playing() ? 1u : 0u) | (player::paused() ? 2u : 0u) | (s_btConn ? 4u : 0u));
  h = uilogic::hashMix(h, player::volume() | ((uint32_t)s_loop << 8));
  return uilogic::hashMix(h, s_btConnects);
}

void iconPrev(const Rect& r, uint16_t c) {
  int x = cx(r), y = cy(r);
  g->fillRect(x - 10, y - 8, 3, 17, c);
  g->fillTriangle(x + 8, y - 8, x + 8, y + 8, x - 6, y, c);
}
void iconNext(const Rect& r, uint16_t c) {
  int x = cx(r), y = cy(r);
  g->fillRect(x + 8, y - 8, 3, 17, c);
  g->fillTriangle(x - 8, y - 8, x - 8, y + 8, x + 6, y, c);
}
void iconPlay(int x, int y, int s, uint16_t c) { g->fillTriangle(x - s / 2, y - s, x - s / 2, y + s, x + s, y, c); }
void iconPause(int x, int y, int s, uint16_t c) {
  g->fillRect(x - s, y - s, s * 2 / 3 + 1, 2 * s + 1, c);
  g->fillRect(x + s / 3, y - s, s * 2 / 3 + 1, 2 * s + 1, c);
}

void drawPlay() {
  using uilogic::PlayHit;
  int cur = player::currentSlot();
  bool playing = player::playing(), paused = player::paused();
  uint8_t vol = player::volume();
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);

  // slot rows (40 px): ▶ marker, number, name
  static char tags[4][32];
  for (int i = 0; i < 4; ++i) {
    Rect r = uilogic::playRect((PlayHit)((int)PlayHit::Slot1 + i));
    bool isCur = cur == i + 1;
    uint16_t bg = isCur ? C_ROW_CUR : (i % 2 ? C_ROW_B : C_ROW_A);
    g->fillRect(r.x, r.y, r.w, r.h - 1, bg);
    if (isCur) iconPlay(11, cy(r), 7, playing ? C_GREEN : paused ? C_YELLOW : C_DIM);
    snprintf(tags[i], sizeof tags[i], "slot %d%s", i + 1,
             !isCur ? "" : playing ? " > (a tocar)" : paused ? " > (pausa)" : " > (parado)");
    queueText(Font::S, String(i + 1), 24, cy(r), ML_DATUM, C_DIM, bg);
    if (s_has[i]) queueText(Font::M, s_names[i], 40, cy(r), ML_DATUM, C_TEXT, bg, 240 - 40 - 4, tags[i]);
    else queueText(Font::M, "Vazio", 40, cy(r), ML_DATUM, C_DIM, bg, 0, tags[i]);
  }

  // transport + volume (the − button also shows the volume)
  button(uilogic::playRect(PlayHit::Prev), C_BTN);
  iconPrev(uilogic::playRect(PlayHit::Prev), C_TEXT);
  Rect pp = uilogic::playRect(PlayHit::PlayPause);
  button(pp, C_BTN_HI);
  if (playing) iconPause(cx(pp), cy(pp), 9, C_TEXT);
  else iconPlay(cx(pp), cy(pp), 9, C_TEXT);
  button(uilogic::playRect(PlayHit::Next), C_BTN);
  iconNext(uilogic::playRect(PlayHit::Next), C_TEXT);
  Rect vd = uilogic::playRect(PlayHit::VolDown), vu = uilogic::playRect(PlayHit::VolUp);
  button(vd, C_BTN);
  g->fillRect(cx(vd) - 9, cy(vd) - 9, 18, 4, C_TEXT);
  queueText(Font::S, String(vol), cx(vd), cy(vd) + 8, MC_DATUM, C_CYAN, C_BTN, 0, "volume");
  button(vu, C_BTN);
  g->fillRect(cx(vu) - 9, cy(vu) - 2, 18, 4, C_TEXT);
  g->fillRect(cx(vu) - 2, cy(vu) - 9, 4, 18, C_TEXT);
  DESC("buttons [prev] [%s] [next] [vol-] [vol+]", playing ? "pause" : "play");

  // bottom row: loop | Bluetooth (with BT state) | Programar
  bool all = s_loop == slotlogic::LoopMode::AllSlots;
  label2Button(uilogic::playRect(PlayHit::Loop), "Repetir", all ? "todas" : "faixa", C_BTN, C_TEXT, "loop button");
  label2Button(uilogic::playRect(PlayHit::Bluetooth), "BT:", s_btConn ? s_btName : String("desligado"), C_BTN,
               s_btConn ? C_CYAN : C_DIM, "bluetooth button BT:");
  labelButton(uilogic::playRect(PlayHit::Program), "Programar", C_BTN, "program button");
}

void apply(const uilogic::Action& a) {
  switch (a.kind) {
    case uilogic::Action::Kind::Play: player::play(a.slot); break;
    case uilogic::Action::Kind::Pause: player::pause(); break;
    case uilogic::Action::Kind::Resume: player::resume(); break;
    default: break;
  }
}

void setPending(const String& name, uint32_t now) {
  s_pending = name;
  s_pendingAt = now;
  ++s_pendingVer;
}

void openBluetooth(uint32_t now) {
  s_inPlayBt = true;
  s_btPage = 0;
  setPending("", now);
  bt::startScan();
}

void saveVolume() {
  s_volDirty = false;
  settings::setVolume(player::volume());
}

void tapPlay(int x, int y, uint32_t now) {
  using uilogic::PlayHit;
  PlayHit h = uilogic::playHitTest(x, y);
  int cur = player::currentSlot();
  bool playing = player::playing(), paused = player::paused();
  switch (h) {
    case PlayHit::Slot1:
    case PlayHit::Slot2:
    case PlayHit::Slot3:
    case PlayHit::Slot4:
      apply(uilogic::slotTapAction(uilogic::slotOf(h), playing, paused, cur, s_has));
      break;
    case PlayHit::PlayPause: apply(uilogic::playPauseAction(playing, paused, cur, s_has)); break;
    case PlayHit::Prev: player::prev(); break;
    case PlayHit::Next: player::next(); break;
    case PlayHit::VolDown:
    case PlayHit::VolUp:
      player::setVolume(uilogic::stepVolume(player::volume(), h == PlayHit::VolUp ? kVolStep : -kVolStep));
      s_volDirty = true;
      s_volAt = now;
      break;
    case PlayHit::Loop:
      s_loop = s_loop == slotlogic::LoopMode::AllSlots ? slotlogic::LoopMode::RepeatOne
                                                       : slotlogic::LoopMode::AllSlots;
      settings::setLoopMode(s_loop);
      break;
    case PlayHit::Bluetooth: openBluetooth(now); break;
    case PlayHit::Program:
      if (s_volDirty) saveVolume();  // the player is about to be torn down
      s_host.requestMode(Mode::Program);
      break;
    default: break;
  }
}

// ---- Bluetooth --------------------------------------------------------------
uint32_t sigBt(uint32_t now) {
  refreshBtName();
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

void drawBt() {
  bool scanning = bt::scanning();
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  queueText(Font::M, "Bluetooth", 6, uilogic::kBtHeaderH / 2, ML_DATUM, C_TEXT, C_BG, 0, "title");
  queueText(Font::S, scanning ? "A procurar\xE2\x80\xA6" : "Parado", 234, uilogic::kBtHeaderH / 2, MR_DATUM,
            scanning ? C_YELLOW : C_DIM, C_BG, 0, "state");

  int n = (int)s_btNames.size();
  int pages = uilogic::pageCount(n, uilogic::kBtRowsPerPage);
  int first = uilogic::pageFirst(s_btPage, uilogic::kBtRowsPerPage);
  int rows = uilogic::pageRows(s_btPage, n, uilogic::kBtRowsPerPage);
  s_btList.assign(s_btNames.begin() + first, s_btNames.begin() + first + rows);
  static char tags[uilogic::kBtRowsPerPage][24];
  for (int r = 0; r < rows; ++r) {
    Rect rr = uilogic::btRowRect(r);
    String name = s_btList[r].c_str();
    bool isConn = s_btConn && name == s_btName;
    bool isPend = !isConn && s_pending.length() && name == s_pending;
    uint16_t bg = isConn ? C_ROW_OK : (r % 2 ? C_ROW_B : C_ROW_A);
    g->fillRect(rr.x, rr.y, rr.w, rr.h - 1, bg);
    const char* tag = isConn ? "ligado" : isPend ? "a ligar\xE2\x80\xA6" : "";
    snprintf(tags[r], sizeof tags[r], "row %d%s%s", r, tag[0] ? " " : "", isConn ? "(ligado)" : isPend ? "(a ligar)" : "");
    queueText(Font::M, name, 6, cy(rr), ML_DATUM, C_TEXT, bg, tag[0] ? 240 - 12 - 70 : 240 - 12, tags[r]);
    if (tag[0]) queueText(Font::S, tag, 234, cy(rr), MR_DATUM, isConn ? C_GREEN : C_YELLOW, bg);
  }
  if (n == 0) {
    queueText(Font::S, scanning ? "A procurar dispositivos\xE2\x80\xA6" : "Nenhum dispositivo encontrado.", 120, 70,
              MC_DATUM, C_DIM, C_BG, 230, "empty");
    queueText(Font::S, "Põe o auricular ou a coluna", 120, 110, MC_DATUM, C_DIM, C_BG, 230);
    queueText(Font::S, "em modo de emparelhamento.", 120, 130, MC_DATUM, C_DIM, C_BG, 230);
  }
  labelButton(uilogic::btButtonRect(uilogic::BtButton::Scan), scanning ? "Parar" : "Procurar", C_BTN, "button");
  if (pages > 1)
    labelButton(uilogic::btButtonRect(uilogic::BtButton::More), "Mais " + String(s_btPage + 1) + "/" + String(pages),
                C_BTN, "button");
  labelButton(uilogic::btButtonRect(uilogic::BtButton::Back), "Voltar", C_BTN_HI, "button");
  DESC("page %d/%d of %d devices", s_btPage + 1, pages, n);
}

void tapBt(int x, int y, uint32_t now) {
  int total = (int)s_btNames.size();
  bool more = uilogic::pageCount(total, uilogic::kBtRowsPerPage) > 1;
  uilogic::BtHit h = uilogic::btHitTest(x, y, (int)s_btList.size(), more);
  switch (h.kind) {
    case uilogic::BtHit::Kind::Row:
      setPending(s_btList[h.row].c_str(), now);
      Serial.printf("[ui] connect to '%s'\n", s_pending.c_str());
      bt::connectTo(s_pending);
      break;
    case uilogic::BtHit::Kind::Scan:
      if (bt::scanning()) bt::stopScan();
      else bt::startScan();
      break;
    case uilogic::BtHit::Kind::More: s_btPage = uilogic::nextPage(s_btPage, total, uilogic::kBtRowsPerPage); break;
    case uilogic::BtHit::Kind::Back:
      bt::stopScan();
      s_inPlayBt = false;
      break;
    default: break;
  }
}

// ---- Programação ------------------------------------------------------------
uint32_t sigProgram() {
  uint32_t h = uilogic::hashMix(uilogic::kHashSeed, 3);
  h = uilogic::hashMix(h, s_host.ipVersion());
  return uilogic::hashMix(h, s_host.uploading() ? 1u : 0u);
}

void drawProgram() {
  String ip = s_host.ipText();
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  queueText(Font::M, "Programação", 120, 18, MC_DATUM, C_TEXT, C_BG, 0, "title");
  int ap = ip.indexOf(" (");
  bool noLink = ip.startsWith("Sem liga");  // STA link dropped (netmode::ipText)
  if (ip.isEmpty() || noLink || ip.startsWith("A ligar")) {
    queueText(Font::M,
              ip.isEmpty() ? "A iniciar\xE2\x80\xA6" : noLink ? "Sem liga\xC3\xA7\xC3\xA3o Wi-Fi" : "A ligar ao Wi-Fi\xE2\x80\xA6",
              120, 68, MC_DATUM, C_YELLOW, C_BG, 0, "ip");
  } else {
    // 30 px digits: even "255.255.255.255" fits 236 px (truncation is only a safety net).
    queueText(Font::L, ap > 0 ? ip.substring(0, ap) : ip, 120, 68, MC_DATUM, C_CYAN, C_BG, 236, "ip");
    if (ap > 0)  // setup access point: tell which network to join
      queueText(Font::S, "Liga-te à rede Wi-Fi " + ip.substring(ap + 2, ip.length() - 1), 120, 98, MC_DATUM,
                C_TEXT, C_BG, 236, "ap");
  }
  queueText(Font::S, "Abre este endereço no browser", 120, 130, MC_DATUM, C_DIM, C_BG, 236, "hint");
  if (s_host.uploading()) queueText(Font::S, "A receber\xE2\x80\xA6", 120, 162, MC_DATUM, C_YELLOW, C_BG, 0, "upload");
  Rect b = uilogic::programBackRect();
  button(b, C_BTN_HI);
  queueText(Font::M, "Reprodução", cx(b), cy(b), MC_DATUM, C_TEXT, C_BTN_HI, 0, "button");
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

// ---- frame ------------------------------------------------------------------
void loadSlots() {
  storage::hasFileAll(s_has);
  for (int i = 0; i < 4; ++i) s_names[i] = s_has[i] ? storage::slotName(i + 1) : String();
  s_loop = settings::loopMode();
}

void setScreen(Screen s) {
  if (s == s_screen) return;
  Serial.printf("[ui] screen %s -> %s\n", screenName(s_screen), screenName(s));
  if (s == Screen::Play && s_screen != Screen::Bluetooth) loadSlots();  // fresh from Programação/boot
  if (s == Screen::Bluetooth) s_devVer = 0xFFFFFFFF;                   // rebuild the list
  s_screen = s;
  s_dirty = true;
}

void refresh(uint32_t now) {
  Screen want;
  if (s_host.busy()) want = Screen::Busy;
  else if (s_host.mode() == Mode::Program) want = Screen::Program;
  else want = s_inPlayBt ? Screen::Bluetooth : Screen::Play;
  if (want != Screen::Bluetooth && want != Screen::Play) s_inPlayBt = false;
  setScreen(want);

  // Cheap signature of everything shown (no String/vector built unless a
  // list really changed); redraw only when it differs.
  bool hint = (int32_t)(s_touchHintUntil - now) > 0;
  uint32_t sig = 0;
  switch (s_screen) {
    case Screen::Busy: sig = uilogic::hashMix(uilogic::kHashSeed, 4); break;
    case Screen::Play: sig = sigPlay(); break;
    case Screen::Bluetooth: sig = sigBt(now); break;
    case Screen::Program: sig = sigProgram(); break;
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
    case Screen::Play: drawPlay(); break;
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
  switch (s_screen) {
    case Screen::Play: tapPlay(x, y, now); break;
    case Screen::Bluetooth: tapBt(x, y, now); break;
    case Screen::Program:
      if (uilogic::programHitTest(x, y)) s_host.requestMode(Mode::Play);
      break;
    default: break;
  }
  s_dirty = true;
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
    } else if (c.length()) {
      Serial.println("[ui] ? tap <x> <y> | screen | heap | scan | reboot | stall | soak");
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
  switch (s_touch.update(now, down, x, y)) {
    case uilogic::TouchTracker::Event::Tap: onTap(s_touch.tapX(), s_touch.tapY(), now); break;
    case uilogic::TouchTracker::Event::Wake: backlight(true); break;
    case uilogic::TouchTracker::Event::Sleep: backlight(false); break;
    default: break;
  }
  // Debounced volume save, whatever screen is shown (player::volume() stays
  // valid after player::end()).
  if (s_volDirty && now - s_volAt >= kVolSaveMs) saveVolume();
  if (s_touch.screenOn() && (s_dirty || now - s_lastPoll >= kPollMs)) {
    s_lastPoll = now;
    refresh(now);
  }
}

}  // namespace ui
