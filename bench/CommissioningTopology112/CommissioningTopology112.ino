#include <Adafruit_NeoPixel.h>

// Bench-only commissioning sketch for final installed topology.
// Keeps diagnostics out of production NightlightContest.ino.

constexpr uint8_t LED_PIN = 6;
constexpr uint16_t LED_COUNT = 112;
constexpr uint8_t BRIGHTNESS = 24; // keep low on bench, 32 max

constexpr uint16_t J1_START = 0;
constexpr uint16_t J1_COUNT = 12;

constexpr uint16_t J2_START = 12;
constexpr uint16_t J2_COUNT = 12;

constexpr uint16_t FRONT_RING_START = 24;
constexpr uint16_t FRONT_RING_COUNT = 44;

constexpr uint16_t REAR_RING_START = 68;
constexpr uint16_t REAR_RING_COUNT = 44;

constexpr uint16_t STEP_DELAY_MS = 45;
constexpr uint16_t HOLD_MS = 450;

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

uint32_t cR(uint8_t v) { return strip.Color(v, 0, 0, 0); }
uint32_t cG(uint8_t v) { return strip.Color(0, v, 0, 0); }
uint32_t cB(uint8_t v) { return strip.Color(0, 0, v, 0); }
uint32_t cW(uint8_t v) { return strip.Color(0, 0, 0, v); }

void clearAll() {
  strip.clear();
  strip.show();
}

void fillRange(uint16_t start, uint16_t count, uint32_t color) {
  for (uint16_t i = 0; i < count; ++i) {
    strip.setPixelColor(start + i, color);
  }
  strip.show();
}

void progressiveFillAll() {
  clearAll();
  for (uint16_t i = 0; i < LED_COUNT; ++i) {
    strip.setPixelColor(i, cW(16));
    strip.show();
    delay(STEP_DELAY_MS);
  }
  delay(HOLD_MS);
}

void segmentFillTest() {
  clearAll();
  fillRange(J1_START, J1_COUNT, cR(24));
  delay(HOLD_MS);

  clearAll();
  fillRange(J2_START, J2_COUNT, cG(24));
  delay(HOLD_MS);

  clearAll();
  fillRange(FRONT_RING_START, FRONT_RING_COUNT, cB(24));
  delay(HOLD_MS);

  clearAll();
  fillRange(REAR_RING_START, REAR_RING_COUNT, cW(24));
  delay(HOLD_MS);
}

void directionTest(uint16_t start, uint16_t count, bool forward, uint32_t color) {
  clearAll();
  for (uint16_t i = 0; i < count; ++i) {
    uint16_t idx = forward ? (start + i) : (start + (count - 1 - i));
    strip.setPixelColor(idx, color);
    strip.show();
    delay(STEP_DELAY_MS);
    strip.setPixelColor(idx, 0);
  }
  strip.show();
  delay(HOLD_MS);
}

void segmentDirectionTest() {
  // Physical directions:
  // J1: top -> bottom
  directionTest(J1_START, J1_COUNT, true, cR(26));

  // J2: bottom -> top
  directionTest(J2_START, J2_COUNT, false, cG(26));

  // Front ring: 6 o'clock clockwise back to 6
  directionTest(FRONT_RING_START, FRONT_RING_COUNT, true, cB(26));

  // Rear ring: 6 o'clock counterclockwise back to 6
  directionTest(REAR_RING_START, REAR_RING_COUNT, false, cW(26));
}

void lowWhiteTest() {
  clearAll();
  fillRange(0, LED_COUNT, cW(18));
  delay(900);
  clearAll();
}

void rgbwChannelTest() {
  clearAll();
  fillRange(0, LED_COUNT, cR(18));
  delay(HOLD_MS);
  fillRange(0, LED_COUNT, cG(18));
  delay(HOLD_MS);
  fillRange(0, LED_COUNT, cB(18));
  delay(HOLD_MS);
  fillRange(0, LED_COUNT, cW(18));
  delay(HOLD_MS);
  clearAll();
}

void setup() {
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  clearAll();
  delay(250);
}

void loop() {
  progressiveFillAll();       // 1) 0 -> 111 physical order
  segmentFillTest();          // 2) segment fill
  segmentDirectionTest();     // 3) direction by segment
  lowWhiteTest();             // 4) low all-white RGBW
  rgbwChannelTest();          // 5) low R/G/B/W channel test
  delay(700);
}
