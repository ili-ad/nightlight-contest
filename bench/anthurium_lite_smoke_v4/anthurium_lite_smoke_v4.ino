#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobot_C4001.h>

// -----------------------------------------------------------------------------
// Anthurium Lite Smoke v4.3
//
// Bench sketch. Standalone. No app/scene framework required.
//
// Behavioral inheritance from smoke v3:
// - Stable C4001 accept / hold / fade path.
// - Motion is primarily derived from range delta, not blindly from raw speed.
// - A shared scene envelope controls hue, charge, white haze, ingress level.
// - J-shapes are old-style travelling wave shaders, not literal packet queues.
// - Rings are reservoirs: per-pixel charge plus normalized hue memory.
// - v4.3 makes ring injection field-led instead of two-node-led.
//
// v4 changes:
// - Real-ish bench topology: front ring 44, rear ring 44, left J 12, right J 12.
// - Two reservoirs: front is sharper, rear is softer/dimmer/wall-wash-like.
// - Optional anchored plasma drift around the two ingress points.
// - Static scratch buffers instead of large local reservoir arrays.
//
// Default strip order for the installed/bench Anthurium wiring:
//   [ right J 12 ][ left J 12 ][ front ring 44 ][ rear ring 44 ]
//
// Physical path:
//   Arduino -> right J, top to bottom
//           -> left J, bottom to top
//           -> front ring, starts near 6 o'clock and runs clockwise
//           -> rear ring, starts near 6 o'clock and runs counterclockwise
//
// The segment starts below are therefore circuit-order starts, not spatial order.
// -----------------------------------------------------------------------------

