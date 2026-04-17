#pragma once

#include <stdint.h>

enum class Mode : uint8_t {
  Off = 0,
  Nightlight = 1,
  Anthurium = 2,
};

class ModeController {
 public:
  Mode currentMode() const;
  Mode advanceMode();

 private:
  Mode mode_ = Mode::Off;
};
