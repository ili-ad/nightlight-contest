#include "PixelTopology.h"
#include "../BuildConfig.h"

SegmentRange PixelTopology::ring() {
  SegmentRange range;
  range.start = 0;
  range.count = BuildConfig::kRingPixels;
  return range;
}

SegmentRange PixelTopology::leftStamen() {
  SegmentRange range;
  range.start = BuildConfig::kRingPixels;
  range.count = BuildConfig::kLeftStamenPixels;
  return range;
}

SegmentRange PixelTopology::rightStamen() {
  SegmentRange range;
  range.start = BuildConfig::kRingPixels + BuildConfig::kLeftStamenPixels;
  range.count = BuildConfig::kRightStamenPixels;
  return range;
}

uint16_t PixelTopology::totalPixels() {
  return BuildConfig::kTotalPixels;
}

bool PixelTopology::isValidPixel(uint16_t pixelIndex) {
  return pixelIndex < totalPixels();
}

bool PixelTopology::isRing(uint16_t pixelIndex) {
  return ring().contains(pixelIndex);
}

bool PixelTopology::isLeftStamen(uint16_t pixelIndex) {
  return leftStamen().contains(pixelIndex);
}

bool PixelTopology::isRightStamen(uint16_t pixelIndex) {
  return rightStamen().contains(pixelIndex);
}