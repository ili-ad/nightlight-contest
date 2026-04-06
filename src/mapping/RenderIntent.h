#pragma once
#include <stdint.h>
#include "../Types.h"

struct RenderIntent {
  // Utility light
  float whiteLevel = 0.0f;

  // Expressive color
  float hue = 0.0f;         // normalized 0..1
  float saturation = 0.0f;  // normalized 0..1
  float rgbLevel = 0.0f;    // normalized 0..1

  // Motion / animation
  float animationRate = 0.0f;
  float phase = 0.0f;

  // Geometry / emphasis
  SegmentId emphasizedSegment = SegmentId::WholeObject;

  // Effect selection
  uint8_t effectId = 0;
  bool clearBeforeRender = true;
};