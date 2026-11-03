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
String s_lastSig;

// Slot cache: files only change in Programação, so this is reloaded on every
// entry to Reprodução (not polled: slotName() reads /names.json).
bool s_has[4] = {false, false, false, false};
String s_names[4];

bool s_volDirty = false;
uint32_t s_volAt = 0;

int s_btPage = 0;
String s_pending;  // device tapped, waiting for the link
uint32_t s_pendingAt = 0;
std::vector<std::string> s_btList;  // rows as drawn (for hit -> name)
int s_btTotal = 0;                  // devices in the list as drawn

#ifdef UI_DEBUG
String s_desc;  // text description of the last drawn frame
String s_line;
int s_inject = 0;
int16_t s_injX = 0, s_injY = 0;
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
enum class Font { None, S, M, L };
Font s_font = Font::None;

void useFont(Font f) {
  if (f == s_font) return;
  if (s_font != Font::None) g->unloadFont();
  g->loadFont(f == Font::S ? kFontS : f == Font::M ? kFontM : kFontL);
  s_font = f;
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

void text(const String& s, int x, int y, uint8_t datum, uint16_t fg, uint16_t bg) {
  g->setTextDatum(datum);
  g->setTextColor(fg, bg);
  g->drawString(s, x, y);
}

int cx(const Rect& r) { return r.x + r.w / 2; }
int cy(const Rect& r) { return r.y + r.h / 2; }

void button(const Rect& r, uint16_t color) { g->fillRoundRect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 7, color); }

void labelButton(const Rect& r, const String& label, uint16_t color, uint16_t fg = C_TEXT) {
  button(r, color);
  useFont(Font::S);
  text(fit(label, r.w - 8), cx(r), cy(r), MC_DATUM, fg, color);
}

// Two-line label (used where a single line does not fit 80 px).
void label2Button(const Rect& r, const String& l1, const String& l2, uint16_t color) {
  button(r, color);
  useFont(Font::S);
  text(fit(l1, r.w - 8), cx(r), cy(r) - 9, MC_DATUM, C_TEXT, color);
  text(fit(l2, r.w - 8), cx(r), cy(r) + 9, MC_DATUM, C_TEXT, color);
}

// ---- Reprodução ---------------------------------------------------------------
struct PlayModel {
  int cur;
  bool playing, paused;
  uint8_t vol;
  slotlogic::LoopMode loop;
  bool btConn;
  String btName;
};
slotlogic::LoopMode s_loop = slotlogic::LoopMode::RepeatOne;

PlayModel readPlay() {
  PlayModel m;
  m.cur = player::currentSlot();
  m.playing = player::playing();
  m.paused = player::paused();
  m.vol = player::volume();
  m.loop = s_loop;
  m.btConn = bt::connected();
  m.btName = m.btConn ? bt::connectedName() : String();
  return m;
}

