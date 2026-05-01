#include "AnthuriumScene.h"

#include <Arduino.h>
#include <math.h>

namespace {
// These constants are intentionally copied from the known-good bench sketch
// bench/anthurium_lite_smoke_v3/anthurium_lite_smoke_v3.ino unless noted.
constexpr float kNearM = 0.20f;
constexpr float kFarM = 2.20f;

constexpr float kMotionStillMps = 0.045f;
constexpr float kMotionFullScaleMps = 0.35f;
constexpr float kMotionAlpha = 0.22f;

constexpr float kStillHue = 0.33f;
constexpr float kApproachHue = 0.01f;
constexpr float kRetreatHue = 0.60f;
constexpr float kNearWarmBias = 0.58f;
constexpr float kRetreatBias = 0.95f;

constexpr float kIngressTravelSeconds = 3.0f;
constexpr float kIngressSmoothingSec = 0.28f;
constexpr float kIngressConveyorWidth = 0.22f;
constexpr float kIngressFloorFromCharge = 0.42f;
constexpr float kTorusClearSeconds = 4.5f;
constexpr float kTorusDiffusionPerSecond = 0.34f;
constexpr float kReservoirInjectionGain = 0.48f;
constexpr float kTorusInstantGain = 0.08f;
constexpr float kTorusBaseFieldLevel = 0.06f;
constexpr float kStamenAmbientFloor = 0.05f;

constexpr float kChargeRiseAlpha = 0.24f;
constexpr float kChargeFallAlpha = 0.14f;
constexpr float kChargeDeadband = 0.012f;

constexpr float kBaseSaturation = 0.24f;
constexpr float kMotionSaturationBoost = 0.48f;
constexpr float kNearSaturationBoost = 0.12f;
constexpr float kBaseRgbLevel = 0.08f;
constexpr float kMotionRgbBoost = 0.10f;
constexpr float kNearRgbBoost = 0.14f;
constexpr float kWhiteBase = 0.015f;
constexpr float kWhiteChargeGain = 0.14f;
constexpr float kTorusWhiteGain = 0.28f;
constexpr float kStamenWhiteGain = 0.12f;

constexpr uint16_t kVirtualIngressA = 2;
constexpr uint16_t kVirtualIngressB = (45 / 2) + 2;  // 24, matching v3.
constexpr float kIngressSpreadPixels = 3.5f;
constexpr bool kTipAtHighIndex = true;

// Stage 7 display-only tuning.
// Reduce the Stage 6 RGB lift by about 30% to regain saturation through
// stained glass, and keep W even more restrained so warm/cool chroma is not
// washed into pastel/white.
constexpr float kVisibleRgbGain = 2.8f;
constexpr float kVisibleWhiteGain = 0.65f;

// Render-only safety net for no-target / fully-dark moments. This does not feed
// any reservoir or memory state, so it cannot create the bathtub/saturation
// problem. It fades in only after the computed ring has nearly disappeared and
// fades out quickly when real Anthurium signal returns.
constexpr float kIdleSafetyNetFadeInSec = 1.7f;
constexpr float kIdleSafetyNetFadeOutSec = 0.85f;
// The prior threshold was below the ring's visually-black luma range. Logs show
// all pixels can read inactive while mean luma is still around 0.008-0.010, so
// fade the net in progressively before the computed ring reaches absolute zero.
constexpr float kIdleSafetyNetDarkMean = 0.001f;
constexpr float kIdleSafetyNetFullMean = 0.0002f;
constexpr float kIdleSafetyNetWarmR = 0.026f;
constexpr float kIdleSafetyNetWarmG = 0.013f;
constexpr float kIdleSafetyNetWarmB = 0.004f;
constexpr float kIdleSafetyNetWarmW = 0.004f;
}  // namespace

AnthuriumScene::AnthuriumScene(PixelOutput& output) : output_(output) {}

