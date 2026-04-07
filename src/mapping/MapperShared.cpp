#include "MapperShared.h"

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

  float activeWhiteLevel(const BehaviorContext& context) {
    const float confidence = clamp01(context.presenceConfidence);
    const float motion = clamp01(context.motionHint);
    return 0.10f + (confidence * 0.15f) + (motion * 0.10f);
  }
}

RenderIntent MapperShared::map(const BehaviorContext& context) {
  RenderIntent intent;
  intent.effectId = static_cast<uint8_t>(context.state);

  switch (context.state) {
    case LampState::DayDormant:
    case LampState::FaultSafe:
      intent.whiteLevel = 0.0f;
      intent.rgbLevel = 0.0f;
      intent.saturation = 0.0f;
      intent.animationRate = 0.0f;
      break;

    case LampState::NightIdle:
      intent.whiteLevel = context.darkAllowed ? 0.08f : 0.0f;
      intent.rgbLevel = 0.02f;
      intent.saturation = 0.05f;
      intent.hue = 0.10f;
      intent.animationRate = 0.02f;
      break;

    case LampState::ActiveInterpretive: {
      const float confidence = clamp01(context.presenceConfidence);
      const float distance = clamp01(context.distanceHint);
      const float motion = clamp01(context.motionHint);

      intent.whiteLevel = context.darkAllowed ? activeWhiteLevel(context) : 0.0f;
      intent.hue = 0.58f - (distance * 0.20f);
      intent.saturation = 0.20f + (motion * 0.50f);
      intent.rgbLevel = 0.08f + (confidence * 0.28f);
      intent.animationRate = 0.10f + (motion * 0.80f);
      intent.phase = distance;
      break;
    }

    case LampState::Decay: {
      const float confidence = clamp01(context.presenceConfidence);
      const float motion = clamp01(context.motionHint);

      intent.whiteLevel = context.darkAllowed ? (0.05f + (confidence * 0.08f)) : 0.0f;
      intent.hue = 0.12f;
      intent.saturation = 0.12f + (motion * 0.10f);
      intent.rgbLevel = 0.03f + (confidence * 0.10f);
      intent.animationRate = 0.06f;
      intent.phase = 0.0f;
      break;
    }

    case LampState::BootAnimation:
    case LampState::InterludeGlitch:
    default:
      break;
  }

  return intent;
}
