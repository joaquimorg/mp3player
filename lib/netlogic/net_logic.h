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
// generous: the streaming loop still enforces the hard 3 MiB cap.
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

// Slot number from a one-character path tail ("1".."9"); 0 if invalid.
int parseSlotNumber(const std::string& s);

}  // namespace netlogic
