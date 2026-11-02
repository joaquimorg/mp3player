#define LILYGO_WATCH_2020_V1
#include <LilyGoWatch.h>
#include <LittleFS.h>
#include "config.h"
#include "storage.h"
#include "settings.h"

#ifndef UNIT_TEST

#ifdef TASK4_HARNESS
#include "harness_task4.h"
#endif

TTGOClass *watch = nullptr;

void setup() {
#ifdef TASK4_HARNESS
  Serial.setRxBufferSize(4096);
#endif
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
  }
#ifdef TASK4_HARNESS
  harness::setup();
#endif
}

void loop() {
#ifdef TASK4_HARNESS
  harness::loop();
#else
  delay(1000);
#endif
}
#endif  // UNIT_TEST
