#include "AnthuriumScene.h"

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
constexpr float kStamenWhiteGain = 0.12f;

constexpr bool kTipAtHighIndex = true;
constexpr uint16_t kFrontRingProjectionRotation = 11;

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
constexpr float kIdleSafetyNetDarkMean = 0.010f;
constexpr float kIdleSafetyNetFullMean = 0.0025f;
constexpr float kIdleSafetyNetWarmR = 0.026f;
constexpr float kIdleSafetyNetWarmG = 0.013f;
constexpr float kIdleSafetyNetWarmB = 0.004f;
constexpr float kIdleSafetyNetWarmW = 0.004f;

// Phase 3: V4-style J/spadix travelling-wave render. Keep these deliberately
// separate from the front-ring native brightness array so lighting
// the physical J spans does not perturb the proven front-ring behavior.
constexpr float kJIngressFloorFromCharge = 0.42f;
constexpr float kJAmbientFloor = 0.090f;
constexpr float kJOutputScale = 1.65f;
constexpr float kJWhiteGain = 0.12f;
constexpr float kJVisibleRgbGain = 2.4f;
constexpr float kJVisibleWhiteGain = 0.75f;
constexpr float kJIdleSafetyNetWarmR = 0.018f;
constexpr float kJIdleSafetyNetWarmG = 0.009f;
constexpr float kJIdleSafetyNetWarmB = 0.003f;
constexpr float kJIdleSafetyNetWarmW = 0.003f;
constexpr bool kLeftJTipAtHighIndex = false;
constexpr bool kRightJTipAtHighIndex = false;
constexpr float kLeftJPhaseOffset = 0.00f;
constexpr float kRightJPhaseOffset = 0.035f;

// Phase 4: render the V4 front-ring reservoir language onto the physical
// RearRing as a soft wall-wash. These constants intentionally borrow the
// V4 front-reservoir plumbing, but the drive color is crossfaded more slowly
// so the wall does not snap from red to blue.
constexpr float kRearWallColorCrossfadeSec = 1.55f;
constexpr float kRearWallClearSeconds = 5.2f;
constexpr float kRearWallColorClearSeconds = 4.4f;
constexpr float kRearWallDiffusionPerSecond = 0.60f;
constexpr float kRearWallColorDiffusionPerSecond = 0.70f;
constexpr float kRearWallInjectionGain = 0.35f;
constexpr float kRearWallInstantGain = 0.030f;
constexpr float kRearWallBaseFieldLevel = 0.060f;
constexpr float kRearWallIngressSpreadPixels = 13.5f;
constexpr float kRearWallOutputScale = 0.92f;
constexpr float kRearWallWhiteGain = 0.080f;
constexpr float kRearWallMoodWashGain = 0.020f;
constexpr float kRearWallMaxPixelAddPerFrame = 0.010f;
constexpr float kRearWallColorFlushPerAdd = 1.65f;
constexpr float kRearWallAnchorA = 43.5f;
constexpr float kRearWallAnchorB = 21.5f;
constexpr bool kRearWallReverseOutput = true;

// Experimental rear horizon counter-color. When enabled, the lower/6-o'clock
// rear-wall inlet uses a hue-rotated companion color while the upper/noon inlet
// keeps the normal scene color. Set false to return to the symmetric rear wash.

constexpr float kRearWallIdleRgbLevel = 0.0025f;
constexpr float kRearWallIdleWhiteLevel = 0.0035f;

