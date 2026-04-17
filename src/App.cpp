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
    : layoutMap_(layoutMap), pixelOutput_(pixelOutput), anthuriumScene_(pixelOutput) {}

void App::setup() {
  (void)layoutMap_;
  Serial.begin(115200);
  Serial.println("Nightlight v2 Anthurium boot (ARCH-062)");

  pixelOutput_.begin();
  stableSource_.begin();
  anthuriumScene_.begin();
}

void App::loop() {
  const uint32_t nowMs = millis();
  const StableTrack track = stableSource_.read(nowMs);
  anthuriumScene_.render(track, nowMs);
  delay(16);
}

App& getApp() {
  return gApp;
}
