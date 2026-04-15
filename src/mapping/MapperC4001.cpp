#include "MapperC4001.h"
#include <math.h>
#include "../BuildConfig.h"

namespace {
  constexpr uint8_t kScenePhaseAccepted = 0;
  constexpr uint8_t kScenePhaseHold = 1;
  constexpr uint8_t kScenePhaseDecay = 2;
  constexpr uint8_t kScenePhaseEmpty = 3;

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
    // Keep near-field compression on a fixed polynomial ease to avoid expensive
    // exponent math in this hot path.
    const float eased = t * t * (3.0f - 2.0f * t);
    const float nearCharge = startCharge + ((1.0f - startCharge) * eased);
    return clamp01((nearCharge > baseCharge) ? nearCharge : baseCharge);
  }

  float applyRejectDecay(float value, float ageSec) {
    if (value <= 0.0f) {
      return 0.0f;
    }
    const float decayRate =
        (BuildConfig::kAnthuriumRejectedDecayPerSecond <= 0.0f)
            ? 0.0f
            : BuildConfig::kAnthuriumRejectedDecayPerSecond;
    if (decayRate <= 0.0f || ageSec <= 0.0f) {
      return value;
    }
    const float floor = clamp01(BuildConfig::kAnthuriumRejectedFloor);
    const float decayed = value - (ageSec * decayRate);
    if (decayed > value) {
      return value;
    }
    return (decayed <= floor) ? 0.0f : decayed;
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
}

