#include "C4001TrackFilter.h"

#include <math.h>

namespace {
C4001TrackFilter::Output makeEmpty() {
  return {};
}

float clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

float phaseInfluence(C4001TrackFilter::InputClass inputClass, float decayScale) {
  if (inputClass == C4001TrackFilter::InputClass::Valid) {
    return 1.0f;
  }
  if (inputClass == C4001TrackFilter::InputClass::SoftReject) {
    return 0.88f;
  }
  return clamp01(decayScale);
}
}  // namespace

void C4001TrackFilter::configure(uint32_t holdMs, float decayPerSecond, float decayFloor) {
  mHoldMs = holdMs;
  mDecayPerSecond = decayPerSecond;
  mDecayFloor = decayFloor;
}

void C4001TrackFilter::reset() {
  mHasTrack = false;
  mLastAcceptedMs = 0;
  mHeld = {};
}

C4001TrackFilter::Output C4001TrackFilter::update(InputClass inputClass,
                                                  uint32_t nowMs,
                                                  const Sample* validSample) {
  if (inputClass == InputClass::Valid) {
    if (validSample == nullptr) {
      return makeEmpty();
    }

    mHasTrack = true;
    mLastAcceptedMs = nowMs;
    mHeld = *validSample;

    Output out;
    out.sample = mHeld;
    out.phase = Phase::Valid;
    out.ageMs = 0;
    out.visibility = 1.0f;
    out.influence = 1.0f;
    out.hasTrack = true;
    return out;
  }

  if (!mHasTrack) {
    return makeEmpty();
  }

  Output out;
  out.sample = mHeld;
  out.ageMs = (nowMs >= mLastAcceptedMs) ? (nowMs - mLastAcceptedMs) : 0;
  out.visibility = 1.0f;
  out.influence = 1.0f;
  out.hasTrack = true;

  if (inputClass == InputClass::SoftReject) {
    out.phase = Phase::SoftReject;
    out.influence = phaseInfluence(inputClass, 1.0f);
    return out;
  }

  const bool withinHold = (out.ageMs <= mHoldMs);
  if (withinHold) {
    out.phase = (inputClass == InputClass::LinkIssue) ? Phase::LinkIssue : Phase::Hold;
    return out;
  }

  const float ageSec = static_cast<float>(out.ageMs - mHoldMs) / 1000.0f;
  const float scale = applyDecayScale(ageSec, mDecayPerSecond, mDecayFloor);

  // Preserve range anchoring during dropout fade; only influence terms decay.
  out.sample.chargeTarget *= scale;
  out.sample.ingressTarget *= scale;
  out.sample.fieldTarget *= scale;
  out.sample.energyBoostTarget *= scale;
  out.sample.speedMps *= scale;
  out.sample.energyNorm *= scale;

  if (scale <= 0.0f) {
    mHasTrack = false;
    out = makeEmpty();
    return out;
  }

  out.phase = Phase::Decay;
  out.visibility = clamp01(scale);
  out.influence = phaseInfluence(inputClass, scale);
  return out;
}

float C4001TrackFilter::clamp01(float value) {
  return ::clamp01(value);
}

float C4001TrackFilter::applyDecayScale(float ageSec, float decayPerSecond, float decayFloor) {
  if (ageSec <= 0.0f || decayPerSecond <= 0.0f) {
    return 1.0f;
  }

  const float floor = clamp01(decayFloor);
  const float decayed = 1.0f - (ageSec * decayPerSecond);
  if (decayed > 1.0f) {
    return 1.0f;
  }
  if (decayed <= floor) {
    return 0.0f;
  }
  return decayed;
}