namespace Config {
// ---------------------------------------------------------------------------
// Hardware
// ---------------------------------------------------------------------------
constexpr uint8_t kLedPin = 6;
constexpr uint8_t kC4001Addr = 0x2B;
constexpr bool kRgbwStrip = true;

// Set this true for a no-sensor tabletop demo. It synthesizes a slow approach /
// retreat target and bypasses the C4001 completely.
constexpr bool kSyntheticDemoMode = false;

// If the C4001 is unplugged, use the synthetic target instead of going dark.
// Leave false when validating real sensor behavior.
constexpr bool kSyntheticIfSensorMissing = false;

// ---------------------------------------------------------------------------
// Topology
// ---------------------------------------------------------------------------
constexpr uint16_t kFrontRingPixels = 44;
constexpr uint16_t kRearRingPixels = 44;
constexpr uint16_t kLeftJPixels = 12;
constexpr uint16_t kRightJPixels = 12;
constexpr uint16_t kRingPixels = kFrontRingPixels;  // reservoirs assume equal rings

constexpr uint16_t kRightJStart = 0;
constexpr uint16_t kLeftJStart = kRightJStart + kRightJPixels;
constexpr uint16_t kFrontRingStart = kLeftJStart + kLeftJPixels;
constexpr uint16_t kRearRingStart = kFrontRingStart + kFrontRingPixels;
constexpr uint16_t kTotalPixels = kRearRingStart + kRearRingPixels;

// Physical strip direction correction. Logical index 0 is the effect index;
// these flags only flip the address sent to the LED strip.
//
// Front ring is already clockwise from 6 o'clock in the described wiring.
// Rear ring is physically counterclockwise, so reverse it to keep logical ring
// direction consistent with the front ring.
constexpr bool kFrontRingReverse = false;
constexpr bool kRearRingReverse = true;
constexpr bool kLeftJReverse = false;
constexpr bool kRightJReverse = false;

// J-shape orientation for the travelling wave. false means phase 0 appears at
// logical index 0 and travels toward the high index. With the wiring described
// above, this follows right-J top->bottom and left-J bottom->top.
constexpr bool kLeftTipAtHighIndex = false;
constexpr bool kRightTipAtHighIndex = false;
constexpr float kLeftJPhaseOffset = 0.00f;
constexpr float kRightJPhaseOffset = 0.035f;

// ---------------------------------------------------------------------------
// Timing / output
// ---------------------------------------------------------------------------
constexpr uint8_t kGlobalBrightness = 48;
constexpr uint32_t kFrameMs = 16;       // ~60 fps target
constexpr uint32_t kSensorPollMs = 60;  // C4001 path is slower than render path
constexpr uint32_t kPrintMs = 160;
constexpr bool kDebugSerial = true;

// ---------------------------------------------------------------------------
// Sensor corridor / stability
// ---------------------------------------------------------------------------
constexpr float kNearM = 0.20f;
constexpr float kFarM = 2.20f;
constexpr float kMaxAcceptedSpeedMps = 2.60f;
constexpr uint32_t kHoldMs = 1800;   // ride through short C4001 zero-return gaps
constexpr uint32_t kFadeMs = 6500;   // slow visual fade instead of sudden blackout

// If the sensor stays online but gives no accepted target for a long time,
// gently re-run the C4001 init sequence. This is not a visual crutch; it is a
// bench-level recovery hook for the occasional "zeroed returns forever" gremlin.
constexpr bool kReinitSensorAfterLongDropout = true;
constexpr uint32_t kSensorDropoutReinitMs = 14000;
constexpr uint32_t kSensorReinitCooldownMs = 12000;

// DFRobot C4001 speed-mode threshold values, preserved from v3.
constexpr uint16_t kDetectRangeThreshold = 11;
constexpr uint16_t kDetectEnergyThreshold = 1200;
constexpr uint16_t kDetectDelayThreshold = 10;
constexpr float kEnergyReference = 1200.0f;
constexpr float kEnergyChargeBlend = 0.16f;  // range still dominates charge

// ---------------------------------------------------------------------------
// Motion interpretation
// ---------------------------------------------------------------------------
constexpr float kMotionStillMps = 0.045f;
constexpr float kMotionFullScaleMps = 0.35f;
constexpr float kMotionSmoothingSec = 0.075f;
constexpr float kMotionGoalDecaySec = 0.42f;

// Hue mapping
constexpr float kStillHue = 0.33f;      // green
constexpr float kApproachHue = 0.01f;   // warm red / ember
constexpr float kRetreatHue = 0.60f;    // blue
constexpr float kNearWarmBias = 0.58f;  // close presence warms the field
constexpr float kRetreatBias = 0.95f;

// ---------------------------------------------------------------------------
// Conveyor / reservoir timing
// ---------------------------------------------------------------------------
constexpr float kIngressTravelSeconds = 3.0f;
constexpr float kIngressSmoothingSec = 0.28f;
constexpr float kIngressConveyorWidth = 0.22f;
constexpr float kIngressFloorFromCharge = 0.42f;
constexpr float kStamenAmbientFloor = 0.090f;
constexpr float kStamenOutputScale = 1.65f;

// Front ring: sharper, more legible.
constexpr float kFrontClearSeconds = 8.2f;
constexpr float kFrontDiffusionPerSecond = 0.62f;
constexpr float kFrontInjectionGain = 0.32f;
constexpr float kFrontInstantGain = 0.010f;
constexpr float kFrontBaseFieldLevel = 0.075f;
constexpr float kFrontIngressSpreadPixels = 14.0f;
constexpr float kFrontOutputScale = 1.18f;
constexpr float kFrontWhiteGain = 0.035f;

// Rear ring: wider, slower, dimmer wall-wash memory.
constexpr float kRearClearSeconds = 9.0f;
constexpr float kRearDiffusionPerSecond = 0.70f;
constexpr float kRearInjectionGain = 0.24f;
constexpr float kRearInstantGain = 0.008f;
constexpr float kRearBaseFieldLevel = 0.064f;
constexpr float kRearIngressSpreadPixels = 16.0f;
constexpr float kRearOutputScale = 0.90f;
constexpr float kRearWhiteGain = 0.12f;
constexpr float kRearHueSoftening = 0.10f;  // pull rear slightly toward still hue
constexpr float kRearIngressOffsetPixels = 0.0f;
constexpr bool kRearMirrorIngress = true;

// v4.3 ring philosophy: the old ingress points are now only soft biases.
// A low whole-field infusion prevents two white nodes from becoming the whole
// story and lets the rings read as reservoirs rather than injection nozzles.
constexpr float kFrontWholeFieldInjection = 0.34f;
constexpr float kFrontAnchorInjection = 0.46f;
constexpr float kRearWholeFieldInjection = 0.42f;
constexpr float kRearAnchorInjection = 0.36f;
constexpr float kRingColorAssimilation = 0.72f;
constexpr float kRingMaxPixelAddPerFrame = 0.018f;
constexpr float kFrontMoodWashGain = 0.034f;
constexpr float kRearMoodWashGain = 0.042f;

// A ring-wide smoke shimmer restores the accidental v4 behavior where the rear
// ring was receiving a mix of rear-reservoir memory and J-wave rendering because
// of the old segment offset. This makes the loved "after pixel 68" behavior a
// disabled in v4.2: this created a radar / choo-choo band on the rings.
constexpr bool kEnableRingSurfaceWave = false;
constexpr float kFrontSurfaceWaveGain = 0.00f;
constexpr float kRearSurfaceWaveGain = 0.00f;
constexpr float kRingSurfaceWaveWidth = 0.18f;
constexpr float kRingSurfaceWaveSecondWidth = 0.27f;
constexpr float kRingSurfaceWaveWhiteGain = 0.035f;

// The travelling J-wave can give the rings a small arrival bump whenever the
// phase wraps from entry back to tip. This keeps old v3 continuous injection
// while making the causal seam more visible.
constexpr float kArrivalInjectionBoost = 0.00f;
constexpr float kArrivalPulseWidth = 0.12f;

// Ingress anchors on a 44-pixel logical ring. These are the v3 anchors adapted
// from 45 -> 44: one near the start, one across the ring.
constexpr float kLeftIngressAnchor = 2.0f;
constexpr float kRightIngressAnchor = (kRingPixels * 0.5f) + 2.0f;

// In v4.2 the ring ingress is pinned back to fixed v3-style anchors to restore
// reservoir elegance before revisiting any Brownian behavior.
constexpr bool kEnableRovingIngress = false;
constexpr float kIngressMaxOffsetPixels = 0.0f;
constexpr float kIngressJitterAccel = 4.4f;     // pixels / second^2, randomized
constexpr float kIngressMotionBias = 1.1f;      // motion nudges cursor velocity
constexpr float kIngressVelocityDamping = 2.9f; // higher = sleepier cursor
constexpr float kIngressMaxSpeed = 2.3f;        // pixels / second

// Charge smoothing
constexpr float kChargeRiseAlpha = 0.24f;
constexpr float kChargeFallAlpha = 0.14f;
constexpr float kChargeDeadband = 0.012f;

// White / intensity
constexpr float kBaseSaturation = 0.30f;
constexpr float kMotionSaturationBoost = 0.55f;
constexpr float kNearSaturationBoost = 0.16f;
constexpr float kBaseRgbLevel = 0.10f;
constexpr float kMotionRgbBoost = 0.13f;
constexpr float kNearRgbBoost = 0.18f;
constexpr float kWhiteBase = 0.015f;
constexpr float kWhiteChargeGain = 0.060f;
constexpr float kStamenWhiteGain = 0.12f;

// A tiny nonzero white hush so the bench is visibly alive even between targets.
constexpr float kIdleRgbLevel = 0.004f;
constexpr float kIdleWhiteLevel = 0.005f;

// Off is not a sensor state. When the C4001 goes quiet, keep a low living
// field so the sculpture exhales instead of blacking out.
constexpr float kNoTrackLivingCharge = 0.070f;
constexpr float kNoTrackRgbLevel = 0.014f;
constexpr float kNoTrackWhite = 0.006f;
constexpr float kNoTrackIngress = 0.020f;
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
  bool reversed;
};

