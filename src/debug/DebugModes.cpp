#include "DebugModes.h"

#include "../BuildConfig.h"

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
  sample.ambientLux = 2.0f;
  sample.presence.online = true;
  sample.presence.present = false;
  sample.presence.presenceConfidence = 0.0f;
  sample.presence.distanceHint = 0.0f;
  sample.presence.motionHint = 0.0f;
  sample.presence.timestampMs = nowMs;
  sample.forceFaultSafe = false;

  const uint32_t loopMs = (BuildConfig::kSimLoopMs == 0) ? 1 : BuildConfig::kSimLoopMs;
  const uint32_t phaseMs = nowMs % loopMs;

  if (phaseMs < 1200) {
    // 1) Dark idle.
    return sample;
  }

  if (phaseMs < 3200) {
    // 2) Approach ramp.
    const float t = normalizedRamp(phaseMs - 1200, 2000);
    sample.presence.present = (t >= 0.25f);
    sample.presence.presenceConfidence = 0.05f + (0.90f * t);
    sample.presence.distanceHint = 0.10f + (0.85f * t);
    sample.presence.motionHint = 0.20f + (0.30f * t);
    return sample;
  }

  if (phaseMs < 4600) {
    // 3) Near but mostly still.
    sample.presence.present = true;
    sample.presence.presenceConfidence = 0.95f;
    sample.presence.distanceHint = 0.95f;
    sample.presence.motionHint = 0.08f;
    return sample;
  }

  if (phaseMs < 6200) {
    // 4) Retreat.
    const float t = normalizedRamp(phaseMs - 4600, 1600);
    sample.presence.present = (t < 0.70f);
    sample.presence.presenceConfidence = 0.95f * (1.0f - t);
    sample.presence.distanceHint = 0.95f * (1.0f - t);
    sample.presence.motionHint = 0.22f;
    return sample;
  }

  if (phaseMs < 7200) {
    // 5) Absent in dark.
    return sample;
  }

  if (phaseMs < 8400) {
    // 6) Day lockout.
    sample.darkAllowed = false;
    sample.ambientLux = 80.0f;
    return sample;
  }

  // 7) Explicit fault-safe pulse.
  sample.darkAllowed = false;
  sample.ambientLux = 80.0f;
  sample.forceFaultSafe = true;
  return sample;
}
