#include "PresenceC4001.h"

#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <DFRobot_C4001.h>

#include "../BuildConfig.h"

namespace {
  constexpr float kAssumedUsefulRangeM = 6.0f;
  constexpr float kMotionSpeedScaleMps = 1.2f;
  constexpr float kRoomRangeNearM = 0.45f;
  constexpr float kRoomRangeFarM = 6.50f;
  constexpr float kTakeoverNearerGainM = 0.30f;
  constexpr float kUsefulCorridorNearM = 0.20f;
  constexpr float kUsefulCorridorFarM = 2.20f;
  constexpr float kUsefulCorridorDeltaAllowanceM = 0.55f;

  float normalizeRange(float rangeM) {
    const float span = kRoomRangeFarM - kRoomRangeNearM;
    if (span <= 0.0f) {
      return 0.5f;
    }
    const float normalized = (rangeM - kRoomRangeNearM) / span;
    if (normalized < 0.0f) {
      return 0.0f;
    }
    if (normalized > 1.0f) {
      return 1.0f;
    }
    return normalized;
  }

  float normalizeEnergy(int energy) {
    const float normalized = static_cast<float>(energy) / 100.0f;
    if (normalized < 0.0f) {
      return 0.0f;
    }
    if (normalized > 1.0f) {
      return 1.0f;
    }
    return normalized;
  }

  float chargeAtRange(float rangeM) {
    const float nearness = 1.0f - normalizeRange(rangeM);
    const float charge = nearness * BuildConfig::kAnthuriumDistanceToChargeGain;
    if (charge < 0.0f) {
      return 0.0f;
    }
    if (charge > 1.0f) {
      return 1.0f;
    }
    return charge;
  }

  float mappedChargeFromRange(float rangeM) {
    const float clampedRange = (rangeM < 0.0f) ? 0.0f : rangeM;
    const float baseCharge = chargeAtRange(clampedRange);
    const float nearStart = BuildConfig::kAnthuriumNearFieldCompressionStartM;
    if (nearStart <= 0.001f || clampedRange >= nearStart) {
      return baseCharge;
    }

    const float startCharge = chargeAtRange(nearStart);
    float t = (nearStart - clampedRange) / nearStart;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
    const float eased = t * t * (3.0f - 2.0f * t);
    const float nearCharge = startCharge + ((1.0f - startCharge) * eased);
    return (nearCharge > baseCharge) ? nearCharge : baseCharge;
  }

  float clampDelta(float previous, float target, float maxDelta) {
    if (target > previous + maxDelta) {
      return previous + maxDelta;
    }
    if (target < previous - maxDelta) {
      return previous - maxDelta;
    }
    return target;
  }

  float smoothToward(float previous, float target, float riseAlpha, float fallAlpha) {
    const float alpha = (target >= previous) ? riseAlpha : fallAlpha;
    float clampedAlpha = alpha;
    if (clampedAlpha < 0.0f) {
      clampedAlpha = 0.0f;
    } else if (clampedAlpha > 1.0f) {
      clampedAlpha = 1.0f;
    }
    return previous + ((target - previous) * clampedAlpha);
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
  hasAcceptedTarget_ = false;
  lastAcceptedTargetMs_ = 0;
  acceptedRangeM_ = 0.0f;
  acceptedSpeedMps_ = 0.0f;
  stableTrackFilter_.reset();
  stableTrackFilter_.configure(BuildConfig::kC4001DropoutHoldMs,
                               BuildConfig::kAnthuriumRejectedDecayPerSecond,
                               BuildConfig::kAnthuriumRejectedFloor);
  stableHasSmoothedRange_ = false;
  stableHasChargeTarget_ = false;
  stableSmoothedRangeM_ = 0.0f;
  stableLastChargeTarget_ = 0.0f;
  coreHintsInitialized_ = false;
  corePresenceConfidence_ = 0.0f;
  coreDistanceHint_ = 0.0f;
  coreMotionHint_ = 0.0f;
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
  linkStatus_.nearField = false;
  linkStatus_.nearFieldPendingCount = 0;

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
      linkStatus_.nearField = false;
      clearNearFieldCoherence();
      ++linkStatus_.consecutiveNoTargetSamples;
      if (linkStatus_.noTargetSinceMs == 0) {
        linkStatus_.noTargetSinceMs = nowMs;
      }
      linkStatus_.noTargetHolding = false;
      linkStatus_.noTargetCommitted = true;
      applyStableTrack(rich, C4001TrackFilter::InputClass::HardAbsent, nowMs, nullptr);
      CorePresence core = buildCoreFromStableTrack(rich, nowMs);
      lastRich_ = rich;
      lastCore_ = core;
      return {core, rich};
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
      const C4001TrackFilter::Sample stableSample = buildStableSample(rich);
      applyStableTrack(rich, C4001TrackFilter::InputClass::Accepted, nowMs, &stableSample);
    } else {
      applyStableTrack(rich, C4001TrackFilter::InputClass::SoftReject, nowMs, nullptr);
    }

