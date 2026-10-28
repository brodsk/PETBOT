# PETBOT firmware 2.1 — personality and sound controls

Board target: LILYGO T3 V1.6.1 / ESP32, SSD1306 OLED at 0x3C, SDA 21 / SCL 22.

## What's new
- Persistent sound mute setting, microphone threshold, and interaction counter in ESP32 Preferences (NVS).
- Serial diagnostics and a built-in command help menu.
- `PET` reaction: affectionate LOVE face, sound, and persistent interaction count.
- `PLAY` sound test.
- `SOUND:ON` and `SOUND:OFF` persist across reboots.
- `MIC:CAL` measures the quiet-room noise span for 3 seconds and stores a starting threshold.
- `MIC:THRESHOLD=240` sets sensitivity manually (accepted range 50–1500).
- Cloud commands `PET` / `LOVE`, `PLAY`, `MUTE`, and `UNMUTE` are supported by the firmware command handler.
- Existing autonomous moods, eye gaze, blinking, Wi-Fi/cloud heartbeat, and short personality chirps remain.

## Wiring
- Passive piezo: GPIO25 to GND. If the salvaged element is not a small passive piezo or draws significant current, do not connect it directly; use a suitable driver.
- Microphone module: OUT to GPIO34, GND to GND, VCC only to the voltage specified for the exact module. Keep the output within ESP32 ADC input limits (0–3.3 V). The exact module pinout has not been visually verified.

## Serial Monitor
Set baud to **115200** and line ending to **Newline**. Available commands:
- `HELP`
- `STATUS`
- `PET`
- `PLAY`
- `SOUND:ON`
- `SOUND:OFF`
- `MIC:CAL` (keep the room quiet during the 3-second measurement)
- `MIC:THRESHOLD=240` (raise threshold if false triggers occur; lower it if it misses sounds)

The microphone detects sudden sound-level changes only. It does **not** recognize words or understand speech. The piezo is an electronic sounder, not a speaker capable of spoken voice.

## Flashing and validation
Compile and flash using the board profile and libraries already used by this project. After flashing, open Serial Monitor and send `STATUS`, then `PET`, then `MIC:CAL`. Test loudness and microphone reactions separately; the buzzer can be picked up by the microphone and may trigger one extra reaction.

**Validation status:** repository changes were committed, but this firmware has not been compiled against the exact installed Arduino-ESP32 core or tested on the physical board. If compilation reports an error, capture the complete error text and fix against that environment before relying on it.
