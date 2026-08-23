// Code by Grey and my buddy Claude AI
//
// Microphone test: ES8311 codec bring-up (I2C config + I2S data path),
// live mic-to-speaker passthrough -- talk into the mic and you should hear
// yourself immediately through the speaker. CONFIRMED WORKING on hardware.
//
// Status and peak audio level are re-printed periodically (not just once
// in setup()) -- native USB CDC serial capture has repeatedly lost
// one-shot output to timing races this session, so this makes status
// catchable no matter when a monitor attaches.
//
// Uses the OLDER bundled Arduino-ESP32 I2S.h library (I2SClass global
// `I2S` object), not the newer ESP_I2S.h/recordWAV()/playWAV() API
// Freenove's own example uses -- our pinned toolchain doesn't have that
// newer header available. That wrapper class has no API to configure a
// separate MCLK pin, but the legacy ESP-IDF driver/i2s.h it's built on
// DOES support one -- see the direct esp_i2s::i2s_set_pin() call below.
// A real MCLK signal on GPIO4 turned out to be required for the speaker:
// deriving the codec's clock from SCLK/BCLK instead (mclk_from_mclk_pin =
// false) let the mic work but left the speaker silent, even though codec
// init reported success. See project memory for the full debugging story.

#include <Arduino.h>
#include <I2S.h>
#include <Wire.h>

#include "es8311.h"

// I2S pins (FNK0104AB/S -- do not use the FNK0104N 3.5" variant's different pins)
#define I2S_MCK 4
#define I2S_BCK 5
#define I2S_WS 7
#define I2S_DINT 6
#define I2S_DOUT 8
#define AP_ENABLE 1

// I2C pins -- same bus as the FT6336U touch controller (different address)
#define I2C_SCL 15
#define I2C_SDA 16
#define I2C_SPEED 400000

static const long SAMPLE_RATE = 44100; // must match es8311_codec_init()'s EXAMPLE_SAMPLE_RATE
static const int BITS_PER_SAMPLE = 16;

uint8_t *audioBuffer;
size_t audioBufferBytes;
String statusLine = "not started yet";
int16_t peakLevel = 0;
unsigned long lastReportMs = 0;

// Step-by-step progress printed directly from setup() (with an explicit
// flush after each line) rather than only via the periodic loop() report --
// if any single init call below hangs, this is the only way to see how far
// we got before the hang, since loop() would never be reached to print
// statusLine at all.
void step(const char *msg) {
  Serial.println(msg);
  Serial.flush();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  step("setup: starting");

  // AP_ENABLE polarity tested both ways (LOW matches Freenove's own
  // example; HIGH was tried as a diagnostic) -- neither changed the
  // silent-speaker symptom, so reverting to Freenove's documented LOW.
  pinMode(AP_ENABLE, OUTPUT);
  digitalWrite(AP_ENABLE, LOW);
  step("setup: AP_ENABLE set low");

  Wire.begin(I2C_SDA, I2C_SCL, I2C_SPEED);
  step("setup: Wire.begin done");

  if (!I2S.setDuplex()) {
    statusLine = "ERROR - could not set duplex";
    step("setup: I2S.setDuplex FAILED");
    return;
  }
  step("setup: I2S.setDuplex done");

  I2S.setAllPins(I2S_BCK, I2S_WS, -1, I2S_DOUT, I2S_DINT); // sck, fs, sd(unused in duplex), outSd, inSd
  step("setup: I2S.setAllPins done");

  if (!I2S.begin(I2S_PHILIPS_MODE, SAMPLE_RATE, BITS_PER_SAMPLE)) {
    statusLine = "ERROR - failed to initialize I2S";
    step("setup: I2S.begin FAILED");
    return;
  }
  step("setup: I2S.begin done");

  // Re-apply pin config directly via the legacy ESP-IDF driver to add a
  // real MCLK output on GPIO4 -- I2S.begin() above already configured
  // BCK/WS/DIN/DOUT correctly, this call only adds the MCLK pin the
  // Arduino wrapper doesn't know how to set. Arduino's I2S.h wraps the
  // whole ESP-IDF driver in an `esp_i2s` namespace, so we reach through
  // that instead of including driver/i2s.h's top-level names ourselves.
  {
    esp_i2s::i2s_pin_config_t mclk_pin_config = {
      .mck_io_num = I2S_MCK,
      .bck_io_num = I2S_BCK,
      .ws_io_num = I2S_WS,
      .data_out_num = I2S_DOUT,
      .data_in_num = I2S_DINT
    };
    if (esp_i2s::i2s_set_pin((esp_i2s::i2s_port_t)0, &mclk_pin_config) != ESP_OK) {
      statusLine = "ERROR - i2s_set_pin (MCLK) FAILED";
      step("setup: i2s_set_pin MCLK FAILED");
      return;
    }
  }
  step("setup: MCLK pin (GPIO4) applied");

  if (es8311_codec_init() != ESP_OK) {
    statusLine = "ERROR - ES8311 codec init failed";
    step("setup: es8311_codec_init FAILED");
    return;
  }
  step("setup: es8311_codec_init done");

  audioBufferBytes = I2S.getBufferSize() * (BITS_PER_SAMPLE / 8);
  audioBuffer = (uint8_t *)malloc(audioBufferBytes);
  if (audioBuffer == NULL) {
    statusLine = "ERROR - failed to allocate audio buffer";
    step("setup: audio buffer malloc FAILED");
    return;
  }
  step("setup: audio buffer allocated");

  statusLine = "OK - running passthrough, talk into the mic";
  step("setup: complete, entering loop");
}

void loop() {
  if (statusLine.startsWith("ERROR") || statusLine == "not started yet") {
    Serial.println(statusLine);
    delay(1000);
    return;
  }

  I2S.read(audioBuffer, audioBufferBytes);

  int16_t *samples = (int16_t *)audioBuffer;
  size_t sampleCount = audioBufferBytes / 2;
  int16_t framePeak = 0;
  for (size_t i = 0; i < sampleCount; i++) {
    int16_t v = samples[i];
    if (v < 0) v = -v;
    if (v > framePeak) framePeak = v;
  }
  if (framePeak > peakLevel) peakLevel = framePeak;

  I2S.write(audioBuffer, audioBufferBytes);

  if (millis() - lastReportMs > 500) {
    Serial.printf("%s | mic peak level (since last report): %d / 32767\n", statusLine.c_str(), peakLevel);
    peakLevel = 0;
    lastReportMs = millis();
  }
}
