/*
  ============================================================
  KebunDemo v1.2 CYD - Siraman Pasu Pintar dengan Skrin Sentuh
  ------------------------------------------------------------
  Papan   : ESP32-2432S028R (CYD, Cheap Yellow Display)
  Panel 1 : Bot Telegram (arahan + notifikasi)
  Panel 2 : Panel web GitHub Pages melalui Google Apps Script
  Panel 3 : Skrin sentuh CYD (berfungsi walaupun tanpa internet)

  Formula IoT: SENSOR (rasa) -> ESP32 (fikir) -> PAM (buat) -> TELEFON/SKRIN (lapor)

  PENDAWAIAN (berbeza daripada ESP32 DevKit!)
    Sensor kelembapan AOUT -> GPIO35  (penyambung P3; GPIO34 = LDR terbina dalam CYD)
    Sensor VCC / GND       -> 3.3V / GND (penyambung CN1)
    Relay IN               -> GPIO22  (penyambung CN1 atau P3)
    Relay VCC / GND        -> 5V / GND (penyambung P1 / kuasa USB)
    LED status             -> LED RGB terbina (merah GPIO4, aktif LOW)

  Library : LovyanGFX, UniversalTelegramBot (Brian Lough), ArduinoJson
  Oleh    : SrieMatjeri / D77Tech
  ============================================================
*/

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ================= TETAPAN PENGGUNA (UBAH DI SINI) =================
const char* WIFI_SSID = "NAMA_WIFI";
const char* WIFI_PASS = "KATALALUAN_WIFI";
#define BOT_TOKEN "TOKEN_DARI_BOTFATHER"

const char* CHAT_DIBENARKAN[] = { "123456789" };
const int BIL_CHAT = sizeof(CHAT_DIBENARKAN) / sizeof(CHAT_DIBENARKAN[0]);

const char* SKRIP_URL   = "https://script.google.com/macros/s/AKfycbyR_lLo00KKktvE1oYMdEHWqJ39hsJLjOxLcv9UCarO7XXhM2V-TOQCAVY3WSykdntvog/exec";
const char* SKRIP_KUNCI = "KUNCI_API";

// Pin (CYD)
const int  PIN_SENSOR      = 35;    // input sahaja, ADC1 - sesuai untuk sensor analog
const int  PIN_RELAY       = 22;
const int  PIN_LED         = 4;     // LED RGB merah terbina dalam
const bool LED_AKTIF_LOW   = true;  // LED RGB CYD aktif LOW
const bool RELAY_AKTIF_LOW = true;

// Paparan
const int ROTASI_SKRIN = 7;  // nilai yang telah disahkan; cuba 1 atau 3 jika paparan terbalik/cermin

// Kalibrasi sensor (guna /kalibrasi atau lihat bacaan raw pada skrin)
int NILAI_KERING = 3000;
int NILAI_BASAH  = 1300;

// Tetapan siraman
const int           AMBANG_LALAI    = 40;
const unsigned long MASA_PAM_AUTO   = 5000;
const unsigned long MASA_PAM_MANUAL = 5000;
const unsigned long MASA_PAM_MAKS   = 30000;
const unsigned long MASA_RESAP      = 60000;
const unsigned long SELA_BACA       = 2000;
const unsigned long SELA_TELEGRAM   = 1500;
const unsigned long SELA_SKRIP      = 15000;
const unsigned long SELA_WIFI       = 10000;
// ===================================================================

