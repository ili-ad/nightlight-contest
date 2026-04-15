#pragma once

#include "BehaviorContext.h"
#include "../sensors/PresenceTypes.h"

class LampStateMachine {
public:
  void begin();
  void update(bool darkAllowed, float ambientLux, const CorePresence& presence, bool forceFaultSafe);
  const BehaviorContext& context() const;

private:
  BehaviorContext mContext;
};
