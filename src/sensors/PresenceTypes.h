#pragma once
#include <stdint.h>

struct CorePresence {
  bool online = false;
  bool present = false;

  // Shared normalized hints used by the state machine and common mappers.
  float presenceConfidence = 0.0f;  // 0..1
  float distanceHint = 0.0f;        // 0..1, interpretation deferred
  float motionHint = 0.0f;          // 0..1, interpretation deferred
  // Placeholder directional semantics only (not a hardware contract):
  // hasAngle: whether any directional/angular estimate is believed valid.
  bool hasAngle = false;
  // angleNorm: normalized unsigned off-centerline deviation (suggested 0..1).
  // 0 = centered/head-on, 1 = strongly off-axis/oblique.
  float angleNorm = 0.0f;
  // lateralBias: normalized signed lateral bias (suggested -1..1).
  // 0 = centered; negative/positive indicate opposite lateral sides.
  float lateralBias = 0.0f;

  uint32_t timestampMs = 0;
};

struct Ld2410PresenceRich {
  bool movingTarget = false;
  bool stationaryTarget = false;

  int movingEnergy = 0;
  int stationaryEnergy = 0;

  int movingDistanceGate = -1;
  int stationaryDistanceGate = -1;

  // Placeholder directional semantics only (not yet driven by raw protocol).
  bool hasAngle = false;
  float angleNorm = 0.0f;    // Suggested placeholder range 0..1.
  float lateralBias = 0.0f;  // Suggested placeholder range -1..1.

  // Optional future expansion: engineering mode gate arrays.
  // int movingGateEnergy[9];
  // int stationaryGateEnergy[9];
};

struct C4001PresenceRich {
  int targetNumber = 0;
  float targetRangeM = 0.0f;
  float targetSpeedMps = 0.0f;
  float targetRangeRawM = 0.0f;
  float targetSpeedRawM = 0.0f;
  bool targetSampleAccepted = false;
  uint8_t targetRejectedReason = 0;
  int targetEnergy = 0;

  // Placeholder directional semantics only (not yet driven by raw protocol).
  bool hasAngle = false;
  float angleNorm = 0.0f;    // Suggested placeholder range 0..1.
  float lateralBias = 0.0f;  // Suggested placeholder range -1..1.

  // Upstream stable-track output (owned by PresenceC4001 lifecycle).
  bool stableTrackHasTrack = false;
  uint8_t stableTrackPhase = 0;
  uint32_t stableTrackAgeMs = 0;
  float stableRangeM = 0.0f;
  float stableSmoothedRangeM = 0.0f;
  float stableChargeTarget = 0.0f;
  float stableIngressTarget = 0.0f;
  float stableFieldTarget = 0.0f;
  float stableEnergyBoostTarget = 0.0f;
  float stableSpeedMps = 0.0f;
  float stableEnergyNorm = 0.0f;
};
