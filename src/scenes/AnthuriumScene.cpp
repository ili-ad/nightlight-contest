#include "AnthuriumScene.h"

#include <math.h>

#include "../config/Profiles.h"

AnthuriumScene::AnthuriumScene(PixelOutput& output) : output_(output) {}

void AnthuriumScene::begin() {
  initialized_ = true;
  lastNowMs_ = 0;
  hadRangeSample_ = false;
  prevAcceptedRangeM_ = 0.0f;
  motionSignal_ = 0.0f;
  chargeSignal_ = 0.0f;
  ingressSignal_ = 0.0f;
  displayHue_ = 0.33f;
  displaySat_ = 0.24f;
  displayLevel_ = 0.08f;
  displayWhite_ = 0.015f;

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
  if (!initialized_) begin();
  uint32_t dtMs = 16;
  if (lastNowMs_ != 0) {
    dtMs = nowMs - lastNowMs_;
    if (dtMs < 1) dtMs = 1;
    if (dtMs > 200) dtMs = 200;
  }
  lastNowMs_ = nowMs;
  const float dtSec = static_cast<float>(dtMs) / 1000.0f;

  updateContinuousSignal(track, dtSec);

  Profiles::RgbwFloat rightImpulse = {0.0f, 0.0f, 0.0f, 0.0f};
  Profiles::RgbwFloat leftImpulse = {0.0f, 0.0f, 0.0f, 0.0f};
  updateJDelayLines(track, dtSec, rightImpulse, leftImpulse);
  updateFrontRingField(dtSec, rightImpulse, leftImpulse);
  updateRearRingField(dtSec);

  const auto& profile = Profiles::anthurium();
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const float luma = clamp01(frontLuma_[i]);
    output_.setFrontRingPixel(i, toByte(frontColor_[i].r * luma), toByte(frontColor_[i].g * luma),
                              toByte(frontColor_[i].b * luma),
                              toByte(frontColor_[i].w * luma * profile.displayWhiteOutputScale));
  }
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const float luma = clamp01(rearLuma_[i]);
    output_.setRearRingPixel(i, toByte(rearColor_[i].r * luma), toByte(rearColor_[i].g * luma),
                             toByte(rearColor_[i].b * luma),
                             toByte(rearColor_[i].w * luma * profile.displayWhiteOutputScale));
  }
  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    output_.setRightJPixel(i, toByte(rightJColor_[i].r), toByte(rightJColor_[i].g),
                           toByte(rightJColor_[i].b),
                           toByte(rightJColor_[i].w * profile.displayWhiteOutputScale));
  }
  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    output_.setLeftJPixel(i, toByte(leftJColor_[i].r), toByte(leftJColor_[i].g), toByte(leftJColor_[i].b),
                          toByte(leftJColor_[i].w * profile.displayWhiteOutputScale));
  }
  output_.show();
}

void AnthuriumScene::updateContinuousSignal(const StableTrack& track, float dtSec) {
  const auto& sensor = Profiles::c4001();
  float nearness = 0.0f;
  if (track.hasTarget) {
    const float span = sensor.rangeFarM - sensor.rangeNearM;
    const float safeSpan = span > 0.001f ? span : 0.001f;
    nearness = clamp01(1.0f - ((track.rangeM - sensor.rangeNearM) / safeSpan));
  }

  float rawMotion = 0.0f;
  if (track.hasTarget && dtSec > 0.0001f) {
    if (hadRangeSample_) {
      const float velocityMps = (prevAcceptedRangeM_ - track.rangeM) / dtSec;
      rawMotion = clamp01(fabsf(velocityMps) / 0.35f);
      if (velocityMps < 0.0f) rawMotion = -rawMotion;
    }
    prevAcceptedRangeM_ = track.rangeM;
    hadRangeSample_ = true;
  } else {
    hadRangeSample_ = false;
  }
  motionSignal_ = motionSignal_ + ((rawMotion - motionSignal_) * 0.22f);

  const float chargeTarget = nearness * clamp01(track.continuity);
  chargeSignal_ = smoothToward(chargeSignal_, chargeTarget, 0.24f, 0.14f);
  const float ingressTarget = clamp01((clamp01(fabsf(motionSignal_)) * 0.78f) + (chargeSignal_ * 0.42f));
  const float ingressAlpha = clamp01(dtSec / (0.28f + dtSec));
  ingressSignal_ = clamp01(ingressSignal_ + ((ingressTarget - ingressSignal_) * ingressAlpha));

  const float approach = clamp01(motionSignal_);
  const float retreat = clamp01(-motionSignal_);
  const float warm = clamp01((approach * 0.72f) + (chargeSignal_ * 0.58f));
  const float cool = clamp01(retreat * 0.95f);
  const float targetHue = (cool > warm) ? (0.33f + ((0.60f - 0.33f) * cool)) : (0.33f + ((0.01f - 0.33f) * warm));
  const float targetSat = clamp01(0.24f + (fabsf(motionSignal_) * 0.48f) + (chargeSignal_ * 0.12f));
  const float targetLevel = clamp01(0.08f + (fabsf(motionSignal_) * 0.10f) + (chargeSignal_ * 0.14f));
  const float targetWhite = clamp01(0.015f + (chargeSignal_ * 0.14f));

  displayHue_ += (targetHue - displayHue_) * 0.20f;
  displaySat_ += (targetSat - displaySat_) * 0.18f;
  displayLevel_ += (targetLevel - displayLevel_) * 0.18f;
  displayWhite_ += (targetWhite - displayWhite_) * 0.18f;
}

