#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Standalone face-refresh firmware for PETBOT / classic ESP32.
// Hardware: SSD1306 128x64 I2C (SDA 21, SCL 22), passive piezo GPIO25,
// analog microphone output GPIO34 (0-3.3V only), BOOT key GPIO0.
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_ADDR 0x3C
#define PIEZO_PIN 25
#define MIC_PIN 34
#define BOOT_PIN 0
#define MIC_THRESHOLD_DEFAULT 90

Adafruit_SSD1306 display(128, 64, &Wire, -1);

const char* const moods[] = {
  "CURIOUS", "HAPPY", "LOVE", "SURPRISED", "ANNOYED", "SAD",
  "EXCITED", "CONFUSED", "SCARED", "SHY", "BORED", "SLEEPY"
};
const uint8_t MOOD_COUNT = sizeof(moods) / sizeof(moods[0]);
String mood = "CURIOUS";
String previousMood = "";
uint32_t moodSince = 0;
uint32_t lastInputAt = 0;
uint32_t nextMoodAt = 0;
uint32_t nextGazeAt = 0;
uint32_t nextBlinkAt = 0;
uint32_t blinkUntil = 0;
uint32_t lastMicSampleAt = 0;
uint32_t lastMicEventAt = 0;
uint32_t lastSoundAt = 0;
uint32_t lastFrameAt = 0;
uint32_t buttonDownAt = 0;
uint32_t lastButtonAt = 0;
uint32_t serialLastByteAt = 0;
String serialLine;
float gazeX = 0.0f, gazeY = 0.0f;
float gazeTargetX = 0.0f, gazeTargetY = 0.0f;
int micBaseline = 0;
int micThreshold = MIC_THRESHOLD_DEFAULT;
int micEnvelope = 0;
bool micEnabled = true;
bool soundEnabled = true;
bool sleeping = false;
bool buttonWasDown = false;
bool blinking = false;
uint8_t demoMood = 0;

static bool isMood(const char* name) { return mood == name; }
static int randomRange(int low, int high) {
  return low + (int)(esp_random() % (uint32_t)(high - low + 1));
}

static void setMood(const char* next, uint32_t holdMs, bool sound) {
  if (mood == next) return;
  mood = next;
  moodSince = millis();
  lastInputAt = moodSince;
  nextMoodAt = moodSince + holdMs;
  sleeping = mood == "SLEEPY";
  if (sleeping) {
    noTone(PIEZO_PIN);
    gazeTargetX = gazeTargetY = 0.0f;
    gazeX = gazeY = 0.0f;
    blinking = false;
  } else if (sound) {
    // Sound is deliberately impossible while sleeping, even if a stale request arrives.
    const uint16_t f = mood == "HAPPY" || mood == "EXCITED" ? 1800 :
                       mood == "LOVE" ? 1100 : mood == "ANNOYED" ? 500 :
                       mood == "SAD" ? 650 : mood == "SURPRISED" ? 2400 : 1350;
    playTone(f, mood == "EXCITED" ? 90 : 65);
  }
  drawFace();
}

static void playTone(uint16_t frequency, uint16_t durationMs) {
  if (!soundEnabled || sleeping || mood == "SLEEPY") {
    noTone(PIEZO_PIN);
    return;
  }
  tone(PIEZO_PIN, frequency, durationMs);
  lastSoundAt = millis();
}

static void drawHeart(int cx, int cy, int r, uint16_t color) {
  display.fillCircle(cx - r / 2, cy - r / 3, r / 2, color);
  display.fillCircle(cx + r / 2, cy - r / 3, r / 2, color);
  display.fillTriangle(cx - r, cy - r / 4, cx + r, cy - r / 4, cx, cy + r, color);
}

