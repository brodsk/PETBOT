/*
  PETBOT firmware v3.3.0
  Board: LILYGO T3 V1.6.1 / classic ESP32
  Features: local Vector-inspired behavior model, seven internal drives,
  OLED face, autonomous gaze, piezo sound motifs, optional cloud commands.
  Core behavior continues without Wi-Fi after a bounded provisioning attempt.
  GPIO34 analog sound input is disabled by default. Motors and camera are not present.
*/

#include <WiFi.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <freertos/semphr.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Preferences petbotPrefs;
String deviceToken;
const char* FIRMWARE_VERSION = "petbot-3.4.0";
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
#define PETBOT_BUTTON_PIN 0  // BOOT button on most T3 V1.6.1 boards, active low
#define PETBOT_TOUCH_PIN 4   // optional bare-wire capacitive touch pad (touch channel T0)
bool touchEnabled = false;
int touchBaseline = 0;
void calibrateTouch();

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
bool blinking = false;             // non-blocking blink state (see loop)
unsigned long blinkUntil = 0;
String lastDrawnMood = "";         // detects emotion changes for the onset "pop"
unsigned long moodChangedAt = 0;
bool displayReady = false;
unsigned long lastSoundSample = 0;
unsigned long lastInteractionAt = 0;
bool micEnabled = false; // optional input is off until a compatible sensor is wired
unsigned long lastSoundEvent = 0;
unsigned long nextIdleSoundAt = 0;
unsigned long lastMoodSoundAt = 0;
int micBaseline = 0;
int micPeak = 0;
int micThreshold = PETBOT_MIC_THRESHOLD_DEFAULT;
bool soundEnabled = true;
uint32_t interactionCount = 0;

// Local pet needs/personality model. Runs on the ESP32 even without cloud access.
int petCuriosity = 58;       // rises during quiet time; positive interaction settles it
int petEnergy = 88;          // falls while awake and recovers while sleeping
int petMoodValue = 68;       // general affect, separate from the displayed mood label
int petAttachment = 12;      // grows gradually through positive interactions
int petStimulation = 24;     // short-lived arousal/alertness from events
int petSocialNeed = 32;      // rises slowly during isolation; contact relieves it
int petConfidence = 52;      // rises with successful interactions, falls after setbacks
unsigned long lastNeedsUpdate = 0;
unsigned long lastNeedsSave = 0;

void savePetNeeds() {
  petbotPrefs.begin("petbot", false);
  petbotPrefs.putInt("curiosity", petCuriosity);
  petbotPrefs.putInt("energy", petEnergy);
  petbotPrefs.putInt("mood_value", petMoodValue);
  petbotPrefs.putInt("attachment", petAttachment);
  petbotPrefs.putInt("stimulation", petStimulation);
  petbotPrefs.putInt("social_need", petSocialNeed);
  petbotPrefs.putInt("confidence", petConfidence);
  petbotPrefs.end();
  lastNeedsSave = millis();
}

void updatePetNeeds() {
  const unsigned long now = millis();
  if (lastNeedsUpdate == 0) {
    lastNeedsUpdate = now;
    return;
  }
  const unsigned long elapsed = now - lastNeedsUpdate;
  if (elapsed < 60000UL) return;
  const int minutes = (int)min((unsigned long)6, elapsed / 60000UL);
  lastNeedsUpdate = now;

  const bool sleeping = mood == "SLEEPY" || petEnergy < 14;
  petStimulation = max(0, petStimulation - minutes * (sleeping ? 8 : 5));
  if (sleeping) {
    petEnergy = min(100, petEnergy + minutes * 5);
    petCuriosity = max(8, petCuriosity - minutes * 2);
    petMoodValue = min(100, petMoodValue + minutes);
    petSocialNeed = min(100, petSocialNeed + minutes);
  } else {
    petEnergy = max(0, petEnergy - minutes);
    if (now - lastActivity > 12000UL) {
      petCuriosity = min(100, petCuriosity + minutes * 2);
      petSocialNeed = min(100, petSocialNeed + minutes);
    }
    if (now - lastActivity > 90000UL) {
      const int moodDecay = petAttachment >= 60 ? 0 : (petAttachment >= 30 ? max(0, minutes - 1) : minutes);
      petMoodValue = max(20, petMoodValue - moodDecay);
    }
  }
  if (now - lastNeedsSave > 60000UL) savePetNeeds();
}

void registerPetInteraction(bool affectionate) {
  const unsigned long now = millis();
  lastActivity = now;
  lastInteractionAt = now;
  interactionCount++;
  petEnergy = min(100, petEnergy + 8);
  petMoodValue = min(100, petMoodValue + (affectionate ? 12 : 6));
  petCuriosity = max(10, petCuriosity - 12);
  petStimulation = min(100, petStimulation + (affectionate ? 18 : 12));
  petSocialNeed = max(0, petSocialNeed - (affectionate ? 22 : 10));
  petConfidence = min(100, petConfidence + (affectionate ? 2 : 1));
  if (affectionate) petAttachment = min(100, petAttachment + (petAttachment < 60 ? 3 : 1));
  if (now - lastNeedsSave > 60000UL) savePetNeeds();
  petbotPrefs.begin("petbot", false);
  petbotPrefs.putUInt("interactions", interactionCount);
  petbotPrefs.end();
}
// ---------------------------------------------------------------------------
// PETBOT voice: Vector-style non-verbal vocalizations on a passive piezo.
// The phrase engine now builds more expressive robot syllables, not only pure
// tone glides. This makes idle sounds and reactions feel more like a living
// companion instead of a simple buzzer.
// ---------------------------------------------------------------------------
enum VoiceMod : uint8_t { VM_GLIDE, VM_VIBRATO, VM_PURR, VM_TRILL, VM_REST, VM_BURBLE, VM_CHIRP };
struct VoiceSeg { uint16_t f0; uint16_t f1; uint16_t ms; uint8_t mod; };
#define VOICE_MAX_SEGS 12
// Request byte: low 7 bits = mood code (1..8), bit 7 = low-priority idle murmur.
volatile uint8_t voiceRequest = 0;
VoiceSeg voiceSegs[VOICE_MAX_SEGS];  // touched only by the voice task
uint8_t voiceLen = 0;

uint8_t voiceCodeForMood(const String& m) {
  if (m == "HAPPY") return 1;
  if (m == "CURIOUS") return 2;
  if (m == "SURPRISED") return 3;
  if (m == "LOVE") return 4;
  if (m == "ANNOYED") return 5;
  if (m == "SLEEPY") return 6;
  if (m == "SAD") return 7;
  if (m == "EXCITED") return 9;
  if (m == "SHY") return 10;
  if (m == "CONFUSED") return 11;
  if (m == "BORED") return 12;
  if (m == "SCARED") return 13;
  return 8;
}

// base +/- pct percent
static uint16_t vj(uint16_t base, uint8_t pct) {
  const int d = (int)base * pct / 100;
  return (uint16_t)((int)base + (int)(esp_random() % (2 * d + 1)) - d);
}

static void vAdd(uint16_t f0, uint16_t f1, uint16_t ms, uint8_t mod) {
  if (voiceLen >= VOICE_MAX_SEGS) return;
  float tempo = 1.0f, pitch = 1.0f;
  if (petEnergy < 25) { tempo = 1.35f; pitch = 0.88f; }
  else if (petStimulation > 65) { tempo = 0.85f; pitch = 1.08f; }
  if (mod != VM_REST) {
    f0 = (uint16_t)max(300.0f, min(4200.0f, f0 * pitch));
    f1 = (uint16_t)max(300.0f, min(4200.0f, f1 * pitch));
  }
  voiceSegs[voiceLen++] = {f0, f1, (uint16_t)(ms * tempo), mod};
}
static void vRest(uint16_t ms) { vAdd(0, 0, ms, VM_REST); }

