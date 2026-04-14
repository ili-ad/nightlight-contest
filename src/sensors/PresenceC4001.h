#pragma once

#include "PresenceTypes.h"

class PresenceC4001 {
public:
  struct Snapshot {
    CorePresence core;
    C4001PresenceRich rich;
  };

  void begin();
  Snapshot read();

  const C4001PresenceRich& lastRich() const;

private:
  bool initSensor();
  bool readSensorRich(C4001PresenceRich& outRich);

  static float clamp01(float value);
  CorePresence buildCoreFromRich(const C4001PresenceRich& rich, uint32_t nowMs) const;

  Snapshot applyFailure(uint32_t nowMs);

  bool initialized_ = false;
  bool online_ = false;
  uint8_t consecutiveFailures_ = 0;

  CorePresence lastCore_{};
  C4001PresenceRich lastRich_{};

  float confidenceEma_ = 0.0f;
  float distanceEma_ = 0.0f;
  float motionEma_ = 0.0f;
};
