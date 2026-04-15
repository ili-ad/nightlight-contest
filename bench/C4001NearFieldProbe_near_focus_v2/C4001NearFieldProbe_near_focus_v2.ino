
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_C4001.h>
#include <math.h>

// Forward declarations to keep the Arduino auto-prototyper from tripping over
// user-defined types when this sketch is opened on its own.
enum class Phase : uint8_t;
struct RawReading;
struct TrackSample;
struct GateDecision;
struct FilterOutput;
class CursorSmoother;


// -----------------------------------------------------------------------------
// C4001 Near-Field Hunter Probe
//
// Goal:
// - Favor the sweet spot for a hallway nightlight: roughly 0.75m to 3.2m.
// - Do not instantly collapse to zero on dropped frames / no-target potholes.
// - Let closer, repeated candidates "take over" from a stale far lock.
// - Make the display cursor move with inertia instead of teleporting.
// - Expose more useful bench logging for diagnosis.
// -----------------------------------------------------------------------------

namespace BuildConfig {
constexpr uint8_t kPixelDataPin = 6;
constexpr uint8_t kC4001I2cAddress = 0x2B;
constexpr uint16_t kTotalPixels = 77;
constexpr uint8_t kStatusPixels = 8;
constexpr uint8_t kGlobalBrightness = 26;
constexpr uint32_t kPrintIntervalMs = 50;

// Display mapping window.
// We still allow exploration a bit inside this, but the strip is focused here.
constexpr float kDisplayNearM = 0.75f;
constexpr float kDisplayFarM = 3.20f;

// Raw acceptance / probe policy.
constexpr float kAbsoluteMinRangeM = 0.06f;
constexpr float kFocusFarLimitM = 3.20f;
constexpr float kNearAutoAcquireM = 1.80f;
constexpr float kAcquireMinSpeedMps = 0.05f;
constexpr float kMaxAcceptedSpeedMps = 2.60f;

// Range-jump handling.
constexpr float kMaxAcceptedRangeDeltaPerSecond = 1.80f;
constexpr float kCloserTakeoverMinDeltaM = 0.18f;
constexpr uint8_t kCloserTakeoverCoherentSamples = 2;
constexpr float kCloserTakeoverBandM = 0.24f;

// Hold / fade policy for no-targets and soft rejects.
// This is intentionally more patient than the main app bench profile.
constexpr uint32_t kTrackFullHoldMs = 700;
constexpr uint32_t kTrackFadeMs = 1800;

// Cursor motion. Closer movement can be a bit quicker than retreat.
constexpr float kCursorApproachRateMps = 2.80f;
constexpr float kCursorRetreatRateMps = 1.80f;
constexpr uint8_t kRawCursorBrightInFocus = 96;
constexpr uint8_t kRawCursorBrightOutOfFocus = 28;
constexpr uint8_t kFilteredCursorBright = 180;

// Sensor cadence / startup.
constexpr uint32_t kPollIntervalMs = 35;
constexpr uint32_t kReinitIntervalMs = 1000;
}  // namespace BuildConfig

enum class Phase : uint8_t {
  Valid = 0,
  Hold = 1,
  SoftReject = 2,
  HardAbsent = 3,
  Decay = 4,
  Empty = 5,
  LinkIssue = 6
};

enum class RejectReason : uint8_t {
  None = 0,
  NoTarget = 1,
  ReadFailure = 2,
  SpeedCap = 3,
  OutOfWindow = 4,
  AcquireNeedsMotion = 5,
  RangeJump = 6,
  TakeoverPending = 7
};

struct RawReading {
  bool readOk = false;
  int targetNumber = 0;
  float rangeM = 0.0f;
  float speedMps = 0.0f;
  int energy = 0;
};

struct TrackSample {
  float rangeM = 0.0f;
  float speedMps = 0.0f;
  float energyNorm = 0.0f;
};

struct GateDecision {
  bool readOk = false;
  bool hasRawTarget = false;
  bool inFocusWindow = false;
  bool nearField = false;
  bool acceptedThisFrame = false;
  bool hasAcceptedTrack = false;
  bool pendingTakeover = false;
  bool acquireGatePassed = false;
  uint8_t pendingCount = 0;
  RejectReason reason = RejectReason::None;
  TrackSample acceptedSample{};
};

