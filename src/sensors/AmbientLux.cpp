#include "AmbientLux.h"

#include <Arduino.h>
#include <Wire.h>

namespace {
constexpr uint8_t kDefaultBh1750Address = 0x23;
constexpr uint32_t kSampleIntervalMs = 120;

constexpr uint8_t kBh1750PowerOn = 0x01;
constexpr uint8_t kBh1750Reset = 0x07;
constexpr uint8_t kBh1750ContinuousHighResMode = 0x10;

constexpr float kLuxScale = 1.0f / 1.2f;
constexpr float kEnterDarkLux = 18.0f;
constexpr float kExitDarkLux = 24.0f;
constexpr float kSmoothingAlpha = 0.20f;
}  // namespace

bool AmbientLux::initializeSensor() {
  Wire.beginTransmission(i2cAddress_);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.beginTransmission(i2cAddress_);
  Wire.write(kBh1750PowerOn);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.beginTransmission(i2cAddress_);
  Wire.write(kBh1750Reset);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  Wire.beginTransmission(i2cAddress_);
  Wire.write(kBh1750ContinuousHighResMode);
  return (Wire.endTransmission() == 0);
}

bool AmbientLux::readRawCount(uint16_t& rawCount) {
  const int bytesRead = Wire.requestFrom(static_cast<int>(i2cAddress_), 2);
  if (bytesRead != 2 || Wire.available() < 2) {
    return false;
  }

  const uint8_t msb = static_cast<uint8_t>(Wire.read());
  const uint8_t lsb = static_cast<uint8_t>(Wire.read());
  rawCount = static_cast<uint16_t>((msb << 8) | lsb);
  return true;
}

void AmbientLux::begin(uint8_t i2cAddress) {
  i2cAddress_ = i2cAddress;
  Wire.begin();

  initialized_ = true;
  sensorOnline_ = initializeSensor();
  smoothingReady_ = false;
  lastSampleMs_ = 0;
  smoothedLux_ = 0.0f;
  band_ = Band::Bright;
}

void AmbientLux::begin() {
  begin(kDefaultBh1750Address);
}

AmbientLux::Band AmbientLux::readBand(uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  if (lastSampleMs_ != 0 && (nowMs - lastSampleMs_) < kSampleIntervalMs) {
    return band_;
  }
  lastSampleMs_ = nowMs;

  uint16_t rawCount = 0;
  if (!readRawCount(rawCount)) {
    sensorOnline_ = initializeSensor();
    if (sensorOnline_) {
      if (!readRawCount(rawCount)) {
        sensorOnline_ = false;
      }
    }
  } else {
    sensorOnline_ = true;
  }

  if (!sensorOnline_) {
    return band_;
  }

  const float lux = max(0.0f, static_cast<float>(rawCount) * kLuxScale);

  if (!smoothingReady_) {
    smoothedLux_ = lux;
    smoothingReady_ = true;
  } else {
    smoothedLux_ += (lux - smoothedLux_) * kSmoothingAlpha;
  }

  if (band_ == Band::Dark) {
    if (smoothedLux_ >= kExitDarkLux) {
      band_ = Band::Bright;
    }
  } else if (smoothedLux_ <= kEnterDarkLux) {
    band_ = Band::Dark;
  }

  return band_;
}