static void buildVoice(uint8_t code, bool idle) {
  voiceLen = 0;
  const uint32_t r = esp_random() % 4;
  switch (code) {
    case 1:  // HAPPY: bright rising chirps, ends in a bird-like trill
      if (idle) { vAdd(vj(1500, 10), vj(2100, 10), 70, VM_BURBLE); vRest(25); vAdd(vj(850, 15), vj(1200, 15), 80, VM_CHIRP); break; }
      if (r == 0) { vAdd(1300, 2300, 90, VM_GLIDE); vRest(35); vAdd(1700, 2700, 80, VM_BURBLE); vAdd(2400, 2400, 130, VM_TRILL); }
      else if (r == 1) { vAdd(1600, 2400, 70, VM_GLIDE); vAdd(2400, 1500, 110, VM_VIBRATO); vRest(30); vAdd(1500, 2500, 100, VM_CHIRP); }
      else if (r == 2) { vAdd(1400, 2000, 60, VM_CHIRP); vRest(25); vAdd(1400, 2000, 60, VM_BURBLE); vRest(25); vAdd(1800, 2800, 120, VM_TRILL); }
      else { vAdd(1200, 1900, 80, VM_CHIRP); vRest(25); vAdd(1700, 2600, 90, VM_GLIDE); vRest(20); vAdd(2200, 2200, 150, VM_TRILL); }
      break;
    case 2:  // CURIOUS: question-like upward glides and wobbles
      if (idle) { vAdd(vj(1000, 15), vj(1500, 15), 90, VM_BURBLE); vRest(35); vAdd(vj(750, 10), vj(1200, 12), 60, VM_CHIRP); break; }
      if (r == 0) { vAdd(900, 1500, 150, VM_GLIDE); vRest(60); vAdd(1500, 1700, 130, VM_VIBRATO); }
      else if (r == 1) { vAdd(1200, 1000, 60, VM_BURBLE); vRest(40); vAdd(1000, 1800, 160, VM_GLIDE); vAdd(1800, 1800, 90, VM_TRILL); }
      else if (r == 2) { vAdd(1100, 1700, 110, VM_VIBRATO); vRest(50); vAdd(1300, 1900, 90, VM_BURBLE); }
      else { vAdd(800, 1600, 120, VM_CHIRP); vRest(35); vAdd(1600, 2200, 140, VM_GLIDE); }
      break;
    case 3:  // SURPRISED: quick squeak, then a wobbly fall
      vAdd(1400, 2900, 60, VM_CHIRP);
      vAdd(2900, 1800, 130, VM_VIBRATO);
      if (r == 2) { vRest(40); vAdd(1500, 2200, 70, VM_GLIDE); }
      else if (r == 3) { vAdd(2200, 1600, 120, VM_BURBLE); }
      break;
    case 4: {  // LOVE: a coo and a purr; the more attached, the longer it purrs
      const uint16_t purrMs = (uint16_t)(450 + min(100, max(0, petAttachment)) * 5);
      if (idle) { vAdd(1200, 1500, 150, VM_VIBRATO); vAdd(1100, 900, purrMs / 2, VM_PURR); vRest(25); vAdd(1500, 1800, 120, VM_BURBLE); break; }
      vAdd(1100, 1700, 140, VM_GLIDE);
      vAdd(1100, 900, purrMs, VM_PURR);
      vAdd(1700, 1300, 200, VM_VIBRATO);
      if (r == 2) vAdd(1300, 1600, 110, VM_BURBLE);
      break;
    }
    case 5:  // ANNOYED: low grumble, then a disapproving drop
      if (idle) { vAdd(600, 450, 180, VM_PURR); vRest(25); vAdd(520, 420, 120, VM_CHIRP); break; }
      vAdd(700, 500, 260, VM_PURR);
      vRest(50);
      vAdd(520, 320, 140, VM_GLIDE);
      if (r == 2) { vRest(40); vAdd(450, 450, 200, VM_PURR); }
      else if (r == 3) { vRest(20); vAdd(370, 280, 130, VM_BURBLE); }
      break;
    case 6:  // SLEEPY: a yawn; when idle, a slow snore
      if (idle) { vAdd(700, 520, 320, VM_PURR); vRest(90); vAdd(430, 360, 220, VM_BURBLE); break; }
      vAdd(1200, 1500, 200, VM_VIBRATO);
      vAdd(1500, 500, 520, VM_VIBRATO);
      if (r == 3) vAdd(470, 330, 280, VM_PURR);
      break;
    case 7:  // SAD: slow descending whimper
      vAdd(1300, 800, idle ? 260 : 420, VM_VIBRATO);
      if (!idle) { vRest(90); vAdd(1000, 520, 380, VM_BURBLE); }
      else { vRest(40); vAdd(600, 400, 240, VM_PURR); }
      break;
    case 9:  // EXCITED: fast rising triple chirp and a high trill
      if (idle) { vAdd(1800, 2800, 60, VM_CHIRP); vRest(20); vAdd(2000, 3000, 60, VM_CHIRP); break; }
      vAdd(1500, 2600, 55, VM_CHIRP); vRest(20);
      vAdd(1800, 2900, 55, VM_CHIRP); vRest(20);
      vAdd(2100, 3100, 70, VM_GLIDE);
      vAdd(2800, 2800, r == 1 ? 260 : 170, VM_TRILL);
      break;
    case 10:  // SHY: soft hesitant coo, a pause, a smaller coo
      if (idle) { vAdd(1100, 1300, 140, VM_VIBRATO); break; }
      vAdd(1000, 1300, 120, VM_VIBRATO); vRest(130);
      vAdd(1250, 1050, 170, VM_VIBRATO);
      if (r == 2) { vRest(90); vAdd(1000, 1150, 100, VM_BURBLE); }
      break;
    case 11:  // CONFUSED: "hm? hmm?" wobbles that end on a rising question
      if (idle) { vAdd(1000, 1400, 120, VM_VIBRATO); break; }
      vAdd(900, 1100, 130, VM_VIBRATO); vRest(60);
      vAdd(1200, 900, 100, VM_GLIDE); vRest(50);
      vAdd(900, 1700, 190, VM_GLIDE);
      break;
    case 12:  // BORED: a long sigh
      if (idle) { vAdd(900, 600, 300, VM_VIBRATO); break; }
      vAdd(1300, 650, 700, VM_VIBRATO);
      if (r == 3) { vRest(120); vAdd(700, 520, 260, VM_PURR); }
      break;
    case 13:  // SCARED: trembling squeal, a gulp of silence, a shaky whimper
      if (idle) { vAdd(2300, 2800, 120, VM_TRILL); break; }
      vAdd(2200, 3000, 70, VM_GLIDE);
      vAdd(3000, 3000, 260, VM_TRILL); vRest(70);
      vAdd(2400, 2000, 160, VM_TRILL);
      break;
    default:
      vAdd(1200, 1200, 55, VM_BURBLE);
      vAdd(1300, 1000, 80, VM_CHIRP);
  }
}

// Returns true if the phrase was interrupted by a newer request.
static bool playVoiceSegs(bool idle) {
  unsigned int lastF = 0;
  for (uint8_t i = 0; i < voiceLen; i++) {
    const VoiceSeg s = voiceSegs[i];
    const uint32_t t0 = millis();
    uint32_t t;
    if (s.mod == VM_REST) { noTone(PETBOT_PIEZO_PIN); lastF = 0; }
    while ((t = millis() - t0) < s.ms) {
      if (!soundEnabled) { noTone(PETBOT_PIEZO_PIN); return true; }
      const uint8_t req = voiceRequest;
      if (req) {
        // A real reaction interrupts anything; an idle murmur never interrupts a reaction.
        if (idle || !(req & 0x80)) { noTone(PETBOT_PIEZO_PIN); return true; }
        voiceRequest = 0;
      }
      if (s.mod != VM_REST) {
        float f = (float)s.f0 + ((float)s.f1 - (float)s.f0) * ((float)t / (float)s.ms);
        if (s.mod == VM_VIBRATO) f *= 1.0f + 0.045f * sinf((float)t * 0.0408f);  // ~6.5 Hz wobble
        else if (s.mod == VM_PURR) f *= ((t / 18) & 1) ? 0.90f : 1.0f;            // ~28 Hz flutter = purr
        else if (s.mod == VM_TRILL) f *= ((t / 32) & 1) ? 1.20f : 1.0f;           // ~15 Hz warble
        else if (s.mod == VM_BURBLE) f *= 1.0f + 0.08f * sinf((float)t * 0.068f);
        else if (s.mod == VM_CHIRP) f *= 1.0f + 0.16f * sinf((float)t * 0.095f);
        const unsigned int fi = (unsigned int)f;
        if (fi != lastF) { tone(PETBOT_PIEZO_PIN, fi); lastF = fi; }
      }
      delay(5);
    }
  }
  noTone(PETBOT_PIEZO_PIN);
  return false;
}

