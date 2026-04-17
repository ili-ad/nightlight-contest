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

  // ARCH-064: keep this restrained and easy to disable/tune later.
  static constexpr bool kEnableBreathe = true;
};
