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
    0x2B,   // i2cAddress
    100,    // pollIntervalMs: 10 Hz, matching DFRobot's I2C example cadence and lowering Wire pressure.
    1800,   // holdMs: ride through short C4001 zero-return gaps.
    6500,   // fadeMs: slow visual fade instead of sudden blackout.
    5000,   // initRetryMs: base cold/offline retry; code backs off to a 30s ceiling.
    60000,  // acceptedDroughtReinitMs: after 1 min of post-target silence, try one gentle coaxing rung.
    60000,  // reinitCooldownMs: no more than one recovery rung per minute.
    true,   // enableC4001AutoInit

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
    1.55f,  // jConveyorTravelSec
    2.20f,  // jColorMemorySec
    0.55f,  // jAdvectionStrength
    0.22f,  // jDiffusionStrength
    false,  // rightJIngressReversed
    true,   // leftJIngressReversed
    1.10f,  // jTipInjectionGain
    0.10f,  // jBaseGlow

    3.20f,  // frontRingColorMemorySec
    0.40f,  // frontRingDiffusion
    1.20f,  // frontRingImpulseGain
    2,      // frontRingBlurPasses
    0.58f,  // rearRingWashScale
    5.40f,  // rearRingMemorySec
    0.02f,  // torusBaseField
    0.05f,  // speedDeadbandMps
    0.80f,  // speedFullScaleMps
    0.34f,  // approachRiseAlpha
    0.10f,  // approachFallAlpha
    0.34f,  // retreatRiseAlpha
    0.10f,  // retreatFallAlpha
    0.26f,  // motionRiseAlpha
    0.08f,  // motionFallAlpha

    0.24f,  // lumaRiseAlpha
    0.14f,  // lumaFallAlpha

    0.10f,  // idleFrontRingFloor
    0.07f,  // idleRearRingFloor
    0.05f,  // idleJFloor

    {1.00f, 0.28f, 0.08f, 0.20f},  // approachColor
    {0.22f, 0.45f, 1.00f, 0.18f},  // retreatColor
    {0.46f, 0.18f, 0.06f, 0.04f},  // idleColor
};


constexpr ClapProfile kClapProfile = {
    1.50f,  // firstThresholdMin
    0.94f,  // firstThresholdScale
    1.07f,  // firstThresholdOffset

    1.20f,  // secondThresholdMin
    0.54f,  // secondThresholdScale
    0.74f,  // secondThresholdOffset
    2.80f,  // secondThresholdHardFloor
};

constexpr OutputProfile kOutputProfile = {
    0.35f,  // globalScale
    80,     // maxChannel
};


constexpr StartupProfile kStartupProfile = {
    20,  // stepMs
    250, // holdMs
    32,  // r
    12,  // g
    2,   // b
    16,  // w
};

constexpr NightlightProfile kNightlightProfile = {
    30,       // baseR: warm amber-white, less pumpkin than pure orange
    12,       // baseG
    2,        // baseB
    22,       // baseW: add light output without relying on oversaturated RGB

    0.85f,    // jScale
    1.00f,    // frontRingScale
    0.65f,    // rearRingScale

    0.02f,    // breatheAmplitude
    14000.0f  // breathePeriodMs
};

constexpr ProductProfile kProductProfile = {
    kTopologyProfile,
    kC4001Profile,
    kAnthuriumProfile,
    kStartupProfile,
    kNightlightProfile,
    kOutputProfile,
    kClapProfile,
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

const StartupProfile& startup() {
  return kProductProfile.startup;
}

const NightlightProfile& nightlight() {
  return kProductProfile.nightlight;
}

const OutputProfile& output() {
  return kProductProfile.output;
}

const ClapProfile& clap() {
  return kProductProfile.clap;
}

const ProductProfile& product() {
  return kProductProfile;
}

}  // namespace Profiles
