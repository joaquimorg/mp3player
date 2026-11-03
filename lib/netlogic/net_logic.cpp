#include "net_logic.h"
#include <cstdio>

namespace netlogic {

using slotlogic::UploadCheck;

// Length of a well-formed UTF-8 sequence starting at s[i], 0 if invalid.
static size_t utf8Len(const std::string& s, size_t i) {
  uint8_t c = (uint8_t)s[i];
  size_t n;
  if (c < 0x80) return 1;
  else if (c >= 0xC2 && c <= 0xDF) n = 2;
  else if (c >= 0xE0 && c <= 0xEF) n = 3;
  else if (c >= 0xF0 && c <= 0xF4) n = 4;
  else return 0;
  if (i + n > s.size()) return 0;
  for (size_t k = 1; k < n; k++)
    if (((uint8_t)s[i + k] & 0xC0) != 0x80) return 0;
  return n;
}

std::string sanitizeFileName(const std::string& in) {
  size_t cut = in.find_last_of("/\\");
  std::string base = cut == std::string::npos ? in : in.substr(cut + 1);
  // Not valid UTF-8 (e.g. Windows tools sending the ANSI code page): read it as
  // Latin-1, which covers the Portuguese accents.
  bool valid = true;
  for (size_t i = 0; i < base.size() && valid;) {
    size_t n = utf8Len(base, i);
    if (n == 0) valid = false;
    i += n;
  }
  if (!valid) {
    std::string u;
    for (char ch : base) {
      uint8_t c = (uint8_t)ch;
      if (c < 0x80) u += ch;
      else { u += (char)(0xC0 | (c >> 6)); u += (char)(0x80 | (c & 0x3F)); }
    }
    base = u;
  }
  std::string out;
  for (size_t i = 0; i < base.size();) {
    size_t n = utf8Len(base, i);
    if (n == 0) { i++; continue; }  // drop invalid byte
    uint8_t c = (uint8_t)base[i];
    if (n == 1 && (c < 0x20 || c == 0x7F || c == '<' || c == '>')) { i++; continue; }
    out.append(base, i, n);
    i += n;
  }
  size_t a = 0, b = out.size();
  while (a < b && out[a] == ' ') a++;
  while (b > a && out[b - 1] == ' ') b--;
  out = out.substr(a, b - a);
  if (out.size() >= 4) {
    std::string ext = out.substr(out.size() - 4);
    for (auto& ch : ext) if (ch >= 'A' && ch <= 'Z') ch += 32;
    if (ext == ".mp3") out.resize(out.size() - 4);
  }
  b = out.size();
  while (b > 0 && out[b - 1] == ' ') b--;
  out.resize(b);
  if (out.size() > kMaxNameBytes) {
    size_t n = kMaxNameBytes;
    while (n > 0 && ((uint8_t)out[n] & 0xC0) == 0x80) n--;
    out.resize(n);
    while (!out.empty() && out.back() == ' ') out.pop_back();
  }
  return out;
}

std::string jsonEscape(const std::string& s) {
  std::string o;
  for (char ch : s) {
    uint8_t c = (uint8_t)ch;
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c < 0x20) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else o += ch;
  }
  return o;
}

std::string jsonError(const std::string& msg) { return "{\"error\":\"" + jsonEscape(msg) + "\"}"; }

std::string buildSlotsJson(size_t freeBytes, const SlotInfo slots[slotlogic::kNumSlots]) {
  std::string j = "{\"free\":" + std::to_string(freeBytes) + ",\"slots\":[";
  for (int i = 0; i < slotlogic::kNumSlots; i++) {
    if (i) j += ",";
    j += "{\"n\":" + std::to_string(i + 1) + ",\"size\":" + std::to_string(slots[i].size) + ",\"name\":\"" +
         jsonEscape(slots[i].name) + "\"}";
  }
  return j + "]}";
}

size_t estimateFileSize(long contentLength) {
  if (contentLength <= 0) return 0;
  size_t cl = (size_t)contentLength;
  return cl > kMultipartSlack ? cl - kMultipartSlack : 1;
}

const char* uploadErrorMessage(UploadCheck c, bool overwrite) {
  switch (c) {
    case UploadCheck::Ok: return "";
    case UploadCheck::BadSlot: return "Slot inválido (1 a 4).";
    case UploadCheck::NotMp3: return "Ficheiro inválido: só são aceites ficheiros MP3.";
    case UploadCheck::NoSpace:
      return overwrite ? "Sem espaço: apaga primeiro este slot e volta a enviar."
                       : "Sem espaço livre no relógio. Apaga outro slot e tenta de novo.";
  }
  return "Erro desconhecido.";
}

static std::string mb(size_t bytes) {
  char b[24];
  snprintf(b, sizeof b, "%.1f", (double)bytes / 1048576.0);
  for (char* p = b; *p; ++p)
    if (*p == '.') *p = ',';
  return b;
}

std::string noSpaceMessage(size_t needBytes, size_t freeBytes, bool overwrite) {
  return "Não cabe: o ficheiro precisa de " + mb(needBytes) + " MB e há " + mb(freeBytes) + " MB livres. " +
         (overwrite ? "Apaga primeiro este slot e volta a enviar." : "Apaga uma música e tenta de novo.");
}

int parseSlotNumber(const std::string& s) {
  if (s.size() != 1 || s[0] < '0' || s[0] > '9') return 0;
  return s[0] - '0';
}

ConnectStep connectStep(int attempt, uint32_t elapsedMs) {
  if (elapsedMs < kConnectAttemptMs) return ConnectStep::Wait;
  return attempt < kConnectAttempts ? ConnectStep::Retry : ConnectStep::FallbackAp;
}

bool uploadStalled(uint32_t nowMs, uint32_t lastDataMs, uint32_t limitMs) {
  return (uint32_t)(nowMs - lastDataMs) > limitMs;
}

}  // namespace netlogic
