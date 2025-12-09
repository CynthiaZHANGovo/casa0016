// ============================================================================
//  Focus/Rest System (Full Version)
//  Added feature: After reaching required focus time, system enters a RED
//  warning state and only switches to true rest mode after the user has been
//  absent for at least 2 seconds.
//
//  Many English comments were added for clarity.
// ============================================================================

#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_NeoPixel.h>
#include "rgb_lcd.h"

#define TRIG_PIN 9
#define ECHO_PIN 8
#define SOUND_PIN A0
#define ENV_LED_PIN 6
#define STATUS_LED_PIN 7
#define ENV_LED_COUNT 8
#define STATUS_LED_COUNT 8

Adafruit_VEML7700 veml;
Adafruit_NeoPixel envStrip(ENV_LED_COUNT, ENV_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel statusStrip(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
rgb_lcd lcd;

// ------------------- configurable thresholds -------------------
int PRESENT_DISTANCE = 80;
int TOO_CLOSE_DISTANCE = 20;
int MIN_LUX = 80;
int MAX_NOISE = 300;

// ------------------- timing variables --------------------------
unsigned long recommendedFocusTime = 45UL * 60UL * 1000UL;
unsigned long dynamicRestTime = 15UL * 60UL * 1000UL;

unsigned long focusStartTime = 0;
unsigned long totalFocusTime = 0;
unsigned long restStartTime = 0;
unsigned long absenceStartTime = 0;

// ------------------- state flags -------------------------------
bool isPresent = false;
bool breakWarning = false;
bool inRestMode = false;
bool restFinished = false;
bool waitingForUserToLeave = false;   // NEW FEATURE

// ------------------- LCD management ----------------------------
unsigned long lastLcdUpdate = 0;
int lcdPage = 0;

// ------------------- LED optimization --------------------------
unsigned long lastColorChangeTime = 0;
int lastR = -1, lastG = -1, lastB = -1;

// ============================================================================
//  Safe LED updater
// ============================================================================

void safeSetStatusColor(int r, int g, int b) {
  if (r == lastR && g == lastG && b == lastB) return;

  for (int i = 0; i < STATUS_LED_COUNT; i++) {
    statusStrip.setPixelColor(i, statusStrip.Color(r, g, b));
  }
  statusStrip.show();

  lastR = r;
  lastG = g;
  lastB = b;
}

// ----------------------------------------------------------------------------
// Ultrasonic distance
// ----------------------------------------------------------------------------
long getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 0;
  return duration * 0.034 / 2;
}

// ----------------------------------------------------------------------------
// Toggles environment LED bar
// ----------------------------------------------------------------------------
void setEnvironmentLight(bool state) {
  for (int i = 0; i < ENV_LED_COUNT; i++) {
    if (state) envStrip.setPixelColor(i, envStrip.Color(40, 40, 40));
    else envStrip.setPixelColor(i, 0);
  }
  envStrip.show();
}

// ----------------------------------------------------------------------------
// Dynamic recommended focus time calculation (unchanged)
// ----------------------------------------------------------------------------
void calculateRecommendedTime(float lux, int noise, long distanceCM) {
  int baseMin = 45;
  int delta = 0;

  if (lux >= 300) delta += 10;
  else if (lux >= 150) delta += 5;
  else if (lux < 80) delta -= 10;

  if (noise < 200) delta += 10;
  else if (noise < 350) delta += 5;
  else if (noise > 650) delta -= 10;

  if (distanceCM >= 40) delta += 5;
  else if (distanceCM < 20 && distanceCM > 0) delta -= 5;

  int recMin = baseMin + delta;
  if (recMin < 25) recMin = 25;
  if (recMin > 60) recMin = 60;

  recommendedFocusTime = recMin * 60000UL;
}

// ----------------------------------------------------------------------------
// Dynamic rest time calculation (unchanged)
// ----------------------------------------------------------------------------
void calculateRestTime(float lux, int noise) {
  int baseMin = 15;
  int delta = 0;

  if (lux < 80) delta += 5;
  if (noise > 650) delta += 5;
  if (lux > 150 && noise < 250) delta -= 5;

  int restMin = baseMin + delta;
  if (restMin < 5) restMin = 5;
  if (restMin > 25) restMin = 25;

  dynamicRestTime = restMin * 60000UL;
}

// ============================================================================
//  Setup
// ============================================================================
void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.begin(9600);

  envStrip.begin();
  statusStrip.begin();
  lcd.begin(16, 2);

  lcd.setRGB(0, 40, 0);
  lcd.clear();

  if (!veml.begin()) {
    lcd.print("VEML ERROR");
    while (1);
  }

  lcd.print("System Ready");
  delay(1500);
  lcd.clear();
}

