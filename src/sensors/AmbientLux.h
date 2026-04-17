#pragma once

#include <stdint.h>

class AmbientLux {
 public:
  enum class Band : uint8_t {
    Bright = 0,
    Dark = 1,
  };

  void begin(uint8_t analogPin);
  void begin();

  Band readBand(uint32_t nowMs);

 private:
  static float clamp01(float v);

  uint8_t analogPin_ = 0;
  bool initialized_ = false;
  bool smoothingReady_ = false;
  uint32_t lastSampleMs_ = 0;
  float smoothedNorm_ = 1.0f;
  Band band_ = Band::Bright;
};
