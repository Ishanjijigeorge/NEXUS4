// ==================================================
// SMART WHEELCHAIR – FINAL (BRAKE + SPEED + OLED FIX)
// + EEG BLINK CONTROL FOR SELF DRIVE / LINE FOLLOWING
// + EMERGENCY BRAKE OVERRIDE FOR ALL MODES
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

// =============== OLED DEFINITIONS ================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =============== PARAMETERS ================
#define MPU_ADDR 0x68

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

// ================= EMERGENCY BRAKE =================
bool emergencyActive = false;
String lastCommandBeforeEmergency = "stop";
String lastDriveModeBeforeEmergency = "Line Following";

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

// OLED display buffer
String displayLine1 = "";
String displayLine2 = "";
String displayLine3 = "";
String displayLine4 = "";

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

// ================= EMERGENCY FUNCTIONS =================

void activateEmergency() {
  if (!emergencyActive) {
    emergencyActive = true;

    // Store current state before emergency
    if (motorRunning) {
      if (motorA_forward && motorB_forward) lastCommandBeforeEmergency = "forward";
      else if (motorA_backward && motorB_backward) lastCommandBeforeEmergency = "back";
      else if (motorA_forward && !motorB_forward) lastCommandBeforeEmergency = "left";
      else if (!motorA_forward && motorB_forward) lastCommandBeforeEmergency = "right";
      else lastCommandBeforeEmergency = "stop";
    } else {
      lastCommandBeforeEmergency = "stop";
    }

    lastDriveModeBeforeEmergency = currentDriveMode;

    // IMMEDIATELY stop all motors
    stopMotors();

    Serial.println("EMERGENCY:ON");
    Serial.println("LOCKED");
  }
}

void releaseEmergency() {
  if (emergencyActive) {
    emergencyActive = false;

    // Stay stopped after release and wait for a fresh command.
    stopMotors();
    lastCommandBeforeEmergency = "stop";

    Serial.println("WAITING_FOR_NEW_COMMAND");
    Serial.println("EMERGENCY:OFF");
  }
}

bool isEmergencyLocked() {
  return emergencyActive;
}

// ================= MOVEMENT =================

void moveForward() {
  if (emergencyActive) {
    Serial.println("LOCKED - Emergency active");
    return;
  }

  if (tiltStopActive) return;

  safeTransition();

  digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
  digitalWrite(B1, HIGH); digitalWrite(B2, LOW);

  analogWrite(PWM_A, MOTOR_SPEED);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_forward = true;
  motorA_backward = false;
  motorB_forward = true;
  motorB_backward = false;
  motorRunning = true;

  Serial.println("DIR:forward");
}

void moveBackward() {
  if (emergencyActive) {
    Serial.println("LOCKED - Emergency active");
    return;
  }

  safeTransition();

  digitalWrite(A1, HIGH); digitalWrite(A2, LOW);
  digitalWrite(B1, LOW); digitalWrite(B2, HIGH);

  analogWrite(PWM_A, MOTOR_SPEED);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_forward = false;
  motorA_backward = true;
  motorB_forward = false;
  motorB_backward = true;
  motorRunning = true;

  Serial.println("DIR:back");
}

void turnLeft() {
  if (emergencyActive) {
    Serial.println("LOCKED - Emergency active");
    return;
  }

  safeTransition();

  digitalWrite(B1, LOW); digitalWrite(B2, LOW);
  analogWrite(PWM_B, 0);

  digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
  analogWrite(PWM_A, MOTOR_SPEED);

  motorA_forward = true;
  motorA_backward = false;
  motorB_forward = false;
  motorB_backward = false;
  motorRunning = true;

  Serial.println("DIR:left");
}

