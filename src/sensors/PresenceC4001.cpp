#include "PresenceC4001.h"

#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <DFRobot_C4001.h>

#include "../BuildConfig.h"

namespace {
  constexpr float kAssumedUsefulRangeM = 6.0f;
  constexpr float kMotionSpeedScaleMps = 1.2f;

  float ema(float prev, float input, float alpha) {
    return prev + ((input - prev) * alpha);
  }
}

namespace {
  DFRobot_C4001_I2C gC4001(&Wire, BuildConfig::kC4001I2cAddress);
}

void PresenceC4001::begin() {
  initialized_ = true;
  linkStatus_ = {};
  lastPollMs_ = 0;
  lastInitAttemptMs_ = 0;
  hasPolled_ = false;
  sensorReady_ = false;
  confidenceEma_ = 0.0f;
  distanceEma_ = 0.0f;
  motionEma_ = 0.0f;
  hasAcceptedTarget_ = false;
  lastAcceptedTargetMs_ = 0;
  acceptedRangeM_ = 0.0f;
  acceptedSpeedMps_ = 0.0f;
  clearNearFieldCoherence();
  lastCore_ = {};
  lastRich_ = {};

  Wire.begin();
  sensorReady_ = initSensor();
  const bool online = sensorReady_;
  linkStatus_.state = online ? LinkState::Online : LinkState::Offline;
  linkStatus_.online = online;
  linkStatus_.holding = false;
  linkStatus_.consecutiveFailures = 0;
  linkStatus_.lastSuccessMs = online ? millis() : 0;
  linkStatus_.lastFailureMs = 0;
  linkStatus_.lastSampleMs = 0;

  lastCore_.online = online;
  lastCore_.timestampMs = millis();
}

PresenceC4001::Snapshot PresenceC4001::read() {
  if (!initialized_) {
    begin();
  }

  const uint32_t nowMs = millis();
  if (!shouldPoll(nowMs)) {
    return {lastCore_, lastRich_};
  }
  lastPollMs_ = nowMs;
  hasPolled_ = true;
  linkStatus_.lastSampleMs = nowMs;

  if (!sensorReady_ && shouldAttemptInit(nowMs)) {
    lastInitAttemptMs_ = nowMs;
    sensorReady_ = initSensor();
  }

  if (!sensorReady_) {
    return applyFailure(nowMs);
  }

  C4001PresenceRich rich{};
  if (readSensorRich(rich)) {
    linkStatus_.consecutiveFailures = 0;
    linkStatus_.online = true;
    linkStatus_.holding = false;
    linkStatus_.state = LinkState::Online;
    linkStatus_.lastSuccessMs = nowMs;
    linkStatus_.sampleKind = (rich.targetNumber > 0) ? SampleKind::Target : SampleKind::NoTarget;
    linkStatus_.rejected = false;
    linkStatus_.rejectReason = RejectReason::None;

    if (rich.targetNumber <= 0) {
      clearNearFieldCoherence();
      return applyNoTargetSuccess(rich, nowMs);
    }

    linkStatus_.consecutiveNoTargetSamples = 0;
    linkStatus_.noTargetSinceMs = 0;
    linkStatus_.lastTargetMs = nowMs;
    linkStatus_.noTargetHolding = false;
    linkStatus_.noTargetCommitted = false;

    rich.targetRangeRawM = rich.targetRangeM;
    rich.targetSpeedRawM = rich.targetSpeedMps;
    rich.targetSampleAccepted = false;
    rich.targetRejectedReason = static_cast<uint8_t>(RejectReason::None);

    float acceptedRangeM = rich.targetRangeM;
    float acceptedSpeedMps = rich.targetSpeedMps;
    RejectReason rejectReason = RejectReason::None;
    const bool accepted =
        acceptTargetSample(rich, nowMs, acceptedRangeM, acceptedSpeedMps, rejectReason);
    if (!accepted && hasAcceptedTarget_) {
      acceptedRangeM = acceptedRangeM_;
      acceptedSpeedMps = acceptedSpeedMps_;
    }

    rich.targetRangeM = acceptedRangeM;
    rich.targetSpeedMps = acceptedSpeedMps;
    rich.targetSampleAccepted = accepted;
    rich.targetRejectedReason = static_cast<uint8_t>(rejectReason);
    linkStatus_.rejected = !accepted;
    linkStatus_.rejectReason = rejectReason;

    if (accepted) {
      hasAcceptedTarget_ = true;
      lastAcceptedTargetMs_ = nowMs;
      acceptedRangeM_ = acceptedRangeM;
      acceptedSpeedMps_ = acceptedSpeedMps;
    }

    CorePresence core = buildCoreFromRich(rich, nowMs);
    lastRich_ = rich;
    lastCore_ = core;
    return {core, rich};
  }

  return applyFailure(nowMs);
}

