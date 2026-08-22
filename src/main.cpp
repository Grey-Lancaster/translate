// Code by Grey and my buddy Claude AI
//
// v0.4.2 - toolchain bring-up only: proves the PlatformIO/Arduino build and
// upload path works on the Freenove ESP32-S3 board before any display, wifi,
// or audio code is added (those land in v0.4.3+).

#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("translate firmware v0.4.2 - toolchain check OK");
}

void loop() {
  Serial.println("alive");
  delay(2000);
}
