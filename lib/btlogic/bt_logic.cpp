#include "bt_logic.h"

#include <cstdio>
#include <cstring>

namespace btlogic {

bool isListedCod(uint32_t cod) {
  if (!codValid(cod)) return false;
  if (codMajor(cod) == kMajorAudioVideo) return true;
  return (cod & (kSrvcRendering | kSrvcAudio | kSrvcTelephony)) != 0;
}

const char* majorName(uint32_t cod) {
  switch (codMajor(cod)) {
    case 0x00: return "Misc";
    case 0x01: return "Computer";
    case 0x02: return "Phone";
    case 0x03: return "LAN";
    case 0x04: return "Audio/Video";
    case 0x05: return "Peripheral";
    case 0x06: return "Imaging";
    case 0x07: return "Wearable";
    case 0x08: return "Toy";
    case 0x09: return "Health";
    case 0x1F: return "Uncategorized";
    default: return "Reserved";
  }
}

const char* avMinorName(uint32_t cod) {
  if (codMajor(cod) != kMajorAudioVideo) return "";
  static const char* const kNames[] = {
      "uncategorized", "headset", "hands-free", "reserved", "microphone", "loudspeaker",
      "headphones", "portable audio", "car audio", "set-top box", "hifi audio", "vcr",
      "video camera", "camcorder", "video monitor", "video display+loudspeaker",
      "video conferencing", "reserved", "gaming/toy"};
  uint32_t m = codMinor(cod);
  return m < sizeof(kNames) / sizeof(kNames[0]) ? kNames[m] : "reserved";
}

std::string serviceNames(uint32_t cod) {
  static const struct {
    int bit;
    const char* name;
  } kBits[] = {{13, "limited-discoverable"}, {16, "positioning"}, {17, "networking"},
               {18, "rendering"},           {19, "capturing"},   {20, "object-transfer"},
               {21, "audio"},               {22, "telephony"},   {23, "information"}};
  std::string out;
  for (const auto& b : kBits) {
    if (cod & (1u << b.bit)) {
      if (!out.empty()) out += ',';
      out += b.name;
    }
  }
  return out;
}

std::string addrText(const uint8_t a[6]) {
  char buf[18];
  snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3], a[4], a[5]);
  return buf;
}

std::string displayName(const std::string& name, const uint8_t a[6]) {
  return name.empty() ? addrText(a) : name;
}

bool DeviceList::upsert(const uint8_t addr[6], const std::string& name, int rssi) {
  for (auto& e : v_) {
    if (memcmp(e.addr, addr, 6) == 0) {
      e.rssi = rssi;
      if (name.empty() || name == e.name) return false;
      e.name = name;
      return true;
    }
  }
  if (v_.size() >= cap_) return false;
  Entry e;
  memcpy(e.addr, addr, 6);
  e.name = name;
  e.rssi = rssi;
  v_.push_back(e);
  return true;
}

bool DeviceList::clearExcept(const uint8_t* keep) {
  size_t before = v_.size();
  std::vector<Entry> kept;
  if (keep) {
    for (auto& e : v_)
      if (memcmp(e.addr, keep, 6) == 0) kept.push_back(e);
  }
  v_.swap(kept);
  return v_.size() != before;
}

std::vector<std::string> DeviceList::names() const {
  std::vector<std::string> out;
  out.reserve(v_.size());
  for (const auto& e : v_) out.push_back(displayName(e.name, e.addr));
  return out;
}

const DeviceList::Entry* DeviceList::findByAddr(const uint8_t a[6]) const {
  for (const auto& e : v_)
    if (memcmp(e.addr, a, 6) == 0) return &e;
  return nullptr;
}

const DeviceList::Entry* DeviceList::findByDisplay(const std::string& display) const {
  for (const auto& e : v_)
    if (displayName(e.name, e.addr) == display) return &e;
  return nullptr;
}

}  // namespace btlogic