    CorePresence core = buildCoreFromStableTrack(rich, nowMs);
    lastRich_ = rich;
    lastCore_ = core;
    return {core, rich};
  }

  return applyFailure(nowMs, &rich);
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

CorePresence PresenceC4001::buildCoreFromStableTrack(const C4001PresenceRich& rich, uint32_t nowMs) {
  CorePresence core{};
  const bool hasStableTrack = rich.stableTrackHasTrack;
  const float stableRange = hasStableTrack ? rich.stableRangeM : 0.0f;
  const float stableSpeed = hasStableTrack ? rich.stableSpeedMps : 0.0f;
  const float stableVisibility = hasStableTrack ? clamp01(rich.stableVisibility) : 0.0f;
  const float stableInfluence = hasStableTrack ? clamp01(rich.stableInfluence) : 0.0f;
  const float distanceNearness = hasStableTrack ? clamp01(1.0f - (stableRange / kAssumedUsefulRangeM)) : 0.0f;
  const float motion = hasStableTrack ? clamp01(fabsf(stableSpeed) / kMotionSpeedScaleMps) : 0.0f;

  const float rawPresenceConfidence = stableInfluence;
  const float rawDistanceHint = distanceNearness * stableInfluence;
  const float rawMotionHint = motion * stableInfluence;

  if (!coreHintsInitialized_) {
    corePresenceConfidence_ = rawPresenceConfidence;
    coreDistanceHint_ = rawDistanceHint;
    coreMotionHint_ = rawMotionHint;
    coreHintsInitialized_ = true;
  } else {
    constexpr float kPresenceRiseAlpha = 0.26f;
    constexpr float kPresenceFallAlpha = 0.10f;
    constexpr float kDistanceRiseAlpha = 0.22f;
    constexpr float kDistanceFallAlpha = 0.11f;
    constexpr float kMotionRiseAlpha = 0.30f;
    constexpr float kMotionFallAlpha = 0.14f;

    corePresenceConfidence_ = smoothToward(corePresenceConfidence_,
                                           rawPresenceConfidence,
                                           kPresenceRiseAlpha,
                                           kPresenceFallAlpha);
    coreDistanceHint_ = smoothToward(coreDistanceHint_,
                                     rawDistanceHint,
                                     kDistanceRiseAlpha,
                                     kDistanceFallAlpha);
    coreMotionHint_ = smoothToward(coreMotionHint_,
                                   rawMotionHint,
                                   kMotionRiseAlpha,
                                   kMotionFallAlpha);
  }

  core.online = linkStatus_.online;
  core.present = hasStableTrack && (stableVisibility > 0.0f) && (corePresenceConfidence_ > 0.01f);
  core.presenceConfidence = corePresenceConfidence_;
  core.distanceHint = coreDistanceHint_;
  core.motionHint = coreMotionHint_;
  core.hasAngle = rich.hasAngle;
  core.angleNorm = rich.angleNorm;
  core.lateralBias = rich.lateralBias;
  core.timestampMs = nowMs;

  return core;
}

