#pragma once
// Pure serial protocol helpers (no Arduino dependency: also compiles on a PC).
// Line commands (parseLine), CRC16/CRC32, hex, and the block framing used by PUT.
#include <cstddef>
#include <cstdint>

namespace serproto {

constexpr size_t kMaxLine = 200;     // longer lines -> err "tamanho"
constexpr size_t kBlockSize = 4096;  // max data bytes per block
constexpr size_t kMaxName = 48;      // decoded name bytes

enum class Cmd { Unknown, Hello, Wifi, WifiCheck, ModeProg, ModePlay, Baud, Put, Cancel, List };

struct Parsed {
  Cmd cmd;
  char ssid[33];    // NUL-terminated (Wifi, WifiCheck)
  char pass[64];    // NUL-terminated, may be empty
  int slot;         // PUT: 1..4
  uint32_t size;    // PUT: bytes
  uint32_t crc32;   // PUT: expected total CRC32
  char name[49];    // PUT: decoded name bytes (UTF-8 verbatim), NUL-terminated
  int baud;         // BAUD: 921600 or 115200
  const char* err;  // set on failure: "comando", "tamanho", "argumentos" or "wifi"
};

// `line` is one command without its line terminator; `len` its byte count (NULs allowed, never read past len).
// Returns false and sets out.err on failure; never crashes on junk.
bool parseLine(const char* line, size_t len, Parsed& out);

uint16_t crc16ccitt(const uint8_t* d, size_t n, uint16_t init = 0xFFFF);
// Start with crc = 0xFFFFFFFF, call crc32Update per chunk, then crc32Final.
uint32_t crc32Update(uint32_t crc, const uint8_t* d, size_t n);
uint32_t crc32Final(uint32_t crc);

// Upper/lower-case hex, even length. false on bad digit, odd length or overflow of cap.
// Empty string -> n = 0, true.
bool hexDecode(const char* hex, uint8_t* out, size_t cap, size_t& n);

// Assembles blocks `[seq][data][crc16 hi,lo]` (seq = block index mod 256, starting at 0; crc16 CCITT over
// seq+data) from a byte stream. Expected data length = min(4096, remaining). Decided from the first byte of a
// frame: seq == next -> normal block; seq == last accepted -> duplicate (lost ACK; length of the last block);
// anything else -> BadSeq (that one byte is consumed; caller should drain input and ask for a resend).
// data()/dataLen() are valid after Ok, Done and OkDuplicate (do not write on duplicates).
// total == 0: feed() always returns Done and consumes nothing.
//
// crc16 is appended BIG-ENDIAN (high byte first); the PC side must match.
//
// What the serial handler must do after each Result:
//   NeedMore    - keep reading. If no byte arrives for 3 s mid-frame: abortFrame(), send 'R'.
//   Ok / Done   - write data()/dataLen(), send 'K' (Done: finish, compare crc32()).
//   OkDuplicate - send 'K', do NOT write.
//   BadCrc      - (whole frame consumed) drain the line by silence (~10-20 ms with no bytes), abortFrame(), send 'R'.
//   BadSeq      - (only 1 byte consumed, rest of the frame still arriving) drain by silence, abortFrame(), send 'R'.
// received()/next seq are kept by abortFrame(), so the sender resends the same block.
// ~4.1 KB object (internal frame buffer): must not live on a task stack; use static or heap storage.
class BlockReceiver {
 public:
  enum class Result { NeedMore, Ok, OkDuplicate, BadCrc, BadSeq, Done };
  explicit BlockReceiver(uint32_t total);
  Result feed(const uint8_t* b, size_t n, size_t& consumed);
  const uint8_t* data() const { return buf_ + 1; }
  size_t dataLen() const { return dataLen_; }
  uint32_t received() const { return received_; }
  uint32_t crc32() const { return crc32Final(crc_); }
  // Drops a partially received frame (timeout, or after draining a bad frame). Keeps received_/seq state.
  void abortFrame() { have_ = 0; need_ = 0; dup_ = false; }

 private:
  uint32_t total_, received_;
  uint32_t crc_;
  uint8_t nextSeq_, lastSeq_;
  bool haveLast_;
  size_t lastLen_;  // data length of the last accepted block
  size_t have_;     // bytes collected of the current frame
  size_t need_;     // frame bytes expected for the current frame
  size_t dataLen_;
  bool dup_;
  uint8_t buf_[1 + kBlockSize + 2];
};

}  // namespace serproto
