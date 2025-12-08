/*
 * ==========================================================
 * Context-Aware Campus Bus Tracker (IoT Final Project)
 * HALTE UNIT (Terminal Display Device)
 *
 * Hardware:
 *   - ESP32 Dev Board
 *   - LCD I2C 16x2 (via PCF8574 backpack)
 *   - WiFi hotspot for Internet connectivity
 *   - MQTT broker as cloud communication layer
 *
 * Purpose:
 *   • Subscribe to bus location updates via MQTT
 *   • Interpret bus's current zone relative to THIS halte
 *   • Display simple, glanceable arrival information
 *   • Configurable for *any* halte in the campus loop
 *
 * Wiring:
 *   LCD VCC  -> ESP32 VIN (5V)
 *   LCD GND  -> ESP32 GND
 *   LCD SDA  -> ESP32 GPIO21
 *   LCD SCL  -> ESP32 GPIO22
 *
 * Required Libraries:
 *   - PubSubClient       by Nick O'Leary
 *   - ArduinoJson        by Benoit Blanchon
 *   - LiquidCrystal_I2C  fork supporting .begin(16,2)
 *
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// ==========================
// 🔧 CONFIGURATION (EDIT THESE)
// ==========================
#define MY_HALTE_ID   "FT"          // Which halte this device represents
String RELEVANT_LINE  = "BLUE";       // "RED", "BLUE", or "BOTH"

// WiFi hotspot credentials
const char* WIFI_SSID = "OrganicTrash";
const char* WIFI_PASS = "oops1112";

// MQTT broker
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "bikun/location"; // Sesuaikan dengan topik bus

// ==========================
// LCD Setup
// ==========================
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ==========================
// Route Data
// ==========================
struct Zone { const char* name; double lat; double lon; };

const Zone redRoute[] = {
  {"ASRAMA",-6.348191,106.829705}, {"MENWA",-6.353438,106.831816},
  {"STASIUN",-6.360717,106.831752}, {"FH",-6.364840,106.832226},
  {"BALAIRUNG",-6.368286,106.831712}, {"RIK",-6.370153,106.831040},
  {"FKM",-6.371713,106.829113}, {"FMIPA",-6.369845,106.825761},
  {"FT",-6.361060,106.823179}, {"FEB",-6.359419,106.825684},
  {"FISIP",-6.361711,106.830344}, {"STASIUN",-6.360717,106.831752},
  {"MENWA",-6.353438,106.831816}
};
const int NUM_RED_ZONES = sizeof(redRoute) / sizeof(redRoute[0]);

const Zone blueRoute[] = {
  {"ASRAMA",-6.348191,106.829705}, {"MENWA",-6.353438,106.831816},
  {"STASIUN",-6.360778,106.831417}, {"FIB",-6.360887,106.829157},
  {"FEB",-6.359658,106.825722}, {"FT",-6.361258,106.823307},
  {"STASIUN",-6.360778,106.831417}, {"MENWA",-6.353438,106.831816}
};
const int NUM_BLUE_ZONES = sizeof(blueRoute) / sizeof(blueRoute[0]);


// ==========================
// Bus State Object
// ==========================
struct BusState {
  String bus_id = "";
  String line = "";
  String currentZone = "---";
  int lastKnownIndex = -1;
  unsigned long lastUpdateTime = 0; // [BARU] Untuk timeout
};

BusState redBus;
BusState blueBus;

const unsigned long BUS_TIMEOUT_MS = 30000; // 30 detik

// ==========================
// Utilities
// ==========================
int findNextInstanceOfZone(const char* targetZone, const Zone* route, int routeSize, int startIndex) {
  // Jika ini pencarian pertama, cari di seluruh array
  if (startIndex <= 0) {
    for (int i = 0; i < routeSize; i++) {
      if (strcmp(route[i].name, targetZone) == 0) return i;
    }
  } else { // Jika tidak, lanjutkan pencarian dari posisi terakhir
    // Cari dari titik awal ke depan
    for (int i = startIndex; i < routeSize; i++) {
      if (strcmp(route[i].name, targetZone) == 0) return i;
    }
    // Jika tidak ketemu, cari dari awal (loop back)
    for (int i = 0; i < startIndex; i++) {
      if (strcmp(route[i].name, targetZone) == 0) return i;
    }
  }
  return -1; // Tidak ditemukan
}


String checkRelation(int busIndex, int halteIndex) {
  if (busIndex == -1) return "Unknown";
  if (busIndex == halteIndex) return "Arrived";
  if (busIndex < halteIndex) return "Approaching";
  return "Passed";
}

// ==========================
// LCD Display Logic
// ==========================
void displayStatus() {
  lcd.clear();
  Serial.println("[LCD] --- Update Display ---");

  bool showRed = (RELEVANT_LINE == "RED" || RELEVANT_LINE == "BOTH");
  bool showBlue = (RELEVANT_LINE == "BLUE" || RELEVANT_LINE == "BOTH");

  // Jika tidak ada data sama sekali, tampilkan pesan tunggu
  if (redBus.lastUpdateTime == 0 && blueBus.lastUpdateTime == 0) {
    lcdPrint(0, 0, "Halte: " + String(MY_HALTE_ID));
    lcdPrint(0, 1, "Menunggu data...");
    return;
  }

  if (showRed) {
    if (millis() - redBus.lastUpdateTime > BUS_TIMEOUT_MS) {
      lcdPrint(0, 0, "M:Sinyal Hilang ");
    } else {
      lcdPrint(0, 0, "M:" + redBus.currentZone + "      ");
    }
  }

  if (showBlue) {
    if (millis() - blueBus.lastUpdateTime > BUS_TIMEOUT_MS) {
      lcdPrint(0, 1, "B:Sinyal Hilang ");
    } else {
      lcdPrint(0, 1, "B:" + blueBus.currentZone + "      ");
    }
  }
}

// ==========================
// MQTT Callback
// ==========================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload, length);

  String line_id = doc["line_id"];
  const char* current_zone = doc["current_zone"];

  BusState* busToUpdate = nullptr;
  const Zone* route = nullptr;
  int routeSize = 0;

  // Filter dan pilih bus yang akan diupdate
  if (line_id == "RED" && (RELEVANT_LINE == "RED" || RELEVANT_LINE == "BOTH")) {
    busToUpdate = &redBus;
    route = redRoute;
    routeSize = NUM_RED_ZONES;
  } else if (line_id == "BLUE" && (RELEVANT_LINE == "BLUE" || RELEVANT_LINE == "BOTH")) {
    busToUpdate = &blueBus;
    route = blueRoute;
    routeSize = NUM_BLUE_ZONES;
  } else {
    return; // Pesan tidak relevan untuk halte ini, abaikan
  }

  // Lakukan pencarian cerdas
  int newIndex = findNextInstanceOfZone(current_zone, route, routeSize, busToUpdate->lastKnownIndex + 1);
  
  if (newIndex != -1) {
    busToUpdate->lastKnownIndex = newIndex;
  }
  
  // Update data bus
  busToUpdate->bus_id = doc["bus_id"].as<String>();
  busToUpdate->line = line_id;
  busToUpdate->currentZone = current_zone;
  busToUpdate->lastUpdateTime = millis();

  // Panggil fungsi display untuk memperbarui LCD
  displayStatus();
}

// ==========================
// WiFi + MQTT Connection
// ==========================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (!WiFi.isConnected()) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void connectMQTT() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  while (!mqttClient.connected()) {
    Serial.print(".");
    mqttClient.connect("HalteClient");
    delay(400);
  }
  mqttClient.subscribe(MQTT_TOPIC);
  Serial.println("MQTT Connected!");
}

// ==========================
// Debug to serial
// ==========================
void lcdPrint(int col, int row, const String &text) {
  lcd.setCursor(col, row);
  lcd.print(text);

  // Debug: mirror to Serial
  Serial.printf("[LCD] (%d,%d) %s\n", col, row, text.c_str());
}


// ==========================
// Main Loop
// ==========================
void setup() {
  Serial.begin(115200);
  delay(3000);

  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Booting Halte...");
  lcd.setCursor(0, 1); lcd.print(MY_HALTE_ID);
  
  connectWiFi();
  connectMQTT();
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Sistem Online");
  delay(2000);
  displayStatus();
}

unsigned long lastDisplayUpdate = 0;

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  // Periksa timeout bus dan perbarui display setiap 5 detik
  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    displayStatus();
  }
}
