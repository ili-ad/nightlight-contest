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

void LampStateMachine::update(bool darkAllowed, float ambientLux, const CorePresence& presence) {
  const uint32_t now = millis();
  mContext.nowMs = now;

  mContext.darkAllowed = darkAllowed;
  mContext.ambientLux = ambientLux;
  mContext.presenceOnline = presence.online;
  mContext.presenceDetected = presence.present;
  mContext.presenceConfidence = presence.presenceConfidence;
  mContext.distanceHint = presence.distanceHint;
  mContext.motionHint = presence.motionHint;

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
