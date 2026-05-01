#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <math.h>

// ClapSceneUxSmokeV2AckBlink.ino
//
// Purpose:
//   Visual usability smoke test for the final 112-pixel nightlight topology.
//
//   This keeps the KY-037 analog V4 detector behavior, but replaces the simple
//   red/green/blue diagnostic fills with a scene-cycling UX overlay:
//
//     double clap 1: Anthurium    -> Stable Light
//     double clap 2: Stable Light -> Off
//     double clap 3: Off          -> Anthurium
//
//   First clap feedback:
//     - Rear wall-wash ring: white "crawling ants" moving clockwise.
//     - Front ring: red acknowledgement fill by default.
//       Set kFrontAckWhite = true below if you want brilliant white instead.
//     - Two J/spadix segments: mirrored countdown drain toward the visual bottom.
//
//   Second clap feedback:
//     - A target-independent double blink fires before the new scene settles,
//       including Stable Light -> Off and Off -> Anthurium.
//
// Intended gesture:
//   clap, see the acknowledgement overlay, clap again.
//
// Wiring:
//   KY-037 AO  -> A0
//   KY-037 GND -> Arduino GND
//   KY-037 VCC -> Arduino 5V
//   NeoPixel data -> D6
//
// Optional:
//   KY-037 D0 -> D2
//   D0 remains disabled by default.

namespace Pins {
constexpr uint8_t kMicAnalogPin = A0;
constexpr uint8_t kMicDigitalPin = 2;
constexpr uint8_t kStripPin = 6;
}

namespace Topology {
// Final installed topology, 112 pixels total.
constexpr uint16_t kPixels = 112;

constexpr uint16_t kJ1Start = 0;
constexpr uint16_t kJ1Count = 12;

constexpr uint16_t kJ2Start = 12;
constexpr uint16_t kJ2Count = 12;

constexpr uint16_t kFrontRingStart = 24;
constexpr uint16_t kFrontRingCount = 44;

constexpr uint16_t kRearRingStart = 68;
constexpr uint16_t kRearRingCount = 44;
}

constexpr uint8_t kBrightness = 30;

// First-clap UX knobs.
constexpr bool kFrontAckWhite = false;       // false = red, true = brilliant white
constexpr uint16_t kAntStepMs = 90;          // lower = faster crawling ants
constexpr uint16_t kRenderMs = 33;           // visual refresh; mic still samples every 1 ms
// Scene change acknowledgement: a target-independent double blink that fires
// after the second clap, before the new scene settles. This is deliberately
// visible even when the target scene is Off.
constexpr uint16_t kSceneSwitchPulseMs = 340;
constexpr uint16_t kSceneAckBlinkOnMs = 105;
constexpr uint16_t kSceneAckBlinkGapMs = 80;

// Set true only if a later D0 tuning pass shows D0 is quiet at rest and edges on
// a clap. Current bench setup does not have D0 soldered.
constexpr bool kUseDigitalGate = false;

// Mic sampling. 1 ms is the cadence used by the recorder/tuner sketches.
constexpr uint32_t kSampleUs = 1000;
constexpr uint8_t kP2PWindowSamples = 12;

// Logging cadence.
constexpr uint32_t kPrintMs = 110;

// Thresholds in raw 10-bit ADC counts. These are V4 values.
constexpr float kFirstDevMin = 18.0f;
constexpr float kFirstP2PMin = 24.0f;
constexpr float kSecondDevMin = 14.0f;
constexpr float kSecondP2PMin = 16.0f;

// P2P-only candidates must still show some raw deviation. This was the V4 fix
// that made the detector less twitchy.
constexpr float kFirstP2POnlyDevMin = 10.0f;
constexpr float kSecondP2POnlyDevMin = 6.0f;

