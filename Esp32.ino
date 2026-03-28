// ESP32_WIFI_BRIDGE_WITH_SPEED_CONTROL.ino
// Acts as a transparent bridge: WiFi (HTTP) <--> Serial (Arduino)
// Includes CORS headers, speed control, and angle monitoring

#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>

// ============= CONFIGURATION =============
const char* ssid = "5.0";        // Change to your WiFi
const char* password = "ishan008";

WebServer server(80);
HardwareSerial& arduinoSerial = Serial2;     // ESP32 Serial2 (RX=16, TX=17)

// Global variables for telemetry
float currentAngle = 0;
unsigned long lastAngleUpdate = 0;
bool emergencyActive = false;
int currentSpeed = 20;  // Default speed (matches Arduino's MOTOR_SPEED)

// ============= CORS HEADERS =============
void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ============= FORWARD COMMAND TO ARDUINO =============
void forwardCommand(String cmd) {
  arduinoSerial.println(cmd);
  Serial.print("Forwarded: ");
  Serial.println(cmd);
}

// ============= PARSE ARDUINO TELEMETRY =============
void parseArduinoData(String data) {
  data.trim();
  
  // Parse angle data
  if (data.startsWith("ANGLE:")) {
    String angleStr = data.substring(6);
    currentAngle = angleStr.toFloat();
    lastAngleUpdate = millis();
  }
  // Parse speed confirmation
  else if (data.startsWith("SPEED_SET:")) {
    String speedStr = data.substring(9);
    currentSpeed = speedStr.toInt();
    Serial.print("Speed confirmed: ");
    Serial.println(currentSpeed);
  }
  // Parse tilt stop
  else if (data.startsWith("TILT STOP:")) {
    String angleStr = data.substring(10);
    float tiltAngle = angleStr.toFloat();
    Serial.print("TILT WARNING! Angle: ");
    Serial.println(tiltAngle);
  }
  // Parse system ready
  else if (data == "SYSTEM_READY") {
    Serial.println("Arduino system ready");
  }
}

// ============= HTTP ENDPOINTS =============
void handleRoot() {
  addCORSHeaders();
  String html = "ESP32 Bridge Active\n";
  html += "Commands:\n";
  html += "- GET /cmd?data=CMD:forward\n";
  html += "- GET /cmd?data=CMD:left\n";
  html += "- GET /cmd?data=CMD:right\n";
  html += "- GET /cmd?data=CMD:back\n";
  html += "- GET /cmd?data=CMD:stop\n";
  html += "- GET /speed?speed=0-255\n";
  html += "- GET /angle (returns current tilt angle)\n";
  html += "- GET /status (returns system status)\n";
  html += "- GET /emergency\n";
  server.send(200, "text/plain", html);
}

void handleOptions() {
  // Handle preflight CORS requests
  addCORSHeaders();
  server.send(204);
}

void handleCommand() {
  addCORSHeaders();
  
  if (server.hasArg("data")) {
    String command = server.arg("data");
    command.trim();
    
    if (command.length() > 0) {
      forwardCommand(command);
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Empty command");
    }
  } else {
    server.send(400, "text/plain", "Missing 'data' parameter");
  }
}

void handleSpeed() {
  addCORSHeaders();
  
  if (server.hasArg("speed")) {
    int percent = server.arg("speed").toInt();
    
    if (percent >= 0 && percent <= 100) {

      // Send percentage directly
      forwardCommand("CMD:SPEED:" + String(percent));
      
      String response = "{\"status\":\"success\",\"speed_percent\":" + String(percent) + "}";
      server.send(200, "application/json", response);

    } else {
      server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Speed must be 0-100\"}");
    }
  } else {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing speed\"}");
  }
}

void handleAngle() {
  addCORSHeaders();
  
  // Return current angle as simple text for easy parsing
  String angleStr = String(currentAngle, 1);
  server.send(200, "text/plain", angleStr);
}

// Add this to handleEmergency() function in ESP32 code
void handleEmergency() {
  addCORSHeaders();
  // Send emergency stop with immediate brake
  forwardCommand("EMERGENCY");
  emergencyActive = true;
  
  // Optional: Send additional stop command for redundancy
  forwardCommand("CMD:stop");
  
  String response = "{\"status\":\"emergency_activated\",\"message\":\"Emergency stop with active braking sent\"}";
  server.send(200, "application/json", response);
}

