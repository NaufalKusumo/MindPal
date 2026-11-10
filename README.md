# MindPal v9 — Box Breathing + Improved Button UX

Versi ini melanjutkan firmware **v8 Alarm Clock**. Perubahan dipusatkan pada input dua tombol, OLED 128×64, LED WS2812, dan pengalaman Fun Clicker. **Dashboard React/Firebase dan Firebase Security Rules dari v8 tidak diubah.**

## Firmware

Buka langsung:

`firmware/MindPal_v9_BoxBreathing/MindPal_v9_BoxBreathing.ino`

Pastikan `MindPal_Cloud.h` berada di folder yang sama. Jangan membuka sketch kosong lalu menambahkan `.ino` kedua—itu menimbulkan duplikasi `setup()` dan `loop()`.

## Kontrol

| Input | Aksi |
|---|---|
| GPIO33 (utama) klik | Statistik ketika idle; countdown saat Pomodoro aktif |
| GPIO33 tahan ≥2 detik | Start/Stop Pomodoro (circular progress) |
| GPIO32 (kedua) klik | Silent/Normal + konfirmasi fullscreen OLED |
| GPIO32 tahan ≥2 detik | Start/Stop Fun Clicker (circular progress) |
| **GPIO33+GPIO32 bersamaan, tahan 5 detik** | **Toggle Box Breathing (4-4-4-4)** (circular progress 5 detik) |
| Sentuh TTP223 saat Fun Clicker | Tambah 1 poin; reaksi wajah imut; rayakan milestone |
| Saat alarm terjadwal berdering | GPIO33 = Snooze; GPIO32 = Dismiss (prioritas utama) |

Tekan **dua tombol berdekatan waktunya** agar combo mulai sebelum aksi tahan individual (2 detik). Ketika combo dideteksi, kedua tombol terkunci sampai keduanya dilepas; melepas sebelum 5 detik tidak memicu Pomodoro, Game, atau silent secara tidak sengaja.

## Box Breathing

- Teknik: **4 detik tarik napas → 4 detik tahan → 4 detik hembuskan → 4 detik tahan**.
- OLED: titik bergerak mengikuti sisi kotak, karakter bulat mengembang/menyusut, countdown besar, nomor putaran.
- LED ring: cahaya lembut bergerak sesuai fase; tidak memakai nada supaya tidak mengganggu.
- Box breathing berlangsung hingga **kedua tombol ditahan 5 detik lagi**. Pomodoro (jika aktif) berjalan di belakang; pembacaan sensor tetap berlangsung.
- Dalam mode ini tombol tunggal dan touch/game dicegah mengubah fungsi; alarm jam dari Firebase tetap mendapat prioritas di OLED dan buzzer.
- Ini hanya panduan pernapasan santai, bukan diagnosis atau terapi medis. Jangan memaksakan menahan napas; hentikan jika pusing atau tidak nyaman.

Durasi fase diatur di sketch:

```cpp
constexpr uint32_t BREATH_PHASE_MS = 4000UL;
constexpr uint32_t DUAL_HOLD_MS = 5000UL;
```

## Fun Clicker

- Hilang garis pemisah panjang di bawah wajah; counter tampil sebagai teks kecil mengambang tanpa garis.
- Tiap tap memberi sparkle dan reaksi wajah menyenangkan (*joy, laughing, cat, squint grin*).
- Setiap kelipatan `gameTarget` (default 30, dapat diubah dari dashboard) memicu animasi bintang + jingle (kecuali Silent) + **rainbow LED** berpadu sparkling selama ~2,6 detik, mirip perayaan Pomodoro.

## Software / dependensi

Firmware membutuhkan: **Adafruit SSD1306**, **Adafruit GFX**, **Adafruit NeoPixel**, **SparkFun MAX3010x**, dan **ArduinoJson v7**. ESP32 core sudah menyediakan `WiFi`, `Wire`, `Preferences`, `HTTPClient` dan `time`.

Untuk offline, biarkan konfigurasi cloud kosong di `MindPal_Cloud.h`; jangan menghapus file header. Untuk Firebase gunakan konfigurasi pribadi dari perangkat lokal, **tidak disertakan** dalam paket ini.

## Wiring — tidak ada perubahan

- OLED & MAX30102 SDA=GPIO21, SCL=GPIO22.
- WS2812 data GPIO25, supply 5V memadai, semua GND bersama; resistor data 220Ω bisa dipakai untuk uji, level shifter 3.3V ke 5V tetap direkomendasikan untuk keandalan.
- Passive buzzer modul IO GPIO26.
- Touch TTP223 IO GPIO27.
- Tombol utama GPIO33 ke GND; tombol kedua GPIO32 ke GND (keduanya `INPUT_PULLUP`).

## Status verifikasi

- `g++ -fsyntax-only` dengan stub API Arduino/periferal: **lulus**.
- Simulasi C++ input tombol, kombinasi hold, pembatalan, silent, Pomodoro, game: **lulus**.
- **Belum** dikompilasi dengan Arduino-ESP32 asli dan **belum** diuji pada ESP32/OLED/LED ring fisik.
- UI breathing dan animasi diprogram secara prosedural, bukan dinilai dari footage hardware aktual.

Lihat `docs/BOX_BREATHING_V9.md` dan `docs/TEST_PLAN_V9.md` untuk detail.