// Dynamic floor scaling.
constexpr float kFirstDevFloorScale = 6.0f;
constexpr float kFirstP2PFloorScale = 4.0f;
constexpr float kSecondDevFloorScale = 4.5f;
constexpr float kSecondP2PFloorScale = 3.0f;
constexpr float kDevFloorOffset = 6.0f;
constexpr float kP2PFloorOffset = 4.0f;

// Clap pattern timing.
constexpr uint32_t kCandidateLockoutMs = 80;
constexpr uint32_t kSecondGapMinMs = 70;

// Fast second-clap path. Kept available, but strict. Normal "clap, see cue,
// clap again" is the reliable path.
constexpr bool kEnableFastSecondDuringRelease = true;
constexpr uint32_t kFastSecondGapMinMs = 115;
constexpr float kFastSecondDevMin = 22.0f;
constexpr float kFastSecondP2PMin = 26.0f;

// Wider window for the slower, reliable pattern.
constexpr uint32_t kSecondGapMaxMs = 1900;
constexpr uint32_t kSecondListenTimeoutMs = 2300;

// How long the signal must be quiet before the normal second-clap gate opens.
constexpr uint32_t kQuietRearmMs = 8;
constexpr uint32_t kPostToggleCooldownMs = 350;

// How quiet the signal must become after clap 1 before the normal second-clap
// gate opens. Looser factor arms earlier without lowering the actual threshold.
constexpr float kReleaseFactor = 0.66f;

Adafruit_NeoPixel gStrip(Topology::kPixels, Pins::kStripPin, NEO_GRBW + NEO_KHZ800);

enum class DetectorState : uint8_t {
  Idle,
  WaitRelease,
  WaitSecond,
};

enum class SceneMode : uint8_t {
  Anthurium = 0,
  StableLight = 1,
  Off = 2,
};

DetectorState gState = DetectorState::Idle;
SceneMode gScene = SceneMode::Anthurium;

float gBaseline = 0.0f;
float gDevFloor = 1.0f;
float gP2PFloor = 6.0f;
float gLastDev = 0.0f;
float gLastP2P = 0.0f;

uint16_t gWindow[kP2PWindowSamples];
uint8_t gWindowHead = 0;
uint8_t gWindowCount = 0;

uint32_t gLastSampleUs = 0;
uint32_t gLastRenderMs = 0;
uint32_t gLastPrintMs = 0;
uint32_t gLastCandidateMs = 0;
uint32_t gFirstClapMs = 0;
uint32_t gQuietSinceMs = 0;
uint32_t gLastToggleMs = 0;
uint32_t gSceneChangedMs = 0;

bool gPrevD0 = false;

static float maxf(float a, float b) {
  return a > b ? a : b;
}

static float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static uint8_t clamp8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return static_cast<uint8_t>(v);
}

static uint8_t triWave8(uint32_t now, uint16_t periodMs) {
  if (periodMs < 2) return 0;
  const uint16_t half = periodMs / 2;
  const uint16_t x = now % periodMs;

  if (x < half) {
    return static_cast<uint8_t>((static_cast<uint32_t>(x) * 255UL) / half);
  }
  return static_cast<uint8_t>(255UL - ((static_cast<uint32_t>(x - half) * 255UL) / (periodMs - half)));
}

static uint32_t cRGBW(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
  return gStrip.Color(r, g, b, w);
}

static uint32_t cWarmWhite(uint8_t w) {
  // Tiny amber component keeps the white from feeling hospital-cold.
  return cRGBW(w / 5, w / 9, 0, w);
}

static uint32_t wheel(uint8_t pos, uint8_t scale = 55) {
  pos = 255 - pos;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  if (pos < 85) {
    r = 255 - pos * 3;
    b = pos * 3;
  } else if (pos < 170) {
    pos -= 85;
    g = pos * 3;
    b = 255 - pos * 3;
  } else {
    pos -= 170;
    r = pos * 3;
    g = 255 - pos * 3;
  }

  return cRGBW((r * scale) / 255, (g * scale) / 255, (b * scale) / 255, 0);
}

