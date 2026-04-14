#pragma once

#include "../behavior/LampStateMachine.h"
#include "../mapping/RenderIntent.h"
#include "../processing/AmbientGate.h"
#include "../sensors/PresenceC4001.h"
#include "../sensors/PresenceTypes.h"

class Telemetry {
public:
  void begin();
  void update(const LampStateMachine& stateMachine,
              const PresenceC4001::LinkStatus& c4001LinkStatus,
              const AmbientGateResult& ambientGate,
              const C4001PresenceRich& c4001Rich,
              const RenderIntent& intent);

private:
  static const char* stateCode(LampState state);

  bool mHasLastLinkState = false;
  PresenceC4001::LinkState mLastLinkState = PresenceC4001::LinkState::Offline;
  uint32_t mLastOfflineLogMs = 0;
  uint32_t mLastS27LogMs = 0;

  bool mHasLastState = false;
  LampState mLastState = LampState::BootAnimation;
};
