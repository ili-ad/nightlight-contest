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

  // Native production model: FrontRing is rendered directly over 44 pixels.
  void updateMotionSignal(const StableTrack& track, float dtSec);
  void updateSmoothedScene(const StableTrack& track, float dtSec);
  void renderFrontRingCompat(float dtSec, uint32_t nowMs);
  void renderJSpans(float dtSec);
  void updateRearDriveColor(float dtSec);
  void updateRearRingReservoir(float dtSec);
  void renderRearRing(float dtSec, uint32_t nowMs);

  ColorF renderNativeFrontPixel(uint16_t logicalPixel, float dtSec);
  ColorF renderJPixel(uint16_t logicalPixel, uint16_t pixelCount, float* brightnessState,
                      bool tipAtHighIndex, float phaseOffset, float dtSec);

  ColorF currentSceneColor(float brightnessScale) const;
  float sampleNativeFrontIngress(uint16_t logicalPixel) const;
  float sampleJIngress(uint16_t logicalPixel, uint16_t pixelCount,
                       bool tipAtHighIndex, float phaseOffset) const;

  static ColorF makeColor(float r = 0.0f, float g = 0.0f, float b = 0.0f, float w = 0.0f);
  static ColorF scaleColor(const ColorF& color, float scale);
  static ColorF lerpColor(const ColorF& a, const ColorF& b, float t);
  static ColorF hsvColor(float hue, float sat, float val, float white);
  static float normalizeNearness(float rangeM);
  static float emaAlphaApprox(float dtSec, float tauSec);
  static float decayApprox(float dtSec, float clearSec);
  static float applyDeadband(float previous, float target, float threshold);
  static float applyBrightnessSlew(float previous, float target, float dtSec);
  static float polynomialKernel(float distance, float width);
  static float circularDistance(float a, float b, float count);
  static float clamp01(float value);
  static float clampSigned(float value, float lo, float hi);
  static float lerp(float a, float b, float t);
  static float absf(float value);
  static float maxf(float a, float b);
  static float minf(float a, float b);
  static float colorLuma(const ColorF& color);
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

  float nativeFrontBrightness_[kFrontRingPixels] = {0.0f};
  float physicalLeftBrightness_[kLeftJPixels] = {0.0f};
  float physicalRightBrightness_[kRightJPixels] = {0.0f};

  // Phase 4: V4-style front-ring reservoir, rendered onto the physical
  // RearRing as a calmer wall-wash layer. It is deliberately separate from
  // the proven native FrontRing and from the J/spadix brightness state.
  float rearReservoirCharge_[kRearRingPixels] = {0.0f};
  ColorF rearReservoirColor_[kRearRingPixels] = {{0.0f, 0.0f, 0.0f, 0.0f}};
  float rearReservoirBrightness_[kRearRingPixels] = {0.0f};
  ColorF rearDriveColor_ = {0.0f, 0.0f, 0.0f, 0.0f};
  float rearDriveWhite_ = 0.0f;
  float rearIdleSafetyNetLevel_ = 0.0f;

  float idleSafetyNetLevel_ = 0.0f;
};
