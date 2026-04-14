#pragma once
#include <stdint.h>

struct AmbientGateResult {
  bool darkAllowed = false;
  float rawLux = 0.0f;
  float gateLux = 0.0f;
  bool transitionCommitted = false;
  bool waitingOnDwell = false;
  bool waitingOnHold = false;
  bool pendingToDark = false;
  uint32_t pendingElapsedMs = 0;
  uint32_t pendingRequiredMs = 0;
  uint32_t holdRemainingMs = 0;
};

class AmbientGate {
public:
  AmbientGateResult update(float lux);

private:
  void clearPending();

  bool darkAllowed_ = false;
  bool initialized_ = false;
  bool smoothingReady_ = false;
  float gateLux_ = 0.0f;

  bool pendingActive_ = false;
  bool pendingToDark_ = false;
  uint32_t pendingSinceMs_ = 0;
  uint32_t pendingRequiredMs_ = 0;

  uint32_t lastTransitionMs_ = 0;
};
