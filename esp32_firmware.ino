/*
  ===================================================================================
  ESP32 Smart Herbal Garden - Dual/Tri-Herb Firmware Gateway
  Project: Voice-Controlled Smart Herbal Garden
  
  Safety Rules Implemented:
  1. Active-LOW relay polarity initialized HIGH at boot (no spurious pulse on power-up)
  2. ADC1 channels exclusively (GPIO 32, 33, 34) - Wi-Fi safe (ADC2 disabled by Wi-Fi)
  3. Non-blocking state machine using millis() (no delay())
  4. Moisture Ceiling (Root Hypoxia Guard): Hardware cutoff if soil moisture >= 60%
  5. Dosing restriction: Strictly fixed 3-second (3000 ms) pulse (~40 mL)
  6. Absorption Cooldown: 60-second (60000 ms) lockout between doses per channel
  7. WebSocket telemetry streaming on port 81 (JSON packets)
  ===================================================================================
*/

#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------------
// Network Configuration
// ---------------------------------------------------------------------------------
const char* WIFI_SSID     = "HerbalGarden_ESP32"; // ESP32 Access Point SSID
const char* WIFI_PASSWORD = "herbalgarden123";     // AP Password (or configure STA mode)
WebSocketsServer webSocket = WebSocketsServer(81);

// ---------------------------------------------------------------------------------
// Pin Definitions (Strictly ADC1 channels for Wi-Fi compatibility)
// ---------------------------------------------------------------------------------
const int PIN_SENSOR_TULSI    = 32; // ADC1_CH4
const int PIN_SENSOR_MINT     = 33; // ADC1_CH5
const int PIN_SENSOR_ROSEMARY = 34; // ADC1_CH6

const int PIN_RELAY_TULSI     = 25; // Pump 1 Relay (Active-LOW)
const int PIN_RELAY_MINT      = 26; // Pump 2 Relay (Active-LOW)
const int PIN_RELAY_ROSEMARY  = 27; // Pump 3 Relay (Active-LOW)

// Relay active states
#define RELAY_ACTIVE   LOW
#define RELAY_INACTIVE HIGH

// ---------------------------------------------------------------------------------
// Invariant Safety Constants (Rules.md)
// ---------------------------------------------------------------------------------
const unsigned long PUMP_DOSE_DURATION_MS = 3000;   // Exactly 3 seconds (~40 mL)
const unsigned long ABSORPTION_COOLDOWN_MS = 60000; // 60 seconds capillary rest
const float MOISTURE_SAFETY_CEILING       = 60.0;  // 60% root hypoxia ceiling
const float MOISTURE_DEFICIT_THRESHOLD    = 40.0;  // 40% deficit threshold

// ---------------------------------------------------------------------------------
// Calibration Bounds (Skills.md: Calibrate Soil Moisture Sensor)
// ---------------------------------------------------------------------------------
// 12-bit ADC range: 0 - 4095
const int ADC_AIR_DRY_TULSI     = 3200;
const int ADC_WATER_WET_TULSI    = 1500;

const int ADC_AIR_DRY_MINT      = 3250;
const int ADC_WATER_WET_MINT     = 1520;

const int ADC_AIR_DRY_ROSEMARY  = 3180;
const int ADC_WATER_WET_ROSEMARY = 1480;

// ---------------------------------------------------------------------------------
// Herb State Structures
// ---------------------------------------------------------------------------------
struct HerbChannel {
  const char* id;
  const char* name;
  int sensorPin;
  int relayPin;
  int adcDry;
  int adcWet;
  int rawAdc;
  float moisture;
  bool pumpActive;
  unsigned long pumpStartTime;
  unsigned long lastDoseEndTime;
  bool inCooldown;
};

HerbChannel herbs[] = {
  { "tulsi",    "Tulsi",    PIN_SENSOR_TULSI,    PIN_RELAY_TULSI,    ADC_AIR_DRY_TULSI,    ADC_WATER_WET_TULSI,    0, 0.0, false, 0, 0, false },
  { "mint",     "Mint",     PIN_SENSOR_MINT,     PIN_RELAY_MINT,     ADC_AIR_DRY_MINT,     ADC_WATER_WET_MINT,     0, 0.0, false, 0, 0, false },
  { "rosemary", "Rosemary", PIN_SENSOR_ROSEMARY, PIN_RELAY_ROSEMARY, ADC_AIR_DRY_ROSEMARY, ADC_WATER_WET_ROSEMARY, 0, 0.0, false, 0, 0, false }
};

const int NUM_HERBS = sizeof(herbs) / sizeof(herbs[0]);

// Telemetry timer
unsigned long lastTelemetryBroadcast = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 1000; // 1 Hz broadcast

// ---------------------------------------------------------------------------------
// Sensor Sampling & Linear Calibration Mapping
// ---------------------------------------------------------------------------------
int readAveragedADC(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(250); // microsecond jitter spacing, non-blocking to loop
  }
  return (int)(sum / samples);
}

