#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ----------------------------
// Definisi WiFi & Server
String ssid = "Juan";
String pass = "tanoto1510";
String serverUrl = "http://nga-untransforming-rick.ngrok-free.dev";

// ----------------------------
// Definisi Variabel yang boleh diubah
#define MAX_JADWAL 3
#define intervalbacaSensor 1000
#define delayLoop 250
#define intervalCekJadwal 30000
#define durasiBukaManual 10000
#define JARAK_KOSONG_CM 20.0
#define JARAK_PENUH_CM 3.0

// ----------------------------
// Definisi PIN
#define pin_Button 27
#define pin_IN1 26
#define pin_IN2 25
#define pin_Servo 4
#define pinXSHUTSensor 18

// ----------------------------
// Definisi konfigurasi NTP 
const char* ntpServer = "pool.ntp.org"; 
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0; 

// ----------------------------
// Definisi Global Variabel
bool sensorTerhubung = true;
bool sistemTerpicu = false;
uint16_t pakanJarak = 0;
String waktuTerakhirTerpicu = ""; // Mencegah pakan terpicu berulang di menit yang sama
String jsonJadwalLama = "";
String triggerSourcePakan;

String waktuLokal;
int jumlahJadwal = 0;

u_int32_t durasiBuka;

u_int32_t lastloop = 0;
u_int32_t lastbacaSensor = 0;
u_int32_t lastbukaPakan = 0;
u_int32_t lastinisiasiSensor = 0;
u_int32_t lastinisiasiWifi = 0;
u_int32_t lastkirimData = 0;
u_int32_t lastCekJadwal = 0;

// ----------------------------
// Pembuatan Objek
Servo servo;
VL53L0X sensor;
WiFiClient client;
HTTPClient http;
struct tm timeinfo;
struct JadwalPakan {
    String feedingTime;
    int durationSeconds;
}; 
JadwalPakan daftarJadwal[MAX_JADWAL];
Preferences preferences;


// ----------------------------
// Fungsi Servo
void setServo(u_int16_t sudut){ // sudut 0-180
  servo.write(sudut);
  Serial.printf("Servo Gerak ke: %do\n", sudut);
}

void inisiasiServo(){
  servo.setPeriodHertz(50);
  servo.attach(pin_Servo, 500, 2400);
  servo.write(0);
  Serial.println("Servo diinisiasi dan berada di posisi 0 derajat");
}

// ----------------------------
// Fungsi Relay
void setRelay(bool state){ // state: HIGH/LOW
  Serial.printf("Relay berada pada posisi %d\n", state);
  digitalWrite(pin_IN1, state);
  digitalWrite(pin_IN2, state);
}

void inisiasiRelay(){
  pinMode(pin_IN1, OUTPUT);
  pinMode(pin_IN2, OUTPUT);
  digitalWrite(pin_IN1, HIGH);
  digitalWrite(pin_IN2, HIGH);
}

// ----------------------------
// Fungsi buka tutup Pakan
void bukaPakan(bool state){ //state: true/false
  if (state){
    setRelay(LOW);
    setServo(90);
  }
  else{
    setRelay(HIGH);
    setServo(0);
  }
}

// ----------------------------
// Fungsi Sensor VL53L0X
void inisiasiSensor(){
  sensor.setTimeout(50);
  sensor.stopContinuous();
  digitalWrite(pinXSHUTSensor, LOW);
  delay(10);
  digitalWrite(pinXSHUTSensor,HIGH);
  delay(10);

  if (!sensor.init()) {
      Serial.println("Gagal menemukan sensor VL53 saat booting.");
      sensorTerhubung = false;
  }
  else{
      Serial.println("Sensor Berhasil Booting");
      sensor.startContinuous();
      sensorTerhubung = true;
  }
}

void bacaSensor(){
  if (sensorTerhubung) {
    uint16_t measure = sensor.readRangeContinuousMillimeters();
    if (measure <= 2000){
      pakanJarak = measure;
      Serial.printf("Jarak : %d mm\n", pakanJarak);
    }

    else if (measure >= 65535){
      Serial.printf(
        "TIMEOUT, ada kabel lepas di sensor. Kode:%d\n",
        measure
      );
      sensorTerhubung=false;
    }

    else if (measure >= 8190){
      Serial.printf(
        "OUT OF RANGE. Kode:%d\n",
        measure
      );
    }
  }

  else {
    if (millis()-lastinisiasiSensor >= 10000){
      lastinisiasiSensor = millis();
      Serial.println("Mencoba Reconnect Sensor...");
      inisiasiSensor();
    }
  }
}