static const char *stateName() {
  switch (gState) {
    case DetectorState::Idle: return "idle";
    case DetectorState::WaitRelease: return "release";
    case DetectorState::WaitSecond: return "second";
  }
  return "?";
}

static const char *sceneName(SceneMode scene) {
  switch (scene) {
    case SceneMode::Anthurium: return "anthurium";
    case SceneMode::StableLight: return "stable_light";
    case SceneMode::Off: return "off";
  }
  return "?";
}

static void clearPixels() {
  gStrip.clear();
}

static void fillRange(uint16_t start, uint16_t count, uint32_t color) {
  for (uint16_t i = 0; i < count; ++i) {
    gStrip.setPixelColor(start + i, color);
  }
}

static uint16_t rearClockwisePixel(uint16_t logicalIndex) {
  // Commissioning sketch says rear ring forward is counterclockwise, so reverse
  // order gives visual clockwise motion.
  if (logicalIndex >= Topology::kRearRingCount) logicalIndex %= Topology::kRearRingCount;
  return Topology::kRearRingStart + (Topology::kRearRingCount - 1 - logicalIndex);
}

static void renderJCountdown(uint32_t now) {
  uint32_t elapsed = now - gFirstClapMs;
  if (elapsed > kSecondListenTimeoutMs) elapsed = kSecondListenTimeoutMs;

  const uint8_t expired = static_cast<uint8_t>(
      (elapsed * Topology::kJ1Count) / kSecondListenTimeoutMs);
  const uint8_t remaining = (expired >= Topology::kJ1Count)
      ? 0
      : static_cast<uint8_t>(Topology::kJ1Count - expired);

  const uint32_t spadixWhite = cRGBW(0, 0, 0, 82);

  // J1: 0..11, drains by removing 0, then 1, then 2...
  for (uint8_t i = 0; i < remaining; ++i) {
    const uint16_t idx = Topology::kJ1Start + (Topology::kJ1Count - remaining) + i;
    gStrip.setPixelColor(idx, spadixWhite);
  }

  // J2: 12..23, drains by removing 23, then 22, then 21...
  for (uint8_t i = 0; i < remaining; ++i) {
    const uint16_t idx = Topology::kJ2Start + i;
    gStrip.setPixelColor(idx, spadixWhite);
  }
}

static void renderRearCrawlingAnts(uint32_t now) {
  const uint8_t phase = (now / kAntStepMs) & 0x03;

  for (uint16_t i = 0; i < Topology::kRearRingCount; ++i) {
    const bool on = (((i + phase) & 0x03) < 2);
    const uint16_t idx = rearClockwisePixel(i);
    gStrip.setPixelColor(idx, on ? cRGBW(0, 0, 0, 72) : cRGBW(0, 0, 0, 2));
  }
}

static void renderFrontAck(uint32_t now) {
  if (kFrontAckWhite) {
    const uint8_t pulse = 68 + (triWave8(now, 360) / 8);
    fillRange(Topology::kFrontRingStart, Topology::kFrontRingCount, cRGBW(0, 0, 0, pulse));
  } else {
    const uint8_t pulse = 72 + (triWave8(now, 360) / 10);
    fillRange(Topology::kFrontRingStart, Topology::kFrontRingCount, cRGBW(pulse, 0, 0, 0));
  }
}

static void renderClapWaitOverlay(uint32_t now) {
  clearPixels();
  renderJCountdown(now);
  renderFrontAck(now);
  renderRearCrawlingAnts(now);
}

