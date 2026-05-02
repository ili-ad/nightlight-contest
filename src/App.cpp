#include "App.h"

#include <Arduino.h>

#ifndef NIGHTLIGHT_ENABLE_WATCHDOG
#define NIGHTLIGHT_ENABLE_WATCHDOG 1
#endif

#if NIGHTLIGHT_ENABLE_WATCHDOG && defined(__AVR__)
#include <avr/wdt.h>
// Nano Every / ATmega4809 watchdog smoke-tested with raw PERIOD=0x0B
// (~8s). The WDT_PERIOD_* names are enum constants on this core, so
// preprocessor tests can miss them; use the known raw value directly.
#define NIGHTLIGHT_WATCHDOG_AVAILABLE 1
#define NIGHTLIGHT_WATCHDOG_TIMEOUT 0x0B
#else
#define NIGHTLIGHT_WATCHDOG_AVAILABLE 0
#endif

#include "config/Profiles.h"
#include "render/PixelOutput.h"
#include "topology/LayoutMap.h"

#ifndef NIGHTLIGHT_ENABLE_TELEMETRY
#define NIGHTLIGHT_ENABLE_TELEMETRY 0
#endif

#ifndef NIGHTLIGHT_ENABLE_SERIAL_EVENTS
#define NIGHTLIGHT_ENABLE_SERIAL_EVENTS 0
#endif

#ifndef NIGHTLIGHT_ENABLE_SERIAL_COMMANDS
#define NIGHTLIGHT_ENABLE_SERIAL_COMMANDS 0
#endif

// Sparse C4001 fault/recovery breadcrumbs. This enables Serial.begin() without
// re-enabling the heavy telemetry stream. Set to 0 for the final silent build.
#ifndef NIGHTLIGHT_ENABLE_RADAR_FAULT_LOG
#define NIGHTLIGHT_ENABLE_RADAR_FAULT_LOG 1
#endif

#if NIGHTLIGHT_ENABLE_TELEMETRY || NIGHTLIGHT_ENABLE_SERIAL_EVENTS || NIGHTLIGHT_ENABLE_SERIAL_COMMANDS || NIGHTLIGHT_ENABLE_RADAR_FAULT_LOG
#define NIGHTLIGHT_SERIAL_ENABLED 1
#else
#define NIGHTLIGHT_SERIAL_ENABLED 0
#endif

namespace {
LayoutMap gLayoutMap;
PixelOutput gPixelOutput(gLayoutMap);
App gApp(gLayoutMap, gPixelOutput);

constexpr uint16_t kSceneSwitchPulseMs = 340;
constexpr uint16_t kSceneAckBlinkOnMs = 105;
constexpr uint16_t kSceneAckBlinkGapMs = 80;
constexpr uint16_t kAntStepMs = 90;
bool gWatchdogEnabled = false;

void disableWatchdogOnBoot() {
#if NIGHTLIGHT_WATCHDOG_AVAILABLE
  wdt_disable();
  delay(5);
  gWatchdogEnabled = false;
#endif
}

void maybeEnableWatchdog() {
#if NIGHTLIGHT_WATCHDOG_AVAILABLE
  if (!gWatchdogEnabled) {
    wdt_reset();
    wdt_enable(NIGHTLIGHT_WATCHDOG_TIMEOUT);
    gWatchdogEnabled = true;
  }
#endif
}

void kickWatchdog() {
#if NIGHTLIGHT_WATCHDOG_AVAILABLE
  if (gWatchdogEnabled) wdt_reset();
#endif
}

uint8_t triWave8(uint32_t now, uint16_t periodMs) {
  if (periodMs < 2) return 0;
  const uint16_t half = periodMs / 2;
  const uint16_t x = now % periodMs;
  if (x < half) {
    return static_cast<uint8_t>((static_cast<uint32_t>(x) * 255UL) / half);
  }
  return static_cast<uint8_t>(255UL - ((static_cast<uint32_t>(x - half) * 255UL) / (periodMs - half)));
}

bool sceneAckBlinkIsOn(uint32_t elapsed) {
  if (elapsed < kSceneAckBlinkOnMs) return true;
  const uint32_t secondStart = kSceneAckBlinkOnMs + kSceneAckBlinkGapMs;
  return elapsed >= secondStart && elapsed < secondStart + kSceneAckBlinkOnMs;
}
}  // namespace

App::App(LayoutMap& layoutMap, PixelOutput& pixelOutput)
    : layoutMap_(layoutMap),
      pixelOutput_(pixelOutput),
      anthuriumScene_(pixelOutput),
      nightlightScene_(pixelOutput),
      startupScene_(pixelOutput) {}