void voiceTask(void*) {
  for (;;) {
    const uint8_t req = voiceRequest;
    if (!req) { delay(10); continue; }
    voiceRequest = 0;
    if (!soundEnabled) continue;
    const bool idle = (req & 0x80) != 0;
    buildVoice(req & 0x7F, idle);
    playVoiceSegs(idle);  // if interrupted, the newer request is picked up next loop
  }
}

void requestVoice(const String& m, bool idle) {
  if (!soundEnabled) return;
  const uint8_t code = voiceCodeForMood(m);
  // never let an idle murmur overwrite a pending real reaction
  if (idle && voiceRequest && !(voiceRequest & 0x80)) return;
  voiceRequest = code | (idle ? 0x80 : 0);
}

void playRobotSound(const String& soundMood) {
  requestVoice(soundMood, false);
  lastMoodSoundAt = millis();
}

void scheduleNextIdleSound() {
  // A living companion should make occasional expressive beeps while awake, but remain quiet enough
  // not to become an irritating alarm. Sleep has a slower, softer vocal rhythm.
  if (mood == "SLEEPY") {
    // Dream chirps are deliberately sparse.
    nextIdleSoundAt = millis() + 25000UL + (esp_random() % 30000UL);
  } else {
    // Leave quiet gaps between spontaneous sounds; event reactions remain immediate.
    nextIdleSoundAt = millis() + 9000UL + (esp_random() % 13000UL);
  }
}

void playIdleSound() {
  // Short murmurs; the voice engine picks a phrase that fits the current mood.
  requestVoice(mood, true);
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

  // Sleep is not silence: occasional low, gentle dream-like beeps make PETBOT
  // feel alive without keeping the room noisy.
  if (now >= nextIdleSoundAt && now - lastSoundEvent > 1800UL) {
    playIdleSound();
    lastMoodSoundAt = now;
    scheduleNextIdleSound();
  }
}

