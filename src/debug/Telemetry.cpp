#include "Telemetry.h"

#include <Arduino.h>

namespace {
  const char* stateName(LampState state) {
    switch (state) {
      case LampState::BootAnimation:
        return "BootAnimation";
      case LampState::DayDormant:
        return "DayDormant";
      case LampState::NightIdle:
        return "NightIdle";
      case LampState::ActiveInterpretive:
        return "ActiveInterpretive";
      case LampState::Decay:
        return "Decay";
      case LampState::InterludeGlitch:
        return "InterludeGlitch";
      case LampState::FaultSafe:
        return "FaultSafe";
      default:
        return "Unknown";
    }
  }
}

void Telemetry::begin() {
  Serial.begin(115200);
  mHasLastState = false;
  mLastState = LampState::BootAnimation;
}

void Telemetry::update(const LampStateMachine& stateMachine) {
  const BehaviorContext& context = stateMachine.context();
  if (mHasLastState && context.state == mLastState) {
    return;
  }

  mHasLastState = true;
  mLastState = context.state;

  Serial.print("state=");
  Serial.print(stateName(context.state));
  Serial.print(" darkAllowed=");
  Serial.print(context.darkAllowed ? "1" : "0");
  Serial.print(" ambientLux=");
  Serial.print(context.ambientLux, 2);
  Serial.print(" confidence=");
  Serial.print(context.presenceConfidence, 2);
  Serial.print(" distance=");
  Serial.print(context.distanceHint, 2);
  Serial.print(" motion=");
  Serial.println(context.motionHint, 2);
}
