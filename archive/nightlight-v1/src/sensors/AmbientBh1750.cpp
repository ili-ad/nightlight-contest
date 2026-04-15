#include "AmbientBh1750.h"

#include <Arduino.h>
#include <Wire.h>

#include "../BuildConfig.h"

namespace {
  constexpr uint8_t kBh1750PowerOn = 0x01;
  constexpr uint8_t kBh1750Reset = 0x07;
  constexpr uint8_t kBh1750ContinuousHighResMode = 0x10;
}

bool AmbientBh1750::initializeSensor() {
  Wire.beginTransmission(BuildConfig::kBh1750I2cAddress);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.beginTransmission(BuildConfig::kBh1750I2cAddress);
  Wire.write(kBh1750PowerOn);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.beginTransmission(BuildConfig::kBh1750I2cAddress);
  Wire.write(kBh1750Reset);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.beginTransmission(BuildConfig::kBh1750I2cAddress);
  Wire.write(kBh1750ContinuousHighResMode);
  return (Wire.endTransmission() == 0);
}

bool AmbientBh1750::readRawCount(uint16_t& rawCount) {
  const int bytesRead = Wire.requestFrom(static_cast<int>(BuildConfig::kBh1750I2cAddress), 2);
  if (bytesRead != 2 || Wire.available() < 2) {
    return false;
  }

  const uint8_t msb = static_cast<uint8_t>(Wire.read());
  const uint8_t lsb = static_cast<uint8_t>(Wire.read());
  rawCount = static_cast<uint16_t>((msb << 8) | lsb);
  return true;
}

void AmbientBh1750::begin() {
  Wire.begin();
  began_ = true;
  online_ = initializeSensor();
  smoothingReady_ = false;
  lastRawLux_ = 0.0f;
  smoothedLux_ = 0.0f;
}

AmbientReading AmbientBh1750::read() {
  if (!began_) {
    begin();
  }

  uint16_t rawCount = 0;
  if (!readRawCount(rawCount)) {
    online_ = initializeSensor();
    if (online_) {
      if (!readRawCount(rawCount)) {
        online_ = false;
      }
    }
  } else {
    online_ = true;
  }

  if (!online_) {
    AmbientReading fallback;
    fallback.online = false;
    fallback.luxRaw = lastRawLux_;
    fallback.luxSmoothed = smoothedLux_;
    return fallback;
  }

  const float luxRaw = max(0.0f, static_cast<float>(rawCount) * kLuxScale);
  if (!smoothingReady_) {
    smoothedLux_ = luxRaw;
    smoothingReady_ = true;
  } else {
    smoothedLux_ += (luxRaw - smoothedLux_) * kSmoothingAlpha;
  }

  lastRawLux_ = luxRaw;

  AmbientReading reading;
  reading.online = true;
  reading.luxRaw = luxRaw;
  reading.luxSmoothed = smoothedLux_;
  return reading;
}
