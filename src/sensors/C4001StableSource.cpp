#include "C4001StableSource.h"

#include <Arduino.h>
#include <DFRobot_C4001.h>
#include <Wire.h>

#ifndef NIGHTLIGHT_ENABLE_WATCHDOG
#define NIGHTLIGHT_ENABLE_WATCHDOG 1
#endif

#if NIGHTLIGHT_ENABLE_WATCHDOG && defined(__AVR__)
#include <avr/wdt.h>
#if defined(WDTO_2S)
#define C4001_WATCHDOG_AVAILABLE 1
#endif
#endif

#ifndef C4001_WATCHDOG_AVAILABLE
#define C4001_WATCHDOG_AVAILABLE 0
#endif

#include "../config/Profiles.h"

#ifndef C4001_ENABLE_SERIAL_EVENTS
#define C4001_ENABLE_SERIAL_EVENTS 0
#endif

// Sparse overnight diagnostics for the hard-to-reproduce C4001 zombie state.
// This is intentionally much lighter than full telemetry: one health line per
// minute plus init/recovery breadcrumbs. Set to 0 for the final silent build.
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
constexpr uint32_t kMaxInitRetryDelayMs = 30000;
constexpr uint8_t kMaxInitFailureCount = 6;

constexpr uint8_t kI2cSdaPin = A4;
constexpr uint8_t kI2cSclPin = A5;
uint16_t gI2cClearCount = 0;
uint8_t gLastI2cSdaHigh = 1;
uint8_t gLastI2cSclHigh = 1;

void kickWatchdog() {
#if C4001_WATCHDOG_AVAILABLE
  wdt_reset();
#endif
}

bool i2cBusLooksIdle() {
  return digitalRead(kI2cSdaPin) == HIGH && digitalRead(kI2cSclPin) == HIGH;
}

void releaseI2cLine(uint8_t pin) {
  pinMode(pin, INPUT_PULLUP);
}

void pullI2cLineLow(uint8_t pin) {
  digitalWrite(pin, LOW);
  pinMode(pin, OUTPUT);
}

void clearI2cBus() {
#if defined(WIRE_HAS_END)
  Wire.end();
#endif
  releaseI2cLine(kI2cSdaPin);
  releaseI2cLine(kI2cSclPin);
  delayMicroseconds(10);

  for (uint8_t i = 0; i < 16; ++i) {
    if (i >= 9 && i2cBusLooksIdle()) break;
    pullI2cLineLow(kI2cSclPin);
    delayMicroseconds(7);
    releaseI2cLine(kI2cSclPin);
    delayMicroseconds(7);
  }

  // STOP condition: SDA low while SCL is released high, then release SDA.
  pullI2cLineLow(kI2cSdaPin);
  delayMicroseconds(7);
  releaseI2cLine(kI2cSclPin);
  delayMicroseconds(7);
  releaseI2cLine(kI2cSdaPin);
  delayMicroseconds(10);

  gLastI2cSdaHigh = digitalRead(kI2cSdaPin) == HIGH ? 1 : 0;
  gLastI2cSclHigh = digitalRead(kI2cSclPin) == HIGH ? 1 : 0;
  ++gI2cClearCount;

  Wire.begin();
  delay(20);
  kickWatchdog();
}

float normalizeRange(float rangeM, const Profiles::C4001Profile& profile) {
  const float span = profile.rangeFarM - profile.rangeNearM;
  if (span <= 0.001f) return 0.0f;
  float t = (rangeM - profile.rangeNearM) / span;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return t;
}

