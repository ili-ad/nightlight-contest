#include "NightlightScene.h"

#include <math.h>

#include "../config/Profiles.h"

namespace {
float clamp01(float v) {
  if (v < 0.0f) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}
}  // namespace

NightlightScene::NightlightScene(PixelOutput& output) : output_(output) {}

void NightlightScene::begin() {
  initialized_ = true;
}

void NightlightScene::render(uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  const auto& profile = Profiles::nightlight();

  float scale = 1.0f;
  if (kEnableBreathe) {
    const float phase = (static_cast<float>(nowMs) / profile.breathePeriodMs) * 2.0f * PI;
    scale += sinf(phase) * profile.breatheAmplitude;
  }
  scale = clamp01(scale);

  const uint8_t jR = scaleChannel(profile.baseR, scale * profile.jScale);
  const uint8_t jG = scaleChannel(profile.baseG, scale * profile.jScale);
  const uint8_t jB = scaleChannel(profile.baseB, scale * profile.jScale);
  const uint8_t jW = scaleChannel(profile.baseW, scale * profile.jScale);

  const uint8_t frontRingR = scaleChannel(profile.baseR, scale * profile.frontRingScale);
  const uint8_t frontRingG = scaleChannel(profile.baseG, scale * profile.frontRingScale);
  const uint8_t frontRingB = scaleChannel(profile.baseB, scale * profile.frontRingScale);
  const uint8_t frontRingW = scaleChannel(profile.baseW, scale * profile.frontRingScale);

  const uint8_t rearRingR = scaleChannel(profile.baseR, scale * profile.rearRingScale);
  const uint8_t rearRingG = scaleChannel(profile.baseG, scale * profile.rearRingScale);
  const uint8_t rearRingB = scaleChannel(profile.baseB, scale * profile.rearRingScale);
  const uint8_t rearRingW = scaleChannel(profile.baseW, scale * profile.rearRingScale);

  for (uint16_t i = 0; i < Profiles::kRightJPixels; ++i) {
    output_.setRightJPixel(i, jR, jG, jB, jW);
  }
  for (uint16_t i = 0; i < Profiles::kLeftJPixels; ++i) {
    output_.setLeftJPixel(i, jR, jG, jB, jW);
  }
  for (uint16_t i = 0; i < Profiles::kFrontRingPixels; ++i) {
    output_.setFrontRingPixel(i, frontRingR, frontRingG, frontRingB, frontRingW);
  }
  for (uint16_t i = 0; i < Profiles::kRearRingPixels; ++i) {
    output_.setRearRingPixel(i, rearRingR, rearRingG, rearRingB, rearRingW);
  }

  output_.show();
}

uint8_t NightlightScene::scaleChannel(uint8_t channel, float scale) {
  const float v = static_cast<float>(channel) * scale;
  if (v <= 0.0f) {
    return 0;
  }
  if (v >= 255.0f) {
    return 255;
  }
  return static_cast<uint8_t>(v);
}
