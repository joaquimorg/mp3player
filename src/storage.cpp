#include "log.h"
#include "storage.h"
#include <LittleFS.h>
#include "slot_logic.h"
#include <atomic>

namespace storage {
namespace {
// LittleFS usedBytes() walks the whole file system (~1 s with ~10 MB of music), and TWATCH?/LISTA/the web
// page all report the free space. It is cached for one "generation": every change bumps s_gen
// (invalidateFree) and the value is recomputed at the end of the operation (warmFree), so the next
// query is instant. Never cached while data may still be written: write() invalidates too.
std::atomic<uint32_t> s_gen{0};
std::atomic<uint32_t> s_cacheGen{0xFFFFFFFFu};
std::atomic<size_t> s_cacheFree{0};
void invalidateFree() { s_gen.fetch_add(1); }
void warmFree();

constexpr size_t kMaxNameBytes = 48;
const char* kNamesPath = "/names.json";
const char* kNamesTmp = "/names.tmp";
String gNames[slotlogic::kNumSlots];
bool gNamesLoaded = false;

String tmpPathFor(int slot) {
  const char* p = slotlogic::slotPath(slot);
  if (!p) return String();
  String s(p);
  s.replace(".mp3", ".tmp");
  return s;
}

// Truncate to max bytes without splitting a UTF-8 sequence.
String capName(const String& in) {
  if (in.length() <= kMaxNameBytes) return in;
  size_t n = kMaxNameBytes;
  while (n > 0 && (((uint8_t)in[n]) & 0xC0) == 0x80) n--;  // in[n] is a continuation byte
  return in.substring(0, n);
}

void jsonEscape(const String& s, String& out) {
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      out += b;
    } else out += (char)c;  // UTF-8 bytes pass through
  }
}

