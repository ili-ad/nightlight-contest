#pragma once
#include <stdint.h>

enum class PresenceBackend : uint8_t {
  None,
  LD2410,
  C4001
};

enum class RenderBackend : uint8_t {
  RGB,
  RGBW
};

enum class DebugInputMode : uint8_t {
  None,
  SimulatedApproachLoop
};

#define PRESENCE_BACKEND_NONE 0
#define PRESENCE_BACKEND_LD2410 1
#define PRESENCE_BACKEND_C4001 2

#define RENDER_BACKEND_RGB 0
#define RENDER_BACKEND_RGBW 1

#define DEBUG_INPUT_MODE_NONE 0
#define DEBUG_INPUT_MODE_SIMULATED_APPROACH_LOOP 1

#define TELEM_NONE 0
#define TELEM_MINIMAL 1
#define TELEM_DROPOUT_TINY 2
#define TELEM_C4001_PROBE 3

#define BUILD_PROFILE_CONTEST 1
#define BUILD_PROFILE_C4001_PROBE 2

#ifndef BUILD_PROFILE
#define BUILD_PROFILE BUILD_PROFILE_CONTEST
#endif

#define BUILD_C4001_PROBE (BUILD_PROFILE == BUILD_PROFILE_C4001_PROBE)

#ifndef TELEM_PROFILE
#if BUILD_C4001_PROBE
#define TELEM_PROFILE TELEM_C4001_PROBE
#else
#define TELEM_PROFILE TELEM_DROPOUT_TINY
#endif
#endif

#ifndef BUILD_PRESENCE_BACKEND
#define BUILD_PRESENCE_BACKEND PRESENCE_BACKEND_C4001
#endif

#ifndef BUILD_RENDER_BACKEND
#if BUILD_C4001_PROBE
#define BUILD_RENDER_BACKEND RENDER_BACKEND_RGBW
#else
#define BUILD_RENDER_BACKEND RENDER_BACKEND_RGBW
#endif
#endif

#ifndef BUILD_DEBUG_INPUT_MODE
#if BUILD_C4001_PROBE
#define BUILD_DEBUG_INPUT_MODE DEBUG_INPUT_MODE_NONE
#else
#define BUILD_DEBUG_INPUT_MODE DEBUG_INPUT_MODE_NONE
#endif
#endif

#ifndef BUILD_MIN_SIZE_PROFILE
#if BUILD_PROFILE == BUILD_PROFILE_CONTEST
#define BUILD_MIN_SIZE_PROFILE 1
#else
#define BUILD_MIN_SIZE_PROFILE 0
#endif
#endif

#ifndef BUILD_ENABLE_BOOT_ANIMATION
#if BUILD_C4001_PROBE || BUILD_MIN_SIZE_PROFILE
#define BUILD_ENABLE_BOOT_ANIMATION 0
#else
#define BUILD_ENABLE_BOOT_ANIMATION 1
#endif
#endif

#ifndef BUILD_ENABLE_INTERLUDES
#if BUILD_C4001_PROBE || BUILD_MIN_SIZE_PROFILE
#define BUILD_ENABLE_INTERLUDES 0
#else
#define BUILD_ENABLE_INTERLUDES 1
#endif
#endif

#ifndef BUILD_ENABLE_DEBUG_MODES
#if BUILD_C4001_PROBE
#define BUILD_ENABLE_DEBUG_MODES 0
#else
#define BUILD_ENABLE_DEBUG_MODES 1
#endif
#endif

#define BUILD_HAS_DEBUG_SIM ((BUILD_ENABLE_DEBUG_MODES != 0) && (BUILD_DEBUG_INPUT_MODE != DEBUG_INPUT_MODE_NONE))
#define BUILD_HAS_RGB_RENDERER (BUILD_RENDER_BACKEND == RENDER_BACKEND_RGB)
#define BUILD_HAS_RGBW_RENDERER (BUILD_RENDER_BACKEND == RENDER_BACKEND_RGBW)
#define BUILD_HAS_LD2410_BACKEND (BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_LD2410)
#define BUILD_HAS_C4001_BACKEND (BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_C4001)

namespace BuildConfig {
  // ---------------------------------------------------------------------------
  // Bench-proven hardware profile (Arduino Nano Every)
  // ---------------------------------------------------------------------------
  // LED backend is Adafruit NeoPixel on D6. See PixelBus for strip order
  // (NEO_GRBW + NEO_KHZ800).
  // I2C bus is the Nano Every hardware bus: A4=SDA, A5=SCL.
  // Bench power topology: strip on external 5V, Nano on USB, and shared ground.
  constexpr uint8_t kBh1750I2cAddress = 0x23;
  constexpr uint8_t kC4001I2cAddress = 0x2B;

