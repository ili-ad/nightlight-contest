#pragma once

#include <stdint.h>

#include "../model/StableTrack.h"

class C4001StableSource {
 public:
  void begin();
  void service(uint32_t nowMs);
  void requestManualInit();
  StableTrack read(uint32_t nowMs);

 private:
  static float clamp01(float v);
  static float smooth(float previous, float target, float alpha);

  bool initialized_ = false;
  bool wireReady_ = false;
  bool sensorReady_ = false;
  bool manualInitRequested_ = false;
  uint32_t lastPollMs_ = 0;
  uint32_t lastInitAttemptMs_ = 0;
  uint32_t lastSeenMs_ = 0;

  bool stableHasTarget_ = false;
  float stableRangeM_ = 1.2f;
  float stableSpeedMps_ = 0.0f;
  float smoothedCharge_ = 0.0f;
  float smoothedIngress_ = 0.0f;
  float continuity_ = 0.0f;
  StableTrack::MotionPhase phase_ = StableTrack::MotionPhase::None;
};
