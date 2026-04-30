#include "PixelOutput.h"

#include "../config/Pins.h"
#include "../config/Profiles.h"

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

bool PixelOutput::setPhysicalPixel(uint16_t physicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  return setMappedPixel(physicalPixel, r, g, b, w);
}

uint16_t PixelOutput::physicalPixelCount() const {
  return layoutMap_.totalPhysicalPixels();
}

void PixelOutput::show() {
  strip_.show();
}

uint8_t PixelOutput::limitChannel(uint8_t value) const {
  const auto& profile = Profiles::output();
  float scaled = static_cast<float>(value) * profile.globalScale;
  if (scaled > static_cast<float>(profile.maxChannel)) {
    scaled = static_cast<float>(profile.maxChannel);
  }
  if (scaled < 0.0f) {
    scaled = 0.0f;
  }
  return static_cast<uint8_t>(scaled);
}

bool PixelOutput::setMappedPixel(uint16_t physicalPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  if (physicalPixel == LayoutMap::kInvalidPixel) {
    return false;
  }

  strip_.setPixelColor(physicalPixel,
                      strip_.Color(limitChannel(r), limitChannel(g), limitChannel(b), limitChannel(w)));
  return true;
}
