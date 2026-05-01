#pragma once

#include "modes/ModeController.h"
#include "scenes/AnthuriumScene.h"
#include "scenes/NightlightScene.h"
#include "scenes/StartupScene.h"
#include "sensors/C4001StableSource.h"
#include "sensors/ClapDetector.h"

class LayoutMap;
class PixelOutput;

class App {
 public:
  App(LayoutMap& layoutMap, PixelOutput& pixelOutput);

  void setup();
  void loop();

 private:
  enum class AppPhase : uint8_t {
    Startup,
    Running,
  };

  static const char* modeName(Mode mode);

  void renderOff();
  void renderClapWaitOverlay(uint32_t nowMs);
  void renderSceneAck(uint32_t nowMs);
  bool sceneAckActive(uint32_t nowMs) const;
  void maybePrintAnthuriumTelemetry(const StableTrack& track, uint32_t nowMs);
  static const char* phaseName(StableTrack::MotionPhase phase);

  LayoutMap& layoutMap_;
  PixelOutput& pixelOutput_;
  C4001StableSource stableSource_;
  ClapDetector clapDetector_;
  ModeController modeController_;
  AnthuriumScene anthuriumScene_;
  NightlightScene nightlightScene_;
  StartupScene startupScene_;
  AppPhase phase_ = AppPhase::Startup;
  uint32_t lastAnthuriumTelemetryMs_ = 0;
  uint32_t sceneAckStartedMs_ = 0;
};
