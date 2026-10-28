/*
  PETBOT firmware v2.3
  Board: LILYGO T3 V1.6.1 / ESP32
  Features: expressive OLED face, autonomous moods and blinks, Wi-Fi setup,
  cloud heartbeat and commands, token stored in ESP32 NVS.
  Unused IR input removed; USB Serial RX remains available for provisioning.
  Piezo sound effects on GPIO25 and analog sound-level input on GPIO34.
  Motors, UV sensor and camera remain disabled.
*/

#include <WiFi.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Preferences petbotPrefs;
String deviceToken;
const char* FIRMWARE_VERSION = "petbot-2.3";
const char* CLOUD_BASE = "https://petbotpet.vercel.app";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C
#define OLED_SDA 21
#define OLED_SCL 22

// Optional sound hardware: passive piezo on GPIO25; analog microphone OUT on GPIO34.
#define PETBOT_PIEZO_PIN 25
#define PETBOT_MIC_PIN 34
#define PETBOT_MIC_THRESHOLD_DEFAULT 240  // ADC delta; tune from Serial readings for your specific mic board.

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

String mood = "CURIOUS";
String lastCloudMood = "";
unsigned long moodOverrideUntil = 0;
unsigned long lastActivity = 0;
unsigned long wifiLostAt = 0;
bool wifiWasConnected = false;
unsigned long lastPoll = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastBlink = 0;
unsigned long nextBlinkAt = 0;
unsigned long nextAutonomousMoodAt = 0;
unsigned long nextMicroActionAt = 0;
unsigned long microActionUntil = 0;
String microAction = "";
String notificationMessage = "";
unsigned long notificationUntil = 0;
float gazeX = 0.0f;
float gazeY = 0.0f;
float gazeTargetX = 0.0f;
float gazeTargetY = 0.0f;
bool eyesOpen = true;
bool displayReady = false;
unsigned long lastSoundSample = 0;
unsigned long lastSoundEvent = 0;
unsigned long nextIdleSoundAt = 0;
unsigned long lastMoodSoundAt = 0;
int micBaseline = 0;
int micPeak = 0;
int micThreshold = PETBOT_MIC_THRESHOLD_DEFAULT;
bool soundEnabled = true;
uint32_t interactionCount = 0;
unsigned long lastInteractionAt = 0;

void playRobotSound(const String& soundMood) {
  if (!soundEnabled) return;
  // Short non-blocking-ish sound patterns; tone() duration keeps each call brief.
  if (soundMood == "HAPPY") {
    tone(PETBOT_PIEZO_PIN, 1500, 70);
    delay(90);
    tone(PETBOT_PIEZO_PIN, 2100, 90);
  } else if (soundMood == "SURPRISED") {
    tone(PETBOT_PIEZO_PIN, 2300, 120);
  } else if (soundMood == "LOVE") {
    tone(PETBOT_PIEZO_PIN, 1700, 70);
    delay(90);
    tone(PETBOT_PIEZO_PIN, 1900, 70);
    delay(90);
    tone(PETBOT_PIEZO_PIN, 2300, 100);
  } else if (soundMood == "ANNOYED") {
    // A tiny grumble, like a toy robot protesting.
    tone(PETBOT_PIEZO_PIN, 390, 95);
    delay(105);
    tone(PETBOT_PIEZO_PIN, 300, 115);
  } else if (soundMood == "SLEEPY") {
    tone(PETBOT_PIEZO_PIN, 650, 160);
    delay(180);
    tone(PETBOT_PIEZO_PIN, 470, 210);
  } else if (soundMood == "SAD") {
    tone(PETBOT_PIEZO_PIN, 760, 120);
    delay(140);
    tone(PETBOT_PIEZO_PIN, 520, 190);
  } else {
    tone(PETBOT_PIEZO_PIN, 1200, 55);
  }
}

void scheduleNextIdleSound() {
  // A living companion should vocalize regularly while awake, but remain quiet enough
  // not to become an irritating alarm. Sleep has a slower, softer vocal rhythm.
  if (mood == "SLEEPY") {
    nextIdleSoundAt = millis() + 14000UL + (esp_random() % 21000UL);
  } else {
    nextIdleSoundAt = millis() + 3800UL + (esp_random() % 8200UL);
  }
}

