#pragma once

#include "../behavior/LampStateMachine.h"
#include "../sensors/PresenceC4001.h"

class Telemetry {
public:
  void begin();
  void update(const LampStateMachine& stateMachine, const PresenceC4001::LinkStatus& c4001LinkStatus);

private:
  static const char* linkStateName(PresenceC4001::LinkState state);

  bool mHasLastLinkState = false;
  PresenceC4001::LinkState mLastLinkState = PresenceC4001::LinkState::Offline;
  uint32_t mLastOfflineLogMs = 0;
  uint32_t mLastPresenceLogMs = 0;

  bool mHasLastState = false;
  LampState mLastState = LampState::BootAnimation;
};