const C4001PresenceRich& PresenceC4001::lastRich() const {
  return lastRich_;
}

const PresenceC4001::LinkStatus& PresenceC4001::linkStatus() const {
  return linkStatus_;
}

bool PresenceC4001::initSensor() {
  if (!gC4001.begin()) {
    return false;
  }

  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
  return true;
}

bool PresenceC4001::readSensorRich(C4001PresenceRich& outRich) {
  // C4001 API audit (current DFRobot I2C path used by this repo):
  // - getTargetNumber()
  // - getTargetRange()
  // - getTargetSpeed()
  // - getTargetEnergy()
  // No angle/azimuth/lateral/XY/beam-index accessor is used or exposed in this path,
  // so directional placeholders remain intentionally neutral.
  outRich.targetNumber = gC4001.getTargetNumber();
  outRich.targetRangeM = gC4001.getTargetRange();
  outRich.targetSpeedMps = gC4001.getTargetSpeed();
  outRich.targetEnergy = gC4001.getTargetEnergy();
  outRich.hasAngle = false;
  outRich.angleNorm = 0.0f;
  outRich.lateralBias = 0.0f;
  return true;
}

bool PresenceC4001::shouldAttemptInit(uint32_t nowMs) const {
  if (lastInitAttemptMs_ == 0) {
    return true;
  }
  return (nowMs - lastInitAttemptMs_) >= BuildConfig::kC4001ReinitIntervalMs;
}

bool PresenceC4001::shouldPoll(uint32_t nowMs) const {
  if (!hasPolled_) {
    return true;
  }
  return (nowMs - lastPollMs_) >= BuildConfig::kC4001PollIntervalMs;
}

float PresenceC4001::clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

float PresenceC4001::decayTowardZero(float value, float decayPerFailure) {
  return clamp01(value * (1.0f - decayPerFailure));
}

CorePresence PresenceC4001::buildCoreFromRich(const C4001PresenceRich& rich, uint32_t nowMs) {
  CorePresence core{};

  const bool targetSeen = (rich.targetNumber > 0);
  float distanceNearness = 0.0f;
  float motion = 0.0f;
  if (targetSeen) {
    distanceNearness = clamp01(1.0f - (rich.targetRangeM / kAssumedUsefulRangeM));
    const float speedAbs = fabsf(rich.targetSpeedMps);
    motion = clamp01(speedAbs / kMotionSpeedScaleMps);
  }

  const float baseConfidence = targetSeen ? (0.55f + (0.30f * distanceNearness) + (0.15f * motion)) : 0.0f;

  confidenceEma_ = ema(confidenceEma_, clamp01(baseConfidence), BuildConfig::kC4001ConfidenceEmaAlpha);
  distanceEma_ = ema(distanceEma_, distanceNearness, BuildConfig::kC4001DistanceEmaAlpha);
  motionEma_ = ema(motionEma_, motion, BuildConfig::kC4001MotionEmaAlpha);

  core.online = true;
  core.present = targetSeen;
  core.presenceConfidence = confidenceEma_;
  core.distanceHint = distanceEma_;
  core.motionHint = motionEma_;
  core.hasAngle = rich.hasAngle;
  core.angleNorm = rich.angleNorm;
  core.lateralBias = rich.lateralBias;
  core.timestampMs = nowMs;

  return core;
}

