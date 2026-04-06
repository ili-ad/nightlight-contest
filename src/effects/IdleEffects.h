#pragma once
#include <stdint.h>

struct IdleFrame {
  uint8_t level;

  IdleFrame() : level(0) {}
};

class IdleEffects {
public:
  static IdleFrame sample();
};