
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobot_C4001.h>

// -----------------------------------------------------------------------------
// Anthurium Lite Smoke v3
//
// Goal:
// Reintroduce the old Anthurium scene mechanics on top of the stable,
// non-blinking C4001 input path:
//
// - explicit moving ingress conveyor on both stamens
// - torus wash-tub reservoir with diffusion + decay over time
// - ingress-point injection into the ring
// - still/approach/retreat mapped to green / warm-red / cool-blue
//
// This is fully standalone:
// - no repo includes
// - no day mode
// - no clap
// - no boot animation
// -----------------------------------------------------------------------------

namespace Config {
// Hardware
constexpr uint8_t kLedPin = 6;
constexpr uint8_t kC4001Addr = 0x2B;
constexpr bool kRgbwStrip = true;

// Topology: ring first, then left stamen, then right stamen.
constexpr uint16_t kRingPixels = 45;
constexpr uint16_t kLeftStamenPixels = 16;
constexpr uint16_t kRightStamenPixels = 16;
constexpr uint16_t kTotalPixels = kRingPixels + kLeftStamenPixels + kRightStamenPixels;

// Timing / output
constexpr uint8_t kGlobalBrightness = 42;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kPrintMs = 100;

// Sensor corridor / stability
constexpr float kNearM = 0.20f;
constexpr float kFarM = 2.20f;
constexpr float kMaxAcceptedSpeedMps = 2.60f;
constexpr uint32_t kHoldMs = 450;
constexpr uint32_t kFadeMs = 1500;

// Motion interpretation
constexpr float kMotionStillMps = 0.045f;
constexpr float kMotionFullScaleMps = 0.35f;
constexpr float kMotionAlpha = 0.22f;

// Hue mapping
constexpr float kStillHue = 0.33f;      // green
constexpr float kApproachHue = 0.01f;   // red
constexpr float kRetreatHue = 0.60f;    // blue
constexpr float kNearWarmBias = 0.58f;  // push warmth when close even if slow
constexpr float kRetreatBias = 0.95f;

// Conveyor / reservoir timing
constexpr float kIngressTravelSeconds = 3.0f;
constexpr float kIngressSmoothingSec = 0.28f;
constexpr float kIngressConveyorWidth = 0.22f;
constexpr float kIngressFloorFromCharge = 0.42f;
constexpr float kTorusClearSeconds = 4.5f;
constexpr float kTorusDiffusionPerSecond = 0.34f;
constexpr float kReservoirInjectionGain = 0.48f;
constexpr float kTorusInstantGain = 0.08f;
constexpr float kTorusBaseFieldLevel = 0.06f;
constexpr float kStamenAmbientFloor = 0.05f;

// Charge smoothing
constexpr float kChargeRiseAlpha = 0.24f;
constexpr float kChargeFallAlpha = 0.14f;
constexpr float kChargeDeadband = 0.012f;

// White / intensity
constexpr float kBaseSaturation = 0.24f;
constexpr float kMotionSaturationBoost = 0.48f;
constexpr float kNearSaturationBoost = 0.12f;
constexpr float kBaseRgbLevel = 0.08f;
constexpr float kMotionRgbBoost = 0.10f;
constexpr float kNearRgbBoost = 0.14f;
constexpr float kWhiteBase = 0.015f;
constexpr float kWhiteChargeGain = 0.14f;
constexpr float kTorusWhiteGain = 0.28f;
constexpr float kStamenWhiteGain = 0.12f;

// Ingress points, borrowed from the archived Anthurium layout.
constexpr uint16_t kIngressA = 2;
constexpr uint16_t kIngressB = (kRingPixels / 2) + 2;
constexpr float kIngressSpreadPixels = 3.5f;

// If the physical stamen direction is backward, flip this.
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
SegmentRange rightSeg() {
  return {static_cast<uint16_t>(Config::kRingPixels + Config::kLeftStamenPixels),
          Config::kRightStamenPixels};
}

struct ColorF {
  float r;
  float g;
  float b;
  float w;
};

ColorF makeColor(float r = 0.0f, float g = 0.0f, float b = 0.0f, float w = 0.0f) {
  ColorF c;
  c.r = r;
  c.g = g;
  c.b = b;
  c.w = w;
  return c;
}

struct StableTrack {
  bool online;
  bool hasTrack;
  float rangeM;
  float speedMps;
  float influence;
  uint32_t lastAcceptedMs;
  uint8_t phase;  // 0 empty, 1 accepted, 2 hold, 3 fade
};

StableTrack gTrack = {false, false, 0.0f, 0.0f, 0.0f, 0u, 0u};

bool gSensorReady = false;
uint32_t gLastInitAttemptMs = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastPrintMs = 0;

// Motion / stable scene state
bool gHadRangeSample = false;
float gPrevAcceptedRangeM = 0.0f;
float gMotionSignal = 0.0f;   // -1..1, negative retreat, positive approach
float gSmoothedCharge = 0.0f;
float gStableCharge = 0.0f;
float gSmoothedIngressLevel = 0.0f;
float gIngressConveyorPhase = 0.0f;
float gDisplayHue = Config::kStillHue;
float gDisplaySat = Config::kBaseSaturation;
float gDisplayRgbLevel = Config::kBaseRgbLevel;
float gDisplayWhite = Config::kWhiteBase;

// Scene state
float gTorusCharge[Config::kRingPixels];
ColorF gTorusColor[Config::kRingPixels];
float gRingBrightness[Config::kRingPixels];
float gLeftBrightness[Config::kLeftStamenPixels];
float gRightBrightness[Config::kRightStamenPixels];

// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------
float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float clampSigned(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

float lerp(float a, float b, float t) {
  return a + ((b - a) * t);
}

float absf(float v) {
  return (v < 0.0f) ? -v : v;
}

float minf(float a, float b) {
  return (a < b) ? a : b;
}

float maxf(float a, float b) {
  return (a > b) ? a : b;
}

float emaAlphaApprox(float dtSec, float tauSec) {
  if (tauSec <= 0.001f) return 1.0f;
  if (dtSec <= 0.0f) return 0.0f;
  return clamp01(dtSec / (tauSec + dtSec));
}

float decayApprox(float dtSec, float clearSec) {
  if (clearSec <= 0.001f) return 0.0f;
  return clamp01(clearSec / (clearSec + dtSec));
}

float applyDeadband(float previous, float target, float threshold) {
  if (absf(target - previous) <= threshold) return previous;
  return target;
}

float applyBrightnessSlew(float previous, float target, float dtSec) {
  const float maxStep = 2.10f * dtSec;
  if (target > previous + maxStep) return previous + maxStep;
  if (target < previous - maxStep) return previous - maxStep;
  return target;
}

float polynomialKernel(float distance, float width) {
  const float safeWidth = (width < 0.001f) ? 0.001f : width;
  const float x = clamp01(1.0f - (distance / safeWidth));
  return x * x;
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

  float rf = v;
  float gf = t;
  float bf = p;
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

ColorF hsvColor(float hue, float sat, float val, float white) {
  uint8_t r = 0, g = 0, b = 0;
  hsvToRgb(hue, sat, val, r, g, b);
  return makeColor(r / 255.0f, g / 255.0f, b / 255.0f, clamp01(white));
}

float normalizeNearness(float rangeM) {
  const float span = Config::kFarM - Config::kNearM;
  if (span <= 0.001f) return 0.0f;
  const float t = clamp01((rangeM - Config::kNearM) / span);
  return 1.0f - t;
}

ColorF scaleColor(const ColorF& c, float s) {
  return makeColor(clamp01(c.r * s), clamp01(c.g * s), clamp01(c.b * s), clamp01(c.w * s));
}

ColorF addColor(const ColorF& a, const ColorF& b) {
  return makeColor(clamp01(a.r + b.r), clamp01(a.g + b.g), clamp01(a.b + b.b), clamp01(a.w + b.w));
}

ColorF lerpColor(const ColorF& a, const ColorF& b, float t) {
  return makeColor(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.w, b.w, t));
}

// -----------------------------------------------------------------------------
// Sensor
// -----------------------------------------------------------------------------
bool initC4001() {
  if (!gC4001.begin()) return false;
  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
  return true;
}

bool readC4001(int& targetNumber, float& rangeM, float& speedMps, int& energy) {
  if (!gSensorReady) return false;
  targetNumber = gC4001.getTargetNumber();
  rangeM = gC4001.getTargetRange();
  speedMps = gC4001.getTargetSpeed();
  energy = gC4001.getTargetEnergy();
  return true;
}

bool shouldAccept(float rangeM, float speedMps) {
  if (rangeM < Config::kNearM) return false;
  if (rangeM > Config::kFarM) return false;
  if (absf(speedMps) > Config::kMaxAcceptedSpeedMps) return false;
  return true;
}

void updateStableTrack(uint32_t nowMs) {
  if (!gSensorReady && (gLastInitAttemptMs == 0 || (nowMs - gLastInitAttemptMs) >= 1000u)) {
    gLastInitAttemptMs = nowMs;
    gSensorReady = initC4001();
  }

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

  if (ageMs >= (Config::kHoldMs + Config::kFadeMs)) {
    gTrack.hasTrack = false;
    gTrack.influence = 0.0f;
    gTrack.phase = 0;
    return;
  }

  const float t = float(ageMs - Config::kHoldMs) / float(Config::kFadeMs);
  gTrack.influence = clamp01(1.0f - t);
  gTrack.phase = 3;
}

// -----------------------------------------------------------------------------
// Scene mapping
// -----------------------------------------------------------------------------
void updateMotionSignal(float dtSec) {
  float rawMotion = 0.0f;

  if (gTrack.hasTrack && dtSec > 0.0001f) {
    if (gHadRangeSample) {
      // Positive means approaching.
      const float velocityMps = (gPrevAcceptedRangeM - gTrack.rangeM) / dtSec;
      if (absf(velocityMps) <= Config::kMotionStillMps) {
        rawMotion = 0.0f;
      } else {
        rawMotion = clampSigned(velocityMps / Config::kMotionFullScaleMps, -1.0f, 1.0f);
      }
    }
    gPrevAcceptedRangeM = gTrack.rangeM;
    gHadRangeSample = true;
  } else {
    gHadRangeSample = false;
  }

  gMotionSignal = lerp(gMotionSignal, rawMotion, Config::kMotionAlpha);
  if (!gTrack.hasTrack) {
    gMotionSignal = lerp(gMotionSignal, 0.0f, 0.06f);
  }
}

void computeSceneTargets(float& hue, float& sat, float& rgbLevel, float& whiteBase,
                         float& chargeTarget, float& ingressTarget) {
  if (!gTrack.hasTrack) {
    hue = Config::kStillHue;
    sat = 0.10f;
    rgbLevel = 0.02f * gTrack.influence;
    whiteBase = 0.0f;
    chargeTarget = 0.0f;
    ingressTarget = 0.0f;
    return;
  }

  const float nearness = normalizeNearness(gTrack.rangeM) * gTrack.influence;
  const float approach = clamp01(maxf(0.0f, gMotionSignal));
  const float retreat = clamp01(maxf(0.0f, -gMotionSignal));
  const float motionAbs = clamp01(absf(gMotionSignal));

  // Warmth is allowed to come from both approach velocity and closeness.
  const float warm = clamp01((approach * 0.72f) + (nearness * Config::kNearWarmBias));
  const float cool = clamp01(retreat * Config::kRetreatBias);

  if (cool > warm) {
    hue = lerp(Config::kStillHue, Config::kRetreatHue, cool);
  } else {
    hue = lerp(Config::kStillHue, Config::kApproachHue, warm);
  }

  sat = clamp01(Config::kBaseSaturation +
                (motionAbs * Config::kMotionSaturationBoost) +
                (nearness * Config::kNearSaturationBoost));
  rgbLevel = clamp01(Config::kBaseRgbLevel +
                     (motionAbs * Config::kMotionRgbBoost) +
                     (nearness * Config::kNearRgbBoost));
  whiteBase = clamp01(Config::kWhiteBase + (nearness * Config::kWhiteChargeGain * 0.35f));
  chargeTarget = clamp01(nearness);
  ingressTarget = clamp01((motionAbs * 0.78f) + (chargeTarget * 0.26f));
}

float compressChargeTarget(float charge) {
  return clamp01(charge);
}

void updateSmoothedScene(float dtSec) {
  updateMotionSignal(dtSec);

  float targetHue = Config::kStillHue;
  float targetSat = 0.0f;
  float targetRgbLevel = 0.0f;
  float targetWhite = 0.0f;
  float rawChargeTarget = 0.0f;
  float ingressTarget = 0.0f;

  computeSceneTargets(targetHue, targetSat, targetRgbLevel, targetWhite, rawChargeTarget, ingressTarget);

  const float chargeTarget = compressChargeTarget(rawChargeTarget);
  const float chargeAlpha = (chargeTarget >= gSmoothedCharge) ? Config::kChargeRiseAlpha : Config::kChargeFallAlpha;
  gSmoothedCharge += (chargeTarget - gSmoothedCharge) * chargeAlpha;
  gSmoothedCharge = clamp01(gSmoothedCharge);
  gStableCharge = applyDeadband(gStableCharge, gSmoothedCharge, Config::kChargeDeadband);
  gStableCharge = clamp01(gStableCharge);

  const float ingressAlpha = emaAlphaApprox(dtSec, Config::kIngressSmoothingSec);
  const float ingressGoal = clamp01(ingressTarget * (0.30f + (0.70f * gStableCharge)));
  gSmoothedIngressLevel += (ingressGoal - gSmoothedIngressLevel) * ingressAlpha;
  gSmoothedIngressLevel = clamp01(gSmoothedIngressLevel);

  gDisplayHue = lerp(gDisplayHue, targetHue, 0.20f);
  gDisplaySat = lerp(gDisplaySat, targetSat, 0.18f);
  gDisplayRgbLevel = lerp(gDisplayRgbLevel, targetRgbLevel, 0.18f);
  gDisplayWhite = lerp(gDisplayWhite, targetWhite, 0.18f);

  // Explicit old-style conveyor phase.
  const float travelSec = (Config::kIngressTravelSeconds < 0.01f) ? 0.01f : Config::kIngressTravelSeconds;
  gIngressConveyorPhase += dtSec / travelSec;
  while (gIngressConveyorPhase >= 1.0f) {
    gIngressConveyorPhase -= 1.0f;
  }
}

float sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) {
  if (stamenCount == 0) return 0.0f;

  const float denom = (stamenCount > 1) ? float(stamenCount - 1) : 1.0f;
  const float stamenPos = float(stamenPixel) / denom;

  // phase 0 = tip, phase 1 = ring entry.
  const float tipToEntry = Config::kTipAtHighIndex ? (1.0f - stamenPos) : stamenPos;
  float delta = absf(tipToEntry - gIngressConveyorPhase);
  if (delta > 0.5f) {
    delta = 1.0f - delta;
  }

  const float moving = polynomialKernel(delta, Config::kIngressConveyorWidth);
  const float floor = gStableCharge * Config::kIngressFloorFromCharge;
  return clamp01(floor + (moving * gSmoothedIngressLevel));
}

ColorF currentSceneColor(float brightnessScale) {
  ColorF c = hsvColor(gDisplayHue, gDisplaySat, gDisplayRgbLevel * brightnessScale,
                      gDisplayWhite * brightnessScale);
  return c;
}

// -----------------------------------------------------------------------------
// Torus reservoir
// -----------------------------------------------------------------------------
void clearArraysIfNeeded() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    gTorusCharge[i] = 0.0f;
    gTorusColor[i] = makeColor();
    gRingBrightness[i] = 0.0f;
  }
  for (uint16_t i = 0; i < Config::kLeftStamenPixels; ++i) {
    gLeftBrightness[i] = 0.0f;
  }
  for (uint16_t i = 0; i < Config::kRightStamenPixels; ++i) {
    gRightBrightness[i] = 0.0f;
  }
}

