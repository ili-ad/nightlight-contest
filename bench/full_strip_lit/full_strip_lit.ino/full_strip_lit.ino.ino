#include <Adafruit_NeoPixel.h>


constexpr uint8_t kPixelDataPin = 6;   // matches src/config/Pins.h
constexpr uint16_t kPixelCount = 144;  // bench strip
constexpr uint8_t kBrightness = 96;

Adafruit_NeoPixel strip(
    kPixelCount,
    kPixelDataPin,
    NEO_GRBW + NEO_KHZ800
);

void setup() {
  strip.begin();
  strip.setBrightness(kBrightness);
  strip.clear();

  for (uint16_t i = 0; i < strip.numPixels(); ++i) {
    strip.setPixelColor(i, strip.Color(0, 0, 0, 255));
  }

  strip.show();
}

void loop() {
}