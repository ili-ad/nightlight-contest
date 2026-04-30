
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobot_C4001.h>

// -----------------------------------------------------------------------------
// Anthurium Lite Smoke
// Self-contained bench sketch.
// - Reads C4001 directly over I2C
// - Builds a stable "magenta truth" style track with hold/fade
// - Drives fast J-shapes (stamens) + slow torus reservoir
// - No ambient gate, no clap, no external repo includes
// -----------------------------------------------------------------------------

namespace Config {
// Hardware
constexpr uint8_t kLedPin = 6;
constexpr uint8_t kC4001Addr = 0x2B;
constexpr bool kRgbwStrip = true;

// Topology from archived v1 geometry:
// ring first, then left stamen, then right stamen.
constexpr uint16_t kRingPixels = 45;
constexpr uint16_t kLeftStamenPixels = 16;
constexpr uint16_t kRightStamenPixels = 16;
constexpr uint16_t kTotalPixels = kRingPixels + kLeftStamenPixels + kRightStamenPixels;

// Display / brightness
constexpr uint8_t kGlobalBrightness = 42;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kPrintMs = 80;

// Sensor corridor
constexpr float kNearM = 0.20f;
constexpr float kFarM  = 2.20f;

// Stable track behavior
constexpr uint32_t kHoldMs = 450;
constexpr uint32_t kFadeMs = 1500;
constexpr float kSpeedStillThresholdMps = 0.08f;

// Scene timing
constexpr float kStamenAlpha = 0.28f;      // fast response
constexpr float kTorusAlpha = 0.06f;       // slow reservoir response
constexpr float kTorusDecayPerSec = 0.22f; // 3-6s-ish memory
constexpr float kTorusDiffusionPerSec = 0.28f;
constexpr float kTorusInjectionGain = 0.18f;
constexpr float kWhiteMin = 0.03f;
constexpr float kWhiteMax = 0.17f;

// If your physical stamens run opposite direction, flip this.
constexpr bool kTipAtHighIndex = true;
}  // namespace Config

namespace {
DFRobot_C4001_I2C gC4001(&Wire, Config::kC4001Addr);
Adafruit_NeoPixel gStrip(
    Config::kTotalPixels,
    Config::kLedPin,
    Config::kRgbwStrip ? (NEO_GRBW + NEO_KHZ800) : (NEO_GRB + NEO_KHZ800));

struct SegmentRange {
  uint16_t start;
  uint16_t count;
};

SegmentRange ringSeg() { return {0, Config::kRingPixels}; }
SegmentRange leftSeg() { return {Config::kRingPixels, Config::kLeftStamenPixels}; }
SegmentRange rightSeg() { return {static_cast<uint16_t>(Config::kRingPixels + Config::kLeftStamenPixels), Config::kRightStamenPixels}; }

float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float lerp(float a, float b, float t) {
  return a + ((b - a) * t);
}

float normalizeNearness(float rangeM) {
  const float span = Config::kFarM - Config::kNearM;
  if (span <= 0.001f) return 0.0f;
  const float t = clamp01((rangeM - Config::kNearM) / span);
  return 1.0f - t;
}

uint8_t toByte(float v) {
  return static_cast<uint8_t>(clamp01(v) * 255.0f);
}

void hsvToRgb(float hue, float sat, float val, uint8_t& r, uint8_t& g, uint8_t& b) {
  float h = hue;
  while (h < 0.0f) h += 1.0f;
  while (h >= 1.0f) h -= 1.0f;
  const float s = clamp01(sat);
  const float v = clamp01(val);

  const float scaled = h * 6.0f;
  const int sector = static_cast<int>(scaled);
  const float f = scaled - sector;
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));

  float rf, gf, bf;
  switch (sector % 6) {
    case 0: rf = v; gf = t; bf = p; break;
    case 1: rf = q; gf = v; bf = p; break;
    case 2: rf = p; gf = v; bf = t; break;
    case 3: rf = p; gf = q; bf = v; break;
    case 4: rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;
  }

  r = toByte(rf);
  g = toByte(gf);
  b = toByte(bf);
}

struct StableTrack {
  bool online = false;
  bool hasTrack = false;
  float rangeM = 0.0f;
  float speedMps = 0.0f;
  float influence = 0.0f;
  uint32_t lastAcceptedMs = 0;
  uint8_t phase = 0;  // 0 empty, 1 accepted, 2 hold, 3 fade
};

StableTrack gTrack;
uint32_t gLastFrameMs = 0;
uint32_t gLastPrintMs = 0;

// Scene state
float gLeftLevels[Config::kLeftStamenPixels] = {0};
float gRightLevels[Config::kRightStamenPixels] = {0};
float gTorusHeat[Config::kRingPixels] = {0};

