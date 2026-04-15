#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "../../src/BuildConfig.h"
#include "../../src/Pins.h"
#include "../../src/mapping/C4001TrackFilter.h"
#include "../../src/sensors/PresenceManager.h"

namespace {
constexpr uint8_t kStatusPixels = 8;
constexpr uint8_t kGlobalBrightness = 26;
constexpr uint32_t kPrintIntervalMs = 50;

PresenceManager gPresence;
Adafruit_NeoPixel gStrip(BuildConfig::kTotalPixels,
                         Pins::kPixelDataPin,
                         NEO_GRBW + NEO_KHZ800);
C4001TrackFilter gPermissive;
C4001TrackFilter gStrict;
uint32_t gLastPrintMs = 0;

int32_t mmFromMeters(float meters) {
  return static_cast<int32_t>(meters * 1000.0f);
}

float clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

uint16_t rangeToPixel(float rangeM) {
  const float nearM = 0.45f;
  const float farM = 6.50f;
  const float t = clamp01((rangeM - nearM) / (farM - nearM));
  const uint16_t dynamicPixels = (BuildConfig::kTotalPixels > kStatusPixels)
                                     ? (BuildConfig::kTotalPixels - kStatusPixels)
                                     : 1;
  const uint16_t rel = static_cast<uint16_t>(t * static_cast<float>(dynamicPixels - 1));
  return kStatusPixels + rel;
}

uint8_t phaseCode(const C4001TrackFilter::Output& out) {
  return static_cast<uint8_t>(out.phase);
}

uint8_t strictPhaseCode(const C4001TrackFilter::Output& out,
                        const PresenceC4001::LinkStatus& linkStatus,
                        bool hardAbsent) {
  if (linkStatus.state != PresenceC4001::LinkState::Online) {
    return 4;  // link issue
  }
  if (out.phase == C4001TrackFilter::Phase::Valid) {
    return 0;
  }
  if (hardAbsent) {
    return 3;
  }
  if (out.phase == C4001TrackFilter::Phase::SoftReject) {
    return 2;
  }
  return 1;
}

void drawStatusBar(uint8_t strictPhase) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t w = 0;

  switch (strictPhase) {
    case 0:  // valid
      g = 80;
      break;
    case 1:  // hold
      b = 90;
      break;
    case 2:  // soft reject
      r = 80;
      b = 80;
      break;
    case 3:  // hard absent
      r = 110;
      break;
    case 4:  // link issue
    default:
      w = 90;
      break;
  }

  for (uint16_t i = 0; i < kStatusPixels && i < BuildConfig::kTotalPixels; ++i) {
    gStrip.setPixelColor(i, gStrip.Color(r, g, b, w));
  }
}

void drawCursor(const C4001TrackFilter::Output& out, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (!out.hasTrack || out.sample.rangeM <= 0.0f) {
    return;
  }
  const uint16_t idx = rangeToPixel(out.sample.rangeM);
  if (idx < BuildConfig::kTotalPixels) {
    gStrip.setPixelColor(idx, gStrip.Color(r, g, b, w));
  }
}

uint8_t compactFlags(const PresenceC4001::LinkStatus& link,
                     const C4001PresenceRich& rich,
                     bool hardAbsent) {
  uint8_t flags = 0;
  if (link.state != PresenceC4001::LinkState::Online) {
    flags |= 0x01;
  }
  if (link.nearField) {
    flags |= 0x02;
  }
  if (rich.targetNumber > 0) {
    flags |= 0x04;
  }
  if (rich.targetSampleAccepted) {
    flags |= 0x08;
  }
  if (hardAbsent) {
    flags |= 0x10;
  }
  return flags;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  gPresence.begin();

  gStrip.begin();
  gStrip.setBrightness(kGlobalBrightness);
  gStrip.clear();
  gStrip.show();

  gPermissive.configure(BuildConfig::kC4001DropoutHoldMs,
                        BuildConfig::kAnthuriumRejectedDecayPerSecond,
                        BuildConfig::kAnthuriumRejectedFloor);
  gStrict.configure(BuildConfig::kC4001DropoutHoldMs,
                    BuildConfig::kAnthuriumRejectedDecayPerSecond,
                    BuildConfig::kAnthuriumRejectedFloor);

  Serial.println("ms,raw_mm,perm_mm,strict_mm,phase,reason,hold_ms,flags");
}