  // ---------------------------------------------------------------------------
  // Hardware/backend selection
  // ---------------------------------------------------------------------------
  // Default runtime posture is hardware-first:
  // - BH1750 ambient via AmbientBh1750/AmbientGate
  // - C4001 presence via PresenceManager
  // Switch these only when intentionally testing alternate benches/backends.
  constexpr PresenceBackend kPresenceBackend =
#if BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_NONE
      PresenceBackend::None;
#elif BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_LD2410
      PresenceBackend::LD2410;
#else
      PresenceBackend::C4001;
#endif

  constexpr RenderBackend kRenderBackend =
#if BUILD_RENDER_BACKEND == RENDER_BACKEND_RGB
      RenderBackend::RGB;
#else
      RenderBackend::RGBW;
#endif

  // ---------------------------------------------------------------------------
  // Debug simulation
  // ---------------------------------------------------------------------------
  // Simulation remains available for explicit debug/repro runs.
  // Set to SimulatedApproachLoop only when intentionally overriding live input.
  constexpr DebugInputMode kDebugInputMode =
#if BUILD_DEBUG_INPUT_MODE == DEBUG_INPUT_MODE_SIMULATED_APPROACH_LOOP
      DebugInputMode::SimulatedApproachLoop;
#else
      DebugInputMode::None;
#endif

  // Major simulated loop choreography boundaries.
  constexpr uint32_t kSimDarkIdleMs = 1200;
  constexpr uint32_t kSimApproachEndMs = 3200;
  constexpr uint32_t kSimNearStillEndMs = 4600;
  constexpr uint32_t kSimRetreatEndMs = 6200;
  constexpr uint32_t kSimDarkAbsentEndMs = 7200;
  constexpr uint32_t kSimDayLockoutEndMs = 8400;
  constexpr uint32_t kSimLoopMs = 9000;

  // Simulated ambient levels for dark/day phases.
  constexpr float kSimDarkAmbientLux = 2.0f;
  constexpr float kSimDayAmbientLux = 80.0f;

  // ---------------------------------------------------------------------------
  // Provisional geometry and pixel counts
  // ---------------------------------------------------------------------------
  // Geometry and final strip routing are still provisional; tune these as assembly
  // details settle.
  constexpr float kRingDiameterMm = 100.0f;
  constexpr float kStamenLengthMm = 110.0f;
  constexpr float kLedPitchMm = 6.94f;

  // Pixel allocation is still provisional pending final physical routing.
  constexpr uint16_t kRingPixels = 45;
  constexpr uint16_t kLeftStamenPixels = 16;
  constexpr uint16_t kRightStamenPixels = 16;
  constexpr uint16_t kTotalPixels = kRingPixels + kLeftStamenPixels + kRightStamenPixels;

  // ---------------------------------------------------------------------------
  // Lifecycle timing and feature flags
  // ---------------------------------------------------------------------------
  constexpr bool kEnableBootAnimation = (BUILD_ENABLE_BOOT_ANIMATION != 0);
  constexpr bool kEnableInterludes = (BUILD_ENABLE_INTERLUDES != 0);
  constexpr bool kEnableTelemetry = (TELEM_PROFILE != TELEM_NONE);
  constexpr uint8_t kTelemetryProfile = TELEM_PROFILE;
  constexpr uint32_t kTelemetryPresenceLogIntervalMs = 500;

  constexpr uint32_t kBootAnimationMs = 1800;
  constexpr uint32_t kInterludeMaxMs = 400;
  constexpr uint32_t kStateTickMs = 16;  // ~60fps target cadence
  constexpr uint32_t kDecayMs = 1200;
  constexpr uint32_t kFaultSafeHoldMs = 600;

  // ---------------------------------------------------------------------------
  // Presence thresholds
  // ---------------------------------------------------------------------------
  constexpr float kPresenceEnterThreshold = 0.20f;
  constexpr float kPresenceExitThreshold = 0.08f;

  // ---------------------------------------------------------------------------
  // C4001 resilience / connector-burp tolerance
  // ---------------------------------------------------------------------------
  // Tuning policy:
  // - Hold briefly across connector burps.
  // - Decay confidence + hints gently while disconnected.
  // - Drop to sane empty state after sustained dropout.
  constexpr uint8_t kC4001MaxConsecutiveFailuresForOnline = 2;
  constexpr uint32_t kC4001PollIntervalMs = 35;
  constexpr uint32_t kC4001DropoutHoldMs = 450;
  constexpr float kC4001ConfidenceDecayPerFailure = 0.18f;
  constexpr float kC4001DistanceDecayPerFailure = 0.12f;
  constexpr float kC4001MotionDecayPerFailure = 0.18f;
  constexpr uint32_t kC4001DropoutForceEmptyMs = 1400;
  constexpr uint32_t kC4001ReinitIntervalMs = 1000;

