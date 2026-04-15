#pragma once
#include <stdint.h>

class PixelPaths {
public:
  static uint16_t ringClockwiseLength();
  static uint16_t ringClockwiseAt(uint16_t step);

  static uint16_t leftStamenCenterToTipLength();
  static uint16_t leftStamenCenterToTipAt(uint16_t step);

  static uint16_t rightStamenTipToCenterLength();
  static uint16_t rightStamenTipToCenterAt(uint16_t step);

  static uint16_t startupPathLength();
  static uint16_t startupPixelAt(uint16_t step);
};