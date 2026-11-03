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
  size_t size() const { return v_.size(); }

 private:
  std::vector<Entry> v_;
  size_t cap_;
};

}  // namespace btlogic
