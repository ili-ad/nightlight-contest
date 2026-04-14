#include "AnthuriumScene.h"

#include <math.h>
#include "../BuildConfig.h"
#include "../topology/PixelTopology.h"

namespace {
  float clamp01(float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 1.0f) {
      return 1.0f;
    }
    return value;
  }

  uint8_t toByte(float value) {
    return static_cast<uint8_t>(clamp01(value) * 255.0f);
  }

  void hsvToRgb(float hue, float saturation, float value, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float s = clamp01(saturation);
    const float v = clamp01(value);
    float h = hue;

    while (h < 0.0f) {
      h += 1.0f;
    }
    while (h >= 1.0f) {
      h -= 1.0f;
    }

    const float scaled = h * 6.0f;
    const int sector = static_cast<int>(scaled);
    const float fraction = scaled - sector;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - (s * fraction));
    const float t = v * (1.0f - (s * (1.0f - fraction)));

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

    r = toByte(rf);
    g = toByte(gf);
    b = toByte(bf);
  }
}

void AnthuriumScene::reset() {
  mInitialized = false;
  mLastNowMs = 0;
  mLastDtSec = 0.016f;
  mSmoothedCharge = 0.0f;
  mCompressedChargeTarget = 0.0f;
  mClampedChargeTarget = 0.0f;
  mStableCharge = 0.0f;
  mSmoothedIngressLevel = 0.0f;
  mIngressConveyorPhase = 0.0f;

  for (uint16_t i = 0; i < BuildConfig::kRingPixels; ++i) {
    mTorusCharge[i] = 0.0f;
    mRingBrightness[i] = 0.0f;
  }
  for (uint16_t i = 0; i < BuildConfig::kLeftStamenPixels; ++i) {
    mLeftBrightness[i] = 0.0f;
  }
  for (uint16_t i = 0; i < BuildConfig::kRightStamenPixels; ++i) {
    mRightBrightness[i] = 0.0f;
  }
}

void AnthuriumScene::renderRgb(PixelBus& bus, const RenderIntent& intent) {
  updateDynamics(intent);
  writeFrame(bus, intent, false);
}

void AnthuriumScene::renderRgbw(PixelBus& bus, const RenderIntent& intent) {
  updateDynamics(intent);
  writeFrame(bus, intent, true);
}