void AnthuriumScene::begin() {
  initialized_ = true;
  lastNowMs_ = 0;

  hadRangeSample_ = false;
  prevAcceptedRangeM_ = 0.0f;
  motionSignal_ = 0.0f;
  smoothedCharge_ = 0.0f;
  stableCharge_ = 0.0f;
  smoothedIngressLevel_ = 0.0f;
  ingressConveyorPhase_ = 0.0f;
  displayHue_ = kStillHue;
  displaySat_ = kBaseSaturation;
  displayRgbLevel_ = kBaseRgbLevel;
  displayWhite_ = kWhiteBase;

  for (uint16_t i = 0; i < kVirtualRingPixels; ++i) {
    torusCharge_[i] = 0.0f;
    torusColor_[i] = makeColor();
    ringBrightness_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kVirtualLeftStamenPixels; ++i) {
    leftBrightness_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kVirtualRightStamenPixels; ++i) {
    rightBrightness_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    nativeFrontBrightness_[i] = 0.0f;
  }
  lastCompareLogMs_ = 0;
  lastCompareDumpMs_ = 0;
  idleSafetyNetLevel_ = 0.0f;
}

void AnthuriumScene::render(const StableTrack& track, uint32_t nowMs) {
  if (!initialized_) {
    begin();
  }

  uint32_t dtMs = 16;
  if (lastNowMs_ != 0) {
    dtMs = nowMs - lastNowMs_;
    if (dtMs < 1) dtMs = 1;
    if (dtMs > 200) dtMs = 200;
  }
  lastNowMs_ = nowMs;
  const float dtSec = static_cast<float>(dtMs) / 1000.0f;

  updateMotionSignal(track, dtSec);
  updateSmoothedScene(track, dtSec);
  updateTorus(dtSec);
  renderFrontRingCompat(dtSec, nowMs);
  clearInactiveSpans();
  output_.show();
}

void AnthuriumScene::updateMotionSignal(const StableTrack& track, float dtSec) {
  float rawMotion = 0.0f;

  if (track.hasTarget && track.continuity > 0.001f && dtSec > 0.0001f) {
    if (hadRangeSample_) {
      // Positive means approaching, matching the v3 sketch.
      const float velocityMps = (prevAcceptedRangeM_ - track.rangeM) / dtSec;
      if (absf(velocityMps) <= kMotionStillMps) {
        rawMotion = 0.0f;
      } else {
        rawMotion = clampSigned(velocityMps / kMotionFullScaleMps, -1.0f, 1.0f);
      }
    }
    prevAcceptedRangeM_ = track.rangeM;
    hadRangeSample_ = true;
  } else {
    hadRangeSample_ = false;
  }

  motionSignal_ = lerp(motionSignal_, rawMotion, kMotionAlpha);
  if (!track.hasTarget || track.continuity <= 0.001f) {
    motionSignal_ = lerp(motionSignal_, 0.0f, 0.06f);
  }
}

