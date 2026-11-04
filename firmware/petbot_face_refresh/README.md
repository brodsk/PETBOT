# PETBOT face-refresh firmware

This is a standalone Arduino sketch for the PETBOT ESP32/LILYGO T3 V1.6.1 wiring (SSD1306 OLED on SDA 21/SCL 22, passive piezo GPIO25, analog microphone GPIO34, BOOT button GPIO0). It adds animated expressive eyes inspired by the supplied reference, pupil gaze, microphone sound-event reactions, and guaranteed silent sleep.

## Build and upload

Open `petbot_face_refresh.ino` in Arduino IDE from this folder. Install the **Adafruit GFX Library** and **Adafruit SSD1306** libraries; select the ESP32 board matching the existing PETBOT build and upload. This standalone sketch replaces the existing cloud-connected firmware while it is installed; it does not include Wi-Fi/cloud messaging. Keep the existing firmware folder intact so it can be restored.

The microphone detects changes in sound level only; it does not recognize speech. Use Serial Monitor at 115200 baud. Commands: `SLEEP`, `WAKE`, `MIC:ON`, `MIC:OFF`, `MIC:THRESHOLD=120`, `MIC:CAL`, `SOUND:ON`, `SOUND:OFF`, `HELP`.

Hold BOOT for 1.5 seconds to toggle sleep/wake. A single short press cycles expressions for a quick face demo.
