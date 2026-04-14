#include "Telemetry.h"

#include <Arduino.h>

#include "../BuildConfig.h"

namespace {
#if TELEM_PROFILE >= TELEM_MINIMAL
const char* linkStateCode(PresenceC4001::LinkState state) {
  switch (state) {
    case PresenceC4001::LinkState::Online:
      return "on";
    case PresenceC4001::LinkState::DegradedHold:
      return "dg";
    case PresenceC4001::LinkState::Offline:
    default:
      return "off";
  }
}

const char* sampleKindCode(PresenceC4001::SampleKind kind) {
  switch (kind) {
    case PresenceC4001::SampleKind::Target:
      return "tg";
    case PresenceC4001::SampleKind::NoTarget:
      return "nt";
    case PresenceC4001::SampleKind::ReadFailure:
      return "rf";
    case PresenceC4001::SampleKind::Unknown:
    default:
      return "uk";
  }
}
#endif
}  // namespace

void Telemetry::begin() {
#if TELEM_PROFILE != TELEM_NONE
  Serial.begin(115200);
#endif
  mHasLastState = false;
  mLastState = LampState::BootAnimation;
  mHasLastLinkState = false;
  mLastLinkState = PresenceC4001::LinkState::Offline;
  mLastOfflineLogMs = 0;
  mLastS27LogMs = 0;
}

const char* Telemetry::stateCode(LampState state) {
  switch (state) {
    case LampState::BootAnimation:
      return "BT";
    case LampState::DayDormant:
      return "DD";
    case LampState::NightIdle:
      return "NI";
    case LampState::ActiveInterpretive:
      return "AI";
    case LampState::Decay:
      return "DC";
    case LampState::InterludeGlitch:
      return "IG";
    case LampState::FaultSafe:
      return "FS";
    default:
      return "UK";
  }
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

#if TELEM_PROFILE >= TELEM_MINIMAL
  const bool linkTransitioned = !mHasLastLinkState || (c4001LinkStatus.state != mLastLinkState);
  const bool offlinePeriodic =
      (c4001LinkStatus.state == PresenceC4001::LinkState::Offline) &&
      ((mLastOfflineLogMs == 0) ||
       ((nowMs - mLastOfflineLogMs) >= BuildConfig::kTelemetryOfflineLogIntervalMs));
  const bool linkChanged = linkTransitioned || offlinePeriodic;

  const bool ambientCommit = ambientGate.transitionCommitted;
#endif

#if TELEM_PROFILE >= TELEM_SENSOR27
  const bool s27Periodic =
      ((mLastS27LogMs == 0) ||
       ((nowMs - mLastS27LogMs) >= BuildConfig::kTelemetryC4001RawLogIntervalMs));
  const bool shouldLogS27 = s27Periodic && (context.state == LampState::ActiveInterpretive);
#endif

#if TELEM_PROFILE >= TELEM_MINIMAL
  if (ambientCommit) {
    Serial.print("ag c=");
    Serial.print(ambientGate.darkAllowed ? "n" : "d");
    Serial.print(" lx=");
    Serial.println(ambientGate.gateLux, 1);
  }

  if (linkChanged) {
    mHasLastLinkState = true;
    mLastLinkState = c4001LinkStatus.state;
    if (c4001LinkStatus.state == PresenceC4001::LinkState::Offline) {
      mLastOfflineLogMs = nowMs;
    }

    Serial.print("ln=");
    Serial.print(linkStateCode(c4001LinkStatus.state));
    Serial.print(" on=");
    Serial.print(c4001LinkStatus.online ? "1" : "0");
    Serial.print(" h=");
    Serial.print(c4001LinkStatus.holding ? "1" : "0");
    Serial.print(" f=");
    Serial.println(c4001LinkStatus.consecutiveFailures);
  }
#endif

#if TELEM_PROFILE >= TELEM_SENSOR27
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
    Serial.print(" tn=");
    Serial.print(c4001Rich.targetNumber);
    Serial.print(" sk=");
    Serial.print(sampleKindCode(c4001LinkStatus.sampleKind));
    Serial.print(" rj=");
    Serial.println(rejectCode);
  }
#endif

#if TELEM_PROFILE >= TELEM_MINIMAL
  if (stateChanged) {
    mHasLastState = true;
    mLastState = context.state;

    Serial.print("st=");
    Serial.print(stateCode(context.state));
    Serial.print(" lx=");
    Serial.print(context.ambientLux, 1);
    Serial.print(" cf=");
    Serial.print(context.presenceConfidence, 2);
    Serial.print(" ds=");
    Serial.print(context.distanceHint, 2);
    Serial.print(" mo=");
    Serial.println(context.motionHint, 2);
  }
#else
  (void)stateChanged;
#endif
#endif
}
