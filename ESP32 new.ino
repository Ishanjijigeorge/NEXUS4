// ==================================================
// ESP32 WIFI BRIDGE – UPDATED (BLINK + MOVEMENT + SAFETY)
// + DHT11 + MQ3 SENSOR SUPPORT
// + MODE PASS-THROUGH + DIR PARSE SUPPORT
// + NON-BLOCKING SERIAL FIX FOR BLINK OUTPUT
// ==================================================

#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ================= WIFI =================
const char* ssid = "IQOO";
const char* password = "123456789";

WebServer server(80);
HardwareSerial& arduinoSerial = Serial2;

// ================= DHT11 + MQ3 =================
#define DHT_PIN 4
#define DHT_TYPE DHT11
#define MQ3_PIN 34

DHT dht(DHT_PIN, DHT_TYPE);

float temperatureC = 0;
float humidity = 0;
int mq3Value = 0;

// ================= ULTRASONIC PINS =================
#define TRIG_FL 12
#define ECHO_FL 13

#define TRIG_FR 18
#define ECHO_FR 19

#define TRIG_BL 23
#define ECHO_BL 25

#define TRIG_BR 14
#define ECHO_BR 15

// ================= OBSTACLE SETTINGS =================
// Keep min small to avoid ignoring close obstacles.
// If you get false stops, raise OBSTACLE_MIN to 3 or 5.
#define OBSTACLE_MIN 1
#define OBSTACLE_MAX 30

// ================= TIMING =================
const unsigned long ULTRASONIC_INTERVAL = 80;
const unsigned long MQ3_INTERVAL = 250;
const unsigned long DHT_INTERVAL = 2000;
const unsigned long WIFI_CHECK_INTERVAL = 5000;

// ================= STATE =================
float currentAngle = 0;
float currentSpeed = 0;

float distFrontLeft  = -1;
float distFrontRight = -1;
float distBackLeft   = -1;
float distBackRight  = -1;

String movementDirection = "stop";

bool emergencyActive = false;
bool obstacleActive = false;

unsigned long lastUpdate = 0;
unsigned long lastUltrasonicRead = 0;
unsigned long lastMq3Read = 0;
unsigned long lastDhtRead = 0;
unsigned long lastWifiCheck = 0;

String serialBuffer = "";

// ================= HELPERS =================
String normalizeDirection(String dir) {
  dir.trim();
  dir.toLowerCase();

  if (dir == "backward" || dir == "reverse") return "back";
  if (dir == "stopped") return "stop";

  return dir;
}

void updateDirectionFromCommand(const String& cmd) {
  if (cmd == "CMD:forward") movementDirection = "forward";
  else if (cmd == "CMD:back") movementDirection = "back";
  else if (cmd == "CMD:left") movementDirection = "left";
  else if (cmd == "CMD:right") movementDirection = "right";
  else if (cmd == "CMD:stop") {
    movementDirection = "stop";
    currentSpeed = 0;
  }
}

bool isObstacleDistance(float d) {
  return (d >= OBSTACLE_MIN && d <= OBSTACLE_MAX);
}

// ================= CORS =================
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ================= SEND TO ARDUINO =================
void sendToArduino(String cmd) {
  arduinoSerial.println(cmd);

  Serial.print("-> ");
  Serial.println(cmd);

  updateDirectionFromCommand(cmd);
}

// ================= ULTRASONIC =================
float readUltrasonic(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 12000);

  if (duration == 0) return -1;

  return duration * 0.034 / 2.0;
}

