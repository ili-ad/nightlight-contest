#pragma once

#include <stdint.h>

#include "../model/StableTrack.h"
#include "../render/PixelOutput.h"

class AnthuriumScene {
 public:
  explicit AnthuriumScene(PixelOutput& output);

  void begin();
  void render(const StableTrack& track, uint32_t nowMs);

 private:
  static float clamp01(float v);
  static uint8_t toByte(float v);
  static float polynomialKernel(float distance, float width);

  void updateTorus(const StableTrack& track, float dtSec);
  void phaseColor(const StableTrack& track, float& r, float& g, float& b, float& w) const;
  float sampleIngress(uint16_t pixel, uint16_t count, const StableTrack& track) const;

  PixelOutput& output_;
  bool initialized_ = false;
  uint32_t lastNowMs_ = 0;
  float ingressPhase_ = 0.0f;

  static constexpr uint16_t kRingPixels = 45;
  static constexpr uint16_t kStamenPixels = 16;

  float torus_[kRingPixels] = {0.0f};
  float ringLuma_[kRingPixels] = {0.0f};
  float leftLuma_[kStamenPixels] = {0.0f};
  float rightLuma_[kStamenPixels] = {0.0f};
};