void App::setup() {
  disableWatchdogOnBoot();
  (void)layoutMap_;
  #if NIGHTLIGHT_SERIAL_ENABLED
  Serial.begin(115200);
  #endif
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
  Serial.println("Nightlight v2 production boot");
#endif

  pixelOutput_.begin();
  anthuriumScene_.begin();
  nightlightScene_.begin();
  startupScene_.begin(millis());
}

void App::loop() {
  kickWatchdog();
  const uint32_t nowMs = millis();

  if (phase_ == AppPhase::Startup) {
    if (startupScene_.render(nowMs)) {
      clapDetector_.begin();
      modeController_.setMode(Mode::Anthurium);
      phase_ = AppPhase::Running;
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
      Serial.println("event=startup_complete mode=Anthurium");
#endif
    }
    delay(16);
    return;
  }

#if NIGHTLIGHT_ENABLE_SERIAL_COMMANDS
  while (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());
    Mode nextMode = modeController_.currentMode();
    bool modeChanged = false;

    switch (command) {
      case 'a':
      case 'A':
        nextMode = Mode::Anthurium;
        modeController_.setMode(nextMode);
        modeChanged = true;
        break;
      case 'n':
      case 'N':
        nextMode = Mode::Nightlight;
        modeController_.setMode(nextMode);
        modeChanged = true;
        break;
      case 'o':
      case 'O':
        nextMode = Mode::Off;
        modeController_.setMode(nextMode);
        modeChanged = true;
        break;
      case 'c':
      case 'C':
        nextMode = modeController_.advanceMode();
        sceneAckStartedMs_ = nowMs;
        modeChanged = true;
        break;
      case 'r':
      case 'R':
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
        Serial.println("event=c4001_init_begin");
#endif
        if (stableSource_.tryInit()) {
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
          Serial.println("event=c4001_init_recovered");
#endif
        } else {
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
          Serial.println("warn=c4001_init_failed");
#endif
        }
        break;
      default:
        break;
    }

    if (modeChanged) {
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
      Serial.print("event=serial_mode mode=");
      Serial.println(modeName(nextMode));
#endif
    }
  }
#endif

  if (clapDetector_.update(nowMs)) {
    const Mode nextMode = modeController_.advanceMode();
    sceneAckStartedMs_ = nowMs;
#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS
    Serial.print("event=double_clap mode=");
    Serial.println(modeName(nextMode));
#endif
  }

  if (sceneAckActive(nowMs)) {
    renderSceneAck(nowMs);
    stableSource_.service(nowMs);
    kickWatchdog();
    delay(16);
    return;
  }

  if (clapDetector_.isWaitingForSecondClap()) {
    renderClapWaitOverlay(nowMs);
    stableSource_.service(nowMs);
    kickWatchdog();
    delay(16);
    return;
  }

  const Mode userMode = modeController_.currentMode();

  switch (userMode) {
    case Mode::Off:
      renderOff();
      break;
    case Mode::Nightlight:
      nightlightScene_.render(nowMs);
      break;
    case Mode::Anthurium: {
      const StableTrack track = stableSource_.read(nowMs);
      if (track.online) maybeEnableWatchdog();
      maybePrintAnthuriumTelemetry(track, nowMs);
      anthuriumScene_.render(track, nowMs);
      stableSource_.service(nowMs);
      kickWatchdog();
      break;
    }
    default:
      renderOff();
      break;
  }

  kickWatchdog();
  delay(16);
}

bool App::sceneAckActive(uint32_t nowMs) const {
  return sceneAckStartedMs_ != 0 && (nowMs - sceneAckStartedMs_) < kSceneSwitchPulseMs;
}

void App::renderClapWaitOverlay(uint32_t nowMs) {
  pixelOutput_.clear();

  uint32_t elapsed = nowMs - clapDetector_.firstClapMs();
  if (elapsed > ClapDetector::kSecondListenTimeoutMs) {
    elapsed = ClapDetector::kSecondListenTimeoutMs;
  }

  const uint8_t expired = static_cast<uint8_t>(
      (elapsed * Profiles::kRightJPixels) / ClapDetector::kSecondListenTimeoutMs);
  const uint8_t remaining = expired >= Profiles::kRightJPixels
      ? 0
      : static_cast<uint8_t>(Profiles::kRightJPixels - expired);

  for (uint8_t i = 0; i < remaining; ++i) {
    const uint8_t rightIndex = static_cast<uint8_t>((Profiles::kRightJPixels - remaining) + i);
    pixelOutput_.setRightJPixel(rightIndex, 0, 0, 0, 82);
    pixelOutput_.setLeftJPixel(i, 0, 0, 0, 82);
  }

  const uint8_t frontPulse = 72 + (triWave8(nowMs, 360) / 10);
  for (uint16_t i = 0; i < Profiles::kFrontRingPixels; ++i) {
    pixelOutput_.setFrontRingPixel(i, frontPulse, 0, 0, 0);
  }

  const uint8_t phase = (nowMs / kAntStepMs) & 0x03;
  for (uint16_t i = 0; i < Profiles::kRearRingPixels; ++i) {
    const bool on = (((i + phase) & 0x03) < 2);
    const uint16_t idx = Profiles::kRearRingPixels - 1 - i;
    pixelOutput_.setRearRingPixel(idx, 0, 0, 0, on ? 72 : 2);
  }

  pixelOutput_.show();
}