SegmentRange frontRingSeg() {
  return {Config::kFrontRingStart, Config::kFrontRingPixels, Config::kFrontRingReverse};
}

SegmentRange rearRingSeg() {
  return {Config::kRearRingStart, Config::kRearRingPixels, Config::kRearRingReverse};
}

SegmentRange leftJSeg() {
  return {Config::kLeftJStart, Config::kLeftJPixels, Config::kLeftJReverse};
}

SegmentRange rightJSeg() {
  return {Config::kRightJStart, Config::kRightJPixels, Config::kRightJReverse};
}

uint16_t segmentPixel(const SegmentRange& seg, uint16_t logicalIndex) {
  if (logicalIndex >= seg.count) return Config::kTotalPixels;
  const uint16_t physical = seg.reversed ? (seg.count - 1u - logicalIndex) : logicalIndex;
  return seg.start + physical;
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
  bool freshAccepted;
  float rangeM;
  float rawSpeedMps;
  uint16_t energy;
  float influence;
  uint32_t lastAcceptedMs;
  uint8_t phase;  // 0 empty, 1 accepted, 2 hold, 3 fade
};

struct SceneEnvelope {
  float motion;       // -1 retreat, +1 approach
  float charge;       // stable nearness / presence intensity
  float ingressLevel; // moving J-wave intensity
  float hue;
  float sat;
  float rgbLevel;
  float white;
  float conveyorPhase;
};

struct RingReservoir {
  float charge[Config::kRingPixels];
  ColorF color[Config::kRingPixels];
  float brightness[Config::kRingPixels];
};

struct IngressCursor {
  float anchor;
  float offset;
  float velocity;
  float maxOffset;
  float polarity;
  uint16_t rng;
};

StableTrack gTrack = {false, false, false, 0.0f, 0.0f, 0u, 0.0f, 0u, 0u};
SceneEnvelope gScene = {0.0f, 0.0f, 0.0f, Config::kStillHue, Config::kBaseSaturation,
                        Config::kBaseRgbLevel, Config::kWhiteBase, 0.0f};

RingReservoir gFront;
RingReservoir gRear;

// One reusable scratch pass for reservoir diffusion. This is intentionally
// global rather than stack-local to keep Nano Every stack pressure low.
float gScratchCharge[Config::kRingPixels];
ColorF gScratchColor[Config::kRingPixels];

float gLeftBrightness[Config::kLeftJPixels];
float gRightBrightness[Config::kRightJPixels];

IngressCursor gLeftCursor = {Config::kLeftIngressAnchor, 0.0f, 0.0f,
                             Config::kIngressMaxOffsetPixels, 1.0f, 0x4A19u};
IngressCursor gRightCursor = {Config::kRightIngressAnchor, 0.0f, 0.0f,
                              Config::kIngressMaxOffsetPixels, -1.0f, 0xB357u};

bool gSensorReady = false;
uint32_t gLastInitAttemptMs = 0;
uint32_t gLastSensorPollMs = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastPrintMs = 0;

bool gHadMotionRangeSample = false;
float gPrevMotionRangeM = 0.0f;
uint32_t gPrevMotionRangeMs = 0;
float gMotionGoal = 0.0f;
float gSmoothedCharge = 0.0f;
float gStableCharge = 0.0f;
float gSmoothedIngressLevel = 0.0f;

// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------
float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float clampRange(float v, float lo, float hi) {
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

float wrapUnit(float v) {
  while (v < 0.0f) v += 1.0f;
  while (v >= 1.0f) v -= 1.0f;
  return v;
}

float wrapRing(float v) {
  const float n = float(Config::kRingPixels);
  while (v < 0.0f) v += n;
  while (v >= n) v -= n;
  return v;
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

float phaseDistance(float a, float b) {
  float d = absf(wrapUnit(a) - wrapUnit(b));
  if (d > 0.5f) d = 1.0f - d;
  return d;
}

float circularDistance(float a, float b, float count) {
  float d = absf(a - b);
  if (d > count * 0.5f) d = count - d;
  return d;
}

uint8_t toByte(float v) {
  return static_cast<uint8_t>(clamp01(v) * 255.0f);
}

float randomSignedUnit(uint16_t& state) {
  state = static_cast<uint16_t>((state * 2053u) + 13849u);
  const uint16_t bucket = state & 0x0FFFu;
  return (float(bucket) / 2047.5f) - 1.0f;
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
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  hsvToRgb(hue, sat, val, r, g, b);
  return makeColor(r / 255.0f, g / 255.0f, b / 255.0f, clamp01(white));
}

ColorF scaleColor(const ColorF& c, float s) {
  return makeColor(clamp01(c.r * s), clamp01(c.g * s), clamp01(c.b * s), clamp01(c.w * s));
}

ColorF addColor(const ColorF& a, const ColorF& b) {
  return makeColor(clamp01(a.r + b.r), clamp01(a.g + b.g), clamp01(a.b + b.b), clamp01(a.w + b.w));
}

ColorF lerpColor(const ColorF& a, const ColorF& b, float t) {
  return makeColor(lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t),
                   lerp(a.w, b.w, t));
}

float normalizeNearness(float rangeM) {
  const float span = Config::kFarM - Config::kNearM;
  if (span <= 0.001f) return 0.0f;
  const float t = clamp01((rangeM - Config::kNearM) / span);
  return 1.0f - t;
}

float compressEnergy(uint16_t energy) {
  return clamp01(float(energy) / Config::kEnergyReference);
}

ColorF currentSceneColor(float brightnessScale, bool rearSoftened) {
  float hue = gScene.hue;
  float sat = gScene.sat;
  float rgb = gScene.rgbLevel;
  float white = gScene.white;

  if (rearSoftened) {
    hue = lerp(hue, Config::kStillHue, Config::kRearHueSoftening);
    sat *= 0.84f;
    rgb *= 0.88f;
    white *= 1.16f;
  }

  return hsvColor(hue, sat, rgb * brightnessScale, white * brightnessScale);
}

// -----------------------------------------------------------------------------
// Sensor
// -----------------------------------------------------------------------------
bool initC4001() {
  if (Config::kSyntheticDemoMode) return false;
  if (!gC4001.begin()) return false;
  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(Config::kDetectRangeThreshold,
                        Config::kDetectEnergyThreshold,
                        Config::kDetectDelayThreshold);
  gC4001.setFrettingDetection(eON);
  return true;
}

bool syntheticTarget(uint32_t nowMs, int& targetNumber, float& rangeM, float& speedMps, uint16_t& energy) {
  const float t = float(nowMs) / 1000.0f;
  const float wave = 0.5f + 0.5f * sinf(t * 0.42f);
  const float wobble = 0.06f * sinf(t * 1.70f);
  rangeM = Config::kFarM - ((Config::kFarM - Config::kNearM) * wave) + wobble;
  rangeM = clampRange(rangeM, Config::kNearM, Config::kFarM);
  speedMps = -0.35f * cosf(t * 0.42f);
  energy = uint16_t(280.0f + (wave * 920.0f));
  targetNumber = 1;
  return true;
}

bool readC4001(uint32_t nowMs, int& targetNumber, float& rangeM, float& speedMps, uint16_t& energy) {
  if (Config::kSyntheticDemoMode) {
    return syntheticTarget(nowMs, targetNumber, rangeM, speedMps, energy);
  }

  if (!gSensorReady) {
    if (Config::kSyntheticIfSensorMissing) {
      return syntheticTarget(nowMs, targetNumber, rangeM, speedMps, energy);
    }
    return false;
  }

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
  gTrack.freshAccepted = false;

  if (!Config::kSyntheticDemoMode && !gSensorReady &&
      (gLastInitAttemptMs == 0 || (nowMs - gLastInitAttemptMs) >= 1000u)) {
    gLastInitAttemptMs = nowMs;
    gSensorReady = initC4001();
  }


  const bool dueForPoll = (gLastSensorPollMs == 0 ||
                           (nowMs - gLastSensorPollMs) >= Config::kSensorPollMs);
  if (dueForPoll) {
    gLastSensorPollMs = nowMs;

    int targetNumber = 0;
    float rawRange = 0.0f;
    float rawSpeed = 0.0f;
    uint16_t energy = 0;

    const bool ok = readC4001(nowMs, targetNumber, rawRange, rawSpeed, energy);
    gTrack.online = ok;

    if (ok && targetNumber > 0 && shouldAccept(rawRange, rawSpeed)) {
      gTrack.hasTrack = true;
      gTrack.freshAccepted = true;
      gTrack.rangeM = rawRange;
      gTrack.rawSpeedMps = rawSpeed;
      gTrack.energy = energy;
      gTrack.influence = 1.0f;
      gTrack.lastAcceptedMs = nowMs;
      gTrack.phase = 1;
      return;
    }
  }

  if (!Config::kSyntheticDemoMode && Config::kReinitSensorAfterLongDropout &&
      gSensorReady && gTrack.lastAcceptedMs > 0 &&
      (nowMs - gTrack.lastAcceptedMs) >= Config::kSensorDropoutReinitMs &&
      (nowMs - gLastInitAttemptMs) >= Config::kSensorReinitCooldownMs) {
    gLastInitAttemptMs = nowMs;
    gSensorReady = initC4001();
    if (Config::kDebugSerial) {
      Serial.println(gSensorReady ? "event=c4001_reinit_after_dropout"
                                  : "event=c4001_reinit_after_dropout_failed");
    }
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
  if (gTrack.freshAccepted) {
    float rawMotion = 0.0f;
    if (gHadMotionRangeSample && gPrevMotionRangeMs > 0) {
      const uint32_t dtMs = gTrack.lastAcceptedMs - gPrevMotionRangeMs;
      const float sampleDt = maxf(0.001f, float(dtMs) / 1000.0f);
      // Positive means approaching.
      const float velocityMps = (gPrevMotionRangeM - gTrack.rangeM) / sampleDt;
      if (absf(velocityMps) <= Config::kMotionStillMps) {
        rawMotion = 0.0f;
      } else {
        rawMotion = clampRange(velocityMps / Config::kMotionFullScaleMps, -1.0f, 1.0f);
      }
    }

    gPrevMotionRangeM = gTrack.rangeM;
    gPrevMotionRangeMs = gTrack.lastAcceptedMs;
    gHadMotionRangeSample = true;
    gMotionGoal = rawMotion;
  } else {
    gMotionGoal *= decayApprox(dtSec, Config::kMotionGoalDecaySec);
  }

  if (!gTrack.hasTrack) {
    gHadMotionRangeSample = false;
    gMotionGoal = 0.0f;
  }

  const float alpha = emaAlphaApprox(dtSec, Config::kMotionSmoothingSec);
  gScene.motion += (gMotionGoal - gScene.motion) * alpha;
  gScene.motion = clampRange(gScene.motion, -1.0f, 1.0f);
}

void computeSceneTargets(float& hue, float& sat, float& rgbLevel, float& whiteBase,
                         float& chargeTarget, float& ingressTarget) {
  if (!gTrack.hasTrack) {
    hue = Config::kStillHue;
    sat = 0.085f;
    rgbLevel = Config::kNoTrackRgbLevel;
    whiteBase = Config::kNoTrackWhite;
    chargeTarget = Config::kNoTrackLivingCharge;
    ingressTarget = Config::kNoTrackIngress;
    return;
  }

  const float rangeNear = normalizeNearness(gTrack.rangeM);
  const float energyNear = compressEnergy(gTrack.energy);
  const float nearness = clamp01(lerp(rangeNear, maxf(rangeNear, energyNear),
                                      Config::kEnergyChargeBlend) * gTrack.influence);
  const float approach = clamp01(maxf(0.0f, gScene.motion));
  const float retreat = clamp01(maxf(0.0f, -gScene.motion));
  const float motionAbs = clamp01(absf(gScene.motion));

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

void updateSmoothedScene(float dtSec) {
  updateMotionSignal(dtSec);

  float targetHue = Config::kStillHue;
  float targetSat = 0.0f;
  float targetRgbLevel = 0.0f;
  float targetWhite = 0.0f;
  float rawChargeTarget = 0.0f;
  float ingressTarget = 0.0f;

  computeSceneTargets(targetHue, targetSat, targetRgbLevel, targetWhite,
                      rawChargeTarget, ingressTarget);

  const float chargeAlpha = (rawChargeTarget >= gSmoothedCharge)
                                ? Config::kChargeRiseAlpha
                                : Config::kChargeFallAlpha;
  gSmoothedCharge += (rawChargeTarget - gSmoothedCharge) * chargeAlpha;
  gSmoothedCharge = clamp01(gSmoothedCharge);
  gStableCharge = applyDeadband(gStableCharge, gSmoothedCharge, Config::kChargeDeadband);
  gStableCharge = clamp01(gStableCharge);

  const float ingressAlpha = emaAlphaApprox(dtSec, Config::kIngressSmoothingSec);
  const float ingressGoal = clamp01(ingressTarget * (0.30f + (0.70f * gStableCharge)));
  gSmoothedIngressLevel += (ingressGoal - gSmoothedIngressLevel) * ingressAlpha;
  gSmoothedIngressLevel = clamp01(gSmoothedIngressLevel);

  gScene.charge = gStableCharge;
  gScene.ingressLevel = gSmoothedIngressLevel;
  gScene.hue = lerp(gScene.hue, targetHue, 0.20f);
  gScene.sat = lerp(gScene.sat, targetSat, 0.18f);
  gScene.rgbLevel = lerp(gScene.rgbLevel, targetRgbLevel, 0.18f);
  gScene.white = lerp(gScene.white, targetWhite, 0.18f);

  // Explicit old-style conveyor phase.
  const float travelSec = (Config::kIngressTravelSeconds < 0.01f)
                              ? 0.01f
                              : Config::kIngressTravelSeconds;
  gScene.conveyorPhase += dtSec / travelSec;
  gScene.conveyorPhase = wrapUnit(gScene.conveyorPhase);
}

float sampleJIngress(uint16_t pixel, uint16_t count, bool tipAtHighIndex, float phaseOffset) {
  if (count == 0) return 0.0f;

  const float denom = (count > 1) ? float(count - 1) : 1.0f;
  const float jPos = float(pixel) / denom;

  // phase 0 = tip, phase 1 = ring entry. The wrap makes this a continuous
  // receipt-ribbon loop rather than a hard-start/hard-stop packet.
  const float tipToEntry = tipAtHighIndex ? (1.0f - jPos) : jPos;
  const float phase = wrapUnit(gScene.conveyorPhase + phaseOffset);
  const float delta = phaseDistance(tipToEntry, phase);

  const float moving = polynomialKernel(delta, Config::kIngressConveyorWidth);
  const float floor = gScene.charge * Config::kIngressFloorFromCharge;
  return clamp01(floor + (moving * gScene.ingressLevel));
}

// -----------------------------------------------------------------------------
// Anchored plasma ingress
// -----------------------------------------------------------------------------
void updateIngressCursor(IngressCursor& cursor, float dtSec) {
  if (!Config::kEnableRovingIngress || cursor.maxOffset <= 0.001f) {
    cursor.offset = 0.0f;
    cursor.velocity = 0.0f;
    return;
  }

  const float jitter = randomSignedUnit(cursor.rng) * Config::kIngressJitterAccel;
  const float motionBias = gScene.motion * Config::kIngressMotionBias * cursor.polarity;
  cursor.velocity += (jitter + motionBias) * dtSec;

  const float damping = clamp01(1.0f - (Config::kIngressVelocityDamping * dtSec));
  cursor.velocity *= damping;
  cursor.velocity = clampRange(cursor.velocity,
                               -Config::kIngressMaxSpeed,
                               Config::kIngressMaxSpeed);

  cursor.offset += cursor.velocity * dtSec;

  if (cursor.offset > cursor.maxOffset) {
    cursor.offset = cursor.maxOffset;
    cursor.velocity *= -0.35f;
  } else if (cursor.offset < -cursor.maxOffset) {
    cursor.offset = -cursor.maxOffset;
    cursor.velocity *= -0.35f;
  }
}

float cursorPosition(const IngressCursor& cursor) {
  return wrapRing(cursor.anchor + cursor.offset);
}

float rearIngressPosition(float frontPos) {
  float p = frontPos;
  if (Config::kRearMirrorIngress) {
    p = float(Config::kRingPixels) - p;
  }
  p += Config::kRearIngressOffsetPixels;
  return wrapRing(p);
}

// -----------------------------------------------------------------------------
// Ring reservoirs
// -----------------------------------------------------------------------------
void clearReservoir(RingReservoir& reservoir) {
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    reservoir.charge[i] = 0.0f;
    reservoir.color[i] = makeColor();
    reservoir.brightness[i] = 0.0f;
  }
}

void clearSceneState() {
  clearReservoir(gFront);
  clearReservoir(gRear);
  for (uint16_t i = 0; i < Config::kLeftJPixels; ++i) gLeftBrightness[i] = 0.0f;
  for (uint16_t i = 0; i < Config::kRightJPixels; ++i) gRightBrightness[i] = 0.0f;
}

void updateReservoir(RingReservoir& reservoir, float dtSec, float clearSeconds,
                     float diffusionPerSecond) {
  const float decay = decayApprox(dtSec, clearSeconds);
  const float diffusion = diffusionPerSecond * dtSec;

  // v4.3: reservoir.color is normalized chroma memory, not additive light
  // energy. We diffuse color as charge-weighted energy and then divide by the
  // diffused charge. This keeps repeated red/green/blue deposits from turning
  // the two ingress pixels into white scars.
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (Config::kRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % Config::kRingPixels;

    const float selfCharge = reservoir.charge[i];
    const float leftCharge = reservoir.charge[left];
    const float rightCharge = reservoir.charge[right];

    float charge = selfCharge;
    charge += (leftCharge + rightCharge - (2.0f * selfCharge)) * diffusion;
    charge *= decay;
    gScratchCharge[i] = clamp01(charge);

    const ColorF c = reservoir.color[i];
    const ColorF l = reservoir.color[left];
    const ColorF r = reservoir.color[right];

    const float selfEnergyR = c.r * selfCharge;
    const float selfEnergyG = c.g * selfCharge;
    const float selfEnergyB = c.b * selfCharge;
    const float selfEnergyW = c.w * selfCharge;

    const float leftEnergyR = l.r * leftCharge;
    const float leftEnergyG = l.g * leftCharge;
    const float leftEnergyB = l.b * leftCharge;
    const float leftEnergyW = l.w * leftCharge;

    const float rightEnergyR = r.r * rightCharge;
    const float rightEnergyG = r.g * rightCharge;
    const float rightEnergyB = r.b * rightCharge;
    const float rightEnergyW = r.w * rightCharge;

    float energyR = selfEnergyR + ((leftEnergyR + rightEnergyR - (2.0f * selfEnergyR)) * diffusion);
    float energyG = selfEnergyG + ((leftEnergyG + rightEnergyG - (2.0f * selfEnergyG)) * diffusion);
    float energyB = selfEnergyB + ((leftEnergyB + rightEnergyB - (2.0f * selfEnergyB)) * diffusion);
    float energyW = selfEnergyW + ((leftEnergyW + rightEnergyW - (2.0f * selfEnergyW)) * diffusion);

    energyR *= decay;
    energyG *= decay;
    energyB *= decay;
    energyW *= decay;

    ColorF out;
    if (gScratchCharge[i] > 0.001f) {
      const float invCharge = 1.0f / gScratchCharge[i];
      out.r = clamp01(energyR * invCharge);
      out.g = clamp01(energyG * invCharge);
      out.b = clamp01(energyB * invCharge);
      out.w = clamp01(energyW * invCharge);
    } else {
      out = makeColor();
    }
    gScratchColor[i] = out;
  }

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    reservoir.charge[i] = gScratchCharge[i];
    reservoir.color[i] = gScratchColor[i];
  }
}

void addReservoirPixel(RingReservoir& reservoir, uint16_t i, const ColorF& color,
                       float amount) {
  if (i >= Config::kRingPixels || amount <= 0.0f) return;

  const float add = minf(amount, Config::kRingMaxPixelAddPerFrame);
  const float oldCharge = reservoir.charge[i];
  const float newCharge = clamp01(oldCharge + add);
  const float denom = oldCharge + add + 0.0001f;
  float mix = clamp01((add / denom) * Config::kRingColorAssimilation);
  if (oldCharge <= 0.001f) mix = 1.0f;

  reservoir.charge[i] = newCharge;
  reservoir.color[i] = lerpColor(reservoir.color[i], color, mix);
}

void injectReservoirField(RingReservoir& reservoir, float posA, float posB,
                          const ColorF& color, float amount, float spreadPixels,
                          float wholeFieldWeight, float anchorWeight) {
  if (amount <= 0.0f) return;

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const float distA = circularDistance(float(i), posA, float(Config::kRingPixels));
    const float distB = circularDistance(float(i), posB, float(Config::kRingPixels));
    const float wA = polynomialKernel(distA, spreadPixels);
    const float wB = polynomialKernel(distB, spreadPixels);

    // The whole-field term is the important bit: it keeps the ring from being
    // merely two bright wounds with dark quadrants between them.
    const float anchor = clamp01(wA + wB);
    const float weight = clamp01(wholeFieldWeight + (anchorWeight * anchor));
    addReservoirPixel(reservoir, i, color, amount * weight);
  }
}

