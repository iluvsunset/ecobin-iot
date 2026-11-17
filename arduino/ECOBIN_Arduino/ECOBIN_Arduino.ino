// ==============================================================================
// ECOBIN - SMART TRASH BIN (PHYSICAL HARDWARE VERSION)
// ==============================================================================

// --- 1. WI-FI CONFIGURATION (Edit this to choose your Wi-Fi SSID & Password) ---
char ssid[] = "THE INTERNATIONAL SCHOOL";
char pass[] = "tisschool";

// --- 2. BLYNK IOT CLOUD CONFIGURATION ---
#define BLYNK_TEMPLATE_ID   "TMPL6G83dz2ey"
#define BLYNK_TEMPLATE_NAME "ECOBIN"
#define BLYNK_AUTH_TOKEN    "zssqYonS9qCYu8d_6KIFS-n3OhSYTQSs"
#define BLYNK_PRINT Serial

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DFRobotDFPlayerMini.h"

// Match these lid/servo constants to the physical bin code before flashing real hardware.
#define LID_TRIG_PIN       5
#define LID_ECHO_PIN       18
#define SERVO_PIN          19
float lidOpenCm                  = 20.0f;
const int   LID_CLOSE_ANGLE      = 0;
int   lidOpenAngle               = 85;
const unsigned long LID_HOLD_MS      = 3000UL;
const int           LID_DWELL_READS  = 2;
const unsigned long LID_MAX_OPEN_MS  = 15000UL;

#define FILL_TRIG_PIN      26
#define FILL_ECHO_PIN      27
// Physical carton is 37 x 23 x 37 cm; V5 slider can override this live if a liner shifts the bottom.
float binHeightCm                = 37.0f;
const float FULL_THRESHOLD_PCT   = 90.0f;
const float FULL_RESET_PCT       = 80.0f;
#define BLYNK_EVENT_FULL   "trash_full"

#define SCREEN_WIDTH       128
#define SCREEN_HEIGHT      64
#define OLED_RESET         -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

HardwareSerial myFPSerial(2);
DFRobotDFPlayerMini myDFPlayer;
Servo myServo;
BlynkTimer timer;

bool isLidOpen               = false;
bool remoteLidOverride       = false;
bool oledReady               = false;
bool dfReady                 = false;
bool fullAlertSent           = false;
int  fullConfirmCount        = 0;
int  openCount               = 0;
int  lidDetectStreak         = 0;
bool lidLockedUntilClear     = false;
float lastFillPct            = 0.0f;
float lastLidDistCm          = -1.0f;
unsigned long lastHandSeenMs = 0;
unsigned long lidOpenedAtMs  = 0;

bool hasOledState            = false;
int  lastDispDistTenths      = -999;
int  lastDispFillPct         = -1;
int  lastDispOpenCount       = -1;
bool lastDispLidOpen         = false;
bool lastDispBlynk           = false;

void checkLidSensor();

BLYNK_WRITE(V2) {
  remoteLidOverride = (param.asInt() == 1);
  if (!remoteLidOverride) {
    lidOpenedAtMs = millis();
  }
  Serial.print("[Blynk V2] Remote lid override: ");
  Serial.println(remoteLidOverride ? "ON" : "OFF");
  checkLidSensor();
}

BLYNK_WRITE(V5) {
  float newHeight = param.asFloat();
  // Guard against uninitialized 0 from Blynk breaking fill percentage division.
  if (newHeight >= 10.0f && newHeight <= 120.0f) {
    binHeightCm = newHeight;
    Serial.print("[Blynk V5] Bin height calibrated to: ");
    Serial.print(binHeightCm, 1);
    Serial.println(" cm");
  }
}

BLYNK_WRITE(V6) {
  float newTrigger = param.asFloat();
  // Guard against uninitialized 0 from Blynk disabling lid detection.
  if (newTrigger >= 5.0f && newTrigger <= 150.0f) {
    lidOpenCm = newTrigger;
    Serial.print("[Blynk V6] Lid trigger distance set to: ");
    Serial.print(lidOpenCm, 1);
    Serial.println(" cm");
  }
}

BLYNK_WRITE(V7) {
  int newAngle = param.asInt();
  // Guard against uninitialized 0 from Blynk disabling lid movement.
  if (newAngle >= 15 && newAngle <= 180) {
    lidOpenAngle = newAngle;
    if (isLidOpen) {
      myServo.write(lidOpenAngle);
    }
    Serial.print("[Blynk V7] Servo open angle set to: ");
    Serial.print(lidOpenAngle);
    Serial.println(" deg");
  }
}

