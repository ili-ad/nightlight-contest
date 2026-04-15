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
#elif __has_include("../src/BuildConfig.h")
#include "../src/BuildConfig.h"
#include "../src/Pins.h"
#include "../src/mapping/C4001TrackFilter.h"
#include "../src/sensors/PresenceManager.h"
#else
#include <Wire.h>
#include <DFRobot_C4001.h>

namespace BuildConfig {
constexpr uint16_t kTotalPixels = 77;
constexpr uint32_t kC4001DropoutHoldMs = 450;
constexpr float kAnthuriumRejectedDecayPerSecond = 2.40f;
constexpr float kAnthuriumRejectedFloor = 0.0f;
}

namespace Pins {
constexpr uint8_t kPixelDataPin = 6;
}

struct CorePresence {};

struct C4001PresenceRich {
  int targetNumber = 0;
  float targetRangeM = 0.0f;
  float targetSpeedMps = 0.0f;
  float targetRangeRawM = 0.0f;
  float targetSpeedRawM = 0.0f;
  bool targetSampleAccepted = false;
  uint8_t targetRejectedReason = 0;
  int targetEnergy = 0;
};

class PresenceC4001 {
public:
  enum class LinkState : uint8_t { Online, DegradedHold, Offline };
  enum class SampleKind : uint8_t { Unknown, Target, NoTarget, ReadFailure };
  enum class RejectReason : uint8_t { None = 0, SpeedCap, RangeDelta, NearFieldCoherence, NoTarget };

  struct LinkStatus {
    LinkState state = LinkState::Offline;
    SampleKind sampleKind = SampleKind::Unknown;
    bool nearField = false;
  };
};

class C4001TrackFilter {
public:
  enum class InputClass : uint8_t { Valid = 0, SoftReject = 1, HardAbsent = 2, LinkIssue = 3 };
  enum class Phase : uint8_t { Valid = 0, Hold = 1, SoftReject = 2, HardAbsent = 3, Decay = 4, Empty = 5, LinkIssue = 6 };
  struct Sample {
    float rangeM = 0.0f, smoothedRangeM = 0.0f, chargeTarget = 0.0f, ingressTarget = 0.0f;
    float fieldTarget = 0.0f, energyBoostTarget = 0.0f, speedMps = 0.0f, energyNorm = 0.0f;
  };
  struct Output { Sample sample{}; Phase phase = Phase::Empty; uint32_t ageMs = 0; bool hasTrack = false; };

  void configure(uint32_t holdMs, float decayPerSecond, float decayFloor) { mHoldMs = holdMs; mDecayPerSecond = decayPerSecond; mDecayFloor = decayFloor; }
  Output update(InputClass inputClass, uint32_t nowMs, const Sample* validSample = nullptr) {
    if (inputClass == InputClass::Valid && validSample != nullptr) {
      mHasTrack = true; mLastAcceptedMs = nowMs; mHeld = *validSample;
      return {mHeld, Phase::Valid, 0u, true};
    }
    if (!mHasTrack) return {};
    Output out{mHeld, Phase::Hold, (nowMs >= mLastAcceptedMs) ? (nowMs - mLastAcceptedMs) : 0u, true};
    if (inputClass == InputClass::SoftReject) { out.phase = Phase::SoftReject; return out; }
    if (out.ageMs <= mHoldMs) { out.phase = (inputClass == InputClass::LinkIssue) ? Phase::LinkIssue : Phase::Hold; return out; }
    const float ageSec = static_cast<float>(out.ageMs - mHoldMs) / 1000.0f;
    float scale = 1.0f - (ageSec * mDecayPerSecond);
    if (scale < mDecayFloor) scale = 0.0f;
    out.sample.rangeM *= scale; out.sample.smoothedRangeM *= scale; out.sample.chargeTarget *= scale;
    out.sample.ingressTarget *= scale; out.sample.fieldTarget *= scale; out.sample.energyBoostTarget *= scale;
    out.sample.speedMps *= scale; out.sample.energyNorm *= scale;
    out.phase = (scale > 0.0f) ? Phase::Decay : Phase::Empty;
    out.hasTrack = (scale > 0.0f);
    if (!out.hasTrack) mHasTrack = false;
    return out;
  }

private:
  uint32_t mHoldMs = 450, mLastAcceptedMs = 0;
  float mDecayPerSecond = 2.4f, mDecayFloor = 0.0f;
  bool mHasTrack = false;
  Sample mHeld{};
};

class PresenceManager {
public:
  void begin() {
    Wire.begin();
    mReady = mRadar.begin();
    if (mReady) {
      mRadar.setSensorMode(eSpeedMode);
      mRadar.setDetectThres(11, 1200, 10);
      mRadar.setFrettingDetection(eON);
      mLink.state = PresenceC4001::LinkState::Online;
    }
  }

  CorePresence readCore() {
    if (!mReady) {
      mLink.state = PresenceC4001::LinkState::Offline;
      mLink.sampleKind = PresenceC4001::SampleKind::ReadFailure;
      return {};
    }
    mRich.targetNumber = mRadar.getTargetNumber();
    mRich.targetRangeRawM = mRadar.getTargetRange();
    mRich.targetSpeedRawM = mRadar.getTargetSpeed();
    mRich.targetEnergy = mRadar.getTargetEnergy();
    mRich.targetRangeM = mRich.targetRangeRawM;
    mRich.targetSpeedMps = mRich.targetSpeedRawM;
    mRich.targetSampleAccepted = (mRich.targetNumber > 0) && (mRich.targetRangeRawM > 0.06f);
    mRich.targetRejectedReason = mRich.targetSampleAccepted ? 0u : static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);
    mLink.state = PresenceC4001::LinkState::Online;
    mLink.sampleKind = (mRich.targetNumber > 0) ? PresenceC4001::SampleKind::Target : PresenceC4001::SampleKind::NoTarget;
    mLink.nearField = (mRich.targetRangeRawM > 0.0f) && (mRich.targetRangeRawM <= 1.80f);
    return {};
  }

  const C4001PresenceRich& lastC4001Rich() const { return mRich; }
  const PresenceC4001::LinkStatus& c4001LinkStatus() const { return mLink; }

private:
  DFRobot_C4001_I2C mRadar{&Wire, 0x2B};
  bool mReady = false;
  C4001PresenceRich mRich{};
  PresenceC4001::LinkStatus mLink{};
};
#endif

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
