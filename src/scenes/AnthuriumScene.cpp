#include "AnthuriumScene.h"

#include <math.h>

#include "../config/Profiles.h"

AnthuriumScene::AnthuriumScene(PixelOutput& output) : output_(output) {}

void AnthuriumScene::begin() {
  initialized_ = true;
  lastNowMs_ = 0;
  jConveyorPhase_ = 0.0f;
  proximity_ = 0.0f;
  signedSpeedMps_ = 0.0f;
  speedLevel_ = 0.0f;
  approachLevel_ = 0.0f;
  retreatLevel_ = 0.0f;
  motionLevel_ = 0.0f;

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    frontField_[i] = 0.0f;
    frontLuma_[i] = 0.0f;
    frontColor_[i] = {0.0f, 0.0f, 0.0f, 0.0f};
  }
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    rearLuma_[i] = 0.0f;
    rearColor_[i] = {0.0f, 0.0f, 0.0f, 0.0f};
  }
  for (uint16_t i = 0; i < kLeftJPixels; ++i) leftJColor_[i] = {0.0f, 0.0f, 0.0f, 0.0f};
  for (uint16_t i = 0; i < kRightJPixels; ++i) rightJColor_[i] = {0.0f, 0.0f, 0.0f, 0.0f};
}

void AnthuriumScene::render(const StableTrack& track, uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  uint32_t dtMs = 16;
  if (lastNowMs_ != 0) {
    dtMs = nowMs - lastNowMs_;
    if (dtMs > 200) {
      dtMs = 200;
    }
    if (dtMs < 1) {
      dtMs = 1;
    }
  }
  lastNowMs_ = nowMs;
  const float dtSec = static_cast<float>(dtMs) / 1000.0f;

  const auto& profile = Profiles::anthurium();

  const float travelSec = profile.jConveyorTravelSec > 0.001f ? profile.jConveyorTravelSec : 0.001f;
  jConveyorPhase_ += dtSec / travelSec;
  while (jConveyorPhase_ >= 1.0f) {
    jConveyorPhase_ -= 1.0f;
  }

  updateContinuousSignal(track);

  Profiles::RgbwFloat rightImpulse = {0.0f, 0.0f, 0.0f, 0.0f};
  Profiles::RgbwFloat leftImpulse = {0.0f, 0.0f, 0.0f, 0.0f};
  updateJDelayLines(track, dtSec, rightImpulse, leftImpulse);
  updateFrontRingField(dtSec, rightImpulse, leftImpulse);
  updateRearRingField(dtSec);

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const float field = clamp01(profile.torusBaseField + frontField_[i]);
    const float visible = field > profile.idleFrontRingFloor ? field : profile.idleFrontRingFloor;
    output_.setFrontRingPixel(i, toByte(frontColor_[i].r * visible), toByte(frontColor_[i].g * visible),
                              toByte(frontColor_[i].b * visible), toByte(frontColor_[i].w * visible));
  }

  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const float field = clamp01(profile.torusBaseField + rearLuma_[i]);
    const float visible = field > profile.idleRearRingFloor ? field : profile.idleRearRingFloor;
    output_.setRearRingPixel(i, toByte(rearColor_[i].r * visible), toByte(rearColor_[i].g * visible),
                             toByte(rearColor_[i].b * visible), toByte(rearColor_[i].w * visible));
  }

  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    const float glow = profile.idleJFloor + profile.jBaseGlow;
    output_.setLeftJPixel(i, toByte(leftJColor_[i].r + glow * 0.05f), toByte(leftJColor_[i].g + glow * 0.02f),
                          toByte(leftJColor_[i].b + glow * 0.01f), toByte(leftJColor_[i].w + glow * 0.02f));
  }

  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    const float glow = profile.idleJFloor + profile.jBaseGlow;
    output_.setRightJPixel(i, toByte(rightJColor_[i].r + glow * 0.05f), toByte(rightJColor_[i].g + glow * 0.02f),
                           toByte(rightJColor_[i].b + glow * 0.01f), toByte(rightJColor_[i].w + glow * 0.02f));
  }

  output_.show();
}

