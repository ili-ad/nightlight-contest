#include "LayoutMap.h"

LayoutMap::LayoutMap() : spans_{}, totalPhysicalPixels_(0) {
  const Profiles::TopologyProfile& topology = Profiles::topology();

  for (uint8_t i = 0; i < kSpanCount; ++i) {
    spans_[i].role = topology.spans[i].role;
    spans_[i].logicalCount = topology.spans[i].logicalCount;
    spans_[i].reversed = topology.spans[i].reversed;
  }

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
