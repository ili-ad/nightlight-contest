#pragma once

#include <stdint.h>

namespace Profiles {

// ARCH-061 scaffold assumptions copied from the proven v1 hardware profile.
constexpr uint16_t kRingPixels = 45;
constexpr uint16_t kLeftStamenPixels = 16;
constexpr uint16_t kRightStamenPixels = 16;

enum class SpanRole : uint8_t {
  Ring,
  LeftStamen,
  RightStamen,
};

struct SpanProfile {
  SpanRole role;
  uint16_t logicalCount;
  bool reversed;
};

constexpr uint8_t kTopologySpanCount = 3;

struct TopologyProfile {
  SpanProfile spans[kTopologySpanCount];
};

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
  TopologyProfile topology;
  C4001Profile c4001;
  AnthuriumProfile anthurium;
  NightlightProfile nightlight;
};

const TopologyProfile& topology();
const C4001Profile& c4001();
const AnthuriumProfile& anthurium();
const NightlightProfile& nightlight();
const ProductProfile& product();

}  // namespace Profiles