void AnthuriumScene::updateSmoothedScene(const StableTrack& track, float dtSec) {
  float targetHue = kStillHue;
  float targetSat = 0.0f;
  float targetRgbLevel = 0.0f;
  float targetWhite = 0.0f;
  float rawChargeTarget = 0.0f;
  float ingressTarget = 0.0f;

  if (track.hasTarget && track.continuity > 0.001f) {
    const float influence = clamp01(track.continuity);
    const float nearness = normalizeNearness(track.rangeM) * influence;
    const float approach = clamp01(maxf(0.0f, motionSignal_));
    const float retreat = clamp01(maxf(0.0f, -motionSignal_));
    const float motionAbs = clamp01(absf(motionSignal_));

    const float warm = clamp01((approach * 0.72f) + (nearness * kNearWarmBias));
    const float cool = clamp01(retreat * kRetreatBias);

    if (cool > warm) {
      targetHue = lerp(kStillHue, kRetreatHue, cool);
    } else {
      targetHue = lerp(kStillHue, kApproachHue, warm);
    }

    targetSat = clamp01(kBaseSaturation +
                        (motionAbs * kMotionSaturationBoost) +
                        (nearness * kNearSaturationBoost));
    targetRgbLevel = clamp01(kBaseRgbLevel +
                             (motionAbs * kMotionRgbBoost) +
                             (nearness * kNearRgbBoost));
    targetWhite = clamp01(kWhiteBase + (nearness * kWhiteChargeGain * 0.35f));
    rawChargeTarget = clamp01(nearness);
    ingressTarget = clamp01((motionAbs * 0.78f) + (rawChargeTarget * 0.26f));
  }

  const float chargeAlpha = rawChargeTarget >= smoothedCharge_ ? kChargeRiseAlpha : kChargeFallAlpha;
  smoothedCharge_ = clamp01(smoothedCharge_ + ((rawChargeTarget - smoothedCharge_) * chargeAlpha));
  stableCharge_ = applyDeadband(stableCharge_, smoothedCharge_, kChargeDeadband);
  stableCharge_ = clamp01(stableCharge_);

  const float ingressAlpha = emaAlphaApprox(dtSec, kIngressSmoothingSec);
  const float ingressGoal = clamp01(ingressTarget * (0.30f + (0.70f * stableCharge_)));
  smoothedIngressLevel_ = clamp01(smoothedIngressLevel_ +
                                  ((ingressGoal - smoothedIngressLevel_) * ingressAlpha));

  displayHue_ = lerp(displayHue_, targetHue, 0.20f);
  displaySat_ = lerp(displaySat_, targetSat, 0.18f);
  displayRgbLevel_ = lerp(displayRgbLevel_, targetRgbLevel, 0.18f);
  displayWhite_ = lerp(displayWhite_, targetWhite, 0.18f);

  const float travelSec = kIngressTravelSeconds < 0.01f ? 0.01f : kIngressTravelSeconds;
  ingressConveyorPhase_ += dtSec / travelSec;
  while (ingressConveyorPhase_ >= 1.0f) {
    ingressConveyorPhase_ -= 1.0f;
  }
}

void AnthuriumScene::updateTorus(float dtSec) {
  float nextCharge[kVirtualRingPixels];
  ColorF nextColor[kVirtualRingPixels];
  const float decay = decayApprox(dtSec, kTorusClearSeconds);
  const float diffusion = kTorusDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < kVirtualRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (kVirtualRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % kVirtualRingPixels;

    float charge = torusCharge_[i];
    charge += (torusCharge_[left] + torusCharge_[right] - (2.0f * torusCharge_[i])) * diffusion;
    charge *= decay;
    nextCharge[i] = clamp01(charge);

    const ColorF c = torusColor_[i];
    const ColorF l = torusColor_[left];
    const ColorF r = torusColor_[right];
    nextColor[i].r = clamp01((c.r + ((l.r + r.r - (2.0f * c.r)) * diffusion)) * decay);
    nextColor[i].g = clamp01((c.g + ((l.g + r.g - (2.0f * c.g)) * diffusion)) * decay);
    nextColor[i].b = clamp01((c.b + ((l.b + r.b - (2.0f * c.b)) * diffusion)) * decay);
    nextColor[i].w = clamp01((c.w + ((l.w + r.w - (2.0f * c.w)) * diffusion)) * decay);
  }

  const float torusInput = clamp01(stableCharge_) * dtSec * kReservoirInjectionGain;
  const ColorF injectColor = currentSceneColor(clamp01(0.65f + (stableCharge_ * 0.35f)));

  for (uint16_t i = 0; i < kVirtualRingPixels; ++i) {
    float distA = absf(static_cast<float>(static_cast<int>(i) - static_cast<int>(kVirtualIngressA)));
    float distB = absf(static_cast<float>(static_cast<int>(i) - static_cast<int>(kVirtualIngressB)));
    if (distA > (kVirtualRingPixels * 0.5f)) distA = kVirtualRingPixels - distA;
    if (distB > (kVirtualRingPixels * 0.5f)) distB = kVirtualRingPixels - distB;

    const float wA = polynomialKernel(distA, kIngressSpreadPixels * 2.0f);
    const float wB = polynomialKernel(distB, kIngressSpreadPixels * 2.0f);
    const float weight = clamp01(wA + wB);
    if (weight <= 0.0f) continue;

    nextCharge[i] = clamp01(nextCharge[i] + (torusInput * weight));
    nextColor[i].r = clamp01(nextColor[i].r + (injectColor.r * torusInput * weight));
    nextColor[i].g = clamp01(nextColor[i].g + (injectColor.g * torusInput * weight));
    nextColor[i].b = clamp01(nextColor[i].b + (injectColor.b * torusInput * weight));
    nextColor[i].w = clamp01(nextColor[i].w + (injectColor.w * torusInput * weight));
  }

  for (uint16_t i = 0; i < kVirtualRingPixels; ++i) {
    torusCharge_[i] = nextCharge[i];
    torusColor_[i] = nextColor[i];
  }
}

