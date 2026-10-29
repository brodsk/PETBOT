# PETBOT hardware roadmap

Target controller: LILYGO T3 V1.6.1 (classic ESP32 / ESP32-PICO-D4, OLED 128x64, LoRa, microSD). Keep the existing board as the face + behavior controller for now.

## Buy in this order

### 1. Drive base (after choosing chassis)
- Two small DC geared motors with wheels and a caster wheel; choose a chassis that fits the intended PETBOT body.
- Dual H-bridge driver: **DRV8833** for small low-voltage gear motors, or **TB6612FNG** as a good alternative. Match the driver to the motors' stall current, not just their no-load current.
- Separate motor battery/power rail sized for the motors; common GND with the controller. Do not power motors from the ESP32 3.3 V pin and never connect motors directly to GPIO.
- A physical motor power switch and a software stop command are required before any autonomous driving.

Do not select motor voltage or battery capacity until the actual motors/chassis are selected. Start with wheels lifted off the table during first tests.

### 2. Fall protection and obstacle detection
- **Two downward-facing IR reflectance sensors** (for example, TCRT5000-style modules) near the left and right front underside. These are for detecting a change from tabletop to an edge; calibrate on the actual table and floor. They are not safety-rated and can fail on dark, glossy, transparent, or unusual surfaces.
- **One forward time-of-flight distance sensor**, preferably VL53L1X (or VL53L0X for a lower-cost short-range start), on the I2C bus shared with the OLED (GPIO21 SDA / GPIO22 SCL). Check address compatibility and breakout voltage before wiring.
- **Two simple bumper switches** at the front corners are a useful final fallback for low-speed contact. A touch sensor such as TTP223 is fine for a head/body touch interaction, but it is not a substitute for cliff sensors or a bumper.
- First mobility firmware should stop if any cliff sensor is uncertain, sensor readings are stale, or the front distance is below a conservative threshold. Do not test near a real table edge until sensor readings have been validated on a safe bench.

### 3. Sound and touch
- For sound-level reactions, use a documented microphone module with a 0–3.3 V analog OUT (e.g. MAX9814 breakout with its output checked), or an I2S MEMS microphone such as INMP441 if enough pins are available.
- Do not connect the salvaged vape sensor blindly: it may be a pressure/draw sensor rather than a microphone, and its board supply/output voltage is unknown.
- Use a dedicated touch module or a metal touch pad with a known capacitive-sensing circuit. Keep this as a low-risk input-only feature first.

### 4. Camera and visual recognition
- Recommended first camera add-on: a separate **ESP32-S3 camera board with PSRAM and OV2640/OV3660**, such as an XIAO ESP32S3 Sense or a suitable ESP32-S3 camera development board. It communicates with the existing LILYGO over Wi-Fi or UART.
- The current T3 V1.6.1 is not a convenient camera host: its GPIOs are heavily shared with LoRa, microSD and OLED, and it typically has no PSRAM. A separate camera board avoids pin conflicts and leaves the current face/behavior firmware stable.
- An ESP32-S3 camera can handle capture, streaming and some small vision models, but it will not automatically provide Vector-level general object understanding. For richer recognition, send selected images to a server/cloud vision service or use a more capable compute module later. Decide privacy/offline requirements before enabling image upload.

## Known T3 V1.6.1 pin constraints

The current firmware uses:
- GPIO21 SDA / GPIO22 SCL: OLED; keep available for I2C sensors.
- GPIO25: piezo in the current sketch; board documentation also maps the onboard LED to GPIO25, so be aware of the shared signal.
- GPIO34: optional analog sound input; disabled by default. GPIO34 is input-only and has no internal pull-up.
- GPIO5, 18, 19, 23, 26, 27 and 32/33 are used by the onboard LoRa radio on common T3 V1.6.1 variants.
- GPIO2, 13, 14 and 15 are associated with the microSD slot and/or boot strapping.
- GPIO35 is shown as a battery ADC on some board documentation. Do not assume it is free without checking your exact board schematic.

The exact usable pins depend on the fitted LoRa variant and board schematic. Do not finalize motor-driver wiring from a generic ESP32 pinout. Before connecting a motor driver, create a pin allocation for the exact board and decide whether LoRa and microSD remain enabled. A small I2C GPIO expander (e.g. MCP23017) can help with switches and simple digital sensors, but it is not a motor controller and does not replace timing-critical PWM pins.

## Firmware stages

1. v3.0 behavior model: seven internal drives, local behavior policy and serial event simulation.
2. Sensor abstraction layer: each sensor reports a timestamped observation; no hardware is considered present until configured and tested.
3. Bench-tested cliff/distance sensors and a hard stop.
4. Motor driver with low speed, wheels lifted, timeout watchdog and explicit enable/disable.
5. Camera board sends only detected events (e.g. person/obstacle detected) to PETBOT; keep camera processing isolated from the face controller.
6. Integrate behavior: stimulation/curiosity can trigger a glance or approach request, but the safety controller always has priority over personality.

## Safety rules

- Motors off by default after boot and after any reset or sensor fault.
- Hardware power switch must be reachable; do not rely only on Wi-Fi or software to stop motors.
- Common ground between controller and motor driver, separate motor supply, and a suitable bulk capacitor near the driver.
- Verify sensor logic levels are 3.3 V safe before connecting to ESP32.
- Never let an emotion, cloud command or camera model override a cliff/bumper stop.
