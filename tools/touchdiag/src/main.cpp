// Throwaway diagnostic: why does the FT6236 touch controller (0x38 on SDA23/SCL32)
// not answer on this T-Watch 2020? Prints everything to serial (115200) and runs
// once per boot. Never touches DCDC3 (AXP202 reg 0x12 bit1 = ESP32 supply).
#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>

namespace {

constexpr uint8_t AXP = 0x35, FT = 0x38;
constexpr int SDA_T = 23, SCL_T = 32, INT_T = 38;

uint8_t axpRead(uint8_t r) {
  Wire.beginTransmission(AXP);
  Wire.write(r);
  if (Wire.endTransmission(false) != 0) return 0xEE;
  if (Wire.requestFrom((int)AXP, 1) != 1) return 0xEE;
  return Wire.read();
}

void axpWrite(uint8_t r, uint8_t v) {
  Wire.beginTransmission(AXP);
  Wire.write(r);
  Wire.write(v);
  Wire.endTransmission();
}

// Read-modify-write of the output control register that can never clear DCDC3.
void setOut(uint8_t v) { axpWrite(0x12, v | 0x02); }

void decode12(uint8_t v) {
  Serial.printf("  0x12=%02X  EXTEN=%d DCDC3=%d LDO2=%d LDO4=%d DCDC2=%d LDO3=%d\n", v, v & 1, (v >> 1) & 1,
                (v >> 2) & 1, (v >> 3) & 1, (v >> 4) & 1, (v >> 6) & 1);
}

void axpDump() {
  Serial.println("[axp] register dump");
  const uint8_t regs[] = {0x00, 0x01, 0x10, 0x12, 0x23, 0x25, 0x27, 0x28, 0x29, 0x30, 0x31, 0x32, 0x33,
                          0x36, 0x40, 0x41, 0x42, 0x43, 0x44, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x82, 0x83,
                          0x84, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99};
  for (uint8_t r : regs) Serial.printf("  reg %02X = %02X\n", r, axpRead(r));
  uint8_t v12 = axpRead(0x12);
  decode12(v12);
  uint8_t r28 = axpRead(0x28), r29 = axpRead(0x29);
  Serial.printf("  DCDC2=%umV DCDC3=%umV LDO2=%umV LDO4 code=%u LDO3=%umV (0x29 bit7 mode=%u)\n",
                700 + 25 * (axpRead(0x23) & 0x3F), 700 + 25 * (axpRead(0x27) & 0x7F), 1800 + 100 * (r28 >> 4),
                r28 & 0x0F, 700 + 25 * (r29 & 0x7F), r29 >> 7);
}

void pinLevels(const char* tag) {
  int lv[3][2];
  const int pins[2] = {SDA_T, SCL_T};
  const int modes[3] = {INPUT, INPUT_PULLDOWN, INPUT_PULLUP};
  for (int m = 0; m < 3; ++m)
    for (int p = 0; p < 2; ++p) {
      pinMode(pins[p], modes[m]);
      delay(5);
      lv[m][p] = digitalRead(pins[p]);
    }
  pinMode(SDA_T, INPUT);
  pinMode(SCL_T, INPUT);
  Serial.printf("[pins] %s: SDA23 in/pd/pu=%d/%d/%d  SCL32 in/pd/pu=%d/%d/%d  INT38=%d  IO14=%d IO15=%d\n", tag,
                lv[0][0], lv[1][0], lv[2][0], lv[0][1], lv[1][1], lv[2][1], digitalRead(INT_T), digitalRead(14),
                digitalRead(15));
}

uint8_t ftRead(uint8_t r, bool& ok) {
  Wire1.beginTransmission(FT);
  Wire1.write(r);
  ok = Wire1.endTransmission(false) == 0 && Wire1.requestFrom((int)FT, 1) == 1;
  return ok ? Wire1.read() : 0;
}

// Scans Wire1 on the given pins/frequency. Returns true when 0x38 answered.
bool scan(int sda, int scl, uint32_t hz, const char* tag) {
  Wire1.end();
  bool began = Wire1.begin(sda, scl, hz);
  Wire1.setTimeOut(50);
  int codes[6] = {0};
  String found;
  for (int a = 1; a < 127; ++a) {
    Wire1.beginTransmission(a);
    int e = Wire1.endTransmission();
    codes[e < 6 ? e : 4]++;
    if (e == 0) found += " 0x" + String(a, HEX);
  }
  bool ft = found.indexOf("0x38") >= 0;
  Serial.printf("[scan] %-34s sda=%d scl=%d %6u Hz begin=%d found:%s  (codes ok=%d nack=%d other=%d timeout=%d) "
                "lvl SDA=%d SCL=%d INT=%d\n",
                tag, sda, scl, (unsigned)hz, began, found.length() ? found.c_str() : " none", codes[0], codes[2],
                codes[4], codes[5], gpio_get_level((gpio_num_t)sda), gpio_get_level((gpio_num_t)scl),
                digitalRead(INT_T));
  if (ft) {  // equivalent of FocalTech_Class::begin() + getters
    bool o1, o2, o3, o4, o5;
    uint8_t vend = ftRead(0xA8, o1), chip = ftRead(0xA3, o2), fw = ftRead(0xA6, o3), pm = ftRead(0xA5, o4),
            td = ftRead(0x02, o5);
    Serial.printf("[ft] vendor(A8)=%02X chip(A3)=%02X fw(A6)=%02X pmode(A5)=%02X touches(02)=%02X ok=%d%d%d%d%d\n",
                  vend, chip, fw, pm, td, o1, o2, o3, o4, o5);
  }
  return ft;
}

bool std1(const char* tag) { return scan(SDA_T, SCL_T, 100000, tag); }

void busRecovery() {
  Wire1.end();
  pinMode(SDA_T, INPUT_PULLUP);
  pinMode(SCL_T, OUTPUT_OPEN_DRAIN);
  digitalWrite(SCL_T, HIGH);
  delayMicroseconds(10);
  String sdaTrace;
  for (int i = 0; i < 16; ++i) {
    digitalWrite(SCL_T, LOW);
    delayMicroseconds(10);
    digitalWrite(SCL_T, HIGH);
    delayMicroseconds(10);
    sdaTrace += digitalRead(SDA_T) ? '1' : '0';
  }
  // STOP: SDA low while SCL high, then SDA high.
  pinMode(SDA_T, OUTPUT_OPEN_DRAIN);
  digitalWrite(SDA_T, LOW);
  delayMicroseconds(10);
  digitalWrite(SDA_T, HIGH);
  delayMicroseconds(10);
  pinMode(SDA_T, INPUT);
  pinMode(SCL_T, INPUT);
  Serial.printf("[recover] 16 SCL clocks, SDA after each: %s, then STOP\n", sdaTrace.c_str());
}

void railTest(uint8_t bit, const char* name, uint8_t orig) {
  bool on = orig & (1 << bit);
  char tag[48];
  if (on) {
    setOut(orig & ~(1 << bit));
    delay(500);
    snprintf(tag, sizeof tag, "%s OFF (500ms)", name);
    std1(tag);
    setOut(orig);
    delay(400);
    snprintf(tag, sizeof tag, "%s OFF->ON (400ms)", name);
  } else {
    setOut(orig | (1 << bit));
    delay(400);
    snprintf(tag, sizeof tag, "%s turned ON (400ms)", name);
  }
  std1(tag);
  setOut(orig);
  delay(50);
}

void axpGpioPulse(uint8_t reg, const char* name) {
  uint8_t orig = axpRead(reg);
  axpWrite(reg, orig & ~0x07);  // 000 = output low
  delay(20);
  axpWrite(reg, orig);
  delay(300);
  char tag[48];
  snprintf(tag, sizeof tag, "%s low pulse 20ms (was %02X)", name, orig);
  std1(tag);
}

void runAll() {
  Serial.printf("\n===== touchdiag  reset reason %d  chip %s rev %d =====\n", (int)esp_reset_reason(),
                ESP.getChipModel(), ESP.getChipRevision());
  pinMode(INT_T, INPUT);
  pinLevels("before any Wire1");
  Wire.begin(21, 22, 400000);
  axpDump();
  {
    String f;
    for (int a = 1; a < 127; ++a) {
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) f += " 0x" + String(a, HEX);
    }
    Serial.printf("[scan] bus0 (21/22): %s\n", f.c_str());
  }