  // Successful no-target reads are treated as transient target loss for a
  // short grace period before true absence is committed.
  constexpr uint32_t kC4001NoTargetGraceMs = 420;
  constexpr uint16_t kC4001NoTargetRequiredConsecutiveSamples = 5;
  constexpr uint32_t kC4001NoTargetRequiredWindowMs = 180;
  constexpr float kC4001NoTargetGraceDecayPerSample = 0.05f;
  constexpr float kC4001NoTargetDecayPerSample = 0.18f;

  constexpr float kC4001ConfidenceEmaAlpha = 0.30f;
  constexpr float kC4001DistanceEmaAlpha = 0.35f;
  constexpr float kC4001MotionEmaAlpha = 0.35f;
  constexpr float kC4001MaxAcceptedSpeedMps = 2.60f;
  constexpr float kC4001MaxAcceptedRangeDeltaPerSecond = 1.80f;
  constexpr float kC4001NearFieldStartM = 1.80f;
  constexpr float kC4001NearFieldStrongSwingM = 0.20f;
  constexpr uint8_t kC4001NearFieldCoherentSamples = 2;
  constexpr float kC4001NearFieldCoherenceBandM = 0.22f;

  // ---------------------------------------------------------------------------
  // Brightness ranges
  // ---------------------------------------------------------------------------
  constexpr float kGlobalBrightnessLimit = 0.20f;
  constexpr float kIdleBrightness = 0.03f;
  constexpr float kActiveBrightnessMin = 0.05f;
  constexpr float kActiveBrightnessMax = 0.18f;

  // ---------------------------------------------------------------------------
  // Shared mapper tuning
  // ---------------------------------------------------------------------------
  constexpr float kIdleHue = 0.09f;
  constexpr float kIdleSaturation = 0.08f;
  constexpr float kIdleRgbLevel = 0.02f;
  constexpr float kIdleAnimationRate = 0.02f;

  constexpr float kFaultSafeWhiteLevel = 0.02f;
  constexpr float kFaultSafeHue = 0.56f;
  constexpr float kFaultSafeSaturation = 0.01f;

  constexpr float kActiveFarHue = 0.58f;
  constexpr float kActiveNearHue = 0.10f;
  constexpr float kActiveBaseRgbLevel = 0.07f;
  constexpr float kActiveMotionRgbBoost = 0.20f;
  constexpr float kActiveBaseSaturation = 0.18f;
  constexpr float kActiveMotionSaturationBoost = 0.45f;

  constexpr float kActiveConfidenceWeight = 0.65f;
  constexpr float kActiveNearnessWeight = 0.35f;
  constexpr float kStillCloseHueBiasStrength = 0.35f;
  constexpr float kStillCloseSaturationSoftening = 0.10f;
  constexpr float kStillCloseRgbSoftening = 0.04f;
  constexpr float kActiveAnimationBaseRate = 0.08f;
  constexpr float kActiveAnimationMotionBoost = 0.92f;

  constexpr float kDecayEndHue = 0.12f;
  constexpr float kDecayEndSaturation = 0.07f;
  constexpr float kDecayEndRgbLevel = 0.02f;
  constexpr float kDecayAnimationFloor = 0.01f;
  constexpr float kDecaySegmentReturnThreshold = 0.5f;

  // ---------------------------------------------------------------------------
  // Sensor-specific mapper refinements
  // ---------------------------------------------------------------------------
  constexpr float kLd2410MotionEnergySaturationBoost = 0.10f;
  constexpr float kLd2410StaticEnergyWhiteBoost = 0.06f;
  constexpr float kC4001SpeedAnimationBoost = 0.20f;
  constexpr float kC4001TargetEnergyRgbBoost = 0.10f;

