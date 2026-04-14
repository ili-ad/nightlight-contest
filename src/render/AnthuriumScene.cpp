#include "AnthuriumScene.h"

#include <math.h>
#include "../BuildConfig.h"
#include "../topology/PixelTopology.h"

namespace {
  float clamp01(float value) {
    if (value < 0.0f) {
      return 0.0f;
    }
    if (value > 1.0f) {
      return 1.0f;
    }
    return value;
  }

  uint8_t toByte(float value) {
    return static_cast<uint8_t>(clamp01(value) * 255.0f);
  }

  void hsvToRgb(float hue, float saturation, float value, uint8_t& r, uint8_t& g, uint8_t& b) {
    const float s = clamp01(saturation);
    const float v = clamp01(value);
    float h = hue;

    while (h < 0.0f) {
      h += 1.0f;
    }
    while (h >= 1.0f) {
      h -= 1.0f;
    }

    const float scaled = h * 6.0f;
    const int sector = static_cast<int>(scaled);
    const float fraction = scaled - sector;
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - (s * fraction));
    const float t = v * (1.0f - (s * (1.0f - fraction)));

    float rf = v;
    float gf = t;
    float bf = p;

    switch (sector % 6) {
      case 0: rf = v; gf = t; bf = p; break;
      case 1: rf = q; gf = v; bf = p; break;
      case 2: rf = p; gf = v; bf = t; break;
      case 3: rf = p; gf = q; bf = v; break;
      case 4: rf = t; gf = p; bf = v; break;
      default: rf = v; gf = p; bf = q; break;
    }

    r = toByte(rf);
    g = toByte(gf);
    b = toByte(bf);
  }
}

void AnthuriumScene::reset() {
  mInitialized = false;
  mLastNowMs = 0;
  mIngressEmitAccumulatorMs = 0;

  for (uint8_t i = 0; i < kMaxIngressPulses; ++i) {
    mIngressPulses[i] = IngressPulse();
  }

  for (uint16_t i = 0; i < BuildConfig::kRingPixels; ++i) {
    mTorusCharge[i] = 0.0f;
  }
}

void AnthuriumScene::renderRgb(PixelBus& bus, const RenderIntent& intent) {
  updateDynamics(intent);
  writeFrame(bus, intent, false);
}

void AnthuriumScene::renderRgbw(PixelBus& bus, const RenderIntent& intent) {
  updateDynamics(intent);
  writeFrame(bus, intent, true);
}

void AnthuriumScene::emitIngressPulse(float pulseAmplitude) {
  uint8_t slot = kMaxIngressPulses;
  for (uint8_t i = 0; i < kMaxIngressPulses; ++i) {
    if (!mIngressPulses[i].active) {
      slot = i;
      break;
    }
  }

  if (slot == kMaxIngressPulses) {
    float weakest = mIngressPulses[0].amplitude;
    slot = 0;
    for (uint8_t i = 1; i < kMaxIngressPulses; ++i) {
      if (mIngressPulses[i].amplitude < weakest) {
        weakest = mIngressPulses[i].amplitude;
        slot = i;
      }
    }
  }

  mIngressPulses[slot].active = true;
  mIngressPulses[slot].progress = 0.0f;
  mIngressPulses[slot].amplitude = clamp01(pulseAmplitude);
}

