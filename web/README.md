# MindPal v8 Dashboard

Run with `npm install` then `npm run dev` after copying the **existing, working** `.env` from the v7 dashboard into this `web/` directory.

The **Alarms & sounds** menu is now a full multi-alarm manager: add, edit, delete, enable/disable, set 24-hour time, weekdays or one date, message, one of six buzzer melodies, and snooze duration. Alarm edits save automatically to Firebase under `devices/mindpal/config/scheduledAlarms`. Existing Pomodoro chimes remain independent.

Hardware ringing requires the v8 firmware, powered ESP32, valid NTP-synchronized clock (WIB), and downloaded alarm configuration. Browser preview sounds do not imply reliable browser background alarms.

See `../docs/ALARM_SETUP_ID.md` and `../docs/ALARM_SPEC.md`.
