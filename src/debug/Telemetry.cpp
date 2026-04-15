#include "Telemetry.h"

#include <Arduino.h>

#include "../BuildConfig.h"

void Telemetry::begin() {
#if TELEM_PROFILE != TELEM_NONE
  Serial.begin(115200);
#endif
  mHasLastState = false;
  mLastState = LampState::BootAnimation;
  mHasLastLinkState = false;
  mLastLinkState = PresenceC4001::LinkState::Offline;
  mLastDropoutLogMs = 0;
}

void Telemetry::update(const LampStateMachine& stateMachine,
                       const PresenceC4001::LinkStatus& c4001LinkStatus,
                       const AmbientGateResult& ambientGate,
                       const C4001PresenceRich& c4001Rich,
                       const RenderIntent& intent) {
#if TELEM_PROFILE == TELEM_NONE
  (void)stateMachine;
  (void)c4001LinkStatus;
  (void)ambientGate;
  (void)c4001Rich;
  (void)intent;
  return;
#else
  const BehaviorContext& context = stateMachine.context();
  const uint32_t nowMs = millis();
  const bool stateChanged = !mHasLastState || (context.state != mLastState);

#if TELEM_PROFILE == TELEM_MINIMAL
  const bool linkTransitioned = !mHasLastLinkState || (c4001LinkStatus.state != mLastLinkState);
  const bool linkChanged = linkTransitioned;

  const bool ambientCommit = ambientGate.transitionCommitted;
#endif

#if TELEM_PROFILE == TELEM_DROPOUT_TINY
  const bool periodic =
      ((mLastDropoutLogMs == 0) ||
       ((nowMs - mLastDropoutLogMs) >= BuildConfig::kTelemetryC4001RawLogIntervalMs));
  const bool inDropoutRelevantState =
      (context.state == LampState::ActiveInterpretive) || (context.state == LampState::Decay);
  const bool shouldLogDropout = periodic && inDropoutRelevantState;
#endif

#if TELEM_PROFILE == TELEM_MINIMAL
  if (ambientCommit) {
    Serial.print("ag c=");
    Serial.println(ambientGate.darkAllowed ? "n" : "d");
  }

  if (linkChanged) {
    mHasLastLinkState = true;
    mLastLinkState = c4001LinkStatus.state;

  Serial.print("ln=");
  Serial.println(static_cast<uint8_t>(c4001LinkStatus.state));
  }
#endif

#if TELEM_PROFILE == TELEM_DROPOUT_TINY
  if (shouldLogDropout) {
    mLastDropoutLogMs = nowMs;
    const int16_t rr = static_cast<int16_t>(c4001Rich.targetRangeRawM * 1000.0f);
    const int16_t ar = static_cast<int16_t>(intent.sceneTargetRangeM * 1000.0f);
    const int16_t ig = static_cast<int16_t>(intent.sceneIngressLevel * 1000.0f);
    const int16_t ct = static_cast<int16_t>(intent.sceneChargeTarget * 1000.0f);
    const int16_t cs = static_cast<int16_t>(intent.sceneCharge * 1000.0f);
    Serial.print("d,p=");
    Serial.print(intent.sceneDropoutPhase);
    Serial.print(",r=");
    Serial.print(intent.sceneRejectReason);
    Serial.print(",rr=");
    Serial.print(rr);
    Serial.print(",ar=");
    Serial.print(ar);
    Serial.print(",ig=");
    Serial.print(ig);
    Serial.print(",ct=");
    Serial.print(ct);
    Serial.print(",cs=");
    Serial.println(cs);
  }
#endif

#if TELEM_PROFILE == TELEM_MINIMAL
  if (stateChanged) {
    mHasLastState = true;
    mLastState = context.state;

  Serial.print("st=");
  Serial.println(static_cast<uint8_t>(context.state));
  }
#else
  (void)stateChanged;
#endif
#endif
}
