#include "MapperC4001.h"

#include <math.h>

#include "../BuildConfig.h"

namespace {
constexpr float kRoomRangeNearM = 0.45f;
constexpr float kRoomRangeFarM = 6.50f;
constexpr float kSpeedStillThresholdMps = 0.08f;

float clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

float normalizeRange(float rangeM) {
  const float span = kRoomRangeFarM - kRoomRangeNearM;
  if (span <= 0.0f) {
    return 0.5f;
  }
  return clamp01((rangeM - kRoomRangeNearM) / span);
}

float normalizeEnergy(int energy) {
  return clamp01(static_cast<float>(energy) / 100.0f);
}

float normalizeSpeedMag(float speedMps) {
  return clamp01(fabsf(speedMps) / 1.50f);
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

float chargeAtRange(float rangeM) {
  const float nearness = clamp01(1.0f - normalizeRange(rangeM));
  return clamp01(nearness * BuildConfig::kAnthuriumDistanceToChargeGain);
}

float mappedChargeFromRange(float rangeM) {
  const float clampedRange = (rangeM < 0.0f) ? 0.0f : rangeM;
  const float baseCharge = chargeAtRange(clampedRange);
  const float nearStart = BuildConfig::kAnthuriumNearFieldCompressionStartM;
  if (nearStart <= 0.001f || clampedRange >= nearStart) {
    return baseCharge;
  }

  const float startCharge = chargeAtRange(nearStart);
  const float t = clamp01((nearStart - clampedRange) / nearStart);
  const float eased = t * t * (3.0f - 2.0f * t);
  const float nearCharge = startCharge + ((1.0f - startCharge) * eased);
  return clamp01((nearCharge > baseCharge) ? nearCharge : baseCharge);
}

float safeDeltaSec(uint32_t nowMs, uint32_t previousMs) {
  if (previousMs == 0 || nowMs <= previousMs) {
    return 0.0f;
  }
  return static_cast<float>(nowMs - previousMs) / 1000.0f;
}

float stepToward(float current, float target, float dtSec, float risePerSec, float fallPerSec) {
  if (dtSec <= 0.0f) {
    return target;
  }
  const float rate = (target >= current) ? risePerSec : fallPerSec;
  const float maxDelta = (rate <= 0.0f) ? fabsf(target - current) : (rate * dtSec);
  return clampDelta(current, target, maxDelta);
}

float validRangeOrZero(float value) {
  return (value > 0.0f) ? value : 0.0f;
}
}  // namespace

RenderIntent MapperC4001::map(const BehaviorContext& context,
                              const C4001PresenceRich& rich,
                              const PresenceC4001::LinkStatus& linkStatus) {
  if ((context.state != LampState::ActiveInterpretive) &&
      (context.state != LampState::Decay)) {
    resetSceneState();
    return mShared.map(context);
  }

  RenderIntent intent = (context.state == LampState::ActiveInterpretive)
                            ? mShared.mapActiveBaseline(context)
                            : mShared.mapDecayBaseline(context);
  intent.sceneNowMs = context.nowMs;

  const float dtSec = safeDeltaSec(context.nowMs, mLastSceneUpdateMs);
  mLastSceneUpdateMs = context.nowMs;

  mTrackFilter.configure(BuildConfig::kC4001DropoutHoldMs,
                         BuildConfig::kAnthuriumRejectedDecayPerSecond,
                         BuildConfig::kAnthuriumRejectedFloor);

  const bool allowValid = (context.state == LampState::ActiveInterpretive);
  const EffectiveSample sample = buildEffectiveSample(context, rich, linkStatus, allowValid);

  applySceneDriveSmoothing(dtSec, sample);

  const float speedMag = normalizeSpeedMag(sample.speedMps);
  return composeSceneIntent(context, intent, sample, speedMag);
}

MapperC4001::EffectiveSample MapperC4001::buildEffectiveSample(const BehaviorContext& context,
                                                               const C4001PresenceRich& rich,
                                                               const PresenceC4001::LinkStatus& linkStatus,
                                                               bool allowValid) {
  EffectiveSample sample{};
  C4001TrackFilter::InputClass inputClass = C4001TrackFilter::InputClass::HardAbsent;

  const bool hasTarget = (rich.targetNumber > 0);
  const bool hasRawRange = (rich.targetRangeRawM > 0.0f) &&
                           (rich.targetRangeRawM >= BuildConfig::kAnthuriumMinAcceptedRangeM);
  const bool hasAcceptedRange = (rich.targetRangeM > 0.0f) &&
                                (rich.targetRangeM >= BuildConfig::kAnthuriumMinAcceptedRangeM);
  const bool hasLinkIssue = (linkStatus.state == PresenceC4001::LinkState::Offline) ||
                            (linkStatus.sampleKind == PresenceC4001::SampleKind::ReadFailure);

  if (allowValid && hasTarget && hasRawRange && hasAcceptedRange &&
      rich.targetSampleAccepted && (rich.targetRejectedReason == 0u)) {
    acceptValidSample(context, rich);

    C4001TrackFilter::Sample accepted{};
    accepted.rangeM = mHeldRangeM;
    accepted.smoothedRangeM = mHeldSmoothedRangeM;
    accepted.chargeTarget = mHeldCharge;
    accepted.ingressTarget = mHeldIngressLevel;
    accepted.fieldTarget = mHeldFieldLevel;
    accepted.energyBoostTarget = mHeldEnergyBoost;
    accepted.speedMps = mHeldSpeedMps;
    accepted.energyNorm = mHeldEnergyNorm;
    const C4001TrackFilter::Output out =
        mTrackFilter.update(C4001TrackFilter::InputClass::Valid, context.nowMs, &accepted);

    sample.valid = true;
    sample.phase = out.phase;
    sample.rejectReason = 0u;
    sample.ageMs = out.ageMs;
    sample.rangeM = out.sample.rangeM;
    sample.smoothedRangeM = out.sample.smoothedRangeM;
    sample.chargeTarget = out.sample.chargeTarget;
    sample.ingressTarget = out.sample.ingressTarget;
    sample.fieldTarget = out.sample.fieldTarget;
    sample.energyBoostTarget = out.sample.energyBoostTarget;
    sample.speedMps = out.sample.speedMps;
    sample.energyNorm = out.sample.energyNorm;
    return sample;
  }

  if (hasLinkIssue) {
    inputClass = C4001TrackFilter::InputClass::LinkIssue;
  } else if (hasTarget && hasRawRange) {
    inputClass = C4001TrackFilter::InputClass::SoftReject;
    sample.rejectReason = static_cast<uint8_t>(rich.targetRejectedReason);
    if (sample.rejectReason == 0u) {
      sample.rejectReason = static_cast<uint8_t>(PresenceC4001::RejectReason::NearFieldCoherence);
    }
  } else {
    inputClass = C4001TrackFilter::InputClass::HardAbsent;
    sample.rejectReason = static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);
  }

