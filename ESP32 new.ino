// ==================================================
// ESP32 WIFI BRIDGE – FINAL (ANGLE + SPEED + SAFETY)
// + DHT11 + MQ3 SENSOR SUPPORT
// ==================================================

#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <DHT.h>

// WIFI
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

// ULTRASONIC PINS
#define TRIG_FL 12
#define ECHO_FL 13

#define TRIG_FR 18
#define ECHO_FR 19

#define TRIG_BL 23
#define ECHO_BL 25

#define TRIG_BR 14
#define ECHO_BR 15

#define OBSTACLE_MIN 20
#define OBSTACLE_MAX 30

// STATE
float currentAngle = 0;
float currentSpeed = 0;

float distFrontLeft  = 0;
float distFrontRight = 0;
float distBackLeft   = 0;
float distBackRight  = 0;

String movementDirection = "stop";

bool emergencyActive = false;
bool obstacleActive = false;

unsigned long lastUpdate = 0;
unsigned long lastSensorRead = 0;

// ================= CORS =================
void addCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ================= SEND TO ARDUINO =================
void sendToArduino(String cmd) {

  arduinoSerial.println(cmd);

  Serial.print("→ ");
  Serial.println(cmd);

  if (cmd == "CMD:forward") movementDirection = "forward";
  else if (cmd == "CMD:back") movementDirection = "back";
  else if (cmd == "CMD:left") movementDirection = "left";
  else if (cmd == "CMD:right") movementDirection = "right";
  else if (cmd == "CMD:stop") movementDirection = "stop";
}

// ================= ULTRASONIC =================
float readUltrasonic(int trigPin,int echoPin){

  digitalWrite(trigPin,LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin,HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin,LOW);

  long duration = pulseIn(echoPin,HIGH,12000);

  if(duration==0) return -1;

  return duration * 0.034 / 2;
}

// ================= OBSTACLE CHECK =================
void checkObstacle(){

  bool frontLeftObs  = distFrontLeft  >0 && distFrontLeft  >=OBSTACLE_MIN && distFrontLeft  <=OBSTACLE_MAX;
  bool frontRightObs = distFrontRight >0 && distFrontRight >=OBSTACLE_MIN && distFrontRight <=OBSTACLE_MAX;
  bool backLeftObs   = distBackLeft   >0 && distBackLeft   >=OBSTACLE_MIN && distBackLeft   <=OBSTACLE_MAX;
  bool backRightObs  = distBackRight  >0 && distBackRight  >=OBSTACLE_MIN && distBackRight  <=OBSTACLE_MAX;

  bool obstacle=false;

  if(movementDirection=="forward")
    obstacle = frontLeftObs || frontRightObs;

  else if(movementDirection=="back")
    obstacle = backLeftObs || backRightObs;

  else if(movementDirection=="right")
    obstacle = frontLeftObs || backLeftObs;

  else if(movementDirection=="left")
    obstacle = frontRightObs || backRightObs;

  else
    obstacle = frontLeftObs || frontRightObs || backLeftObs || backRightObs;

  if(obstacle && !obstacleActive){

    Serial.println("OBSTACLE STOP");

    arduinoSerial.println("CMD:stop");

    movementDirection="stop";
    obstacleActive=true;
  }

  if(!obstacle)
    obstacleActive=false;
}

// ================= SENSOR READ =================
void readSensors(){

  distFrontLeft  = readUltrasonic(TRIG_FL,ECHO_FL);
  distFrontRight = readUltrasonic(TRIG_FR,ECHO_FR);
  distBackLeft   = readUltrasonic(TRIG_BL,ECHO_BL);
  distBackRight  = readUltrasonic(TRIG_BR,ECHO_BR);

  checkObstacle();
}

// ================= DHT11 + MQ3 =================
void readEnvSensors(){

  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if(!isnan(t)) temperatureC = t;
  if(!isnan(h)) humidity = h;

  mq3Value = analogRead(MQ3_PIN);
}

