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
#include <Preferences.h>

// =========================
// ==== CONFIGURATIONS ====
// =========================

// --- Mode Operasi ---
#define SIMULATION_MODE true // UBAH MENJADI 'false' SAAT GPS BARU TIBA

// --- Identification (edit per bus) ---
#define BUS_ID       "BUS_1"   // e.g. "BUS_1", "BUS_2"
#define DEFAULT_LINE "RED"      // "RED" or "BLUE" at startup

// --- WiFi Hotspot ---
const char* WIFI_SSID     = "OrganicTrash";
const char* WIFI_PASSWORD = "oops1112";

// --- MQTT Broker ---
const char* MQTT_BROKER   = "broker.hivemq.com"; // or "test.mosquitto.org"
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_TOPIC    = "bikun/location"; // all buses publish here

// --- GPIO Pins (BUS UNIT) ---
const int PIN_GPS_RX   = 16;  // ESP32 RX2  <- GPS TX
const int PIN_GPS_TX   = 17;  // ESP32 TX2  -> GPS RX (optional)
const int PIN_BUTTON   = 4;   // Push button input
const int PIN_LED_RED  = 25;  // RED line LED
const int PIN_LED_BLUE = 26;  // BLUE line LED

// --- Timings ---
const uint32_t BUTTON_DEBOUNCE_MS  = 50;
const uint32_t PUBLISH_INTERVAL_MS = 2000; // send MQTT every 1s

const uint32_t SIMULATION_MOVE_INTERVAL_MS = 15000; // Bus "pindah" setiap 15 detik di mode simulasi

Preferences preferences;

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
  char zone[20];
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
const Zone redRoute[] = {
    {"ASRAMA", -6.348191, 106.829705},
    {"MENWA", -6.353438, 106.831816},
    {"STASIUN", -6.360717, 106.831752},
    {"FH", -6.364840, 106.832226},
    {"BALAIRUNG", -6.368286, 106.831712},
    {"RIK", -6.370153, 106.831040},
    {"FKM", -6.371713, 106.829113},
    {"RSUI", -6.372728, 106.828575},
    {"FIK", -6.371179, 106.826968},
    {"FMIPA", -6.369845, 106.825761},
    {"SOR", -6.367006, 106.824346},
    {"VOKASI", -6.366118, 106.821655},
    {"FT", -6.361060, 106.823179},
    {"FEB", -6.359419, 106.825684},
    {"FIB", -6.360619, 106.829290},
    {"FISIP", -6.361711, 106.830344},
    {"FPsi", -6.362391, 106.831005},
    {"STASIUN", -6.360717, 106.831752},
    {"MENWA", -6.353438, 106.831816},
    {"ASRAMA", -6.348191, 106.829705}
};
const int NUM_RED_ZONES = sizeof(redRoute) / sizeof(redRoute[0]);

const Zone blueRoute[] = {
    {"ASRAMA", -6.348191, 106.829705},
    {"MENWA", -6.353438, 106.831816},
    {"STASIUN", -6.360778, 106.831417},
    {"FPsi", -6.362880, 106.831129},
    {"FISIP", -6.361868, 106.830092},
    {"FIB", -6.360887, 106.829157},
    {"FEB", -6.359658, 106.825722},
    {"FT", -6.361258, 106.823307},
    {"VOKASI", -6.366007, 106.821855},
    {"PUSGIWA", -6.366744, 106.823464},
    {"FARMASI", -6.368512, 106.827630},
    {"BALAI SIDANG", -6.369061, 106.829390},
    {"BALAIRUNG", -6.368136, 106.831576},
    {"MASJID UI", -6.365565, 106.832023},
    {"FH", -6.364443, 106.832040},
    {"STASIUN", -6.360778, 106.831417},
    {"MENWA", -6.353438, 106.831816},
    {"ASRAMA", -6.348191, 106.829705}
};
const int NUM_BLUE_ZONES = sizeof(blueRoute) / sizeof(blueRoute[0]);


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

  if (currentLine.equals("RED")) {
    zones = (Zone*)redRoute;
    zoneCount = NUM_RED_ZONES;
  } else {
    zones = (Zone*)blueRoute;
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
  if (currentLine.equals("RED")) {
    digitalWrite(PIN_LED_RED, HIGH);
    digitalWrite(PIN_LED_BLUE, LOW);
  } else { // "BLUE"
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_BLUE, HIGH);
  }
}

void toggleLine() {
  if (currentLine.equals("RED")) {
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

void taskInput(void* parameter) {
  pinMode(PIN_BUTTON, INPUT);
  uint32_t lastPressTime = 0;
  for (;;) {
    if (digitalRead(PIN_BUTTON) == HIGH) {
      if (millis() - lastPressTime > BUTTON_DEBOUNCE_MS) {
        lastPressTime = millis();
        currentLine = (currentLine.equals("RED")) ? "BLUE" : "RED";
        updateLineLEDs();
        preferences.begin("bus-state", false);
        preferences.putString("currentLine", currentLine);
        preferences.end();
        Serial.printf("[INPUT] Jalur diubah menjadi: %s\n", currentLine.c_str());
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}


// =========================
// ====== GPS TASK =========
// =========================

void taskGPS(void* parameter) {
  #if SIMULATION_MODE
    // --- MODE SIMULASI ---
    Serial.println("[GPS Task] Dimulai dalam MODE SIMULASI.");
    int currentIndex = 0;
    for (;;) {
      const Zone* zones;
      int zoneCount;
      if (currentLine == "RED") {
        zones = redRoute;
        zoneCount = NUM_RED_ZONES;
      } else {
        zones = blueRoute;
        zoneCount = NUM_BLUE_ZONES;
      }
      
      GpsData_t data;
      strncpy(data.zone, zones[currentIndex].name, sizeof(data.zone) - 1);
      data.zone[sizeof(data.zone) - 1] = '\0';
      
      xQueueOverwrite(gpsQueue, &data);
      
      Serial.printf("[SIMULATOR] Bus %s sekarang di -> %s\n", currentLine.c_str(), data.zone);
      
      currentIndex++;
      if (currentIndex >= zoneCount) {
        currentIndex = 0; // Kembali ke awal rute
      }
      
      vTaskDelay(pdMS_TO_TICKS(SIMULATION_MOVE_INTERVAL_MS));
    }
  #else
    // --- MODE GPS NYATA ---
    GPSSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
    Serial.println("[GPS Task] Dimulai dalam MODE GPS NYATA.");
    for (;;) {
      while (GPSSerial.available() > 0) {
        gps.encode(GPSSerial.read());
      }
      if (gps.location.isUpdated() && gps.location.isValid()) {
        GpsData_t data;
        const char* zone = determineCurrentZone(gps.location.lat(), gps.location.lng());
        strncpy(data.zone, zone, sizeof(data.zone) - 1);
        data.zone[sizeof(data.zone) - 1] = '\0';
        xQueueOverwrite(gpsQueue, &data);
      }
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  #endif
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