// ============================================================================
//  Main Loop
// ============================================================================
void loop() {

  long distance = getDistanceCM();
  int noiseVal = analogRead(SOUND_PIN);
  float lux = veml.readLux();

  bool validDistance = (distance > 5 && distance < 200);
  bool userNear = validDistance && (distance < PRESENT_DISTANCE);
  bool userTooClose = validDistance && (distance < TOO_CLOSE_DISTANCE);

  calculateRecommendedTime(lux, noiseVal, validDistance ? distance : 999);
  calculateRestTime(lux, noiseVal);

  // ============================================================================
  //  1) WAITING-FOR-USER-TO-LEAVE STATE (RED WARNING)
  //     This happens immediately when focus time is reached.
  //     System only enters real rest mode after 2 seconds absence.
  // ============================================================================
  if (waitingForUserToLeave) {

    safeSetStatusColor(40, 0, 0);
    lcd.setRGB(40, 0, 0);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Take a Break!");

    // User still detected → reset absence timer
    if (userNear) {
      absenceStartTime = millis();
    }

    // User absent for >= 2 seconds → enter rest mode
    if (!userNear && millis() - absenceStartTime >= 2000) {
      waitingForUserToLeave = false;
      inRestMode = true;
      restStartTime = millis();
    }

    delay(250);
    return;
  }

  // ============================================================================
  //  2) REST MODE
  // ============================================================================
  if (inRestMode) {

    safeSetStatusColor(40, 0, 0);
    setEnvironmentLight(false);

    unsigned long restElapsedMs = millis() - restStartTime;

    lcd.setRGB(40, 0, 0);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Rest:");
    lcd.print(restElapsedMs / 60000UL);
    lcd.print("/");
    lcd.print(dynamicRestTime / 60000UL);
    lcd.print("m");

    lcd.setCursor(0, 1);
    lcd.print("Leave desk...");

    // Rest completes ONLY if user is away
    if (!userNear && restElapsedMs >= dynamicRestTime) {
      inRestMode = false;
      restFinished = true;
      safeSetStatusColor(20, 20, 0);
    }

    delay(250);
    return;
  }

  // ============================================================================
  //  3) NORMAL FOCUS MODE
  // ============================================================================
  if (userNear && !isPresent) {
    isPresent = true;
    restFinished = false;
    focusStartTime = millis();
  }
  if (!userNear && isPresent) {
    totalFocusTime += millis() - focusStartTime;
    isPresent = false;
  }

  unsigned long currentFocusMs = isPresent ? (millis() - focusStartTime) : 0;
  unsigned long todayFocusMs = totalFocusTime + currentFocusMs;

  // ----------- ENTER RED WARNING MODE WHEN FOCUS TIME ELAPSED ------------
  if (!inRestMode && isPresent && currentFocusMs >= recommendedFocusTime) {
    totalFocusTime += currentFocusMs;
    isPresent = false;

    waitingForUserToLeave = true;
    absenceStartTime = millis();

    safeSetStatusColor(40, 0, 0);
    return;
  }

  // ============================================================================
  //  After finishing rest, user must return to clear restFinished flag
  // ============================================================================
  if (restFinished) {
    setEnvironmentLight(false);
    safeSetStatusColor(20, 20, 0);  // Yellow

    if (userNear) {
      restFinished = false;
      focusStartTime = millis();
    }
  }

  // ============================================================================
  //  LED and LCD color states during focus mode
  // ============================================================================
  if (isPresent) {
    if (!validDistance) {
      if (noiseVal > MAX_NOISE) {
        safeSetStatusColor(40, 40, 0);
        lcd.setRGB(40, 40, 0);
      } else {
        safeSetStatusColor(0, 0, 0);
        lcd.setRGB(0, 0, 0);
      }
    }
    else if (userTooClose) {
      safeSetStatusColor(0, 0, 40);
      lcd.setRGB(0, 0, 40);
    }
    else if (noiseVal > MAX_NOISE) {
      safeSetStatusColor(40, 40, 0);
      lcd.setRGB(40, 40, 0);
    }
    else {
      safeSetStatusColor(0, 40, 0);
      lcd.setRGB(0, 40, 0);
    }
  }

  // ============================================================================
  //  LCD rotation pages
  // ============================================================================
  setEnvironmentLight(lux < MIN_LUX);

  if (millis() - lastLcdUpdate > 2000) {
    lastLcdUpdate = millis();
    lcdPage = (lcdPage + 1) % 2;
    lcd.clear();
  }

  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Total:");
    lcd.print(todayFocusMs / 60000UL);
    lcd.print("m");

    lcd.setCursor(0, 1);
    lcd.print("(");
    lcd.print(currentFocusMs / 60000UL);
    lcd.print(" / Rec:");
    lcd.print(recommendedFocusTime / 60000UL);
    lcd.print(")");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Lux:");
    lcd.print((int)lux);

    lcd.setCursor(0, 1);
    lcd.print("Noise:");
    lcd.print(noiseVal);
  }

  delay(200);
}
