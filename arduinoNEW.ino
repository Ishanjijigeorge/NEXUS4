// ==================================================
// SMART WHEELCHAIR – FINAL (BRAKE + SPEED + OLED FIX)
// + EEG BLINK CONTROL FOR SELF DRIVE / LINE FOLLOWING
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

int MOTOR_SPEED = 20;

// =============== PARAMETERS ================
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

// SPEED
float currentSpeed = 0;
float targetSpeed = 0;
#define MAX_SPEED 1.5
#define ACCEL_RATE 0.05

// MOTOR STATE
bool motorRunning = false;
bool motorA_forward = false;
bool motorA_backward = false;
bool motorB_forward = false;
bool motorB_backward = false;

// ================= EEG BLINK CONTROL =================
const int eegPin = A0;
int threshold = 460;

const unsigned long refractoryPeriod = 150;
const unsigned long waitNextBlink = 450;

unsigned long lastBlinkTime = 0;
unsigned long lastDetectedBlink = 0;

int blinkCount = 0;

bool eyeClosed = false;
bool readyPrinted = false;
bool eegControlEnabled = false;
String currentDriveMode = "Line Following";

// ================= BRAKING =================

void brakeMotorA() {

  int brakePower = constrain(MOTOR_SPEED * 3, 180, 255);

  if (motorA_forward) {
    digitalWrite(A1, HIGH); digitalWrite(A2, LOW);
  } else if (motorA_backward) {
    digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
  } else return;

  analogWrite(PWM_A, brakePower);
  delay(60);

  analogWrite(PWM_A, brakePower / 2);
  delay(40);

  digitalWrite(A1, HIGH);
  digitalWrite(A2, HIGH);
  analogWrite(PWM_A, 0);
  delay(20);

  digitalWrite(A1, LOW);
  digitalWrite(A2, LOW);
  analogWrite(PWM_A, 0);

  motorA_forward = false;
  motorA_backward = false;
}

void brakeMotorB() {

  int brakePower = constrain(MOTOR_SPEED * 3, 180, 255);

  if (motorB_forward) {
    digitalWrite(B1, LOW); digitalWrite(B2, HIGH);
  } else if (motorB_backward) {
    digitalWrite(B1, HIGH); digitalWrite(B2, LOW);
  } else return;

  analogWrite(PWM_B, brakePower);
  delay(60);

  analogWrite(PWM_B, brakePower / 2);
  delay(40);

  digitalWrite(B1, HIGH);
  digitalWrite(B2, HIGH);
  analogWrite(PWM_B, 0);
  delay(20);

  digitalWrite(B1, LOW);
  digitalWrite(B2, LOW);
  analogWrite(PWM_B, 0);

  motorB_forward = false;
  motorB_backward = false;
}

void stopMotors() {

  brakeMotorA();
  brakeMotorB();

  motorRunning = false;
  targetSpeed = 0;

  Serial.println("STOP");
  Serial.println("DIR:stop");
}

void safeTransition() {

  if (motorRunning) {
    brakeMotorA();
    brakeMotorB();
    delay(50);
  }
}

// ================= MOVEMENT =================

void moveForward() {

  if (tiltStopActive) return;

  safeTransition();

  digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
  digitalWrite(B1, HIGH); digitalWrite(B2, LOW);

  analogWrite(PWM_A, MOTOR_SPEED);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_forward = true;
  motorB_forward = true;
  motorRunning = true;

  Serial.println("DIR:forward");
}

void moveBackward() {

  safeTransition();

  digitalWrite(A1, HIGH); digitalWrite(A2, LOW);
  digitalWrite(B1, LOW); digitalWrite(B2, HIGH);

  analogWrite(PWM_A, MOTOR_SPEED);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_backward = true;
  motorB_backward = true;
  motorRunning = true;

  Serial.println("DIR:back");
}

void turnLeft() {

  safeTransition();

  digitalWrite(B1, LOW); digitalWrite(B2, LOW);
  analogWrite(PWM_B, 0);

  digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
  analogWrite(PWM_A, MOTOR_SPEED);

  motorA_forward = true;
  motorRunning = true;

  Serial.println("DIR:left");
}

void turnRight() {

  safeTransition();

  digitalWrite(A1, LOW); digitalWrite(A2, LOW);
  analogWrite(PWM_A, 0);

  digitalWrite(B1, HIGH); digitalWrite(B2, LOW);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorB_forward = true;
  motorRunning = true;

  Serial.println("DIR:right");
}

// ================= SPEED =================

void updateSpeed() {

  if (motorRunning)
    targetSpeed = (MOTOR_SPEED / 255.0) * MAX_SPEED;
  else
    targetSpeed = 0;

  currentSpeed += (targetSpeed - currentSpeed) * ACCEL_RATE;

  if (targetSpeed == 0 && currentSpeed < 0.02)
    currentSpeed = 0;
}

