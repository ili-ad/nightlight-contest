#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <Adafruit_NeoPixel.h>
#include <DFRobot_C4001.h>

// -----------------------------------------------------------------------------
// Anthurium Lite Smoke v2
// Standalone bench sketch.
//
// Goals:
// - Keep the stable, non-blinking C4001 behavior from the earlier smoke sketch
// - Restore true "conveyor belt" stamens
// - Make the torus remember COLOR history, not just heat
// - No ambient/day logic, no clap, no external repo includes
//
// Visual contract:
// - approaching: warm / red cast
// - retreating: cool / blue cast
// - mostly still: green
// - stamens: fast conveyor / antennae
// - torus: slower reservoir with ~3-6 second memory
// -----------------------------------------------------------------------------

namespace Config {
// Hardware
constexpr uint8_t kLedPin = 6;
constexpr uint8_t kC4001Addr = 0x2B;
constexpr bool kRgbwStrip = true;

// Topology (ring first, then left stamen, then right stamen)
constexpr uint16_t kRingPixels = 45;
constexpr uint16_t kLeftStamenPixels = 16;
constexpr uint16_t kRightStamenPixels = 16;
constexpr uint16_t kTotalPixels = kRingPixels + kLeftStamenPixels + kRightStamenPixels;

// Strip / frame
constexpr uint8_t kGlobalBrightness = 42;
constexpr uint32_t kFrameMs = 16;
constexpr uint32_t kPrintMs = 80;

// Sensor corridor
constexpr float kNearM = 0.20f;
constexpr float kFarM = 2.20f;
constexpr float kMaxAcceptedSpeedMps = 2.60f;

// Stable track hold / fade
constexpr uint32_t kHoldMs = 450;
constexpr uint32_t kFadeMs = 1500;

// Motion interpretation from range delta
constexpr float kMotionStillMps = 0.05f;
constexpr float kMotionFullScaleMps = 0.45f;
constexpr float kMotionAlpha = 0.22f;
constexpr float kTipColorAlpha = 0.28f;

// Conveyor / reservoir timing
constexpr float kConveyorPixelsPerSecond = 8.0f;   // ~2 sec from tip to ring across 16 px
constexpr float kConveyorFadePerSecond = 0.18f;    // mild fading while traveling
constexpr float kTorusClearSeconds = 4.2f;         // bathtub memory target
constexpr float kTorusDiffusionPerSecond = 0.38f;
constexpr float kReservoirInjectionGain = 0.95f;
constexpr float kIngressSpreadPixels = 3.6f;

// Brightness / white
constexpr float kTipBrightnessMin = 0.07f;
constexpr float kTipBrightnessMax = 0.62f;
constexpr float kTorusWhiteGain = 0.18f;
constexpr float kStamenWhiteGain = 0.12f;

// If your physical stamens run the other direction, flip this.
// true  => tip is highest index, ring root is index 0
// false => tip is index 0, ring root is highest index
constexpr bool kTipAtHighIndex = true;

// Ring ingress points, borrowed from the archived piece
constexpr uint16_t kIngressA = 2;
constexpr uint16_t kIngressB = (kRingPixels / 2) + 2;
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
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float w = 0.0f;
};

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
bool gSensorReady = false;
uint32_t gLastInitAttemptMs = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastPrintMs = 0;

// Motion / tip state
bool gHadRangeSample = false;
float gPrevAcceptedRangeM = 0.0f;
float gMotionSignal = 0.0f;  // -1..1, negative retreat, positive approach
ColorF gCurrentTipColor;
float gConveyorPhase = 0.0f;

// Scene buffers
ColorF gLeftConveyor[Config::kLeftStamenPixels];
ColorF gRightConveyor[Config::kRightStamenPixels];
ColorF gTorus[Config::kRingPixels];

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

ColorF lerpColor(const ColorF& a, const ColorF& b, float t) {
  ColorF out;
  out.r = lerp(a.r, b.r, t);
  out.g = lerp(a.g, b.g, t);
  out.b = lerp(a.b, b.b, t);
  out.w = lerp(a.w, b.w, t);
  return out;
}

ColorF scaleColor(const ColorF& c, float s) {
  ColorF out;
  out.r = c.r * s;
  out.g = c.g * s;
  out.b = c.b * s;
  out.w = c.w * s;
  return out;
}

ColorF addColor(const ColorF& a, const ColorF& b) {
  ColorF out;
  out.r = clamp01(a.r + b.r);
  out.g = clamp01(a.g + b.g);
  out.b = clamp01(a.b + b.b);
  out.w = clamp01(a.w + b.w);
  return out;
}

