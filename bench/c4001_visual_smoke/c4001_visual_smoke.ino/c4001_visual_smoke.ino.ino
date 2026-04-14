#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "DFRobot_C4001.h"

#define LED_PIN    6
#define LED_COUNT  144

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);
DFRobot_C4001_I2C radar(&Wire, 0x2B);

// ----------------------------
// Bench tuning
// ----------------------------
const uint8_t GLOBAL_BRIGHTNESS = 20;
const unsigned long PRINT_INTERVAL_MS = 120;

// Your room-scale data now clearly spans much farther than the earlier short test.
const float RANGE_MIN_M = 0.45f;
const float RANGE_MAX_M = 5.20f;

// Motion thresholds
const float SPEED_STILL_MPS  = 0.03f;
const float SPEED_COLOR_CLIP = 0.80f;
const float SPEED_REJECT_MPS = 1.50f;

// Smoothing
const float RANGE_ALPHA  = 0.30f;
const float SPEED_ALPHA  = 0.22f;
const float ENERGY_ALPHA = 0.08f;

// Hold last track briefly through tiny dropouts
const unsigned long TARGET_HOLD_MS = 350;

// Filter state
bool  haveTrack = false;
float filtRange = 0.0f;
float filtSpeed = 0.0f;
float filtEnergyNorm = 0.0f;
unsigned long lastSeenMs = 0;

// ----------------------------
// Helpers
// ----------------------------
float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

float mapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
  if (inMax <= inMin) return outMin;
  float t = (x - inMin) / (inMax - inMin);
  t = clamp01(t);
  return outMin + (t * (outMax - outMin));
}

uint8_t toByte(float x) {
  return (uint8_t)(clamp01(x) * 255.0f);
}

// Energy is very noisy. Log compression makes it less absurd.
float compressEnergy(uint32_t rawEnergy) {
  const float top = logf(65536.0f);
  const float val = logf(1.0f + (float)rawEnergy);
  return clamp01(val / top);
}

void clearStrip() {
  strip.clear();
  strip.show();
}

void renderIdle(unsigned long nowMs) {
  strip.clear();

  // Faint breathing blue idle
  float breath = 0.5f + 0.5f * sinf((float)(nowMs % 3000UL) * 6.28318f / 3000.0f);
  uint8_t b = (uint8_t)(3 + 8 * breath);

  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, strip.Color(0, 0, b, 0));
  }

  strip.show();
}

// Negative speed = approaching = warmer / redder
// Positive speed = retreating = cooler / bluer
// Near zero = calm green-white
void speedToColor(float speedMps, uint8_t &r, uint8_t &g, uint8_t &b, uint8_t &w, float energyNorm) {
  float s = speedMps;

  if (s > SPEED_COLOR_CLIP)  s = SPEED_COLOR_CLIP;
  if (s < -SPEED_COLOR_CLIP) s = -SPEED_COLOR_CLIP;

  if (fabsf(s) < SPEED_STILL_MPS) {
    r = 10;
    g = 160;
    b = 30;
    w = (uint8_t)(10 + 35 * energyNorm);
    return;
  }

  float t = clamp01(fabsf(s) / SPEED_COLOR_CLIP);

  if (s < 0.0f) {
    // Approaching: yellow/orange -> red
    r = 180;
    g = (uint8_t)(120.0f * (1.0f - t));
    b = 0;
    w = (uint8_t)(8.0f * energyNorm);
  } else {
    // Retreating: cyan/green -> blue
    r = 0;
    g = (uint8_t)(120.0f * (1.0f - t));
    b = 180;
    w = (uint8_t)(5.0f * energyNorm);
  }
}