  // ---------------------------------------------------------------------------
  // Anthurium reservoir active-scene tuning (POL-021)
  // ---------------------------------------------------------------------------
  constexpr uint32_t kAnthuriumIngressTravelMs = 3000;
  constexpr uint32_t kAnthuriumTorusClearMs = 18000;
  constexpr float kAnthuriumDistanceToChargeGain = 0.92f;
  constexpr float kAnthuriumIngressBaseLevel = 0.18f;
  constexpr float kAnthuriumTorusFieldBaseLevel = 0.30f;
  constexpr float kAnthuriumEnergyWhiteBoostGain = 0.20f;
  constexpr float kAnthuriumTorusAccumulationGain = 0.45f;
  constexpr float kAnthuriumTorusInstantGain = 0.08f;
  constexpr float kAnthuriumTorusBaseFieldLevel = 0.06f;
  constexpr float kAnthuriumTorusDiffusionPerSecond = 0.34f;
  constexpr float kAnthuriumContinuousInjectionGain = 1.0f;
  constexpr uint32_t kAnthuriumChargeRiseTauMs = 260;
  constexpr uint32_t kAnthuriumChargeFallTauMs = 520;
  constexpr float kAnthuriumIngressSmoothingSec = 0.28f;
  constexpr float kAnthuriumIngressConveyorWidth = 0.22f;
  constexpr float kAnthuriumIngressFloorFromCharge = 0.42f;
  constexpr float kAnthuriumMaxBrightnessDeltaPerSecond = 2.10f;
  constexpr float kAnthuriumStamenAmbientFloor = 0.05f;
  constexpr uint16_t kAnthuriumTorusIngressA = 2;
  constexpr uint16_t kAnthuriumTorusIngressB = (kRingPixels / 2) + 2;
  constexpr float kAnthuriumTorusIngressSpread = 3.5f;
  constexpr uint32_t kTelemetryC4001RawLogIntervalMs = 400;
  constexpr float kAnthuriumNearFieldCompressionStartM = 0.90f;
  constexpr float kAnthuriumNearFieldCompressionExponent = 2.10f;
  constexpr float kAnthuriumNearFieldChargeTargetMaxDeltaPerUpdate = 0.03f;
  constexpr float kAnthuriumChargeCompressionKnee = 0.68f;
  constexpr float kAnthuriumChargeCompressionSoftness = 0.72f;
  constexpr float kAnthuriumChargeTargetMaxDeltaPerUpdate = 0.06f;
  constexpr float kAnthuriumMinAcceptedRangeM = 0.06f;
  constexpr float kAnthuriumRangeSmoothingAlpha = 0.20f;
  constexpr float kAnthuriumSceneChargeSmoothingAlpha = 0.22f;
  constexpr float kAnthuriumSceneChargeDeadband = 0.012f;
  constexpr float kAnthuriumTorusBrightnessSmoothingAlpha = 0.24f;
  constexpr float kAnthuriumIngressBrightnessSmoothingAlpha = 0.22f;
  constexpr float kAnthuriumLuminanceDeadband = 0.015f;
  constexpr uint32_t kAnthuriumRejectedHoldMs = 450;
  constexpr float kAnthuriumRejectedDecayPerSecond = 2.40f;
  constexpr float kAnthuriumRejectedFloor = 0.0f;

  // ---------------------------------------------------------------------------
  // RenderIntent smoothing policy (non-procedural states)
  // ---------------------------------------------------------------------------
  constexpr float kIntentWhiteRiseAlpha = 0.16f;
  constexpr float kIntentWhiteFallAlpha = 0.10f;
  constexpr float kIntentRgbRiseAlpha = 0.22f;
  constexpr float kIntentRgbFallAlpha = 0.14f;
  constexpr float kIntentHueAlpha = 0.16f;
  constexpr float kIntentSaturationAlpha = 0.14f;
  constexpr float kIntentBlobCenterAlpha = 0.24f;
  constexpr float kIntentBlobWidthAlpha = 0.12f;

  // ---------------------------------------------------------------------------
  // Ambient-light thresholds
  // ---------------------------------------------------------------------------
  constexpr float kDarkEnterLux = 8.0f;
  constexpr float kDarkExitLux = 16.0f;
  constexpr float kAmbientGateSmoothingAlpha = 0.16f;
  constexpr uint32_t kAmbientEnterDwellMs = 1800;
  constexpr uint32_t kAmbientExitDwellMs = 2800;
  constexpr uint32_t kAmbientMinDayHoldMs = 2200;
  constexpr uint32_t kAmbientMinNightHoldMs = 2600;
  constexpr float kAmbientNightSelfLightExitMarginLux = 4.0f;
  constexpr uint32_t kAmbientNightSelfLightExtraExitDwellMs = 1200;
  constexpr float kAmbientActiveModeSuppressMinPresenceConfidence = 0.45f;
  constexpr float kAmbientActiveModeSuppressDecayMinPresenceConfidence = 0.55f;
  constexpr uint32_t kAmbientActiveModeSuppressMaxBlockMs = 18000;
  constexpr uint32_t kAmbientActiveModeSuppressEscapedExitDwellMs = 5000;
  constexpr float kAmbientActiveModeSuppressBrightOverrideLux = 52.0f;

