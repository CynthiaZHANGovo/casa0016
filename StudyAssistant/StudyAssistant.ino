/*
============================================================
 Focused Study Feedback Station - Final System Description
============================================================

This system is a smart desk assistant designed to monitor:
- User presence
- Study time
- Environmental brightness
- Environmental noise
- Sitting distance

It provides:
- Real-time feedback on an RGB LCD
- Status indication using RGB LEDs
- Automatic environment light ON/OFF
- Intelligent break reminder

------------------------------------------------------------
LCD DISPLAY LAYOUT (2 PAGES ONLY)
------------------------------------------------------------
Page 0:
- Total focus time today (minutes)
- Recommended continuous study time (fixed: 45 min)

Page 1:
- Brightness (Lux)
- Noise level (Analog value)

------------------------------------------------------------
STATUS RGB LED LOGIC (LOW BRIGHTNESS)
------------------------------------------------------------
- RED    : Environment problem detected
- GREEN  : User present & environment OK
- YELLOW : User away from desk

------------------------------------------------------------
ENVIRONMENT LIGHT LOGIC (ON/OFF ONLY)
------------------------------------------------------------
- Light ON  : If brightness is too low
- Light OFF : If brightness is sufficient

------------------------------------------------------------
SMART BREAK REMINDER
------------------------------------------------------------
Break warning is triggered ONLY if:
1) Continuous focus time > 45 minutes
AND
2) Any environment problem exists
============================================================
*/

#include <Wire.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_NeoPixel.h>
#include "rgb_lcd.h"

// ================= HARDWARE =================
#define TRIG_PIN 8
#define ECHO_PIN 9
#define SOUND_PIN A0

#define ENV_LED_PIN 6
#define STATUS_LED_PIN 7
#define ENV_LED_COUNT 8
#define STATUS_LED_COUNT 8

// ================= OBJECTS =================
Adafruit_VEML7700 veml;
Adafruit_NeoPixel envStrip(ENV_LED_COUNT, ENV_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel statusStrip(STATUS_LED_COUNT, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);
rgb_lcd lcd;

// ================= THRESHOLDS =================
int PRESENT_DISTANCE = 60;
int TOO_CLOSE_DISTANCE = 25;
int MIN_LUX = 80;
int MAX_NOISE = 600;

unsigned long FOCUS_LIMIT = 45UL * 60UL * 1000UL;

// ================= STATUS =================
bool isPresent = false;
bool breakWarning = false;

unsigned long focusStartTime = 0;
unsigned long totalFocusTime = 0;

// ================= LCD PAGE CONTROL =================
unsigned long lastLcdUpdate = 0;
int lcdPage = 0;

// ================= DISTANCE FUNCTION =================
long getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  return duration * 0.034 / 2;
}

// ================= STATUS LED (LOW BRIGHTNESS) =================
void setStatusColor(int r, int g, int b) {
  for (int i = 0; i < STATUS_LED_COUNT; i++) {
    statusStrip.setPixelColor(i, statusStrip.Color(r, g, b));
  }
  statusStrip.show();
}

// ================= ENVIRONMENT LIGHT (ON / OFF ONLY) =================
void setEnvironmentLight(bool state) {
  if (state) {
    for (int i = 0; i < ENV_LED_COUNT; i++) {
      envStrip.setPixelColor(i, envStrip.Color(60, 60, 60));
    }
  } else {
    for (int i = 0; i < ENV_LED_COUNT; i++) {
      envStrip.setPixelColor(i, 0);
    }
  }
  envStrip.show();
}

// ================= SETUP =================
void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.begin(9600);
  Serial.println("===== Focused Study Feedback Station START =====");

  envStrip.begin();
  statusStrip.begin();

  lcd.begin(16, 2);
  lcd.setRGB(0, 50, 0);
  lcd.clear();

  if (!veml.begin()) {
    lcd.print("VEML ERROR");
    Serial.println("ERROR: VEML7700 NOT DETECTED!");
    while (1);
  }

  lcd.setCursor(0, 0);
  lcd.print("Focused Study");
  lcd.setCursor(0, 1);
  lcd.print("System Ready");

  Serial.println("System Ready.");
  delay(2000);
  lcd.clear();
}

