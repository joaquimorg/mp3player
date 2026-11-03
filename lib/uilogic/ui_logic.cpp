#include "ui_logic.h"
#include <cstdio>

namespace uilogic {

const char* const kEllipsis = "\xE2\x80\xA6";

// ---- layout -----------------------------------------------------------------
namespace {
Rect r(int x, int y, int w, int h) { return Rect{(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h}; }
}  // namespace

Rect playerRect(PlayerHit h) {
  switch (h) {
    case PlayerHit::Prev: return r(8, 31, 64, 70);
    case PlayerHit::PlayPause: return r(80, 28, 80, 76);
    case PlayerHit::Next: return r(168, 31, 64, 70);
    case PlayerHit::VolDown: return r(8, 112, 64, 56);
    case PlayerHit::VolUp: return r(168, 112, 64, 56);
    case PlayerHit::Songs: return r(8, 176, 108, 56);
    case PlayerHit::Menu: return r(124, 176, 108, 56);
    default: return r(0, 0, 0, 0);
  }
}
Rect playerVolumeLabel() { return r(80, 112, 80, 56); }

PlayerHit playerHitTest(int x, int y) {
  for (int i = 1; i <= kPlayerTargets; ++i)
    if (playerRect((PlayerHit)i).contains(x, y)) return (PlayerHit)i;
  return PlayerHit::None;
}

Rect songsRect(SongsHit h) {
  if (h >= SongsHit::Slot1 && h <= SongsHit::Slot4) return r(8, 8 + ((int)h - 1) * 58, 164, 50);
  if (h == SongsHit::Back) return r(180, 8, 52, 224);
  return r(0, 0, 0, 0);
}
SongsHit songsHitTest(int x, int y) {
  for (int i = 1; i <= kSongsTargets; ++i)
    if (songsRect((SongsHit)i).contains(x, y)) return (SongsHit)i;
  return SongsHit::None;
}

Rect menuRect(MenuHit h) {
  if (h == MenuHit::None) return r(0, 0, 0, 0);
  int k = (int)h - 1;
  return r(8 + (k % 2) * 116, 8 + (k / 2) * 116, 108, 108);
}
MenuHit menuHitTest(int x, int y) {
  for (int i = 1; i <= kMenuTargets; ++i)
    if (menuRect((MenuHit)i).contains(x, y)) return (MenuHit)i;
  return MenuHit::None;
}

Rect btRowRect(int row) { return r(8, 8 + row * (kBtRowH + kGap), 224, kBtRowH); }

Rect btButtonRect(BtButton b) { return r(8 + (int)b * 77, kBtButtonsY, 69, 50); }

BtHit btHitTest(int x, int y, int rowsShown, bool moreShown) {
  for (int k = 0; k < rowsShown && k < kBtRowsPerPage; ++k)
    if (btRowRect(k).contains(x, y)) return BtHit{BtHit::Kind::Row, k};
  if (btButtonRect(BtButton::Scan).contains(x, y)) return BtHit{BtHit::Kind::Scan, 0};
  if (moreShown && btButtonRect(BtButton::More).contains(x, y)) return BtHit{BtHit::Kind::More, 0};
  if (btButtonRect(BtButton::Back).contains(x, y)) return BtHit{BtHit::Kind::Back, 0};
  return BtHit{BtHit::Kind::None, 0};
}

Rect programBackRect() { return r(8, 182, 224, 50); }
bool programHitTest(int x, int y) { return programBackRect().contains(x, y); }

// ---- navigation -------------------------------------------------------------
View afterPlayerTap(PlayerHit h) {
  if (h == PlayerHit::Songs) return View::Songs;
  if (h == PlayerHit::Menu) return View::Menu;
  return View::Player;
}
View afterSongsTap(SongsHit h, const Action& a) {
  if (h == SongsHit::Back) return View::Player;
  if (slotOf(h) && a.kind == Action::Kind::Play) return View::Player;
  return View::Songs;
}
View afterMenuTap(MenuHit h) {
  if (h == MenuHit::Bluetooth) return View::Bluetooth;
  if (h == MenuHit::Back) return View::Player;
  return View::Menu;  // Loop toggles in place; Programar starts a mode switch
}
View afterBtTap(BtHit::Kind k) { return k == BtHit::Kind::Back ? View::Player : View::Bluetooth; }

// ---- paging -----------------------------------------------------------------
int pageCount(int items, int perPage) {
  if (perPage <= 0 || items <= 0) return 1;
  return (items + perPage - 1) / perPage;
}
int clampPage(int page, int items, int perPage) {
  int n = pageCount(items, perPage);
  if (page < 0) return 0;
  return page >= n ? n - 1 : page;
}
int nextPage(int page, int items, int perPage) {
  int n = pageCount(items, perPage);
  return (clampPage(page, items, perPage) + 1) % n;
}
int pageFirst(int page, int perPage) { return page * perPage; }
int pageRows(int page, int items, int perPage) {
  int first = pageFirst(clampPage(page, items, perPage), perPage);
  int left = items - first;
  if (left <= 0) return 0;
  return left < perPage ? left : perPage;
}

// ---- text -------------------------------------------------------------------
namespace {
void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

// Decodes one codepoint at s[i]; returns bytes consumed (>= 1), cp = 0xFFFFFFFF if invalid.
size_t decode(const std::string& s, size_t i, uint32_t& cp) {
  uint8_t c = (uint8_t)s[i];
  if (c < 0x80) {
    cp = c;
    return 1;
  }
  int n;
  uint32_t min;
  if ((c & 0xE0) == 0xC0) n = 1, cp = c & 0x1F, min = 0x80;
  else if ((c & 0xF0) == 0xE0) n = 2, cp = c & 0x0F, min = 0x800;
  else if ((c & 0xF8) == 0xF0) n = 3, cp = c & 0x07, min = 0x10000;
  else {
    cp = 0xFFFFFFFF;
    return 1;
  }
  for (int k = 1; k <= n; ++k) {
    if (i + k >= s.size() || ((uint8_t)s[i + k] & 0xC0) != 0x80) {
      cp = 0xFFFFFFFF;
      return k;  // skip the lead + valid continuations seen so far
    }
    cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFFFFFF;
  return n + 1;
}

size_t cpLen(uint8_t lead) {
  if (lead < 0x80) return 1;
  if ((lead & 0xE0) == 0xC0) return 2;
  if ((lead & 0xF0) == 0xE0) return 3;
  if ((lead & 0xF8) == 0xF0) return 4;
  return 1;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && s[a] == ' ') ++a;
  while (b > a && s[b - 1] == ' ') --b;
  return s.substr(a, b - a);
}
}  // namespace

std::string sanitize(const std::string& utf8, GlyphFn hasGlyph, void* ctx) {
  std::string out;
  for (size_t i = 0; i < utf8.size();) {
    uint32_t cp;
    i += decode(utf8, i, cp);
    if (cp == 0xFFFFFFFF || cp > 0xFFFF) {
      out += '?';
    } else if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0)) {
      out += ' ';
    } else if (cp == 0x20 || cp == 0xA0) {
      out += ' ';
    } else if (hasGlyph && !hasGlyph(cp, ctx)) {
      out += '?';
    } else {
      appendUtf8(out, cp);
    }
  }
  return trim(out);
}

