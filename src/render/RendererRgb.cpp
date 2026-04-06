#include "RendererRgb.h"
#include "../topology/PixelPaths.h"

void RendererRgb::begin(PixelBus& bus) {
  bus.clear();
}

void RendererRgb::renderBoot(PixelBus& bus, const BootFrame& frame) {
  bus.clear();

  for (uint8_t tail = 0; tail < frame.tailLength; ++tail) {
    if (frame.pathStep < tail) {
      break;
    }

    const uint16_t step = frame.pathStep - tail;
    const uint16_t pixel = PixelPaths::startupPixelAt(step);

    int value = 200 - (tail * 35);
    if (value < 0) {
      value = 0;
    }

    bus.setRgb(pixel,
               static_cast<uint8_t>(value),
               static_cast<uint8_t>(value / 2),
               0);
  }
}

void RendererRgb::renderIdle(PixelBus& bus, uint8_t level) {
  for (uint16_t i = 0; i < bus.size(); ++i) {
    bus.setRgb(i, level, static_cast<uint8_t>(level / 2), 0);
  }
}

void RendererRgb::renderInterlude(PixelBus& bus, const InterludeFrame& frame) {
  bus.clear();

  for (uint16_t i = 0; i < bus.size(); ++i) {
    if ((i % frame.spacing) == frame.offset) {
      bus.setRgb(i, frame.level, frame.level, frame.level);
    }
  }
}