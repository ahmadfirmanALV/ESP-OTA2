/*
  ESP32 (30-pin) - Tes OTA + WiFiManager + LED internal (GPIO2)

  Library yang dibutuhkan:
    - "WiFiManager" by tzapu (install lewat Library Manager)
    - ArduinoOTA & WiFi sudah bawaan board package ESP32

  Setting Arduino IDE:
    - Board      : ESP32 Dev Module
    - Partition  : Default 4MB with spiffs (JANGAN pilih "No OTA")
    - Upload pertama harus lewat kabel USB
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>

// ===================== KONFIGURASI =====================
#define LED_PIN 2                          // LED biru internal (D2 / GPIO2)

const char* AP_NAME      = "ESP32-Setup";  // nama hotspot untuk konfigurasi WiFi
const char* AP_PASSWORD  = "12345678";     // password hotspot (min. 8 karakter)

const char* OTA_HOSTNAME = "esp32-ota";    // nama yang muncul di Port jaringan Arduino IDE
const char* OTA_PASSWORD = "ota12345";     // password OTA (kosongkan "" kalau tidak mau)

// false = LED nyala terus
// true  = LED kedip-kedip (tinggal ganti ini, lalu upload via OTA untuk tes)
const bool  BLINK_MODE     = false;
const unsigned long BLINK_INTERVAL = 500;  // ms
// =======================================================

unsigned long lastBlink = 0;
bool ledState = false;

void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("OTA mulai: " + type);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA selesai, restart...");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA progress: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error [%u]: ", error);
    if (error == OTA_AUTH_ERROR)         Serial.println("Auth gagal");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin gagal");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect gagal");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive gagal");
    else if (error == OTA_END_ERROR)     Serial.println("End gagal");
  });

  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 OTA + WiFiManager ===");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);             // LED langsung nyala

  // ---------- WiFiManager ----------
  WiFiManager wm;
  // wm.resetSettings();                   // buka komentar ini SEKALI kalau mau hapus WiFi tersimpan
  wm.setConfigPortalTimeout(180);          // portal tutup otomatis setelah 3 menit

  wm.setAPCallback([](WiFiManager* mgr) {
    Serial.println("Mode konfigurasi aktif.");
    Serial.print("Sambungkan HP/laptop ke WiFi: ");
    Serial.println(AP_NAME);
    Serial.println("Lalu buka http://192.168.4.1");
  });

  if (!wm.autoConnect(AP_NAME, AP_PASSWORD)) {
    Serial.println("Gagal konek WiFi / timeout, restart...");
    delay(1000);
    ESP.restart();
  }

  Serial.print("Terhubung ke WiFi: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // ---------- OTA ----------
  setupOTA();
  Serial.println("OTA siap. Pilih Port jaringan di Arduino IDE.");
}

void loop() {
  ArduinoOTA.handle();                     // WAJIB sering dipanggil, jangan pakai delay() panjang

  if (BLINK_MODE) {
    unsigned long now = millis();
    if (now - lastBlink >= BLINK_INTERVAL) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  } else {
    digitalWrite(LED_PIN, HIGH);           // nyala terus
  }
}