int16_t toCenti(float value) {
  return (int16_t)(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
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
    clearI2cBus();
#if defined(WIRE_HAS_TIMEOUT)
    Wire.setWireTimeout(25000, true);
#endif
    delay(40);
    wireReady_ = true;
  }

  i2cOnline_ = false;
  configured_ = false;
  statusHealthy_ = false;
  manualInitRequested_ = false;
  everHadAcceptedTarget_ = false;
  droughtReinitRequested_ = false;
  recoveryStage_ = 0;
  lastRecoveryStep_ = 0;
  initFailureCount_ = 0;
  lastPollMs_ = 0;
  lastInitAttemptMs_ = 0;
  lastAcceptedMs_ = 0;
  lastRawReadMs_ = 0;
  lastStatusReadMs_ = 0;
  lastRawTargetNumber_ = 0;
  lastRawRangeM_ = 0.0f;
  lastRawSpeedMps_ = 0.0f;
  lastRawEnergy_ = 0;
  lastRawAccepted_ = false;
  lastStatusWork_ = 0;
  lastStatusMode_ = 0;
  lastStatusInit_ = 0;
  lastModeSetOk_ = false;
  lastDetectThresOk_ = false;
  stableHasTarget_ = false;
  stableRangeM_ = 1.2f;
  stableSpeedMps_ = 0.0f;
  smoothedCharge_ = 0.0f;
  smoothedIngress_ = 0.0f;
  continuity_ = 0.0f;
  phase_ = StableTrack::MotionPhase::None;
}

bool C4001StableSource::captureStatus(uint32_t nowMs) {
  if (!wireReady_) return false;
  if (!i2cBusLooksIdle()) {
    clearI2cBus();
    statusHealthy_ = false;
    lastStatusReadMs_ = 0;
    return false;
  }
  kickWatchdog();
  sSensorStatus_t data = gC4001.getStatus();
  kickWatchdog();
  lastStatusWork_ = data.workStatus;
  lastStatusMode_ = data.workMode;
  lastStatusInit_ = data.initStatus;
  lastStatusReadMs_ = nowMs;
  statusHealthy_ = i2cOnline_ && statusBitsHealthy();
  return statusHealthy_;
}

bool C4001StableSource::statusBitsHealthy() const {
  return lastStatusReadMs_ != 0 &&
         lastStatusWork_ == 1 &&
         lastStatusMode_ == eSpeedMode &&
         lastStatusInit_ == 1;
}

bool C4001StableSource::probeSpeedMode() {
  lastModeSetOk_ = false;
  i2cOnline_ = gC4001.begin();
  kickWatchdog();
  if (!i2cOnline_) {
    statusHealthy_ = false;
    lastStatusReadMs_ = 0;
    return false;
  }
  lastModeSetOk_ = gC4001.setSensorMode(eSpeedMode);
  kickWatchdog();
  captureStatus(millis());
  statusHealthy_ = i2cOnline_ && lastModeSetOk_ && statusBitsHealthy();
  return statusHealthy_;
}

uint32_t C4001StableSource::initRetryDelayMs() const {
  const auto& profile = Profiles::c4001();
  uint32_t delayMs = profile.initRetryMs;

  // Capped exponential backoff for cold/offline init only. The first retry
  // stays near the configured base delay; repeated failures rapidly settle at
  // a 30-second ceiling instead of hammering Wire/begin() forever.
  for (uint8_t i = 1; i < initFailureCount_; ++i) {
    if (delayMs >= kMaxInitRetryDelayMs / 2) {
      return kMaxInitRetryDelayMs;
    }
    delayMs *= 2;
    if (delayMs > kMaxInitRetryDelayMs) {
      return kMaxInitRetryDelayMs;
    }
  }

  return delayMs;
}

void C4001StableSource::noteInitFailure() {
  if (initFailureCount_ < kMaxInitFailureCount) {
    ++initFailureCount_;
  }
}

void C4001StableSource::printStatusTriple() const {
#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (lastStatusReadMs_ == 0) {
    Serial.print(F("x/x/x"));
    return;
  }
  Serial.print((int)lastStatusWork_);
  Serial.print('/');
  Serial.print((int)lastStatusMode_);
  Serial.print('/');
  Serial.print((int)lastStatusInit_);