// ---------------- Konfigurasi LovyanGFX untuk CYD ----------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;   // CYD versi 2 USB guna ST7789: tukar kepada lgfx::Panel_ST7789
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;
public:
  LGFX() {
    { auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;  cfg.spi_mode = 0;
      cfg.freq_write = 40000000; cfg.freq_read = 16000000;
      cfg.spi_3wire = false;     cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14; cfg.pin_mosi = 13; cfg.pin_miso = 12; cfg.pin_dc = 2;
      _bus.config(cfg); _panel.setBus(&_bus); }
    { auto cfg = _panel.config();
      cfg.pin_cs = 15; cfg.pin_rst = -1; cfg.pin_busy = -1;
      cfg.panel_width = 320; cfg.panel_height = 240;
      cfg.offset_x = 0; cfg.offset_y = 0; cfg.offset_rotation = 0;
      cfg.readable = true; cfg.invert = false; cfg.rgb_order = true;
      cfg.dlen_16bit = false; cfg.bus_shared = false;
      _panel.config(cfg); }
    { auto cfg = _light.config();
      cfg.pin_bl = 21; cfg.invert = false; cfg.freq = 44100; cfg.pwm_channel = 7;
      _light.config(cfg); _panel.setLight(&_light); }
    { auto cfg = _touch.config();
      cfg.x_min = 300; cfg.x_max = 3900; cfg.y_min = 200; cfg.y_max = 3700;
      cfg.pin_int = 36; cfg.bus_shared = false; cfg.offset_rotation = 0;
      cfg.spi_host = VSPI_HOST; cfg.freq = 1000000;
      cfg.pin_sclk = 25; cfg.pin_mosi = 32; cfg.pin_miso = 39; cfg.pin_cs = 33;
      _touch.config(cfg); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
LGFX tft;

WiFiClientSecure klienTelegram;
UniversalTelegramBot bot(BOT_TOKEN, klienTelegram);
Preferences prefs;

int  rawSensor    = 0;
int  kelembapan   = 0;
bool sensorOK     = true;
int  ambangKering = AMBANG_LALAI;
bool modAuto      = true;

bool          pamHidup      = false;
unsigned long pamMula       = 0;
unsigned long pamTempoh     = 0;
unsigned long akhirSiram    = 0;
bool          pernahSiram   = false;
unsigned long jumlahSiraman = 0;

unsigned long akhirBaca = 0, akhirBot = 0, akhirWifi = 0, akhirSkrip = 0;
bool skripAktif = false;
bool skripOK    = false;
bool paksaData  = false;

const int MAKS_LOG = 6;
String logJenis[MAKS_LOG], logMesej[MAKS_LOG];
int bilLog = 0;

// ---------------- Warna & susun atur skrin ----------------
uint16_t W_LATAR, W_GELAP, W_KAD, W_TEKS, W_PUDAR, W_DAUN, W_HIJAU, W_OREN, W_MERAH, W_JALUR;

struct Butang { int x, y, w, h; const char* id; };
const Butang B_SIRAM  = {   4, 196, 100, 40, "siram" };
const Butang B_STOP   = { 110, 196, 100, 40, "stop" };
const Butang B_AUTO   = { 216, 196, 100, 40, "auto" };
const Butang B_KURANG = { 172, 120,  64, 32, "kurang" };
const Butang B_TAMBAH = { 248, 120,  64, 32, "tambah" };

const int GX = 80, GY = 112, GR_LUAR = 62, GR_DALAM = 48;  // tolok

// Nilai yang terakhir dilukis (elak lukis semula yang tidak perlu)
int  dKelembapan = -1, dAmbang = -1, dRaw = -1;
int  dPam = -1, dAuto = -1, dSensor = -1, dWifi = -1, dWeb = -1;
String mesejSkrin = "";
unsigned long mesejTamat = 0;
bool mesejDipapar = false;

// ---------------- Fungsi bantu ----------------
void relayTulis(bool hidup) {
  digitalWrite(PIN_RELAY, RELAY_AKTIF_LOW ? !hidup : hidup);
  digitalWrite(PIN_LED, LED_AKTIF_LOW ? !hidup : hidup);
}

bool chatDibenarkan(const String& id) {
  for (int i = 0; i < BIL_CHAT; i++) if (id == CHAT_DIBENARKAN[i]) return true;
  return false;
}

void hantarSemua(const String& mesej) {
  if (WiFi.status() != WL_CONNECTED) return;
  for (int i = 0; i < BIL_CHAT; i++) bot.sendMessage(CHAT_DIBENARKAN[i], mesej, "");
}

void tambahLog(const String& jenis, const String& mesej) {
  if (!skripAktif) return;
  if (bilLog >= MAKS_LOG) {
    for (int i = 1; i < MAKS_LOG; i++) { logJenis[i - 1] = logJenis[i]; logMesej[i - 1] = logMesej[i]; }
    bilLog = MAKS_LOG - 1;
  }
  logJenis[bilLog] = jenis; logMesej[bilLog] = mesej; bilLog++;
}

String urlEncode(const String& s) {
  String o; char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else { snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c); o += buf; }
  }
  return o;
}

