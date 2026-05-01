#include "ClapDetector.h"

#include <Arduino.h>
#include <math.h>

namespace {
constexpr uint8_t kDefaultMicAnalogPin = A0;

// KY-037 analog detector constants, ported from bench/KY037ClapSmokeAnalogV2.
constexpr uint32_t kSampleUs = 1000;
constexpr uint8_t kMaxSamplesPerUpdate = 24;

constexpr float kFirstDevMin = 18.0f;
constexpr float kFirstP2PMin = 24.0f;
constexpr float kSecondDevMin = 14.0f;
constexpr float kSecondP2PMin = 16.0f;
constexpr float kFirstP2POnlyDevMin = 10.0f;
constexpr float kSecondP2POnlyDevMin = 6.0f;

constexpr float kFirstDevFloorScale = 6.0f;
constexpr float kFirstP2PFloorScale = 4.0f;
constexpr float kSecondDevFloorScale = 4.5f;
constexpr float kSecondP2PFloorScale = 3.0f;
constexpr float kDevFloorOffset = 6.0f;
constexpr float kP2PFloorOffset = 4.0f;

constexpr uint32_t kCandidateLockoutMs = 80;
constexpr uint32_t kSecondGapMinMs = 70;
constexpr bool kEnableFastSecondDuringRelease = true;
constexpr uint32_t kFastSecondGapMinMs = 115;
constexpr float kFastSecondDevMin = 22.0f;
constexpr float kFastSecondP2PMin = 26.0f;
constexpr uint32_t kSecondGapMaxMs = 1900;
constexpr uint32_t kQuietRearmMs = 8;
constexpr uint32_t kPostToggleCooldownMs = 350;
constexpr float kReleaseFactor = 0.66f;
}  // namespace

float ClapDetector::maxf(float a, float b) {
  return a > b ? a : b;
}

void ClapDetector::begin(uint8_t micAnalogPin) {
  micAnalogPin_ = micAnalogPin;
  pinMode(micAnalogPin_, INPUT);

  initialized_ = true;
  state_ = DetectorState::Idle;
  baseline_ = 0.0f;
  devFloor_ = 1.0f;
  p2pFloor_ = 6.0f;
  windowHead_ = 0;
  windowCount_ = 0;
  lastCandidateMs_ = 0;
  firstClapMs_ = 0;
  quietSinceMs_ = 0;
  lastToggleMs_ = 0;

  seedBaseline();
  lastSampleUs_ = micros();
}

void ClapDetector::begin() {
  begin(kDefaultMicAnalogPin);
}

bool ClapDetector::update(uint32_t nowMs) {
  (void)nowMs;
  if (!initialized_) {
    begin();
  }

  bool doubleClap = false;
  const uint32_t nowUs = micros();
  uint8_t processed = 0;

  while (static_cast<uint32_t>(nowUs - lastSampleUs_) >= kSampleUs) {
    lastSampleUs_ += kSampleUs;
    const uint16_t raw = static_cast<uint16_t>(analogRead(micAnalogPin_));
    if (processSample(raw, millis())) {
      doubleClap = true;
    }

    ++processed;
    if (processed >= kMaxSamplesPerUpdate) {
      // Avoid long catch-up bursts if rendering or sensor service stalls.
      lastSampleUs_ = nowUs;
      break;
    }
  }

  return doubleClap;
}

bool ClapDetector::isWaitingForSecondClap() const {
  return state_ == DetectorState::WaitRelease || state_ == DetectorState::WaitSecond;
}

uint32_t ClapDetector::firstClapMs() const {
  return firstClapMs_;
}

void ClapDetector::seedBaseline() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 96; ++i) {
    const uint16_t raw = static_cast<uint16_t>(analogRead(micAnalogPin_));
    sum += raw;
    pushWindow(raw);
    delay(1);
  }
  baseline_ = static_cast<float>(sum) / 96.0f;
}

void ClapDetector::pushWindow(uint16_t raw) {
  window_[windowHead_] = raw;
  windowHead_ = (windowHead_ + 1) % kP2PWindowSamples;
  if (windowCount_ < kP2PWindowSamples) {
    ++windowCount_;
  }
}

uint16_t ClapDetector::currentP2P() const {
  if (windowCount_ == 0) {
    return 0;
  }

  uint16_t wMin = 1023;
  uint16_t wMax = 0;
  for (uint8_t i = 0; i < windowCount_; ++i) {
    const uint16_t raw = window_[i];
    if (raw < wMin) wMin = raw;
    if (raw > wMax) wMax = raw;
  }
  return wMax - wMin;
}

bool ClapDetector::p2pCandidateWithDev(float dev, uint16_t p2p, float p2pThreshold,
                                       float p2pOnlyDevMin) const {
  return static_cast<float>(p2p) >= p2pThreshold && dev >= p2pOnlyDevMin;
}