#endif
}

bool C4001StableSource::tryInit() {
  if (!initialized_) begin();
  if (!wireReady_) return false;

  lastInitAttemptMs_ = millis();
  recoveryStage_ = 0;
  lastRecoveryStep_ = 0;
  lastModeSetOk_ = false;
  lastDetectThresOk_ = configured_;
  clearI2cBus();
  i2cOnline_ = gC4001.begin();
  kickWatchdog();
  if (!i2cOnline_) {
    statusHealthy_ = false;
    lastStatusReadMs_ = 0;
    noteInitFailure();
    return false;
  }

  lastModeSetOk_ = gC4001.setSensorMode(eSpeedMode);
  kickWatchdog();
  captureStatus(millis());
  if (lastModeSetOk_ && lastStatusWork_ == 0) {
    gC4001.setSensor(eStartSen);
    kickWatchdog();
    captureStatus(millis());
  }

  // Configuration is a boot/cold-init operation only. These DFRobot calls save
  // parameters internally, so drought recovery must not repeat them. Keep trying
  // on cold/offline init attempts until it actually succeeds; do not let one
  // half-awake ACK/status pass permanently skip threshold/fretting setup.
  if (!configured_) {
    if (lastModeSetOk_ && statusBitsHealthy()) {
      lastDetectThresOk_ = gC4001.setDetectThres(11, 1200, 10);
      kickWatchdog();
      if (lastDetectThresOk_) {
        gC4001.setFrettingDetection(eON);
        kickWatchdog();
        configured_ = true;
      }
      captureStatus(millis());
    } else {
      lastDetectThresOk_ = false;
    }
  }

  statusHealthy_ = i2cOnline_ && lastModeSetOk_ && statusBitsHealthy();
  if (statusHealthy_) {
    initFailureCount_ = 0;
  } else {
    noteInitFailure();
  }
  lastPollMs_ = 0;
  return statusHealthy_;
}

bool C4001StableSource::trySoftRecover() {
  if (!initialized_) begin();
  if (!wireReady_) return false;

  lastInitAttemptMs_ = millis();
  lastModeSetOk_ = false;
  lastDetectThresOk_ = configured_;

  clearI2cBus();

  // Gentle drought recovery only. Every call advances at most one rung, then
  // waits for the long cooldown in Profiles before trying the next rung. This
  // keeps the C4001 from being hammered during ordinary speed-mode silence.
  i2cOnline_ = gC4001.begin();
  kickWatchdog();
  if (!i2cOnline_) {
    statusHealthy_ = false;
    lastStatusReadMs_ = 0;
    return false;
  }

  const uint8_t stage = recoveryStage_;

  if (stage == 0) {
    // Rung 1: observe/status only. If the radar reports healthy speed mode,
    // leave it alone; silence by itself is not proof that the sensor is broken.
    lastRecoveryStep_ = 1;
    captureStatus(millis());
    statusHealthy_ = i2cOnline_ && statusBitsHealthy();
    recoveryStage_ = 1;
    lastPollMs_ = 0;
    return statusHealthy_;
  }

  if (stage == 1) {
    // Rung 2: minimal nudge. Ask the radar to keep sensing, but do not stop,
    // reset, save config, factory-recover, or change modes.
    lastRecoveryStep_ = 2;
    (void)gC4001.setSensor(eStartSen);
    kickWatchdog();
    delay(150);
    kickWatchdog();
    captureStatus(millis());
    statusHealthy_ = i2cOnline_ && statusBitsHealthy();
    recoveryStage_ = 2;
    lastPollMs_ = 0;
    return statusHealthy_;
  }

  if (stage == 2) {
    // Rung 3: repair mode only if status says the C4001 drifted out of the
    // speed/ranging mode this scene expects. Do not churn mode when it is sane.
    lastRecoveryStep_ = 3;
    captureStatus(millis());
    if (lastStatusReadMs_ != 0 && lastStatusMode_ != eSpeedMode) {
      lastModeSetOk_ = gC4001.setSensorMode(eSpeedMode);
      kickWatchdog();
      delay(150);
      kickWatchdog();
      captureStatus(millis());
    }
    statusHealthy_ = i2cOnline_ && statusBitsHealthy();
    recoveryStage_ = 3;
    lastPollMs_ = 0;
    return statusHealthy_;
  }

  // No automatic hard software reset in this show build. Keep future drought
  // attempts observational so the fallback scene remains stable instead of
  // repeatedly stressing the I2C bus. Manual init is still available.
  lastRecoveryStep_ = 4;
  captureStatus(millis());
  statusHealthy_ = i2cOnline_ && statusBitsHealthy();
  recoveryStage_ = 3;
  lastPollMs_ = 0;
  return statusHealthy_;
}

