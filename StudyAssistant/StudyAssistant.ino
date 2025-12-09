/*
===========================================================================
 Focused Study Feedback Station - FINAL COMPLETE SKETCH (All-English)
===========================================================================

PURPOSE
  - Smart desk assistant that monitors:
      * user presence & sitting distance (HC-SR04 ultrasonic)
      * environmental brightness (VEML7700)
      * environmental noise (analog microphone)
      * study time (per-session & total today)
  - Provides:
      * 2-page RGB LCD (total/rec on page0, lux/noise on page1)
      * Status RGB LED strip (status indicator colors)
      * Environment LED strip (simple on/off room light)
      * Automatic environment light on/off
      * Dynamic recommended study time
      * Dynamic rest time (based on lux & noise)
      * Smart rest mode with power saving behavior

BEHAVIOR (high level)
  - Recommended study time ("recommendedFocusTime") is calculated
    dynamically from lux, noise and distance.
  - When current continuous study >= recommendedFocusTime -> enter REST MODE:
      * Status LEDs locked to RED (indicating rest required)
      * Environment lights are OFF (to save energy)
      * LCD shows: "Rest: <elapsed_minutes> / <recommended_rest_minutes> m"
      * System will wait for rest duration (dynamicRestTime) before switching
        to a "rest finished" state (ORANGE), which stays until user returns.
      * When user returns after rest finished -> start a new study segment
        (focus segment starts) and Total study time continues accumulating.
  - When user is absent (distance out of valid range) -> DO NOT change the
    status LED color (except when in rest mode or rest-finished ORANGE).
  - During REST MODE environment lights are OFF, but status LEDs remain RED.

HARDWARE / LIBS / PINS
  - Adafruit_VEML7700 for lux (I2C)
  - Adafruit_NeoPixel for 2 NeoPixel strips (ENV_LED, STATUS_LED)
  - rgb_lcd for 16x2 RGB-backlit LCD
  - Ultrasonic TRIG/ECHO for distance
  - SOUND_PIN (analog) for noise level

SERIAL OUTPUT (very detailed)
  - The sketch prints a clear, labeled block to Serial every loop with:
      Distance (cm)
      Lux (float)
      Noise (raw analog)
      User Present (YES/NO)
      Current Focus (min:sec)
      Total Study (min)
      Recommended Study (min)
      Dynamic Rest Duration (min)
      Rest Elapsed (min)
      Rest Mode (YES/NO)
      Rest Finished (YES/NO)
      Status LED color (R,G,B)
===========================================================================

NOTES / TUNABLES
  - Tweak PRESENT_DISTANCE, TOO_CLOSE_DISTANCE, MIN_LUX, MAX_NOISE to suit
    your environment and sensor calibrations.
  - The code uses simple integer thresholds and millis() timing; it avoids
    long blocking delays (only a small final delay for loop pacing).
=========================================================================== */

#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_NeoPixel.h>
#include "rgb_lcd.h"

// ================= HARDWARE PINS =================
#define TRIG_PIN 9
#define ECHO_PIN 8
#define SOUND_PIN A0

#define ENV_LED_PIN 7
#define STATUS_LED_PIN 6
#define ENV_LED_COUNT 8
#define STATUS_LED_COUNT 8