// Render-only rear dropout safety net. This mirrors the front-ring net: it is
// a separate low warm shimmer that fades in only when the rear reservoir/mood
// output has nearly disappeared. It never feeds reservoir memory, so it cannot
// create the bathtub/whiteout failure mode.
constexpr float kRearIdleSafetyNetFadeInSec = 2.3f;
constexpr float kRearIdleSafetyNetFadeOutSec = 1.05f;
constexpr float kRearIdleSafetyNetDarkMean = 0.010f;
constexpr float kRearIdleSafetyNetFullMean = 0.0025f;
constexpr float kRearIdleSafetyNetWarmR = 0.026f;
constexpr float kRearIdleSafetyNetWarmG = 0.013f;
constexpr float kRearIdleSafetyNetWarmB = 0.004f;
constexpr float kRearIdleSafetyNetWarmW = 0.004f;
constexpr float kRearWallVisibleRgbGain = 2.05f;
constexpr float kRearWallVisibleWhiteGain = 0.70f;
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

  for (uint16_t i = 0; i < kFrontRingPixels; ++i) {
    nativeFrontBrightness_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    physicalLeftBrightness_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    physicalRightBrightness_[i] = 0.0f;
  }
  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    rearReservoirCharge_[i] = 0.0f;
    rearReservoirColor_[i] = makeColor();
    rearReservoirBrightness_[i] = 0.0f;
  }
  rearDriveColor_ = currentSceneColor(0.0f);
  rearDriveWhite_ = 0.0f;
  rearIdleSafetyNetLevel_ = 0.0f;
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
  updateRearDriveColor(dtSec);
  updateRearRingReservoir(dtSec);
  renderFrontRingCompat(dtSec, nowMs);
  renderJSpans(dtSec);
  renderRearRing(dtSec, nowMs);
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


