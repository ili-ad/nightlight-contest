#pragma once

#include <stdint.h>

enum class Mode : uint8_t {
  Anthurium = 0,
  Nightlight = 1,
  Off = 2,
};

class ModeController {
 public:
  Mode currentMode() const;
  Mode advanceMode();
  void setMode(Mode mode);

 private:
  Mode mode_ = Mode::Anthurium;
};
