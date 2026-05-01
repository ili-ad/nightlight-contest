#pragma once

#include <stdint.h>

#include "../model/StableTrack.h"

class C4001StableSource {
 public:
  void begin();
  bool tryInit();
  void service(uint32_t nowMs);
  void requestManualInit();
  StableTrack read(uint32_t nowMs);

 private:
  static float clamp01(float v);
  static float smooth(float previous, float target, float alpha);

  StableTrack currentTrack() const;
  void noteNoAcceptedTarget(uint32_t nowMs);
  void updateSmoothedSignals(bool hasEffectiveTarget);
  void maybeRequestDroughtReinit(uint32_t nowMs);

  bool initialized_ = false;
  bool wireReady_ = false;
  bool sensorReady_ = false;
  bool manualInitRequested_ = false;
  bool everHadAcceptedTarget_ = false;
  bool droughtReinitRequested_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t lastInitAttemptMs_ = 0;
  uint32_t lastAcceptedMs_ = 0;

  bool stableHasTarget_ = false;
  float stableRangeM_ = 1.2f;
  float stableSpeedMps_ = 0.0f;
  float smoothedCharge_ = 0.0f;
  float smoothedIngress_ = 0.0f;
  float continuity_ = 0.0f;
  StableTrack::MotionPhase phase_ = StableTrack::MotionPhase::None;
};