void updateReservoirsAndIngress(float dtSec) {
  updateIngressCursor(gLeftCursor, dtSec);
  updateIngressCursor(gRightCursor, dtSec);

  updateReservoir(gFront, dtSec, Config::kFrontClearSeconds, Config::kFrontDiffusionPerSecond);
  updateReservoir(gRear, dtSec, Config::kRearClearSeconds, Config::kRearDiffusionPerSecond);

  const float arrival = polynomialKernel(phaseDistance(gScene.conveyorPhase, 0.0f),
                                         Config::kArrivalPulseWidth);
  const float arrivalBoost = 1.0f + (arrival * gScene.ingressLevel * Config::kArrivalInjectionBoost);
  const float frontInput = clamp01(gScene.charge) * dtSec * Config::kFrontInjectionGain * arrivalBoost;
  const float rearInput = clamp01(gScene.charge) * dtSec * Config::kRearInjectionGain * arrivalBoost;

  const float leftPos = cursorPosition(gLeftCursor);
  const float rightPos = cursorPosition(gRightCursor);

  ColorF frontColor = currentSceneColor(clamp01(0.72f + (gScene.charge * 0.28f)), false);
  ColorF rearColor = currentSceneColor(clamp01(0.62f + (gScene.charge * 0.24f)), true);

  // Chroma goes into the ring reservoir; white is added later during render.
  // This reduces the white-hot ingress scars while preserving smoke haze.
  frontColor.w *= 0.20f;
  rearColor.w *= 0.36f;

  injectReservoirField(gFront, leftPos, rightPos, frontColor, frontInput,
                       Config::kFrontIngressSpreadPixels,
                       Config::kFrontWholeFieldInjection,
                       Config::kFrontAnchorInjection);

  injectReservoirField(gRear, rearIngressPosition(leftPos), rearIngressPosition(rightPos),
                       rearColor, rearInput,
                       Config::kRearIngressSpreadPixels,
                       Config::kRearWholeFieldInjection,
                       Config::kRearAnchorInjection);
}