void sampleMicrophone() {
  if (!micEnabled) return; // prevent floating GPIO34 from causing false interactions
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
    registerPetInteraction(false);
    mood = "SURPRISED";
    moodOverrideUntil = now + 2200UL;
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    gazeTargetX = (esp_random() & 1) ? -4.0f : 4.0f;
    gazeTargetY = -1.0f;
    microAction = "HEARD_SOUND";
    microActionUntil = now + 650UL;
    drawFace();
    playRobotSound("SURPRISED");
    Serial.printf("PETBOT sound: adc=%d baseline=%d delta=%d envelope=%d\n",
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

  // The value must start right after the colon. Without this check
  // "command":null made the parser grab the NEXT key ("poll_after_ms")
  // and every idle poll looked like user activity.
  int valueAt = colon + 1;
  while (valueAt < (int)json.length() && json[valueAt] == ' ') valueAt++;
  if (valueAt >= (int)json.length() || json[valueAt] != '"') return "";

  int endQuote = json.indexOf('"', valueAt + 1);
  if (endQuote < 0) return "";

  return json.substring(valueAt + 1, endQuote);
}

void drawFace() {
  if (!displayReady) return;

  const unsigned long t = millis();
  if (mood != lastDrawnMood) {
    lastDrawnMood = mood;
    moodChangedAt = t;
  }
  // 1.0 right after an emotion starts, fading to 0 in half a second: every
  // new emotion "pops" instead of just swapping pictures.
  const float age = (float)(t - moodChangedAt);
  const float onset = age < 500.0f ? 1.0f - age / 500.0f : 0.0f;

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

  // ---- body language: bounce, tremble, sway, droop ----
  float bobY = 0.0f, swayX = 0.0f;
  if (mood == "EXCITED") bobY = -3.0f * fabsf(sinf(t * 0.011f));
  else if (mood == "HAPPY") bobY = -1.2f * sinf(t * 0.006f);
  else if (mood == "LOVE") bobY = -0.8f * sinf(t * 0.007f);
  else if (mood == "SAD") bobY = 1.0f + 0.8f * sinf(t * 0.0025f);
  else if (mood == "BORED") bobY = 0.8f * sinf(t * 0.002f);
  else if (mood == "SCARED") swayX = 1.6f * sinf(t * 0.11f) + 0.8f * sinf(t * 0.23f);
  else if (mood == "SHY") swayX = 2.0f * sinf(t * 0.0035f);

  const int leftX = 37 + (int)roundf(swayX);
  const int rightX = 91 + (int)roundf(swayX);
  const int eyeY = 34 + (int)roundf(bobY);
  const int eyeW = 29;
  const int eyeH = 35;
  const int glanceX = (int)roundf(gazeX);
  const int glanceY = (int)roundf(gazeY);
  const bool excitedAction = microAction == "EXCITED";
  const bool squint = microAction == "SQUINT";
  const bool happy = mood == "HAPPY" || excitedAction;
  const bool annoyed = mood == "ANNOYED" || squint;
  const bool alreadyClosedLook = mood == "HAPPY" || mood == "SHY" || mood == "SLEEPY" || excitedAction;
  const bool sleepy = !eyesOpen || mood == "SLEEPY";
  const bool lidsDown = sleepy || (blinking && !alreadyClosedLook);

  // Extra life: pupils drift slightly even while the robot is idle.
  const float lifeWaveX = 0.8f * sinf(t * 0.008f + 0.7f);
  const float lifeWaveY = 0.7f * cosf(t * 0.01f + 1.3f);
  const int pupilDriftX = (int)roundf(glanceX + lifeWaveX);
  const int pupilDriftY = (int)roundf(glanceY + lifeWaveY);
  const int eyeCx[2] = {leftX, rightX};

  if (lidsDown) {
    // Closed eyes: a breathing line while asleep, a quick shutter for a blink.
    const int bh = (mood == "SLEEPY") ? 8 + (int)roundf(1.5f * sinf(t * 0.003f)) : 7;
    for (int i = 0; i < 2; i++) display.fillRoundRect(eyeCx[i] - eyeW / 2, eyeY - bh / 2, eyeW, bh, 3, SSD1306_WHITE);
    if (mood == "SLEEPY") {
      const int phase = (int)((t / 650UL) % 4UL);  // z, then Z, drifting up
      if (phase >= 1) { display.setCursor(108, 21); display.print("z"); }
      if (phase >= 2) { display.setCursor(114, 12); display.print("Z"); }
    }
  } else if (mood == "BORED") {
    // Heavy lids, a slow side-to-side stare.
    const int gx = (int)roundf(6.0f * sinf(t * 0.0012f));
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      display.fillRoundRect(ex - eyeW / 2, eyeY - 14, eyeW, 28, 10, SSD1306_WHITE);
      display.fillCircle(ex + gx, eyeY + 5, 6, SSD1306_BLACK);
      display.fillRect(ex - eyeW / 2 - 1, eyeY - 16, eyeW + 2, 17, SSD1306_BLACK);
      display.drawLine(ex - 13, eyeY - 4, ex + 13, eyeY - 4, SSD1306_WHITE);
    }
  } else if (mood == "LOVE") {
    // Pixel hearts that beat; a tiny heart floats up between the eyes.
    const float pulse = 1.0f + 0.14f * sinf(t * 0.014f) + onset * 0.25f;
    const int r = (int)roundf(5.0f * pulse);
    for (int i = 0; i < 2; i++) {
      const int cx = eyeCx[i] + glanceX;
      const int cy = eyeY - 2 + glanceY;
      display.fillCircle(cx - r, cy - 2, r, SSD1306_WHITE);
      display.fillCircle(cx + r, cy - 2, r, SSD1306_WHITE);
      display.fillTriangle(cx - 2 * r, cy, cx + 2 * r, cy, cx, cy + (int)roundf(13.0f * pulse), SSD1306_WHITE);
    }
    const int hy = 45 - (int)((t % 2400UL) / 80UL);
    if (hy > 17) {
      display.fillCircle(62, hy, 2, SSD1306_WHITE);
      display.fillCircle(66, hy, 2, SSD1306_WHITE);
      display.fillTriangle(60, hy + 1, 68, hy + 1, 64, hy + 6, SSD1306_WHITE);
    }
  } else if (mood == "SAD") {
    // Drooping lids, pupils low, a tear that rolls down.
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      display.fillRoundRect(ex - 14, eyeY - 12, 28, 27, 10, SSD1306_WHITE);
      display.fillRect(ex - 15, eyeY - 13, 30, 9, SSD1306_BLACK);
      display.fillCircle(ex + pupilDriftX, eyeY + 4 + pupilDriftY, 5, SSD1306_BLACK);
    }
    display.drawLine(leftX - 13, eyeY - 18, leftX + 5, eyeY - 12, SSD1306_WHITE);
    display.drawLine(rightX - 5, eyeY - 12, rightX + 13, eyeY - 18, SSD1306_WHITE);
    const int tp = (int)(t % 2800UL);
    if (tp < 1800) {
      const int ty = eyeY + 11 + tp / 100;
      if (ty < 62) {
        display.fillCircle(leftX - 8, ty, 2, SSD1306_WHITE);
        display.drawPixel(leftX - 8, ty - 3, SSD1306_WHITE);
      }
    }
  } else if (mood == "SCARED") {
    // Trembling wide eyes, tiny darting pupils, a sweat drop.
    const int grow = (int)roundf(onset * 3.0f);
    const int dart = (int)((t / 220UL) % 3UL) - 1;
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      display.drawRoundRect(ex - 14 - grow, eyeY - 17 - grow, 28 + 2 * grow, 34 + 2 * grow, 10, SSD1306_WHITE);
      display.fillCircle(ex + dart * 3, eyeY + pupilDriftY, 3, SSD1306_WHITE);
    }
    display.drawLine(leftX - 13, eyeY - 21, leftX + 8, eyeY - 16, SSD1306_WHITE);
    display.drawLine(rightX - 8, eyeY - 16, rightX + 13, eyeY - 21, SSD1306_WHITE);
    const int sy = 17 + (int)((t / 180UL) % 5UL);
    display.fillCircle(rightX + 19, sy, 2, SSD1306_WHITE);
    display.drawPixel(rightX + 19, sy - 3, SSD1306_WHITE);
  } else if (mood == "SURPRISED") {
    // Eyes pop open wide, then settle; pupils start tiny and relax.
    const int grow = (int)roundf(onset * 4.0f);
    const int ps = 3 + (int)roundf((1.0f - onset) * 4.0f);
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      display.drawRoundRect(ex - 14 - grow, eyeY - 17 - grow, 28 + 2 * grow, 34 + 2 * grow, 10 + grow, SSD1306_WHITE);
      display.fillCircle(ex + pupilDriftX, eyeY + pupilDriftY, ps, SSD1306_WHITE);
      display.fillCircle(ex + pupilDriftX, eyeY + pupilDriftY, 2, SSD1306_BLACK);
      display.drawLine(ex - 8, eyeY - 21 - grow / 2, ex + 8, eyeY - 21 - grow / 2, SSD1306_WHITE);
    }
  } else if (mood == "CONFUSED") {
    // Mismatched eyes that look around, and a "?" that bobs.
    const int sw = (int)roundf(3.0f * sinf(t * 0.004f));
    display.fillRoundRect(leftX - 14, eyeY - 13, 28, 29, 10, SSD1306_WHITE);
    display.fillRoundRect(rightX - 14, eyeY - 18, 28, 34, 10, SSD1306_WHITE);
    display.fillCircle(leftX + pupilDriftX - 2 + sw, eyeY + 4 + pupilDriftY, 5, SSD1306_BLACK);
    display.fillCircle(rightX + pupilDriftX + 2 - sw, eyeY - 2 + pupilDriftY, 5, SSD1306_BLACK);
    display.drawLine(leftX - 12, eyeY - 20, leftX + 8, eyeY - 17, SSD1306_WHITE);
    display.drawLine(rightX - 8, eyeY - 22, rightX + 12, eyeY - 16, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(110, 14 + (int)roundf(2.0f * sinf(t * 0.006f)));
    display.print("?");
    display.setTextSize(1);
  } else if (mood == "SHY") {
    // Happy-closed eyes, looking away with a sway, blushing cheeks.
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      display.drawLine(ex - 12, eyeY + 3, ex, eyeY - 5, SSD1306_WHITE);
      display.drawLine(ex, eyeY - 5, ex + 12, eyeY + 3, SSD1306_WHITE);
      display.drawLine(ex - 12, eyeY + 4, ex, eyeY - 4, SSD1306_WHITE);
      display.drawLine(ex, eyeY - 4, ex + 12, eyeY + 4, SSD1306_WHITE);
      for (int k = 0; k < 3; k++) {
        display.drawLine(ex - 9 + k * 6, eyeY + 17, ex - 6 + k * 6, eyeY + 13, SSD1306_WHITE);
      }
    }
  } else if (mood == "EXCITED") {
    // Huge shining eyes that bounce, with flickering sparkles.
    const int r = 13 + (int)roundf(onset * 3.0f);
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      const int px = ex + (int)roundf(3.0f * sinf(t * 0.02f + i));
      const int py = eyeY + (int)roundf(2.0f * cosf(t * 0.017f));
      display.fillCircle(ex, eyeY, r, SSD1306_WHITE);
      display.fillCircle(px, py, 8, SSD1306_BLACK);
      display.fillCircle(px - 3, py - 3, 3, SSD1306_WHITE);
      display.fillCircle(px + 3, py + 3, 1, SSD1306_WHITE);
    }
    const int sp = (int)((t / 150UL) % 4UL);
    const int sx[3] = {22, 106, 64};
    const int sy2[3] = {18, 20, 15};
    for (int k = 0; k < 3; k++) {
      if (k == sp || k == (sp + 1) % 4) {
        display.drawLine(sx[k] - 3, sy2[k], sx[k] + 3, sy2[k], SSD1306_WHITE);
        display.drawLine(sx[k], sy2[k] - 3, sx[k], sy2[k] + 3, SSD1306_WHITE);
      }
    }
  } else if (happy) {
    // Happy crescent eyes with a lifted outer corner.
    for (int i = 0; i < 2; i++) {
      const int ex = eyeCx[i];
      display.drawLine(ex - 13, eyeY + 3, ex - 7, eyeY - 5, SSD1306_WHITE);
      display.drawLine(ex - 7, eyeY - 5, ex, eyeY - 8, SSD1306_WHITE);
      display.drawLine(ex, eyeY - 8, ex + 7, eyeY - 5, SSD1306_WHITE);
      display.drawLine(ex + 7, eyeY - 5, ex + 13, eyeY + 3, SSD1306_WHITE);
    }
  } else {
    // CURIOUS / ANNOYED / neutral. Curious eyes rock like a tilting head.
    const int tilt = (mood == "CURIOUS") ? (int)roundf(2.5f * sinf(t * 0.0021f)) : 0;
    int top = eyeY - eyeH / 2;
    int height = eyeH;
    if (annoyed) { top += 5; height -= 7; }
    display.fillRoundRect(leftX - eyeW / 2, top + tilt, eyeW, height, 11, SSD1306_WHITE);
    display.fillRoundRect(rightX - eyeW / 2, top - tilt, eyeW, height, 11, SSD1306_WHITE);

    int pupilY = eyeY + pupilDriftY;
    if (mood == "CURIOUS") pupilY -= 4;
    if (annoyed) pupilY += 4;
    const int pupilLX = leftX + 2 + pupilDriftX;
    const int pupilRX = rightX + 2 + pupilDriftX;
    display.fillCircle(pupilLX, pupilY + tilt, 6, SSD1306_BLACK);
    display.fillCircle(pupilRX, pupilY - tilt, 6, SSD1306_BLACK);
    display.fillCircle(pupilLX - 2, pupilY + tilt - 2, 2, SSD1306_WHITE);
    display.fillCircle(pupilRX - 2, pupilY - tilt - 2, 2, SSD1306_WHITE);

    if (annoyed) {
      display.drawLine(leftX - 13, eyeY - 17, leftX + 7, eyeY - 12, SSD1306_WHITE);
      display.drawLine(rightX - 7, eyeY - 12, rightX + 13, eyeY - 17, SSD1306_WHITE);
    }
  }

  // Mouth: every emotion has its own.
  const int mouthY = 56;
  if (lidsDown) {
    if (mood == "SLEEPY") display.drawCircle(64, mouthY, 1, SSD1306_WHITE);
  } else if (mood == "EXCITED") {
    display.fillCircle(64, mouthY - 2, 5, SSD1306_WHITE);
    display.fillRect(58, mouthY - 8, 13, 6, SSD1306_BLACK);
  } else if (happy || mood == "LOVE" || mood == "SHY") {
    display.drawLine(59, mouthY - 1, 64, mouthY + 2, SSD1306_WHITE);
    display.drawLine(64, mouthY + 2, 69, mouthY - 1, SSD1306_WHITE);
  } else if (mood == "SCARED") {
    display.drawLine(56, mouthY, 60, mouthY - 3, SSD1306_WHITE);
    display.drawLine(60, mouthY - 3, 64, mouthY + 1, SSD1306_WHITE);
    display.drawLine(64, mouthY + 1, 68, mouthY - 3, SSD1306_WHITE);
    display.drawLine(68, mouthY - 3, 72, mouthY, SSD1306_WHITE);
  } else if (mood == "SURPRISED") {
    display.drawCircle(64, mouthY, 3, SSD1306_WHITE);
  } else if (annoyed) {
    display.drawLine(60, mouthY, 68, mouthY - 2, SSD1306_WHITE);
  } else if (mood == "CONFUSED") {
    display.drawLine(60, mouthY, 64, mouthY - 2, SSD1306_WHITE);
    display.drawLine(64, mouthY - 2, 68, mouthY + 1, SSD1306_WHITE);
  } else if (mood == "SAD") {
    display.drawLine(58, mouthY + 2, 64, mouthY - 2, SSD1306_WHITE);
    display.drawLine(64, mouthY - 2, 70, mouthY + 2, SSD1306_WHITE);
  } else if (mood == "BORED") {
    display.drawLine(58, mouthY + 1, 70, mouthY + 1, SSD1306_WHITE);
  } else if (mood == "CURIOUS") {
    display.drawCircle(64, mouthY, 2, SSD1306_WHITE);
  } else {
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

String buildHeartbeatBody() {
  String body =
      "{\"firmware\":\"" + String(FIRMWARE_VERSION) +
      "\",\"wifi_rssi\":" + String(WiFi.RSSI()) +
      ",\"free_heap\":" + String(ESP.getFreeHeap()) +
      ",\"ip\":\"" + WiFi.localIP().toString() +
      "\",\"mood\":\"" + mood +
      "\",\"drives\":{\"curiosity\":" + String(petCuriosity) +
      ",\"energy\":" + String(petEnergy) +
      ",\"mood_value\":" + String(petMoodValue) +
      ",\"attachment\":" + String(petAttachment) +
      ",\"stimulation\":" + String(petStimulation) +
      ",\"social_need\":" + String(petSocialNeed) +
      ",\"confidence\":" + String(petConfidence) +
      "},\"autonomy\":true,\"battery_mv\":null,"
      "\"uv_sensor_connected\":false,"
      "\"camera_connected\":false,"
      "\"motors_enabled\":false}";

  return body;
}

// ---------------------------------------------------------------------------
// Cloud I/O lives in its own task. An HTTPS request takes hundreds of
// milliseconds; run from loop() it froze the face every few seconds. Now the
// task only fetches / posts, and loop() consumes the result, so the animation
// never stalls. After any cloud event it polls every 1.5 s for a minute so a
// conversation feels immediate; otherwise every 5 s to stay inside free-tier
// limits.
// ---------------------------------------------------------------------------
SemaphoreHandle_t cloudMutex = nullptr;
String cloudInbox;                  // latest /poll response (written by cloudTask)
volatile bool cloudInboxReady = false;
String cloudOutbox;                 // heartbeat body (written by loop)
volatile bool cloudOutboxReady = false;
volatile unsigned long cloudFastPollUntil = 0;

void sendHeartbeat() {
  if (!cloudMutex) return;
  String body = buildHeartbeatBody();
  xSemaphoreTake(cloudMutex, portMAX_DELAY);
  cloudOutbox = body;
  cloudOutboxReady = true;
  xSemaphoreGive(cloudMutex);
}

void cloudTask(void*) {
  unsigned long lastPollAt = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) { delay(500); continue; }

    if (cloudOutboxReady) {
      String body;
      xSemaphoreTake(cloudMutex, portMAX_DELAY);
      body = cloudOutbox;
      cloudOutboxReady = false;
      xSemaphoreGive(cloudMutex);
      if (cloudRequest("/api/device/heartbeat", "POST", body, nullptr)) {
        Serial.println("PETBOT heartbeat OK");
      }
    }

    const unsigned long now = millis();
    const unsigned long every = ((long)(now - cloudFastPollUntil) < 0) ? 1500UL : 5000UL;
    if (!cloudInboxReady && now - lastPollAt >= every) {
      lastPollAt = now;
      String response;
      if (cloudRequest("/api/device/poll", "GET", "", &response)) {
        xSemaphoreTake(cloudMutex, portMAX_DELAY);
        cloudInbox = response;
        cloudInboxReady = true;
        xSemaphoreGive(cloudMutex);
      }
    }
    delay(40);
  }
}

