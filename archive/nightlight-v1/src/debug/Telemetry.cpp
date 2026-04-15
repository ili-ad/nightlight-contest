#include "Telemetry.h"

#include <Arduino.h>

#include "../BuildConfig.h"

namespace {
int32_t quantizeQ1000(float value) {
  return static_cast<int32_t>(value * 1000.0f);
}

int32_t quantizeMillimeters(float meters) {
  return static_cast<int32_t>(meters * 1000.0f);
}
}  // namespace

void Telemetry::begin() {
#if TELEM_PROFILE != TELEM_NONE
  Serial.begin(115200);
#endif
  mTinyBootLogged = false;
  mHasLastState = false;
  mLastState = LampState::BootAnimation;
  mHasLastLinkState = false;
  mLastLinkState = PresenceC4001::LinkState::Offline;
  mLastDropoutLogMs = 0;
  mLastDropoutPhase = 0;
  mHasDropoutPhase = false;
  mLastDropoutReason = 0;
  mHasDropoutReason = false;

#if TELEM_PROFILE == TELEM_DROPOUT_TINY
  if (!mTinyBootLogged) {
    Serial.print("b,");
    Serial.println(static_cast<uint8_t>(BuildConfig::kTelemetryProfile));
    mTinyBootLogged = true;
  }
#endif
}

void Telemetry::update(const LampStateMachine& stateMachine,
                       const PresenceC4001::LinkStatus& c4001LinkStatus,
                       const AmbientGateResult& ambientGate,
                       const C4001PresenceRich& c4001Rich,
                       const RenderIntent& rawIntent,
                       const RenderIntent& finalIntent) {
#if TELEM_PROFILE == TELEM_NONE
  (void)stateMachine;
  (void)c4001LinkStatus;
  (void)ambientGate;
  (void)c4001Rich;
  (void)rawIntent;
  (void)finalIntent;
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
  const bool linkChanged = !mHasLastLinkState || (c4001LinkStatus.state != mLastLinkState);
  const bool phaseChanged = !mHasDropoutPhase || (rawIntent.sceneDropoutPhase != mLastDropoutPhase);
  const bool reasonChanged = !mHasDropoutReason || (rawIntent.sceneRejectReason != mLastDropoutReason);
  const bool periodic =
      ((mLastDropoutLogMs == 0) ||
       ((nowMs - mLastDropoutLogMs) >= 100u));
  const bool inDropoutRelevantState =
      (context.state == LampState::ActiveInterpretive) || (context.state == LampState::Decay);
  const bool notAcceptedPhase = (rawIntent.sceneDropoutPhase != 0u);
  const bool shouldLogDropout =
      inDropoutRelevantState && (phaseChanged || reasonChanged || (notAcceptedPhase && periodic));
#endif

#if TELEM_PROFILE == TELEM_C4001_PROBE
  const bool invalid = (rawIntent.sceneDropoutPhase != 0u);
  const bool phaseChanged = !mHasDropoutPhase || (rawIntent.sceneDropoutPhase != mLastDropoutPhase);
  const bool reasonChanged = !mHasDropoutReason || (rawIntent.sceneRejectReason != mLastDropoutReason);
  const bool periodicInvalid = invalid &&
                              ((mLastDropoutLogMs == 0) || ((nowMs - mLastDropoutLogMs) >= 100u));
  const bool shouldLogProbe = phaseChanged || reasonChanged || periodicInvalid;

  if (shouldLogProbe) {
    mHasDropoutPhase = true;
    mHasDropoutReason = true;
    mLastDropoutPhase = rawIntent.sceneDropoutPhase;
    mLastDropoutReason = rawIntent.sceneRejectReason;
    mLastDropoutLogMs = nowMs;

    Serial.print(static_cast<uint8_t>(rawIntent.sceneDropoutPhase));
    Serial.print(',');
    Serial.print(static_cast<uint8_t>(rawIntent.sceneRejectReason));
    Serial.print(',');
    Serial.print(quantizeMillimeters(c4001Rich.targetRangeRawM));
    Serial.print(',');
    Serial.print(quantizeMillimeters(rawIntent.sceneTargetRangeM));
    Serial.print(',');
    Serial.print(rawIntent.sceneSampleAgeMs);
    Serial.print(',');
    Serial.print(quantizeQ1000(rawIntent.sceneIngressLevel));
    Serial.print(',');
    Serial.print(quantizeQ1000(rawIntent.sceneChargeTarget));
    Serial.print(',');
    Serial.println(quantizeQ1000(rawIntent.sceneCharge));
  }
#endif

  (void)finalIntent;

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
  if (linkChanged) {
    mHasLastLinkState = true;
    mLastLinkState = c4001LinkStatus.state;
    Serial.print("l,");
    Serial.println(static_cast<uint8_t>(c4001LinkStatus.state));
  }

  if (stateChanged) {
    mHasLastState = true;
    mLastState = context.state;
    Serial.print("s,");
    Serial.println(static_cast<uint8_t>(context.state));
  }

  if (shouldLogDropout) {
    mHasDropoutPhase = true;
    mHasDropoutReason = true;
    mLastDropoutPhase = rawIntent.sceneDropoutPhase;
    mLastDropoutReason = rawIntent.sceneRejectReason;
    mLastDropoutLogMs = nowMs;
    Serial.print("d,");
    Serial.print(rawIntent.sceneDropoutPhase);
    Serial.print(',');
    Serial.print(rawIntent.sceneRejectReason);
    Serial.print(',');
    Serial.print(quantizeMillimeters(c4001Rich.targetRangeRawM));
    Serial.print(',');
    Serial.print(quantizeMillimeters(rawIntent.sceneTargetRangeM));
    Serial.print(',');
    Serial.print(rawIntent.sceneSampleAgeMs);
    Serial.print(',');
    Serial.print(quantizeQ1000(rawIntent.sceneIngressLevel));
    Serial.print(',');
    Serial.print(quantizeQ1000(rawIntent.sceneChargeTarget));
    Serial.print(',');
    Serial.println(quantizeQ1000(rawIntent.sceneCharge));
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
