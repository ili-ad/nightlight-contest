#pragma once

#include <stdint.h>

struct AmbientReading {
  bool online = false;
  float luxRaw = 0.0f;
  float luxSmoothed = 0.0f;
};

class AmbientBh1750 {
public:
  void begin();
  AmbientReading read();

private:
  static constexpr float kLuxScale = 1.0f / 1.2f;
  static constexpr float kSmoothingAlpha = 0.25f;

  bool initializeSensor();
  bool readRawCount(uint16_t& rawCount);

  bool began_ = false;
  bool online_ = false;
  bool smoothingReady_ = false;
  float lastRawLux_ = 0.0f;
  float smoothedLux_ = 0.0f;
};
