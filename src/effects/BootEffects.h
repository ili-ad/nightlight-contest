#pragma once
#include <stdint.h>

struct BootFrame {
  uint16_t pathStep;
  uint16_t leadPixel;
  uint8_t tailLength;

  BootFrame() : pathStep(0), leadPixel(0), tailLength(0) {}
};

class BootEffects {
public:
  static BootFrame sample(uint32_t elapsedMs);
};