void playIdleSound() {
  // Keep idle sounds brief and vary them by personality.
  const uint32_t variant = esp_random() % 4;
  if (mood == "SLEEPY") {
    if (variant == 0) tone(PETBOT_PIEZO_PIN, 520, 100);
    else if (variant == 1) { tone(PETBOT_PIEZO_PIN, 700, 70); delay(90); tone(PETBOT_PIEZO_PIN, 560, 110); }
    else tone(PETBOT_PIEZO_PIN, 620, 65);
  } else if (mood == "ANNOYED") {
    if (variant == 0) tone(PETBOT_PIEZO_PIN, 380, 85);
    else { tone(PETBOT_PIEZO_PIN, 800, 45); delay(65); tone(PETBOT_PIEZO_PIN, 420, 90); }
  } else if (mood == "LOVE" || mood == "HAPPY") {
    if (variant == 0) { tone(PETBOT_PIEZO_PIN, 1450, 55); delay(70); tone(PETBOT_PIEZO_PIN, 1850, 75); }
    else if (variant == 1) tone(PETBOT_PIEZO_PIN, 2050, 55);
    else { tone(PETBOT_PIEZO_PIN, 1700, 45); delay(55); tone(PETBOT_PIEZO_PIN, 2200, 65); }
  } else if (mood == "SAD") {
    if (variant == 0) { tone(PETBOT_PIEZO_PIN, 700, 80); delay(95); tone(PETBOT_PIEZO_PIN, 520, 120); }
    else if (variant == 1) tone(PETBOT_PIEZO_PIN, 580, 100);
    else { tone(PETBOT_PIEZO_PIN, 620, 70); delay(85); tone(PETBOT_PIEZO_PIN, 490, 105); }
  } else if (mood == "SURPRISED") {
    if (variant == 0) { tone(PETBOT_PIEZO_PIN, 1750, 45); delay(60); tone(PETBOT_PIEZO_PIN, 2250, 90); }
    else if (variant == 1) { tone(PETBOT_PIEZO_PIN, 2200, 70); delay(75); tone(PETBOT_PIEZO_PIN, 1650, 55); }
    else tone(PETBOT_PIEZO_PIN, 1950, 75);
  } else {
    if (variant == 0) { tone(PETBOT_PIEZO_PIN, 1250, 45); delay(65); tone(PETBOT_PIEZO_PIN, 1650, 55); }
    else if (variant == 1) { tone(PETBOT_PIEZO_PIN, 1900, 45); delay(55); tone(PETBOT_PIEZO_PIN, 1450, 60); }
    else if (variant == 2) tone(PETBOT_PIEZO_PIN, 1050, 70);
    else { tone(PETBOT_PIEZO_PIN, 1550, 40); delay(55); tone(PETBOT_PIEZO_PIN, 2000, 45); delay(60); tone(PETBOT_PIEZO_PIN, 1750, 55); }
  }
}

void updateSoundPersonality() {
  const unsigned long now = millis();
  if (!soundEnabled) return;
  if (nextIdleSoundAt == 0) scheduleNextIdleSound();

  // Let event reactions finish before idle chatter resumes.
  if (now < moodOverrideUntil) {
    if (now >= nextIdleSoundAt) scheduleNextIdleSound();
    return;
  }

  // Sleep is not silence: occasional low, gentle "snore"/dream chirps make PETBOT
  // feel alive without keeping the room noisy.
  if (now >= nextIdleSoundAt && now - lastSoundEvent > 1800UL) {
    playIdleSound();
    lastMoodSoundAt = now;
    scheduleNextIdleSound();
  }
}

void sampleMicrophone() {
  const unsigned long now = millis();
  if (now - lastSoundSample < 25) return;
  lastSoundSample = now;

  int sample = analogRead(PETBOT_MIC_PIN);

  // Track the DC baseline and react to rapid changes in microphone level.
  // This detects sounds, but does not identify words or understand speech.
  if (micBaseline == 0) micBaseline = sample;
  int deviation = abs(sample - micBaseline);
  micBaseline = (micBaseline * 15 + sample) / 16;

  static int soundEnvelope = 0;
  soundEnvelope = max(deviation, (soundEnvelope * 3) / 4);

  if (soundEnvelope > micThreshold &&
      now - lastSoundEvent > 1400UL) {
    lastSoundEvent = now;
    lastActivity = now;
    interactionCount++;
    mood = "SURPRISED";
    moodOverrideUntil = now + 2200UL;
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    gazeTargetX = (esp_random() & 1) ? -4.0f : 4.0f;
    gazeTargetY = -1.0f;
    microAction = "HEARD_SOUND";
    microActionUntil = now + 650UL;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putUInt("interactions", interactionCount);
    petbotPrefs.end();
    drawFace();
    playRobotSound("SURPRISED");
    Serial.printf("PETBOT sound: adc=%d baseline=%d delta=%d envelope=%d\\n",
                  sample, micBaseline, deviation, soundEnvelope);
  }
}

