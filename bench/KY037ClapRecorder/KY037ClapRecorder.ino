#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <math.h>

// KY-037 Clap Recorder
//
// Purpose:
//   This is a data-capture companion to KY037ClapSmoke.ino. It does not try to
//   decide whether a clap happened. Instead, it gives a visible cue every few
//   seconds, records the microphone waveform around that cue, and prints a CSV-ish
//   log block that can be pasted back into ChatGPT or a spreadsheet for threshold
//   tuning.
//
// Use:
//   1. Open Serial Monitor at 115200 baud.
//   2. Wait for the strip to turn RED.
//   3. Clap once, or clap the same double-clap pattern you want the final project
//      to recognize.
//   4. Copy complete BEGIN_TRIAL ... END_TRIAL blocks for analysis.
//
// Hardware assumptions are intentionally copied from the existing Clap Smoke sketch.

namespace Pins {
constexpr uint8_t kMicAnalogPin = A0;
constexpr uint8_t kStripPin = 6;
}

constexpr uint16_t kPixels = 144;
constexpr uint8_t kBrightness = 30;

// Visual cue timing.
constexpr uint32_t kFirstCueDelayMs = 5000UL;
constexpr uint32_t kCuePeriodMs = 10000UL;

// Capture timing.
// 1 ms sampling is fast enough to catch the large clap impulse while keeping the
// serial log and SRAM footprint manageable on an Arduino Nano Every.
constexpr uint32_t kSampleIntervalUs = 1000UL;
constexpr uint16_t kPreSamples = 256;    // ~256 ms before red cue
constexpr uint16_t kPostSamples = 1280;  // ~1.28 s after red cue
constexpr uint16_t kTotalSamples = kPreSamples + kPostSamples;

// Ignore the first few milliseconds after the red cue when calculating peak
// suggestions. This prevents the NeoPixel state change itself from becoming the
// "loudest" event in the summary.
constexpr uint16_t kIgnoreCueSettleSamples = 50;  // ~50 ms
constexpr uint8_t kP2PWindowSamples = 12;         // ~12 ms local peak-to-peak window

Adafruit_NeoPixel gStrip(kPixels, Pins::kStripPin, NEO_GRBW + NEO_KHZ800);

// During idle, the first kPreSamples entries are used as a circular pre-roll.
// During capture, entries kPreSamples..kTotalSamples-1 receive post-cue samples.
uint16_t gSamples[kTotalSamples];
uint16_t gRingIndex = 0;
uint16_t gPrimeCount = 0;
uint16_t gTrial = 0;
uint32_t gNextIdleSampleUs = 0;
uint32_t gNextCueMs = 0;
uint32_t gLastIdleRenderMs = 0;

static void fillColor(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
  for (uint16_t i = 0; i < kPixels; ++i) {
    gStrip.setPixelColor(i, gStrip.Color(r, g, b, w));
  }
  gStrip.show();
}

static void renderIdle(uint32_t nowMs) {
  if (nowMs - gLastIdleRenderMs < 250UL) return;
  gLastIdleRenderMs = nowMs;

  fillColor(0, 0, 8, 0);

  // Small white progress tick toward the next red cue.
  const uint32_t remaining = (nowMs < gNextCueMs) ? (gNextCueMs - nowMs) : 0UL;
  const uint32_t elapsed = (remaining >= kCuePeriodMs) ? 0UL : (kCuePeriodMs - remaining);
  const uint16_t tick = static_cast<uint16_t>((static_cast<uint32_t>(kPixels - 1) * elapsed) / kCuePeriodMs);
  gStrip.setPixelColor(tick, gStrip.Color(0, 0, 0, 38));
  gStrip.show();
}

static void waitUntilUs(uint32_t targetUs) {
  while (static_cast<int32_t>(micros() - targetUs) < 0) {
    // Tight loop on purpose: this is the actual recorder clock.
  }
}

static void sampleIdlePreRoll() {
  const uint32_t nowUs = micros();
  if (static_cast<int32_t>(nowUs - gNextIdleSampleUs) < 0) return;

  // Keep the pre-roll roughly fresh, but do not spend forever catching up after
  // LED updates or serial work.
  uint8_t catchup = 0;
  while (static_cast<int32_t>(micros() - gNextIdleSampleUs) >= 0 && catchup < 4) {
    gSamples[gRingIndex] = analogRead(Pins::kMicAnalogPin);
    gRingIndex = static_cast<uint16_t>((gRingIndex + 1) % kPreSamples);
    if (gPrimeCount < kPreSamples) ++gPrimeCount;
    gNextIdleSampleUs += kSampleIntervalUs;
    ++catchup;
  }

  if (catchup >= 4) {
    gNextIdleSampleUs = micros() + kSampleIntervalUs;
  }
}

