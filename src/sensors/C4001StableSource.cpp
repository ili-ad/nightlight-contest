#include "C4001StableSource.h"

#include <Arduino.h>
#include <DFRobot_C4001.h>
#include <Wire.h>

#include "../config/Profiles.h"

#ifndef C4001_ENABLE_SERIAL_EVENTS
#define C4001_ENABLE_SERIAL_EVENTS 0
#endif

// Sparse overnight diagnostics for the hard-to-reproduce C4001 zombie state.
// This is intentionally much lighter than full telemetry: one health line per
// minute plus init attempt/result breadcrumbs. Set to 0 for the final silent build.
#ifndef C4001_ENABLE_FAULT_DIAGNOSTICS
#define C4001_ENABLE_FAULT_DIAGNOSTICS 1
#endif

namespace {
constexpr uint8_t kC4001I2cAddress = 0x2B;
constexpr float kMaxAcceptedSpeedMps = 2.60f;
DFRobot_C4001_I2C gC4001(&Wire, kC4001I2cAddress);
uint32_t gLastFaultDiagMs = 0;
uint32_t gLastInitAttemptLogMs = 0;
constexpr uint32_t kFaultDiagIntervalMs = 60000;
constexpr uint32_t kInitAttemptLogIntervalMs = 60000;

float normalizeRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float span = profile.rangeFarM - profile.rangeNearM;
  if (span <= 0.001f) return 0.0f;
  float t = (rangeM - profile.rangeNearM) / span;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t;
}

float chargeFromRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float nearness = 1.0f - normalizeRange(rangeM, profile);
  const float curved = nearness + (profile.nearnessLiftGain * nearness * (1.0f - nearness));
  return curved > 1.0f ? 1.0f : curved;
}

}  // namespace

void C4001StableSource::begin() {
  initialized_ = true;
  if (!wireReady_) {
    Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
    Wire.setWireTimeout(25000, true);
#endif
    delay(60);
    wireReady_ = true;
  }

  sensorReady_ = false;
  manualInitRequested_ = false;
  everHadAcceptedTarget_ = false;
  droughtReinitRequested_ = false;
  lastPollMs_ = 0;
  lastInitAttemptMs_ = 0;
  lastAcceptedMs_ = 0;
  stableHasTarget_ = false;
  stableRangeM_ = 1.2f;
  stableSpeedMps_ = 0.0f;
  smoothedCharge_ = 0.0f;
  smoothedIngress_ = 0.0f;
  continuity_ = 0.0f;
  phase_ = StableTrack::MotionPhase::None;
}

bool C4001StableSource::tryInit() {
  if (!initialized_) begin();
  if (!wireReady_) return false;

  lastInitAttemptMs_ = millis();
  if (!gC4001.begin()) {
    sensorReady_ = false;
    return false;
  }

  gC4001.setSensorMode(eSpeedMode);
  gC4001.setDetectThres(11, 1200, 10);
  gC4001.setFrettingDetection(eON);
  sensorReady_ = true;
  lastPollMs_ = 0;
  return true;
}

void C4001StableSource::service(uint32_t nowMs) {
  if (!initialized_) begin();

  const auto& profile = Profiles::c4001();
#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (gLastFaultDiagMs == 0 || (nowMs - gLastFaultDiagMs) >= kFaultDiagIntervalMs) {
    gLastFaultDiagMs = nowMs;
    Serial.print("radar_diag ready=");
    Serial.print(sensorReady_ ? 1 : 0);
    Serial.print(" target=");
    Serial.print(stableHasTarget_ ? 1 : 0);
    Serial.print(" ever=");
    Serial.print(everHadAcceptedTarget_ ? 1 : 0);
    Serial.print(" drought=");
    Serial.print(droughtReinitRequested_ ? 1 : 0);
    Serial.print(" ageAcceptedMs=");
    Serial.print(lastAcceptedMs_ == 0 ? 0 : nowMs - lastAcceptedMs_);
    Serial.print(" ageInitMs=");
    Serial.println(lastInitAttemptMs_ == 0 ? 0 : nowMs - lastInitAttemptMs_);
  }
