#pragma once

#include "model/StableTrack.h"
#include "scenes/AnthuriumScene.h"
#include "sensors/C4001StableSource.h"

class LayoutMap;
class PixelOutput;

class App {
 public:
  App(LayoutMap& layoutMap, PixelOutput& pixelOutput);

  void setup();
  void loop();

 private:
  LayoutMap& layoutMap_;
  PixelOutput& pixelOutput_;
  C4001StableSource stableSource_;
  AnthuriumScene anthuriumScene_;
};