  const C4001TrackFilter::Output out = mTrackFilter.update(inputClass, context.nowMs, nullptr);
  sample.valid = false;
  sample.phase = out.phase;
  sample.ageMs = out.ageMs;
  sample.rangeM = out.sample.rangeM;
  sample.smoothedRangeM = out.sample.smoothedRangeM;
  sample.chargeTarget = out.sample.chargeTarget;
  sample.ingressTarget = out.sample.ingressTarget;
  sample.fieldTarget = out.sample.fieldTarget;
  sample.energyBoostTarget = out.sample.energyBoostTarget;
  sample.speedMps = out.sample.speedMps;
  sample.energyNorm = out.sample.energyNorm;
  return sample;
}

void MapperC4001::acceptValidSample(const BehaviorContext& context,
                                    const C4001PresenceRich& rich) {
  if (!mHasSmoothedRange) {
    mSmoothedRangeM = rich.targetRangeM;
    mHasSmoothedRange = true;
  } else {
    const float rangeAlpha = clamp01(BuildConfig::kAnthuriumRangeSmoothingAlpha);
    mSmoothedRangeM += (rich.targetRangeM - mSmoothedRangeM) * rangeAlpha;
  }

  float chargeTarget = mappedChargeFromRange(mSmoothedRangeM);
  if (mHasChargeTarget && (mSmoothedRangeM <= BuildConfig::kAnthuriumNearFieldCompressionStartM)) {
    const float maxStep =
        (BuildConfig::kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate < 0.001f)
            ? 0.001f
            : BuildConfig::kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate;
    chargeTarget = clampDelta(mLastChargeTarget, chargeTarget, maxStep);
  }
  mHasChargeTarget = true;
  mLastChargeTarget = chargeTarget;

  mHeldCharge = chargeTarget;
  mHeldIngressLevel = clamp01(BuildConfig::kAnthuriumIngressBaseLevel + (chargeTarget * 0.75f));
  mHeldFieldLevel = clamp01(BuildConfig::kAnthuriumTorusFieldBaseLevel + (chargeTarget * 0.85f));
  mHeldEnergyNorm = normalizeEnergy(rich.targetEnergy);
  mHeldEnergyBoost = clamp01(mHeldEnergyNorm * BuildConfig::kAnthuriumEnergyWhiteBoostGain);
  mHeldSpeedMps = rich.targetSpeedMps;
  mHeldRangeM = rich.targetRangeM;
  mHeldSmoothedRangeM = mSmoothedRangeM;
  (void)context;
}

