/*
 MindPal v10.1 — Speech pool diagnostics, robust RTDB list parsing.
 Based on MindPal Animated v3. Original procedural face and chimes.
 SDA=21, SCL=22, WS2812 DIN=25, passive buzzer IO=26,
 TTP223 IO=27, tactile button GPIO33 -> GND (INPUT_PULLUP).

 LIBRARIES: Adafruit SSD1306, Adafruit GFX, Adafruit NeoPixel,
            SparkFun MAX3010x Pulse and Proximity Sensor Library.
            Wire & Preferences are supplied by Arduino-ESP32.

 Button: click = stats/timer; hold >= 2 sec = start/stop cycle.
 TEST_MODE true: focus 30 sec, short break 12 sec, long break 20 sec.
 At completion: fireworks -> sparkling happy face -> break -> focus.
 Lifetime focus seconds and completed focus rounds persisted using Preferences.
 Statistics for an unfinished session are committed when holding to stop.
 Losing power mid-focus loses only uncommitted elapsed seconds.
 NOTE: non-medical BPM estimate; PPG checks run independently of Pomodoro.
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <math.h>
#include <string.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "MindPal_Cloud.h"

// ============================================================
// DEKLARASI TIPE DI BAGIAN ATAS (Arduino .ino compatibility)
// Arduino IDE membuat prototype fungsi otomatis. Tipe yang
// dipakai sebagai parameter harus sudah dikenali sebelumnya.
// ============================================================
enum PomoState : uint8_t {
  POMO_STOPPED, POMO_FOCUS, POMO_SHORT_BREAK, POMO_LONG_BREAK,
  POMO_FIREWORKS, POMO_HAPPY
};

enum OverlayType : uint8_t {
  OVERLAY_NONE, OVERLAY_STATS, OVERLAY_TIMER
};

struct Note {
  uint16_t hz;
  uint16_t lengthMs;
};

enum FaceMood : uint8_t {
  FACE_CALM, FACE_LOOK_LEFT, FACE_LOOK_RIGHT, FACE_SLEEPY,
  FACE_HAPPY, FACE_SURPRISED, FACE_WINK,
  FACE_BLUSH, FACE_TONGUE, FACE_X_EYES, FACE_DREAMY, FACE_SMUG,
  FACE_TOUCH_CAT, FACE_TOUCH_WIGGLE,
  FACE_JOY_BURST, FACE_LAUGH, FACE_FURIOUS, FACE_POUT,
  FACE_SQUINT_GRIN, FACE_SHY_KISS, FACE_CRYING, FACE_LOVE, FACE_CONFUSED
};

enum SpeechMode : uint8_t { SPEECH_IDLE=0, SPEECH_FOCUS=1, SPEECH_BREAK=2 };
enum ModeNotice : uint8_t { NOTICE_NONE, NOTICE_SILENT, NOTICE_SOUND, NOTICE_GAME_ON, NOTICE_GAME_OFF, NOTICE_BREATH_OFF };
enum HoldTarget : uint8_t { HOLD_MAIN, HOLD_MODE, HOLD_COMBO };
struct SpeechConfig {
  bool textEnabled;
  bool voiceEnabled;
  uint32_t intervalMinMs;
  uint32_t intervalMaxMs;
};

// Alarm terjadwal disimpan terpisah dari jingle Pomodoro.
// Format hari mengikuti tm_wday: Minggu=0, Senin=1, ... Sabtu=6.
constexpr uint8_t MAX_SCHEDULED_ALARMS = 12;
struct ScheduledAlarm {
  char id[32] = {0};
  char time[6] = {0};
  char date[11] = {0};
  char label[37] = {0};
  char message[76] = {0};
  uint8_t daysMask = 0;
  uint8_t tone = 0;
  uint8_t snoozeMin = 5;
  bool enabled = false;
  bool once = false;
  time_t lastFiredMinute = 0;
};
ScheduledAlarm scheduledAlarms[MAX_SCHEDULED_ALARMS];
uint8_t scheduledAlarmCount = 0;
bool scheduledAlarmActive = false;
int8_t scheduledAlarmIndex = -1;
int8_t snoozeAlarmIndex = -1;
time_t snoozeUntilEpoch = 0;
uint32_t scheduledAlarmStartedAt = 0;
uint32_t scheduledAlarmLastTuneAt = 0;

// Prototype eksplisit untuk fungsi dengan parameter tipe custom.
void showOverlay(OverlayType kind);
void playTune(const Note *notes, size_t count);
void cancelSpeech();
void scheduleNextSpeech();
void refreshSpeechMode();
void updateCloud();
void applyCloudConfig(JsonVariantConst cfg);
void playSelectedAlarm(uint8_t which);
void parseScheduledAlarms(JsonVariantConst obj);
void updateScheduledAlarms();
void drawScheduledAlarmScreen();
void stopScheduledAlarm(bool snooze);
SpeechMode modeFromPomodoro();
FaceMood currentFaceMood(uint32_t now);
void copyMessages(JsonVariantConst array, uint8_t mode);
void playScheduledAlarmTone(uint8_t choice);
void updateButtons();
void drawBoxBreathing(uint32_t now);
void drawModeNotice(uint32_t now);
void drawHoldScreen(uint32_t now, uint32_t startedAt, uint32_t duration, const char* title, bool finished);
void toggleBoxBreathing(uint32_t now);
void drawFace(uint32_t now);
void drawStar(int16_t x, int16_t y, int16_t r);
void initCloud();
void startCloudWorker();
void cloudWorkerTask(void *);
void printSpeechDiagnostics();

// ================== KONFIGURASI ==================
constexpr uint8_t SDA_PIN = 21;
constexpr uint8_t SCL_PIN = 22;
constexpr uint8_t LED_PIN = 25;
constexpr uint8_t BUZZER_PIN = 26;
constexpr uint8_t TOUCH_PIN = 27;
constexpr uint8_t BUTTON_PIN = 33;
constexpr uint8_t MODE_BUTTON_PIN = 32; // tombol baru: GPIO32 ke GND

constexpr uint8_t LED_COUNT = 16; // UBAH sesuai jumlah LED fisik
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t OLED_W = 128;
constexpr uint8_t OLED_H = 64;

constexpr uint32_t OLED_FRAME_MS = 45;   // target ~22 fps, I2C 400 kHz
constexpr uint32_t RING_FRAME_MS = 65;
constexpr uint32_t DEBUG_INTERVAL_MS = 3000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t FINGER_ON_IR = 50000;
constexpr uint32_t FINGER_OFF_IR = 35000;
constexpr uint32_t FINGER_OFF_WAIT_MS = 280;
constexpr uint32_t TOUCH_DEBOUNCE_MS = 30;
constexpr uint32_t TOUCH_SPAM_WINDOW_MS = 4000;
constexpr uint8_t TOUCH_SPAM_COUNT = 5;
constexpr uint32_t TOUCH_REACTION_MS = 3000;
constexpr uint32_t TOUCH_REACTION_COOLDOWN_MS = 3500;

Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
MAX30105 heartSensor;

bool oledReady = false;
bool sensorReady = false;
bool touchActive = false;
bool touchRawLast = false;
uint32_t touchRawChangedAt = 0;
uint32_t touchDownAt = 0;
uint32_t touchPreviewUntil = 0;
uint32_t touchStreakStartedAt = 0;
uint8_t touchStreak = 0;
FaceMood touchSpecialMood = FACE_CALM;
uint32_t touchSpecialUntil = 0;
uint32_t touchSpecialCooldownUntil = 0;

// ================== HEART RATE ==================
uint32_t lastIR = 0;
bool fingerDetected = false;
uint32_t fingerBelowAt = 0;
uint32_t lastBeatAt = 0;
float instantBPM = 0;
int avgBPM = 0;
uint8_t rateValues[4] = {0};
uint8_t rateIndex = 0;
uint8_t rateCount = 0;
uint32_t lastPulseEffectAt = 0;

void resetBPM() {
  lastBeatAt = 0;
  instantBPM = 0;
  avgBPM = 0;
  rateIndex = 0;
  rateCount = 0;
  for (uint8_t i = 0; i < 4; ++i) rateValues[i] = 0;
}

void processIR(uint32_t ir) {
  lastIR = ir;
  uint32_t now = millis();

  if (!fingerDetected) {
    if (ir > FINGER_ON_IR) {
      fingerDetected = true;
      fingerBelowAt = 0;
      resetBPM();
      Serial.println("[MAX30102] Finger detected: measuring full screen");
    } else {
      return;
    }
  }

  // Histeresis mencegah mode berkedip karena noise sesaat.
  if (ir < FINGER_OFF_IR) {
    if (fingerBelowAt == 0) fingerBelowAt = now;
    if (now - fingerBelowAt >= FINGER_OFF_WAIT_MS) {
      fingerDetected = false;
      fingerBelowAt = 0;
      resetBPM();
      Serial.println("[MAX30102] Finger removed: back to face");
      return;
    }
  } else {
    fingerBelowAt = 0;
  }

  if (checkForBeat(ir)) {
    if (lastBeatAt != 0) {
      uint32_t interval = now - lastBeatAt;
      if (interval > 0) {
        float value = 60000.0f / (float)interval;
        if (value >= 40.0f && value <= 200.0f) {
          instantBPM = value;
          rateValues[rateIndex] = (uint8_t)(value + 0.5f);
          rateIndex = (rateIndex + 1) % 4;
          if (rateCount < 4) ++rateCount;

          uint16_t sum = 0;
          for (uint8_t i = 0; i < rateCount; ++i) sum += rateValues[i];
          avgBPM = sum / rateCount;
          lastPulseEffectAt = now;

          Serial.printf("[HEART] BPM %.1f | AVG %d | IR %lu\n",
                        instantBPM, avgBPM, (unsigned long)ir);
        }
      }
    }
    lastBeatAt = now;
  }

  if (lastBeatAt != 0 && now - lastBeatAt > 5000) {
    resetBPM();
  }
}

void updateHeartRate() {
  if (!sensorReady) return;
  heartSensor.check(); // Isi FIFO internal dari FIFO MAX30102
  while (heartSensor.available()) {
    processIR(heartSensor.getFIFOIR());
    heartSensor.nextSample();
  }
}


// ================== POMODORO STATE MACHINE ==================
// TEST: ubah TEST_MODE=false untuk 25/5/15 menit.
constexpr bool TEST_MODE = true;
constexpr uint32_t FOCUS_MS       = TEST_MODE ? 30000UL   : 25UL * 60UL * 1000UL;
constexpr uint32_t SHORT_BREAK_MS = TEST_MODE ? 12000UL   :  5UL * 60UL * 1000UL;
constexpr uint32_t LONG_BREAK_MS  = TEST_MODE ? 20000UL   : 15UL * 60UL * 1000UL;
constexpr uint32_t HOLD_MS = 2000UL;
constexpr uint32_t DUAL_HOLD_MS = 5000UL;
constexpr uint32_t BREATH_PHASE_MS = 4000UL;  // 4s inhale, 4s hold, 4s exhale, 4s hold
constexpr uint32_t OVERLAY_MS = 3200UL;
constexpr uint32_t SPEECH_MIN_INTERVAL_MS = 1000UL;
constexpr uint32_t SPEECH_MAX_INTERVAL_MS = 1800000UL;
constexpr uint32_t FIREWORK_MS = 2900UL;
constexpr uint32_t HAPPY_MS = 1900UL;

// Tidak ada background music: buzzer untuk notifikasi dan suara vokal robot.
// CONFIG: suatu hari nanti bisa diubah dari dashboard melalui API/backend.
// Saat ini bisa diubah via Serial Monitor (115200, newline), lalu disimpan NVS.
// Rentang pengucapan DEMO kira-kira setiap 10 detik (random per mode).
SpeechConfig speechConfig[3] = {
  {true, true,  8000UL, 13000UL}, // IDLE
  {true, true,  9000UL, 14000UL}, // FOCUS
  {true, true, 10000UL, 15000UL}  // BREAK
};
// No hard-coded message fallback. Every auto speech message comes from Firebase.
// At boot before the first sync, the speech pools are empty; this is intentional.
// Clearing a pool in the dashboard means silence for that mode.

// Cloud-managed content. Copied into fixed buffers, never dangling JSON pointers.
char cloudMessages[3][12][76] = {};
uint8_t cloudMessageCount[3] = {0,0,0};
bool useMoodMessagesForIdle = true; // configurable in dashboard; default: mood-specific
uint8_t currentMood = 2; // 0 Great, 1 Good, 2 Neutral, 3 Sad, 4 Awful
const char *moodNames[] = {"Great","Good","Neutral","Sad","Awful"};
uint32_t currentMoodColor = 0x38BDF8;
uint8_t ringEffect = 0; // 0 auto, 1 solid, 2 breathe, 3 rainbow, 4 sparkle, 5 off
uint32_t ringCustomColor = 0x40A8FF;
uint8_t alarmStart=0, alarmFinish=1, alarmBreak=2;
bool silentMode = false;
bool silentLocalOverride=false; // pressing hardware button wins until reboot
bool gameMode = false;
uint32_t gameCount = 0, gameBest = 0;
uint16_t gameTarget = 30;
uint32_t gameCelebrationUntil=0;
uint32_t gameFaceUntil=0;
uint32_t gameLastClickAt=0;
uint8_t gameReaction=0;
uint32_t lastGameSaveAt=0;
bool modeButtonStable=HIGH, modeButtonLast=HIGH, modeLongHandled=false;
uint32_t modeChangedAt=0, modePressedAt=0;
bool boxBreathingActive = false;
uint32_t boxBreathingStartedAt = 0;
bool comboGuard = false;              // blocks both independent button actions until both released
bool comboTriggered = false;
uint32_t comboStartedAt = 0;
ModeNotice modeNotice = NOTICE_NONE;
uint32_t modeNoticeUntil = 0;
HoldTarget holdLastTarget = HOLD_MAIN;
uint32_t holdCompletedUntil = 0;

SpeechMode activeSpeechMode = SPEECH_IDLE;
uint32_t nextSpeechAt = 0;
uint32_t speechStartedAt = 0;
uint32_t speechDurationMs = 0;
const char* speechMessage = nullptr;
bool speechVisible = false;
bool speechVoiceRunning = false;
bool speechNoteOn = false;
uint16_t speechCharIndex = 0;
uint32_t speechSoundNextAt = 0;

PomoState pomoState = POMO_STOPPED;
uint32_t segmentStartedAt = 0;
uint32_t celebrationStartedAt = 0;
uint8_t completedInBlock = 0; // 0..4, reset setelah break panjang

// Statistik tersimpan di NVS (Preferences) setelah fokus selesai/stop.
Preferences storage;
bool storageReady = false;
uint32_t savedFocusSeconds = 0;
uint32_t completedCycles = 0;


OverlayType overlayType = OVERLAY_NONE;
uint32_t overlayStartedAt = 0;

bool buttonReadingLast = HIGH;
bool buttonStable = HIGH;
uint32_t buttonChangedAt = 0;
uint32_t buttonPressedAt = 0;
bool longPressHandled = false;
uint32_t holdCompletedAt = 0;  // retained for button compatibility
bool holdCompletedStart = false;

bool pomoActive() { return pomoState != POMO_STOPPED; }
bool inFocus() { return pomoState == POMO_FOCUS; }
bool inBreak() { return pomoState == POMO_SHORT_BREAK || pomoState == POMO_LONG_BREAK; }

uint32_t phaseDurationMs() {
  if (pomoState == POMO_FOCUS) return FOCUS_MS;
  if (pomoState == POMO_SHORT_BREAK) return SHORT_BREAK_MS;
  if (pomoState == POMO_LONG_BREAK) return LONG_BREAK_MS;
  return 0;
}

uint32_t phaseRemainingSeconds(uint32_t now) {
  uint32_t duration = phaseDurationMs();
  if (!duration) return 0;
  uint32_t passed = now - segmentStartedAt;
  if (passed >= duration) return 0;
  return (duration - passed + 999UL) / 1000UL;
}

uint32_t totalFocusSeconds(uint32_t now) {
  uint32_t current = 0;
  if (inFocus()) {
    uint32_t elapsed = now - segmentStartedAt;
    if (elapsed > FOCUS_MS) elapsed = FOCUS_MS;
    current = elapsed / 1000UL;
  }
  return savedFocusSeconds + current;
}

void saveProgress() {
  if (!storageReady) return;
  storage.putUInt("focus_s", savedFocusSeconds);
  storage.putUInt("cycles", completedCycles);
}

void commitFocusTime(uint32_t now, bool complete) {
  uint32_t elapsed = now - segmentStartedAt;
  if (elapsed > FOCUS_MS) elapsed = FOCUS_MS;
  savedFocusSeconds += complete ? FOCUS_MS / 1000UL : elapsed / 1000UL;
  if (complete) ++completedCycles;
  saveProgress();
}

void showOverlay(OverlayType kind) {
  cancelSpeech();
  overlayType = kind;
  overlayStartedAt = millis();
}

// ================== NOTIFIKASI NADA TANPA DELAY ==================

// Chime orisinal: dung - dung - ding, lalu jingle perayaan.
const Note START_CHIME[] = {
  {392,170}, {0,85}, {392,170}, {0,85}, {784,390}
};
const Note FINISH_TUNE[] = {
  {523,160},{659,160},{784,200},{0,60},
  {659,150},{784,150},{988,260},{0,80},
  {1047,170},{988,150},{1047,450}
};
const Note BREAK_CHIME[] = {
  {659,150},{784,170},{988,310}
};
const Note STOP_CHIME[] = {
  {659,130},{523,140},{392,270}
};

const Note *currentTune = nullptr;
size_t tuneSize = 0;
size_t tunePosition = 0;
uint32_t tuneStepAt = 0;
bool tuneSilenced = true;

void stopTune() {
  noTone(BUZZER_PIN);
  currentTune = nullptr;
  tuneSize = 0;
}

void playTune(const Note *notes, size_t count) {
  // Box breathing is deliberately quiet; scheduled clock alarms may override it.
  if (boxBreathingActive && !scheduledAlarmActive) return;
  cancelSpeech();
  stopTune();
  currentTune = notes;
  tuneSize = count;
  tunePosition = 0;
  tuneStepAt = millis();
  tuneSilenced = notes[0].hz == 0;
  if (!tuneSilenced && !silentMode) tone(BUZZER_PIN, notes[0].hz);
}

void updateTune() {
  if (currentTune == nullptr) return;
  uint32_t now = millis();
  const Note &n = currentTune[tunePosition];
  uint32_t dt = now - tuneStepAt;
  if (!tuneSilenced && dt >= (uint32_t)n.lengthMs * 87UL / 100UL) {
    noTone(BUZZER_PIN);
    tuneSilenced = true;
  }
  if (dt < n.lengthMs) return;
  tunePosition++;
  if (tunePosition >= tuneSize) {
    stopTune();
    return;
  }
  tuneStepAt = now;
  const Note &next = currentTune[tunePosition];
  tuneSilenced = (next.hz == 0);
  if (tuneSilenced) noTone(BUZZER_PIN);
  else if (!silentMode) tone(BUZZER_PIN, next.hz);
}

// ================== PESAN DAN SUARA VOKAL ==================
// Nada setiap vokal: A tertinggi -> I -> E -> U -> O terendah.
// Ini suara sintetis lewat buzzer, bukan text-to-speech manusia.
uint16_t vowelTone(char c) {
  switch (c) {
    case 'a': case 'A': return 1047; // C6
    case 'i': case 'I': return 880;  // A5
    case 'e': case 'E': return 784;  // G5
    case 'u': case 'U': return 659;  // E5
    case 'o': case 'O': return 523;  // C5
    default: return 0;
  }
}
SpeechMode modeFromPomodoro() {
  if (pomoState == POMO_FOCUS) return SPEECH_FOCUS;
  if (inBreak()) return SPEECH_BREAK;
  return SPEECH_IDLE;
}
void cancelSpeech() {
  if (speechNoteOn && currentTune == nullptr) noTone(BUZZER_PIN);
  speechVisible = false;
  speechVoiceRunning = false;
  speechNoteOn = false;
  speechMessage = nullptr;
}
void scheduleNextSpeech() {
  const SpeechConfig &cfg = speechConfig[(uint8_t)activeSpeechMode];
  uint32_t lo = constrain(cfg.intervalMinMs, SPEECH_MIN_INTERVAL_MS, SPEECH_MAX_INTERVAL_MS);
  uint32_t hi = constrain(cfg.intervalMaxMs, lo, SPEECH_MAX_INTERVAL_MS);
  nextSpeechAt = millis() + (uint32_t)random((long)lo, (long)hi + 1L);
}
void refreshSpeechMode() {
  SpeechMode newMode = modeFromPomodoro();
  if (newMode != activeSpeechMode) {
    cancelSpeech();
    activeSpeechMode = newMode;
    scheduleNextSpeech();
  }
}
void beginSpeech() {
  const SpeechConfig &cfg = speechConfig[(uint8_t)activeSpeechMode];
  if (gameMode || (!cfg.textEnabled && !cfg.voiceEnabled)) {
    scheduleNextSpeech();
    return;
  }
  const uint8_t mode = (uint8_t)activeSpeechMode;
  const uint8_t count = cloudMessageCount[mode];
  if (count == 0) { // Empty Firebase list => absolutely no automatic speech.
    scheduleNextSpeech();
    return;
  }
  speechMessage = cloudMessages[mode][random(0L, (long)count)];
  speechStartedAt = millis();
  uint32_t charDuration = strlen(speechMessage) * 105UL;
  speechDurationMs = constrain(charDuration, 2000UL, 4200UL);
  speechVisible = cfg.textEnabled;
  speechVoiceRunning = cfg.voiceEnabled;
  speechNoteOn = false;
  speechCharIndex = 0;
  speechSoundNextAt = speechStartedAt;
  Serial.printf("[SPEECH] mode=%u text=%u voice=%u: %s\n",
    (unsigned)activeSpeechMode, cfg.textEnabled, cfg.voiceEnabled, speechMessage);
  scheduleNextSpeech();
}
void updateSpeechVoice() {
  if (!speechVoiceRunning || !speechMessage) return;
  if (currentTune != nullptr) { cancelSpeech(); return; }
  uint32_t now = millis();
  if ((int32_t)(now - speechSoundNextAt) < 0) return;
  if (speechNoteOn) {
    noTone(BUZZER_PIN);
    speechNoteOn = false;
    speechSoundNextAt = now + 58UL;
    return;
  }
  while (speechMessage[speechCharIndex]) {
    uint16_t hz = vowelTone(speechMessage[speechCharIndex++]);
    if (hz) {
      if (!silentMode) tone(BUZZER_PIN, hz);
      speechNoteOn = true;
      speechSoundNextAt = now + 125UL;
      return;
    }
  }
  speechVoiceRunning = false;
  noTone(BUZZER_PIN);
}
void updateSpeech() {
  uint32_t now = millis();
  if (boxBreathingActive) return;
  if (overlayType!=OVERLAY_NONE && now-overlayStartedAt>=OVERLAY_MS) overlayType=OVERLAY_NONE;
  refreshSpeechMode();
  if (speechVisible && now - speechStartedAt >= speechDurationMs) speechVisible = false;
  updateSpeechVoice();
  bool safeToSpeak = !boxBreathingActive && !gameMode && (pomoState == POMO_STOPPED || inFocus() || inBreak()) &&
    overlayType == OVERLAY_NONE && !fingerDetected && !touchActive &&
    (int32_t)(now - touchPreviewUntil) >= 0 &&
    (int32_t)(now - touchSpecialUntil) >= 0 &&
    buttonStable == HIGH && currentTune == nullptr && !speechVisible && !speechVoiceRunning;
  if (safeToSpeak && (int32_t)(now - nextSpeechAt) >= 0) beginSpeech();
}

// Konfigurasi per mode yang siap dihubungkan dengan dashboard di masa depan.
// Serial: VOICE IDLE OFF, VOICE FOCUS ON, TEXT BREAK OFF,
//         RANGE FOCUS 8000 14000, CONFIG
const char* speechModeName(uint8_t i) {
  return i == 0 ? "IDLE" : i == 1 ? "FOCUS" : "BREAK";
}
int parseSpeechMode(const char *s) {
  if (strcmp(s,"IDLE")==0) return 0;
  if (strcmp(s,"FOCUS")==0) return 1;
  if (strcmp(s,"BREAK")==0) return 2;
  return -1;
}
void saveSpeechConfig(uint8_t i) {
  if (!storageReady) return;
  char key[12];
  snprintf(key,sizeof(key),"txt_%u",i); storage.putBool(key,speechConfig[i].textEnabled);
  snprintf(key,sizeof(key),"vox_%u",i); storage.putBool(key,speechConfig[i].voiceEnabled);
  snprintf(key,sizeof(key),"min_%u",i); storage.putUInt(key,speechConfig[i].intervalMinMs);
  snprintf(key,sizeof(key),"max_%u",i); storage.putUInt(key,speechConfig[i].intervalMaxMs);
}
void loadSpeechConfig() {
  if (!storageReady) return;
  for (uint8_t i=0;i<3;++i) {
    char key[12];
    snprintf(key,sizeof(key),"txt_%u",i); speechConfig[i].textEnabled=storage.getBool(key,speechConfig[i].textEnabled);
    snprintf(key,sizeof(key),"vox_%u",i); speechConfig[i].voiceEnabled=storage.getBool(key,speechConfig[i].voiceEnabled);
    snprintf(key,sizeof(key),"min_%u",i); speechConfig[i].intervalMinMs=storage.getUInt(key,speechConfig[i].intervalMinMs);
    snprintf(key,sizeof(key),"max_%u",i); speechConfig[i].intervalMaxMs=storage.getUInt(key,speechConfig[i].intervalMaxMs);
    speechConfig[i].intervalMinMs = constrain(speechConfig[i].intervalMinMs,SPEECH_MIN_INTERVAL_MS,SPEECH_MAX_INTERVAL_MS);
    speechConfig[i].intervalMaxMs = constrain(speechConfig[i].intervalMaxMs,speechConfig[i].intervalMinMs,SPEECH_MAX_INTERVAL_MS);
  }
}
void printSpeechConfig() {
  for(uint8_t i=0;i<3;++i) {
    Serial.printf("[CONFIG] %s: TEXT=%s VOICE=%s RANGE=%lu..%lu ms\n",
      speechModeName(i), speechConfig[i].textEnabled?"ON":"OFF",
      speechConfig[i].voiceEnabled?"ON":"OFF",
      (unsigned long)speechConfig[i].intervalMinMs,
      (unsigned long)speechConfig[i].intervalMaxMs);
  }
}
// Run this from Serial Monitor with: SPEECHDEBUG
// Shows which list is active and why spontaneous text may be absent.
void printSpeechDiagnostics() {
  uint8_t m = (uint8_t)activeSpeechMode;
  const SpeechConfig &c = speechConfig[m];
  uint32_t now = millis();
  uint32_t waitMs = (int32_t)(nextSpeechAt-now)>0 ? nextSpeechAt-now : 0;
  Serial.printf("[SPEECH DEBUG] mode=%s | source=%s | mood=%s | count=%u | text=%u | voice=%u | interval=%lu..%lu sec | next=%lu sec\n",
    speechModeName(m), m==0?(useMoodMessagesForIdle?"MOOD":"GENERAL"):"MODE",
    moodNames[currentMood], (unsigned)cloudMessageCount[m], (unsigned)c.textEnabled,
    (unsigned)c.voiceEnabled, (unsigned long)(c.intervalMinMs/1000UL),
    (unsigned long)(c.intervalMaxMs/1000UL), (unsigned long)(waitMs/1000UL));
  Serial.printf("[SPEECH DEBUG] blocked: game=%u breathing=%u finger=%u touch=%u overlay=%u tune=%u visible=%u playing=%u\n",
    (unsigned)gameMode, (unsigned)boxBreathingActive, (unsigned)fingerDetected,
    (unsigned)touchActive,(unsigned)(overlayType!=OVERLAY_NONE),
    (unsigned)(currentTune!=nullptr), (unsigned)speechVisible,(unsigned)speechVoiceRunning);
  for (uint8_t i=0; i<cloudMessageCount[m]; ++i)
    Serial.printf("[SPEECH DEBUG] message[%u]=%s\n",(unsigned)i,cloudMessages[m][i]);
  if (cloudMessageCount[m]==0)
    Serial.println("[SPEECH WARN] Active message list is EMPTY. Open Words & Mood, select the correct Idle source/mood, add messages, click Save changes. No firmware fallback is used.");
  else if (!c.textEnabled)
    Serial.println("[SPEECH WARN] Show messages on OLED is OFF for active mode.");
}
void executeSerialCommand(char *cmd) {
  char verb[12]={0}, mode[12]={0}, value[12]={0};
  unsigned long lo=0,hi=0;
  if (strcmp(cmd,"CONFIG")==0) { printSpeechConfig(); return; }
  if (strcmp(cmd,"SPEECHDEBUG")==0) { printSpeechDiagnostics(); return; }
  if (strcmp(cmd,"SPEECHTEST")==0) {
    printSpeechDiagnostics();
    if (cloudMessageCount[(uint8_t)activeSpeechMode]>0) beginSpeech();
    return;
  }
  if (sscanf(cmd,"%11s %11s %11s",verb,mode,value)==3 &&
      (strcmp(verb,"TEXT")==0 || strcmp(verb,"VOICE")==0)) {
    int idx=parseSpeechMode(mode);
    if (idx < 0 || (strcmp(value,"ON") && strcmp(value,"OFF"))) {
      Serial.println("[CONFIG] Invalid mode/value"); return;
    }
    bool enabled = strcmp(value,"ON")==0;
    if (strcmp(verb,"TEXT")==0) speechConfig[idx].textEnabled=enabled;
    else speechConfig[idx].voiceEnabled=enabled;
    cancelSpeech(); saveSpeechConfig(idx); scheduleNextSpeech(); printSpeechConfig(); return;
  }
  if (sscanf(cmd,"%11s %11s %lu %lu",verb,mode,&lo,&hi)==4 && strcmp(verb,"RANGE")==0) {
    int idx=parseSpeechMode(mode);
    if (idx < 0 || lo<1000UL || hi<lo || hi>SPEECH_MAX_INTERVAL_MS) {
      Serial.println("[CONFIG] Range invalid; use 1000..1800000 ms"); return;
    }
    speechConfig[idx].intervalMinMs=lo;
    speechConfig[idx].intervalMaxMs=hi;
    saveSpeechConfig(idx); scheduleNextSpeech(); printSpeechConfig(); return;
  }
  Serial.println("[CONFIG] Commands: CONFIG | SPEECHDEBUG | SPEECHTEST | TEXT IDLE/FOCUS/BREAK ON/OFF | VOICE ... | RANGE IDLE/FOCUS/BREAK minMs maxMs");
}
void updateSerialCommands() {
  static char line[72]; static uint8_t used=0;
  uint8_t budget=16; // tidak memonopoli loop pembacaan PPG
  while (budget-- && Serial.available()>0) {
    char c=(char)Serial.read();
    if (c=='\r' || c=='\n') {
      if (used) { line[used]='\0'; executeSerialCommand(line); used=0; }
    } else if (used<sizeof(line)-1) line[used++]=c;
    else used=0;
  }
}

// ================== TRANSISI SESI ==================
void beginFocus(bool first) {
  cancelSpeech();
  uint32_t now = millis();
  pomoState = POMO_FOCUS;
  segmentStartedAt = now;
  showOverlay(OVERLAY_TIMER);
  if (first) {
    playSelectedAlarm(alarmStart);
  } else {
    playSelectedAlarm(alarmBreak);
  }
  Serial.printf("[POMO] START FOCUS | round %u/4 | %lu seconds\n",
                (unsigned)(completedInBlock + 1), (unsigned long)(FOCUS_MS/1000UL));
}

void stopPomodoro() {
  cancelSpeech();
  uint32_t now = millis();
  if (inFocus()) commitFocusTime(now, false); // Hitung waktu fokus parsial.
  pomoState = POMO_STOPPED;
  completedInBlock = 0;
  showOverlay(OVERLAY_STATS);
  playTune(STOP_CHIME, sizeof(STOP_CHIME)/sizeof(STOP_CHIME[0]));
  Serial.printf("[POMO] STOP | focus=%lus | cycles=%lu\n",
                (unsigned long)savedFocusSeconds, (unsigned long)completedCycles);
}

void startBreak() {
  cancelSpeech();
  uint32_t now = millis();
  pomoState = (completedInBlock >= 4) ? POMO_LONG_BREAK : POMO_SHORT_BREAK;
  segmentStartedAt = now;
  showOverlay(OVERLAY_TIMER);
  Serial.printf("[POMO] %s START | %lu seconds\n",
                pomoState == POMO_LONG_BREAK ? "LONG BREAK" : "SHORT BREAK",
                (unsigned long)(phaseDurationMs()/1000UL));
}

void finishFocus(uint32_t now) {
  cancelSpeech();
  commitFocusTime(now, true);
  ++completedInBlock;
  pomoState = POMO_FIREWORKS;
  celebrationStartedAt = now;
  overlayType = OVERLAY_NONE;
  playSelectedAlarm(alarmFinish);
  Serial.printf("[POMO] FOCUS COMPLETED! cycle=%lu | focus total=%lus | block=%u/4\n",
                (unsigned long)completedCycles,
                (unsigned long)savedFocusSeconds, (unsigned)completedInBlock);
}

void updatePomodoro() {
  uint32_t now = millis();
  if (pomoState == POMO_FOCUS && now - segmentStartedAt >= FOCUS_MS) {
    finishFocus(now);
  } else if (pomoState == POMO_FIREWORKS &&
             now - celebrationStartedAt >= FIREWORK_MS) {
    pomoState = POMO_HAPPY;
    celebrationStartedAt = now;
    Serial.println("[POMO] Sparkling happy face!");
  } else if (pomoState == POMO_HAPPY &&
             now - celebrationStartedAt >= HAPPY_MS) {
    startBreak();
  } else if (inBreak() && now - segmentStartedAt >= phaseDurationMs()) {
    Serial.println("[POMO] Break finished; starting next focus automatically.");
    if (pomoState == POMO_LONG_BREAK) completedInBlock = 0;
    beginFocus(false);
  }
}

// ================== INPUT: CLICK VS LONG HOLD ==================
void updateTouch() {
  uint32_t now=millis();
  bool reading=(digitalRead(TOUCH_PIN)==HIGH);
  if (reading != touchRawLast) { touchRawLast=reading; touchRawChangedAt=now; }
  if (reading==touchActive || now-touchRawChangedAt<TOUCH_DEBOUNCE_MS) return;
  touchActive=reading;
  cancelSpeech();
  if (touchActive && gameMode && !boxBreathingActive) {
    gameCount++;
    if (gameCount>gameBest) gameBest=gameCount;
    gameReaction=(uint8_t)random(0L,4L);
    gameFaceUntil=now+850UL;
    gameLastClickAt=now;
    // Fun Clicker selalu memakai ekspresi hangat/imut, bukan marah.
    const FaceMood gameFaces[4] = {FACE_JOY_BURST, FACE_LAUGH, FACE_TOUCH_CAT, FACE_SQUINT_GRIN};
    touchSpecialMood = gameFaces[gameReaction];
    touchSpecialUntil=gameFaceUntil;
    if (gameTarget && gameCount % gameTarget==0) {
      gameCelebrationUntil=now+2600UL;
      playTune(FINISH_TUNE,sizeof(FINISH_TUNE)/sizeof(FINISH_TUNE[0]));
      Serial.printf("[GAME] TARGET! count=%lu\n",(unsigned long)gameCount);
    }
    if (storageReady && (gameCount % 10UL==0UL || now-lastGameSaveAt>30000UL)) {
      storage.putUInt("game_cnt",gameCount);
      storage.putUInt("game_best",gameBest);
      lastGameSaveAt=now;
    }
    Serial.printf("[GAME] TAP count=%lu | best=%lu\n",(unsigned long)gameCount,(unsigned long)gameBest);
  }
  if (touchActive) {
    touchDownAt=now;
    touchPreviewUntil=now+650UL;
    Serial.println("[TOUCH] PRESS - petting mode");
    if (!gameMode && !boxBreathingActive && (int32_t)(now-touchSpecialCooldownUntil)>=0) {
      if (touchStreak==0 || now-touchStreakStartedAt>TOUCH_SPAM_WINDOW_MS) {
        touchStreakStartedAt=now; touchStreak=0;
      }
      ++touchStreak;
      Serial.printf("[TOUCH] Streak %u/%u\n",(unsigned)touchStreak,(unsigned)TOUCH_SPAM_COUNT);
      if (touchStreak>=TOUCH_SPAM_COUNT) {
        touchSpecialMood=(FaceMood)(FACE_JOY_BURST + (uint8_t)random(0L,4L));
        touchSpecialUntil=now+TOUCH_REACTION_MS;
        touchSpecialCooldownUntil=now+TOUCH_REACTION_COOLDOWN_MS;
        touchStreak=0;
        Serial.printf("[TOUCH] SPAM REACTION: %s\n",
          touchSpecialMood==FACE_JOY_BURST?"SUPER HAPPY":
          touchSpecialMood==FACE_LAUGH?"LAUGHING":
          touchSpecialMood==FACE_FURIOUS?"FURIOUS":"POUTING");
      }
    }
  } else {
    touchPreviewUntil=now+550UL;
    Serial.printf("[TOUCH] RELEASE after %lu ms\n",(unsigned long)(now-touchDownAt));
  }
}

// ================== INPUT: 2 TOMBOL + PRIORITAS COMBO ==================
// GPIO33 tap=timer/stats, hold 2s=Pomodoro.
// GPIO32 tap=silent/normal, hold 2s=Fun Clicker.
// KEDUANYA bersama 5s=toggle Box Breathing.
// Combo mengunci kedua input sampai KEDUA tombol dilepas, sehingga
// tidak terjadi klik/long-press individual secara tidak sengaja.
void toggleBoxBreathing(uint32_t now) {
  boxBreathingActive = !boxBreathingActive;
  cancelSpeech();
  overlayType = OVERLAY_NONE;
  stopTune();
  holdLastTarget = HOLD_COMBO;
  holdCompletedUntil = now + 350UL;
  if (boxBreathingActive) {
    boxBreathingStartedAt = now;
    modeNotice = NOTICE_NONE;
    Serial.println("[BREATH] START: 4s IN / 4s HOLD / 4s OUT / 4s HOLD");
  } else {
    modeNotice = NOTICE_BREATH_OFF;
    modeNoticeUntil = now + 1400UL;
    scheduleNextSpeech();
    Serial.println("[BREATH] STOP; previous mode resumes");
  }
}

void updateButtons() {
  const uint32_t now = millis();
  const bool mainRaw = digitalRead(BUTTON_PIN);
  const bool modeRaw = digitalRead(MODE_BUTTON_PIN);
  if (mainRaw != buttonReadingLast) buttonChangedAt = now;
  if (modeRaw != modeButtonLast) modeChangedAt = now;
  bool mainDown = false, mainUp = false, modeDown = false, modeUp = false;
  if (now - buttonChangedAt >= BUTTON_DEBOUNCE_MS && mainRaw != buttonStable) {
    buttonStable = mainRaw;
    mainDown = (mainRaw == LOW);
    mainUp = !mainDown;
  }
  if (now - modeChangedAt >= BUTTON_DEBOUNCE_MS && modeRaw != modeButtonStable) {
    modeButtonStable = modeRaw;
    modeDown = (modeRaw == LOW);
    modeUp = !modeDown;
  }
  buttonReadingLast = mainRaw;
  modeButtonLast = modeRaw;

  // Alarm clock punya prioritas lebih tinggi (33=snooze, 32=dismiss).
  if (scheduledAlarmActive) {
    if (mainDown) {
      stopScheduledAlarm(true);
      Serial.println("[ALARM] Snooze via GPIO33");
    } else if (modeDown) {
      stopScheduledAlarm(false);
      Serial.println("[ALARM] Dismiss via GPIO32");
    }
    comboGuard = true;
    return;
  }

  // Dua tombol tertekan: MULAI menghitung 5 detik sejak keduanya LOW.
  if (buttonStable == LOW && modeButtonStable == LOW) {
    if (!comboGuard) {
      comboGuard = true;
      comboTriggered = false;
      comboStartedAt = now;
      cancelSpeech();
      Serial.println("[BUTTON] BOTH DOWN: hold 5s to toggle Box Breathing");
    }
    if (!comboTriggered && now - comboStartedAt >= DUAL_HOLD_MS) {
      comboTriggered = true;
      toggleBoxBreathing(now);
    }
    return;
  }

  // Sekali combo dimulai, masing-masing tombol tidak boleh menyebabkan
  // aksi lain meskipun salah satu tombol dilepas lebih dulu.
  if (comboGuard) {
    if (buttonStable == HIGH && modeButtonStable == HIGH) {
      comboGuard = false;
      comboTriggered = false;
      Serial.println("[BUTTON] BOTH RELEASED");
    }
    return;
  }

  if (mainDown) {
    buttonPressedAt = now;
    longPressHandled = false;
    cancelSpeech();
    Serial.println("[BUTTON33] DOWN");
  }
  if (modeDown) {
    modePressedAt = now;
    modeLongHandled = false;
    cancelSpeech();
    Serial.println("[BUTTON32] DOWN");
  }

  // Jangan menjalankan tindakan individual selama Box Breathing.
  // Untuk keluar, tahan kedua tombol selama 5 detik lagi.
  if (boxBreathingActive) {
    if (mainUp) Serial.println("[BREATH] GPIO33 ignored; use BOTH buttons");
    if (modeUp) Serial.println("[BREATH] GPIO32 ignored; use BOTH buttons");
    return;
  }

  if (buttonStable == LOW && !longPressHandled && now-buttonPressedAt >= HOLD_MS) {
    longPressHandled = true;
    holdCompletedAt = now;
    holdLastTarget = HOLD_MAIN;
    holdCompletedUntil = now+350UL;
    if (pomoActive()) stopPomodoro();
    else beginFocus(true);
  }
  if (modeButtonStable == LOW && !modeLongHandled && now-modePressedAt >= HOLD_MS) {
    modeLongHandled = true;
    holdLastTarget = HOLD_MODE;
    holdCompletedUntil = now+300UL;
    gameMode = !gameMode;
    cancelSpeech();
    if (gameMode) {
      gameCount = 0;
      if (storageReady) storage.putUInt("game_cnt",0);
    } else if (storageReady) {
      storage.putUInt("game_cnt",gameCount);
      storage.putUInt("game_best",gameBest);
    }
    modeNotice = gameMode ? NOTICE_GAME_ON : NOTICE_GAME_OFF;
    modeNoticeUntil = now + 1450UL;
    Serial.printf("[GAME] %s\n",gameMode?"START":"STOP");
  }
  if (mainUp && !longPressHandled) {
    if (pomoActive()) {
      showOverlay(OVERLAY_TIMER);
      Serial.println("[BUTTON33] TAP: countdown");
    } else {
      showOverlay(OVERLAY_STATS);
      Serial.println("[BUTTON33] TAP: statistics");
    }
  }
  if (modeUp && !modeLongHandled) {
    silentMode = !silentMode;
    silentLocalOverride = true;
    if (silentMode) {
      noTone(BUZZER_PIN);
      speechVoiceRunning = false;
      stopTune();
    }
    if (storageReady) storage.putBool("silent",silentMode);
    modeNotice = silentMode ? NOTICE_SILENT : NOTICE_SOUND;
    modeNoticeUntil = now + 1700UL;
    Serial.printf("[SOUND] %s\n",silentMode?"SILENT":"NORMAL");
  }
}

void drawGameCounter(uint32_t now) {
  drawFace(now);
  // Counter kecil mengambang, TANPA garis pemisah yang memotong wajah.
  // Di bagian bawah OLED ada background kecil agar angka tetap terbaca.
  char counter[24];
  snprintf(counter,sizeof(counter),"%lu / %u",(unsigned long)gameCount,(unsigned)gameTarget);
  uint8_t tw = (uint8_t)min(122, (int)strlen(counter)*6);
  int x = (OLED_W-tw)/2;
  display.fillRoundRect(x-4,54,tw+8,10,3,SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x,55);
  display.print(counter);
  // Kecil, namun terasa hidup: sparkles yang muncul sebentar setelah tap.
  if (now-gameLastClickAt < 400UL) {
    uint8_t delta=(now-gameLastClickAt)/100UL;
    drawStar(11+delta*2,10+delta,2);
    drawStar(115-delta*2,12+delta,2);
  }
}

void drawGameCelebration(uint32_t now) {
  drawFace(now);
  for(uint8_t i=0;i<7;i++) {
    int x=(i*31 + now/80)%128;
    int y=(i*17 + now/130)%55;
    drawStar(x,y,2+(i%2));
  }
  display.fillRoundRect(19,53,90,11,4,SSD1306_BLACK);
  display.setTextSize(1);display.setCursor(28,55);
  display.print("GOAL! ");display.print(gameCount);
}

// ================== WAJAH V6: SATU SISTEM MORPHING ==================
// Semua emosi menggunakan geometri dasar mata + mulut yang sama.
// Tidak ada lagi early-return untuk wajah ekspresi khusus.

FaceMood idleMood = FACE_CALM;
uint32_t moodEndsAt = 0;
uint32_t nextMoodAt = 0;
uint32_t nextBlinkAt = 0;
uint32_t blinkStartedAt = 0;
bool blinkRunning = false;

// Parameter wajah yang diperhalus dengan easing berbasis waktu.
float lookX = 0, lookY = 0;
float eyeLeftH = 32, eyeRightH = 32, eyeWidth = 22;
float mouthSmile = 0.55f, mouthOpen = 0, mouthWidth = 28;
float faceShiftX = 0, faceShiftY = 0;
float fxBlush = 0, fxTongue = 0, fxX = 0, fxCat = 0;
float fxSparkle = 0, fxAngry = 0, fxPout = 0, fxTears = 0;
uint32_t faceFrameAt = 0;

float approach(float cur, float target, float alpha) {
  return cur + (target-cur) * alpha;
}

void updateFaceBehavior() {
  uint32_t now = millis();
  if (blinkRunning && now-blinkStartedAt>=190UL) {
    blinkRunning=false;
    nextBlinkAt=now+(uint32_t)random(2400L,5500L);
  }
  if (!blinkRunning && (int32_t)(now-nextBlinkAt)>=0) {
    blinkRunning=true;
    blinkStartedAt=now;
  }
  if (idleMood!=FACE_CALM && (int32_t)(now-moodEndsAt)>=0)
    idleMood=FACE_CALM;
  if (idleMood==FACE_CALM && (int32_t)(now-nextMoodAt)>=0) {
    // Mood affects expression mix, without changing the shared morphing geometry.
    long pick=random(0L,100L);
    if(currentMood==0 && pick<45) idleMood=(FaceMood)random((long)FACE_SQUINT_GRIN,(long)FACE_LOVE+1);
    else if(currentMood==1 && pick<30) idleMood=(FaceMood)random((long)FACE_HAPPY,(long)FACE_WINK+1);
    else if(currentMood==3 && pick<30) idleMood=FACE_SHY_KISS;
    else if(currentMood==4 && pick<30) idleMood=FACE_CRYING;
    else if(pick<20) idleMood=(FaceMood)random((long)FACE_SQUINT_GRIN,(long)FACE_CONFUSED+1);
    else idleMood=(FaceMood)random(1L,(long)FACE_TOUCH_CAT);
    moodEndsAt=now+(uint32_t)random(1050L,2250L);
    nextMoodAt=moodEndsAt+(uint32_t)random(1300L,3300L);
  }
}

void filledEye(int16_t cx,int16_t cy,int16_t w,int16_t h) {
  w=constrain(w,3,34); h=constrain(h,2,42);
  int16_t radius=min(w/2,h/2);
  display.fillRoundRect(cx-w/2,cy-h/2,w,h,radius,SSD1306_WHITE);
}

void drawStar(int16_t x,int16_t y,int16_t r) {
  if (r<1) return;
  display.drawLine(x-r,y,x+r,y,SSD1306_WHITE);
  display.drawLine(x,y-r,x,y+r,SSD1306_WHITE);
  if (r>=3) {
    display.drawPixel(x-r+1,y-r+1,SSD1306_WHITE);
    display.drawPixel(x+r-1,y+r-1,SSD1306_WHITE);
  }
}

FaceMood currentFaceMood(uint32_t now) {
  if (pomoState==POMO_HAPPY) return FACE_JOY_BURST;
  if ((int32_t)(now-touchSpecialUntil)<0) return touchSpecialMood;
  if (touchActive) {
    uint32_t held=now-touchDownAt;
    uint8_t stage=(held/1200UL)%3;
    return stage==0 ? FACE_HAPPY :
           stage==1 ? FACE_TOUCH_CAT : FACE_TOUCH_WIGGLE;
  }
  if ((int32_t)(now-touchPreviewUntil)<0) return FACE_HAPPY;
  return idleMood;
}

void drawSoftBlush(int x,int y,float strength) {
  if (strength<0.22f) return;
  int16_t extent=(int16_t)(strength*4.0f);
  for (int i=0;i<3;i++) {
    int16_t xx=x+i*4;
    display.drawLine(xx,y,xx+extent/2,y+extent,SSD1306_WHITE);
  }
}

// Mata X terbentuk bertahap, tidak muncul sekaligus.
void drawGrowingX(int cx,int cy,float t) {
  int r=(int)(9.0f*t);
  if (r<1) return;
  display.drawLine(cx-r,cy-r,cx+r,cy+r,SSD1306_WHITE);
  display.drawLine(cx+r,cy-r,cx-r,cy+r,SSD1306_WHITE);
}

void drawFace(uint32_t now) {
  FaceMood mood=currentFaceMood(now);
  float tx=0,ty=0,thL=32,thR=32,tw=22;
  float smile=.55f,openness=0,mw=28;
  float hdx=0,hdy=0,blush=0,tongue=0,xeyes=0,cat=0,sparkle=0,angry=0,pout=0,tears=0;
  const float slow=sinf((float)now/900.0f);
  const float quick=sinf((float)now/165.0f);

  switch(mood) {
    case FACE_LOOK_LEFT: tx=-8; break;
    case FACE_LOOK_RIGHT: tx=8; break;
    case FACE_SLEEPY: thL=thR=7; smile=.19f; hdy=1.5f*slow; break;
    case FACE_HAPPY: thL=thR=9; smile=1; mw=34; blush=.7f; break;
    case FACE_SURPRISED: thL=thR=37; tw=27; openness=.75f; mw=13; smile=.15f; break;
    case FACE_WINK: thL=4; smile=.9f; mw=31; break;
    case FACE_BLUSH: thL=thR=27; smile=.85f; mw=31; blush=1; tx=2.5f*slow; break;
    case FACE_TONGUE: thL=5; thR=30; smile=.7f; openness=.6f; mw=18; tongue=1; break;
    case FACE_X_EYES: thL=thR=5; tw=8; smile=.15f; xeyes=1; hdy=2.5f*slow; break;
    case FACE_DREAMY: thL=thR=14; tx=5.0f*slow; ty=-3; smile=.5f; sparkle=.3f; break;
    case FACE_SMUG: thL=10; thR=20; smile=.77f; tx=3; mw=23; break;
    case FACE_TOUCH_CAT:
      thL=thR=8; ty=-5; hdy=-2.0f+1.2f*slow; smile=.9f;
      mw=25; blush=.7f; cat=1; break;
    case FACE_TOUCH_WIGGLE:
      thL=thR=8; mw=35; openness=.97f; smile=1;
      hdx=4.0f*quick; hdy=1.4f*sinf((float)now/250.0f); blush=1; break;
    case FACE_JOY_BURST:
      thL=thR=28; mw=37; openness=.90f; smile=1;
      sparkle=1; blush=1; hdy=1.5f*slow; break;
    case FACE_LAUGH:
      thL=thR=6; openness=1; smile=1; mw=40;
      blush=.9f; hdx=3.0f*quick; hdy=2.0f*quick; break;
    case FACE_FURIOUS:
      thL=thR=17; tw=23; angry=1; smile=.05f; mw=17;
      hdx=1.3f*quick; break;
    case FACE_POUT:
      thL=thR=12; smile=.05f; openness=.40f;
      mw=10; blush=.3f; pout=1; hdy=.8f*slow; break;
    // Inspired by the user's reference sheet, but optimized for 1-bit OLED.
    case FACE_SQUINT_GRIN:
      thL=thR=5; smile=1; mw=35; openness=.8f;
      blush=.5f; hdy=1.5f*slow; break;
    case FACE_SHY_KISS:
      thL=6; thR=29; tw=19; smile=.0f; openness=.35f;
      mw=10; blush=1; tx=2.0f*slow; break;
    case FACE_CRYING:
      thL=thR=11; smile=.04f; mw=18; openness=.5f;
      tears=1; hdy=1.5f*slow; break;
    case FACE_LOVE:
      thL=thR=22; smile=1; mw=30; blush=1; sparkle=1;
      hdy=-1.5f; break;
    case FACE_CONFUSED:
      thL=17; thR=34; smile=.1f; mw=13; openness=.36f;
      tx=3.0f*slow; break;
    default: break;
  }

  // Ease exponential menurut delta waktu (konsisten walau FPS berubah).
  uint32_t dt=faceFrameAt ? now-faceFrameAt : OLED_FRAME_MS;
  faceFrameAt=now;
  dt=constrain(dt,1UL,120UL);
  const float alpha=1.0f-expf(-((float)dt)/190.0f);
  lookX=approach(lookX,tx,alpha);
  lookY=approach(lookY,ty,alpha);
  eyeLeftH=approach(eyeLeftH,thL,alpha);
  eyeRightH=approach(eyeRightH,thR,alpha);
  eyeWidth=approach(eyeWidth,tw,alpha);
  mouthSmile=approach(mouthSmile,smile,alpha);
  mouthOpen=approach(mouthOpen,openness,alpha);
  mouthWidth=approach(mouthWidth,mw,alpha);
  faceShiftX=approach(faceShiftX,hdx,alpha);
  faceShiftY=approach(faceShiftY,hdy,alpha);
  fxBlush=approach(fxBlush,blush,alpha);
  fxTongue=approach(fxTongue,tongue,alpha);
  fxX=approach(fxX,xeyes,alpha);
  fxCat=approach(fxCat,cat,alpha);
  fxSparkle=approach(fxSparkle,sparkle,alpha);
  fxAngry=approach(fxAngry,angry,alpha);
  fxPout=approach(fxPout,pout,alpha);
  fxTears=approach(fxTears,tears,alpha);

  float blinkFactor=1;
  if (blinkRunning && fxX < .5f && fxCat < .65f) {
    float p=constrain((float)(now-blinkStartedAt)/190.0f,0.0f,1.0f);
    blinkFactor=1.0f-0.94f*(1.0f-fabsf(2.0f*p-1.0f));
  }
  const int hx=(int)roundf(faceShiftX), hy=(int)roundf(faceShiftY);
  const int ey=(int)roundf(26.0f + lookY + hy + .7f*sinf((float)now/1350.0f));
  const int ex1=37+(int)roundf(lookX)+hx;
  const int ex2=91+(int)roundf(lookX)+hx;
  const int eh1=(int)roundf(eyeLeftH*blinkFactor);
  const int eh2=(int)roundf(eyeRightH*blinkFactor);
  const int ew=(int)roundf(eyeWidth);

  // Wajah dasar tetap identik untuk semua ekspresi.
  filledEye(ex1,ey,ew,eh1);
  filledEye(ex2,ey,ew,eh2);
  if (fxX>.06f) {
    drawGrowingX(ex1,ey,fxX);
    drawGrowingX(ex2,ey,fxX);
  }

  // Alis marah dibentuk pelan-pelan, tetap rounded & kecil.
  if (fxAngry>.10f) {
    int offset=(int)(8*fxAngry);
    display.drawLine(ex1-10,ey-16,ex1+9,ey-16+offset,SSD1306_WHITE);
    display.drawLine(ex2-9,ey-16+offset,ex2+10,ey-16,SSD1306_WHITE);
  }
  // Mendongak saat dielus: bintang kecil, hidung kucing dan kumis pendek.
  if (fxCat>.28f) {
    int nr=max(1,(int)(3*fxCat));
    display.fillTriangle(64-nr,35+hy,64+nr,35+hy,64,38+hy,SSD1306_WHITE);
    if (fxCat>.60f) {
      display.drawLine(19,39+hy,29,41+hy,SSD1306_WHITE);
      display.drawLine(99,41+hy,109,39+hy,SSD1306_WHITE);
    }
  }

  int mouthCx=64+hx;
  int mouthY=45+hy;
  int halfWidth=(int)(mouthWidth/2.0f);
  int left=mouthCx-halfWidth, right=mouthCx+halfWidth;
  // Semua ekspresi tetap mempunyai mulut yang mengikuti parameter yang sama.
  if (mouthOpen>.20f) {
    int openHeight=3+(int)(mouthOpen*12.0f);
    int width=max(9,2*halfWidth);
    int top=mouthY-2;
    int rr=min(openHeight/2,width/2);
    display.fillRoundRect(mouthCx-width/2,top,width,openHeight,rr,SSD1306_WHITE);
    // Interior gelap memberi kesan senyum terbuka, bukan kotak kaku.
    if (openHeight>=9 && width>=16)
      display.fillRoundRect(mouthCx-width/2+4,top+2,width-8,max(2,openHeight-6),
                            min(4,(openHeight-6)/2),SSD1306_BLACK);
    if (fxTongue>.46f)
      display.fillRoundRect(mouthCx-5,top+openHeight-4,10,5,2,SSD1306_WHITE);
  } else {
    int rise=(int)(7.0f*mouthSmile);
    int centerY=mouthY+rise;
    int q1=left+(right-left)/4, q3=right-(right-left)/4;
    display.drawLine(left,mouthY,q1,centerY-1,SSD1306_WHITE);
    display.drawLine(q1,centerY-1,mouthCx,centerY,SSD1306_WHITE);
    display.drawLine(mouthCx,centerY,q3,centerY-1,SSD1306_WHITE);
    display.drawLine(q3,centerY-1,right,mouthY,SSD1306_WHITE);
  }
  // Efek tambahan tumbuh/menghilang secara bertahap.
  drawSoftBlush(11+hx,44+hy,fxBlush);
  drawSoftBlush(105+hx,44+hy,fxBlush);
  if (fxPout>.45f) {
    int r=(int)(4*fxPout);
    display.drawCircle(19,43+hy,max(1,r),SSD1306_WHITE);
    display.drawCircle(109,43+hy,max(1,r),SSD1306_WHITE);
  }
  if(fxTears>.20f) {
    int len=(int)(21.0f*fxTears);
    int drift=(int)(now/100UL%8UL);
    display.drawLine(ex1,ey+8,ex1-2,ey+8+max(1,len-drift),SSD1306_WHITE);
    display.drawLine(ex1+2,ey+9,ex1+1,ey+9+max(1,len-drift),SSD1306_WHITE);
    display.drawLine(ex2,ey+8,ex2+2,ey+8+max(1,len-drift),SSD1306_WHITE);
    display.drawLine(ex2-2,ey+9,ex2-1,ey+9+max(1,len-drift),SSD1306_WHITE);
  }
  if (fxSparkle>.25f) {
    int r=max(1,(int)(4.0f*fxSparkle));
    int jitter=(int)(sinf((float)now/260.0f)*2.0f);
    drawStar(12+jitter,13,r);
    drawStar(115-jitter,12,r);
    if (fxSparkle>.7f) drawStar(64,7,2);
  }
}

void drawHeart(int16_t cx, int16_t cy, int16_t scale) {
  display.fillCircle(cx - scale / 2, cy - scale / 3,
                     scale / 2, SSD1306_WHITE);
  display.fillCircle(cx + scale / 2, cy - scale / 3,
                     scale / 2, SSD1306_WHITE);
  display.fillTriangle(cx - scale, cy - scale / 4,
                       cx + scale, cy - scale / 4,
                       cx, cy + scale, SSD1306_WHITE);
}

void drawMeasureScreen(uint32_t now) {
  // Saat belum ada BPM valid: seluruh layar untuk proses pengukuran.
  if (avgBPM <= 0) {
    int16_t pulse = 8 + (int16_t)(2.0f * sinf((float)now / 170.0f));
    drawHeart(64, 21, pulse);

    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(17, 39);
    display.print("READING");

    // Tiga titik bergerak sebagai indikator aktivitas.
    uint8_t dotCount = (now / 330) % 4;
    for (uint8_t i = 0; i < dotCount; ++i) {
      display.fillCircle(104 + i * 7, 49, 2, SSD1306_WHITE);
    }
    return;
  }

  // Setelah ada BPM: angka besar terpusat, tanpa footer kecil.
  char bpmText[6];
  snprintf(bpmText, sizeof(bpmText), "%d", avgBPM);

  int16_t x1, y1;
  uint16_t textW, textH;
  display.setTextSize(4);
  display.getTextBounds(bpmText, 0, 0, &x1, &y1, &textW, &textH);
  display.setCursor((OLED_W - textW) / 2, 8);
  display.print(bpmText);

  display.setTextSize(2);
  display.setCursor(46, 43);
  display.print("BPM");

  // Titik berdenyut setiap ada beat valid.
  if (now - lastPulseEffectAt < 150) {
    display.fillCircle(14, 52, 4, SSD1306_WHITE);
  }
}


// ================== UI: PROGRESS BAR HORIZONTAL ==================
// Memakai seluruh lebar OLED, 5 px di paling bawah layar, tanpa hidung.
void drawFaceProgressBar(uint32_t now) {
  if (!inFocus() && !inBreak()) return;
  uint32_t duration=phaseDurationMs();
  if (!duration) return;
  uint32_t elapsed=min((uint32_t)(now-segmentStartedAt),duration);
  const int x=0,y=59,w=128,h=5;
  display.fillRect(x,y,w,h,SSD1306_BLACK); // pastikan tidak berbenturan dengan muka
  display.drawRect(x,y,w,h,SSD1306_WHITE);
  int fill=(int)((uint64_t)elapsed*(w-4)/duration);
  if (fill>0) display.fillRect(x+2,y+1,fill,3,SSD1306_WHITE);
}

// Circular loading UI dipakai untuk SEMUA jenis hold (2s dan 5s).
void drawHoldScreen(uint32_t now, uint32_t startedAt, uint32_t duration,
                    const char* title, bool finished) {
  uint32_t elapsed = finished ? duration : min((uint32_t)(now-startedAt),duration);
  float ratio = (float)elapsed / (float)duration;
  const int cx=64, cy=32;
  display.drawCircle(cx,cy,28,SSD1306_WHITE);
  const int segments=60;
  int active=(int)(segments*ratio+.5f);
  for (int i=0;i<active;++i) {
    float a=(-90.0f+i*360.0f/segments)*PI/180.0f;
    int x1=cx+(int)(21*cosf(a)), y1=cy+(int)(21*sinf(a));
    int x2=cx+(int)(26*cosf(a)), y2=cy+(int)(26*sinf(a));
    display.drawLine(x1,y1,x2,y2,SSD1306_WHITE);
  }
  display.setTextSize(1);
  int titleLen=strlen(title);
  display.setCursor(cx-(titleLen*6)/2,22);
  display.print(title);
  char pct[8];snprintf(pct,sizeof(pct),"%u%%",(unsigned)(ratio*100));
  display.setCursor(cx-((int)strlen(pct)*6)/2,35);
  display.print(pct);
}

// Konfirmasi pergantian sound dan Fun Clicker: benar-benar tampil fullscreen.
void drawModeNotice(uint32_t now) {
  const bool muted=(modeNotice==NOTICE_SILENT);
  if (modeNotice==NOTICE_SILENT || modeNotice==NOTICE_SOUND) {
    // Speaker icon: source body, cone, and expanding sound waves.
    display.fillRect(38,10,7,11,SSD1306_WHITE);
    display.fillTriangle(45,10,55,4,55,27,SSD1306_WHITE);
    if (muted) {
      display.drawLine(60,7,76,25,SSD1306_WHITE);
      display.drawLine(60,25,76,7,SSD1306_WHITE);
    } else {
      display.drawCircle(55,16,13,SSD1306_WHITE);
      display.drawCircle(55,16,17,SSD1306_WHITE);
    }
    display.setTextSize(2);
    display.setCursor(muted?28:19,37);
    display.print(muted?"SILENT":"SOUND ON");
  } else if (modeNotice==NOTICE_GAME_ON || modeNotice==NOTICE_GAME_OFF) {
    int b=(now/135UL)%5;
    drawStar(17+b,11,3);drawStar(111-b,11,3);
    display.setTextSize(2);
    display.setCursor(22,12);display.print("CLICKER");
    display.setCursor(modeNotice==NOTICE_GAME_ON?23:27,36);
    display.print(modeNotice==NOTICE_GAME_ON?"GAME ON":"GAME OFF");
  } else if(modeNotice==NOTICE_BREATH_OFF) {
    display.setTextSize(2);
    display.setCursor(31,13);display.print("BREATH");
    display.setCursor(45,38);display.print("OFF");
  }
}

// 4-4-4-4: square tracing + growing/shrinking cute breathing orb.
// Satu frame menggambar ulang dari waktu millis(), tanpa delay().
void drawBoxBreathing(uint32_t now) {
  const uint32_t elapsed = now-boxBreathingStartedAt;
  const uint8_t stage=(elapsed/BREATH_PHASE_MS)%4;
  const uint32_t within=elapsed%BREATH_PHASE_MS;
  const float t=(float)within/(float)BREATH_PHASE_MS;
  const uint32_t round=elapsed/(BREATH_PHASE_MS*4UL)+1UL;
  const int x0=7,y0=7,L=49, cx=31,cy=32;

  // Empat sisi kotak, kursor berjalan perlahan di setiap sisi.
  display.drawRoundRect(x0,y0,L,L,5,SSD1306_WHITE);
  int length=(int)((L-8)*t);
  int px=x0+4,py=y0+4;
  if(stage==0) { px+=length;display.drawLine(x0+4,y0+1,px,y0+1,SSD1306_WHITE); }
  else if(stage==1) {px=x0+L-5;py+=length;display.drawLine(x0+L-2,y0+4,x0+L-2,py,SSD1306_WHITE);}
  else if(stage==2) {px=x0+L-5-length;py=y0+L-5;display.drawLine(px,y0+L-2,x0+L-5,y0+L-2,SSD1306_WHITE);}
  else {py=y0+L-5-length;display.drawLine(x0+1,py,x0+1,y0+L-5,SSD1306_WHITE);}
  display.fillCircle(px,py,2,SSD1306_WHITE);

  float sizeFraction=stage==0?t:stage==1?1.0f:stage==2?1.0f-t:0.0f;
  // eased fraction smooth di awal/akhir tiap tahap
  sizeFraction=sizeFraction*sizeFraction*(3.0f-2.0f*sizeFraction);
  int r=7+(int)(10.0f*sizeFraction);
  display.drawCircle(cx,cy,r+3,SSD1306_WHITE);
  display.fillCircle(cx,cy,r,SSD1306_WHITE);
  if(r>=11) {
    display.fillCircle(cx-4,cy-2,1,SSD1306_BLACK);
    display.fillCircle(cx+4,cy-2,1,SSD1306_BLACK);
    display.drawLine(cx-3,cy+4,cx,cy+5,SSD1306_BLACK);
    display.drawLine(cx,cy+5,cx+3,cy+4,SSD1306_BLACK);
  }
  const char* title = stage==0?"BREATHE IN":stage==1?"HOLD":stage==2?"BREATHE OUT":"HOLD";
  display.setTextSize(1);
  display.setCursor(stage==2?60:65,6); display.print(title);
  int remain=4-(int)(within/1000UL);
  display.setTextSize(3);display.setCursor(82,23);display.print(remain);
  display.setTextSize(1);
  display.setCursor(71,51);display.print("ROUND ");display.print(round);
  display.setCursor(66,59);display.print("4-4-4-4");
}

void drawStatsScreen(uint32_t now) {
  uint32_t seconds = totalFocusSeconds(now);
  display.setTextSize(1);
  display.setCursor(31, 0); display.print("FOCUS TOTAL");
  char timeStr[20];
  snprintf(timeStr,sizeof(timeStr),"%lum %02lus",
           (unsigned long)(seconds/60), (unsigned long)(seconds%60));
  int16_t x,y; uint16_t w,h;
  display.setTextSize(2);
  display.getTextBounds(timeStr,0,0,&x,&y,&w,&h);
  display.setCursor((OLED_W-(int)w)/2,14); display.print(timeStr);
  display.drawLine(10,35,117,35,SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(20,41); display.print("COMPLETED CYCLES");
  display.setTextSize(2);
  char cyclesText[16];
  snprintf(cyclesText,sizeof(cyclesText),"%lu",(unsigned long)completedCycles);
  display.getTextBounds(cyclesText,0,0,&x,&y,&w,&h);
  display.setCursor((OLED_W-(int)w)/2,50); display.print(cyclesText);
}

void drawTimerScreen(uint32_t now) {
  uint32_t secs = phaseRemainingSeconds(now);
  const bool focus = inFocus();
  display.setTextSize(1);
  if (focus) {
    display.setCursor(42,2); display.print("FOCUS");
    display.setCursor(95,2); display.printf("%u/4", (unsigned)(completedInBlock+1));
  } else {
    display.setCursor(pomoState == POMO_LONG_BREAK ? 27 : 24,2);
    display.print(pomoState == POMO_LONG_BREAK ? "LONG BREAK" : "SHORT BREAK");
  }

  char timer[8];
  snprintf(timer,sizeof(timer),"%02lu:%02lu",
           (unsigned long)(secs/60), (unsigned long)(secs%60));
  int16_t x,y; uint16_t tw,th;
  display.setTextSize(3);
  display.getTextBounds(timer,0,0,&x,&y,&tw,&th);
  display.setCursor((OLED_W-(int)tw)/2,18); display.print(timer);

  uint32_t duration = phaseDurationMs();
  uint32_t elapsed = now - segmentStartedAt;
  if (elapsed > duration) elapsed=duration;
  display.drawRect(7,55,114,7,SSD1306_WHITE);
  int width = (int)((uint64_t)elapsed * 110UL / duration);
  if (width>0) display.fillRect(9,57,width,3,SSD1306_WHITE);
}

void drawFireworkScreen(uint32_t now) {
  uint32_t t = now-celebrationStartedAt;
  // Beberapa ledakan bergantian, ditampilkan sebagai garis-garis radial.
  const int centersX[4] = {26, 92, 63, 109};
  const int centersY[4] = {23, 18, 39, 40};
  for (int burst=0; burst<4; ++burst) {
    uint32_t phase = (t + 2900UL - (uint32_t)burst*480UL) % 1450UL;
    if (phase > 1050UL) continue;
    int radius = 2 + (int)(phase / 80UL);
    int cx=centersX[burst],cy=centersY[burst];
    for (int k=0;k<10;k++) {
      float angle=(k * (2.0f*PI/10.0f)) + burst*0.37f;
      int x1=cx+(int)((radius-3)*cosf(angle));
      int y1=cy+(int)((radius-3)*sinf(angle));
      int x2=cx+(int)(radius*cosf(angle));
      int y2=cy+(int)(radius*sinf(angle));
      display.drawLine(x1,y1,x2,y2,SSD1306_WHITE);
    }
  }
  if ((t/180UL)%2==0) {
    display.fillRect(35,44,58,17,SSD1306_BLACK);
    display.setTextSize(2); display.setCursor(38,44); display.print("YAY!");
  }
}

void drawSparklingHappy(uint32_t now) {
  drawFace(now); // state POMO_HAPPY memaksa ekspresi HAPPY, tanpa hidung.
  int shift=(now/140UL)%6;
  drawStar(12+shift,13,4);
  drawStar(116-shift,15,3);
  drawStar(18,51,3);
  drawStar(109,53,4);
  if ((now/170UL)%2==0) {
    drawStar(63,7,3);
    display.fillCircle(10,33,2,SSD1306_WHITE);
  }
}

// ================== TEKS OTOMATIS V6 ==================
// Pilih font terbesar yang muat secara utuh dengan pembungkus kata.
// Size 3: maksimum 2 baris; size 2: maksimum 3 baris;
// size 1 hanya dipakai untuk kalimat yang benar-benar sangat panjang.

// Break into words, then use balanced dynamic-programming word wrapping.
// Fixed arrays: no String/new allocation each OLED frame.
bool layoutSpeechAtSize(const char* msg, uint8_t fontSize,
                        char out[4][22], uint8_t& outCount) {
  const int maxChars = min(21,(int)((OLED_W-8)/(6*fontSize)));
  const int step=8*fontSize+3;
  const int maxLines=min(4,(int)((OLED_H-4+3)/step));
  if (maxLines<1 || maxChars<1) return false;

  char words[16][22]={{0}};
  int n=0,totalLength=0;
  const char* p=msg;
  while (*p) {
    while (*p==' ') ++p;
    if (!*p) break;
    if (n>=16) return false;
    int len=0;
    while (*p && *p!=' ') {
      if (len>=21) return false;
      words[n][len++]=*p++;
    }
    if (len>maxChars) return false;
    words[n][len]=0;
    totalLength+=len;
    n++;
  }
  if (!n) return false;
  totalLength+=n-1; // spaces

  const int INF=10000000;
  for (int rows=1;rows<=maxLines;rows++) {
    int dp[5][17],parent[5][17];
    for (int i=0;i<5;i++) for(int j=0;j<17;j++) {
      dp[i][j]=INF;parent[i][j]=-1;
    }
    dp[0][0]=0;
    const float ideal=(float)totalLength/(float)rows;
    for (int k=1;k<=rows;k++) {
      for (int i=k;i<=n;i++) {
        int used=0;
        for (int j=i-1;j>=k-1;j--) {
          used+=(int)strlen(words[j]);
          if (j<i-1) used++;
          if (used>maxChars) break;
          if (dp[k-1][j]==INF) continue;
          float diff=used-ideal;
          int cost=dp[k-1][j]+(int)(diff*diff*10.0f);
          if (cost<dp[k][i]) {dp[k][i]=cost;parent[k][i]=j;}
        }
      }
    }
    if (dp[rows][n]==INF) continue;
    int bounds[5]={0};bounds[rows]=n;
    int pos=n;
    for (int k=rows;k>0;k--) {
      pos=parent[k][pos];
      if (pos<0) return false;
      bounds[k-1]=pos;
    }
    for (int k=0;k<4;k++) out[k][0]=0;
    for (int k=0;k<rows;k++) {
      for (int w=bounds[k];w<bounds[k+1];w++) {
        if (w>bounds[k]) strcat(out[k]," ");
        strcat(out[k],words[w]);
      }
    }
    outCount=(uint8_t)rows;
    return true;
  }
  return false;
}

void drawSpeechScreen() {
  if (!speechMessage) return;
  char lines[4][22]={{0}};
  uint8_t count=0, chosenSize=1;
  bool found=false;
  for (int s=3;s>=1;s--) {
    if (layoutSpeechAtSize(speechMessage,(uint8_t)s,lines,count)) {
      chosenSize=(uint8_t)s;
      found=true;
      break;
    }
  }
  if (!found) return;
  int h=8*chosenSize;
  int gap=3;
  int totalH=(int)count*h+((int)count-1)*gap;
  int y=(OLED_H-totalH)/2;
  // Masuk perlahan dari bawah ~160ms.
  uint32_t passed=millis()-speechStartedAt;
  if (passed<160UL) y+=(int)((160UL-passed)*4UL/160UL);
  display.setTextWrap(false);
  display.setTextSize(chosenSize);
  for (uint8_t i=0;i<count;i++) {
    int width=(int)strlen(lines[i])*6*chosenSize;
    int x=(OLED_W-width)/2;
    display.setCursor(max(0,x),y+i*(h+gap));
    display.print(lines[i]);
  }
  display.setTextWrap(true);
}

uint32_t lastOLEDAt = 0;
void updateOLED() {
  if (!oledReady) return;
  uint32_t now = millis();
  if (now - lastOLEDAt < OLED_FRAME_MS) return;
  lastOLEDAt = now;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Prioritas: alarm > semua progress hold > box breathing > notice > game > Pomodoro.
  if (scheduledAlarmActive) {
    drawScheduledAlarmScreen();
  } else if (comboGuard && !comboTriggered &&
             buttonStable==LOW && modeButtonStable==LOW &&
             now-comboStartedAt>=150UL) {
    drawHoldScreen(now,comboStartedAt,DUAL_HOLD_MS,
                   boxBreathingActive?"EXIT":"BREATHE",false);
  } else if ((int32_t)(now-holdCompletedUntil)<0) {
    drawHoldScreen(now,now,1UL,"DONE",true);
  } else if (boxBreathingActive) {
    drawBoxBreathing(now);
  } else if (buttonStable==LOW && modeButtonStable==HIGH && !longPressHandled &&
             now-buttonPressedAt>=160UL) {
    drawHoldScreen(now,buttonPressedAt,HOLD_MS,pomoActive()?"STOP":"START",false);
  } else if (modeButtonStable==LOW && buttonStable==HIGH && !modeLongHandled &&
             now-modePressedAt>=160UL) {
    drawHoldScreen(now,modePressedAt,HOLD_MS,gameMode?"LEAVE":"GAME",false);
  } else if (modeNotice!=NOTICE_NONE && (int32_t)(now-modeNoticeUntil)<0) {
    drawModeNotice(now);
  } else if (gameMode && (int32_t)(now-gameCelebrationUntil)<0) {
    drawGameCelebration(now);
  } else if (gameMode) {
    drawGameCounter(now);
  } else if (pomoState == POMO_FIREWORKS) {
    drawFireworkScreen(now);
  } else if (pomoState == POMO_HAPPY) {
    drawSparklingHappy(now);
  } else if ((int32_t)(now-touchSpecialUntil)<0 || touchActive ||
             (int32_t)(now-touchPreviewUntil)<0) {
    drawFace(now);
    drawFaceProgressBar(now);
  } else if (overlayType != OVERLAY_NONE &&
             now - overlayStartedAt < OVERLAY_MS) {
    if (overlayType == OVERLAY_STATS) drawStatsScreen(now);
    else if (inFocus() || inBreak()) drawTimerScreen(now);
    else drawStatsScreen(now);
  } else if (!sensorReady) {
    display.setTextSize(2);
    display.setCursor(18,15);display.print("SENSOR");
    display.setCursor(24,37);display.print("ERROR");
  } else if (fingerDetected) {
    drawMeasureScreen(now);
  } else if (speechVisible && speechMessage) {
    drawSpeechScreen();
  } else {
    drawFace(now);
    drawFaceProgressBar(now);
  }
  display.display();
}

// ================== LED RING ==================
uint32_t lastRingAt = 0;
void updateRing() {
  uint32_t now=millis();
  if (now-lastRingAt < RING_FRAME_MS) return;
  lastRingAt=now;

  for (uint16_t i=0;i<LED_COUNT;++i) {
    float wave=0.5f+0.5f*sinf(now/360.0f+i*0.50f);
    uint8_t r=0,g=0,b=0;
    if (scheduledAlarmActive) {
      uint8_t glow=(uint8_t)(100+95*(0.5f+0.5f*sinf(now/170.0f)));
      ring.setPixelColor(i,ring.Color(glow,glow/4,glow/2));
      continue;
    } else if (boxBreathingActive) {
      // Lampu bernafas halus bersama orb di OLED.
      const uint32_t elapsed=now-boxBreathingStartedAt;
      const uint8_t st=(elapsed/BREATH_PHASE_MS)%4;
      float t=(float)(elapsed%BREATH_PHASE_MS)/(float)BREATH_PHASE_MS;
      float amount=st==0?t:st==1?1.0f:st==2?1.0f-t:0.0f;
      amount=amount*amount*(3.0f-2.0f*amount);
      uint8_t value=(uint8_t)(20+105*amount+10*wave);
      uint8_t hue=st==0?145:st==1?115:st==2?184:200;
      ring.setPixelColor(i,ring.gamma32(ring.ColorHSV((uint16_t)hue*256U,155,value)));
      continue;
    } else if (pomoState==POMO_FIREWORKS || pomoState==POMO_HAPPY || (gameMode && (int32_t)(now-gameCelebrationUntil)<0)) {
      // Rainbow berputar seperti perayaan Pomodoro, ditambah sparkle putih.
      const uint16_t hue=(uint16_t)((now*48UL+i*(65535UL/LED_COUNT))&0xFFFF);
      uint8_t value=(uint8_t)(155+65*(.5f+.5f*sinf(now/115.0f+i*1.6f)));
      if (random(0,9)==0) ring.setPixelColor(i,ring.Color(210,210,210));
      else ring.setPixelColor(i,ring.gamma32(ring.ColorHSV(hue,255,value)));
      continue;
    } else if (ringEffect==5) { r=0;g=0;b=0;
    } else if(ringEffect==3) {
      ring.setPixelColor(i,ring.gamma32(ring.ColorHSV((now*35UL+i*65535UL/LED_COUNT)&65535,210,175)));continue;
    } else if(ringEffect==4) {
      ring.setPixelColor(i,ring.Color((random(0,7)==0)?180:0,(random(0,8)==0)?110:0,(random(0,7)==0)?170:0));continue;
    } else if(ringEffect==1 || ringEffect==2) {
      float intensity=ringEffect==1?0.65f:(0.18f+0.65f*wave);
      r=(ringCustomColor>>16&255)*intensity;
      g=(ringCustomColor>>8&255)*intensity;
      b=(ringCustomColor&255)*intensity;
    } else if (gameMode) {
      r=12+(uint8_t)(65*wave);g=5+(uint8_t)(30*wave);b=40+(uint8_t)(85*wave);
    } else if (touchActive) {
      r=90+(uint8_t)(50*wave); g=42+(uint8_t)(45*wave); b=3;
    } else if (pomoState==POMO_FOCUS) {
      r=2;g=16+(uint8_t)(30*wave);b=55+(uint8_t)(65*wave);
    } else if (inBreak()) {
      r=4+(uint8_t)(12*wave);g=42+(uint8_t)(38*wave);b=6;
    } else {
      float intensity=0.08f+0.22f*wave;
      r=(uint8_t)((currentMoodColor>>16&255)*intensity);
      g=(uint8_t)((currentMoodColor>>8&255)*intensity);
      b=(uint8_t)((currentMoodColor&255)*intensity);
    }
    ring.setPixelColor(i,ring.Color(r,g,b));
  }
  ring.show();
}

// ================== LOG SERIAL ==================
uint32_t lastLogAt=0;
void updateSerial() {
  uint32_t now=millis();
  if (now-lastLogAt < DEBUG_INTERVAL_MS) return;
  lastLogAt=now;
  const char* phase="IDLE";
  if (pomoState==POMO_FOCUS) phase="FOCUS";
  else if (pomoState==POMO_SHORT_BREAK) phase="SHORT_BREAK";
  else if (pomoState==POMO_LONG_BREAK) phase="LONG_BREAK";
  else if (pomoState==POMO_FIREWORKS) phase="FIREWORKS";
  else if (pomoState==POMO_HAPPY) phase="HAPPY";
  Serial.printf("[STATUS] mode=%s | breath=%u | left=%lus | focus_total=%lus | cycles=%lu | touch=%d | finger=%d | IR=%lu | BPM=%d\n",
                phase,(unsigned)boxBreathingActive,(unsigned long)phaseRemainingSeconds(now),
                (unsigned long)totalFocusSeconds(now),
                (unsigned long)completedCycles, touchActive,
                fingerDetected,(unsigned long)lastIR,avgBPM);
}

void scanI2C() {
  Serial.println("[I2C] Scanning...");
  for (uint8_t a=1;a<127;++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission()==0) Serial.printf("[I2C] Found 0x%02X\n",a);
  }
}

// ================== SETUP & LOOP ==================
void setup() {
  Serial.begin(115200);
  Serial.printf("[BOOT] reset_reason=%d (inspect boot ROM rst:0x... when unexpected)\n", (int)esp_reset_reason());
  Wire.begin(SDA_PIN,SCL_PIN);
  Wire.setClock(400000);
  pinMode(TOUCH_PIN,INPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP);
  pinMode(MODE_BUTTON_PIN,INPUT_PULLUP);
  pinMode(BUZZER_PIN,OUTPUT);
  digitalWrite(BUZZER_PIN,LOW);
  Serial.println("\n==============================");
  Serial.println(" MINDPAL V10.4 - CA TRUST FIX");
  Serial.println("[TLS] Trust roots: GTS R1/R2/R3/R4, ISRG X1, GlobalSign Root CA");
  Serial.println("==============================");
  scanI2C();

  storageReady = storage.begin("mindpal",false);
  if (storageReady) {
    loadSpeechConfig();
    savedFocusSeconds=storage.getUInt("focus_s",0);
    completedCycles=storage.getUInt("cycles",0);
    gameBest=storage.getUInt("game_best",0);
    gameCount=storage.getUInt("game_cnt",0);
    silentMode=storage.getBool("silent",false);
    Serial.printf("[NVS] Loaded focus=%lus, cycles=%lu\n",
                  (unsigned long)savedFocusSeconds,(unsigned long)completedCycles);
  } else Serial.println("[NVS] Warning: persistent stats unavailable.");

  oledReady=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDRESS);
  Serial.println(oledReady?"[OLED] OK":"[OLED] FAILED");
  if (oledReady) {
    display.clearDisplay(); display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2); display.setCursor(17,24);
    display.print("MindPal");display.display();
  }
  sensorReady=heartSensor.begin(Wire,I2C_SPEED_FAST);
  if (sensorReady) {
    heartSensor.setup(0x1F,4,2,200,411,4096);
    heartSensor.setPulseAmplitudeRed(0x0A);
    heartSensor.setPulseAmplitudeIR(0x1F);
    heartSensor.setPulseAmplitudeGreen(0);
    Serial.println("[MAX30102] OK");
  } else Serial.println("[MAX30102] FAILED");

  ring.begin();ring.setBrightness(45);ring.clear();ring.show();
  modeButtonLast=digitalRead(MODE_BUTTON_PIN);modeButtonStable=modeButtonLast;
  buttonReadingLast=digitalRead(BUTTON_PIN);
  buttonStable=buttonReadingLast;
  touchActive=(digitalRead(TOUCH_PIN)==HIGH);
  touchRawLast=touchActive;
  touchRawChangedAt=millis();
  if (touchActive) touchDownAt=millis();
  randomSeed(micros());
  activeSpeechMode=modeFromPomodoro();
  scheduleNextSpeech();
  printSpeechConfig();
  nextMoodAt=millis()+1700;
  nextBlinkAt=millis()+1800;
  initCloud();
  startCloudWorker();
  Serial.println("[SYSTEM] GPIO33 tap=timer hold2=pomo | GPIO32 tap=sound hold2=game");
  Serial.println("[SYSTEM] BOTH buttons hold5=toggle box breathing (4-4-4-4)");
  Serial.printf("[TEST] Focus=%lus, short break=%lus, long break=%lus\n",
    (unsigned long)(FOCUS_MS/1000UL),
    (unsigned long)(SHORT_BREAK_MS/1000UL),
    (unsigned long)(LONG_BREAK_MS/1000UL));
}

void loop() {
  // Report unusually long loop gaps to distinguish network stalls from visual hiccups.
  static uint32_t previousLoopAt=0;
  uint32_t t=millis();
  if(previousLoopAt && t-previousLoopAt>250UL) {
    Serial.printf("[PERF] loop gap=%lums | heap=%u | minHeap=%u\n",
      (unsigned long)(t-previousLoopAt), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  }
  previousLoopAt=t;
  updateHeartRate();
  updateButtons();
  updateTouch();
  updatePomodoro();
  updateTune();
  updateFaceBehavior();
  updateSpeech();
  updateSerialCommands();
  updateCloud();
  updateScheduledAlarms();
  updateRing();
  updateOLED();
  updateSerial();
}


// ================ FIREBASE REALTIME DATABASE ================
// ESP32 uses its own anonymous device identity. No owner password on device.
Preferences cloudStorage;
String cloudRefresh, cloudIdToken, cloudUID;
uint32_t cloudTokenDeadline=0, cloudNextPull=0, cloudNextPush=0;
bool cloudConfigured=false;
uint32_t cloudLastAuthAttempt=0;

void initCloud(){
  cloudStorage.begin("mpcloud",false);
  String cached=cloudStorage.getString("sched_cache","");
  if(cached.length()) {
    JsonDocument localDoc;
    if(!deserializeJson(localDoc,cached)) parseScheduledAlarms(localDoc.as<JsonVariantConst>());
  }
  if(!strlen(WIFI_SSID)||!strlen(FB_API_KEY)||!strlen(FB_DATABASE_URL)) {
    Serial.println("[CLOUD] Offline mode; cached alarm schedules need synced clock to ring");return;
  }
  WiFi.mode(WIFI_STA);
  // Optional for mains-powered desk robots: reduces Wi-Fi power-save latency.
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  // WIB = UTC+7. ESP32 butuh sinkronisasi NTP sesudah terhubung Wi-Fi.
  configTzTime("WIB-7", "pool.ntp.org", "time.google.com");
  cloudRefresh=cloudStorage.getString("refresh","");
  cloudUID=cloudStorage.getString("uid","");
  cloudConfigured=true;
  cloudNextPull=millis()+2000;
  cloudNextPush=millis()+5000;
}

// Return true only after SNTP has set an approximately valid date.
// TLS CA certificate validation depends on the ESP32's system clock.
bool cloudClockReady() {
  return time(nullptr) > (time_t)1735689600; // after 2025-01-01 UTC
}

// Run only after a connection failure, at most once per minute.
// Distinguishes DNS failure, TCP reachability and a later TLS handshake problem.
void diagnoseTokenNetwork() {
  static uint32_t lastProbe = 0;
  const uint32_t now = millis();
  if(lastProbe && now-lastProbe < 60000UL) return;
  lastProbe = now;
  const char *host = "securetoken.googleapis.com";
  IPAddress ip;
  if(WiFi.hostByName(host, ip) != 1) {
    Serial.println("[NETDIAG] DNS securetoken.googleapis.com FAIL");
    return;
  }
  Serial.printf("[NETDIAG] DNS OK -> %s\n", ip.toString().c_str());
  WiFiClient socket;
  bool connected = socket.connect(ip, 443, 3000);
  Serial.printf("[NETDIAG] TCP 443 %s\n", connected ? "OK (TLS remains suspect)" : "FAIL");
  socket.stop();
}

// Google trust roots. CA PEM is in MindPal_Cloud.h; never disable TLS verification.
String firebaseCall(const String &url, const String &method, const String &payload, int &code){
  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CA);
  HTTPClient http;
  http.setConnectTimeout(8000);http.setTimeout(10000);
  if(!http.begin(client,url)){
    code=-1;
    Serial.println("[NET] http.begin failed (URL initialization, no HTTP response)");
    return "";
  }
  http.addHeader("Content-Type","application/json");
  code=method=="GET"?http.GET():method=="PUT"?http.PUT(payload):http.POST(payload);
  if (code<0) {
    char tlsError[128] = {0};
    int tlsCode = client.lastError(tlsError, sizeof(tlsError));
    Serial.printf("[NET] HTTP request failed: %d (%s) | TLS=%d (%s) | WiFi=%d RSSI=%d heap=%u\n",
      code,http.errorToString(code).c_str(),tlsCode,tlsError,
      (int)WiFi.status(),WiFi.RSSI(),ESP.getFreeHeap());
  }
  String body=code>0?http.getString():"";
  http.end();return body;
}

bool cloudToken(){
  if(!cloudConfigured||WiFi.status()!=WL_CONNECTED)return false;
  if(cloudIdToken.length() && (int32_t)(millis()-cloudTokenDeadline)<0)return true;
  // Secure TLS certificate validation requires a valid wall clock.
  if(!cloudClockReady()) {
    static uint32_t lastClockWarning = 0;
    if(!lastClockWarning || millis()-lastClockWarning >= 30000UL) {
      lastClockWarning=millis();
      Serial.printf("[NETDIAG] CLOCK NOT SYNCED epoch=%lld WiFi=%d; waiting for NTP\n",
        (long long)time(nullptr), (int)WiFi.status());
    }
    return false;
  }
  if(cloudLastAuthAttempt && millis()-cloudLastAuthAttempt<15000UL)return false;
  cloudLastAuthAttempt=millis();
  int code=0;String body;
  bool refreshing = cloudRefresh.length()>0;
  Serial.printf("[CLOUD] Auth %s | WiFi=%d RSSI=%d heap=%u epoch=%lld\n",
    refreshing?"refresh":"anonymous sign-in",(int)WiFi.status(),WiFi.RSSI(),ESP.getFreeHeap(),
    (long long)time(nullptr));
  if(cloudRefresh.length()){
    String url=String("https://securetoken.googleapis.com/v1/token?key=")+FB_API_KEY;
    // form URL encoded refresh token (tokens use URL-safe characters)
    WiFiClientSecure client;client.setCACert(FIREBASE_ROOT_CA);
    HTTPClient h;h.setConnectTimeout(8000);h.setTimeout(10000);
    if(!h.begin(client,url)) {
      Serial.println("[CLOUD] Refresh http.begin failed (no response)");
      return false;
    }
    h.addHeader("Content-Type","application/x-www-form-urlencoded");
    code=h.POST(String("grant_type=refresh_token&refresh_token=")+cloudRefresh);
    if(code<0) {
      char tlsError[128] = {0};
      int tlsCode = client.lastError(tlsError, sizeof(tlsError));
      Serial.printf("[CLOUD] Refresh transport: %d (%s) TLS=%d (%s)\n",
        code,h.errorToString(code).c_str(),tlsCode,tlsError);
    }
    body=code>0?h.getString():"";
    h.end();
    // Use a separate, plain TCP probe AFTER the failed HTTPS client is closed.
    if(code==-1) diagnoseTokenNetwork();
  } else {
    body=firebaseCall(String("https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=")+FB_API_KEY,
      "POST","{\"returnSecureToken\":true}",code);
  }
  if(code<200||code>=300){
    Serial.printf("[CLOUD] Auth %s failed: %d | WiFi=%d | heap=%u\n",
      refreshing?"refresh":"sign-in",code,(int)WiFi.status(),ESP.getFreeHeap());
    if(code==400 || code==401 || code==403)
      Serial.println("[CLOUD] Firebase rejected the token/request; check the response and device identity. Do not clear saved UID blindly.");
    return false;
  }
  JsonDocument doc;
  if(deserializeJson(doc,body))return false;
  cloudIdToken=doc["idToken"].is<const char*>() ? doc["idToken"].as<String>() : doc["id_token"].as<String>();
  cloudRefresh=doc["refreshToken"].is<const char*>() ? doc["refreshToken"].as<String>() : doc["refresh_token"].as<String>();
  String newUid=doc["localId"].is<const char*>() ? doc["localId"].as<String>() : doc["user_id"].as<String>();
  if(newUid.length())cloudUID=newUid;
  if(!cloudUID.length()||!cloudIdToken.length())return false;
  cloudStorage.putString("uid",cloudUID);cloudStorage.putString("refresh",cloudRefresh);
  int seconds=3600;
  if(!doc["expiresIn"].isNull())seconds=String(doc["expiresIn"].as<String>()).toInt();
  else if(!doc["expires_in"].isNull())seconds=String(doc["expires_in"].as<String>()).toInt();
  cloudTokenDeadline=millis()+((uint32_t)max(60,seconds-90))*1000UL;
  Serial.printf("[CLOUD] Device UID: %s\n",cloudUID.c_str());
  return true;
}

String dbUrl(const String &path){
  String url=FB_DATABASE_URL;
  if(url.endsWith("/"))url.remove(url.length()-1);
  return url+"/devices/mindpal/"+path+".json?auth="+cloudIdToken;
}

// RTDB typically returns JSON arrays, but can return sparse numeric-key objects.
// Explicit empty/null/missing values always mean zero messages (no fallback).
void appendCloudMessage(JsonVariantConst val, uint8_t mode) {
  if (cloudMessageCount[mode] >= 12 || !val.is<const char*>()) return;
  String msg = val.as<String>();
  msg.trim();
  if (msg.length()==0) return;
  msg.toCharArray(cloudMessages[mode][cloudMessageCount[mode]], 76);
  cloudMessageCount[mode]++;
}
void copyMessages(JsonVariantConst array, uint8_t mode){
  cloudMessageCount[mode] = 0;
  if (array.is<JsonArrayConst>()) {
    for (JsonVariantConst val : array.as<JsonArrayConst>()) appendCloudMessage(val,mode);
  } else if (array.is<JsonObjectConst>()) {
    // Support indexed-object form; ignore metadata and nonnumeric keys.
    JsonObjectConst obj = array.as<JsonObjectConst>();
    for (uint8_t index=0; index<12; ++index) {
      char key[5]; snprintf(key,sizeof(key),"%u",(unsigned)index);
      JsonVariantConst val = obj[key];
      if (!val.isNull()) appendCloudMessage(val,mode);
    }
  }
}

// Fingerprint the *effective* spoken content and timing, not LED / alarms / game.
// Unrelated Firebase edits must not restart the speech timer.
uint32_t speechFingerprint(){
  uint32_t h = 2166136261UL;
  auto mix = [&](uint8_t c){h = (h ^ c) * 16777619UL;};
  uint8_t i = (uint8_t)activeSpeechMode;
  const SpeechConfig &c = speechConfig[i];
  mix(c.textEnabled); mix(c.voiceEnabled);
  for (uint8_t k=0;k<4;k++) {mix((c.intervalMinMs>>(k*8))&255); mix((c.intervalMaxMs>>(k*8))&255);}
  mix(cloudMessageCount[i]);
  for (uint8_t j=0;j<cloudMessageCount[i];j++) {
    const char *p=cloudMessages[i][j];
    while (*p) mix((uint8_t)*p++);
    mix(0);
  }
  return h;
}

void applyCloudConfig(JsonVariantConst cfg){
  if(cfg.isNull()) return;
  uint32_t previousSpeech = speechFingerprint();
  int mood=cfg["mood"]|2;currentMood=constrain(mood,0,4);
  if(!silentLocalOverride) silentMode=cfg["silent"]|silentMode;
  if(silentMode){noTone(BUZZER_PIN);stopTune();speechVoiceRunning=false;}
  gameTarget=constrain((int)(cfg["gameTarget"]|30),1,10000);
  String effect=cfg["led"]["effect"]|"auto";
  ringEffect=effect=="solid"?1:effect=="breathe"?2:effect=="rainbow"?3:effect=="sparkle"?4:effect=="off"?5:0;
  String col=cfg["led"]["color"]|"#40A8FF";
  if(col.startsWith("#"))col.remove(0,1);
  ringCustomColor=strtoul(col.c_str(),nullptr,16);
  alarmStart=constrain((int)(cfg["alarms"]["start"]|0),0,3);
  alarmFinish=constrain((int)(cfg["alarms"]["finish"]|1),0,3);
  alarmBreak=constrain((int)(cfg["alarms"]["break"]|2),0,3);
  JsonVariantConst alarmList = cfg["scheduledAlarms"];
  if (!alarmList.isNull()) {
    String json; serializeJson(alarmList,json);
    if (json.length()<3600 && json!=cloudStorage.getString("sched_cache","")) {
      cloudStorage.putString("sched_cache",json);
      parseScheduledAlarms(alarmList);
    }
  }
  const char *labels[]={"idle","focus","break"};
  for(uint8_t i=0;i<3;i++) {
    JsonVariantConst section=cfg["speech"][labels[i]];
    if(!section.isNull()) {
      speechConfig[i].textEnabled=section["textEnabled"]|speechConfig[i].textEnabled;
      speechConfig[i].voiceEnabled=section["voiceEnabled"]|speechConfig[i].voiceEnabled;
      uint32_t minS=constrain((uint32_t)(section["minSec"]|10UL),3UL,1800UL);
      uint32_t maxS=constrain((uint32_t)(section["maxSec"]|15UL),minS,1800UL);
      speechConfig[i].intervalMinMs=minS*1000UL;
      speechConfig[i].intervalMaxMs=maxS*1000UL;
    }
    // Always clear if missing or empty, so deleted words cannot return.
    copyMessages(section["messages"],i);
  }
  String idleSource=cfg["speech"]["idle"]["source"]|"mood";
  useMoodMessagesForIdle = (idleSource != "general");
  JsonVariantConst moodSet=cfg["moods"][moodNames[currentMood]];
  if(useMoodMessagesForIdle) {
    copyMessages(moodSet["messages"],0);
  }
  if(!moodSet.isNull()) {
    String color=moodSet["color"]|"#38BDF8";
    if(color.startsWith("#"))color.remove(0,1);
    currentMoodColor=strtoul(color.c_str(),nullptr,16);
  }
  uint32_t effectiveSpeech = speechFingerprint();
  if(previousSpeech != effectiveSpeech) {
    cancelSpeech();
    scheduleNextSpeech();
    Serial.printf("[SPEECH] Effective config changed: IDLE=%s, count=%u, next in %lus\n",
      useMoodMessagesForIdle ? "MOOD" : "GENERAL",
      (unsigned)cloudMessageCount[(uint8_t)activeSpeechMode],
      (unsigned long)((nextSpeechAt-millis())/1000UL));
  }
  Serial.printf("[CLOUD] Config changed. Mood=%s, target=%u (speech timer preserved if unchanged)\n",moodNames[currentMood],gameTarget);
  printSpeechDiagnostics();
}

// Four short original chimes, selectable from dashboard
const Note ALARM_SOFT[]={{523,160},{659,160},{784,240}};
const Note ALARM_BRIGHT[]={{784,110},{988,120},{1175,250}};
const Note ALARM_COZY[]={{523,250},{659,250},{784,320},{659,230},{523,480}};
const Note ALARM_VICTORY[]={{523,170},{784,180},{988,220},{784,180},{1175,480}};
void playSelectedAlarm(uint8_t which){
  if(which==0)playTune(START_CHIME,sizeof(START_CHIME)/sizeof(Note));
  else if(which==1)playTune(FINISH_TUNE,sizeof(FINISH_TUNE)/sizeof(Note));
  else if(which==2)playTune(ALARM_SOFT,sizeof(ALARM_SOFT)/sizeof(Note));
  else if(which==3)playTune(ALARM_BRIGHT,sizeof(ALARM_BRIGHT)/sizeof(Note));
  else if(which==4)playTune(ALARM_COZY,sizeof(ALARM_COZY)/sizeof(Note));
  else playTune(ALARM_VICTORY,sizeof(ALARM_VICTORY)/sizeof(Note));
}

// Cloud networking runs in a dedicated FreeRTOS task on core 0.
// ONLY loop() may modify face, speech, alarms, game or Pomodoro state.
// The background task exchanges JSON strings with loop() under a mutex.
SemaphoreHandle_t cloudMutex = nullptr;
TaskHandle_t cloudTaskHandle = nullptr;
String cloudIncomingConfig, cloudIncomingReset, cloudOutgoingStatus;
bool cloudConfigPending = false, cloudResetPending = false;
uint32_t cloudLastStatusQueued = 0;

uint32_t jsonFingerprint(const String &s){
  uint32_t h=2166136261UL;
  for(size_t i=0;i<s.length();i++)h=(h^(uint8_t)s[i])*16777619UL;
  return h;
}

void cloudWorkerTask(void *){
  uint32_t lastConfigHash=0, lastResetHash=0;
  size_t lastConfigSize=0, lastResetSize=0;
  bool haveConfig=false, haveReset=false;
  uint32_t nextPull=millis()+1000UL, nextPush=millis()+4000UL, retry=0;
  for(;;) {
    if(WiFi.status()!=WL_CONNECTED){
      if(millis()-retry>=20000UL){retry=millis();WiFi.reconnect();}
      vTaskDelay(pdMS_TO_TICKS(250));continue;
    }
    if(!cloudToken()){vTaskDelay(pdMS_TO_TICKS(250));continue;}
    uint32_t now=millis(); int code=0;
    if((int32_t)(now-nextPull)>=0) {
      nextPull=now+10000UL;
      String json=firebaseCall(dbUrl("config"),"GET","",code);
      if(code==200 && json.length()<32000) {
        uint32_t h=jsonFingerprint(json);
        if(!haveConfig || h!=lastConfigHash || json.length()!=lastConfigSize) {
          if(xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(100))==pdTRUE) {
            cloudIncomingConfig=json;cloudConfigPending=true;
            xSemaphoreGive(cloudMutex);
            haveConfig=true;lastConfigHash=h;lastConfigSize=json.length();
          }
        }
      } else if(code!=200) Serial.printf("[CLOUD] Config fetch failed: %d\n",code);
      String cmd=firebaseCall(dbUrl("commands/resetGame"),"GET","",code);
      if(code==200 && cmd.length()) {
        uint32_t h=jsonFingerprint(cmd);
        if(!haveReset || h!=lastResetHash || cmd.length()!=lastResetSize) {
          if(xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(100))==pdTRUE) {
            cloudIncomingReset=cmd;cloudResetPending=true;
            xSemaphoreGive(cloudMutex);
            haveReset=true;lastResetHash=h;lastResetSize=cmd.length();
          }
        }
      }
    }
    if((int32_t)(now-nextPush)>=0) {
      nextPush=now+15000UL;
      String payload;
      if(xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(100))==pdTRUE) {
        payload=cloudOutgoingStatus;xSemaphoreGive(cloudMutex);
      }
      if(payload.length()) {
        firebaseCall(dbUrl("status"),"PUT",payload,code);
        if(code!=200) Serial.printf("[CLOUD] Status upload HTTP %d\n",code);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void startCloudWorker(){
  if(!cloudConfigured || cloudTaskHandle) return;
  cloudMutex=xSemaphoreCreateMutex();
  if(!cloudMutex){Serial.println("[CLOUD] Unable to create mutex");return;}
  BaseType_t result=xTaskCreatePinnedToCore(cloudWorkerTask,"mindpal-cloud",16384,nullptr,1,&cloudTaskHandle,0);
  if(result!=pdPASS){
    Serial.println("[CLOUD] Unable to create worker task");
    cloudTaskHandle=nullptr;
    vSemaphoreDelete(cloudMutex);cloudMutex=nullptr;
  } else Serial.println("[CLOUD] Async worker started (core 0)");
}

void updateCloud(){
  if(!cloudConfigured || !cloudMutex) return;
  String incoming, reset;
  if(xSemaphoreTake(cloudMutex,0)==pdTRUE){
    if(cloudConfigPending){incoming=cloudIncomingConfig;cloudConfigPending=false;}
    if(cloudResetPending){reset=cloudIncomingReset;cloudResetPending=false;}
    xSemaphoreGive(cloudMutex);
  }
  if(incoming.length()) {
    JsonDocument doc;
    DeserializationError err=deserializeJson(doc,incoming);
    if(!err)applyCloudConfig(doc.as<JsonVariantConst>());
    else Serial.printf("[CLOUD] Config JSON invalid: %s\n",err.c_str());
  }
  if(reset.length()) {
    JsonDocument doc;
    if(!deserializeJson(doc,reset)){
      uint32_t revision=doc["revision"]|0UL;
      uint32_t ack=cloudStorage.getUInt("reset_rev",0);
      if(revision>ack){
        gameBest=0;gameCount=0;
        if(storageReady){storage.putUInt("game_best",0);storage.putUInt("game_cnt",0);}
        cloudStorage.putUInt("reset_rev",revision);
        Serial.println("[GAME] Reset from dashboard");
      }
    }
  }
  uint32_t now=millis();
  if(now-cloudLastStatusQueued>=15000UL || !cloudLastStatusQueued){
    cloudLastStatusQueued=now;
    JsonDocument doc;
    doc["online"]=true;doc["uptimeMs"]=now;
    doc["gameCount"]=gameCount;doc["gameBest"]=gameBest;
    doc["focusSeconds"]=savedFocusSeconds;doc["cycles"]=completedCycles;
    doc["bpm"]=avgBPM;doc["silent"]=silentMode;
    doc["gameMode"]=gameMode;doc["boxBreathing"]=boxBreathingActive;
    doc["clockSynced"]=(time(nullptr)>1700000000);
    doc["alarmRinging"]=scheduledAlarmActive;
    doc["scheduledAlarmCount"]=(unsigned)scheduledAlarmCount;
    if(scheduledAlarmActive && scheduledAlarmIndex>=0)
      doc["alarmLabel"]=scheduledAlarms[scheduledAlarmIndex].label;
    doc["phase"]=boxBreathingActive?"BREATH":gameMode?"GAME":inFocus()?"FOCUS":inBreak()?"BREAK":"IDLE";
    doc["updatedAt"][".sv"]="timestamp";
    String payload;serializeJson(doc,payload);
    if(xSemaphoreTake(cloudMutex,pdMS_TO_TICKS(5))==pdTRUE){
      cloudOutgoingStatus=payload;xSemaphoreGive(cloudMutex);
    }
  }
}

// ================= SCHEDULED ALARM CLOCK (WIB) =================
void parseScheduledAlarms(JsonVariantConst obj) {
  if(!obj.is<JsonObjectConst>()) return;
  if(scheduledAlarmActive) stopScheduledAlarm(false);
  // Indeks snooze lama bisa bergeser setelah pengguna menghapus/mengurutkan alarm.
  snoozeAlarmIndex=-1;snoozeUntilEpoch=0;
  ScheduledAlarm old[MAX_SCHEDULED_ALARMS];
  uint8_t oldCount=scheduledAlarmCount;
  for(uint8_t i=0;i<oldCount;i++) old[i]=scheduledAlarms[i];
  scheduledAlarmCount=0;
  for(JsonPairConst item : obj.as<JsonObjectConst>()) {
    if(scheduledAlarmCount>=MAX_SCHEDULED_ALARMS)break;
    JsonVariantConst v=item.value();
    if(!v.is<JsonObjectConst>())continue;
    ScheduledAlarm &a=scheduledAlarms[scheduledAlarmCount];
    a=ScheduledAlarm{};
    String(item.key().c_str()).toCharArray(a.id,sizeof(a.id));
    String(v["time"]|"07:00").toCharArray(a.time,sizeof(a.time));
    String(v["date"]|"").toCharArray(a.date,sizeof(a.date));
    String(v["label"]|"Alarm").toCharArray(a.label,sizeof(a.label));
    String(v["message"]|"Time to go!").toCharArray(a.message,sizeof(a.message));
    a.enabled=v["enabled"]|false;
    a.once=String(v["repeat"]|"weekly")=="once";
    a.tone=constrain((int)(v["tone"]|0),0,5);
    a.snoozeMin=constrain((int)(v["snoozeMin"]|5),1,10);
    if(!a.once && v["weekdays"].is<JsonArrayConst>()) {
      for(JsonVariantConst day:v["weekdays"].as<JsonArrayConst>()){
        int d=day.as<int>();if(d>=0&&d<=6)a.daysMask|=(1U<<d);
      }
    }
    for(uint8_t j=0;j<oldCount;j++) {
      if(strcmp(a.id,old[j].id)==0){a.lastFiredMinute=old[j].lastFiredMinute;break;}
    }
    scheduledAlarmCount++;
  }
  Serial.printf("[ALARM] %u schedules loaded\n",(unsigned)scheduledAlarmCount);
}

void startScheduledAlarm(uint8_t i) {
  if(i>=scheduledAlarmCount || scheduledAlarmActive)return;
  scheduledAlarmActive=true;
  scheduledAlarmIndex=i;
  scheduledAlarmStartedAt=millis();
  scheduledAlarmLastTuneAt=0;
  cancelSpeech();
  playScheduledAlarmTone(scheduledAlarms[i].tone);
  scheduledAlarmLastTuneAt=millis();
  Serial.printf("[ALARM] Ringing %s at %s\n",scheduledAlarms[i].label,scheduledAlarms[i].time);
}

void stopScheduledAlarm(bool snooze) {
  if(!scheduledAlarmActive)return;
  if(snooze && scheduledAlarmIndex>=0) {
    time_t now=time(nullptr);
    if(now>1700000000){
      snoozeAlarmIndex=scheduledAlarmIndex;
      snoozeUntilEpoch=now+(time_t)scheduledAlarms[scheduledAlarmIndex].snoozeMin*60;
    }
  } else {snoozeAlarmIndex=-1;snoozeUntilEpoch=0;}
  scheduledAlarmActive=false;
  scheduledAlarmIndex=-1;
  stopTune();
  scheduleNextSpeech();
}

void updateScheduledAlarms(){
  static uint32_t lastCheck=0;
  uint32_t ms=millis();
  if(ms-lastCheck<950UL)return;
  lastCheck=ms;
  time_t now=time(nullptr);
  // ESP32 reboot tanpa NTP belum mengetahui tanggal/jam yang benar.
  if(now<1700000000)return;
  if(scheduledAlarmActive){
    if(ms-scheduledAlarmStartedAt>=60000UL){stopScheduledAlarm(false);return;}
    if(ms-scheduledAlarmLastTuneAt>=8500UL){
      playScheduledAlarmTone(scheduledAlarms[scheduledAlarmIndex].tone);
      scheduledAlarmLastTuneAt=millis();
    }
    return;
  }
  if(snoozeAlarmIndex>=0 && now>=snoozeUntilEpoch){
    uint8_t index=snoozeAlarmIndex;snoozeAlarmIndex=-1;snoozeUntilEpoch=0;
    startScheduledAlarm(index);return;
  }
  struct tm local{};
  localtime_r(&now,&local);
  char today[11];
  strftime(today,sizeof(today),"%Y-%m-%d",&local);
  for(uint8_t i=0;i<scheduledAlarmCount;i++) {
    ScheduledAlarm &a=scheduledAlarms[i];
    if(!a.enabled || strlen(a.time)!=5)continue;
    int hour=-1,minute=-1;
    if(sscanf(a.time,"%d:%d",&hour,&minute)!=2)continue;
    if(hour!=local.tm_hour||minute!=local.tm_min)continue;
    if(a.once ? strcmp(today,a.date)!=0 : !(a.daysMask&(1U<<local.tm_wday)))continue;
    time_t marker=now/60;
    if(a.lastFiredMinute==marker)continue;
    a.lastFiredMinute=marker;
    startScheduledAlarm(i);
    return;
  }
}

void drawScheduledAlarmScreen() {
  if(scheduledAlarmIndex<0||scheduledAlarmIndex>=scheduledAlarmCount)return;
  const ScheduledAlarm &a=scheduledAlarms[scheduledAlarmIndex];
  display.setTextSize(1);
  display.setCursor(1,0);
  display.print(a.label);
  display.setTextSize(2);
  display.setCursor(33,11);
  display.print(a.time);
  display.setTextSize(1);
  // Pesan singkat dapat tampil hingga dua baris dalam 128px.
  String msg(a.message);
  if(msg.length()>20){
    int split=msg.lastIndexOf(' ',20);
    if(split<6)split=20;
    display.setCursor(2,33);display.print(msg.substring(0,split));
    display.setCursor(2,43);display.print(msg.substring(split+1,split+21));
  }else{display.setCursor(2,39);display.print(msg);}
  display.drawLine(0,54,127,54,SSD1306_WHITE);
  display.setCursor(1,56);display.print("33:Snooze 32:Stop");
}

// Audio untuk alarm waktu yang disetel pengguna (berbeda dari jingle Pomodoro).
const Note CLOCK_DUN[]={{392,180},{0,60},{392,180},{0,60},{784,420}};
const Note CLOCK_MORNING[]={{523,200},{0,60},{659,200},{0,60},{784,240},{0,60},{988,360}};
const Note CLOCK_SOFT[]={{440,240},{0,60},{523,240},{0,60},{659,260},{0,60},{784,400}};
const Note CLOCK_BRIGHT[]={{784,160},{0,60},{988,160},{0,60},{1175,220},{0,60},{1568,400}};
const Note CLOCK_COZY[]={{523,250},{0,60},{659,250},{0,60},{784,320},{0,60},{659,230},{0,60},{523,480}};
const Note CLOCK_VICTORY[]={{523,170},{0,60},{784,180},{0,60},{988,220},{0,60},{784,180},{0,60},{1175,480}};
void playScheduledAlarmTone(uint8_t which){
  switch(which){
    case 0:playTune(CLOCK_DUN,sizeof(CLOCK_DUN)/sizeof(Note));break;
    case 1:playTune(CLOCK_MORNING,sizeof(CLOCK_MORNING)/sizeof(Note));break;
    case 2:playTune(CLOCK_SOFT,sizeof(CLOCK_SOFT)/sizeof(Note));break;
    case 3:playTune(CLOCK_BRIGHT,sizeof(CLOCK_BRIGHT)/sizeof(Note));break;
    case 4:playTune(CLOCK_COZY,sizeof(CLOCK_COZY)/sizeof(Note));break;
    default:playTune(CLOCK_VICTORY,sizeof(CLOCK_VICTORY)/sizeof(Note));break;
  }
}