PresenceC4001::Snapshot PresenceC4001::applyNoTargetSuccess(const C4001PresenceRich& rich,
                                                            uint32_t nowMs) {
  CorePresence core = lastCore_;
  lastRich_ = rich;

  ++linkStatus_.consecutiveNoTargetSamples;
  if (linkStatus_.noTargetSinceMs == 0) {
    linkStatus_.noTargetSinceMs = nowMs;
  }

  const uint32_t msSinceLastTarget =
      (linkStatus_.lastTargetMs == 0) ? 0xFFFFFFFFu : (nowMs - linkStatus_.lastTargetMs);
  const uint32_t noTargetDurationMs = nowMs - linkStatus_.noTargetSinceMs;
  const bool withinNoTargetGrace =
      (linkStatus_.lastTargetMs != 0) && (msSinceLastTarget <= BuildConfig::kC4001NoTargetGraceMs);
  const bool sustainedNoTarget =
      (linkStatus_.consecutiveNoTargetSamples >= BuildConfig::kC4001NoTargetRequiredConsecutiveSamples) &&
      (noTargetDurationMs >= BuildConfig::kC4001NoTargetRequiredWindowMs);

  linkStatus_.noTargetHolding = withinNoTargetGrace;
  linkStatus_.noTargetCommitted = !withinNoTargetGrace && sustainedNoTarget;

  const float decay = withinNoTargetGrace ? BuildConfig::kC4001NoTargetGraceDecayPerSample
                                          : BuildConfig::kC4001NoTargetDecayPerSample;

  core.online = true;
  core.presenceConfidence = decayTowardZero(core.presenceConfidence, decay);
  core.distanceHint = decayTowardZero(core.distanceHint, decay);
  core.motionHint = decayTowardZero(core.motionHint, decay);
  core.timestampMs = nowMs;

  if (linkStatus_.noTargetCommitted) {
    core.present = false;
    core.presenceConfidence = 0.0f;
    core.distanceHint = 0.0f;
    core.motionHint = 0.0f;
  } else {
    core.present = withinNoTargetGrace || (core.presenceConfidence > BuildConfig::kPresenceExitThreshold);
  }

  confidenceEma_ = core.presenceConfidence;
  distanceEma_ = core.distanceHint;
  motionEma_ = core.motionHint;
  lastCore_ = core;
  return {core, rich};
}

PresenceC4001::Snapshot PresenceC4001::applyFailure(uint32_t nowMs) {
  ++linkStatus_.consecutiveFailures;
  linkStatus_.lastFailureMs = nowMs;
  linkStatus_.lastSampleMs = nowMs;
  linkStatus_.sampleKind = SampleKind::ReadFailure;
  linkStatus_.rejected = false;
  linkStatus_.rejectReason = RejectReason::None;
  linkStatus_.noTargetHolding = false;
  linkStatus_.noTargetCommitted = false;
  linkStatus_.online =
      (linkStatus_.consecutiveFailures <= BuildConfig::kC4001MaxConsecutiveFailuresForOnline);

  CorePresence held = lastCore_;
  const uint32_t msSinceSuccess =
      (linkStatus_.lastSuccessMs == 0) ? 0xFFFFFFFFu : (nowMs - linkStatus_.lastSuccessMs);
  const bool withinHold = (msSinceSuccess <= BuildConfig::kC4001DropoutHoldMs);
  const bool forceEmpty = (msSinceSuccess >= BuildConfig::kC4001DropoutForceEmptyMs);
  linkStatus_.holding = withinHold;
  linkStatus_.state = linkStatus_.online ? (withinHold ? LinkState::DegradedHold : LinkState::Online)
                                         : LinkState::Offline;

  if (withinHold) {
    held.online = linkStatus_.online;
    held.timestampMs = nowMs;
    held.presenceConfidence = decayTowardZero(held.presenceConfidence, BuildConfig::kC4001ConfidenceDecayPerFailure);
    held.distanceHint = decayTowardZero(held.distanceHint, BuildConfig::kC4001DistanceDecayPerFailure);
    held.motionHint = decayTowardZero(held.motionHint, BuildConfig::kC4001MotionDecayPerFailure);

    if (held.presenceConfidence <= BuildConfig::kPresenceExitThreshold) {
      held.present = false;
    }
    lastCore_ = held;
    confidenceEma_ = held.presenceConfidence;
    distanceEma_ = held.distanceHint;
    motionEma_ = held.motionHint;
    return {held, lastRich_};
  }

  held.online = linkStatus_.online;
  held.presenceConfidence = decayTowardZero(held.presenceConfidence, BuildConfig::kC4001ConfidenceDecayPerFailure);
  held.distanceHint = decayTowardZero(held.distanceHint, BuildConfig::kC4001DistanceDecayPerFailure);
  held.motionHint = decayTowardZero(held.motionHint, BuildConfig::kC4001MotionDecayPerFailure);
  held.present = (held.presenceConfidence > BuildConfig::kPresenceExitThreshold);
  held.timestampMs = nowMs;

  if (forceEmpty) {
    held.present = false;
    held.online = false;
    held.presenceConfidence = 0.0f;
    held.distanceHint = 0.0f;
    held.motionHint = 0.0f;
    linkStatus_.holding = false;
    linkStatus_.state = LinkState::Offline;
  }

  confidenceEma_ = held.presenceConfidence;
  distanceEma_ = held.distanceHint;
  motionEma_ = held.motionHint;

  lastCore_ = held;
  return {held, lastRich_};
}