// A chat reply from the cloud brain: the face is the robot's own emotion, the
// impulse tells the local drive model how the conversation affected it.
void applyChatEvent(String face, String impulse, bool faceAlreadyApplied) {
  face.toUpperCase();
  impulse.toUpperCase();

  if (impulse == "AFFECTION") {
    registerPetInteraction(true);
  } else if (impulse == "PRAISE") {
    registerPetInteraction(false);
    petConfidence = min(100, petConfidence + 6);
    petMoodValue = min(100, petMoodValue + 4);
  } else if (impulse == "SCOLD") {
    lastActivity = millis();
    petConfidence = max(0, petConfidence - 6);
    petMoodValue = max(0, petMoodValue - 8);
    petStimulation = min(100, petStimulation + 10);
    petSocialNeed = max(0, petSocialNeed - 6);
  } else {
    registerPetInteraction(false);  // plain conversation is still social contact
  }

  const bool validFace =
      face == "HAPPY" || face == "CURIOUS" || face == "SLEEPY" ||
      face == "SURPRISED" || face == "ANNOYED" || face == "LOVE" ||
      face == "SAD" || face == "EXCITED" || face == "SHY" ||
      face == "CONFUSED" || face == "BORED" || face == "SCARED";
  if (validFace) {
    mood = face;
    lastCloudMood = face;
    if (!faceAlreadyApplied) playRobotSound(mood);
    moodOverrideUntil = millis() + 12000UL;
    lastActivity = millis();
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    drawFace();
  }
  if (millis() - lastNeedsSave > 60000UL) savePetNeeds();
  Serial.println("Chat event: " + face + " / " + impulse);
}