void AnthuriumScene::renderFrontRingCompat(float dtSec, uint32_t nowMs) {
  // Build the 32-pixel projected reference band from the old v3 virtual stamen outputs.
  // Stage 5 renders the direct native 44-pixel candidate, but keeps the projected
  // reference computed in parallel so Serial logs can still compare the two.
  ColorF source[kProjectedSourcePixels];

  for (uint16_t i = 0; i < kVirtualLeftStamenPixels; ++i) {
    source[i] = renderVirtualStamenPixel(i, kVirtualLeftStamenPixels, leftBrightness_, dtSec);
  }
  for (uint16_t i = 0; i < kVirtualRightStamenPixels; ++i) {
    source[kVirtualLeftStamenPixels + i] =
        renderVirtualStamenPixel(i, kVirtualRightStamenPixels, rightBrightness_, dtSec);
  }

  ColorF projected[kFrontRingPixels];
  ColorF native[kFrontRingPixels];
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    projected[i] = makeColor();
    native[i] = makeColor();
  }

  for (uint16_t dst = 0; dst < kFrontRingPixels; ++dst) {
    const float srcPos = (static_cast<float>(dst) * static_cast<float>(kProjectedSourcePixels)) /
                         static_cast<float>(kFrontRingPixels);
    const uint16_t src0 = static_cast<uint16_t>(srcPos) % kProjectedSourcePixels;
    const uint16_t src1 = (src0 + 1) % kProjectedSourcePixels;
    const float mix = srcPos - static_cast<float>(src0);
    const ColorF projectedColor = lerpColor(source[src0], source[src1], mix);
    const ColorF nativeColor = renderNativeFrontPixel(dst, dtSec);

    const uint16_t rotated = static_cast<uint16_t>((dst + kFrontRingProjectionRotation) % kFrontRingPixels);
    projected[rotated] = projectedColor;
    native[rotated] = nativeColor;
  }

  float nativeMean = 0.0f;
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    nativeMean += colorLuma(native[i]);
  }
  nativeMean /= static_cast<float>(kFrontRingPixels);

  const float idleTarget = clamp01((kIdleSafetyNetDarkMean - nativeMean) /
                                   (kIdleSafetyNetDarkMean - kIdleSafetyNetFullMean));
  const float idleTau = idleTarget > idleSafetyNetLevel_ ?
      kIdleSafetyNetFadeInSec : kIdleSafetyNetFadeOutSec;
  idleSafetyNetLevel_ += (idleTarget - idleSafetyNetLevel_) * emaAlphaApprox(dtSec, idleTau);
  idleSafetyNetLevel_ = clamp01(idleSafetyNetLevel_);

  for (uint16_t physicalFrontPixel = 0; physicalFrontPixel < kFrontRingPixels; ++physicalFrontPixel) {
    ColorF visible = native[physicalFrontPixel];

    if (idleSafetyNetLevel_ > 0.001f) {
      const float t = static_cast<float>(nowMs) * 0.0013f;
      const float shimmerA = 0.5f + (0.5f * sinf(t + (static_cast<float>(physicalFrontPixel) * 0.73f)));
      const float shimmerB = 0.5f + (0.5f * sinf((t * 0.47f) + (static_cast<float>(physicalFrontPixel) * 1.37f)));
      const float shimmer = 0.58f + (0.28f * shimmerA) + (0.14f * shimmerB);
      const float amount = idleSafetyNetLevel_ * shimmer;
      visible.r = clamp01(visible.r + (kIdleSafetyNetWarmR * amount));
      visible.g = clamp01(visible.g + (kIdleSafetyNetWarmG * amount));
      visible.b = clamp01(visible.b + (kIdleSafetyNetWarmB * amount));
      visible.w = clamp01(visible.w + (kIdleSafetyNetWarmW * amount));
    }

    output_.setFrontRingPixel(physicalFrontPixel,
                              toByte(visible.r * kVisibleRgbGain),
                              toByte(visible.g * kVisibleRgbGain),
                              toByte(visible.b * kVisibleRgbGain),
                              toByte(visible.w * kVisibleWhiteGain));
  }

  maybeLogProjectionComparison(projected, native, nowMs);
  maybeDumpProjectionArrays(projected, native, nowMs);
}

