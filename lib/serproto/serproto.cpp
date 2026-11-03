#include "serproto.h"
#include <cstring>

namespace serproto {

namespace {

bool fail(Parsed& o, const char* e) {
  o.err = e;
  return false;
}

bool eq(const char* l, size_t len, const char* lit) {
  size_t n = strlen(lit);
  return len == n && memcmp(l, lit, n) == 0;
}
bool starts(const char* l, size_t len, const char* lit) {
  size_t n = strlen(lit);
  return len >= n && memcmp(l, lit, n) == 0;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Unsigned decimal, 1..10 digits, fits uint32.
bool parseU32(const char* s, size_t n, uint32_t& v) {
  if (n == 0 || n > 10) return false;
  uint64_t x = 0;
  for (size_t i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    x = x * 10 + (uint64_t)(s[i] - '0');
  }
  if (x > 0xFFFFFFFFull) return false;
  v = (uint32_t)x;
  return true;
}

// WIFI / WIFI? arguments: "<ssid>\t<pass>" (first TAB separates; bytes kept verbatim).
bool parseWifiArgs(const char* a, size_t n, Parsed& o) {
  size_t tab = 0;
  while (tab < n && a[tab] != '\t') tab++;
  if (tab == n) return fail(o, "argumentos");
  size_t sl = tab, pl = n - tab - 1;
  if (sl < 1 || sl > 32) return fail(o, "wifi");
  if (pl != 0 && (pl < 8 || pl > 63)) return fail(o, "wifi");
  for (size_t i = 0; i < n; i++)
    if (a[i] == '\0') return fail(o, "wifi");
  memcpy(o.ssid, a, sl);
  o.ssid[sl] = 0;
  memcpy(o.pass, a + tab + 1, pl);
  o.pass[pl] = 0;
  return true;
}

// PUT arguments: "<slot> <size> <crc32 hex 8> <name hex>"
bool parsePut(const char* a, size_t n, Parsed& o) {
  const char* tok[4];
  size_t tl[4];
  size_t pos = 0;
  for (int i = 0; i < 4; i++) {
    size_t st = pos;
    while (pos < n && a[pos] != ' ') pos++;
    tok[i] = a + st;
    tl[i] = pos - st;
    if (i < 3) {
      if (pos >= n) return fail(o, "argumentos");
      pos++;
    }
  }
  if (pos != n) return fail(o, "argumentos");  // extra tokens
  uint32_t slot, size;
  if (!parseU32(tok[0], tl[0], slot) || slot < 1 || slot > 4) return fail(o, "argumentos");
  if (!parseU32(tok[1], tl[1], size) || size == 0) return fail(o, "argumentos");
  if (tl[2] != 8) return fail(o, "argumentos");
  uint32_t crc = 0;
  for (size_t i = 0; i < 8; i++) {
    int h = hexVal(tok[2][i]);
    if (h < 0) return fail(o, "argumentos");
    crc = (crc << 4) | (uint32_t)h;
  }
  if (tl[3] == 0 || tl[3] > 2 * kMaxName) return fail(o, "argumentos");
  char hex[2 * kMaxName + 1];
  memcpy(hex, tok[3], tl[3]);
  hex[tl[3]] = 0;
  uint8_t nm[kMaxName];
  size_t nn = 0;
  if (!hexDecode(hex, nm, sizeof nm, nn) || nn == 0) return fail(o, "argumentos");
  for (size_t i = 0; i < nn; i++)
    if (nm[i] == 0) return fail(o, "argumentos");
  o.slot = (int)slot;
  o.size = size;
  o.crc32 = crc;
  memcpy(o.name, nm, nn);
  o.name[nn] = 0;
  return true;
}

}  // namespace

bool parseLine(const char* line, size_t len, Parsed& out) {
  memset(&out, 0, sizeof out);
  out.cmd = Cmd::Unknown;
  out.err = nullptr;
  if (line == nullptr) return fail(out, "comando");
  if (len > kMaxLine) return fail(out, "tamanho");

  if (eq(line, len, "TWATCH?")) { out.cmd = Cmd::Hello; return true; }
  if (eq(line, len, "CANCELAR")) { out.cmd = Cmd::Cancel; return true; }
  if (eq(line, len, "LISTA")) { out.cmd = Cmd::List; return true; }

  if (eq(line, len, "WIFI?") || starts(line, len, "WIFI? ")) {
    if (len < 6) return fail(out, "argumentos");
    if (!parseWifiArgs(line + 6, len - 6, out)) return false;
    out.cmd = Cmd::WifiCheck;
    return true;
  }
  if (eq(line, len, "WIFI") || starts(line, len, "WIFI ")) {
    if (len < 5) return fail(out, "argumentos");
    if (!parseWifiArgs(line + 5, len - 5, out)) return false;
    out.cmd = Cmd::Wifi;
    return true;
  }
  if (eq(line, len, "MODO") || starts(line, len, "MODO ")) {
    if (eq(line, len, "MODO PROG")) { out.cmd = Cmd::ModeProg; return true; }
    if (eq(line, len, "MODO PLAY")) { out.cmd = Cmd::ModePlay; return true; }
    return fail(out, "argumentos");
  }
  if (eq(line, len, "BAUD") || starts(line, len, "BAUD ")) {
    if (eq(line, len, "BAUD 921600")) { out.cmd = Cmd::Baud; out.baud = 921600; return true; }
    if (eq(line, len, "BAUD 115200")) { out.cmd = Cmd::Baud; out.baud = 115200; return true; }
    return fail(out, "argumentos");
  }
  if (eq(line, len, "PUT") || starts(line, len, "PUT ")) {
    if (len < 4) return fail(out, "argumentos");
    if (!parsePut(line + 4, len - 4, out)) return false;
    out.cmd = Cmd::Put;
    return true;
  }
  return fail(out, "comando");
}

uint16_t crc16ccitt(const uint8_t* d, size_t n, uint16_t init) {
  uint16_t crc = init;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)((uint16_t)d[i] << 8);
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* d, size_t n) {
  for (size_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
  }
  return crc;
}
uint32_t crc32Final(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

bool hexDecode(const char* hex, uint8_t* out, size_t cap, size_t& n) {
  n = 0;
  if (hex == nullptr) return false;
  size_t l = strlen(hex);
  if (l % 2 != 0 || l / 2 > cap) return false;
  for (size_t i = 0; i < l; i += 2) {
    int h = hexVal(hex[i]), lo = hexVal(hex[i + 1]);
    if (h < 0 || lo < 0) { n = 0; return false; }
    out[n++] = (uint8_t)((h << 4) | lo);
  }
  return true;
}

BlockReceiver::BlockReceiver(uint32_t total)
    : total_(total), received_(0), crc_(0xFFFFFFFFu), nextSeq_(0), lastSeq_(0), haveLast_(false),
      lastLen_(0), have_(0), need_(0), dataLen_(0), dup_(false) {
  memset(buf_, 0, sizeof buf_);
}

BlockReceiver::Result BlockReceiver::feed(const uint8_t* b, size_t n, size_t& consumed) {
  consumed = 0;
  if (total_ == 0) return Result::Done;
  while (consumed < n) {
    if (have_ == 0) {  // first byte of a frame: decide which block it claims to be
      uint8_t seq = b[consumed];
      uint32_t remaining = total_ - received_;
      if (remaining > 0 && seq == nextSeq_) {
        dataLen_ = remaining < kBlockSize ? remaining : kBlockSize;
        dup_ = false;
      } else if (haveLast_ && seq == lastSeq_) {
        dataLen_ = lastLen_;
        dup_ = true;
      } else {
        consumed++;
        return Result::BadSeq;
      }
      need_ = 1 + dataLen_ + 2;
    }
    size_t take = need_ - have_;
    if (take > n - consumed) take = n - consumed;
    memcpy(buf_ + have_, b + consumed, take);
    have_ += take;
    consumed += take;
    if (have_ < need_) return Result::NeedMore;

    have_ = 0;
    uint16_t want = (uint16_t)((buf_[1 + dataLen_] << 8) | buf_[2 + dataLen_]);
    if (crc16ccitt(buf_, 1 + dataLen_) != want) return Result::BadCrc;
    if (dup_) return Result::OkDuplicate;
    crc_ = crc32Update(crc_, buf_ + 1, dataLen_);
    received_ += (uint32_t)dataLen_;
    lastSeq_ = nextSeq_;
    nextSeq_++;
    lastLen_ = dataLen_;
    haveLast_ = true;
    return received_ >= total_ ? Result::Done : Result::Ok;
  }
  return Result::NeedMore;
}

}  // namespace serproto
