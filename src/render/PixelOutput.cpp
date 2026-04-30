#include "PixelOutput.h"

#include "../config/Pins.h"

PixelOutput::PixelOutput(const LayoutMap& layoutMap)
    : layoutMap_(layoutMap),
      strip_(layoutMap.totalPhysicalPixels(), Pins::kPixelDataPin, NEO_GRBW + NEO_KHZ800) {}

void PixelOutput::begin() {
  strip_.begin();
  strip_.clear();
  strip_.show();
}

void PixelOutput::clear() {
  strip_.clear();
}

bool PixelOutput::setRightJPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return setMappedPixel(layoutMap_.rightJToPhysical(logicalPixel), r, g, b, w);
}

bool PixelOutput::setLeftJPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return setMappedPixel(layoutMap_.leftJToPhysical(logicalPixel), r, g, b, w);
}

bool PixelOutput::setFrontRingPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return setMappedPixel(layoutMap_.frontRingToPhysical(logicalPixel), r, g, b, w);
}

bool PixelOutput::setRearRingPixel(uint16_t logicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return setMappedPixel(layoutMap_.rearRingToPhysical(logicalPixel), r, g, b, w);
}

void PixelOutput::show() {
  strip_.show();
}

bool PixelOutput::setMappedPixel(uint16_t physicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (physicalPixel == LayoutMap::kInvalidPixel) {
    return false;
  }

  strip_.setPixelColor(physicalPixel, strip_.Color(r, g, b, w));
  return true;
}
