#include "src/App.h"

class LayoutMap;
class PixelOutput;
App& getApp();

void setup() {
  getApp().setup();
}

void loop() {
  getApp().loop();
}
