#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN    6
#define LED_COUNT  144

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);
BH1750 lightMeter;

// Fill timing
const unsigned long FILL_DURATION_MS = 6000;
const unsigned long HOLD_AT_FULL_MS  = 1200;
const unsigned long HOLD_AT_EMPTY_MS = 500;

// Lux mapping range
const float LUX_MIN = 0.0f;
const float LUX_MAX = 80.0f;

// LED brightness cap
const uint8_t GLOBAL_BRIGHTNESS = 20;

// ----------------------------
// Helpers
// ----------------------------

float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

float mapLuxToNormalized(float lux) {
  float t = (lux - LUX_MIN) / (LUX_MAX - LUX_MIN);
  return clamp01(t);
}

// Map lux to a blue -> green -> red gradient
void luxToColor(float luxNorm, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &w) {
  // Keep white off for this diagnostic so the hue shift is obvious
  w = 0;

  if (luxNorm < 0.5f) {
    // blue -> green
    float t = luxNorm / 0.5f;
    r = 0;
    g = (uint8_t)(255.0f * t);
    b = (uint8_t)(255.0f * (1.0f - t));
  } else {
    // green -> red
    float t = (luxNorm - 0.5f) / 0.5f;
    r = (uint8_t)(255.0f * t);
    g = (uint8_t)(255.0f * (1.0f - t));
    b = 0;
  }
}

int currentFillCount(unsigned long nowMs) {
  const unsigned long cycleMs = FILL_DURATION_MS + HOLD_AT_FULL_MS + HOLD_AT_EMPTY_MS;
  unsigned long t = nowMs % cycleMs;

  if (t < FILL_DURATION_MS) {
    float p = (float)t / (float)FILL_DURATION_MS;
    int n = (int)(p * LED_COUNT);
    if (n < 1) n = 1;
    if (n > LED_COUNT) n = LED_COUNT;
    return n;
  }

  t -= FILL_DURATION_MS;
  if (t < HOLD_AT_FULL_MS) {
    return LED_COUNT;
  }

  return 0;
}

void renderFill(int litCount, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  strip.clear();

  if (litCount > LED_COUNT) litCount = LED_COUNT;
  if (litCount < 0) litCount = 0;

  for (int i = 0; i < litCount; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b, w));
  }

  strip.show();
}

// ----------------------------
// Setup / Loop
// ----------------------------

void setup() {
  Serial.begin(19200);
  delay(200);

  Wire.begin();

  strip.begin();
  strip.setBrightness(GLOBAL_BRIGHTNESS);
  strip.clear();
  strip.show();

  if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 ready");
  } else {
    Serial.println("BH1750 init failed");
  }
}

void loop() {
  unsigned long nowMs = millis();

  float lux = lightMeter.readLightLevel();
  if (lux < 0) lux = 0;

  float luxNorm = mapLuxToNormalized(lux);

  uint8_t r, g, b, w;
  luxToColor(luxNorm, r, g, b, w);

  int litCount = currentFillCount(nowMs);
  renderFill(litCount, r, g, b, w);

  static unsigned long lastPrint = 0;
  if (nowMs - lastPrint > 200) {
    lastPrint = nowMs;

    Serial.print("lux=");
    Serial.print(lux, 2);
    Serial.print("  norm=");
    Serial.print(luxNorm, 3);
    Serial.print("  lit=");
    Serial.print(litCount);
    Serial.print("  color=(");
    Serial.print(r);
    Serial.print(",");
    Serial.print(g);
    Serial.print(",");
    Serial.print(b);
    Serial.print(",");
    Serial.print(w);
    Serial.println(")");
  }

  delay(20);
}