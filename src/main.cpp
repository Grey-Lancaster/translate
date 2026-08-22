// Code by Grey and my buddy Claude AI
//
// Baby-step display test: draw plain text via TFT_eSPI directly, no LVGL,
// no touch yet -- confirming the real ILI9341 pins for the FNK0104B board
// (sourced from Freenove's own shipped TFT_eSPI setup header) actually
// light up the physical panel. USE_HSPI_PORT deliberately left out of the
// build flags -- see platformio.ini comment for why.

#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);

  for (int i = 5; i > 0; i--) {
    Serial.printf("starting in %d...\n", i);
    delay(500);
  }

  Serial.println("[1] calling tft.init()");
  tft.init();
  Serial.println("[2] tft.init() returned OK");

  tft.setRotation(1);
  Serial.println("[3] setRotation OK");

  tft.fillScreen(TFT_BLACK);
  Serial.println("[4] fillScreen OK");

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(4);
  tft.setCursor(20, 20);
  tft.print("Hello Grey");
  Serial.println("[5] text drawn OK");

  Serial.println("translate firmware - Hello Grey text test OK");
}

void loop() {
  delay(1000);
}