BLYNK_CONNECTED() {
  Blynk.syncVirtual(V2, V5, V6, V7);
  Blynk.virtualWrite(V3, openCount);
  Blynk.virtualWrite(V4, isLidOpen ? 1 : 0);
}

float readUltrasonicCm(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // 30ms timeout prevents pulseIn from stalling Blynk.run() if an echo is lost.
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) {
    return -1.0f;
  }
  return duration * 0.034f / 2.0f;
}

float readMedianUltrasonicCm(uint8_t trigPin, uint8_t echoPin) {
  float samples[3];
  int validCount = 0;

  for (int i = 0; i < 3; i++) {
    float d = readUltrasonicCm(trigPin, echoPin);
    if (d > 0.0f) {
      samples[validCount++] = d;
    }
    if (i < 2) {
      delay(15);
    }
  }

  if (validCount == 0) return -1.0f;
  if (validCount == 1) return samples[0];
  if (validCount == 2) return (samples[0] + samples[1]) * 0.5f;

  if (samples[0] > samples[1]) { float t = samples[0]; samples[0] = samples[1]; samples[1] = t; }
  if (samples[1] > samples[2]) { float t = samples[1]; samples[1] = samples[2]; samples[2] = t; }
  if (samples[0] > samples[1]) { float t = samples[0]; samples[0] = samples[1]; samples[1] = t; }
  return samples[1];
}

void updateOLED(float lidDistance) {
  if (!oledReady) return;

  int distTenths = (lidDistance < 0.0f || lidDistance > 300.0f) ? -1 : (int)roundf(lidDistance * 10.0f);
  int fillInt = constrain((int)roundf(lastFillPct), 0, 100);
  bool blynkUp = Blynk.connected();

  if (hasOledState &&
      distTenths == lastDispDistTenths &&
      fillInt == lastDispFillPct &&
      openCount == lastDispOpenCount &&
      isLidOpen == lastDispLidOpen &&
      blynkUp == lastDispBlynk) {
    return;
  }

  hasOledState       = true;
  lastDispDistTenths = distTenths;
  lastDispFillPct    = fillInt;
  lastDispOpenCount  = openCount;
  lastDispLidOpen    = isLidOpen;
  lastDispBlynk      = blynkUp;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("ECOBIN");
  display.setCursor(78, 0);
  if (fillInt >= (int)FULL_THRESHOLD_PCT) {
    display.print("[FULL!]");
  } else if (blynkUp) {
    display.print("  [IoT]");
  } else {
    display.print("[LOCAL]");
  }

  display.setCursor(0, 12);
  display.print("Vat can: ");
  if (distTenths < 0) {
    display.println("---");
  } else {
    display.print(distTenths / 10.0f, 1);
    display.println(" cm");
  }

  display.setCursor(0, 22);
  display.print("Do day:  ");
  display.print(fillInt);
  display.print("%  #");
  display.println(openCount);

  int barWidth = map(fillInt, 0, 100, 0, 124);
  display.drawRect(0, 33, 128, 8, SSD1306_WHITE);
  if (barWidth > 0) {
    display.fillRect(2, 35, barWidth, 4, SSD1306_WHITE);
  }

  display.setTextSize(2);
  display.setCursor(0, 46);
  display.println(isLidOpen ? "MO CUA" : "DONG CUA");

  display.display();
}

void setLidState(bool open) {
  if (open == isLidOpen) return;

  isLidOpen = open;
  if (open) {
    lidOpenedAtMs = millis();
    myServo.write(lidOpenAngle);
    openCount++;
    Serial.print("[LID] MO CUA (");
    Serial.print(lidOpenAngle);
    Serial.print(" deg) | Count: ");
    Serial.println(openCount);
    if (dfReady) {
      myDFPlayer.playFolder(1, 1);
    }
  } else {
    myServo.write(LID_CLOSE_ANGLE);
    Serial.println("[LID] DONG CUA (0 deg)");
  }

  if (Blynk.connected()) {
    Blynk.virtualWrite(V4, isLidOpen ? 1 : 0);
    if (open) {
      Blynk.virtualWrite(V3, openCount);
    }
  }
}

