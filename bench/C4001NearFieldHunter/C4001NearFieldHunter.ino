#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#if __has_include("../../src/BuildConfig.h")
#include "../../src/BuildConfig.h"
#include "../../src/Pins.h"
#include "../../src/mapping/C4001TrackFilter.h"
#include "../../src/sensors/PresenceManager.h"
#elif __has_include("../../../src/BuildConfig.h")
#include "../../../src/BuildConfig.h"
#include "../../../src/Pins.h"
#include "../../../src/mapping/C4001TrackFilter.h"
#include "../../../src/sensors/PresenceManager.h"
#else
#error "Expected repository source tree includes for C4001NearFieldHunter."
#endif

namespace {
constexpr float kNearDisplayM = 0.20f;
constexpr float kFarDisplayM = 2.00f;
constexpr uint8_t kStatusPixels = 5;
constexpr uint8_t kGlobalBrightness = 26;
constexpr uint32_t kPrintIntervalMs = 50;
constexpr uint32_t kStrictHoldMs = 450;
constexpr bool kSuppressFarTargets = true;

// CSV phase codes.
enum PhaseCode : uint8_t {
  Accepted = 0,
  SoftRejectHold = 1,
  HardAbsentHold = 2,
  Empty = 3,
  FarDominant = 4,
};

PresenceManager gPresence;
Adafruit_NeoPixel gStrip(BuildConfig::kTotalPixels, Pins::kPixelDataPin, NEO_GRBW + NEO_KHZ800);
C4001TrackFilter gPermissive;
C4001TrackFilter gStrict;
uint32_t gLastPrintMs = 0;

float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

int32_t mmFromMeters(float meters) {
  return static_cast<int32_t>(meters * 1000.0f);
}

uint16_t rangeToPixel(float rangeM) {
  if (BuildConfig::kTotalPixels <= kStatusPixels) {
    return 0;
  }

  const float t = clamp01((rangeM - kNearDisplayM) / (kFarDisplayM - kNearDisplayM));
  const uint16_t dynamicPixels = BuildConfig::kTotalPixels - kStatusPixels;
  const uint16_t rel = static_cast<uint16_t>(t * static_cast<float>(dynamicPixels - 1));
  return kStatusPixels + rel;
}

void drawCursor(const C4001TrackFilter::Output& out, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (!out.hasTrack || out.sample.rangeM < kNearDisplayM || out.sample.rangeM > kFarDisplayM) {
    return;
  }

  const uint16_t idx = rangeToPixel(out.sample.rangeM);
  if (idx < BuildConfig::kTotalPixels) {
    gStrip.setPixelColor(idx, gStrip.Color(r, g, b, w));
  }
}

void drawStatusBar(PhaseCode phase) {
  // [0]=accepted, [1]=soft reject hold, [2]=hard absent hold, [3]=empty, [4]=far dominant
  const uint32_t colors[kStatusPixels] = {
      gStrip.Color((phase == PhaseCode::Accepted) ? 0 : 0,
                   (phase == PhaseCode::Accepted) ? 90 : 8,
                   0,
                   0),
      gStrip.Color((phase == PhaseCode::SoftRejectHold) ? 90 : 8,
                   0,
                   (phase == PhaseCode::SoftRejectHold) ? 90 : 8,
                   0),
      gStrip.Color((phase == PhaseCode::HardAbsentHold) ? 110 : 8,
                   0,
                   0,
                   0),
      gStrip.Color(0,
                   0,
                   0,
                   (phase == PhaseCode::Empty) ? 110 : 8),
      gStrip.Color((phase == PhaseCode::FarDominant) ? 120 : 8,
                   (phase == PhaseCode::FarDominant) ? 90 : 8,
                   0,
                   0),
  };

  for (uint8_t i = 0; i < kStatusPixels && i < BuildConfig::kTotalPixels; ++i) {
    gStrip.setPixelColor(i, colors[i]);
  }
}

uint8_t compactFlags(const PresenceC4001::LinkStatus& link,
                     const C4001PresenceRich& rich,
                     bool farDominant,
                     bool hardAbsent) {
  uint8_t flags = 0;
  if (link.state != PresenceC4001::LinkState::Online) flags |= 0x01;
  if (rich.targetNumber > 0) flags |= 0x02;
  if (rich.targetSampleAccepted) flags |= 0x04;
  if (hardAbsent) flags |= 0x08;
  if (farDominant) flags |= 0x10;
  if (kSuppressFarTargets) flags |= 0x20;
  return flags;
}

PhaseCode phaseFromStrict(const C4001TrackFilter::Output& strict, bool hardAbsent, bool farDominant) {
  if (farDominant) {
    return PhaseCode::FarDominant;
  }
  if (!strict.hasTrack) {
    return PhaseCode::Empty;
  }
  if (strict.phase == C4001TrackFilter::Phase::Accepted) {
    return PhaseCode::Accepted;
  }
  if (strict.phase == C4001TrackFilter::Phase::SoftReject) {
    return PhaseCode::SoftRejectHold;
  }
  if (hardAbsent && strict.ageMs <= kStrictHoldMs) {
    return PhaseCode::HardAbsentHold;
  }
  return PhaseCode::Empty;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  gPresence.begin();

  gStrip.begin();
  gStrip.setBrightness(kGlobalBrightness);
  gStrip.clear();
  gStrip.show();

  gPermissive.configure(kStrictHoldMs,
                        BuildConfig::kAnthuriumRejectedDecayPerSecond,
                        BuildConfig::kAnthuriumRejectedFloor);
  gStrict.configure(kStrictHoldMs,
                    BuildConfig::kAnthuriumRejectedDecayPerSecond,
                    BuildConfig::kAnthuriumRejectedFloor);

  Serial.println("ms,raw_mm,perm_mm,strict_mm,phase,reason,hold_ms,flags");
}

void loop() {
  const uint32_t nowMs = millis();
  gPresence.readCore();

  const C4001PresenceRich& rich = gPresence.lastC4001Rich();
  const PresenceC4001::LinkStatus& link = gPresence.c4001LinkStatus();

  const bool hasTarget = (rich.targetNumber > 0);
  const bool hasRawRange = (rich.targetRangeRawM > 0.0f);
  const bool linkIssue = (link.state != PresenceC4001::LinkState::Online) ||
                         (link.sampleKind == PresenceC4001::SampleKind::ReadFailure);
  const bool hardAbsent = !hasTarget || !hasRawRange;
  const bool farDominant = hasTarget && hasRawRange && (rich.targetRangeRawM > kFarDisplayM);

  C4001TrackFilter::Sample continuitySample{};
  continuitySample.rangeM = rich.targetRangeRawM;
  continuitySample.smoothedRangeM = rich.targetRangeRawM;
  continuitySample.chargeTarget = clamp01(1.0f - (rich.targetRangeRawM / kFarDisplayM));
  continuitySample.ingressTarget = continuitySample.chargeTarget;
  continuitySample.fieldTarget = continuitySample.chargeTarget;
  continuitySample.energyBoostTarget = clamp01(static_cast<float>(rich.targetEnergy) / 100.0f);
  continuitySample.speedMps = rich.targetSpeedRawM;
  continuitySample.energyNorm = continuitySample.energyBoostTarget;

  C4001TrackFilter::InputClass permissiveInput = C4001TrackFilter::InputClass::HardAbsent;
  if (linkIssue) {
    permissiveInput = C4001TrackFilter::InputClass::LinkIssue;
  } else if (hasTarget && hasRawRange && (!kSuppressFarTargets || !farDominant)) {
    permissiveInput = C4001TrackFilter::InputClass::Accepted;
  }

  C4001TrackFilter::InputClass strictInput = C4001TrackFilter::InputClass::HardAbsent;
  if (linkIssue) {
    strictInput = C4001TrackFilter::InputClass::LinkIssue;
  } else if (hasTarget && hasRawRange && rich.targetSampleAccepted && (rich.targetRejectedReason == 0u) &&
             (!kSuppressFarTargets || !farDominant)) {
    strictInput = C4001TrackFilter::InputClass::Accepted;
  } else if (hasTarget && hasRawRange && (!kSuppressFarTargets || !farDominant)) {
    strictInput = C4001TrackFilter::InputClass::SoftReject;
  }

  const C4001TrackFilter::Output permissive =
      (permissiveInput == C4001TrackFilter::InputClass::Accepted)
          ? gPermissive.update(permissiveInput, nowMs, &continuitySample)
          : gPermissive.update(permissiveInput, nowMs, nullptr);

  const C4001TrackFilter::Output strict =
      (strictInput == C4001TrackFilter::InputClass::Accepted)
          ? gStrict.update(strictInput, nowMs, &continuitySample)
          : gStrict.update(strictInput, nowMs, nullptr);

  const PhaseCode phase = phaseFromStrict(strict, hardAbsent, farDominant);
  const uint8_t reason = hasTarget ? rich.targetRejectedReason
                                   : static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);

  gStrip.clear();
  drawStatusBar(phase);
  drawCursor(permissive, 0, 0, 0, 150);    // permissive continuity (white)
  drawCursor(strict, 120, 0, 120, 0);      // strict production style (magenta)
  gStrip.show();

  if ((nowMs - gLastPrintMs) >= kPrintIntervalMs) {
    gLastPrintMs = nowMs;
    Serial.print(nowMs);
    Serial.print(',');
    Serial.print(mmFromMeters(rich.targetRangeRawM));
    Serial.print(',');
    Serial.print(mmFromMeters(permissive.sample.rangeM));
    Serial.print(',');
    Serial.print(mmFromMeters(strict.sample.rangeM));
    Serial.print(',');
    Serial.print(static_cast<uint8_t>(phase));
    Serial.print(',');
    Serial.print(reason);
    Serial.print(',');
    Serial.print(strict.ageMs);
    Serial.print(',');
    Serial.println(compactFlags(link, rich, farDominant, hardAbsent));
  }
}
