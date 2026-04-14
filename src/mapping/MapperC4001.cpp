#include "MapperC4001.h"
#include <math.h>

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

    const bool targetValid = (rich.targetNumber > 0) && (rich.targetRangeM > 0.01f);
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

    const float speed = mHeldSpeedMps;
    const float speedMag = normalizeSpeedMag(speed);
    const float energyNorm = mHeldEnergyNorm;

    intent.useLocalizedBlob = true;
    intent.blobCenter = 1.0f - normalizeRange(mHeldRangeM);
    intent.blobWidth = clamp01(0.16f + ((1.0f - context.presenceConfidence) * 0.08f) +
                               ((1.0f - speedMag) * 0.06f));
    intent.blobSoftness = 1.8f;

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
    intent.rgbLevel = clamp01(0.10f + (speedMag * 0.06f) + (energyNorm * 0.07f));
    intent.whiteLevel = context.darkAllowed
                            ? clamp01(intent.whiteLevel + (energyNorm * 0.05f) +
                                      (stillnessBoost * 0.03f))
                            : 0.0f;

    if (rich.targetNumber > 1) {
      intent.blobWidth = clamp01(intent.blobWidth + 0.04f);
    }

    intent.effectId = static_cast<uint8_t>(context.state);
    return intent;
  }

  if (context.state == LampState::Decay) {
    return mShared.mapDecayBaseline(context);
  }

  return mShared.map(context);
}