void checkLidSensor() {
  float distance = readUltrasonicCm(LID_TRIG_PIN, LID_ECHO_PIN);
  lastLidDistCm = distance;
  unsigned long now = millis();
  bool inRange = (distance > 0.0f && distance <= lidOpenCm);

  if (!inRange) {
    lidDetectStreak = 0;
    lidLockedUntilClear = false;
  } else if (!lidLockedUntilClear) {
    lidDetectStreak++;
    if (lidDetectStreak >= LID_DWELL_READS) {
      lastHandSeenMs = now;
    }
  }

  // Force-close if blocked for 15s and stay locked until the path clears.
  if (isLidOpen && !remoteLidOverride && (now - lidOpenedAtMs >= LID_MAX_OPEN_MS)) {
    lidLockedUntilClear = true;
    lidDetectStreak = 0;
    Serial.println("[LID] 15s max-open timeout - closing until path clears");
  }

  bool confirmedHand = !lidLockedUntilClear && (lidDetectStreak >= LID_DWELL_READS);
  bool holdActive = isLidOpen && !lidLockedUntilClear && (now - lastHandSeenMs < LID_HOLD_MS);
  bool shouldOpen = remoteLidOverride || confirmedHand || holdActive;

  setLidState(shouldOpen);
  updateOLED(distance);
}

void sendFillLevelToBlynk() {
  if (isLidOpen) {
    Serial.println("[IoT] Lid open - skipping fill read");
    return;
  }

  float distance = readMedianUltrasonicCm(FILL_TRIG_PIN, FILL_ECHO_PIN);
  if (distance < 0.0f) {
    Serial.println("[SR04 #2] Timeout / khong nhan duoc tin hieu");
    return;
  }

  float fillPercent = ((binHeightCm - distance) / binHeightCm) * 100.0f;
  fillPercent = constrain(fillPercent, 0.0f, 100.0f);
  lastFillPct = fillPercent;

  if (fillPercent >= FULL_THRESHOLD_PCT) {
    fullConfirmCount++;
  } else {
    fullConfirmCount = 0;
  }

  if (Blynk.connected()) {
    Blynk.virtualWrite(V1, fillPercent);

    if (fullConfirmCount >= 2 && !fullAlertSent) {
      String msg = "EcoBin is full (" + String(fillPercent, 0) + "%)! Please empty the bin.";
      Blynk.logEvent(BLYNK_EVENT_FULL, msg);
      fullAlertSent = true;
      Serial.println("[Blynk EVENT] Sent 'trash_full' notification to phone!");
    } else if (fillPercent < FULL_RESET_PCT && fullAlertSent) {
      Blynk.resolveEvent(BLYNK_EVENT_FULL);
      fullAlertSent = false;
      Serial.println("[Blynk EVENT] Trash emptied - reset 'trash_full' alert");
    }
  }

  updateOLED(lastLidDistCm);

  Serial.print("[IoT] Cao: ");
  Serial.print(binHeightCm, 0);
  Serial.print("cm | Khoang cach rac: ");
  Serial.print(distance, 1);
  Serial.print(" cm | Do day: ");
  Serial.print(fillPercent, 1);
  Serial.print(" % | Blynk: ");
  Serial.println(Blynk.connected() ? "ONLINE" : "OFFLINE");
}

void checkBlynkConnection() {
  if (WiFi.status() == WL_CONNECTED && !Blynk.connected()) {
    Blynk.connect(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== ECOBIN SYSTEM STARTING ===");

  pinMode(LID_TRIG_PIN, OUTPUT);
  digitalWrite(LID_TRIG_PIN, LOW);
  pinMode(LID_ECHO_PIN, INPUT);
  pinMode(FILL_TRIG_PIN, OUTPUT);
  digitalWrite(FILL_TRIG_PIN, LOW);
  pinMode(FILL_ECHO_PIN, INPUT);

  myServo.attach(SERVO_PIN);
  myServo.write(LID_CLOSE_ANGLE);

  Wire.begin(21, 22);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledReady = true;
    Serial.println("[OLED] Ready (0x3C)");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println("ECOBIN KHOI DONG...");
    display.display();
  } else {
    Serial.println("Loi: Khong tim thay OLED 0x3C!");
  }

  // Khoi tao DFPlayer Mini tren HardwareSerial 2 (RX2 = GPIO 16, TX2 = GPIO 17)
  myFPSerial.begin(9600, SERIAL_8N1, 16, 17);
  if (myDFPlayer.begin(myFPSerial, true, true)) {
    dfReady = true;
    myDFPlayer.volume(25);
    Serial.println("DFPlayer da san sang!");
  } else {
    Serial.println("Canh bao: Khong tim thay DFPlayer!");
  }

  Serial.print("Connecting WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 8000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected! Connecting Blynk...");
    Blynk.connect(5000);
  } else {
    Serial.println("WiFi timeout - running in offline mode");
  }

  timer.setInterval(200L, checkLidSensor);
  timer.setInterval(2000L, sendFillLevelToBlynk);
  timer.setInterval(10000L, checkBlynkConnection);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  timer.run();
}
