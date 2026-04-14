#include "PresenceC4001.h"

#include <Arduino.h>
#include <math.h>
#include <Wire.h>

#include "../BuildConfig.h"

#if __has_include(<DFRobot_C4001.h>)
#include <DFRobot_C4001.h>
#define NIGHTLIGHT_HAS_C4001_LIB 1
#else
#define NIGHTLIGHT_HAS_C4001_LIB 0
#endif

namespace {
  constexpr float kAssumedUsefulRangeM = 6.0f;
  constexpr float kMotionSpeedScaleMps = 1.2f;

  float ema(float prev, float input, float alpha) {
    return prev + ((input - prev) * alpha);
  }
}

#if NIGHTLIGHT_HAS_C4001_LIB
namespace {
  DFRobot_C4001_I2C gC4001(&Wire, BuildConfig::kC4001I2cAddress);
}
#endif

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
#if NIGHTLIGHT_HAS_C4001_LIB
  if (!gC4001.begin()) {
    return false;
  }

  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
  return true;
#else
  return false;
#endif
}

bool PresenceC4001::readSensorRich(C4001PresenceRich& outRich) {
#if NIGHTLIGHT_HAS_C4001_LIB
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
#else
  return false;
#endif
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
  const float distanceNearness = clamp01(1.0f - (rich.targetRangeM / kAssumedUsefulRangeM));
  const float speedAbs = fabsf(rich.targetSpeedMps);
  const float motion = clamp01(speedAbs / kMotionSpeedScaleMps);

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

PresenceC4001::Snapshot PresenceC4001::applyFailure(uint32_t nowMs) {
  ++linkStatus_.consecutiveFailures;
  linkStatus_.lastFailureMs = nowMs;
  linkStatus_.lastSampleMs = nowMs;
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
