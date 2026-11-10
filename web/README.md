# MindPal Dashboard

React + Vite + Firebase Authentication (Email/Password) and Firebase Realtime Database.

```bash
cp .env.example .env
# Fill Firebase settings from project console
npm install
npm run dev
```

Start with `../docs/FIREBASE_SETUP.md`. This app doesn't create a Firebase project automatically and does not contain real credentials.

Current features: owner sign-in, daily mood check-in, mood-specific message editing, modes' phrase/timing/voice settings, Fun Clicker score/goal/reset, original alarm tone preview using Web Audio, selectable LED effects, device status. `Alarm words` are configuration-only until future firmware support.
