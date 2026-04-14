#include "MapperC4001.h"
#include <math.h>
#include "../BuildConfig.h"

namespace {
  constexpr float kRoomRangeNearM = 0.45f;
  constexpr float kRoomRangeFarM = 6.50f;
  constexpr float kSpeedStillThresholdMps = 0.08f;
  constexpr uint32_t kTargetHoldMs = 260;

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

}

RenderIntent MapperC4001::map(const BehaviorContext& context, const C4001PresenceRich& rich) {
  if (context.state == LampState::ActiveInterpretive) {
    RenderIntent intent = mShared.mapActiveBaseline(context);
    intent.sceneNowMs = context.nowMs;

    const bool targetValid = rich.targetSampleAccepted &&
                             (rich.targetNumber > 0) &&
                             (rich.targetRangeM >= BuildConfig::kAnthuriumMinAcceptedRangeM);
    if (targetValid) {
      mHasHeldTarget = true;
      mHeldAtMs = context.nowMs;
      mHeldRangeM = rich.targetRangeM;
      mHeldSpeedMps = rich.targetSpeedMps;
      mHeldEnergyNorm = normalizeEnergy(rich.targetEnergy);
    }

    bool useHeldTarget = false;
    if (mHasHeldTarget) {
      useHeldTarget = targetValid || ((context.nowMs - mHeldAtMs) <= kTargetHoldMs);
    }

    if (!useHeldTarget) {
      intent.effectId = static_cast<uint8_t>(context.state);
      return intent;
    }

    if (!mHasSmoothedRange) {
      mSmoothedRangeM = mHeldRangeM;
      mHasSmoothedRange = true;
    } else {
      const float rangeAlpha = clamp01(BuildConfig::kAnthuriumRangeSmoothingAlpha);
      mSmoothedRangeM += (mHeldRangeM - mSmoothedRangeM) * rangeAlpha;
    }

    const float speed = mHeldSpeedMps;
    const float speedMag = normalizeSpeedMag(speed);
    const float energyNorm = mHeldEnergyNorm;

    intent.activeSceneMode = ActiveSceneMode::AnthuriumReservoir;
    intent.sceneNowMs = context.nowMs;

    const float nearness = clamp01(1.0f - normalizeRange(mSmoothedRangeM));
    intent.sceneCharge = clamp01((nearness * BuildConfig::kAnthuriumDistanceToChargeGain) +
                                 (energyNorm * 0.10f));
    intent.sceneTargetRangeM = mHeldRangeM;
    intent.sceneTargetRangeSmoothedM = mSmoothedRangeM;
    intent.sceneChargeTarget = intent.sceneCharge;
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
    return intent;
  }

  if (context.state == LampState::Decay) {
    return mShared.mapDecayBaseline(context);
  }

  return mShared.map(context);
}
