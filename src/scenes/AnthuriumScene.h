#pragma once

#include <stdint.h>

#include "../model/StableTrack.h"
#include "../render/PixelOutput.h"

class AnthuriumScene {
 public:
  explicit AnthuriumScene(PixelOutput& output);

  void begin();
  void render(const StableTrack& track, uint32_t nowMs);

 private:
  struct ColorF {
    float r;
    float g;
    float b;
    float w;
  };

  static constexpr uint16_t kFrontRingPixels = 44;
  static constexpr uint16_t kRearRingPixels = 44;
  static constexpr uint16_t kRightJPixels = 12;
  static constexpr uint16_t kLeftJPixels = 12;

  // Compatibility model for the known-good bench sketch:
  // bench/anthurium_lite_smoke_v3/anthurium_lite_smoke_v3.ino
  // The old sketch wrote a 77-pixel virtual chain directly. On the final
  // hardware, physical pixels 24..67 are the front ring, so this stage renders
  // virtual pixels 24..67 onto the semantic FrontRing. This intentionally
  // preserves the visual behavior the bench sketch produced on the actual piece.
  static constexpr uint16_t kVirtualRingPixels = 45;
  static constexpr uint16_t kVirtualLeftStamenPixels = 16;
  static constexpr uint16_t kVirtualRightStamenPixels = 16;
  static constexpr uint16_t kVirtualFrontRingPhysicalStart = 24;

  void updateMotionSignal(const StableTrack& track, float dtSec);
  void updateSmoothedScene(const StableTrack& track, float dtSec);
  void updateTorus(float dtSec);
  void renderFrontRingCompat(float dtSec);
  void clearInactiveSpans();

  ColorF renderVirtualRingPixel(uint16_t ringPixel, float dtSec);
  ColorF renderVirtualStamenPixel(uint16_t stamenPixel, uint16_t stamenCount,
                                  float* brightnessState, float dtSec);

  ColorF currentSceneColor(float brightnessScale) const;
  float sampleTorusField(uint16_t ringPixel) const;
  float sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) const;

  static ColorF makeColor(float r = 0.0f, float g = 0.0f, float b = 0.0f, float w = 0.0f);
  static ColorF scaleColor(const ColorF& color, float scale);
  static ColorF hsvColor(float hue, float sat, float val, float white);
  static float normalizeNearness(float rangeM);
  static float emaAlphaApprox(float dtSec, float tauSec);
  static float decayApprox(float dtSec, float clearSec);
  static float applyDeadband(float previous, float target, float threshold);
  static float applyBrightnessSlew(float previous, float target, float dtSec);
  static float polynomialKernel(float distance, float width);
  static float clamp01(float value);
  static float clampSigned(float value, float lo, float hi);
  static float lerp(float a, float b, float t);
  static float absf(float value);
  static float maxf(float a, float b);
  static uint8_t toByte(float value);

  PixelOutput& output_;
  bool initialized_ = false;
  uint32_t lastNowMs_ = 0;

  bool hadRangeSample_ = false;
  float prevAcceptedRangeM_ = 0.0f;
  float motionSignal_ = 0.0f;
  float smoothedCharge_ = 0.0f;
  float stableCharge_ = 0.0f;
  float smoothedIngressLevel_ = 0.0f;
  float ingressConveyorPhase_ = 0.0f;
  float displayHue_ = 0.33f;
  float displaySat_ = 0.24f;
  float displayRgbLevel_ = 0.08f;
  float displayWhite_ = 0.015f;

  float torusCharge_[kVirtualRingPixels] = {0.0f};
  ColorF torusColor_[kVirtualRingPixels] = {{0.0f, 0.0f, 0.0f, 0.0f}};
  float ringBrightness_[kVirtualRingPixels] = {0.0f};
  float leftBrightness_[kVirtualLeftStamenPixels] = {0.0f};
  float rightBrightness_[kVirtualRightStamenPixels] = {0.0f};
};
