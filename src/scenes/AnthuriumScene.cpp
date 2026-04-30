#include "AnthuriumScene.h"

#include <math.h>

#include "../config/Profiles.h"

AnthuriumScene::AnthuriumScene(PixelOutput& output) : output_(output) {}

void AnthuriumScene::begin() {
  initialized_ = true;
  lastNowMs_ = 0;
  ingressPhase_ = 0.0f;
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
  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    leftJLuma_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    rightJLuma_[i] = 0.0f;
  }
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

  ingressPhase_ += dtSec / profile.ingressTravelSec;
  while (ingressPhase_ >= 1.0f) {
    ingressPhase_ -= 1.0f;
  }

  updateTorus(track, dtSec);
  updateContinuousSignal(track);

  const auto ringColorAdd = signalColor(track);
  const float r = ringColorAdd.r;
  const float g = ringColorAdd.g;
  const float b = ringColorAdd.b;
  const float w = ringColorAdd.w;
  const float memorySec = profile.ringColorMemorySec > 0.001f ? profile.ringColorMemorySec : 0.001f;
  const float ringFade = expf(-dtSec / memorySec);

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const float field = clamp01(profile.torusBaseField + frontField_[i]);
    const float target = clamp01((field * track.continuity) + (track.charge * profile.torusInstantGain));
    const float alpha = target > frontLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    frontLuma_[i] = frontLuma_[i] + ((target - frontLuma_[i]) * alpha);
    const float visibleLuma = frontLuma_[i] > profile.idleFrontRingFloor ? frontLuma_[i] : profile.idleFrontRingFloor;

    fadeColor(frontColor_[i], ringFade);
    const float inject = target * dtSec * 1.6f;
    addColor(frontColor_[i], ringColorAdd, inject);

    output_.setFrontRingPixel(i,
                              toByte(frontColor_[i].r * visibleLuma),
                              toByte(frontColor_[i].g * visibleLuma),
                              toByte(frontColor_[i].b * visibleLuma),
                              toByte(frontColor_[i].w * field));
  }

  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    uint16_t source = i;
    if (profile.rearRingMirror) {
      source = kRearRingPixels - 1 - i;
    }

    float phasePosition = static_cast<float>(source) + (profile.rearRingPhaseOffset * kRearRingPixels);
    while (phasePosition >= kRearRingPixels) {
      phasePosition -= kRearRingPixels;
    }

    const uint16_t base = static_cast<uint16_t>(phasePosition);
    const uint16_t next = (base + 1) % kRearRingPixels;
    const float mix = phasePosition - static_cast<float>(base);
    const float rearField = ((frontField_[base] * (1.0f - mix)) + (frontField_[next] * mix)) * profile.rearRingScale;

    const float field = clamp01(profile.torusBaseField + rearField);
    const float target = clamp01((field * track.continuity) + (track.charge * profile.torusInstantGain * profile.rearRingScale));
    const float alpha = target > rearLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    rearLuma_[i] = rearLuma_[i] + ((target - rearLuma_[i]) * alpha);
    const float visibleLuma = rearLuma_[i] > profile.idleRearRingFloor ? rearLuma_[i] : profile.idleRearRingFloor;

    fadeColor(rearColor_[i], ringFade);
    const float inject = target * dtSec * (1.6f * profile.rearRingScale);
    addColor(rearColor_[i], ringColorAdd, inject);

    output_.setRearRingPixel(i,
                             toByte(rearColor_[i].r * visibleLuma),
                             toByte(rearColor_[i].g * visibleLuma),
                             toByte(rearColor_[i].b * visibleLuma),
                             toByte(rearColor_[i].w * field));
  }

  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    const float ingress = sampleIngress(i, kLeftJPixels, track, profile.leftJIngressReversed);
    const float alpha = ingress > leftJLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    leftJLuma_[i] = leftJLuma_[i] + ((ingress - leftJLuma_[i]) * alpha);
    const float visibleLuma = leftJLuma_[i] > profile.idleJFloor ? leftJLuma_[i] : profile.idleJFloor;
    const float visibleIngress = ingress > profile.idleJFloor ? ingress : profile.idleJFloor;

    output_.setLeftJPixel(i,
                          toByte(r * visibleLuma),
                          toByte(g * visibleLuma),
                          toByte(b * visibleLuma),
                          toByte(w * visibleIngress));
  }

  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    const float ingress = sampleIngress(i, kRightJPixels, track, profile.rightJIngressReversed);
    const float alpha = ingress > rightJLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    rightJLuma_[i] = rightJLuma_[i] + ((ingress - rightJLuma_[i]) * alpha);
    const float visibleLuma = rightJLuma_[i] > profile.idleJFloor ? rightJLuma_[i] : profile.idleJFloor;
    const float visibleIngress = ingress > profile.idleJFloor ? ingress : profile.idleJFloor;

    output_.setRightJPixel(i,
                           toByte(r * visibleLuma),
                           toByte(g * visibleLuma),
                           toByte(b * visibleLuma),
                           toByte(w * visibleIngress));
  }

  output_.show();
}

void AnthuriumScene::updateTorus(const StableTrack& track, float dtSec) {
  const auto& profile = Profiles::anthurium();

  float tmp[kFrontRingPixels] = {0.0f};
  const float decay = expf(-profile.torusDecayPerSecond * dtSec);
  const float diffusion = profile.torusDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (kFrontRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % kFrontRingPixels;

    float v = frontField_[i];
    v += (frontField_[left] + frontField_[right] - (2.0f * frontField_[i])) * diffusion;
    v *= decay;
    tmp[i] = clamp01(v);
  }

  const float input = track.charge * track.continuity * profile.torusAccumulationGain * dtSec;

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    float da = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(profile.ingressA)));
    float db = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(profile.ingressB)));
    if (da > (kFrontRingPixels * 0.5f)) {
      da = kFrontRingPixels - da;
    }
    if (db > (kFrontRingPixels * 0.5f)) {
      db = kFrontRingPixels - db;
    }

    tmp[i] = clamp01(tmp[i] + (input * (polynomialKernel(da, profile.ingressSpread) +
                                        polynomialKernel(db, profile.ingressSpread))));
  }

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    frontField_[i] = tmp[i];
  }
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

float AnthuriumScene::sampleIngress(uint16_t pixel, uint16_t count, const StableTrack& track, bool reverseLogical) const {
  const float denom = (count > 1) ? static_cast<float>(count - 1) : 1.0f;
  const float linearPosition = static_cast<float>(pixel) / denom;
  const float position = reverseLogical ? (1.0f - linearPosition) : linearPosition;
  const float tipToEntry = 1.0f - position;

  float delta = fabsf(tipToEntry - ingressPhase_);
  if (delta > 0.5f) {
    delta = 1.0f - delta;
  }

  const auto& profile = Profiles::anthurium();

  const float conveyor = polynomialKernel(delta, profile.ingressWidth);
  const float floor = track.charge * profile.ingressFloor;
  const float level = floor + (conveyor * track.ingressLevel);
  return clamp01(level);
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

float AnthuriumScene::polynomialKernel(float distance, float width) {
  const float safeWidth = width < 0.001f ? 0.001f : width;
  const float x = clamp01(1.0f - (distance / safeWidth));
  return x * x;
}

float AnthuriumScene::smoothToward(float current, float target, float riseAlpha, float fallAlpha) {
  const float rise = clamp01(riseAlpha);
  const float fall = clamp01(fallAlpha);
  const float alpha = target > current ? rise : fall;
  return current + ((target - current) * alpha);
}