// ================= OBSTACLE CHECK =================
void checkObstacle() {
  bool frontLeftObs  = isObstacleDistance(distFrontLeft);
  bool frontRightObs = isObstacleDistance(distFrontRight);
  bool backLeftObs   = isObstacleDistance(distBackLeft);
  bool backRightObs  = isObstacleDistance(distBackRight);

  bool obstacle = false;
  String dir = normalizeDirection(movementDirection);

  if (dir == "forward") {
    obstacle = frontLeftObs || frontRightObs;
  }
  else if (dir == "back") {
    obstacle = backLeftObs || backRightObs;
  }
  else if (dir == "right") {
    obstacle = frontLeftObs || backLeftObs;
  }
  else if (dir == "left") {
    obstacle = frontRightObs || backRightObs;
  }
  else {
    obstacle = frontLeftObs || frontRightObs || backLeftObs || backRightObs;
  }

  if (obstacle && !obstacleActive) {
    Serial.println("OBSTACLE STOP");
    arduinoSerial.println("CMD:stop");
    movementDirection = "stop";
    currentSpeed = 0;
  }

  obstacleActive = obstacle;
}

// ================= SENSOR READ =================
void readSensors() {
  distFrontLeft  = -1;
  distFrontRight = -1;
  distBackLeft   = -1;
  distBackRight  = -1;

  String dir = normalizeDirection(movementDirection);

  if (dir == "forward") {
    distFrontLeft  = readUltrasonic(TRIG_FL, ECHO_FL);
    distFrontRight = readUltrasonic(TRIG_FR, ECHO_FR);
  }
  else if (dir == "back") {
    distBackLeft   = readUltrasonic(TRIG_BL, ECHO_BL);
    distBackRight  = readUltrasonic(TRIG_BR, ECHO_BR);
  }
  else if (dir == "right") {
    distFrontLeft  = readUltrasonic(TRIG_FL, ECHO_FL);
    distBackLeft   = readUltrasonic(TRIG_BL, ECHO_BL);
  }
  else if (dir == "left") {
    distFrontRight = readUltrasonic(TRIG_FR, ECHO_FR);
    distBackRight  = readUltrasonic(TRIG_BR, ECHO_BR);
  }
  else {
    distFrontLeft  = readUltrasonic(TRIG_FL, ECHO_FL);
    distFrontRight = readUltrasonic(TRIG_FR, ECHO_FR);
    distBackLeft   = readUltrasonic(TRIG_BL, ECHO_BL);
    distBackRight  = readUltrasonic(TRIG_BR, ECHO_BR);
  }

  checkObstacle();
}

// ================= ENV SENSOR READ =================
void readDHTSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) temperatureC = t;
  if (!isnan(h)) humidity = h;
}

void readMQ3Sensor() {
  mq3Value = analogRead(MQ3_PIN);
}

// ================= PARSE SERIAL =================
void parseArduino(String data) {
  data.trim();
  if (data.length() == 0) return;

  Serial.print("<- ");
  Serial.println(data);

  if (data.startsWith("ANGLE:")) {
    currentAngle = data.substring(6).toFloat();
    lastUpdate = millis();
  }
  else if (data.startsWith("SPEED:")) {
    currentSpeed = data.substring(6).toFloat();
  }
  else if (data.startsWith("DIR:")) {
    movementDirection = normalizeDirection(data.substring(4));
  }
  else if (data == "STOP") {
    currentSpeed = 0;
    movementDirection = "stop";
  }
  else if (data == "SYSTEM_READY") {
    Serial.println("Arduino Ready");
  }
  else if (data == "READY") {
    Serial.println("EEG Ready");
  }
  else if (data.startsWith("Blink Count:")) {
    Serial.println(data);
  }
  else if (data.startsWith("MODE:")) {
    Serial.println(data);
  }
}

// ================= NON-BLOCKING SERIAL READER =================
void processArduinoSerial() {
  while (arduinoSerial.available()) {
    char c = (char)arduinoSerial.read();

    if (c == '\r') continue;

    if (c == '\n') {
      if (serialBuffer.length() > 0) {
        parseArduino(serialBuffer);
        serialBuffer = "";
      }
    }
    else {
      if (serialBuffer.length() < 120) {
        serialBuffer += c;
      } else {
        serialBuffer = "";
      }
    }
  }
}