struct FilterOutput {
  TrackSample sample{};
  Phase phase = Phase::Empty;
  uint32_t ageMs = 0;
  bool hasTrack = false;
  uint8_t visibility = 0;
};

namespace {
DFRobot_C4001_I2C gRadar(&Wire, BuildConfig::kC4001I2cAddress);
Adafruit_NeoPixel gStrip(BuildConfig::kTotalPixels,
                         BuildConfig::kPixelDataPin,
                         NEO_GRBW + NEO_KHZ800);
uint32_t gLastPrintMs = 0;
}  // namespace

float clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

float clampf(float value, float lo, float hi) {
  if (value < lo) {
    return lo;
  }
  if (value > hi) {
    return hi;
  }
  return value;
}

int32_t mmFromMeters(float meters) {
  if (meters <= 0.0f) {
    return 0;
  }
  return static_cast<int32_t>(meters * 1000.0f);
}

float normalizeEnergy(int energy) {
  return clamp01(static_cast<float>(energy) / 100.0f);
}

uint16_t rangeToPixel(float rangeM) {
  const float clamped = clampf(rangeM, BuildConfig::kDisplayNearM, BuildConfig::kDisplayFarM);
  const float span = BuildConfig::kDisplayFarM - BuildConfig::kDisplayNearM;
  const float t = (span <= 0.001f) ? 0.0f : ((clamped - BuildConfig::kDisplayNearM) / span);
  const uint16_t dynamicPixels = (BuildConfig::kTotalPixels > BuildConfig::kStatusPixels)
                                     ? (BuildConfig::kTotalPixels - BuildConfig::kStatusPixels)
                                     : 1;
  const uint16_t rel = static_cast<uint16_t>(t * static_cast<float>(dynamicPixels - 1));
  return BuildConfig::kStatusPixels + rel;
}

bool isValidRange(float rangeM) {
  return (rangeM >= BuildConfig::kAbsoluteMinRangeM);
}

bool isInFocusWindow(float rangeM) {
  return isValidRange(rangeM) && (rangeM <= BuildConfig::kFocusFarLimitM);
}

bool passesAcquireGate(const RawReading& raw) {
  if (!isInFocusWindow(raw.rangeM)) {
    return false;
  }

  if (raw.rangeM <= BuildConfig::kNearAutoAcquireM) {
    return true;
  }

  return (fabsf(raw.speedMps) >= BuildConfig::kAcquireMinSpeedMps);
}

class PresenceProbe {
public:
  void begin() {
    Wire.begin();
    tryInit(millis());
  }

  RawReading read(uint32_t nowMs) {
    if (!mReady && ((mLastInitAttemptMs == 0) ||
                    ((nowMs - mLastInitAttemptMs) >= BuildConfig::kReinitIntervalMs))) {
      tryInit(nowMs);
    }

    RawReading out;
    if (!mReady) {
      out.readOk = false;
      return out;
    }

    if (mHasPolled && ((nowMs - mLastPollMs) < BuildConfig::kPollIntervalMs)) {
      return mLast;
    }

    mHasPolled = true;
    mLastPollMs = nowMs;

    out.readOk = true;
    out.targetNumber = gRadar.getTargetNumber();
    out.rangeM = gRadar.getTargetRange();
    out.speedMps = gRadar.getTargetSpeed();
    out.energy = gRadar.getTargetEnergy();

    // Guard against obviously broken values from the library path.
    if (out.rangeM < 0.0f || out.rangeM > 30.0f) {
      out.rangeM = 0.0f;
    }
    if (!isfinite(out.speedMps)) {
      out.speedMps = 0.0f;
    }

    mLast = out;
    return out;
  }

private:
  void tryInit(uint32_t nowMs) {
    mLastInitAttemptMs = nowMs;
    mReady = gRadar.begin();
    if (mReady) {
      gRadar.setSensorMode(eSpeedMode);
      gRadar.setDetectThres(11, 1200, 10);
      gRadar.setFrettingDetection(eON);
    }
  }

  bool mReady = false;
  bool mHasPolled = false;
  uint32_t mLastPollMs = 0;
  uint32_t mLastInitAttemptMs = 0;
  RawReading mLast{};
};

class AcquisitionGate {
public:
  void clearAccepted() {
    mHasAccepted = false;
    mLastAcceptedMs = 0;
    mAccepted = TrackSample();
    clearPending();
  }