int bacaRaw() {
  long jumlah = 0;
  for (int i = 0; i < 10; i++) { jumlah += analogRead(PIN_SENSOR); delay(5); }
  return jumlah / 10;
}

int rawKePeratus(int raw) {
  return constrain(map(raw, NILAI_KERING, NILAI_BASAH, 0, 100), 0, 100);
}

String masaHidup() {
  unsigned long s = millis() / 1000;
  return String(s / 3600) + "j " + String((s % 3600) / 60) + "m";
}

void paparMesej(const String& m) {
  mesejSkrin = m;
  mesejTamat = millis() + 3000;
  mesejDipapar = false;
}

// ---------------- Kawalan pam ----------------
void pamOn(unsigned long ms, const String& sebab) {
  if (pamHidup) return;
  if (ms > MASA_PAM_MAKS) ms = MASA_PAM_MAKS;
  relayTulis(true);  // skrin: pam terus hidup, notifikasi menyusul
  pamHidup = true; pamMula = millis(); pamTempoh = ms;
  jumlahSiraman++; paksaData = true;
  tambahLog("PAM", "Pam HIDUP " + String(ms / 1000) + " s - " + sebab);
  Serial.printf("[PAM] HIDUP %lu ms - %s\n", ms, sebab.c_str());
  kemasKiniSkrin();
  hantarSemua("💧 Pam HIDUP " + String(ms / 1000) + " saat\nSebab: " + sebab);
  pamMula = millis();  // mula kira selepas notifikasi, supaya tempoh siraman tepat
}

void pamOff(const String& sebab) {
  if (!pamHidup) return;
  relayTulis(false);
  pamHidup = false; akhirSiram = millis(); pernahSiram = true; paksaData = true;
  tambahLog("PAM", "Pam MATI - " + sebab);
  Serial.printf("[PAM] MATI - %s\n", sebab.c_str());
  kemasKiniSkrin();
  hantarSemua("✅ Pam MATI (" + sebab + ")\nKelembapan semasa: " + String(kelembapan) + "%");
}

String laksanaArahan(String cmd, int nilai, const String& sumber) {
  cmd.toLowerCase();
  if (cmd == "siram") {
    if (pamHidup) return "Pam sedang hidup.";
    unsigned long ms = nilai > 0 ? (unsigned long)nilai * 1000UL : MASA_PAM_MANUAL;
    String amaran = kelembapan >= 80 ? "⚠️ Tanah sudah lembap (" + String(kelembapan) + "%), arahan tetap diteruskan." : "";
    pamOn(ms, "arahan manual " + sumber);
    return amaran;
  }
  if (cmd == "stop") {
    if (!pamHidup) return "Pam sudah mati.";
    pamOff("dihentikan melalui " + sumber);
    return "";
  }
  if (cmd == "auto_on" || cmd == "auto_off") {
    modAuto = (cmd == "auto_on");
    prefs.putBool("auto", modAuto);
    paksaData = true;
    tambahLog("TETAPAN", String("Mod auto ") + (modAuto ? "ON" : "OFF") + " (" + sumber + ")");
    if (sumber != "Telegram") hantarSemua(String("⚙️ Mod auto ") + (modAuto ? "ON" : "OFF") + " melalui " + sumber);
    return modAuto
      ? "🤖 Mod auto ON. Pam hidup sendiri bila kelembapan bawah " + String(ambangKering) + "%."
      : "✋ Mod auto OFF. Siraman hanya melalui arahan manual.";
  }
  if (cmd == "ambang") {
    if (nilai < 10 || nilai > 90) return "Guna nilai 10 hingga 90. Contoh: /ambang 40";
    ambangKering = nilai;
    prefs.putInt("ambang", nilai);
    paksaData = true;
    tambahLog("TETAPAN", "Ambang " + String(nilai) + "% (" + sumber + ")");
    return "Ambang kering ditetapkan kepada " + String(nilai) + "%.";
  }
  return "Arahan tidak dikenali.";
}