void renderBlob(float rangeM, float speedMps, float energyNorm) {
  strip.clear();

  // Range owns the position
  float center = mapFloat(rangeM, RANGE_MIN_M, RANGE_MAX_M, 0.0f, (float)(LED_COUNT - 1));

  // Keep width mostly stable so the display reads like a cursor, not a fog bank.
  float speedAbs = fabsf(speedMps);
  float width = 4.5f + (2.0f * clamp01(speedAbs / SPEED_COLOR_CLIP));

  // Energy only modestly boosts brightness
  float brightness = 0.30f + (0.45f * energyNorm);

  uint8_t baseR, baseG, baseB, baseW;
  speedToColor(speedMps, baseR, baseG, baseB, baseW, energyNorm);

  for (int i = 0; i < LED_COUNT; i++) {
    float dist = fabsf((float)i - center);
    float falloff = 1.0f - (dist / width);
    falloff = clamp01(falloff);

    // Sharper cursor-like falloff
    falloff = falloff * falloff * falloff;

    uint8_t r = (uint8_t)(baseR * brightness * falloff);
    uint8_t g = (uint8_t)(baseG * brightness * falloff);
    uint8_t b = (uint8_t)(baseB * brightness * falloff);
    uint8_t w = (uint8_t)(baseW * falloff);

    strip.setPixelColor(i, strip.Color(r, g, b, w));
  }

  // Add a tiny white center tick so the exact position is easier to read
  int centerIdx = (int)roundf(center);
  if (centerIdx >= 0 && centerIdx < LED_COUNT) {
    strip.setPixelColor(centerIdx, strip.Color(baseR, baseG, baseB, 40));
  }

  strip.show();
}

// ----------------------------
// Setup / Loop
// ----------------------------
void setup() {
  Serial.begin(19200);
  delay(500);

  Wire.begin();

  strip.begin();
  strip.setBrightness(GLOBAL_BRIGHTNESS);
  strip.clear();
  strip.show();

  while (!radar.begin()) {
    Serial.println("C4001 not found.");
    delay(1000);
  }

  Serial.println("C4001 connected.");

  radar.setSensorMode(eSpeedMode);
  radar.setDetectThres(11, 1200, 10);
  radar.setFrettingDetection(eON);

  Serial.println("Radar configured.");
  renderIdle(millis());
}

void loop() {
  unsigned long now = millis();

  uint8_t  rawTargets = radar.getTargetNumber();
  float    rawRange   = radar.getTargetRange();
  float    rawSpeed   = radar.getTargetSpeed();
  uint32_t rawEnergy  = radar.getTargetEnergy();

  bool speedRejected = false;

  if (fabsf(rawSpeed) > SPEED_REJECT_MPS) {
    rawSpeed = 0.0f;
    speedRejected = true;
  }

  if (rawTargets > 0 && rawRange > 0.0f) {
    float energyNorm = compressEnergy(rawEnergy);

    if (!haveTrack) {
      filtRange = rawRange;
      filtSpeed = rawSpeed;
      filtEnergyNorm = energyNorm;
      haveTrack = true;
    } else {
      filtRange      += RANGE_ALPHA  * (rawRange   - filtRange);
      filtSpeed      += SPEED_ALPHA  * (rawSpeed   - filtSpeed);
      filtEnergyNorm += ENERGY_ALPHA * (energyNorm - filtEnergyNorm);
    }

    lastSeenMs = now;
  } else {
    if (haveTrack && (now - lastSeenMs > TARGET_HOLD_MS)) {
      haveTrack = false;
    }
  }

  if (haveTrack) {
    renderBlob(filtRange, filtSpeed, filtEnergyNorm);
  } else {
    renderIdle(now);
  }

  static unsigned long lastPrint = 0;
  if (now - lastPrint >= PRINT_INTERVAL_MS) {
    lastPrint = now;

    Serial.print("targets=");
    Serial.print(rawTargets);

    Serial.print("  raw_range=");
    Serial.print(rawRange, 3);

    Serial.print("  filt_range=");
    Serial.print(filtRange, 3);

    Serial.print("  raw_speed=");
    Serial.print(rawSpeed, 3);

    Serial.print("  filt_speed=");
    Serial.print(filtSpeed, 3);

    Serial.print("  raw_energy=");
    Serial.print(rawEnergy);

    Serial.print("  filt_energy_norm=");
    Serial.print(filtEnergyNorm, 3);

    Serial.print("  speed_rejected=");
    Serial.println(speedRejected ? "yes" : "no");
  }

  delay(35);
}