  GateDecision update(const RawReading& raw, uint32_t nowMs) {
    GateDecision out;
    out.readOk = raw.readOk;
    out.hasAcceptedTrack = mHasAccepted;

    if (!raw.readOk) {
      out.reason = RejectReason::ReadFailure;
      out.pendingCount = mPendingCount;
      return out;
    }

    out.hasRawTarget = (raw.targetNumber > 0) && isValidRange(raw.rangeM);
    if (!out.hasRawTarget) {
      clearPending();
      out.reason = RejectReason::NoTarget;
      out.pendingCount = 0;
      out.hasAcceptedTrack = mHasAccepted;
      if (mHasAccepted) {
        out.acceptedSample = mAccepted;
      }
      return out;
    }

    out.inFocusWindow = isInFocusWindow(raw.rangeM);
    out.nearField = (raw.rangeM <= BuildConfig::kNearAutoAcquireM);
    out.acquireGatePassed = passesAcquireGate(raw);

    if (fabsf(raw.speedMps) > BuildConfig::kMaxAcceptedSpeedMps) {
      clearPending();
      out.reason = RejectReason::SpeedCap;
      out.pendingCount = 0;
      if (mHasAccepted) {
        out.acceptedSample = mAccepted;
      }
      return out;
    }

    if (!mHasAccepted) {
      if (!out.inFocusWindow) {
        out.reason = RejectReason::OutOfWindow;
        return out;
      }

      if (!out.acquireGatePassed) {
        out.reason = RejectReason::AcquireNeedsMotion;
        return out;
      }

      acceptRaw(raw, nowMs);
      out.acceptedThisFrame = true;
      out.hasAcceptedTrack = true;
      out.reason = RejectReason::None;
      out.acceptedSample = mAccepted;
      return out;
    }

    // Default: keep current accepted sample unless a new one survives the gate.
    out.acceptedSample = mAccepted;
    out.hasAcceptedTrack = true;

    if (!out.inFocusWindow) {
      clearPending();
      out.reason = RejectReason::OutOfWindow;
      return out;
    }

    const uint32_t dtMs = (mLastAcceptedMs == 0) ? BuildConfig::kPollIntervalMs
                                                 : (nowMs - mLastAcceptedMs);
    const float dtSec = static_cast<float>((dtMs == 0) ? 1 : dtMs) / 1000.0f;
    const float maxDelta = BuildConfig::kMaxAcceptedRangeDeltaPerSecond * dtSec;
    const float delta = fabsf(raw.rangeM - mAccepted.rangeM);

    if (delta <= maxDelta) {
      acceptRaw(raw, nowMs);
      out.acceptedThisFrame = true;
      out.reason = RejectReason::None;
      out.acceptedSample = mAccepted;
      clearPending();
      return out;
    }

    const bool closerTakeover =
        (raw.rangeM < (mAccepted.rangeM - BuildConfig::kCloserTakeoverMinDeltaM)) &&
        out.acquireGatePassed;

    if (closerTakeover) {
      if (mHasPending &&
          (fabsf(raw.rangeM - mPending.rangeM) <= BuildConfig::kCloserTakeoverBandM)) {
        ++mPendingCount;
        mPending.rangeM = (mPending.rangeM + raw.rangeM) * 0.5f;
        mPending.speedMps = (mPending.speedMps + raw.speedMps) * 0.5f;
        mPending.energyNorm = (mPending.energyNorm + normalizeEnergy(raw.energy)) * 0.5f;
      } else {
        mHasPending = true;
        mPendingCount = 1;
        mPending.rangeM = raw.rangeM;
        mPending.speedMps = raw.speedMps;
        mPending.energyNorm = normalizeEnergy(raw.energy);
      }

      out.pendingTakeover = true;
      out.pendingCount = mPendingCount;
      out.reason = RejectReason::TakeoverPending;

      if (mPendingCount >= BuildConfig::kCloserTakeoverCoherentSamples) {
        mAccepted = mPending;
        mHasAccepted = true;
        mLastAcceptedMs = nowMs;
        out.acceptedThisFrame = true;
        out.pendingTakeover = false;
        out.pendingCount = 0;
        out.reason = RejectReason::None;
        out.acceptedSample = mAccepted;
        clearPending();
      }

      return out;
    }

    clearPending();
    out.reason = RejectReason::RangeJump;
    return out;
  }

private:
  void acceptRaw(const RawReading& raw, uint32_t nowMs) {
    mAccepted.rangeM = raw.rangeM;
    mAccepted.speedMps = raw.speedMps;
    mAccepted.energyNorm = normalizeEnergy(raw.energy);
    mHasAccepted = true;
    mLastAcceptedMs = nowMs;
  }