void MapperC4001::applySceneDriveSmoothing(float dtSec, const EffectiveSample& sample) {
  if (!mHasSceneDriveState) {
    mSceneCharge = sample.chargeTarget;
    mSceneIngressLevel = sample.ingressTarget;
    mSceneFieldLevel = sample.fieldTarget;
    mSceneEnergyBoost = sample.energyBoostTarget;
    mHasSceneDriveState = true;
    return;
  }

  constexpr float kChargeRisePerSec = 7.5f;
  constexpr float kChargeFallPerSec = 3.6f;
  constexpr float kIngressRisePerSec = 4.8f;
  constexpr float kIngressFallPerSec = 3.2f;
  constexpr float kFieldRisePerSec = 4.6f;
  constexpr float kFieldFallPerSec = 3.0f;
  constexpr float kEnergyRisePerSec = 5.5f;
  constexpr float kEnergyFallPerSec = 3.2f;

  mSceneCharge = clamp01(stepToward(mSceneCharge, sample.chargeTarget, dtSec, kChargeRisePerSec, kChargeFallPerSec));
  mSceneIngressLevel =
      clamp01(stepToward(mSceneIngressLevel, sample.ingressTarget, dtSec, kIngressRisePerSec, kIngressFallPerSec));
  mSceneFieldLevel =
      clamp01(stepToward(mSceneFieldLevel, sample.fieldTarget, dtSec, kFieldRisePerSec, kFieldFallPerSec));
  mSceneEnergyBoost =
      clamp01(stepToward(mSceneEnergyBoost, sample.energyBoostTarget, dtSec, kEnergyRisePerSec, kEnergyFallPerSec));
}

RenderIntent MapperC4001::composeSceneIntent(const BehaviorContext& context,
                                             RenderIntent intent,
                                             const EffectiveSample& sample,
                                             float speedMag) const {
  intent.activeSceneMode = ActiveSceneMode::AnthuriumReservoir;
  intent.sceneNowMs = context.nowMs;
  intent.sceneTargetRangeM = validRangeOrZero(sample.rangeM);
  intent.sceneTargetRangeSmoothedM = validRangeOrZero(sample.smoothedRangeM);
  intent.sceneSampleAgeMs = sample.ageMs;
  intent.sceneChargeTarget = sample.chargeTarget;
  intent.sceneCharge = mSceneCharge;
  intent.sceneIngressLevel = mSceneIngressLevel;
  intent.sceneFieldLevel = mSceneFieldLevel;
  intent.sceneEnergyBoost = mSceneEnergyBoost;
  intent.sceneDropoutPhase = static_cast<uint8_t>(sample.phase);
  intent.sceneRejectReason = sample.rejectReason;
  intent.useLocalizedBlob = false;

  if (sample.speedMps < -kSpeedStillThresholdMps) {
    intent.hue = 0.02f;
    intent.saturation = 0.86f;
  } else if (sample.speedMps > kSpeedStillThresholdMps) {
    intent.hue = 0.58f;
    intent.saturation = 0.78f;
  } else {
    intent.hue = 0.30f;
    intent.saturation = 0.20f;
  }

  const float stillnessBoost = 1.0f - speedMag;
  intent.rgbLevel = clamp01(0.11f + (intent.sceneCharge * 0.05f) + (speedMag * 0.05f));
  intent.whiteLevel = context.darkAllowed
                          ? clamp01(intent.whiteLevel + (sample.energyNorm * 0.04f) +
                                    (stillnessBoost * 0.02f))
                          : 0.0f;
  intent.effectId = static_cast<uint8_t>(context.state);
  return intent;
}

void MapperC4001::resetSceneState() {
  mHasSmoothedRange = false;
  mHasChargeTarget = false;
  mHasSceneDriveState = false;
  mLastSceneUpdateMs = 0;
  mSceneEnergyBoost = 0.0f;
  mTrackFilter.reset();
}
