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
  mLastS27LogMs = 0;
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

#if TELEM_PROFILE == TELEM_SENSOR27
  const bool s27Periodic =
      ((mLastS27LogMs == 0) ||
       ((nowMs - mLastS27LogMs) >= BuildConfig::kTelemetryC4001RawLogIntervalMs));
  const bool shouldLogS27 = s27Periodic && (context.state == LampState::ActiveInterpretive);
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

#if TELEM_PROFILE == TELEM_SENSOR27
  if (shouldLogS27) {
    mLastS27LogMs = nowMs;
    const uint8_t rejectCode = static_cast<uint8_t>(c4001Rich.targetRejectedReason);
    Serial.print("s27 rr=");
    Serial.print(c4001Rich.targetRangeRawM, 3);
    Serial.print(" ar=");
    Serial.print(c4001Rich.targetRangeM, 3);
    Serial.print(" rv=");
    Serial.print(c4001Rich.targetSpeedRawM, 3);
    Serial.print(" av=");
    Serial.print(c4001Rich.targetSpeedMps, 3);
    Serial.print(" ct=");
    Serial.print(intent.sceneChargeTarget, 3);
    Serial.print(" cs=");
    Serial.print(intent.sceneCharge, 3);
    Serial.print(" ig=");
    Serial.print(intent.sceneIngressLevel, 3);
    Serial.print(" rj=");
    Serial.println(rejectCode);
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