bool initC4001() {
  if (!gC4001.begin()) return false;
  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
  return true;
}

bool readC4001(int& targetNumber, float& rangeM, float& speedMps, int& energy) {
  targetNumber = gC4001.getTargetNumber();
  rangeM = gC4001.getTargetRange();
  speedMps = gC4001.getTargetSpeed();
  energy = gC4001.getTargetEnergy();
  return true;
}

bool shouldAccept(float rangeM, float speedMps) {
  if (rangeM < Config::kNearM) return false;
  if (rangeM > Config::kFarM) return false;
  if (fabsf(speedMps) > 2.6f) return false;
  return true;
}

void updateStableTrack(uint32_t nowMs) {
  int targetNumber = 0;
  float rawRange = 0.0f;
  float rawSpeed = 0.0f;
  int energy = 0;

  const bool ok = readC4001(targetNumber, rawRange, rawSpeed, energy);
  gTrack.online = ok;

  if (ok && targetNumber > 0 && shouldAccept(rawRange, rawSpeed)) {
    gTrack.hasTrack = true;
    gTrack.rangeM = rawRange;
    gTrack.speedMps = rawSpeed;
    gTrack.influence = 1.0f;
    gTrack.lastAcceptedMs = nowMs;
    gTrack.phase = 1;
    return;
  }

  if (!gTrack.hasTrack) {
    gTrack.influence = 0.0f;
    gTrack.phase = 0;
    return;
  }

  const uint32_t ageMs = nowMs - gTrack.lastAcceptedMs;
  if (ageMs <= Config::kHoldMs) {
    gTrack.phase = 2;
    gTrack.influence = 1.0f;
    return;
  }

  if (ageMs >= Config::kHoldMs + Config::kFadeMs) {
    gTrack.hasTrack = false;
    gTrack.influence = 0.0f;
    gTrack.phase = 0;
    return;
  }

  const float fadeT = float(ageMs - Config::kHoldMs) / float(Config::kFadeMs);
  gTrack.influence = 1.0f - clamp01(fadeT);
  gTrack.phase = 3;
}

float targetHueFromTrack() {
  if (!gTrack.hasTrack) return 0.33f;  // greenish idle
  const float nearness = normalizeNearness(gTrack.rangeM);

  // farther = cooler blue, closer = warm amber/red
  const float coolHue = 0.60f;
  const float warmHue = 0.03f;
  float hue = lerp(coolHue, warmHue, nearness);

  if (fabsf(gTrack.speedMps) <= Config::kSpeedStillThresholdMps) {
    // settle toward green when mostly still
    hue = lerp(hue, 0.33f, 0.45f);
  }
  return hue;
}

float targetBrightnessFromTrack() {
  if (!gTrack.hasTrack) return 0.0f;
  const float nearness = normalizeNearness(gTrack.rangeM);
  const float base = lerp(Config::kWhiteMin, Config::kWhiteMax, nearness);
  return base * gTrack.influence;
}

void injectReservoir(float hueSignal, float amount) {
  const SegmentRange ring = ringSeg();
  const uint16_t a = 2 % ring.count;
  const uint16_t b = (ring.count / 2 + 2) % ring.count;

  for (uint16_t i = 0; i < ring.count; ++i) {
    const int dA = abs(int(i) - int(a));
    const int dB = abs(int(i) - int(b));
    const int wrapA = min(dA, int(ring.count) - dA);
    const int wrapB = min(dB, int(ring.count) - dB);

    float w = 0.0f;
    if (wrapA <= 3) w += (1.0f - float(wrapA) / 3.0f);
    if (wrapB <= 3) w += (1.0f - float(wrapB) / 3.0f);

    gTorusHeat[i] = clamp01(gTorusHeat[i] + amount * w * Config::kTorusInjectionGain);
  }
}

void updateScene(float dtSec) {
  const float targetHue = targetHueFromTrack();
  const float targetBrightness = targetBrightnessFromTrack();

  // Fast antenna conveyors
  for (uint16_t i = 0; i < Config::kLeftStamenPixels; ++i) {
    const float pos = (Config::kLeftStamenPixels <= 1) ? 0.0f : float(i) / float(Config::kLeftStamenPixels - 1);
    const float fromTip = Config::kTipAtHighIndex ? pos : (1.0f - pos);
    const float tipWeight = 0.35f + 0.65f * fromTip;
    const float target = targetBrightness * tipWeight;
    gLeftLevels[i] = lerp(gLeftLevels[i], target, Config::kStamenAlpha);
  }

  for (uint16_t i = 0; i < Config::kRightStamenPixels; ++i) {
    const float pos = (Config::kRightStamenPixels <= 1) ? 0.0f : float(i) / float(Config::kRightStamenPixels - 1);
    const float fromTip = Config::kTipAtHighIndex ? pos : (1.0f - pos);
    const float tipWeight = 0.35f + 0.65f * fromTip;
    const float target = targetBrightness * tipWeight;
    gRightLevels[i] = lerp(gRightLevels[i], target, Config::kStamenAlpha);
  }

  // Reservoir decay + diffusion
  float nextHeat[Config::kRingPixels];
  const float decay = clamp01(1.0f - Config::kTorusDecayPerSec * dtSec);
  const float diffusion = Config::kTorusDiffusionPerSec * dtSec;
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (Config::kRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % Config::kRingPixels;
    float v = gTorusHeat[i];
    v += (gTorusHeat[left] + gTorusHeat[right] - 2.0f * gTorusHeat[i]) * diffusion;
    nextHeat[i] = clamp01(v * decay);
  }
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) gTorusHeat[i] = nextHeat[i];

  // Feed the reservoir from current conveyor activity
  if (gTrack.hasTrack) {
    injectReservoir(targetHue, targetBrightness * dtSec * 6.0f);
  }
}