// ---------------- Skrin ----------------
void lengkok(int a0, int a1, uint16_t warna) {
  // Sudut LovyanGFX: 0 = arah jam 3, mengikut arah jam. Pecahkan jika melepasi 360.
  if (a1 <= a0) return;
  if (a1 <= 360) { tft.fillArc(GX, GY, GR_LUAR, GR_DALAM, a0, a1, warna); return; }
  tft.fillArc(GX, GY, GR_LUAR, GR_DALAM, a0, 360, warna);
  tft.fillArc(GX, GY, GR_LUAR, GR_DALAM, 0, a1 - 360, warna);
}

void lukisButang(const Butang& b, const char* label, uint16_t latar, uint16_t teks) {
  tft.fillRoundRect(b.x, b.y, b.w, b.h, 8, latar);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(teks, latar);
  tft.setTextDatum(middle_center);
  tft.drawString(label, b.x + b.w / 2, b.y + b.h / 2 + 1);
}

void lukisStatik() {
  tft.fillScreen(W_LATAR);
  tft.fillRect(0, 0, 320, 26, W_GELAP);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(W_LATAR, W_GELAP);
  tft.setTextDatum(middle_left);
  tft.drawString("KebunDemo", 8, 14);

  tft.setFont(&fonts::FreeSans9pt7b);
  tft.setTextColor(W_PUDAR, W_LATAR);
  tft.setTextDatum(middle_left);
  tft.drawString("Pam",    172, 42);
  tft.drawString("Auto",   172, 68);
  tft.drawString("Ambang", 172, 98);

  lukisButang(B_KURANG, "- 5%", W_JALUR, W_GELAP);
  lukisButang(B_TAMBAH, "+ 5%", W_JALUR, W_GELAP);
  lukisButang(B_SIRAM, "SIRAM", W_HIJAU, W_LATAR);
  lukisButang(B_STOP,  "STOP",  W_MERAH, W_LATAR);
}

void lukisNilaiKanan(int y, const String& teks, uint16_t warna) {
  tft.fillRect(240, y - 10, 78, 21, W_LATAR);
  tft.setFont(&fonts::FreeSansBold9pt7b);
  tft.setTextColor(warna, W_LATAR);
  tft.setTextDatum(middle_right);
  tft.drawString(teks, 314, y);
}