void AnthuriumScene::renderFrontRingCompat(float dtSec, uint32_t nowMs) {
  float nativeMean = 0.0f;
  ColorF native[kFrontRingPixels];

  for (uint16_t dst = 0; dst < kFrontRingPixels; ++dst) {
    const uint16_t rotated = static_cast<uint16_t>((dst + kFrontRingProjectionRotation) % kFrontRingPixels);
    native[rotated] = renderNativeFrontPixel(dst, dtSec);
  }

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



void AnthuriumScene::renderJSpans(float dtSec) {
  const float jIdleNet = maxf(idleSafetyNetLevel_, rearIdleSafetyNetLevel_);

  auto applyJIdleNet = [&](ColorF color, uint16_t pixelIndex) {
    if (jIdleNet <= 0.001f) return color;

    const float phase = ingressConveyorPhase_ + (static_cast<float>(pixelIndex) * 0.17f);
    const float shimmer = 0.72f + (0.18f * sinf(phase * 6.28318f)) +
                          (0.10f * sinf((phase * 3.17f) + 0.61f));
    const float amount = clamp01(jIdleNet * shimmer);
    color.r = clamp01(color.r + (kJIdleSafetyNetWarmR * amount));
    color.g = clamp01(color.g + (kJIdleSafetyNetWarmG * amount));
    color.b = clamp01(color.b + (kJIdleSafetyNetWarmB * amount));
    color.w = clamp01(color.w + (kJIdleSafetyNetWarmW * amount));
    return color;
  };

  for (uint16_t i = 0; i < kRightJPixels; ++i) {
    ColorF color = renderJPixel(i, kRightJPixels, physicalRightBrightness_,
                                kRightJTipAtHighIndex, kRightJPhaseOffset, dtSec);
    color = applyJIdleNet(color, i);
    output_.setRightJPixel(i,
                           toByte(color.r * kJVisibleRgbGain),
                           toByte(color.g * kJVisibleRgbGain),
                           toByte(color.b * kJVisibleRgbGain),
                           toByte(color.w * kJVisibleWhiteGain));
  }

  for (uint16_t i = 0; i < kLeftJPixels; ++i) {
    ColorF color = renderJPixel(i, kLeftJPixels, physicalLeftBrightness_,
                                kLeftJTipAtHighIndex, kLeftJPhaseOffset, dtSec);
    color = applyJIdleNet(color, static_cast<uint16_t>(i + kRightJPixels));
    output_.setLeftJPixel(i,
                          toByte(color.r * kJVisibleRgbGain),
                          toByte(color.g * kJVisibleRgbGain),
                          toByte(color.b * kJVisibleRgbGain),
                          toByte(color.w * kJVisibleWhiteGain));
  }
}

AnthuriumScene::ColorF AnthuriumScene::renderJPixel(uint16_t logicalPixel,
                                                    uint16_t pixelCount,
                                                    float* brightnessState,
                                                    bool tipAtHighIndex,
                                                    float phaseOffset,
                                                    float dtSec) {
  const float ingress = sampleJIngress(logicalPixel, pixelCount, tipAtHighIndex, phaseOffset);
  const float targetBrightness = clamp01(
      (ingress * (0.45f + (0.55f * smoothedIngressLevel_))) +
      (kJAmbientFloor * maxf(0.25f, stableCharge_)));

  float smoothed = brightnessState[logicalPixel] +
                   ((targetBrightness - brightnessState[logicalPixel]) * 0.22f);
  smoothed = applyDeadband(brightnessState[logicalPixel], smoothed, 0.015f);
  brightnessState[logicalPixel] = applyBrightnessSlew(brightnessState[logicalPixel], smoothed, dtSec);

  ColorF color = currentSceneColor(clamp01(brightnessState[logicalPixel] * kJOutputScale));
  const float avg = (color.r + color.g + color.b) * 0.333f;
  color.w = clamp01(color.w + (avg * kJWhiteGain));
  return color;
}

void AnthuriumScene::updateRearDriveColor(float dtSec) {
  // Crossfade the wall-wash drive in RGBW space. This deliberately avoids
  // immediate hue snapping; red-to-blue transitions pass through a soft violet
  // rather than flipping the whole rear wash in one frame.
  ColorF target = currentSceneColor(clamp01(0.72f + (stableCharge_ * 0.28f)));

  // Store color energy, not haze. White is kept as a slow render-only lift.
  const float targetWhite = clamp01(target.w * 0.25f);
  target.w = 0.0f;

  const float a = emaAlphaApprox(dtSec, kRearWallColorCrossfadeSec);
  rearDriveColor_ = lerpColor(rearDriveColor_, target, a);
  rearDriveWhite_ = lerp(rearDriveWhite_, targetWhite, a);
}

void AnthuriumScene::updateRearRingReservoir(float dtSec) {
  ColorF nextColor[kRearRingPixels];
  float nextCharge[kRearRingPixels];
  const float chargeDecay = decayApprox(dtSec, kRearWallClearSeconds);
  const float colorDecay = decayApprox(dtSec, kRearWallColorClearSeconds);
  const float chargeDiffusion = kRearWallDiffusionPerSecond * dtSec;
  const float colorDiffusion = kRearWallColorDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const uint16_t left = (i == 0) ? (kRearRingPixels - 1) : (i - 1);
    const uint16_t right = (i + 1) % kRearRingPixels;

    float charge = rearReservoirCharge_[i];
    charge += (rearReservoirCharge_[left] + rearReservoirCharge_[right] -
               (2.0f * rearReservoirCharge_[i])) * chargeDiffusion;
    nextCharge[i] = clamp01(charge * chargeDecay);

    const ColorF c = rearReservoirColor_[i];
    const ColorF l = rearReservoirColor_[left];
    const ColorF r = rearReservoirColor_[right];
    nextColor[i].r = clamp01((c.r + ((l.r + r.r - (2.0f * c.r)) * colorDiffusion)) * colorDecay);
    nextColor[i].g = clamp01((c.g + ((l.g + r.g - (2.0f * c.g)) * colorDiffusion)) * colorDecay);
    nextColor[i].b = clamp01((c.b + ((l.b + r.b - (2.0f * c.b)) * colorDiffusion)) * colorDecay);
    nextColor[i].w = clamp01((c.w + ((l.w + r.w - (2.0f * c.w)) * colorDiffusion)) * colorDecay);
  }

  const float input = clamp01(stableCharge_) * dtSec * kRearWallInjectionGain;
  if (input > 0.0f) {
    for (uint16_t i = 0; i < kRearRingPixels; ++i) {
      const float distA = circularDistance(static_cast<float>(i), kRearWallAnchorA, static_cast<float>(kRearRingPixels));
      const float distB = circularDistance(static_cast<float>(i), kRearWallAnchorB, static_cast<float>(kRearRingPixels));
      const float weightA = polynomialKernel(distA, kRearWallIngressSpreadPixels);
      const float weightB = polynomialKernel(distB, kRearWallIngressSpreadPixels);
      const float weight = clamp01(weightA + weightB);
      if (weight <= 0.0f) continue;

      const ColorF drive = rearDriveColor_;

      const float add = minf(input * weight, kRearWallMaxPixelAddPerFrame);
      const float flush = clamp01(add * kRearWallColorFlushPerAdd);
      nextColor[i].r *= (1.0f - flush);
      nextColor[i].g *= (1.0f - flush);
      nextColor[i].b *= (1.0f - flush);
      nextColor[i].w *= (1.0f - flush);

      nextCharge[i] = clamp01(nextCharge[i] + add);
      nextColor[i].r = clamp01(nextColor[i].r + (drive.r * add));
      nextColor[i].g = clamp01(nextColor[i].g + (drive.g * add));
      nextColor[i].b = clamp01(nextColor[i].b + (drive.b * add));
      nextColor[i].w = clamp01(nextColor[i].w + (drive.w * add));
    }
  }

  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    rearReservoirCharge_[i] = nextCharge[i];
    rearReservoirColor_[i] = nextColor[i];
  }
}