void AnthuriumScene::updateDynamics(const RenderIntent& intent) {
  const uint16_t ringCount = BuildConfig::kRingPixels;
  if (ringCount == 0) {
    return;
  }

  if (!mInitialized) {
    mLastNowMs = intent.sceneNowMs;
    mInitialized = true;
  }

  uint32_t dtMs = intent.sceneNowMs - mLastNowMs;
  if (dtMs > 200) {
    dtMs = 200;
  }
  mLastNowMs = intent.sceneNowMs;

  const float dtSec = static_cast<float>(dtMs) / 1000.0f;

  if (dtMs > 0) {
    mIngressEmitAccumulatorMs += dtMs;
    const uint32_t emitPeriodMs = BuildConfig::kAnthuriumIngressEmitPeriodMs;
    while (mIngressEmitAccumulatorMs >= emitPeriodMs) {
      mIngressEmitAccumulatorMs -= emitPeriodMs;
      const float pulseAmplitude = intent.sceneCharge *
                                   (static_cast<float>(emitPeriodMs) / 1000.0f) *
                                   BuildConfig::kAnthuriumIngressPulseGain;
      emitIngressPulse(pulseAmplitude);
    }
  }

  float torusInput = 0.0f;
  const float travelSec = BuildConfig::kAnthuriumIngressTravelMs / 1000.0f;
  const float progressStep = (travelSec > 0.01f) ? (dtSec / travelSec) : 1.0f;

  for (uint8_t i = 0; i < kMaxIngressPulses; ++i) {
    if (!mIngressPulses[i].active) {
      continue;
    }

    mIngressPulses[i].progress += progressStep;
    if (mIngressPulses[i].progress >= 1.0f) {
      torusInput += mIngressPulses[i].amplitude * BuildConfig::kAnthuriumTorusAccumulationGain;
      mIngressPulses[i] = IngressPulse();
    }
  }

  float temp[BuildConfig::kRingPixels] = {0.0f};
  const float decay = expf(-dtSec / (BuildConfig::kAnthuriumTorusClearMs / 1000.0f));
  const float diffusion = BuildConfig::kAnthuriumTorusDiffusionPerSecond * dtSec;

  for (uint16_t i = 0; i < ringCount; ++i) {
    const uint16_t left = (i == 0) ? (ringCount - 1) : (i - 1);
    const uint16_t right = (i + 1) % ringCount;

    float value = mTorusCharge[i];
    value += (mTorusCharge[left] + mTorusCharge[right] - (2.0f * mTorusCharge[i])) * diffusion;
    value *= decay;
    temp[i] = clamp01(value);
  }

  const uint16_t ingressA = BuildConfig::kAnthuriumTorusIngressA % ringCount;
  const uint16_t ingressB = BuildConfig::kAnthuriumTorusIngressB % ringCount;
  const float spread = BuildConfig::kAnthuriumTorusIngressSpread;

  for (uint16_t i = 0; i < ringCount; ++i) {
    const float distA = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(ingressA)));
    const float distB = fabsf(static_cast<float>(static_cast<int16_t>(i) - static_cast<int16_t>(ingressB)));
    const float wrapDistA = (distA > (ringCount * 0.5f)) ? (ringCount - distA) : distA;
    const float wrapDistB = (distB > (ringCount * 0.5f)) ? (ringCount - distB) : distB;

    const float aWeight = expf(-(wrapDistA * wrapDistA) / (2.0f * spread * spread));
    const float bWeight = expf(-(wrapDistB * wrapDistB) / (2.0f * spread * spread));
    temp[i] = clamp01(temp[i] + (torusInput * (aWeight + bWeight)));
  }

  for (uint16_t i = 0; i < ringCount; ++i) {
    mTorusCharge[i] = temp[i];
  }
}

float AnthuriumScene::sampleStamenIngress(uint16_t stamenPixel, uint16_t stamenCount) const {
  if (stamenCount == 0) {
    return 0.0f;
  }

  const float stamenPos = static_cast<float>(stamenPixel) / static_cast<float>(stamenCount - 1);
  const float tipToEntry = 1.0f - stamenPos;
  float signal = 0.0f;

  for (uint8_t i = 0; i < kMaxIngressPulses; ++i) {
    if (!mIngressPulses[i].active) {
      continue;
    }

    const float delta = tipToEntry - mIngressPulses[i].progress;
    const float width = BuildConfig::kAnthuriumIngressPulseWidth;
    signal += mIngressPulses[i].amplitude * expf(-(delta * delta) / (2.0f * width * width));
  }

  return clamp01(signal);
}

float AnthuriumScene::sampleTorusField(uint16_t ringPixel, uint16_t ringCount) const {
  if (ringCount == 0) {
    return 0.0f;
  }

  const float base = BuildConfig::kAnthuriumTorusBaseFieldLevel;
  const float memory = mTorusCharge[ringPixel % ringCount];
  return clamp01(base + memory);
}