void AnthuriumScene::updateDynamics(const RenderIntent& intent) {
  const uint16_t ringCount = BuildConfig::kRingPixels;
  if (ringCount == 0) {
    return;
  }

  if (!mInitialized) {
    mLastNowMs = intent.sceneNowMs;
    mInitialized = true;
  }

  uint32_t dtMs = intent.sceneNowMs - mLastNowMs;
  if (dtMs > 200) {
    dtMs = 200;
  }
  mLastNowMs = intent.sceneNowMs;

  const float dtSec = static_cast<float>(dtMs) / 1000.0f;
  mLastDtSec = dtSec;
  const float rawChargeTarget = clamp01(intent.sceneChargeTarget > 0.0f ? intent.sceneChargeTarget : intent.sceneCharge);
  const float compressedChargeTarget = compressChargeTarget(rawChargeTarget);
  const float chargeTarget = clampChargeTargetDelta(mClampedChargeTarget, compressedChargeTarget);
  mCompressedChargeTarget = compressedChargeTarget;
  mClampedChargeTarget = chargeTarget;
  const float chargeAlpha = clamp01(BuildConfig::kAnthuriumSceneChargeSmoothingAlpha);
  mSmoothedCharge += (chargeTarget - mSmoothedCharge) * chargeAlpha;
  mSmoothedCharge = clamp01(mSmoothedCharge);
  mStableCharge = applyDeadband(mStableCharge, mSmoothedCharge, BuildConfig::kAnthuriumSceneChargeDeadband);
  mStableCharge = clamp01(mStableCharge);

  const float ingressTarget = clamp01(intent.sceneIngressLevel * (0.30f + (0.70f * mStableCharge)));
  const float ingressAlpha = (BuildConfig::kAnthuriumIngressSmoothingSec > 0.001f)
                                 ? (1.0f - expf(-dtSec / BuildConfig::kAnthuriumIngressSmoothingSec))
                                 : 1.0f;
  mSmoothedIngressLevel += (ingressTarget - mSmoothedIngressLevel) * ingressAlpha;
  mSmoothedIngressLevel = clamp01(mSmoothedIngressLevel);

  const float travelSec = BuildConfig::kAnthuriumIngressTravelMs / 1000.0f;
  if (travelSec > 0.01f) {
    mIngressConveyorPhase += dtSec / travelSec;
    while (mIngressConveyorPhase >= 1.0f) {
      mIngressConveyorPhase -= 1.0f;
    }
  } else {
    mIngressConveyorPhase = 0.0f;
  }

  float temp[BuildConfig::kRingPixels] = {0.0f};
  const float decay = expf(-dtSec / (BuildConfig::kAnthuriumTorusClearMs / 1000.0f));
  const float diffusion = BuildConfig::kAnthuriumTorusDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < ringCount; ++i) {
    const uint16_t left = (i == 0) ? (ringCount - 1) : (i - 1);
    const uint16_t right = (i + 1) % ringCount;

    float value = mTorusCharge[i];
    value += (mTorusCharge[left] + mTorusCharge[right] - (2.0f * mTorusCharge[i])) * diffusion;
    value *= decay;
    temp[i] = clamp01(value);
  }

  const uint16_t ingressA = BuildConfig::kAnthuriumTorusIngressA % ringCount;
  const uint16_t ingressB = BuildConfig::kAnthuriumTorusIngressB % ringCount;
  const float spread = BuildConfig::kAnthuriumTorusIngressSpread;
  const float torusInput = clamp01(mStableCharge * BuildConfig::kAnthuriumDistanceToChargeGain) * dtSec *
                           BuildConfig::kAnthuriumTorusAccumulationGain *
                           BuildConfig::kAnthuriumContinuousInjectionGain;

  for (uint16_t i = 0; i < ringCount; ++i) {
    const float distA = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(ingressA)));
    const float distB = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(ingressB)));
    const float wrapDistA = (distA > (ringCount * 0.5f)) ? (ringCount - distA) : distA;
    const float wrapDistB = (distB > (ringCount * 0.5f)) ? (ringCount - distB) : distB;

    const float aWeight = expf(-(wrapDistA * wrapDistA) / (2.0f * spread * spread));
    const float bWeight = expf(-(wrapDistB * wrapDistB) / (2.0f * spread * spread));
    temp[i] = clamp01(temp[i] + (torusInput * (aWeight + bWeight)));
  }

  for (uint16_t i = 0; i < ringCount; ++i) {
    mTorusCharge[i] = temp[i];
  }
}

float AnthuriumScene::compressChargeTarget(float targetCharge) const {
  const float clamped = clamp01(targetCharge);
  const float knee = clamp01(BuildConfig::kAnthuriumChargeCompressionKnee);
  if (clamped <= knee || knee >= 0.999f) {
    return clamped;
  }

  const float softness = (BuildConfig::kAnthuriumChargeCompressionSoftness < 0.05f)
                             ? 0.05f
                             : BuildConfig::kAnthuriumChargeCompressionSoftness;
  const float x = (clamped - knee) / (1.0f - knee);
  const float powered = powf(x, 1.0f + softness);
  return clamp01(knee + ((1.0f - knee) * powered));
}

float AnthuriumScene::clampChargeTargetDelta(float previous, float target) const {
  const float maxDelta = (BuildConfig::kAnthuriumChargeTargetMaxDeltaPerUpdate < 0.001f)
                             ? 0.001f
                             : BuildConfig::kAnthuriumChargeTargetMaxDeltaPerUpdate;
  if (target > previous + maxDelta) {
    return previous + maxDelta;
  }
  if (target < previous - maxDelta) {
    return previous - maxDelta;
  }
  return target;
}

