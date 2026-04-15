#pragma once

#include "MapperShared.h"
#include "../behavior/BehaviorContext.h"
#include "../sensors/PresenceTypes.h"

class MapperC4001 {
public:
  RenderIntent map(const BehaviorContext& context, const C4001PresenceRich& rich);

private:
  MapperShared mShared;

  bool mHasSmoothedRange = false;
  bool mHasChargeTarget = false;
  bool mHasAcceptedSceneDrive = false;
  float mSmoothedRangeM = 0.0f;
  float mLastChargeTarget = 0.0f;
  float mHeldCharge = 0.0f;
  float mHeldRangeM = 0.0f;
  float mHeldSmoothedRangeM = 0.0f;
  uint32_t mLastAcceptedSceneMs = 0;
};
