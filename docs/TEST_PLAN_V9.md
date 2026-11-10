# MindPal v9 — Hardware Test Plan

Start with Serial Monitor 115200 baud, ESP32 Upload Speed 115200, both buttons connected to GND (INPUT_PULLUP).

## Steps

1. **Tap GPIO32**: OLED shows full-screen speaker icon + SILENT; buzzer no longer sounds. Tap again: SOUND ON.
2. **Hold GPIO32 2 seconds**: OLED circular progress. At 100%, Fun Clicker starts. Tap TTP223 3 times and confirm counter changes without horizontal divider and reactive cute faces/sparkles. Hold GPIO32 again 2 seconds to exit.
3. **GPIO33 hold 2 seconds**: existing circular progress and Pomodoro starts; timer overlay and progress at bottom face appear as before.
4. **Both buttons held simultaneously for 5 seconds**: OLED circular progress from 0 to 100%, no Pomodoro stop or Game toggle at 2 sec; then breathing screen (4/4/4/4) appears. LED ring pulses with phase. Test one complete 16-second round.
5. **Tap/hold only GPIO33 or GPIO32 while breathing**: should not stop Pomodoro, silent, or game. **Hold both for 5s**: breathing exits, Pomodoro resumes its normal display (time keeps advancing).
6. **Cancel combo**: hold both for 2 sec, release one, then the other. No unintended Pomodoro/Game/Silent action should occur.
7. **Game achievement**: enter game; send >= gameTarget TTP223 presses. Ring should display rotating rainbow with white sparkle and short tune if not silent; no footer line.
8. **Sensor**: cover MAX30102, check reading screen and return. During breath UI, breath overlay takes precedence (sensor still sampled in background).
9. **Firebase**: with valid config, verify `devices/mindpal/status/boxBreathing` and status `phase=BREATH` are sent when online. Existing scheduled alarms still snooze/dismiss with GPIO33/GPIO32.
10. **Restart / offline**: `MindPal_Cloud.h` may be blank; all local features should still work. Check saved focus totals and game best persist.

## Expected Serial lines

```text
[BUTTON] BOTH DOWN: hold 5s to toggle Box Breathing
[BREATH] START: 4s IN / 4s HOLD / 4s OUT / 4s HOLD
[BUTTON] BOTH RELEASED
[BREATH] STOP; previous mode resumes
[SOUND] SILENT
[SOUND] NORMAL
[GAME] START
[GAME] TAP count=1 | best=1
[GAME] TARGET! count=30
```

## Validation scope

This package was syntax-checked with g++ and Arduino API stubs and button sequences were simulated. Arduino IDE compile, physical display smoothness, power integrity and Bluetooth were **not** tested here.