String jsonStringField(const String& json, const String& key) {
  String needle = "\"" + key + "\"";
  int keyAt = key == "command"
      ? json.lastIndexOf(needle)
      : json.indexOf(needle);

  if (keyAt < 0) return "";

  int colon = json.indexOf(':', keyAt + needle.length());
  if (colon < 0) return "";

  int firstQuote = json.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";

  int endQuote = json.indexOf('"', firstQuote + 1);
  if (endQuote < 0) return "";

  return json.substring(firstQuote + 1, endQuote);
}

void drawFace() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // Compact status bar: fixed-width labels stay inside the 128px OLED.
  display.setCursor(3, 2);
  display.print("PET");
  display.fillCircle(25, 5, 2, SSD1306_WHITE);
  display.setCursor(104, 2);
  display.print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");

  if (notificationMessage.length() && (long)(notificationUntil - millis()) > 0) {
    display.drawLine(4, 12, 123, 12, SSD1306_WHITE);
    String remaining = notificationMessage;
    int lines = 0;
    display.setCursor(4, 17);
    while (remaining.length() && lines < 4) {
      int cut = remaining.length() > 20 ? 20 : remaining.length();
      if (cut < remaining.length()) {
        int spaceAt = remaining.lastIndexOf(' ', cut);
        if (spaceAt > 0) cut = spaceAt;
      }
      display.println(remaining.substring(0, cut));
      remaining = remaining.substring(cut);
      remaining.trim();
      lines++;
    }
    display.display();
    return;
  }

  const int leftX = 37;
  const int rightX = 91;
  const int eyeY = 34;
  const int eyeW = 29;
  const int eyeH = 35;
  const int glanceX = (int)roundf(gazeX);
  const int glanceY = (int)roundf(gazeY);
  const bool happy = mood == "HAPPY";
  const bool sleepy = !eyesOpen || mood == "SLEEPY";
  const bool annoyed = mood == "ANNOYED";
  const bool sad = mood == "SAD";

  // Vector-inspired face: bold capsule eyes, crisp pupils and small mouth.
  // Keep all expressions inside the 128x64 OLED safe area.
  if (sleepy) {
    display.fillRoundRect(leftX - eyeW/2, eyeY - 4, eyeW, 9, 4, SSD1306_WHITE);
    display.fillRoundRect(rightX - eyeW/2, eyeY - 4, eyeW, 9, 4, SSD1306_WHITE);
    display.drawLine(leftX - 8, eyeY + 9, leftX + 8, eyeY + 9, SSD1306_WHITE);
    display.drawLine(rightX - 8, eyeY + 9, rightX + 8, eyeY + 9, SSD1306_WHITE);
  } else if (mood == "LOVE") {
    // Pixel hearts read clearly on a monochrome screen.
    for (int i = 0; i < 2; i++) {
      int cx = i == 0 ? leftX : rightX;
      cx += glanceX;
      int cy = eyeY - 2 + glanceY;
      display.fillCircle(cx - 5, cy - 2, 5, SSD1306_WHITE);
      display.fillCircle(cx + 5, cy - 2, 5, SSD1306_WHITE);
      display.fillTriangle(cx - 10, cy, cx + 10, cy, cx, cy + 13, SSD1306_WHITE);
    }
  } else if (sad) {
    // Drooping eyelids and inward-tilted brows create a distinct sad expression.
    display.fillRoundRect(leftX - 14, eyeY - 12, 28, 27, 10, SSD1306_WHITE);
    display.fillRoundRect(rightX - 14, eyeY - 12, 28, 27, 10, SSD1306_WHITE);
    display.fillRect(leftX - 15, eyeY - 13, 30, 9, SSD1306_BLACK);
    display.fillRect(rightX - 15, eyeY - 13, 30, 9, SSD1306_BLACK);
    display.fillCircle(leftX + glanceX, eyeY + 4 + glanceY, 5, SSD1306_BLACK);
    display.fillCircle(rightX + glanceX, eyeY + 4 + glanceY, 5, SSD1306_BLACK);
    display.drawLine(leftX - 13, eyeY - 18, leftX + 5, eyeY - 12, SSD1306_WHITE);
    display.drawLine(rightX - 5, eyeY - 12, rightX + 13, eyeY - 18, SSD1306_WHITE);
  } else if (mood == "SURPRISED") {
    display.drawRoundRect(leftX - 14, eyeY - 17, 28, 34, 10, SSD1306_WHITE);
    display.drawRoundRect(rightX - 14, eyeY - 17, 28, 34, 10, SSD1306_WHITE);
    display.fillCircle(leftX + glanceX, eyeY + glanceY, 7, SSD1306_WHITE);
    display.fillCircle(rightX + glanceX, eyeY + glanceY, 7, SSD1306_WHITE);
    display.fillCircle(leftX + glanceX, eyeY + glanceY, 3, SSD1306_BLACK);
    display.fillCircle(rightX + glanceX, eyeY + glanceY, 3, SSD1306_BLACK);
  } else if (happy) {
    // Happy crescent eyes with a lifted outer corner.
    display.drawLine(leftX - 13, eyeY + 3, leftX - 7, eyeY - 5, SSD1306_WHITE);
    display.drawLine(leftX - 7, eyeY - 5, leftX, eyeY - 8, SSD1306_WHITE);
    display.drawLine(leftX, eyeY - 8, leftX + 7, eyeY - 5, SSD1306_WHITE);
    display.drawLine(leftX + 7, eyeY - 5, leftX + 13, eyeY + 3, SSD1306_WHITE);
    display.drawLine(rightX - 13, eyeY + 3, rightX - 7, eyeY - 5, SSD1306_WHITE);
    display.drawLine(rightX - 7, eyeY - 5, rightX, eyeY - 8, SSD1306_WHITE);
    display.drawLine(rightX, eyeY - 8, rightX + 7, eyeY - 5, SSD1306_WHITE);
    display.drawLine(rightX + 7, eyeY - 5, rightX + 13, eyeY + 3, SSD1306_WHITE);
  } else {
    int top = eyeY - eyeH/2;
    int height = eyeH;
    if (annoyed) { top += 5; height -= 7; }
    display.fillRoundRect(leftX - eyeW/2, top, eyeW, height, 11, SSD1306_WHITE);
    display.fillRoundRect(rightX - eyeW/2, top, eyeW, height, 11, SSD1306_WHITE);

    int pupilY = eyeY + glanceY;
    int pupilLX = leftX + 2 + glanceX;
    int pupilRX = rightX + 2 + glanceX;
    if (mood == "CURIOUS") pupilY -= 4;
    if (annoyed) pupilY += 4;
    display.fillCircle(pupilLX, pupilY, 6, SSD1306_BLACK);
    display.fillCircle(pupilRX, pupilY, 6, SSD1306_BLACK);
    // Tiny white catchlights make the eyes feel alive.
    display.fillCircle(pupilLX - 2, pupilY - 2, 2, SSD1306_WHITE);
    display.fillCircle(pupilRX - 2, pupilY - 2, 2, SSD1306_WHITE);

    if (annoyed) {
      display.drawLine(leftX - 13, eyeY - 17, leftX + 7, eyeY - 12, SSD1306_WHITE);
      display.drawLine(rightX - 7, eyeY - 12, rightX + 13, eyeY - 17, SSD1306_WHITE);
    }
  }

  // Tiny mouth glyph adds character without cluttering the display.
  const int mouthY = 56;
  if (happy || mood == "LOVE") {
    display.drawLine(59, mouthY - 1, 64, mouthY + 2, SSD1306_WHITE);
    display.drawLine(64, mouthY + 2, 69, mouthY - 1, SSD1306_WHITE);
  } else if (annoyed) {
    display.drawLine(60, mouthY, 68, mouthY - 2, SSD1306_WHITE);
  } else if (mood == "SURPRISED") {
    display.drawCircle(64, mouthY, 3, SSD1306_WHITE);
  } else if (sad) {
    display.drawLine(58, mouthY + 2, 64, mouthY - 2, SSD1306_WHITE);
    display.drawLine(64, mouthY - 2, 70, mouthY + 2, SSD1306_WHITE);
  } else if (!sleepy) {
    display.drawLine(61, mouthY, 67, mouthY, SSD1306_WHITE);
  }

  display.display();
}
bool cloudRequest(
    const char* path,
    const String& method,
    const String& body,
    String* responseBody
) {
  if (WiFi.status() != WL_CONNECTED ||
      deviceToken.length() < 32) {
    return false;
  }

  HTTPClient http;
  String url = String(CLOUD_BASE) + path;

  if (!http.begin(url)) return false;

  http.setTimeout(4500);
  http.addHeader(
      "Authorization", String("Bearer ") + deviceToken
  );
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  int status = method == "POST"
      ? http.POST(body)
      : http.GET();

  bool ok = status >= 200 && status < 300;

  if (responseBody && status > 0) {
    *responseBody = http.getString();
  }

  if (!ok && status > 0) {
    Serial.printf("Cloud request failed: %s -> HTTP %d\n", path, status);
  }

  http.end();
  return ok;
}