Profiles::RgbwFloat AnthuriumScene::signalColor(const StableTrack&) const {
  const float h = displayHue_;
  const float s = clamp01(displaySat_);
  const float v = clamp01(displayLevel_);
  const float scaled = h * 6.0f;
  const int sector = static_cast<int>(scaled);
  const float f = scaled - sector;
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));
  float rf = v, gf = t, bf = p;
  switch (sector % 6) {
    case 0: rf = v; gf = t; bf = p; break;
    case 1: rf = q; gf = v; bf = p; break;
    case 2: rf = p; gf = v; bf = t; break;
    case 3: rf = p; gf = q; bf = v; break;
    case 4: rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;
  }
  return {clamp01(rf), clamp01(gf), clamp01(bf), clamp01(displayWhite_)};
}

void AnthuriumScene::updateJDelayLines(const StableTrack& track, float dtSec, Profiles::RgbwFloat& rightImpulse,
                                       Profiles::RgbwFloat& leftImpulse) {
  const auto& profile = Profiles::anthurium();
  const Profiles::RgbwFloat color = signalColor(track);
  const float decay = clamp01(4.5f / (4.5f + dtSec));
  const float adv = clamp01((dtSec / 3.0f) * 0.90f * kLeftJPixels);
  const float diff = clamp01(0.22f * dtSec);

  auto step = [&](RgbwField* line, uint16_t n, bool ingressReversed, Profiles::RgbwFloat& impulse) {
    RgbwField tmp[kLeftJPixels];
    const uint16_t ingress = ingressReversed ? (n - 1) : 0;
    const uint16_t egress = ingressReversed ? 0 : (n - 1);
    for (uint16_t i = 0; i < n; ++i) {
      const uint16_t prev = ingressReversed ? (i + 1 < n ? i + 1 : n - 1) : (i > 0 ? i - 1 : 0);
      tmp[i].r = clamp01((line[i].r * (1.0f - adv)) + (line[prev].r * adv));
      tmp[i].g = clamp01((line[i].g * (1.0f - adv)) + (line[prev].g * adv));
      tmp[i].b = clamp01((line[i].b * (1.0f - adv)) + (line[prev].b * adv));
      tmp[i].w = clamp01((line[i].w * (1.0f - adv)) + (line[prev].w * adv));
    }
    for (uint16_t i = 0; i < n; ++i) {
      const uint16_t l = i == 0 ? 0 : i - 1;
      const uint16_t r = i + 1 < n ? i + 1 : n - 1;
      line[i].r = clamp01((tmp[i].r * (1.0f - diff)) + (((tmp[l].r + tmp[r].r) * 0.5f) * diff));
      line[i].g = clamp01((tmp[i].g * (1.0f - diff)) + (((tmp[l].g + tmp[r].g) * 0.5f) * diff));
      line[i].b = clamp01((tmp[i].b * (1.0f - diff)) + (((tmp[l].b + tmp[r].b) * 0.5f) * diff));
      line[i].w = clamp01((tmp[i].w * (1.0f - diff)) + (((tmp[l].w + tmp[r].w) * 0.5f) * diff));
      fadeColor(line[i], decay);
    }
    addColor(line[ingress], color, clamp01((ingressSignal_ * 0.22f) + (chargeSignal_ * 0.42f) + 0.05f));
    impulse = {line[egress].r, line[egress].g, line[egress].b, line[egress].w};
  };

  step(rightJColor_, kRightJPixels, profile.rightJIngressReversed, rightImpulse);
  step(leftJColor_, kLeftJPixels, profile.leftJIngressReversed, leftImpulse);
}