  Serial.println("--- step 1: plain scans (read-only) ---");
  std1("SDA23/SCL32");
  scan(SDA_T, SCL_T, 400000, "SDA23/SCL32");
  scan(SDA_T, SCL_T, 20000, "SDA23/SCL32 slow");
  scan(SCL_T, SDA_T, 100000, "SWAPPED");
  scan(SCL_T, SDA_T, 400000, "SWAPPED");
  Wire1.end();
  pinLevels("after Wire1.end");

  Serial.println("--- step 2e: stuck-bus recovery ---");
  busRecovery();
  pinLevels("after recovery");
  std1("after bus recovery");

  Serial.println("--- step 3: V3 hypothesis: TOUCH_RST on IO14 ---");
  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);
  delay(10);
  digitalWrite(14, HIGH);
  delay(300);
  std1("IO14 low 10ms -> HIGH held 300ms");
  digitalWrite(14, LOW);
  delay(50);
  digitalWrite(14, HIGH);
  delay(1000);
  std1("IO14 low 50ms -> HIGH held 1s");

  Serial.println("--- step 2a: AXP202 rails (DCDC3 never touched) ---");
  uint8_t orig = axpRead(0x12);
  decode12(orig);
  railTest(0, "EXTEN", orig);
  railTest(6, "LDO3", orig);
  railTest(3, "LDO4", orig);
  railTest(4, "DCDC2", orig);
  railTest(2, "LDO2", orig);

  Serial.println("--- V2 sequence: EXTEN on10/off8/on + LDO3 on ---");
  setOut(orig | 0x01);
  delay(10);
  setOut(orig & ~0x01);
  delay(8);
  setOut(orig | 0x01 | 0x40);
  delay(500);
  std1("V2 EXTEN reset + LDO3 on");

  Serial.println("--- step 2c: all optional rails on, then full cycle ---");
  setOut(orig | 0x5D);
  delay(500);
  decode12(axpRead(0x12));
  std1("ALL optional rails ON");
  setOut(orig & ~0x5D);
  delay(1500);
  decode12(axpRead(0x12));
  std1("ALL optional rails OFF 1.5s");
  pinLevels("all optional rails off");
  setOut(orig | 0x5D);
  delay(600);
  std1("ALL OFF 1.5s -> ALL ON 600ms");
  setOut(orig);
  delay(600);
  decode12(axpRead(0x12));
  std1("restored 0x12");

  Serial.println("--- AXP GPIO0..2 pulses ---");
  axpGpioPulse(0x90, "AXP GPIO0");
  axpGpioPulse(0x92, "AXP GPIO1");
  axpGpioPulse(0x93, "AXP GPIO2");

  Serial.println("--- step 2b: INT38 drive: N/A (GPIO34-39 are input-only on ESP32) ---");
  Serial.println("--- final ---");
  axpDump();
  pinLevels("final");
  std1("final");
  Serial.println("===== touchdiag DONE =====");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);
  runAll();
}

void loop() {
  static uint32_t n = 0;
  if (n < 5) {
    delay(2000);
    ++n;
    std1("idle re-scan");
  }
  delay(100);
}