void turnRight() {
  if (emergencyActive) {
    Serial.println("LOCKED - Emergency active");
    return;
  }

  safeTransition();

  digitalWrite(A1, LOW); digitalWrite(A2, LOW);
  analogWrite(PWM_A, 0);

  digitalWrite(B1, HIGH); digitalWrite(B2, LOW);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_forward = false;
  motorA_backward = false;
  motorB_forward = true;
  motorB_backward = false;
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
  // Block EEG control during emergency
  if (emergencyActive) return;

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

    // Handle emergency commands with highest priority
    if (cmd == "EMERGENCY_ON") {
      activateEmergency();
      return;
    }
    else if (cmd == "EMERGENCY_OFF") {
      releaseEmergency();
      return;
    }

    // Block all movement commands during emergency
    if (emergencyActive) {
      Serial.println("LOCKED");
      return;
    }

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

bool initOLED() {
  // Initialize OLED with I2C address 0x3C
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED initialization failed!");
    return false;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Wheelchair v2.0");
  display.println("System Starting...");
  display.display();
  delay(1000);

  Serial.println("OLED initialized successfully");
  return true;
}

void updateOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    // Try to re-initialize if connection lost
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Line 1: Emergency status or Angle
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (emergencyActive) {
    display.println("[EMERGENCY LOCKED]");
    display.setTextSize(2);
    display.setCursor(0, 16);
    display.println("STOP");
  } else {
    display.print("Angle: ");
    display.print(angleX, 1);
    display.println((char)247);
  }

  // Line 2: Tilt status or Speed
  display.setTextSize(1);
  display.setCursor(0, 24);
  if (tiltStopActive && !emergencyActive) {
    display.println("TILT STOP ACTIVE");
  } else if (!emergencyActive) {
    display.print("Speed: ");
    display.print(currentSpeed, 1);
    display.println(" m/s");
  } else {
    display.println("Press Release");
  }

  // Line 3: Motor status
  display.setCursor(0, 40);
  if (motorRunning && !emergencyActive) {
    display.print("Motor: RUNNING");
  } else if (emergencyActive) {
    display.print("EMERGENCY MODE");
  } else {
    display.print("Motor: STOPPED");
  }

  // Line 4: Mode
  display.setCursor(0, 54);
  display.print("Mode: ");
  display.print(currentDriveMode.substring(0, 12));

  display.display();
}

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== WHEELCHAIR SYSTEM STARTING ===");

  // Initialize motor pins
  pinMode(PWM_A, OUTPUT);
  pinMode(PWM_B, OUTPUT);
  pinMode(A1, OUTPUT);
  pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT);
  pinMode(B2, OUTPUT);

  // Initialize all motor control pins to LOW
  digitalWrite(A1, LOW);
  digitalWrite(A2, LOW);
  digitalWrite(B1, LOW);
  digitalWrite(B2, LOW);
  analogWrite(PWM_A, 0);
  analogWrite(PWM_B, 0);

  // Initialize I2C
  Wire.begin();
  Wire.setClock(100000); // Use 100kHz for better compatibility

  // Initialize OLED
  initOLED();

  // Initialize MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();

  // Calibrate gyro
  Serial.println("Calibrating gyro...");
  float sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += readGyro();
    delay(2);
  }
  gyroOffset = sum / 100.0;
  Serial.print("Gyro offset: ");
  Serial.println(gyroOffset);

  lastTime = micros();

  // Display ready message on OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("System Ready!");
  display.println("Mode: Line Following");
  display.display();

  Serial.println("SYSTEM_READY");
  delay(500);
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

  // Tilt stop should NOT override emergency - emergency has priority
  if (!emergencyActive) {
    if (abs(angleX) > TILT_STOP_THRESHOLD) {
      if (motorRunning) stopMotors();
      tiltStopActive = true;
    }
    else if (abs(angleX) < (TILT_STOP_THRESHOLD - TILT_HYSTERESIS)) {
      tiltStopActive = false;
    }
  } else {
    // During emergency, tilt stop is ignored
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

  if (now_m - lastDisplay > 200) { // Update OLED every 200ms
    lastDisplay = now_m;
    updateOLED();
  }

  delay(5);
}