float AnthuriumScene::sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) const {
  if (stamenCount == 0) {
    return 0.0f;
  }

  const float denom = (stamenCount > 1) ? static_cast<float>(stamenCount - 1) : 1.0f;
  const float stamenPos = static_cast<float>(stamenPixel) / denom;
  const float tipToEntry = 1.0f - stamenPos;
  float delta = fabsf(tipToEntry - mIngressConveyorPhase);
  if (delta > 0.5f) {
    delta = 1.0f - delta;
  }

  const float width = BuildConfig::kAnthuriumIngressConveyorWidth;
  const float moving = expf(-(delta * delta) / (2.0f * width * width));
  const float floor = mStableCharge * BuildConfig::kAnthuriumIngressFloorFromCharge;
  return clamp01(floor + (moving * mSmoothedIngressLevel));
}

float AnthuriumScene::sampleTorusField(uint16_t ringPixel, uint16_t ringCount) const {
  if (ringCount == 0) {
    return 0.0f;
  }

  const float base = BuildConfig::kAnthuriumTorusBaseFieldLevel;
  const float memory = mTorusCharge[ringPixel % ringCount];
  return clamp01(base + memory);
}

float AnthuriumScene::applyBrightnessSlew(float previous, float target, float dtSec) const {
  const float maxStep = BuildConfig::kAnthuriumMaxBrightnessDeltaPerSecond * dtSec;
  if (target > previous + maxStep) {
    return previous + maxStep;
  }
  if (target < previous - maxStep) {
    return previous - maxStep;
  }
  return target;
}

float AnthuriumScene::applyDeadband(float previous, float target, float threshold) const {
  if (fabsf(target - previous) <= threshold) {
    return previous;
  }
  return target;
}