void AnthuriumScene::renderRearRing(float dtSec, uint32_t nowMs) {
  ColorF visible[kRearRingPixels];
  float activeMean = 0.0f;

  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    const float field = clamp01(kRearWallBaseFieldLevel + rearReservoirCharge_[i]);
    const float targetBrightness = clamp01((field * maxf(0.16f, stableCharge_)) +
                                           (stableCharge_ * kRearWallInstantGain));
    float smoothed = rearReservoirBrightness_[i] +
                     ((targetBrightness - rearReservoirBrightness_[i]) * 0.16f);
    smoothed = applyDeadband(rearReservoirBrightness_[i], smoothed, 0.012f);
    rearReservoirBrightness_[i] = applyBrightnessSlew(rearReservoirBrightness_[i], smoothed, dtSec);

    ColorF memory = scaleColor(rearReservoirColor_[i],
                               kRearWallOutputScale * clamp01(0.30f + rearReservoirBrightness_[i]));
    const float avg = (memory.r + memory.g + memory.b) * 0.333f;
    memory.w = clamp01((memory.w * 0.25f) +
                       (avg * kRearWallWhiteGain) +
                       (rearReservoirCharge_[i] * 0.006f * kRearWallOutputScale) +
                       (rearDriveWhite_ * 0.10f));

    // Render-only mood wash. This keeps the wall wash alive in low-signal
    // moments, but it does not feed memory and therefore cannot overfill the
    // reservoir or desaturate old color into ghost-white.
    ColorF mood = scaleColor(rearDriveColor_, kRearWallMoodWashGain * (0.20f + stableCharge_));
    mood.w = rearDriveWhite_ * 0.10f * (0.25f + stableCharge_);

    const ColorF active = makeColor(clamp01(mood.r + memory.r),
                                    clamp01(mood.g + memory.g),
                                    clamp01(mood.b + memory.b),
                                    clamp01(mood.w + memory.w));
    activeMean += colorLuma(active);

    const ColorF idle = hsvColor(kStillHue, 0.035f, kRearWallIdleRgbLevel, kRearWallIdleWhiteLevel);
    visible[i] = makeColor(clamp01(idle.r + active.r),
                           clamp01(idle.g + active.g),
                           clamp01(idle.b + active.b),
                           clamp01(idle.w + active.w));
  }

  activeMean /= static_cast<float>(kRearRingPixels);
  const float idleTarget = clamp01((kRearIdleSafetyNetDarkMean - activeMean) /
                                   (kRearIdleSafetyNetDarkMean - kRearIdleSafetyNetFullMean));
  const float idleTau = idleTarget > rearIdleSafetyNetLevel_ ?
      kRearIdleSafetyNetFadeInSec : kRearIdleSafetyNetFadeOutSec;
  rearIdleSafetyNetLevel_ += (idleTarget - rearIdleSafetyNetLevel_) * emaAlphaApprox(dtSec, idleTau);
  rearIdleSafetyNetLevel_ = clamp01(rearIdleSafetyNetLevel_);

  for (uint16_t i = 0; i < kRearRingPixels; ++i) {
    ColorF out = visible[i];

    if (rearIdleSafetyNetLevel_ > 0.001f) {
      const float t = static_cast<float>(nowMs) * 0.0011f;
      const float shimmerA = 0.5f + (0.5f * sinf(t + (static_cast<float>(i) * 0.61f)));
      const float shimmerB = 0.5f + (0.5f * sinf((t * 0.43f) + (static_cast<float>(i) * 1.19f)));
      const float shimmer = 0.60f + (0.25f * shimmerA) + (0.15f * shimmerB);
      const float amount = rearIdleSafetyNetLevel_ * shimmer;
      out.r = clamp01(out.r + (kRearIdleSafetyNetWarmR * amount));
      out.g = clamp01(out.g + (kRearIdleSafetyNetWarmG * amount));
      out.b = clamp01(out.b + (kRearIdleSafetyNetWarmB * amount));
      out.w = clamp01(out.w + (kRearIdleSafetyNetWarmW * amount));
    }

    const uint16_t logical = kRearWallReverseOutput ?
        static_cast<uint16_t>((kRearRingPixels - 1) - i) : i;
    output_.setRearRingPixel(logical,
                             toByte(out.r * kRearWallVisibleRgbGain),
                             toByte(out.g * kRearWallVisibleRgbGain),
                             toByte(out.b * kRearWallVisibleRgbGain),
                             toByte(out.w * kRearWallVisibleWhiteGain));
  }
}

