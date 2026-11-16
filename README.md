# KebunDemo v1.1: Siraman Pasu Pintar dengan Telegram + Panel Web

Projek demo untuk kursus Asas IoT untuk Pengurusan Kebun. Satu pasu bunga disiram secara automatik bila tanah kering, dan boleh dikawal melalui dua panel: bot Telegram dan panel web.

**Formula IoT:** Sensor kelembapan (rasa) → ESP32 (fikir) → Pam (buat) → Telefon (lapor)

```
                    ┌──────────────────┐
  Panel 1 ────────► │   Bot Telegram   │ ◄──── arahan + notifikasi
                    └────────┬─────────┘
                             │
                    ┌────────▼─────────┐        ┌────────────────────┐
  Sensor ─────────► │      ESP32       │ ◄────► │  Google Apps Script │ ◄──── Panel 2:
  Pam    ◄───────── │  (auto + kawal)  │  15 s  │  + Google Sheet     │       Panel web
                    └──────────────────┘        └────────────────────┘       (GitHub Pages)
```

**Struktur fail:**

```
kebundemo-panel/
├── index.html                    Panel kawalan web (GitHub Pages)
├── apps-script/Code.gs           Backend Google Apps Script
└── esp32/KebunDemo/KebunDemo.ino Kod ESP32
```

---

## 1. Senarai komponen

| Bil | Komponen | Kuantiti |
|---|---|---|
| 1 | ESP32 DevKit V1 | 1 |
| 2 | Sensor kelembapan tanah kapasitif v1.2 | 1 |
| 3 | Modul relay 1 saluran 5V | 1 |
| 4 | Pam air mini celup 3–6V DC | 1 |
| 5 | Hos silikon 8 mm (sekitar 1 meter) | 1 |
| 6 | Bekalan kuasa 5V 2A (adaptor USB) untuk pam | 1 |
| 7 | Kabel USB untuk ESP32 | 1 |
| 8 | Wayar jumper (male-female, male-male) | 1 set |
| 9 | Pasu bunga dengan tanah + bekas air kecil | 1 |

Pilihan: papan breadboard, kotak plastik untuk kemas.

---

## 2. Pendawaian

| Dari | Ke ESP32 | Catatan |
|---|---|---|
| Sensor VCC | 3V3 | **Guna 3.3V**, bukan 5V, supaya isyarat selamat untuk ESP32 |
| Sensor GND | GND | |
| Sensor AOUT | GPIO 34 | Mesti pin ADC1 (GPIO 32–39) |
| Relay VCC | VIN (5V) | |
| Relay GND | GND | |
| Relay IN | GPIO 26 | |

**Litar pam (melalui relay):**

```
5V adaptor (+) ──► Relay COM
Relay NO       ──► Pam (+)
Pam (−)        ──► 5V adaptor (−)
```

Pam guna bekalan kuasa sendiri, bukan dari pin ESP32. Arus mula pam boleh membuat ESP32 reset.

---

## 3. Sediakan bot Telegram

1. Buka Telegram, cari **@BotFather**, taip `/newbot`.
2. Beri nama bot, contoh `Demo Kebun P16` dan username `demokebunp16_bot`.
3. Salin **token** yang diberi.
4. Cari **@userinfobot**, taip `/start`, salin **ID** anda.

**Untuk demo kursus (ramai peserta boleh kawal):**

1. Cipta group Telegram, contoh "Demo Kebun IoT", dan tambah bot ke dalam group.
2. Taip `/status` dalam group. Bot akan balas "Akses ditolak. Chat ID anda: -100xxxxxxxxxx".
3. Salin ID group tersebut (bermula dengan tanda tolak) dan masukkan dalam kod.
4. Peserta yang join group boleh tekan butang arahan dan nampak notifikasi bersama.

Untuk bot boleh baca arahan dalam group, di @BotFather taip `/setprivacy` → pilih bot → **Disable**.

---

## 4. Upload kod

1. Arduino IDE → Boards Manager → pasang **esp32 by Espressif Systems**.
2. Library Manager → pasang **UniversalTelegramBot** (Brian Lough) dan **ArduinoJson**.
3. Buka `esp32/KebunDemo/KebunDemo.ino`, ubah bahagian **TETAPAN PENGGUNA**:
   - `WIFI_SSID`, `WIFI_PASS`
   - `BOT_TOKEN`
   - `CHAT_DIBENARKAN` (ID peribadi dan/atau ID group, boleh lebih dari satu: `{ "123456789", "-1001234567890" }`)
   - `SKRIP_URL`, `SKRIP_KUNCI` (lihat bahagian 9; biarkan `XXXX` jika guna Telegram sahaja)
4. Board: **ESP32 Dev Module**. Upload.
5. Buka Serial Monitor (115200) untuk lihat bacaan.

---

## 5. Kalibrasi sensor (wajib, 2 minit)

1. Taip `/kalibrasi` di Telegram.
2. Pegang sensor di udara, catat nilai raw → `NILAI_KERING`.
3. Celup sensor dalam air sehingga garisan putih → catat nilai raw → `NILAI_BASAH`.
4. Kemas kini dua nilai itu dalam kod, upload semula.

---

## 6. Arahan Telegram

