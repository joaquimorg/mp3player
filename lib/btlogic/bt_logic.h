#pragma once
// Pure Bluetooth device-list logic (no ESP-IDF / Arduino): Class-of-Device
// decoding, which inquiry results we list, display names and the list itself.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace btlogic {

// Class of Device service bits (raw 24-bit COD positions).
constexpr uint32_t kSrvcRendering = 1u << 18;
constexpr uint32_t kSrvcCapturing = 1u << 19;
constexpr uint32_t kSrvcAudio = 1u << 21;
constexpr uint32_t kSrvcTelephony = 1u << 22;
constexpr uint32_t kMajorAudioVideo = 0x04;

inline uint32_t codMajor(uint32_t cod) { return (cod >> 8) & 0x1F; }
inline uint32_t codMinor(uint32_t cod) { return (cod >> 2) & 0x3F; }
inline bool codValid(uint32_t cod) { return cod != 0 && (cod & 0x3) == 0; }  // format type 1

// Listed: major class Audio/Video (headsets, headphones, earbuds, speakers,
// car audio, TVs...) OR a Rendering/Audio/Telephony service bit (what
// ESP32-A2DP accepted). Invalid/zero COD is not listed.
bool isListedCod(uint32_t cod);
// Short human text for logs: "Audio/Video", "Phone", ...
const char* majorName(uint32_t cod);
// Minor class for major Audio/Video ("headset", "headphones", "loudspeaker", ...), else "".
const char* avMinorName(uint32_t cod);
// "rendering,audio" style list of the set service bits ("" if none).
std::string serviceNames(uint32_t cod);

std::string addrText(const uint8_t a[6]);  // "aa:bb:cc:dd:ee:ff"
// Name shown in the UI: the device name, or its address while it has none.
std::string displayName(const std::string& name, const uint8_t a[6]);

class DeviceList {
 public:
  struct Entry {
    uint8_t addr[6];
    std::string name;  // may be "" until EIR / remote-name request supplies it
    int rssi;
  };
  explicit DeviceList(size_t cap = 32) : cap_(cap) {}

  // Adds or updates a device. An empty name never replaces a known one.
  // Returns true if what names() returns changed.
  bool upsert(const uint8_t addr[6], const std::string& name, int rssi);
  // New search: forget every device except `keep` (the connected peer; may be
  // nullptr). Returns true if the visible list changed.
  bool clearExcept(const uint8_t* keep);
  std::vector<std::string> names() const;  // display names, discovery order
  const Entry* findByAddr(const uint8_t a[6]) const;
  const Entry* findByDisplay(const std::string& display) const;
  // Connect targets: matches the display name, the stored name or the address
  // text (a row tapped while it still showed "aa:bb:.." stays connectable).
  const Entry* findTarget(const std::string& text) const;
  size_t size() const { return v_.size(); }

 private:
  std::vector<Entry> v_;
  size_t cap_;
};

// ---- search policy ----------------------------------------------------------
// Inquiry and remote-name paging steal radio time from an A2DP stream and cause
// audible dropouts on the ESP32, so while a device is connected:
//  - opening the Bluetooth screen does NOT search (it just shows the list);
//  - "Procurar" runs ONE inquiry window (~12.8 s), without name requests, and
//    then stops by itself.
// While nothing is connected the search is continuous and unnamed devices get
// one remote-name request each.
enum class SearchTrigger { EnterScreen, Button };
struct SearchPolicy {
  bool start;         // start an inquiry at all
  bool continuous;    // restart inquiries until stopped
  bool nameRequests;  // remote-name requests for unnamed devices
  bool singleShot;    // stop by itself after one inquiry window
};
SearchPolicy searchPolicy(bool connected, SearchTrigger trigger);

// Remote-name requests of one search: at most ONE attempt per address (a
// failed request is never retried in the same search), a bounded queue and a
// bounded total. reset() at every new search.
class NameRequests {
 public:
  NameRequests(size_t maxQueued = 8, size_t maxAsked = 16) : maxQueued_(maxQueued), maxAsked_(maxAsked) {}
  void reset();
  // Queue an unnamed device; ignored if already queued/asked or a cap is hit.
  bool want(const uint8_t addr[6]);
  // Next address to ask (marked as asked); false when the queue is empty.
  bool next(uint8_t out[6]);
  size_t asked() const { return asked_.size(); }
  size_t queued() const { return queue_.size(); }

 private:
  struct Addr {
    uint8_t a[6];
  };
  static bool has(const std::vector<Addr>& v, const uint8_t a[6]);
  std::vector<Addr> queue_, asked_;
  size_t maxQueued_, maxAsked_;
};

}  // namespace btlogic
