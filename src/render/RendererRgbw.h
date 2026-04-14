#pragma once
#include <stdint.h>
#include "PixelBus.h"
#include "../mapping/RenderIntent.h"
#include "../effects/BootEffects.h"
#include "../effects/InterludeEffects.h"
#include "AnthuriumScene.h"

class RendererRgbw {
public:
  void begin(PixelBus& bus);
  void renderBoot(PixelBus& bus, const BootFrame& frame);
  void renderIntent(PixelBus& bus, const RenderIntent& intent);
  void renderIdle(PixelBus& bus, uint8_t level);
  void renderInterlude(PixelBus& bus, const InterludeFrame& frame);

private:
  AnthuriumScene mAnthuriumScene;
};