void sendHeartbeat() {
  String body =
      "{\"firmware\":\"" + String(FIRMWARE_VERSION) +
      "\",\"wifi_rssi\":" + String(WiFi.RSSI()) +
      ",\"free_heap\":" + String(ESP.getFreeHeap()) +
      ",\"ip\":\"" + WiFi.localIP().toString() +
      "\",\"mood\":\"" + mood +
      "\",\"autonomy\":true,\"battery_mv\":null,"
      "\"uv_sensor_connected\":false,"
      "\"camera_connected\":false,"
      "\"motors_enabled\":false}";

  if (cloudRequest(
          "/api/device/heartbeat", "POST", body, nullptr)) {
    Serial.println("PETBOT heartbeat OK");
  }
}

void pollCloud() {
  String response;

  if (!cloudRequest(
          "/api/device/poll", "GET", "", &response)) {
    return;
  }

  // The cloud can send either "mood" or "emotion". Prefer an explicit
  // emotion so chat replies can drive the face without a separate button.
  String receivedMood = jsonStringField(response, "emotion");
  if (!receivedMood.length()) receivedMood = jsonStringField(response, "mood");
  receivedMood.toUpperCase();

  const bool validMood =
      receivedMood == "HAPPY" ||
      receivedMood == "CURIOUS" ||
      receivedMood == "SLEEPY" ||
      receivedMood == "SURPRISED" ||
      receivedMood == "ANNOYED" ||
      receivedMood == "LOVE" ||
      receivedMood == "SAD";

  if (validMood && receivedMood != lastCloudMood) {
    lastCloudMood = receivedMood;
    mood = receivedMood;
    playRobotSound(mood);
    moodOverrideUntil = millis() + 12000UL;
    lastActivity = millis();
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;

    Serial.println("Cloud emotion: " + mood);
    drawFace();
  }

  String command = jsonStringField(response, "command");
  command.toUpperCase();

  // Also accept direct expression commands from the admin/chat backend.
  if (command == "HAPPY" || command == "CURIOUS" ||
      command == "SLEEPY" || command == "SURPRISED" ||
      command == "ANNOYED" || command == "LOVE" ||
      command == "SAD") {
    if (command != lastCloudMood) {
      lastCloudMood = command;
      mood = command;
      playRobotSound(mood);
      moodOverrideUntil = millis() + 12000UL;
      lastActivity = millis();
      nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
      drawFace();
    }
    return;
  }

  if (command == "MESSAGE") {
    notificationMessage = jsonStringField(response, "message");
    if (notificationMessage.length()) {
      notificationUntil = millis() + 6000UL;
      lastActivity = millis();
      Serial.println("Cloud notification received.");
      drawFace();
    }
  } else if (command.length()) {
    Serial.println("Cloud command: " + command);

    lastActivity = millis();
    moodOverrideUntil = millis() + 8000UL;

    if (command == "SLEEP") {
      mood = "SLEEPY";
    } else if (command == "WAKE" || command == "PING") {
      mood = "CURIOUS";
    } else if (command == "LOOK_AROUND") {
      mood = "SURPRISED";
    } else if (command == "PET" || command == "LOVE") {
      interactionCount++;
      lastActivity = millis();
      mood = "LOVE";
      moodOverrideUntil = millis() + 9000UL;
      petbotPrefs.begin("petbot", false);
      petbotPrefs.putUInt("interactions", interactionCount);
      petbotPrefs.end();
      playRobotSound("LOVE");
    } else if (command == "PLAY") {
      playRobotSound("HAPPY");
    } else if (command == "MUTE" || command == "SOUND_OFF") {
      soundEnabled = false;
      petbotPrefs.begin("petbot", false);
      petbotPrefs.putBool("sound", false);
      petbotPrefs.end();
    } else if (command == "UNMUTE" || command == "SOUND_ON") {
      soundEnabled = true;
      petbotPrefs.begin("petbot", false);
      petbotPrefs.putBool("sound", true);
      petbotPrefs.end();
    }

    drawFace();
  }
}

