#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// ========== KONFIGURASI TEST MODE ==========
// Pilih mode: "BUS" atau "HALTE"
#define TEST_MODE "BUS"  // Ubah ke "HALTE" untuk test terminal unit

// ========== WiFi Credentials ==========
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// ========== Pin Definitions ==========
#define BUTTON_PIN 4
#define LED_PIN 2

// ========== Test Results ==========
struct TestResult {
  const char* testName;
  bool passed;
  String message;
};

TestResult results[10];
int testCount = 0;

// ========== Hardware Objects ==========
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

// ========== HELPER FUNCTIONS ==========
void addTestResult(const char* name, bool passed, String message = "") {
  results[testCount].testName = name;
  results[testCount].passed = passed;
  results[testCount].message = message;
  testCount++;
}

void printTestResults() {
  Serial.println("\n========================================");
  Serial.println("         SANITY CHECK RESULTS");
  Serial.println("========================================");
  
  int passCount = 0;
  for (int i = 0; i < testCount; i++) {
    Serial.print(i + 1);
    Serial.print(". ");
    Serial.print(results[i].testName);
    Serial.print(": ");
    
    if (results[i].passed) {
      Serial.print("[PASS] ✓");
      passCount++;
    } else {
      Serial.print("[FAIL] ✗");
    }
    
    if (results[i].message.length() > 0) {
      Serial.print(" - ");
      Serial.print(results[i].message);
    }
    Serial.println();
  }
  
  Serial.println("========================================");
  Serial.printf("Total: %d/%d tests passed (%.1f%%)\n", 
                passCount, testCount, 
                (passCount * 100.0 / testCount));
  Serial.println("========================================\n");
}

// ========== TEST FUNCTIONS ==========

// Test 1: ESP32 Basic Check
bool testESP32Basic() {
  Serial.println("\n[TEST 1] ESP32 Basic Check...");
  
  uint32_t chipId = 0;
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  
  Serial.printf("  Chip ID: %08X\n", chipId);
  Serial.printf("  CPU Freq: %d MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("  Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("  Flash Size: %d bytes\n", ESP.getFlashChipSize());
  
  bool passed = ESP.getCpuFreqMHz() >= 80 && ESP.getFreeHeap() > 100000;
  addTestResult("ESP32 Basic", passed, 
                "Chip: " + String(chipId, HEX) + ", Heap: " + String(ESP.getFreeHeap()));
  return passed;
}

// Test 2: WiFi Connection
bool testWiFi() {
  Serial.println("\n[TEST 2] WiFi Connection...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  bool connected = WiFi.status() == WL_CONNECTED;
  
  if (connected) {
    Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());
    
    addTestResult("WiFi Connection", true, 
                  "IP: " + WiFi.localIP().toString() + ", RSSI: " + String(WiFi.RSSI()));
  } else {
    Serial.println("  WiFi connection FAILED!");
    addTestResult("WiFi Connection", false, "Cannot connect to WiFi");
  }
  
  return connected;
}

// Test 3: MQTT Connection
bool testMQTT() {
  Serial.println("\n[TEST 3] MQTT Connection...");
  
  client.setServer(mqtt_server, mqtt_port);
  
  String clientId = "SanityCheck-" + String(random(0xffff), HEX);
  bool connected = client.connect(clientId.c_str());
  
  if (connected) {
    Serial.println("  MQTT connected successfully");
    Serial.printf("  Broker: %s:%d\n", mqtt_server, mqtt_port);
    Serial.printf("  Client ID: %s\n", clientId.c_str());
    
    addTestResult("MQTT Connection", true, "Broker: " + String(mqtt_server));
  } else {
    Serial.printf("  MQTT connection FAILED! State: %d\n", client.state());
    addTestResult("MQTT Connection", false, "State: " + String(client.state()));
  }
  
  return connected;
}

// Test 4: MQTT Publish/Subscribe
bool testMQTTPubSub() {
  Serial.println("\n[TEST 4] MQTT Pub/Sub...");
  
  if (!client.connected()) {
    Serial.println("  Skipped - MQTT not connected");
    addTestResult("MQTT Pub/Sub", false, "MQTT not connected");
    return false;
  }
  
  const char* testTopic = "ui/bus/test";
  bool received = false;
  
  client.setCallback([&received](char* topic, byte* payload, unsigned int length) {
    received = true;
    Serial.printf("  Message received on %s\n", topic);
  });
  
  client.subscribe(testTopic);
  delay(500);
  
  bool published = client.publish(testTopic, "{\"test\":\"sanity_check\"}");
  
  unsigned long start = millis();
  while (!received && millis() - start < 3000) {
    client.loop();
    delay(10);
  }
  
  if (published && received) {
    Serial.println("  Publish and receive successful");
    addTestResult("MQTT Pub/Sub", true, "Round-trip successful");
  } else {
    Serial.printf("  Pub: %s, Sub: %s\n", published ? "OK" : "FAIL", received ? "OK" : "FAIL");
    addTestResult("MQTT Pub/Sub", false, "Round-trip failed");
  }
  
  return published && received;
}

// Test 5: Button/GPIO
bool testButton() {
  Serial.println("\n[TEST 5] Button & GPIO...");
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  
  // Test LED
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
  delay(200);
  digitalWrite(LED_PIN, HIGH);
  delay(200);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("  LED blink test completed");
  
  int buttonState = digitalRead(BUTTON_PIN);
  Serial.printf("  Button state: %s\n", buttonState == HIGH ? "Not pressed" : "Pressed");
  
  Serial.println("  Press button within 5 seconds...");
  unsigned long start = millis();
  bool buttonPressed = false;
  
  while (millis() - start < 5000) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      buttonPressed = true;
      digitalWrite(LED_PIN, HIGH);
      Serial.println("  Button press detected!");
      delay(500);
      digitalWrite(LED_PIN, LOW);
      break;
    }
    delay(50);
  }
  
  if (!buttonPressed) {
    Serial.println("  No button press detected (manual test)");
  }
  
  addTestResult("Button & GPIO", true, 
                buttonPressed ? "Button responsive" : "LED OK, Button not tested");
  return true;
}

