#pragma once

#include <stdint.h>

#include "../render/PixelOutput.h"

class StartupScene {
 public:
  explicit StartupScene(PixelOutput& output);

  void begin(uint32_t nowMs);
  bool render(uint32_t nowMs);

 private:
  PixelOutput& output_;
  bool initialized_ = false;
  uint32_t startMs_ = 0;
};
