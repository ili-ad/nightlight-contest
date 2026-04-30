#pragma once

#include <stdint.h>

#include "../config/Profiles.h"
#include "../model/StableTrack.h"
#include "../render/PixelOutput.h"

class AnthuriumScene {
 public:
  explicit AnthuriumScene(PixelOutput& output);

  void begin();
  void render(const StableTrack& track, uint32_t nowMs);

 private:
  struct RgbwField {
    float r;
    float g;
    float b;
    float w;
  };

  static float clamp01(float v);
  static uint8_t toByte(float v);
  void updateJDelayLines(const StableTrack& track, float dtSec, Profiles::RgbwFloat& rightImpulse, Profiles::RgbwFloat& leftImpulse);
  void updateFrontRingField(float dtSec, const Profiles::RgbwFloat& rightImpulse, const Profiles::RgbwFloat& leftImpulse);
  void updateRearRingField(float dtSec);
  void updateContinuousSignal(const StableTrack& track);
  Profiles::RgbwFloat signalColor(const StableTrack& track) const;
  static void fadeColor(RgbwField& color, float fade);
  static void addColor(RgbwField& color, const Profiles::RgbwFloat& add, float amount);
  static float smoothToward(float current, float target, float riseAlpha, float fallAlpha);

  PixelOutput& output_;
  bool initialized_ = false;
  uint32_t lastNowMs_ = 0;
  float jConveyorPhase_ = 0.0f;
  float proximity_ = 0.0f;
  float signedSpeedMps_ = 0.0f;
  float speedLevel_ = 0.0f;
  float approachLevel_ = 0.0f;
  float retreatLevel_ = 0.0f;
  float motionLevel_ = 0.0f;

  static constexpr uint16_t kFrontRingPixels = Profiles::kFrontRingPixels;
  static constexpr uint16_t kRearRingPixels = Profiles::kRearRingPixels;
  static constexpr uint16_t kRightJPixels = Profiles::kRightJPixels;
  static constexpr uint16_t kLeftJPixels = Profiles::kLeftJPixels;

  float frontField_[kFrontRingPixels] = {0.0f};
  float frontLuma_[kFrontRingPixels] = {0.0f};
  float rearLuma_[kRearRingPixels] = {0.0f};
  RgbwField frontColor_[kFrontRingPixels] = {{0.0f, 0.0f, 0.0f, 0.0f}};
  RgbwField rearColor_[kRearRingPixels] = {{0.0f, 0.0f, 0.0f, 0.0f}};
  RgbwField leftJColor_[kLeftJPixels] = {{0.0f, 0.0f, 0.0f, 0.0f}};
  RgbwField rightJColor_[kRightJPixels] = {{0.0f, 0.0f, 0.0f, 0.0f}};
};
