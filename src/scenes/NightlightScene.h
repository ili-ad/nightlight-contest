#pragma once

#include <stdint.h>

#include "../render/PixelOutput.h"

class NightlightScene {
 public:
  explicit NightlightScene(PixelOutput& output);

  void begin();
  void render(uint32_t nowMs);

 private:
  static uint8_t scaleChannel(uint8_t channel, float scale);

  PixelOutput& output_;
  bool initialized_ = false;

  // ARCH-064: tiny optional movement; default is a mostly steady output.
  // Disable by setting to false, or tune via amplitude/period constants in .cpp.
  static constexpr bool kEnableBreathe = true;
};
