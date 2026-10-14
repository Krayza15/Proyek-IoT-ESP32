#include <Arduino.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

// ----------------------------
// Definisi WiFi & Server
String ssid = "Juan";
String pass = "tanoto1510";
String serverUrl = "http://nga-untransforming-rick.ngrok-free.dev";

// ----------------------------
// Definisi PIN
#define pin_Button 27
#define pin_IN1 26
#define pin_IN2 25
#define pin_Servo 4
#define pinXSHUTSensor 18

// ----------------------------
// Definisi Global Variabel
bool sensorTerhubung = true;
bool buttonDitekan = false;
uint16_t pakanJarak = 0;
const u_int16_t intervalbacaSensor = 1000;
u_int16_t durasiBuka = 10000;
u_int8_t delayLoop = 100;

u_int32_t lastloop = 0;
u_int32_t lastbacaSensor = 0;
u_int32_t lastbukaPakan = 0;
u_int32_t lastinisiasiSensor = 0;
u_int32_t lastinisiasiWifi = 0;
u_int32_t lastkirimData = 0;

// ----------------------------
// Pembuatan Objek
Servo servo;
VL53L0X sensor;
WiFiClient client;
HTTPClient http;

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
  digitalWrite(pinXSHUTSensor,HIGH);

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

void kirimDataSensor(uint16_t jarakMm){
  // Pastikan WiFi tersambung
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi belum tersambung. Data tidak dikirim.");
    return;
  }

  float jarakCm = jarakMm / 10.0;

  String url = serverUrl + "/api/logs/telemetry";

  Serial.println("Mengirim data ke:");
  Serial.println(url);

  // Mulai HTTP connection
  http.begin(client, url);

  // Header bahwa data yang dikirim adalah JSON
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true"); // <-- TAMBAHKAN INI

  String jsonData = "{";
  jsonData += "\"distanceCm\":" + String(jarakCm, 1);
  jsonData += "}";

  Serial.println("Data JSON:");
  Serial.println(jsonData);

  // Kirim POST
  int httpResponseCode = http.POST(jsonData);

  // Cek hasil
  if (httpResponseCode > 0) {
    Serial.printf("HTTP Response Code: %d\n", httpResponseCode);
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

  http.begin(client, url); http.addHeader("Content-Type", "application/json"); // Format JSON sesuai ekspektasi logController.js (triggerSource &amp; durationSeconds) 
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

// ----------------------------
// Inisiasi WiFi
void inisiasiWifi(){
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(),pass.c_str());
    WiFi.setAutoReconnect(true);
}

// ----------------------------
void setup(){
  // Begin
  Serial.begin(115200);
  Wire.begin();

  // Inisiasi WiFi
  inisiasiWifi();

  // Inisiasi Pin
  pinMode(pinXSHUTSensor,OUTPUT);
  pinMode(pin_Button, INPUT);

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

  if (millis() - lastkirimData >= 10000){
    lastkirimData = millis();
    kirimDataSensor(pakanJarak);
    Serial.println("Mengirim Data Sensor");
  }
  
  // main loop
  if (millis() - lastloop >= delayLoop)
  {
    lastloop = millis();
    // Ketika tombol ditekan
    if (digitalRead(pin_Button)){
      bukaPakan(true);
      lastbukaPakan = millis();
      buttonDitekan = true;
    }

    // Ketika waktu buka habis
    if ((millis() - lastbukaPakan >= durasiBuka) && buttonDitekan){
      buttonDitekan = false;
      bukaPakan(false);
      kirimFeedingLog("MANUAL", durasiBuka/1000.0);
    }
  }
}