// Code by Grey and my buddy Claude AI
//
// Freenove FNK0104B ("Freenove ESP32-S3 Display", 2.8" ILI9341 240x320,
// FT6336U capacitive touch). Display pins are CONFIRMED on real hardware
// (see platformio.ini build_flags -- this file exists as a reusable
// reference alongside them). Touch pins are sourced from Freenove's own
// shipped Sketch_18.1_Lvgl_Multifunctionality display.h but not yet
// exercised on hardware -- confirm when touch is actually wired in.
#pragma once

// --- Display: ILI9341 240x320 (SPI pins live in platformio.ini build_flags) ---
static const uint16_t SCREEN_W = 240;
static const uint16_t SCREEN_H = 320;

// --- Touch: FT6336U (I2C, capacitive) -- from Freenove's own shipped code,
// not yet confirmed on hardware ---
#define TOUCH_I2C_SDA 16
#define TOUCH_I2C_SCL 15
#define TOUCH_RST_PIN 18
#define TOUCH_INT_PIN 17

// --- Audio codec (added in v0.4.5) ---
// TODO: unconfirmed whether this board even has an onboard mic/codec --
// see [[reference-freenove-fnk0102-docs]] memory note before assuming the
// FNK0102 Media Kit's ES8311 setup applies here.