#endif
  const bool retryElapsed =
      lastInitAttemptMs_ == 0 || (nowMs - lastInitAttemptMs_) >= profile.initRetryMs;
  const bool reinitCooldownElapsed =
      lastInitAttemptMs_ == 0 || (nowMs - lastInitAttemptMs_) >= profile.reinitCooldownMs;

  bool shouldAttempt = manualInitRequested_;

  if (profile.enableC4001AutoInit) {
    if (!sensorReady_ && retryElapsed) {
      shouldAttempt = true;
    } else if (droughtReinitRequested_ && reinitCooldownElapsed) {
      shouldAttempt = true;
    }
  }

  if (!shouldAttempt) return;

  const bool manualAttempt = manualInitRequested_;
  const bool droughtAttempt = droughtReinitRequested_ && sensorReady_;
  manualInitRequested_ = false;
  droughtReinitRequested_ = false;

  const bool logAttempt = manualAttempt || droughtAttempt ||
      gLastInitAttemptLogMs == 0 || (nowMs - gLastInitAttemptLogMs) >= kInitAttemptLogIntervalMs;
#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (logAttempt) {
    gLastInitAttemptLogMs = nowMs;
    Serial.print("event=c4001_init_attempt reason=");
    if (manualAttempt) Serial.print("manual");
    else if (droughtAttempt) Serial.print("drought");
    else Serial.print("offline");
    Serial.print(" wasReady=");
    Serial.println(sensorReady_ ? 1 : 0);
    Serial.flush();
  }
#endif

  const bool wasReady = sensorReady_;
  sensorReady_ = tryInit();

#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (logAttempt) {
    Serial.print("event=c4001_init_result ok=");
    Serial.println(sensorReady_ ? 1 : 0);
  }
#endif

  if (sensorReady_) {
    if (droughtAttempt) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("event=c4001_reinit_after_dropout");
      #endif
    } else if (!wasReady) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("event=c4001_init_online");
      #endif
    } else if (manualAttempt) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("event=c4001_manual_reinit");
      #endif
    }
  } else {
    if (droughtAttempt) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("warn=c4001_reinit_after_dropout_failed");
      #endif
    } else if (manualAttempt) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("warn=c4001_manual_init_failed");
      #endif
    }
  }
}

void C4001StableSource::requestManualInit() {
  if (!initialized_) begin();
  manualInitRequested_ = true;
}

StableTrack C4001StableSource::read(uint32_t nowMs) {
  const auto& profile = Profiles::c4001();
  if (!initialized_) {
    begin();
  }

  // If the sensor is offline, keep any remembered accepted target in hold/fade
  // instead of hard-zeroing immediately. This keeps the scene alive while the
  // service path retries the C4001 after render.
  if (!sensorReady_) {
    noteNoAcceptedTarget(nowMs);
    updateSmoothedSignals(stableHasTarget_);
    return currentTrack();
  }

  if (lastPollMs_ != 0 && (nowMs - lastPollMs_) < profile.pollIntervalMs) {
    return currentTrack();
  }
  lastPollMs_ = nowMs;

  bool accepted = false;
  float rawRangeM = stableRangeM_;
  float rawSpeedMps = 0.0f;

  const int targetNumber = gC4001.getTargetNumber();
  const float sensedRange = gC4001.getTargetRange();
  const float sensedSpeed = gC4001.getTargetSpeed();
  // Energy has shown unstable / overflow-looking values in logs. Keep reading it
  // to match the bench sketch's sampling cadence, but do not use it for charge.
  (void)gC4001.getTargetEnergy();

  if (targetNumber > 0 &&
      sensedRange >= profile.rangeNearM &&
      sensedRange <= profile.rangeFarM &&
      fabsf(sensedSpeed) <= kMaxAcceptedSpeedMps) {
    accepted = true;
    rawRangeM = sensedRange;
    rawSpeedMps = sensedSpeed;
  }

  if (accepted) {
    if (!stableHasTarget_ && everHadAcceptedTarget_) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("event=c4001_read_resume");
      #endif
    }

    stableHasTarget_ = true;
    everHadAcceptedTarget_ = true;
    droughtReinitRequested_ = false;
    lastAcceptedMs_ = nowMs;
    stableRangeM_ = rawRangeM;
    stableSpeedMps_ = smooth(stableSpeedMps_, rawSpeedMps, profile.speedAlpha);
    continuity_ = 1.0f;
  } else {
    noteNoAcceptedTarget(nowMs);
    maybeRequestDroughtReinit(nowMs);
  }

  updateSmoothedSignals(stableHasTarget_);
  return currentTrack();
}

