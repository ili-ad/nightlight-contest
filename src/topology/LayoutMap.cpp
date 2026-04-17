#include "LayoutMap.h"

#include "../config/Profiles.h"

LayoutMap::LayoutMap()
    : spans_{{
          // Assumed physical chain order for ARCH-061:
          // 1) ring, 2) left stamen, 3) right stamen.
          {Role::Ring, Profiles::kRingPixels, false, 0},
          {Role::LeftStamen, Profiles::kLeftStamenPixels, false, 0},
          // Right stamen is reversed relative to logical center->tip direction.
          {Role::RightStamen, Profiles::kRightStamenPixels, true, 0},
      }},
      totalPhysicalPixels_(0) {
  uint16_t cursor = 0;
  for (uint8_t i = 0; i < kSpanCount; ++i) {
    spans_[i].physicalStart = cursor;
    cursor += spans_[i].logicalCount;
  }

  totalPhysicalPixels_ = cursor;
}

uint16_t LayoutMap::ringToPhysical(uint16_t logicalPixel) const {
  return roleToPhysical(Role::Ring, logicalPixel);
}

uint16_t LayoutMap::leftStamenToPhysical(uint16_t logicalPixel) const {
  return roleToPhysical(Role::LeftStamen, logicalPixel);
}

uint16_t LayoutMap::rightStamenToPhysical(uint16_t logicalPixel) const {
  return roleToPhysical(Role::RightStamen, logicalPixel);
}

uint16_t LayoutMap::totalPhysicalPixels() const {
  return totalPhysicalPixels_;
}

uint16_t LayoutMap::roleToPhysical(Role role, uint16_t logicalPixel) const {
  uint16_t remaining = logicalPixel;

  for (uint8_t i = 0; i < kSpanCount; ++i) {
    const Span& span = spans_[i];
    if (span.role != role) {
      continue;
    }

    if (remaining >= span.logicalCount) {
      remaining -= span.logicalCount;
      continue;
    }

    if (span.reversed) {
      return span.physicalStart + (span.logicalCount - 1 - remaining);
    }

    return span.physicalStart + remaining;
  }

  return kInvalidPixel;
}
