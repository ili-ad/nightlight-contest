#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobot_C4001.h>

// -----------------------------------------------------------------------------
// Anthurium Lite Smoke v4.9
//
// Bench sketch. Standalone. No app/scene framework required.
//
// Behavioral inheritance from smoke v3:
// - Stable C4001 accept / hold / fade path.
// - Motion is primarily derived from range delta, not blindly from raw speed.
// - A shared scene envelope controls hue, charge, white haze, ingress level.
// - J-shapes are old-style travelling wave shaders, not literal packet queues.
// - Rings are reservoirs: v3-style additive color energy, diffusion, and decay.
// - v4.7 adds sampled front-ring RGBW logging and a small 2-pixel clockwise anchor trim.
//
// v4 changes:
// - Real-ish bench topology: front ring 44, rear ring 44, left J 12, right J 12.
// - Two reservoirs: front is sharper, rear is softer/dimmer/wall-wash-like.
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
constexpr uint8_t kGlobalBrightness = 60;
constexpr uint32_t kFrameMs = 16;       // ~60 fps target
constexpr uint32_t kSensorPollMs = 60;  // C4001 path is slower than render path
constexpr uint32_t kPrintMs = 250;
constexpr bool kRingColorDebug = true;
constexpr bool kRingColorFullDump = false;
constexpr bool kLogRearRingColors = false;
constexpr uint32_t kRingSummaryPrintMs = 500;
constexpr uint32_t kRingFullDumpPrintMs = 1000;
constexpr uint8_t kRingSummarySampleStride = 4;
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
constexpr float kEnergyChargeBlend = 0.06f;  // range dominates; energy is too spiky for reservoir fill

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

// Front ring: charge lingers for a few seconds; color drains a little faster
// so new red/blue/green can overtake old memory instead of averaging to white.
constexpr float kFrontClearSeconds = 5.2f;       // charge reservoir time constant
constexpr float kFrontColorClearSeconds = 4.4f;  // chroma time constant
constexpr float kFrontDiffusionPerSecond = 0.60f;
constexpr float kFrontColorDiffusionPerSecond = 0.70f;
constexpr float kFrontInjectionGain = 0.35f;
constexpr float kFrontInstantGain = 0.030f;
constexpr float kFrontBaseFieldLevel = 0.060f;
constexpr float kFrontIngressSpreadPixels = 13.5f;  // broad enough to close the loop without adding fake inlets
constexpr float kFrontOutputScale = 1.60f;
constexpr float kFrontWhiteGain = 0.040f;

// Rear ring: longer-memory and wider for wall wash. It is intentionally lazier
// than the front, but still under budget.
constexpr float kRearClearSeconds = 6.4f;
constexpr float kRearColorClearSeconds = 5.4f;
constexpr float kRearDiffusionPerSecond = 0.50f;
constexpr float kRearColorDiffusionPerSecond = 0.56f;
constexpr float kRearInjectionGain = 0.28f;
constexpr float kRearInstantGain = 0.016f;
constexpr float kRearBaseFieldLevel = 0.046f;
constexpr float kRearIngressSpreadPixels = 8.5f;
constexpr float kRearOutputScale = 0.94f;
constexpr float kRearWhiteGain = 0.14f;
constexpr float kRearHueSoftening = 0.10f;  // pull rear slightly toward still hue
constexpr float kRearIngressOffsetPixels = 0.0f;
constexpr bool kRearMirrorIngress = true;

// v4.4 ring plumbing: memory is local dye, not a whole-ring faucet. The mood
// wash below is render-only and cannot fill the reservoir. Incoming dye also
// displaces a little stale dye locally, which prevents long red+green+blue
// histories from becoming chalky white.
constexpr float kRingMaxPixelAddPerFrame = 0.010f;
constexpr float kRingColorFlushPerAdd = 1.65f;
constexpr float kFrontMoodWashGain = 0.038f;
constexpr float kRearMoodWashGain = 0.034f;