void ClapDetector::learnQuietSignal(uint16_t raw, float dev, uint16_t p2p, bool quiet, uint32_t nowMs) {
  if (!quiet) return;
  if (nowMs - lastCandidateMs_ < 250) return;

  baseline_ += (static_cast<float>(raw) - baseline_) * 0.006f;
  devFloor_ += (dev - devFloor_) * 0.015f;
  p2pFloor_ += (static_cast<float>(p2p) - p2pFloor_) * 0.015f;

  if (devFloor_ < 0.6f) devFloor_ = 0.6f;
  if (p2pFloor_ < 2.0f) p2pFloor_ = 2.0f;
  if (p2pFloor_ > 80.0f) p2pFloor_ = 80.0f;
}

bool ClapDetector::acceptDoubleClap(uint32_t nowMs) {
  state_ = DetectorState::Idle;
  lastCandidateMs_ = nowMs;
  lastToggleMs_ = nowMs;
  quietSinceMs_ = 0;
  return true;
}

bool ClapDetector::processSample(uint16_t raw, uint32_t nowMs) {
  pushWindow(raw);

  if (baseline_ <= 0.0f) {
    baseline_ = static_cast<float>(raw);
  }

  const float dev = fabsf(static_cast<float>(raw) - baseline_);
  const uint16_t p2p = currentP2P();

  const float firstDevThreshold = maxf(kFirstDevMin, devFloor_ * kFirstDevFloorScale + kDevFloorOffset);
  const float firstP2PThreshold = maxf(kFirstP2PMin, p2pFloor_ * kFirstP2PFloorScale + kP2PFloorOffset);
  const float secondDevThreshold = maxf(kSecondDevMin, devFloor_ * kSecondDevFloorScale + kDevFloorOffset);
  const float secondP2PThreshold = maxf(kSecondP2PMin, p2pFloor_ * kSecondP2PFloorScale + kP2PFloorOffset);

  const float releaseDevThreshold = secondDevThreshold * kReleaseFactor;
  const float releaseP2PThreshold = secondP2PThreshold * kReleaseFactor;
  const bool quiet = dev <= releaseDevThreshold && static_cast<float>(p2p) <= releaseP2PThreshold;

  learnQuietSignal(raw, dev, p2p, quiet, nowMs);

  const bool lockedOut = nowMs - lastCandidateMs_ < kCandidateLockoutMs;
  const bool firstCandidate = !lockedOut &&
      (dev >= firstDevThreshold ||
       p2pCandidateWithDev(dev, p2p, firstP2PThreshold, kFirstP2POnlyDevMin));
  const bool secondCandidate = !lockedOut &&
      (dev >= secondDevThreshold ||
       p2pCandidateWithDev(dev, p2p, secondP2PThreshold, kSecondP2POnlyDevMin));

  switch (state_) {
    case DetectorState::Idle:
      if (firstCandidate && nowMs - lastToggleMs_ > kPostToggleCooldownMs) {
        state_ = DetectorState::WaitRelease;
        firstClapMs_ = nowMs;
        lastCandidateMs_ = nowMs;
        quietSinceMs_ = 0;
      }
      break;

    case DetectorState::WaitRelease: {
      const uint32_t dt = nowMs - firstClapMs_;
      const float fastDevThreshold = maxf(secondDevThreshold, kFastSecondDevMin);
      const float fastP2PThreshold = maxf(secondP2PThreshold, kFastSecondP2PMin);
      const bool fastSecondCandidate = kEnableFastSecondDuringRelease &&
          !lockedOut &&
          dt >= kFastSecondGapMinMs &&
          dt <= kSecondGapMaxMs &&
          dev >= fastDevThreshold &&
          static_cast<float>(p2p) >= fastP2PThreshold;

      if (fastSecondCandidate) {
        return acceptDoubleClap(nowMs);
      }

      if (quiet) {
        if (quietSinceMs_ == 0) quietSinceMs_ = nowMs;
        if (nowMs - quietSinceMs_ >= kQuietRearmMs) {
          state_ = DetectorState::WaitSecond;
        }
      } else {
        quietSinceMs_ = 0;
      }

      if (nowMs - firstClapMs_ > kSecondListenTimeoutMs) {
        state_ = DetectorState::Idle;
        quietSinceMs_ = 0;
      }
      break;
    }

    case DetectorState::WaitSecond: {
      const uint32_t dt = nowMs - firstClapMs_;

      if (secondCandidate && dt >= kSecondGapMinMs && dt <= kSecondGapMaxMs) {
        return acceptDoubleClap(nowMs);
      }

      if (dt > kSecondGapMaxMs && firstCandidate) {
        state_ = DetectorState::WaitRelease;
        firstClapMs_ = nowMs;
        lastCandidateMs_ = nowMs;
        quietSinceMs_ = 0;
      } else if (nowMs - firstClapMs_ > kSecondListenTimeoutMs) {
        state_ = DetectorState::Idle;
        quietSinceMs_ = 0;
      }
      break;
    }
  }

  return false;
}
