#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

// KY037TrimTuner.ino
// Purpose: help tune the blue KY-037 trimmer/potentiometer.
//
// Wiring:
//   KY-037 AO  -> A0
//   KY-037 D0  -> D2  (optional but strongly recommended for trimmer tuning)
//   KY-037 GND -> Arduino GND
//   KY-037 VCC -> Arduino 5V
//   NeoPixel data -> D6
//
// What to do:
//   1. Open Serial Monitor at 115200 baud.
//   2. Mark the current trimmer position before turning.
//   3. Every 10 seconds the strip turns red.
//   4. Clap once when it turns red.
//   5. Read the TRIM_TRIAL summary line.
//
// Why this sketch exists:
//   The KY-037 blue trimmer usually adjusts the digital comparator threshold
//   for D0, not the analog AO amplitude. This sketch watches both AO and D0
//   so you can see whether turning the screw actually helps the usable signal.

namespace Pins {
constexpr uint8_t kMicAnalogPin = A0;
constexpr uint8_t kMicDigitalPin = 2;
constexpr uint8_t kStripPin = 6;
}

constexpr uint16_t kPixels = 144;
constexpr uint8_t kBrightness = 24;

constexpr uint16_t kPreSamples = 256;
constexpr uint16_t kPostSamples = 1280;
constexpr uint16_t kWindowSamples = 12;
constexpr uint32_t kSampleUs = 1000;
constexpr uint32_t kTrialIntervalMs = 10000;

Adafruit_NeoPixel gStrip(kPixels, Pins::kStripPin, NEO_GRBW + NEO_KHZ800);

struct PreSample {
  uint16_t raw;
  uint8_t d0;
};

PreSample gPre[kPreSamples];
uint16_t gPreHead = 0;
bool gPreFilled = false;
uint32_t gLastSampleUs = 0;
uint32_t gLastTrialMs = 0;
uint16_t gTrial = 0;

static void fillStrip(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
  for (uint16_t i = 0; i < kPixels; ++i) {
    gStrip.setPixelColor(i, gStrip.Color(r, g, b, w));
  }
  gStrip.show();
}

static void samplePreRingIfDue() {
  const uint32_t nowUs = micros();
  if (nowUs - gLastSampleUs < kSampleUs) return;
  gLastSampleUs += kSampleUs;

  gPre[gPreHead].raw = analogRead(Pins::kMicAnalogPin);
  gPre[gPreHead].d0 = digitalRead(Pins::kMicDigitalPin) ? 1 : 0;
  gPreHead = (gPreHead + 1) % kPreSamples;
  if (gPreHead == 0) gPreFilled = true;
}

static uint16_t preIndexOldest(uint16_t i) {
  // Return the ring index for the i-th oldest pre sample.
  return (gPreHead + i) % kPreSamples;
}