bool PresenceC4001::acceptTargetSample(const C4001PresenceRich& rawRich,
                                       uint32_t nowMs,
                                       float& acceptedRangeM,
                                       float& acceptedSpeedMps,
                                       RejectReason& reason) {
  reason = RejectReason::None;
  acceptedRangeM = rawRich.targetRangeM;
  acceptedSpeedMps = rawRich.targetSpeedMps;

  if (rawRich.targetRangeM <= 0.01f) {
    reason = RejectReason::RangeDelta;
    return false;
  }

  if (fabsf(rawRich.targetSpeedMps) > BuildConfig::kC4001MaxAcceptedSpeedMps) {
    reason = RejectReason::SpeedCap;
    return false;
  }

  if (hasAcceptedTarget_) {
    const uint32_t dtMs = (lastAcceptedTargetMs_ == 0) ? BuildConfig::kC4001PollIntervalMs
                                                       : (nowMs - lastAcceptedTargetMs_);
    const float dtSec = static_cast<float>((dtMs == 0) ? 1 : dtMs) / 1000.0f;
    const float maxDelta = BuildConfig::kC4001MaxAcceptedRangeDeltaPerSecond * dtSec;
    const float delta = fabsf(rawRich.targetRangeM - acceptedRangeM_);
    if (delta > maxDelta) {
      reason = RejectReason::RangeDelta;
      return false;
    }
  }

  const bool nearField = (rawRich.targetRangeM <= BuildConfig::kC4001NearFieldStartM);
  if (!nearField) {
    clearNearFieldCoherence();
    return true;
  }

  if (!hasAcceptedTarget_) {
    return true;
  }

  const float nearDelta = fabsf(rawRich.targetRangeM - acceptedRangeM_);
  if (nearDelta <= BuildConfig::kC4001NearFieldStrongSwingM) {
    clearNearFieldCoherence();
    return true;
  }

  if (hasNearFieldPending_ &&
      (fabsf(rawRich.targetRangeM - nearFieldPendingRangeM_) <= BuildConfig::kC4001NearFieldCoherenceBandM)) {
    ++nearFieldPendingCount_;
    nearFieldPendingRangeM_ = (nearFieldPendingRangeM_ + rawRich.targetRangeM) * 0.5f;
    nearFieldPendingSpeedMps_ = (nearFieldPendingSpeedMps_ + rawRich.targetSpeedMps) * 0.5f;
  } else {
    hasNearFieldPending_ = true;
    nearFieldPendingCount_ = 1;
    nearFieldPendingRangeM_ = rawRich.targetRangeM;
    nearFieldPendingSpeedMps_ = rawRich.targetSpeedMps;
  }

  if (nearFieldPendingCount_ < BuildConfig::kC4001NearFieldCoherentSamples) {
    reason = RejectReason::NearFieldCoherence;
    return false;
  }

  acceptedRangeM = nearFieldPendingRangeM_;
  acceptedSpeedMps = nearFieldPendingSpeedMps_;
  clearNearFieldCoherence();
  return true;
}

void PresenceC4001::clearNearFieldCoherence() {
  hasNearFieldPending_ = false;
  nearFieldPendingCount_ = 0;
  nearFieldPendingRangeM_ = 0.0f;
  nearFieldPendingSpeedMps_ = 0.0f;
}
