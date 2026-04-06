#pragma once
#include <stdint.h>

struct CorePresence {
  bool online = false;
  bool present = false;

  // Shared normalized hints used by the state machine and common mappers.
  float presenceConfidence = 0.0f;  // 0..1
  float distanceHint = 0.0f;        // 0..1, interpretation deferred
  float motionHint = 0.0f;          // 0..1, interpretation deferred

  uint32_t timestampMs = 0;
};

struct Ld2410PresenceRich {
  bool movingTarget = false;
  bool stationaryTarget = false;

  int movingEnergy = 0;
  int stationaryEnergy = 0;

  int movingDistanceGate = -1;
  int stationaryDistanceGate = -1;

  // Optional future expansion: engineering mode gate arrays.
  // int movingGateEnergy[9];
  // int stationaryGateEnergy[9];
};

struct C4001PresenceRich {
  int targetNumber = 0;
  float targetRangeM = 0.0f;
  float targetSpeedMps = 0.0f;
  int targetEnergy = 0;
};