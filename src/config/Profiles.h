#pragma once

#include <stdint.h>

namespace Profiles {

// ARCH-061 scaffold assumptions copied from the proven v1 hardware profile.
constexpr uint16_t kRingPixels = 45;
constexpr uint16_t kLeftStamenPixels = 16;
constexpr uint16_t kRightStamenPixels = 16;

struct C4001Profile {
  uint32_t pollIntervalMs;
  uint32_t holdMs;

  float rangeNearM;
  float rangeFarM;
  float stillSpeedMps;
  float nearnessLiftGain;

  float approachRangeAlpha;
  float retreatRangeAlpha;
  float speedAlpha;
  float speedDecayAlpha;

  float chargeRiseAlpha;
  float chargeFallAlpha;
  float ingressBase;
  float ingressGain;
  float ingressRiseAlpha;
  float ingressFallAlpha;
  float continuityRiseAlpha;
  float continuityFallAlpha;
};

struct RgbwFloat {
  float r;
  float g;
  float b;
  float w;
};

struct AnthuriumProfile {
  float ingressTravelSec;
  float ingressWidth;
  float ingressFloor;

  float torusDecayPerSecond;
  float torusDiffusionPerSecond;
  float torusAccumulationGain;
  float torusInstantGain;
  float torusBaseField;

  uint16_t ingressA;
  uint16_t ingressB;
  float ingressSpread;

  float lumaRiseAlpha;
  float lumaFallAlpha;

  RgbwFloat approachColor;
  RgbwFloat stillColor;
  RgbwFloat retreatColor;
  RgbwFloat idleColor;
};

struct NightlightProfile {
  uint8_t baseR;
  uint8_t baseG;
  uint8_t baseB;
  uint8_t baseW;

  float breatheAmplitude;
  float breathePeriodMs;
};

struct ProductProfile {
  C4001Profile c4001;
  AnthuriumProfile anthurium;
  NightlightProfile nightlight;
};

const C4001Profile& c4001();
const AnthuriumProfile& anthurium();
const NightlightProfile& nightlight();
const ProductProfile& product();

}  // namespace Profiles