String sigPlay(const PlayModel& m) {
  char b[64];
  snprintf(b, sizeof b, "P%d%d%d%u%d%d|", m.cur, m.playing, m.paused, m.vol, (int)m.loop, m.btConn);
  return String(b) + m.btName;
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

void drawPlay(const PlayModel& m) {
  using uilogic::PlayHit;
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  // status bar
  useFont(Font::S);
  String st = fit(String(uilogic::btStatusText(m.btConn, m.btName.c_str()).c_str()), 170);
  text(st, 4, uilogic::kStatusH / 2, ML_DATUM, m.btConn ? C_CYAN : C_DIM, C_BG);
  String vol = "Vol " + String(m.vol);
  text(vol, 236, uilogic::kStatusH / 2, MR_DATUM, C_TEXT, C_BG);
  DESC("status '%s' '%s'", st.c_str(), vol.c_str());

  // slot rows
  for (int i = 0; i < 4; ++i) {
    Rect r = uilogic::playRect((PlayHit)((int)PlayHit::Slot1 + i));
    bool cur = m.cur == i + 1;
    uint16_t bg = cur ? C_ROW_CUR : (i % 2 ? C_ROW_B : C_ROW_A);
    g->fillRect(r.x, r.y, r.w, r.h - 1, bg);
    const char* mark = "";
    if (cur) {
      uint16_t mc = m.playing ? C_GREEN : m.paused ? C_YELLOW : C_DIM;
      iconPlay(11, cy(r), 7, mc);
      mark = m.playing ? "> (a tocar)" : m.paused ? "> (pausa)" : "> (parado)";
    }
    useFont(Font::S);
    text(String(i + 1), 24, cy(r), ML_DATUM, C_DIM, bg);
    useFont(Font::M);
    String name = s_has[i] ? fit(s_names[i], 240 - 40 - 4) : String("Vazio");
    text(name, 40, cy(r), ML_DATUM, s_has[i] ? C_TEXT : C_DIM, bg);
    DESC("slot %d %s '%s'", i + 1, mark, name.c_str());
  }

  // transport + volume
  button(uilogic::playRect(PlayHit::Prev), C_BTN);
  iconPrev(uilogic::playRect(PlayHit::Prev), C_TEXT);
  Rect pp = uilogic::playRect(PlayHit::PlayPause);
  button(pp, C_BTN_HI);
  if (m.playing) iconPause(cx(pp), cy(pp), 9, C_TEXT);
  else iconPlay(cx(pp), cy(pp), 9, C_TEXT);
  button(uilogic::playRect(PlayHit::Next), C_BTN);
  iconNext(uilogic::playRect(PlayHit::Next), C_TEXT);
  Rect vd = uilogic::playRect(PlayHit::VolDown), vu = uilogic::playRect(PlayHit::VolUp);
  button(vd, C_BTN);
  g->fillRect(cx(vd) - 9, cy(vd) - 2, 18, 4, C_TEXT);
  button(vu, C_BTN);
  g->fillRect(cx(vu) - 9, cy(vu) - 2, 18, 4, C_TEXT);
  g->fillRect(cx(vu) - 2, cy(vu) - 9, 4, 18, C_TEXT);
  DESC("buttons [prev] [%s] [next] [vol-] [vol+]", m.playing ? "pause" : "play");

  // bottom row
  const char* loopTxt = m.loop == slotlogic::LoopMode::AllSlots ? "todas" : "faixa";
  label2Button(uilogic::playRect(PlayHit::Loop), "Repetir", loopTxt, C_BTN);
  labelButton(uilogic::playRect(PlayHit::Bluetooth), "Bluetooth", C_BTN);
  labelButton(uilogic::playRect(PlayHit::Program), "Programar", C_BTN);
  DESC("buttons [Repetir %s] [Bluetooth] [Programar]", loopTxt);
}

void apply(const uilogic::Action& a) {
  switch (a.kind) {
    case uilogic::Action::Kind::Play: player::play(a.slot); break;
    case uilogic::Action::Kind::Pause: player::pause(); break;
    case uilogic::Action::Kind::Resume: player::resume(); break;
    default: break;
  }
}

void openBluetooth() {
  s_inPlayBt = true;
  s_btPage = 0;
  s_pending = "";
  bt::startScan();
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
    case PlayHit::Bluetooth: openBluetooth(); break;
    case PlayHit::Program:
      if (s_volDirty) {  // the player is about to be torn down: keep the volume just set
        s_volDirty = false;
        settings::setVolume(player::volume());
      }
      s_host.requestMode(Mode::Program);
      break;
    default: break;
  }
}

// ---- Bluetooth --------------------------------------------------------------
struct BtModel {
  std::vector<std::string> names;
  bool scanning, connected;
  String connName;
};

BtModel readBt(uint32_t now) {
  BtModel m;
  std::vector<std::string> raw;
  for (auto& n : bt::devices()) raw.push_back(n.c_str());
  m.names = uilogic::btListNames(raw);
  m.scanning = bt::scanning();
  m.connected = bt::connected();
  m.connName = m.connected ? bt::connectedName() : String();
  if (s_pending.length() && (m.connected && m.connName == s_pending)) s_pending = "";
  if (s_pending.length() && now - s_pendingAt > 30000) s_pending = "";  // gave up
  s_btPage = uilogic::clampPage(s_btPage, (int)m.names.size(), uilogic::kBtRowsPerPage);
  return m;
}

String sigBt(const BtModel& m) {
  String s = "B" + String(m.scanning) + String(m.connected) + m.connName + "|" + s_pending + "|" + String(s_btPage);
  for (auto& n : m.names) s += "|" + String(n.c_str());
  return s;
}

