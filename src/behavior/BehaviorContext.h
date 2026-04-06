#pragma once
#include <stdint.h>
#include "LampState.h"
#include "../Types.h"

struct BehaviorContext {
  LampState state = LampState::BootAnimation;
  uint32_t stateEnteredMs = 0;
  uint32_t nowMs = 0;

  // Ambient gate summary
  bool darkAllowed = false;
  float ambientLux = 0.0f;

  // Shared presence summary
  bool presenceOnline = false;
  bool presenceDetected = false;
  float presenceConfidence = 0.0f;
  float distanceHint = 0.0f;
  float motionHint = 0.0f;

  // Render / behavior bookkeeping
  uint8_t activeEffectId = 0;
  InterludeReason interludeReason = InterludeReason::None;

  uint32_t elapsedInStateMs() const {
    return nowMs - stateEnteredMs;
  }
};