void AnthuriumScene::writeFrame(PixelBus& bus, const RenderIntent& intent, bool useWhite) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  hsvToRgb(intent.hue, intent.saturation, intent.rgbLevel, r, g, b);

  const SegmentRange ring = PixelTopology::ring();
  const SegmentRange left = PixelTopology::leftStamen();
  const SegmentRange right = PixelTopology::rightStamen();

  for (uint16_t i = 0; i < ring.count; ++i) {
    const uint16_t px = ring.start + i;
    const float field = sampleTorusField(i, ring.count);
    const float brightness = clamp01((field * intent.sceneFieldLevel) +
                                     (intent.sceneCharge * BuildConfig::kAnthuriumTorusInstantGain));

    const float white = clamp01((intent.whiteLevel + intent.sceneEnergyBoost) * field);

    if (useWhite) {
      bus.setRgbw(px,
                  toByte((static_cast<float>(r) / 255.0f) * brightness),
                  toByte((static_cast<float>(g) / 255.0f) * brightness),
                  toByte((static_cast<float>(b) / 255.0f) * brightness),
                  toByte(white));
    } else {
      uint8_t rr = toByte((static_cast<float>(r) / 255.0f) * brightness);
      uint8_t gg = toByte((static_cast<float>(g) / 255.0f) * brightness);
      uint8_t bb = toByte((static_cast<float>(b) / 255.0f) * brightness);
      const uint8_t whiteLift = static_cast<uint8_t>(toByte(white) / 3);
      rr = static_cast<uint8_t>(rr + ((255 - rr) < whiteLift ? (255 - rr) : whiteLift));
      gg = static_cast<uint8_t>(gg + ((255 - gg) < whiteLift ? (255 - gg) : whiteLift));
      bb = static_cast<uint8_t>(bb + ((255 - bb) < whiteLift ? (255 - bb) : whiteLift));
      bus.setRgb(px, rr, gg, bb);
    }
  }

  for (uint16_t i = 0; i < left.count; ++i) {
    const uint16_t px = left.start + i;
    const float ingress = sampleStamenIngress(i, left.count);
    const float brightness = clamp01((ingress * intent.sceneIngressLevel) +
                                     (BuildConfig::kAnthuriumStamenAmbientFloor * intent.sceneFieldLevel));
    const float white = clamp01((intent.whiteLevel + intent.sceneEnergyBoost) * ingress);

    if (useWhite) {
      bus.setRgbw(px,
                  toByte((static_cast<float>(r) / 255.0f) * brightness),
                  toByte((static_cast<float>(g) / 255.0f) * brightness),
                  toByte((static_cast<float>(b) / 255.0f) * brightness),
                  toByte(white));
    } else {
      uint8_t rr = toByte((static_cast<float>(r) / 255.0f) * brightness);
      uint8_t gg = toByte((static_cast<float>(g) / 255.0f) * brightness);
      uint8_t bb = toByte((static_cast<float>(b) / 255.0f) * brightness);
      const uint8_t whiteLift = static_cast<uint8_t>(toByte(white) / 3);
      rr = static_cast<uint8_t>(rr + ((255 - rr) < whiteLift ? (255 - rr) : whiteLift));
      gg = static_cast<uint8_t>(gg + ((255 - gg) < whiteLift ? (255 - gg) : whiteLift));
      bb = static_cast<uint8_t>(bb + ((255 - bb) < whiteLift ? (255 - bb) : whiteLift));
      bus.setRgb(px, rr, gg, bb);
    }
  }

  for (uint16_t i = 0; i < right.count; ++i) {
    const uint16_t px = right.start + i;
    const float ingress = sampleStamenIngress(i, right.count);
    const float brightness = clamp01((ingress * intent.sceneIngressLevel) +
                                     (BuildConfig::kAnthuriumStamenAmbientFloor * intent.sceneFieldLevel));
    const float white = clamp01((intent.whiteLevel + intent.sceneEnergyBoost) * ingress);

    if (useWhite) {
      bus.setRgbw(px,
                  toByte((static_cast<float>(r) / 255.0f) * brightness),
                  toByte((static_cast<float>(g) / 255.0f) * brightness),
                  toByte((static_cast<float>(b) / 255.0f) * brightness),
                  toByte(white));
    } else {
      uint8_t rr = toByte((static_cast<float>(r) / 255.0f) * brightness);
      uint8_t gg = toByte((static_cast<float>(g) / 255.0f) * brightness);
      uint8_t bb = toByte((static_cast<float>(b) / 255.0f) * brightness);
      const uint8_t whiteLift = static_cast<uint8_t>(toByte(white) / 3);
      rr = static_cast<uint8_t>(rr + ((255 - rr) < whiteLift ? (255 - rr) : whiteLift));
      gg = static_cast<uint8_t>(gg + ((255 - gg) < whiteLift ? (255 - gg) : whiteLift));
      bb = static_cast<uint8_t>(bb + ((255 - bb) < whiteLift ? (255 - bb) : whiteLift));
      bus.setRgb(px, rr, gg, bb);
    }
  }
}