void handlePing() {
  addCORSHeaders();
  server.send(200, "text/plain", "PONG");
}

void handleStatus() {
  addCORSHeaders();
  
  // Create JSON status response
  StaticJsonDocument<200> doc;
  doc["connected"] = true;
  doc["ip"] = WiFi.localIP().toString();
  doc["angle"] = currentAngle;
  doc["emergency_active"] = emergencyActive;
  doc["current_speed"] = currentSpeed;
  doc["uptime"] = millis() / 1000;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleResetEmergency() {
  addCORSHeaders();
  emergencyActive = false;
  String response = "{\"status\":\"success\",\"message\":\"Emergency reset\"}";
  server.send(200, "application/json", response);
}

void handleTelemetry() {
  addCORSHeaders();
  
  // Comprehensive telemetry endpoint
  StaticJsonDocument<256> doc;
  doc["angle"] = currentAngle;
  doc["angle_updated"] = lastAngleUpdate;
  doc["emergency"] = emergencyActive;
  doc["speed"] = currentSpeed;
  doc["timestamp"] = millis();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// ============= SETUP =============
void setup() {
  Serial.begin(115200);
  arduinoSerial.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  
  Serial.println("\n\n==================================");
  Serial.println("ESP32 Wheelchair Bridge Starting...");
  Serial.println("==================================\n");
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✓ WiFi Connected!");
    Serial.print("  IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("  Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm\n");
  } else {
    Serial.println("✗ WiFi Failed! Check credentials");
    Serial.println("  Make sure SSID and password are correct\n");
  }
  
  // Setup HTTP endpoints
  server.on("/", handleRoot);
  server.on("/cmd", HTTP_GET, handleCommand);
  server.on("/cmd", HTTP_OPTIONS, handleOptions);
  server.on("/speed", HTTP_GET, handleSpeed);
  server.on("/speed", HTTP_OPTIONS, handleOptions);
  server.on("/angle", HTTP_GET, handleAngle);
  server.on("/angle", HTTP_OPTIONS, handleOptions);
  server.on("/emergency", HTTP_GET, handleEmergency);
  server.on("/emergency", HTTP_OPTIONS, handleOptions);
  server.on("/reset_emergency", HTTP_GET, handleResetEmergency);
  server.on("/reset_emergency", HTTP_OPTIONS, handleOptions);
  server.on("/ping", HTTP_GET, handlePing);
  server.on("/ping", HTTP_OPTIONS, handleOptions);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/telemetry", HTTP_GET, handleTelemetry);
  server.on("/telemetry", HTTP_OPTIONS, handleOptions);
  
  server.begin();
  Serial.println("✓ HTTP Server Started with CORS enabled");
  Serial.println("\nAvailable Endpoints:");
  Serial.println("  GET /cmd?data=CMD:forward   - Control wheelchair");
  Serial.println("  GET /speed?speed=150        - Set motor speed (0-255)");
  Serial.println("  GET /angle                  - Get current tilt angle");
  Serial.println("  GET /emergency              - Emergency stop");
  Serial.println("  GET /status                 - System status");
  Serial.println("  GET /telemetry              - Full telemetry data");
  Serial.println("\n----------------------------------\n");
  
  // Test Arduino connection
  delay(1000);
  arduinoSerial.println("PING");
  Serial.println("→ Sent test PING to Arduino");
}

// ============= MAIN LOOP =============
void loop() {
  // Handle incoming HTTP requests
  server.handleClient();
  
  // Forward any data from Arduino back to Serial monitor
  // and parse telemetry data
  while (arduinoSerial.available()) {
    String data = arduinoSerial.readStringUntil('\n');
    data.trim();
    
    if (data.length() > 0) {
      // Echo to Serial monitor for debugging
      Serial.print("← ");
      Serial.println(data);
      
      // Parse telemetry data
      parseArduinoData(data);
    }
  }
  
  // WiFi reconnection check
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 5000) {
      Serial.println("⚠ WiFi lost! Reconnecting...");
      WiFi.reconnect();
      lastReconnectAttempt = millis();
    }
  }
  
  // Optional: Send periodic ping to keep connection alive
  static unsigned long lastPing = 0;
  if (millis() - lastPing > 10000) {  // Every 10 seconds
    arduinoSerial.println("PING");
    lastPing = millis();
  }
  
  delay(10);  // Keep responsive
}
