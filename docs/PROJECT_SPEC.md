> **MindPal v8 upgrade notice:** For scheduled multi-alarm behavior and data model, read `ALARM_SPEC.md`; it supersedes the v7 alarm section below.

# MindPal v7 — Technical Project Specification (AI Agent Context)

## Problem / purpose
MindPal is an interactive desk companion built for habit formation, focused study, emotionally adaptive messages, and occasional heart-rate checks. It is not a medical or mental-health diagnostic system. It runs on ESP32-WROVER with a small SSD1306 OLED 128x64 and a single 16-pixel WS2812 ring (configurable physical count).

## Existing hardware / pins
- ESP32-WROVER Dev
- SSD1306 128x64 OLED and MAX30102 on shared I2C bus SDA=GPIO21, SCL=GPIO22
- WS2812 ring input GPIO25 (5V ring power, 3.3-to-5V logic level-shifter recommended)
- passive buzzer module IO=GPIO26
- TTP223 capacitive touch IO=GPIO27
- primary tactile button GPIO33→GND with INPUT_PULLUP
- new secondary tactile button GPIO32→GND with INPUT_PULLUP
- Use stable upload baud 115200. All grounds common. Do not use flash/PSRAM/boot pins.

## Software architecture
1. Firmware `.ino`: modular nonblocking update functions (`updateHeartRate`, `updateTouch`, `updateButton`, `updateModeButton`, `updatePomodoro`, `updateTune`, `updateSpeech`, `updateFaceBehavior`, `updateRing`, `updateOLED`, `updateCloud`). `loop()` orchestrates all.
2. Character drawing: procedural, time-smoothed face morphs inherited from v6; cute expressive reference supplied by user (pixel face mood sheet). Use large friendly eyes, expressive mouth, consistent tweening/easing; prefer coherent face grammar over sharp static drawings.
3. Pomodoro state machine: STOPPED → FOCUS → FIREWORKS → HAPPY → BREAK → FOCUS; 4 focuses = long break. Primary button short shows stats/timer; hold toggles active/inactive. Default test 30 s, short break 12 s, long 20 s. Real 25/5/15 min when `TEST_MODE=false`.
4. Fun Clicker secondary mode: secondary button click toggles buzzer mute; 2 s hold toggles game. Touch PRESS edges increment game score exactly once per touch (TTP223 might output a continuous HIGH when held). Face reacts randomly, milestone celebration on every N clicks, screen bottom retains small counter, suppress all spontaneous messages while game active. Saved NVS high score and current count.
5. Firebase Auth: owner Web email/password; ESP32 distinct Firebase Anonymous Auth user. API key public; device refresh token in NVS; Google HTTPS via CA. No admin credential on ESP32.
6. Firebase RTDB paths: `/devices/mindpal/config` (owner writes, device reads), `/devices/mindpal/status` (device writes, owner reads), `/devices/mindpal/commands/resetGame` (owner writes, device reads). Rules scoped by exact two Firebase UIDs.
7. Vite React web client with Firebase modular JS SDK. Responsive dark dashboard. Real-time status subscription, configuration saved on demand, daily mood prompt based on Asia/Jakarta local day marker, voice/timing/messages per mode, custom notes selection, ring colors/effects, game milestone and reset.

## Config schema (see web defaults)
```
config:
  mood: 0..4                 # Great, Good, Neutral, Sad, Awful
  silent: boolean
  gameTarget: 1..10000
  moods:
    Great|Good|Neutral|Sad|Awful:
      color: '#RRGGBB'
      messages: [string,...] # up to 12, max 75 chars
  speech:
    idle|focus|break:
      textEnabled: boolean
      voiceEnabled: boolean
      minSec: 3..1800
      maxSec: 3..1800
      messages: [string,...]
  led:
    effect: auto|solid|breathe|rainbow|sparkle|off
    color: '#RRGGBB'
  alarms:
    start: 0..3
    finish: 0..3
    break: 0..3
    words: {start:string,finish:string,break:string} # stored, firmware display pending
status:
  online, uptimeMs, gameCount, gameBest,
  focusSeconds, cycles, bpm, silent, gameMode, phase,
  updatedAt: {'.sv':'timestamp'}
commands/resetGame:
  revision: integer Unix seconds; monotonically increasing
```

## UI requirements
- Owner authentication and automatic connection state
- Mood onboarding at first dashboard open per local Jakarta day, Great/Good/Neutral/Sad/Awful, editable phrases and color
- Speech settings for IDLE/FOCUS/BREAK independently: text enable, vowel voice enable, min/max interval, phrase lists
- Buzzer global silent toggle on physical GPIO32, and cloud `silent` config (physical switch has precedence until reboot, then cloud config takes over; future revisions should add explicit config acknowledgements)
- Alarms: select among four original chimes and preview via browser Web Audio. Custom alarm labels currently UI/database only.
- Ring: auto, solid, breathe, rainbow, sparkle, off; selectable RGB color; celebration animations override.
- Fun Clicker target per N taps, last count and best score, dashboard reset command.
- Future: daily focus aggregates; per-day best; multiple devices; offline write buffering; logged events; config version/ack; server-side validation; async cloud task; actual live mutable alarm phrases on OLED.

## Critical design constraints
- All OLED rendering is 1-bit monochrome; mood colors drive AUTO ring lighting in idle and web display, not OLED pixels.
- MAX30102 only estimates BPM; do not claim stress diagnosis.
- Do not block sensor processing for long cloud requests (prototype currently does briefly).
- Avoid GPIO conflicts and unsafe 5V wiring.
- Never print secrets in logs or put owner credentials in sketch.
- Arduino automatic prototypes can fail if type definitions follow methods; declare custom enums/structs before function definitions or explicit prototypes.
- Retain v6 smile/blink/morph behavior rather than replacing all faces with rigid symbols.
- Add features incrementally and test on physical OLED, especially word wrapping.

## Known limitations in this deliverable
- Cloud over periodic synchronous HTTPS can stutter animation. Upgrade to network task before final demo.
- Firebase can overwrite local silent state on each config polling cycle; decide whether physical switch or dashboard wins and add versioned bidirectional status/command protocol.
- `gameMode` masks Pomodoro OLED but does not pause its timer. Hold the secondary button to exit game if you want timer overlay again.
- The dashboard does not implement daily history or accounts beyond one owner/device pair.
- Visual references are not bitmaps and remain approximated by v6 procedural face, not exact reproduction.