void handlePollResponse(const String& response) {

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
      receivedMood == "SAD" || receivedMood == "EXCITED" ||
      receivedMood == "SHY" || receivedMood == "CONFUSED" ||
      receivedMood == "BORED" || receivedMood == "SCARED";

  bool moodAppliedNow = false;
  if (validMood && receivedMood != lastCloudMood) {
    moodAppliedNow = true;
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
  if (command.length() || moodAppliedNow) cloudFastPollUntil = millis() + 60000UL;

  if (command == "CHAT_EVENT") {
    applyChatEvent(jsonStringField(response, "face"),
                   jsonStringField(response, "impulse"), moodAppliedNow);
    return;
  }

  // Also accept direct expression commands from the admin/chat backend.
  if (command == "HAPPY" || command == "CURIOUS" ||
      command == "SLEEPY" || command == "SURPRISED" ||
      command == "ANNOYED" || command == "LOVE" ||
      command == "SAD" || command == "EXCITED" ||
      command == "SHY" || command == "CONFUSED" ||
      command == "BORED" || command == "SCARED") {
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
      playRobotSound("SLEEPY");
    } else if (command == "WAKE" || command == "PING") {
      mood = "CURIOUS";
      playRobotSound("CURIOUS");
    } else if (command == "LOOK_AROUND") {
      mood = "SURPRISED";
      playRobotSound("SURPRISED");
    } else if (command == "PET" || command == "LOVE") {
      registerPetInteraction(true);
      mood = "LOVE";
      moodOverrideUntil = millis() + 9000UL;
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
      playRobotSound("HAPPY");
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
  Serial.println("TOUCH:ON / TOUCH:OFF / TOUCH:READ - optional touch wire on GPIO4");
  Serial.println("VOICE:HAPPY|CURIOUS|SURPRISED|LOVE|ANNOYED|SLEEPY|SAD / PURR - test voice phrases");
  Serial.println("MIC:ON / MIC:OFF - enable or disable analog sound input");
  Serial.println("MIC:THRESHOLD=240 - set sound sensitivity (50..1500)");
  Serial.println("MIC:CAL - sample quiet-room baseline for 3 seconds");
  Serial.println("NEEDS - show all internal state dimensions");
  Serial.println("EVENT:SOUND / EVENT:SUCCESS / EVENT:FAIL - simulate sensor/outcome");
  Serial.println("WAKE / SLEEP - test sleep and wake behavior");
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
    Serial.println(String("Mic enabled: ") + (micEnabled ? "YES" : "NO"));
    Serial.println(String("Mic threshold: ") + micThreshold);
    Serial.println(String("Interactions: ") + interactionCount);
    Serial.println(String("Needs C/E/M/A: ") + petCuriosity + "/" + petEnergy + "/" + petMoodValue + "/" + petAttachment);
    Serial.println(String("Vector model S/S/C: ") + petStimulation + "/" + petSocialNeed + "/" + petConfidence);
    Serial.println(String("Free heap: ") + ESP.getFreeHeap());
  } else if (line == "HELP") {
    printPetbotHelp();
  } else if (line == "NEEDS") {
    Serial.printf("PETBOT state: curiosity=%d energy=%d mood=%d attachment=%d stimulation=%d social_need=%d confidence=%d interactions=%lu\n", petCuriosity, petEnergy, petMoodValue, petAttachment, petStimulation, petSocialNeed, petConfidence, interactionCount);
  } else if (line == "EVENT:SOUND") {
    petStimulation = min(100, petStimulation + 28);
    petConfidence = max(0, petConfidence - 1);
    lastActivity = millis();
    mood = "SURPRISED";
    moodOverrideUntil = millis() + 2200UL;
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    gazeTargetX = (esp_random() & 1) ? -5.0f : 5.0f;
    gazeTargetY = -1.0f;
    microAction = "WIDE_EYES";
    microActionUntil = millis() + 650UL;
    playRobotSound("SURPRISED");
    drawFace();
    Serial.println("Test event: sudden sound");
  } else if (line == "EVENT:SUCCESS") {
    petConfidence = min(100, petConfidence + 8);
    petMoodValue = min(100, petMoodValue + 4);
    petStimulation = max(0, petStimulation - 5);
    if (millis() - lastNeedsSave > 60000UL) savePetNeeds();
    Serial.println("Test outcome: confidence increased.");
  } else if (line == "EVENT:FAIL") {
    petConfidence = max(0, petConfidence - 8);
    petMoodValue = max(0, petMoodValue - 4);
    petStimulation = min(100, petStimulation + 10);
    if (millis() - lastNeedsSave > 60000UL) savePetNeeds();
    Serial.println("Test outcome: confidence decreased.");
  } else if (line == "WAKE") {
    registerPetInteraction(false);
    mood = "SURPRISED";
    moodOverrideUntil = millis() + 2500UL;
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    eyesOpen = true;
    playRobotSound("SURPRISED");
    drawFace();
    Serial.println("PETBOT woke up.");
  } else if (line == "SLEEP") {
    mood = "SLEEPY";
    moodOverrideUntil = millis() + 5000UL;
    eyesOpen = false;
    playRobotSound("SLEEPY");
    drawFace();
    Serial.println("PETBOT entering sleep.");
  } else if (line == "PET") {
    registerPetInteraction(true);
    mood = "LOVE";
    moodOverrideUntil = millis() + 9000UL;
    nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
    playRobotSound("LOVE");
    drawFace();
    Serial.println("PETBOT: happy to see you!");
  } else if (line == "PLAY") {
    playRobotSound("HAPPY");
  } else if (line.startsWith("VOICE:")) {
    String m = line.substring(6);
    m.toUpperCase();
    requestVoice(m, false);
    Serial.println("Voice test: " + m);
  } else if (line == "TOUCH:ON") {
    touchEnabled = true;
    calibrateTouch();
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putBool("touch_en", touchEnabled);
    petbotPrefs.end();
    Serial.println(touchEnabled ? "Touch pad enabled." : "Touch pad NOT enabled.");
  } else if (line == "TOUCH:OFF") {
    touchEnabled = false;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putBool("touch_en", false);
    petbotPrefs.end();
    Serial.println("Touch pad disabled.");
  } else if (line == "TOUCH:READ") {
    Serial.println("Touch raw=" + String(touchRead(PETBOT_TOUCH_PIN)) + " baseline=" + String(touchBaseline));
  } else if (line == "PURR") {
    requestVoice("LOVE", false);
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
  } else if (line == "MIC:ON") {
    micEnabled = true;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putBool("mic_enabled", true);
    petbotPrefs.end();
    Serial.println("Mic input enabled. Confirm sensor output is safe for GPIO34 (0-3.3V).");
  } else if (line == "MIC:OFF") {
    micEnabled = false;
    petbotPrefs.begin("petbot", false);
    petbotPrefs.putBool("mic_enabled", false);
    petbotPrefs.end();
    Serial.println("Mic input disabled.");
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
    Serial.printf("Mic calibrated: baseline=%d noise-span=%d threshold=%d\n", micBaseline, noiseSpan, micThreshold);
  } else if (line.length()) {
    Serial.println("Unknown command. Type HELP.");
  }
}

// ---------------------------------------------------------------------------
// Physical interaction with no extra parts:
//  * BOOT button (GPIO0, active low): 1 click = pet, 2 clicks = play,
//    3+ clicks = poke (he gets annoyed), long press (1.5 s) = sleep / wake.
//  * Optional touch pad: a bare wire or foil on PETBOT_TOUCH_PIN (ESP32
//    capacitive touch, no module needed). Off by default; enable with TOUCH:ON.
// ---------------------------------------------------------------------------
volatile bool btnDown = false;
volatile bool btnReleased = false;
volatile uint32_t btnPressAt = 0;
volatile uint32_t btnReleaseAt = 0;
volatile uint32_t btnLastEdge = 0;
uint8_t btnClicks = 0;
uint32_t btnLastClickAt = 0;

void IRAM_ATTR onButtonEdge() {
  const uint32_t now = millis();
  if (now - btnLastEdge < 25) return;  // debounce
  btnLastEdge = now;
  if (digitalRead(PETBOT_BUTTON_PIN) == LOW) {
    btnDown = true;
    btnPressAt = now;
  } else if (btnDown) {
    btnDown = false;
    btnReleaseAt = now;
    btnReleased = true;
  }
}

static void ownerReact(const String& newMood, unsigned long holdMs) {
  mood = newMood;
  lastCloudMood = "";  // so the same cloud mood can be applied again later
  moodOverrideUntil = millis() + holdMs;
  nextAutonomousMoodAt = moodOverrideUntil + 1000UL;
  eyesOpen = true;
  playRobotSound(newMood);
  drawFace();
}

void ownerPet() {
  registerPetInteraction(true);
  ownerReact("LOVE", 9000UL);
  Serial.println("Owner: pet");
}

void ownerPlay() {
  registerPetInteraction(false);
  petStimulation = min(100, petStimulation + 8);
  ownerReact("HAPPY", 6000UL);
  Serial.println("Owner: play");
}

void ownerPoke() {
  lastActivity = millis();
  petMoodValue = max(0, petMoodValue - 6);
  petStimulation = min(100, petStimulation + 8);
  petSocialNeed = max(0, petSocialNeed - 4);
  ownerReact("ANNOYED", 6000UL);
  Serial.println("Owner: poke");
}

void ownerToggleSleep() {
  if (mood == "SLEEPY") {
    registerPetInteraction(false);
    ownerReact("SURPRISED", 2500UL);
    Serial.println("Owner: wake");
  } else {
    ownerReact("SLEEPY", 8000UL);
    eyesOpen = false;
    drawFace();
    Serial.println("Owner: sleep");
  }
}

void calibrateTouch() {
  long sum = 0;
  for (int i = 0; i < 25; i++) {
    sum += touchRead(PETBOT_TOUCH_PIN);
    delay(10);
  }
  touchBaseline = (int)(sum / 25);
  Serial.println("Touch baseline: " + String(touchBaseline) + " (don't touch the pad while calibrating)");
  if (touchBaseline < 15) {
    touchEnabled = false;
    Serial.println("Touch disabled: baseline too low. Check wiring / pin number.");
  }
}

void updatePhysicalInput() {
  const uint32_t now = millis();

  if (btnReleased) {
    btnReleased = false;
    const uint32_t held = btnReleaseAt - btnPressAt;
    if (held >= 1500UL) {
      btnClicks = 0;
      ownerToggleSleep();
    } else {
      btnClicks++;
      btnLastClickAt = btnReleaseAt;
    }
  }
  if (btnClicks && now - btnLastClickAt > 350UL) {
    const uint8_t n = btnClicks;
    btnClicks = 0;
    if (n == 1) ownerPet();
    else if (n == 2) ownerPlay();
    else ownerPoke();
  }

  if (!touchEnabled) return;
  static uint32_t lastTouchRead = 0, touchStartAt = 0, touchLastSeenAt = 0, touchLastPetAt = 0, touchLastPurrAt = 0;
  static uint8_t touchHits = 0;
  static bool touchHeld = false;
  if (now - lastTouchRead < 40UL) return;
  lastTouchRead = now;
  const int v = touchRead(PETBOT_TOUCH_PIN);
  const bool touched = touchBaseline > 0 && v < touchBaseline * 65 / 100;  // classic ESP32: value drops on touch
  touchHits = touched ? (uint8_t)min(255, touchHits + 1) : 0;
  if (touchHits >= 3) {  // ~120 ms of steady contact
    touchLastSeenAt = now;
    if (!touchHeld) {
      touchHeld = true;
      touchStartAt = now;
      if (now - touchLastPetAt > 3000UL) {
        touchLastPetAt = now;
        ownerPet();
      }
    } else if (now - touchStartAt > 1500UL && now - touchLastPurrAt > 2500UL) {
      // being stroked: keep purring and slowly feel better
      touchLastPurrAt = now;
      lastActivity = now;
      moodOverrideUntil = now + 6000UL;
      petSocialNeed = max(0, petSocialNeed - 4);
      petMoodValue = min(100, petMoodValue + 2);
      requestVoice("LOVE", false);
    }
  } else if (touchHeld && now - touchLastSeenAt > 400UL) {
    touchHeld = false;
  }
}

static unsigned long blinkGapFor(const String& m) {
  if (m == "EXCITED") return 900UL + (esp_random() % 1400UL);
  if (m == "SCARED") return 3500UL + (esp_random() % 3000UL);
  if (m == "SLEEPY" || m == "BORED") return 2600UL + (esp_random() % 3200UL);
  if (m == "SURPRISED") return 2800UL + (esp_random() % 2500UL);
  return 1200UL + (esp_random() % 2200UL);
}

static unsigned long blinkLengthFor(const String& m) {
  if (m == "SLEEPY") return 380UL;
  if (m == "BORED") return 280UL;
  if (m == "SCARED") return 70UL;
  if (m == "EXCITED") return 90UL;
  return 130UL;
}

void setup() {
  Serial.begin(115200);
  pinMode(PETBOT_PIEZO_PIN, OUTPUT);
  xTaskCreatePinnedToCore(voiceTask, "petbot_voice", 4096, NULL, 1, NULL, 0);
  pinMode(PETBOT_MIC_PIN, INPUT);
  pinMode(PETBOT_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PETBOT_BUTTON_PIN), onButtonEdge, CHANGE);
  analogReadResolution(12);
  analogSetPinAttenuation(PETBOT_MIC_PIN, ADC_11db);

  petbotPrefs.begin("petbot", false);
  deviceToken = petbotPrefs.getString("device_token", "");
  soundEnabled = petbotPrefs.getBool("sound", true);
  micEnabled = petbotPrefs.getBool("mic_enabled", false);
  touchEnabled = petbotPrefs.getBool("touch_en", false);
  micThreshold = petbotPrefs.getInt("mic_threshold", PETBOT_MIC_THRESHOLD_DEFAULT);
  interactionCount = petbotPrefs.getUInt("interactions", 0);
  petCuriosity = constrain(petbotPrefs.getInt("curiosity", 58), 0, 100);
  petEnergy = constrain(petbotPrefs.getInt("energy", 88), 0, 100);
  petMoodValue = constrain(petbotPrefs.getInt("mood_value", 68), 0, 100);
  petAttachment = constrain(petbotPrefs.getInt("attachment", 12), 0, 100);
  petStimulation = constrain(petbotPrefs.getInt("stimulation", 24), 0, 100);
  petSocialNeed = constrain(petbotPrefs.getInt("social_need", 32), 0, 100);
  petConfidence = constrain(petbotPrefs.getInt("confidence", 52), 0, 100);
  petbotPrefs.end();
  if (touchEnabled) calibrateTouch();

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
  wifiManager.setConfigPortalTimeout(30);
  wifiManager.setConnectTimeout(8);
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

  if (connected) {
    Serial.println("Wi-Fi connected: " + WiFi.SSID());
    Serial.println("IP address: " + WiFi.localIP().toString());
  } else {
    // Do not hold the local personality hostage to network setup.
    Serial.println("Wi-Fi unavailable; starting PETBOT in offline autonomous mode.");
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_STA);
  }

  lastActivity = millis();
  lastNeedsUpdate = millis();
  wifiWasConnected = (WiFi.status() == WL_CONNECTED);

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
  // A compact Vector-inspired state policy: internal drives bias actions;
  // random variation is only used to avoid robotic repetition.
  const unsigned long idleFor = millis() - lastActivity;
  if (petEnergy < 14 || idleFor > 120000UL) return "SLEEPY";
  const uint32_t roll = esp_random() % 100;

  if (petConfidence < 18 && petStimulation >= 55) return roll < 60 ? "SCARED" : "ANNOYED";
  if (petStimulation >= 60 && petMoodValue >= 55) return roll < 60 ? "EXCITED" : "HAPPY";
  if (petStimulation >= 72) return roll < 72 ? "SURPRISED" : "CURIOUS";
  if (petAttachment < 25 && idleFor < 20000UL && roll < 30) return "SHY";
  if (petStimulation < 30 && idleFor > 30000UL && idleFor <= 120000UL) return roll < 55 ? "BORED" : "CURIOUS";
  if (petSocialNeed >= 78 && idleFor > 45000UL) {
    if (petAttachment >= 35 && petMoodValue >= 35) return roll < 62 ? "LOVE" : "SAD";
    return roll < 72 ? "SAD" : "CURIOUS";
  }
  if (petEnergy < 28) return "SLEEPY";
  if (petConfidence < 24 && petStimulation >= 35) return roll < 60 ? "ANNOYED" : "CURIOUS";
  if (petMoodValue < 30 && idleFor > 60000UL) return roll < 65 ? "SAD" : "CURIOUS";
  if (petCuriosity >= 68) return roll < 58 ? "CURIOUS" : roll < 82 ? "SURPRISED" : "HAPPY";
  if (petMoodValue >= 72 && petSocialNeed < 55) return roll < 55 ? "HAPPY" : "CURIOUS";

  String next = roll < 44 ? "CURIOUS" : roll < 72 ? "HAPPY" : roll < 90 ? "SURPRISED" : "CONFUSED";
  if (next == current) next = current == "CURIOUS" ? "HAPPY" : "CURIOUS";
  return next;
}

