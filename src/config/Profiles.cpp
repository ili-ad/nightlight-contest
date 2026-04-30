#include "Profiles.h"

namespace Profiles {
namespace {
constexpr TopologyProfile kTopologyProfile = {{
    // RightJ/J1: physical indices 0-11, wired top -> bottom.
    {SpanRole::RightJ, kRightJPixels, false},
    // LeftJ/J2: physical indices 12-23, wired bottom -> top.
    {SpanRole::LeftJ, kLeftJPixels, false},
    // FrontRing/O1: physical indices 24-67, starts at 6 o'clock, clockwise.
    {SpanRole::FrontRing, kFrontRingPixels, false},
    // RearRing/O2: physical indices 68-111, starts at 6 o'clock, counterclockwise.
    {SpanRole::RearRing, kRearRingPixels, false},
}};

constexpr C4001Profile kC4001Profile = {
    33,     // pollIntervalMs
    420,    // holdMs

    0.35f,  // rangeNearM
    2.40f,  // rangeFarM
    0.06f,  // stillSpeedMps
    0.35f,  // nearnessLiftGain

    0.26f,  // approachRangeAlpha
    0.14f,  // retreatRangeAlpha
    0.22f,  // speedAlpha
    0.10f,  // speedDecayAlpha

    0.20f,  // chargeRiseAlpha
    0.06f,  // chargeFallAlpha
    0.22f,  // ingressBase
    0.78f,  // ingressGain
    0.24f,  // ingressRiseAlpha
    0.08f,  // ingressFallAlpha
    0.30f,  // continuityRiseAlpha
    0.03f,  // continuityFallAlpha
};

constexpr AnthuriumProfile kAnthuriumProfile = {
    1.45f,  // ingressTravelSec
    0.18f,  // ingressWidth
    0.08f,  // ingressFloor

    0.65f,  // torusDecayPerSecond
    0.35f,  // torusDiffusionPerSecond
    1.85f,  // torusAccumulationGain
    0.55f,  // torusInstantGain
    0.02f,  // torusBaseField

    7,      // ingressA
    29,     // ingressB
    3.4f,   // ingressSpread

    0.24f,  // lumaRiseAlpha
    0.14f,  // lumaFallAlpha

    {1.00f, 0.28f, 0.08f, 0.20f},  // approachColor
    {0.95f, 0.62f, 0.20f, 0.34f},  // stillColor
    {0.72f, 0.26f, 0.45f, 0.12f},  // retreatColor
    {0.55f, 0.22f, 0.10f, 0.05f},  // idleColor
};


constexpr OutputProfile kOutputProfile = {
    0.18f,  // globalScale
    80,     // maxChannel
};

constexpr NightlightProfile kNightlightProfile = {
    18,       // baseR
    7,        // baseG
    1,        // baseB
    4,        // baseW
    0.02f,    // breatheAmplitude
    14000.0f  // breathePeriodMs
};

constexpr ProductProfile kProductProfile = {
    kTopologyProfile,
    kC4001Profile,
    kAnthuriumProfile,
    kNightlightProfile,
    kOutputProfile,
};
}  // namespace

const TopologyProfile& topology() {
  return kProductProfile.topology;
}

const C4001Profile& c4001() {
  return kProductProfile.c4001;
}

const AnthuriumProfile& anthurium() {
  return kProductProfile.anthurium;
}

const NightlightProfile& nightlight() {
  return kProductProfile.nightlight;
}

const OutputProfile& output() {
  return kProductProfile.output;
}

const ProductProfile& product() {
  return kProductProfile;
}

}  // namespace Profiles
