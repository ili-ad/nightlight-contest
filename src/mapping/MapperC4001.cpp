#include "MapperC4001.h"
#include "../BuildConfig.h"
#include <math.h>

namespace {
  float clamp01(float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 1.0f) {
      return 1.0f;
    }
    return value;
  }

  float normalizeSpeed(float speedMps) {
    const float absSpeedMps = fabsf(speedMps);
    return clamp01(absSpeedMps / 2.0f);
  }

  float normalizeEnergy(int energy) {
    return clamp01(static_cast<float>(energy) / 100.0f);
  }

  float normalizeRange(float rangeM) {
    return clamp01(rangeM / 4.0f);
  }
}

RenderIntent MapperC4001::map(const BehaviorContext& context, const C4001PresenceRich& rich) const {
  if (context.state == LampState::ActiveInterpretive) {
    RenderIntent intent = mShared.mapActiveBaseline(context);

    const float speed = normalizeSpeed(rich.targetSpeedMps);
    const float energy = normalizeEnergy(rich.targetEnergy);

    intent.animationRate = clamp01(intent.animationRate + (speed * BuildConfig::kC4001SpeedAnimationBoost));
    intent.rgbLevel = clamp01(intent.rgbLevel + (energy * BuildConfig::kC4001TargetEnergyRgbBoost));

    const float nearTarget = 1.0f - normalizeRange(rich.targetRangeM);
    if ((rich.targetNumber > 0) && (nearTarget > 0.50f)) {
      intent.emphasizedSegment = SegmentId::WholeObject;
    }

    if (rich.targetNumber > 1) {
      intent.saturation = clamp01(intent.saturation * 0.95f);
    }

    intent.effectId = static_cast<uint8_t>(context.state);
    return intent;
  }

  if (context.state == LampState::Decay) {
    return mShared.mapDecayBaseline(context);
  }

  return mShared.map(context);
}
