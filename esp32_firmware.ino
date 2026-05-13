#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
// ================= KONFIGURASI =================

// WiFi
const char* ssid = "Woss Koss";
const char* password = "Bedjo3168";

// MQTT (HiveMQ)
const char* mqtt_server = "7bca26641abc4f9e9bebe7ce4647e2db.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char* mqtt_user = "aprilio";
const char* mqtt_password = "7AJ2aprilio";
const char* mqtt_topic_publish = "iot/sensor/data";
const char* mqtt_topic_subscribe = "iot/control";

// Pin Sensor dan Aktuator
#define DHTPIN 4
#define DHTTYPE DHT11
#define SOIL_PIN 34
#define RELAY_PIN 5
LiquidCrystal_I2C lcd(0x27, 16, 2);

// NTP Server (Waktu)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

#include <WiFiClientSecure.h>

// Variabel Global
DHT dht(DHTPIN, DHTTYPE);
WiFiClientSecure espClient;
PubSubClient client(espClient);

String currentMode = "auto";
bool pumpState = false;
String schedule1 = "12:00"; 
String schedule2 = "17:00"; 
unsigned long lastMsg = 0;
bool scheduledWateringActive = false; 
unsigned long scheduledWateringStartTime = 0; 
bool scheduleEnabled = true; 

// ================= FUNGSI-FUNGSI =================

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.println(msg);

  // Parsing JSON dengan ArduinoJson
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Update Mode
  if (doc.containsKey("mode")) {
    currentMode = doc["mode"].as<String>();
  }

  // Update Jadwal
  if (doc.containsKey("schedule1")) {
    if (doc["schedule1"].is<long>()) {
      long ms = doc["schedule1"].as<long>();
      int total_mins = ms / 60000;
      char buff[6];
      sprintf(buff, "%02d:%02d", total_mins / 60, total_mins % 60);
      schedule1 = String(buff);
    } else {
      schedule1 = doc["schedule1"].as<String>();
    }
    Serial.print("Jadwal 1 diupdate menjadi: "); Serial.println(schedule1);
  }
  if (doc.containsKey("schedule2")) {
    if (doc["schedule2"].is<long>()) {
      long ms = doc["schedule2"].as<long>();
      int total_mins = ms / 60000;
      char buff[6];
      sprintf(buff, "%02d:%02d", total_mins / 60, total_mins % 60);
      schedule2 = String(buff);
    } else {
      schedule2 = doc["schedule2"].as<String>();
    }
    Serial.print("Jadwal 2 diupdate menjadi: "); Serial.println(schedule2);
  }
  if (doc.containsKey("schedule_enabled")) {
    scheduleEnabled = doc["schedule_enabled"].as<int>() == 1;
  }

  // Kontrol Manual (Hanya berlaku jika mode manual)
  if (currentMode == "manual" && doc.containsKey("pump")) {
    int pumpCmd = doc["pump"].as<int>();
    if (pumpCmd == 1) {
      turnOnPump();
    } else {
      turnOffPump();
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    // HiveMQ Cloud mewajibkan penggunaan username & password
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
      client.subscribe(mqtt_topic_subscribe);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  espClient.setInsecure(); 
  
  pinMode(RELAY_PIN, OUTPUT);
  turnOffPump();

  dht.begin();
  
  setup_wifi();
  
  // Setup MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Setup Waktu (NTP)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // Setup OTA
  ArduinoOTA.setHostname("ESP32-IoT");
  ArduinoOTA.setPassword("admin123"); 
  ArduinoOTA.begin();
}

void turnOnPump() {
  if (!pumpState) {
    digitalWrite(RELAY_PIN, HIGH); 
    pumpState = true;
    Serial.println("Pompa MENYALA");
    publishData(); 
  }
}

void turnOffPump() {
  if (pumpState) {
    digitalWrite(RELAY_PIN, LOW); 
    pumpState = false;
    scheduledWateringActive = false; 
    Serial.println("Pompa MATI");
    publishData();
  }
}

String getTimeString() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "00:00";
  }
  char timeStringBuff[10];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M", &timeinfo);
  return String(timeStringBuff);
}

void checkScheduleAndSoil() {
  // Baca sensor
  int soilRaw = analogRead(SOIL_PIN);
    
  // --- BANTUAN KALIBRASI ---
  // Tampilkan nilai mentah (RAW) ke Serial Monitor agar kita tahu nilai aslinya saat kering
  Serial.print("Nilai RAW Sensor Tanah: ");
  Serial.println(soilRaw);
  
  // Konversi ke persentase (Nilai analog ESP32 0-4095. Asumsi: 4095=Kering, 0=Basah)
  // Berdasarkan kalibrasi Anda: Kering ~2600. Asumsi Basah ~1200.
  int soilMoisture = map(soilRaw, 2600, 1100, 0, 100);
  if (soilMoisture < 0) soilMoisture = 0;
  if (soilMoisture > 100) soilMoisture = 100;

  if (currentMode == "auto") {
    String currentTime = getTimeString();
    if (scheduleEnabled && (currentTime == schedule1 || currentTime == schedule2) && !scheduledWateringActive) {
      Serial.println("Penyiraman terjadwal dimulai (5 Menit)!");
      scheduledWateringActive = true;
      scheduledWateringStartTime = millis();
      if (!pumpState) turnOnPump();
    }


    if (scheduledWateringActive) {
      if (millis() - scheduledWateringStartTime >= 5 * 60 * 1000) {
         Serial.println("Waktu penyiraman terjadwal (5 menit) telah selesai.");
         turnOffPump(); 
      }
    } else {
      if (soilMoisture < 30) {
        if (!pumpState) turnOnPump();
      } else if (soilMoisture > 60) {
        if (pumpState) turnOffPump();
      }
    }
  }
}

void publishData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int soilRaw = analogRead(SOIL_PIN);
  int soilMoisture = map(soilRaw, 2600, 1050, 0, 100);

  if (isnan(h) || isnan(t)) {
    Serial.println("Gagal membaca DHT!");
    return;
  }
  lcd.clear();

  // Baris pertama
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(t, 1);
  lcd.print((char)223); 
  lcd.print("C ");

  lcd.print("H:");
  lcd.print((int)h);
  lcd.print("%");

  // Baris kedua
  lcd.setCursor(0, 1);
  lcd.print("Soil:");
  lcd.print(soilMoisture);
  lcd.print("%");

  // Format JSON menggunakan ArduinoJson
  StaticJsonDocument<256> doc;
  doc["suhu"] = t;
  doc["kelembaban_udara"] = h;
  doc["kelembaban_tanah"] = constrain(soilMoisture, 0, 100);
  doc["status_pompa"] = pumpState ? 1 : 0;
  doc["mode"] = currentMode;

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_topic_publish, jsonBuffer);
  Serial.print("Data dikirim: ");
  Serial.println(jsonBuffer);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  ArduinoOTA.handle();

  unsigned long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
    
    checkScheduleAndSoil();
    publishData();

  }
}