static void renderAnthuriumPlaceholder(uint32_t now) {
  // Placeholder only. In production this slot should be replaced by the C4001
  // Anthurium render intent. The point here is to test how the clap UX overlays
  // on a colorful animated scene.
  clearPixels();

  const uint8_t phase = (now / 26) & 0xFF;

  for (uint16_t i = 0; i < Topology::kFrontRingCount; ++i) {
    gStrip.setPixelColor(Topology::kFrontRingStart + i, wheel(phase + i * 5, 54));
  }

  for (uint16_t i = 0; i < Topology::kRearRingCount; ++i) {
    const uint8_t w = 8 + (triWave8(now + i * 17, 1900) / 16);
    const uint8_t b = 18 + (triWave8(now + i * 29, 2400) / 10);
    gStrip.setPixelColor(Topology::kRearRingStart + i, cRGBW(12, 0, b, w));
  }

  for (uint16_t i = 0; i < Topology::kJ1Count; ++i) {
    gStrip.setPixelColor(Topology::kJ1Start + i, cRGBW(55, 0, 20, 3));
  }
  for (uint16_t i = 0; i < Topology::kJ2Count; ++i) {
    gStrip.setPixelColor(Topology::kJ2Start + i, cRGBW(20, 32, 0, 3));
  }
}

static void renderStableLight(uint32_t now) {
  clearPixels();

  const uint8_t breath = 20 + (triWave8(now, 4800) / 24); // 20..30
  const uint32_t stable = cWarmWhite(breath);

  fillRange(Topology::kJ1Start, Topology::kJ1Count, cWarmWhite(16));
  fillRange(Topology::kJ2Start, Topology::kJ2Count, cWarmWhite(16));
  fillRange(Topology::kFrontRingStart, Topology::kFrontRingCount, stable);
  fillRange(Topology::kRearRingStart, Topology::kRearRingCount, cWarmWhite(breath / 2));
}

static void renderOff() {
  clearPixels();
}

static bool sceneAckBlinkIsOn(uint32_t elapsed) {
  if (elapsed < kSceneAckBlinkOnMs) return true;

  const uint32_t secondStart = kSceneAckBlinkOnMs + kSceneAckBlinkGapMs;
  if (elapsed >= secondStart && elapsed < secondStart + kSceneAckBlinkOnMs) return true;

  return false;
}

static void renderSceneAckDoubleBlink(uint32_t now, uint32_t elapsed) {
  clearPixels();

  const bool on = sceneAckBlinkIsOn(elapsed);
  if (!on) {
    // A real dark valley between blinks makes the acknowledgement legible,
    // especially on Stable Light -> Off.
    return;
  }

  // Crisp, target-independent "we caught the second clap" confirmation.
  // Full-strip enough to be unmistakable, but the segments keep their own roles.
  const uint8_t shimmer = triWave8(now, 160) / 10;

  fillRange(Topology::kJ1Start, Topology::kJ1Count, cRGBW(0, 0, 0, 72 + shimmer));
  fillRange(Topology::kJ2Start, Topology::kJ2Count, cRGBW(0, 0, 0, 72 + shimmer));
  fillRange(Topology::kFrontRingStart, Topology::kFrontRingCount, cRGBW(0, 0, 0, 96 + shimmer));
  fillRange(Topology::kRearRingStart, Topology::kRearRingCount, cRGBW(0, 0, 0, 82 + shimmer));

  // Small rear-ring sparkle/crawl accent so the blink still belongs to the
  // piece rather than feeling like a generic flashlight pop.
  const uint8_t phase = (now / 42) % Topology::kRearRingCount;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t idx = rearClockwisePixel((phase + i * 11) % Topology::kRearRingCount);
    gStrip.setPixelColor(idx, cRGBW(18, 8, 0, 110));
  }
}

static void renderSceneSwitchPulse(uint32_t now) {
  const uint32_t elapsed = now - gSceneChangedMs;

  // First priority: acknowledge the second clap the same way for every target.
  // After this window, renderNormalScene() will allow the target scene to show.
  renderSceneAckDoubleBlink(now, elapsed);
}
static void renderNormalScene(uint32_t now) {
  if (now - gSceneChangedMs < kSceneSwitchPulseMs) {
    renderSceneSwitchPulse(now);
    return;
  }

  switch (gScene) {
    case SceneMode::Anthurium:
      renderAnthuriumPlaceholder(now);
      break;
    case SceneMode::StableLight:
      renderStableLight(now);
      break;
    case SceneMode::Off:
      renderOff();
      break;
  }
}