AnthuriumScene::ColorF AnthuriumScene::renderNativeFrontPixel(uint16_t logicalPixel, float dtSec) {
  const float ingress = sampleNativeFrontIngress(logicalPixel);
  const float targetBrightness = clamp01((ingress * smoothedIngressLevel_) +
                                         (kStamenAmbientFloor * maxf(0.25f, stableCharge_)));
  float smoothed = nativeFrontBrightness_[logicalPixel] +
                   ((targetBrightness - nativeFrontBrightness_[logicalPixel]) * 0.22f);
  smoothed = applyDeadband(nativeFrontBrightness_[logicalPixel], smoothed, 0.015f);
  nativeFrontBrightness_[logicalPixel] = applyBrightnessSlew(nativeFrontBrightness_[logicalPixel], smoothed, dtSec);

  ColorF color = currentSceneColor(nativeFrontBrightness_[logicalPixel]);
  color.w = clamp01(color.w + (((color.r + color.g + color.b) * 0.333f) * kStamenWhiteGain));
  return color;
}

AnthuriumScene::ColorF AnthuriumScene::renderVirtualRingPixel(uint16_t ringPixel, float dtSec) {
  const float field = sampleTorusField(ringPixel);
  const float targetBrightness = clamp01((field * maxf(0.22f, stableCharge_)) +
                                         (stableCharge_ * kTorusInstantGain));
  float smoothed = ringBrightness_[ringPixel] + ((targetBrightness - ringBrightness_[ringPixel]) * 0.24f);
  smoothed = applyDeadband(ringBrightness_[ringPixel], smoothed, 0.015f);
  ringBrightness_[ringPixel] = applyBrightnessSlew(ringBrightness_[ringPixel], smoothed, dtSec);

  ColorF c = scaleColor(torusColor_[ringPixel], clamp01(0.28f + ringBrightness_[ringPixel]));
  const float avg = (c.r + c.g + c.b) * 0.333f;
  c.w = clamp01(c.w + (avg * kTorusWhiteGain) + (torusCharge_[ringPixel] * 0.06f));
  return c;
}