RenderIntent MapperC4001::map(const BehaviorContext& context, const C4001PresenceRich& rich) {
  if (context.state == LampState::ActiveInterpretive) {
    RenderIntent intent = mShared.mapActiveBaseline(context);
    intent.sceneNowMs = context.nowMs;

    const float dtSec = safeDeltaSec(context.nowMs, mLastSceneUpdateMs);
    mLastSceneUpdateMs = context.nowMs;

    const bool hasTarget = (rich.targetNumber > 0);
    const bool hasValidRawRange = (rich.targetRangeRawM > 0.0f) &&
                                  (rich.targetRangeRawM >= BuildConfig::kAnthuriumMinAcceptedRangeM);
    const bool hasValidAcceptedRange = (rich.targetRangeM > 0.0f) &&
                                       (rich.targetRangeM >= BuildConfig::kAnthuriumMinAcceptedRangeM);
    const bool invalidSceneFrame = !hasTarget ||
                                   !rich.targetSampleAccepted ||
                                   (rich.targetRejectedReason != 0u) ||
                                   !hasValidRawRange ||
                                   !hasValidAcceptedRange;
    const bool targetValid = !invalidSceneFrame;

    const bool hasValidHistory = mHasAcceptedSceneDrive;
    float targetRangeM = hasValidHistory ? mHeldRangeM : 0.0f;
    float targetSmoothedRangeM = hasValidHistory ? mHeldSmoothedRangeM : 0.0f;
    float chargeTarget = hasValidHistory ? mHeldCharge : 0.0f;
    float ingressTarget = hasValidHistory ? mHeldIngressLevel : 0.0f;
    float fieldTarget = hasValidHistory ? mHeldFieldLevel : 0.0f;
    float energyBoostTarget = hasValidHistory ? mHeldEnergyBoost : 0.0f;
    float speed = hasValidHistory ? mHeldSpeedMps : 0.0f;
    float speedMag = normalizeSpeedMag(speed);
    float energyNorm = hasValidHistory ? mHeldEnergyNorm : 0.0f;
    uint8_t sceneDropoutPhase = hasValidHistory ? kScenePhaseHold : kScenePhaseEmpty;

    if (targetValid) {
      if (!mHasSmoothedRange) {
        mSmoothedRangeM = rich.targetRangeM;
        mHasSmoothedRange = true;
      } else {
        const float rangeAlpha = clamp01(BuildConfig::kAnthuriumRangeSmoothingAlpha);
        mSmoothedRangeM += (rich.targetRangeM - mSmoothedRangeM) * rangeAlpha;
      }

      speed = rich.targetSpeedMps;
      speedMag = normalizeSpeedMag(speed);
      energyNorm = normalizeEnergy(rich.targetEnergy);

      chargeTarget = mappedChargeFromRange(mSmoothedRangeM);
      if (mHasChargeTarget && (mSmoothedRangeM <= BuildConfig::kAnthuriumNearFieldCompressionStartM)) {
        const float maxStep =
            (BuildConfig::kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate < 0.001f)
                ? 0.001f
                : BuildConfig::kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate;
        chargeTarget = clampDelta(mLastChargeTarget, chargeTarget, maxStep);
      }
      mHasChargeTarget = true;
      mLastChargeTarget = chargeTarget;

      targetRangeM = rich.targetRangeM;
      targetSmoothedRangeM = mSmoothedRangeM;
      energyBoostTarget = clamp01(energyNorm * BuildConfig::kAnthuriumEnergyWhiteBoostGain);
      ingressTarget = clamp01(BuildConfig::kAnthuriumIngressBaseLevel +
                              (chargeTarget * 0.75f));
      fieldTarget = clamp01(BuildConfig::kAnthuriumTorusFieldBaseLevel +
                            (chargeTarget * 0.85f));
      mHasAcceptedSceneDrive = true;
      mHeldCharge = chargeTarget;
      mHeldIngressLevel = ingressTarget;
      mHeldFieldLevel = fieldTarget;
      mHeldEnergyBoost = energyBoostTarget;
      mHeldSpeedMps = speed;
      mHeldEnergyNorm = energyNorm;
      mHeldRangeM = targetRangeM;
      mHeldSmoothedRangeM = targetSmoothedRangeM;
      mLastAcceptedSceneMs = context.nowMs;
      sceneDropoutPhase = kScenePhaseAccepted;
    } else {
      const InvalidSceneDrive invalid = applyInvalidSceneDrive(context.nowMs);
      targetRangeM = invalid.targetRangeM;
      targetSmoothedRangeM = invalid.targetSmoothedRangeM;
      chargeTarget = invalid.chargeTarget;
      ingressTarget = invalid.ingressTarget;
      fieldTarget = invalid.fieldTarget;
      energyBoostTarget = invalid.energyBoostTarget;
      speed = invalid.speedMps;
      speedMag = normalizeSpeedMag(speed);
      energyNorm = invalid.energyNorm;
      sceneDropoutPhase = invalid.phase;
      if (!hasValidHistory) {
        mHasChargeTarget = false;
        mLastChargeTarget = 0.0f;
        mHasSmoothedRange = false;
        mSmoothedRangeM = 0.0f;
      }
      if (sceneDropoutPhase == kScenePhaseEmpty) {
        mHasAcceptedSceneDrive = false;
      }
    }

    if (!mHasSceneDriveState) {
      mSceneCharge = chargeTarget;
      mSceneIngressLevel = ingressTarget;
      mSceneFieldLevel = fieldTarget;
      mSceneEnergyBoost = energyBoostTarget;
      mHasSceneDriveState = true;
    } else {
      constexpr float kChargeRisePerSec = 7.5f;
      constexpr float kChargeFallPerSec = 3.6f;
      constexpr float kIngressRisePerSec = 4.8f;
      constexpr float kIngressFallPerSec = 3.2f;
      constexpr float kFieldRisePerSec = 4.6f;
      constexpr float kFieldFallPerSec = 3.0f;
      constexpr float kEnergyRisePerSec = 5.5f;
      constexpr float kEnergyFallPerSec = 3.2f;
      mSceneCharge = clamp01(stepToward(mSceneCharge, chargeTarget, dtSec, kChargeRisePerSec, kChargeFallPerSec));
      mSceneIngressLevel =
          clamp01(stepToward(mSceneIngressLevel, ingressTarget, dtSec, kIngressRisePerSec, kIngressFallPerSec));
      mSceneFieldLevel = clamp01(stepToward(mSceneFieldLevel, fieldTarget, dtSec, kFieldRisePerSec, kFieldFallPerSec));
      mSceneEnergyBoost =
          clamp01(stepToward(mSceneEnergyBoost, energyBoostTarget, dtSec, kEnergyRisePerSec, kEnergyFallPerSec));
    }

    intent.activeSceneMode = ActiveSceneMode::AnthuriumReservoir;
    intent.sceneNowMs = context.nowMs;
    intent.sceneTargetRangeM = validRangeOrZero(targetRangeM);
    intent.sceneTargetRangeSmoothedM = validRangeOrZero(targetSmoothedRangeM);
    intent.sceneChargeTarget = chargeTarget;
    intent.sceneCharge = mSceneCharge;
    intent.sceneIngressLevel = mSceneIngressLevel;
    intent.sceneFieldLevel = mSceneFieldLevel;
    intent.sceneEnergyBoost = mSceneEnergyBoost;
    intent.sceneDropoutPhase = sceneDropoutPhase;
    intent.sceneRejectReason = targetValid ? 0u : static_cast<uint8_t>(rich.targetRejectedReason);

    intent.useLocalizedBlob = false;

    if (speed < -kSpeedStillThresholdMps) {
      // Negative radial speed is treated as approaching.
      intent.hue = 0.02f;
      intent.saturation = 0.86f;
    } else if (speed > kSpeedStillThresholdMps) {
      // Positive radial speed is treated as retreating.
      intent.hue = 0.58f;
      intent.saturation = 0.78f;
    } else {
      intent.hue = 0.30f;
      intent.saturation = 0.20f;
    }

    const float stillnessBoost = 1.0f - speedMag;
    intent.rgbLevel = clamp01(0.11f + (intent.sceneCharge * 0.05f) + (speedMag * 0.05f));
    intent.whiteLevel = context.darkAllowed
                            ? clamp01(intent.whiteLevel + (energyNorm * 0.04f) +
                                      (stillnessBoost * 0.02f))
                            : 0.0f;

    intent.effectId = static_cast<uint8_t>(context.state);
    return intent;
  }

  if (context.state == LampState::Decay) {
    RenderIntent intent = mShared.mapDecayBaseline(context);
    intent.sceneNowMs = context.nowMs;

    const float dtSec = safeDeltaSec(context.nowMs, mLastSceneUpdateMs);
    mLastSceneUpdateMs = context.nowMs;

    const InvalidSceneDrive invalid = applyInvalidSceneDrive(context.nowMs);
    const float chargeTarget = invalid.chargeTarget;
    const float ingressTarget = invalid.ingressTarget;
    const float fieldTarget = invalid.fieldTarget;
    const float energyBoostTarget = invalid.energyBoostTarget;
    const float speed = invalid.speedMps;
    const float speedMag = normalizeSpeedMag(speed);
    const float energyNorm = invalid.energyNorm;

    if (!mHasSceneDriveState) {
      mSceneCharge = chargeTarget;
      mSceneIngressLevel = ingressTarget;
      mSceneFieldLevel = fieldTarget;
      mSceneEnergyBoost = energyBoostTarget;
      mHasSceneDriveState = true;
    } else {
      constexpr float kChargeRisePerSec = 7.5f;
      constexpr float kChargeFallPerSec = 3.6f;
      constexpr float kIngressRisePerSec = 4.8f;
      constexpr float kIngressFallPerSec = 3.2f;
      constexpr float kFieldRisePerSec = 4.6f;
      constexpr float kFieldFallPerSec = 3.0f;
      constexpr float kEnergyRisePerSec = 5.5f;
      constexpr float kEnergyFallPerSec = 3.2f;
      mSceneCharge = clamp01(stepToward(mSceneCharge, chargeTarget, dtSec, kChargeRisePerSec, kChargeFallPerSec));
      mSceneIngressLevel =
          clamp01(stepToward(mSceneIngressLevel, ingressTarget, dtSec, kIngressRisePerSec, kIngressFallPerSec));
      mSceneFieldLevel = clamp01(stepToward(mSceneFieldLevel, fieldTarget, dtSec, kFieldRisePerSec, kFieldFallPerSec));
      mSceneEnergyBoost =
          clamp01(stepToward(mSceneEnergyBoost, energyBoostTarget, dtSec, kEnergyRisePerSec, kEnergyFallPerSec));
    }

    intent.activeSceneMode = ActiveSceneMode::AnthuriumReservoir;
    intent.sceneTargetRangeM = validRangeOrZero(invalid.targetRangeM);
    intent.sceneTargetRangeSmoothedM = validRangeOrZero(invalid.targetSmoothedRangeM);
    intent.sceneChargeTarget = chargeTarget;
    intent.sceneCharge = mSceneCharge;
    intent.sceneIngressLevel = mSceneIngressLevel;
    intent.sceneFieldLevel = mSceneFieldLevel;
    intent.sceneEnergyBoost = mSceneEnergyBoost;
    intent.sceneDropoutPhase = invalid.phase;
    intent.sceneRejectReason = 0;

    if (speed < -kSpeedStillThresholdMps) {
      intent.hue = 0.02f;
      intent.saturation = 0.86f;
    } else if (speed > kSpeedStillThresholdMps) {
      intent.hue = 0.58f;
      intent.saturation = 0.78f;
    } else {
      intent.hue = 0.30f;
      intent.saturation = 0.20f;
    }

    const float stillnessBoost = 1.0f - speedMag;
    intent.rgbLevel = clamp01(0.11f + (intent.sceneCharge * 0.05f) + (speedMag * 0.05f));
    intent.whiteLevel = context.darkAllowed
                            ? clamp01(intent.whiteLevel + (energyNorm * 0.04f) +
                                      (stillnessBoost * 0.02f))
                            : 0.0f;

    if (invalid.phase == kScenePhaseEmpty) {
      mHasAcceptedSceneDrive = false;
      mHasSmoothedRange = false;
      mHasChargeTarget = false;
      mSmoothedRangeM = 0.0f;
      mLastChargeTarget = 0.0f;
    }

    intent.effectId = static_cast<uint8_t>(context.state);
    return intent;
  }

  mHasAcceptedSceneDrive = false;
  mHasSmoothedRange = false;
  mHasChargeTarget = false;
  mHasSceneDriveState = false;
  mLastSceneUpdateMs = 0;
  mSceneEnergyBoost = 0.0f;
  return mShared.map(context);
}

