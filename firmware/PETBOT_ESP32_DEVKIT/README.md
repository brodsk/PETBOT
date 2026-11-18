# PETBOT ESP32 DevKit port

This is a separate port for the classic ESP32 DevKit board. It does not replace the original LILYGO T3 firmware.

## Open in Arduino IDE
Open `PETBOT_ESP32_DEVKIT.ino` from this folder. Keep both header files in the same sketch folder.

Select **ESP32 Dev Module** as the board.

Install these Arduino libraries if they are not already installed:
- WiFiManager by tzapu
- Adafruit GFX Library
- Adafruit SSD1306

## Initial wiring
| Part | ESP32 DevKit |
|---|---|
| OLED GND | GND |
| OLED VDD | 3V3 |
| OLED SCK | GPIO22 |
| OLED SDA | GPIO21 |
| Passive buzzer signal | GPIO25 |
| IR obstacle sensor OUT/TX | GPIO34 |
| External button, other side to GND | GPIO26 |

GPIO34 is input-only. The IR sensor output must never exceed 3.3 V. Power the sensor from 3V3 initially if its module supports it. Do not connect 5 V to any ESP32 GPIO.

The battery monitor is intentionally not sampled in this first DevKit build. GPIO35 is reserved for a future light-sensor analog input / later battery-divider redesign. PIR, DHT11, motors and motor drivers are not enabled yet.

## Notes
- OLED remains 128x64 I2C at address 0x3C.
- The seven local personality drives, face expressions, gaze, non-verbal piezo sounds, button interactions, IR obstacle event, Wi-Fi/cloud code are retained.
- OTA is temporarily disabled in this port because the original sketch depended on a missing custom TLS certificate bundle. This avoids silently weakening TLS verification.
- This branch is a source port, not a claim of a successful compile or hardware test.