float calculateMoisture(int rawAdc, int adcDry, int adcWet) {
  // Moisture% = constrain(map(rawADC, ADC_AIR_DRY, ADC_WATER_WET, 0, 100), 0, 100)
  if (adcDry == adcWet) return 0.0;
  float val = (float)(rawAdc - adcDry) * 100.0 / (float)(adcWet - adcDry);
  if (val < 0.0) val = 0.0;
  if (val > 100.0) val = 100.0;
  return val;
}

void updateSensorReadings() {
  for (int i = 0; i < NUM_HERBS; i++) {
    herbs[i].rawAdc = readAveragedADC(herbs[i].sensorPin);
    herbs[i].moisture = calculateMoisture(herbs[i].rawAdc, herbs[i].adcDry, herbs[i].adcWet);
  }
}

// ---------------------------------------------------------------------------------
// Pump Arbitration & Execution Engine
// ---------------------------------------------------------------------------------
bool requestWaterDose(int herbIndex, String reason, String& outMessage) {
  if (herbIndex < 0 || herbIndex >= NUM_HERBS) {
    outMessage = "Unknown herb index.";
    return false;
  }

  HerbChannel &h = herbs[herbIndex];
  unsigned long now = millis();

  // Arbitration Rule 1: Pump already active
  if (h.pumpActive) {
    outMessage = String(h.name) + " pump is already dispensing.";
    return false;
  }

  // Arbitration Rule 2: Root Hypoxia Guard (Moisture >= 60%)
  if (h.moisture >= MOISTURE_SAFETY_CEILING) {
    outMessage = String("Safety Lockout: ") + h.name + " moisture is " + String(h.moisture, 1) + 
                 "%. Exceeds " + String(MOISTURE_SAFETY_CEILING, 0) + "% ceiling. Hypoxia guard active.";
    return false;
  }

  // Arbitration Rule 3: Capillary Absorption Cooldown (60s lock)
  if (h.inCooldown && (now - h.lastDoseEndTime < ABSORPTION_COOLDOWN_MS)) {
    unsigned long remainingSec = (ABSORPTION_COOLDOWN_MS - (now - h.lastDoseEndTime)) / 1000;
    outMessage = String("Cooldown Active: ") + h.name + " soil absorbing. Wait " + String(remainingSec) + "s.";
    return false;
  }

  // Approved Actuation
  h.pumpActive = true;
  h.pumpStartTime = now;
  digitalWrite(h.relayPin, RELAY_ACTIVE); // Energize pump

  outMessage = String("APPROVED: Dispensing 40mL pulse to ") + h.name + " (Reason: " + reason + ").";
  return true;
}

void checkPumpTimeouts() {
  unsigned long now = millis();

  for (int i = 0; i < NUM_HERBS; i++) {
    HerbChannel &h = herbs[i];

    // Check if active pump pulse completed
    if (h.pumpActive && (now - h.pumpStartTime >= PUMP_DOSE_DURATION_MS)) {
      digitalWrite(h.relayPin, RELAY_INACTIVE); // De-energize pump immediately
      h.pumpActive = false;
      h.lastDoseEndTime = now;
      h.inCooldown = true;
      Serial.printf("[SAFETY] Pulse ended for %s. 60s absorption cooldown started.\n", h.name);
    }

    // Check if absorption cooldown has elapsed
    if (h.inCooldown && (now - h.lastDoseEndTime >= ABSORPTION_COOLDOWN_MS)) {
      h.inCooldown = false;
      Serial.printf("[SAFETY] Absorption cooldown cleared for %s.\n", h.name);
    }
  }
}

// ---------------------------------------------------------------------------------
// WebSocket JSON Protocol Handler (Skills.md)
// ---------------------------------------------------------------------------------
void sendTelemetry(int targetClient = -1) {
#if defined(ARDUINOJSON_VERSION_MAJOR) && (ARDUINOJSON_VERSION_MAJOR >= 7)
  JsonDocument doc;
#else
  StaticJsonDocument<768> doc;
#endif
  
  for (int i = 0; i < NUM_HERBS; i++) {
    doc["herbs"][herbs[i].id]["name"] = herbs[i].name;
    doc["herbs"][herbs[i].id]["moisture"] = round(herbs[i].moisture * 10.0) / 10.0;
    doc["herbs"][herbs[i].id]["rawAdc"] = herbs[i].rawAdc;
    doc["herbs"][herbs[i].id]["pump"] = herbs[i].pumpActive;
    doc["herbs"][herbs[i].id]["cooldown"] = herbs[i].inCooldown;
    if (herbs[i].inCooldown) {
      unsigned long elapsed = millis() - herbs[i].lastDoseEndTime;
      doc["herbs"][herbs[i].id]["cooldownRemaining"] = elapsed < ABSORPTION_COOLDOWN_MS ? (ABSORPTION_COOLDOWN_MS - elapsed) / 1000 : 0;
    } else {
      doc["herbs"][herbs[i].id]["cooldownRemaining"] = 0;
    }
  }

  doc["uptimeSec"] = millis() / 1000;
  doc["safetyCeiling"] = MOISTURE_SAFETY_CEILING;

  String output;
  serializeJson(doc, output);
  if (targetClient >= 0) {
    webSocket.sendTXT((uint8_t)targetClient, output);
  } else {
    webSocket.broadcastTXT(output);
  }
}