void App::renderSceneAck(uint32_t nowMs) {
  pixelOutput_.clear();

  const uint32_t elapsed = nowMs - sceneAckStartedMs_;
  if (!sceneAckBlinkIsOn(elapsed)) {
    pixelOutput_.show();
    return;
  }

  const uint8_t shimmer = triWave8(nowMs, 160) / 10;
  const uint8_t jW = 72 + shimmer;
  const uint8_t frontW = 96 + shimmer;
  const uint8_t rearW = 82 + shimmer;

  for (uint16_t i = 0; i < Profiles::kRightJPixels; ++i) {
    pixelOutput_.setRightJPixel(i, 0, 0, 0, jW);
  }
  for (uint16_t i = 0; i < Profiles::kLeftJPixels; ++i) {
    pixelOutput_.setLeftJPixel(i, 0, 0, 0, jW);
  }
  for (uint16_t i = 0; i < Profiles::kFrontRingPixels; ++i) {
    pixelOutput_.setFrontRingPixel(i, 0, 0, 0, frontW);
  }
  for (uint16_t i = 0; i < Profiles::kRearRingPixels; ++i) {
    pixelOutput_.setRearRingPixel(i, 0, 0, 0, rearW);
  }

  const uint8_t phase = (nowMs / 42) % Profiles::kRearRingPixels;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint16_t idx = Profiles::kRearRingPixels - 1 - ((phase + i * 11) % Profiles::kRearRingPixels);
    pixelOutput_.setRearRingPixel(idx, 18, 8, 0, 110);
  }

  pixelOutput_.show();
}

#if NIGHTLIGHT_ENABLE_TELEMETRY
void App::maybePrintAnthuriumTelemetry(const StableTrack& track, uint32_t nowMs) {
  constexpr uint32_t kTelemetryIntervalMs = 500;
  if (lastAnthuriumTelemetryMs_ != 0 && (nowMs - lastAnthuriumTelemetryMs_ < kTelemetryIntervalMs)) {
    return;
  }
  lastAnthuriumTelemetryMs_ = nowMs;

  Serial.print("telemetry mode=Anthurium online=");
  Serial.print(track.online ? 1 : 0);
  Serial.print(" hasTarget=");
  Serial.print(track.hasTarget ? 1 : 0);
  Serial.print(" rangeM=");
  Serial.print(track.rangeM, 2);
  Serial.print(" speedMps=");
  Serial.print(track.speedMps, 2);
  Serial.print(" charge=");
  Serial.print(track.charge, 2);
  Serial.print(" ingressLevel=");
  Serial.print(track.ingressLevel, 2);
  Serial.print(" continuity=");
  Serial.print(track.continuity, 2);
  Serial.print(" phase=");
  Serial.println(phaseName(track.phase));
}
#else
void App::maybePrintAnthuriumTelemetry(const StableTrack&, uint32_t) {}
#endif

#if NIGHTLIGHT_ENABLE_SERIAL_EVENTS || NIGHTLIGHT_ENABLE_SERIAL_COMMANDS
const char* App::modeName(Mode mode) {
  switch (mode) {
    case Mode::Off:
      return "Off";
    case Mode::Nightlight:
      return "Nightlight";
    case Mode::Anthurium:
      return "Anthurium";
    default:
      return "Unknown";
  }
}
#else
const char* App::modeName(Mode) { return ""; }
#endif

#if NIGHTLIGHT_ENABLE_TELEMETRY
const char* App::phaseName(StableTrack::MotionPhase phase) {
  switch (phase) {
    case StableTrack::MotionPhase::None:
      return "None";
    case StableTrack::MotionPhase::Approach:
      return "Approach";
    case StableTrack::MotionPhase::Still:
      return "Still";
    case StableTrack::MotionPhase::Retreat:
      return "Retreat";
    default:
      return "Unknown";
  }
}
#else
const char* App::phaseName(StableTrack::MotionPhase) {
  return "";
}
#endif

void App::renderOff() {
  pixelOutput_.clear();
  pixelOutput_.show();
}

App& getApp() {
  return gApp;
}
