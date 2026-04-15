#include "MapperC4001.h"

#include <math.h>

namespace {
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

float normalizeSpeedMag(float speedMps) {
  return clamp01(fabsf(speedMps) / 1.50f);
}

float safeDeltaSec(uint32_t nowMs, uint32_t previousMs) {
  if (previousMs == 0 || nowMs <= previousMs) {
    return 0.0f;
  }
  return static_cast<float>(nowMs - previousMs) / 1000.0f;
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

  const bool allowValid =
      (context.state == LampState::ActiveInterpretive) || (context.state == LampState::Decay);
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
  sample.sampleClass = SampleClass::HardAbsent;
  sample.valid = allowValid && rich.stableTrackHasTrack;
  sample.phase = static_cast<C4001TrackFilter::Phase>(rich.stableTrackPhase);
  sample.ageMs = rich.stableTrackAgeMs;
  sample.rangeM = rich.stableRangeM;
  sample.smoothedRangeM = rich.stableSmoothedRangeM;
  sample.chargeTarget = rich.stableChargeTarget;
  sample.ingressTarget = rich.stableIngressTarget;
  sample.fieldTarget = rich.stableFieldTarget;
  sample.energyBoostTarget = rich.stableEnergyBoostTarget;
  sample.speedMps = rich.stableSpeedMps;
  sample.energyNorm = rich.stableEnergyNorm;
  sample.rejectReason = static_cast<uint8_t>(rich.targetRejectedReason);

  if (rich.targetSampleAccepted && sample.valid) {
    sample.sampleClass = SampleClass::Accepted;
    sample.rejectReason = 0u;
  } else if ((linkStatus.sampleKind == PresenceC4001::SampleKind::ReadFailure) ||
             (linkStatus.state == PresenceC4001::LinkState::Offline)) {
    sample.sampleClass = SampleClass::HardAbsent;
    sample.rejectReason = static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);
  } else if (rich.targetNumber > 0) {
    sample.sampleClass = SampleClass::SoftReject;
    if (sample.rejectReason == 0u) {
      sample.rejectReason = static_cast<uint8_t>(PresenceC4001::RejectReason::NearFieldCoherence);
    }
  } else {
    sample.sampleClass = SampleClass::HardAbsent;
    sample.rejectReason = static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);
  }
  return sample;
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
  mHasSceneDriveState = false;
  mLastSceneUpdateMs = 0;
  mSceneEnergyBoost = 0.0f;
}
