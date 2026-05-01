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
  bool captureStatus(uint32_t nowMs);
  bool statusHealthy() const;
  bool probeSpeedMode();
  bool trySoftRecover();
  void printStatusTriple() const;

  bool initialized_ = false;
  bool wireReady_ = false;
  bool sensorReady_ = false;
  bool manualInitRequested_ = false;
  bool everHadAcceptedTarget_ = false;
  bool droughtReinitRequested_ = false;
  bool configAttempted_ = false;
  bool configApplied_ = false;
  uint8_t recoveryStage_ = 0;
  uint8_t lastRecoveryStep_ = 0;
  uint32_t lastPollMs_ = 0;
  uint32_t lastInitAttemptMs_ = 0;
  uint32_t lastAcceptedMs_ = 0;
  uint32_t lastRawReadMs_ = 0;
  uint32_t lastStatusReadMs_ = 0;

  int lastRawTargetNumber_ = 0;
  float lastRawRangeM_ = 0.0f;
  float lastRawSpeedMps_ = 0.0f;
  uint32_t lastRawEnergy_ = 0;
  bool lastRawAccepted_ = false;

  uint8_t lastStatusWork_ = 0;
  uint8_t lastStatusMode_ = 0;
  uint8_t lastStatusInit_ = 0;
  bool lastModeSetOk_ = false;
  bool lastDetectThresOk_ = false;

  bool stableHasTarget_ = false;
  float stableRangeM_ = 1.2f;
  float stableSpeedMps_ = 0.0f;
  float smoothedCharge_ = 0.0f;
  float smoothedIngress_ = 0.0f;
  float continuity_ = 0.0f;
  StableTrack::MotionPhase phase_ = StableTrack::MotionPhase::None;
};