void AnthuriumScene::updateJDelayLines(const StableTrack& track, float dtSec, Profiles::RgbwFloat& rightImpulse, Profiles::RgbwFloat& leftImpulse) {
  const auto& profile = Profiles::anthurium();
  auto color = signalColor(track);
  const float fade = expf(-dtSec / (profile.jColorMemorySec > 0.001f ? profile.jColorMemorySec : 0.001f));
  const float adv = clamp01((dtSec / (profile.jConveyorTravelSec > 0.001f ? profile.jConveyorTravelSec : 0.001f)) * profile.jAdvectionStrength * kLeftJPixels);
  const float diff = clamp01(profile.jDiffusionStrength * dtSec);
  auto stepLine=[&](RgbwField* line, uint16_t n, Profiles::RgbwFloat& impulse){
    RgbwField tmp[kLeftJPixels];
    for(uint16_t i=0;i<n;++i){ uint16_t prev=i==0?0:i-1; tmp[i]=line[i]; tmp[i].r=clamp01((line[i].r*(1-adv))+(line[prev].r*adv)); tmp[i].g=clamp01((line[i].g*(1-adv))+(line[prev].g*adv)); tmp[i].b=clamp01((line[i].b*(1-adv))+(line[prev].b*adv)); tmp[i].w=clamp01((line[i].w*(1-adv))+(line[prev].w*adv));}
    for(uint16_t i=0;i<n;++i){ uint16_t l=i==0?0:i-1,r=i+1<n?i+1:n-1; line[i].r=clamp01((tmp[i].r*(1-diff))+(((tmp[l].r+tmp[r].r)*0.5f)*diff)); line[i].g=clamp01((tmp[i].g*(1-diff))+(((tmp[l].g+tmp[r].g)*0.5f)*diff)); line[i].b=clamp01((tmp[i].b*(1-diff))+(((tmp[l].b+tmp[r].b)*0.5f)*diff)); line[i].w=clamp01((tmp[i].w*(1-diff))+(((tmp[l].w+tmp[r].w)*0.5f)*diff)); fadeColor(line[i],fade);}
    addColor(line[0], color, track.ingressLevel * profile.jTipInjectionGain);
    impulse = {line[n-1].r, line[n-1].g, line[n-1].b, line[n-1].w};
  };
  stepLine(rightJColor_, kRightJPixels, rightImpulse);
  stepLine(leftJColor_, kLeftJPixels, leftImpulse);
}

void AnthuriumScene::updateContinuousSignal(const StableTrack& track) {
  const auto& profile = Profiles::anthurium();
  const auto& sensor = Profiles::c4001();

  float proximity = 0.0f;
  if (track.hasTarget && track.online) {
    const float rangeSpan = sensor.rangeFarM - sensor.rangeNearM;
    const float safeSpan = rangeSpan > 0.001f ? rangeSpan : 0.001f;
    const float nearNorm = 1.0f - ((track.rangeM - sensor.rangeNearM) / safeSpan);
    proximity = clamp01(nearNorm);
  }
  proximity_ = proximity;

  signedSpeedMps_ = track.speedMps;
  const float absSpeed = fabsf(signedSpeedMps_);
  const float fullScale = profile.speedFullScaleMps > 0.001f ? profile.speedFullScaleMps : 0.001f;
  speedLevel_ = clamp01(absSpeed / fullScale);

  const float deadband = profile.speedDeadbandMps > 0.0f ? profile.speedDeadbandMps : 0.0f;
  const float negSpeed = (-signedSpeedMps_) - deadband;
  const float posSpeed = signedSpeedMps_ - deadband;
  const float approachTarget = clamp01(negSpeed / fullScale);
  const float retreatTarget = clamp01(posSpeed / fullScale);
  const float motionTarget = clamp01((absSpeed - deadband) / fullScale);

  approachLevel_ = smoothToward(approachLevel_, approachTarget, profile.approachRiseAlpha, profile.approachFallAlpha);
  retreatLevel_ = smoothToward(retreatLevel_, retreatTarget, profile.retreatRiseAlpha, profile.retreatFallAlpha);
  motionLevel_ = smoothToward(motionLevel_, motionTarget, profile.motionRiseAlpha, profile.motionFallAlpha);
}

Profiles::RgbwFloat AnthuriumScene::signalColor(const StableTrack& track) const {
  const auto& profile = Profiles::anthurium();
  Profiles::RgbwFloat color = profile.idleColor;

  const float trust = clamp01(track.continuity);
  const float intensity = clamp01((0.40f * proximity_) + (0.60f * track.charge));
  const float energy = clamp01((0.20f * speedLevel_) + (0.80f * motionLevel_));
  const float approach = clamp01(approachLevel_ * energy * intensity * trust);
  const float retreat = clamp01(retreatLevel_ * energy * intensity * trust);

  color.r = clamp01(color.r + (profile.approachColor.r * approach) + (profile.retreatColor.r * retreat));
  color.g = clamp01(color.g + (profile.approachColor.g * approach) + (profile.retreatColor.g * retreat));
  color.b = clamp01(color.b + (profile.approachColor.b * approach) + (profile.retreatColor.b * retreat));
  color.w = clamp01(color.w + (profile.approachColor.w * approach) + (profile.retreatColor.w * retreat));
  return color;
}

