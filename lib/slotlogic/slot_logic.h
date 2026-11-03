#pragma once
#include <cstddef>
#include <cstdint>

namespace slotlogic {

constexpr int kNumSlots = 4;
// Per-slot cap: 3 MB (decimal). The data partition is 0xBF0000 = 12,517,376 B
// (3056 LittleFS blocks of 4096 B). Four files of 3,000,000 B take 4 x 733 blocks
// (CTZ skip-list pointers cost ~0.2 % of each block) = ~2940 blocks, which leaves
// >100 blocks (>450 kB) for the root metadata pair, names.json and the next
// upload's staging file, so FOUR max-size songs always fit.
constexpr size_t kMaxFileSize = 3000000u;
// Block-granularity slack required on top of the file size when checking free space.
constexpr size_t kFsOverhead = 16384;

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