void drawBt(const BtModel& m) {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  useFont(Font::M);
  text("Bluetooth", 6, uilogic::kBtHeaderH / 2, ML_DATUM, C_TEXT, C_BG);
  useFont(Font::S);
  String state = m.scanning ? "A procurar\xE2\x80\xA6" : "Parado";
  text(state, 234, uilogic::kBtHeaderH / 2, MR_DATUM, m.scanning ? C_YELLOW : C_DIM, C_BG);
  DESC("title 'Bluetooth' state '%s'", state.c_str());

  int n = (int)m.names.size();
  int pages = uilogic::pageCount(n, uilogic::kBtRowsPerPage);
  int first = uilogic::pageFirst(s_btPage, uilogic::kBtRowsPerPage);
  int rows = uilogic::pageRows(s_btPage, n, uilogic::kBtRowsPerPage);
  s_btList.assign(m.names.begin() + first, m.names.begin() + first + rows);
  s_btTotal = n;
  for (int r = 0; r < rows; ++r) {
    Rect rr = uilogic::btRowRect(r);
    String name = s_btList[r].c_str();
    bool isConn = m.connected && name == m.connName;
    bool isPend = !isConn && s_pending.length() && name == s_pending;
    uint16_t bg = isConn ? C_ROW_OK : (r % 2 ? C_ROW_B : C_ROW_A);
    g->fillRect(rr.x, rr.y, rr.w, rr.h - 1, bg);
    const char* tag = isConn ? "ligado" : isPend ? "a ligar\xE2\x80\xA6" : "";
    useFont(Font::M);
    String shown = fit(name, tag[0] ? 240 - 12 - 70 : 240 - 12);
    text(shown, 6, cy(rr), ML_DATUM, C_TEXT, bg);
    if (tag[0]) {
      useFont(Font::S);
      text(tag, 234, cy(rr), MR_DATUM, isConn ? C_GREEN : C_YELLOW, bg);
    }
    DESC("row %d '%s' %s", r, shown.c_str(), tag);
  }
  if (n == 0) {
    useFont(Font::S);
    String l1 = m.scanning ? "A procurar dispositivos\xE2\x80\xA6" : "Nenhum dispositivo encontrado.";
    text(l1, 120, 70, MC_DATUM, C_DIM, C_BG);
    auto hint = uilogic::wrap("Põe o auricular ou a coluna em modo de emparelhamento.", 220, 3, widthOf, nullptr);
    for (size_t i = 0; i < hint.size(); ++i) text(hint[i].c_str(), 120, 110 + 20 * (int)i, MC_DATUM, C_DIM, C_BG);
    DESC("empty '%s'", l1.c_str());
  }
  labelButton(uilogic::btButtonRect(uilogic::BtButton::Scan), m.scanning ? "Parar" : "Procurar", C_BTN);
  if (pages > 1)
    labelButton(uilogic::btButtonRect(uilogic::BtButton::More), "Mais " + String(s_btPage + 1) + "/" + String(pages),
                C_BTN);
  labelButton(uilogic::btButtonRect(uilogic::BtButton::Back), "Voltar", C_BTN_HI);
  DESC("buttons [%s] %s [Voltar] page %d/%d of %d devices", m.scanning ? "Parar" : "Procurar",
       pages > 1 ? "[Mais]" : "", s_btPage + 1, pages, n);
}

