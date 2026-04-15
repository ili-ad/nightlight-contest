#pragma once

#include "MapperShared.h"
#include "../behavior/BehaviorContext.h"
#include "../sensors/PresenceTypes.h"

class MapperC4001 {
public:
  RenderIntent map(const BehaviorContext& context, const C4001PresenceRich& rich);

private:
  struct InvalidSceneDrive {
    float targetRangeM;
    float targetSmoothedRangeM;
    float chargeTarget;
    float ingressTarget;
    float fieldTarget;
    float energyBoostTarget;
    float speedMps;
    float energyNorm;
    uint8_t phase;
  };

  InvalidSceneDrive applyInvalidSceneDrive(uint32_t nowMs) const;

  MapperShared mShared;

  bool mHasSmoothedRange = false;
  bool mHasChargeTarget = false;
  bool mHasAcceptedSceneDrive = false;
  float mSmoothedRangeM = 0.0f;
  float mLastChargeTarget = 0.0f;
  float mHeldCharge = 0.0f;
  float mHeldIngressLevel = 0.0f;
  float mHeldFieldLevel = 0.0f;
  float mHeldEnergyBoost = 0.0f;
  float mHeldSpeedMps = 0.0f;
  float mHeldEnergyNorm = 0.0f;
  float mHeldRangeM = 0.0f;
  float mHeldSmoothedRangeM = 0.0f;
  uint32_t mLastAcceptedSceneMs = 0;
  bool mHasSceneDriveState = false;
  float mSceneCharge = 0.0f;
  float mSceneIngressLevel = 0.0f;
  float mSceneFieldLevel = 0.0f;
  float mSceneEnergyBoost = 0.0f;
  uint32_t mLastSceneUpdateMs = 0;
};
