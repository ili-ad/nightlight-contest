#include "C4001StableSource.h"

#include <Arduino.h>
#include <DFRobot_C4001.h>
#include <Wire.h>

#include "../config/Profiles.h"

namespace {
constexpr uint8_t kC4001I2cAddress = 0x2B;
DFRobot_C4001_I2C gC4001(&Wire, kC4001I2cAddress);

float normalizeRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float span = profile.rangeFarM - profile.rangeNearM;
  if (span <= 0.001f) return 0.0f;
  float t = (rangeM - profile.rangeNearM) / span;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t;
}

float chargeFromRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float nearness = 1.0f - normalizeRange(rangeM, profile);
  const float curved = nearness + (profile.nearnessLiftGain * nearness * (1.0f - nearness));
  return curved > 1.0f ? 1.0f : curved;
}

}  // namespace

void C4001StableSource::begin() {
  initialized_ = true;
  if (!wireReady_) {
    Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
    Wire.setWireTimeout(25000, true);
#endif
    delay(60);
    wireReady_ = true;
  }

  sensorReady_ = false;
  manualInitRequested_ = false;
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

bool C4001StableSource::tryInit() {
  if (!initialized_) begin();
  if (!wireReady_) return false;
  if (!gC4001.begin()) return false;
  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
  sensorReady_ = true;
  return true;
}

void C4001StableSource::service(uint32_t nowMs) {
  (void)nowMs;
  if (!initialized_) begin();
  if (!manualInitRequested_) return;
  manualInitRequested_ = false;
  const bool wasReady = sensorReady_;
  sensorReady_ = tryInit();
  if (sensorReady_ && !wasReady) {
    Serial.println("event=c4001_read_resume");
  }
}

void C4001StableSource::requestManualInit() {
  if (!initialized_) begin();
  manualInitRequested_ = true;
}

StableTrack C4001StableSource::read(uint32_t nowMs) {
  const auto& profile = Profiles::c4001();
  if (!initialized_ || !sensorReady_) {
    StableTrack t;
    t.online = false;
    t.hasTarget = false;
    t.rangeM = stableRangeM_;
    t.speedMps = 0.0f;
    t.charge = 0.0f;
    t.ingressLevel = 0.0f;
    t.continuity = 0.0f;
    t.phase = StableTrack::MotionPhase::None;
    return t;
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

  bool accepted = false;
  float rawRangeM = stableRangeM_;
  float rawSpeedMps = 0.0f;

  if (sensorReady_) {
    const int targetNumber = gC4001.getTargetNumber();
    const float sensedRange = gC4001.getTargetRange();
    const float sensedSpeed = gC4001.getTargetSpeed();
    if (targetNumber > 0 && sensedRange >= profile.rangeNearM && sensedRange <= profile.rangeFarM) {
      accepted = true;
      rawRangeM = sensedRange;
      rawSpeedMps = sensedSpeed;
    }
  }

  if (accepted) {
    if (!stableHasTarget_ && lastSeenMs_ != 0) {
      Serial.println("event=c4001_read_resume");
    }
    stableHasTarget_ = true;
    lastSeenMs_ = nowMs;
    stableRangeM_ = rawRangeM;
    stableSpeedMps_ = smooth(stableSpeedMps_, rawSpeedMps, profile.speedAlpha);
    continuity_ = 1.0f;
  } else if (stableHasTarget_) {
    const uint32_t ageMs = nowMs - lastSeenMs_;
    if (ageMs <= profile.holdMs) {
      continuity_ = 1.0f;
      stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
    } else if (ageMs >= (profile.holdMs + 1500u)) {
      stableHasTarget_ = false;
      continuity_ = 0.0f;
      stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
    } else {
      const float t = float(ageMs - profile.holdMs) / 1500.0f;
      continuity_ = clamp01(1.0f - t);
      stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
    }
  } else {
    continuity_ = 0.0f;
    stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
  }

  const float targetCharge = stableHasTarget_ ? chargeFromRange(stableRangeM_, profile) * continuity_ : 0.0f;
  smoothedCharge_ = smooth(smoothedCharge_, targetCharge, stableHasTarget_ ? profile.chargeRiseAlpha : profile.chargeFallAlpha);
  const float motion = clamp01(fabsf(stableSpeedMps_) / 0.35f);
  const float ingressTarget = clamp01((motion * 0.78f) + (smoothedCharge_ * 0.26f));
  smoothedIngress_ = smooth(smoothedIngress_, ingressTarget, stableHasTarget_ ? profile.ingressRiseAlpha : profile.ingressFallAlpha);

  if (!stableHasTarget_ || continuity_ <= 0.001f) phase_ = StableTrack::MotionPhase::None;
  else if (fabsf(stableSpeedMps_) <= profile.stillSpeedMps) phase_ = StableTrack::MotionPhase::Still;
  else if (stableSpeedMps_ < 0.0f) phase_ = StableTrack::MotionPhase::Approach;
  else phase_ = StableTrack::MotionPhase::Retreat;

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
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float C4001StableSource::smooth(float previous, float target, float alpha) {
  const float a = clamp01(alpha);
  return previous + ((target - previous) * a);
}
