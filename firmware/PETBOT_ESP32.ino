/*
  PETBOT firmware v0.4 — LILYGO T3 V1.6.1 / ESP32
  Autonomous moods + Wi-Fi captive-portal onboarding. Motors remain disabled.
  Install Adafruit GFX, Adafruit SSD1306 and WiFiManager libraries in Arduino IDE.
*/
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* DEVICE_TOKEN = "PASTE_PETBOT_DEVICE_TOKEN_HERE";
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
bool eyesOpen = true;
bool displayReady = false;

String jsonStringField(const String& json, const String& key) {
  String needle = "\"" + key + "\"";
  int keyAt = key == "command" ? json.lastIndexOf(needle) : json.indexOf(needle);
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
  display.setCursor(0, 0);
  display.print("PETBOT  ");
  display.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "Offline");

  int leftX = 38, rightX = 90, eyeY = 31;
  int eyeW = 23, eyeH = 25;
  if (!eyesOpen || mood == "SLEEPY") {
    display.drawLine(leftX - 10, eyeY, leftX + 10, eyeY, SSD1306_WHITE);
    display.drawLine(rightX - 10, eyeY, rightX + 10, eyeY, SSD1306_WHITE);
  } else if (mood == "SURPRISED") {
    display.drawCircle(leftX, eyeY, 12, SSD1306_WHITE);
    display.drawCircle(rightX, eyeY, 12, SSD1306_WHITE);
    display.fillCircle(leftX, eyeY, 4, SSD1306_WHITE);
    display.fillCircle(rightX, eyeY, 4, SSD1306_WHITE);
  } else if (mood == "LOVE") {
    display.fillCircle(leftX - 5, eyeY - 3, 6, SSD1306_WHITE);
    display.fillCircle(leftX + 5, eyeY - 3, 6, SSD1306_WHITE);
    display.fillTriangle(leftX - 11, eyeY, leftX + 11, eyeY, leftX, eyeY + 13, SSD1306_WHITE);
    display.fillCircle(rightX - 5, eyeY - 3, 6, SSD1306_WHITE);
    display.fillCircle(rightX + 5, eyeY - 3, 6, SSD1306_WHITE);
    display.fillTriangle(rightX - 11, eyeY, rightX + 11, eyeY, rightX, eyeY + 13, SSD1306_WHITE);
  } else {
    if (mood == "ANNOYED") {
      display.drawLine(leftX - 12, eyeY - 15, leftX + 8, eyeY - 9, SSD1306_WHITE);
      display.drawLine(rightX - 8, eyeY - 9, rightX + 12, eyeY - 15, SSD1306_WHITE);
    }
    display.fillRoundRect(leftX - eyeW / 2, eyeY - eyeH / 2, eyeW, eyeH, 8, SSD1306_WHITE);
    display.fillRoundRect(rightX - eyeW / 2, eyeY - eyeH / 2, eyeW, eyeH, 8, SSD1306_WHITE);
    display.fillCircle(leftX + 3, eyeY + 1, 4, SSD1306_BLACK);
    display.fillCircle(rightX + 3, eyeY + 1, 4, SSD1306_BLACK);
  }

  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("Mood: ");
  display.print(mood);
  display.display();
}

bool cloudRequest(const char* path, const String& method, const String& body, String* responseBody) {
  if (WiFi.status() != WL_CONNECTED || String(DEVICE_TOKEN).startsWith("PASTE_")) return false;
  HTTPClient http;
  String url = String(CLOUD_BASE) + path;
  if (!http.begin(url)) return false;
  http.setTimeout(4500);
  http.addHeader("Authorization", String("Bearer ") + DEVICE_TOKEN);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  int status = method == "POST" ? http.POST(body) : http.GET();
  bool ok = status >= 200 && status < 300;
  if (responseBody && status > 0) *responseBody = http.getString();
  http.end();
  return ok;
}

