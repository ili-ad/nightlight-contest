#include "StartupScene.h"

#include "../config/Profiles.h"

StartupScene::StartupScene(PixelOutput& output) : output_(output) {}

void StartupScene::begin(uint32_t nowMs) {
  output_.clear();
  output_.show();
  initialized_ = true;
  startMs_ = nowMs;
}

bool StartupScene::render(uint32_t nowMs) {
  if (!initialized_) {
    begin(nowMs);
  }

  const auto& profile = Profiles::startup();
  const uint16_t pixelCount = output_.physicalPixelCount();
  const uint32_t elapsedMs = nowMs - startMs_;
  const uint32_t fillDurationMs = static_cast<uint32_t>(profile.stepMs) * pixelCount;

  uint16_t litCount = pixelCount;
  if (profile.stepMs > 0 && elapsedMs < fillDurationMs) {
    litCount = static_cast<uint16_t>(elapsedMs / profile.stepMs) + 1;
  }

  if (litCount > pixelCount) {
    litCount = pixelCount;
  }

  output_.clear();
  for (uint16_t i = 0; i < litCount; ++i) {
    output_.setPhysicalPixel(i, profile.r, profile.g, profile.b, profile.w);
  }
  output_.show();

  return elapsedMs >= fillDurationMs + profile.holdMs;
}
