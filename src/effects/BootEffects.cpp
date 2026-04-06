#include "BootEffects.h"
#include "../BuildConfig.h"
#include "../topology/PixelPaths.h"

BootFrame BootEffects::sample(uint32_t elapsedMs) {
  BootFrame frame;

  const uint16_t pathLength = PixelPaths::startupPathLength();
  if (pathLength == 0) {
    return frame;
  }

  uint32_t rawStep = 0;
  if (BuildConfig::kBootAnimationMs > 0) {
    rawStep = (elapsedMs * pathLength) / BuildConfig::kBootAnimationMs;
  }

  if (rawStep >= pathLength) {
    rawStep = pathLength - 1;
  }

  frame.pathStep = static_cast<uint16_t>(rawStep);
  frame.leadPixel = PixelPaths::startupPixelAt(frame.pathStep);
  frame.tailLength = 5;

  return frame;
}