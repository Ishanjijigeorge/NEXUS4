// ================================================== 
// SMART WHEELCHAIR – FINAL (FIXED ISOLATED BRAKING)
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

#define RETURN_SPEED 0.5
#define DEAD_ZONE 0.3

// 🔥 PER MOTOR STATE
bool motorA_forward = false;
bool motorA_backward = false;
bool motorB_forward = false;
bool motorB_backward = false;

bool motorRunning = false;

// =============== MOTOR CONTROL ================

// 🔥 INDIVIDUAL MOTOR BRAKE
void brakeMotorA() {
  int brakePower = constrain(MOTOR_SPEED * 2, 150, 255);

  if (motorA_forward) {
    digitalWrite(A1, HIGH); digitalWrite(A2, LOW);
    analogWrite(PWM_A, brakePower);
    delay(120);
  } 
  else if (motorA_backward) {
    digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
    analogWrite(PWM_A, brakePower);
    delay(120);
  }

  // float after brake
  digitalWrite(A1, LOW); digitalWrite(A2, LOW);
  analogWrite(PWM_A, 0);

  motorA_forward = false;
  motorA_backward = false;
}

void brakeMotorB() {
  int brakePower = constrain(MOTOR_SPEED * 2, 150, 255);

  if (motorB_forward) {
    digitalWrite(B1, LOW); digitalWrite(B2, HIGH);
    analogWrite(PWM_B, brakePower);
    delay(120);
  } 
  else if (motorB_backward) {
    digitalWrite(B1, HIGH); digitalWrite(B2, LOW);
    analogWrite(PWM_B, brakePower);
    delay(120);
  }

  digitalWrite(B1, LOW); digitalWrite(B2, LOW);
  analogWrite(PWM_B, 0);

  motorB_forward = false;
  motorB_backward = false;
}

void stopMotors() {

  // 🔥 brake each motor independently
  brakeMotorA();
  brakeMotorB();

  motorRunning = false;

  Serial.println("STOP");
}

// =============== DRIVE FUNCTIONS ================

void moveForward() {
  if (!tiltStopActive) {

    safeTransition();  // 🔥 ADD THIS

    digitalWrite(A1, LOW); digitalWrite(A2, HIGH);
    digitalWrite(B1, HIGH); digitalWrite(B2, LOW);

    analogWrite(PWM_A, MOTOR_SPEED);
    analogWrite(PWM_B, MOTOR_SPEED);

    motorA_forward = true;
    motorB_forward = true;
    motorA_backward = false;
    motorB_backward = false;

    motorRunning = true;
  }
}

void moveBackward() {

  safeTransition();  // 🔥 ADD THIS

  digitalWrite(A1, HIGH); digitalWrite(A2, LOW);
  digitalWrite(B1, LOW); digitalWrite(B2, HIGH);

  analogWrite(PWM_A, MOTOR_SPEED);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_backward = true;
  motorB_backward = true;
  motorA_forward = false;
  motorB_forward = false;

  motorRunning = true;
}

void turnLeft() {

  safeTransition();

  // 🔥 RUN MOTOR A instead of B
  digitalWrite(B1, LOW); 
  digitalWrite(B2, LOW);
  analogWrite(PWM_B, 0);

  digitalWrite(A1, LOW); 
  digitalWrite(A2, HIGH);
  analogWrite(PWM_A, MOTOR_SPEED);

  motorB_forward = false;
  motorB_backward = false;

  motorA_forward = true;
  motorA_backward = false;

  motorRunning = true;
}

void turnRight() {

  safeTransition();

  // 🔥 RUN MOTOR B instead of A
  digitalWrite(A1, LOW); 
  digitalWrite(A2, LOW);
  analogWrite(PWM_A, 0);

  digitalWrite(B1, HIGH); 
  digitalWrite(B2, LOW);
  analogWrite(PWM_B, MOTOR_SPEED);

  motorA_forward = false;
  motorA_backward = false;

  motorB_forward = true;
  motorB_backward = false;

  motorRunning = true;
}

void safeTransition() {
  if (motorRunning) {
    brakeMotorA();
    brakeMotorB();
    delay(50); // small settling time (important)
  }
}


// =============== WEB COMMAND ================
void handleWebSerial() {
  if (Serial.available() > 0) {
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
    }
  }
}

// =============== GYRO ================
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

// =============== OLED ================
void updateOLED() {
  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor(20, 0);
  display.print(angleX, 1);
  display.print((char)247);

  display.setTextSize(1);
  display.setCursor(0, 20);

  if (tiltStopActive) display.print("TILT STOP!");
  else display.print("ACTIVE");

  display.setCursor(0, 57);
  display.print(motorRunning ? "M:ON " : "M:OFF");

  display.display();
}

// =============== SETUP ================
void setup() {
  Serial.begin(115200);

  pinMode(PWM_A, OUTPUT); pinMode(PWM_B, OUTPUT);
  pinMode(A1, OUTPUT); pinMode(A2, OUTPUT);
  pinMode(B1, OUTPUT); pinMode(B2, OUTPUT);

  Wire.begin();

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
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
}

// =============== LOOP ================
void loop() {
  unsigned long now_u = micros();
  unsigned long now_m = millis();

  handleWebSerial();

  dt = (now_u - lastTime) / 1000000.0;
  if (dt > 0.02) dt = 0.01;
  lastTime = now_u;

 float gyro = readGyro() - gyroOffset;

// 🔥 DEAD ZONE (ignore tiny noise)
if (abs(gyro) < 0.5) gyro = 0;

// 🔥 INTEGRATE
angleX += gyro * dt;

// 🔥 AUTO RETURN TO ZERO (decay)
if (gyro == 0) {
  angleX *= 0.98;   // decay factor (tune 0.95–0.99)
  
  // snap to zero when very small
  if (abs(angleX) < 0.2) angleX = 0;
}

// 🔥 LIMIT
angleX = constrain(angleX, -45, 45);

  if (abs(angleX) > TILT_STOP_THRESHOLD) {
    if (motorRunning) stopMotors();
    tiltStopActive = true;
  } else if (abs(angleX) < (TILT_STOP_THRESHOLD - TILT_HYSTERESIS)) {
    tiltStopActive = false;
  }

  if (now_m - lastDisplay > 50) {
    lastDisplay = now_m;
    updateOLED();
  }

  delay(1);
}