void clearStrip() {
  for (uint16_t i = 0; i < Config::kTotalPixels; ++i) {
    if (Config::kRgbwStrip) gStrip.setPixelColor(i, gStrip.Color(0, 0, 0, 0));
    else gStrip.setPixelColor(i, gStrip.Color(0, 0, 0));
  }
}

void setPixelRgbw(uint16_t idx, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (idx >= Config::kTotalPixels) return;
  if (Config::kRgbwStrip) gStrip.setPixelColor(idx, gStrip.Color(r, g, b, w));
  else gStrip.setPixelColor(idx, gStrip.Color(r, g, b));
}

void renderScene() {
  clearStrip();

  const float activeHue = targetHueFromTrack();
  uint8_t r, g, b;
  hsvToRgb(activeHue, 0.85f, 1.0f, r, g, b);

  // Torus reservoir: slower, more averaged
  const SegmentRange ring = ringSeg();
  for (uint16_t i = 0; i < ring.count; ++i) {
    const float brightness = clamp01(gTorusHeat[i] * 0.95f + 0.02f);
    const float white = clamp01(brightness * 0.25f);
    setPixelRgbw(ring.start + i,
                 toByte(float(r) / 255.0f * brightness),
                 toByte(float(g) / 255.0f * brightness),
                 toByte(float(b) / 255.0f * brightness),
                 toByte(white));
  }

  // Stamens / antennae: quick reaction
  for (uint16_t i = 0; i < Config::kLeftStamenPixels; ++i) {
    const float brightness = gLeftLevels[i];
    setPixelRgbw(Config::kRingPixels + i,
                 toByte(float(r) / 255.0f * brightness),
                 toByte(float(g) / 255.0f * brightness),
                 toByte(float(b) / 255.0f * brightness),
                 toByte(brightness * 0.18f));
  }
  for (uint16_t i = 0; i < Config::kRightStamenPixels; ++i) {
    const float brightness = gRightLevels[i];
    setPixelRgbw(Config::kRingPixels + Config::kLeftStamenPixels + i,
                 toByte(float(r) / 255.0f * brightness),
                 toByte(float(g) / 255.0f * brightness),
                 toByte(float(b) / 255.0f * brightness),
                 toByte(brightness * 0.18f));
  }

  gStrip.show();
}

void printDebug(uint32_t nowMs) {
  if (nowMs - gLastPrintMs < Config::kPrintMs) return;
  gLastPrintMs = nowMs;
  Serial.print("online=");
  Serial.print(gTrack.online ? 1 : 0);
  Serial.print(" has=");
  Serial.print(gTrack.hasTrack ? 1 : 0);
  Serial.print(" phase=");
  Serial.print(gTrack.phase);
  Serial.print(" range_m=");
  Serial.print(gTrack.rangeM, 3);
  Serial.print(" speed=");
  Serial.print(gTrack.speedMps, 3);
  Serial.print(" infl=");
  Serial.print(gTrack.influence, 3);
  Serial.print(" hue=");
  Serial.print(targetHueFromTrack(), 3);
  Serial.print(" bright=");
  Serial.println(targetBrightnessFromTrack(), 3);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Wire.begin();
  initC4001();
  gStrip.begin();
  gStrip.setBrightness(Config::kGlobalBrightness);
  gStrip.show();
}

void loop() {
  const uint32_t nowMs = millis();
  if (gLastFrameMs == 0) gLastFrameMs = nowMs;
  const uint32_t dtMs = nowMs - gLastFrameMs;
  if (dtMs < Config::kFrameMs) return;
  gLastFrameMs = nowMs;
  const float dtSec = float(dtMs) / 1000.0f;

  updateStableTrack(nowMs);
  updateScene(dtSec);
  renderScene();
  printDebug(nowMs);
}