static uint16_t logicalRawAt(uint16_t logicalIndex, uint16_t ringHead) {
  if (logicalIndex < kPreSamples) {
    return gSamples[(ringHead + logicalIndex) % kPreSamples];
  }
  return gSamples[logicalIndex];
}

static int32_t logicalTimeUs(uint16_t logicalIndex) {
  return (static_cast<int32_t>(logicalIndex) - static_cast<int32_t>(kPreSamples)) *
         static_cast<int32_t>(kSampleIntervalUs);
}

static float computeBaseline(uint16_t ringHead) {
  uint32_t sum = 0;
  for (uint16_t i = 0; i < kPreSamples; ++i) {
    sum += logicalRawAt(i, ringHead);
  }
  return static_cast<float>(sum) / static_cast<float>(kPreSamples);
}

static uint16_t localPeakToPeak(uint16_t centerIndex, uint16_t ringHead) {
  const int16_t half = static_cast<int16_t>(kP2PWindowSamples / 2);
  int16_t start = static_cast<int16_t>(centerIndex) - half;
  int16_t end = start + static_cast<int16_t>(kP2PWindowSamples);

  if (start < 0) start = 0;
  if (end > static_cast<int16_t>(kTotalSamples)) end = static_cast<int16_t>(kTotalSamples);

  uint16_t minRaw = 1023;
  uint16_t maxRaw = 0;
  for (int16_t i = start; i < end; ++i) {
    const uint16_t raw = logicalRawAt(static_cast<uint16_t>(i), ringHead);
    if (raw < minRaw) minRaw = raw;
    if (raw > maxRaw) maxRaw = raw;
  }
  return maxRaw - minRaw;
}

static void printSummary(uint16_t ringHead, uint32_t cueMs, uint32_t captureEndMs) {
  const float baseline = computeBaseline(ringHead);

  float preDevSum = 0.0f;
  float preDevMax = 0.0f;
  uint16_t preP2PMax = 0;

  for (uint16_t i = 0; i < kPreSamples; ++i) {
    const float dev = fabsf(static_cast<float>(logicalRawAt(i, ringHead)) - baseline);
    preDevSum += dev;
    if (dev > preDevMax) preDevMax = dev;

    const uint16_t p2p = localPeakToPeak(i, ringHead);
    if (p2p > preP2PMax) preP2PMax = p2p;
  }

  float postPeakDev = 0.0f;
  int32_t postPeakDevUs = 0;
  uint16_t postP2PMax = 0;
  int32_t postP2PUs = 0;
  uint16_t postRawMin = 1023;
  uint16_t postRawMax = 0;

  const uint16_t firstPostForStats = kPreSamples + kIgnoreCueSettleSamples;
  for (uint16_t i = firstPostForStats; i < kTotalSamples; ++i) {
    const uint16_t raw = logicalRawAt(i, ringHead);
    if (raw < postRawMin) postRawMin = raw;
    if (raw > postRawMax) postRawMax = raw;

    const float dev = fabsf(static_cast<float>(raw) - baseline);
    if (dev > postPeakDev) {
      postPeakDev = dev;
      postPeakDevUs = logicalTimeUs(i);
    }

    const uint16_t p2p = localPeakToPeak(i, ringHead);
    if (p2p > postP2PMax) {
      postP2PMax = p2p;
      postP2PUs = logicalTimeUs(i);
    }
  }

  const float preDevMean = preDevSum / static_cast<float>(kPreSamples);

  Serial.print(F("BEGIN_TRIAL,")); Serial.print(gTrial);
  Serial.print(F(",cue_ms,")); Serial.print(cueMs);
  Serial.print(F(",capture_end_ms,")); Serial.print(captureEndMs);
  Serial.print(F(",sample_us,")); Serial.print(kSampleIntervalUs);
  Serial.print(F(",pre_samples,")); Serial.print(kPreSamples);
  Serial.print(F(",post_samples,")); Serial.print(kPostSamples);
  Serial.print(F(",ignore_settle_ms,")); Serial.println(kIgnoreCueSettleSamples);

  Serial.print(F("SUMMARY,")); Serial.print(gTrial);
  Serial.print(F(",baseline,")); Serial.print(baseline, 2);
  Serial.print(F(",pre_dev_mean,")); Serial.print(preDevMean, 2);
  Serial.print(F(",pre_dev_max,")); Serial.print(preDevMax, 2);
  Serial.print(F(",pre_p2p_max,")); Serial.print(preP2PMax);
  Serial.print(F(",post_peak_dev,")); Serial.print(postPeakDev, 2);
  Serial.print(F(",post_peak_dev_ms,")); Serial.print(static_cast<float>(postPeakDevUs) / 1000.0f, 1);
  Serial.print(F(",post_p2p_max,")); Serial.print(postP2PMax);
  Serial.print(F(",post_p2p_ms,")); Serial.print(static_cast<float>(postP2PUs) / 1000.0f, 1);
  Serial.print(F(",post_raw_min,")); Serial.print(postRawMin);
  Serial.print(F(",post_raw_max,")); Serial.println(postRawMax);

  // These are not final detector values. They are breadcrumbs for the next pass.
  float devLift = postPeakDev - preDevMax;
  if (devLift < 0.0f) devLift = 0.0f;
  float p2pLift = static_cast<float>(postP2PMax) - static_cast<float>(preP2PMax);
  if (p2pLift < 0.0f) p2pLift = 0.0f;

  const float conservativeDevThreshold = preDevMax + (devLift * 0.35f);
  const float conservativeP2PThreshold = static_cast<float>(preP2PMax) + (p2pLift * 0.35f);

  Serial.print(F("SUGGESTION,")); Serial.print(gTrial);
  Serial.print(F(",dev_threshold_start,")); Serial.print(conservativeDevThreshold, 2);
  Serial.print(F(",p2p_threshold_start,")); Serial.println(conservativeP2PThreshold, 2);
}