  void clearPending() {
    mHasPending = false;
    mPendingCount = 0;
    mPending = TrackSample();
  }

  bool mHasAccepted = false;
  uint32_t mLastAcceptedMs = 0;
  TrackSample mAccepted{};

  bool mHasPending = false;
  uint8_t mPendingCount = 0;
  TrackSample mPending{};
};

class InertialTrackFilter {
public:
  enum class InputClass : uint8_t {
    Valid = 0,
    SoftReject = 1,
    HardAbsent = 2,
    LinkIssue = 3
  };

  void configure(uint32_t fullHoldMs, uint32_t fadeMs) {
    mFullHoldMs = fullHoldMs;
    mFadeMs = fadeMs;
  }

  FilterOutput update(InputClass inputClass, uint32_t nowMs, const TrackSample* sample = nullptr) {
    if (inputClass == InputClass::Valid && sample != nullptr) {
      mHasTrack = true;
      mLastAcceptedMs = nowMs;
      mHeld = *sample;

      FilterOutput out;
      out.sample = mHeld;
      out.phase = Phase::Valid;
      out.ageMs = 0;
      out.hasTrack = true;
      out.visibility = 255;
      return out;
    }

    if (!mHasTrack) {
      return FilterOutput();
    }

    FilterOutput out;
    out.sample = mHeld;
    out.ageMs = (nowMs >= mLastAcceptedMs) ? (nowMs - mLastAcceptedMs) : 0;
    out.visibility = visibilityForAge(out.ageMs);
    out.hasTrack = (out.visibility > 0);

    if (!out.hasTrack) {
      mHasTrack = false;
      out.phase = Phase::Empty;
      out.sample = TrackSample();
      return out;
    }

    if (out.ageMs <= mFullHoldMs) {
      switch (inputClass) {
        case InputClass::SoftReject:
          out.phase = Phase::SoftReject;
          break;
        case InputClass::LinkIssue:
          out.phase = Phase::LinkIssue;
          break;
        case InputClass::HardAbsent:
        default:
          out.phase = Phase::Hold;
          break;
      }
      return out;
    }

    out.phase = Phase::Decay;
    return out;
  }

private:
  uint8_t visibilityForAge(uint32_t ageMs) const {
    if (ageMs <= mFullHoldMs) {
      return 255;
    }

    if (mFadeMs == 0) {
      return 0;
    }

    const uint32_t fadeAge = ageMs - mFullHoldMs;
    if (fadeAge >= mFadeMs) {
      return 0;
    }

    const uint32_t remain = mFadeMs - fadeAge;
    return static_cast<uint8_t>((remain * 255UL) / mFadeMs);
  }

  uint32_t mFullHoldMs = 700;
  uint32_t mFadeMs = 1800;
  bool mHasTrack = false;
  uint32_t mLastAcceptedMs = 0;
  TrackSample mHeld{};
};

class CursorSmoother {
public:
  void reset() {
    mInitialized = false;
    mCurrentRangeM = 0.0f;
    mVisibility = 0;
    mLastMs = 0;
  }