C4001TrackFilter::Sample PresenceC4001::buildStableSample(const C4001PresenceRich& rich) {
  C4001TrackFilter::Sample sample{};
  if (!stableHasSmoothedRange_) {
    stableSmoothedRangeM_ = rich.targetRangeM;
    stableHasSmoothedRange_ = true;
  } else {
    const float rangeAlpha = clamp01(BuildConfig::kAnthuriumRangeSmoothingAlpha);
    stableSmoothedRangeM_ += (rich.targetRangeM - stableSmoothedRangeM_) * rangeAlpha;
  }

  float chargeTarget = mappedChargeFromRange(stableSmoothedRangeM_);
  if (stableHasChargeTarget_ &&
      (stableSmoothedRangeM_ <= BuildConfig::kAnthuriumNearFieldCompressionStartM)) {
    const float maxStep =
        (BuildConfig::kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate < 0.001f)
            ? 0.001f
            : BuildConfig::kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate;
    chargeTarget = clampDelta(stableLastChargeTarget_, chargeTarget, maxStep);
  }
  stableHasChargeTarget_ = true;
  stableLastChargeTarget_ = chargeTarget;

  sample.rangeM = rich.targetRangeM;
  sample.smoothedRangeM = stableSmoothedRangeM_;
  sample.chargeTarget = chargeTarget;
  sample.ingressTarget = clamp01(BuildConfig::kAnthuriumIngressBaseLevel + (chargeTarget * 0.75f));
  sample.fieldTarget = clamp01(BuildConfig::kAnthuriumTorusFieldBaseLevel + (chargeTarget * 0.85f));
  sample.energyNorm = normalizeEnergy(rich.targetEnergy);
  sample.energyBoostTarget = clamp01(sample.energyNorm * BuildConfig::kAnthuriumEnergyWhiteBoostGain);
  sample.speedMps = rich.targetSpeedMps;
  return sample;
}

void PresenceC4001::applyStableTrack(C4001PresenceRich& rich,
                                     C4001TrackFilter::InputClass inputClass,
                                     uint32_t nowMs,
                                     const C4001TrackFilter::Sample* acceptedSample) {
  const C4001TrackFilter::Output stableOut = stableTrackFilter_.update(inputClass, nowMs, acceptedSample);
  rich.stableTrackHasTrack = stableOut.hasTrack;
  rich.stableTrackPhase = static_cast<uint8_t>(stableOut.phase);
  rich.stableTrackAgeMs = stableOut.ageMs;
  rich.stableRangeM = stableOut.sample.rangeM;
  rich.stableSmoothedRangeM = stableOut.sample.smoothedRangeM;
  rich.stableChargeTarget = stableOut.sample.chargeTarget;
  rich.stableIngressTarget = stableOut.sample.ingressTarget;
  rich.stableFieldTarget = stableOut.sample.fieldTarget;
  rich.stableEnergyBoostTarget = stableOut.sample.energyBoostTarget;
  rich.stableSpeedMps = stableOut.sample.speedMps;
  rich.stableEnergyNorm = stableOut.sample.energyNorm;
  rich.stableVisibility = stableOut.visibility;
  rich.stableInfluence = stableOut.influence;
}

