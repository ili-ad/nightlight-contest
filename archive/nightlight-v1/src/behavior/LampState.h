#pragma once
#include <stdint.h>

enum class LampState : uint8_t {
  BootAnimation,
  DayDormant,
  NightIdle,
  ActiveInterpretive,
  Decay,
  InterludeGlitch,
  FaultSafe
};