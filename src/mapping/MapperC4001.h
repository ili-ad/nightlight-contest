#pragma once

#include "C4001TrackFilter.h"
#include "MapperShared.h"
#include "../behavior/BehaviorContext.h"
#include "../sensors/PresenceC4001.h"
#include "../sensors/PresenceTypes.h"

class MapperC4001 {
public:
  RenderIntent map(const BehaviorContext& context,
                   const C4001PresenceRich& rich,
                   const PresenceC4001::LinkStatus& linkStatus);

private:
  struct EffectiveSample {
    bool valid;
    C4001TrackFilter::Phase phase;
    uint8_t rejectReason;
    uint32_t ageMs;
    float rangeM;
    float smoothedRangeM;
    float chargeTarget;
    float ingressTarget;
    float fieldTarget;
    float energyBoostTarget;
    float speedMps;
    float energyNorm;
  };

  EffectiveSample buildEffectiveSample(const BehaviorContext& context,
                                       const C4001PresenceRich& rich,
                                       const PresenceC4001::LinkStatus& linkStatus,
                                       bool allowValid);
  void acceptValidSample(const BehaviorContext& context, const C4001PresenceRich& rich);
  void applySceneDriveSmoothing(float dtSec, const EffectiveSample& sample);
  RenderIntent composeSceneIntent(const BehaviorContext& context,
                                  RenderIntent intent,
                                  const EffectiveSample& sample,
                                  float speedMag) const;
  void resetSceneState();

  MapperShared mShared;

  bool mHasSmoothedRange = false;
  bool mHasChargeTarget = false;
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
  bool mHasSceneDriveState = false;
  float mSceneCharge = 0.0f;
  float mSceneIngressLevel = 0.0f;
  float mSceneFieldLevel = 0.0f;
  float mSceneEnergyBoost = 0.0f;
  uint32_t mLastSceneUpdateMs = 0;
  C4001TrackFilter mTrackFilter;
};