std::string fitWidth(const std::string& s, int maxW, WidthFn width, void* ctx) {
  if (width(s, ctx) <= maxW) return s;
  // Codepoint boundaries (input is expected to be sanitized UTF-8).
  std::vector<size_t> ends;
  for (size_t i = 0; i < s.size();) {
    i += cpLen((uint8_t)s[i]);
    ends.push_back(i > s.size() ? s.size() : i);
  }
  // Longest prefix p with width(p + "…") <= maxW (binary search on monotone width).
  int lo = 0, hi = (int)ends.size() - 1, best = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    std::string t = trim(s.substr(0, ends[mid])) + kEllipsis;
    if (width(t, ctx) <= maxW) best = mid, lo = mid + 1;
    else hi = mid - 1;
  }
  if (best < 0) return width(kEllipsis, ctx) <= maxW ? std::string(kEllipsis) : std::string();
  return trim(s.substr(0, ends[best])) + kEllipsis;
}

std::vector<std::string> wrap(const std::string& s, int maxW, int maxLines, WidthFn width, void* ctx) {
  std::vector<std::string> words;
  std::string cur;
  for (char c : s) {
    if (c == ' ') {
      if (!cur.empty()) words.push_back(cur), cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) words.push_back(cur);

  std::vector<std::string> lines;
  std::string line;
  size_t w = 0;
  for (; w < words.size(); ++w) {
    std::string cand = line.empty() ? words[w] : line + " " + words[w];
    if (line.empty() || width(cand, ctx) <= maxW) {
      line = cand;
    } else {
      lines.push_back(line);
      line = words[w];
      if ((int)lines.size() == maxLines - 1) {
        for (++w; w < words.size(); ++w) line += " " + words[w];
        break;
      }
    }
  }
  if (!line.empty()) lines.push_back(line);
  if (maxLines > 0 && (int)lines.size() > maxLines) lines.resize(maxLines);
  for (auto& l : lines) l = fitWidth(l, maxW, width, ctx);
  return lines;
}

// ---- player decisions -------------------------------------------------------
uint8_t stepVolume(uint8_t v, int delta) {
  int n = (int)v + delta;
  if (n < 0) n = 0;
  if (n > 100) n = 100;
  return (uint8_t)n;
}

int firstNonEmpty(const bool has[4]) {
  for (int i = 0; i < 4; ++i)
    if (has[i]) return i + 1;
  return 0;
}

Action playPauseAction(bool playing, bool paused, int current, const bool has[4]) {
  if (playing) return Action{Action::Kind::Pause, 0};
  if (paused) return Action{Action::Kind::Resume, 0};
  int s = (current >= 1 && current <= 4 && has[current - 1]) ? current : firstNonEmpty(has);
  if (!s) return Action{Action::Kind::None, 0};
  return Action{Action::Kind::Play, s};
}

Action slotTapAction(int slot, bool playing, bool paused, int current, const bool has[4]) {
  if (slot < 1 || slot > 4 || !has[slot - 1]) return Action{Action::Kind::None, 0};
  if (slot == current && playing) return Action{Action::Kind::Pause, 0};
  if (slot == current && paused) return Action{Action::Kind::Resume, 0};
  return Action{Action::Kind::Play, slot};
}

Title playerTitle(int current, bool playing, bool paused, const bool has[4], const std::string& currentName) {
  if (!firstNonEmpty(has)) return Title{Title::Icon::None, "Nenhuma m\xC3\xBAsica"};
  if ((playing || paused) && current >= 1 && current <= 4) {
    std::string t = std::to_string(current) + " \xC2\xB7 " + currentName;
    return Title{playing ? Title::Icon::Play : Title::Icon::Pause, t};
  }
  return Title{Title::Icon::None, "Parado"};
}

std::string btStatusText(bool connected, const std::string& name) {
  if (!connected) return "BT: Desligado";
  return "BT: " + (name.empty() ? std::string("ligado") : name);
}

std::vector<std::string> btListNames(const std::vector<std::string>& raw) {
  std::vector<std::string> out;
  for (const auto& n : raw) {
    std::string t = trim(n);
    if (t.empty()) continue;
    bool dup = false;
    for (const auto& o : out)
      if (o == t) dup = true;
    if (!dup) out.push_back(t);
  }
  return out;
}

bool bootIntoProgram(bool programFlag, bool softwareReset) { return programFlag && softwareReset; }

// ---- touch ------------------------------------------------------------------
void TouchTracker::wake(uint32_t now) {
  on_ = true;
  last_ = now;
}

TouchTracker::Event TouchTracker::update(uint32_t now, bool down, int x, int y) {
  if (down) {
    last_ = now;
    if (down_) return Event::None;  // still the same press
    down_ = true;
    if (!on_) {
      on_ = true;
      return Event::Wake;  // swallowed until release
    }
    tx_ = x;
    ty_ = y;
    return Event::Tap;
  }
  down_ = false;
  if (on_ && (uint32_t)(now - last_) >= timeout_) {
    on_ = false;
    return Event::Sleep;
  }
  return Event::None;
}

std::string formatMB(uint32_t bytes) {
  char b[24];
  if (bytes < 100u * 1024u) {
    snprintf(b, sizeof b, "%u KB", (unsigned)(bytes / 1024u));
  } else {
    uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10 + 524288u) / 1048576u);
    snprintf(b, sizeof b, "%u,%u MB", (unsigned)(tenths / 10), (unsigned)(tenths % 10));
  }
  return b;
}

int percent(uint32_t received, uint32_t total) {
  if (total == 0) return 0;
  uint64_t p = (uint64_t)received * 100 / total;
  return p > 100 ? 100 : (int)p;
}

int etaSeconds(uint32_t received, uint32_t total, uint32_t elapsedMs) {
  if (received == 0 || total == 0 || elapsedMs == 0) return -1;
  if (received >= total) return 0;
  uint64_t ms = (uint64_t)(total - received) * elapsedMs / received;
  uint64_t s = (ms + 500) / 1000;
  return s > 86400 ? -1 : (int)s;
}

}  // namespace uilogic