void printPetbotHelp() {
  Serial.println("PETBOT commands:");
  Serial.println("STATUS - connection, mood, sound, mic threshold, interactions");
  Serial.println("HELP - show commands");
  Serial.println("PET - affectionate reaction");
  Serial.println("PLAY - happy sound");
  Serial.println("SOUND:ON / SOUND:OFF");
  Serial.println("MIC:THRESHOLD=240 - set sound sensitivity (50..1500)");
  Serial.println("MIC:CAL - sample quiet-room baseline for 3 seconds");
}

void handleSerialProvisioning() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line.startsWith("TOKEN:")) {
    String candidate = line.substring(6);
    candidate.trim();

    if (candidate.length() >= 32 &&
        candidate.length() <= 200) {

      petbotPrefs.begin("petbot", false);
      size_t saved = petbotPrefs.putString(
          "device_token", candidate
      );
      petbotPrefs.end();

      if (saved > 0) {
        deviceToken = candidate;
        Serial.println(
            "PETBOT token saved in ESP32 NVS."
        );
      } else {
        Serial.println(
            "ERROR: token could not be saved."
        );
      }
    } else {
      Serial.println(
          "Invalid token length; expected 32-200 characters."
      );
    }
  } else if (line == "STATUS") {
    Serial.println(String("Firmware: ") + FIRMWARE_VERSION);
    Serial.println(String("Token configured: ") + (deviceToken.length() >= 32 ? "YES" : "NO"));
    Serial.println(String("Wi-Fi connected: ") + (WiFi.status() == WL_CONNECTED ? "YES" : "NO"));
    Serial.println(String("Mood: ") + mood);
    Serial.println(String("Sound enabled: ") + (soundEnabled ? "YES" : "NO"));
    Serial.println(String("Mic threshold: ") + micThreshold);
    Serial.println(String("Interactions: ") + interactionCount);
    Serial.println(String("Free heap: ") + ESP.getFreeHeap());
  } else if (line == "HELP") {
    printPetbotHelp();
  } else if (line == "PET") {
    interactionCount++;
    lastActivity = millis();
    mood = "LOVE";
    moodOverrideUntil = millis() + 9000UL;
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putUInt("interactions", interactionCount);
    petbotPrefs.end();
    playRobotSound("LOVE");
    drawFace();
    Serial.println("PETBOT: happy to see you!");
  } else if (line == "PLAY") {
    playRobotSound("HAPPY");
  } else if (line == "SOUND:ON") {
    soundEnabled = true;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putBool("sound", true);
    petbotPrefs.end();
    Serial.println("Sound enabled.");
  } else if (line == "SOUND:OFF") {
    soundEnabled = false;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putBool("sound", false);
    petbotPrefs.end();
    Serial.println("Sound muted.");
  } else if (line.startsWith("MIC:THRESHOLD=")) {
    int value = line.substring(14).toInt();
    if (value >= 50 && value <= 1500) {
      micThreshold = value;
      petbotPrefs.begin("petbot", false);
      petbotPrefs.putInt("mic_threshold", micThreshold);
      petbotPrefs.end();
      Serial.println(String("Mic threshold set to ") + micThreshold);
    } else {
      Serial.println("Threshold must be between 50 and 1500.");
    }
  } else if (line == "MIC:CAL") {
    Serial.println("Calibrating microphone for 3 seconds. Keep the room quiet...");
    long total = 0;
    int minimum = 4095;
    int maximum = 0;
    const unsigned long started = millis();
    uint32_t count = 0;
    while (millis() - started < 3000UL) {
      int value = analogRead(PETBOT_MIC_PIN);
      total += value;
      minimum = min(minimum, value);
      maximum = max(maximum, value);
      count++;
      delay(5);
    }
    micBaseline = count ? (int)(total / (long)count) : 0;
    int noiseSpan = maximum - minimum;
    micThreshold = constrain(noiseSpan * 3, 80, 1500);
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putInt("mic_threshold", micThreshold);
    petbotPrefs.end();
    Serial.printf("Mic calibrated: baseline=%d noise-span=%d threshold=%d\\n", micBaseline, noiseSpan, micThreshold);
  } else if (line.length()) {
    Serial.println("Unknown command. Type HELP.");
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PETBOT_PIEZO_PIN, OUTPUT);
  pinMode(PETBOT_MIC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PETBOT_MIC_PIN, ADC_11db);

  petbotPrefs.begin("petbot", false);
  deviceToken = petbotPrefs.getString("device_token", "");
  soundEnabled = petbotPrefs.getBool("sound", true);
  micThreshold = petbotPrefs.getInt("mic_threshold", PETBOT_MIC_THRESHOLD_DEFAULT);
  interactionCount = petbotPrefs.getUInt("interactions", 0);
  petbotPrefs.end();

  Serial.println(
      String("PETBOT firmware ") + FIRMWARE_VERSION
  );
  Serial.println(
      String("Device token configured: ") +
      (deviceToken.length() >= 32 ? "YES" : "NO")
  );
  Serial.println(String("Mic threshold: ") + micThreshold + " | sound: " + (soundEnabled ? "ON" : "OFF"));
  printPetbotHelp();

  Wire.begin(OLED_SDA, OLED_SCL);
  displayReady = display.begin(
      SSD1306_SWITCHCAPVCC, OLED_ADDRESS
  );

  if (displayReady) drawFace();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(0);
  wifiManager.setConnectTimeout(25);
  wifiManager.setTitle("PETBOT Wi-Fi setup");

  Serial.println("Starting Wi-Fi provisioning");

  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("PETBOT");
    display.println("Wi-Fi setup");
    display.println("AP: PETBOT-Setup");
    display.println("Pass: PETBOT-4827");
    display.println("Open 192.168.4.1");
    display.display();
  }

  bool connected = wifiManager.autoConnect(
      "PETBOT-Setup", "PETBOT-4827"
  );

  if (!connected) {
    Serial.println("Wi-Fi setup failed; restarting");
    delay(1000);
    ESP.restart();
  }

  Serial.println("Wi-Fi connected: " + WiFi.SSID());
  Serial.println("IP address: " + WiFi.localIP().toString());

  lastActivity = millis();
  wifiWasConnected = true;

  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("PETBOT online");
    display.println(WiFi.SSID());
    display.println(WiFi.localIP());
    display.display();
    delay(1800);
  }

  drawFace();
  playRobotSound("HAPPY");  // short boot-up greeting once the face is online
  scheduleNextIdleSound();
}