AnthuriumScene::ColorF AnthuriumScene::renderVirtualStamenPixel(uint16_t stamenPixel,
                                                                uint16_t stamenCount,
                                                                float* brightnessState,
                                                                float dtSec) {
  const float ingress = sampleStamenIngress(stamenPixel, stamenCount);
  const float targetBrightness = clamp01((ingress * smoothedIngressLevel_) +
                                         (kStamenAmbientFloor * maxf(0.25f, stableCharge_)));
  float smoothed = brightnessState[stamenPixel] + ((targetBrightness - brightnessState[stamenPixel]) * 0.22f);
  smoothed = applyDeadband(brightnessState[stamenPixel], smoothed, 0.015f);
  brightnessState[stamenPixel] = applyBrightnessSlew(brightnessState[stamenPixel], smoothed, dtSec);

  ColorF c = currentSceneColor(brightnessState[stamenPixel]);
  c.w = clamp01(c.w + (((c.r + c.g + c.b) * 0.333f) * kStamenWhiteGain));
  return c;
}

void AnthuriumScene::clearInactiveSpans() {
  for (uint16_t i = 0; i < kRightJPixels; ++i) output_.setRightJPixel(i, 0, 0, 0, 0);
  for (uint16_t i = 0; i < kLeftJPixels; ++i) output_.setLeftJPixel(i, 0, 0, 0, 0);
  for (uint16_t i = 0; i < kRearRingPixels; ++i) output_.setRearRingPixel(i, 0, 0, 0, 0);
}

float AnthuriumScene::sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) const {
  if (stamenCount == 0) return 0.0f;
  const float denom = (stamenCount > 1) ? static_cast<float>(stamenCount - 1) : 1.0f;
  const float stamenPos = static_cast<float>(stamenPixel) / denom;
  const float tipToEntry = kTipAtHighIndex ? (1.0f - stamenPos) : stamenPos;
  float delta = absf(tipToEntry - ingressConveyorPhase_);
  if (delta > 0.5f) delta = 1.0f - delta;

  const float moving = polynomialKernel(delta, kIngressConveyorWidth);
  const float floor = stableCharge_ * kIngressFloorFromCharge;
  return clamp01(floor + (moving * smoothedIngressLevel_));
}

float AnthuriumScene::sampleNativeFrontIngress(uint16_t logicalPixel) const {
  // Shadow candidate: compute the same conveyor directly over the 44-pixel
  // FrontRing as two 22-cell halves, instead of stretching the 32-pixel virtual
  // source band. This is not rendered; it is only compared in Serial logs.
  const uint16_t halfPixels = kFrontRingPixels / 2;
  const uint16_t withinHalf = logicalPixel < halfPixels ? logicalPixel : logicalPixel - halfPixels;
  const float denom = halfPixels > 1 ? static_cast<float>(halfPixels - 1) : 1.0f;
  const float pos = static_cast<float>(withinHalf) / denom;
  const float tipToEntry = kTipAtHighIndex ? (1.0f - pos) : pos;
  float delta = absf(tipToEntry - ingressConveyorPhase_);
  if (delta > 0.5f) delta = 1.0f - delta;

  const float moving = polynomialKernel(delta, kIngressConveyorWidth);
  const float floor = stableCharge_ * kIngressFloorFromCharge;
  return clamp01(floor + (moving * smoothedIngressLevel_));
}