// ================= ROUTES =================
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
  sendToArduino("CMD:SPEED:" + String(percent));

  server.send(200, "text/plain", "SPEED_SET");
}

void handleAngle() {
  addCORS();
  server.send(200, "text/plain", String(currentAngle, 1));
}

void handleTelemetry() {
  addCORS();

  StaticJsonDocument<320> doc;

  doc["angle"] = currentAngle;
  doc["speed"] = currentSpeed;
  doc["emergency"] = emergencyActive;

  doc["FL"] = distFrontLeft;
  doc["FR"] = distFrontRight;
  doc["BL"] = distBackLeft;
  doc["BR"] = distBackRight;

  doc["temp"] = temperatureC;
  doc["humidity"] = humidity;
  doc["mq3"] = mq3Value;

  doc["obstacle"] = obstacleActive;
  doc["direction"] = movementDirection;
  doc["last_update"] = millis() - lastUpdate;

  String res;
  serializeJson(doc, res);

  server.send(200, "application/json", res);
}

void handleEmergency() {
  addCORS();

  emergencyActive = true;
  sendToArduino("CMD:stop");

  server.send(200, "application/json", "{\"status\":\"EMERGENCY_TRIGGERED\"}");
}

void handleResetEmergency() {
  addCORS();

  emergencyActive = false;
  server.send(200, "application/json", "{\"status\":\"RESET\"}");
}

void handlePing() {
  addCORS();
  server.send(200, "text/plain", "PONG");
}

void handleOptions() {
  addCORS();
  server.send(204);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  arduinoSerial.begin(115200, SERIAL_8N1, 16, 17);

  WiFi.setSleep(false);

  dht.begin();
  pinMode(MQ3_PIN, INPUT);

  pinMode(TRIG_FL, OUTPUT);
  pinMode(ECHO_FL, INPUT);

  pinMode(TRIG_FR, OUTPUT);
  pinMode(ECHO_FR, INPUT);

  pinMode(TRIG_BL, OUTPUT);
  pinMode(ECHO_BL, INPUT);

  pinMode(TRIG_BR, OUTPUT);
  pinMode(ECHO_BR, INPUT);

  Serial.println("\nESP32 BRIDGE STARTING...\n");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nCONNECTED");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/cmd", HTTP_GET, handleCmd);
  server.on("/speed", HTTP_GET, handleSpeed);
  server.on("/angle", HTTP_GET, handleAngle);
  server.on("/telemetry", HTTP_GET, handleTelemetry);
  server.on("/emergency", HTTP_GET, handleEmergency);
  server.on("/reset", HTTP_GET, handleResetEmergency);
  server.on("/ping", HTTP_GET, handlePing);

  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/cmd", HTTP_OPTIONS, handleOptions);
  server.on("/speed", HTTP_OPTIONS, handleOptions);
  server.on("/angle", HTTP_OPTIONS, handleOptions);
  server.on("/telemetry", HTTP_OPTIONS, handleOptions);
  server.on("/emergency", HTTP_OPTIONS, handleOptions);
  server.on("/reset", HTTP_OPTIONS, handleOptions);
  server.on("/ping", HTTP_OPTIONS, handleOptions);

  server.begin();
}

// ================= LOOP =================
void loop() {
  processArduinoSerial();
  server.handleClient();

  unsigned long now = millis();

  if (now - lastUltrasonicRead >= ULTRASONIC_INTERVAL) {
    lastUltrasonicRead = now;
    readSensors();
  }

  if (now - lastMq3Read >= MQ3_INTERVAL) {
    lastMq3Read = now;
    readMQ3Sensor();
  }

  if (now - lastDhtRead >= DHT_INTERVAL) {
    lastDhtRead = now;
    readDHTSensor();
  }

  if (now - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
    lastWifiCheck = now;

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Reconnecting WiFi...");
      WiFi.reconnect();
    }
  }

  processArduinoSerial();
  delay(2);
}
