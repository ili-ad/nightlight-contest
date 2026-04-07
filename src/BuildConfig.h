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

namespace BuildConfig {
  // Active hardware choices for this build.
  constexpr PresenceBackend kPresenceBackend = PresenceBackend::None;
  constexpr RenderBackend kRenderBackend = RenderBackend::RGBW;

  // Provisional physical model.
  constexpr float kRingDiameterMm = 100.0f;
  constexpr float kStamenLengthMm = 110.0f;
  constexpr float kLedPitchMm = 6.94f;

  // Provisional pixel allocation.
  constexpr uint16_t kRingPixels = 45;
  constexpr uint16_t kLeftStamenPixels = 16;
  constexpr uint16_t kRightStamenPixels = 16;
  constexpr uint16_t kTotalPixels = kRingPixels + kLeftStamenPixels + kRightStamenPixels;

  // Timing and feature flags.
  constexpr bool kEnableBootAnimation = true;
  constexpr bool kEnableInterludes = true;
  constexpr bool kEnableTelemetry = true;

  constexpr uint32_t kBootAnimationMs = 1800;
  constexpr uint32_t kInterludeMaxMs = 400;
  constexpr uint32_t kStateTickMs = 16;  // ~60fps target cadence

  // Presence gating placeholders for initial behavior hysteresis and decay timing.
  constexpr float kPresenceEnterThreshold = 0.20f;
  constexpr float kPresenceExitThreshold = 0.08f;
  constexpr uint32_t kDecayMs = 1200;

  // Safe brightness caps. These are just placeholders for now.
  constexpr float kGlobalBrightnessLimit = 0.20f;
  constexpr float kIdleBrightness = 0.03f;
  constexpr float kActiveBrightnessMin = 0.05f;
  constexpr float kActiveBrightnessMax = 0.18f;

  // Ambient-light placeholders. Real values come later.
  constexpr float kDarkEnterLux = 8.0f;
  constexpr float kDarkExitLux = 16.0f;
  constexpr uint32_t kAmbientDwellMs = 2000;

  static_assert(kRingPixels > 0, "Ring pixel count must be > 0");
  static_assert(kLeftStamenPixels > 0, "Left stamen pixel count must be > 0");
  static_assert(kRightStamenPixels > 0, "Right stamen pixel count must be > 0");
  static_assert(kTotalPixels == (kRingPixels + kLeftStamenPixels + kRightStamenPixels),
                "Total pixel count mismatch");
}