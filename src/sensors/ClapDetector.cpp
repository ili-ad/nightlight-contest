#include "ClapDetector.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr uint8_t kDefaultMicAnalogPin = A0;

constexpr float kFirstThresholdMin = 2.0f;
constexpr float kFirstThresholdScale = 0.94f;
constexpr float kFirstThresholdOffset = 1.07f;

constexpr float kSecondThresholdMin = 1.54f;
constexpr float kSecondThresholdScale = 0.54f;
constexpr float kSecondThresholdOffset = 0.74f;

constexpr uint32_t kTriggerCooldownMs = 320;
constexpr uint32_t kSecondGapMinMs = 90;
constexpr uint32_t kSecondGapMaxMs = 950;
constexpr uint32_t kSecondListenTimeoutMs = 1250;
constexpr uint32_t kQuietRearmMs = 45;
constexpr uint32_t kModeAdvanceLockoutMs = 600;

constexpr float kFirstReleaseFactor = 0.56f;
}  // namespace

void ClapDetector::begin(uint8_t micAnalogPin) {
  micAnalogPin_ = micAnalogPin;
  pinMode(micAnalogPin_, INPUT);

  initialized_ = true;
  baseline_ = 0.0f;
  devEma_ = 0.0f;
  env_ = 0.0f;
  noiseFloor_ = 1.0f;

  armed_ = false;
  readyForSecond_ = false;
  firstClapMs_ = 0;
  lastTriggerMs_ = 0;
  armQuietSinceMs_ = 0;
  modeAdvanceLockoutUntilMs_ = 0;
}

void ClapDetector::begin() {
  begin(kDefaultMicAnalogPin);
}

bool ClapDetector::update(uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  const int raw = analogRead(micAnalogPin_);

  if (baseline_ == 0.0f) {
    baseline_ = raw;
  }
  baseline_ += (raw - baseline_) * 0.02f;

  const float dev = fabsf(raw - baseline_);
  devEma_ += (dev - devEma_) * 0.08f;
  env_ += (devEma_ - env_) * 0.22f;

  const bool learnFloor = !armed_ && (nowMs - lastTriggerMs_ > kTriggerCooldownMs);
  if (learnFloor) {
    noiseFloor_ += (env_ - noiseFloor_) * 0.015f;
    if (noiseFloor_ < 0.6f) {
      noiseFloor_ = 0.6f;
    }
  }

  float thr1 = (noiseFloor_ * kFirstThresholdScale) + kFirstThresholdOffset;
  if (thr1 < kFirstThresholdMin) {
    thr1 = kFirstThresholdMin;
  }
  if (thr1 > 14.0f) {
    thr1 = 14.0f;
  }

  float thr2 = (noiseFloor_ * kSecondThresholdScale) + kSecondThresholdOffset;
  if (thr2 < kSecondThresholdMin) {
    thr2 = kSecondThresholdMin;
  }
  if (thr2 > (thr1 - 0.8f)) {
    thr2 = thr1 - 0.8f;
  }
  if (thr2 < 4.0f) {
    thr2 = 4.0f;
  }

  const float release1 = thr1 * kFirstReleaseFactor;

  if (!armed_) {
    if (env_ >= thr1 &&
        (nowMs - lastTriggerMs_ > kTriggerCooldownMs) &&
        (nowMs >= modeAdvanceLockoutUntilMs_)) {
      armed_ = true;
      readyForSecond_ = false;
      firstClapMs_ = nowMs;
      lastTriggerMs_ = nowMs;
      armQuietSinceMs_ = 0;
    }
  } else {
    if (!readyForSecond_) {
      if (env_ <= release1) {
        if (armQuietSinceMs_ == 0) {
          armQuietSinceMs_ = nowMs;
        }
        if (nowMs - armQuietSinceMs_ >= kQuietRearmMs) {
          readyForSecond_ = true;
        }
      } else {
        armQuietSinceMs_ = 0;
      }
    } else {
      const uint32_t dt = nowMs - firstClapMs_;
      if (env_ >= thr2 && dt >= kSecondGapMinMs && dt <= kSecondGapMaxMs) {
        armed_ = false;
        readyForSecond_ = false;
        armQuietSinceMs_ = 0;
        lastTriggerMs_ = nowMs;
        modeAdvanceLockoutUntilMs_ = nowMs + kModeAdvanceLockoutMs;
        return true;
      }

      if (env_ >= thr1 && dt > kSecondGapMinMs) {
        firstClapMs_ = nowMs;
        lastTriggerMs_ = nowMs;
        readyForSecond_ = false;
        armQuietSinceMs_ = 0;
      }
    }

    if (armed_ && (nowMs - firstClapMs_ > kSecondListenTimeoutMs)) {
      armed_ = false;
      readyForSecond_ = false;
      armQuietSinceMs_ = 0;
    }
  }

  return false;
}
