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

namespace BuildConfig {
  // ---------------------------------------------------------------------------
  // Hardware/backend selection
  // ---------------------------------------------------------------------------
  constexpr PresenceBackend kPresenceBackend = PresenceBackend::None;
  constexpr RenderBackend kRenderBackend = RenderBackend::RGBW;

  // ---------------------------------------------------------------------------
  // Debug simulation
  // ---------------------------------------------------------------------------
  // Set to None when live sensor bring-up begins.
  constexpr DebugInputMode kDebugInputMode = DebugInputMode::SimulatedApproachLoop;

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
  // Pre-bench physical model (provisional). Final strip routing and assembly path
  // can shift these counts slightly; tune here first during bench bring-up.
  constexpr float kRingDiameterMm = 100.0f;
  constexpr float kStamenLengthMm = 110.0f;
  constexpr float kLedPitchMm = 6.94f;

  // Provisional pre-bench pixel allocation.
  constexpr uint16_t kRingPixels = 45;
  constexpr uint16_t kLeftStamenPixels = 16;
  constexpr uint16_t kRightStamenPixels = 16;
  constexpr uint16_t kTotalPixels = kRingPixels + kLeftStamenPixels + kRightStamenPixels;

  // ---------------------------------------------------------------------------
  // Lifecycle timing and feature flags
  // ---------------------------------------------------------------------------
  constexpr bool kEnableBootAnimation = true;
  constexpr bool kEnableInterludes = true;
  constexpr bool kEnableTelemetry = true;

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
  // Ambient-light thresholds
  // ---------------------------------------------------------------------------
  constexpr float kDarkEnterLux = 8.0f;
  constexpr float kDarkExitLux = 16.0f;
  constexpr uint32_t kAmbientDwellMs = 2000;

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
}