// ----------------------------
// Fungsi kirim dan get ke Backend
void kirimDataSensor(uint16_t jarakMm){
  // Pastikan WiFi tersambung
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi belum tersambung. Data tidak dikirim.");
    return;
  }

  float jarakCm = jarakMm / 10.0;

  int remainingPct = map(jarakCm, JARAK_KOSONG_CM, JARAK_PENUH_CM, 0, 100);
  remainingPct = constrain(remainingPct, 0, 100); // Kunci agar nilainya selalu 0% - 100%

  String url = serverUrl + "/api/logs/telemetry";

  Serial.println("Mengirim data ke:");
  Serial.println(url);

  // Mulai HTTP connection
  http.begin(client, url);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  String jsonData = "{";
  jsonData += "\"distanceCm\":" + String(jarakCm, 1) + ",";
  jsonData += "\"remainingPct\":" + String(remainingPct);
  jsonData += "}";

  Serial.println("Data JSON:");
  Serial.println(jsonData);

  // Kirim POST
  int httpResponseCode = http.POST(jsonData);

  // Cek hasil
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response Code: %d (Sisa Pakan: %d%%)\n", httpResponseCode, remainingPct);
    String response = http.getString();
    Serial.println("Response Server:");
    Serial.println(response);
  }
  else {
    Serial.printf(
      "Gagal mengirim HTTP. Error: %s\n",
      http.errorToString(httpResponseCode).c_str()
    );
  }
  // Tutup koneksi HTTP
  http.end();
}

