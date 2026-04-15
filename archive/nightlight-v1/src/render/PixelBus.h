#pragma once
#include <stdint.h>
#include "../BuildConfig.h"

struct PixelData {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;

  PixelData() : r(0), g(0), b(0), w(0) {}
};

class PixelBus {
public:
  void begin();
  void clear();
  void show();

  uint16_t size() const;

  void setRgb(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
  void setRgbw(uint16_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

  const PixelData& pixelAt(uint16_t index) const;

private:
  PixelData mPixels[BuildConfig::kTotalPixels];
};