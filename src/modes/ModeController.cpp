#include "ModeController.h"

Mode ModeController::currentMode() const {
  return mode_;
}

Mode ModeController::advanceMode() {
  switch (mode_) {
    case Mode::Off:
      mode_ = Mode::Nightlight;
      break;
    case Mode::Nightlight:
      mode_ = Mode::Anthurium;
      break;
    case Mode::Anthurium:
    default:
      mode_ = Mode::Off;
      break;
  }
  return mode_;
}

void ModeController::setMode(Mode mode) {
  mode_ = mode;
}