bool saveNames() {
  String j = "{";
  bool first = true;
  for (int i = 0; i < slotlogic::kNumSlots; i++) {
    if (gNames[i].length() == 0) continue;
    if (!first) j += ",";
    first = false;
    j += "\"";
    j += String(i + 1);
    j += "\":\"";
    jsonEscape(gNames[i], j);
    j += "\"";
  }
  j += "}";
  // Write tmp then rename so a power cut can't corrupt the names file.
  File f = LittleFS.open(kNamesTmp, "w");
  if (!f) return false;
  size_t w = f.print(j);
  f.close();
  if (w != j.length()) { LittleFS.remove(kNamesTmp); return false; }
  // No remove-then-rename fallback: rename atomically replaces the old file,
  // and a failed rename must never delete the existing names.json.
  if (!LittleFS.rename(kNamesTmp, kNamesPath)) { LittleFS.remove(kNamesTmp); return false; }
  invalidateFree();
  return true;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void appendUtf8(String& out, uint32_t cp) {
  if (cp < 0x80) out += (char)cp;
  else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

// Parses {"1":"...","2":"..."}; tolerant, stops at malformed input.
void parseNames(const String& j) {
  size_t i = 0, n = j.length();
  auto readString = [&](String& out) -> bool {
    if (i >= n || j[i] != '"') return false;
    i++;
    out = "";
    while (i < n) {
      char c = j[i++];
      if (c == '"') return true;
      if (c == '\\' && i < n) {
        char e = j[i++];
        if (e == 'n') out += '\n';
        else if (e == 'r') out += '\r';
        else if (e == 't') out += '\t';
        else if (e == 'u' && i + 4 <= n) {
          uint32_t cp = 0;
          for (int k = 0; k < 4; k++) { int h = hexVal(j[i + k]); cp = (cp << 4) | (h < 0 ? 0 : h); }
          i += 4;
          appendUtf8(out, cp);
        } else out += e;  // \" \\ \/
      } else out += c;
    }
    return false;
  };
  while (i < n && j[i] != '"') i++;
  while (i < n) {
    String key, val;
    if (!readString(key)) break;
    while (i < n && j[i] != '"') i++;  // skip ':' to the value's opening quote
    if (!readString(val)) break;
    int s = key.toInt();
    if (s >= 1 && s <= slotlogic::kNumSlots) gNames[s - 1] = capName(val);
    while (i < n && j[i] != '"') i++;  // skip ',' to next key
  }
}

void loadNames() {
  if (gNamesLoaded) return;
  gNamesLoaded = true;
  for (auto& s : gNames) s = "";
  File f = LittleFS.open(kNamesPath, "r");
  if (!f) return;
  String j = f.readString();
  f.close();
  parseNames(j);
}
}  // namespace

bool begin() {
  if (!LittleFS.begin(true)) return false;
  for (int s = 1; s <= slotlogic::kNumSlots; s++) {
    String t = tmpPathFor(s);
    if (LittleFS.exists(t)) LittleFS.remove(t);
  }
  if (LittleFS.exists(kNamesTmp)) LittleFS.remove(kNamesTmp);
  gNamesLoaded = false;
  loadNames();
  invalidateFree();
  warmFree();
  return true;
}

bool hasFile(int slot) {
  const char* p = slotlogic::slotPath(slot);
  return p && LittleFS.exists(p);
}

size_t slotSize(int slot) {
  const char* p = slotlogic::slotPath(slot);
  if (!p) return 0;
  File f = LittleFS.open(p, "r");
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

size_t freeBytes() {
  uint32_t g = s_gen.load();
  if (s_cacheGen.load() == g) return s_cacheFree.load();
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  size_t f = total > used ? total - used : 0;
  if (s_gen.load() == g) {  // nothing changed while computing: keep it
    s_cacheFree.store(f);
    s_cacheGen.store(g);
  }
  return f;
}

namespace {
// Recompute now if something changed since the cached value.
void warmFree() {
  if (s_cacheGen.load() != s_gen.load()) freeBytes();
}
}  // namespace

void hasFileAll(bool out[4]) {
  for (int i = 0; i < slotlogic::kNumSlots; i++) out[i] = hasFile(i + 1);
}

bool remove(int slot) {
  const char* p = slotlogic::slotPath(slot);
  if (!p) return false;
  bool ok = !LittleFS.exists(p) || LittleFS.remove(p);
  invalidateFree();
  loadNames();
  if (ok && gNames[slot - 1].length()) {
    gNames[slot - 1] = "";
    saveNames();
  }
  warmFree();  // recompute now: the next TWATCH?/LISTA must be instant
  return ok;
}

fs::File openRead(int slot) {
  const char* p = slotlogic::slotPath(slot);
  if (!p) return fs::File();
  return LittleFS.open(p, "r");
}

String slotName(int slot) {
  if (slot < 1 || slot > slotlogic::kNumSlots) return String();
  loadNames();
  if (gNames[slot - 1].length()) return gNames[slot - 1];
  return String("Faixa ") + slot;
}

void setSlotName(int slot, const String& name) {
  if (slot < 1 || slot > slotlogic::kNumSlots) return;
  loadNames();
  gNames[slot - 1] = capName(name);
  if (saveNames()) { warmFree(); return; }
  // Full filesystem: do not leave a stale name for this slot (in RAM or in
  // names.json). Fall back to the default "Faixa N" and make the stored file agree:
  // dropping names.json frees its blocks, then the remaining names are rewritten.
  DBG_PRINTF("[storage] names.json write failed (FS full?): slot %d falls back to 'Faixa %d'\n", slot, slot);
  gNames[slot - 1] = "";
  if (!saveNames()) {
    LittleFS.remove(kNamesPath);
    if (!saveNames()) {
      DBG_PRINTLN("[storage] names.json could not be rewritten; names are not persisted");
      LittleFS.remove(kNamesPath);
    }
  }
  invalidateFree();
  warmFree();
}

// ---- SlotWriter ----
SlotWriter::SlotWriter(int slot) : slot_(slot) {
  tmpPath_ = tmpPathFor(slot);
  if (tmpPath_.length() == 0) { failed_ = true; done_ = true; return; }
  if (LittleFS.exists(tmpPath_)) LittleFS.remove(tmpPath_);  // stale tmp
  invalidateFree();
  file_ = LittleFS.open(tmpPath_, "w");
  if (!file_) { failed_ = true; done_ = true; return; }
  open_ = true;
}

SlotWriter::~SlotWriter() { abort(); }

bool SlotWriter::write(const uint8_t* data, size_t len) {
  if (!open_ || done_ || failed_) return false;
  if (len == 0) return true;
  if (!data) return false;
  size_t w = file_.write(data, len);
  invalidateFree();  // the file system changed under any cached free-space value
  if (w != len) {
    failed_ = true;  // e.g. out of space; commit() will refuse
    return false;
  }
  written_ += w;
  return true;
}

bool SlotWriter::commit() {
  if (done_ || !open_ || failed_ || written_ == 0) {
    abort();
    return false;
  }
  file_.close();
  open_ = false;
  // Verify the tmp file is complete before touching the old content.
  File t = LittleFS.open(tmpPath_, "r");
  size_t sz = t ? t.size() : 0;
  if (t) t.close();
  if (sz != written_) {
    abort();
    return false;
  }
  const char* dst = slotlogic::slotPath(slot_);
  // No remove-then-rename fallback: LittleFS rename atomically replaces an
  // existing destination, and a failed rename must never delete the old slot.
  bool ok = LittleFS.rename(tmpPath_, dst);
  if (!ok) {
    abort();
    return false;
  }
  done_ = true;
  committed_ = true;
  invalidateFree();  // the song's name is saved next (setSlotName warms the cache)
  return true;
}

void SlotWriter::abort() {
  if (committed_) return;  // tmp was renamed away; never touch another writer's tmp
  if (open_) {
    file_.close();
    open_ = false;
  }
  if (tmpPath_.length() && LittleFS.exists(tmpPath_)) {
    LittleFS.remove(tmpPath_);
    invalidateFree();
    warmFree();
  }
  done_ = true;
}

}  // namespace storage
