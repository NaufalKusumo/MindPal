# Firebase setup — step by step

## A. Create the Firebase project

1. Visit https://console.firebase.google.com and choose **Create a project**. Choose a project ID such as `mindpal-demo-...`. Analytics is optional.
2. In **Project settings → General → Your apps**, register a **Web app**. Copy the `firebaseConfig` fields (API key, auth domain, project ID, app ID).
3. In **Build → Authentication → Sign-in method**, enable **Email/Password** and **Anonymous**. These are two *different* identities: dashboard owner and ESP32 device.
4. In **Authentication → Users → Add user**, create your dashboard account. Keep the password private; never put it in firmware.
5. In **Build → Realtime Database**, create a database, choose an appropriate region (near Indonesia if available), and select **locked mode**. Copy the exact database URL, including any region suffix.

## B. Start the React dashboard locally

Use Node 20+:

```sh
cd web
cp .env.example .env
npm install
npm run dev
```

Fill `web/.env`:

```ini
VITE_FIREBASE_API_KEY=<apiKey from Firebase app settings>
VITE_FIREBASE_AUTH_DOMAIN=<authDomain>
VITE_FIREBASE_DATABASE_URL=<EXACT RTDB URL>
VITE_FIREBASE_PROJECT_ID=<projectId>
VITE_FIREBASE_APP_ID=<appId>
```

The Firebase web API key is a **project identifier** (not a privileged service-account secret). Security comes from Authentication and strict RTDB Rules. Never publish any *service account private key* or your private account password.

Open the Vite URL (normally http://localhost:5173) and sign in. Before adding rules you may see `PERMISSION_DENIED` / cannot save; that is expected. Confirm your owner UID in Firebase Console → Authentication → Users.

## C. First boot ESP32 / get DEVICE_UID

1. Install Arduino libraries: **Adafruit SSD1306**, **Adafruit GFX Library**, **Adafruit NeoPixel**, **SparkFun MAX3010x**, and **ArduinoJson version 7**. `WiFi`, `HTTPClient`, `Preferences` and TLS are included in ESP32 Arduino core.
2. Open `firmware/MindPal_v7/MindPal_v7.ino` in Arduino IDE (all firmware files must stay inside the `MindPal_v7` folder).
3. Edit adjacent `MindPal_Cloud.h`: set Wi-Fi SSID, Wi-Fi password, Firebase Web API key, and exact Realtime Database URL. **Do not add your dashboard account password.**
4. Use upload speed **115200** (previously stable on the WROVER board). Open Serial Monitor at 115200.
5. ESP32 signs in **anonymously** through Firebase Auth and prints `[CLOUD] Device UID: <some-id>`. Record this UID. If it does not appear, check credentials, internet, root CA, DNS and Authentication anonymous provider.

**Why credentials in Cloud.h?** ESP32 creates its own restricted anonymous Firebase user and retains only a refresh token in local NVS. Every database operation uses a short-lived Firebase ID token. It never uses owner credentials or an Admin SDK key.

## D. Lock down the database

1. Open `database.rules.json`.
2. Replace `OWNER_UID` with your dashboard Firebase Authentication UID.
3. Replace `DEVICE_UID` with the UID printed by the ESP32.
4. In Firebase Console → Realtime Database → Rules, paste the JSON and **Publish**.
5. Verify that dashboard can write `/devices/mindpal/config`; ESP32 can read it and write `/devices/mindpal/status`.

Never use global rules such as `".read": true` or `".write": true` for convenience. The provided rules deliberately limit each identity to only its intended paths.

## E. Configure your companion

1. Open the dashboard, sign in, and complete the mood check-in. Choose a mood color and edit that mood's phrases.
2. Under **Focus settings**, choose the text/voice switches, min/max seconds, and phrases for IDLE/FOCUS/BREAK.
3. Under **Fun Clicker**, set milestone size, save, then hold GPIO32 to enter the game. Tap TTP223; the counter should increase. Use Reset to send a command to ESP32.
4. Under **Alarms**, select start/finish/break tones and click **Preview** to play a browser approximation. Custom alarm text is stored for later firmware support.
5. Under **LED lighting**, choose auto, solid, breathe, rainbow, sparkle, or off, and set a color when appropriate.
6. Allow 10–15 seconds for config download/status upload, longer if Wi-Fi reconnects. Check Serial Monitor for `[CLOUD]` errors.

## F. Security & production notes

- Keep Wi-Fi password out of public repositories. Before pushing firmware, move these fields to a private `MindPal_Secrets.h` and add it to `.gitignore`.
- The included GTS Root R1 and ISRG Root X1 CA roots provide TLS verification for common Google HTTPS services; certificate chains can rotate. Revisit certificate trust configuration if TLS fails. **Do not use `setInsecure()` in a deployed product.**
- Anonymous Firebase device UID is saved to NVS. Removing the account in console breaks existing refresh tokens. Re-pair by clearing `mpcloud` NVS / resetting flash user storage and repeating setup.
- Device has write access to its own status only; the owner can edit config and reset commands. RTDB rules are not a full device attestation mechanism, so keep physical access to your ESP32 controlled.
- Wi-Fi polling uses synchronous HTTPS. This prototype prioritizes clarity; a production implementation should use an async network task, local write queue, retry/backoff, server-side validation, and firmware OTA strategy.
- If hosting publicly, use Firebase Hosting or another TLS-enabled host and register the domain in **Authentication → Settings → Authorized domains** as required.
