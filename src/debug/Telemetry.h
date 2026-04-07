#pragma once

#include "../behavior/LampStateMachine.h"

class Telemetry {
public:
  void begin();
  void update(const LampStateMachine& stateMachine);

private:
  bool mHasLastState = false;
  LampState mLastState = LampState::BootAnimation;
};
