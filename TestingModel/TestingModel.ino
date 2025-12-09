// ----- DEMO VERSION -----
// Fixed focus time = 1 minute
// Fixed rest time = 1 minute
// Sensors only influence LEDs and LCD, NOT timing
// Rest mode now requires 2 seconds of no user after red warning
// ----------------------------------------------

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

// Fixed demo times
unsigned long FIXED_FOCUS_TIME = 1UL * 60UL * 1000UL;  
unsigned long FIXED_REST_TIME  = 1UL * 60UL * 1000UL;

bool isPresent = false;
bool inRestMode = false;
bool waitingForUserToLeave = false;

unsigned long focusStartTime = 0;
unsigned long restStartTime = 0;
unsigned long absenceStartTime = 0;

int MIN_LUX = 80;
int MAX_NOISE = 300;
int PRESENT_DISTANCE = 80;
int TOO_CLOSE_DISTANCE = 20;

// -------------------------------- LED HELPERS -----------------------------------

void safeSetStatusColor(int r, int g, int b) {
  for (int i = 0; i < STATUS_LED_COUNT; i++)
    statusStrip.setPixelColor(i, statusStrip.Color(r, g, b));
  statusStrip.show();
}

void setEnvironmentLight(bool state) {
  for (int i = 0; i < ENV_LED_COUNT; i++) {
    if (state) envStrip.setPixelColor(i, envStrip.Color(40, 40, 40));
    else envStrip.setPixelColor(i, 0);
  }
  envStrip.show();
}

// -------------------------------- SENSOR FUNCTIONS -----------------------------------

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

// -------------------------------- SETUP -----------------------------------

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.begin(9600);

  envStrip.begin();
  statusStrip.begin();

  lcd.begin(16, 2);
  lcd.setRGB(0, 40, 0);
  lcd.clear();
  lcd.print("DEMO Mode Ready");
  delay(1500);
  lcd.clear();

  if (!veml.begin()) {
    lcd.print("VEML ERROR");
    while (1);
  }
}

// -------------------------------- MAIN LOOP -----------------------------------

void loop() {

  long distance = getDistanceCM();
  int noiseVal = analogRead(SOUND_PIN);
  float lux = veml.readLux();

  bool validDistance = (distance > 5 && distance < 200);
  bool userNear = validDistance && (distance < PRESENT_DISTANCE);
  bool userTooClose = validDistance && (distance < TOO_CLOSE_DISTANCE);

  // ======================================================================
  // ------------------------- REST MODE ----------------------------------
  // ======================================================================
  if (inRestMode) {
    safeSetStatusColor(40, 0, 0);   // Red
    setEnvironmentLight(false);

    lcd.setRGB(40, 0, 0);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Resting...");

    unsigned long elapsed = millis() - restStartTime;
    lcd.setCursor(0, 1);
    lcd.print(elapsed / 1000);
    lcd.print(" / 60 s");

    if (!userNear && elapsed >= FIXED_REST_TIME) {
      inRestMode = false;
      safeSetStatusColor(0, 40, 0);  // Green again
      lcd.setRGB(0, 40, 0);
    }
    delay(300);
    return;
  }

  // ======================================================================
  // ---------------- WAITING FOR USER TO LEAVE (RED WARNING) -------------
  // ======================================================================
  if (waitingForUserToLeave) {
    safeSetStatusColor(40, 0, 0); // red warning
    setEnvironmentLight(false);

    lcd.setRGB(40, 0, 0);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Time for a break!");

    // User is still present → reset clock
    if (userNear) {
      absenceStartTime = millis();
    }

    // User left for 2 seconds → enter rest mode
    if (!userNear && millis() - absenceStartTime >= 2000) {
      waitingForUserToLeave = false;
      inRestMode = true;
      restStartTime = millis();
    }

    delay(300);
    return;
  }

  // ======================================================================
  // ---------------------------- FOCUS MODE -------------------------------
  // ======================================================================
  if (userNear && !isPresent) {
    isPresent = true;
    focusStartTime = millis();
  }
  if (!userNear && isPresent) {
    isPresent = false;
  }

  unsigned long currentFocus = isPresent ? millis() - focusStartTime : 0;

  // Reached 1-minute focus → red warning (NOT rest yet)
  if (isPresent && currentFocus >= FIXED_FOCUS_TIME) {
    waitingForUserToLeave = true;
    absenceStartTime = millis();  // Start tracking absence
    safeSetStatusColor(40, 0, 0); // red
    return;
  }

  // LED + LCD based on sensors
  if (!validDistance) {
    safeSetStatusColor(0, 0, 0);
    lcd.setRGB(0, 0, 0);
  }
  else if (userTooClose) {
    safeSetStatusColor(0, 0, 40);   // Blue
    lcd.setRGB(0, 0, 40);
  }
  else if (noiseVal > MAX_NOISE) {
    safeSetStatusColor(40, 40, 0);  // Yellow
    lcd.setRGB(40, 40, 0);
  }
  else {
    safeSetStatusColor(0, 40, 0);   // Green
    lcd.setRGB(0, 40, 0);
  }

  setEnvironmentLight(lux < MIN_LUX);

  // LCD in focus mode
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Focus:");
  lcd.print(currentFocus / 1000);
  lcd.print("s");

  lcd.setCursor(0, 1);
  lcd.print("Lux:");
  lcd.print((int)lux);
  lcd.print("  N:");
  lcd.print(noiseVal);

  delay(300);
}
