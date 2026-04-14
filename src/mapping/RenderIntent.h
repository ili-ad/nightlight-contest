#pragma once
#include <stdint.h>
#include "../Types.h"

enum class ActiveSceneMode : uint8_t {
  LegacyBlob = 0,
  AnthuriumReservoir = 1
};

struct RenderIntent {
  // Utility light
  float whiteLevel = 0.0f;

  // Expressive color
  float hue = 0.0f;         // normalized 0..1
  float saturation = 0.0f;  // normalized 0..1
  float rgbLevel = 0.0f;    // normalized 0..1

  // Motion / animation
  float animationRate = 0.0f;
  float phase = 0.0f;

  // Geometry / emphasis
  SegmentId emphasizedSegment = SegmentId::WholeObject;

  // Optional localized active blob (strip-address-space semantics).
  bool useLocalizedBlob = false;
  float blobCenter = 0.5f;    // normalized 0..1 along strip
  float blobWidth = 0.25f;    // normalized 0..1
  float blobSoftness = 1.2f;  // >= 1.0f; larger softens edge falloff



  // Topology-aware active scenes
  ActiveSceneMode activeSceneMode = ActiveSceneMode::LegacyBlob;
  uint32_t sceneNowMs = 0;
  float sceneCharge = 0.0f;      // 0..1 injected energy strength
  float sceneIngressLevel = 0.0f;
  float sceneFieldLevel = 0.0f;
  float sceneEnergyBoost = 0.0f;
  float sceneTargetRangeM = 0.0f;
  float sceneTargetRangeSmoothedM = 0.0f;
  float sceneChargeTarget = 0.0f;

  // Effect selection
  uint8_t effectId = 0;
  bool clearBeforeRender = true;
};