| Arahan | Fungsi |
|---|---|
| `/status` | Kelembapan, mod auto, status pam, WiFi |
| `/siram` | Siram 5 saat |
| `/siram 10` | Siram ikut saat (maksimum 30) |
| `/stop` | Matikan pam serta-merta |
| `/auto_on` / `/auto_off` | Hidup / matikan siraman automatik |
| `/ambang 40` | Tetapkan had kering (%) |
| `/kalibrasi` | Bacaan raw sensor |
| `/bantuan` | Menu dengan butang |

Tetapan ambang dan mod auto disimpan dalam memori ESP32, kekal walaupun kuasa terputus.

---

## 7. Logik automatik

- Bila kelembapan < ambang (lalai 40%): pam hidup **5 saat**.
- Selepas itu ESP32 tunggu **60 saat** supaya air meresap, kemudian semak semula.
- Jika masih kering, siram lagi. Jika sudah lembap, berhenti.
- Pam tidak akan hidup lebih **30 saat** sekali jalan (had keselamatan).
- Jika sensor tercabut atau rosak, mod auto ditangguhkan dan amaran dihantar ke Telegram.
- Jika WiFi terputus, siraman automatik **tetap berjalan**. Telegram sahaja yang tidak aktif.

---

## 8. Skrip demo semasa kursus (10 minit)

1. **Tunjuk formula:** tunjuk sensor, ESP32, relay dan pam satu per satu.
2. **Kering → auto siram:** cabut sensor dari pasu dan pegang di udara. Dalam 2 saat pam hidup, telefon terima notifikasi "Pam HIDUP".
3. **Basah → berhenti:** cucuk semula sensor dalam tanah lembap atau celup dalam air. Selepas masa resap, pam tidak hidup lagi.
4. **Kawal dari telefon:** minta peserta dalam group tekan butang `/siram`. Bunyi "klik" relay dan air mengalir.
5. **Ubah ambang:** taip `/ambang 70`, tunjuk pam hidup sebab tanah kini dikira "kering".
6. **Tanpa internet:** matikan hotspot. Tunjuk siraman auto masih berjalan. Ini mesej penting: kebun tidak lumpuh bila internet putus.

**Petua:** untuk demo laju, ubah `MASA_RESAP` kepada `15000` (15 saat) dalam kod.

---

## 9. Backend Google Apps Script (simpan data + baris gilir arahan)

1. Cipta Google Sheet baharu, contoh **KebunDemo Data**.
2. **Extensions → Apps Script**. Padam kod asal, tampal kandungan `apps-script/Code.gs`, simpan.
3. Pilih fungsi `setup` di bar atas, tekan **Run**. Benarkan akses bila diminta.
4. Buka **Execution log**, salin **KUNCI API** (contoh `939d0e221e`). Empat helaian akan dicipta: Status, Data, Arahan, Log.
5. **Deploy → New deployment → Web app**:
   - Execute as: **Me**
   - Who has access: **Anyone**
6. Salin **URL Web App** (berakhir dengan `/exec`).
7. Masukkan URL dan kunci dalam `KebunDemo.ino` (`SKRIP_URL`, `SKRIP_KUNCI`), upload semula ke ESP32.

**Apa yang disimpan:**

| Helaian | Isi |
|---|---|
| Status | Satu baris status terkini ESP32 (dikemas kini setiap 15 saat) |
| Data | Kelembapan setiap 5 minit, dan setiap kali pam hidup/mati |
| Arahan | Baris gilir arahan dari panel web: BARU → DIHANTAR, atau LUPUT selepas 5 minit |
| Log | Peristiwa: pam hidup/mati, tukar tetapan, amaran sensor |

**Ubah kod Apps Script kemudian?** Deploy → Manage deployments → Edit → Version: **New version**. URL kekal sama.

---

## 10. Panel web di GitHub Pages

1. Repo: `matjeri99/kebundemo-panel` (Public), `index.html` di root.
2. **Settings → Pages → Source: Deploy from a branch → main / (root)** → Save.
3. Dalam 1–2 minit panel boleh dibuka di `https://matjeri99.github.io/kebundemo-panel/`.
4. Kali pertama buka, masukkan **URL Web App** dan **KUNCI API**. Maklumat ini disimpan dalam pelayar itu sahaja, **bukan** dalam GitHub.

**Fungsi panel:**

- Tolok kelembapan dengan status Kering / Lembap / Sensor bermasalah
- Status dalam talian ESP32, pam, mod auto, ambang, isyarat WiFi
- Butang Siram 5/10/20 saat, STOP, hidup/matikan mod auto
- Slider ambang kering
- Carta kelembapan 24 jam dengan garis ambang
- Log peristiwa terkini
- Muat semula automatik setiap 10 saat

**Notifikasi silang:** arahan dari panel web turut dilaporkan dalam Telegram ("🖥️ Panel web: ..."), jadi semua orang dalam group nampak siapa buat apa.

---

## 11. Keselamatan dan had

- **Kunci API** ialah kata laluan sistem. Jangan letak dalam repo GitHub. Tukar di Apps Script → Project Settings → Script Properties → `KUNCI`.
- Panel web **tidak serta-merta**: ESP32 ambil arahan setiap 15 saat. Telegram lebih pantas (1 saat).
- Arahan yang tidak diambil dalam 5 minit dibatalkan, supaya pam tidak hidup mengejut bila ESP32 kembali dalam talian.
- Kod ESP32 guna `setInsecure()` untuk sambungan ke Google (sesuai untuk demo). Untuk sistem sebenar, gunakan sijil root Google.

---

*SrieMatjeri*