void loop() {
  const uint32_t nowMs = millis();
  const CorePresence core = gPresence.readCore();
  const C4001PresenceRich& rich = gPresence.lastC4001Rich();
  const PresenceC4001::LinkStatus& link = gPresence.c4001LinkStatus();

  const bool hasTarget = (rich.targetNumber > 0);
  const bool hasRawRange = rich.targetRangeRawM > 0.0f;
  const bool linkIssue = (link.state != PresenceC4001::LinkState::Online) ||
                         (link.sampleKind == PresenceC4001::SampleKind::ReadFailure);
  const bool hardAbsent = !hasTarget || !hasRawRange;

  C4001TrackFilter::Sample permissiveSample{};
  permissiveSample.rangeM = rich.targetRangeRawM;
  permissiveSample.smoothedRangeM = rich.targetRangeRawM;
  permissiveSample.chargeTarget = clamp01(1.0f - (rich.targetRangeRawM / 6.50f));
  permissiveSample.ingressTarget = permissiveSample.chargeTarget;
  permissiveSample.fieldTarget = permissiveSample.chargeTarget;
  permissiveSample.energyBoostTarget = clamp01(static_cast<float>(rich.targetEnergy) / 100.0f);
  permissiveSample.speedMps = rich.targetSpeedRawM;
  permissiveSample.energyNorm = permissiveSample.energyBoostTarget;

  C4001TrackFilter::InputClass permissiveInput = C4001TrackFilter::InputClass::HardAbsent;
  if (linkIssue) {
    permissiveInput = C4001TrackFilter::InputClass::LinkIssue;
  } else if (hasTarget && hasRawRange) {
    permissiveInput = C4001TrackFilter::InputClass::Valid;
  }

  C4001TrackFilter::InputClass strictInput = C4001TrackFilter::InputClass::HardAbsent;
  if (linkIssue) {
    strictInput = C4001TrackFilter::InputClass::LinkIssue;
  } else if (hasTarget && hasRawRange && rich.targetSampleAccepted && (rich.targetRejectedReason == 0u)) {
    strictInput = C4001TrackFilter::InputClass::Valid;
  } else if (hasTarget && hasRawRange) {
    strictInput = C4001TrackFilter::InputClass::SoftReject;
  }

  const C4001TrackFilter::Output permissive =
      (permissiveInput == C4001TrackFilter::InputClass::Valid)
          ? gPermissive.update(permissiveInput, nowMs, &permissiveSample)
          : gPermissive.update(permissiveInput, nowMs, nullptr);

  const C4001TrackFilter::Output strict =
      (strictInput == C4001TrackFilter::InputClass::Valid)
          ? gStrict.update(strictInput, nowMs, &permissiveSample)
          : gStrict.update(strictInput, nowMs, nullptr);

  const uint8_t strictPhase = strictPhaseCode(strict, link, hardAbsent);
  const uint8_t reason = hasTarget ? rich.targetRejectedReason
                                   : static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);
  const uint32_t holdMs = strict.ageMs;

  gStrip.clear();
  drawStatusBar(strictPhase);
  drawCursor(permissive, 0, 0, 0, 160);  // white cursor (W-only)
  drawCursor(strict, 120, 0, 120, 0);    // magenta cursor
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
    Serial.print(strictPhase);
    Serial.print(',');
    Serial.print(reason);
    Serial.print(',');
    Serial.print(holdMs);
    Serial.print(',');
    Serial.println(compactFlags(link, rich, hardAbsent));
  }

  (void)core;
}
