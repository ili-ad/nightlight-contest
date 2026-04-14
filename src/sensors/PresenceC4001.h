#pragma once

#include "PresenceTypes.h"

class PresenceC4001 {
public:
  enum class LinkState : uint8_t {
    Online,
    DegradedHold,
    Offline
  };

  struct LinkStatus {
    LinkState state = LinkState::Offline;
    bool online = false;
    bool holding = false;
    uint8_t consecutiveFailures = 0;
    uint32_t lastSuccessMs = 0;
    uint32_t lastFailureMs = 0;
  };

  struct Snapshot {
    CorePresence core;
    C4001PresenceRich rich;
  };

  void begin();
  Snapshot read();

  const C4001PresenceRich& lastRich() const;
  const LinkStatus& linkStatus() const;

private:
  bool initSensor();
  bool readSensorRich(C4001PresenceRich& outRich);

  static float clamp01(float value);
  static float decayTowardZero(float value, float decayPerFailure);
  CorePresence buildCoreFromRich(const C4001PresenceRich& rich, uint32_t nowMs);

  Snapshot applyFailure(uint32_t nowMs);

  bool initialized_ = false;
  LinkStatus linkStatus_{};

  CorePresence lastCore_{};
  C4001PresenceRich lastRich_{};

  float confidenceEma_ = 0.0f;
  float distanceEma_ = 0.0f;
  float motionEma_ = 0.0f;
};