void C4001StableSource::noteNoAcceptedTarget(uint32_t nowMs) {
  const auto& profile = Profiles::c4001();

  if (stableHasTarget_ && lastAcceptedMs_ != 0) {
    const uint32_t ageMs = nowMs - lastAcceptedMs_;
    if (ageMs <= profile.holdMs) {
      continuity_ = 1.0f;
      stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
      return;
    }

    const uint32_t fadeEndMs = profile.holdMs + profile.fadeMs;
    if (ageMs < fadeEndMs) {
      const float t = float(ageMs - profile.holdMs) /
                      float(profile.fadeMs > 0 ? profile.fadeMs : 1);
      continuity_ = clamp01(1.0f - t);
      stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
      return;
    }
  }

  stableHasTarget_ = false;
  continuity_ = 0.0f;
  stableSpeedMps_ = smooth(stableSpeedMps_, 0.0f, profile.speedDecayAlpha);
}

void C4001StableSource::maybeRequestDroughtReinit(uint32_t nowMs) {
  const auto& profile = Profiles::c4001();

  if (!sensorReady_ ||
      !profile.enableC4001AutoInit ||
      !everHadAcceptedTarget_ ||
      lastAcceptedMs_ == 0) {
    return;
  }

  const bool droughtElapsed = (nowMs - lastAcceptedMs_) >= profile.acceptedDroughtReinitMs;
  const bool cooldownElapsed =
      lastInitAttemptMs_ == 0 || (nowMs - lastInitAttemptMs_) >= profile.reinitCooldownMs;

  if (droughtElapsed && cooldownElapsed) {
    droughtReinitRequested_ = true;
  }
}

void C4001StableSource::updateSmoothedSignals(bool hasEffectiveTarget) {
  const auto& profile = Profiles::c4001();

  const float targetCharge =
      hasEffectiveTarget ? chargeFromRange(stableRangeM_, profile) * continuity_ : 0.0f;
  smoothedCharge_ = smooth(smoothedCharge_, targetCharge,
                           hasEffectiveTarget ? profile.chargeRiseAlpha : profile.chargeFallAlpha);

  const float motion = clamp01(fabsf(stableSpeedMps_) / 0.35f);
  const float ingressTarget =
      hasEffectiveTarget ? clamp01((motion * 0.78f) + (smoothedCharge_ * 0.26f)) : 0.0f;
  smoothedIngress_ = smooth(smoothedIngress_, ingressTarget,
                            hasEffectiveTarget ? profile.ingressRiseAlpha
                                               : profile.ingressFallAlpha);

  if (!hasEffectiveTarget || continuity_ <= 0.001f) {
    phase_ = StableTrack::MotionPhase::None;
  } else if (fabsf(stableSpeedMps_) <= profile.stillSpeedMps) {
    phase_ = StableTrack::MotionPhase::Still;
  } else if (stableSpeedMps_ < 0.0f) {
    phase_ = StableTrack::MotionPhase::Approach;
  } else {
    phase_ = StableTrack::MotionPhase::Retreat;
  }
}

StableTrack C4001StableSource::currentTrack() const {
  StableTrack t;
  t.online = sensorReady_;
  t.hasTarget = stableHasTarget_ && continuity_ > 0.001f;
  t.rangeM = stableRangeM_;
  t.speedMps = stableSpeedMps_;
  t.charge = smoothedCharge_;
  t.ingressLevel = smoothedIngress_;
  t.continuity = continuity_;
  t.phase = phase_;
  return t;
}

float C4001StableSource::clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float C4001StableSource::smooth(float previous, float target, float alpha) {
  const float a = clamp01(alpha);
  return previous + ((target - previous) * a);
}