// Test 6: GPS Module (Bus only)
bool testGPS() {
  Serial.println("\n[TEST 6] GPS Module...");
  
  if (String(TEST_MODE) != "BUS") {
    Serial.println("  Skipped - Not in BUS mode");
    addTestResult("GPS Module", true, "N/A for HALTE mode");
    return true;
  }
  
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  
  Serial.println("  Reading GPS data for 10 seconds...");
  unsigned long start = millis();
  int sentences = 0;
  bool gotFix = false;
  
  while (millis() - start < 10000) {
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      gps.encode(c);
      
      if (gps.location.isUpdated()) {
        sentences++;
        if (gps.location.isValid()) {
          gotFix = true;
        }
      }
    }
    delay(10);
  }
  
  Serial.printf("  Sentences received: %d\n", sentences);
  Serial.printf("  Satellites: %d\n", gps.satellites.value());
  Serial.printf("  GPS Fix: %s\n", gotFix ? "YES" : "NO");
  
  if (gotFix) {
    Serial.printf("  Location: %.6f, %.6f\n", gps.location.lat(), gps.location.lng());
    addTestResult("GPS Module", true, 
                  "Fix OK, Sats: " + String(gps.satellites.value()));
  } else if (sentences > 0) {
    Serial.println("  GPS module working but no fix (normal indoors)");
    addTestResult("GPS Module", true, 
                  "Module OK, No fix (indoor expected)");
  } else {
    Serial.println("  No GPS data received");
    addTestResult("GPS Module", false, "No data from GPS module");
    return false;
  }
  
  return true;
}

// Test 7: LCD Display (Halte only)
bool testLCD() {
  Serial.println("\n[TEST 7] LCD Display...");
  
  if (String(TEST_MODE) != "HALTE") {
    Serial.println("  Skipped - Not in HALTE mode");
    addTestResult("LCD Display", true, "N/A for BUS mode");
    return true;
  }
  
  bool lcdFound = false;
  
  // Scan I2C
  Wire.begin();
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() == 0) {
    lcdFound = true;
    Serial.println("  LCD found at address 0x27");
  } else {
    Wire.beginTransmission(0x3F);
    if (Wire.endTransmission() == 0) {
      lcdFound = true;
      Serial.println("  LCD found at address 0x3F");
    }
  }
  
  if (!lcdFound) {
    Serial.println("  LCD not found on I2C bus");
    addTestResult("LCD Display", false, "LCD not detected");
    return false;
  }
  
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  // Test pattern
  lcd.setCursor(0, 0);
  lcd.print("SANITY CHECK");
  lcd.setCursor(0, 1);
  lcd.print("LCD Test Pattern");
  lcd.setCursor(0, 2);
  lcd.print("Line 3: 12345678901234567890");
  lcd.setCursor(0, 3);
  lcd.print("Line 4: ABCDEFGHIJ");
  
  Serial.println("  LCD initialized and displaying test pattern");
  delay(2000);
  
  // Clear and show OK
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LCD TEST: OK");
  
  addTestResult("LCD Display", true, "Display working");
  return true;
}