void kirimFeedingLog(String triggerSource, float durationSeconds){
  if (WiFi.status() != WL_CONNECTED) { 
    Serial.println("WiFi belum tersambung. Log feeding tidak dikirim."); 
    return; 
  }

  String url = serverUrl + "/api/logs/feeding-event";

  http.begin(client, url); 
  
  http.addHeader("Content-Type", "application/json"); // Format JSON sesuai ekspektasi logController.js (triggerSource &amp; durationSeconds) 
  http.addHeader("ngrok-skip-browser-warning", "true");

  String jsonData = "{"; 
  jsonData += "\"triggerSource\":\"" + triggerSource + "\",";
  jsonData += "\"durationSeconds\":" + String(durationSeconds); 
  jsonData += "}"; 
  
  int httpResponseCode = http.POST(jsonData); 
  
  if (httpResponseCode > 0) { 
    Serial.printf("✅ Feeding Log HTTP Response Code: %d\n", httpResponseCode);
  } 
  else {
     Serial.printf("❌ Gagal kirim Feeding Log. Error: %s\n",
      http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void cekJadwalServer() { 
  if (WiFi.status() != WL_CONNECTED) return; 

  String url = serverUrl + "/api/schedules"; 
  
  http.begin(client, url); 
  http.addHeader("ngrok-skip-browser-warning", "true");

  int httpResponseCode = http.GET(); 
  if (httpResponseCode == 200) { 
    String payloadBaru = http.getString(); 
    if (payloadBaru != jsonJadwalLama){
      Serial.println("🔄 Perubahan jadwal terdeteksi! Memperbarui Preferences (Flash)...");
      jsonJadwalLama = payloadBaru;

      preferences.putString("schedules", payloadBaru);
      parseJadwalServer(payloadBaru);
    }
    else{
      // Serial.println("Tidak ada perubahan");
    }
  }
  else { 
    Serial.printf("❌ Gagal mengambil jadwal. HTTP Code: %d\n", httpResponseCode);
  }
  http.end(); 
}

// ----------------------------
// Inisiasi WiFi
void inisiasiWifi(){
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(),pass.c_str());
    WiFi.setAutoReconnect(true);
}

// ----------------------------
// Fungsi Pewaktuan
String getwaktuLokal(){
    if(!getLocalTime(&timeinfo)){
        Serial.println("Gagal mendapatkan waktu NTP Server");
        return "";
    }

    char timeFormatted[6];
    strftime(timeFormatted, sizeof(timeFormatted), "%H:%M", &timeinfo);

    return String(timeFormatted);
}

// ----------------------------
// Fungsi Parsing Json
void parseJadwalServer(String jsonPayload) { 
    DynamicJsonDocument doc(1024); 
    DeserializationError err = deserializeJson(doc, jsonPayload);

    if (!err && doc["success"] == true) { 
        JsonArray schedules = doc["data"].as<JsonArray>();
        jumlahJadwal = 0;
        
        for (JsonObject sch : schedules){
            if (jumlahJadwal >= MAX_JADWAL){
                Serial.println("⚠️ Batas Jadwal sudah tercapai, mengabaikan sisanya");
                break;
            }

            daftarJadwal[jumlahJadwal].feedingTime = sch["feedingTime"].as<String>();
            daftarJadwal[jumlahJadwal].durationSeconds = sch["durationSeconds"] | 10;
            jumlahJadwal++;
        }

        Serial.printf("✅ Berhasil memuat %d jadwal ke dalam ESP32. \n", jumlahJadwal);
    }
}

// ----------------------------
void setup(){
  // Begin
  Serial.begin(115200);
  Wire.begin();

  // Inisiasi Preference
  preferences.begin("feeder", false); 

  jsonJadwalLama = preferences.getString("schedules", ""); 
  if (jsonJadwalLama.length() > 0) { 
    Serial.println("📂 Memuat jadwal cadangan dari Preferences (Flash)..."); 
    parseJadwalServer(jsonJadwalLama); 
  }
  else {
    Serial.println("ℹ️ Belum ada jadwal tersimpan di Preferences.");
  }


  // Inisiasi WiFi
  inisiasiWifi();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Inisiasi Pin
  pinMode(pinXSHUTSensor,OUTPUT);
  pinMode(pin_Button, INPUT_PULLDOWN);

  // Panggil Fungsi untuk Setup
  Serial.println("Mencoba Inisiasi Relay, Servo, Sensor");

  inisiasiRelay();
  inisiasiServo();
  inisiasiSensor();

  Serial.println("Inisiasi Selesai");
}

// ----------------------------
void loop(){
  // Baca Sensor
  if (millis() - lastbacaSensor >= intervalbacaSensor){
    lastbacaSensor = millis();
    bacaSensor();
  }

  // kirim data sensor
  if (millis() - lastkirimData >= 10000){
    lastkirimData = millis();
    kirimDataSensor(pakanJarak);
    Serial.println("Mengirim Data Sensor");
  }
  
  // minta jadwal ke backend
  if (millis() - lastCekJadwal >= intervalCekJadwal){ 
    lastCekJadwal = millis(); 
    cekJadwalServer(); 
  }
  
  // main loop
  if (millis() - lastloop >= delayLoop)
  {
    lastloop = millis();
    waktuLokal = getwaktuLokal();

    // Pemicu: Jadwal
    if (waktuLokal != "" && !sistemTerpicu) {
        bool jadwalCocok = false;

        for (int i = 0; i < jumlahJadwal; i++) {
            if(daftarJadwal[i].feedingTime == waktuLokal){
                if (waktuTerakhirTerpicu != waktuLokal){
                    waktuTerakhirTerpicu = waktuLokal;
                    triggerSourcePakan = "SCHEDULED";
                    durasiBuka = daftarJadwal[i].durationSeconds * 1000; // Konversi detik ke ms

                    bukaPakan(true);
                    lastbukaPakan = millis();
                    sistemTerpicu = true;
                    Serial.printf("⏰ Pakan Otomatis Terpicu pada Jadwal [%s]!\n", waktuLokal.c_str());
                }
                jadwalCocok = true;
                break;
            }
        }

        // Jika di menit ini tidak ada jadwal yang cocok, reset pembatas menit 
        if (!jadwalCocok) { waktuTerakhirTerpicu = ""; }
    }
    
    // Pemicu: Button
    if (digitalRead(pin_Button) && !sistemTerpicu){
      triggerSourcePakan = "MANUAL";
      durasiBuka = durasiBukaManual;
      bukaPakan(true);

      lastbukaPakan = millis();
      sistemTerpicu = true;
      Serial.println("Tombol fisik ditekan");

    }

    // Penutupan Pakan & kirim Log 
    if ((millis() - lastbukaPakan >= durasiBuka) && sistemTerpicu){
      sistemTerpicu = false;
      bukaPakan(false);
      kirimFeedingLog(triggerSourcePakan, durasiBuka/1000.0);
    }
  }
}