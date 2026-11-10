# MindPal v9 — Design and Firmware Notes for CLI AI Coding Agents

## Baseline

- Branch/version: v9 based on working v8 Alarm Clock.
- ESP32-WROVER, OLED SSD1306 128×64 I2C SDA21/SCL22, MAX30102 on same I2C, TTP223 IO27, WS2812 DIN25, buzzer module IO26, buttons GPIO33 and GPIO32 grounded using INPUT_PULLUP.
- Firmware: `firmware/MindPal_v9_BoxBreathing/MindPal_v9_BoxBreathing.ino`; cloud header next to it.
- React/Vite Firebase dashboard under `web/` is unchanged from v8.
- Firebase status path remains `/devices/mindpal/status`; new fields `boxBreathing: bool` and `phase: "BREATH"` when breathing; updates still approximately every 15 sec online.
- Firebase Rules owner/device model unchanged; no new database permissions.

## Button input arbitration (CRITICAL)

Single-button 2-second holds and dual-button 5-second hold must not conflict.

1. `updateButtons` debounces GPIO33 and GPIO32 independently (40ms).
2. A dual hold starts when BOTH debounced inputs are LOW. A combo guard suppresses all single-button short/long actions until BOTH buttons are released.
3. After 5000 ms of uninterrupted simultaneous holding, toggle box breathing ON/OFF once. Continue to ignore subsequent hold actions until both released.
4. A combo released early must not trigger short click, silent toggle, start/stop Pomodoro or game mode.
5. If a scheduled alarm is ringing, existing alarm actions take priority: GPIO33 snooze, GPIO32 dismiss. To avoid mis-actions, combo guard remains until both released.
6. In breath mode, single-button taps/holds are ignored; only another dual hold exits. Pomodoro time continues; MAX30102 sampling continues. Alarm clocks can interrupt visually and audibly.

## UI/animation priority

Priority: alarm scheduled > dual hold progress > just-completed hold acknowledgement > box breathing > single hold progress > sound/game notice > game celebration > game face + counter > Pomodoro celebration > face/timer/PPG/speech.

- Hold progress: shared 60-segment circular ring with central action name and percentage, duration 2 seconds or 5 seconds.
- Box breathing: 4 phases of 4000ms each (`BREATH_PHASE_MS`), repeating indefinitely, phase based on elapsed millis. Left 49px rounded box, dot traces its 4 sides. Cute orb (large/small animated with smoothstep) indicates breath. Right text: BREATHE IN / HOLD / BREATHE OUT / HOLD, second countdown 4..1, round count. No `delay()`.
- LED breathing: gentle gradient (cyan/teal/violet/blue), brightness follows orb, while WS2812 power remains protected with existing brightness cap.
- Silent mode: full screen `SILENT` or `SOUND ON` for 1.7s with speaker icon; `silentMode` persisted in NVS. Silent applies to chimes/speech; alarm visual still works.
- Game enter/exit: full-screen messages for 1.45s; game counter bottom has NO horizontal line (a small black background patch to maintain readable text).
- Fun Clicker tap: sparkling eyes/ambient stars and one of four positive expressions. Achievement at every multiple of `gameTarget` produces continuous rainbow+sparkles plus FINISH_TUNE (silent-respecting) for ~2.6s.
- Pomo Fireworks uses the same celebration LED renderer; it continues during normal mode.

## Existing systems / things not to break

- Pomodoro test mode: focus 30s, short break 12s, long break 20s. Switch `TEST_MODE=false` for 25/5/15min.
- Preferences NVS statistics: `focus_s`, `cycles`, `game_cnt`, `game_best`, `silent`.
- Face geometry and easing in `drawFace` remain unchanged; no sudden rigid face swaps.
- TTP223 and MAX30102 available even when Pomodoro running. Touch game increments are suppressed while breathing.
- Firebase HTTPS polling may briefly stall animation (preexisting limitation); avoid blocking calls if later migrating to async transport.
- Cloud alarm schedule and owner/device security rules remain untouched.

## Future improvements

- Dashboard customization: breath phase seconds, optional completion length, LED breathing palette, and quick-start remote command. These are NOT included in v9.
- Dedicated event journal/sync for game/focus events vs periodic status snapshots.
- Optional next step: unit tests of timer rollover and interrupted power, actual ESP32 build, visual footage inspection.
