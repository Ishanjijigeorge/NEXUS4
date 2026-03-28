// ================================================== 
// SMART WHEELCHAIR – FINAL INTEGRATED VERSION
// ================================================== 

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =============== PIN DEFINITIONS ================
#define PWM_A 9
#define PWM_B 10
#define A1 7
#define A2 8
#define B1 11
#define B2 12
#define EXG_PIN A0

#define MOTOR_SPEED 150

// =============== PARAMETERS ================
#define BLINK_THRESHOLD 28      
#define DOUBLE_BLINK_WINDOW 300 // Time to wait for next blink (ms)
#define MIN_BLINK_DURATION 30   // Noise filter

#define MPU_ADDR 0x68
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =============== VARIABLES ================
float angleX = 0;
float gyroOffset = 0;
unsigned long lastTime;
unsigned long lastDisplay = 0;
unsigned long lastTelemetry = 0;
float dt = 0.01;

#define TILT_STOP_THRESHOLD 18.0
#define TILT_HYSTERESIS 2.0
bool tiltStopActive = false;

#define RETURN_SPEED 0.5         
#define DEAD_ZONE 0.3            

bool motorRunning = false;
bool inBlink = false;
unsigned long blinkStartTime = 0;
unsigned long lastBlinkEndTime = 0;
int blinkCount = 0;
float baseline = 0;

// =============== MOTOR CONTROL ================
void stopMotors() {
  analogWrite(PWM_A, 0);
  analogWrite(PWM_B, 0);
  motorRunning = false;
  Serial.println("STOP");
}

void setMotors(int speedA, int speedB) {
  if (!tiltStopActive) {
    digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
    digitalWrite(B1, HIGH); digitalWrite(B2, LOW);
    analogWrite(PWM_A, speedA);
    analogWrite(PWM_B, speedB);
    motorRunning = (speedA > 0 || speedB > 0);
  }
}

// =============== WEB COMMAND PARSER ================
void handleWebSerial() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input == "PING") {
      Serial.println("PONG");
    } 
    else if (input == "EMERGENCY") {
      tiltStopActive = true;
      stopMotors();
      Serial.println("TILT STOP: 99.0"); // Triggers Web Alert
    }
    else if (input.startsWith("CMD:")) {
      String cmd = input.substring(4);
      if (cmd == "forward") { setMotors(MOTOR_SPEED, MOTOR_SPEED); Serial.println("FWD"); }
      else if (cmd == "left")    { setMotors(0, MOTOR_SPEED); Serial.println("LEFT"); }
      else if (cmd == "right")   { setMotors(MOTOR_SPEED, 0); Serial.println("RIGHT"); }
      else if (cmd == "stop")    { stopMotors(); }
      else if (cmd == "back") {
         digitalWrite(A1, HIGH); digitalWrite(A2, LOW);
         digitalWrite(B1, LOW); digitalWrite(B2, HIGH);
         analogWrite(PWM_A, MOTOR_SPEED);
         analogWrite(PWM_B, MOTOR_SPEED);
         Serial.println("REV");
      }
    }
  }
}

// =============== SENSORS ================
float readGyro() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x43);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 2);
  if(Wire.available() >= 2) {
    int16_t g = Wire.read() << 8 | Wire.read();
    return g / 131.0;
  }
  return 0;
}

// =============== OLED DASHBOARD ================
void updateOLED() {
  display.clearDisplay();
  
  // 1. Angle Value
  display.setTextSize(2);
  display.setCursor(20, 0);
  char angleStr[10];
  dtostrf(angleX, 5, 1, angleStr);
  if(angleX >= 0) display.print(" ");
  display.print(angleStr);
  display.print((char)247);

  // 2. Status Text
  display.setTextSize(1);
  display.setCursor(0, 20);
  if (tiltStopActive) display.print("TILT STOP!");
  else if (angleX > DEAD_ZONE) display.print("LEFT <--");
  else if (angleX < -DEAD_ZONE) display.print("RIGHT -->");
  else display.print("STRAIGHT");

  // 3. Visual Bar
  display.drawLine(0, 50, 128, 50, WHITE);
  int dotPos = map(angleX, -45, 45, 20, 108);
  display.fillCircle(constrain(dotPos, 20, 108), 57, 3, WHITE);

  // 4. Footer
  display.setCursor(0, 57);
  display.print(motorRunning ? "M:ON " : "M:OFF");
  display.setCursor(85, 57);
  display.print("WEB:OK");

  display.display();
}

