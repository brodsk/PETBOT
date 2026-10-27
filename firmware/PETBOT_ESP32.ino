/*
  PETBOT firmware v1.4
  Board: LILYGO T3 V1.6.1 / ESP32
  Features: expressive OLED face, autonomous moods and blinks, Wi-Fi setup,
  cloud heartbeat and commands, token stored in ESP32 NVS.
  Unused IR input removed; USB Serial RX remains available for provisioning.
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
const char* FIRMWARE_VERSION = "petbot-1.5";
const char* CLOUD_BASE = "https://petbotpet.vercel.app";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C
#define OLED_SDA 21
#define OLED_SCL 22

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

  // Minimal status bar; the face, not text, is the main character.
  display.setCursor(4, 2);
  display.print("PET");
  display.fillCircle(28, 5, 2, SSD1306_WHITE);
  display.setCursor(98, 2);
  display.print(WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFF");

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

  String receivedMood = jsonStringField(response, "mood");

  if (receivedMood.length() &&
      receivedMood != lastCloudMood &&
      (receivedMood == "HAPPY" ||
       receivedMood == "CURIOUS" ||
       receivedMood == "SLEEPY" ||
       receivedMood == "SURPRISED" ||
       receivedMood == "ANNOYED" ||
       receivedMood == "LOVE")) {

    lastCloudMood = receivedMood;
    mood = receivedMood;
    moodOverrideUntil = millis() + 20000UL;
    lastActivity = millis();

    Serial.println("Cloud mood: " + mood);
    drawFace();
  }

  String command = jsonStringField(response, "command");

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
    }

    drawFace();
  }
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
    Serial.println(
        String("Token configured: ") +
        (deviceToken.length() >= 32 ? "YES" : "NO")
    );
    Serial.println(
        String("Wi-Fi connected: ") +
        (WiFi.status() == WL_CONNECTED ? "YES" : "NO")
    );
  } else if (line.length()) {
  }
}

void setup() {
  Serial.begin(115200);

  petbotPrefs.begin("petbot", true);
  deviceToken = petbotPrefs.getString("device_token", "");
  petbotPrefs.end();

  Serial.println(
      String("PETBOT firmware ") + FIRMWARE_VERSION
  );
  Serial.println(
      String("Device token configured: ") +
      (deviceToken.length() >= 32 ? "YES" : "NO")
  );

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
}

String chooseAutonomousMood(const String& current) {
  // Small personality loop: keep changing expressions, but never repeat
  // the current one immediately. Cloud-selected moods get a temporary hold.
  const char* choices[] = {
    "CURIOUS", "HAPPY", "LOVE", "SURPRISED", "ANNOYED", "SLEEPY"
  };
  String next = current;
  for (int attempt = 0; attempt < 8 && next == current; attempt++) {
    next = choices[esp_random() % 6];
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
    }
  }

  if (gazeMoved) drawFace();
}

void updateAutonomousMood() {
  const unsigned long now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (nextAutonomousMoodAt == 0) {
    nextAutonomousMoodAt = now + 2500UL + (esp_random() % 2500UL);
  }

  if (!connected) {
    if (wifiWasConnected || wifiLostAt == 0) wifiLostAt = now;
    if (now >= moodOverrideUntil) {
      String next = (now - wifiLostAt < 12000UL) ? "ANNOYED" : "SLEEPY";
      if (next != mood) {
        mood = next;
        drawFace();
      }
    }
  } else {
    if (!wifiWasConnected) {
      mood = "SURPRISED";
      moodOverrideUntil = now + 8000UL;
      lastActivity = now;
      nextAutonomousMoodAt = moodOverrideUntil + 3000UL;
      drawFace();
    } else if (now >= moodOverrideUntil && now >= nextAutonomousMoodAt) {
      String next = chooseAutonomousMood(mood);
      mood = next;
      lastActivity = now;
      // Expressions last a little different amount of time for a less robotic feel.
      nextAutonomousMoodAt = now + 3500UL + (esp_random() % 4500UL);
      drawFace();
      Serial.println("Autonomous mood: " + mood);
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
