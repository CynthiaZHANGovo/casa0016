/*
  Integrated Test with U8g2 OLED for:
  - XFP1116-07AY I2C OLED display (assumed SSD1306 128x64 I2C)
  - VEML7700 light sensor (I2C)
  - HC-SR04 ultrasonic distance sensor
  - Sound Sensor v1.6 (analog output to A0)
  - WS2812B addressable LED strip / ring

  Board: Arduino UNO

  Pin mapping:
    I2C: A4 (SDA), A5 (SCL)
    OLED (XFP1116-07AY): GND -> GND, VCC -> 5V, SDA -> A4, SCL -> A5
    VEML7700: VIN -> 5V, GND -> GND, SDA -> A4, SCL -> A5, 3Vo not connected

    HC-SR04:  TRIG -> D2, ECHO -> D3, VCC -> 5V, GND -> GND
    Sound:    SIG -> A0, VCC -> 5V, GND -> GND, NC not connected
    WS2812B:  DIN -> D6 (through 330R resistor), 5V -> 5V, GND -> GND
*/

#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_VEML7700.h>
#include <Adafruit_NeoPixel.h>

// ---------- U8g2 OLED ----------
// Try this constructor first (SSD1306 128x64 I2C):
// If nothing shows, later we can try another (e.g. SH1106).
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  U8G2_R0,        // rotation
  /* reset=*/ U8X8_PIN_NONE
);

// ---------- VEML7700 ----------
Adafruit_VEML7700 veml;

// ---------- HC-SR04 pins ----------
const int TRIG_PIN = 2;
const int ECHO_PIN = 3;

// ---------- Sound sensor ----------
const int SOUND_PIN = A0;

// ---------- WS2812B ----------
const int LED_PIN = 6;
const int NUM_LEDS = 8;   // adjust to your actual number of LEDs
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ---------- State ----------
bool vemlOK = false;
unsigned long lastPrint = 0;

// ---------- Prototypes ----------
float readDistanceCM();
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max);
void updateLEDs(float distance, float lux, int soundRaw);
void drawOLED(float distance, float lux, int soundRaw);

void setup() {
  // Serial debug
  Serial.begin(115200);
  while (!Serial) { ; }
  Serial.println("Starting integrated test (U8g2 + sensors)...");

  // Init I2C
  Wire.begin();

  // Init OLED
  Serial.println("Initializing OLED...");
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(0, 12, "OLED init OK");
  u8g2.sendBuffer();

  // Init VEML7700
  Serial.println("Initializing VEML7700...");
  if (!veml.begin()) {
    Serial.println("ERROR: VEML7700 not found. Check wiring.");
    vemlOK = false;
  } else {
    Serial.println("VEML7700 found!");
    vemlOK = true;
    veml.setGain(VEML7700_GAIN_1);
    veml.setIntegrationTime(VEML7700_IT_100MS);
  }

  // HC-SR04 pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // WS2812B
  strip.begin();
  strip.show();

  // Simple startup LED animation
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(0, 0, 40)); // dim blue
    strip.show();
    delay(60);
  }
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();

  delay(500);
}

void loop() {
  // 1) Read sensors
  float distanceCM = readDistanceCM();

  float lux = 0.0;
  if (vemlOK) {
    lux = veml.readLux();
  }

  int soundRaw = analogRead(SOUND_PIN);

  // 2) Update LEDs
  updateLEDs(distanceCM, lux, soundRaw);

  // 3) Draw on OLED
  drawOLED(distanceCM, lux, soundRaw);

  // 4) Print to Serial every 500 ms
  unsigned long now = millis();
  if (now - lastPrint > 500) {
    lastPrint = now;
    Serial.print("Distance(cm): ");
    Serial.print(distanceCM);
    Serial.print("  Lux: ");
    Serial.print(lux);
    Serial.print("  Sound(raw): ");
    Serial.println(soundRaw);
  }

  delay(100);
}

// ---------- HC-SR04 distance ----------
float readDistanceCM() {
  // Trigger a 10us pulse
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout

  if (duration == 0) {
    // No echo received
    return -1.0;
  }

  float distance = duration / 58.0; // in cm (approx)
  return distance;
}

// ---------- Map float ----------
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  if (x <= in_min) return out_min;
  if (x >= in_max) return out_max;
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ---------- LED logic ----------
void updateLEDs(float distance, float lux, int soundRaw) {
  uint32_t color;

  if (distance < 0) {
    // Purple = no distance / out of range
    color = strip.Color(70, 0, 70);
  } else {
    // Simple comfort score from lux + sound
    float luxScore = mapFloat(lux, 0.0, 400.0, 0.0, 100.0);      // brighter is better
    float soundScore = mapFloat(soundRaw, 250.0, 900.0, 100.0, 0.0); // louder is worse

    if (luxScore < 0) luxScore = 0;
    if (luxScore > 100) luxScore = 100;
    if (soundScore < 0) soundScore = 0;
    if (soundScore > 100) soundScore = 100;

    float comfort = (luxScore + soundScore) * 0.5f;

    if (comfort > 80) {
      color = strip.Color(0, 120, 0);       // green
    } else if (comfort > 50) {
      color = strip.Color(170, 120, 0);     // yellow
    } else {
      color = strip.Color(160, 0, 0);       // red
    }
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

// ---------- OLED drawing ----------
void drawOLED(float distance, float lux, int soundRaw) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tf);

  // Line 1: title
  u8g2.drawStr(0, 12, "Env Test");

  // Line 2: Distance
  char buf[32];
  if (distance < 0) {
    snprintf(buf, sizeof(buf), "Dist: N/A");
  } else {
    snprintf(buf, sizeof(buf), "Dist: %.0f cm", distance);
  }
  u8g2.drawStr(0, 26, buf);

  // Line 3: Lux
  if (vemlOK) {
    snprintf(buf, sizeof(buf), "Lux : %.0f", lux);
  } else {
    snprintf(buf, sizeof(buf), "Lux : ERR");
  }
  u8g2.drawStr(0, 40, buf);

  // Line 4: Sound
  snprintf(buf, sizeof(buf), "Sound: %4d", soundRaw);
  u8g2.drawStr(0, 54, buf);

  u8g2.sendBuffer();
}
