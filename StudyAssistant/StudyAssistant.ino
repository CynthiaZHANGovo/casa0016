/*
  Smart Study Assistant
  - Monitors distance, sound, light
  - Tracks study duration when distance indicates attention
  - Changes WS2812B color to yellow if too close
  - Increases light if too dark
  - Suggests study time based on brightness, sound, distance
  - Displays data and suggestions on OLED and Serial

  Hardware:
    UNO + XFP1116-07AY OLED (I2C)
    VEML7700 light sensor
    HC-SR04 distance sensor
    Sound Sensor v1.6
    WS2812B
*/

#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_NeoPixel.h>

// ---------- OLED ----------
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// ---------- VEML7700 ----------
Adafruit_VEML7700 veml;

// ---------- HC-SR04 ----------
const int TRIG_PIN = 2;
const int ECHO_PIN = 3;

// ---------- Sound Sensor ----------
const int SOUND_PIN = A0;

// ---------- WS2812B ----------
const int LED_PIN = 6;
const int NUM_LEDS = 8;
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- Study tracking ----------
bool studying = false;
unsigned long studyStart = 0;     // session start
unsigned long sessionTime = 0;    // current session duration in ms
unsigned long totalStudyTime = 0; // total study duration in ms

// ---------- Helper ----------
bool vemlOK = false;
unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}

  // OLED init
  oled.begin();
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawStr(0, 12, "Smart Study Init");
  oled.sendBuffer();

  // VEML7700 init
  if (!veml.begin()) {
    Serial.println("VEML7700 not found!");
    vemlOK = false;
  } else {
    vemlOK = true;
    veml.setGain(VEML7700_GAIN_1);
    veml.setIntegrationTime(VEML7700_IT_100MS);
  }

  // HC-SR04
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // WS2812B
  strip.begin();
  strip.show();
}

void loop() {
  // Read sensors
  float distance = readDistanceCM();
  float lux = vemlOK ? veml.readLux() : 0.0;
  int soundRaw = analogRead(SOUND_PIN);

  // Study detection based on distance (e.g., >30cm)
  if (distance > 30 && distance < 100) {
    if (!studying) {
      studying = true;
      studyStart = millis();
    }
    sessionTime = millis() - studyStart;
  } else {
    if (studying) {
      totalStudyTime += sessionTime;
      sessionTime = 0;
      studying = false;
    }
  }

  // Calculate suggested study time (simple heuristic)
  // Base: 50 min session, modified by conditions
  float comfortScore = 1.0; // 1.0 = ideal
  if (lux < 150) comfortScore *= 0.8;      // too dark
  if (soundRaw > 600) comfortScore *= 0.9; // noisy
  if (distance < 30) comfortScore *= 0.7;  // too close

  float suggestedMinutes = 50 * comfortScore;

  // Update LEDs
  updateLEDs(distance, lux, soundRaw);

  // Update OLED
  drawOLED(distance, lux, soundRaw, suggestedMinutes);

  // Serial output
  if (millis() - lastUpdate > 500) {
    lastUpdate = millis();
    Serial.print("Distance(cm): "); Serial.print(distance);
    Serial.print("  Lux: "); Serial.print(lux);
    Serial.print("  Sound: "); Serial.print(soundRaw);
    Serial.print("  Suggested(min): "); Serial.print(suggestedMinutes);
    Serial.print("  Session(s): "); Serial.print(sessionTime/1000);
    Serial.print("  Total(s): "); Serial.println(totalStudyTime/1000);
  }

  delay(100);
}

// ----------------- Functions -----------------
float readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  return duration / 58.0;
}

void updateLEDs(float distance, float lux, int soundRaw) {
  uint32_t color;

  if (distance < 0) {
    color = strip.Color(50, 0, 50); // error
  } else if (distance < 30) {
    color = strip.Color(200, 200, 0); // yellow for too close
  } else {
    // Adjust brightness by light
    int brightness = constrain(map(lux, 0, 400, 50, 255), 50, 255);
    color = strip.Color(0, brightness, 0); // green
  }

  for (int i=0; i<NUM_LEDS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void drawOLED(float distance, float lux, int soundRaw, float suggestedMinutes) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  char buf[32];

  oled.drawStr(0, 12, "Smart Study");

  snprintf(buf, sizeof(buf), "Dist: %.0f cm", distance);
  oled.drawStr(0, 26, buf);

  snprintf(buf, sizeof(buf), "Lux: %.0f  Sound: %d", lux, soundRaw);
  oled.drawStr(0, 40, buf);

  snprintf(buf, sizeof(buf), "Suggested: %.0f min", suggestedMinutes);
  oled.drawStr(0, 54, buf);

  oled.sendBuffer();
}
