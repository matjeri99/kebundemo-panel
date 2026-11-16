/*
  ============================================================
  KebunDemo v1.3 - Siraman Pasu Pintar: Telegram + Panel Web + OLED
  ------------------------------------------------------------
  Demo kursus: Asas IoT untuk Pengurusan Kebun
  Formula IoT: SENSOR (rasa) -> ESP32 (fikir) -> PAM (buat) -> TELEFON (lapor)

  Panel 1 : Bot Telegram (arahan + notifikasi)
  Panel 2 : Panel web GitHub Pages, melalui Google Apps Script (data disimpan di Google Sheet)

  Perkakasan : ESP32 DevKit V1, sensor kelembapan tanah kapasitif v1.2,
               modul relay 1 saluran, pam air mini 5V
  Paparan    : OLED 0.96" SSD1306 128x64 I2C (SDA=GPIO21, SCL=GPIO22, VCC=3.3V)
  Library    : UniversalTelegramBot (Brian Lough), ArduinoJson,
               Adafruit SSD1306, Adafruit GFX
  Oleh       : SrieMatjeri / D77Tech
  ============================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= TETAPAN PENGGUNA (UBAH DI SINI) =================
const char* WIFI_SSID = "NAMA_WIFI";
const char* WIFI_PASS = "KATALALUAN_WIFI";
#define BOT_TOKEN "TOKEN_DARI_BOTFATHER"

// Chat ID yang dibenarkan mengawal bot. Boleh ID peribadi atau ID group
// Telegram (ID group bermula dengan tanda tolak, contoh "-1001234567890").
const char* CHAT_DIBENARKAN[] = { "123456789" };
const int BIL_CHAT = sizeof(CHAT_DIBENARKAN) / sizeof(CHAT_DIBENARKAN[0]);

// Google Apps Script (biarkan "XXXX" jika belum guna panel web)
const char* SKRIP_URL   = "https://script.google.com/macros/s/AKfycbyR_lLo00KKktvE1oYMdEHWqJ39hsJLjOxLcv9UCarO7XXhM2V-TOQCAVY3WSykdntvog/exec";
const char* SKRIP_KUNCI = "KUNCI_API";

// Pin
const int  PIN_SENSOR      = 34;    // Mesti pin ADC1 (32-39). ADC2 tidak berfungsi bila WiFi aktif
const int  PIN_RELAY       = 26;
const int  PIN_LED         = 2;     // LED biru pada papan, menyala bila pam hidup
const bool RELAY_AKTIF_LOW = true;  // Kebanyakan modul relay murah aktif LOW

// OLED (jika tiada OLED, sistem tetap berjalan seperti biasa)
const int     PIN_SDA     = 21;
const int     PIN_SCL     = 22;
const uint8_t OLED_ALAMAT = 0x3C;    // sesetengah modul guna 0x3D

// Kalibrasi sensor (guna arahan /kalibrasi untuk dapatkan nilai sebenar)
int NILAI_KERING = 3000;  // bacaan raw sensor di udara / tanah kering
int NILAI_BASAH  = 1300;  // bacaan raw sensor dalam air

// Tetapan siraman
const int           AMBANG_LALAI    = 40;     // % kelembapan: bawah nilai ini = kering
const unsigned long MASA_PAM_AUTO   = 5000;   // ms pam hidup setiap kali auto
const unsigned long MASA_PAM_MANUAL = 5000;   // ms lalai untuk siram manual
const unsigned long MASA_PAM_MAKS   = 30000;  // had keselamatan
const unsigned long MASA_RESAP      = 60000;  // ms tunggu air meresap sebelum semak semula
const unsigned long SELA_BACA       = 2000;   // ms antara bacaan sensor
const unsigned long SELA_TELEGRAM   = 1000;   // ms antara semakan Telegram
const unsigned long SELA_SKRIP      = 15000;  // ms antara hubungan dengan Apps Script
const unsigned long SELA_WIFI       = 10000;  // ms antara cubaan sambung semula WiFi
// ===================================================================

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
bool skripOK    = false;  // hubungan terakhir dengan Apps Script berjaya?
bool paksaData  = false;

// Baris gilir log untuk Apps Script (dihantar bila pam tidak hidup)
const int MAKS_LOG = 6;
String logJenis[MAKS_LOG], logMesej[MAKS_LOG];
int bilLog = 0;

// OLED
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledOK = false;
String mesejOled = "";
unsigned long mesejOledTamat = 0, akhirOled = 0;
const unsigned long SELA_OLED = 500;

void paparMesej(const String& m) {
  mesejOled = m.substring(0, 21);  // satu baris OLED = 21 aksara
  mesejOledTamat = millis() + 4000;
}

void oledBoot(const String& baris1, const String& baris2) {
  if (!oledOK) return;
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);  oled.print("KebunDemo v1.3");
  oled.setCursor(0, 24); oled.print(baris1);
  oled.setCursor(0, 40); oled.print(baris2);
  oled.display();
}

void bulatan(int x, bool penuh) {
  if (penuh) oled.fillCircle(x, 3, 3, SSD1306_WHITE);
  else oled.drawCircle(x, 3, 3, SSD1306_WHITE);
}

// ---------------- Fungsi bantu ----------------
void relayTulis(bool hidup) {
  digitalWrite(PIN_RELAY, RELAY_AKTIF_LOW ? !hidup : hidup);
  digitalWrite(PIN_LED, hidup ? HIGH : LOW);
}

bool chatDibenarkan(const String& id) {
  for (int i = 0; i < BIL_CHAT; i++) {
    if (id == CHAT_DIBENARKAN[i]) return true;
  }
  return false;
}

void hantarSemua(const String& mesej) {
  if (WiFi.status() != WL_CONNECTED) return;
  for (int i = 0; i < BIL_CHAT; i++) {
    bot.sendMessage(CHAT_DIBENARKAN[i], mesej, "");
  }
}

void tambahLog(const String& jenis, const String& mesej) {
  if (!skripAktif) return;
  if (bilLog >= MAKS_LOG) {  // penuh: buang yang paling lama
    for (int i = 1; i < MAKS_LOG; i++) { logJenis[i - 1] = logJenis[i]; logMesej[i - 1] = logMesej[i]; }
    bilLog = MAKS_LOG - 1;
  }
  logJenis[bilLog] = jenis;
  logMesej[bilLog] = mesej;
  bilLog++;
}

String urlEncode(const String& s) {
  String o;
  char buf[4];
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else { snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c); o += buf; }
  }
  return o;
}

int bacaRaw() {
  long jumlah = 0;
  for (int i = 0; i < 10; i++) {
    jumlah += analogRead(PIN_SENSOR);
    delay(5);
  }
  return jumlah / 10;
}

int rawKePeratus(int raw) {
  int p = map(raw, NILAI_KERING, NILAI_BASAH, 0, 100);
  return constrain(p, 0, 100);
}

String masaHidup() {
  unsigned long s = millis() / 1000;
  return String(s / 3600) + "j " + String((s % 3600) / 60) + "m";
}

// ---------------- Kawalan pam ----------------
void pamOn(unsigned long ms, const String& sebab) {
  if (pamHidup) return;
  if (ms > MASA_PAM_MAKS) ms = MASA_PAM_MAKS;
  // Notifikasi dahulu, supaya masa hantar mesej tidak memanjangkan siraman
  hantarSemua("💧 Pam HIDUP " + String(ms / 1000) + " saat\nSebab: " + sebab);
  relayTulis(true);
  pamHidup  = true;
  pamMula   = millis();
  pamTempoh = ms;
  jumlahSiraman++;
  paksaData = true;
  tambahLog("PAM", "Pam HIDUP " + String(ms / 1000) + " s - " + sebab);
  Serial.printf("[PAM] HIDUP %lu ms - %s\n", ms, sebab.c_str());
  lukisOled();
}

void pamOff(const String& sebab) {
  if (!pamHidup) return;
  relayTulis(false);  // matikan dahulu, baru hantar notifikasi
  pamHidup    = false;
  akhirSiram  = millis();
  pernahSiram = true;
  paksaData   = true;
  tambahLog("PAM", "Pam MATI - " + sebab);
  Serial.printf("[PAM] MATI - %s\n", sebab.c_str());
  lukisOled();
  hantarSemua("✅ Pam MATI (" + sebab + ")\nKelembapan semasa: " + String(kelembapan) + "%");
}

// Satu fungsi untuk semua sumber arahan (Telegram dan panel web).
// Pulangkan teks balasan; kosong bermaksud notifikasi pam sudah dihantar.
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

// ---------------- Telegram ----------------
String teksStatus() {
  String s = "📊 STATUS PASU\n";
  s += "Kelembapan: " + String(kelembapan) + "% (raw " + String(rawSensor) + ")\n";
  s += "Keadaan: " + String(!sensorOK ? "SENSOR BERMASALAH" : (kelembapan < ambangKering ? "KERING" : "OK")) + "\n";
  s += "Ambang kering: " + String(ambangKering) + "%\n";
  s += "Mod auto: " + String(modAuto ? "ON" : "OFF") + "\n";
  s += "Pam: " + String(pamHidup ? "HIDUP" : "MATI") + "\n";
  s += "Siraman sejak hidup: " + String(jumlahSiraman) + " kali\n";
  s += "Panel web: " + String(skripAktif ? "AKTIF" : "TIDAK DIGUNAKAN") + "\n";
  s += "WiFi: " + String(WiFi.RSSI()) + " dBm\n";
  s += "Masa hidup: " + masaHidup();
  return s;
}

String teksBantuan() {
  return String("🌱 KebunDemo - Siraman Pasu Pintar\n\n") +
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
    if (ruang > 0) {
      arahan = teks.substring(0, ruang);
      nilai  = teks.substring(ruang + 1);
      nilai.trim();
    }
    int at = arahan.indexOf('@');  // buang "@namabot" dalam group
    if (at > 0) arahan = arahan.substring(0, at);
    arahan.toLowerCase();

    if (arahan == "/start" || arahan == "/bantuan" || arahan == "/help") {
      hantarMenu(chatId, teksBantuan());
    } else if (arahan == "/status") {
      bot.sendMessage(chatId, teksStatus(), "");
    } else if (arahan == "/siram" || arahan == "/stop" || arahan == "/auto_on" ||
               arahan == "/auto_off" || arahan == "/ambang") {
      String balas = laksanaArahan(arahan.substring(1), nilai.toInt(), "Telegram");
      paparMesej("Telegram: " + arahan);
      if (balas.length() > 0) bot.sendMessage(chatId, balas, "");
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
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Apps Script redirect ke googleusercontent
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
  if (deserializeJson(doc, respons)) { Serial.println("[SKRIP] JSON tidak sah"); return; }
  if (!(doc["ok"] | false)) { Serial.printf("[SKRIP] Ralat: %s\n", (const char*)(doc["ralat"] | "?")); return; }

  JsonArray senarai = doc["arahan"].as<JsonArray>();
  for (JsonObject a : senarai) {
    String cmd = a["cmd"].as<String>();
    int nilai  = a["nilai"] | 0;
    Serial.printf("[SKRIP] Arahan panel web: %s %d\n", cmd.c_str(), nilai);
    String balas = laksanaArahan(cmd, nilai, "panel web");
    paparMesej("Web: " + cmd + (nilai > 0 ? " " + String(nilai) : ""));
    if (balas.length() > 0) hantarSemua("🖥️ Panel web: " + balas);
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
  unsigned long mula = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mula < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi gagal. Mod auto tetap berjalan tanpa internet.");
  }
}

// ---------------- Setup & loop ----------------
// ---------------- OLED ----------------
// Susun atur 128x64 (OLED dua warna: 16 piksel atas kuning)
//  KebunDemo      W o G o     <- WiFi / panel web (bulatan penuh = OK)
//  Auto:ON   Ambang:40%
//  42%        LEMBAP
//             raw 2310
//  Pam: HIDUP 4s
//  Telegram: /siram        <- mesej sementara / IP
void lukisOled() {
  if (!oledOK) return;
  bool wifi = (WiFi.status() == WL_CONNECTED);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  // Baris atas
  oled.setCursor(0, 0);  oled.print("KebunDemo");
  oled.setCursor(84, 0); oled.print("W");
  bulatan(95, wifi);
  oled.setCursor(106, 0); oled.print("G");
  bulatan(117, skripAktif && skripOK);
  oled.setCursor(0, 8);
  oled.print("Auto:"); oled.print(modAuto ? "ON " : "OFF");
  oled.setCursor(62, 8);
  oled.print("Ambang:"); oled.print(ambangKering); oled.print("%");

  // Kelembapan besar
  oled.setTextSize(3);
  oled.setCursor(0, 20);
  oled.print(sensorOK ? String(kelembapan) + "%" : "--");
  oled.setTextSize(1);
  oled.setCursor(76, 22);
  oled.print(!sensorOK ? "ROSAK!" : (kelembapan < ambangKering ? "KERING" : "LEMBAP"));
  oled.setCursor(76, 34);
  oled.print("raw "); oled.print(rawSensor);

  // Status pam (dengan kiraan detik)
  oled.setCursor(0, 46);
  if (pamHidup) {
    long baki = ((long)pamTempoh - (long)(millis() - pamMula)) / 1000 + 1;
    oled.print("Pam: HIDUP "); oled.print(max(0L, baki)); oled.print("s");
    if ((millis() / 400) % 2) oled.fillRect(122, 46, 6, 7, SSD1306_WHITE);  // berkelip
  } else {
    oled.print("Pam: MATI  ("); oled.print(jumlahSiraman); oled.print("x)");
  }

  // Baris bawah: mesej sementara, atau IP / status
  oled.setCursor(0, 56);
  if (millis() < mesejOledTamat) oled.print(mesejOled);
  else if (wifi) oled.print(WiFi.localIP());
  else oled.print("Luar talian - auto OK");

  oled.display();
}

void setup() {
  Serial.begin(115200);

  // Pastikan pam MATI sebelum pin jadi output (elak pam tersentak semasa boot)
  digitalWrite(PIN_RELAY, RELAY_AKTIF_LOW ? HIGH : LOW);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  relayTulis(false);

  analogReadResolution(12);  // 0-4095

  prefs.begin("kebundemo", false);
  ambangKering = prefs.getInt("ambang", AMBANG_LALAI);
  modAuto      = prefs.getBool("auto", true);

  skripAktif = (String(SKRIP_URL).indexOf("XXXX") < 0);

  klienTelegram.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  Wire.begin(PIN_SDA, PIN_SCL);
  oledOK = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ALAMAT);
  if (!oledOK) Serial.println("[OLED] Tidak dijumpai. Sistem diteruskan tanpa paparan.");
  oledBoot("Menyambung WiFi...", WIFI_SSID);

  sambungWiFi();
  oledBoot(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi GAGAL",
           WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Mod auto tetap jalan");
  delay(1500);

  rawSensor  = bacaRaw();
  kelembapan = rawKePeratus(rawSensor);

  tambahLog("SISTEM", "ESP32 dihidupkan semula");
  paksaData = true;
  hantarSemua("🌱 KebunDemo dalam talian.\nKelembapan: " + String(kelembapan) + "%\nTaip /bantuan untuk senarai arahan.");
}

void loop() {
  unsigned long kini = millis();

  // 1. SENSOR (rasa)
  if (kini - akhirBaca >= SELA_BACA) {
    akhirBaca  = kini;
    rawSensor  = bacaRaw();
    kelembapan = rawKePeratus(rawSensor);
    bool okBaru = (rawSensor > 200 && rawSensor < 4000);  // luar julat = wayar tercabut / rosak
    if (sensorOK && !okBaru) {
      hantarSemua("⚠️ Sensor bermasalah (raw " + String(rawSensor) + "). Mod auto ditangguhkan. Semak sambungan.");
      tambahLog("AMARAN", "Sensor bermasalah, raw " + String(rawSensor));
    }
    if (!sensorOK && okBaru) {
      hantarSemua("✅ Sensor kembali normal.");
      tambahLog("INFO", "Sensor kembali normal");
    }
    sensorOK = okBaru;
    Serial.printf("Raw: %d | Kelembapan: %d%% | Auto: %s | Pam: %s\n",
                  rawSensor, kelembapan, modAuto ? "ON" : "OFF", pamHidup ? "HIDUP" : "MATI");
  }

  // 2. PENGAWAL (fikir): siram jika kering, auto ON, sensor OK, dan masa resap sudah tamat
  bool masaResapTamat = !pernahSiram || (kini - akhirSiram >= MASA_RESAP);
  if (modAuto && sensorOK && !pamHidup && kelembapan < ambangKering && masaResapTamat) {
    // 3. AKTUATOR (buat)
    pamOn(MASA_PAM_AUTO, "Auto: tanah kering (" + String(kelembapan) + "%)");
  }

  // Matikan pam bila tempoh tamat (had keselamatan sentiasa aktif)
  if (pamHidup && (millis() - pamMula >= pamTempoh)) {
    pamOff("tempoh siraman tamat");
  }

  // Paparan OLED
  if (millis() - akhirOled >= SELA_OLED) {
    akhirOled = millis();
    lukisOled();
  }

  // 4. TELEFON (lapor & terima arahan)
  if (WiFi.status() == WL_CONNECTED) {
    // Panel 1: Telegram
    if (millis() - akhirBot >= SELA_TELEGRAM) {
      akhirBot = millis();
      int bil = bot.getUpdates(bot.last_message_received + 1);
      while (bil) {
        urusMesej(bil);
        bil = bot.getUpdates(bot.last_message_received + 1);
      }
    }
    // Panel 2: Apps Script + panel web
    if (skripAktif) {
      unsigned long sejakSkrip = millis() - akhirSkrip;
      bool tiba = (sejakSkrip >= SELA_SKRIP) || (paksaData && !pamHidup && sejakSkrip >= 3000);
      if (tiba) {
        akhirSkrip = millis();
        pollSkrip();
      } else if (!pamHidup && bilLog > 0) {
        hantarLogSeterusnya();
      }
    }
  } else if (kini - akhirWifi >= SELA_WIFI) {
    akhirWifi = kini;
    Serial.println("WiFi terputus, cuba sambung semula...");
    WiFi.reconnect();
  }
}