static void drawEye(int cx, int cy, int side, uint32_t now) {
  const int dx = (int)lroundf(gazeX) + side;
  const int dy = (int)lroundf(gazeY);
  const bool closed = sleeping || blinking || isMood("SHY") ||
                      (isMood("HAPPY") && ((now / 1800U) % 7U == 0));

  if (sleeping) {
    display.drawLine(cx - 13, cy, cx + 13, cy, SSD1306_WHITE);
    display.drawLine(cx - 11, cy + 2, cx + 11, cy + 2, SSD1306_WHITE);
    return;
  }
  if (closed) {
    display.drawLine(cx - 12, cy + 2, cx - 4, cy - 3, SSD1306_WHITE);
    display.drawLine(cx - 4, cy - 3, cx + 4, cy - 3, SSD1306_WHITE);
    display.drawLine(cx + 4, cy - 3, cx + 12, cy + 2, SSD1306_WHITE);
    if (isMood("SHY")) {
      for (int k = -1; k <= 1; ++k) display.drawLine(cx + k * 5, cy + 8, cx + k * 5 + 2, cy + 5, SSD1306_WHITE);
    }
    return;
  }

  if (isMood("LOVE")) {
    const int pulse = 1 + (int)((sinf(now * 0.009f) + 1.0f) * 1.2f);
    drawHeart(cx + dx, cy + dy, 8 + pulse, SSD1306_WHITE);
    return;
  }

  const bool angry = isMood("ANNOYED");
  const bool sleepyLid = isMood("BORED") || isMood("SAD");
  const int eyeTop = cy - 14;
  const int eyeHeight = angry || sleepyLid ? 23 : (isMood("SURPRISED") || isMood("SCARED") ? 31 : 28);
  const int eyeWidth = isMood("SCARED") ? 29 : 27;

  if (isMood("EXCITED")) {
    display.fillCircle(cx, cy, 14, SSD1306_WHITE);
    display.fillCircle(cx + dx, cy + dy, 6, SSD1306_BLACK);
    display.fillCircle(cx + dx - 2, cy + dy - 2, 2, SSD1306_WHITE);
    display.drawPixel(cx + 6, cy - 7, SSD1306_WHITE);
    return;
  }

  if (isMood("SCARED")) {
    display.drawCircle(cx, cy, 15, SSD1306_WHITE);
    display.drawCircle(cx, cy, 13, SSD1306_WHITE);
    display.fillCircle(cx + dx, cy + dy, 3, SSD1306_WHITE);
    return;
  }

  display.fillRoundRect(cx - eyeWidth / 2, eyeTop, eyeWidth, eyeHeight, 10, SSD1306_WHITE);
  // Clip lids/pupils to a safe central area so the gaze stays inside the eye outline.
  const int px = constrain(cx + dx, cx - 7, cx + 7);
  const int py = constrain(cy + dy + (isMood("SAD") ? 5 : 0), cy - 6, cy + 7);
  display.fillCircle(px, py, angry ? 5 : 6, SSD1306_BLACK);
  display.fillCircle(px - 2, py - 2, 2, SSD1306_WHITE);

  if (angry) {
    display.fillTriangle(cx - 14, cy - 14, cx + 12, cy - 10, cx + 12, cy - 6, SSD1306_BLACK);
  } else if (isMood("CURIOUS") || isMood("CONFUSED")) {
    display.drawLine(cx - 11, cy - 19, cx + 9, cy - 17, SSD1306_WHITE);
  }
  if (sleepyLid) {
    display.fillRect(cx - 15, cy - 15, 30, 11, SSD1306_BLACK);
    display.drawLine(cx - 13, cy - 4, cx + 13, cy - 4, SSD1306_WHITE);
  }
  if (isMood("SURPRISED")) display.drawLine(cx - 8, cy - 19, cx + 8, cy - 19, SSD1306_WHITE);
}

static void drawMouth(uint32_t now) {
  const int y = 56;
  if (sleeping) {
    display.drawCircle(64, y, 1, SSD1306_WHITE);
  } else if (isMood("SURPRISED")) {
    display.drawCircle(64, y, 3, SSD1306_WHITE);
  } else if (isMood("EXCITED")) {
    const int r = 3 + (int)((now / 180U) % 3U);
    display.fillCircle(64, y - 1, r, SSD1306_WHITE);
  } else if (isMood("SAD")) {
    display.drawLine(56, y + 2, 64, y - 3, SSD1306_WHITE);
    display.drawLine(64, y - 3, 72, y + 2, SSD1306_WHITE);
  } else if (isMood("ANNOYED") || isMood("BORED")) {
    display.drawLine(58, y, 70, y - (isMood("ANNOYED") ? 2 : 0), SSD1306_WHITE);
  } else if (isMood("SCARED")) {
    display.drawLine(59, y - 1, 64, y + 2, SSD1306_WHITE);
    display.drawLine(64, y + 2, 69, y - 1, SSD1306_WHITE);
  } else if (isMood("CONFUSED")) {
    display.drawLine(58, y, 64, y - 2, SSD1306_WHITE);
    display.drawLine(64, y - 2, 70, y + 1, SSD1306_WHITE);
  } else {
    display.drawLine(57, y - 1, 64, y + 3, SSD1306_WHITE);
    display.drawLine(64, y + 3, 71, y - 1, SSD1306_WHITE);
  }
}

