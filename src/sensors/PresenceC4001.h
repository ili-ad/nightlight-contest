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
    uint16_t consecutiveFailures = 0;
    uint32_t lastSuccessMs = 0;
    uint32_t lastFailureMs = 0;
    uint32_t lastSampleMs = 0;
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
  bool shouldAttemptInit(uint32_t nowMs) const;

  static float clamp01(float value);
  static float decayTowardZero(float value, float decayPerFailure);
  CorePresence buildCoreFromRich(const C4001PresenceRich& rich, uint32_t nowMs);

  Snapshot applyFailure(uint32_t nowMs);
  bool shouldPoll(uint32_t nowMs) const;

  bool initialized_ = false;
  LinkStatus linkStatus_{};
  uint32_t lastPollMs_ = 0;
  uint32_t lastInitAttemptMs_ = 0;
  bool hasPolled_ = false;
  bool sensorReady_ = false;

  CorePresence lastCore_{};
  C4001PresenceRich lastRich_{};

  float confidenceEma_ = 0.0f;
  float distanceEma_ = 0.0f;
  float motionEma_ = 0.0f;
};