void AnthuriumScene::writeFrame(PixelBus& bus, const RenderIntent& intent, bool useWhite) {
  const float dtSec = (mLastDtSec > 0.0001f) ? mLastDtSec : 0.016f;
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  hsvToRgb(intent.hue, intent.saturation, intent.rgbLevel, r, g, b);

  const SegmentRange ring = PixelTopology::ring();
  const SegmentRange left = PixelTopology::leftStamen();
  const SegmentRange right = PixelTopology::rightStamen();

  for (uint16_t i = 0; i < ring.count; ++i) {
    const uint16_t px = ring.start + i;
    const float field = sampleTorusField(i, ring.count);
    const float targetBrightness = clamp01((field * intent.sceneFieldLevel) +
                                           (mStableCharge * BuildConfig::kAnthuriumTorusInstantGain));
    const float torusAlpha = clamp01(BuildConfig::kAnthuriumTorusBrightnessSmoothingAlpha);
    const float torusSmoothed = mRingBrightness[i] + ((targetBrightness - mRingBrightness[i]) * torusAlpha);
    const float torusDeadbanded =
        applyDeadband(mRingBrightness[i], torusSmoothed, BuildConfig::kAnthuriumLuminanceDeadband);
    mRingBrightness[i] = applyBrightnessSlew(mRingBrightness[i], torusDeadbanded, dtSec);
    const float brightness = mRingBrightness[i];

    const float white = clamp01((intent.whiteLevel + intent.sceneEnergyBoost) * field);

    if (useWhite) {
      bus.setRgbw(px,
                  toByte((static_cast<float>(r) / 255.0f) * brightness),
                  toByte((static_cast<float>(g) / 255.0f) * brightness),
                  toByte((static_cast<float>(b) / 255.0f) * brightness),
                  toByte(white));
    } else {
      uint8_t rr = toByte((static_cast<float>(r) / 255.0f) * brightness);
      uint8_t gg = toByte((static_cast<float>(g) / 255.0f) * brightness);
      uint8_t bb = toByte((static_cast<float>(b) / 255.0f) * brightness);
      const uint8_t whiteLift = static_cast<uint8_t>(toByte(white) / 3);
      rr = static_cast<uint8_t>(rr + ((255 - rr) < whiteLift ? (255 - rr) : whiteLift));
      gg = static_cast<uint8_t>(gg + ((255 - gg) < whiteLift ? (255 - gg) : whiteLift));
      bb = static_cast<uint8_t>(bb + ((255 - bb) < whiteLift ? (255 - bb) : whiteLift));
      bus.setRgb(px, rr, gg, bb);
    }
  }

  for (uint16_t i = 0; i < left.count; ++i) {
    const uint16_t px = left.start + i;
    const float ingress = sampleStamenIngress(i, left.count);
    const float targetBrightness =
        clamp01((ingress * intent.sceneIngressLevel) +
                (BuildConfig::kAnthuriumStamenAmbientFloor * intent.sceneFieldLevel));
    const float ingressAlpha = clamp01(BuildConfig::kAnthuriumIngressBrightnessSmoothingAlpha);
    const float ingressSmoothed = mLeftBrightness[i] + ((targetBrightness - mLeftBrightness[i]) * ingressAlpha);
    const float ingressDeadbanded =
        applyDeadband(mLeftBrightness[i], ingressSmoothed, BuildConfig::kAnthuriumLuminanceDeadband);
    mLeftBrightness[i] = applyBrightnessSlew(mLeftBrightness[i], ingressDeadbanded, dtSec);
    const float brightness = mLeftBrightness[i];
    const float white = clamp01((intent.whiteLevel + intent.sceneEnergyBoost) * ingress);

    if (useWhite) {
      bus.setRgbw(px,
                  toByte((static_cast<float>(r) / 255.0f) * brightness),
                  toByte((static_cast<float>(g) / 255.0f) * brightness),
                  toByte((static_cast<float>(b) / 255.0f) * brightness),
                  toByte(white));
    } else {
      uint8_t rr = toByte((static_cast<float>(r) / 255.0f) * brightness);
      uint8_t gg = toByte((static_cast<float>(g) / 255.0f) * brightness);
      uint8_t bb = toByte((static_cast<float>(b) / 255.0f) * brightness);
      const uint8_t whiteLift = static_cast<uint8_t>(toByte(white) / 3);
      rr = static_cast<uint8_t>(rr + ((255 - rr) < whiteLift ? (255 - rr) : whiteLift));
      gg = static_cast<uint8_t>(gg + ((255 - gg) < whiteLift ? (255 - gg) : whiteLift));
      bb = static_cast<uint8_t>(bb + ((255 - bb) < whiteLift ? (255 - bb) : whiteLift));
      bus.setRgb(px, rr, gg, bb);
    }
  }

  for (uint16_t i = 0; i < right.count; ++i) {
    const uint16_t px = right.start + i;
    const float ingress = sampleStamenIngress(i, right.count);
    const float targetBrightness =
        clamp01((ingress * intent.sceneIngressLevel) +
                (BuildConfig::kAnthuriumStamenAmbientFloor * intent.sceneFieldLevel));
    const float ingressAlpha = clamp01(BuildConfig::kAnthuriumIngressBrightnessSmoothingAlpha);
    const float ingressSmoothed = mRightBrightness[i] + ((targetBrightness - mRightBrightness[i]) * ingressAlpha);
    const float ingressDeadbanded =
        applyDeadband(mRightBrightness[i], ingressSmoothed, BuildConfig::kAnthuriumLuminanceDeadband);
    mRightBrightness[i] = applyBrightnessSlew(mRightBrightness[i], ingressDeadbanded, dtSec);
    const float brightness = mRightBrightness[i];
    const float white = clamp01((intent.whiteLevel + intent.sceneEnergyBoost) * ingress);

    if (useWhite) {
      bus.setRgbw(px,
                  toByte((static_cast<float>(r) / 255.0f) * brightness),
                  toByte((static_cast<float>(g) / 255.0f) * brightness),
                  toByte((static_cast<float>(b) / 255.0f) * brightness),
                  toByte(white));
    } else {
      uint8_t rr = toByte((static_cast<float>(r) / 255.0f) * brightness);
      uint8_t gg = toByte((static_cast<float>(g) / 255.0f) * brightness);
      uint8_t bb = toByte((static_cast<float>(b) / 255.0f) * brightness);
      const uint8_t whiteLift = static_cast<uint8_t>(toByte(white) / 3);
      rr = static_cast<uint8_t>(rr + ((255 - rr) < whiteLift ? (255 - rr) : whiteLift));
      gg = static_cast<uint8_t>(gg + ((255 - gg) < whiteLift ? (255 - gg) : whiteLift));
      bb = static_cast<uint8_t>(bb + ((255 - bb) < whiteLift ? (255 - bb) : whiteLift));
      bus.setRgb(px, rr, gg, bb);
    }
  }

}
