#pragma once

#include "MapperShared.h"
#include "../behavior/BehaviorContext.h"
#include "../sensors/PresenceTypes.h"

class MapperC4001 {
public:
  RenderIntent map(const BehaviorContext& context, const C4001PresenceRich& rich);

private:
  MapperShared mShared;

  bool mHasHeldTarget = false;
  bool mHasSmoothedRange = false;
  uint32_t mHeldAtMs = 0;
  float mHeldRangeM = 0.0f;
  float mHeldSpeedMps = 0.0f;
  float mHeldEnergyNorm = 0.0f;
  float mSmoothedRangeM = 0.0f;
};
