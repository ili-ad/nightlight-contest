#include "App.h"

#include <Arduino.h>

#include "render/PixelOutput.h"
#include "topology/LayoutMap.h"

namespace {
LayoutMap gLayoutMap;
PixelOutput gPixelOutput(gLayoutMap);
}  // namespace

void App::setup() {
  Serial.begin(115200);
  Serial.println("Nightlight v2 scaffold boot (ARCH-061)");

  gPixelOutput.begin();
  gPixelOutput.setRingPixel(0, 0, 0, 0, 16);
  gPixelOutput.show();
}

void App::loop() {
  delay(1000);
}
