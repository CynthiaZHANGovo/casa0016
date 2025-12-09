// ============================================================================
//  Study Assistant - Focus & Rest System (Final Version, English)
//  Features:
//   - Dynamic recommended focus time based on environment
//   - Dynamic rest time (longer rest for worse environment)
//   - Warning stage before real rest mode
//   - Distance / noise / light monitoring
//   - Auto environment LED control
//   - LCD page switching
//
//  NOTE:
//  This version includes the corrected logic:
//     Worse light or louder noise → SHORTER study time, LONGER rest time
//
//  Serial debug output remains identical to earlier versions.
// ============================================================================

#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_NeoPixel.h>
#include "rgb_lcd.h"

// ------------------------ PIN DEFINITIONS -----------------------------------
#define TRIG_PIN 9
#define ECHO_PIN 8
#define SOUND_PIN A0
#define ENV_LED_PIN 6
#define STATUS_LED_PIN 7
#define ENV_LED_COUNT 8
#define STATUS_LED_COUNT 8

// ------------------------ OBJECTS -------------------------------------------
Adafruit_VEML7700 veml;
Adafruit_NeoPixel envStrip(ENV_LED_COUNT, ENV_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel statusStrip(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
rgb_lcd lcd;

// ------------------------ THRESHOLDS ----------------------------------------
int PRESENT_DISTANCE = 80;
int TOO_CLOSE_DISTANCE = 20;
int MIN_LUX = 80;
int MAX_NOISE = 300;

// ------------------------ TIMING VARIABLES ----------------------------------
unsigned long recommendedFocusTime = 45UL * 60UL * 1000UL;  
unsigned long dynamicRestTime      = 15UL * 60UL * 1000UL;  

unsigned long focusStartTime = 0;
unsigned long totalFocusTime = 0;
unsigned long restStartTime = 0;
unsigned long absenceStartTime = 0;

// ------------------------ STATE FLAGS ---------------------------------------
bool isPresent = false;
bool inRestMode = false;
bool restFinished = false;
bool waitingForUserToLeave = false;   // new: break warning state

// ------------------------ LCD PAGE CONTROL ----------------------------------
unsigned long lastLcdUpdate = 0;
int lcdPage = 0;

// ------------------------ LED OPTIMIZATION ----------------------------------
int lastR = -1, lastG = -1, lastB = -1;

// ============================================================================
// Helper: Set status LED color only when needed
// ============================================================================
void safeSetStatusColor(int r, int g, int b) {
  if (r == lastR && g == lastG && b == lastB) return;

  for (int i = 0; i < STATUS_LED_COUNT; i++)
    statusStrip.setPixelColor(i, statusStrip.Color(r, g, b));

  statusStrip.show();

  lastR = r;
  lastG = g;
  lastB = b;
}

// ============================================================================
// Ultrasonic distance measurement
// ============================================================================
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

// ============================================================================
// Environment LED control
// ============================================================================
void setEnvironmentLight(bool on) {
  for (int i = 0; i < ENV_LED_COUNT; i++) {
    if (on) envStrip.setPixelColor(i, envStrip.Color(40, 40, 40));
    else    envStrip.setPixelColor(i, 0);
  }
  envStrip.show();
}

// ============================================================================
// Recommended Focus Time (Modified Logic)
// Worse light or louder noise → shorter study time
// ============================================================================
void calculateRecommendedTime(float lux, int noise, long distanceCM) {
  int baseMin = 45;
  int delta = 0;

  // Light logic: dim → study shorter
  if (lux >= 300) delta += 5;         // bright → slightly longer session
  else if (lux >= 150) delta += 0;    // normal
  else if (lux < 80) delta -= 10;     // too dim → shorter recommended session

  // Noise logic: loud → study shorter
  if (noise < 200) delta += 5;        // quiet → slightly longer
  else if (noise < 350) delta += 0;   // moderate
  else if (noise > 650) delta -= 10;  // very loud → shorter study time

  // Distance logic: too close → study shorter
  if (distanceCM >= 40) delta += 5;  
  else if (distanceCM < 20 && distanceCM > 0) delta -= 5;

  int recMin = baseMin + delta;

  if (recMin < 20) recMin = 20;
  if (recMin > 60) recMin = 60;

  recommendedFocusTime = recMin * 60000UL;
}

// ============================================================================
// Dynamic Rest Time (Modified Logic)
// Worse environment → longer rest
// ============================================================================
void calculateRestTime(float lux, int noise) {
  int baseMin = 15;
  int delta = 0;

  if (lux < 80) delta += 5;       // dim → more rest
  if (noise > 650) delta += 5;    // loud → more rest

  if (lux > 150 && noise < 250)   // good environment → less rest
    delta -= 5;

  int restMin = baseMin + delta;

  if (restMin < 5) restMin = 5;
  if (restMin > 25) restMin = 25;

  dynamicRestTime = restMin * 60000UL;
}

// ============================================================================
// Setup
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
  lcd.print("System Ready");
  delay(1500);
  lcd.clear();

  if (!veml.begin()) {
    lcd.print("VEML ERROR");
    while (1);
  }
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {

  long distance = getDistanceCM();
  int noiseVal  = analogRead(SOUND_PIN);
  float lux     = veml.readLux();

  bool validDistance = (distance > 5 && distance < 200);
  bool userNear      = validDistance && (distance < PRESENT_DISTANCE);
  bool userTooClose  = validDistance && (distance < TOO_CLOSE_DISTANCE);

  calculateRecommendedTime(lux, noiseVal, validDistance ? distance : 999);
  calculateRestTime(lux, noiseVal);

  // ========================================================================
  // 1) BREAK WARNING STATE (user must leave for 2 sec)
  // ========================================================================
  if (waitingForUserToLeave) {

    safeSetStatusColor(40, 0, 0);
    lcd.setRGB(40, 0, 0);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Time for break!");

    if (userNear) {
      absenceStartTime = millis();
    }

    if (!userNear && (millis() - absenceStartTime >= 2000)) {
      waitingForUserToLeave = false;
      inRestMode = true;
      restStartTime = millis();
    }

    delay(250);
    return;
  }

  // ========================================================================
  // 2) REAL REST MODE
  // ========================================================================
  if (inRestMode) {

    safeSetStatusColor(40, 0, 0);
    setEnvironmentLight(false);

    unsigned long restElapsed = millis() - restStartTime;

    lcd.setRGB(40, 0, 0);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Rest:");
    lcd.print(restElapsed / 60000UL);
    lcd.print("/");
    lcd.print(dynamicRestTime / 60000UL);
    lcd.print("m");

    lcd.setCursor(0, 1);
    lcd.print("Leave desk...");

    if (!userNear && restElapsed >= dynamicRestTime) {
      inRestMode = false;
      restFinished = true;
      safeSetStatusColor(20, 20, 0);
    }

    delay(250);
    return;
  }

  // ========================================================================
  // 3) NORMAL FOCUS MODE
  // ========================================================================
  if (userNear && !isPresent) {
    isPresent = true;
    restFinished = false;
    focusStartTime = millis();
  }

  if (!userNear && isPresent) {
    totalFocusTime += millis() - focusStartTime;
    isPresent = false;
  }

  unsigned long currentFocus = isPresent ? millis() - focusStartTime : 0;
  unsigned long todayFocus   = totalFocusTime + currentFocus;

  // Enter WARNING state
  if (!inRestMode && isPresent && currentFocus >= recommendedFocusTime) {
    totalFocusTime += currentFocus;
    isPresent = false;

    waitingForUserToLeave = true;
    absenceStartTime = millis();
    safeSetStatusColor(40, 0, 0);
    return;
  }

  // ========================================================================
  // After rest finished: wait for user return
  // ========================================================================
  if (restFinished) {
    setEnvironmentLight(false);
    safeSetStatusColor(20, 20, 0);

    if (userNear) {
      restFinished = false;
      focusStartTime = millis();
    }
  }

  // ========================================================================
  // STATUS LED + LCD COLOR
  // ========================================================================
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

  setEnvironmentLight(lux < MIN_LUX);

  // ========================================================================
  // LCD rotation every 2 sec
  // ========================================================================
  if (millis() - lastLcdUpdate > 2000) {
    lastLcdUpdate = millis();
    lcdPage = (lcdPage + 1) % 2;
    lcd.clear();
  }

  if (lcdPage == 0) {
    lcd.setCursor(0, 0);
    lcd.print("Total:");
    lcd.print(todayFocus / 60000UL);
    lcd.print("m");

    lcd.setCursor(0, 1);
    lcd.print("(");
    lcd.print(currentFocus / 60000UL);
    lcd.print(" / Rec:");
    lcd.print(recommendedFocusTime / 60000UL);
    lcd.print(")");
  }
  else {
    lcd.setCursor(0, 0);
    lcd.print("Lux:");
    lcd.print((int)lux);

    lcd.setCursor(0, 1);
    lcd.print("Noise:");
    lcd.print(noiseVal);
  }

  // ========================================================================
  // FULL SERIAL DEBUG OUTPUT (unchanged)
  // ========================================================================
  Serial.println("================ SENSOR DEBUG ================");
  Serial.print("Distance(cm): "); Serial.println(distance);
  Serial.print("ValidDistance: "); Serial.println(validDistance ? "YES" : "NO");
  Serial.print("Lux: "); Serial.println(lux);
  Serial.print("Noise: "); Serial.println(noiseVal);
  Serial.print("UserPresent: "); Serial.println(isPresent ? "YES" : "NO");
  Serial.print("CurrentFocus(ms): "); Serial.println(currentFocus);
  Serial.print("TotalFocus(ms): "); Serial.println(todayFocus);
  Serial.print("RecommendedFocus(ms): "); Serial.println(recommendedFocusTime);
  Serial.print("RestMode: "); Serial.println(inRestMode ? "YES" : "NO");
  Serial.print("WaitingLeave: "); Serial.println(waitingForUserToLeave ? "YES" : "NO");
  Serial.print("RestFinished: "); Serial.println(restFinished ? "YES" : "NO");
  Serial.print("LED(R,G,B): ");
  Serial.print(lastR); Serial.print(",");
  Serial.print(lastG); Serial.print(",");
  Serial.println(lastB);
  Serial.println("----------------------------------------------");

  delay(200);
}
