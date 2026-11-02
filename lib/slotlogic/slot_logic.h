#pragma once
#include <cstddef>
#include <cstdint>

namespace slotlogic {

constexpr int kNumSlots = 4;
constexpr size_t kMaxFileSize = 3u * 1024u * 1024u;

enum class LoopMode : uint8_t { RepeatOne, AllSlots };
enum class UploadCheck { Ok, BadSlot, TooBig, NoSpace, NotMp3 };

// "/slot1.mp3".."/slot4.mp3"; nullptr if slot not in 1..4.
const char* slotPath(int slot);

// Slot to play when current ends (1..4), 0 if every slot is empty.
int nextSlot(int current, LoopMode mode, const bool hasFile[4]);

// Cyclic previous non-empty slot, 0 if none.
int prevSlot(int current, const bool hasFile[4]);

// Check order: BadSlot, NotMp3, TooBig, NoSpace. size 0 is rejected as NotMp3.
UploadCheck validateUpload(int slot, size_t size, size_t freeBytes, const uint8_t head[4]);

}  // namespace slotlogic