// ================= LOOP =================
void loop() {
  long distance = getDistanceCM();
  int noiseValue = analogRead(SOUND_PIN);
  float lux = veml.readLux();

  // ===== Presence Detection =====
  if (distance < PRESENT_DISTANCE) {
    if (!isPresent) {
      isPresent = true;
      focusStartTime = millis();
      Serial.println("EVENT: User Detected -> Focus Started");
    }
  } else {
    if (isPresent) {
      totalFocusTime += millis() - focusStartTime;
      isPresent = false;
      Serial.println("EVENT: User Left -> Focus Paused");
    }
  }

  unsigned long currentFocusTime = isPresent ? (millis() - focusStartTime) : 0;
  unsigned long todayFocusTime = totalFocusTime + currentFocusTime;

  // ===== Environment Analysis =====
  bool tooClose = distance < TOO_CLOSE_DISTANCE;
  bool tooDark = lux < MIN_LUX;
  bool tooNoisy = noiseValue > MAX_NOISE;

  // ===== Status RGB LED (DIMMED) =====
  String ledState = "YELLOW (AWAY)";
  if (tooClose || tooDark || tooNoisy) {
    setStatusColor(50, 0, 0);
    lcd.setRGB(50, 0, 0);
    ledState = "RED (PROBLEM)";
  } 
  else if (isPresent) {
    setStatusColor(0, 50, 0);
    lcd.setRGB(0, 50, 0);
    ledState = "GREEN (GOOD)";
  } 
  else {
    setStatusColor(50, 50, 0);
    lcd.setRGB(50, 50, 0);
  }

  // ===== Environment Light ON/OFF =====
  bool envLightState = lux < MIN_LUX;
  setEnvironmentLight(envLightState);

  // ===== Smart Break Logic =====
  breakWarning = (currentFocusTime > FOCUS_LIMIT) && (tooClose || tooDark || tooNoisy);

  // ===== LCD Page Switch (2 Pages) =====
  if (millis() - lastLcdUpdate > 2000) {
    lastLcdUpdate = millis();
    lcdPage = (lcdPage + 1) % 2;
    lcd.clear();
  }

  if (breakWarning) {
    lcd.setCursor(0, 0);
    lcd.print(" TAKE A BREAK ");
    lcd.setCursor(0, 1);
    lcd.print(" REST YOUR EYES ");
  } 
  else {
    if (lcdPage == 0) {
      lcd.setCursor(0, 0);
      lcd.print("Total:");
      lcd.print(todayFocusTime / 60000);
      lcd.print("m");

      lcd.setCursor(0, 1);
      lcd.print("Recommend:45m");
    } 
    else if (lcdPage == 1) {
      lcd.setCursor(0, 0);
      lcd.print("Lux:");
      lcd.print((int)lux);

      lcd.setCursor(0, 1);
      lcd.print("Noise:");
      lcd.print(noiseValue);
    }
  }

  // ================= SERIAL DEBUG OUTPUT =================
  Serial.println("------------------------------------------------");
  Serial.print("Distance: "); Serial.print(distance); Serial.println(" cm");
  Serial.print("Brightness: "); Serial.print(lux); Serial.println(" lux");
  Serial.print("Noise: "); Serial.println(noiseValue);

  Serial.print("User Present: ");
  Serial.println(isPresent ? "YES" : "NO");

  Serial.print("Too Close: ");
  Serial.println(tooClose ? "YES" : "NO");

  Serial.print("Too Dark: ");
  Serial.println(tooDark ? "YES" : "NO");

  Serial.print("Too Noisy: ");
  Serial.println(tooNoisy ? "YES" : "NO");

  Serial.print("Current Focus Time: ");
  Serial.print(currentFocusTime / 60000);
  Serial.println(" min");

  Serial.print("Today's Total Focus: ");
  Serial.print(todayFocusTime / 60000);
  Serial.println(" min");

  Serial.print("Break Warning: ");
  Serial.println(breakWarning ? "YES" : "NO");

  Serial.print("LCD Page: ");
  Serial.println(lcdPage);

  Serial.print("Status LED: ");
  Serial.println(ledState);

  Serial.print("Environment Light: ");
  Serial.println(envLightState ? "ON" : "OFF");

  delay(500);
}