MapperC4001::InvalidSceneDrive MapperC4001::applyInvalidSceneDrive(uint32_t nowMs) const {
  // Invariant: invalid frame must never be treated as fresh accepted scene-drive.
  if (!mHasAcceptedSceneDrive) {
    return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, kScenePhaseEmpty};
  }

  const uint32_t ageMs = nowMs - mLastAcceptedSceneMs;
  const bool withinHold = ageMs <= BuildConfig::kAnthuriumRejectedHoldMs;
  const float rejectAgeSec =
      withinHold ? 0.0f : static_cast<float>(ageMs - BuildConfig::kAnthuriumRejectedHoldMs) / 1000.0f;
  const float decayScale = withinHold ? 1.0f : applyRejectDecay(1.0f, rejectAgeSec);
  const uint8_t phase =
      withinHold ? kScenePhaseHold : ((decayScale > 0.0f) ? kScenePhaseDecay : kScenePhaseEmpty);
  return {
      mHeldRangeM * decayScale,
      mHeldSmoothedRangeM * decayScale,
      mHeldCharge * decayScale,
      mHeldIngressLevel * decayScale,
      mHeldFieldLevel * decayScale,
      mHeldEnergyBoost * decayScale,
      mHeldSpeedMps * decayScale,
      mHeldEnergyNorm * decayScale,
      phase,
  };
}
