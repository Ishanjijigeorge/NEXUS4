// ==================================================
// ESP32 WIFI BRIDGE – FINAL (ANGLE + SPEED + CLEAN)
// ==================================================

#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>

// ============= WIFI CONFIG =============
const char* ssid = "5.0";
const char* password = "ishan008";

WebServer server(80);
HardwareSerial& arduinoSerial = Serial2;

// ============= STATE VARIABLES =============
float currentAngle = 0;
float currentSpeed = 0;
bool emergencyActive = false;
unsigned long lastUpdate = 0;

// ============= CORS =============
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ============= SEND TO ARDUINO =============
void sendToArduino(String cmd) {
  arduinoSerial.println(cmd);
  Serial.print("→ ");
  Serial.println(cmd);
}

// ============= PARSE SERIAL DATA =============
void parseArduino(String data) {

  data.trim();
  if (data.length() == 0) return;

  Serial.print("← ");
  Serial.println(data);

  if (data.startsWith("ANGLE:")) {
    currentAngle = data.substring(6).toFloat();
    lastUpdate = millis();
  }

  else if (data.startsWith("SPEED:")) {
    currentSpeed = data.substring(6).toFloat();
  }

  else if (data == "STOP") {
    currentSpeed = 0;
  }

  else if (data == "SYSTEM_READY") {
    Serial.println("Arduino Ready");
  }
}

// ============= ROUTES =============

void handleRoot() {
  addCORS();
  server.send(200, "text/plain", "ESP32 Bridge Running");
}

void handleCmd() {
  addCORS();

  if (!server.hasArg("data")) {
    server.send(400, "text/plain", "Missing data");
    return;
  }

  String cmd = server.arg("data");
  sendToArduino(cmd);

  server.send(200, "text/plain", "OK");
}

void handleSpeed() {
  addCORS();

  if (!server.hasArg("speed")) {
    server.send(400, "text/plain", "Missing speed");
    return;
  }

  int percent = server.arg("speed").toInt();

  if (percent < 0 || percent > 100) {
    server.send(400, "text/plain", "Invalid range");
    return;
  }

  sendToArduino("CMD:SPEED:" + String(percent));

  server.send(200, "text/plain", "SPEED_SET");
}

void handleAngle() {
  addCORS();
  server.send(200, "text/plain", String(currentAngle, 1));
}

void handleTelemetry() {
  addCORS();

  StaticJsonDocument<200> doc;
  doc["angle"] = currentAngle;
  doc["speed"] = currentSpeed;
  doc["emergency"] = emergencyActive;
  doc["last_update"] = millis() - lastUpdate;

  String res;
  serializeJson(doc, res);

  server.send(200, "application/json", res);
}

void handleEmergency() {
  addCORS();

  emergencyActive = true;

  // 🔥 HARD STOP
  sendToArduino("CMD:stop");

  server.send(200, "application/json",
    "{\"status\":\"EMERGENCY_TRIGGERED\"}");
}

void handleResetEmergency() {
  addCORS();

  emergencyActive = false;

  server.send(200, "application/json",
    "{\"status\":\"RESET\"}");
}

void handlePing() {
  addCORS();
  server.send(200, "text/plain", "PONG");
}

// ============= SETUP =============
void setup() {

  Serial.begin(115200);
  arduinoSerial.begin(115200, SERIAL_8N1, 16, 17);

  Serial.println("\nESP32 BRIDGE STARTING...\n");

  WiFi.begin(ssid, password);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nCONNECTED");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFAILED WIFI");
  }

  // ROUTES
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.on("/speed", handleSpeed);
  server.on("/angle", handleAngle);
  server.on("/telemetry", handleTelemetry);
  server.on("/emergency", handleEmergency);
  server.on("/reset", handleResetEmergency);
  server.on("/ping", handlePing);

  server.begin();
}

// ============= LOOP =============
void loop() {

  server.handleClient();

  // 🔥 READ SERIAL FAST
  while (arduinoSerial.available()) {
    String data = arduinoSerial.readStringUntil('\n');
    parseArduino(data);
  }

  // 🔥 AUTO WIFI RECONNECT
  static unsigned long lastCheck = 0;

  if (millis() - lastCheck > 5000) {
    lastCheck = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconnecting WiFi...");
      WiFi.reconnect();
    }
  }

  delay(5);
}
