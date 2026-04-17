#pragma once

#include <stdint.h>

class LayoutMap {
 public:
  enum class Role : uint8_t {
    Ring,
    LeftStamen,
    RightStamen,
  };

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
  static constexpr uint8_t kSpanCount = 3;

  uint16_t roleToPhysical(Role role, uint16_t logicalPixel) const;

  Span spans_[kSpanCount];
  uint16_t totalPhysicalPixels_;
};