void kemasKiniSkrin() {
  bool wifi = (WiFi.status() == WL_CONNECTED);
  int web = skripAktif ? (skripOK ? 1 : 0) : 2;

  // Header: status WiFi dan panel web
  if (wifi != dWifi || web != dWeb) {
    dWifi = wifi; dWeb = web;
    tft.fillRect(150, 0, 170, 26, W_GELAP);
    tft.setFont(&fonts::Font2);
    tft.setTextDatum(middle_right);
    tft.setTextColor(wifi ? W_DAUN : W_OREN, W_GELAP);
    tft.drawString(wifi ? "WiFi OK" : "Tiada WiFi", 236, 14);
    tft.setTextColor(web == 1 ? W_DAUN : (web == 0 ? W_OREN : W_PUDAR), W_GELAP);
    tft.drawString(web == 1 ? "Web OK" : (web == 0 ? "Web --" : "Web off"), 312, 14);
  }

  // Tolok kelembapan
  if (kelembapan != dKelembapan || ambangKering != dAmbang || sensorOK != dSensor) {
    bool kering = kelembapan < ambangKering;
    uint16_t warna = !sensorOK ? W_MERAH : (kering ? W_OREN : W_DAUN);
    int hujung = 135 + (270 * kelembapan) / 100;
    lengkok(135, 405, W_JALUR);
    lengkok(135, hujung, warna);
    // Penanda ambang
    float a = (135 + 270.0f * ambangKering / 100) * DEG_TO_RAD;
    tft.drawLine((int)(GX + cosf(a) * (GR_DALAM - 4)), (int)(GY + sinf(a) * (GR_DALAM - 4)),
                 (int)(GX + cosf(a) * (GR_LUAR + 4)),  (int)(GY + sinf(a) * (GR_LUAR + 4)), W_GELAP);

    tft.fillCircle(GX, GY, GR_DALAM - 2, W_LATAR);
    tft.setTextDatum(middle_center);
    tft.setFont(&fonts::FreeSansBold18pt7b);
    tft.setTextColor(W_TEKS, W_LATAR);
    tft.drawString(String(kelembapan) + "%", GX, GY - 4);
    tft.setFont(&fonts::Font2);
    tft.setTextColor(W_PUDAR, W_LATAR);
    tft.drawString("kelembapan", GX, GY + 22);

    tft.fillRect(10, 172, 140, 20, W_LATAR);
    tft.setFont(&fonts::FreeSansBold9pt7b);
    tft.setTextColor(warna, W_LATAR);
    tft.drawString(!sensorOK ? "SENSOR ROSAK" : (kering ? "KERING" : "LEMBAP"), GX, 182);
  }

  if ((int)pamHidup != dPam) {
    dPam = pamHidup;
    lukisNilaiKanan(42, pamHidup ? "HIDUP" : "MATI", pamHidup ? W_HIJAU : W_TEKS);
  }
  if ((int)modAuto != dAuto) {
    dAuto = modAuto;
    lukisNilaiKanan(68, modAuto ? "ON" : "OFF", modAuto ? W_HIJAU : W_OREN);
    lukisButang(B_AUTO, modAuto ? "AUTO: ON" : "AUTO: OFF", modAuto ? W_GELAP : W_OREN, W_LATAR);
  }
  if (ambangKering != dAmbang) {
    dAmbang = ambangKering;
    lukisNilaiKanan(98, String(ambangKering) + "%", W_TEKS);
  }
  dKelembapan = kelembapan; dSensor = sensorOK;

  // Baris info / mesej sementara
  bool adaMesej = millis() < mesejTamat;
  if (adaMesej && !mesejDipapar) {
    mesejDipapar = true;
    tft.fillRect(160, 158, 160, 34, W_LATAR);
    tft.setFont(&fonts::Font2);
    tft.setTextDatum(middle_center);
    tft.setTextColor(W_GELAP, W_LATAR);
    tft.drawString(mesejSkrin, 240, 174);
    dRaw = -1;
  } else if (!adaMesej && (rawSensor / 20 != dRaw)) {
    dRaw = rawSensor / 20;  // kurangkan kelipan: lukis semula bila berubah > 20
    tft.fillRect(160, 158, 160, 34, W_LATAR);
    tft.setFont(&fonts::Font2);
    tft.setTextDatum(middle_center);
    tft.setTextColor(W_PUDAR, W_LATAR);
    tft.drawString("raw " + String(rawSensor) + "  |  " + (wifi ? String(WiFi.RSSI()) + " dBm" : String("luar talian")), 240, 174);
  }
}

