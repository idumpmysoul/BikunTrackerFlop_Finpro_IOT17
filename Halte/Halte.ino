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
String MY_HALTE_TYPE  = "DEDICATED"; // "DEDICATED" or "SHARED"
String RELEVANT_LINE  = "RED";       // "RED", "BLUE", or "BOTH"

// WiFi hotspot credentials
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

// MQTT broker
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC = "campusbus/location";

// ==========================
// LCD Setup
// ==========================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================
// Route Data
// ==========================
const char* redRoute[] = {"STASIUN", "ASRAMA", "FT", "FE", "FIB", "STASIUN"};
const int NUM_RED = 6;

const char* blueRoute[] = {"STASIUN", "ASRAMA", "FISIP", "FH", "FPsi", "STASIUN"};
const int NUM_BLUE = 6;

// ==========================
// Bus State Object
// ==========================
struct BusState {
  String bus_id = "";
  String line = "";
  String zone = "UNKNOWN";
  int zoneIndex = -1;
  String relation = "Unknown"; // "Approaching", "Passed", "Arrived"
};

BusState redBus;
BusState blueBus;

// ==========================
// Utilities
// ==========================
int findIndexInRoute(const char* target, const char** route, int count) {
  for (int i = 0; i < count; i++) {
    if (strcmp(target, route[i]) == 0) return i;
  }
  return -1;
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

  bool showRed = (RELEVANT_LINE == "RED" || RELEVANT_LINE == "BOTH");
  bool showBlue = (RELEVANT_LINE == "BLUE" || RELEVANT_LINE == "BOTH");

  BusState* primary = nullptr;
  BusState* secondary = nullptr;

  if (showRed && redBus.bus_id != "") primary = &redBus;
  if (!primary && showBlue && blueBus.bus_id != "") primary = &blueBus;

  if (primary) {
    lcd.setCursor(0, 0);
    lcd.print(primary->line.substring(0,1)); lcd.print(": ");
    lcd.print(primary->bus_id);
    lcd.setCursor(0, 1);
    lcd.print(primary->relation);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Waiting for Bus");
    lcd.setCursor(0, 1);
    lcd.print(MY_HALTE_ID);
  }
}

// ==========================
// MQTT Callback
// ==========================
WiFiClient client;
PubSubClient mqtt(client);

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String data = "";
  for (int i = 0; i < length; i++) data += (char)payload[i];

  Serial.println("MQTT IN: " + data);

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, data)) return;

  String bus_id = doc["bus_id"];
  String line = doc["line_id"];
  const char* zone = doc["current_zone"];

  BusState* bus = nullptr;
  int halteIndex = -1;

  if (line == "RED" && (RELEVANT_LINE == "RED" || RELEVANT_LINE == "BOTH")) {
    bus = &redBus;
    halteIndex = findIndexInRoute(MY_HALTE_ID, redRoute, NUM_RED);
    bus->zoneIndex = findIndexInRoute(zone, redRoute, NUM_RED);
  }
  else if (line == "BLUE" && (RELEVANT_LINE == "BLUE" || RELEVANT_LINE == "BOTH")) {
    bus = &blueBus;
    halteIndex = findIndexInRoute(MY_HALTE_ID, blueRoute, NUM_BLUE);
    bus->zoneIndex = findIndexInRoute(zone, blueRoute, NUM_BLUE);
  }
  else {
    return;  // irrelevant message
  }

  bus->bus_id = bus_id;
  bus->line = line;
  bus->zone = zone;
  bus->relation = checkRelation(bus->zoneIndex, halteIndex);

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
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  while (!mqtt.connected()) {
    mqtt.connect("HalteClient");
    delay(400);
  }
  mqtt.subscribe(MQTT_TOPIC);
  Serial.println("MQTT Connected!");
}

// ==========================
// Main Loop
// ==========================
void setup() {
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.backlight();

  connectWiFi();
  connectMQTT();

  lcd.setCursor(0,0); lcd.print("Halte Active:");
  lcd.setCursor(0,1); lcd.print(MY_HALTE_ID);
  delay(1500);
  lcd.clear();
}

void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
}
