#include "slot_logic.h"

namespace slotlogic {

static bool validSlot(int s) { return s >= 1 && s <= kNumSlots; }

const char* slotPath(int slot) {
  switch (slot) {
    case 1: return "/slot1.mp3";
    case 2: return "/slot2.mp3";
    case 3: return "/slot3.mp3";
    case 4: return "/slot4.mp3";
    default: return nullptr;
  }
}

static int stepSearch(int current, int dir, const bool hasFile[4]) {
  int start = validSlot(current) ? current : (dir > 0 ? 0 : 1);
  for (int i = 1; i <= kNumSlots; ++i) {
    int idx = ((start - 1 + dir * i) % kNumSlots + kNumSlots) % kNumSlots;
    if (hasFile[idx]) return idx + 1;
  }
  return 0;
}

int nextSlot(int current, LoopMode mode, const bool hasFile[4]) {
  if (mode == LoopMode::RepeatOne && validSlot(current) && hasFile[current - 1]) return current;
  return stepSearch(current, +1, hasFile);
}

int prevSlot(int current, const bool hasFile[4]) {
  return stepSearch(current, -1, hasFile);
}

UploadCheck validateUpload(int slot, size_t size, size_t freeBytes, const uint8_t head[4]) {
  if (!validSlot(slot)) return UploadCheck::BadSlot;
  bool id3 = head[0] == 'I' && head[1] == 'D' && head[2] == '3';
  bool sync = head[0] == 0xFF && (head[1] & 0xE0) == 0xE0;
  if (size == 0 || !(id3 || sync)) return UploadCheck::NotMp3;
  if (size + kFsOverhead > freeBytes) return UploadCheck::NoSpace;
  return UploadCheck::Ok;
}

}  // namespace slotlogic
