# Changelog v9

- Centralized debounced button state machine with combo guard (GPIO33+GPIO32 5s).
- Box breathing indefinitely looping 4/4/4/4 with OLED cute orb/square tracer and WS2812 breathing effect; toggle OFF by another dual hold.
- Unified circular progress visuals for BOTH individual 2s holds and joint 5s hold.
- Fullscreen confirmation for Silent/Normal, Fun Clicker on/off.
- Removed full-width line from Fun Clicker counter; added tap sparkle and ensured game reactions are all friendly.
- Updated celebration LEDs with rainbow + sparkle consistent for game milestones and completed Pomodoro sessions.
- Cloud status fields `boxBreathing` and `phase="BREATH"`, while retaining existing config and security rules.
- Left web dashboard, Firebase rules and scheduled alarm editor untouched.
