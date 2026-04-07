#include "MapperLd2410.h"
#include "../BuildConfig.h"

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

  float normalizedLd2410Energy(int energy) {
    return clamp01(static_cast<float>(energy) / 100.0f);
  }

  bool hasValidGate(int gate) {
    return gate >= 0;
  }
}

RenderIntent MapperLd2410::map(const BehaviorContext& context, const Ld2410PresenceRich& rich) const {
  if (context.state == LampState::ActiveInterpretive) {
    RenderIntent intent = mShared.mapActiveBaseline(context);

    const float movingEnergy = normalizedLd2410Energy(rich.movingEnergy);
    const float stationaryEnergy = normalizedLd2410Energy(rich.stationaryEnergy);

    intent.saturation = clamp01(
      intent.saturation + (movingEnergy * BuildConfig::kLd2410MotionEnergySaturationBoost));

    if (context.darkAllowed && (rich.stationaryTarget || rich.movingTarget)) {
      intent.whiteLevel = clamp01(
        intent.whiteLevel + (stationaryEnergy * BuildConfig::kLd2410StaticEnergyWhiteBoost));
    }

    if (hasValidGate(rich.movingDistanceGate) || hasValidGate(rich.stationaryDistanceGate)) {
      intent.emphasizedSegment = SegmentId::WholeObject;
    }

    intent.effectId = static_cast<uint8_t>(context.state);
    return intent;
  }

  if (context.state == LampState::Decay) {
    return mShared.mapDecayBaseline(context);
  }

  return mShared.map(context);
}
