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
  constexpr uint8_t kMaxConsecutiveFailuresForOnline = 2;
  constexpr uint32_t kDropoutHoldMs = 450;
  constexpr float kConfidenceDecayPerFailure = 0.22f;

  constexpr float kAssumedUsefulRangeM = 6.0f;
  constexpr float kMotionSpeedScaleMps = 1.2f;

  constexpr float kConfidenceEmaAlpha = 0.30f;
  constexpr float kDistanceEmaAlpha = 0.35f;
  constexpr float kMotionEmaAlpha = 0.35f;

  float ema(float prev, float input, float alpha) {
    return prev + ((input - prev) * alpha);
  }

  bool i2cDeviceResponds(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  }
}

#if NIGHTLIGHT_HAS_C4001_LIB
namespace {
  DFRobot_C4001_I2C gC4001;
}
#endif

void PresenceC4001::begin() {
  initialized_ = true;
  consecutiveFailures_ = 0;
  confidenceEma_ = 0.0f;
  distanceEma_ = 0.0f;
  motionEma_ = 0.0f;
  lastCore_ = {};
  lastRich_ = {};

  Wire.begin();
  online_ = initSensor();
  lastCore_.online = online_;
  lastCore_.timestampMs = millis();
}

PresenceC4001::Snapshot PresenceC4001::read() {
  if (!initialized_) {
    begin();
  }

  const uint32_t nowMs = millis();

  C4001PresenceRich rich{};
  if (readSensorRich(rich)) {
    consecutiveFailures_ = 0;
    online_ = true;

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

bool PresenceC4001::initSensor() {
#if NIGHTLIGHT_HAS_C4001_LIB
  return gC4001.begin(BuildConfig::kC4001I2cAddress, &Wire);
#else
  return i2cDeviceResponds(BuildConfig::kC4001I2cAddress);
#endif
}

bool PresenceC4001::readSensorRich(C4001PresenceRich& outRich) {
#if NIGHTLIGHT_HAS_C4001_LIB
  if (!gC4001.update()) {
    return false;
  }

  outRich.targetNumber = gC4001.getTargetNumber();
  outRich.targetRangeM = gC4001.getTargetRange();
  outRich.targetSpeedMps = gC4001.getTargetSpeed();
  outRich.targetEnergy = gC4001.getTargetEnergy();
  return true;
#else
  return false;
#endif
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

CorePresence PresenceC4001::buildCoreFromRich(const C4001PresenceRich& rich, uint32_t nowMs) const {
  CorePresence core{};

  const bool targetSeen = (rich.targetNumber > 0);
  const float distanceNearness = clamp01(1.0f - (rich.targetRangeM / kAssumedUsefulRangeM));
  const float speedAbs = fabsf(rich.targetSpeedMps);
  const float motion = clamp01(speedAbs / kMotionSpeedScaleMps);

  const float baseConfidence = targetSeen ? (0.55f + (0.30f * distanceNearness) + (0.15f * motion)) : 0.0f;

  core.online = true;
  core.present = targetSeen;
  core.presenceConfidence = ema(confidenceEma_, clamp01(baseConfidence), kConfidenceEmaAlpha);
  core.distanceHint = ema(distanceEma_, distanceNearness, kDistanceEmaAlpha);
  core.motionHint = ema(motionEma_, motion, kMotionEmaAlpha);
  core.timestampMs = nowMs;

  return core;
}

PresenceC4001::Snapshot PresenceC4001::applyFailure(uint32_t nowMs) {
  ++consecutiveFailures_;
  online_ = (consecutiveFailures_ <= kMaxConsecutiveFailuresForOnline);

  CorePresence held = lastCore_;
  const bool withinHold = (nowMs - held.timestampMs) <= kDropoutHoldMs;

  if (withinHold) {
    held.online = online_;
    held.timestampMs = nowMs;
    held.presenceConfidence = clamp01(held.presenceConfidence * (1.0f - kConfidenceDecayPerFailure));
    if (held.presenceConfidence < BuildConfig::kPresenceExitThreshold) {
      held.present = false;
    }
    lastCore_ = held;
    confidenceEma_ = held.presenceConfidence;
    distanceEma_ = held.distanceHint;
    motionEma_ = held.motionHint;
    return {held, lastRich_};
  }

  held.online = online_;
  held.present = false;
  held.presenceConfidence = 0.0f;
  held.distanceHint = 0.0f;
  held.motionHint = 0.0f;
  held.timestampMs = nowMs;

  confidenceEma_ = 0.0f;
  distanceEma_ = 0.0f;
  motionEma_ = 0.0f;

  lastCore_ = held;
  return {held, lastRich_};
}
