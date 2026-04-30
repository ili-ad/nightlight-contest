#pragma once

#include <stdint.h>

namespace Profiles {

// Final installed topology (physical chain order):
// RightJ (J1) 12 + LeftJ (J2) 12 + FrontRing (O1) 44 + RearRing (O2) 44.
constexpr uint16_t kRightJPixels = 12;
constexpr uint16_t kLeftJPixels = 12;
constexpr uint16_t kFrontRingPixels = 44;
constexpr uint16_t kRearRingPixels = 44;
// Expected total physical chain length; keep this equal to the sum of all
// configured topology span logicalCount values.
constexpr uint16_t kPhysicalPixels = 112;

enum class SpanRole : uint8_t {
  RightJ,
  LeftJ,
  FrontRing,
  RearRing,
};

struct SpanProfile {
  SpanRole role;
  uint16_t logicalCount;
  bool reversed;
};

constexpr uint8_t kTopologySpanCount = 4;

struct TopologyProfile {
  SpanProfile spans[kTopologySpanCount];
};

struct C4001Profile {
  uint8_t i2cAddress;
  uint32_t pollIntervalMs;
  uint32_t holdMs;
  uint32_t initRetryMs;

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
  float jConveyorTravelSec;
  float jColorMemorySec;
  float jAdvectionStrength;
  float jDiffusionStrength;
  bool rightJIngressReversed;
  bool leftJIngressReversed;
  float jTipInjectionGain;
  float jBaseGlow;

  float frontRingColorMemorySec;
  float frontRingDiffusion;
  float frontRingImpulseGain;
  uint8_t frontRingBlurPasses;
  float rearRingWashScale;
  float rearRingMemorySec;
  float torusBaseField;
  float speedDeadbandMps;
  float speedFullScaleMps;
  float approachRiseAlpha;
  float approachFallAlpha;
  float retreatRiseAlpha;
  float retreatFallAlpha;
  float motionRiseAlpha;
  float motionFallAlpha;

  float lumaRiseAlpha;
  float lumaFallAlpha;

  float idleFrontRingFloor;
  float idleRearRingFloor;
  float idleJFloor;

  RgbwFloat approachColor;
  RgbwFloat retreatColor;
  RgbwFloat idleColor;
};

struct ClapProfile {
  float firstThresholdMin;
  float firstThresholdScale;
  float firstThresholdOffset;

  float secondThresholdMin;
  float secondThresholdScale;
  float secondThresholdOffset;
  float secondThresholdHardFloor;
};

struct OutputProfile {
  float globalScale;
  uint8_t maxChannel;
};

struct StartupProfile {
  uint16_t stepMs;
  uint16_t holdMs;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t w;
};

struct NightlightProfile {
  uint8_t baseR;
  uint8_t baseG;
  uint8_t baseB;
  uint8_t baseW;

  float jScale;
  float frontRingScale;
  float rearRingScale;

  float breatheAmplitude;
  float breathePeriodMs;
};

struct ProductProfile {
  TopologyProfile topology;
  C4001Profile c4001;
  AnthuriumProfile anthurium;
  StartupProfile startup;
  NightlightProfile nightlight;
  OutputProfile output;
  ClapProfile clap;
};

const TopologyProfile& topology();
const C4001Profile& c4001();
const AnthuriumProfile& anthurium();
const StartupProfile& startup();
const NightlightProfile& nightlight();
const OutputProfile& output();
const ClapProfile& clap();
const ProductProfile& product();

}  // namespace Profiles
