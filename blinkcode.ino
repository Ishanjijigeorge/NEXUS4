const int eegPin = A0;

int threshold = 460;

const unsigned long refractoryPeriod = 150;
const unsigned long waitNextBlink = 450;

unsigned long lastBlinkTime = 0;
unsigned long lastDetectedBlink = 0;

int blinkCount = 0;

bool eyeClosed = false;
bool readyPrinted = false;

void setup() {
  Serial.begin(115200);
}

void loop() {

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

    if (blinkCount == 1) Serial.println("STOP");
    else if (blinkCount == 2) Serial.println("FORWARD");
    else if (blinkCount == 3) Serial.println("LEFT");
    else if (blinkCount == 4) Serial.println("RIGHT");
    else if (blinkCount > 4) Serial.println("STOP");

    blinkCount = 0;
  }
}