String chooseAutonomousMood(const String& current) {
  // PETBOT should feel curious and warm, not like a slot machine of random emotions.
  // Sleep is driven by inactivity; LOVE and ANNOYED are reserved for meaningful events.
  const unsigned long idleFor = millis() - lastActivity;

  if (idleFor > 120000UL) return "SLEEPY";

  // Mostly curious, with occasional happy/surprised moments. Avoid abrupt sadness,
  // annoyance or love without a reason.
  const uint32_t roll = esp_random() % 100;
  String next;
  if (idleFor > 45000UL) {
    next = roll < 52 ? "CURIOUS" : roll < 82 ? "HAPPY" : "SLEEPY";
  } else {
    next = roll < 48 ? "CURIOUS" : roll < 78 ? "HAPPY" : "SURPRISED";
  }

  if (next == current) {
    if (current == "CURIOUS") next = roll < 65 ? "HAPPY" : "SURPRISED";
    else next = "CURIOUS";
  }
  return next;
}

void updateMicroAction() {
  const unsigned long now = millis();

  // Ease the eyes toward their target instead of jumping between positions.
  const float easing = 0.22f;
  float oldX = gazeX;
  float oldY = gazeY;
  gazeX += (gazeTargetX - gazeX) * easing;
  gazeY += (gazeTargetY - gazeY) * easing;
  if (fabsf(gazeTargetX - gazeX) < 0.18f) gazeX = gazeTargetX;
  if (fabsf(gazeTargetY - gazeY) < 0.18f) gazeY = gazeTargetY;
  bool gazeMoved = (fabsf(gazeX - oldX) > 0.15f || fabsf(gazeY - oldY) > 0.15f);

  if (now < moodOverrideUntil || mood == "SLEEPY") {
    gazeTargetX = 0.0f;
    gazeTargetY = 0.0f;
    microAction = "";
    if (gazeMoved) drawFace();
    nextMicroActionAt = now + 1200UL;
    return;
  }

  if (microAction.length() && now >= microActionUntil) {
    microAction = "";
    gazeTargetX = 0.0f;
    gazeTargetY = 0.0f;
    nextMicroActionAt = now + 1600UL + (esp_random() % 2600UL);
  }

  if (!microAction.length()) {
    if (nextMicroActionAt == 0) {
      nextMicroActionAt = now + 1000UL + (esp_random() % 1600UL);
    }
    if (now >= nextMicroActionAt) {
      const uint32_t action = esp_random() % 4;
      microAction = action == 0 ? "LOOK_LEFT" :
                    action == 1 ? "LOOK_RIGHT" :
                    action == 2 ? "LOOK_UP" : "LOOK_DIAGONAL";
      gazeTargetX = action == 0 ? -5.0f :
                    action == 1 ? 5.0f :
                    action == 3 ? 3.0f : 0.0f;
      gazeTargetY = action == 2 ? -4.0f : (action == 3 ? -2.0f : 0.0f);
      microActionUntil = now + 450UL + (esp_random() % 650UL);

      // Occasionally mutter while inspecting the world; not every glance gets a sound.
      if (soundEnabled && (esp_random() % 100) < 16 && now - lastMoodSoundAt > 5000UL) {
        if (action == 0 || action == 1) {
          tone(PETBOT_PIEZO_PIN, 950 + (esp_random() % 500), 45);
        } else if (action == 2) {
          tone(PETBOT_PIEZO_PIN, 1550, 45);
          delay(55);
          tone(PETBOT_PIEZO_PIN, 1250, 55);
        } else {
          tone(PETBOT_PIEZO_PIN, 1100, 40);
          delay(50);
          tone(PETBOT_PIEZO_PIN, 1750, 50);
        }
        lastMoodSoundAt = now;
      }
    }
  }

  if (gazeMoved) drawFace();
}

