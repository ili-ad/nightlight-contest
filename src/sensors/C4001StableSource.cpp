#include "C4001StableSource.h"

#include <Arduino.h>
#include <DFRobot_C4001.h>
#include <Wire.h>

namespace {
constexpr uint8_t kC4001I2cAddress = 0x2A;
constexpr uint32_t kPollIntervalMs = 33;
constexpr uint32_t kHoldMs = 420;

constexpr float kRangeNearM = 0.35f;
constexpr float kRangeFarM = 2.40f;
constexpr float kStillSpeedMps = 0.06f;

DFRobot_C4001_I2C gC4001(&Wire, kC4001I2cAddress);

float normalizeRange(float rangeM) {
  const float span = kRangeFarM - kRangeNearM;
  if (span <= 0.001f) {
    return 0.0f;
  }

  float t = (rangeM - kRangeNearM) / span;
  if (t < 0.0f) {
    t = 0.0f;
  }
  if (t > 1.0f) {
    t = 1.0f;
  }
  return t;
}

float chargeFromRange(float rangeM) {
  const float nearness = 1.0f - normalizeRange(rangeM);
  // Gentle near-field lift to preserve close-range elegance without blinking spikes.
  const float curved = nearness + (0.35f * nearness * (1.0f - nearness));
  return curved > 1.0f ? 1.0f : curved;
}
}  // namespace

void C4001StableSource::begin() {
  initialized_ = true;
  Wire.begin();
  sensorReady_ = gC4001.begin();
  if (sensorReady_) {
    gC4001.setSensorMode(eSpeedMode);
    gC4001.setDetectThres(11, 1200, 10);
    gC4001.setFrettingDetection(eON);
  }

  lastPollMs_ = 0;
  lastSeenMs_ = 0;
  stableHasTarget_ = false;
  stableRangeM_ = 1.2f;
  stableSpeedMps_ = 0.0f;
  smoothedCharge_ = 0.0f;
  smoothedIngress_ = 0.0f;
  continuity_ = 0.0f;
  phase_ = StableTrack::MotionPhase::None;
}

StableTrack C4001StableSource::read(uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  if (lastPollMs_ != 0 && (nowMs - lastPollMs_) < kPollIntervalMs) {
    StableTrack t;
    t.online = sensorReady_;
    t.hasTarget = stableHasTarget_;
    t.rangeM = stableRangeM_;
    t.speedMps = stableSpeedMps_;
    t.charge = smoothedCharge_;
    t.ingressLevel = smoothedIngress_;
    t.continuity = continuity_;
    t.phase = phase_;
    return t;
  }
  lastPollMs_ = nowMs;

  bool rawHasTarget = false;
  float rawRangeM = stableRangeM_;
  float rawSpeedMps = 0.0f;

  if (sensorReady_) {
    const int targetNumber = gC4001.getTargetNumber();
    if (targetNumber > 0) {
      const float sensedRange = gC4001.getTargetRange();
      const float sensedSpeed = gC4001.getTargetSpeed();
      if (sensedRange > 0.02f && sensedRange < 8.0f) {
        rawHasTarget = true;
        rawRangeM = sensedRange;
        rawSpeedMps = sensedSpeed;
      }
    }
  }

  if (rawHasTarget) {
    lastSeenMs_ = nowMs;
    stableHasTarget_ = true;

    const bool approaching = rawRangeM < stableRangeM_;
    const float rangeAlpha = approaching ? 0.26f : 0.14f;
    stableRangeM_ = smooth(stableRangeM_, rawRangeM, rangeAlpha);
    stableSpeedMps_ = smooth(stableSpeedM_, rawSpeedMps, 0.22f);
  } else {
    if (stableHasTarget_ && (nowMs - lastSeenMs_) > kHoldMs) {
      stableHasTarget_ = false;
    }
    stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, 0.10f);
  }

  const float targetCharge = stableHasTarget_ ? chargeFromRange(stableRangeM_) : 0.0f;
  smoothedCharge_ = smooth(smoothedCharge_, targetCharge, stableHasTarget_ ? 0.20f : 0.06f);

  const float ingressTarget = stableHasTarget_ ? (0.22f + (0.78f * smoothedCharge_)) : 0.0f;
  smoothedIngress_ = smooth(smoothedIngress_, ingressTarget, stableHasTarget_ ? 0.24f : 0.08f);

  const float continuityTarget = stableHasTarget_ ? 1.0f : 0.0f;
  continuity_ = smooth(continuity_, continuityTarget, stableHasTarget_ ? 0.30f : 0.03f);

  if (!stableHasTarget_) {
    phase_ = StableTrack::MotionPhase::None;
  } else if (fabsf(stableSpeedMps_) <= kStillSpeedMps) {
    phase_ = StableTrack::MotionPhase::Still;
  } else if (stableSpeedMps_ < 0.0f) {
    phase_ = StableTrack::MotionPhase::Approach;
  } else {
    phase_ = StableTrack::MotionPhase::Retreat;
  }

  StableTrack t;
  t.online = sensorReady_;
  t.hasTarget = stableHasTarget_;
  t.rangeM = stableRangeM_;
  t.speedMps = stableSpeedMps_;
  t.charge = smoothedCharge_;
  t.ingressLevel = smoothedIngress_;
  t.continuity = continuity_;
  t.phase = phase_;
  return t;
}

float C4001StableSource::clamp01(float v) {
  if (v < 0.0f) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}

float C4001StableSource::smooth(float previous, float target, float alpha) {
  const float a = clamp01(alpha);
  return previous + ((target - previous) * a);
}
