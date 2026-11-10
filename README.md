# MindPal v7 — Companion + Firebase Dashboard

An ESP32-WROVER desk companion with OLED 128x64, MAX30102 PPG, WS2812 ring, TTP223, passive buzzer, and **two physical buttons**. This is an extension of MindPal Pomodoro v6; the animated face system and Pomodoro remain in place.

## Project structure

- `firmware/MindPal_v7/MindPal_v7.ino` — ESP32 firmware; existing animated faces, Pomodoro, game mode, audible/quiet button, Firebase sync
- `firmware/MindPal_v7/MindPal_Cloud.h` — Wi-Fi/Firebase API settings and TLS trusted roots
- `web/` — Vite + React + Firebase Authentication + Realtime Database dashboard
- `database.rules.json` — restricted RTDB access (replace both UID placeholders)
- `docs/FIREBASE_SETUP.md` — setup from scratch
- `docs/PROJECT_SPEC.md` — system architecture / requirements, intended for coding agents
- `docs/TEST_PLAN.md` — integration and hardware tests

## Wiring

| Component | ESP32 | Notes |
|---|---|---|
| SSD1306 OLED SDA, MAX30102 SDA | GPIO21 | Shared I2C |
| SSD1306 OLED SCL, MAX30102 SCL | GPIO22 | Shared I2C |
| WS2812 ring DIN (verify your PCB's `D1` vs `D0` marking) | GPIO25 | 220Ω series resistor; proper 3.3→5 V buffer recommended |
| Passive buzzer module IO | GPIO26 | 3.3V module; ensure module driver compatibility |
| TTP223 IO | GPIO27 | TTP223 VCC to 3.3V |
| Pomodoro button | GPIO33 → GND | INPUT_PULLUP |
| **New mode button** | **GPIO32 → GND** | INPUT_PULLUP; only two opposite switch sides used |

Do not feed 5 V to any ESP32 GPIO. WS2812 ring uses separate adequate 5 V supply and common ground. Disconnect power before re-wiring.

## Hardware interactions

- Main button GPIO33: click to view statistics/timer; hold ≥2 s to start/stop Pomodoro.
- New button GPIO32: click to toggle SILENT vs NORMAL; hold ≥2 s to enter/exit Fun Clicker.
- Game mode: every *press edge* on the TTP223 scores one tap, changes facial reaction, and displays a small count below the face. At every configurable milestone, show sparkling celebration and play a short jingle unless silent.
- During game mode, all automatic messages are suspended. Live Pomodoro countdown continues if active (but is temporarily hidden by the game face).
- Focus test times: 30 s focus, 12 s short break, 20 s long break. Change `TEST_MODE` to `false` for real timing.
- Silent mode mutes chimes and synthesized vowel notes but never disables OLED or LED.
- Tap scores and best record persist in ESP32 NVS, and status is periodically sent to Firebase.

## Important compatibility notes

- Dashboard `Alarm words` are **saved only**, not yet shown by firmware alarm screens. Alarm *tones* do synchronize.
- The web displays current/lifetime Pomodoro totals; **daily historical time-series and per-day progress tracking are not implemented in this prototype**.
- Dashboard speech word lists support up to 12 messages per mode, 75 characters per message. OLED wraps lines using existing v6 renderer.
- Mood-specific word lists replace idle speech list when a configuration is downloaded. Mood color is stored in Firebase but the OLED is monochrome; LED auto effect uses the selected mood color while idle.
- Dashboard is **not deployed** and needs Firebase setup. Wi-Fi sync is periodic HTTPS (10 s config, 15 s telemetry) and can cause brief display pauses. Fully asynchronous cloud tasks are a future improvement.
- The anonymous device account must remain enabled in Firebase Auth; deleting that user requires re-pairing device (reset stored `mpcloud` namespace or reflash with NVS erase).
- Not a medical heart-rate monitor; BPM is an approximate recreational/educational PPG reading.

## Getting started

Follow `docs/FIREBASE_SETUP.md`, then `docs/TEST_PLAN.md`. To test offline without Firebase, leave Cloud.h credentials empty; the firmware still runs locally.
