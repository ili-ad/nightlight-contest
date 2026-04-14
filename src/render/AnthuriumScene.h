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
  struct IngressPulse {
    bool active = false;
    float progress = 0.0f;
    float amplitude = 0.0f;
  };

  static constexpr uint8_t kMaxIngressPulses = 32;

  bool mInitialized = false;
  uint32_t mLastNowMs = 0;
  uint32_t mIngressEmitAccumulatorMs = 0;
  IngressPulse mIngressPulses[kMaxIngressPulses];

  float mTorusCharge[BuildConfig::kRingPixels] = {0.0f};

  void updateDynamics(const RenderIntent& intent);
  void writeFrame(PixelBus& bus, const RenderIntent& intent, bool useWhite);

  float sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) const;
  float sampleTorusField(uint16_t ringPixel, uint16_t ringCount) const;

  void emitIngressPulse(float pulseAmplitude);
};
