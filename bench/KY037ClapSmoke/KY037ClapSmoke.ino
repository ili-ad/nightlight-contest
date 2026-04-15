#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

namespace Pins {
constexpr uint8_t kMicAnalogPin = A0;
constexpr uint8_t kStripPin = 6;
}

constexpr uint16_t kPixels = 144;
constexpr uint8_t kBrightness = 28;
constexpr uint32_t kPrintMs = 40;

float gBaseline = 0.0f;
float gDevEma = 0.0f;
float gEnv = 0.0f;
float gNoiseFloor = 1.0f;

bool gLedOn = false;
bool gArmed = false;
bool gReadyForSecond = false;
uint32_t gFirstClapMs = 0;
uint32_t gLastTriggerMs = 0;
uint32_t gArmQuietSinceMs = 0;

constexpr float kFirstThresholdMin = 2.0f;
constexpr float kFirstThresholdScale = 0.94f;
constexpr float kFirstThresholdOffset = 1.07f;

constexpr float kSecondThresholdMin = 1.54f;
constexpr float kSecondThresholdScale = 0.54f;
constexpr float kSecondThresholdOffset = 0.74f;

constexpr uint32_t kCooldownMs = 320;
constexpr uint32_t kSecondGapMinMs = 90;
constexpr uint32_t kSecondGapMaxMs = 950;
constexpr uint32_t kSecondListenTimeoutMs = 1250;
constexpr uint32_t kQuietRearmMs = 45;

constexpr float kFirstReleaseFactor = 0.56f;
constexpr float kSecondReleaseFactor = 0.72f;

Adafruit_NeoPixel gStrip(kPixels, Pins::kStripPin, NEO_GRBW + NEO_KHZ800);

static float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static void fillColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
  for (uint16_t i = 0; i < kPixels; ++i) {
    gStrip.setPixelColor(i, gStrip.Color(r, g, b, w));
  }
}

static void renderStrip(float env, float thr1, float thr2) {
  if (gArmed) {
    fillColor(0, 0, 90);
  } else if (gLedOn) {
    fillColor(0, 90, 0);
  } else {
    fillColor(90, 0, 0);
  }

  const float meterNorm = clamp01(env / 36.0f);
  const uint16_t lit = static_cast<uint16_t>(meterNorm * kPixels);
  for (uint16_t i = 0; i < lit && i < kPixels; ++i) {
    gStrip.setPixelColor(i, gStrip.Color(0, 0, 0, 72));
  }

  const uint16_t t1 = static_cast<uint16_t>(clamp01(thr1 / 36.0f) * (kPixels - 1));
  gStrip.setPixelColor(t1, gStrip.Color(90, 45, 0, 0));
  const uint16_t t2 = static_cast<uint16_t>(clamp01(thr2 / 36.0f) * (kPixels - 1));
  gStrip.setPixelColor(t2, gStrip.Color(90, 0, 90, 0));

  gStrip.show();
}

void setup() {
  Serial.begin(115200);
  gStrip.begin();
  gStrip.setBrightness(kBrightness);
  gStrip.clear();
  gStrip.show();
  pinMode(Pins::kMicAnalogPin, INPUT);
  Serial.println("raw dev env floor thr1 thr2 armed ready led");
}

void loop() {
  const uint32_t now = millis();
  const int raw = analogRead(Pins::kMicAnalogPin);

  if (gBaseline == 0.0f) gBaseline = raw;
  gBaseline += (raw - gBaseline) * 0.02f;

  const float dev = fabsf(raw - gBaseline);
  gDevEma += (dev - gDevEma) * 0.08f;
  gEnv += (gDevEma - gEnv) * 0.22f;

  const bool learnFloor = !gArmed && (now - gLastTriggerMs > kCooldownMs);
  if (learnFloor) {
    gNoiseFloor += (gEnv - gNoiseFloor) * 0.015f;
    if (gNoiseFloor < 0.6f) gNoiseFloor = 0.6f;
  }

  float thr1 = gNoiseFloor * kFirstThresholdScale + kFirstThresholdOffset;
  if (thr1 < kFirstThresholdMin) thr1 = kFirstThresholdMin;
  if (thr1 > 14.0f) thr1 = 14.0f;

  float thr2 = gNoiseFloor * kSecondThresholdScale + kSecondThresholdOffset;
  if (thr2 < kSecondThresholdMin) thr2 = kSecondThresholdMin;
  if (thr2 > (thr1 - 0.8f)) thr2 = thr1 - 0.8f;
  if (thr2 < 4.0f) thr2 = 4.0f;

  const float release1 = thr1 * kFirstReleaseFactor;

  if (!gArmed) {
    if (gEnv >= thr1 && (now - gLastTriggerMs > kCooldownMs)) {
      gArmed = true;
      gReadyForSecond = false;
      gFirstClapMs = now;
      gLastTriggerMs = now;
      gArmQuietSinceMs = 0;
      Serial.println("event=clap1");
    }
  } else {
    if (!gReadyForSecond) {
      if (gEnv <= release1) {
        if (gArmQuietSinceMs == 0) gArmQuietSinceMs = now;
        if (now - gArmQuietSinceMs >= kQuietRearmMs) {
          gReadyForSecond = true;
          Serial.println("event=ready_for_second");
        }
      } else {
        gArmQuietSinceMs = 0;
      }
    } else {
      const uint32_t dt = now - gFirstClapMs;
      if (gEnv >= thr2 && dt >= kSecondGapMinMs && dt <= kSecondGapMaxMs) {
        gLedOn = !gLedOn;
        gLastTriggerMs = now;
        gArmed = false;
        gReadyForSecond = false;
        gArmQuietSinceMs = 0;
        Serial.print("event=double_clap toggle=");
        Serial.println(gLedOn ? "on" : "off");
      } else if (gEnv >= thr1 && dt > kSecondGapMinMs) {
        gFirstClapMs = now;
        gLastTriggerMs = now;
        gReadyForSecond = false;
        gArmQuietSinceMs = 0;
        Serial.println("event=clap1_rearm");
      }
    }

    if (gArmed && (now - gFirstClapMs > kSecondListenTimeoutMs)) {
      gArmed = false;
      gReadyForSecond = false;
      gArmQuietSinceMs = 0;
      Serial.println("event=clap1_timeout");
    }
  }

  renderStrip(gEnv, thr1, thr2);

  static uint32_t lastPrint = 0;
  if (now - lastPrint >= kPrintMs) {
    lastPrint = now;
    Serial.print("raw="); Serial.print(raw);
    Serial.print(" dev="); Serial.print(dev, 1);
    Serial.print(" env="); Serial.print(gEnv, 1);
    Serial.print(" floor="); Serial.print(gNoiseFloor, 1);
    Serial.print(" thr1="); Serial.print(thr1, 1);
    Serial.print(" thr2="); Serial.print(thr2, 1);
    Serial.print(" armed="); Serial.print(gArmed ? 1 : 0);
    Serial.print(" ready="); Serial.print(gReadyForSecond ? 1 : 0);
    Serial.print(" led="); Serial.println(gLedOn ? 1 : 0);
  }
}