// ================= EEG CONTROL =================

void handleEEGControl() {

  if (!eegControlEnabled) return;

  if (!readyPrinted) {
    Serial.println("READY");
    readyPrinted = true;
  }

  int signal = analogRead(eegPin);
  unsigned long now = millis();

  if (signal > threshold && !eyeClosed) {
    eyeClosed = true;
  }

  if (signal < threshold && eyeClosed && (now - lastBlinkTime > refractoryPeriod)) {

    eyeClosed = false;
    lastBlinkTime = now;
    lastDetectedBlink = now;

    blinkCount++;

    Serial.print("Blink Count: ");
    Serial.println(blinkCount);
  }

  if (blinkCount > 0 && (now - lastDetectedBlink > waitNextBlink)) {

    if (blinkCount == 1) stopMotors();
    else if (blinkCount == 2) moveForward();
    else if (blinkCount == 3) turnLeft();
    else if (blinkCount == 4) turnRight();
    else if (blinkCount > 4) stopMotors();

    blinkCount = 0;
  }
}

// ================= SERIAL =================

void handleWebSerial() {

  if (!Serial.available()) return;

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input.startsWith("CMD:")) {

    String cmd = input.substring(4);

    if (cmd == "forward") moveForward();
    else if (cmd == "back") moveBackward();
    else if (cmd == "left") turnLeft();
    else if (cmd == "right") turnRight();
    else if (cmd == "stop") stopMotors();
    else if (cmd.startsWith("SPEED:")) {

      int percent = cmd.substring(6).toInt();
      MOTOR_SPEED = map(percent, 0, 100, 0, 255);
    }

    return;
  }

  if (input.startsWith("MODE:")) {

    currentDriveMode = input.substring(5);

    if (currentDriveMode == "Self Drive" || currentDriveMode == "Line Following") {
      eegControlEnabled = true;
      readyPrinted = false;
    } else {
      eegControlEnabled = false;
      blinkCount = 0;
      eyeClosed = false;
    }

    Serial.print("MODE:");
    Serial.println(currentDriveMode);
  }
}

// ================= GYRO =================

float readGyro() {

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x43);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 2);

  if (Wire.available() >= 2) {

    int16_t g = Wire.read() << 8 | Wire.read();
    return g / 131.0;
  }

  return 0;
}

// ================= OLED =================

void updateOLED() {

  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(10, 0);
  display.print(angleX, 1);
  display.print((char)247);

  display.setTextSize(1);

  display.setCursor(0, 22);
  display.print(tiltStopActive ? "TILT STOP!" : "ACTIVE");

  display.setCursor(0, 35);
  display.print("SPD:");
  display.print(currentSpeed, 2);
  display.print(" m/s");

  display.setCursor(0, 50);
  display.print(motorRunning ? "M:ON" : "M:OFF");

  display.display();
}

// ================= SETUP =================

void setup() {

  Serial.begin(115200);

  pinMode(PWM_A, OUTPUT);
  pinMode(PWM_B, OUTPUT);
  pinMode(A1, OUTPUT);
  pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT);
  pinMode(B2, OUTPUT);

  Wire.begin();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);

  float sum = 0;

  for (int i = 0; i < 100; i++) {
    sum += readGyro();
    delay(1);
  }

  gyroOffset = sum / 100.0;

  lastTime = micros();

  Serial.println("SYSTEM_READY");
}

// ================= LOOP =================

void loop() {

  unsigned long now_u = micros();
  unsigned long now_m = millis();

  handleWebSerial();
  handleEEGControl();

  dt = (now_u - lastTime) / 1000000.0;
  if (dt > 0.02) dt = 0.01;
  lastTime = now_u;

  float gyro = readGyro() - gyroOffset;

  if (abs(gyro) < 0.5) gyro = 0;

  angleX += gyro * dt;

  if (gyro == 0) {
    angleX *= 0.98;
    if (abs(angleX) < 0.2) angleX = 0;
  }

  angleX = constrain(angleX, -45, 45);

  if (abs(angleX) > TILT_STOP_THRESHOLD) {

    if (motorRunning) stopMotors();
    tiltStopActive = true;
  }
  else if (abs(angleX) < (TILT_STOP_THRESHOLD - TILT_HYSTERESIS)) {

    tiltStopActive = false;
  }

  updateSpeed();

  if (now_m - lastTelemetry > 100) {

    lastTelemetry = now_m;

    Serial.print("ANGLE:");
    Serial.println(angleX);

    Serial.print("SPEED:");
    Serial.println(currentSpeed);
  }

  if (now_m - lastDisplay > 100) {

    lastDisplay = now_m;
    updateOLED();
  }

  delay(1);
} 
