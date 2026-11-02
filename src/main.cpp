#define LILYGO_WATCH_2020_V1
#include <LilyGoWatch.h>
#include <LittleFS.h>
#include "config.h"
#include "storage.h"
#include "settings.h"

#ifndef UNIT_TEST

TTGOClass *watch = nullptr;

// TEMPORARY Task 3 self-test (replaced in Task 6).
static void selfTest() {
  using namespace storage;
  Serial.println("[ST] start");
  static uint8_t buf[1003];
  memset(buf, 0, sizeof buf);
  memcpy(buf, "ID3", 3);
  {
    SlotWriter w(1);
    bool a = w.write(buf, 1000), c = w.commit();
    Serial.printf("[ST] write=%d commit=%d hasFile(1)=%d slotSize(1)=%u (expect 1,1,1,1000)\n", a, c, hasFile(1), (unsigned)slotSize(1));
  }
  {  // second commit replaces the first
    SlotWriter w(1);
    w.write(buf, 1003);
    bool c = w.commit();
    Serial.printf("[ST] commit 1003 = %d size=%u\n", c, (unsigned)slotSize(1));
  }
  {
    SlotWriter w(1);
    w.write(buf, 10);
    w.abort();
    size_t sz = slotSize(1);
    bool tmp = LittleFS.exists("/slot1.tmp");
    bool wr = w.write(buf, 1), cm = w.commit();
    Serial.printf("[ST] after abort: slotSize(1)=%u (expect 1003) tmpExists=%d write-after-abort=%d commit-after-abort=%d\n", (unsigned)sz, tmp, wr, cm);
  }
  {
    SlotWriter w(1);
    w.write(buf, 10);
  }  // dropped without abort -> RAII
  Serial.printf("[ST] after drop: slotSize(1)=%u tmpExists=%d (expect 1003, 0)\n", (unsigned)slotSize(1), LittleFS.exists("/slot1.tmp"));
  {
    SlotWriter w(1);
    bool c = w.commit();
    Serial.printf("[ST] commit-never-written=%d size=%u (expect 0, 1003)\n", c, (unsigned)slotSize(1));
  }
  setSlotName(1, "M\xC3\xBAsica \xC3\xA7\xC3\xA3o \"q\" \\ .mp3");
  Serial.printf("[ST] name=%s\n", slotName(1).c_str());
  bool rm = remove(1);
  Serial.printf("[ST] remove(1)=%d hasFile(1)=%d name=%s (expect Faixa 1)\n", rm, hasFile(1), slotName(1).c_str());

  if (settings::wifiSsid() != "selftest-ssid") {
    settings::setWifi("selftest-ssid", "pw");
    settings::setVolume(77);
    settings::setLoopMode(slotlogic::LoopMode::AllSlots);
    Serial.println("[ST] settings written, restarting");
    Serial.flush();
    delay(200);
    ESP.restart();
  } else {
    Serial.printf("[ST] after restart ssid=%s pass=%s vol=%u loop=%d (expect selftest-ssid pw 77 1)\n",
                  settings::wifiSsid().c_str(), settings::wifiPass().c_str(), settings::volume(), (int)settings::loopMode());
    settings::setWifi("", "");
    settings::setVolume(50);
    settings::setLoopMode(slotlogic::LoopMode::RepeatOne);
    Serial.println("[ST] done");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  watch = TTGOClass::getWatch();
  watch->begin();
  watch->openBL();

  watch->tft->fillScreen(TFT_BLACK);
  watch->tft->setTextColor(TFT_WHITE, TFT_BLACK);
  watch->tft->setTextSize(2);
  watch->tft->drawString("T-Watch MP3", 20, 100);

  if (!storage::begin()) {
    Serial.println("LittleFS mount FAILED");
  } else {
    Serial.printf("LittleFS total bytes: %u\n", (unsigned)LittleFS.totalBytes());
    Serial.printf("LittleFS used bytes: %u\n", (unsigned)LittleFS.usedBytes());
    selfTest();
  }
}

void loop() {
  delay(1000);
}
#endif  // UNIT_TEST
