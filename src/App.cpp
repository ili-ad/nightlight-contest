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
      nightlightScene_(pixelOutput) {}

void App::setup() {
  (void)layoutMap_;
  Serial.begin(115200);
  Serial.println("Nightlight v2 ARCH-064 ambient override boot");

  pixelOutput_.begin();
  stableSource_.begin();
  clapDetector_.begin();
  ambientLux_.begin();
  anthuriumScene_.begin();
  nightlightScene_.begin();

  renderOff();
}

void App::loop() {
  const uint32_t nowMs = millis();

  if (clapDetector_.update(nowMs)) {
    const Mode nextMode = modeController_.advanceMode();
    Serial.print("event=double_clap mode=");
    Serial.println(modeName(nextMode));
  }

  const Mode userMode = modeController_.currentMode();
  const AmbientLux::Band ambientBand = ambientLux_.readBand(nowMs);
  const Mode effectiveMode = resolveEffectiveMode(userMode, ambientBand);

  switch (effectiveMode) {
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

Mode App::resolveEffectiveMode(Mode userMode, AmbientLux::Band ambientBand) {
  // ARCH-064 ambient policy:
  // Off -> Off, Nightlight -> Nightlight,
  // Anthurium + Dark -> Nightlight, else user-selected mode.
  if (userMode == Mode::Off) {
    return Mode::Off;
  }

  if (userMode == Mode::Nightlight) {
    return Mode::Nightlight;
  }

  if (userMode == Mode::Anthurium && ambientBand == AmbientLux::Band::Dark) {
    return Mode::Nightlight;
  }

  return userMode;
}

void App::renderOff() {
  pixelOutput_.clear();
  pixelOutput_.show();
}

App& getApp() {
  return gApp;
}
