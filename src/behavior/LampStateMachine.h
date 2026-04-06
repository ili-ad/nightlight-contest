#pragma once

#include "BehaviorContext.h"

class LampStateMachine {
public:
  void begin();
  void update();
  const BehaviorContext& context() const;

private:
  BehaviorContext mContext;
};
