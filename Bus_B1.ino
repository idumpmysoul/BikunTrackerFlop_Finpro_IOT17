/*
 * Context-Aware Campus Bus Tracker
 * BUS UNIT (ESP32 + GPS + Button + LEDs + MQTT + FreeRTOS)
 *
 * Hardware (per Bus Unit):
 * - ESP32 Dev Kit
 * - GPS NEO-6M (UART2):  RX2=GPIO16, TX2=GPIO17
 * - Push Button on GPIO4 (3V3 + 10k pull-down to GND)
 * - Red LED on GPIO12  (RED line indicator)
 * - Blue LED on GPIO13 (BLUE line indicator)
 *
 * Wiring summary:
 *   GPS VCC  -> VIN
 *   GPS GND  -> GND
 *   GPS TX   -> GPIO16 (RX2)
 *   GPS RX   -> GPIO17 (TX2)
 *
 *   Button leg 1 -> 3V3
 *   Button leg 2 -> GPIO4
 *   10k resistor between GPIO4 and GND (pull-down)
 *
 *   RED LED anode  -> 220R -> GPIO12
 *   RED LED cathode-> GND
 *   BLUE LED anode -> 220R -> GPIO13
 *   BLUE LED cathode-> GND
 *
 * You can use this same code for both buses:
 *   - Change BUS_ID
 *   - Change DEFAULT_LINE if you want one to start as BLUE
 *
 * Required libraries:
 *   - TinyGPSPlus by Mikal Hart
 *   - PubSubClient by Nick O'Leary
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <TinyGPSPlus.h>

// =========================
// ==== CONFIGURATIONS ====
// =========================

// --- Identification (edit per bus) ---
#define BUS_ID       "BUS_B1"   // e.g. "BUS_R1", "BUS_B1"
#define DEFAULT_LINE "BLUE"      // "RED" or "BLUE" at startup

// --- WiFi Hotspot (PHONE 1) ---
const char* WIFI_SSID     = "YOUR_HOTSPOT_SSID";
const char* WIFI_PASSWORD = "YOUR_HOTSPOT_PASSWORD";

// --- MQTT Broker ---
const char* MQTT_BROKER   = "broker.hivemq.com"; // or "test.mosquitto.org"
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_TOPIC    = "campusbus/location"; // all buses publish here

// --- GPIO Pins (BUS UNIT) ---
const int PIN_GPS_RX   = 16;  // ESP32 RX2  <- GPS TX
const int PIN_GPS_TX   = 17;  // ESP32 TX2  -> GPS RX (optional)
const int PIN_BUTTON   = 4;   // Push button input
const int PIN_LED_RED  = 12;  // RED line LED
const int PIN_LED_BLUE = 13;  // BLUE line LED

// --- Button Debounce ---
const uint32_t BUTTON_DEBOUNCE_MS = 50;

// --- Publish Interval ---
const uint32_t PUBLISH_INTERVAL_MS = 1000; // send MQTT every 1s

// =========================
// ====== GLOBALS ==========
// =========================

TinyGPSPlus gps;
HardwareSerial GPSSerial(2); // UART2

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// FreeRTOS Queue for GPS data
QueueHandle_t gpsQueue;

// Current bus line (changes when button toggled)
String currentLine = DEFAULT_LINE; // "RED" or "BLUE"

// Button state for debouncing
int lastButtonReading = LOW;
int stableButtonState = LOW;
uint32_t lastDebounceTime = 0;

// Struct to pass via Queue
typedef struct {
  double latitude;
  double longitude;
  char zone[16];  // e.g. "FT", "ASRAMA", "STASIUN", or "UNKNOWN"
} GpsData_t;

// =========================
//   ZONE MAPPING CONFIG
// =========================

struct Zone {
  const char* name;
  double lat;
  double lon;
};

// RED Route stops
Zone redZones[] = {
  {"STASIUN", -6.361045, 106.831663},
  {"ASRAMA",  -6.348348, 106.829673},
  {"FT",      -6.361271, 106.823302},
  {"FE",      -6.359542, 106.825729},
  {"FIB",     -6.361129, 106.829465},
  {"STASIUN", -6.361045, 106.831663} // loop back
};
const int NUM_RED_ZONES = sizeof(redZones) / sizeof(redZones[0]);

// BLUE Route stops
Zone blueZones[] = {
  {"STASIUN", -6.361045, 106.831663},
  {"ASRAMA",  -6.348348, 106.829673},
  {"FISIP",   -6.361850, 106.830193},
  {"FH",      -6.364871, 106.832133},
  {"FPsi",    -6.362234, 106.830678},
  {"STASIUN", -6.361045, 106.831663} // loop back
};
const int NUM_BLUE_ZONES = sizeof(blueZones) / sizeof(blueZones[0]);

const double EARTH_RADIUS_M = 6371000.0;
const double ZONE_DETECT_THRESHOLD_M = 100.0; // 100m radius

// =========================
// ====== UTILITIES ========
// =========================

double distanceMeters(double lat1, double lon1, double lat2, double lon2) {

  double radLat1 = lat1 * DEG_TO_RAD;
  double radLon1 = lon1 * DEG_TO_RAD;
  double radLat2 = lat2 * DEG_TO_RAD;
  double radLon2 = lon2 * DEG_TO_RAD;

  double dLat = radLat2 - radLat1;
  double dLon = radLon2 - radLon1;

  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radLat1) * cos(radLat2) *
             sin(dLon / 2) * sin(dLon / 2);

  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return EARTH_RADIUS_M * c;
}

const char* determineCurrentZone(double lat, double lon) {
  double minDist = 999999.0;
  const char* nearest = "UNKNOWN";

  // Select array based on current line
  Zone* zones;
  int zoneCount;

  if (currentLine == "RED") {
    zones = redZones;
    zoneCount = NUM_RED_ZONES;
  } else {
    zones = blueZones;
    zoneCount = NUM_BLUE_ZONES;
  }

  for (int i = 0; i < zoneCount; i++) {
    double d = distanceMeters(lat, lon, zones[i].lat, zones[i].lon);

    // Debug distance output:
    Serial.print("[DEBUG] Dist to ");
    Serial.print(zones[i].name);
    Serial.print(" = ");
    Serial.print(d);
    Serial.println(" m");

    if (d < minDist) {
      minDist = d;
      nearest = zones[i].name;
    }
  }

  if (minDist <= ZONE_DETECT_THRESHOLD_M) {
    return nearest;
  } else {
    return "UNKNOWN";
  }
}

void updateLineLEDs() {
  if (currentLine == "RED") {
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_BLUE, LOW);
  } else { // "BLUE"
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_BLUE, HIGH);
  }
}

void toggleLine() {
  if (currentLine == "RED") {
    currentLine = "BLUE";
  } else {
    currentLine = "RED";
  }
  Serial.print("[LINE] Switched to ");
  Serial.println(currentLine);
  updateLineLEDs();
}

// =========================
// == BUTTON HANDLING ======
// =========================

void handleButton() {
  int reading = digitalRead(PIN_BUTTON);
  uint32_t now = millis();

  if (reading != lastButtonReading) {
    // input changed → reset debounce timer
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > BUTTON_DEBOUNCE_MS) {
    // if reading has been stable long enough, treat as actual state
    if (reading != stableButtonState) {
      stableButtonState = reading;

      // Rising edge: LOW -> HIGH (button pressed)
      if (stableButtonState == HIGH) {
        toggleLine();
      }
    }
  }

  lastButtonReading = reading;
}

// =========================
// ===== WIFI / MQTT =======
// =========================

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected.");
  Serial.print("[WiFi] IP address: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);

  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Connecting to broker...");
    String clientId = "BusUnit-" + String(BUS_ID) + "-" + String(random(0xFFFF), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 2 seconds...");
      delay(2000);
    }
  }
}

// =========================
// ====== GPS TASK =========
// =========================

void taskGPS(void* parameter) {
  (void)parameter;

  Serial.println("[GPS Task] Started.");

  for (;;) {
    // Read all available NMEA chars from GPS module
    while (GPSSerial.available() > 0) {
      char c = GPSSerial.read();
      gps.encode(c);
    }

    if (gps.location.isUpdated() && gps.location.isValid()) {
      GpsData_t data;
      data.latitude  = gps.location.lat();
      data.longitude = gps.location.lng();

      const char* zone = determineCurrentZone(data.latitude, data.longitude);
      strncpy(data.zone, zone, sizeof(data.zone));
      data.zone[sizeof(data.zone) - 1] = '\0';

      // Put latest GPS data to Queue (overwrite if full)
      if (gpsQueue != NULL) {
        xQueueOverwrite(gpsQueue, &data);
      }

      // Debug
      Serial.print("[GPS] Lat: ");
      Serial.print(data.latitude, 6);
      Serial.print(" Lon: ");
      Serial.print(data.longitude, 6);
      Serial.print(" Zone: ");
      Serial.println(data.zone);
    }

    vTaskDelay(pdMS_TO_TICKS(200)); // ~5Hz update
  }
}

// =========================
// ===== MQTT TASK =========
// =========================

void taskMQTT(void* parameter) {
  (void)parameter;

  Serial.println("[MQTT Task] Started.");

  GpsData_t latestGps {};
  uint32_t lastPublishTime = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }

    if (!mqttClient.connected()) {
      connectMQTT();
    }

    mqttClient.loop();

    uint32_t now = millis();
    if (now - lastPublishTime >= PUBLISH_INTERVAL_MS) {
      lastPublishTime = now;

      // Get latest GPS data from Queue (if any)
      if (gpsQueue != NULL && uxQueueMessagesWaiting(gpsQueue) > 0) {
        xQueueReceive(gpsQueue, &latestGps, 0);
      }

      // Build JSON payload
      String payload = "{";
      payload += "\"bus_id\":\"";       payload += BUS_ID;          payload += "\",";
      payload += "\"line_id\":\"";      payload += currentLine;     payload += "\",";
      payload += "\"current_zone\":\""; payload += latestGps.zone;  payload += "\"";
      payload += "}";

      Serial.print("[MQTT] Publishing: ");
      Serial.println(payload);

      bool ok = mqttClient.publish(MQTT_TOPIC, payload.c_str());
      if (!ok) {
        Serial.println("[MQTT] Publish failed.");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =========================
/* ========= SETUP ========= */
// =========================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("=== BUS UNIT START ===");

  // Pins
  pinMode(PIN_BUTTON,   INPUT);       // external pulldown
  pinMode(PIN_LED_RED,  OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);

  updateLineLEDs();

  // GPS Serial (UART2)
  GPSSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  // WiFi & MQTT initial connect
  connectWiFi();
  connectMQTT();

  // Create Queue (length=1, holds latest GPS)
  gpsQueue = xQueueCreate(1, sizeof(GpsData_t));
  if (gpsQueue == NULL) {
    Serial.println("Error creating GPS queue!");
  }

  // Create tasks
  xTaskCreatePinnedToCore(
    taskGPS,
    "GPS Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );

  xTaskCreatePinnedToCore(
    taskMQTT,
    "MQTT Task",
    4096,
    NULL,
    1,
    NULL,
    1
  );
}

// =========================
/* ========= LOOP ========== */
// =========================

void loop() {
  // Handle button & line switching in main loop
  handleButton();
  delay(10);
}
