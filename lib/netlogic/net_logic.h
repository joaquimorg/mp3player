#pragma once
// Pure helpers for the web API (no Arduino dependency, unit-testable natively).
#include <cstddef>
#include <cstdint>
#include <string>
#include "slot_logic.h"

namespace netlogic {

constexpr size_t kMaxNameBytes = 48;
// Bytes of a multipart body that are not file data (boundaries + part headers),
// subtracted from Content-Length to estimate the file size. Deliberately
// generous: the streaming loop still enforces the free-space check.
constexpr size_t kMultipartSlack = 400;

struct SlotInfo {
  size_t size;       // 0 if empty
  std::string name;  // UTF-8
};

// Upload file name -> slot name: strips directories, control chars, '<' '>',
// (non-UTF-8 input is read as Latin-1), a trailing ".mp3", trims blanks, caps at kMaxNameBytes
// without splitting a UTF-8 sequence. May return "" (caller keeps default).
std::string sanitizeFileName(const std::string& in);

std::string jsonEscape(const std::string& s);
std::string jsonError(const std::string& msg);  // {"error":"..."}
std::string buildSlotsJson(size_t freeBytes, const SlotInfo slots[slotlogic::kNumSlots]);

// Estimated file size from a multipart Content-Length (0 if unknown/invalid).
size_t estimateFileSize(long contentLength);

// Portuguese (Portugal) message for a failed check. `overwrite` = slot already
// holds a file (an overwrite needs old+new on flash, so NoSpace means "delete first").
const char* uploadErrorMessage(slotlogic::UploadCheck c, bool overwrite);

// "Não cabe: o ficheiro precisa de X MB e há Y MB livres. ..." (decimal comma).
std::string noSpaceMessage(size_t needBytes, size_t freeBytes, bool overwrite);

// Slot number from a one-character path tail ("1".."9"); 0 if invalid.
int parseSlotNumber(const std::string& s);

// Joining the saved network: each attempt gets kConnectAttemptMs; after a failed
// attempt the join is restarted from scratch, and only after kConnectAttempts
// failures does the watch fall back to the setup access point. (Without saved
// credentials the access point starts at once.)
constexpr uint32_t kConnectAttemptMs = 15000;
constexpr int kConnectAttempts = 3;
enum class ConnectStep { Wait, Retry, FallbackAp };
// `attempt` is 1-based; `elapsedMs` is the time since that attempt started.
ConnectStep connectStep(int attempt, uint32_t elapsedMs);

// An upload body that has not delivered a single byte for this long is dropped.
// The WebServer body loop has no read timeout: a connection that stays open but
// stops sending (peer alive, link stalled) would otherwise block the single-threaded
// server, and every later request, until the peer closes it. Windows gives up on its
// own after ~30 s of unanswered retransmissions; an upload that stalled for 64 s and
// then completed was observed, so the limit stays above that.
constexpr uint32_t kUploadStallMs = 75000;

// true when an upload whose last data arrived at `lastDataMs` has been silent for
// more than `limitMs` at `nowMs` (millis() values, wrap-around safe).
bool uploadStalled(uint32_t nowMs, uint32_t lastDataMs, uint32_t limitMs);

}  // namespace netlogic