void updateTorus(float dtSec) {
  clearArraysIfNeeded();

  float nextCharge[Config::kRingPixels];
  ColorF nextColor[Config::kRingPixels];
  const float decay = decayApprox(dtSec, Config::kTorusClearSeconds);
  const float diffusion = Config::kTorusDiffusionPerSecond * dtSec;

  // Diffuse + decay existing state.
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (Config::kRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % Config::kRingPixels;

    float charge = gTorusCharge[i];
    charge += (gTorusCharge[left] + gTorusCharge[right] - (2.0f * gTorusCharge[i])) * diffusion;
    charge *= decay;
    nextCharge[i] = clamp01(charge);

    ColorF c = gTorusColor[i];
    ColorF l = gTorusColor[left];
    ColorF r = gTorusColor[right];

    ColorF out;
    out.r = clamp01((c.r + ((l.r + r.r - (2.0f * c.r)) * diffusion)) * decay);
    out.g = clamp01((c.g + ((l.g + r.g - (2.0f * c.g)) * diffusion)) * decay);
    out.b = clamp01((c.b + ((l.b + r.b - (2.0f * c.b)) * diffusion)) * decay);
    out.w = clamp01((c.w + ((l.w + r.w - (2.0f * c.w)) * diffusion)) * decay);
    nextColor[i] = out;
  }

  // Inject current scene color at the two ingress points every frame.
  const float torusInput = clamp01(gStableCharge) * dtSec * Config::kReservoirInjectionGain;
  const ColorF injectColor = currentSceneColor(clamp01(0.65f + (gStableCharge * 0.35f)));

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    float distA = absf(float(int(i) - int(Config::kIngressA % Config::kRingPixels)));
    float distB = absf(float(int(i) - int(Config::kIngressB % Config::kRingPixels)));
    if (distA > (Config::kRingPixels * 0.5f)) distA = Config::kRingPixels - distA;
    if (distB > (Config::kRingPixels * 0.5f)) distB = Config::kRingPixels - distB;

    const float wA = polynomialKernel(distA, Config::kIngressSpreadPixels * 2.0f);
    const float wB = polynomialKernel(distB, Config::kIngressSpreadPixels * 2.0f);
    const float weight = clamp01(wA + wB);
    if (weight <= 0.0f) continue;

    nextCharge[i] = clamp01(nextCharge[i] + (torusInput * weight));
    nextColor[i].r = clamp01(nextColor[i].r + (injectColor.r * torusInput * weight));
    nextColor[i].g = clamp01(nextColor[i].g + (injectColor.g * torusInput * weight));
    nextColor[i].b = clamp01(nextColor[i].b + (injectColor.b * torusInput * weight));
    nextColor[i].w = clamp01(nextColor[i].w + (injectColor.w * torusInput * weight));
  }

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    gTorusCharge[i] = nextCharge[i];
    gTorusColor[i] = nextColor[i];
  }
}

