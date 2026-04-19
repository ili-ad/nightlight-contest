#pragma once

#include <stdint.h>

#include "../config/Profiles.h"

class LayoutMap {
 public:
  using Role = Profiles::SpanRole;

  struct Span {
    Role role;
    uint16_t logicalCount;
    bool reversed;
    uint16_t physicalStart;
  };

  static constexpr uint16_t kInvalidPixel = 0xFFFF;

  LayoutMap();

  uint16_t ringToPhysical(uint16_t logicalPixel) const;
  uint16_t leftStamenToPhysical(uint16_t logicalPixel) const;
  uint16_t rightStamenToPhysical(uint16_t logicalPixel) const;
  uint16_t totalPhysicalPixels() const;

 private:
  static constexpr uint8_t kSpanCount = Profiles::kTopologySpanCount;

  uint16_t roleToPhysical(Role role, uint16_t logicalPixel) const;

  Span spans_[kSpanCount];
  uint16_t totalPhysicalPixels_;
};
