#include "AnthuriumScene.h"

#include <math.h>

#include "../config/Profiles.h"


AnthuriumScene::AnthuriumScene(PixelOutput& output) : output_(output) {}

void AnthuriumScene::begin() {
  initialized_ = true;
  lastNowMs_ = 0;
  ingressPhase_ = 0.0f;

  for (uint16_t i = 0; i < kRingPixels; ++i) {
    torus_[i] = 0.0f;
    ringLuma_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kStamenPixels; ++i) {
    leftLuma_[i] = 0.0f;
    rightLuma_[i] = 0.0f;
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

  float r = 1.0f;
  float g = 0.45f;
  float b = 0.25f;
  float w = 0.0f;
  phaseColor(track, r, g, b, w);

  for (uint16_t i = 0; i < Profiles::kRingPixels; ++i) {
    const float field = clamp01(profile.torusBaseField + torus_[i]);
    const float target = clamp01((field * track.continuity) + (track.charge * profile.torusInstantGain));
    const float alpha = target > ringLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    ringLuma_[i] = ringLuma_[i] + ((target - ringLuma_[i]) * alpha);

    output_.setRingPixel(i,
                         toByte(r * ringLuma_[i]),
                         toByte(g * ringLuma_[i]),
                         toByte(b * ringLuma_[i]),
                         toByte(w * field));
  }

  for (uint16_t i = 0; i < Profiles::kLeftStamenPixels; ++i) {
    const float ingress = sampleIngress(i, Profiles::kLeftStamenPixels, track);
    const float alpha = ingress > leftLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    leftLuma_[i] = leftLuma_[i] + ((ingress - leftLuma_[i]) * alpha);

    output_.setLeftStamenPixel(i,
                               toByte(r * leftLuma_[i]),
                               toByte(g * leftLuma_[i]),
                               toByte(b * leftLuma_[i]),
                               toByte(w * ingress));
  }

  for (uint16_t i = 0; i < Profiles::kRightStamenPixels; ++i) {
    const float ingress = sampleIngress(i, Profiles::kRightStamenPixels, track);
    const float alpha = ingress > rightLuma_[i] ? profile.lumaRiseAlpha : profile.lumaFallAlpha;
    rightLuma_[i] = rightLuma_[i] + ((ingress - rightLuma_[i]) * alpha);

    output_.setRightStamenPixel(i,
                                toByte(r * rightLuma_[i]),
                                toByte(g * rightLuma_[i]),
                                toByte(b * rightLuma_[i]),
                                toByte(w * ingress));
  }

  output_.show();
}

void AnthuriumScene::updateTorus(const StableTrack& track, float dtSec) {
  const auto& profile = Profiles::anthurium();

  float tmp[kRingPixels] = {0.0f};
  const float decay = expf(-profile.torusDecayPerSecond * dtSec);
  const float diffusion = profile.torusDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < Profiles::kRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (Profiles::kRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % Profiles::kRingPixels;

    float v = torus_[i];
    v += (torus_[left] + torus_[right] - (2.0f * torus_[i])) * diffusion;
    v *= decay;
    tmp[i] = clamp01(v);
  }

  const float input = track.charge * track.continuity * profile.torusAccumulationGain * dtSec;

  for (uint16_t i = 0; i < Profiles::kRingPixels; ++i) {
    float da = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(profile.ingressA)));
    float db = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(profile.ingressB)));
    if (da > (Profiles::kRingPixels * 0.5f)) {
      da = Profiles::kRingPixels - da;
    }
    if (db > (Profiles::kRingPixels * 0.5f)) {
      db = Profiles::kRingPixels - db;
    }

    tmp[i] = clamp01(tmp[i] + (input * (polynomialKernel(da, profile.ingressSpread) +
                                        polynomialKernel(db, profile.ingressSpread))));
  }

  for (uint16_t i = 0; i < Profiles::kRingPixels; ++i) {
    torus_[i] = tmp[i];
  }
}

void AnthuriumScene::phaseColor(const StableTrack& track, float& r, float& g, float& b, float& w) const {
  const auto& profile = Profiles::anthurium();
  const Profiles::RgbwFloat* color = &profile.idleColor;

  switch (track.phase) {
    case StableTrack::MotionPhase::Approach:
      color = &profile.approachColor;
      break;
    case StableTrack::MotionPhase::Still:
      color = &profile.stillColor;
      break;
    case StableTrack::MotionPhase::Retreat:
      color = &profile.retreatColor;
      break;
    case StableTrack::MotionPhase::None:
    default:
      color = &profile.idleColor;
      break;
  }

  r = color->r;
  g = color->g;
  b = color->b;
  w = color->w;
}

float AnthuriumScene::sampleIngress(uint16_t pixel, uint16_t count, const StableTrack& track) const {
  const float denom = (count > 1) ? static_cast<float>(count - 1) : 1.0f;
  const float position = static_cast<float>(pixel) / denom;
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
