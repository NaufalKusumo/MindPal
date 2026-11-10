# MindPal v8 — Scheduled Alarm Feature Specification (for coding agents)

## Objective
Replace the v7 static 3-chime alarm page with a phone-like multiple alarm manager. Preserve existing Pomodoro chimes as separate controls. No extra hardware other than existing ESP32-WROVER, OLED, WS2812, TTP223, MAX30102, buzzer and two buttons.

## Technology
- Web: React 19 + Vite 6 + Firebase Auth (Email/Password) + Firebase Realtime Database.
- Firmware: Arduino-ESP32, ArduinoJson v7, HTTPS Firebase RTDB polling, ESP32 NTP `configTzTime("WIB-7",...)`.
- Time zone: `Asia/Jakarta` / WIB, UTC+07:00, no DST. Client dates/times should be interpreted as WIB, not viewer-local timezone.

## Data contract
Path: `devices/mindpal/config/scheduledAlarms` (part of existing owner-write/device-read `config`). An `_version: 1` marker keeps the node present when the list is empty. Alarm keys are stable IDs.

```json
{
  "_version": 1,
  "aexample1": {
    "id": "aexample1",
    "label": "Bangun pagi",
    "time": "07:00",
    "repeat": "weekly",
    "weekdays": [1,2,3,4,5],
    "date": "2026-11-11",
    "message": "Good morning!",
    "tone": 1,
    "enabled": true,
    "snoozeMin": 5
  },
  "aexample2": {
    "id": "aexample2",
    "label": "Meeting",
    "time": "15:30",
    "repeat": "once",
    "weekdays": [],
    "date": "2026-11-11",
    "message": "Ready for your meeting?",
    "tone": 4,
    "enabled": true,
    "snoozeMin": 1
  }
}
```
- `weekdays`: 0=Sunday, 1=Monday, ...6=Saturday. For `once`, match `date` only; for `weekly`, match `weekdays` only.
- Max 12 alarms, label <=36 chars, message <=75 chars, 6 tone IDs (0..5), 1/5/10 minute snooze.
- `alarms` (existing field) still controls Pomodoro `start`, `finish`, `break`; do NOT overwrite its legacy contract.
- RTDB rules unchanged when existing owner/device access to `config` is correct. Never grant broad public writes.
- UI auto-saves alarm create/update/delete/toggle through existing `persist()` call; `onValue` merges `scheduledAlarms` with defaults.

## Trigger behavior
- Device syncs schedules from config; caches last downloaded schedule JSON in NVS (config must be <=3600 chars for this local cache).
- Device checks `time(nullptr)` every ~1 second; does nothing if epoch indicates NTP not synced.
- Match minute, weekday/date, enabled status. Avoid duplicate triggers within the same minute in memory.
- At trigger: alarm OLED overlay + synthetic melody, re-play melody every ~8.5 seconds for up to 60 seconds.
- While ringing: GPIO33 press snoozes for configured minutes, GPIO32 press dismisses. Silence button toggle does not occur on dismissal.
- `silentMode` suppresses audio but not the OLED alarm.
- Alarm schedule changes while currently ringing should dismiss the currently active overlay safely.
- Existing Pomodoro, face, clicker, Firebase Auth, telemetry, and mood messages remain intact.

## Out of scope and caveats
- True phone-level reliability, operation while powered off, RTC coin-cell backup, long music files (MP3/WAV), arbitrary uploaded music, web browser background notifications, reliable persistence of snooze across reboot, historical alarm logs, and multi-timezone/DST scheduling.
- Hardware compilation and board testing are required before production use.

## Important files
- `web/src/AlarmManager.jsx` — editor, listing, alarm previews, repeat dates.
- `web/src/alarms.css` — responsive styles.
- `web/src/main.jsx` — host app integration + Firebase persist.
- `firmware/MindPal_v8_Alarm_Clock/MindPal_v8_Alarm_Clock.ino` — scheduler, NTP integration, OLED, buzzer.
