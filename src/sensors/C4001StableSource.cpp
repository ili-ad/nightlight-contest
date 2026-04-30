#include "C4001StableSource.h"

#include <Arduino.h>
#include <DFRobot_C4001.h>
#include <Wire.h>

#include "../config/Profiles.h"

namespace {
DFRobot_C4001_I2C gC4001(&Wire, Profiles::c4001().i2cAddress);

float normalizeRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float span = profile.rangeFarM - profile.rangeNearM;
  if (span <= 0.001f) {
    return 0.0f;
  }

  float t = (rangeM - profile.rangeNearM) / span;
  if (t < 0.0f) {
    t = 0.0f;
  }
  if (t > 1.0f) {
    t = 1.0f;
  }
  return t;
}

float chargeFromRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float nearness = 1.0f - normalizeRange(rangeM, profile);
  // Gentle near-field lift to preserve close-range elegance without blinking spikes.
  const float curved = nearness + (profile.nearnessLiftGain * nearness * (1.0f - nearness));
  return curved > 1.0f ? 1.0f : curved;
}

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  const uint8_t status = Wire.endTransmission();
  return status == 0;
}

void configureSensor() {
  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
}
}  // namespace

void C4001StableSource::begin() {
  initialized_ = true;
  if (!wireReady_) {
    Wire.begin();
    wireReady_ = true;
  }
  sensorReady_ = false;
  manualInitRequested_ = false;
  lastPollMs_ = 0;
  lastInitAttemptMs_ = 0;
  lastSeenMs_ = 0;
  stableHasTarget_ = false;
  stableRangeM_ = 1.2f;
  stableSpeedMps_ = 0.0f;
  smoothedCharge_ = 0.0f;
  smoothedIngress_ = 0.0f;
  continuity_ = 0.0f;
  phase_ = StableTrack::MotionPhase::None;
}

void C4001StableSource::service(uint32_t nowMs) {
  const auto& profile = Profiles::c4001();
  if (!initialized_) {
    begin();
  }

  if (sensorReady_) {
    return;
  }

  const bool retryElapsed = (lastInitAttemptMs_ == 0 || (nowMs - lastInitAttemptMs_) >= profile.initRetryMs);
  if (!retryElapsed) {
    return;
  }

  const bool manualAttempt = manualInitRequested_;
  const bool autoAttempt = (!manualAttempt && profile.enableC4001AutoInit);
  if (!manualAttempt && !autoAttempt) {
    return;
  }

  manualInitRequested_ = false;
  lastInitAttemptMs_ = nowMs;
  if (manualAttempt) {
    Serial.println("event=c4001_init_attempt_manual");
  } else {
    Serial.println("event=c4001_init_attempt_auto");
  }

  if (!wireReady_) {
    Wire.begin();
    wireReady_ = true;
  }

  if (!i2cDevicePresent(profile.i2cAddress)) {
    Serial.println("warn=c4001_init_failed");
    return;
  }

  sensorReady_ = gC4001.begin();
  if (sensorReady_) {
    configureSensor();
    Serial.println("event=c4001_init_recovered");
  } else {
    Serial.println("warn=c4001_init_failed");
  }
}

void C4001StableSource::requestManualInit() {
  if (!initialized_) {
    begin();
  }
  manualInitRequested_ = true;
}

StableTrack C4001StableSource::read(uint32_t nowMs) {
  const auto& profile = Profiles::c4001();
  if (!initialized_) {
    begin();
  }
  if (lastPollMs_ != 0 && (nowMs - lastPollMs_) < profile.pollIntervalMs) {
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
    const float rangeAlpha = approaching ? profile.approachRangeAlpha : profile.retreatRangeAlpha;
    stableRangeM_ = smooth(stableRangeM_, rawRangeM, rangeAlpha);
    stableSpeedMps_ = smooth(stableSpeedMps_, rawSpeedMps, profile.speedAlpha);
  } else {
    if (stableHasTarget_ && (nowMs - lastSeenMs_) > profile.holdMs) {
      stableHasTarget_ = false;
    }
    stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
  }

  const float targetCharge = stableHasTarget_ ? chargeFromRange(stableRangeM_, profile) : 0.0f;
  smoothedCharge_ = smooth(smoothedCharge_, targetCharge,
                           stableHasTarget_ ? profile.chargeRiseAlpha : profile.chargeFallAlpha);

  const float ingressTarget = stableHasTarget_ ? (profile.ingressBase + (profile.ingressGain * smoothedCharge_)) : 0.0f;
  smoothedIngress_ = smooth(smoothedIngress_, ingressTarget,
                            stableHasTarget_ ? profile.ingressRiseAlpha : profile.ingressFallAlpha);

  const float continuityTarget = stableHasTarget_ ? 1.0f : 0.0f;
  continuity_ = smooth(continuity_, continuityTarget,
                       stableHasTarget_ ? profile.continuityRiseAlpha : profile.continuityFallAlpha);

  if (!stableHasTarget_) {
    phase_ = StableTrack::MotionPhase::None;
  } else if (fabsf(stableSpeedMps_) <= profile.stillSpeedMps) {
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
