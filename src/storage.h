#pragma once
#include <Arduino.h>
#include <FS.h>

// Slot storage on LittleFS. Slots are 1..4 (slotlogic::slotPath).
namespace storage {

// Mounts LittleFS (formatting on first use) and removes stale /slotN.tmp files.
bool begin();
bool hasFile(int slot);
size_t slotSize(int slot);   // 0 if absent
size_t freeBytes();
void hasFileAll(bool out[4]);
bool remove(int slot);       // deletes the file and clears its name
fs::File openRead(int slot);

// Names persisted in /names.json (UTF-8, capped at 48 bytes).
String slotName(int slot);   // "Faixa N" if unset
void setSlotName(int slot, const String& name);

// Atomic writer: data goes to /slotN.tmp; commit() replaces /slotN.mp3.
// An aborted/dropped writer leaves the previous slot content untouched.
// The destructor aborts an uncommitted writer (RAII).
class SlotWriter {
 public:
  explicit SlotWriter(int slot);
  ~SlotWriter();
  SlotWriter(const SlotWriter&) = delete;
  SlotWriter& operator=(const SlotWriter&) = delete;

  bool write(const uint8_t* data, size_t len);  // false after abort/commit/failure
  bool commit();                                // false if never written/aborted/failed
  void abort();                                 // idempotent; no-op after commit
  size_t written() const { return written_; }

 private:
  int slot_;
  bool open_ = false;       // tmp file is open
  bool failed_ = false;     // a write failed or open failed
  bool done_ = false;       // committed or aborted
  bool committed_ = false;
  size_t written_ = 0;
  fs::File file_;
  String tmpPath_;
};

}  // namespace storage