void AnthuriumScene::fadeColor(RgbwField& color, float fade) {
  color.r = clamp01(color.r * fade);
  color.g = clamp01(color.g * fade);
  color.b = clamp01(color.b * fade);
  color.w = clamp01(color.w * fade);
}

void AnthuriumScene::addColor(RgbwField& color, const Profiles::RgbwFloat& add, float amount) {
  color.r = clamp01(color.r + (add.r * amount));
  color.g = clamp01(color.g + (add.g * amount));
  color.b = clamp01(color.b + (add.b * amount));
  color.w = clamp01(color.w + (add.w * amount));
}

void AnthuriumScene::updateFrontRingField(float dtSec, const Profiles::RgbwFloat& rightImpulse, const Profiles::RgbwFloat& leftImpulse) {
  const auto& profile = Profiles::anthurium();
  const float fade = expf(-dtSec / (profile.frontRingColorMemorySec > 0.001f ? profile.frontRingColorMemorySec : 0.001f));
  const float diff = clamp01(profile.frontRingDiffusion * dtSec);
  const uint8_t blurPasses = profile.frontRingBlurPasses > 0 ? profile.frontRingBlurPasses : 1;
  for (uint8_t pass = 0; pass < blurPasses; ++pass) {
    for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
      uint16_t l = (i == 0) ? (kFrontRingPixels - 1) : (i - 1);
      uint16_t r = (i + 1) % kFrontRingPixels;
      frontField_[i] = clamp01((frontField_[i] * (1.0f - diff)) + (((frontField_[l] + frontField_[r]) * 0.5f) * diff));
      fadeColor(frontColor_[i], fade);
    }
  }
  const uint16_t injA = 7;
  const uint16_t injB = 29;
  addColor(frontColor_[injA], rightImpulse, profile.frontRingImpulseGain);
  addColor(frontColor_[injB], leftImpulse, profile.frontRingImpulseGain);
  frontField_[injA] = clamp01(frontField_[injA] + (rightImpulse.w * profile.frontRingImpulseGain));
  frontField_[injB] = clamp01(frontField_[injB] + (leftImpulse.w * profile.frontRingImpulseGain));
}

void AnthuriumScene::updateRearRingField(float dtSec) {
  const auto& profile = Profiles::anthurium();
  const float fade = expf(-dtSec / (profile.rearRingMemorySec > 0.001f ? profile.rearRingMemorySec : 0.001f));
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const uint16_t a = i % kFrontRingPixels;
    const uint16_t b = (i + 1) % kFrontRingPixels;
    rearLuma_[i] = clamp01((frontField_[a] * 0.7f + frontField_[b] * 0.3f) * profile.rearRingWashScale);
    rearColor_[i].r = clamp01((rearColor_[i].r * fade) + (frontColor_[a].r * 0.05f * profile.rearRingWashScale));
    rearColor_[i].g = clamp01((rearColor_[i].g * fade) + (frontColor_[a].g * 0.05f * profile.rearRingWashScale));
    rearColor_[i].b = clamp01((rearColor_[i].b * fade) + (frontColor_[a].b * 0.05f * profile.rearRingWashScale));
    rearColor_[i].w = clamp01((rearColor_[i].w * fade) + (frontColor_[a].w * 0.05f * profile.rearRingWashScale));
  }
}

float AnthuriumScene::clamp01(float v) {
  if (v < 0.0f) {
    return 0.0f;
  }
  if (v > 1.0f) {
    return 1.0f;
  }
  return v;
}

uint8_t AnthuriumScene::toByte(float v) {
  return static_cast<uint8_t>(clamp01(v) * 255.0f);
}

float AnthuriumScene::smoothToward(float current, float target, float riseAlpha, float fallAlpha) {
  const float rise = clamp01(riseAlpha);
  const float fall = clamp01(fallAlpha);
  const float alpha = target > current ? rise : fall;
  return current + ((target - current) * alpha);
}
