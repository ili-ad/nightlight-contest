#include "AnthuriumScene.h"

#include <math.h>

#ifndef ANTHURIUM_RENDER_HEARTBEAT
#define ANTHURIUM_RENDER_HEARTBEAT 0
#endif

#include "../config/Profiles.h"

AnthuriumScene::AnthuriumScene(PixelOutput& output) : output_(output) {}

void AnthuriumScene::begin() {
  initialized_ = true;
  lastNowMs_ = 0;
  jConveyorPhase_ = 0.0f;
  proximity_ = 0.0f;
  motionSignal_ = 0.0f;
  chargeSignal_ = 0.0f;
  ingressSignal_ = 0.0f;
  displayHue_ = 0.33f;
  displaySat_ = 0.12f;
  displayLevel_ = 0.04f;
  displayWhite_ = 0.01f;
  hadRangeSample_ = false;
  prevAcceptedRangeM_ = 0.0f;
  heartbeatFrame_ = 0;
  heartbeatLastLogMs_ = 0;

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
    if (dtMs > 200) dtMs = 200;
    if (dtMs < 1) dtMs = 1;
  }
  lastNowMs_ = nowMs;
  const float dtSec = static_cast<float>(dtMs) / 1000.0f;

  const auto& profile = Profiles::anthurium();
  const float travelSec = profile.jConveyorTravelSec > 0.001f ? profile.jConveyorTravelSec : 0.001f;
  jConveyorPhase_ += dtSec / travelSec;
  while (jConveyorPhase_ >= 1.0f) jConveyorPhase_ -= 1.0f;

  updateContinuousSignal(track, dtSec);

  Profiles::RgbwFloat rightImpulse = {0.0f, 0.0f, 0.0f, 0.0f};
  Profiles::RgbwFloat leftImpulse = {0.0f, 0.0f, 0.0f, 0.0f};
  updateJDelayLines(track, dtSec, rightImpulse, leftImpulse);
  updateFrontRingField(dtSec, rightImpulse, leftImpulse);
  updateRearRingField(dtSec);

  const bool idleOnly = !track.online || !track.hasTarget;
  const float idleMix = idleOnly ? 1.0f : 0.24f;
  const auto& idle = profile.idleColor;

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const float visible = clamp01(0.10f + frontField_[i]);
    const float idleFloor = profile.idleFrontRingFloor * idleMix;
    const float r = clamp01((frontColor_[i].r * visible) + (idle.r * idleFloor));
    const float g = clamp01((frontColor_[i].g * visible) + (idle.g * idleFloor));
    const float b = clamp01((frontColor_[i].b * visible) + (idle.b * idleFloor));
    const float w = clamp01((frontColor_[i].w * visible) + (idle.w * idleFloor));
    output_.setFrontRingPixel(i, toByte(r), toByte(g), toByte(b), toByte(w));
  }
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const float visible = clamp01(0.08f + (rearLuma_[i] * 0.7f));
    const float idleFloor = profile.idleRearRingFloor * idleMix;
    const float r = clamp01((rearColor_[i].r * visible) + (idle.r * idleFloor));
    const float g = clamp01((rearColor_[i].g * visible) + (idle.g * idleFloor));
    const float b = clamp01((rearColor_[i].b * visible) + (idle.b * idleFloor));
    const float w = clamp01((rearColor_[i].w * visible) + (idle.w * idleFloor));
    output_.setRearRingPixel(i, toByte(r), toByte(g), toByte(b), toByte(w));
  }
  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    const float idleFloor = profile.idleJFloor * idleMix;
    const float r = clamp01(leftJColor_[i].r + (idle.r * idleFloor));
    const float g = clamp01(leftJColor_[i].g + (idle.g * idleFloor));
    const float b = clamp01(leftJColor_[i].b + (idle.b * idleFloor));
    const float w = clamp01(leftJColor_[i].w + (idle.w * idleFloor));
    output_.setLeftJPixel(i, toByte(r), toByte(g), toByte(b), toByte(w));
  }
  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    const float idleFloor = profile.idleJFloor * idleMix;
    const float r = clamp01(rightJColor_[i].r + (idle.r * idleFloor));
    const float g = clamp01(rightJColor_[i].g + (idle.g * idleFloor));
    const float b = clamp01(rightJColor_[i].b + (idle.b * idleFloor));
    const float w = clamp01(rightJColor_[i].w + (idle.w * idleFloor));
    output_.setRightJPixel(i, toByte(r), toByte(g), toByte(b), toByte(w));
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
  proximity_ = nearness * clamp01(track.continuity);

  float rawMotion = 0.0f;
  if (track.hasTarget && dtSec > 0.0001f) {
    if (hadRangeSample_) {
      const float velocityMps = (prevAcceptedRangeM_ - track.rangeM) / dtSec;
      const float fullScale = 0.35f;
      rawMotion = clamp01(fabsf(velocityMps) / fullScale);
      rawMotion = velocityMps >= 0.0f ? rawMotion : -rawMotion;
    }
    prevAcceptedRangeM_ = track.rangeM;
    hadRangeSample_ = true;
  } else {
    hadRangeSample_ = false;
  }
  motionSignal_ = motionSignal_ + ((rawMotion - motionSignal_) * 0.22f);
  if (!track.hasTarget) motionSignal_ = motionSignal_ + ((0.0f - motionSignal_) * 0.06f);

  const float motionAbs = clamp01(fabsf(motionSignal_));
  const float chargeTarget = proximity_;
  const float chargeAlpha = chargeTarget >= chargeSignal_ ? 0.24f : 0.14f;
  chargeSignal_ = clamp01(chargeSignal_ + ((chargeTarget - chargeSignal_) * chargeAlpha));

  const float ingressTarget = clamp01((motionAbs * 0.78f) + (chargeSignal_ * 0.26f));
  const float ingressAlpha = clamp01(dtSec / (0.28f + dtSec));
  ingressSignal_ = clamp01(ingressSignal_ + ((ingressTarget - ingressSignal_) * ingressAlpha));

  const float approach = clamp01(motionSignal_);
  const float retreat = clamp01(-motionSignal_);
  const float warm = clamp01((approach * 0.72f) + (chargeSignal_ * 0.58f));
  const float cool = clamp01(retreat * 0.95f);
  const float targetHue = (cool > warm) ? (0.33f + ((0.60f - 0.33f) * cool)) : (0.33f + ((0.01f - 0.33f) * warm));
  const float targetSat = clamp01(0.24f + (motionAbs * 0.48f) + (chargeSignal_ * 0.12f));
  const float targetLevel = clamp01(0.08f + (motionAbs * 0.10f) + (chargeSignal_ * 0.14f));
  const float targetWhite = clamp01(0.015f + (chargeSignal_ * 0.05f));

  displayHue_ += (targetHue - displayHue_) * 0.20f;
  displaySat_ += (targetSat - displaySat_) * 0.18f;
  displayLevel_ += (targetLevel - displayLevel_) * 0.18f;
  displayWhite_ += (targetWhite - displayWhite_) * 0.18f;
}

