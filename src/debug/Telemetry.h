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
  bool mTinyBootLogged = false;
  bool mHasLastLinkState = false;
  PresenceC4001::LinkState mLastLinkState = PresenceC4001::LinkState::Offline;
  uint32_t mLastDropoutLogMs = 0;
  uint8_t mLastDropoutPhase = 0;
  bool mHasDropoutPhase = false;
  uint8_t mLastDropoutReason = 0;
  bool mHasDropoutReason = false;

  bool mHasLastState = false;
  LampState mLastState = LampState::BootAnimation;
};
