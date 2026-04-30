#pragma once

#include <Adafruit_NeoPixel.h>
#include <stdint.h>

#include "../topology/LayoutMap.h"

class PixelOutput {
 public:
  explicit PixelOutput(const LayoutMap& layoutMap);

  void begin();
  void clear();

  bool setRightJPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
  bool setLeftJPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
  bool setFrontRingPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);
  bool setRearRingPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0);

  void show();

 private:
  uint8_t limitChannel(uint8_t value) const;
  bool setMappedPixel(uint16_t physicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

  const LayoutMap& layoutMap_;
  Adafruit_NeoPixel strip_;
};