void AnthuriumScene::maybeLogProjectionComparison(const ColorF* projected, const ColorF* native, uint32_t nowMs) {
  constexpr uint32_t kCompareLogMs = 1000;
  if (lastCompareLogMs_ != 0 && (nowMs - lastCompareLogMs_) < kCompareLogMs) return;
  lastCompareLogMs_ = nowMs;

  float pMean = 0.0f;
  float nMean = 0.0f;
  float pMax = 0.0f;
  float nMax = 0.0f;
  float pSat = 0.0f;
  float nSat = 0.0f;
  float pWhite = 0.0f;
  float nWhite = 0.0f;
  float diffMean = 0.0f;
  float diffMax = 0.0f;
  uint8_t pActive = 0;
  uint8_t nActive = 0;

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    const float pl = colorLuma(projected[i]);
    const float nl = colorLuma(native[i]);
    const float d = absf(pl - nl);
    pMean += pl;
    nMean += nl;
    pSat += colorSaturation(projected[i]);
    nSat += colorSaturation(native[i]);
    pWhite += projected[i].w;
    nWhite += native[i].w;
    if (pl > pMax) pMax = pl;
    if (nl > nMax) nMax = nl;
    if (d > diffMax) diffMax = d;
    diffMean += d;
    if (pl > 0.012f) ++pActive;
    if (nl > 0.012f) ++nActive;
  }

  const float denom = static_cast<float>(kFrontRingPixels);
  pMean /= denom;
  nMean /= denom;
  pSat /= denom;
  nSat /= denom;
  pWhite /= denom;
  nWhite /= denom;
  diffMean /= denom;

  Serial.print("shadow44 motion="); Serial.print(motionSignal_, 3);
  Serial.print(" charge="); Serial.print(stableCharge_, 3);
  Serial.print(" ingress="); Serial.print(smoothedIngressLevel_, 3);
  Serial.print(" phase_f="); Serial.print(ingressConveyorPhase_, 3);
  Serial.print(" hue="); Serial.print(displayHue_, 3);
  Serial.print(" sat="); Serial.print(displaySat_, 3);
  Serial.print(" lvl="); Serial.print(displayRgbLevel_, 3);
  Serial.print(" w="); Serial.print(displayWhite_, 3);
  Serial.print(" idleNet="); Serial.print(idleSafetyNetLevel_, 3);
  Serial.print(" pAct="); Serial.print(pActive);
  Serial.print(" nAct="); Serial.print(nActive);
  Serial.print(" pMean="); Serial.print(pMean, 3);
  Serial.print(" nMean="); Serial.print(nMean, 3);
  Serial.print(" pMax="); Serial.print(pMax, 3);
  Serial.print(" nMax="); Serial.print(nMax, 3);
  Serial.print(" pSat="); Serial.print(pSat, 3);
  Serial.print(" nSat="); Serial.print(nSat, 3);
  Serial.print(" pW="); Serial.print(pWhite, 3);
  Serial.print(" nW="); Serial.print(nWhite, 3);
  Serial.print(" dMean="); Serial.print(diffMean, 3);
  Serial.print(" dMax="); Serial.println(diffMax, 3);
}

void AnthuriumScene::maybeDumpProjectionArrays(const ColorF* projected, const ColorF* native, uint32_t nowMs) {
  constexpr uint32_t kDumpMs = 5000;
  if (lastCompareDumpMs_ != 0 && (nowMs - lastCompareDumpMs_) < kDumpMs) return;
  lastCompareDumpMs_ = nowMs;

  Serial.print("shadow44_proj_luma=");
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    if (i > 0) Serial.print(',');
    Serial.print(colorLuma(projected[i]), 2);
  }
  Serial.println();

  Serial.print("shadow44_native_luma=");
  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    if (i > 0) Serial.print(',');
    Serial.print(colorLuma(native[i]), 2);
  }
  Serial.println();
}

float AnthuriumScene::sampleTorusField(uint16_t ringPixel) const {
  return clamp01(kTorusBaseFieldLevel + torusCharge_[ringPixel]);
}

AnthuriumScene::ColorF AnthuriumScene::currentSceneColor(float brightnessScale) const {
  return hsvColor(displayHue_, displaySat_, displayRgbLevel_ * clamp01(brightnessScale), displayWhite_ * clamp01(brightnessScale));
}

AnthuriumScene::ColorF AnthuriumScene::makeColor(float r, float g, float b, float w) {
  ColorF c;
  c.r = r;
  c.g = g;
  c.b = b;
  c.w = w;
  return c;
}

AnthuriumScene::ColorF AnthuriumScene::scaleColor(const ColorF& color, float scale) {
  return makeColor(clamp01(color.r * scale), clamp01(color.g * scale), clamp01(color.b * scale), clamp01(color.w * scale));
}