// ================= PARSE SERIAL =================
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
    movementDirection="stop";
  }

  else if (data == "SYSTEM_READY") {
    Serial.println("Arduino Ready");
  }
}

// ================= ROUTES =================

void handleRoot(){
  addCORS();
  server.send(200,"text/plain","ESP32 Bridge Running");
}

void handleCmd(){

  addCORS();

  if(!server.hasArg("data")){
    server.send(400,"text/plain","Missing data");
    return;
  }

  String cmd = server.arg("data");

  sendToArduino(cmd);

  server.send(200,"text/plain","OK");
}

void handleSpeed(){

  addCORS();

  if(!server.hasArg("speed")){
    server.send(400,"text/plain","Missing speed");
    return;
  }

  int percent = server.arg("speed").toInt();

  sendToArduino("CMD:SPEED:"+String(percent));

  server.send(200,"text/plain","SPEED_SET");
}

void handleAngle(){
  addCORS();
  server.send(200,"text/plain",String(currentAngle,1));
}

void handleTelemetry(){

  addCORS();

  StaticJsonDocument<256> doc;

  doc["angle"]=currentAngle;
  doc["speed"]=currentSpeed;
  doc["emergency"]=emergencyActive;

  doc["FL"]=distFrontLeft;
  doc["FR"]=distFrontRight;
  doc["BL"]=distBackLeft;
  doc["BR"]=distBackRight;

  // Added sensors
  doc["temp"]=temperatureC;
  doc["humidity"]=humidity;
  doc["mq3"]=mq3Value;

  doc["obstacle"]=obstacleActive;
  doc["direction"]=movementDirection;

  doc["last_update"]=millis()-lastUpdate;

  String res;
  serializeJson(doc,res);

  server.send(200,"application/json",res);
}

void handleEmergency(){

  addCORS();

  emergencyActive=true;

  sendToArduino("CMD:stop");

  server.send(200,"application/json",
  "{\"status\":\"EMERGENCY_TRIGGERED\"}");
}

void handleResetEmergency(){

  addCORS();

  emergencyActive=false;

  server.send(200,"application/json",
  "{\"status\":\"RESET\"}");
}

void handlePing(){
  addCORS();
  server.send(200,"text/plain","PONG");
}

// ================= SETUP =================
void setup(){

  Serial.begin(115200);
  arduinoSerial.begin(115200,SERIAL_8N1,16,17);

  // Initialize sensors
  dht.begin();
  pinMode(MQ3_PIN,INPUT);

  pinMode(TRIG_FL,OUTPUT);
  pinMode(ECHO_FL,INPUT);

  pinMode(TRIG_FR,OUTPUT);
  pinMode(ECHO_FR,INPUT);

  pinMode(TRIG_BL,OUTPUT);
  pinMode(ECHO_BL,INPUT);

  pinMode(TRIG_BR,OUTPUT);
  pinMode(ECHO_BR,INPUT);

  Serial.println("\nESP32 BRIDGE STARTING...\n");

  WiFi.begin(ssid,password);

  while(WiFi.status()!=WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nCONNECTED");
  Serial.println(WiFi.localIP());

  server.on("/",handleRoot);
  server.on("/cmd",handleCmd);
  server.on("/speed",handleSpeed);
  server.on("/angle",handleAngle);
  server.on("/telemetry",handleTelemetry);
  server.on("/emergency",handleEmergency);
  server.on("/reset",handleResetEmergency);
  server.on("/ping",handlePing);

  server.begin();
}

// ================= LOOP =================
void loop(){

  server.handleClient();

  while(arduinoSerial.available()){
    String data = arduinoSerial.readStringUntil('\n');
    parseArduino(data);
  }

  if(millis()-lastSensorRead>80){
    lastSensorRead=millis();
    readSensors();
    readEnvSensors();
  }

  static unsigned long lastCheck=0;

  if(millis()-lastCheck>5000){
    lastCheck=millis();

    if(WiFi.status()!=WL_CONNECTED){
      Serial.println("Reconnecting WiFi...");
      WiFi.reconnect();
    }
  }

  delay(5);
}
