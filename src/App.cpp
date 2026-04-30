#include "App.h"

#include <Arduino.h>

#include "render/PixelOutput.h"
#include "topology/LayoutMap.h"

namespace {
LayoutMap gLayoutMap;
PixelOutput gPixelOutput(gLayoutMap);
App gApp(gLayoutMap, gPixelOutput);
}  // namespace

App::App(LayoutMap& layoutMap, PixelOutput& pixelOutput)
    : layoutMap_(layoutMap),
      pixelOutput_(pixelOutput),
      anthuriumScene_(pixelOutput),
      nightlightScene_(pixelOutput),
      startupScene_(pixelOutput) {}

void App::setup() {
  (void)layoutMap_;
  Serial.begin(115200);
  Serial.println("Nightlight v2 production boot");

  pixelOutput_.begin();
  anthuriumScene_.begin();
  nightlightScene_.begin();
  startupScene_.begin(millis());

}

void App::loop() {
  const uint32_t nowMs = millis();

  if (phase_ == AppPhase::Startup) {
    if (startupScene_.render(nowMs)) {
      clapDetector_.begin();
      modeController_.setMode(Mode::Anthurium);
      phase_ = AppPhase::Running;
      Serial.println("event=startup_complete mode=Anthurium");
    }
    delay(16);
    return;
  }

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
        modeChanged = true;
        break;
      case 'r':
      case 'R':
        Serial.println("event=c4001_init_attempt");
        stableSource_.requestManualInit();
        stableSource_.service(nowMs);
        break;
      default:
        break;
    }

    if (modeChanged) {
      Serial.print("event=serial_mode mode=");
      Serial.println(modeName(nextMode));
    }
  }

  if (clapDetector_.update(nowMs)) {
    const Mode nextMode = modeController_.advanceMode();
    Serial.print("event=double_clap mode=");
    Serial.println(modeName(nextMode));
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
      maybePrintAnthuriumTelemetry(track, nowMs);
      anthuriumScene_.render(track, nowMs);
      stableSource_.service(nowMs);
      break;
    }
    default:
      renderOff();
      break;
  }

  delay(16);
}

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

void App::renderOff() {
  pixelOutput_.clear();
  pixelOutput_.show();
}

App& getApp() {
  return gApp;
}
