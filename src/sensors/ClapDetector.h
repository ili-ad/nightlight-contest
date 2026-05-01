#pragma once

#include <stdint.h>

class ClapDetector {
 public:
  static constexpr uint32_t kSecondListenTimeoutMs = 2300;

  void begin(uint8_t micAnalogPin);
  void begin();

  // Samples the mic at an internal 1 ms cadence. Returns true exactly once
  // when a valid double-clap is recognized.
  bool update(uint32_t nowMs);

  bool isWaitingForSecondClap() const;
  uint32_t firstClapMs() const;

 private:
  enum class DetectorState : uint8_t {
    Idle,
    WaitRelease,
    WaitSecond,
  };

  static float maxf(float a, float b);

  void seedBaseline();
  void pushWindow(uint16_t raw);
  uint16_t currentP2P() const;
  bool p2pCandidateWithDev(float dev, uint16_t p2p, float p2pThreshold, float p2pOnlyDevMin) const;
  void learnQuietSignal(uint16_t raw, float dev, uint16_t p2p, bool quiet, uint32_t nowMs);
  bool processSample(uint16_t raw, uint32_t nowMs);
  bool acceptDoubleClap(uint32_t nowMs);

  uint8_t micAnalogPin_ = 0;
  bool initialized_ = false;
  DetectorState state_ = DetectorState::Idle;

  float baseline_ = 0.0f;
  float devFloor_ = 1.0f;
  float p2pFloor_ = 6.0f;

  static constexpr uint8_t kP2PWindowSamples = 12;
  uint16_t window_[kP2PWindowSamples] = {0};
  uint8_t windowHead_ = 0;
  uint8_t windowCount_ = 0;

  uint32_t lastSampleUs_ = 0;
  uint32_t lastCandidateMs_ = 0;
  uint32_t firstClapMs_ = 0;
  uint32_t quietSinceMs_ = 0;
  uint32_t lastToggleMs_ = 0;
};