static void renderAll(uint32_t now) {
  if (gState == DetectorState::WaitRelease || gState == DetectorState::WaitSecond) {
    renderClapWaitOverlay(now);
  } else {
    renderNormalScene(now);
  }

  gStrip.show();
}

static void pushWindow(uint16_t raw) {
  gWindow[gWindowHead] = raw;
  gWindowHead = (gWindowHead + 1) % kP2PWindowSamples;
  if (gWindowCount < kP2PWindowSamples) ++gWindowCount;
}

static uint16_t currentP2P() {
  if (gWindowCount == 0) return 0;

  uint16_t wMin = 1023;
  uint16_t wMax = 0;
  for (uint8_t i = 0; i < gWindowCount; ++i) {
    const uint16_t raw = gWindow[i];
    if (raw < wMin) wMin = raw;
    if (raw > wMax) wMax = raw;
  }
  return wMax - wMin;
}

static void printEvent(const char *eventName, uint16_t raw, float dev, uint16_t p2p,
                       float devThreshold, float p2pThreshold) {
  Serial.print(F("event=")); Serial.print(eventName);
  Serial.print(F(" raw=")); Serial.print(raw);
  Serial.print(F(" dev=")); Serial.print(dev, 1);
  Serial.print(F(" p2p=")); Serial.print(p2p);
  Serial.print(F(" dev_thr=")); Serial.print(devThreshold, 1);
  Serial.print(F(" p2p_thr=")); Serial.print(p2pThreshold, 1);
  Serial.print(F(" baseline=")); Serial.print(gBaseline, 1);
  Serial.print(F(" state=")); Serial.print(stateName());
  Serial.print(F(" scene=")); Serial.println(sceneName(gScene));
}

static bool readD0Edge() {
  if (!kUseDigitalGate) return false;

  const bool d0 = digitalRead(Pins::kMicDigitalPin) ? true : false;
  const bool edge = (d0 != gPrevD0);
  gPrevD0 = d0;
  return edge;
}

static void learnQuietSignal(uint16_t raw, float dev, uint16_t p2p, bool quiet, uint32_t now) {
  if (!quiet) return;
  if (now - gLastCandidateMs < 250) return;

  gBaseline += (static_cast<float>(raw) - gBaseline) * 0.006f;
  gDevFloor += (dev - gDevFloor) * 0.015f;
  gP2PFloor += (static_cast<float>(p2p) - gP2PFloor) * 0.015f;

  if (gDevFloor < 0.6f) gDevFloor = 0.6f;
  if (gP2PFloor < 2.0f) gP2PFloor = 2.0f;
  if (gP2PFloor > 80.0f) gP2PFloor = 80.0f;
}

static bool p2pCandidateWithDev(float dev, uint16_t p2p, float p2pThreshold, float p2pOnlyDevMin) {
  return static_cast<float>(p2p) >= p2pThreshold && dev >= p2pOnlyDevMin;
}

static void cycleScene(uint32_t now) {
  switch (gScene) {
    case SceneMode::Anthurium:
      gScene = SceneMode::StableLight;
      break;
    case SceneMode::StableLight:
      gScene = SceneMode::Off;
      break;
    case SceneMode::Off:
      gScene = SceneMode::Anthurium;
      break;
  }

  gSceneChangedMs = now;
  Serial.print(F("scene="));
  Serial.println(sceneName(gScene));
}

static void acceptDoubleClap(const char *eventName, uint16_t raw, float dev, uint16_t p2p,
                             float devThreshold, float p2pThreshold, uint32_t now) {
  gState = DetectorState::Idle;
  gLastCandidateMs = now;
  gLastToggleMs = now;
  gQuietSinceMs = 0;

  printEvent(eventName, raw, dev, p2p, devThreshold, p2pThreshold);
  cycleScene(now);
}