  // Day-dormant visual floor (soft boundary; quieter than NightIdle)
  constexpr float kDayDormantWhiteFloor = 0.005f;
  constexpr float kDayDormantRgbFloor = 0.003f;
  constexpr float kDayDormantSaturation = 0.015f;
  constexpr float kDayDormantAnimationRate = 0.006f;

  // ---------------------------------------------------------------------------
  // Compile-time sanity checks
  // ---------------------------------------------------------------------------
  static_assert(kRingPixels > 0, "Ring pixel count must be > 0");
  static_assert(kLeftStamenPixels > 0, "Left stamen pixel count must be > 0");
  static_assert(kRightStamenPixels > 0, "Right stamen pixel count must be > 0");
  static_assert(kTotalPixels == (kRingPixels + kLeftStamenPixels + kRightStamenPixels),
                "Total pixel count mismatch");

  static_assert(kPresenceEnterThreshold > kPresenceExitThreshold,
                "Presence enter threshold must exceed exit threshold");
  static_assert(kActiveBrightnessMax >= kActiveBrightnessMin,
                "Active brightness max must be >= min");
  static_assert(kBootAnimationMs > 0, "Boot animation duration must be > 0");
  static_assert(kInterludeMaxMs > 0, "Interlude max duration must be > 0");
  static_assert(kDecayMs > 0, "Decay duration must be > 0");
  static_assert(kFaultSafeHoldMs > 0, "Fault-safe hold duration must be > 0");
  static_assert(kSimLoopMs > 0, "Simulation loop duration must be > 0");
  static_assert(kSimDarkIdleMs < kSimApproachEndMs,
                "Simulation boundary order invalid: kSimDarkIdleMs must be < "
                "kSimApproachEndMs");
  static_assert(kSimApproachEndMs < kSimNearStillEndMs,
                "Simulation boundary order invalid: kSimApproachEndMs must be < "
                "kSimNearStillEndMs");
  static_assert(kSimNearStillEndMs < kSimRetreatEndMs,
                "Simulation boundary order invalid: kSimNearStillEndMs must be < "
                "kSimRetreatEndMs");
  static_assert(kSimRetreatEndMs < kSimDarkAbsentEndMs,
                "Simulation boundary order invalid: kSimRetreatEndMs must be < "
                "kSimDarkAbsentEndMs");
  static_assert(kSimDarkAbsentEndMs < kSimDayLockoutEndMs,
                "Simulation boundary order invalid: kSimDarkAbsentEndMs must be < "
                "kSimDayLockoutEndMs");
  static_assert(kSimDayLockoutEndMs < kSimLoopMs,
                "Simulation boundary order invalid: kSimDayLockoutEndMs must be < "
                "kSimLoopMs");
  static_assert((TELEM_PROFILE == TELEM_NONE) || (TELEM_PROFILE == TELEM_MINIMAL) ||
                    (TELEM_PROFILE == TELEM_DROPOUT_TINY) || (TELEM_PROFILE == TELEM_C4001_PROBE),
                "TELEM_PROFILE must be TELEM_NONE, TELEM_MINIMAL, TELEM_DROPOUT_TINY, or TELEM_C4001_PROBE");
  static_assert((BUILD_PROFILE == BUILD_PROFILE_CONTEST) || (BUILD_PROFILE == BUILD_PROFILE_C4001_PROBE),
                "BUILD_PROFILE must be BUILD_PROFILE_CONTEST or BUILD_PROFILE_C4001_PROBE");
  static_assert((BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_NONE) ||
                    (BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_LD2410) ||
                    (BUILD_PRESENCE_BACKEND == PRESENCE_BACKEND_C4001),
                "BUILD_PRESENCE_BACKEND must be PRESENCE_BACKEND_* enum value");
  static_assert((BUILD_RENDER_BACKEND == RENDER_BACKEND_RGB) ||
                    (BUILD_RENDER_BACKEND == RENDER_BACKEND_RGBW),
                "BUILD_RENDER_BACKEND must be RENDER_BACKEND_RGB or RENDER_BACKEND_RGBW");
  static_assert((BUILD_DEBUG_INPUT_MODE == DEBUG_INPUT_MODE_NONE) ||
                    (BUILD_DEBUG_INPUT_MODE == DEBUG_INPUT_MODE_SIMULATED_APPROACH_LOOP),
                "BUILD_DEBUG_INPUT_MODE must be DEBUG_INPUT_MODE_* enum value");
}
