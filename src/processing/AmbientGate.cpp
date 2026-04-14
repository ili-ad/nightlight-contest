#include "AmbientGate.h"

#include <Arduino.h>

#include "../BuildConfig.h"

void AmbientGate::clearPending() {
  pendingActive_ = false;
  pendingToDark_ = false;
  pendingSinceMs_ = 0;
  pendingRequiredMs_ = 0;
}

AmbientGateResult AmbientGate::update(float lux,
                                      LampState lampState,
                                      float presenceConfidence) {
  const uint32_t nowMs = millis();

  if (!smoothingReady_) {
    gateLux_ = lux;
    smoothingReady_ = true;
  } else {
    gateLux_ += (lux - gateLux_) * BuildConfig::kAmbientGateSmoothingAlpha;
  }

  if (!initialized_) {
    darkAllowed_ = (gateLux_ <= BuildConfig::kDarkEnterLux);
    initialized_ = true;
    lastTransitionMs_ = nowMs;
    clearPending();
  }

  const uint32_t holdMs =
      darkAllowed_ ? BuildConfig::kAmbientMinNightHoldMs : BuildConfig::kAmbientMinDayHoldMs;
  const uint32_t elapsedInStateMs = nowMs - lastTransitionMs_;
  const bool holdActive = elapsedInStateMs < holdMs;
  const uint32_t holdRemainingMs = holdActive ? (holdMs - elapsedInStateMs) : 0;

  const bool candidateToDark = !darkAllowed_ && (gateLux_ <= BuildConfig::kDarkEnterLux);
  const float dayExitLux =
      BuildConfig::kDarkExitLux +
      (darkAllowed_ ? BuildConfig::kAmbientNightSelfLightExitMarginLux : 0.0f);
  const bool candidateToDay = darkAllowed_ && (gateLux_ >= dayExitLux);

  const bool activeNightState =
      (lampState == LampState::ActiveInterpretive) || (lampState == LampState::Decay);
  const float activeSuppressPresenceThreshold =
      (lampState == LampState::Decay)
          ? BuildConfig::kAmbientActiveModeSuppressDecayMinPresenceConfidence
          : BuildConfig::kAmbientActiveModeSuppressMinPresenceConfidence;
  const bool strongPresence = presenceConfidence >= activeSuppressPresenceThreshold;
  const bool escapeBrightnessOverride =
      gateLux_ >= BuildConfig::kAmbientActiveModeSuppressBrightOverrideLux;
  const bool candidateSuppressedByActive = candidateToDay && activeNightState && strongPresence;

  if (!candidateSuppressedByActive) {
    suppressingDayExit_ = false;
    suppressDayExitSinceMs_ = 0;
  } else if (!suppressingDayExit_) {
    suppressingDayExit_ = true;
    suppressDayExitSinceMs_ = nowMs;
  }

  const uint32_t suppressElapsedMs = suppressingDayExit_ ? (nowMs - suppressDayExitSinceMs_) : 0;
  const bool suppressEscapeByTime =
      suppressingDayExit_ &&
      (suppressElapsedMs >= BuildConfig::kAmbientActiveModeSuppressMaxBlockMs);
  const bool suppressDayExitByActive =
      candidateSuppressedByActive && !escapeBrightnessOverride && !suppressEscapeByTime;
  const bool suppressionEscaped = candidateSuppressedByActive && !suppressDayExitByActive;

  bool wantsTransition = false;
  bool targetToDark = false;
  uint32_t requiredDwellMs = 0;
  if (candidateToDark) {
    wantsTransition = true;
    targetToDark = true;
    requiredDwellMs = BuildConfig::kAmbientEnterDwellMs;
  } else if (candidateToDay) {
    if (!suppressDayExitByActive) {
      wantsTransition = true;
      targetToDark = false;
      requiredDwellMs = BuildConfig::kAmbientExitDwellMs;
      if (darkAllowed_) {
        requiredDwellMs += BuildConfig::kAmbientNightSelfLightExtraExitDwellMs;
      }
      if (candidateSuppressedByActive) {
        requiredDwellMs += BuildConfig::kAmbientActiveModeSuppressEscapedExitDwellMs;
      }
    }
  }

  if (!wantsTransition) {
    clearPending();
  } else if (!pendingActive_ || pendingToDark_ != targetToDark) {
    pendingActive_ = true;
    pendingToDark_ = targetToDark;
    pendingSinceMs_ = nowMs;
    pendingRequiredMs_ = requiredDwellMs;
  } else {
    pendingRequiredMs_ = requiredDwellMs;
  }

  bool transitionCommitted = false;
  if (pendingActive_ && !holdActive) {
    const uint32_t pendingElapsedMs = nowMs - pendingSinceMs_;
    if (pendingElapsedMs >= pendingRequiredMs_) {
      darkAllowed_ = pendingToDark_;
      lastTransitionMs_ = nowMs;
      transitionCommitted = true;
      clearPending();
    }
  }

  AmbientGateResult result;
  result.darkAllowed = darkAllowed_;
  result.rawLux = lux;
  result.gateLux = gateLux_;
  result.transitionCommitted = transitionCommitted;
  result.waitingOnDwell = pendingActive_ && !holdActive;
  result.waitingOnHold = pendingActive_ && holdActive;
  result.pendingToDark = pendingToDark_;
  result.pendingElapsedMs = pendingActive_ ? (nowMs - pendingSinceMs_) : 0;
  result.pendingRequiredMs = pendingActive_ ? pendingRequiredMs_ : 0;
  result.holdRemainingMs = pendingActive_ ? holdRemainingMs : 0;
  result.dayExitSuppressedByActive = suppressDayExitByActive;
  result.dayExitSuppressionEscaped = suppressionEscaped;
  return result;
}
