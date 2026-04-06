#include "PixelBus.h"

void PixelBus::begin() {
  clear();
}

void PixelBus::clear() {
  for (uint16_t i = 0; i < size(); ++i) {
    mPixels[i] = PixelData();
  }
}

void PixelBus::show() {
  // Hardware LED output comes later.
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