bool dalam(const Butang& b, int x, int y) {
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

void urusSentuhan() {
  static bool tadiDitekan = false;
  static unsigned long akhirTekan = 0;
  int32_t x, y;
  bool ditekan = tft.getTouch(&x, &y);
  if (!ditekan) { tadiDitekan = false; return; }
  if (tadiDitekan || millis() - akhirTekan < 300) return;
  tadiDitekan = true;
  akhirTekan = millis();
  Serial.printf("[SENTUH] x=%d y=%d\n", (int)x, (int)y);

  String balas;
  if (dalam(B_SIRAM, x, y)) {
    lukisButang(B_SIRAM, "SIRAM", W_GELAP, W_LATAR);
    balas = laksanaArahan("siram", 0, "skrin CYD");
    lukisButang(B_SIRAM, "SIRAM", W_HIJAU, W_LATAR);
    if (balas == "") balas = "Siram " + String(MASA_PAM_MANUAL / 1000) + " saat";
  } else if (dalam(B_STOP, x, y)) {
    balas = laksanaArahan("stop", 0, "skrin CYD");
    if (balas == "") balas = "Pam dihentikan";
  } else if (dalam(B_AUTO, x, y)) {
    laksanaArahan(modAuto ? "auto_off" : "auto_on", 0, "skrin CYD");
    balas = modAuto ? "Mod auto ON" : "Mod auto OFF";
  } else if (dalam(B_KURANG, x, y)) {
    laksanaArahan("ambang", max(10, ambangKering - 5), "skrin CYD");
    balas = "Ambang " + String(ambangKering) + "%";
  } else if (dalam(B_TAMBAH, x, y)) {
    laksanaArahan("ambang", min(90, ambangKering + 5), "skrin CYD");
    balas = "Ambang " + String(ambangKering) + "%";
  } else {
    return;
  }
  // Buang emoji/ayat panjang untuk skrin kecil
  if (balas.startsWith("⚠️")) balas = "Tanah sudah lembap";
  paparMesej(balas);
  kemasKiniSkrin();
}

// ---------------- Telegram ----------------
String teksStatus() {
  String s = "📊 STATUS PASU\n";
  s += "Kelembapan: " + String(kelembapan) + "% (raw " + String(rawSensor) + ")\n";
  s += "Keadaan: " + String(!sensorOK ? "SENSOR BERMASALAH" : (kelembapan < ambangKering ? "KERING" : "OK")) + "\n";
  s += "Ambang kering: " + String(ambangKering) + "%\n";
  s += "Mod auto: " + String(modAuto ? "ON" : "OFF") + "\n";
  s += "Pam: " + String(pamHidup ? "HIDUP" : "MATI") + "\n";
  s += "Siraman sejak hidup: " + String(jumlahSiraman) + " kali\n";
  s += "Panel web: " + String(!skripAktif ? "TIDAK DIGUNAKAN" : (skripOK ? "AKTIF" : "GAGAL HUBUNG")) + "\n";
  s += "WiFi: " + String(WiFi.RSSI()) + " dBm\n";
  s += "Masa hidup: " + masaHidup();
  return s;
}

String teksBantuan() {
  return String("🌱 KebunDemo CYD - Siraman Pasu Pintar\n\n") +
         "/status - lihat kelembapan & status\n" +
         "/siram - siram 5 saat\n" +
         "/siram 10 - siram ikut saat (maks 30)\n" +
         "/stop - matikan pam segera\n" +
         "/auto_on - hidupkan siraman automatik\n" +
         "/auto_off - matikan siraman automatik\n" +
         "/ambang 40 - tetapkan had kering (%)\n" +
         "/kalibrasi - lihat bacaan raw sensor";
}

void hantarMenu(const String& chatId, const String& mesej) {
  String papan = "[[\"/status\",\"/siram\"],[\"/auto_on\",\"/auto_off\"],[\"/stop\",\"/bantuan\"]]";
  bot.sendMessageWithReplyKeyboard(chatId, mesej, "", papan, true);
}

void urusMesej(int bilangan) {
  for (int i = 0; i < bilangan; i++) {
    String chatId = bot.messages[i].chat_id;
    String teks   = bot.messages[i].text;
    teks.trim();
    if (!chatDibenarkan(chatId)) {
      bot.sendMessage(chatId, "⛔ Akses ditolak. Chat ID anda: " + chatId, "");
      continue;
    }
    String arahan = teks, nilai = "";
    int ruang = teks.indexOf(' ');
    if (ruang > 0) { arahan = teks.substring(0, ruang); nilai = teks.substring(ruang + 1); nilai.trim(); }
    int at = arahan.indexOf('@');
    if (at > 0) arahan = arahan.substring(0, at);
    arahan.toLowerCase();

    if (arahan == "/start" || arahan == "/bantuan" || arahan == "/help") {
      hantarMenu(chatId, teksBantuan());
    } else if (arahan == "/status") {
      bot.sendMessage(chatId, teksStatus(), "");
    } else if (arahan == "/siram" || arahan == "/stop" || arahan == "/auto_on" ||
               arahan == "/auto_off" || arahan == "/ambang") {
      String balas = laksanaArahan(arahan.substring(1), nilai.toInt(), "Telegram");
      if (balas.length() > 0) bot.sendMessage(chatId, balas, "");
      paparMesej("Arahan Telegram: " + arahan);
    } else if (arahan == "/kalibrasi") {
      String s = "🔧 KALIBRASI\nBacaan raw sekarang: " + String(bacaRaw()) + "\n\n";
      s += "1. Pegang sensor di udara, catat raw -> NILAI_KERING\n";
      s += "2. Celup sensor dalam air (hingga garisan), catat raw -> NILAI_BASAH\n";
      s += "3. Kemas kini nilai dalam kod dan upload semula.\n\n";
      s += "Nilai dalam kod sekarang: KERING=" + String(NILAI_KERING) + ", BASAH=" + String(NILAI_BASAH);
      bot.sendMessage(chatId, s, "");
    } else {
      bot.sendMessage(chatId, "Arahan tidak dikenali. Taip /bantuan", "");
    }
  }
}

// ---------------- Apps Script ----------------
bool panggilSkrip(const String& query, String& respons) {
  if (!skripAktif || WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure klien;
  klien.setInsecure();  // demo: tidak semak sijil Google
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  String url = String(SKRIP_URL) + "?key=" + urlEncode(SKRIP_KUNCI) + "&" + query;
  if (!http.begin(klien, url)) return false;
  int kod = http.GET();
  bool ok = (kod == 200);
  if (ok) respons = http.getString();
  else Serial.printf("[SKRIP] Gagal, kod HTTP %d\n", kod);
  http.end();
  return ok;
}

void pollSkrip() {
  String q = "action=poll";
  q += "&k=" + String(kelembapan);
  q += "&raw=" + String(rawSensor);
  q += "&pam=" + String(pamHidup ? 1 : 0);
  q += "&auto=" + String(modAuto ? 1 : 0);
  q += "&ambang=" + String(ambangKering);
  q += "&sensor=" + String(sensorOK ? 1 : 0);
  q += "&rssi=" + String(WiFi.RSSI());
  q += "&uptime=" + String(millis() / 1000);
  if (paksaData) q += "&paksa=1";

  String respons;
  skripOK = panggilSkrip(q, respons);
  if (!skripOK) return;
  paksaData = false;

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, respons)) { Serial.println("[SKRIP] JSON tidak sah"); skripOK = false; return; }
  if (!(doc["ok"] | false)) { Serial.printf("[SKRIP] Ralat: %s\n", (const char*)(doc["ralat"] | "?")); skripOK = false; return; }

  JsonArray senarai = doc["arahan"].as<JsonArray>();
  for (JsonObject a : senarai) {
    String cmd = a["cmd"].as<String>();
    int nilai  = a["nilai"] | 0;
    Serial.printf("[SKRIP] Arahan panel web: %s %d\n", cmd.c_str(), nilai);
    String balas = laksanaArahan(cmd, nilai, "panel web");
    if (balas.length() > 0) hantarSemua("🖥️ Panel web: " + balas);
    paparMesej("Arahan panel web: " + cmd);
  }
}

