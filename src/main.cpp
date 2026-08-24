// Code by Grey and my buddy Claude AI
//
// Integration: boot logo + main "Papa Lanc Translates" screen (LVGL),
// WiFi+OTA, audio, and Groq Whisper STT.
//
// A BOOT-button-triggered sys-info screen was tried and pulled back out
// 2026-08-22: GPIO0 read a steady LOW regardless of the physical button.
// CORRECTED 2026-08-24: this was a missing internal pull-up, not a
// hardware dead end -- a separate project on this exact board (XiaoZhi AI)
// reads GPIO0/BOOT reliably by explicitly enabling the pull-up
// (active-low button, nothing was holding the line firmly HIGH when idle
// otherwise). Re-added below with pinMode(BOOT_BUTTON, INPUT_PULLUP).
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
// TFT_eSPI must be included before I2S.h -- Arduino's I2S.h wraps the
// entire legacy ESP-IDF driver/i2s.h in a `namespace esp_i2s { ... }`
// block. Shared low-level headers pulled in by driver/i2s.h (like the one
// declaring periph_module_t) get their types scoped inside that namespace
// on first inclusion; a later #include of the same header at global scope
// (e.g. via TFT_eSPI -> driver/spi_common.h) is a no-op because of the
// include guard, leaving periph_module_t undefined globally. Including
// TFT_eSPI first forces those shared headers to be processed at global
// scope first, before I2S.h's namespace-wrapped re-inclusion.
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <I2S.h>
#include <Wire.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "es8311.h"
#include "secrets.h"
#include "pins.h"
#include "boot_logo_cyd.h"
#include "FT6336U.h"
#include "translate_fonts.h"

TFT_eSPI tft = TFT_eSPI();
// Named touchCtrl, not touch -- FT6336U.h's own TouchStatusEnum declares an
// enum value literally named `touch`, which collides with an object of that
// name at global scope.
FT6336U touchCtrl(TOUCH_I2C_SDA, TOUCH_I2C_SCL, TOUCH_RST_PIN, TOUCH_INT_PIN);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t lvglBuf[SCREEN_W * 40];
static lv_obj_t *translateScreen = nullptr;
static lv_obj_t *sttLabel = nullptr;
static lv_obj_t *translatedLabel = nullptr;

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

// I2S pins (FNK0104AB/S -- do not use the FNK0104N 3.5" variant's different pins)
#define I2S_MCK 4
#define I2S_BCK 5
#define I2S_WS 7
#define I2S_DINT 6
#define I2S_DOUT 8
#define AP_ENABLE 1
#define BOOT_BUTTON 0

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
String transcriptionResult = "not started yet";
String translationResult = "";

// Step-by-step progress printed directly from setup() (with an explicit
// flush after each line) rather than only via the periodic loop() report --
// if any single init call below hangs, this is the only way to see how far
// we got before the hang, since loop() would never be reached to print
// statusLine at all.
void step(const char *msg) {
  Serial.println(msg);
  Serial.flush();
}

// WiFi + OTA are added here so future firmware iteration on this board can
// go over the air instead of the manual BOOT-hold+replug+RESET-tap dance
// every USB flash has needed so far. Connection is bounded (15s timeout)
// and non-fatal -- if WiFi isn't available, audio bring-up still runs and
// is still reachable over USB serial as before.
void setupWifiAndOta() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  step("setup: WiFi.begin called, connecting...");

  unsigned long wifiStartMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartMs < 15000) {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED) {
    step("setup: WiFi connect FAILED (timed out) -- continuing without OTA");
    return;
  }

  char ipMsg[64];
  snprintf(ipMsg, sizeof(ipMsg), "setup: WiFi connected, IP=%s", WiFi.localIP().toString().c_str());
  step(ipMsg);

  ArduinoOTA.setHostname("translate-esp32");
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  step("setup: ArduinoOTA ready");
}

