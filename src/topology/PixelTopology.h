#pragma once
#include <stdint.h>
#include "../Types.h"

class PixelTopology {
public:
  static SegmentRange ring();
  static SegmentRange leftStamen();
  static SegmentRange rightStamen();

  static uint16_t totalPixels();
  static bool isValidPixel(uint16_t pixelIndex);

  static bool isRing(uint16_t pixelIndex);
  static bool isLeftStamen(uint16_t pixelIndex);
  static bool isRightStamen(uint16_t pixelIndex);
};