# Panduan instalasi MindPal v8 — Alarm Clock

## A. Cek interface tanpa ESP32

1. Ekstrak ZIP dan buka `MindPal_v8_Alarm_Clock/web`.
2. **Salin file `.env` yang sudah bekerja dari `MindPal_v7/web/.env`** ke folder `MindPal_v8_Alarm_Clock/web/.env`. Jangan kirim password ke chat atau Git.
3. Jalankan `npm install` lalu `npm run dev`.
4. Buka URL Vite yang ditampilkan (biasanya http://localhost:5173/), login seperti pada dashboard v7.
5. Pilih menu **Alarms & sounds** → **Tambah alarm**.
6. Atur waktu, ulangi/tanggal, teks yang muncul pada OLED, nada, lalu **Simpan alarm**. Untuk alarm jadwal, tombol simpan akan langsung menyimpan ke Firebase; **Save changes** di header tetap berfungsi untuk konfigurasi lainnya.
7. Periksa Firebase Console → Realtime Database → Data → `devices/mindpal/config/scheduledAlarms`.
8. Buat dua alarm berbeda, toggle salah satunya, edit, lalu reload browser. Pastikan tersimpan. Fitur web ini bisa diuji ketika ESP32 mati.

## B. Security Rules

Jika Rules v7 dengan OWNER_UID/DEVICE_UID yang benar sudah berhasil dipublikasikan, tidak perlu mengubah rules. Jadwal berada **di dalam `config`**, yang memang bisa ditulis owner dan dibaca perangkat. Jika permission denied, cek UID pengguna yang login dan rules yang terpasang (bukan hanya file lokal).

## C. ESP32 fisik

1. Buka folder `firmware/MindPal_v8_Alarm_Clock/`.
2. Buka file `.ino` **langsung dari folder ini**, bukan menempelkan ke sketch kosong `sketch_nov...` dengan `setup()/loop()` lain.
3. Salin konfigurasi Wi-Fi/Firebase yang sudah bekerja dari header `MindPal_Cloud.h` versi v7 milikmu; jangan pakai UID pemilik/pasword akun Firebase pada firmware.
4. Pastikan library sebelumnya tetap terpasang: Adafruit GFX, Adafruit SSD1306, Adafruit NeoPixel, SparkFun MAX3010x, ArduinoJson v7, dan board package ESP32.
5. Verify, lalu upload pada ESP32-WROVER (Upload Speed 115200 bila diperlukan).
6. Buka Serial Monitor 115200 dan amati `[ALARM] ... schedules loaded` serta status Wi-Fi/NTP. Pastikan ESP tersambung internet dan waktu tersinkron.
7. Buat alarm **2–3 menit dari sekarang** di dashboard dalam zona WIB; pilih *Sekali saja* dan tanggal hari ini jika belum lewat.
8. Saat berbunyi: tekan GPIO33 untuk snooze sesuai durasi konfigurasi; GPIO32 untuk dismiss. Silent mode membuat layar aktif tanpa bunyi.

## D. Kendala umum

- Alarm tersimpan di web, tidak berbunyi di hardware: cek firmware v8, koneksi internet, validitas waktu NTP, Rules, dan mode Silent.
- Alarm tidak muncul di Firebase: pastikan login memakai akun OWNER_UID, rules punya `.write` pada `/devices/mindpal/config`, dan `.env` benar.
- Setelah listrik mati lalu ESP mulai tanpa Wi-Fi: ESP tidak tahu waktu sekarang; alarm belum bisa dipicu sampai NTP tersinkron.
- Waktu keliru: semua jadwal saat ini diproses berdasarkan **WIB UTC+7**, bukan zona waktu laptop jika berbeda.
- Tidak ada audio MP3: buzzer tiga pin hanya memainkan rangkaian nada; rekaman lagu memerlukan arsitektur audio yang berbeda.