void AnthuriumScene::updateFrontRingField(float dtSec, const Profiles::RgbwFloat&, const Profiles::RgbwFloat&) {
  const auto& profile = Profiles::anthurium();
  float nextField[kFrontRingPixels];
  RgbwField nextColor[kFrontRingPixels];
  const float decay = clamp01(4.5f / (4.5f + dtSec));
  const float diffusion = 0.34f * dtSec;
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const uint16_t l = (i == 0) ? (kFrontRingPixels - 1) : (i - 1);
    const uint16_t r = (i + 1) % kFrontRingPixels;
    nextField[i] = clamp01((frontField_[i] + ((frontField_[l] + frontField_[r] - (2.0f * frontField_[i])) * diffusion)) * decay);
    nextColor[i].r = clamp01((frontColor_[i].r + ((frontColor_[l].r + frontColor_[r].r - (2.0f * frontColor_[i].r)) * diffusion)) * decay);
    nextColor[i].g = clamp01((frontColor_[i].g + ((frontColor_[l].g + frontColor_[r].g - (2.0f * frontColor_[i].g)) * diffusion)) * decay);
    nextColor[i].b = clamp01((frontColor_[i].b + ((frontColor_[l].b + frontColor_[r].b - (2.0f * frontColor_[i].b)) * diffusion)) * decay);
    nextColor[i].w = clamp01((frontColor_[i].w + ((frontColor_[l].w + frontColor_[r].w - (2.0f * frontColor_[i].w)) * diffusion)) * decay);
  }

  auto inject = [&](uint16_t center, const Profiles::RgbwFloat& c, float amount) {
    for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
      int d = static_cast<int>(i) - static_cast<int>(center);
      if (d < 0) d = -d;
      const int wrap = kFrontRingPixels - d;
      const int ringDist = d < wrap ? d : wrap;
      const float k = clamp01(1.0f - (static_cast<float>(ringDist) / 3.5f));
      const float poly = k * k;
      nextField[i] = clamp01(nextField[i] + (amount * poly));
      nextColor[i].r = clamp01(nextColor[i].r + (c.r * amount * poly));
      nextColor[i].g = clamp01(nextColor[i].g + (c.g * amount * poly));
      nextColor[i].b = clamp01(nextColor[i].b + (c.b * amount * poly));
      nextColor[i].w = clamp01(nextColor[i].w + (c.w * amount * poly * 0.12f));
    }
  };

  const auto liveColor = signalColor(StableTrack{});
  const float injectGain = clamp01((chargeSignal_ * 0.48f + ingressSignal_ * 0.08f) * dtSec * profile.frontRingImpulseGain);
  inject(profile.frontRingIngressA % kFrontRingPixels, liveColor, injectGain);
  inject(profile.frontRingIngressB % kFrontRingPixels, liveColor, injectGain);

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    frontField_[i] = nextField[i];
    frontColor_[i] = nextColor[i];
    frontLuma_[i] = smoothToward(frontLuma_[i], clamp01(0.06f + frontField_[i]), 0.24f, 0.14f);
  }
}

void AnthuriumScene::updateRearRingField(float dtSec) {
  const float decay = clamp01(6.0f / (6.0f + dtSec));
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const uint16_t a = i % kFrontRingPixels;
    const uint16_t b = (i + 1) % kFrontRingPixels;
    rearLuma_[i] = clamp01((rearLuma_[i] * decay) + (((frontLuma_[a] * 0.8f) + (frontLuma_[b] * 0.2f)) * 0.10f));
    rearColor_[i].r = clamp01((rearColor_[i].r * decay) + (((frontColor_[a].r * 0.8f) + (frontColor_[b].r * 0.2f)) * 0.05f));
    rearColor_[i].g = clamp01((rearColor_[i].g * decay) + (((frontColor_[a].g * 0.8f) + (frontColor_[b].g * 0.2f)) * 0.05f));
    rearColor_[i].b = clamp01((rearColor_[i].b * decay) + (((frontColor_[a].b * 0.8f) + (frontColor_[b].b * 0.2f)) * 0.05f));
    rearColor_[i].w = clamp01((rearColor_[i].w * decay) + (((frontColor_[a].w * 0.8f) + (frontColor_[b].w * 0.2f)) * 0.02f));
  }
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
float AnthuriumScene::clamp01(float v) { if (v < 0.0f) return 0.0f; if (v > 1.0f) return 1.0f; return v; }
uint8_t AnthuriumScene::toByte(float v) { return static_cast<uint8_t>(clamp01(v) * 255.0f); }
float AnthuriumScene::smoothToward(float current, float target, float riseAlpha, float fallAlpha) {
  const float alpha = target > current ? clamp01(riseAlpha) : clamp01(fallAlpha);
  return current + ((target - current) * alpha);
}