// Front-ring palette rescue. The reservoir is now behaving, but the log/photo
// showed a long-lived pale green-yellow band that reads like a bug lamp. v4.9
// narrows this band and explicitly protects retreat/cyan moments. This remains
// render-only: it does not alter the stored reservoir or the spadices.
constexpr bool kEnableFrontBugLampRescue = true;
constexpr float kBugLampRescueStrength = 0.90f;
constexpr float kBugLampMinRedOverGreen = 0.72f;
constexpr float kBugLampMaxRedOverGreen = 1.12f;
constexpr float kBugLampMaxBlueOverGreen = 0.58f;
constexpr float kBugLampMinVisible = 0.010f;
constexpr float kWarmWhiteRescueGain = 1.16f;
constexpr float kWarmWhiteRescueR = 1.00f;
constexpr float kWarmWhiteRescueG = 0.46f;
constexpr float kWarmWhiteRescueB = 0.12f;
constexpr float kWarmWhiteRescueW = 0.72f;

// Front-ring render-only color grade. This is deliberately not a reservoir
// change: it raises chroma, especially red, without filling the W channel.
constexpr float kFrontRedChannelGain = 1.34f;
constexpr float kFrontGreenChannelGain = 1.04f;
constexpr float kFrontBlueChannelGain = 1.14f;
constexpr float kFrontWhiteChannelGain = 0.90f;

// The yellow rescue worked, but the logs show the front ring often stayed in
// green buckets even while the scene hue visited the retreat/cyan side. Add a
// small render-only cool lift when hue/motion are genuinely cool, before the
// bug-lamp rescue runs. This restores cyan/teal without touching the spadices.
constexpr bool kEnableFrontCoolLift = true;
constexpr float kFrontCoolHueStart = 0.36f;
constexpr float kFrontCoolHueFull = 0.48f;
constexpr float kFrontCoolMotionStart = 0.18f;
constexpr float kFrontCoolLiftStrength = 0.74f;
constexpr float kFrontCoolGreenGain = 0.34f;
constexpr float kFrontCoolBlueGain = 1.10f;
constexpr float kFrontCoolRedDampen = 0.16f;
constexpr float kFrontCoolMinLevel = 0.018f;

// Ring-wide travelling shimmer remains disabled. It produced the radar / choo-choo
// band. Keep the rings as reservoirs for this pass.
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
constexpr float kLeftIngressAnchor = 43.5f;  // v4.9: 2 px counterclockwise trim from v4.8
constexpr float kRightIngressAnchor = 21.5f; // opposite anchor, same trim

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

