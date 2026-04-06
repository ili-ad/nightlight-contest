#include "PixelPaths.h"
#include "PixelTopology.h"

uint16_t PixelPaths::ringClockwiseLength() {
  return PixelTopology::ring().count;
}

uint16_t PixelPaths::ringClockwiseAt(uint16_t step) {
  SegmentRange range = PixelTopology::ring();
  if (range.count == 0) {
    return 0;
  }

  return range.start + (step % range.count);
}

uint16_t PixelPaths::leftStamenCenterToTipLength() {
  return PixelTopology::leftStamen().count;
}

uint16_t PixelPaths::leftStamenCenterToTipAt(uint16_t step) {
  SegmentRange range = PixelTopology::leftStamen();
  if (range.count == 0) {
    return 0;
  }

  if (step >= range.count) {
    step = range.count - 1;
  }

  return range.start + step;
}

uint16_t PixelPaths::rightStamenTipToCenterLength() {
  return PixelTopology::rightStamen().count;
}

uint16_t PixelPaths::rightStamenTipToCenterAt(uint16_t step) {
  SegmentRange range = PixelTopology::rightStamen();
  if (range.count == 0) {
    return 0;
  }

  if (step >= range.count) {
    step = range.count - 1;
  }

  return range.start + (range.count - 1 - step);
}

uint16_t PixelPaths::startupPathLength() {
  return ringClockwiseLength()
       + leftStamenCenterToTipLength()
       + rightStamenTipToCenterLength();
}

uint16_t PixelPaths::startupPixelAt(uint16_t step) {
  const uint16_t ringLen = ringClockwiseLength();
  if (step < ringLen) {
    return ringClockwiseAt(step);
  }
  step -= ringLen;

  const uint16_t leftLen = leftStamenCenterToTipLength();
  if (step < leftLen) {
    return leftStamenCenterToTipAt(step);
  }
  step -= leftLen;

  return rightStamenTipToCenterAt(step);
}