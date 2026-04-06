#include "RendererRgbw.h"
#include "../topology/PixelPaths.h"

void RendererRgbw::begin(PixelBus& bus) {
  bus.clear();
}

void RendererRgbw::renderBoot(PixelBus& bus, const BootFrame& frame) {
  bus.clear();

  for (uint8_t tail = 0; tail < frame.tailLength; ++tail) {
    if (frame.pathStep < tail) {
      break;
    }

    const uint16_t step = frame.pathStep - tail;
    const uint16_t pixel = PixelPaths::startupPixelAt(step);

    int white = 180 - (tail * 30);
    if (white < 0) {
      white = 0;
    }

    int accent = 40 - (tail * 6);
    if (accent < 0) {
      accent = 0;
    }

    bus.setRgbw(pixel,
                static_cast<uint8_t>(accent),
                0,
                static_cast<uint8_t>(accent),
                static_cast<uint8_t>(white));
  }
}

void RendererRgbw::renderIdle(PixelBus& bus, uint8_t level) {
  for (uint16_t i = 0; i < bus.size(); ++i) {
    bus.setRgbw(i, 0, 0, 0, level);
  }
}

void RendererRgbw::renderInterlude(PixelBus& bus, const InterludeFrame& frame) {
  bus.clear();

  for (uint16_t i = 0; i < bus.size(); ++i) {
    if ((i % frame.spacing) == frame.offset) {
      bus.setRgbw(i, 0, frame.level / 2, frame.level, frame.level / 3);
    }
  }
}