static void printSamples(uint16_t ringHead) {
  const float baseline = computeBaseline(ringHead);
  float fastEnv = 0.0f;
  float slowEnv = 0.0f;

  Serial.println(F("COLUMNS,kind,trial,index,t_us,raw,dev,fast_env,slow_env,p2p12"));

  for (uint16_t i = 0; i < kTotalSamples; ++i) {
    const uint16_t raw = logicalRawAt(i, ringHead);
    const float dev = fabsf(static_cast<float>(raw) - baseline);

    // A deliberately responsive envelope for later comparison with the smoother
    // Clap Smoke detector. If raw spikes are huge but env is tiny, the detector
    // is being smoothed into deafness.
    fastEnv += (dev - fastEnv) * 0.35f;
    slowEnv += (fastEnv - slowEnv) * 0.12f;

    Serial.print(F("DATA,")); Serial.print(gTrial);
    Serial.print(','); Serial.print(static_cast<int16_t>(i) - static_cast<int16_t>(kPreSamples));
    Serial.print(','); Serial.print(logicalTimeUs(i));
    Serial.print(','); Serial.print(raw);
    Serial.print(','); Serial.print(dev, 2);
    Serial.print(','); Serial.print(fastEnv, 2);
    Serial.print(','); Serial.print(slowEnv, 2);
    Serial.print(','); Serial.println(localPeakToPeak(i, ringHead));
  }

  Serial.print(F("END_TRIAL,")); Serial.println(gTrial);
}

static void runTrial() {
  ++gTrial;
  const uint16_t ringHead = gRingIndex;

  fillColor(120, 0, 0, 0);  // red: clap now
  const uint32_t cueMs = millis();

  uint32_t nextSampleUs = micros();
  for (uint16_t i = 0; i < kPostSamples; ++i) {
    waitUntilUs(nextSampleUs);
    gSamples[kPreSamples + i] = analogRead(Pins::kMicAnalogPin);
    nextSampleUs += kSampleIntervalUs;
  }

  const uint32_t captureEndMs = millis();
  fillColor(32, 0, 32, 0);  // purple: printing log block

  printSummary(ringHead, cueMs, captureEndMs);
  printSamples(ringHead);
  Serial.println();
  Serial.flush();

  fillColor(0, 0, 8, 0);
  gNextCueMs = millis() + kCuePeriodMs;
  gNextIdleSampleUs = micros() + kSampleIntervalUs;
}

void setup() {
  Serial.begin(115200);

  gStrip.begin();
  gStrip.setBrightness(kBrightness);
  gStrip.clear();
  gStrip.show();

  pinMode(Pins::kMicAnalogPin, INPUT);

  delay(250);
  fillColor(0, 0, 8, 0);

  gNextIdleSampleUs = micros() + kSampleIntervalUs;
  gNextCueMs = millis() + kFirstCueDelayMs;

  Serial.println(F("KY037 Clap Recorder"));
  Serial.println(F("Open Serial Monitor at 115200 baud."));
  Serial.println(F("When the strip turns RED, clap once or perform the target double-clap pattern."));
  Serial.println(F("Copy complete BEGIN_TRIAL ... END_TRIAL blocks for tuning."));
  Serial.println(F("Send 'c' in Serial Monitor to force an immediate cue."));
  Serial.println();
}

void loop() {
  sampleIdlePreRoll();

  if (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == 'c' || c == 'C') {
      if (gPrimeCount >= kPreSamples) runTrial();
      return;
    }
  }

  const uint32_t nowMs = millis();
  renderIdle(nowMs);

  if (gPrimeCount >= kPreSamples && static_cast<int32_t>(nowMs - gNextCueMs) >= 0) {
    runTrial();
  }
}
