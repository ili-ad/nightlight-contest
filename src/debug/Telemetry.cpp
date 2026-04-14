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
  mHasLastLinkState = false;
  mLastLinkState = PresenceC4001::LinkState::Offline;
  mLastFailureCount = 0;
}

const char* Telemetry::linkStateName(PresenceC4001::LinkState state) {
  switch (state) {
    case PresenceC4001::LinkState::Online:
      return "online";
    case PresenceC4001::LinkState::DegradedHold:
      return "degraded_hold";
    case PresenceC4001::LinkState::Offline:
    default:
      return "offline";
  }
}

void Telemetry::update(const LampStateMachine& stateMachine,
                       const PresenceC4001::LinkStatus& c4001LinkStatus) {
  const BehaviorContext& context = stateMachine.context();
  const bool stateChanged = !mHasLastState || (context.state != mLastState);
  const bool linkChanged = !mHasLastLinkState || (c4001LinkStatus.state != mLastLinkState) ||
                           (c4001LinkStatus.consecutiveFailures != mLastFailureCount);

  if (!stateChanged && !linkChanged) {
    return;
  }

  if (linkChanged) {
    mHasLastLinkState = true;
    mLastLinkState = c4001LinkStatus.state;
    mLastFailureCount = c4001LinkStatus.consecutiveFailures;

    Serial.print("presence_link=");
    Serial.print(linkStateName(c4001LinkStatus.state));
    Serial.print(" online=");
    Serial.print(c4001LinkStatus.online ? "1" : "0");
    Serial.print(" holding=");
    Serial.print(c4001LinkStatus.holding ? "1" : "0");
    Serial.print(" failures=");
    Serial.print(c4001LinkStatus.consecutiveFailures);
    Serial.print(" age_ms=");
    if (c4001LinkStatus.lastSuccessMs == 0) {
      Serial.println("n/a");
    } else {
      Serial.println(millis() - c4001LinkStatus.lastSuccessMs);
    }
  }

  if (!stateChanged) {
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
