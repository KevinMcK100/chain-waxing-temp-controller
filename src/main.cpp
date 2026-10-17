#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include "config.h"

// OLED display specifications
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// DS18B20 pin definition
const int oneWireBus = 4;
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

// Overshoot above target at which we consider the wax "cooling"
// (matches the OLED "COOLING to" state and drives the red chart segments)
#define COOLING_OVERSHOOT 2.0

// Declare global instances here so web server sees them immediately
WebServer server(80);
Preferences preferences;

// Runtime state variables
float tempC = 0.0;
float targetTemp = 95.0;
float bulkCoastMargin = 5.0;
float holdReheatTrigger = 1.0;
float holdCoastMargin = 0.5;
float startTemp = 20.0;

bool heaterState = false;
bool isCoasting = false;
bool isHolding = false;

// Notification state tracking flags
bool coastNotified = false;
bool targetNotified = false;
bool overTempNotified = false;
bool sensorFaultNotified = false;

// Timers & Animation state
unsigned long lastSensorMillis = 0;
unsigned long lastAnimMillis = 0;
const long sensorInterval = 2000;
const long animInterval = 300;
int animFrame = 0;

bool initialRunDone = false;

// In-Memory Ring Buffer for Full Session (2000 points @ 2s = ~66 minutes)
#define HISTORY_SIZE 2000
float tempHistory[HISTORY_SIZE];
uint8_t phaseHistory[HISTORY_SIZE];
int historyIndex = 0;
bool historyFull = false;

// Ring-buffered log capture (served at /logs)
#define LOG_BUF_SIZE 4096
char logBuf[LOG_BUF_SIZE];
int logHead = 0;
bool logFull = false;

void logMsg(const char* msg) {
  Serial.print(msg);
  int len = strlen(msg);
  for (int i = 0; i < len; i++) {
    logBuf[logHead] = msg[i];
    logHead = (logHead + 1) % LOG_BUF_SIZE;
    if (logHead == 0) logFull = true;
  }
}

// Function Prototypes for Main Sketch
void setSmartPlug(bool turnOn);
void sendNtfyNotification(String message);
void renderDisplay();

// INCLUDE YOUR HEADER HERE BEFORE SETUP() SO ROUTES ARE RECOGNIZED
#include "wax_webserver.h"

void sendNtfyNotification(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(ntfyServer) + "/" + ntfyTopic;
    http.begin(url);
    http.addHeader("Content-Type", "text/plain");

    int httpResponseCode = http.POST(message);
    if (httpResponseCode > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "ntfy sent successfully. Response code: %d\n", httpResponseCode);
      logMsg(buf);
    } else {
      char buf[64];
      snprintf(buf, sizeof(buf), "Error sending ntfy: %d\n", httpResponseCode);
      logMsg(buf);
    }
    http.end();
  }
}

void setSmartPlug(bool turnOn) {
  heaterState = turnOn;

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String action = turnOn ? "turn_on" : "turn_off";

    String entity = String(smartPlugEntity);
    entity.replace(" ", "%20");

    String url = "http://" + String(smartPlugIP) + "/switch/" + entity + "/" + action;

    http.begin(url);
    int httpResponseCode = http.POST("");

    if (httpResponseCode > 0) {
      char buf[128];
      snprintf(buf, sizeof(buf), "ESPHome smart plug command sent (%s). Code: %d\n", action.c_str(), httpResponseCode);
      logMsg(buf);
    } else {
      logMsg("ESPHome smart plug HTTP connection failed.\n");
    }
    http.end();
  }
}

// 0 = blue  (heating up, or below the hold-reheat line during a hold)
// 1 = green (holding, inside the desired band)
// 2 = red   (overshot target, cooling back down)
uint8_t computePhase() {
  if (tempC > targetTemp + COOLING_OVERSHOOT) return 2;
  if (isHolding && tempC > targetTemp - holdReheatTrigger) return 1;
  return 0;
}

void renderDisplay() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.cp437(true);

  if (tempC > targetTemp + COOLING_OVERSHOOT) {
    display.print("COOLING to ");
    display.print((int)targetTemp);
    display.write(167);
    display.print("C ");

    int blockCount = 5 - (animFrame % 6);
    for (int i = 0; i < blockCount; i++) {
      display.write(219);
    }
  }
  else if (isCoasting && !heaterState && tempC < targetTemp) {
    if (animFrame % 2 == 0) {
      display.print("COASTING to ");
      display.print((int)targetTemp);
      display.write(167);
      display.print("C");
    }
  }
  else if (!heaterState) {
    if (animFrame % 2 == 0) {
      display.print("HOLDING at ");
      display.print((int)targetTemp);
      display.write(167);
      display.print("C");
    }
  }
  else {
    display.print("HEATING to ");
    display.print((int)targetTemp);
    display.write(167);
    display.print("C ");

    int blockCount = animFrame % 6;
    for (int i = 0; i < blockCount; i++) {
      display.write(219);
    }
  }

  display.setCursor(0, 14);
  display.setTextSize(5);
  display.print(tempC, 1);

  display.setTextSize(1);
  display.setCursor(2, 55);
  display.print((int)startTemp);

  display.drawRect(16, 54, 96, 10, SSD1306_WHITE);

  float denominator = targetTemp - startTemp;
  if (denominator < 1.0) denominator = 1.0;
  float progressFraction = (tempC - startTemp) / denominator;
  progressFraction = constrain(progressFraction, 0.0, 1.0);
  int fillWidth = (int)(progressFraction * 92);

  if (fillWidth > 0) {
    display.fillRect(18, 56, fillWidth, 6, SSD1306_WHITE);
  }

  display.setCursor(114, 55);
  display.print((int)targetTemp);

  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  preferences.begin("wax-config", true);
  targetTemp = preferences.getFloat("target", 95.0);
  bulkCoastMargin = preferences.getFloat("bulkcoast", 5.0);
  holdReheatTrigger = preferences.getFloat("holdtrig", 1.0);
  holdCoastMargin = preferences.getFloat("holdcoast", 0.5);
  preferences.end();

  if (holdCoastMargin >= holdReheatTrigger) {
    holdCoastMargin = holdReheatTrigger / 2.0f;
  }
  logMsg("Preferences loaded.\n");

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    logMsg("SSD1306 allocation failed\n");
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logMsg(".");
  }

  logMsg("\nWiFi connected.\n");
  char ipBuf[32];
  snprintf(ipBuf, sizeof(ipBuf), "IP address: %s\n", WiFi.localIP().toString().c_str());
  logMsg(ipBuf);

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/update", handleUpdate);
  server.on("/logs", handleLogs);
  server.begin();
  logMsg("Web server started.\n");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.setCursor(0, 16);
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);

  sensors.begin();
}