void C4001StableSource::service(uint32_t nowMs) {
  if (!initialized_) begin();

  const auto& profile = Profiles::c4001();
#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (gLastFaultDiagMs == 0 || (nowMs - gLastFaultDiagMs) >= kFaultDiagIntervalMs) {
    gLastFaultDiagMs = nowMs;
    Serial.print(F("rd r="));
    Serial.print(statusHealthy_ ? 1 : 0);
    Serial.print(F(" s="));
    printStatusTriple();
    Serial.print(F(" i="));
    Serial.print(lastModeSetOk_ ? 1 : 0);
    Serial.print('/');
    Serial.print(lastDetectThresOk_ ? 1 : 0);
    Serial.print(F(" rc="));
    Serial.print(lastRecoveryStep_);
    Serial.print(F(" rw="));
    Serial.print(lastRawTargetNumber_);
    Serial.print(',');
    Serial.print(toCenti(lastRawRangeM_));
    Serial.print(',');
    Serial.print(toCenti(lastRawSpeedMps_));
    Serial.print(',');
    Serial.print(lastRawEnergy_);
    Serial.print(F(" a="));
    Serial.print(lastRawAccepted_ ? 1 : 0);
    Serial.print(F(" tb="));
    Serial.print(stableHasTarget_ ? 1 : 0);
    Serial.print(',');
    Serial.print(toCenti(stableRangeM_));
    Serial.print(',');
    Serial.print(toCenti(stableSpeedMps_));
    Serial.print(F(" e="));
    Serial.print(everHadAcceptedTarget_ ? 1 : 0);
    Serial.print(F(" d="));
    Serial.print(droughtReinitRequested_ ? 1 : 0);
    Serial.print(F(" aa="));
    Serial.print(lastAcceptedMs_ == 0 ? 0 : nowMs - lastAcceptedMs_);
    Serial.print(F(" ar="));
    Serial.print(lastRawReadMs_ == 0 ? 0 : nowMs - lastRawReadMs_);
    Serial.print(F(" ai="));
    Serial.print(lastInitAttemptMs_ == 0 ? 0 : nowMs - lastInitAttemptMs_);
    Serial.print(F(" if="));
    Serial.print(initFailureCount_);
    Serial.print(F(" nr="));
    Serial.print(initRetryDelayMs());
    Serial.print(F(" bc="));
    Serial.print(gI2cClearCount);
    Serial.print(',');
    Serial.print(gLastI2cSdaHigh);
    Serial.print('/');
    Serial.println(gLastI2cSclHigh);
  }
