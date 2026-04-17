#pragma once

#include <stdint.h>

class ClapDetector {
 public:
  void begin(uint8_t micAnalogPin);
  void begin();

  // Returns true exactly once when a valid double-clap is recognized.
  bool update(uint32_t nowMs);

 private:
  uint8_t micAnalogPin_ = 0;
  bool initialized_ = false;

  float baseline_ = 0.0f;
  float devEma_ = 0.0f;
  float env_ = 0.0f;
  float noiseFloor_ = 1.0f;

  bool armed_ = false;
  bool readyForSecond_ = false;
  uint32_t firstClapMs_ = 0;
  uint32_t lastTriggerMs_ = 0;
  uint32_t armQuietSinceMs_ = 0;
  uint32_t modeAdvanceLockoutUntilMs_ = 0;
};
