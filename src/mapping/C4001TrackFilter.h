#pragma once

#include <stdint.h>

class C4001TrackFilter {
public:
  enum class InputClass : uint8_t {
    Valid = 0,
    SoftReject = 1,
    HardAbsent = 2,
    LinkIssue = 3,
  };

  enum class Phase : uint8_t {
    Valid = 0,
    Hold = 1,
    SoftReject = 2,
    HardAbsent = 3,
    Decay = 4,
    Empty = 5,
    LinkIssue = 6,
  };

  struct Sample {
    float rangeM = 0.0f;
    float smoothedRangeM = 0.0f;
    float chargeTarget = 0.0f;
    float ingressTarget = 0.0f;
    float fieldTarget = 0.0f;
    float energyBoostTarget = 0.0f;
    float speedMps = 0.0f;
    float energyNorm = 0.0f;
  };

  struct Output {
    Sample sample{};
    Phase phase = Phase::Empty;
    uint32_t ageMs = 0;
    bool hasTrack = false;
  };

  void configure(uint32_t holdMs, float decayPerSecond, float decayFloor);
  void reset();
  Output update(InputClass inputClass, uint32_t nowMs, const Sample* validSample = nullptr);

private:
  static float clamp01(float value);
  static float applyDecayScale(float ageSec, float decayPerSecond, float decayFloor);

  uint32_t mHoldMs = 450;
  float mDecayPerSecond = 2.40f;
  float mDecayFloor = 0.0f;

  bool mHasTrack = false;
  uint32_t mLastAcceptedMs = 0;
  Sample mHeld{};
};
