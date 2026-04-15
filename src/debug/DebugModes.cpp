#include "DebugModes.h"

#include "../BuildConfig.h"

#if BUILD_HAS_DEBUG_SIM
namespace {
  float normalizedRamp(uint32_t elapsed, uint32_t spanMs) {
    if (spanMs == 0) {
      return 1.0f;
    }

    const float value = static_cast<float>(elapsed) / static_cast<float>(spanMs);
    if (value <= 0.0f) {
      return 0.0f;
    }

    return (value >= 1.0f) ? 1.0f : value;
  }
}

DebugInputSample DebugModes::sample(uint32_t nowMs) {
  DebugInputSample sample;

  if (BuildConfig::kDebugInputMode == DebugInputMode::None) {
    return sample;
  }

  sample.useSimulated = true;
  sample.darkAllowed = true;
  sample.ambientLux = BuildConfig::kSimDarkAmbientLux;
  sample.presence.online = true;
  sample.presence.present = false;
  sample.presence.presenceConfidence = 0.0f;
  sample.presence.distanceHint = 0.0f;
  sample.presence.motionHint = 0.0f;
  sample.presence.timestampMs = nowMs;
  sample.forceFaultSafe = false;

  const uint32_t loopMs = (BuildConfig::kSimLoopMs == 0) ? 1 : BuildConfig::kSimLoopMs;
  const uint32_t phaseMs = nowMs % loopMs;

  if (phaseMs < BuildConfig::kSimDarkIdleMs) {
    // 1) Dark idle.
    return sample;
  }

  if (phaseMs < BuildConfig::kSimApproachEndMs) {
    // 2) Approach ramp.
    const float t = normalizedRamp(phaseMs - BuildConfig::kSimDarkIdleMs,
                                   BuildConfig::kSimApproachEndMs - BuildConfig::kSimDarkIdleMs);
    sample.presence.present = (t >= 0.25f);
    sample.presence.presenceConfidence = 0.05f + (0.90f * t);
    sample.presence.distanceHint = 0.10f + (0.85f * t);
    sample.presence.motionHint = 0.20f + (0.30f * t);
    return sample;
  }

  if (phaseMs < BuildConfig::kSimNearStillEndMs) {
    // 3) Near but mostly still.
    sample.presence.present = true;
    sample.presence.presenceConfidence = 0.95f;
    sample.presence.distanceHint = 0.95f;
    sample.presence.motionHint = 0.08f;
    return sample;
  }

  if (phaseMs < BuildConfig::kSimRetreatEndMs) {
    // 4) Retreat.
    const float t = normalizedRamp(phaseMs - BuildConfig::kSimNearStillEndMs,
                                   BuildConfig::kSimRetreatEndMs - BuildConfig::kSimNearStillEndMs);
    sample.presence.present = (t < 0.70f);
    sample.presence.presenceConfidence = 0.95f * (1.0f - t);
    sample.presence.distanceHint = 0.95f * (1.0f - t);
    sample.presence.motionHint = 0.22f;
    return sample;
  }

  if (phaseMs < BuildConfig::kSimDarkAbsentEndMs) {
    // 5) Absent in dark.
    return sample;
  }

  if (phaseMs < BuildConfig::kSimDayLockoutEndMs) {
    // 6) Day lockout.
    sample.darkAllowed = false;
    sample.ambientLux = BuildConfig::kSimDayAmbientLux;
    return sample;
  }

  // 7) Explicit fault-safe pulse.
  sample.darkAllowed = false;
  sample.ambientLux = BuildConfig::kSimDayAmbientLux;
  sample.forceFaultSafe = true;
  return sample;
}
#else
DebugInputSample DebugModes::sample(uint32_t nowMs) {
  (void)nowMs;
  return {};
}
#endif
