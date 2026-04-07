#include "RendererRgb.h"
#include "../topology/PixelPaths.h"

namespace {
  float clamp01(float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 1.0f) {
      return 1.0f;
    }
    return value;
  }

  uint8_t toByte(float normalized) {
    const float clamped = clamp01(normalized);
    return static_cast<uint8_t>(clamped * 255.0f);
  }

  void hsvToRgb(float hue, float saturation, float value, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float s = clamp01(saturation);
    const float v = clamp01(value);
    float h = hue;

    while (h < 0.0f) {
      h += 1.0f;
    }
    while (h >= 1.0f) {
      h -= 1.0f;
    }

    const float scaled = h * 6.0f;
    const int sector = static_cast<int>(scaled);
    const float fraction = scaled - sector;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - (s * fraction));
    const float t = v * (1.0f - (s * (1.0f - fraction)));

    float rf = v;
    float gf = t;
    float bf = p;

    switch (sector % 6) {
      case 0: rf = v; gf = t; bf = p; break;
      case 1: rf = q; gf = v; bf = p; break;
      case 2: rf = p; gf = v; bf = t; break;
      case 3: rf = p; gf = q; bf = v; break;
      case 4: rf = t; gf = p; bf = v; break;
      default: rf = v; gf = p; bf = q; break;
    }

    r = toByte(rf);
    g = toByte(gf);
    b = toByte(bf);
  }
}

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

void RendererRgb::renderIntent(PixelBus& bus, const RenderIntent& intent) {
  if (intent.clearBeforeRender) {
    bus.clear();
  }

  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  hsvToRgb(intent.hue, intent.saturation, intent.rgbLevel, r, g, b);

  const uint8_t whiteLift = static_cast<uint8_t>(toByte(intent.whiteLevel) / 3);
  r = static_cast<uint8_t>(r + ((255 - r) < whiteLift ? (255 - r) : whiteLift));
  g = static_cast<uint8_t>(g + ((255 - g) < whiteLift ? (255 - g) : whiteLift));
  b = static_cast<uint8_t>(b + ((255 - b) < whiteLift ? (255 - b) : whiteLift));

  for (uint16_t i = 0; i < bus.size(); ++i) {
    bus.setRgb(i, r, g, b);
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