float sampleRingSurfaceWave(uint16_t i, bool rear) {
  if (!Config::kEnableRingSurfaceWave || Config::kRingPixels == 0) return 0.0f;

  const float pos = float(i) / float(Config::kRingPixels);
  const float rearOffset = rear ? 0.17f : 0.0f;
  const float phaseA = wrapUnit(gScene.conveyorPhase + rearOffset);
  const float phaseB = wrapUnit((1.0f - gScene.conveyorPhase) + 0.47f + rearOffset);

  const float a = polynomialKernel(phaseDistance(pos, phaseA), Config::kRingSurfaceWaveWidth);
  const float b = polynomialKernel(phaseDistance(pos, phaseB), Config::kRingSurfaceWaveSecondWidth);

  const float activity = clamp01((gScene.ingressLevel * 0.72f) + (gScene.charge * 0.28f));
  const float gain = rear ? Config::kRearSurfaceWaveGain : Config::kFrontSurfaceWaveGain;
  return clamp01(((a * 0.72f) + (b * 0.42f)) * activity * gain);
}

// -----------------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------------
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

void clearStrip() {
  for (uint16_t i = 0; i < Config::kTotalPixels; ++i) {
    setPixelRgbw(i, makeColor());
  }
}

void renderJ(const SegmentRange& seg, float* brightnessState, bool tipAtHighIndex,
             float phaseOffset) {
  for (uint16_t i = 0; i < seg.count; ++i) {
    const float ingress = sampleJIngress(i, seg.count, tipAtHighIndex, phaseOffset);
    const float targetBrightness = clamp01(
        (ingress * (0.45f + (0.55f * gScene.ingressLevel))) +
        (Config::kStamenAmbientFloor * maxf(0.25f, gScene.charge)));

    float smoothed = brightnessState[i] + ((targetBrightness - brightnessState[i]) * 0.22f);
    smoothed = applyDeadband(brightnessState[i], smoothed, 0.015f);
    brightnessState[i] = applyBrightnessSlew(brightnessState[i], smoothed,
                                             Config::kFrameMs / 1000.0f);

    ColorF c = currentSceneColor(clamp01(brightnessState[i] * Config::kStamenOutputScale), false);
    const float avg = (c.r + c.g + c.b) * 0.333f;
    c.w = clamp01(c.w + (avg * Config::kStamenWhiteGain));
    setPixelRgbw(segmentPixel(seg, i), c);
  }
}