void updateAutonomousMood() {
  const unsigned long now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;
  const unsigned long idleFor = now - lastActivity;

  if (nextAutonomousMoodAt == 0) {
    nextAutonomousMoodAt = now + 5000UL + (esp_random() % 5000UL);
  }

  if (!connected) {
    if (wifiWasConnected || wifiLostAt == 0) wifiLostAt = now;

    // Losing connection briefly frustrates PETBOT; prolonged isolation makes it drowsy.
    if (now >= moodOverrideUntil) {
      String offlineMood = (now - wifiLostAt < 18000UL) ? "ANNOYED" : "SLEEPY";
      if (offlineMood != mood) {
        mood = offlineMood;
        playRobotSound(offlineMood);  // grumble at a lost connection, then droop into sleep
        scheduleNextIdleSound();
        drawFace();
      }
    }
  } else {
    if (!wifiWasConnected) {
      // Reconnection feels like someone has come back.
      mood = "SURPRISED";
      playRobotSound(mood);
      moodOverrideUntil = now + 5000UL;
      lastActivity = now;
      nextAutonomousMoodAt = moodOverrideUntil + 5000UL;
      drawFace();
    } else if (now >= moodOverrideUntil && now >= nextAutonomousMoodAt) {
      String next = chooseAutonomousMood(mood);

      // Idle behavior has a slower rhythm. After two minutes without interaction,
      // PETBOT settles into sleep instead of endlessly changing expressions.
      if (idleFor > 120000UL) {
        next = "SLEEPY";
      } else if (idleFor < 15000UL && next == "SLEEPY") {
        next = "CURIOUS";
      }

      if (next != mood) {
        mood = next;
        drawFace();
        Serial.println("Autonomous mood: " + mood);
        // Only occasional sounds: the character should not chirp every few seconds.
        if (mood != "SLEEPY" && now - lastMoodSoundAt > 7000UL &&
            (esp_random() % 100) < 35) {
          playRobotSound(mood);
          lastMoodSoundAt = now;
        }
      }

      // Give each mood room to breathe instead of flipping every few seconds.
      nextAutonomousMoodAt = now + 12000UL + (esp_random() % 13000UL);
    }
    wifiLostAt = 0;
  }

  wifiWasConnected = connected;
}

void loop() {
  handleSerialProvisioning();
  if (notificationMessage.length() && (long)(millis() - notificationUntil) >= 0) {
    notificationMessage = "";
    drawFace();
  }
  updateAutonomousMood();
  updateMicroAction();
  sampleMicrophone();
  updateSoundPersonality();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWifiRetry = 0;

    if (millis() - lastWifiRetry > 15000UL) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
  } else {
    if (millis() - lastPoll >= 5000UL) {
      lastPoll = millis();
      pollCloud();
    }

    if (millis() - lastHeartbeat >= 15000UL) {
      lastHeartbeat = millis();
      sendHeartbeat();
    }
  }

  if (nextBlinkAt == 0) nextBlinkAt = millis() + 1200UL;
  if ((long)(millis() - nextBlinkAt) >= 0) {
    lastBlink = millis();
    nextBlinkAt = millis() + 1200UL + (esp_random() % 2200UL);

    eyesOpen = false;
    drawFace();
    delay(130);

    eyesOpen = true;
    drawFace();
  }

  delay(20);
}