void hantarLogSeterusnya() {
  if (bilLog == 0) return;
  String respons;
  String q = "action=log&jenis=" + urlEncode(logJenis[0]) + "&mesej=" + urlEncode(logMesej[0]);
  if (!panggilSkrip(q, respons)) return;
  for (int i = 1; i < bilLog; i++) { logJenis[i - 1] = logJenis[i]; logMesej[i - 1] = logMesej[i]; }
  bilLog--;
}

// ---------------- WiFi ----------------
void sambungWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Menyambung WiFi");
  tft.setFont(&fonts::Font2);
  tft.setTextDatum(middle_center);
  tft.setTextColor(W_TEKS, W_LATAR);
  tft.drawString("Menyambung WiFi...", 160, 120);
  unsigned long mula = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mula < 20000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) Serial.printf("\nWiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  else Serial.println("\nWiFi gagal. Skrin dan mod auto tetap berjalan.");
}

// ---------------- Setup & loop ----------------
void setup() {
  Serial.begin(115200);

  digitalWrite(PIN_RELAY, RELAY_AKTIF_LOW ? HIGH : LOW);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(16, OUTPUT); digitalWrite(16, HIGH);  // LED hijau OFF
  pinMode(17, OUTPUT); digitalWrite(17, HIGH);  // LED biru OFF
  relayTulis(false);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SENSOR, ADC_11db);  // julat penuh 0-3.3V

  tft.init();
  tft.setRotation(ROTASI_SKRIN);
  tft.setBrightness(200);
  W_LATAR = tft.color565(0xF5, 0xF2, 0xE9);
  W_GELAP = tft.color565(0x16, 0x30, 0x1F);
  W_KAD   = tft.color565(0xFF, 0xFD, 0xF7);
  W_TEKS  = tft.color565(0x1D, 0x2A, 0x21);
  W_PUDAR = tft.color565(0x5A, 0x6B, 0x5F);
  W_DAUN  = tft.color565(0x7C, 0xC2, 0x55);
  W_HIJAU = tft.color565(0x35, 0x6B, 0x1C);
  W_OREN  = tft.color565(0xE0, 0x8A, 0x1E);
  W_MERAH = tft.color565(0xC2, 0x41, 0x0C);
  W_JALUR = tft.color565(0xDD, 0xE6, 0xD2);
  tft.fillScreen(W_LATAR);
  if (tft.width() < tft.height()) Serial.println("[SKRIN] Mod potret dikesan. Tukar ROTASI_SKRIN supaya 320x240.");

  prefs.begin("kebundemo", false);
  ambangKering = prefs.getInt("ambang", AMBANG_LALAI);
  modAuto      = prefs.getBool("auto", true);
  skripAktif   = (String(SKRIP_URL).indexOf("XXXX") < 0);

  klienTelegram.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  sambungWiFi();

  rawSensor  = bacaRaw();
  kelembapan = rawKePeratus(rawSensor);

  lukisStatik();
  kemasKiniSkrin();

  tambahLog("SISTEM", "CYD dihidupkan semula");
  paksaData = true;
  hantarSemua("🌱 KebunDemo CYD dalam talian.\nKelembapan: " + String(kelembapan) + "%\nTaip /bantuan untuk senarai arahan.");
}

