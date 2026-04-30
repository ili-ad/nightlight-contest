#include <Adafruit_NeoPixel.h>

constexpr uint8_t LED_PIN = 6;
constexpr uint16_t LED_COUNT = 144;

// Your strip is SK6812 RGBW.
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

constexpr uint8_t BRIGHTNESS = 32;   // Low for bench testing. Raise later if needed.
constexpr uint16_t STEP_MS = 35;

void allOff() {
  strip.clear();
  strip.show();
}

uint32_t testWhite(uint8_t level) {
  // W channel only: R, G, B, W
  return strip.Color(0, 0, 0, level);
}

void movingDot(uint8_t level) {
  for (uint16_t i = 0; i < LED_COUNT; i++) {
    strip.clear();
    strip.setPixelColor(i, testWhite(level));
    strip.show();
    delay(STEP_MS);
  }
}

void progressiveFill(uint8_t level) {
  strip.clear();

  for (uint16_t count = 1; count <= LED_COUNT; count++) {
    for (uint16_t i = 0; i < count; i++) {
      strip.setPixelColor(i, testWhite(level));
    }

    strip.show();
    delay(STEP_MS);
  }

  delay(500);
  allOff();
  delay(300);
}

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  allOff();
  delay(300);
}

void loop() {
  // Very low draw diagnostic: one LED walks the full chain.
  movingDot(64);

  delay(300);

  // Higher draw diagnostic: fills from pixel 0 toward pixel 143.
  progressiveFill(48);

  delay(500);
}