void renderRing(RingReservoir& reservoir, const SegmentRange& seg, bool rear) {
  const float baseField = rear ? Config::kRearBaseFieldLevel : Config::kFrontBaseFieldLevel;
  const float instantGain = rear ? Config::kRearInstantGain : Config::kFrontInstantGain;
  const float outputScale = rear ? Config::kRearOutputScale : Config::kFrontOutputScale;
  const float whiteGain = rear ? Config::kRearWhiteGain : Config::kFrontWhiteGain;
  const float moodWashGain = rear ? Config::kRearMoodWashGain : Config::kFrontMoodWashGain;

  for (uint16_t i = 0; i < seg.count && i < Config::kRingPixels; ++i) {
    const float field = clamp01(baseField + reservoir.charge[i]);
    const float targetBrightness = clamp01((field * maxf(0.16f, gScene.charge)) +
                                           (gScene.charge * instantGain));
    float smoothed = reservoir.brightness[i] +
                     ((targetBrightness - reservoir.brightness[i]) * 0.24f);
    smoothed = applyDeadband(reservoir.brightness[i], smoothed, 0.015f);
    reservoir.brightness[i] = applyBrightnessSlew(reservoir.brightness[i], smoothed,
                                                  Config::kFrameMs / 1000.0f);

    const float memoryLevel = clamp01((reservoir.charge[i] * 0.72f) +
                                      (reservoir.brightness[i] * 0.55f));
    ColorF memory = scaleColor(reservoir.color[i], outputScale * memoryLevel);
    const float avg = (memory.r + memory.g + memory.b) * 0.333f;
    memory.w = clamp01((memory.w * (rear ? 0.65f : 0.35f)) +
                       (avg * whiteGain) +
                       (reservoir.charge[i] * 0.012f * outputScale));

    const float surfaceWave = sampleRingSurfaceWave(i, rear);
    if (surfaceWave > 0.0f) {
      ColorF waveColor = currentSceneColor(surfaceWave * (rear ? 1.08f : 1.0f), rear);
      waveColor.w = clamp01(waveColor.w * 0.25f + surfaceWave * Config::kRingSurfaceWaveWhiteGain);
      memory = addColor(memory, waveColor);
    }

    // Ring-wide atmospheric wash. This is not a travelling band. It is a dim
    // shared chroma floor so the un-fed quadrants do not collapse to black.
    ColorF moodWash = currentSceneColor(clamp01(moodWashGain * (0.20f + gScene.charge)), rear);
    moodWash.w *= rear ? 0.65f : 0.35f;

    const ColorF idle = hsvColor(Config::kStillHue,
                                 rear ? 0.035f : 0.045f,
                                 Config::kIdleRgbLevel * outputScale,
                                 Config::kIdleWhiteLevel * outputScale);
    ColorF out = addColor(addColor(idle, moodWash), memory);
    setPixelRgbw(segmentPixel(seg, i), out);
  }
}

