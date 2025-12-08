/*
 * SANITY CHECK - Context-Aware Campus Bus Tracker
 * Support BOTH architectures: FreeRTOS & Simple Loop
 * 
 * How to use:
 * 1. Set TEST_MODE: "BUS" or "HALTE"
 * 2. Set CODE_ARCH: "FREERTOS" or "SIMPLE"
 * 3. Upload and check Serial Monitor (115200 baud)
 * 4. For HALTE mode, also check LCD display
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

// ========== TEST CONFIGURATION ==========
#define TEST_MODE "BUS"        // "BUS" or "HALTE"
#define CODE_ARCH "FREERTOS"   // "FREERTOS" or "SIMPLE"

// ========== WiFi & MQTT ==========
const char* ssid = "OrganicTrash";
const char* password = "oops1112";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// ========== Pin Definitions ==========
// BUS UNIT Pins
const int PIN_GPS_RX   = 16;
const int PIN_GPS_TX   = 17;
const int PIN_BUTTON   = 4;
const int PIN_LED_RED  = 12;
const int PIN_LED_BLUE = 13;
const int PIN_LED      = 2;   // Built-in LED

// ========== Test Results Storage ==========
struct TestResult {
  const char* testName;
  bool passed;
  String message;
};

TestResult results[15];
int testCount = 0;

// ========== Hardware Objects ==========
WiFiClient espClient;
PubSubClient client(espClient);
LiquidCrystal_I2C lcd(0x27, 16, 2);
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

// ========== FreeRTOS Test Variables ==========
QueueHandle_t sanityTestQueue = NULL;
TaskHandle_t testTaskHandle = NULL;
volatile bool taskTestComplete = false;

// ========== HELPER FUNCTIONS ==========
void addTestResult(const char* name, bool passed, String message = "") {
  results[testCount].testName = name;
  results[testCount].passed = passed;
  results[testCount].message = message;
  testCount++;
}

void printSeparator() {
  Serial.println("========================================");
}

void printTestResults() {
  Serial.println("\n");
  printSeparator();
  Serial.println("       SANITY CHECK RESULTS");
  printSeparator();
  
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
  
  printSeparator();
  Serial.printf("Total: %d/%d tests passed (%.1f%%)\n", 
                passCount, testCount, 
                (passCount * 100.0 / testCount));
  printSeparator();
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
  Serial.printf("  Chip Model: %s\n", ESP.getChipModel());
  Serial.printf("  Chip Revision: %d\n", ESP.getChipRevision());
  Serial.printf("  CPU Cores: %d\n", ESP.getChipCores());
  
  bool passed = ESP.getCpuFreqMHz() >= 80 && ESP.getFreeHeap() > 100000;
  addTestResult("ESP32 Basic", passed, 
                "Heap: " + String(ESP.getFreeHeap()/1024) + "KB");
  return passed;
}

// Test 2: WiFi Connection
bool testWiFi() {
  Serial.println("\n[TEST 2] WiFi Connection...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  Serial.print("  Connecting");
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  bool connected = WiFi.status() == WL_CONNECTED;
  
  if (connected) {
    Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());
    Serial.printf("  Channel: %d\n", WiFi.channel());
    
    addTestResult("WiFi Connection", true, 
                  "RSSI: " + String(WiFi.RSSI()) + " dBm");
  } else {
    Serial.println("  WiFi connection FAILED!");
    addTestResult("WiFi Connection", false, "Timeout");
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
    Serial.printf("  State: %d\n", client.state());
    
    addTestResult("MQTT Connection", true, "Broker OK");
  } else {
    Serial.printf("  MQTT connection FAILED! State: %d\n", client.state());
    addTestResult("MQTT Connection", false, "State: " + String(client.state()));
  }
  
  return connected;
}

// Test 4: MQTT Pub/Sub Round-trip
bool testMQTTPubSub() {
  Serial.println("\n[TEST 4] MQTT Pub/Sub Test...");
  
  if (!client.connected()) {
    Serial.println("  Skipped - MQTT not connected");
    addTestResult("MQTT Pub/Sub", false, "MQTT not connected");
    return false;
  }
  
  const char* testTopic = "sanitycheck/test";
  volatile bool received = false;
  
  client.setCallback([&received](char* topic, byte* payload, unsigned int length) {
    Serial.printf("  ✓ Message received on topic: %s\n", topic);
    received = true;
  });
  
  client.subscribe(testTopic);
  delay(500);
  
  Serial.println("  Publishing test message...");
  bool published = client.publish(testTopic, "{\"test\":\"sanity_check\"}");
  
  unsigned long start = millis();
  while (!received && millis() - start < 3000) {
    client.loop();
    delay(10);
  }
  
  if (published && received) {
    Serial.println("  ✓ Round-trip successful");
    addTestResult("MQTT Pub/Sub", true, "Round-trip OK");
  } else {
    Serial.printf("  Publish: %s, Receive: %s\n", 
                  published ? "OK" : "FAIL", 
                  received ? "OK" : "FAIL");
    addTestResult("MQTT Pub/Sub", false, "Round-trip failed");
  }
  
  return published && received;
}

// Test 5: Button & LEDs (Bus only)
bool testButtonAndLEDs() {
  Serial.println("\n[TEST 5] Button & LEDs Test...");
  
  if (String(TEST_MODE) != "BUS") {
    Serial.println("  Skipped - Not in BUS mode");
    addTestResult("Button & LEDs", true, "N/A for HALTE");
    return true;
  }
  
  pinMode(PIN_BUTTON, INPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  
  Serial.println("  Testing LEDs...");
  
  // Test RED LED
  digitalWrite(PIN_LED_RED, HIGH);
  delay(300);
  digitalWrite(PIN_LED_RED, LOW);
  Serial.println("  ✓ RED LED blinked");
  
  // Test BLUE LED
  digitalWrite(PIN_LED_BLUE, HIGH);
  delay(300);
  digitalWrite(PIN_LED_BLUE, LOW);
  Serial.println("  ✓ BLUE LED blinked");
  
  // Test built-in LED
  digitalWrite(PIN_LED, HIGH);
  delay(300);
  digitalWrite(PIN_LED, LOW);
  Serial.println("  ✓ Built-in LED blinked");
  
  // Test Button
  Serial.println("  Press button within 5 seconds to test...");
  unsigned long start = millis();
  bool buttonPressed = false;
  int buttonState = digitalRead(PIN_BUTTON);
  
  while (millis() - start < 5000) {
    int newState = digitalRead(PIN_BUTTON);
    if (newState != buttonState && newState == HIGH) {
      buttonPressed = true;
      Serial.println("  ✓ Button press detected!");
      digitalWrite(PIN_LED_RED, HIGH);
      digitalWrite(PIN_LED_BLUE, HIGH);
      delay(500);
      digitalWrite(PIN_LED_RED, LOW);
      digitalWrite(PIN_LED_BLUE, LOW);
      break;
    }
    buttonState = newState;
    delay(50);
  }
  
  if (!buttonPressed) {
    Serial.println("  ⚠ Button not tested (no press detected)");
  }
  
  addTestResult("Button & LEDs", true, 
                "LEDs: OK, Button: " + String(buttonPressed ? "OK" : "Not tested"));
  return true;
}

// Test 6: GPS Module (Bus only)
bool testGPS() {
  Serial.println("\n[TEST 6] GPS Module Test...");
  
  if (String(TEST_MODE) != "BUS") {
    Serial.println("  Skipped - Not in BUS mode");
    addTestResult("GPS Module", true, "N/A for HALTE");
    return true;
  }
  
  gpsSerial.begin(9600, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  
  Serial.println("  Reading GPS for 10 seconds...");
  Serial.println("  (No fix is normal indoors)");
  
  unsigned long start = millis();
  int sentences = 0;
  int validSentences = 0;
  bool gotFix = false;
  
  while (millis() - start < 10000) {
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      if (gps.encode(c)) {
        sentences++;
        if (gps.location.isUpdated() && gps.location.isValid()) {
          validSentences++;
          gotFix = true;
        }
      }
    }
    delay(10);
  }
  
  Serial.printf("  Sentences: %d\n", sentences);
  Serial.printf("  Valid updates: %d\n", validSentences);
  Serial.printf("  Satellites: %d\n", gps.satellites.value());
  Serial.printf("  HDOP: %.2f\n", gps.hdop.hdop());
  
  if (gotFix) {
    Serial.printf("  ✓ GPS FIX acquired!\n");
    Serial.printf("  Location: %.6f, %.6f\n", 
                  gps.location.lat(), gps.location.lng());
    addTestResult("GPS Module", true, 
                  "Fix OK, Sats: " + String(gps.satellites.value()));
  } else if (sentences > 0) {
    Serial.println("  ✓ GPS module responding (no fix - normal indoors)");
    addTestResult("GPS Module", true, 
                  "Module OK, " + String(sentences) + " sentences");
  } else {
    Serial.println("  ✗ No GPS data received");
    addTestResult("GPS Module", false, "No data from GPS");
    return false;
  }
  
  return true;
}

// Test 7: LCD Display (Halte only)
bool testLCD() {
  Serial.println("\n[TEST 7] LCD Display Test...");
  
  if (String(TEST_MODE) != "HALTE") {
    Serial.println("  Skipped - Not in HALTE mode");
    addTestResult("LCD Display", true, "N/A for BUS");
    return true;
  }
  
  Serial.println("  Scanning I2C bus...");
  Wire.begin();
  
  byte error, address;
  int nDevices = 0;
  bool lcdFound = false;
  
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("  ✓ I2C device found at address 0x%02X\n", address);
      nDevices++;
      // Asumsikan device pertama yang ditemukan adalah LCD
      if (!lcdFound) {
        lcd = LiquidCrystal_I2C(address, 16, 2);
        lcdFound = true;
      }
    }
  }

  if (!lcdFound) {
    Serial.println("  ✗ No I2C device found. Periksa kabel SDA/SCL dan VCC/GND.");
    addTestResult("LCD Display", false, "Not detected on I2C");
    return false;
  }
  
  Serial.println("  Initializing LCD...");
  lcd.begin(16,2); // [FIX 4] Gunakan lcd.begin() yang lebih modern
  lcd.backlight();
  lcd.clear();
  
  // Display test pattern
  lcd.setCursor(0, 0);
  lcd.print("LCD Test: [PASS]");
  lcd.setCursor(0, 1);
  lcd.print("16x2 Display OK!");
  
  Serial.println("  ✓ LCD test pattern displayed");
  delay(3000);
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sanity Check");
  lcd.setCursor(0, 1);
  lcd.print("Complete!");

  addTestResult("LCD Display", true, "Device Found & OK");
  return true;
}

// Test 8: FreeRTOS (if using FreeRTOS architecture)
bool testFreeRTOS() {
  Serial.println("\n[TEST 8] FreeRTOS Test...");
  
  if (String(CODE_ARCH) != "FREERTOS") {
    Serial.println("  Skipped - Not using FreeRTOS architecture");
    addTestResult("FreeRTOS", true, "N/A for Simple Loop");
    return true;
  }
  
  // Check scheduler state
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
    Serial.println("  ✓ FreeRTOS Scheduler: RUNNING");
  } else {
    Serial.println("  ✗ FreeRTOS Scheduler: NOT RUNNING");
    addTestResult("FreeRTOS", false, "Scheduler not running");
    return false;
  }
  
  // Check task count
  UBaseType_t taskCount = uxTaskGetNumberOfTasks();
  Serial.printf("  Active Tasks: %d\n", taskCount);
  
  // Check heap
  size_t freeHeap = xPortGetFreeHeapSize();
  size_t minHeap = xPortGetMinimumEverFreeHeapSize();
  Serial.printf("  FreeRTOS Heap: %d bytes\n", freeHeap);
  Serial.printf("  Min Free Heap: %d bytes\n", minHeap);
  
  bool passed = taskCount >= 1 && freeHeap > 10000;
  
  if (passed) {
    Serial.println("  ✓ FreeRTOS system healthy");
    addTestResult("FreeRTOS", true, 
                  String(taskCount) + " tasks, " + String(freeHeap/1024) + "KB heap");
  } else {
    addTestResult("FreeRTOS", false, "System issues detected");
  }
  
  return passed;
}

// Test 9: FreeRTOS Queue (if using FreeRTOS)
bool testFreeRTOSQueue() {
  Serial.println("\n[TEST 9] FreeRTOS Queue Test...");
  
  if (String(CODE_ARCH) != "FREERTOS") {
    Serial.println("  Skipped - Not using FreeRTOS");
    addTestResult("FreeRTOS Queue", true, "N/A");
    return true;
  }
  
  Serial.println("  Creating test queue...");
  sanityTestQueue = xQueueCreate(5, sizeof(int));
  
  if (sanityTestQueue == NULL) {
    Serial.println("  ✗ Queue creation FAILED");
    addTestResult("FreeRTOS Queue", false, "Cannot create");
    return false;
  }
  
  Serial.println("  ✓ Queue created");
  
  // Test send/receive
  int testData[] = {10, 20, 30, 40, 50};
  int received = 0;
  bool allOK = true;
  
  Serial.println("  Testing send/receive...");
  for (int i = 0; i < 5; i++) {
    if (xQueueSend(sanityTestQueue, &testData[i], 0) != pdTRUE) {
      Serial.printf("  ✗ Send failed at index %d\n", i);
      allOK = false;
    }
  }
  
  for (int i = 0; i < 5; i++) {
    if (xQueueReceive(sanityTestQueue, &received, 0) == pdTRUE) {
      if (received != testData[i]) {
        Serial.printf("  ✗ Data mismatch: expected %d, got %d\n", 
                      testData[i], received);
        allOK = false;
      }
    } else {
      Serial.printf("  ✗ Receive failed at index %d\n", i);
      allOK = false;
    }
  }
  
  if (allOK) {
    Serial.println("  ✓ All queue operations successful");
  }
  
  vQueueDelete(sanityTestQueue);
  
  addTestResult("FreeRTOS Queue", allOK, "Send/Receive OK");
  return allOK;
}

// Test 10: FreeRTOS Task Creation
void testTaskFunction(void* parameter) {
  taskTestComplete = true;
  vTaskDelete(NULL);
}

bool testTaskCreation() {
  Serial.println("\n[TEST 10] FreeRTOS Task Creation...");
  
  if (String(CODE_ARCH) != "FREERTOS") {
    Serial.println("  Skipped - Not using FreeRTOS");
    addTestResult("Task Creation", true, "N/A");
    return true;
  }
  
  taskTestComplete = false;
  
  BaseType_t result = xTaskCreate(
    testTaskFunction,
    "TestTask",
    2048,
    NULL,
    1,
    &testTaskHandle
  );
  
  if (result != pdPASS) {
    Serial.println("  ✗ Task creation FAILED");
    addTestResult("Task Creation", false, "Cannot create task");
    return false;
  }
  
  Serial.println("  ✓ Task created successfully");
  
  // Wait for task to complete
  unsigned long start = millis();
  while (!taskTestComplete && millis() - start < 1000) {
    delay(10);
  }
  
  if (taskTestComplete) {
    Serial.println("  ✓ Task executed and cleaned up");
    addTestResult("Task Creation", true, "Task lifecycle OK");
  } else {
    Serial.println("  ✗ Task did not complete");
    addTestResult("Task Creation", false, "Task timeout");
    return false;
  }
  
  return true;
}

// Test 11: Memory Stress Test
bool testMemory() {
  Serial.println("\n[TEST 11] Memory Stress Test...");
  
  size_t initialHeap = ESP.getFreeHeap();
  Serial.printf("  Initial Heap: %d bytes\n", initialHeap);
  
  // Allocate and free memory
  const int allocSize = 10000;
  char* testBuffer = (char*)malloc(allocSize);
  
  if (testBuffer == NULL) {
    Serial.println("  ✗ Memory allocation FAILED");
    addTestResult("Memory Test", false, "Cannot allocate");
    return false;
  }
  
  Serial.printf("  ✓ Allocated %d bytes\n", allocSize);
  
  // Fill with test pattern
  for (int i = 0; i < allocSize; i++) {
    testBuffer[i] = i % 256;
  }
  
  // Verify
  bool valid = true;
  for (int i = 0; i < allocSize; i++) {
    if (testBuffer[i] != (i % 256)) {
      valid = false;
      break;
    }
  }
  
  free(testBuffer);
  size_t finalHeap = ESP.getFreeHeap();
  
  Serial.printf("  Final Heap: %d bytes\n", finalHeap);
  Serial.printf("  Leaked: %d bytes\n", initialHeap - finalHeap);
  
  bool passed = valid && (abs((int)(initialHeap - finalHeap)) < 100);
  
  if (passed) {
    Serial.println("  ✓ Memory test passed");
    addTestResult("Memory Test", true, 
                  "No leaks, " + String(finalHeap/1024) + "KB free");
  } else {
    Serial.println("  ✗ Memory issues detected");
    addTestResult("Memory Test", false, "Potential memory leak");
  }
  
  return passed;
}

// Test 12: JSON Processing
bool testJSON() {
  Serial.println("\n[TEST 12] JSON Processing...");
  
  const char* testJson = "{\"bus_id\":\"BUS_R1\",\"line_id\":\"RED\",\"current_zone\":\"FT\"}";
  
  // Simple manual parsing (no ArduinoJson to save memory)
  String jsonStr = String(testJson);
  
  bool hasId = jsonStr.indexOf("bus_id") > 0;
  bool hasLine = jsonStr.indexOf("line_id") > 0;
  bool hasZone = jsonStr.indexOf("current_zone") > 0;
  
  Serial.printf("  Test JSON: %s\n", testJson);
  Serial.printf("  Has bus_id: %s\n", hasId ? "YES" : "NO");
  Serial.printf("  Has line_id: %s\n", hasLine ? "YES" : "NO");
  Serial.printf("  Has current_zone: %s\n", hasZone ? "YES" : "NO");
  
  bool passed = hasId && hasLine && hasZone;
  
  if (passed) {
    Serial.println("  ✓ JSON structure valid");
    addTestResult("JSON Processing", true, "Format OK");
  } else {
    Serial.println("  ✗ JSON structure invalid");
    addTestResult("JSON Processing", false, "Missing fields");
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
  Serial.printf("Architecture: %s\n", CODE_ARCH);
  Serial.printf("Compiled: %s %s\n\n", __DATE__, __TIME__);
  
  delay(1000);
  
  // Run all applicable tests
  testESP32Basic();
  testWiFi();
  testMQTT();
  testMQTTPubSub();
  testButtonAndLEDs();
  testGPS();
  testLCD();
  
  // FreeRTOS specific tests
  if (String(CODE_ARCH) == "FREERTOS") {
    testFreeRTOS();
    testFreeRTOSQueue();
    testTaskCreation();
  }
  
  testMemory();
  testJSON();
  
  // Print summary
  printTestResults();
  
  // Final status
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║         SANITY CHECK COMPLETE         ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  int passed = 0;
  for (int i = 0; i < testCount; i++) {
    if (results[i].passed) passed++;
  }
  
  Serial.printf("\nFinal Score: %d/%d (%.1f%%)\n", 
                passed, testCount, (passed * 100.0 / testCount));
  
  if (passed == testCount) {
    Serial.println("\n🎉 ALL TESTS PASSED! System ready for deployment.\n");
  } else {
    Serial.println("\n⚠️  SOME TESTS FAILED. Check the log above.\n");
  }
  
  // Show on LCD if in HALTE mode
  if (String(TEST_MODE) == "HALTE") {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SANITY CHECK DONE");
    lcd.setCursor(0, 1);
    lcd.print("Tests: ");
    lcd.print(passed);
    lcd.print("/");
    lcd.print(testCount);
    
    if (passed == testCount) {
      lcd.setCursor(0, 2);
      lcd.print("Status: ALL OK!");
      lcd.setCursor(0, 3);
      lcd.print("Ready to deploy!");
    } else {
      lcd.setCursor(0, 2);
      lcd.print("Status: ISSUES");
      lcd.setCursor(0, 3);
      lcd.print("Check Serial Log");
    }
  }
  
  Serial.println("System now running in idle mode (LED blink)");
  Serial.println("You can now upload the actual program.\n");
}

void loop() {
  // Blink LED to show system is alive
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    ledState = !ledState;
    
    if (String(TEST_MODE) == "BUS") {
      digitalWrite(PIN_LED, ledState);
    }
  }
  
  delay(100);
}