AnthuriumScene::ColorF AnthuriumScene::lerpColor(const ColorF& a, const ColorF& b, float t) {
  const float m = clamp01(t);
  return makeColor(lerp(a.r, b.r, m),
                   lerp(a.g, b.g, m),
                   lerp(a.b, b.b, m),
                   lerp(a.w, b.w, m));
}

AnthuriumScene::ColorF AnthuriumScene::hsvColor(float hue, float sat, float val, float white) {
  float h = hue;
  while (h < 0.0f) h += 1.0f;
  while (h >= 1.0f) h -= 1.0f;

  const float s = clamp01(sat);
  const float v = clamp01(val);
  const float scaled = h * 6.0f;
  const int sector = static_cast<int>(scaled);
  const float f = scaled - sector;
  const float p = v * (1.0f - s);
  const float q = v * (1.0f - s * f);
  const float t = v * (1.0f - s * (1.0f - f));

  float rf = v;
  float gf = t;
  float bf = p;
  switch (sector % 6) {
    case 0: rf = v; gf = t; bf = p; break;
    case 1: rf = q; gf = v; bf = p; break;
    case 2: rf = p; gf = v; bf = t; break;
    case 3: rf = p; gf = q; bf = v; break;
    case 4: rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;
  }

  return makeColor(clamp01(rf), clamp01(gf), clamp01(bf), clamp01(white));
}

float AnthuriumScene::normalizeNearness(float rangeM) {
  const float span = kFarM - kNearM;
  if (span <= 0.001f) return 0.0f;
  return 1.0f - clamp01((rangeM - kNearM) / span);
}

float AnthuriumScene::emaAlphaApprox(float dtSec, float tauSec) {
  if (tauSec <= 0.001f) return 1.0f;
  if (dtSec <= 0.0f) return 0.0f;
  return clamp01(dtSec / (tauSec + dtSec));
}

float AnthuriumScene::decayApprox(float dtSec, float clearSec) {
  if (clearSec <= 0.001f) return 0.0f;
  return clamp01(clearSec / (clearSec + dtSec));
}

float AnthuriumScene::applyDeadband(float previous, float target, float threshold) {
  if (absf(target - previous) <= threshold) return previous;
  return target;
}

float AnthuriumScene::applyBrightnessSlew(float previous, float target, float dtSec) {
  const float maxStep = 2.10f * dtSec;
  if (target > previous + maxStep) return previous + maxStep;
  if (target < previous - maxStep) return previous - maxStep;
  return target;
}

float AnthuriumScene::polynomialKernel(float distance, float width) {
  const float safeWidth = (width < 0.001f) ? 0.001f : width;
  const float x = clamp01(1.0f - (distance / safeWidth));
  return x * x;
}

float AnthuriumScene::clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

float AnthuriumScene::clampSigned(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

float AnthuriumScene::lerp(float a, float b, float t) {
  return a + ((b - a) * t);
}

float AnthuriumScene::absf(float value) {
  return value < 0.0f ? -value : value;
}

float AnthuriumScene::maxf(float a, float b) {
  return a > b ? a : b;
}

float AnthuriumScene::minf(float a, float b) {
  return a < b ? a : b;
}

float AnthuriumScene::colorLuma(const ColorF& color) {
  return clamp01((color.r * 0.30f) + (color.g * 0.59f) + (color.b * 0.11f) + color.w);
}

float AnthuriumScene::colorSaturation(const ColorF& color) {
  const float mx = maxf(maxf(color.r, color.g), color.b);
  if (mx <= 0.001f) return 0.0f;
  const float mn = minf(minf(color.r, color.g), color.b);
  return clamp01((mx - mn) / mx);
}

uint8_t AnthuriumScene::toByte(float value) {
  return static_cast<uint8_t>(clamp01(value) * 255.0f);
}
