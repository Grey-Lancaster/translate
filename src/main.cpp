// Code by Grey and my buddy Claude AI
//
// Claude API test: hardcoded English sentence -> Anthropic Messages API
// (claude-sonnet-5, thinking disabled) -> print the German translation to
// Serial. Isolating the HTTPS/JSON/auth plumbing before wiring this into
// the actual push-to-talk flow. WiFiClientSecure uses setInsecure() for
// now (skips TLS cert validation) -- fine for this prototype stage, worth
// pinning Anthropic's root CA before this becomes a real product.
//
// Result is stored and re-printed every second in loop() (not just once in
// setup()) -- native USB CDC serial capture has repeatedly lost one-shot
// output to timing races this session, so this makes the result catchable
// no matter when a monitor attaches.

#include <ArduinoJson.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "secrets.h"

static const char *ENGLISH_TEXT = "Hello, how are you today?";
static String resultLine = "not started yet";

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  resultLine = "connecting to WiFi...";
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    resultLine = "WiFi connect failed";
    return;
  }

  resultLine = "WiFi OK (" + WiFi.localIP().toString() + "), calling Claude API...";

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  https.begin(client, "https://api.anthropic.com/v1/messages");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("x-api-key", ANTHROPIC_API_KEY);
  https.addHeader("anthropic-version", "2023-06-01");

  JsonDocument reqDoc;
  reqDoc["model"] = "claude-sonnet-5";
  reqDoc["max_tokens"] = 1024;
  reqDoc["system"] = "Translate English to German or German to English. Detect the input language automatically. Output only the translation, nothing else.";
  reqDoc["thinking"]["type"] = "disabled";
  JsonArray messages = reqDoc["messages"].to<JsonArray>();
  JsonObject msg = messages.add<JsonObject>();
  msg["role"] = "user";
  msg["content"] = ENGLISH_TEXT;

  String reqBody;
  serializeJson(reqDoc, reqBody);

  int httpCode = https.POST(reqBody);
  String respBody = https.getString();

  if (httpCode == 200) {
    JsonDocument respDoc;
    DeserializationError err = deserializeJson(respDoc, respBody);
    if (err) {
      resultLine = "JSON parse error: " + String(err.c_str());
    } else {
      const char *translation = respDoc["content"][0]["text"];
      resultLine = "Sent: \"" + String(ENGLISH_TEXT) + "\"  ->  Translation: " + String(translation);
    }
  } else {
    resultLine = "HTTP " + String(httpCode) + " error: " + respBody;
  }

  https.end();
}

void loop() {
  Serial.println(resultLine);
  delay(1000);
}
