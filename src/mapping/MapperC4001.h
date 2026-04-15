#pragma once

#include <stdint.h>

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
  enum class HueBand : uint8_t {
    Warm = 0,
    Neutral = 1,
    Cool = 2,
  };

  enum class SampleClass : uint8_t {
    Accepted = 0,
    SoftReject = 1,
    HardAbsent = 2,
  };

  struct EffectiveSample {
    bool valid;
    SampleClass sampleClass;
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
    float visibility;
    float influence;
  };

  EffectiveSample buildEffectiveSample(const BehaviorContext& context,
                                       const C4001PresenceRich& rich,
                                       const PresenceC4001::LinkStatus& linkStatus,
                                       bool allowValid);
  void applySceneDriveSmoothing(float dtSec, const EffectiveSample& sample);
  RenderIntent composeSceneIntent(const BehaviorContext& context,
                                  RenderIntent intent,
                                  const EffectiveSample& sample) const;
  void resetSceneState();

  MapperShared mShared;

  bool mHasSceneDriveState = false;
  float mSceneCharge = 0.0f;
  float mSceneIngressLevel = 0.0f;
  float mSceneFieldLevel = 0.0f;
  float mSceneEnergyBoost = 0.0f;
  float mHeldSpeedMag = 0.0f;
  float mHeldEnergyNorm = 0.0f;
  float mHeldSpeedSigned = 0.0f;
  HueBand mHueBand = HueBand::Neutral;
  uint32_t mLastSceneUpdateMs = 0;
};
