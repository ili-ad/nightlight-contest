#include "LampStateMachine.h"
#include <Arduino.h>
#include "../BuildConfig.h"

namespace {
  void transitionTo(BehaviorContext& context, LampState nextState, uint32_t nowMs) {
    context.state = nextState;
    context.stateEnteredMs = nowMs;
    context.nowMs = nowMs;
  }
}

void LampStateMachine::begin() {
  const uint32_t now = millis();
  mContext.state = LampState::BootAnimation;
  mContext.stateEnteredMs = now;
  mContext.nowMs = now;
}

void LampStateMachine::update() {
  const uint32_t now = millis();
  mContext.nowMs = now;

  switch (mContext.state) {
    case LampState::BootAnimation:
      if (mContext.elapsedInStateMs() >= BuildConfig::kBootAnimationMs) {
        transitionTo(mContext, LampState::NightIdle, now);
      }
      break;

    case LampState::DayDormant:
    case LampState::NightIdle:
    case LampState::ActiveInterpretive:
    case LampState::Decay:
    case LampState::InterludeGlitch:
    case LampState::FaultSafe:
    default:
      break;
  }
}

const BehaviorContext& LampStateMachine::context() const {
  return mContext;
}