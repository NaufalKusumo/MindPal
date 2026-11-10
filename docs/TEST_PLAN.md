# Test plan for MindPal v7

## Hardware baseline
- [ ] OLED and MAX30102 detected at 0x3C (or your OLED address) and 0x57; I2C SDA21 SCL22.
- [ ] Touch press prints `[TOUCH]` and changes facial reaction.
- [ ] New button GPIO32 short click toggles silent; hold 2s enters/exits game.
- [ ] In game, tap TTP223 rapidly: count increments exactly once per down-edge, goal celebration triggers when multiple of N.
- [ ] Primary button GPIO33 still starts/stops Pomodoro by long hold; short click shows timer or totals.
- [ ] Muted state prevents chimes and vowel beeps; LED/OLED continue working.
- [ ] WS2812 uses adequate regulated 5V, common ground, appropriate data level shifting.
- [ ] No overheating or unexplained resets during 16-LED rainbow/sparkle.

## OLED
- [ ] Original cute face and v6 smooth morphs remain.
- [ ] Game face retains counter in bottom strip, game celebrations are visibly animated.
- [ ] Normal Pomodoro progress bar stays horizontal along bottom.
- [ ] Long messages wrap to 2-3 lines as in v6.
- [ ] MAX30102 takeover screen and BPM remain functional when not in game.

## Cloud and dashboard
- [ ] Owner login succeeds, stranger/non-owner cannot read or write.
- [ ] Anonymous device UID appears in Serial, rules published with actual OWNER_UID and DEVICE_UID.
- [ ] Config change on web appears on device after next 10s poll.
- [ ] Device status refreshes within 15s; web shows online if updatedAt recent.
- [ ] Mood prompt appears once per Asia/Jakarta day for given browser and account.
- [ ] Each mood's word list used in IDLE, focus/break use their own lists.
- [ ] Game score best persists through ESP32 reboot and appears in web status.
- [ ] Reset command clears game score on the device after next config poll.
- [ ] Wi-Fi offline: Pomodoro/game still work; on reconnect config and status resume.
- [ ] Inspect Serial for TLS/HTTP errors, prevent repeated auth loops.

## Future enhancements
- [ ] Cloud HTTP in low-priority FreeRTOS task, never block animations.
- [ ] Offline stats/event queue; daily history and analytics per date.
- [ ] Explicit `revision` + `ack` for config; physical vs web silent precedence.
- [ ] Persist latest cloud config locally for offline startup.
- [ ] Display custom alarm text at alarm transitions.
- [ ] Enforce schema/range in Firebase Rules (not only device validation).
