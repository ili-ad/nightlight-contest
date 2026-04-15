#pragma once

#include <stdint.h>
#include "../sensors/PresenceTypes.h"

struct DebugInputSample {
  bool useSimulated = false;
  bool darkAllowed = false;
  float ambientLux = 0.0f;
  CorePresence presence;
  bool forceFaultSafe = false;
};

class DebugModes {
public:
  static DebugInputSample sample(uint32_t nowMs);
};
