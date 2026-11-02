#define LILYGO_WATCH_2020_V1
#include <LilyGoWatch.h>
#include <LittleFS.h>
#include "config.h"

TTGOClass *watch = nullptr;

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

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount FAILED");
  } else {
    Serial.printf("LittleFS total bytes: %u\n", (unsigned)LittleFS.totalBytes());
    Serial.printf("LittleFS used bytes: %u\n", (unsigned)LittleFS.usedBytes());
  }
}

void loop() {
  delay(1000);
}
