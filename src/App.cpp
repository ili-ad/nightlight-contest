#include "App.h"

#include <Arduino.h>

#include "config/Profiles.h"
#include "render/PixelOutput.h"
#include "topology/LayoutMap.h"

namespace {
LayoutMap gLayoutMap;
PixelOutput gPixelOutput(gLayoutMap);
App gApp(gLayoutMap, gPixelOutput);
}  // namespace

App::App(LayoutMap& layoutMap, PixelOutput& pixelOutput)
    : layoutMap_(layoutMap), pixelOutput_(pixelOutput), anthuriumScene_(pixelOutput) {}

void App::setup() {
  (void)layoutMap_;
  Serial.begin(115200);
  Serial.println("Nightlight v2 mode-control boot (ARCH-063)");

  pixelOutput_.begin();
  stableSource_.begin();
  clapDetector_.begin();
  anthuriumScene_.begin();

  renderOff();
}

void App::loop() {
  const uint32_t nowMs = millis();

  if (clapDetector_.update(nowMs)) {
    const Mode nextMode = modeController_.advanceMode();
    Serial.print("event=double_clap mode=");
    Serial.println(modeName(nextMode));
  }

  switch (modeController_.currentMode()) {
    case Mode::Off:
      renderOff();
      break;
    case Mode::Nightlight:
      renderNightlightPlaceholder();
      break;
    case Mode::Anthurium: {
      const StableTrack track = stableSource_.read(nowMs);
      anthuriumScene_.render(track, nowMs);
      break;
    }
    default:
      renderOff();
      break;
  }

  delay(16);
}

const char* App::modeName(Mode mode) {
  switch (mode) {
    case Mode::Off:
      return "Off";
    case Mode::Nightlight:
      return "Nightlight";
    case Mode::Anthurium:
      return "Anthurium";
    default:
      return "Unknown";
  }
}

void App::renderOff() {
  pixelOutput_.clear();
  pixelOutput_.show();
}

void App::renderNightlightPlaceholder() {
  constexpr uint8_t kR = 16;
  constexpr uint8_t kG = 7;
  constexpr uint8_t kB = 1;
  constexpr uint8_t kW = 5;

  for (uint16_t i = 0; i < Profiles::kRingPixels; ++i) {
    pixelOutput_.setRingPixel(i, kR, kG, kB, kW);
  }
  for (uint16_t i = 0; i < Profiles::kLeftStamenPixels; ++i) {
    pixelOutput_.setLeftStamenPixel(i, kR, kG, kB, kW);
  }
  for (uint16_t i = 0; i < Profiles::kRightStamenPixels; ++i) {
    pixelOutput_.setRightStamenPixel(i, kR, kG, kB, kW);
  }

  pixelOutput_.show();
}

App& getApp() {
  return gApp;
}