void sendHeartbeat() {
  String body = "{\"firmware\":\"petbot-0.3-emotions\",\"wifi_rssi\":" + String(WiFi.RSSI()) +
    ",\"free_heap\":" + String(ESP.getFreeHeap()) + ",\"ip\":\"" + WiFi.localIP().toString() +
    "\",\"mood\":\"" + mood + "\",\"autonomy\":true,\"battery_mv\":null," +
    "\"uv_sensor_connected\":false,\"camera_connected\":false,\"motors_enabled\":false}";
  cloudRequest("/api/device/heartbeat", "POST", body, nullptr);
}

void pollCloud() {
  String response;
  if (!cloudRequest("/api/device/poll", "GET", "", &response)) return;
  String receivedMood = jsonStringField(response, "mood");
  // A cloud mood is treated as a short-lived suggestion, not a permanent override.
  // Only react when the cloud value changes; polling the same value must not freeze autonomy.
  if (receivedMood.length() && receivedMood != lastCloudMood &&
      (receivedMood == "HAPPY" || receivedMood == "CURIOUS" || receivedMood == "SLEEPY" ||
       receivedMood == "SURPRISED" || receivedMood == "ANNOYED" || receivedMood == "LOVE")) {
    lastCloudMood = receivedMood;
    mood = receivedMood;
    moodOverrideUntil = millis() + 20000UL;
    lastActivity = millis();
    Serial.println("Temporary cloud mood: " + mood);
    drawFace();
  }
  String command = jsonStringField(response, "command");
  if (command.length()) {
    // Commands are acknowledged for diagnostics only. No motors are driven by this firmware.
    Serial.println("Cloud command received (safe demo only): " + command);
    lastActivity = millis();
    moodOverrideUntil = millis() + 8000UL;
    if (command == "SLEEP") mood = "SLEEPY";
    if (command == "WAKE" || command == "PING") mood = "CURIOUS";
    if (command == "LOOK_AROUND") mood = "SURPRISED";
    drawFace();
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  if (displayReady) drawFace();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(0); // Keep setup AP available until Wi-Fi is configured.
  wifiManager.setConnectTimeout(25);
  wifiManager.setTitle("PETBOT Wi-Fi setup");
  Serial.println("PETBOT firmware v0.4 starting Wi-Fi provisioning");
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
  // First boot: connect to PETBOT-Setup from a phone/laptop, then open
  // http://192.168.4.1 and save the home Wi-Fi credentials. WiFiManager
  // persists them in ESP32 NVS; subsequent boots reconnect automatically.
  bool connected = wifiManager.autoConnect("PETBOT-Setup", "PETBOT-4827");
  if (!connected) {
    Serial.println("Wi-Fi setup did not complete; restarting");
    delay(1000);
    ESP.restart();
  }
  Serial.println("Wi-Fi connected: " + WiFi.SSID());
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

void updateAutonomousMood() {
  const unsigned long now = millis();
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (!connected) {
    if (wifiWasConnected || wifiLostAt == 0) wifiLostAt = now;
    if (now >= moodOverrideUntil) {
      mood = (now - wifiLostAt < 12000UL) ? "ANNOYED" : "SLEEPY";
    }
  } else {
    if (!wifiWasConnected) {
      // Reconnection is a noticeable event, but it fades quickly.
      mood = "SURPRISED";
      moodOverrideUntil = now + 8000UL;
      lastActivity = now;
    } else if (now >= moodOverrideUntil) {
      mood = (now - lastActivity > 120000UL) ? "SLEEPY" : "CURIOUS";
    }
    wifiLostAt = 0;
  }

  wifiWasConnected = connected;
}

void loop() {
  updateAutonomousMood();
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWifiRetry = 0;
    if (millis() - lastWifiRetry > 15000) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
  } else {
    if (millis() - lastPoll >= 5000) {
      lastPoll = millis();
      pollCloud();
    }
    if (millis() - lastHeartbeat >= 15000) {
      lastHeartbeat = millis();
      sendHeartbeat();
    }
  }

  if (millis() - lastBlink > 4200) {
    lastBlink = millis();
    eyesOpen = false;
    drawFace();
    delay(130);
    eyesOpen = true;
    drawFace();
  }
  delay(20);
}
