#pragma once
#include <cstddef>
#include <cstdint>

namespace slotlogic {

constexpr int kNumSlots = 4;
// There is no fixed per-file cap: a file may use whatever free space is left.
// Block-granularity slack required on top of the file size when checking free space.
constexpr size_t kFsOverhead = 16384;

enum class LoopMode : uint8_t { RepeatOne, AllSlots };
enum class UploadCheck { Ok, BadSlot, NoSpace, NotMp3 };

// "/slot1.mp3".."/slot4.mp3"; nullptr if slot not in 1..4.
const char* slotPath(int slot);

// Slot to play when current ends (1..4), 0 if every slot is empty.
int nextSlot(int current, LoopMode mode, const bool hasFile[4]);

// Cyclic previous non-empty slot, 0 if none.
int prevSlot(int current, const bool hasFile[4]);

// Check order: BadSlot, NotMp3, NoSpace (size + kFsOverhead must fit in freeBytes). size 0 is rejected as NotMp3.
UploadCheck validateUpload(int slot, size_t size, size_t freeBytes, const uint8_t head[4]);

}  // namespace slotlogic
