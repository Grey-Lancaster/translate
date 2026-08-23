# Code by Grey and my buddy Claude AI
#
# Local Piper TTS server for the ESP32 translator. The ESP32 POSTs
# translated text here and gets back raw 16-bit PCM audio, ready to feed
# straight into I2S.write() -- no parsing or resampling needed on-device.
#
# Piper's voices output 22050Hz mono, but the firmware's I2S bus is fixed
# at 44100Hz stereo (set once in main.cpp's SAMPLE_RATE / I2S.begin() call,
# shared with the mic recording path). Rather than reconfigure I2S per
# playback, each mono sample is just duplicated 4x -- twice to double the
# rate to 44100Hz, twice more to duplicate mono into L+R stereo frames.
# Cheap and sounds fine for speech; a real resampler would only matter for
# music-quality output.
#
# Run: python piper_server.py
# Then from another machine on the same LAN: POST /tts {"text": "...", "lang": "en"|"de"}
#
# Windows Firewall will likely prompt to allow inbound connections the
# first time this runs -- click Allow (Private networks) so the ESP32 can
# reach it.

from flask import Flask, request, Response
import subprocess
import sys
import numpy as np

# Prevents a console window from flashing open for each piper.exe subprocess
# call when this server itself runs windowless (pythonw.exe, e.g. via the
# PiperTTSServer scheduled task).
_CREATE_NO_WINDOW = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0

app = Flask(__name__)

PIPER_EXE = r"C:\Piper\piper\piper.exe"
VOICES = {
    "en": r"C:\Piper\voices\en_US-lessac-medium.onnx",
    "de": r"C:\Piper\voices\de_DE-thorsten-medium.onnx",
}


@app.route("/tts", methods=["POST"])
def tts():
    data = request.get_json(force=True, silent=True) or {}
    text = data.get("text", "").strip()
    lang = data.get("lang", "en")

    if lang not in VOICES:
        return {"error": "lang must be 'en' or 'de'"}, 400
    if not text:
        return {"error": "text is empty"}, 400

    proc = subprocess.run(
        [PIPER_EXE, "-m", VOICES[lang], "--output_raw", "-q"],
        input=text.encode("utf-8"),
        capture_output=True,
        creationflags=_CREATE_NO_WINDOW,
    )
    if proc.returncode != 0:
        return {"error": proc.stderr.decode("utf-8", "ignore")}, 500

    mono_22050 = np.frombuffer(proc.stdout, dtype="<i2")
    stereo_44100 = np.repeat(mono_22050, 4).astype("<i2")

    return Response(stereo_44100.tobytes(), mimetype="application/octet-stream")


@app.route("/health", methods=["GET"])
def health():
    return {"status": "ok"}


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5001)
