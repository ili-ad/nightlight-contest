#pragma once
#include <stdint.h>

enum class SegmentId : uint8_t {
  Ring,
  LeftStamen,
  RightStamen,
  WholeObject
};

enum class InterludeReason : uint8_t {
  None,
  SensorJitter,
  SensorDropout,
  ManualTest
};

struct SegmentRange {
  uint16_t start;
  uint16_t count;

  SegmentRange() : start(0), count(0) {}
  SegmentRange(uint16_t s, uint16_t c) : start(s), count(c) {}

  uint16_t endExclusive() const {
    return start + count;
  }

  bool contains(uint16_t pixelIndex) const {
    return pixelIndex >= start && pixelIndex < endExclusive();
  }
};