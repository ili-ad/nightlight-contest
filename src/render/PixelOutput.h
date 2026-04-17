#pragma once

#include <Adafruit_NeoPixel.h>
#include <stdint.h>

#include "../topology/LayoutMap.h"

class PixelOutput {
 public:
  explicit PixelOutput(const LayoutMap& layoutMap);

  void begin();
  void clear();

  bool setRingPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
  bool setLeftStamenPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
  bool setRightStamenPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);

  void show();

 private:
  bool setMappedPixel(uint16_t physicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

  const LayoutMap& layoutMap_;
  Adafruit_NeoPixel strip_;
};
