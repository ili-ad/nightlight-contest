#include "Telemetry.h"

#include <Arduino.h>

#include "../BuildConfig.h"

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
  mLastOfflineLogMs = 0;
  mLastPresenceLogMs = 0;
  mHadAmbientPending = false;
  mLastAmbientWaitingOnHold = false;
  mLastAmbientPendingToDark = false;
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
                       const PresenceC4001::LinkStatus& c4001LinkStatus,
                       const AmbientGateResult& ambientGate) {
  const BehaviorContext& context = stateMachine.context();
  const uint32_t nowMs = millis();
  const bool stateChanged = !mHasLastState || (context.state != mLastState);
  const bool linkTransitioned = !mHasLastLinkState || (c4001LinkStatus.state != mLastLinkState);
  const bool offlinePeriodic =
      (c4001LinkStatus.state == PresenceC4001::LinkState::Offline) &&
      ((mLastOfflineLogMs == 0) ||
       ((nowMs - mLastOfflineLogMs) >= BuildConfig::kTelemetryOfflineLogIntervalMs));
  const bool presencePeriodic =
      ((mLastPresenceLogMs == 0) ||
       ((nowMs - mLastPresenceLogMs) >= BuildConfig::kTelemetryPresenceLogIntervalMs));
  const bool linkChanged = linkTransitioned || offlinePeriodic;
  const bool shouldLogPresence = presencePeriodic;
  const bool ambientPendingNow = ambientGate.waitingOnDwell || ambientGate.waitingOnHold;
  const bool ambientPendingChanged =
      (!mHadAmbientPending && ambientPendingNow) || (mHadAmbientPending && !ambientPendingNow);
  const bool ambientPendingModeChanged =
      ambientPendingNow &&
      ((mLastAmbientWaitingOnHold != ambientGate.waitingOnHold) ||
       (mLastAmbientPendingToDark != ambientGate.pendingToDark));
  const bool shouldLogAmbient = ambientGate.transitionCommitted || ambientPendingChanged || ambientPendingModeChanged;

  if (!stateChanged && !linkChanged && !shouldLogPresence && !shouldLogAmbient) {
    return;
  }

  if (shouldLogAmbient) {
    Serial.print("ambient_gate lux_raw=");
    Serial.print(ambientGate.rawLux, 2);
    Serial.print(" lux_gate=");
    Serial.print(ambientGate.gateLux, 2);
    Serial.print(" darkAllowed=");
    Serial.print(ambientGate.darkAllowed ? "1" : "0");

    if (ambientGate.transitionCommitted) {
      Serial.print(" event=commit to=");
      Serial.println(ambientGate.darkAllowed ? "night" : "day");
    } else if (ambientGate.waitingOnHold) {
      Serial.print(" event=hold_wait to=");
      Serial.print(ambientGate.pendingToDark ? "night" : "day");
      Serial.print(" hold_remaining_ms=");
      Serial.println(ambientGate.holdRemainingMs);
    } else if (ambientGate.waitingOnDwell) {
      Serial.print(" event=dwell_wait to=");
      Serial.print(ambientGate.pendingToDark ? "night" : "day");
      Serial.print(" dwell_ms=");
      Serial.print(ambientGate.pendingElapsedMs);
      Serial.print("/");
      Serial.println(ambientGate.pendingRequiredMs);
    } else {
      Serial.println(" event=clear");
    }
  }

  mHadAmbientPending = ambientPendingNow;
  mLastAmbientWaitingOnHold = ambientGate.waitingOnHold;
  mLastAmbientPendingToDark = ambientGate.pendingToDark;

  if (linkChanged) {
    mHasLastLinkState = true;
    mLastLinkState = c4001LinkStatus.state;
    if (c4001LinkStatus.state == PresenceC4001::LinkState::Offline) {
      mLastOfflineLogMs = nowMs;
    }

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
    if (shouldLogPresence) {
      mLastPresenceLogMs = nowMs;
      Serial.print("presence confidence=");
      Serial.print(context.presenceConfidence, 2);
      Serial.print(" distance=");
      Serial.print(context.distanceHint, 2);
      Serial.print(" motion=");
      Serial.print(context.motionHint, 2);
      Serial.print(" age_ms=");
      if (c4001LinkStatus.lastSuccessMs == 0) {
        Serial.println("n/a");
      } else {
        Serial.println(nowMs - c4001LinkStatus.lastSuccessMs);
      }
    }
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