void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  if (!initialRunDone) {
    sensors.requestTemperatures();
    startTemp = sensors.getTempCByIndex(0);
    if (startTemp < -50 || startTemp > 150) {
      startTemp = 20.0;
    }
    tempC = startTemp;

    if (tempC < (targetTemp - bulkCoastMargin)) {
      logMsg("Wax below target on boot. Turning heater ON.\n");
      isCoasting = false;
      isHolding = false;
      setSmartPlug(true);
    } else {
      targetNotified = true;
      isCoasting = false;
      isHolding = true;
      setSmartPlug(false);
    }

    // Log history after the state machine has settled
    tempHistory[historyIndex] = tempC;
    phaseHistory[historyIndex] = computePhase();
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;

    initialRunDone = true;
  }

  if (currentMillis - lastAnimMillis >= animInterval) {
    lastAnimMillis = currentMillis;
    animFrame = (animFrame + 1) % 6;
    renderDisplay();
  }

  if (currentMillis - lastSensorMillis >= sensorInterval) {
    lastSensorMillis = currentMillis;

    sensors.requestTemperatures();
    tempC = sensors.getTempCByIndex(0);

    // 1. SENSOR FAULT CHECK
    // 85.0 is deliberately excluded — it is the DS18B20 power-on default AND a
    // legitimate temperature the wax passes through on every heat-up.
    if (tempC == -127.0 || tempC < -50 || tempC > 150) {
      if (!sensorFaultNotified) {
        sendNtfyNotification("ERROR: Temperature sensor disconnected or invalid reading detected!");
        sensorFaultNotified = true;
      }
      // Reset state-machine latches so the controller can resume cleanly
      // when the sensor recovers on the next cycle.
      coastNotified = false;
      targetNotified = false;
      isCoasting = false;
      setSmartPlug(false);
      return;  // Skip logging bad samples entirely
    } else {
      sensorFaultNotified = false;
    }

    // 2. OVER-TEMPERATURE SAFETY ALERT
    if (tempC >= (targetTemp + 10.0)) {
      if (!overTempNotified) {
        sendNtfyNotification("WARNING: Wax temp over limit at " + String(tempC, 1) + "°C! Check controller.");
        overTempNotified = true;
      }
      setSmartPlug(false);
    } else {
      overTempNotified = false;
    }

    float cutoffTemp = !isHolding ? (targetTemp - bulkCoastMargin) : (targetTemp - holdCoastMargin);
    float restartTemp = !isHolding ? (targetTemp - (bulkCoastMargin + 2.0f)) : (targetTemp - holdReheatTrigger);

    // 3. COASTING CHECK
    if (tempC >= cutoffTemp && heaterState && !coastNotified) {
      if (!isHolding) {
        sendNtfyNotification("Wax temp at " + String(tempC, 1) + "°C. Heater turned OFF, coasting to target " + String(targetTemp, 1) + "°C.");
        coastNotified = true;
      }
      isCoasting = true;
      setSmartPlug(false);
    }

    // 4. TARGET REACHED -> TRANSITION TO HOLDING MODE
    if (tempC >= targetTemp && !targetNotified) {
      sendNtfyNotification("Wax temp reached target " + String(targetTemp, 1) + "°C.");
      targetNotified = true;
      isCoasting = false;
      isHolding = true;
    }

    // 5. REHEATING PHASE STARTED
    if (tempC <= restartTemp && !heaterState) {
      coastNotified = false;
      targetNotified = false;
      isCoasting = false;

      if (!isHolding) {
        sendNtfyNotification("Wax temp dropped to " + String(tempC, 1) + "°C. Heater turned ON, reheating to " + String(targetTemp, 1) + "°C.");
      }
      setSmartPlug(true);
    }

    // 6. LOG HISTORY (after state machine has settled this cycle)
    tempHistory[historyIndex] = tempC;
    phaseHistory[historyIndex] = computePhase();
    historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    if (historyIndex == 0) historyFull = true;
  }

  // --- WiFi health check & heap diagnostics (every 30 s) ---
  static unsigned long lastDiagMillis = 0;
  if (currentMillis - lastDiagMillis >= 30000) {
    lastDiagMillis = currentMillis;

    if (WiFi.status() != WL_CONNECTED) {
      logMsg("WiFi disconnected. Attempting reconnect...\n");
      WiFi.reconnect();
    }

    char diagBuf[64];
    snprintf(diagBuf, sizeof(diagBuf), "Free heap: %u  Max alloc block: %u\n",
      ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    logMsg(diagBuf);
  }
}