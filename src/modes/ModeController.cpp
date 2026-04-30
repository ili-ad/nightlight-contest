#include "ModeController.h"

Mode ModeController::currentMode() const {
  return mode_;
}

Mode ModeController::advanceMode() {
  switch (mode_) {
    case Mode::Anthurium:
      mode_ = Mode::Nightlight;
      break;
    case Mode::Nightlight:
      mode_ = Mode::Off;
      break;
    case Mode::Off:
    default:
      mode_ = Mode::Anthurium;
      break;
  }
  return mode_;
}

void ModeController::setMode(Mode mode) {
  mode_ = mode;
}