// ================= OBJECTS =================
Adafruit_VEML7700 veml;
Adafruit_NeoPixel envStrip(ENV_LED_COUNT, ENV_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel statusStrip(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
rgb_lcd lcd;

// ================= THRESHOLDS & TUNING =================
int PRESENT_DISTANCE = 80;        // cm - distance considered "present"
int TOO_CLOSE_DISTANCE = 20;      // cm - too close
int MIN_LUX = 80;                 // lux threshold for "too dark"
int MAX_NOISE = 300;              // analog threshold for "too noisy" (tune to your microphone)

// ================= TIMING (ms) =================
unsigned long recommendedFocusTime = 45UL * 60UL * 1000UL; // dynamic, default 45 minutes
unsigned long dynamicRestTime = 15UL * 60UL * 1000UL;      // dynamic rest baseline 15 minutes

// ================= STATUS FLAGS & TIMERS =================
bool isPresent = false;
bool breakWarning = false;
bool inRestMode = false;
bool restFinished = false;   // after rest elapsed, before user returns

unsigned long focusStartTime = 0;
unsigned long totalFocusTime = 0;   // accumulative (ms)
unsigned long restStartTime = 0;

// ================= LCD PAGE CONTROL =================
unsigned long lastLcdUpdate = 0;
int lcdPage = 0;

// ================= LED COLOR LOCK =================
unsigned long lastColorChangeTime = 0;
int lastR = -1, lastG = -1, lastB = -1;

void safeSetStatusColor(int r, int g, int b) {
  unsigned long now = millis();
  // do nothing if same color
  if (r == lastR && g == lastG && b == lastB) return;
  // rate-limit changes to 1 second
  if (now - lastColorChangeTime < 1000) return;

  for (int i = 0; i < STATUS_LED_COUNT; i++) {
    statusStrip.setPixelColor(i, statusStrip.Color(r, g, b));
  }
  statusStrip.show();

  lastR = r; lastG = g; lastB = b;
  lastColorChangeTime = now;
}

// ================= DISTANCE (HC-SR04) =================
long getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30ms -> ~5m
  if (duration == 0) return 0;
  long dist = duration * 0.034 / 2;
  return dist;
}

// ================= ENVIRONMENT LIGHT (simple on/off) =================
void setEnvironmentLight(bool state) {
  for (int i = 0; i < ENV_LED_COUNT; i++) {
    if (state) envStrip.setPixelColor(i, envStrip.Color(40, 40, 40)); // dim white-ish
    else envStrip.setPixelColor(i, 0);
  }
  envStrip.show();
}

// ================= RECOMMENDED STUDY TIME (dynamic) =================
void calculateRecommendedTime(float lux, int noise, long distanceCM) {
  int baseMin = 45;
  int delta = 0;

  // brightness influence
  if (lux >= 300) delta += 10;
  else if (lux >= 150) delta += 5;
  else if (lux < 80) delta -= 10;

  // noise influence
  if (noise < 200) delta += 10;
  else if (noise < 350) delta += 5;
  else if (noise > 650) delta -= 10;

  // distance influence
  if (distanceCM >= 40) delta += 5;
  else if (distanceCM < 20 && distanceCM > 0) delta -= 5;

  int recMin = baseMin + delta;
  if (recMin < 25) recMin = 25;
  if (recMin > 60) recMin = 60;

  recommendedFocusTime = (unsigned long)recMin * 60UL * 1000UL;
}

// ================= DYNAMIC REST DURATION =================
void calculateRestTime(float lux, int noise) {
  int baseMin = 15;
  int delta = 0;

  // conditions that increase rest
  if (lux < 80) delta += 5;
  if (noise > 650) delta += 5;

  // very good conditions shorten rest
  if (lux > 150 && noise < 250) delta -= 5;

  int restMin = baseMin + delta;
  if (restMin < 5) restMin = 5;
  if (restMin > 25) restMin = 25;

  dynamicRestTime = (unsigned long)restMin * 60UL * 1000UL;
}

// ================= SETUP =================
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
    while (1); // stop if lux sensor missing
  }

  lcd.setCursor(0, 0);
  lcd.print("Focused Study");
  lcd.setCursor(0, 1);
  lcd.print("System Ready");
  delay(2000);
  lcd.clear();
}

