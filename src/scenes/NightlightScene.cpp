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

  const uint8_t r = scaleChannel(profile.baseR, scale);
  const uint8_t g = scaleChannel(profile.baseG, scale);
  const uint8_t b = scaleChannel(profile.baseB, scale);
  const uint8_t w = scaleChannel(profile.baseW, scale);

  for (uint16_t i = 0; i < Profiles::kRingPixels; ++i) {
    output_.setRingPixel(i, r, g, b, w);
  }
  for (uint16_t i = 0; i < Profiles::kLeftStamenPixels; ++i) {
    output_.setLeftStamenPixel(i, r, g, b, w);
  }
  for (uint16_t i = 0; i < Profiles::kRightStamenPixels; ++i) {
    output_.setRightStamenPixel(i, r, g, b, w);
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