static void processSample(uint16_t raw, uint32_t now) {
  pushWindow(raw);

  if (gBaseline <= 0.0f) {
    gBaseline = static_cast<float>(raw);
  }

  const float dev = fabsf(static_cast<float>(raw) - gBaseline);
  const uint16_t p2p = currentP2P();

  gLastDev = dev;
  gLastP2P = static_cast<float>(p2p);

  const float firstDevThreshold = maxf(kFirstDevMin, gDevFloor * kFirstDevFloorScale + kDevFloorOffset);
  const float firstP2PThreshold = maxf(kFirstP2PMin, gP2PFloor * kFirstP2PFloorScale + kP2PFloorOffset);
  const float secondDevThreshold = maxf(kSecondDevMin, gDevFloor * kSecondDevFloorScale + kDevFloorOffset);
  const float secondP2PThreshold = maxf(kSecondP2PMin, gP2PFloor * kSecondP2PFloorScale + kP2PFloorOffset);

  const float releaseDevThreshold = secondDevThreshold * kReleaseFactor;
  const float releaseP2PThreshold = secondP2PThreshold * kReleaseFactor;
  const bool quiet = (dev <= releaseDevThreshold && static_cast<float>(p2p) <= releaseP2PThreshold);

  learnQuietSignal(raw, dev, p2p, quiet, now);

  const bool d0Edge = readD0Edge();
  const bool lockedOut = (now - gLastCandidateMs < kCandidateLockoutMs);
  const bool firstCandidate = !lockedOut &&
                              (dev >= firstDevThreshold ||
                               p2pCandidateWithDev(dev, p2p, firstP2PThreshold, kFirstP2POnlyDevMin) ||
                               d0Edge);
  const bool secondCandidate = !lockedOut &&
                               (dev >= secondDevThreshold ||
                                p2pCandidateWithDev(dev, p2p, secondP2PThreshold, kSecondP2POnlyDevMin) ||
                                d0Edge);

  switch (gState) {
    case DetectorState::Idle:
      if (firstCandidate && now - gLastToggleMs > kPostToggleCooldownMs) {
        gState = DetectorState::WaitRelease;
        gFirstClapMs = now;
        gLastCandidateMs = now;
        gQuietSinceMs = 0;
        printEvent("clap1", raw, dev, p2p, firstDevThreshold, firstP2PThreshold);
      }
      break;

    case DetectorState::WaitRelease: {
      const uint32_t dt = now - gFirstClapMs;
      const float fastDevThreshold = maxf(secondDevThreshold, kFastSecondDevMin);
      const float fastP2PThreshold = maxf(secondP2PThreshold, kFastSecondP2PMin);
      const bool fastSecondCandidate = kEnableFastSecondDuringRelease &&
                                       !lockedOut &&
                                       dt >= kFastSecondGapMinMs &&
                                       dt <= kSecondGapMaxMs &&
                                       ((dev >= fastDevThreshold && static_cast<float>(p2p) >= fastP2PThreshold) ||
                                        d0Edge);

      if (fastSecondCandidate) {
        acceptDoubleClap("double_clap_fast", raw, dev, p2p, fastDevThreshold, fastP2PThreshold, now);
      } else {
        if (quiet) {
          if (gQuietSinceMs == 0) gQuietSinceMs = now;
          if (now - gQuietSinceMs >= kQuietRearmMs) {
            gState = DetectorState::WaitSecond;
            Serial.println(F("event=ready_for_second"));
          }
        } else {
          gQuietSinceMs = 0;
        }

        if (now - gFirstClapMs > kSecondListenTimeoutMs) {
          gState = DetectorState::Idle;
          gQuietSinceMs = 0;
          Serial.println(F("event=clap1_timeout"));
        }
      }
      break;
    }

    case DetectorState::WaitSecond: {
      const uint32_t dt = now - gFirstClapMs;

      if (secondCandidate && dt >= kSecondGapMinMs && dt <= kSecondGapMaxMs) {
        acceptDoubleClap("double_clap", raw, dev, p2p, secondDevThreshold, secondP2PThreshold, now);
      } else if (dt > kSecondGapMaxMs && firstCandidate) {
        // A late clap starts a new possible pair.
        gState = DetectorState::WaitRelease;
        gFirstClapMs = now;
        gLastCandidateMs = now;
        gQuietSinceMs = 0;
        printEvent("clap1_rearm", raw, dev, p2p, firstDevThreshold, firstP2PThreshold);
      } else if (now - gFirstClapMs > kSecondListenTimeoutMs) {
        gState = DetectorState::Idle;
        gQuietSinceMs = 0;
        Serial.println(F("event=clap1_timeout"));
      }
      break;
    }
  }

  if (now - gLastPrintMs >= kPrintMs) {
    gLastPrintMs = now;
    Serial.print(F("raw=")); Serial.print(raw);
    Serial.print(F(" dev=")); Serial.print(dev, 1);
    Serial.print(F(" p2p=")); Serial.print(p2p);
    Serial.print(F(" dev_floor=")); Serial.print(gDevFloor, 1);
    Serial.print(F(" p2p_floor=")); Serial.print(gP2PFloor, 1);
    Serial.print(F(" thr1_dev=")); Serial.print(firstDevThreshold, 1);
    Serial.print(F(" thr1_p2p=")); Serial.print(firstP2PThreshold, 1);
    Serial.print(F(" thr2_dev=")); Serial.print(secondDevThreshold, 1);
    Serial.print(F(" thr2_p2p=")); Serial.print(secondP2PThreshold, 1);
    Serial.print(F(" state=")); Serial.print(stateName());
    Serial.print(F(" scene=")); Serial.println(sceneName(gScene));
  }
}