// -----------------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------------
void clearStrip() {
  for (uint16_t i = 0; i < Config::kTotalPixels; ++i) {
    if (Config::kRgbwStrip) {
      gStrip.setPixelColor(i, gStrip.Color(0, 0, 0, 0));
    } else {
      gStrip.setPixelColor(i, gStrip.Color(0, 0, 0));
    }
  }
}

void setPixelRgbw(uint16_t idx, const ColorF& c) {
  if (idx >= Config::kTotalPixels) return;
  const uint8_t r = toByte(c.r);
  const uint8_t g = toByte(c.g);
  const uint8_t b = toByte(c.b);
  const uint8_t w = toByte(c.w);
  if (Config::kRgbwStrip) {
    gStrip.setPixelColor(idx, gStrip.Color(r, g, b, w));
  } else {
    gStrip.setPixelColor(idx, gStrip.Color(r, g, b));
  }
}

float sampleTorusField(uint16_t ringPixel) {
  return clamp01(Config::kTorusBaseFieldLevel + gTorusCharge[ringPixel]);
}

void renderStamen(const SegmentRange& seg, float* brightnessState) {
  for (uint16_t i = 0; i < seg.count; ++i) {
    const float ingress = sampleStamenIngress(i, seg.count);
    const float targetBrightness = clamp01((ingress * gSmoothedIngressLevel) +
                                           (Config::kStamenAmbientFloor * maxf(0.25f, gStableCharge)));
    float smoothed = brightnessState[i] + ((targetBrightness - brightnessState[i]) * 0.22f);
    smoothed = applyDeadband(brightnessState[i], smoothed, 0.015f);
    brightnessState[i] = applyBrightnessSlew(brightnessState[i], smoothed, Config::kFrameMs / 1000.0f);

    ColorF c = currentSceneColor(brightnessState[i]);
    c.w = clamp01(c.w + (((c.r + c.g + c.b) * 0.333f) * Config::kStamenWhiteGain));
    setPixelRgbw(seg.start + i, c);
  }
}

