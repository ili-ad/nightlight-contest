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
    const float eased = t * t;
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
}

RenderIntent MapperC4001::map(const BehaviorContext& context, const C4001PresenceRich& rich) {
  if (context.state == LampState::ActiveInterpretive) {
    RenderIntent intent = mShared.mapActiveBaseline(context);
    intent.sceneNowMs = context.nowMs;

    const bool targetValid = rich.targetSampleAccepted &&
                             (rich.targetNumber > 0) &&
                             (rich.targetRangeM >= BuildConfig::kAnthuriumMinAcceptedRangeM);
    if (!targetValid) {
      if (mHasAcceptedSceneDrive) {
        const uint32_t ageMs = context.nowMs - mLastAcceptedSceneMs;
        const bool withinHold = ageMs <= BuildConfig::kAnthuriumRejectedHoldMs;
        const float rejectAgeSec =
            withinHold ? 0.0f : static_cast<float>(ageMs - BuildConfig::kAnthuriumRejectedHoldMs) / 1000.0f;
        const float heldCharge = applyRejectDecay(mHeldCharge, rejectAgeSec);
        intent.activeSceneMode = ActiveSceneMode::AnthuriumReservoir;
        intent.sceneTargetRangeM = mHeldRangeM;
        intent.sceneTargetRangeSmoothedM = mHeldSmoothedRangeM;
        intent.sceneChargeTarget = heldCharge;
        intent.sceneCharge = heldCharge;
        intent.sceneIngressLevel = clamp01(BuildConfig::kAnthuriumIngressBaseLevel +
                                           (heldCharge * 0.75f));
        intent.sceneFieldLevel = clamp01(BuildConfig::kAnthuriumTorusFieldBaseLevel +
                                         (heldCharge * 0.85f));
        intent.sceneEnergyBoost = 0.0f;
      }
      intent.effectId = static_cast<uint8_t>(context.state);
      return intent;
    }

    if (!mHasSmoothedRange) {
      mSmoothedRangeM = rich.targetRangeM;
      mHasSmoothedRange = true;
    } else {
      const float rangeAlpha = clamp01(BuildConfig::kAnthuriumRangeSmoothingAlpha);
      mSmoothedRangeM += (rich.targetRangeM - mSmoothedRangeM) * rangeAlpha;
    }

    const float speed = rich.targetSpeedMps;
    const float speedMag = normalizeSpeedMag(speed);
    const float energyNorm = normalizeEnergy(rich.targetEnergy);

    intent.activeSceneMode = ActiveSceneMode::AnthuriumReservoir;
    intent.sceneNowMs = context.nowMs;

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

    intent.sceneCharge = chargeTarget;
    intent.sceneTargetRangeM = rich.targetRangeM;
    intent.sceneTargetRangeSmoothedM = mSmoothedRangeM;
    intent.sceneChargeTarget = chargeTarget;
    intent.sceneIngressLevel = clamp01(BuildConfig::kAnthuriumIngressBaseLevel +
                                       (intent.sceneCharge * 0.75f));
    intent.sceneFieldLevel = clamp01(BuildConfig::kAnthuriumTorusFieldBaseLevel +
                                     (intent.sceneCharge * 0.85f));
    intent.sceneEnergyBoost = clamp01(energyNorm * BuildConfig::kAnthuriumEnergyWhiteBoostGain);

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
    mHasAcceptedSceneDrive = true;
    mHeldCharge = intent.sceneCharge;
    mHeldRangeM = intent.sceneTargetRangeM;
    mHeldSmoothedRangeM = intent.sceneTargetRangeSmoothedM;
    mLastAcceptedSceneMs = context.nowMs;
    return intent;
  }

  if (context.state == LampState::Decay) {
    mHasAcceptedSceneDrive = false;
    mHasSmoothedRange = false;
    mHasChargeTarget = false;
    return mShared.mapDecayBaseline(context);
  }

  mHasAcceptedSceneDrive = false;
  mHasSmoothedRange = false;
  mHasChargeTarget = false;
  return mShared.map(context);
}