static void seedBaseline() {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < 128; ++i) {
    const uint16_t raw = analogRead(Pins::kMicAnalogPin);
    sum += raw;
    pushWindow(raw);
    delay(2);
  }
  gBaseline = static_cast<float>(sum) / 128.0f;
  gDevFloor = 1.0f;
  gP2PFloor = 6.0f;
}

void setup() {
  Serial.begin(115200);

  pinMode(Pins::kMicAnalogPin, INPUT);
  pinMode(Pins::kMicDigitalPin, INPUT);

  gStrip.begin();
  gStrip.setBrightness(kBrightness);
  clearPixels();
  fillRange(0, Topology::kPixels, cRGBW(0, 0, 0, 10));
  gStrip.show();

  seedBaseline();
  gPrevD0 = digitalRead(Pins::kMicDigitalPin) ? true : false;
  gLastSampleUs = micros();
  gLastRenderMs = millis();
  gLastPrintMs = millis();
  gSceneChangedMs = millis() - kSceneSwitchPulseMs;

  Serial.println(F("ClapSceneUxSmokeV2AckBlink"));
  Serial.println(F("AO -> A0, strip -> D6. D0 optional but disabled by default."));
  Serial.println(F("Scene cycle: Anthurium -> Stable Light -> Off -> Anthurium."));
  Serial.println(F("Second clap acknowledgement: target-independent double blink."));
  Serial.println(F("Gesture: clap, see overlay, clap again."));
}

void loop() {
  const uint32_t nowUs = micros();

  if (nowUs - gLastSampleUs >= kSampleUs) {
    // Advance by exactly one period to keep cadence stable even if rendering
    // makes one sample a little late.
    gLastSampleUs += kSampleUs;
    const uint16_t raw = analogRead(Pins::kMicAnalogPin);
    processSample(raw, millis());
  }

  const uint32_t now = millis();
  if (now - gLastRenderMs >= kRenderMs) {
    gLastRenderMs = now;
    renderAll(now);
  }
}