void renderRing(const SegmentRange& seg) {
  for (uint16_t i = 0; i < seg.count; ++i) {
    const float field = sampleTorusField(i);
    const float targetBrightness = clamp01((field * maxf(0.22f, gStableCharge)) +
                                           (gStableCharge * Config::kTorusInstantGain));
    float smoothed = gRingBrightness[i] + ((targetBrightness - gRingBrightness[i]) * 0.24f);
    smoothed = applyDeadband(gRingBrightness[i], smoothed, 0.015f);
    gRingBrightness[i] = applyBrightnessSlew(gRingBrightness[i], smoothed, Config::kFrameMs / 1000.0f);

    ColorF c = scaleColor(gTorusColor[i], clamp01(0.28f + gRingBrightness[i]));
    const float avg = (c.r + c.g + c.b) * 0.333f;
    c.w = clamp01(c.w + (avg * Config::kTorusWhiteGain) + (gTorusCharge[i] * 0.06f));
    setPixelRgbw(seg.start + i, c);
  }
}

void renderScene() {
  clearStrip();
  renderRing(ringSeg());
  renderStamen(leftSeg(), gLeftBrightness);
  renderStamen(rightSeg(), gRightBrightness);
  gStrip.show();
}

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------
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
  Serial.print(" infl=");
  Serial.print(gTrack.influence, 3);
  Serial.print(" motion=");
  Serial.print(gMotionSignal, 3);
  Serial.print(" charge=");
  Serial.print(gStableCharge, 3);
  Serial.print(" ingress=");
  Serial.print(gSmoothedIngressLevel, 3);
  Serial.print(" phase_f=");
  Serial.print(gIngressConveyorPhase, 3);
  Serial.print(" hue=");
  Serial.println(gDisplayHue, 3);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(60);
  Wire.begin();

  gStrip.begin();
  gStrip.setBrightness(Config::kGlobalBrightness);
  gStrip.show();

  clearArraysIfNeeded();
  gLastInitAttemptMs = millis();
  gSensorReady = initC4001();
}

void loop() {
  const uint32_t nowMs = millis();
  if (gLastFrameMs == 0) gLastFrameMs = nowMs;
  const uint32_t dtMs = nowMs - gLastFrameMs;
  if (dtMs < Config::kFrameMs) return;
  gLastFrameMs = nowMs;

  const float dtSec = float(dtMs) / 1000.0f;

  updateStableTrack(nowMs);
  updateSmoothedScene(dtSec);
  updateTorus(dtSec);
  renderScene();
  printDebug(nowMs);
}