// ================= LOOP =================
void loop() {
  // --- read sensors ---
  long distance = getDistanceCM();          // cm, 0 means invalid/timeout
  int noiseVal = analogRead(SOUND_PIN);     // raw analog
  float lux = veml.readLux();               // float lux

  bool validDistance = (distance > 5 && distance < 200);
  bool userNear = validDistance && (distance < PRESENT_DISTANCE);
  bool userTooClose = validDistance && (distance < TOO_CLOSE_DISTANCE);

  // --- dynamic calculations ---
  calculateRecommendedTime(lux, noiseVal, validDistance ? distance : 999);
  calculateRestTime(lux, noiseVal);

  // --- presence detection & tracking ---
  if (userNear && !inRestMode) {
    if (!isPresent) {
      isPresent = true;
      restFinished = false;      // reset after someone sits
      focusStartTime = millis();
    }
  } else {
    if (isPresent) {
      totalFocusTime += millis() - focusStartTime;
      isPresent = false;
    }
  }

  unsigned long currentFocusMs = isPresent ? (millis() - focusStartTime) : 0;
  unsigned long todayFocusMs = totalFocusTime + currentFocusMs;

  // --- ENTER REST MODE when continuous focus reaches recommended ---
  if (!inRestMode && isPresent && currentFocusMs >= recommendedFocusTime) {
    inRestMode = true;
    breakWarning = true;
    restStartTime = millis();
    totalFocusTime += currentFocusMs;
    isPresent = false;

    // lock status LED to RED immediately
    safeSetStatusColor(40, 0, 0);
  }

  // --- REST MODE OPERATIONS ---
  if (inRestMode) {
    // environment light OFF for power saving
    setEnvironmentLight(false);

    // keep status LED RED (do not allow other code to override)
    safeSetStatusColor(40, 0, 0);

    // Show rest elapsed / recommended rest on LCD (Page override)
    unsigned long restElapsedMs = millis() - restStartTime;
    unsigned long restElapsedMin = restElapsedMs / 60000UL;
    unsigned long restGoalMin = dynamicRestTime / 60000UL;

    // display Rest: elapsed / goal (minutes)
    lcd.setRGB(40, 0, 0);   // red backlight for clarity
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Rest:");
    lcd.print(restElapsedMin);
    lcd.print("/");
    lcd.print(restGoalMin);
    lcd.print("m");

    lcd.setCursor(0, 1);
    lcd.print("Leave desk...");

    // when rest duration has passed *and* user is away, mark restFinished
    if (!userNear && restElapsedMs >= dynamicRestTime) {
      inRestMode = false;
      breakWarning = false;
      restFinished = true; // will show ORANGE until user returns
      // set ORANGE but do not change environment lights (stay off)
      safeSetStatusColor(20, 20, 0);
    }

    // while in rest mode we skip the normal loop updates (power saving)
    // small delay to reduce serial spam
    delay(500);
    return;
  }

  // --- POST-REST: if restFinished -> show ORANGE until user returns ---
  if (restFinished) {
    // Keep environment light OFF until user returns
    setEnvironmentLight(false);
    safeSetStatusColor(20, 20, 0); // ORANGE

    // If user returns, start a new focus segment
    if (userNear) {
      restFinished = false;
      focusStartTime = millis();
      // allow normal LED logic to run after this block (so we won't return)
    }
  }

  // --- STATUS LED decision (only when not in rest mode) ---
  // IMPORTANT: If user is absent, DO NOT change the status LED color
  if (!isPresent) {
    // user away -> intentionally do nothing (preserve last status color)
  } else {
    // user present -> apply normal priority status colors
    if (userTooClose) {
      safeSetStatusColor(0, 0, 40);     // BLUE (too close)
      lcd.setRGB(0, 0, 40);
    }
    else if (noiseVal > MAX_NOISE) {
      safeSetStatusColor(40, 40, 0);    // YELLOW (too noisy)
      lcd.setRGB(40, 40, 0);
    }
    else {
      safeSetStatusColor(0, 40, 0);     // GREEN (present & OK)
      lcd.setRGB(0, 40, 0);
    }
  }

  // --- environment light automatic control (only when not in rest) ---
  bool envLightState = (lux < MIN_LUX);
  setEnvironmentLight(envLightState);

  // --- LCD page rotation (only when not in rest) ---
  if (millis() - lastLcdUpdate > 2000) {
    lastLcdUpdate = millis();
    lcdPage = (lcdPage + 1) % 2;
    lcd.clear();
  }

  // --- LCD display content ---
  if (lcdPage == 0) {
    // Page 0: Total line + (current segment / Rec)
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
    // Page 1: Lux & Noise
    lcd.setCursor(0, 0);
    lcd.print("Lux:");
    lcd.print((int)lux);

    lcd.setCursor(0, 1);
    lcd.print("Noise:");
    lcd.print(noiseVal);
  }

  // --- DETAILED SERIAL OUTPUT (structured, human readable) ---
  Serial.println("====================================================");
  Serial.print("Distance (cm): "); Serial.println(distance);
  Serial.print("Lux (lx): "); Serial.println(lux);
  Serial.print("Noise (raw): "); Serial.println(noiseVal);
  Serial.print("User Present: "); Serial.println(isPresent ? "YES" : "NO");

  // show current focus time as mm:ss for readability
  unsigned long curMs = currentFocusMs;
  unsigned long curMin = curMs / 60000UL;
  unsigned long curSec = (curMs % 60000UL) / 1000UL;
  Serial.print("Current Focus: "); Serial.print(curMin); Serial.print("m ");
  Serial.print(curSec); Serial.println("s");

  Serial.print("Total Focus (min): "); Serial.println(todayFocusMs / 60000UL);
  Serial.print("Recommended Study (min): "); Serial.println(recommendedFocusTime / 60000UL);
  Serial.print("Dynamic Rest Duration (min): "); Serial.println(dynamicRestTime / 60000UL);

  unsigned long restElapsedMin = inRestMode ? ((millis() - restStartTime) / 60000UL) : 0;
  Serial.print("Rest Elapsed (min): "); Serial.println(restElapsedMin);

  Serial.print("Rest Mode: "); Serial.println(inRestMode ? "YES" : "NO");
  Serial.print("Rest Finished (waiting for user): "); Serial.println(restFinished ? "YES" : "NO");

  Serial.print("Flags -> TooClose: "); Serial.print(userTooClose ? "1" : "0");
  Serial.print("  TooDark: "); Serial.print((lux < MIN_LUX) ? "1" : "0");
  Serial.print("  TooNoisy: "); Serial.println((noiseVal > MAX_NOISE) ? "1" : "0");

  Serial.print("Status LED Color (R,G,B): ");
  Serial.print(lastR); Serial.print(","); Serial.print(lastG); Serial.print(","); Serial.println(lastB);
  Serial.println("----------------------------------------------------");

  // small pacing delay
  delay(400);
}
