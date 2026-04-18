#pragma once

#include <stdint.h>

class AmbientLux {
 public:
  enum class Band : uint8_t {
    Bright = 0,
    Dark = 1,
  };

  // Optional custom BH1750 I2C address. Bench profile default is 0x23.
  void begin(uint8_t i2cAddress);
  void begin();

  Band readBand(uint32_t nowMs);

 private:
  bool initializeSensor();
  bool readRawCount(uint16_t& rawCount);

  uint8_t i2cAddress_ = 0;
  bool initialized_ = false;
  bool sensorOnline_ = false;
  bool smoothingReady_ = false;
  uint32_t lastSampleMs_ = 0;
  float smoothedLux_ = 0.0f;
  Band band_ = Band::Bright;
};
