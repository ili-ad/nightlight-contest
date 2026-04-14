#pragma once

#include "../mapping/RenderIntent.h"

class RenderIntentSmoother {
public:
  void reset();
  RenderIntent smooth(const RenderIntent& target);

private:
  bool mInitialized = false;
  RenderIntent mCurrent;
};