void decayColor(ColorF& c, float factor) {
  c.r *= factor;
  c.g *= factor;
  c.b *= factor;
  c.w *= factor;
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

ColorF hsvColor(float hue, float sat, float val, float white) {
  uint8_t r = 0, g = 0, b = 0;
  hsvToRgb(hue, sat, val, r, g, b);
  ColorF out;
  out.r = r / 255.0f;
  out.g = g / 255.0f;
  out.b = b / 255.0f;
  out.w = clamp01(white);
  return out;
}

float normalizeNearness(float rangeM) {
  const float span = Config::kFarM - Config::kNearM;
  if (span <= 0.001f) return 0.0f;
  const float t = clamp01((rangeM - Config::kNearM) / span);
  return 1.0f - t;
}

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
  if (fabsf(speedMps) > Config::kMaxAcceptedSpeedMps) return false;
  return true;
}

void updateStableTrack(uint32_t nowMs) {
  if (!gSensorReady && (gLastInitAttemptMs == 0 || (nowMs - gLastInitAttemptMs) >= 1000)) {
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

void updateMotionSignal(float dtSec) {
  float rawMotion = 0.0f;

  if (gTrack.hasTrack && dtSec > 0.0001f) {
    if (gHadRangeSample) {
      // Positive => approaching, negative => retreating.
      const float velocityMps = (gPrevAcceptedRangeM - gTrack.rangeM) / dtSec;
      if (fabsf(velocityMps) <= Config::kMotionStillMps) {
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

ColorF desiredTipColor() {
  if (!gTrack.hasTrack) {
    return ColorF{};
  }

  const float nearness = normalizeNearness(gTrack.rangeM);
  const float motionAbs = clamp01(fabsf(gMotionSignal));

  float hue = 0.33f;  // green when still
  if (gMotionSignal > 0.0f) {
    // approaching => warm / red
    hue = lerp(0.33f, 0.01f, motionAbs);
  } else if (gMotionSignal < 0.0f) {
    // retreating => cool / blue
    hue = lerp(0.33f, 0.60f, motionAbs);
  }

  const float sat = clamp01(0.28f + (motionAbs * 0.62f));
  const float val = clamp01((Config::kTipBrightnessMin +
                             (Config::kTipBrightnessMax - Config::kTipBrightnessMin) * nearness) *
                            gTrack.influence);
  const float white = clamp01(val * 0.08f);
  return hsvColor(hue, sat, val, white);
}

void shiftStamenTowardRing(ColorF* cells,
                           uint16_t count,
                           const ColorF& newTip,
                           ColorF& outAtRing) {
  if (count == 0) {
    outAtRing = ColorF{};
    return;
  }

  if (Config::kTipAtHighIndex) {
    outAtRing = cells[0];
    for (uint16_t i = 0; i < count - 1; ++i) {
      cells[i] = cells[i + 1];
    }
    cells[count - 1] = newTip;
  } else {
    outAtRing = cells[count - 1];
    for (int16_t i = static_cast<int16_t>(count - 1); i > 0; --i) {
      cells[i] = cells[i - 1];
    }
    cells[0] = newTip;
  }
}

ColorF renderSampleConveyor(const ColorF* cells, uint16_t count, uint16_t index, const ColorF& tipIncoming) {
  if (count == 0 || index >= count) return ColorF{};

  const float frac = clamp01(gConveyorPhase);
  if (Config::kTipAtHighIndex) {
    if (index == count - 1) {
      return lerpColor(cells[index], tipIncoming, frac);
    }
    return lerpColor(cells[index], cells[index + 1], frac);
  } else {
    if (index == 0) {
      return lerpColor(cells[index], tipIncoming, frac);
    }
    return lerpColor(cells[index], cells[index - 1], frac);
  }
}

void decayStamen(ColorF* cells, uint16_t count, float dtSec) {
  const float factor = clamp01(1.0f - Config::kConveyorFadePerSecond * dtSec);
  for (uint16_t i = 0; i < count; ++i) {
    decayColor(cells[i], factor);
  }
}

float kernelWeight(float dist, float width) {
  const float safeWidth = (width < 0.001f) ? 0.001f : width;
  const float x = clamp01(1.0f - (dist / safeWidth));
  return x * x;
}

void injectIntoRing(uint16_t ingress, const ColorF& sample) {
  if (Config::kRingPixels == 0) return;

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    int d = abs(static_cast<int>(i) - static_cast<int>(ingress % Config::kRingPixels));
    d = min(d, static_cast<int>(Config::kRingPixels) - d);
    const float w = kernelWeight(static_cast<float>(d), Config::kIngressSpreadPixels);
    if (w <= 0.0f) continue;

    gTorus[i].r = clamp01(gTorus[i].r + sample.r * w * Config::kReservoirInjectionGain);
    gTorus[i].g = clamp01(gTorus[i].g + sample.g * w * Config::kReservoirInjectionGain);
    gTorus[i].b = clamp01(gTorus[i].b + sample.b * w * Config::kReservoirInjectionGain);
    gTorus[i].w = clamp01(gTorus[i].w + sample.w * w * Config::kReservoirInjectionGain);
  }
}

void updateTorus(float dtSec) {
  ColorF next[Config::kRingPixels];
  const float decay = Config::kTorusClearSeconds / (Config::kTorusClearSeconds + dtSec);
  const float diffusion = Config::kTorusDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (Config::kRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % Config::kRingPixels;

    auto diffuseChannel = [&](float c, float l, float r) {
      float v = c + ((l + r - 2.0f * c) * diffusion);
      v *= decay;
      return clamp01(v);
    };

    next[i].r = diffuseChannel(gTorus[i].r, gTorus[left].r, gTorus[right].r);
    next[i].g = diffuseChannel(gTorus[i].g, gTorus[left].g, gTorus[right].g);
    next[i].b = diffuseChannel(gTorus[i].b, gTorus[left].b, gTorus[right].b);
    next[i].w = diffuseChannel(gTorus[i].w, gTorus[left].w, gTorus[right].w);
  }

  for (uint16_t i = 0; i < Config::kRingPixels; ++i) {
    gTorus[i] = next[i];
  }
}

void updateScene(float dtSec) {
  updateMotionSignal(dtSec);

  const ColorF desiredTip = desiredTipColor();
  gCurrentTipColor = lerpColor(gCurrentTipColor, desiredTip, Config::kTipColorAlpha);

  decayStamen(gLeftConveyor, Config::kLeftStamenPixels, dtSec);
  decayStamen(gRightConveyor, Config::kRightStamenPixels, dtSec);

  gConveyorPhase += dtSec * Config::kConveyorPixelsPerSecond;
  while (gConveyorPhase >= 1.0f) {
    gConveyorPhase -= 1.0f;

    ColorF outLeft;
    ColorF outRight;
    shiftStamenTowardRing(gLeftConveyor, Config::kLeftStamenPixels, gCurrentTipColor, outLeft);
    shiftStamenTowardRing(gRightConveyor, Config::kRightStamenPixels, gCurrentTipColor, outRight);

    injectIntoRing(Config::kIngressA % Config::kRingPixels, outLeft);
    injectIntoRing(Config::kIngressB % Config::kRingPixels, outRight);
  }

  updateTorus(dtSec);
}

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

void renderScene() {
  clearStrip();

  const SegmentRange ring = ringSeg();
  const SegmentRange left = leftSeg();
  const SegmentRange right = rightSeg();

  // Torus draws from its own stored color memory.
  for (uint16_t i = 0; i < ring.count; ++i) {
    ColorF c = gTorus[i];
    c.w = clamp01(c.w + ((c.r + c.g + c.b) * 0.333f * Config::kTorusWhiteGain));
    setPixelRgbw(ring.start + i, c);
  }

  // Stamens draw from the moving conveyor buffers.
  for (uint16_t i = 0; i < left.count; ++i) {
    ColorF c = renderSampleConveyor(gLeftConveyor, left.count, i, gCurrentTipColor);
    c.w = clamp01(c.w + ((c.r + c.g + c.b) * 0.333f * Config::kStamenWhiteGain));
    setPixelRgbw(left.start + i, c);
  }
  for (uint16_t i = 0; i < right.count; ++i) {
    ColorF c = renderSampleConveyor(gRightConveyor, right.count, i, gCurrentTipColor);
    c.w = clamp01(c.w + ((c.r + c.g + c.b) * 0.333f * Config::kStamenWhiteGain));
    setPixelRgbw(right.start + i, c);
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
  Serial.print(" infl=");
  Serial.print(gTrack.influence, 3);
  Serial.print(" motion=");
  Serial.print(gMotionSignal, 3);
  Serial.print(" tip_rgb=");
  Serial.print(gCurrentTipColor.r, 2);
  Serial.print('/');
  Serial.print(gCurrentTipColor.g, 2);
  Serial.print('/');
  Serial.println(gCurrentTipColor.b, 2);
}
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Wire.begin();
  gLastInitAttemptMs = millis();
  gSensorReady = initC4001();

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
