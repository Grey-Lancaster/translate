// Code by Grey and my buddy Claude AI
//
// TODO: these pins are PLACEHOLDERS copied from the generic CYD
// (ESP32-2432S028R) reference wiring, NOT confirmed against the actual
// Freenove FNK0102 board. Replace once we have the board in hand /
// Freenove's schematic PDF. Touch calibration (TOUCH_*_MIN/MAX) is also
// a placeholder guess and needs a real calibration pass on hardware.
#pragma once

// --- Display: ILI9341 240x320 (driven via TFT_eSPI, build_flags in platformio.ini) ---
static const uint16_t SCREEN_W = 320; // landscape, rotation 3 -- matches CYD reference
static const uint16_t SCREEN_H = 240;

// --- Touch: XPT2046 (resistive) ---
#define XPT2046_IRQ  36 // TODO: confirm on Freenove board
#define XPT2046_MOSI 32 // TODO: confirm on Freenove board
#define XPT2046_MISO 39 // TODO: confirm on Freenove board
#define XPT2046_CLK  25 // TODO: confirm on Freenove board
#define XPT2046_CS   33 // TODO: confirm on Freenove board

static const int TOUCH_X_MIN = 200;  // TODO: calibrate on hardware
static const int TOUCH_X_MAX = 3700; // TODO: calibrate on hardware
static const int TOUCH_Y_MIN = 240;  // TODO: calibrate on hardware
static const int TOUCH_Y_MAX = 3800; // TODO: calibrate on hardware

// --- ES8311 audio codec (added in v0.4.5) ---
// TODO: I2C (SDA/SCL) and I2S (BCLK/WS/DOUT/DIN) pins, once confirmed.