float AnthuriumScene::sampleJIngress(uint16_t logicalPixel, uint16_t pixelCount,
                                      bool tipAtHighIndex, float phaseOffset) const {
  if (pixelCount == 0) return 0.0f;

  const float denom = (pixelCount > 1) ? static_cast<float>(pixelCount - 1) : 1.0f;
  const float pos = static_cast<float>(logicalPixel) / denom;
  const float tipToEntry = tipAtHighIndex ? (1.0f - pos) : pos;

  float phase = ingressConveyorPhase_ + phaseOffset;
  while (phase < 0.0f) phase += 1.0f;
  while (phase >= 1.0f) phase -= 1.0f;

  float delta = absf(tipToEntry - phase);
  if (delta > 0.5f) delta = 1.0f - delta;

  const float moving = polynomialKernel(delta, kIngressConveyorWidth);
  const float floor = stableCharge_ * kJIngressFloorFromCharge;
  return clamp01(floor + (moving * smoothedIngressLevel_));
}


float AnthuriumScene::sampleNativeFrontIngress(uint16_t logicalPixel) const {
  // Native FrontRing conveyor: compute the old V3-style travelling wave
  // directly over the 44-pixel front ring as two 22-cell halves.
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

float AnthuriumScene::circularDistance(float a, float b, float count) {
  float d = absf(a - b);
  if (d > count * 0.5f) d = count - d;
  return d;
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


uint8_t AnthuriumScene::toByte(float value) {
  return static_cast<uint8_t>(clamp01(value) * 255.0f);
}
