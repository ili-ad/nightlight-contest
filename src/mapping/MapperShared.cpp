#include "MapperShared.h"
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

  float lerp(float a, float b, float t) {
    return a + ((b - a) * clamp01(t));
  }

  float normalizedPhase(uint32_t elapsedMs, float animationRate) {
    if (animationRate <= 0.0f) {
      return 0.0f;
    }
    const float cycles = (static_cast<float>(elapsedMs) / 1000.0f) * animationRate;
    return cycles - floorf(cycles);
  }
}

RenderIntent MapperShared::map(const BehaviorContext& context) {
  RenderIntent intent{};
  switch (context.state) {
    case LampState::DayDormant:
      intent = mapDayDormant(context);
      break;

    case LampState::NightIdle:
      intent = mapNightIdle(context);
      break;

    case LampState::ActiveInterpretive:
      intent = mapActiveInterpretive(context);
      break;

    case LampState::Decay:
      intent = mapDecay(context);
      break;

    case LampState::FaultSafe:
      intent = mapFaultSafe(context);
      break;

    case LampState::BootAnimation:
    case LampState::InterludeGlitch:
    default:
      break;
  }

  intent.effectId = static_cast<uint8_t>(context.state);
  return intent;
}

RenderIntent MapperShared::mapDayDormant(const BehaviorContext&) const {
  RenderIntent intent{};
  intent.whiteLevel = 0.0f;
  intent.hue = BuildConfig::kIdleHue;
  intent.saturation = 0.0f;
  intent.rgbLevel = 0.0f;
  intent.animationRate = 0.0f;
  intent.phase = 0.0f;
  intent.emphasizedSegment = SegmentId::Ring;
  return intent;
}

RenderIntent MapperShared::mapNightIdle(const BehaviorContext& context) const {
  RenderIntent intent{};
  intent.whiteLevel = context.darkAllowed ? BuildConfig::kIdleBrightness : 0.0f;
  intent.hue = BuildConfig::kIdleHue;
  intent.saturation = BuildConfig::kIdleSaturation;
  intent.rgbLevel = BuildConfig::kIdleRgbLevel;
  intent.animationRate = 0.02f;
  intent.phase = normalizedPhase(context.elapsedInStateMs(), intent.animationRate);
  intent.emphasizedSegment = SegmentId::Ring;
  return intent;
}

RenderIntent MapperShared::mapActiveInterpretive(const BehaviorContext& context) const {
  RenderIntent intent{};
  const float confidence = clamp01(context.presenceConfidence);
  // Input convention is "higher distanceHint == nearer presence".
  const float nearness = clamp01(context.distanceHint);
  const float motion = clamp01(context.motionHint);

  const float strength = clamp01((0.65f * confidence) + (0.35f * nearness));
  const float baseWhite = lerp(BuildConfig::kActiveBrightnessMin, BuildConfig::kActiveBrightnessMax, strength);

  const float stillness = 1.0f - motion;
  const float stillCloseSoftness = clamp01(nearness * stillness);

  intent.whiteLevel = context.darkAllowed ? baseWhite : 0.0f;
  intent.hue = lerp(BuildConfig::kActiveFarHue, BuildConfig::kActiveNearHue, nearness);
  intent.hue = lerp(intent.hue, BuildConfig::kActiveNearHue, stillCloseSoftness * 0.35f);

  intent.saturation = BuildConfig::kActiveBaseSaturation + (motion * BuildConfig::kActiveMotionSaturationBoost);
  intent.saturation = clamp01(intent.saturation - (stillCloseSoftness * 0.10f));

  intent.rgbLevel = BuildConfig::kActiveBaseRgbLevel + (motion * BuildConfig::kActiveMotionRgbBoost);
  intent.rgbLevel = clamp01(intent.rgbLevel - (stillCloseSoftness * 0.04f));

  intent.animationRate = 0.08f + (motion * 0.92f);
  intent.phase = normalizedPhase(context.elapsedInStateMs(), intent.animationRate);
  intent.emphasizedSegment = SegmentId::WholeObject;
  return intent;
}

RenderIntent MapperShared::mapDecay(const BehaviorContext& context) const {
  BehaviorContext seeded = context;
  seeded.presenceConfidence = fmaxf(context.presenceConfidence, BuildConfig::kPresenceEnterThreshold);
  seeded.distanceHint = fmaxf(context.distanceHint, BuildConfig::kPresenceEnterThreshold);
  const RenderIntent start = mapActiveInterpretive(seeded);
  RenderIntent intent = start;

  const float t = clamp01(static_cast<float>(context.elapsedInStateMs()) /
                          static_cast<float>(BuildConfig::kDecayMs));

  intent.whiteLevel = context.darkAllowed ? lerp(start.whiteLevel, BuildConfig::kIdleBrightness, t) : 0.0f;
  intent.hue = lerp(start.hue, BuildConfig::kDecayEndHue, t);
  intent.saturation = lerp(start.saturation, BuildConfig::kDecayEndSaturation, t);
  intent.rgbLevel = lerp(start.rgbLevel, BuildConfig::kDecayEndRgbLevel, t);
  intent.animationRate = lerp(start.animationRate, 0.01f, t);
  intent.phase = normalizedPhase(context.elapsedInStateMs(), intent.animationRate);
  intent.emphasizedSegment = (t < 0.5f) ? SegmentId::WholeObject : SegmentId::Ring;
  return intent;
}

RenderIntent MapperShared::mapFaultSafe(const BehaviorContext&) const {
  RenderIntent intent{};
  intent.whiteLevel = BuildConfig::kFaultSafeWhiteLevel;
  intent.hue = BuildConfig::kFaultSafeHue;
  intent.saturation = BuildConfig::kFaultSafeSaturation;
  intent.rgbLevel = 0.0f;
  intent.animationRate = 0.0f;
  intent.phase = 0.0f;
  intent.emphasizedSegment = SegmentId::WholeObject;
  return intent;
}