void drawFace() {
  if (!display.width()) return;
  const uint32_t now = millis();
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(3, 2);
  display.print("PET");
  display.setCursor(94, 2);
  display.print(sleeping ? "SLEEP" : "AWAKE");
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  const float bob = sleeping ? 0.0f :
    isMood("EXCITED") ? -2.0f * fabsf(sinf(now * 0.008f)) :
    isMood("HAPPY") ? -1.0f * sinf(now * 0.005f) : 0.0f;
  const int cy = 34 + (int)lroundf(bob);
  drawEye(38, cy, -1, now);
  drawEye(90, cy, 1, now);

  if (isMood("SAD")) {
    const int tearY = 41 + (int)((now / 140U) % 17U);
    display.fillCircle(25, tearY, 2, SSD1306_WHITE);
  } else if (isMood("LOVE") && !sleeping) {
    const int hy = 44 - (int)((now / 100U) % 20U);
    drawHeart(64, hy, 5, SSD1306_WHITE);
  } else if (isMood("CONFUSED")) {
    display.setTextSize(2);
    display.setCursor(112, 16 + (int)(2.0f * sinf(now * 0.006f)));
    display.print('?');
    display.setTextSize(1);
  } else if (sleeping) {
    display.setCursor(108, 24 + (int)((now / 900U) % 5U));
    display.print('z');
    display.setCursor(117, 16 + (int)((now / 1100U) % 4U));
    display.print('Z');
  }
  drawMouth(now);
  display.display();
}

static void randomGaze() {
  gazeTargetX = (float)randomRange(-7, 7);
  gazeTargetY = (float)randomRange(-3, 3);
  nextGazeAt = millis() + (uint32_t)randomRange(800, 2300);
}

static void updateAnimations(uint32_t now) {
  if (!sleeping && mood != "SLEEPY") {
    if (now >= nextGazeAt) randomGaze();
    gazeX += (gazeTargetX - gazeX) * 0.22f;
    gazeY += (gazeTargetY - gazeY) * 0.22f;
    if (now >= nextBlinkAt) {
      blinking = true;
      blinkUntil = now + (uint32_t)randomRange(90, 150);
      nextBlinkAt = now + (uint32_t)randomRange(1700, 4200);
    }
    if (blinking && now >= blinkUntil) blinking = false;
  } else {
    blinking = false;
    gazeTargetX = gazeTargetY = gazeX = gazeY = 0.0f;
  }

  if (now >= nextMoodAt && now - lastInputAt > 18000U) {
    if (now - lastInputAt > 120000U || randomRange(0, 99) < 20) {
      setMood("SLEEPY", 0, false);
    } else {
      const uint8_t pick = (uint8_t)randomRange(0, MOOD_COUNT - 2);
      setMood(moods[pick], (uint32_t)randomRange(9000, 18000), false);
    }
  }

  if (now - lastFrameAt >= 66U) {
    lastFrameAt = now;
    drawFace();
  }
}

static void handleMic(uint32_t now) {
  if (!micEnabled || sleeping || mood == "SLEEPY" || now - lastMicSampleAt < 12U) return;
  lastMicSampleAt = now;
  const int sample = analogRead(MIC_PIN);
  if (micBaseline == 0) micBaseline = sample;
  const int deviation = abs(sample - micBaseline);
  micBaseline = (micBaseline * 31 + sample) / 32;
  micEnvelope = max(deviation, (micEnvelope * 7) / 8);
  if (micEnvelope > micThreshold && now - lastMicEventAt > 1800U) {
    lastMicEventAt = now;
    lastInputAt = now;
    nextMoodAt = now + 2400U;
    const char* reaction = micEnvelope > micThreshold * 3 ? "SCARED" : "SURPRISED";
    gazeTargetX = randomRange(0, 1) ? 6.0f : -6.0f;
    gazeTargetY = -1.0f;
    setMood(reaction, 2200U, true);
    Serial.printf("sound event envelope=%d threshold=%d\n", micEnvelope, micThreshold);
  }
}

