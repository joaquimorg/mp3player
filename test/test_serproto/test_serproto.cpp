// Serial protocol library (pure). On the watch: pio test -e watch_test -f test_serproto
#include <unity.h>
#include <cstring>
#include <memory>
#include <string>
#include "serproto.h"

using namespace serproto;
using Result = BlockReceiver::Result;

void setUp() {}
void tearDown() {}

static bool parse(const std::string& s, Parsed& p) { return parseLine(s.data(), s.size(), p); }

// ---- CRC / hex ----
void test_crc16() { TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16ccitt((const uint8_t*)"123456789", 9)); }
void test_crc32() {
  uint32_t c = crc32Update(0xFFFFFFFFu, (const uint8_t*)"123456789", 9);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Final(c));
  // chunked == one shot
  uint32_t d = crc32Update(0xFFFFFFFFu, (const uint8_t*)"1234", 4);
  d = crc32Update(d, (const uint8_t*)"56789", 5);
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Final(d));
}
void test_hex() {
  uint8_t o[4];
  size_t n = 99;
  TEST_ASSERT_TRUE(hexDecode("00fFa1", o, 4, n));
  TEST_ASSERT_EQUAL_UINT32(3, n);
  TEST_ASSERT_EQUAL_HEX8(0xFF, o[1]);
  TEST_ASSERT_TRUE(hexDecode("", o, 4, n));
  TEST_ASSERT_EQUAL_UINT32(0, n);
  TEST_ASSERT_FALSE(hexDecode("abc", o, 4, n));    // odd
  TEST_ASSERT_FALSE(hexDecode("zz", o, 4, n));     // bad digit
  TEST_ASSERT_FALSE(hexDecode("0102030405", o, 4, n));  // over cap
}