PresenceC4001::Snapshot PresenceC4001::applyFailure(uint32_t nowMs, C4001PresenceRich* richForTrack) {
  ++linkStatus_.consecutiveFailures;
  linkStatus_.lastFailureMs = nowMs;
  linkStatus_.lastSampleMs = nowMs;
  linkStatus_.sampleKind = SampleKind::ReadFailure;
  linkStatus_.rejected = false;
  linkStatus_.rejectReason = RejectReason::None;
  linkStatus_.noTargetHolding = false;
  linkStatus_.noTargetCommitted = false;
  linkStatus_.nearField = false;
  linkStatus_.online =
      (linkStatus_.consecutiveFailures <= BuildConfig::kC4001MaxConsecutiveFailuresForOnline);
  lastRich_.targetNumber = 0;
  lastRich_.targetRangeM = 0.0f;
  lastRich_.targetSpeedMps = 0.0f;
  lastRich_.targetRangeRawM = 0.0f;
  lastRich_.targetSpeedRawM = 0.0f;
  lastRich_.targetSampleAccepted = false;
  lastRich_.targetRejectedReason = static_cast<uint8_t>(RejectReason::NoTarget);
  clearNearFieldCoherence();

  if (richForTrack != nullptr) {
    applyStableTrack(*richForTrack, C4001TrackFilter::InputClass::LinkIssue, nowMs, nullptr);
    lastRich_.stableTrackHasTrack = richForTrack->stableTrackHasTrack;
    lastRich_.stableTrackPhase = richForTrack->stableTrackPhase;
    lastRich_.stableTrackAgeMs = richForTrack->stableTrackAgeMs;
    lastRich_.stableRangeM = richForTrack->stableRangeM;
    lastRich_.stableSmoothedRangeM = richForTrack->stableSmoothedRangeM;
    lastRich_.stableChargeTarget = richForTrack->stableChargeTarget;
    lastRich_.stableIngressTarget = richForTrack->stableIngressTarget;
    lastRich_.stableFieldTarget = richForTrack->stableFieldTarget;
    lastRich_.stableEnergyBoostTarget = richForTrack->stableEnergyBoostTarget;
    lastRich_.stableSpeedMps = richForTrack->stableSpeedMps;
    lastRich_.stableEnergyNorm = richForTrack->stableEnergyNorm;
    lastRich_.stableVisibility = richForTrack->stableVisibility;
    lastRich_.stableInfluence = richForTrack->stableInfluence;
  } else {
    applyStableTrack(lastRich_, C4001TrackFilter::InputClass::LinkIssue, nowMs, nullptr);
  }

  CorePresence held = buildCoreFromStableTrack(lastRich_, nowMs);
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
    lastCore_ = held;
    return {held, lastRich_};
  }

  held.online = linkStatus_.online;
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

  if (rawRich.targetRangeM < BuildConfig::kAnthuriumMinAcceptedRangeM) {
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
    float maxDelta = BuildConfig::kC4001MaxAcceptedRangeDeltaPerSecond * dtSec;
    const bool candidateInUsefulCorridor =
        (rawRich.targetRangeM >= kUsefulCorridorNearM) && (rawRich.targetRangeM <= kUsefulCorridorFarM);
    const bool acceptedInUsefulCorridor =
        (acceptedRangeM_ >= kUsefulCorridorNearM) && (acceptedRangeM_ <= kUsefulCorridorFarM);
    if (candidateInUsefulCorridor || acceptedInUsefulCorridor) {
      maxDelta += kUsefulCorridorDeltaAllowanceM;
    }
    const float delta = fabsf(rawRich.targetRangeM - acceptedRangeM_);
    const bool nearerTakeover =
        (rawRich.targetRangeM < acceptedRangeM_) &&
        ((acceptedRangeM_ - rawRich.targetRangeM) >= kTakeoverNearerGainM);
    if (delta > maxDelta) {
      if (nearerTakeover) {
        clearNearFieldCoherence();
      } else {
        reason = RejectReason::RangeDelta;
        return false;
      }
    }
  }

  const bool nearField = (rawRich.targetRangeM <= BuildConfig::kC4001NearFieldStartM);
  linkStatus_.nearField = nearField;
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
  linkStatus_.nearFieldPendingCount = nearFieldPendingCount_;

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
  linkStatus_.nearFieldPendingCount = 0;
}