  void update(const FilterOutput& filtered, uint32_t nowMs) {
    if (!mInitialized) {
      mInitialized = true;
      mLastMs = nowMs;
      if (filtered.hasTrack) {
        mCurrentRangeM = filtered.sample.rangeM;
        mVisibility = filtered.visibility;
      } else {
        mCurrentRangeM = BuildConfig::kDisplayFarM;
        mVisibility = 0;
      }
      return;
    }

    uint32_t dtMs = (nowMs >= mLastMs) ? (nowMs - mLastMs) : 0;
    if (dtMs == 0) {
      dtMs = 1;
    }
    mLastMs = nowMs;
    const float dtSec = static_cast<float>(dtMs) / 1000.0f;

    if (filtered.hasTrack) {
      const float target = filtered.sample.rangeM;
      const float diff = target - mCurrentRangeM;
      const float maxRate = (diff < 0.0f)
                                ? BuildConfig::kCursorApproachRateMps
                                : BuildConfig::kCursorRetreatRateMps;
      const float maxStep = maxRate * dtSec;
      if (diff > maxStep) {
        mCurrentRangeM += maxStep;
      } else if (diff < -maxStep) {
        mCurrentRangeM -= maxStep;
      } else {
        mCurrentRangeM = target;
      }
      mVisibility = filtered.visibility;
    } else {
      // Keep the last range, just fade out.
      if (mVisibility > 8) {
        mVisibility = static_cast<uint8_t>(mVisibility - 8);
      } else {
        mVisibility = 0;
      }
    }
  }

  bool visible() const {
    return mVisibility > 0;
  }

  float rangeM() const {
    return mCurrentRangeM;
  }

  uint8_t visibility() const {
    return mVisibility;
  }

private:
  bool mInitialized = false;
  float mCurrentRangeM = 0.0f;
  uint8_t mVisibility = 0;
  uint32_t mLastMs = 0;
};

PresenceProbe gProbe;
AcquisitionGate gGate;
InertialTrackFilter gFilter;
CursorSmoother gCursor;

uint8_t statusBrightness(uint8_t base, uint8_t visibility) {
  return static_cast<uint8_t>((static_cast<uint16_t>(base) * static_cast<uint16_t>(visibility)) / 255U);
}

Phase statusPhaseFor(const GateDecision& gate, const FilterOutput& filtered) {
  if (!gate.readOk) {
    return Phase::LinkIssue;
  }
  if (filtered.hasTrack) {
    return filtered.phase;
  }
  if (gate.hasRawTarget && !gate.acceptedThisFrame) {
    return Phase::SoftReject;
  }
  return Phase::Empty;
}

void drawStatusBar(Phase phase, uint8_t visibility) {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t w = 0;

  switch (phase) {
    case Phase::Valid:
      g = 90;
      break;
    case Phase::Hold:
      b = 95;
      break;
    case Phase::SoftReject:
      r = 80;
      b = 80;
      break;
    case Phase::HardAbsent:
      r = 90;
      break;
    case Phase::Decay:
      r = 70;
      g = 35;
      break;
    case Phase::LinkIssue:
      w = 90;
      break;
    case Phase::Empty:
    default:
      r = 36;
      break;
  }

  r = statusBrightness(r, visibility);
  g = statusBrightness(g, visibility);
  b = statusBrightness(b, visibility);
  w = statusBrightness(w, visibility);

  for (uint16_t i = 0; i < BuildConfig::kStatusPixels && i < BuildConfig::kTotalPixels; ++i) {
    gStrip.setPixelColor(i, gStrip.Color(r, g, b, w));
  }
}

void drawRawCursor(const RawReading& raw, const GateDecision& gate) {
  if (!gate.hasRawTarget || raw.rangeM <= 0.0f) {
    return;
  }

  const uint16_t idx = rangeToPixel(raw.rangeM);
  if (idx >= BuildConfig::kTotalPixels) {
    return;
  }

  const uint8_t w = gate.inFocusWindow
                        ? BuildConfig::kRawCursorBrightInFocus
                        : BuildConfig::kRawCursorBrightOutOfFocus;
  gStrip.setPixelColor(idx, gStrip.Color(0, 0, 0, w));
}

void drawFilteredCursor(const CursorSmoother& cursor, const FilterOutput& filtered) {
  if (!cursor.visible()) {
    return;
  }

  const uint16_t idx = rangeToPixel(cursor.rangeM());
  if (idx >= BuildConfig::kTotalPixels) {
    return;
  }

  const uint8_t base = static_cast<uint8_t>(
      (static_cast<uint16_t>(BuildConfig::kFilteredCursorBright) *
       static_cast<uint16_t>(cursor.visibility())) / 255U);

  uint8_t r = base;
  uint8_t g = 0;
  uint8_t b = base;
  uint8_t w = 0;

  if (filtered.phase == Phase::Decay) {
    r = static_cast<uint8_t>(base / 2);
    g = static_cast<uint8_t>(base / 4);
    b = static_cast<uint8_t>(base / 2);
  }

  gStrip.setPixelColor(idx, gStrip.Color(r, g, b, w));
}