Profiles::RgbwFloat AnthuriumScene::signalColor(const StableTrack& track) const {
  (void)track;
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
  switch (sector % 6) { case 0: rf=v;gf=t;bf=p; break; case 1: rf=q;gf=v;bf=p; break; case 2: rf=p;gf=v;bf=t; break; case 3: rf=p;gf=q;bf=v; break; case 4: rf=t;gf=p;bf=v; break; default: rf=v;gf=p;bf=q; break; }
  return {clamp01(rf), clamp01(gf), clamp01(bf), clamp01(displayWhite_)};
}

void AnthuriumScene::updateJDelayLines(const StableTrack& track, float dtSec, Profiles::RgbwFloat& rightImpulse, Profiles::RgbwFloat& leftImpulse) {
  const auto& profile = Profiles::anthurium();
  auto color = signalColor(track);
  const float fade = expf(-dtSec / 2.6f);
  const float adv = clamp01((dtSec / (profile.jConveyorTravelSec > 0.001f ? profile.jConveyorTravelSec : 0.001f)) * 0.9f * kLeftJPixels);
  const float diff = clamp01(0.22f * dtSec);
  auto stepLine=[&](RgbwField* line, uint16_t n, bool ingressReversed, Profiles::RgbwFloat& impulse){
    RgbwField tmp[kLeftJPixels]; const uint16_t ingressIndex = ingressReversed ? (n - 1) : 0; const uint16_t egressIndex = ingressReversed ? 0 : (n - 1);
    for(uint16_t i=0;i<n;++i){ const uint16_t advectNeighbor = ingressReversed ? (i + 1 < n ? i + 1 : (n - 1)) : (i == 0 ? 0 : i - 1); tmp[i]=line[i];
      tmp[i].r=clamp01((line[i].r*(1-adv))+(line[advectNeighbor].r*adv)); tmp[i].g=clamp01((line[i].g*(1-adv))+(line[advectNeighbor].g*adv)); tmp[i].b=clamp01((line[i].b*(1-adv))+(line[advectNeighbor].b*adv)); tmp[i].w=clamp01((line[i].w*(1-adv))+(line[advectNeighbor].w*adv)); }
    for(uint16_t i=0;i<n;++i){ uint16_t l=i==0?0:i-1,r=i+1<n?i+1:n-1; line[i].r=clamp01((tmp[i].r*(1-diff))+(((tmp[l].r+tmp[r].r)*0.5f)*diff)); line[i].g=clamp01((tmp[i].g*(1-diff))+(((tmp[l].g+tmp[r].g)*0.5f)*diff)); line[i].b=clamp01((tmp[i].b*(1-diff))+(((tmp[l].b+tmp[r].b)*0.5f)*diff)); line[i].w=clamp01((tmp[i].w*(1-diff))+(((tmp[l].w+tmp[r].w)*0.5f)*diff)); fadeColor(line[i],fade);}    
    const float ambient = 0.05f * (0.25f + (0.75f * chargeSignal_));
    addColor(line[ingressIndex], color, clamp01((ingressSignal_ * 0.65f) + ambient));
    impulse = {line[egressIndex].r, line[egressIndex].g, line[egressIndex].b, line[egressIndex].w};
  };
  stepLine(rightJColor_, kRightJPixels, profile.rightJIngressReversed, rightImpulse);
  stepLine(leftJColor_, kLeftJPixels, profile.leftJIngressReversed, leftImpulse);
}

