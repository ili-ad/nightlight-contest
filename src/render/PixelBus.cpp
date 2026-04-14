#include "PixelBus.h"
#include <Adafruit_NeoPixel.h>
#include "../Pins.h"

namespace {
constexpr uint8_t brightnessFromLimit(float limit) {
  if (limit <= 0.0f) {
    return 0;
  }
  if (limit >= 1.0f) {
    return 255;
  }

  return static_cast<uint8_t>(limit * 255.0f + 0.5f);
}

Adafruit_NeoPixel gStrip(
    BuildConfig::kTotalPixels,
    Pins::kLedData,
    NEO_GRBW + NEO_KHZ800
);
}  // namespace

void PixelBus::begin() {
  clear();
  gStrip.begin();
  gStrip.setBrightness(brightnessFromLimit(BuildConfig::kGlobalBrightnessLimit));
  gStrip.clear();
  show();
}

void PixelBus::clear() {
  for (uint16_t i = 0; i < size(); ++i) {
    mPixels[i] = PixelData();
  }
}

void PixelBus::show() {
  for (uint16_t i = 0; i < size(); ++i) {
    const PixelData& pixel = mPixels[i];
    gStrip.setPixelColor(i, gStrip.Color(pixel.r, pixel.g, pixel.b, pixel.w));
  }

  gStrip.show();
}

uint16_t PixelBus::size() const {
  return BuildConfig::kTotalPixels;
}

void PixelBus::setRgb(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (index >= size()) {
    return;
  }

  mPixels[index].r = r;
  mPixels[index].g = g;
  mPixels[index].b = b;
  mPixels[index].w = 0;
}

void PixelBus::setRgbw(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (index >= size()) {
    return;
  }

  mPixels[index].r = r;
  mPixels[index].g = g;
  mPixels[index].b = b;
  mPixels[index].w = w;
}

const PixelData& PixelBus::pixelAt(uint16_t index) const {
  static PixelData empty;
  if (index >= size()) {
    return empty;
  }

  return mPixels[index];
}
