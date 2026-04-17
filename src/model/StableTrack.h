#pragma once

#include <stdint.h>

struct StableTrack {
  enum class MotionPhase : uint8_t {
    None,
    Approach,
    Still,
    Retreat,
  };

  bool online = false;
  bool hasTarget = false;
  float rangeM = 0.0f;
  float speedMps = 0.0f;
  float charge = 0.0f;
  float ingressLevel = 0.0f;
  float continuity = 0.0f;
  MotionPhase phase = MotionPhase::None;
};