// =============== SETUP ================
void setup() {
  Serial.begin(115200);
  
  pinMode(PWM_A, OUTPUT); pinMode(PWM_B, OUTPUT);
  pinMode(A1, OUTPUT); pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT); pinMode(B2, OUTPUT);

  Wire.begin();
  Wire.setClock(400000);
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
  Wire.endTransmission();
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { for(;;); }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("Initializing...");
  display.display();

  // Fast Gyro Calib
  float sum = 0;
  for (int i = 0; i < 100; i++) { sum += readGyro(); delay(1); }
  gyroOffset = sum / 100.0;

  // EXG Baseline Calib
  long eyeSum = 0;
  for (int i = 0; i < 30; i++) { eyeSum += analogRead(EXG_PIN); delay(1); }
  baseline = eyeSum / 30.0;

  lastTime = micros();
  Serial.println("SYSTEM_READY");
}

// =============== MAIN LOOP ================
void loop() {
  unsigned long now_u = micros();
  unsigned long now_m = millis();

  // Listen for Web Commands
  handleWebSerial();

  // Part 1: Angle Integration (Fast Response)
  dt = (now_u - lastTime) / 1000000.0;
  if(dt > 0.02) dt = 0.01;
  lastTime = now_u;

  float gyro = readGyro() - gyroOffset;
  if(abs(gyro) < 0.2) gyro = 0;
  angleX += gyro * dt;
  
  if(abs(gyro) < 0.3) {
    if(abs(angleX) > 0.5) angleX *= RETURN_SPEED;
    else angleX = 0;
  }
  angleX = constrain(angleX, -45, 45);

  // Part 2: Tilt Safety Logic
  if (abs(angleX) > TILT_STOP_THRESHOLD) {
    if (motorRunning) stopMotors();
    tiltStopActive = true;
    // Notify web immediately on tilt
    static unsigned long lastTiltMsg = 0;
    if (now_m - lastTiltMsg > 1000) {
       Serial.print("TILT STOP: "); Serial.println(angleX);
       lastTiltMsg = now_m;
    }
  } else if (abs(angleX) < (TILT_STOP_THRESHOLD - TILT_HYSTERESIS)) {
    if (tiltStopActive) Serial.println("TILT CLEARED");
    tiltStopActive = false;
  }

  // Part 3: Web Telemetry (Updates Tilt Angle in Web UI)
  if (now_m - lastTelemetry > 500) {
    Serial.print("ANGLE:"); Serial.println(angleX);
    lastTelemetry = now_m;
  }

  // Part 4: Discrete Blink Logic (1=Stop, 2=Forward, 3=Left)
  int raw = analogRead(EXG_PIN);
  baseline = (0.95 * baseline) + (0.05 * raw);
  int diff = raw - (int)baseline;

  if (diff > BLINK_THRESHOLD) {
    if (!inBlink) {
      inBlink = true;
      blinkStartTime = now_u;
    }
  } else if (inBlink) {
    unsigned long duration = (now_u - blinkStartTime) / 1000;
    if (duration > MIN_BLINK_DURATION) {
      blinkCount++;
      lastBlinkEndTime = now_u;
    }
    inBlink = false;
  }

  // Check if user is done blinking (window closed)
  if (!inBlink && blinkCount > 0) {
    unsigned long timeSinceLast = (now_u - lastBlinkEndTime) / 1000;
    if (timeSinceLast > DOUBLE_BLINK_WINDOW) {
      if (!tiltStopActive) {
        if (blinkCount == 1) stopMotors();
        else if (blinkCount == 2) { setMotors(MOTOR_SPEED, MOTOR_SPEED); Serial.println("FWD"); }
        else if (blinkCount == 3) { setMotors(MOTOR_SPEED, 0); Serial.println("LEFT"); }
      }
      blinkCount = 0;
    }
  }

  // Part 5: Dashboard Refresh
  if (now_m - lastDisplay > 50) {
    lastDisplay = now_m;
    updateOLED();
  }

  delay(1); 
}