uint8_t buildFlags(const RawReading& raw, const GateDecision& gate, const FilterOutput& filtered) {
  uint8_t flags = 0;
  if (!raw.readOk) {
    flags |= 0x01;
  }
  if (gate.nearField) {
    flags |= 0x02;
  }
  if (gate.hasRawTarget) {
    flags |= 0x04;
  }
  if (gate.inFocusWindow) {
    flags |= 0x08;
  }
  if (gate.acceptedThisFrame) {
    flags |= 0x10;
  }
  if (filtered.hasTrack && (filtered.phase != Phase::Valid)) {
    flags |= 0x20;
  }
  if (gate.pendingTakeover) {
    flags |= 0x40;
  }
  if (gate.acquireGatePassed) {
    flags |= 0x80;
  }
  return flags;
}

void printLegends() {
  Serial.println("# C4001 Near-Field Hunter Probe");
  Serial.println("# phase: 0=valid 1=hold 2=softReject 3=hardAbsent 4=decay 5=empty 6=linkIssue");
  Serial.println("# reason: 0=none 1=noTarget 2=readFailure 3=speedCap 4=outOfWindow 5=acquireNeedsMotion 6=rangeJump 7=takeoverPending");
  Serial.println("# flags bits: 1=readFail 2=nearField 4=rawTarget 8=inFocus 16=accepted 32=staleTrack 64=pending 128=acquireGate");
  Serial.println("ms,raw_mm,accepted_mm,display_mm,phase,reason,age_ms,energy,flags,pending");
}

void setup() {
  Serial.begin(115200);
  gProbe.begin();

  gStrip.begin();
  gStrip.setBrightness(BuildConfig::kGlobalBrightness);
  gStrip.clear();
  gStrip.show();

  gFilter.configure(BuildConfig::kTrackFullHoldMs, BuildConfig::kTrackFadeMs);
  gCursor.reset();

  printLegends();
}

void loop() {
  const uint32_t nowMs = millis();
  const RawReading raw = gProbe.read(nowMs);
  const GateDecision gate = gGate.update(raw, nowMs);

  InertialTrackFilter::InputClass inputClass = InertialTrackFilter::InputClass::HardAbsent;
  const TrackSample* validSample = nullptr;

  if (!raw.readOk) {
    inputClass = InertialTrackFilter::InputClass::LinkIssue;
  } else if (gate.acceptedThisFrame) {
    inputClass = InertialTrackFilter::InputClass::Valid;
    validSample = &gate.acceptedSample;
  } else if (gate.hasRawTarget) {
    inputClass = InertialTrackFilter::InputClass::SoftReject;
  } else {
    inputClass = InertialTrackFilter::InputClass::HardAbsent;
  }

  const FilterOutput filtered = gFilter.update(inputClass, nowMs, validSample);
  if (!filtered.hasTrack) {
    gGate.clearAccepted();
  }
  gCursor.update(filtered, nowMs);

  gStrip.clear();
  const Phase statusPhase = statusPhaseFor(gate, filtered);
  const uint8_t statusVis = filtered.hasTrack ? filtered.visibility : 255;
  drawStatusBar(statusPhase, statusVis);
  drawRawCursor(raw, gate);
  drawFilteredCursor(gCursor, filtered);
  gStrip.show();

  if ((nowMs - gLastPrintMs) >= BuildConfig::kPrintIntervalMs) {
    gLastPrintMs = nowMs;
    Serial.print(nowMs);
    Serial.print(',');
    Serial.print(mmFromMeters(raw.rangeM));
    Serial.print(',');
    Serial.print(mmFromMeters(filtered.sample.rangeM));
    Serial.print(',');
    Serial.print(mmFromMeters(gCursor.rangeM()));
    Serial.print(',');
    Serial.print(static_cast<uint8_t>(statusPhase));
    Serial.print(',');
    Serial.print(static_cast<uint8_t>(gate.reason));
    Serial.print(',');
    Serial.print(filtered.ageMs);
    Serial.print(',');
    Serial.print(raw.energy);
    Serial.print(',');
    Serial.print(buildFlags(raw, gate, filtered));
    Serial.print(',');
    Serial.println(gate.pendingCount);
  }
}