#endif
  const uint32_t retryDelayMs = initRetryDelayMs();
  const bool retryElapsed =
      lastInitAttemptMs_ == 0 || (nowMs - lastInitAttemptMs_) >= retryDelayMs;
  const bool reinitCooldownElapsed =
      lastInitAttemptMs_ == 0 || (nowMs - lastInitAttemptMs_) >= profile.reinitCooldownMs;

  bool shouldAttempt = manualInitRequested_;

  if (profile.enableC4001AutoInit) {
    if (!statusHealthy_ && retryElapsed) {
      shouldAttempt = true;
    } else if (droughtReinitRequested_ && reinitCooldownElapsed) {
      shouldAttempt = true;
    }
  }

  if (!shouldAttempt) return;

  const bool manualAttempt = manualInitRequested_;
  const bool droughtAttempt = droughtReinitRequested_ && statusHealthy_;
  manualInitRequested_ = false;
  droughtReinitRequested_ = false;

  const bool logAttempt = manualAttempt || droughtAttempt ||
      gLastInitAttemptLogMs == 0 || (nowMs - gLastInitAttemptLogMs) >= kInitAttemptLogIntervalMs;
#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (logAttempt) {
    gLastInitAttemptLogMs = nowMs;
    Serial.print(F("ci "));
    if (manualAttempt) Serial.print(F("man"));
    else if (droughtAttempt) Serial.print(F("dr"));
    else Serial.print(F("off"));
    Serial.print(F(" wr="));
    Serial.println(statusHealthy_ ? 1 : 0);
    Serial.flush();
  }
#endif

  const bool wasReady = statusHealthy_;
  statusHealthy_ = droughtAttempt ? trySoftRecover() : tryInit();

#if C4001_ENABLE_FAULT_DIAGNOSTICS
  if (logAttempt) {
    if (droughtAttempt) {
      Serial.print(F("cr ok="));
      Serial.print(statusHealthy_ ? 1 : 0);
      Serial.print(F(" stp="));
      Serial.print(lastRecoveryStep_);
    } else {
      Serial.print(F("ci ok="));
      Serial.print(statusHealthy_ ? 1 : 0);
    }
    Serial.print(F(" mo="));
    Serial.print(lastModeSetOk_ ? 1 : 0);
    Serial.print(F(" th="));
    Serial.print(lastDetectThresOk_ ? 1 : 0);
    Serial.print(F(" s="));
    printStatusTriple();
    Serial.println();
  }
#endif

  if (statusHealthy_) {
    initFailureCount_ = 0;
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
  if (!statusHealthy_) {
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

  if (!i2cBusLooksIdle()) {
    clearI2cBus();
    statusHealthy_ = false;
    noteNoAcceptedTarget(nowMs);
    updateSmoothedSignals(stableHasTarget_);
    return currentTrack();
  }

  kickWatchdog();
  const int targetNumber = gC4001.getTargetNumber();
  kickWatchdog();
  float sensedRange = 0.0f;
  float sensedSpeed = 0.0f;

  if (targetNumber > 0) {
    sensedRange = gC4001.getTargetRange();
    sensedSpeed = gC4001.getTargetSpeed();
    kickWatchdog();
  }

  lastRawReadMs_ = nowMs;
  lastRawTargetNumber_ = targetNumber;
  lastRawRangeM_ = sensedRange;
  lastRawSpeedMps_ = sensedSpeed;
  lastRawEnergy_ = 0;

  if (targetNumber > 0 &&
      sensedRange >= profile.rangeNearM &&
      sensedRange <= profile.rangeFarM &&
      fabsf(sensedSpeed) <= kMaxAcceptedSpeedMps) {
    accepted = true;
    rawRangeM = sensedRange;
    rawSpeedMps = sensedSpeed;
  }
  lastRawAccepted_ = accepted;

  if (accepted) {
    if (!stableHasTarget_ && everHadAcceptedTarget_) {
      #if C4001_ENABLE_SERIAL_EVENTS
      Serial.println("event=c4001_read_resume");
      #endif
    }

    stableHasTarget_ = true;
    everHadAcceptedTarget_ = true;
    droughtReinitRequested_ = false;
    recoveryStage_ = 0;
    lastRecoveryStep_ = 0;
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

  if (!statusHealthy_ ||
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
  t.online = statusHealthy_;
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
