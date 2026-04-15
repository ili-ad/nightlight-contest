#include "RenderIntentSmoother.h"

#include <math.h>
#include "../BuildConfig.h"

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

  float smoothToward(float current, float target, float riseAlpha, float fallAlpha) {
    const float alpha = (target >= current) ? riseAlpha : fallAlpha;
    return current + ((target - current) * clamp01(alpha));
  }

  float wrap01(float value) {
    float wrapped = fmodf(value, 1.0f);
    if (wrapped < 0.0f) {
      wrapped += 1.0f;
    }
    return wrapped;
  }

  float smoothHue(float current, float target, float alpha) {
    const float wrappedCurrent = wrap01(current);
    const float wrappedTarget = wrap01(target);

    float delta = wrappedTarget - wrappedCurrent;
    if (delta > 0.5f) {
      delta -= 1.0f;
    } else if (delta < -0.5f) {
      delta += 1.0f;
    }

    return wrap01(wrappedCurrent + (delta * clamp01(alpha)));
  }
}

void RenderIntentSmoother::reset() {
  mInitialized = false;
  mCurrent = RenderIntent();
}

RenderIntent RenderIntentSmoother::smooth(const RenderIntent& target) {
  if (!mInitialized) {
    mCurrent = target;
    mInitialized = true;
    return mCurrent;
  }

  RenderIntent result = target;

  result.whiteLevel = clamp01(smoothToward(mCurrent.whiteLevel,
                                           target.whiteLevel,
                                           BuildConfig::kIntentWhiteRiseAlpha,
                                           BuildConfig::kIntentWhiteFallAlpha));

  result.rgbLevel = clamp01(smoothToward(mCurrent.rgbLevel,
                                         target.rgbLevel,
                                         BuildConfig::kIntentRgbRiseAlpha,
                                         BuildConfig::kIntentRgbFallAlpha));

  result.saturation = clamp01(smoothToward(mCurrent.saturation,
                                           target.saturation,
                                           BuildConfig::kIntentSaturationAlpha,
                                           BuildConfig::kIntentSaturationAlpha));

  result.hue = smoothHue(mCurrent.hue, target.hue, BuildConfig::kIntentHueAlpha);

  result.blobCenter = clamp01(smoothToward(mCurrent.blobCenter,
                                           target.blobCenter,
                                           BuildConfig::kIntentBlobCenterAlpha,
                                           BuildConfig::kIntentBlobCenterAlpha));

  result.blobWidth = clamp01(smoothToward(mCurrent.blobWidth,
                                          target.blobWidth,
                                          BuildConfig::kIntentBlobWidthAlpha,
                                          BuildConfig::kIntentBlobWidthAlpha));

  mCurrent = result;
  return result;
}