static void wakePet() {
  sleeping = false;
  setMood("SURPRISED", 2600U, true);
  lastInputAt = millis();
  nextGazeAt = millis() + 200U;
}

static void toggleSleep() {
  if (sleeping || mood == "SLEEPY") {
    wakePet();
  } else {
    setMood("SLEEPY", 0, false);
    sleeping = true;
    noTone(PIEZO_PIN);
  }
}

static void handleButton(uint32_t now) {
  const bool down = digitalRead(BOOT_PIN) == LOW;
  if (down && !buttonWasDown) {
    buttonDownAt = now;
    buttonWasDown = true;
  } else if (!down && buttonWasDown) {
    buttonWasDown = false;
    const uint32_t held = now - buttonDownAt;
    if (now - lastButtonAt < 220U) return;
    lastButtonAt = now;
    if (held >= 1500U) {
      toggleSleep();
    } else if (!sleeping) {
      demoMood = (demoMood + 1) % (MOOD_COUNT - 1);
      setMood(moods[demoMood], 7000U, true);
      randomGaze();
    }
  }
}

static void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line == "HELP") {
    Serial.println("SLEEP, WAKE, MIC:ON, MIC:OFF, MIC:CAL, MIC:THRESHOLD=90, SOUND:ON, SOUND:OFF");
  } else if (line == "SLEEP") {
    toggleSleep();
  } else if (line == "WAKE") {
    if (sleeping || mood == "SLEEPY") wakePet();
    else setMood("CURIOUS", 3000U, true);
  } else if (line == "MIC:ON") {
    micEnabled = true;
    Serial.println("Microphone reactions enabled (sound level only, no speech recognition).");
  } else if (line == "MIC:OFF") {
    micEnabled = false;
    Serial.println("Microphone reactions disabled.");
  } else if (line.startsWith("MIC:THRESHOLD=")) {
    const int value = line.substring(14).toInt();
    if (value >= 30 && value <= 1500) {
      micThreshold = value;
      Serial.printf("Mic threshold: %d\n", micThreshold);
    } else Serial.println("Threshold range is 30..1500.");
  } else if (line == "MIC:CAL") {
    Serial.println("Calibrate in a quiet room for 2 seconds...");
    long total = 0;
    int minValue = 4095, maxValue = 0;
    uint16_t samples = 0;
    const uint32_t start = millis();
    while (millis() - start < 2000U) {
      const int v = analogRead(MIC_PIN);
      total += v;
      minValue = min(minValue, v);
      maxValue = max(maxValue, v);
      ++samples;
      delay(4);
    }
    if (samples) micBaseline = total / samples;
    micThreshold = constrain((maxValue - minValue) * 3, 60, 1500);
    Serial.printf("Mic baseline=%d threshold=%d\n", micBaseline, micThreshold);
  } else if (line == "SOUND:ON") {
    soundEnabled = true;
    Serial.println("Sound enabled.");
  } else if (line == "SOUND:OFF") {
    soundEnabled = false;
    noTone(PIEZO_PIN);
    Serial.println("Sound disabled.");
  } else if (line.length()) {
    Serial.println("Unknown command; send HELP.");
  }
}

static void readSerialCommands() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    serialLastByteAt = millis();
    if (c == '\n' || c == '\r') {
      if (serialLine.length()) {
        handleCommand(serialLine);
        serialLine = "";
      }
    } else if (serialLine.length() < 80) {
      serialLine += c;
    } else {
      serialLine = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIEZO_PIN, OUTPUT);
  noTone(PIEZO_PIN);
  pinMode(MIC_PIN, INPUT);
  pinMode(BOOT_PIN, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found; check I2C wiring/address.");
  } else {
    display.clearDisplay();
    display.display();
  }
  randomSeed(esp_random());
  const uint32_t now = millis();
  moodSince = lastInputAt = now;
  nextMoodAt = now + 12000U;
  nextGazeAt = now + 900U;
  nextBlinkAt = now + 1600U;
  drawFace();
  Serial.println("PETBOT face-refresh ready. HELP for commands. Hold BOOT 1.5s to sleep/wake.");
}

void loop() {
  const uint32_t now = millis();
  readSerialCommands();
  handleButton(now);
  handleMic(now);
  updateAnimations(now);
  if (sleeping || mood == "SLEEPY") noTone(PIEZO_PIN);
  delay(8);
}
