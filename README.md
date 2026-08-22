# translate

Standalone ESP32-S3 English↔German voice translator. No wake word, no prompts —
hold a button on screen to record, release to send. Runs on the Freenove Media
Kit for ESP32-S3 (FNK0102): ILI9341 touch LCD, ES8311 I2S mic/speaker codec.

## Pipeline

1. Push-to-talk record
2. Groq Whisper API → speech-to-text
3. Anthropic Claude API (`claude-sonnet-5`, thinking disabled) → EN↔German translation
4. ElevenLabs TTS → audio
5. Playback on-device + show original/translated text on the LCD

## Build

PlatformIO, `framework = arduino`. See `platformio.ini`.

```bash
pio run
pio run -t upload -t monitor
```

Copy `include/secrets.h.example` to `include/secrets.h` and fill in your API
keys and WiFi info before building the cloud-pipeline stages (added in v0.4.6).

## Status

Built up in phases, each tagged as its own version — see git tags for the
current milestone. GPIO pin assignments in `include/pins.h` are TODO stubs
until confirmed against the actual board.