// ---- parser ----
void test_hello_cancel_list() {
  Parsed p;
  TEST_ASSERT_TRUE(parse("TWATCH?", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Hello);
  TEST_ASSERT_TRUE(parse("CANCELAR", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Cancel);
  TEST_ASSERT_TRUE(parse("LISTA", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::List);
  TEST_ASSERT_FALSE(parse("LISTA x", p));
  TEST_ASSERT_FALSE(parse("twatch?", p));
}
void test_wifi_basic() {
  Parsed p;
  TEST_ASSERT_TRUE(parse("WIFI casa\tabc12345", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Wifi);
  TEST_ASSERT_EQUAL_STRING("casa", p.ssid);
  TEST_ASSERT_EQUAL_STRING("abc12345", p.pass);
  TEST_ASSERT_TRUE(parse("WIFI? casa\tabc12345", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::WifiCheck);
  TEST_ASSERT_EQUAL_STRING("casa", p.ssid);
}
void test_wifi_spaces() {
  Parsed p;
  TEST_ASSERT_TRUE(parse("WIFI minha rede\tcom espacos 1", p));
  TEST_ASSERT_EQUAL_STRING("minha rede", p.ssid);
  TEST_ASSERT_EQUAL_STRING("com espacos 1", p.pass);
  TEST_ASSERT_TRUE(parse("WIFI  x \t  12345678 ", p));  // leading/trailing blanks kept exactly
  TEST_ASSERT_EQUAL_STRING(" x ", p.ssid);
  TEST_ASSERT_EQUAL_STRING("  12345678 ", p.pass);
}
void test_wifi_pass_special_bytes() {
  Parsed p;
  TEST_ASSERT_TRUE(parse("WIFI r&\"e\tp&\"\xC3\xA9 'x'<>\\%", p));
  TEST_ASSERT_EQUAL_STRING("r&\"e", p.ssid);
  TEST_ASSERT_EQUAL_STRING("p&\"\xC3\xA9 'x'<>\\%", p.pass);
  TEST_ASSERT_TRUE(parse("WIFI r\xFF\xE9\tpass\xFF\xFE\x80word", p));  // non-UTF-8 bytes kept verbatim
  TEST_ASSERT_EQUAL_STRING("r\xFF\xE9", p.ssid);
  TEST_ASSERT_EQUAL_STRING("pass\xFF\xFE\x80word", p.pass);
  // password containing a TAB: only the first TAB separates
  TEST_ASSERT_TRUE(parse("WIFI a\tb\tcdefgh", p));
  TEST_ASSERT_EQUAL_STRING("a", p.ssid);
  TEST_ASSERT_EQUAL_STRING("b\tcdefgh", p.pass);
}
void test_wifi_limits() {
  Parsed p;
  TEST_ASSERT_FALSE(parse("WIFI x\t1234567", p));
  TEST_ASSERT_EQUAL_STRING("wifi", p.err);
  TEST_ASSERT_TRUE(parse("WIFI x\t12345678", p));            // 8
  TEST_ASSERT_TRUE(parse("WIFI x\t" + std::string(63, 'p'), p));  // 63
  TEST_ASSERT_EQUAL_UINT32(63, strlen(p.pass));
  TEST_ASSERT_FALSE(parse("WIFI x\t" + std::string(64, 'p'), p));
  TEST_ASSERT_EQUAL_STRING("wifi", p.err);
  TEST_ASSERT_TRUE(parse("WIFI x\t", p));                    // open network
  TEST_ASSERT_EQUAL_STRING("", p.pass);
  TEST_ASSERT_TRUE(parse("WIFI " + std::string(32, 's') + "\t", p));
  TEST_ASSERT_EQUAL_UINT32(32, strlen(p.ssid));
  TEST_ASSERT_FALSE(parse("WIFI " + std::string(33, 's') + "\t", p));
  TEST_ASSERT_EQUAL_STRING("wifi", p.err);
  TEST_ASSERT_FALSE(parse("WIFI \tabcdefgh", p));            // empty ssid
  TEST_ASSERT_EQUAL_STRING("wifi", p.err);
  TEST_ASSERT_FALSE(parse("WIFI x\ta\x01" "bcdefgh" + std::string(1, '\0'), p));  // NUL inside
}
void test_wifi_no_tab() {
  Parsed p;
  TEST_ASSERT_FALSE(parse("WIFI casa abc12345", p));
  TEST_ASSERT_EQUAL_STRING("argumentos", p.err);
  TEST_ASSERT_FALSE(parse("WIFI", p));
  TEST_ASSERT_EQUAL_STRING("argumentos", p.err);
  TEST_ASSERT_FALSE(parse("WIFI?", p));
  TEST_ASSERT_EQUAL_STRING("argumentos", p.err);
  TEST_ASSERT_FALSE(parse("WIFI? casa", p));
  TEST_ASSERT_EQUAL_STRING("argumentos", p.err);
}
void test_put() {
  Parsed p;
  TEST_ASSERT_TRUE(parse("PUT 1 1000 DEADBEEF 616263", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Put);
  TEST_ASSERT_EQUAL_INT(1, p.slot);
  TEST_ASSERT_EQUAL_UINT32(1000, p.size);
  TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, p.crc32);
  TEST_ASSERT_EQUAL_STRING("abc", p.name);
  TEST_ASSERT_TRUE(parse("PUT 4 4400000 deadbeef c3a7c3a3", p));  // "çã" UTF-8, lower-case hex
  TEST_ASSERT_EQUAL_INT(4, p.slot);
  TEST_ASSERT_EQUAL_STRING("\xC3\xA7\xC3\xA3", p.name);
  std::string max(96, '4');  // 48 bytes of 0x44
  TEST_ASSERT_TRUE(parse("PUT 2 5 00000000 " + max, p));
  TEST_ASSERT_EQUAL_UINT32(48, strlen(p.name));
}
void test_put_errors() {
  Parsed p;
  const char* bad[] = {
      "PUT 5 1000 DEADBEEF 616263", "PUT 0 1000 DEADBEEF 616263", "PUT x 1000 DEADBEEF 616263",
      "PUT 1 0 DEADBEEF 616263", "PUT 1 -5 DEADBEEF 616263", "PUT 1 99999999999 DEADBEEF 616263",
      "PUT 1 1000 DEADBEE 616263", "PUT 1 1000 DEADBEEFF 616263", "PUT 1 1000 DEADBEEG 616263",
      "PUT 1 1000 DEADBEEF 61626", "PUT 1 1000 DEADBEEF 6g", "PUT 1 1000 DEADBEEF ",
      "PUT 1 1000 DEADBEEF 00", "PUT 1 1000 DEADBEEF", "PUT 1 1000", "PUT", "PUT ",
      "PUT 1 1000 DEADBEEF 616263 extra", "PUT  1 1000 DEADBEEF 616263"};
  for (const char* s : bad) {
    TEST_ASSERT_FALSE_MESSAGE(parse(s, p), s);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("argumentos", p.err, s);
  }
  TEST_ASSERT_FALSE(parse("PUT 1 5 00000000 " + std::string(98, '4'), p));  // 49 bytes
  TEST_ASSERT_EQUAL_STRING("argumentos", p.err);
}
void test_mode_baud() {
  Parsed p;
  TEST_ASSERT_TRUE(parse("MODO PROG", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::ModeProg);
  TEST_ASSERT_TRUE(parse("MODO PLAY", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::ModePlay);
  TEST_ASSERT_FALSE(parse("MODO", p));
  TEST_ASSERT_FALSE(parse("MODO PROGX", p));
  TEST_ASSERT_FALSE(parse("MODO prog", p));
  TEST_ASSERT_TRUE(parse("BAUD 921600", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Baud);
  TEST_ASSERT_EQUAL_INT(921600, p.baud);
  TEST_ASSERT_TRUE(parse("BAUD 115200", p));
  TEST_ASSERT_EQUAL_INT(115200, p.baud);
  TEST_ASSERT_FALSE(parse("BAUD 9600", p));
  TEST_ASSERT_EQUAL_STRING("argumentos", p.err);
  TEST_ASSERT_FALSE(parse("BAUD", p));
  TEST_ASSERT_FALSE(parse("BAUD 921600 ", p));
}
void test_oversize_line() {
  Parsed p;
  TEST_ASSERT_FALSE(parse(std::string(201, 'A'), p));
  TEST_ASSERT_EQUAL_STRING("tamanho", p.err);
  TEST_ASSERT_FALSE(parse("WIFI x\t" + std::string(300, 'p'), p));
  TEST_ASSERT_EQUAL_STRING("tamanho", p.err);
  TEST_ASSERT_TRUE(parse("WIFI x\t" + std::string(63, 'p'), p));  // 70 bytes fine
  std::string s = "WIFI ";
  s += std::string(32, 's');
  s += '\t';
  s += std::string(63, 'p');  // 101 bytes: long but valid
  TEST_ASSERT_TRUE(parse(s, p));
  std::string big(200, 'A');  // exactly 200: not "tamanho"
  TEST_ASSERT_FALSE(parse(big, p));
  TEST_ASSERT_EQUAL_STRING("comando", p.err);
  TEST_ASSERT_FALSE(parse(std::string(10000, 'A'), p));
  TEST_ASSERT_EQUAL_STRING("tamanho", p.err);
}
void test_junk_never_crashes() {
  Parsed p;
  TEST_ASSERT_FALSE(parse(std::string("\xFF\x00xx", 4), p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Unknown);
  TEST_ASSERT_NOT_NULL(p.err);
  TEST_ASSERT_FALSE(parse("", p));
  TEST_ASSERT_EQUAL_STRING("comando", p.err);
  TEST_ASSERT_FALSE(parseLine(nullptr, 0, p));
  // half lines / prefixes of every command
  const char* halves[] = {"TWAT", "TWATCH", "WIF", "WIFI?x", "MOD", "MODO P", "BAU", "PU", "PUT 1 1000 DEADBEEF 6",
                          "CANCEL", "LIST", "\t", " ", "WIFI \t", "WIFI?\t", "\r", "OK", "ets Jun  8 2016 00:22:57"};
  for (const char* s : halves) {
    bool ok = parseLine(s, strlen(s), p);
    TEST_ASSERT_TRUE(ok || p.err != nullptr);
    if (!ok) TEST_ASSERT_TRUE(p.cmd == Cmd::Unknown || p.cmd == Cmd::Wifi || p.cmd == Cmd::WifiCheck || p.cmd == Cmd::Put);
  }
  // every possible byte as a whole line, and pseudo-random garbage up to 260 bytes
  for (int c = 0; c < 256; c++) {
    char ch = (char)c;
    bool ok = parseLine(&ch, 1, p);
    TEST_ASSERT_TRUE(!ok && p.err != nullptr);
  }
  uint32_t r = 12345;
  char buf[260];
  for (int round = 0; round < 200; round++) {
    size_t len = round + 1 > 260 ? 260 : (size_t)round + 1;
    for (size_t i = 0; i < len; i++) { r = r * 1664525u + 1013904223u; buf[i] = (char)(r >> 24); }
    bool ok = parseLine(buf, len, p);
    TEST_ASSERT_TRUE(ok || p.err != nullptr);
  }
  // failed parse after a good one leaves no stale command
  TEST_ASSERT_TRUE(parse("TWATCH?", p));
  TEST_ASSERT_FALSE(parse("XYZ", p));
  TEST_ASSERT_TRUE(p.cmd == Cmd::Unknown);
}

// ---- BlockReceiver ----
static uint8_t gData[5000];
static uint8_t gStream[5000 + 8];

// builds [seq][data][crc16 hi lo]; returns frame length
static size_t frame(uint8_t seq, const uint8_t* d, size_t n, uint8_t* out) {
  out[0] = seq;
  memcpy(out + 1, d, n);
  uint16_t c = crc16ccitt(out, 1 + n);
  out[1 + n] = (uint8_t)(c >> 8);
  out[2 + n] = (uint8_t)(c & 0xFF);
  return n + 3;
}
static void fillData() {
  for (size_t i = 0; i < sizeof gData; i++) gData[i] = (uint8_t)(i * 31 + 7);
}
static uint32_t crcOf(const uint8_t* d, size_t n) { return crc32Final(crc32Update(0xFFFFFFFFu, d, n)); }

void test_block_two_blocks() {
  fillData();
  std::unique_ptr<BlockReceiver> rx(new BlockReceiver(5000));
  size_t used = 0;
  size_t f = frame(0, gData, 4096, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Ok);
  TEST_ASSERT_EQUAL_UINT32(f, used);
  TEST_ASSERT_EQUAL_UINT32(4096, rx->dataLen());
  TEST_ASSERT_EQUAL_MEMORY(gData, rx->data(), 4096);
  TEST_ASSERT_EQUAL_UINT32(4096, rx->received());
  f = frame(1, gData + 4096, 904, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Done);
  TEST_ASSERT_EQUAL_UINT32(904, rx->dataLen());
  TEST_ASSERT_EQUAL_MEMORY(gData + 4096, rx->data(), 904);
  TEST_ASSERT_EQUAL_UINT32(5000, rx->received());
  TEST_ASSERT_EQUAL_HEX32(crcOf(gData, 5000), rx->crc32());
}
void test_block_split_feeds() {
  fillData();
  std::unique_ptr<BlockReceiver> rx(new BlockReceiver(4096));
  size_t f = frame(0, gData, 4096, gStream);
  size_t used = 0, pos = 0;
  size_t cuts[3] = {1, 2000, f - 2000 - 1};  // 3 feeds + a tail of 2000 -> use 4 pieces below
  (void)cuts;
  TEST_ASSERT_TRUE(rx->feed(gStream, 1, used) == Result::NeedMore);  // only the seq
  TEST_ASSERT_EQUAL_UINT32(1, used);
  pos = 1;
  TEST_ASSERT_TRUE(rx->feed(gStream + pos, 2000, used) == Result::NeedMore);
  TEST_ASSERT_EQUAL_UINT32(2000, used);
  pos += 2000;
  TEST_ASSERT_TRUE(rx->feed(gStream + pos, f - pos - 1, used) == Result::NeedMore);  // all but last CRC byte
  pos += used;
  TEST_ASSERT_TRUE(rx->feed(gStream + pos, 1, used) == Result::Done);
  TEST_ASSERT_EQUAL_HEX32(crcOf(gData, 4096), rx->crc32());
  // one byte at a time
  std::unique_ptr<BlockReceiver> r2(new BlockReceiver(4096));
  Result res = Result::NeedMore;
  for (size_t i = 0; i < f; i++) {
    TEST_ASSERT_TRUE(res == Result::NeedMore);
    res = r2->feed(gStream + i, 1, used);
    TEST_ASSERT_EQUAL_UINT32(1, used);
  }
  TEST_ASSERT_TRUE(res == Result::Done);
  // extra bytes after the frame are not consumed
  std::unique_ptr<BlockReceiver> r3(new BlockReceiver(10));
  uint8_t two[2 * 13];
  size_t f1 = frame(0, gData, 10, two);
  memcpy(two + f1, two, f1);
  TEST_ASSERT_TRUE(r3->feed(two, 2 * f1, used) == Result::Done);
  TEST_ASSERT_EQUAL_UINT32(f1, used);
}
void test_block_bad_crc() {
  fillData();
  std::unique_ptr<BlockReceiver> rx(new BlockReceiver(5000));
  size_t used = 0;
  size_t f = frame(0, gData, 4096, gStream);
  gStream[100] ^= 0x01;  // corrupt data
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadCrc);
  TEST_ASSERT_EQUAL_UINT32(f, used);
  TEST_ASSERT_EQUAL_UINT32(0, rx->received());
  gStream[100] ^= 0x01;
  gStream[f - 1] ^= 0x80;  // corrupt crc itself
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadCrc);
  TEST_ASSERT_EQUAL_UINT32(0, rx->received());
  gStream[f - 1] ^= 0x80;  // resend of the same seq is accepted
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Ok);
  TEST_ASSERT_EQUAL_UINT32(4096, rx->received());
  f = frame(1, gData + 4096, 904, gStream);
  gStream[0] ^= 0x02;  // corrupted seq byte: wrong seq
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadSeq);
  gStream[0] ^= 0x02;
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Done);
  TEST_ASSERT_EQUAL_HEX32(crcOf(gData, 5000), rx->crc32());  // bad frames did not touch the crc32
}
void test_block_duplicate() {
  fillData();
  std::unique_ptr<BlockReceiver> rx(new BlockReceiver(9000));
  size_t used = 0;
  size_t f = frame(0, gData, 4096, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Ok);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::OkDuplicate);  // lost ACK -> resend
  TEST_ASSERT_EQUAL_UINT32(f, used);
  TEST_ASSERT_EQUAL_UINT32(4096, rx->received());  // not counted twice
  gStream[5] ^= 0x10;
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadCrc);  // duplicate with a bad crc
  TEST_ASSERT_EQUAL_UINT32(4096, rx->received());
  gStream[5] ^= 0x10;
  f = frame(1, gData, 4096, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Ok);
  TEST_ASSERT_EQUAL_UINT32(8192, rx->received());
  size_t f2 = frame(2, gData, 808, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f2, used) == Result::Done);
  // a lost ACK for the very last block: resend after Done is a duplicate, not counted
  TEST_ASSERT_TRUE(rx->feed(gStream, f2, used) == Result::OkDuplicate);
  TEST_ASSERT_EQUAL_UINT32(9000, rx->received());
  uint32_t want = crc32Final(crc32Update(crc32Update(crc32Update(0xFFFFFFFFu, gData, 4096), gData, 4096), gData, 808));
  TEST_ASSERT_EQUAL_HEX32(want, rx->crc32());
}
void test_block_wrong_seq() {
  fillData();
  std::unique_ptr<BlockReceiver> rx(new BlockReceiver(9000));
  size_t used = 0;
  size_t f = frame(1, gData, 4096, gStream);  // skipped block 0
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadSeq);
  TEST_ASSERT_TRUE(used >= 1);
  TEST_ASSERT_EQUAL_UINT32(0, rx->received());
  f = frame(0, gData, 4096, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::Ok);
  f = frame(2, gData, 4096, gStream);  // skipped block 1
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadSeq);
  TEST_ASSERT_EQUAL_UINT32(4096, rx->received());
  f = frame(0xFF, gData, 4096, gStream);
  TEST_ASSERT_TRUE(rx->feed(gStream, f, used) == Result::BadSeq);
}
void test_block_seq_wraps() {
  // 258 blocks of 1 byte would need total 258*4096; use a receiver with big total and tiny feed of full blocks
  fillData();
  std::unique_ptr<BlockReceiver> rx(new BlockReceiver(258u * 4096u));
  size_t used = 0;
  uint32_t crc = 0xFFFFFFFFu;
  for (int i = 0; i < 258; i++) {
    size_t f = frame((uint8_t)(i & 0xFF), gData, 4096, gStream);
    Result r = rx->feed(gStream, f, used);
    crc = crc32Update(crc, gData, 4096);
    TEST_ASSERT_TRUE(r == (i == 257 ? Result::Done : Result::Ok));
  }
  TEST_ASSERT_EQUAL_HEX32(crc32Final(crc), rx->crc32());
}
void test_block_total_edges() {
  fillData();
  size_t used = 99;
  // total 0: nothing to receive
  std::unique_ptr<BlockReceiver> z(new BlockReceiver(0));
  TEST_ASSERT_TRUE(z->feed(gStream, 0, used) == Result::Done);
  TEST_ASSERT_EQUAL_UINT32(0, used);
  TEST_ASSERT_TRUE(z->feed(gStream, 5, used) == Result::Done);
  TEST_ASSERT_EQUAL_UINT32(0, used);
  TEST_ASSERT_EQUAL_UINT32(0, z->received());
  TEST_ASSERT_EQUAL_HEX32(0u, z->crc32());
  // exactly 4096: one full block, then Done (no empty trailing block)
  std::unique_ptr<BlockReceiver> a(new BlockReceiver(4096));
  size_t f = frame(0, gData, 4096, gStream);
  TEST_ASSERT_TRUE(a->feed(gStream, f, used) == Result::Done);
  // exactly 8192: Ok then Done
  std::unique_ptr<BlockReceiver> b(new BlockReceiver(8192));
  TEST_ASSERT_TRUE(b->feed(gStream, f, used) == Result::Ok);
  f = frame(1, gData, 4096, gStream);
  TEST_ASSERT_TRUE(b->feed(gStream, f, used) == Result::Done);
  TEST_ASSERT_EQUAL_UINT32(8192, b->received());
  // 1 byte total
  std::unique_ptr<BlockReceiver> c(new BlockReceiver(1));
  f = frame(0, gData, 1, gStream);
  TEST_ASSERT_EQUAL_UINT32(4, f);
  TEST_ASSERT_TRUE(c->feed(gStream, f, used) == Result::Done);
  TEST_ASSERT_EQUAL_HEX32(crcOf(gData, 1), c->crc32());
  // short block at the end: sender sends 904 where 4096 is expected -> frame is misread, never Done/accepted
  std::unique_ptr<BlockReceiver> d(new BlockReceiver(4096));
  f = frame(0, gData, 904, gStream);
  TEST_ASSERT_TRUE(d->feed(gStream, f, used) == Result::NeedMore);  // waiting for more bytes
  TEST_ASSERT_EQUAL_UINT32(f, used);
  TEST_ASSERT_EQUAL_UINT32(0, d->received());
  // truncated final block (cable pulled): stays NeedMore
  std::unique_ptr<BlockReceiver> e(new BlockReceiver(5000));
  f = frame(0, gData, 4096, gStream);
  TEST_ASSERT_TRUE(e->feed(gStream, f, used) == Result::Ok);
  f = frame(1, gData + 4096, 904, gStream);
  TEST_ASSERT_TRUE(e->feed(gStream, f - 1, used) == Result::NeedMore);
  TEST_ASSERT_EQUAL_UINT32(4096, e->received());
  // full-size block where a short last one is expected: crc16 mismatch, not accepted
  std::unique_ptr<BlockReceiver> g(new BlockReceiver(5000));
  f = frame(0, gData, 4096, gStream);
  g->feed(gStream, f, used);
  f = frame(1, gData, 4096, gStream);
  Result r = g->feed(gStream, f, used);
  TEST_ASSERT_TRUE(r == Result::BadCrc);
  TEST_ASSERT_EQUAL_UINT32(4096, g->received());
}

static void runAll() {
  UNITY_BEGIN();
  RUN_TEST(test_crc16); RUN_TEST(test_crc32); RUN_TEST(test_hex);
  RUN_TEST(test_hello_cancel_list); RUN_TEST(test_wifi_basic); RUN_TEST(test_wifi_spaces);
  RUN_TEST(test_wifi_pass_special_bytes); RUN_TEST(test_wifi_limits); RUN_TEST(test_wifi_no_tab);
  RUN_TEST(test_put); RUN_TEST(test_put_errors); RUN_TEST(test_mode_baud);
  RUN_TEST(test_oversize_line); RUN_TEST(test_junk_never_crashes);
  RUN_TEST(test_block_two_blocks); RUN_TEST(test_block_split_feeds); RUN_TEST(test_block_bad_crc);
  RUN_TEST(test_block_duplicate); RUN_TEST(test_block_wrong_seq); RUN_TEST(test_block_seq_wraps);
  RUN_TEST(test_block_total_edges);
  UNITY_END();
}

#ifdef ARDUINO
#include <Arduino.h>
void setup() { delay(2000); runAll(); }
void loop() {}
#else
int main() { runAll(); return 0; }
#endif
