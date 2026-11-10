> **MindPal v8:** First follow `ALARM_SETUP_ID.md` when upgrading alarms. This document describes original v7 setup.

# Mulai dari nol — MindPal v7

## 1. Sambungan tombol baru
Matikan daya lebih dulu. Pasang tombol tactile **kedua** di breadboard, melintasi celah tengah. Satu kaki pada sisi A ke **GPIO32**, satu kaki dari sisi B ke **GND**. Tidak perlu resistor karena firmware memakai `INPUT_PULLUP`. Tombol Pomodoro tetap di GPIO33.

## 2. Buat Firebase
1. Buka https://console.firebase.google.com → Add project.
2. Authentication → Sign-in method → aktifkan **Email/Password** dan **Anonymous**.
3. Authentication → Users → Add user, buat akun khusus untuk login dashboard. Catat UID akun ini.
4. Realtime Database → Create database → pilih **locked mode** → salin **Database URL** persis yang ditampilkan.
5. Project Settings → Web app (`</>`) → Register app → salin `apiKey`, `authDomain`, `projectId`, `appId`.

## 3. Jalankan website
Di terminal Fedora/Linux:

```bash
cd web
cp .env.example .env
# Edit .env dengan nilai Firebase Web app + Database URL
npm install
npm run dev
```

Buka URL lokal yang muncul, kemudian login menggunakan akun Email/Password dari Firebase.

## 4. Siapkan ESP32
1. Arduino Library Manager: pastikan Adafruit SSD1306, Adafruit GFX, Adafruit NeoPixel, SparkFun MAX3010x dan **ArduinoJson v7** terinstal.
2. Buka `firmware/MindPal_v7/MindPal_v7.ino`.
3. Buka file `firmware/MindPal_v7/MindPal_Cloud.h` dan isi `WIFI_SSID`, `WIFI_PASSWORD`, `FB_API_KEY`, `FB_DATABASE_URL`. Jangan isi password login web.
4. Upload Speed `115200`; upload.
5. Serial Monitor 115200. Cari `[CLOUD] Device UID: ...`. Salin UID tersebut.

## 5. Terapkan Rules (PENTING)
Buka `database.rules.json`, ganti `OWNER_UID` dengan UID akun web dan `DEVICE_UID` dengan UID anonim yang dicetak ESP32. Salin seluruh rules ke Firebase Console → Realtime Database → Rules → Publish.

**Jangan** memakai aturan publik read/write untuk sementara sekalipun.

## 6. Uji integrasi
- Dashboard → pilih mood + Save. Tunggu sekitar 10 detik → Serial Monitor harus mencetak `[CLOUD] Config updated`.
- Tahan tombol baru GPIO32 sekitar 2 detik → game mode aktif. Tekan TTP223 → counter naik di OLED dan status dashboard ikut berubah setiap sekitar 15 detik.
- Klik tombol GPIO32 sebentar → suara buzzer ON/OFF. Konfirmasi melalui Serial Monitor.
- Dashboard → target 10 klik → Save; coba 10 touch input → ekspresi dan LED perayaan.
- Dashboard → Reset score → tunggu paling lambat satu siklus poll (10 detik) jika terhubung.
- Jika ingin offline saja, kosongkan config di `MindPal_Cloud.h`; fitur lokal tetap bekerja.

Selengkapnya baca `FIREBASE_SETUP.md`, `PROJECT_SPEC.md`, dan `TEST_PLAN.md`.
