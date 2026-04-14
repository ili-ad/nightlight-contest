#include <Adafruit_NeoPixel.h>

#define LED_PIN    6
#define LED_COUNT  144   // or whatever the strip actually is
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGBW + NEO_KHZ800);

void lightFirstN(int n, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
  strip.clear();

  if (n > LED_COUNT) {
    n = LED_COUNT;
  }

  for (int i = 0; i < n; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b, w));
  }

  strip.show();
}

void holdStage(int n, uint8_t w) {
  lightFirstN(n, 0, 0, 0, w);
  delay(1200);
}

void setup() {
  strip.begin();
  strip.setBrightness(20);   // deliberately conservative
  strip.clear();
  strip.show();
}

void loop() {
  holdStage(10, 70);
  holdStage(20, 70);
  holdStage(40, 70);
  holdStage(80, 70);
  holdStage(LED_COUNT, 40);  // full strip, lower white value

  strip.clear();
  strip.show();
  delay(2000);
}