void loop() {
  unsigned long kini = millis();

  // 0. SKRIN SENTUH (panel 3) - disemak dahulu supaya responsif
  urusSentuhan();

  // 1. SENSOR (rasa)
  if (kini - akhirBaca >= SELA_BACA) {
    akhirBaca  = kini;
    rawSensor  = bacaRaw();
    kelembapan = rawKePeratus(rawSensor);
    bool okBaru = (rawSensor > 200 && rawSensor < 4000);
    if (sensorOK && !okBaru) {
      hantarSemua("⚠️ Sensor bermasalah (raw " + String(rawSensor) + "). Mod auto ditangguhkan. Semak sambungan.");
      tambahLog("AMARAN", "Sensor bermasalah, raw " + String(rawSensor));
    }
    if (!sensorOK && okBaru) {
      hantarSemua("✅ Sensor kembali normal.");
      tambahLog("INFO", "Sensor kembali normal");
    }
    sensorOK = okBaru;
    kemasKiniSkrin();
  }

  // 2. PENGAWAL (fikir)
  bool masaResapTamat = !pernahSiram || (kini - akhirSiram >= MASA_RESAP);
  if (modAuto && sensorOK && !pamHidup && kelembapan < ambangKering && masaResapTamat) {
    // 3. AKTUATOR (buat)
    pamOn(MASA_PAM_AUTO, "Auto: tanah kering (" + String(kelembapan) + "%)");
  }

  if (pamHidup && (millis() - pamMula >= pamTempoh)) pamOff("tempoh siraman tamat");

  // Padam mesej sementara bila tamat
  if (mesejDipapar && millis() >= mesejTamat) { mesejDipapar = false; dRaw = -1; kemasKiniSkrin(); }

  // 4. TELEFON (lapor & terima arahan)
  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - akhirBot >= SELA_TELEGRAM) {
      akhirBot = millis();
      int bil = bot.getUpdates(bot.last_message_received + 1);
      while (bil) { urusMesej(bil); bil = bot.getUpdates(bot.last_message_received + 1); }
    }
    if (skripAktif) {
      unsigned long sejakSkrip = millis() - akhirSkrip;
      bool tiba = (sejakSkrip >= SELA_SKRIP) || (paksaData && !pamHidup && sejakSkrip >= 3000);
      if (tiba) { akhirSkrip = millis(); pollSkrip(); kemasKiniSkrin(); }
      else if (!pamHidup && bilLog > 0) hantarLogSeterusnya();
    }
  } else if (kini - akhirWifi >= SELA_WIFI) {
    akhirWifi = kini;
    Serial.println("WiFi terputus, cuba sambung semula...");
    WiFi.reconnect();
    kemasKiniSkrin();
  }
}