struct Rgbw8 {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;
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

// Last rendered byte-level ring output. Logging this tells us what the viewer
// actually sees after reservoir, mood wash, white haze, and brightness smoothing
// are combined.
Rgbw8 gFrontRendered[Config::kRingPixels];
Rgbw8 gRearRendered[Config::kRingPixels];
uint8_t gFrontYellowRescue[Config::kRingPixels];
uint8_t gRearYellowRescue[Config::kRingPixels];

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
uint32_t gLastRingSummaryPrintMs = 0;
uint32_t gLastRingFullDumpPrintMs = 0;

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

Rgbw8 toRgbw8(const ColorF& c) {
  Rgbw8 out;
  out.r = toByte(c.r);
  out.g = toByte(c.g);
  out.b = toByte(c.b);
  out.w = toByte(c.w);
  return out;
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

ColorF applyFrontRingColorGrade(const ColorF& c) {
  return makeColor(
      clamp01(c.r * Config::kFrontRedChannelGain),
      clamp01(c.g * Config::kFrontGreenChannelGain),
      clamp01(c.b * Config::kFrontBlueChannelGain),
      clamp01(c.w * Config::kFrontWhiteChannelGain));
}

ColorF liftFrontCoolColor(const ColorF& c, float& coolAmount) {
  coolAmount = 0.0f;
  if (!Config::kEnableFrontCoolLift) return c;

  const float hueSpan = maxf(0.001f, Config::kFrontCoolHueFull - Config::kFrontCoolHueStart);
  const float hueCool = clamp01((gScene.hue - Config::kFrontCoolHueStart) / hueSpan);
  const float motionCool = clamp01((-gScene.motion - Config::kFrontCoolMotionStart) /
                                   maxf(0.001f, 1.0f - Config::kFrontCoolMotionStart));
  const float cool = clamp01(maxf(hueCool * 0.78f, motionCool));
  if (cool <= 0.010f) return c;

  const float visible = maxf(c.r, maxf(c.g, c.b));
  const float level = maxf(Config::kFrontCoolMinLevel, visible) *
                      Config::kFrontCoolLiftStrength * cool;

  ColorF out = c;
  out.r = clamp01(out.r * (1.0f - (Config::kFrontCoolRedDampen * cool)));
  out.g = clamp01(out.g + (level * Config::kFrontCoolGreenGain));
  out.b = clamp01(out.b + (level * Config::kFrontCoolBlueGain));
  coolAmount = cool;
  return out;
}

ColorF rescueBugLampYellow(const ColorF& c, float& rescueAmount) {
  rescueAmount = 0.0f;
  if (!Config::kEnableFrontBugLampRescue) return c;

  const float r = c.r;
  const float g = c.g;
  const float b = c.b;
  const float visible = maxf(r, maxf(g, b));
  if (visible < Config::kBugLampMinVisible || g <= 0.001f) return c;

  // Do not warm away retreat/cyan moments. The yellow rescue is for the
  // still/near yellow-green pocket, not the cool side of the palette.
  if (Config::kEnableFrontCoolLift &&
      (gScene.hue >= Config::kFrontCoolHueStart ||
       gScene.motion <= -Config::kFrontCoolMotionStart)) {
    return c;
  }

  const float redOverGreen = r / g;
  const bool redGreenBand =
      redOverGreen >= Config::kBugLampMinRedOverGreen &&
      redOverGreen <= Config::kBugLampMaxRedOverGreen;
  const bool blueIsWeak = b <= (g * Config::kBugLampMaxBlueOverGreen);

  // Do not touch true green, orange/red approach, blue/cyan retreat, or violet.
  // This specifically catches the pale R+G / weak-B band seen in the logs.
  if (!redGreenBand || !blueIsWeak) return c;

  const float targetLevel = visible * Config::kWarmWhiteRescueGain;
  const ColorF warmWhite = makeColor(
      clamp01(targetLevel * Config::kWarmWhiteRescueR),
      clamp01(targetLevel * Config::kWarmWhiteRescueG),
      clamp01(targetLevel * Config::kWarmWhiteRescueB),
      clamp01(targetLevel * Config::kWarmWhiteRescueW));

  rescueAmount = Config::kBugLampRescueStrength;
  return lerpColor(c, warmWhite, Config::kBugLampRescueStrength);
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
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    gFrontRendered[i] = {0, 0, 0, 0};
    gRearRendered[i] = {0, 0, 0, 0};
  }
  for (uint16_t i = 0; i < Config::kLeftJPixels; ++i) gLeftBrightness[i] = 0.0f;
  for (uint16_t i = 0; i < Config::kRightJPixels; ++i) gRightBrightness[i] = 0.0f;
}

void updateReservoir(RingReservoir& reservoir, float dtSec,
                     float chargeClearSeconds, float colorClearSeconds,
                     float chargeDiffusionPerSecond, float colorDiffusionPerSecond) {
  const float chargeDecay = decayApprox(dtSec, chargeClearSeconds);
  const float colorDecay = decayApprox(dtSec, colorClearSeconds);
  const float chargeDiffusion = chargeDiffusionPerSecond * dtSec;
  const float colorDiffusion = colorDiffusionPerSecond * dtSec;

  // v4.4: v3-style additive color-energy reservoir. Charge and color both
  // diffuse and drain, but front color drains slightly faster than charge. That
  // gives new hue authority while preserving a visible memory field.
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (Config::kRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % Config::kRingPixels;

    float charge = reservoir.charge[i];
    charge += (reservoir.charge[left] + reservoir.charge[right] -
               (2.0f * reservoir.charge[i])) * chargeDiffusion;
    charge *= chargeDecay;
    gScratchCharge[i] = clamp01(charge);

    const ColorF c = reservoir.color[i];
    const ColorF l = reservoir.color[left];
    const ColorF r = reservoir.color[right];

    ColorF out;
    out.r = clamp01((c.r + ((l.r + r.r - (2.0f * c.r)) * colorDiffusion)) * colorDecay);
    out.g = clamp01((c.g + ((l.g + r.g - (2.0f * c.g)) * colorDiffusion)) * colorDecay);
    out.b = clamp01((c.b + ((l.b + r.b - (2.0f * c.b)) * colorDiffusion)) * colorDecay);
    out.w = clamp01((c.w + ((l.w + r.w - (2.0f * c.w)) * colorDiffusion)) * colorDecay);
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

  // Incoming dye displaces a little stale dye before it adds its own color.
  // Charge can linger while chroma turns over more quickly.
  const float flush = clamp01(add * Config::kRingColorFlushPerAdd);
  reservoir.color[i].r *= (1.0f - flush);
  reservoir.color[i].g *= (1.0f - flush);
  reservoir.color[i].b *= (1.0f - flush);
  reservoir.color[i].w *= (1.0f - flush);

  reservoir.charge[i] = clamp01(reservoir.charge[i] + add);
  reservoir.color[i].r = clamp01(reservoir.color[i].r + (color.r * add));
  reservoir.color[i].g = clamp01(reservoir.color[i].g + (color.g * add));
  reservoir.color[i].b = clamp01(reservoir.color[i].b + (color.b * add));
  reservoir.color[i].w = clamp01(reservoir.color[i].w + (color.w * add));
}

void injectReservoirLocal(RingReservoir& reservoir, float posA, float posB,
                          const ColorF& color, float amount, float spreadPixels) {
  if (amount <= 0.0f) return;

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const float distA = circularDistance(float(i), posA, float(Config::kRingPixels));
    const float distB = circularDistance(float(i), posB, float(Config::kRingPixels));
    const float wA = polynomialKernel(distA, spreadPixels);
    const float wB = polynomialKernel(distB, spreadPixels);
    const float weight = clamp01(wA + wB);
    if (weight <= 0.0f) continue;

    addReservoirPixel(reservoir, i, color, amount * weight);
  }
}

void updateReservoirsAndIngress(float dtSec) {
  updateIngressCursor(gLeftCursor, dtSec);
  updateIngressCursor(gRightCursor, dtSec);

  updateReservoir(gFront, dtSec,
                  Config::kFrontClearSeconds,
                  Config::kFrontColorClearSeconds,
                  Config::kFrontDiffusionPerSecond,
                  Config::kFrontColorDiffusionPerSecond);
  updateReservoir(gRear, dtSec,
                  Config::kRearClearSeconds,
                  Config::kRearColorClearSeconds,
                  Config::kRearDiffusionPerSecond,
                  Config::kRearColorDiffusionPerSecond);

  const float arrival = polynomialKernel(phaseDistance(gScene.conveyorPhase, 0.0f),
                                         Config::kArrivalPulseWidth);
  const float arrivalBoost = 1.0f + (arrival * gScene.ingressLevel * Config::kArrivalInjectionBoost);
  const float frontInput = clamp01(gScene.charge) * dtSec * Config::kFrontInjectionGain * arrivalBoost;
  const float rearInput = clamp01(gScene.charge) * dtSec * Config::kRearInjectionGain * arrivalBoost;

  const float leftPos = cursorPosition(gLeftCursor);
  const float rightPos = cursorPosition(gRightCursor);

  ColorF frontColor = currentSceneColor(clamp01(0.72f + (gScene.charge * 0.28f)), false);
  ColorF rearColor = currentSceneColor(clamp01(0.62f + (gScene.charge * 0.24f)), true);

  // Store chroma, not haze. White smoke is added during rendering so it cannot
  // accumulate into a ghost-white reservoir.
  frontColor.w = 0.0f;
  rearColor.w *= 0.08f;

  injectReservoirLocal(gFront, leftPos, rightPos, frontColor, frontInput,
                       Config::kFrontIngressSpreadPixels);

  injectReservoirLocal(gRear, rearIngressPosition(leftPos), rearIngressPosition(rightPos),
                       rearColor, rearInput,
                       Config::kRearIngressSpreadPixels);
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
    const float targetBrightness = clamp01((field * maxf(rear ? 0.16f : 0.22f, gScene.charge)) +
                                           (gScene.charge * instantGain));
    float smoothed = reservoir.brightness[i] +
                     ((targetBrightness - reservoir.brightness[i]) * 0.24f);
    smoothed = applyDeadband(reservoir.brightness[i], smoothed, 0.015f);
    reservoir.brightness[i] = applyBrightnessSlew(reservoir.brightness[i], smoothed,
                                                  Config::kFrameMs / 1000.0f);

    ColorF memory = scaleColor(reservoir.color[i], outputScale * clamp01((rear ? 0.30f : 0.38f) + reservoir.brightness[i]));
    const float avg = (memory.r + memory.g + memory.b) * 0.333f;
    memory.w = clamp01((memory.w * (rear ? 0.35f : 0.10f)) +
                       (avg * whiteGain) +
                       (reservoir.charge[i] * (rear ? 0.007f : 0.004f) * outputScale));

    const float surfaceWave = sampleRingSurfaceWave(i, rear);
    if (surfaceWave > 0.0f) {
      ColorF waveColor = currentSceneColor(surfaceWave * (rear ? 1.08f : 1.0f), rear);
      waveColor.w = clamp01(waveColor.w * 0.25f + surfaceWave * Config::kRingSurfaceWaveWhiteGain);
      memory = addColor(memory, waveColor);
    }

    // Ring-wide atmospheric wash. This is not a travelling band. It is a dim
    // shared chroma floor so the un-fed quadrants do not collapse to black.
    ColorF moodWash = currentSceneColor(clamp01(moodWashGain * (0.20f + gScene.charge)), rear);
    moodWash.w *= rear ? 0.30f : 0.08f;

    const ColorF idle = hsvColor(Config::kStillHue,
                                 rear ? 0.035f : 0.045f,
                                 Config::kIdleRgbLevel * outputScale,
                                 Config::kIdleWhiteLevel * outputScale);
    ColorF out = addColor(addColor(idle, moodWash), memory);
    float rescueAmount = 0.0f;
    if (!rear) {
      out = applyFrontRingColorGrade(out);
      float coolAmount = 0.0f;
      out = liftFrontCoolColor(out, coolAmount);
      out = rescueBugLampYellow(out, rescueAmount);
    }

    if (rear) {
      gRearRendered[i] = toRgbw8(out);
      gRearYellowRescue[i] = 0;
    } else {
      gFrontRendered[i] = toRgbw8(out);
      gFrontYellowRescue[i] = toByte(rescueAmount);
    }
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
void printHex2(uint8_t v) {
  if (v < 16) Serial.print('0');
  Serial.print(static_cast<unsigned int>(v), HEX);
}

void printRgbwHex(const Rgbw8& c) {
  printHex2(c.r);
  printHex2(c.g);
  printHex2(c.b);
  printHex2(c.w);
}

char classifyRgbw(const Rgbw8& c) {
  const uint16_t rgbSum = uint16_t(c.r) + uint16_t(c.g) + uint16_t(c.b);
  const uint8_t rgbMax = max(c.r, max(c.g, c.b));
  const uint8_t rgbMin = min(c.r, min(c.g, c.b));

  if (rgbSum + uint16_t(c.w) < 18) return 'd';  // dark / nearly idle

  // White / warm-white-ish: W is active or RGB is low-saturation.
  if (c.w > 18 && c.w >= (rgbMax / 2)) return 'w';
  if (rgbMax > 24 && (uint16_t(rgbMax) - uint16_t(rgbMin)) < (uint16_t(rgbMax) / 4)) return 'w';
  if (c.w >= 4 && c.r >= c.g && c.g >= c.b && c.b * 3 <= c.r * 2) return 'w';

  // Yellow/bug-lamp territory: red + green, weak blue. Thresholds are low
  // because this bench intentionally runs at restrained brightness.
  if (c.r >= 3 && c.g >= 3 && (uint16_t(c.b) * 2u + 1u) < min(c.r, c.g) &&
      uint16_t(c.g) * 100u >= uint16_t(c.r) * 86u &&
      uint16_t(c.r) * 100u >= uint16_t(c.g) * 48u) {
    return 'y';
  }

  // Orange/amber: red leads green, blue is weak.
  if (c.r >= 5 && c.g >= 3 && c.r > c.g && (uint16_t(c.b) * 2u + 1u) < c.r) return 'o';

  // Cyan / teal: green and blue both active, red not dominant. This is useful
  // for validating that retreat/cool information is making it back onto O1.
  if (c.g >= 3 && c.b >= 3 && c.g >= c.r &&
      uint16_t(c.b) * 100u >= uint16_t(c.g) * 45u &&
      uint16_t(c.r) * 100u <= uint16_t(c.g) * 85u) {
    return 'c';
  }

  if (c.r > c.g && c.r > c.b) return 'r';
  if (c.g > c.r && c.g > c.b) return 'g';
  if (c.b > c.r && c.b > c.g) return 'b';
  return 'm';  // mixed / ambiguous
}

void printRingSummaryLine(const char* label, const Rgbw8* pix,
                          const uint8_t* rescue, const RingReservoir& reservoir,
                          uint32_t nowMs) {
  uint32_t sumR = 0;
  uint32_t sumG = 0;
  uint32_t sumB = 0;
  uint32_t sumW = 0;
  float sumCharge = 0.0f;
  float maxCharge = 0.0f;
  uint16_t maxLuma = 0;
  uint8_t maxPix = 0;
  uint32_t sumRescue = 0;
  uint8_t maxRescue = 0;
  uint8_t rescuePixels = 0;

  uint8_t countDark = 0;
  uint8_t countYellow = 0;
  uint8_t countOrange = 0;
  uint8_t countRed = 0;
  uint8_t countGreen = 0;
  uint8_t countCyan = 0;
  uint8_t countBlue = 0;
  uint8_t countWhite = 0;
  uint8_t countMixed = 0;

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const Rgbw8 c = pix[i];
    sumR += c.r;
    sumG += c.g;
    sumB += c.b;
    sumW += c.w;
    sumCharge += reservoir.charge[i];
    if (reservoir.charge[i] > maxCharge) maxCharge = reservoir.charge[i];

    const uint16_t luma = uint16_t(c.r) + uint16_t(c.g) + uint16_t(c.b) + (uint16_t(c.w) * 2);
    if (luma > maxLuma) {
      maxLuma = luma;
      maxPix = i;
    }

    const uint8_t rescueHere = rescue ? rescue[i] : 0;
    sumRescue += rescueHere;
    if (rescueHere > maxRescue) maxRescue = rescueHere;
    if (rescueHere > 0) ++rescuePixels;

    const char bucket = classifyRgbw(c);
    if (bucket == 'd') ++countDark;
    else if (bucket == 'y') ++countYellow;
    else if (bucket == 'o') ++countOrange;
    else if (bucket == 'r') ++countRed;
    else if (bucket == 'g') ++countGreen;
    else if (bucket == 'c') ++countCyan;
    else if (bucket == 'b') ++countBlue;
    else if (bucket == 'w') ++countWhite;
    else ++countMixed;
  }

  Serial.print("ring_summary t=");
  Serial.print(nowMs);
  Serial.print(" side=");
  Serial.print(label);
  Serial.print(" avg_rgbw=");
  Serial.print(sumR / Config::kRingPixels);
  Serial.print(',');
  Serial.print(sumG / Config::kRingPixels);
  Serial.print(',');
  Serial.print(sumB / Config::kRingPixels);
  Serial.print(',');
  Serial.print(sumW / Config::kRingPixels);
  Serial.print(" charge_avg=");
  Serial.print(sumCharge / float(Config::kRingPixels), 3);
  Serial.print(" charge_max=");
  Serial.print(maxCharge, 3);
  Serial.print(" max_pix=");
  Serial.print(maxPix);
  Serial.print(" max_luma=");
  Serial.print(maxLuma);
  Serial.print(" rescue_avg=");
  Serial.print(sumRescue / (255.0f * float(Config::kRingPixels)), 3);
  Serial.print(" rescue_max=");
  Serial.print(maxRescue / 255.0f, 3);
  Serial.print(" rescue_px=");
  Serial.print(rescuePixels);
  Serial.print(" buckets=");
  Serial.print("d:");
  Serial.print(countDark);
  Serial.print(",y:");
  Serial.print(countYellow);
  Serial.print(",o:");
  Serial.print(countOrange);
  Serial.print(",r:");
  Serial.print(countRed);
  Serial.print(",g:");
  Serial.print(countGreen);
  Serial.print(",c:");
  Serial.print(countCyan);
  Serial.print(",b:");
  Serial.print(countBlue);
  Serial.print(",w:");
  Serial.print(countWhite);
  Serial.print(",m:");
  Serial.print(countMixed);

  Serial.print(" samples=");
  for (uint16_t i = 0; i < Config::kRingPixels; i += Config::kRingSummarySampleStride) {
    if (i > 0) Serial.print('|');
    if (i < 10) Serial.print('0');
    Serial.print(i);
    Serial.print(':');
    printRgbwHex(pix[i]);
  }
  Serial.println();
}

void printRingFullDumpLine(const char* label, const Rgbw8* pix, uint32_t nowMs) {
  Serial.print("ring44 t=");
  Serial.print(nowMs);
  Serial.print(" side=");
  Serial.print(label);
  Serial.print(" rgbw_hex=");
  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    if (i > 0) Serial.print(',');
    printRgbwHex(pix[i]);
  }
  Serial.println();
}

void printRingDebug(uint32_t nowMs) {
  if (!Config::kRingColorDebug) return;

  if (nowMs - gLastRingSummaryPrintMs >= Config::kRingSummaryPrintMs) {
    gLastRingSummaryPrintMs = nowMs;
    printRingSummaryLine("front", gFrontRendered, gFrontYellowRescue, gFront, nowMs);
    if (Config::kLogRearRingColors) {
      printRingSummaryLine("rear", gRearRendered, gRearYellowRescue, gRear, nowMs);
    }
  }

  if (Config::kRingColorFullDump &&
      nowMs - gLastRingFullDumpPrintMs >= Config::kRingFullDumpPrintMs) {
    gLastRingFullDumpPrintMs = nowMs;
    printRingFullDumpLine("front", gFrontRendered, nowMs);
    if (Config::kLogRearRingColors) {
      printRingFullDumpLine("rear", gRearRendered, nowMs);
    }
  }
}

void printDebug(uint32_t nowMs) {
  if (!Config::kDebugSerial) return;

  if (nowMs - gLastPrintMs >= Config::kPrintMs) {
    gLastPrintMs = nowMs;

    Serial.print("scene online=");
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

  printRingDebug(nowMs);
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