void AnthuriumScene::updateFrontRingField(float dtSec, const Profiles::RgbwFloat& rightImpulse, const Profiles::RgbwFloat& leftImpulse) {
  const float fade = expf(-dtSec / 4.5f);
  const float diffusion = 0.34f * dtSec;
  float nextField[kFrontRingPixels];
  RgbwField nextColor[kFrontRingPixels];
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const uint16_t l = (i == 0) ? (kFrontRingPixels - 1) : (i - 1);
    const uint16_t r = (i + 1) % kFrontRingPixels;
    nextField[i] = clamp01((frontField_[i] + ((frontField_[l] + frontField_[r] - (2.0f * frontField_[i])) * diffusion)) * fade);
    nextColor[i].r = clamp01((frontColor_[i].r + ((frontColor_[l].r + frontColor_[r].r - (2.0f * frontColor_[i].r)) * diffusion)) * fade);
    nextColor[i].g = clamp01((frontColor_[i].g + ((frontColor_[l].g + frontColor_[r].g - (2.0f * frontColor_[i].g)) * diffusion)) * fade);
    nextColor[i].b = clamp01((frontColor_[i].b + ((frontColor_[l].b + frontColor_[r].b - (2.0f * frontColor_[i].b)) * diffusion)) * fade);
    nextColor[i].w = clamp01((frontColor_[i].w + ((frontColor_[l].w + frontColor_[r].w - (2.0f * frontColor_[i].w)) * diffusion)) * fade);
  }
  const uint16_t injA = 7, injB = 29;
  const float input = chargeSignal_ * dtSec * 0.48f;
  nextField[injA] = clamp01(nextField[injA] + input + (rightImpulse.w * 0.18f));
  nextField[injB] = clamp01(nextField[injB] + input + (leftImpulse.w * 0.18f));
  nextColor[injA].r = clamp01(nextColor[injA].r + (rightImpulse.r * 0.42f)); nextColor[injA].g = clamp01(nextColor[injA].g + (rightImpulse.g * 0.42f)); nextColor[injA].b = clamp01(nextColor[injA].b + (rightImpulse.b * 0.42f)); nextColor[injA].w = clamp01(nextColor[injA].w + (rightImpulse.w * 0.42f));
  nextColor[injB].r = clamp01(nextColor[injB].r + (leftImpulse.r * 0.42f)); nextColor[injB].g = clamp01(nextColor[injB].g + (leftImpulse.g * 0.42f)); nextColor[injB].b = clamp01(nextColor[injB].b + (leftImpulse.b * 0.42f)); nextColor[injB].w = clamp01(nextColor[injB].w + (leftImpulse.w * 0.42f));
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) { frontField_[i] = nextField[i]; frontColor_[i] = nextColor[i]; }
}

void AnthuriumScene::updateRearRingField(float dtSec) {
  const float fade = expf(-dtSec / 6.0f);
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const uint16_t a = i % kFrontRingPixels;
    const uint16_t b = (i + 1) % kFrontRingPixels;
    rearLuma_[i] = clamp01((rearLuma_[i] * fade) + (((frontField_[a] * 0.7f) + (frontField_[b] * 0.3f)) * 0.35f));
    rearColor_[i].r = clamp01((rearColor_[i].r * fade) + (frontColor_[a].r * 0.035f));
    rearColor_[i].g = clamp01((rearColor_[i].g * fade) + (frontColor_[a].g * 0.035f));
    rearColor_[i].b = clamp01((rearColor_[i].b * fade) + (frontColor_[a].b * 0.035f));
    rearColor_[i].w = clamp01((rearColor_[i].w * fade) + (frontColor_[a].w * 0.035f));
  }
}

void AnthuriumScene::fadeColor(RgbwField& color, float fade) { color.r = clamp01(color.r * fade); color.g = clamp01(color.g * fade); color.b = clamp01(color.b * fade); color.w = clamp01(color.w * fade);} 
void AnthuriumScene::addColor(RgbwField& color, const Profiles::RgbwFloat& add, float amount) { color.r = clamp01(color.r + (add.r * amount)); color.g = clamp01(color.g + (add.g * amount)); color.b = clamp01(color.b + (add.b * amount)); color.w = clamp01(color.w + (add.w * amount)); }
float AnthuriumScene::clamp01(float v) { if (v < 0.0f) return 0.0f; if (v > 1.0f) return 1.0f; return v; }
uint8_t AnthuriumScene::toByte(float v) { return static_cast<uint8_t>(clamp01(v) * 255.0f); }
float AnthuriumScene::smoothToward(float current, float target, float riseAlpha, float fallAlpha) { const float alpha = target > current ? clamp01(riseAlpha) : clamp01(fallAlpha); return current + ((target - current) * alpha); }
