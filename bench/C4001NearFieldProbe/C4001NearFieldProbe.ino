#include <Arduino.h>

#include "../../src/BuildConfig.h"
#include "../../src/sensors/PresenceManager.h"

namespace {
PresenceManager gPresence;
uint32_t gLastPollMs = 0;
uint32_t gLastAcceptedMs = 0;

int32_t mmFromMeters(float meters) {
  return static_cast<int32_t>(meters * 1000.0f);
}

uint8_t classifyPhase(const CorePresence& core,
                      const PresenceC4001::LinkStatus& link,
                      const C4001PresenceRich& rich) {
  const bool hasTarget = (rich.targetNumber > 0);
  if (hasTarget && rich.targetSampleAccepted) {
    return 0;  // accepted
  }

  if (link.state == PresenceC4001::LinkState::DegradedHold ||
      link.noTargetHolding ||
      (hasTarget && !rich.targetSampleAccepted)) {
    return 1;  // hold
  }

  if (core.present || (core.presenceConfidence > BuildConfig::kPresenceExitThreshold)) {
    return 2;  // decay
  }

  return 3;  // empty
}

uint8_t classifyReject(const PresenceC4001::LinkStatus& link,
                       const C4001PresenceRich& rich) {
  const bool hasTarget = (rich.targetNumber > 0);
  if (hasTarget && !rich.targetSampleAccepted) {
    return rich.targetRejectedReason;
  }
  if (link.sampleKind == PresenceC4001::SampleKind::NoTarget) {
    return static_cast<uint8_t>(PresenceC4001::RejectReason::NoTarget);
  }
  return 0;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  gPresence.begin();

  Serial.print("b,");
  Serial.print("pollMs=");
  Serial.print(BuildConfig::kC4001PollIntervalMs);
  Serial.print(",nearStartMm=");
  Serial.print(mmFromMeters(BuildConfig::kC4001NearFieldStartM));
  Serial.print(",cohBandMm=");
  Serial.print(mmFromMeters(BuildConfig::kC4001NearFieldCoherenceBandM));
  Serial.print(",cohReq=");
  Serial.print(BuildConfig::kC4001NearFieldCoherentSamples);
  Serial.print(",dropHoldMs=");
  Serial.print(BuildConfig::kC4001DropoutHoldMs);
  Serial.print(",noTargetGraceMs=");
  Serial.println(BuildConfig::kC4001NoTargetGraceMs);
}

void loop() {
  const uint32_t nowMs = millis();
  if ((nowMs - gLastPollMs) < BuildConfig::kC4001PollIntervalMs) {
    return;
  }
  gLastPollMs = nowMs;

  const CorePresence core = gPresence.readCore();
  const C4001PresenceRich& rich = gPresence.lastC4001Rich();
  const PresenceC4001::LinkStatus& link = gPresence.c4001LinkStatus();

  const bool hasTarget = (rich.targetNumber > 0);
  const bool accepted = hasTarget && rich.targetSampleAccepted;
  if (accepted) {
    gLastAcceptedMs = nowMs;
  }

  const uint8_t phase = classifyPhase(core, link, rich);
  const uint8_t reject = classifyReject(link, rich);
  const uint32_t heldAgeMs = (gLastAcceptedMs == 0 || accepted) ? 0u : (nowMs - gLastAcceptedMs);
  const int32_t rawMm = hasTarget ? mmFromMeters(rich.targetRangeRawM) : 0;
  const int32_t effMm = hasTarget ? mmFromMeters(rich.targetRangeM) : 0;
  const uint8_t nearField = (link.nearField || (hasTarget && (rich.targetRangeRawM <= BuildConfig::kC4001NearFieldStartM))) ? 1 : 0;

  Serial.print(nowMs);
  Serial.print(',');
  Serial.print(static_cast<uint8_t>(link.state));
  Serial.print(',');
  Serial.print(hasTarget ? 1 : 0);
  Serial.print(',');
  Serial.print(rawMm);
  Serial.print(',');
  Serial.print(effMm);
  Serial.print(',');
  Serial.print(phase);
  Serial.print(',');
  Serial.print(reject);
  Serial.print(',');
  Serial.print(heldAgeMs);
  Serial.print(',');
  Serial.print(nearField);
  Serial.print(',');
  Serial.println(link.nearFieldPendingCount);
}
