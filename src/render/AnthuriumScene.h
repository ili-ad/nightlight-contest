#pragma once

#include <stdint.h>
#include "PixelBus.h"
#include "../mapping/RenderIntent.h"
#include "../BuildConfig.h"

class AnthuriumScene {
public:
  void reset();
  void renderRgb(PixelBus& bus, const RenderIntent& intent);
  void renderRgbw(PixelBus& bus, const RenderIntent& intent);

private:
  bool mInitialized = false;
  uint32_t mLastNowMs = 0;
  float mLastDtSec = 0.016f;
  float mSmoothedCharge = 0.0f;
  float mSmoothedIngressLevel = 0.0f;
  float mIngressConveyorPhase = 0.0f;

  float mTorusCharge[BuildConfig::kRingPixels] = {0.0f};
  float mRingBrightness[BuildConfig::kRingPixels] = {0.0f};
  float mLeftBrightness[BuildConfig::kLeftStamenPixels] = {0.0f};
  float mRightBrightness[BuildConfig::kRightStamenPixels] = {0.0f};

  void updateDynamics(const RenderIntent& intent);
  void writeFrame(PixelBus& bus, const RenderIntent& intent, bool useWhite);

  float sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) const;
  float sampleTorusField(uint16_t ringPixel, uint16_t ringCount) const;
  float applyBrightnessSlew(float previous, float target, float dtSec) const;
};
