#include "App.h"

#include <Arduino.h>

#include "render/PixelOutput.h"
#include "topology/LayoutMap.h"

namespace {
LayoutMap gLayoutMap;
PixelOutput gPixelOutput(gLayoutMap);
App gApp(gLayoutMap, gPixelOutput);
}  // namespace

App::App(LayoutMap& layoutMap, PixelOutput& pixelOutput)
    : layoutMap_(layoutMap),
      pixelOutput_(pixelOutput),
      anthuriumScene_(pixelOutput),
      nightlightScene_(pixelOutput),
      startupScene_(pixelOutput) {}

void App::setup() {
  (void)layoutMap_;
  Serial.begin(115200);
  Serial.println("Nightlight v2 production boot");

  pixelOutput_.begin();
  stableSource_.begin();
  clapDetector_.begin();
  anthuriumScene_.begin();
  nightlightScene_.begin();
  startupScene_.begin(millis());

  modeController_.setMode(Mode::Anthurium);
}

void App::loop() {
  const uint32_t nowMs = millis();

  if (phase_ == AppPhase::Startup) {
    if (startupScene_.render(nowMs)) {
      modeController_.setMode(Mode::Anthurium);
      phase_ = AppPhase::Running;
    }
    delay(16);
    return;
  }

  if (clapDetector_.update(nowMs)) {
    const Mode nextMode = modeController_.advanceMode();
    Serial.print("event=double_clap mode=");
    Serial.println(modeName(nextMode));
  }

  const Mode userMode = modeController_.currentMode();

  switch (userMode) {
    case Mode::Off:
      renderOff();
      break;
    case Mode::Nightlight:
      nightlightScene_.render(nowMs);
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

App& getApp() {
  return gApp;
}