void updateMicroAction() {
  const unsigned long now = millis();

  // Ease the eyes toward their target instead of jumping between positions.
  const float easing = 0.22f;
  float oldX = gazeX;
  float oldY = gazeY;
  float idleDriftX = 0.0f;
  float idleDriftY = 0.0f;

  const unsigned long idleFor = now - lastActivity;
  if (now >= moodOverrideUntil && mood != "SLEEPY" && petEnergy >= 18) {
    idleDriftX = 1.5f * sinf((float)now * 0.0016f + petCuriosity * 0.06f);
    idleDriftY = 1.0f * cosf((float)now * 0.0013f + petMoodValue * 0.04f);
    if (idleFor > 45000UL) {
      idleDriftX *= 1.3f;
      idleDriftY *= 1.4f;
    }
  }

  gazeX += (gazeTargetX + idleDriftX - gazeX) * easing;
  gazeY += (gazeTargetY + idleDriftY - gazeY) * easing;
  if (fabsf(gazeTargetX + idleDriftX - gazeX) < 0.18f) gazeX = gazeTargetX + idleDriftX;
  if (fabsf(gazeTargetY + idleDriftY - gazeY) < 0.18f) gazeY = gazeTargetY + idleDriftY;
  bool gazeMoved = (fabsf(gazeX - oldX) > 0.15f || fabsf(gazeY - oldY) > 0.15f);

  if (now < moodOverrideUntil || mood == "SLEEPY" || petEnergy < 18) {
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
      // Small autonomous scenes: glances, excited eyes, surprise and a grumpy squint.
      const uint32_t action = esp_random() % 12;
      if (action == 0) {
        microAction = "LOOK_LEFT"; gazeTargetX = -7.0f; gazeTargetY = 0.0f;
      } else if (action == 1) {
        microAction = "LOOK_RIGHT"; gazeTargetX = 7.0f; gazeTargetY = 0.0f;
      } else if (action == 2) {
        microAction = "LOOK_UP"; gazeTargetX = 0.0f; gazeTargetY = -5.0f;
      } else if (action == 3) {
        microAction = "LOOK_DIAGONAL"; gazeTargetX = 5.0f; gazeTargetY = -3.0f;
      } else if (action == 4) {
        microAction = "EXCITED"; gazeTargetX = 0.0f; gazeTargetY = -2.0f;
      } else if (action == 5) {
        microAction = "WIDE_EYES";
        gazeTargetX = (esp_random() & 1) ? -3.0f : 3.0f; gazeTargetY = 0.0f;
      } else if (action == 6) {
        microAction = "SQUINT"; gazeTargetX = 0.0f; gazeTargetY = 1.0f;
      } else if (action == 7) {
        microAction = "LOOK_AROUND";
        gazeTargetX = (esp_random() & 1) ? -7.0f : 7.0f; gazeTargetY = -2.0f;
      } else if (action == 8) {
        microAction = "FOLLOW"; gazeTargetX = (esp_random() & 1) ? -4.5f : 4.5f; gazeTargetY = (esp_random() & 1) ? -2.0f : 2.0f;
      } else if (action == 9) {
        microAction = "NOD"; gazeTargetX = 0.0f; gazeTargetY = -1.0f;
      } else {
        microAction = "GLANCE"; gazeTargetX = (esp_random() & 1) ? -6.0f : 6.0f; gazeTargetY = 1.5f;
      }
      microActionUntil = now + 750UL + (esp_random() % 950UL);
      drawFace();

      // Action-specific chirps make the sound feel tied to what the face is doing.
      if (soundEnabled && (esp_random() % 100) < 38 && now - lastMoodSoundAt > 6500UL) {
        // Routed through the voice task (low priority) so it never collides with other sounds.
        const char* voiceMood = "CURIOUS";
        if (microAction == "EXCITED") voiceMood = "HAPPY";
        else if (microAction == "WIDE_EYES") voiceMood = "SURPRISED";
        else if (microAction == "SQUINT") voiceMood = "ANNOYED";
        else if (microAction == "FOLLOW" || microAction == "GLANCE") voiceMood = "CURIOUS";
        else if (microAction == "NOD") voiceMood = "LOVE";
        requestVoice(voiceMood, true);
        lastMoodSoundAt = millis();
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

    // Offline mode must remain fully autonomous: Wi-Fi is only for cloud features.
    // Do not force sleep merely because the network is unavailable.
    if (now >= moodOverrideUntil && now >= nextAutonomousMoodAt) {
      String next = chooseAutonomousMood(mood);

      if (idleFor > 120000UL) {
        next = "SLEEPY";
      } else if (idleFor > 60000UL) {
        next = petMoodValue > 30 ? "BORED" : "SLEEPY";
      } else if (idleFor < 15000UL && next == "SLEEPY") {
        next = "CURIOUS";
      }

      if (next != mood) {
        mood = next;
        drawFace();
        Serial.println("Autonomous mood (offline): " + mood);
        if (mood != "SLEEPY" && now - lastMoodSoundAt > 7000UL &&
            (esp_random() % 100) < 35) {
          playRobotSound(mood);
          lastMoodSoundAt = now;
        }
      }
      nextAutonomousMoodAt = now + 12000UL + (esp_random() % 13000UL);
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
        // Long inactivity: settle into sleep and let energy recover.
        next = "SLEEPY";
      } else if (idleFor > 60000UL) {
        // A distinct bored phase between active exploration and sleep.
        next = petMoodValue > 30 ? "BORED" : "SLEEPY";
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
  static bool cloudTaskStarted = false;
  if (!cloudTaskStarted) {
    cloudTaskStarted = true;
    cloudMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(cloudTask, "petbot_cloud", 10240, NULL, 1, NULL, 0);
  }
  handleSerialProvisioning();
  if (notificationMessage.length() && (long)(millis() - notificationUntil) >= 0) {
    notificationMessage = "";
    drawFace();
  }
  updatePetNeeds();
  updateAutonomousMood();
  updateMicroAction();
  updatePhysicalInput();
  sampleMicrophone();
  updateSoundPersonality();

  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWifiRetry = 0;

    if (millis() - lastWifiRetry > 15000UL) {
      lastWifiRetry = millis();
      WiFi.reconnect();
    }
  } else {
    // Network I/O happens in cloudTask; here we only consume what it fetched.
    if (cloudInboxReady) {
      String response;
      xSemaphoreTake(cloudMutex, portMAX_DELAY);
      response = cloudInbox;
      cloudInboxReady = false;
      xSemaphoreGive(cloudMutex);
      handlePollResponse(response);
    }

    if (millis() - lastHeartbeat >= 15000UL) {
      lastHeartbeat = millis();
      sendHeartbeat();
    }
  }

  // Non-blocking blink with a rhythm that depends on the emotion.
  const unsigned long nowB = millis();
  if (blinking) {
    if ((long)(nowB - blinkUntil) >= 0) blinking = false;
  } else {
    if (nextBlinkAt == 0) nextBlinkAt = nowB + 1200UL;
    if ((long)(nowB - nextBlinkAt) >= 0) {
      blinking = true;
      lastBlink = nowB;
      blinkUntil = nowB + blinkLengthFor(mood);
      nextBlinkAt = nowB + blinkGapFor(mood);
    }
  }

  // Closed eyes only belong to sleep; any other emotion wakes them up again.
  if (mood != "SLEEPY") eyesOpen = true;

  // ~15 fps: continuous animation (bounce, tremble, tears, sparkles).
  static unsigned long lastFrameAt = 0;
  if (nowB - lastFrameAt >= 66UL) {
    lastFrameAt = nowB;
    drawFace();
  }

  delay(8);
}
