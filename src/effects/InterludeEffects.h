#pragma once
#include <stdint.h>

struct InterludeFrame {
  uint8_t spacing;
  uint8_t offset;
  uint8_t level;

  InterludeFrame() : spacing(4), offset(0), level(0) {}
};

class InterludeEffects {
public:
  static InterludeFrame marchingAnts(uint32_t elapsedMs);
};