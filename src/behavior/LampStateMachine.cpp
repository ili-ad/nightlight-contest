#include "LampStateMachine.h"
#include <Arduino.h>
#include "../BuildConfig.h"

namespace {
  uint8_t effectIdForState(LampState state) {
    return static_cast<uint8_t>(state);
  }

  void transitionTo(BehaviorContext& context, LampState nextState, uint32_t nowMs) {
    context.state = nextState;
    context.stateEnteredMs = nowMs;
    context.nowMs = nowMs;
    context.activeEffectId = effectIdForState(nextState);
  }
}

void LampStateMachine::begin() {
  const uint32_t now = millis();
  mContext.state = LampState::BootAnimation;
  mContext.stateEnteredMs = now;
  mContext.nowMs = now;
  mContext.activeEffectId = effectIdForState(LampState::BootAnimation);
}

void LampStateMachine::update(bool darkAllowed, float ambientLux, const CorePresence& presence, bool forceFaultSafe) {
  const uint32_t now = millis();
  mContext.nowMs = now;

  mContext.darkAllowed = darkAllowed;
  mContext.ambientLux = ambientLux;
  mContext.presenceOnline = presence.online;
  mContext.presenceDetected = presence.present;
  mContext.presenceConfidence = presence.presenceConfidence;
  mContext.distanceHint = presence.distanceHint;
  mContext.motionHint = presence.motionHint;
  mContext.hasAngle = presence.hasAngle;
  mContext.angleNorm = presence.angleNorm;
  mContext.lateralBias = presence.lateralBias;

  if (forceFaultSafe && mContext.state != LampState::FaultSafe) {
    transitionTo(mContext, LampState::FaultSafe, now);
    return;
  }

  switch (mContext.state) {
    case LampState::BootAnimation:
      if (mContext.elapsedInStateMs() >= BuildConfig::kBootAnimationMs) {
        transitionTo(mContext, darkAllowed ? LampState::NightIdle : LampState::DayDormant, now);
      }
      break;

    case LampState::DayDormant:
      if (darkAllowed) {
        transitionTo(mContext, LampState::NightIdle, now);
      }
      break;

    case LampState::NightIdle:
      if (!darkAllowed) {
        transitionTo(mContext, LampState::DayDormant, now);
      } else if (presence.present || presence.presenceConfidence >= BuildConfig::kPresenceEnterThreshold) {
        transitionTo(mContext, LampState::ActiveInterpretive, now);
      }
      break;

    case LampState::ActiveInterpretive:
      if (!darkAllowed) {
        transitionTo(mContext, LampState::DayDormant, now);
      } else if (!presence.present && presence.presenceConfidence <= BuildConfig::kPresenceExitThreshold) {
        transitionTo(mContext, LampState::Decay, now);
      }
      break;

    case LampState::Decay:
      if (!darkAllowed) {
        transitionTo(mContext, LampState::DayDormant, now);
      } else if (presence.present || presence.presenceConfidence >= BuildConfig::kPresenceEnterThreshold) {
        transitionTo(mContext, LampState::ActiveInterpretive, now);
      } else if (mContext.elapsedInStateMs() >= BuildConfig::kDecayMs) {
        transitionTo(mContext, LampState::NightIdle, now);
      }
      break;

    case LampState::InterludeGlitch:
      if (mContext.elapsedInStateMs() >= BuildConfig::kInterludeMaxMs) {
        transitionTo(mContext, darkAllowed ? LampState::NightIdle : LampState::DayDormant, now);
      }
      break;

    case LampState::FaultSafe:
      if (mContext.elapsedInStateMs() >= BuildConfig::kFaultSafeHoldMs) {
        transitionTo(mContext, darkAllowed ? LampState::NightIdle : LampState::DayDormant, now);
      }
      break;

    default:
      break;
  }
}

const BehaviorContext& LampStateMachine::context() const {
  return mContext;
}