void renderScene() {
  clearStrip();
  renderRing(gRear, rearRingSeg(), true);
  renderRing(gFront, frontRingSeg(), false);
  renderJ(leftJSeg(), gLeftBrightness, Config::kLeftTipAtHighIndex, Config::kLeftJPhaseOffset);
  renderJ(rightJSeg(), gRightBrightness, Config::kRightTipAtHighIndex, Config::kRightJPhaseOffset);
  gStrip.show();
}

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------
void printDebug(uint32_t nowMs) {
  if (!Config::kDebugSerial) return;
  if (nowMs - gLastPrintMs < Config::kPrintMs) return;
  gLastPrintMs = nowMs;

  Serial.print("online=");
  Serial.print(gTrack.online ? 1 : 0);
  Serial.print(" has=");
  Serial.print(gTrack.hasTrack ? 1 : 0);
  Serial.print(" fresh=");
  Serial.print(gTrack.freshAccepted ? 1 : 0);
  Serial.print(" phase=");
  Serial.print(gTrack.phase);
  Serial.print(" range_m=");
  Serial.print(gTrack.rangeM, 3);
  Serial.print(" raw_speed=");
  Serial.print(gTrack.rawSpeedMps, 3);
  Serial.print(" energy=");
  Serial.print(gTrack.energy);
  Serial.print(" infl=");
  Serial.print(gTrack.influence, 3);
  Serial.print(" motion=");
  Serial.print(gScene.motion, 3);
  Serial.print(" charge=");
  Serial.print(gScene.charge, 3);
  Serial.print(" ingress=");
  Serial.print(gScene.ingressLevel, 3);
  Serial.print(" wave=");
  Serial.print(gScene.conveyorPhase, 3);
  Serial.print(" Lpos=");
  Serial.print(cursorPosition(gLeftCursor), 2);
  Serial.print(" Rpos=");
  Serial.print(cursorPosition(gRightCursor), 2);
  Serial.print(" hue=");
  Serial.println(gScene.hue, 3);
}

}  // namespace

void setup() {
  if (Config::kDebugSerial) {
    Serial.begin(115200);
    delay(60);
  }

  Wire.begin();

  gStrip.begin();
  gStrip.setBrightness(Config::kGlobalBrightness);
  clearStrip();
  gStrip.show();

  clearSceneState();

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
  updateReservoirsAndIngress(dtSec);
  renderScene();
  printDebug(nowMs);
}