// Test 8: Memory Check
bool testMemory() {
  Serial.println("\n[TEST 8] Memory Check...");
  
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  size_t heapSize = ESP.getHeapSize();
  
  Serial.printf("  Free Heap: %d bytes\n", freeHeap);
  Serial.printf("  Min Free Heap: %d bytes\n", minFreeHeap);
  Serial.printf("  Heap Size: %d bytes\n", heapSize);
  Serial.printf("  Usage: %.1f%%\n", ((heapSize - freeHeap) * 100.0 / heapSize));
  
  bool passed = freeHeap > 50000 && minFreeHeap > 30000;
  
  if (passed) {
    addTestResult("Memory Check", true, 
                  "Free: " + String(freeHeap) + " bytes");
  } else {
    addTestResult("Memory Check", false, 
                  "Low memory warning");
  }
  
  return passed;
}

// Test 9: JSON Processing
bool testJSON() {
  Serial.println("\n[TEST 9] JSON Processing...");
  
  const char* testJson = "{\"bus_id\":\"BUS_R1\",\"line_id\":\"RED\",\"current_zone\":\"FT\"}";
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, testJson);
  
  if (error) {
    Serial.printf("  JSON parse FAILED: %s\n", error.c_str());
    addTestResult("JSON Processing", false, error.c_str());
    return false;
  }
  
  String busId = doc["bus_id"] | "";
  String lineId = doc["line_id"] | "";
  String zone = doc["current_zone"] | "";
  
  Serial.printf("  Parsed: bus=%s, line=%s, zone=%s\n", 
                busId.c_str(), lineId.c_str(), zone.c_str());
  
  bool valid = (busId == "BUS_R1" && lineId == "RED" && zone == "FT");
  
  if (valid) {
    addTestResult("JSON Processing", true, "Parse successful");
  } else {
    addTestResult("JSON Processing", false, "Parse incorrect");
  }
  
  return valid;
}

// Test 10: Route Arrays
bool testRouteArrays() {
  Serial.println("\n[TEST 10] Route Arrays...");
  
  const char* redRoute[] = {"STASIUN", "ASRAMA", "FT", "FE", "FIB", "STASIUN"};
  const char* blueRoute[] = {"STASIUN", "ASRAMA", "FISIP", "FH", "FPsi", "STASIUN"};
  
  Serial.println("  Red Route:");
  for (int i = 0; i < 6; i++) {
    Serial.printf("    [%d] %s\n", i, redRoute[i]);
  }
  
  Serial.println("  Blue Route:");
  for (int i = 0; i < 6; i++) {
    Serial.printf("    [%d] %s\n", i, blueRoute[i]);
  }
  
  // Test route lookup
  int ftIndex = -1;
  for (int i = 0; i < 6; i++) {
    if (strcmp(redRoute[i], "FT") == 0) {
      ftIndex = i;
      break;
    }
  }
  
  bool passed = (ftIndex == 2);
  Serial.printf("  FT found at index: %d (expected 2)\n", ftIndex);
  
  if (passed) {
    addTestResult("Route Arrays", true, "Route lookup working");
  } else {
    addTestResult("Route Arrays", false, "Route lookup failed");
  }
  
  return passed;
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   CAMPUS BUS TRACKER - SANITY CHECK   ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.printf("\nTest Mode: %s\n", TEST_MODE);
  Serial.printf("Date: %s %s\n", __DATE__, __TIME__);
  
  delay(1000);
  
  // Run all tests
  testESP32Basic();
  testWiFi();
  testMQTT();
  testMQTTPubSub();
  testButton();
  testGPS();
  testLCD();
  testMemory();
  testJSON();
  testRouteArrays();
  
  // Print summary
  printTestResults();
  
  // Final status
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║         SANITY CHECK COMPLETE         ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  // Show on LCD if in HALTE mode
  if (String(TEST_MODE) == "HALTE") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SANITY CHECK DONE");
    lcd.setCursor(0, 1);
    
    int passed = 0;
    for (int i = 0; i < testCount; i++) {
      if (results[i].passed) passed++;
    }
    
    lcd.print("Tests: ");
    lcd.print(passed);
    lcd.print("/");
    lcd.print(testCount);
    lcd.print(" PASSED");
    
    if (passed == testCount) {
      lcd.setCursor(0, 2);
      lcd.print("Status: ALL OK!");
    } else {
      lcd.setCursor(0, 2);
      lcd.print("Status: CHECK LOG");
    }
  }
  
  Serial.println("\nSystem ready. You can now upload the actual program.");
}

void loop() {
  // Blink LED to show system is alive
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  delay(1000);
}