void tapBt(int x, int y, uint32_t now) {
  int n = (int)s_btList.size();
  int total = s_btTotal;
  bool more = uilogic::pageCount(total, uilogic::kBtRowsPerPage) > 1;
  uilogic::BtHit h = uilogic::btHitTest(x, y, n, more);
  switch (h.kind) {
    case uilogic::BtHit::Kind::Row:
      s_pending = s_btList[h.row].c_str();
      s_pendingAt = now;
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
String sigProgram(const String& ip, bool up) { return "G" + ip + "|" + String(up); }

void drawProgram(const String& ip, bool uploading) {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  useFont(Font::M);
  text("Programação", 120, 18, MC_DATUM, C_TEXT, C_BG);
  DESC("title 'Programação'");
  int y = 68;
  int ap = ip.indexOf(" (");
  if (ip.isEmpty() || ip.startsWith("A ligar")) {
    String t = ip.isEmpty() ? "A iniciar\xE2\x80\xA6" : "A ligar ao Wi-Fi\xE2\x80\xA6";
    text(t, 120, y, MC_DATUM, C_YELLOW, C_BG);
    DESC("ip '%s'", t.c_str());
  } else {
    String addr = ap > 0 ? ip.substring(0, ap) : ip;
    useFont(Font::L);
    if (g->textWidth(addr) > 232) useFont(Font::M);
    text(fit(addr, 236), 120, y, MC_DATUM, C_CYAN, C_BG);
    DESC("ip '%s'", addr.c_str());
    if (ap > 0) {  // setup access point: tell which network to join
      String net = ip.substring(ap + 2, ip.length() - 1);
      useFont(Font::S);
      text(fit("Liga-te à rede Wi-Fi " + net, 236), 120, 98, MC_DATUM, C_TEXT, C_BG);
      DESC("ap 'Liga-te à rede Wi-Fi %s'", net.c_str());
    }
  }
  useFont(Font::S);
  auto hint = uilogic::wrap("Abre este endereço no browser", 230, 2, widthOf, nullptr);
  for (size_t i = 0; i < hint.size(); ++i) text(hint[i].c_str(), 120, 124 + 19 * (int)i, MC_DATUM, C_DIM, C_BG);
  DESC("hint 'Abre este endereço no browser'");
  if (uploading) {
    text("A receber\xE2\x80\xA6", 120, 168, MC_DATUM, C_YELLOW, C_BG);
    DESC("upload 'A receber…'");
  }
  Rect b = uilogic::programBackRect();
  button(b, C_BTN_HI);
  useFont(Font::M);
  text("Reprodução", cx(b), cy(b), MC_DATUM, C_TEXT, C_BTN_HI);
  DESC("buttons [Reprodução]");
}

// ---- busy -------------------------------------------------------------------
void drawBusy() {
  g->fillRect(0, 0, uilogic::kScreenW, uilogic::kScreenH, C_BG);
  useFont(Font::M);
  text("A mudar de modo\xE2\x80\xA6", 120, 110, MC_DATUM, C_TEXT, C_BG);
  useFont(Font::S);
  text("Aguarda um momento", 120, 140, MC_DATUM, C_DIM, C_BG);
  DESC("busy 'A mudar de modo…'");
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

  String sig;
  PlayModel pm;
  BtModel bm;
  String ip;
  bool up = false;
  switch (s_screen) {
    case Screen::Busy: sig = "busy"; break;
    case Screen::Play: pm = readPlay(); sig = sigPlay(pm); break;
    case Screen::Bluetooth: bm = readBt(now); sig = sigBt(bm); break;
    case Screen::Program:
      ip = s_host.ipText();
      up = s_host.uploading();
      sig = sigProgram(ip, up);
      break;
  }
  if (!s_dirty && sig == s_lastSig) return;
  s_lastSig = sig;
  s_dirty = false;
#ifdef UI_DEBUG
  s_desc = String("screen ") + screenName(s_screen) + "\n";
#endif
  switch (s_screen) {
    case Screen::Busy: drawBusy(); break;
    case Screen::Play: drawPlay(pm); break;
    case Screen::Bluetooth: drawBt(bm); break;
    case Screen::Program: drawProgram(ip, up); break;
  }
  if (s_spr) s_spr->pushSprite(0, 0);
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
// register-pointer write NACKs within ~1 ms, so probe first and, while the
// controller is absent, retry only once a second.
constexpr uint8_t kTouchAddr = 0x38;
bool s_touchOk = true;
uint32_t s_touchProbeAt = 0;

bool readTouch(int16_t& x, int16_t& y, uint32_t now) {
  if (!s_touchOk && now - s_touchProbeAt < 1000) return false;
  s_touchProbeAt = now;
  Wire1.beginTransmission(kTouchAddr);
  Wire1.write(0x02);  // TD_STATUS
  bool ok = Wire1.endTransmission() == 0;
  if (ok != s_touchOk) {
    s_touchOk = ok;
    Serial.println(ok ? "[ui] touch controller answering again"
                      : "[ui] touch controller not answering (turn the watch fully off and on)");
  }
  return ok && s_w->getTouch(x, y);
}

#ifdef UI_DEBUG
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
      Serial.println("[ui] ? tap <x> <y> | screen | heap | scan | reboot");
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
  if (s_volDirty && now - s_volAt >= kVolSaveMs && s_screen != Screen::Busy) {
    s_volDirty = false;
    settings::setVolume(player::volume());
  }
  if (s_touch.screenOn() && (s_dirty || now - s_lastPoll >= kPollMs)) {
    s_lastPoll = now;
#ifdef UI_DEBUG
    uint32_t t0 = millis();
    refresh(now);
    uint32_t dt = millis() - t0;
    if (dt > 100) Serial.printf("[ui] slow: redraw %u ms\n", (unsigned)dt);
#else
    refresh(now);
#endif
  }
}

}  // namespace ui