static void runTrial() {
  if (!gPreFilled) return;

  ++gTrial;

  uint32_t preSum = 0;
  uint16_t preMin = 1023;
  uint16_t preMax = 0;
  uint16_t preD0High = 0;
  uint16_t preD0Low = 0;
  uint16_t preD0Edges = 0;
  uint8_t prevPreD0 = gPre[preIndexOldest(0)].d0;

  for (uint16_t i = 0; i < kPreSamples; ++i) {
    const PreSample &s = gPre[preIndexOldest(i)];
    preSum += s.raw;
    if (s.raw < preMin) preMin = s.raw;
    if (s.raw > preMax) preMax = s.raw;
    if (s.d0) ++preD0High; else ++preD0Low;
    if (i > 0 && s.d0 != prevPreD0) ++preD0Edges;
    prevPreD0 = s.d0;
  }

  const float baseline = static_cast<float>(preSum) / static_cast<float>(kPreSamples);

  float preDevSum = 0.0f;
  float preDevMax = 0.0f;
  for (uint16_t i = 0; i < kPreSamples; ++i) {
    const float dev = fabsf(static_cast<float>(gPre[preIndexOldest(i)].raw) - baseline);
    preDevSum += dev;
    if (dev > preDevMax) preDevMax = dev;
  }
  const float preDevMean = preDevSum / static_cast<float>(kPreSamples);
  const uint16_t preP2P = preMax - preMin;

  fillStrip(90, 0, 0, 0);  // red cue: clap now

  uint16_t postMin = 1023;
  uint16_t postMax = 0;
  float postPeakDev = 0.0f;
  int16_t postPeakDevMs = -1;
  uint16_t postD0High = 0;
  uint16_t postD0Low = 0;
  uint16_t postD0Edges = 0;
  int16_t firstD0EdgeMs = -1;
  uint8_t prevD0 = digitalRead(Pins::kMicDigitalPin) ? 1 : 0;

  uint16_t win[kWindowSamples];
  uint8_t winCount = 0;
  uint8_t winHead = 0;
  uint16_t postP2PMax = 0;
  int16_t postP2PMaxMs = -1;

  uint32_t nextSampleUs = micros();
  for (uint16_t i = 0; i < kPostSamples; ++i) {
    while (static_cast<int32_t>(micros() - nextSampleUs) < 0) {
      // tight wait for fixed cadence
    }
    nextSampleUs += kSampleUs;

    const uint16_t raw = analogRead(Pins::kMicAnalogPin);
    const uint8_t d0 = digitalRead(Pins::kMicDigitalPin) ? 1 : 0;

    if (raw < postMin) postMin = raw;
    if (raw > postMax) postMax = raw;

    const float dev = fabsf(static_cast<float>(raw) - baseline);
    if (dev > postPeakDev) {
      postPeakDev = dev;
      postPeakDevMs = static_cast<int16_t>(i);
    }

    if (d0) ++postD0High; else ++postD0Low;
    if (d0 != prevD0) {
      ++postD0Edges;
      if (firstD0EdgeMs < 0) firstD0EdgeMs = static_cast<int16_t>(i);
    }
    prevD0 = d0;

    win[winHead] = raw;
    winHead = (winHead + 1) % kWindowSamples;
    if (winCount < kWindowSamples) ++winCount;

    uint16_t wMin = 1023;
    uint16_t wMax = 0;
    for (uint8_t j = 0; j < winCount; ++j) {
      if (win[j] < wMin) wMin = win[j];
      if (win[j] > wMax) wMax = win[j];
    }
    const uint16_t p2p = wMax - wMin;
    if (p2p > postP2PMax) {
      postP2PMax = p2p;
      postP2PMaxMs = static_cast<int16_t>(i);
    }
  }

  fillStrip(0, 0, 28, 0);  // blue standby

  Serial.print("TRIM_TRIAL,");
  Serial.print(gTrial);
  Serial.print(",baseline,"); Serial.print(baseline, 2);
  Serial.print(",pre_raw_min,"); Serial.print(preMin);
  Serial.print(",pre_raw_max,"); Serial.print(preMax);
  Serial.print(",pre_p2p,"); Serial.print(preP2P);
  Serial.print(",pre_dev_mean,"); Serial.print(preDevMean, 2);
  Serial.print(",pre_dev_max,"); Serial.print(preDevMax, 2);
  Serial.print(",post_raw_min,"); Serial.print(postMin);
  Serial.print(",post_raw_max,"); Serial.print(postMax);
  Serial.print(",post_peak_dev,"); Serial.print(postPeakDev, 2);
  Serial.print(",post_peak_dev_ms,"); Serial.print(postPeakDevMs);
  Serial.print(",post_p2p_max,"); Serial.print(postP2PMax);
  Serial.print(",post_p2p_ms,"); Serial.print(postP2PMaxMs);
  Serial.print(",pre_d0_high,"); Serial.print(preD0High);
  Serial.print(",pre_d0_low,"); Serial.print(preD0Low);
  Serial.print(",pre_d0_edges,"); Serial.print(preD0Edges);
  Serial.print(",post_d0_high,"); Serial.print(postD0High);
  Serial.print(",post_d0_low,"); Serial.print(postD0Low);
  Serial.print(",post_d0_edges,"); Serial.print(postD0Edges);
  Serial.print(",first_d0_edge_ms,"); Serial.println(firstD0EdgeMs);

  Serial.print("READ_ME,");
  Serial.print(gTrial);
  Serial.print(",analog_lift,"); Serial.print(postPeakDev - preDevMax, 2);
  Serial.print(",d0_was_quiet,"); Serial.print(preD0Edges == 0 ? 1 : 0);
  Serial.print(",d0_changed_after_cue,"); Serial.print(postD0Edges > 0 ? 1 : 0);
  Serial.println(",goal,D0 quiet before cue and changes on clap; analog_lift should be positive");

  // Reset the pre-ring timing after the blocking capture.
  gLastSampleUs = micros();
}

void setup() {
  Serial.begin(115200);
  pinMode(Pins::kMicAnalogPin, INPUT);
  pinMode(Pins::kMicDigitalPin, INPUT);

  gStrip.begin();
  gStrip.setBrightness(kBrightness);
  fillStrip(0, 0, 28, 0);

  gLastSampleUs = micros();
  gLastTrialMs = millis();

  Serial.println("KY037 Trim Tuner");
  Serial.println("AO -> A0, optional but recommended D0 -> D2, strip -> D6");
  Serial.println("Every 10s: red cue = clap once. Turn the blue trimmer in small increments.");
  Serial.println("Goal: D0 quiet before cue, D0 changes on clap, analog_lift positive.");
  Serial.println("Send 'c' in Serial Monitor to force a trial immediately after pre-buffer fills.");
}

void loop() {
  samplePreRingIfDue();

  const uint32_t now = millis();
  if (Serial.available()) {
    const char c = Serial.read();
    if (c == 'c' || c == 'C') {
      runTrial();
      gLastTrialMs = millis();
      return;
    }
  }

  if (now - gLastTrialMs >= kTrialIntervalMs) {
    runTrial();
    gLastTrialMs = millis();
  }
}
