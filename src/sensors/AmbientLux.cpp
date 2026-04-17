#include "AmbientLux.h"

#include <Arduino.h>

namespace {
constexpr uint8_t kDefaultAmbientPin = A1;
constexpr uint32_t kSampleIntervalMs = 100;

// Normalized thresholds with hysteresis.
constexpr float kEnterDarkNorm = 0.18f;
constexpr float kExitDarkNorm = 0.24f;
constexpr float kSmoothingAlpha = 0.16f;
}  // namespace

void AmbientLux::begin(uint8_t analogPin) {
  analogPin_ = analogPin;
  pinMode(analogPin_, INPUT);

  initialized_ = true;
  smoothingReady_ = false;
  lastSampleMs_ = 0;
  smoothedNorm_ = 1.0f;
  band_ = Band::Bright;
}

void AmbientLux::begin() {
  begin(kDefaultAmbientPin);
}

AmbientLux::Band AmbientLux::readBand(uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  if (lastSampleMs_ != 0 && (nowMs - lastSampleMs_) < kSampleIntervalMs) {
    return band_;
  }
  lastSampleMs_ = nowMs;

  const int raw = analogRead(analogPin_);
  const float norm = clamp01(static_cast<float>(raw) / 1023.0f);

  if (!smoothingReady_) {
    smoothedNorm_ = norm;
    smoothingReady_ = true;
  } else {
    smoothedNorm_ += (norm - smoothedNorm_) * kSmoothingAlpha;
  }

  if (band_ == Band::Dark) {
    if (smoothedNorm_ >= kExitDarkNorm) {
      band_ = Band::Bright;
    }
  } else if (smoothedNorm_ <= kEnterDarkNorm) {
    band_ = Band::Dark;
  }

  return band_;
}

float AmbientLux::clamp01(float v) {
  if (v < 0.0f) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}