// Boot logo on a white background for 5 seconds, then the main translate
// screen -- confirmed-working pattern from the earlier isolated display
// bring-up this session. A blocking delay(5000) is used rather than an
// LVGL timer since this all happens before loop()/lv_timer_handler are
// pumped anyway, and WiFi/audio init below need to happen regardless.
static void showBootLogoThenMainScreen() {
  lv_obj_t *logoScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(logoScreen, lv_color_white(), 0);
  lv_obj_set_style_border_width(logoScreen, 0, 0);
  lv_scr_load(logoScreen);

  lv_obj_t *logo = lv_img_create(logoScreen);
  lv_img_set_src(logo, &cyd_boot_logo);
  lv_obj_center(logo);

  lv_timer_handler();
  delay(5000);

  translateScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(translateScreen, lv_color_white(), 0);
  lv_obj_set_style_border_width(translateScreen, 0, 0);

  lv_obj_t *titleLabel = lv_label_create(translateScreen);
  lv_label_set_text(titleLabel, "Papa Lanc Translates");
  lv_obj_set_style_text_color(titleLabel, lv_color_black(), 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 15);

  sttLabel = lv_label_create(translateScreen);
  lv_label_set_text(sttLabel, "Tap anywhere to speak");
  lv_obj_set_style_text_color(sttLabel, lv_color_black(), 0);
  lv_label_set_long_mode(sttLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sttLabel, SCREEN_W - 20);
  lv_obj_align(sttLabel, LV_ALIGN_TOP_MID, 0, 50);

  translatedLabel = lv_label_create(translateScreen);
  lv_label_set_text(translatedLabel, "");
  lv_obj_set_style_text_color(translatedLabel, lv_color_black(), 0);
  lv_label_set_long_mode(translatedLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(translatedLabel, SCREEN_W - 20);
  lv_obj_align(translatedLabel, LV_ALIGN_TOP_MID, 0, 150);

  lv_scr_load(translateScreen);
  lv_timer_handler();
}

// Speech-to-text isolation test: record RECORD_SECONDS of raw I2S audio
// straight into a WAV-wrapped multipart/form-data buffer (built directly in
// PSRAM -- at ~860KB it's too big to trust to internal SRAM alongside the
// WiFi/BT stacks), then POST it to Groq's Whisper API. One-shot test
// triggered a few seconds after boot, not yet wired to push-to-talk (no
// touch UI yet) -- isolating the recording+upload+parse plumbing first,
// same "one piece at a time" approach used for every other subsystem this
// project has brought up so far.
static const int RECORD_SECONDS = 5;
// I2S is configured full-duplex stereo for the passthrough test, but the
// mic itself is physically mono -- both channels carry the same signal.
// De-interleaving down to real mono halves the upload size for the same
// audio duration, which matters here: a stereo upload of this recording
// (~860KB) has shown intermittent mid-transfer failures (HTTP -3) on this
// WiFi network, and a smaller/faster transfer directly reduces exposure to
// that kind of flakiness.
static const int RECORD_CHANNELS = 1;
static const size_t RECORD_PCM_BYTES = (size_t)SAMPLE_RATE * RECORD_CHANNELS * (BITS_PER_SAMPLE / 8) * RECORD_SECONDS;

static void writeWavHeader(uint8_t *buf, uint32_t pcmBytes) {
  uint32_t byteRate = SAMPLE_RATE * RECORD_CHANNELS * (BITS_PER_SAMPLE / 8);
  uint16_t blockAlign = RECORD_CHANNELS * (BITS_PER_SAMPLE / 8);
  uint32_t chunkSize = 36 + pcmBytes;
  uint16_t audioFormat = 1; // PCM
  uint16_t numChannels = RECORD_CHANNELS;
  uint32_t sampleRate = SAMPLE_RATE;
  uint16_t bitsPerSample = BITS_PER_SAMPLE;

  memcpy(buf + 0, "RIFF", 4);
  memcpy(buf + 4, &chunkSize, 4);
  memcpy(buf + 8, "WAVE", 4);
  memcpy(buf + 12, "fmt ", 4);
  uint32_t fmtChunkSize = 16;
  memcpy(buf + 16, &fmtChunkSize, 4);
  memcpy(buf + 20, &audioFormat, 2);
  memcpy(buf + 22, &numChannels, 2);
  memcpy(buf + 24, &sampleRate, 4);
  memcpy(buf + 28, &byteRate, 4);
  memcpy(buf + 32, &blockAlign, 2);
  memcpy(buf + 34, &bitsPerSample, 2);
  memcpy(buf + 36, "data", 4);
  memcpy(buf + 40, &pcmBytes, 4);
}

// LVGL labels don't have a built-in "shrink font to fit" mode -- this
// measures the wrapped text at each available font size (largest first)
// and picks the biggest one that still fits within maxHeightPx, falling
// back to the smallest if even that overflows. Keeps a long sentence fully
// visible instead of overlapping the label below it or running off-screen.
static void setLabelTextAutoShrink(lv_obj_t *label, const char *text, lv_coord_t maxHeightPx) {
  // Custom fonts (translate_fonts.h), not LVGL's built-in Montserrat --
  // the built-in ones only cover ASCII, no German umlauts.
  static const lv_font_t *fonts[] = { &lv_font_translate_14, &lv_font_translate_12, &lv_font_translate_10 };
  const int fontCount = sizeof(fonts) / sizeof(fonts[0]);
  const lv_coord_t maxWidth = SCREEN_W - 20;

  int chosen = fontCount - 1;
  for (int i = 0; i < fontCount; i++) {
    lv_point_t size;
    lv_txt_get_size(&size, text, fonts[i], 0, 0, maxWidth, LV_TEXT_FLAG_NONE);
    if (size.y <= maxHeightPx) {
      chosen = i;
      break;
    }
  }

  lv_obj_set_style_text_font(label, fonts[chosen], 0);
  lv_label_set_text(label, text);
}

static void updateSttLabel(const char *text) {
  transcriptionResult = text;
  if (sttLabel != nullptr) {
    setLabelTextAutoShrink(sttLabel, text, 80);
    lv_timer_handler();
  }
  step(text);
}

static void updateTranslatedLabel(const char *text) {
  translationResult = text;
  if (translatedLabel != nullptr) {
    setLabelTextAutoShrink(translatedLabel, text, 80);
    lv_timer_handler();
  }
  step(text);
}

// Streams the Piper server's response (see tools/piper_server/) straight
// into I2S.write() -- the server already upsamples Piper's 22050Hz mono
// output to the fixed 44100Hz stereo format this firmware's I2S bus runs
// at, so no resampling is needed here, just buffer-and-play. Buffered fully
// in PSRAM first (like the STT recording buffer) rather than streamed
// sample-by-sample, since a translated sentence is only a few hundred KB.
static void playTranslationAudio(const String &text, const char *lang) {
  if (text.length() == 0) return;

  WiFiClient client;
  HTTPClient http;
  http.setTimeout(30000);
  http.begin(client, PIPER_SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  JsonDocument reqDoc;
  reqDoc["text"] = text;
  reqDoc["lang"] = lang;
  String reqBody;
  serializeJson(reqDoc, reqBody);

  int httpCode = http.POST(reqBody);
  if (httpCode != 200) {
    step(("TTS request failed, HTTP " + String(httpCode)).c_str());
    http.end();
    return;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    step("TTS response had no content length");
    http.end();
    return;
  }

  uint8_t *audioOut = (uint8_t *)ps_malloc(contentLength);
  if (audioOut == nullptr) {
    step("ERROR - PSRAM alloc failed for TTS audio");
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t received = 0;
  unsigned long lastDataMs = millis();
  while (received < (size_t)contentLength && millis() - lastDataMs < 10000) {
    size_t avail = stream->available();
    if (avail > 0) {
      size_t toRead = min(avail, (size_t)contentLength - received);
      int r = stream->readBytes(audioOut + received, toRead);
      if (r > 0) {
        received += r;
        lastDataMs = millis();
      }
    } else {
      delay(1);
    }
  }
  http.end();

  if (received == (size_t)contentLength) {
    step("TTS audio received, playing...");
    // The actual bug behind every earlier silent/truncated attempt: the
    // I2S driver's output ring buffer is only ~8KB (2 DMA buffers * 1024
    // samples * stereo * 2 bytes -- see I2SClass::begin()/_buffer_byte_size
    // in the Arduino I2S library), but a translated sentence's audio is
    // routinely 300KB+. xRingbufferSend() rejects an item bigger than the
    // ring's total capacity IMMEDIATELY regardless of blocking mode -- that
    // explains the suspiciously fast ~4ms returns seen in every earlier
    // version of this function (single write_nonblocking, single
    // write_blocking, and manually-sized chunking that still exceeded 8KB
    // per chunk): each one was being silently REJECTED, not queued, so
    // zero audio ever reached the codec. Chunking to audioBufferBytes (the
    // same size already used for I2S.read() elsewhere, guaranteed to fit
    // one DMA buffer) makes each write_blocking() call both fit the ring
    // buffer AND genuinely block for that chunk's real playback duration,
    // which also naturally keeps loop()'s I2S.read() from interleaving
    // mid-sentence -- no separate delay() needed.
    size_t written = 0;
    while (written < (size_t)contentLength) {
      size_t chunk = min(audioBufferBytes, (size_t)contentLength - written);
      I2S.write_blocking(audioOut + written, chunk);
      written += chunk;
    }
    step("TTS playback complete");
  } else {
    step("ERROR - TTS audio download incomplete");
  }
  free(audioOut);
}

// Same request pattern confirmed working in the earlier isolated Claude API
// test (v0.4.9): one-shot translate-only system prompt, thinking disabled.
// Detects EN vs DE automatically rather than assuming a fixed direction, so
// this works the same regardless of which language was actually spoken.
// Response is JSON ({"lang":"en"|"de","text":"..."}) rather than plain text
// so the firmware knows which Piper voice to use for playback without
// re-detecting the language itself.
static void translateText(const String &text) {
  if (text.length() == 0 || text.startsWith("ERROR") || text.startsWith("HTTP ") || text.startsWith("JSON parse error")) {
    return; // nothing meaningful to translate
  }

  updateTranslatedLabel("Translating...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);
  HTTPClient https;
  https.setTimeout(30000);
  https.begin(client, "https://api.anthropic.com/v1/messages");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("x-api-key", ANTHROPIC_API_KEY);
  https.addHeader("anthropic-version", "2023-06-01");

  JsonDocument reqDoc;
  reqDoc["model"] = "claude-sonnet-5";
  reqDoc["max_tokens"] = 1024;
  reqDoc["system"] = "Translate the user's message between English and German (English to German, or German to English), detecting the input language automatically. Respond with ONLY a JSON object of the form {\"lang\":\"en\",\"text\":\"...\"} or {\"lang\":\"de\",\"text\":\"...\"} -- lang is the language of the translated text, text is the translation. No markdown, no code fences, no other text.";
  reqDoc["thinking"]["type"] = "disabled";
  JsonArray messages = reqDoc["messages"].to<JsonArray>();
  JsonObject msg = messages.add<JsonObject>();
  msg["role"] = "user";
  msg["content"] = text;

  String reqBody;
  serializeJson(reqDoc, reqBody);

  int httpCode = https.POST(reqBody);
  String respBody = https.getString();
  https.end();

  if (httpCode == 200) {
    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, respBody);
    if (err) {
      updateTranslatedLabel(("JSON parse error: " + String(err.c_str())).c_str());
      return;
    }
    const char *rawText = respDoc["content"][0]["text"];
    if (rawText == nullptr) {
      updateTranslatedLabel("(empty translation)");
      return;
    }
    JsonDocument innerDoc;
    DeserializationError innerErr = deserializeJson(innerDoc, rawText);
    if (innerErr) {
      // Claude didn't return valid JSON -- fall back to showing the raw
      // text so a translation is still visible, just without TTS.
      updateTranslatedLabel(rawText);
      return;
    }
    const char *lang = innerDoc["lang"];
    const char *translated = innerDoc["text"];
    if (translated == nullptr) {
      updateTranslatedLabel("(empty translation)");
      return;
    }
    updateTranslatedLabel(translated);
    playTranslationAudio(String(translated), lang != nullptr ? lang : "en");
  } else {
    updateTranslatedLabel(("HTTP " + String(httpCode) + " error: " + respBody).c_str());
  }
}

static void recordAndTranscribe() {
  if (WiFi.status() != WL_CONNECTED) {
    updateSttLabel("STT skipped - no WiFi");
    return;
  }

  updateTranslatedLabel(""); // clear any translation from a previous tap

  // Short pause instead of the old 2s countdown -- the tap itself is now
  // the "get ready" signal, this just avoids catching the tap sound/finger
  // lift in the recording.
  updateSttLabel("Get ready...");
  delay(300);

  // Countdown alternates English/German each second (5=EN, 4=DE, 3=EN,
  // 2=DE, 1=EN) -- odd seconds in English, even in German.
  char recMsg[24];
  auto formatCountdown = [](char *out, size_t outSize, int seconds) {
    if (seconds % 2 == 1) {
      snprintf(out, outSize, "Listening %ds", seconds);
    } else {
      snprintf(out, outSize, "Höre zu %ds", seconds);
    }
  };
  formatCountdown(recMsg, sizeof(recMsg), RECORD_SECONDS);
  updateSttLabel(recMsg);
  int lastSecondShown = RECORD_SECONDS;

  static const char *BOUNDARY = "ESP32FormBoundary7MA4YWxkTrZu0gW";
  String part1 = String("--") + BOUNDARY + "\r\n" +
                  "Content-Disposition: form-data; name=\"model\"\r\n\r\n" +
                  "whisper-large-v3-turbo\r\n";
  String part2Header = String("--") + BOUNDARY + "\r\n" +
                        "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n" +
                        "Content-Type: audio/wav\r\n\r\n";
  String trailer = String("\r\n--") + BOUNDARY + "--\r\n";

  size_t wavHeaderBytes = 44;
  size_t totalBytes = part1.length() + part2Header.length() + wavHeaderBytes + RECORD_PCM_BYTES + trailer.length();

  uint8_t *buf = (uint8_t *)ps_malloc(totalBytes);
  if (buf == nullptr) {
    updateSttLabel("ERROR - PSRAM alloc failed for STT buffer");
    return;
  }

  size_t offset = 0;
  memcpy(buf + offset, part1.c_str(), part1.length());
  offset += part1.length();
  memcpy(buf + offset, part2Header.c_str(), part2Header.length());
  offset += part2Header.length();
  writeWavHeader(buf + offset, (uint32_t)RECORD_PCM_BYTES);
  offset += wavHeaderBytes;

  // I2S.read() yields interleaved stereo (L0,R0,L1,R1,...) -- read each
  // chunk into the shared audioBuffer scratch space, then copy just the
  // left-channel samples into the destination mono buffer.
  size_t recordedMonoBytes = 0;
  while (recordedMonoBytes < RECORD_PCM_BYTES) {
    I2S.read(audioBuffer, audioBufferBytes);
    int16_t *stereoSamples = (int16_t *)audioBuffer;
    size_t stereoFrameCount = audioBufferBytes / 4; // 4 bytes per L+R frame
    size_t monoSamplesRemaining = (RECORD_PCM_BYTES - recordedMonoBytes) / 2;
    size_t framesToCopy = min(stereoFrameCount, monoSamplesRemaining);

    int16_t *destMono = (int16_t *)(buf + offset + recordedMonoBytes);
    for (size_t i = 0; i < framesToCopy; i++) {
      destMono[i] = stereoSamples[i * 2]; // left channel only
    }
    recordedMonoBytes += framesToCopy * 2;

    int elapsedSeconds = (int)((recordedMonoBytes / 2) / SAMPLE_RATE);
    int secondsRemaining = RECORD_SECONDS - elapsedSeconds;
    if (secondsRemaining >= 1 && secondsRemaining != lastSecondShown) {
      formatCountdown(recMsg, sizeof(recMsg), secondsRemaining);
      updateSttLabel(recMsg);
      lastSecondShown = secondsRemaining;
    }
  }
  offset += RECORD_PCM_BYTES;
  memcpy(buf + offset, trailer.c_str(), trailer.length());
  offset += trailer.length();

  // Connection to api.groq.com has proven intermittently flaky on this
  // network (a small GET and this same POST have both failed with -1
  // "connection refused" on some attempts, then succeeded moments later
  // with no code change -- confirmed not a payload-size issue since even
  // the tiny GET failed the same way). Retrying a few times with a short
  // pause is the standard mitigation for this kind of transient WiFi/DNS
  // flakiness, and is cheap compared to a 5-second recording being wasted.
  int httpCode = -1;
  String respBody;
  for (int attempt = 1; attempt <= 3; attempt++) {
    char attemptMsg[32];
    snprintf(attemptMsg, sizeof(attemptMsg), "Transcribing (try %d/3)...", attempt);
    updateSttLabel(attemptMsg);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(30000);
    HTTPClient https;
    https.setTimeout(30000);
    https.begin(client, "https://api.groq.com/openai/v1/audio/transcriptions");
    https.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
    https.addHeader("Content-Type", String("multipart/form-data; boundary=") + BOUNDARY);

    httpCode = https.POST(buf, totalBytes);
    if (httpCode > 0) {
      respBody = https.getString();
    }
    https.end();

    char resultMsg[48];
    snprintf(resultMsg, sizeof(resultMsg), "Attempt %d result: httpCode=%d", attempt, httpCode);
    step(resultMsg);

    if (httpCode == 200) {
      break;
    }
    delay(1000);
  }
  free(buf);

  if (httpCode == 200) {
    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, respBody);
    if (err) {
      updateSttLabel(("JSON parse error: " + String(err.c_str())).c_str());
    } else {
      const char *text = respDoc["text"];
      updateSttLabel(text != nullptr ? text : "(empty transcription)");
      if (text != nullptr) {
        translateText(String(text));
      }
    }
  } else {
    updateSttLabel(("HTTP " + String(httpCode) + " error: " + respBody).c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  step("setup: starting");

  lv_init();
  tft.begin();
  tft.setRotation(0); // portrait -- rotated 90 deg from the earlier landscape (rotation 1)
  lv_disp_draw_buf_init(&draw_buf, lvglBuf, nullptr, SCREEN_W * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W; // rotation 0 -> native portrait, no dimension swap needed
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  step("setup: TFT_eSPI + LVGL init done");

  showBootLogoThenMainScreen();
  step("setup: boot logo shown, main screen loaded");

  setupWifiAndOta();

  // Retested post-MCLK-fix: HIGH made playback fully silent (worse than
  // LOW's faint-but-audible result), so reverting to LOW -- matches
  // Freenove's own documented default.
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

  // Touch init happens last, after the codec's I2C traffic is done for
  // good -- FT6336U::begin() unconditionally calls Wire.begin(sda, scl)
  // again (with no speed argument, so back to the ESP32 I2C default),
  // which would reset our earlier Wire.begin(..., I2C_SPEED) if it ran
  // first. The codec never touches I2C again after init, so this is safe.
  touchCtrl.begin();
  step("setup: FT6336U touch init done");

  audioBufferBytes = I2S.getBufferSize() * (BITS_PER_SAMPLE / 8);
  audioBuffer = (uint8_t *)malloc(audioBufferBytes);
  if (audioBuffer == NULL) {
    statusLine = "ERROR - failed to allocate audio buffer";
    step("setup: audio buffer malloc FAILED");
    return;
  }
  step("setup: audio buffer allocated");

  // INPUT_PULLUP (not plain INPUT) is the fix for GPIO0 previously reading
  // a steady LOW regardless of the physical button -- without an internal
  // pull-up actively holding the line HIGH when idle, the board's
  // auto-program circuit weakly loads it LOW instead.
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  step("setup: BOOT_BUTTON pinMode INPUT_PULLUP done");

  statusLine = "OK - running passthrough, talk into the mic";
  step("setup: complete, entering loop");
}

// Tap anywhere on the translate screen to trigger a 5s recording ->
// transcribe cycle -- lets STT accuracy get tested repeatedly without a
// reboot each time. Edge-detected (only fires on the touch-down transition,
// not every loop iteration while a finger stays down) so one tap = one
// recording, not a burst of them.
static void handleTapToSpeak() {
  static bool wasTouched = false;
  FT6336U_TouchPointType touchPoint = touchCtrl.scan();
  bool isTouched = touchPoint.touch_count > 0;

  if (isTouched && !wasTouched) {
    recordAndTranscribe();
  }
  wasTouched = isTouched;
}

// Validates the INPUT_PULLUP fix for GPIO0 (see setup() comment) -- active
// LOW when pressed, edge-detected so one press = one log line, not a
// stream of them while held. Just a serial diagnostic for now, not wired
// to any feature; confirm this works before building anything on top of it.
static int bootButtonPressCount = 0;
static void handleBootButton() {
  static bool wasPressed = false;
  bool isPressed = (digitalRead(BOOT_BUTTON) == LOW);

  if (isPressed && !wasPressed) {
    bootButtonPressCount++;
    char msg[48];
    snprintf(msg, sizeof(msg), "BOOT button pressed (count=%d)", bootButtonPressCount);
    step(msg);
  }
  wasPressed = isPressed;
}

// Debug-only serial hook so TTS playback (and its timing) can be tested by
// sending a line over USB serial, without needing a physical tap -- skips
// the mic/STT/translate steps entirely and calls playTranslationAudio()
// directly. Format: "TTSEN <text>" or "TTSDE <text>".
static void handleSerialTtsTest() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("TTSEN ")) {
      playTranslationAudio(line.substring(6), "en");
    } else if (line.startsWith("TTSDE ")) {
      playTranslationAudio(line.substring(6), "de");
    } else if (line == "PASSTHROUGH") {
      // Re-runs the exact continuous read+write pattern confirmed working
      // for the speaker in the original v0.4.13 passthrough test (mic
      // feedback squeal/hum), to check whether basic speaker output still
      // works on the current firmware/hardware state at all, independent
      // of the TTS download/buffering code.
      step("PASSTHROUGH: starting 3s of continuous read+write");
      unsigned long endMs = millis() + 3000;
      while (millis() < endMs) {
        I2S.read(audioBuffer, audioBufferBytes);
        I2S.write(audioBuffer, audioBufferBytes);
      }
      step("PASSTHROUGH: done");
    }
  }
}

void loop() {
  ArduinoOTA.handle();
  lv_timer_handler();
  handleTapToSpeak();
  handleSerialTtsTest();
  handleBootButton();

  if (statusLine.startsWith("ERROR") || statusLine == "not started yet") {
    Serial.println(statusLine);
    delay(1000);
    return;
  }

  // Mic-to-speaker passthrough already confirmed working on hardware
  // (v0.4.13) -- no longer writing captured audio back out continuously,
  // since that creates an audible feedback loop (mic picks up the
  // speaker's own output) with no real device sitting nearby. The actual
  // app records-then-plays-back, never both at once, so this isn't
  // representative of final behavior anyway. Still reading + reporting mic
  // peak level so the mic path stays visibly alive during further bring-up.
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

  if (millis() - lastReportMs > 500) {
    Serial.printf("%s | mic peak level (since last report): %d / 32767 | STT result: %s\n", statusLine.c_str(), peakLevel, transcriptionResult.c_str());
    peakLevel = 0;
    lastReportMs = millis();
  }
}
