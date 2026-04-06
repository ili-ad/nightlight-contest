#pragma once
#include <stdint.h>
#include "PixelBus.h"
#include "../effects/BootEffects.h"
#include "../effects/InterludeEffects.h"

class RendererRgb {
public:
  void begin(PixelBus& bus);
  void renderBoot(PixelBus& bus, const BootFrame& frame);
  void renderIdle(PixelBus& bus, uint8_t level);
  void renderInterlude(PixelBus& bus, const InterludeFrame& frame);
};