void broadcastTelemetry() {
  sendTelemetry(-1);
}

void handleWebSocketMessage(uint8_t num, uint8_t * payload, size_t length) {
#if defined(ARDUINOJSON_VERSION_MAJOR) && (ARDUINOJSON_VERSION_MAJOR >= 7)
  JsonDocument doc;
#else
  StaticJsonDocument<256> doc;
#endif
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON Parse error: ");
    Serial.println(error.c_str());
    return;
  }

  const char* action = doc["action"]; // e.g. "DOSE", "CALIBRATE", "STATUS"
  
  if (action && strcmp(action, "DOSE") == 0) {
    const char* herbId = doc["herb"];
    if (!herbId) return;

    int targetIdx = -1;
    for (int i = 0; i < NUM_HERBS; i++) {
      if (strcmp(herbs[i].id, herbId) == 0) {
        targetIdx = i;
        break;
      }
    }

    String responseMsg;
    bool approved = requestWaterDose(targetIdx, "WebSocket Client", responseMsg);

    // Send acknowledgement back to requesting client
#if defined(ARDUINOJSON_VERSION_MAJOR) && (ARDUINOJSON_VERSION_MAJOR >= 7)
    JsonDocument ack;
#else
    StaticJsonDocument<256> ack;
#endif
    ack["type"] = "COMMAND_ACK";
    ack["success"] = approved;
    ack["message"] = responseMsg;
    ack["herb"] = herbId;

    String ackStr;
    serializeJson(ack, ackStr);
    webSocket.sendTXT(num, ackStr);
  }
  else if (action && strcmp(action, "CALIBRATE") == 0) {
    // Update calibration bounds dynamically
    const char* herbId = doc["herb"];
    if (!herbId) return; // Guard against null pointer dereference

    int dry = doc["adcDry"] | -1;
    int wet = doc["adcWet"] | -1;

    bool found = false;
    for (int i = 0; i < NUM_HERBS; i++) {
      if (strcmp(herbs[i].id, herbId) == 0) {
        if (dry > 0) herbs[i].adcDry = dry;
        if (wet > 0) herbs[i].adcWet = wet;
        herbs[i].moisture = calculateMoisture(herbs[i].rawAdc, herbs[i].adcDry, herbs[i].adcWet);
        Serial.printf("Calibrated %s: Dry=%d, Wet=%d\n", herbs[i].name, herbs[i].adcDry, herbs[i].adcWet);
        found = true;
        break;
      }
    }

    // Send acknowledgement back to client
#if defined(ARDUINOJSON_VERSION_MAJOR) && (ARDUINOJSON_VERSION_MAJOR >= 7)
    JsonDocument ack;
#else
    StaticJsonDocument<256> ack;
#endif
    ack["type"] = "COMMAND_ACK";
    ack["success"] = found;
    ack["message"] = found ? "Calibration updated." : "Herb not found.";
    ack["herb"] = herbId;

    String ackStr;
    serializeJson(ack, ackStr);
    webSocket.sendTXT(num, ackStr);

    // Send updated telemetry
    broadcastTelemetry();
  }
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Client Disconnected\n", num);
      break;
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Client Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
        // Send immediate telemetry directly to the newly connected client
        sendTelemetry(num);
      }
      break;
    case WStype_TEXT:
      handleWebSocketMessage(num, payload, length);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------------
// Setup & Loop
// ---------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n--- Initializing ESP32 Smart Herbal Garden ---");

  // INVARIANT SAFETY RULE: Relay polarity must explicitly default to HIGH upon boot
  // to prevent spurious activation on active-LOW optoisolated relay modules.
  for (int i = 0; i < NUM_HERBS; i++) {
    digitalWrite(herbs[i].relayPin, RELAY_INACTIVE); // Pre-set HIGH
    pinMode(herbs[i].relayPin, OUTPUT);
    digitalWrite(herbs[i].relayPin, RELAY_INACTIVE); // Enforce HIGH
  }

  // ADC1 Pins setup
  for (int i = 0; i < NUM_HERBS; i++) {
    pinMode(herbs[i].sensorPin, INPUT);
  }
  analogReadResolution(12); // 12-bit ADC (0 - 4095)
  analogSetAttenuation(ADC_11db); // Full range 0 - ~3.1V for capacitive sensors

  // Wi-Fi Access Point setup
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false); // Disable modem sleep to minimize WebSocket latency
  Serial.print("Access Point created. IP: ");
  Serial.println(WiFi.softAPIP());

  // Start WebSocket Server on Port 81
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  Serial.println("WebSocket Server started on port 81 (ws://192.168.4.1:81)");

  // Initial sensor pass
  updateSensorReadings();
}

void loop() {
  webSocket.loop();
  checkPumpTimeouts();

  // Periodic sensor sampling and telemetry broadcast
  unsigned long now = millis();
  if (now - lastTelemetryBroadcast >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryBroadcast = now;
    updateSensorReadings();
    broadcastTelemetry();
  }
}
