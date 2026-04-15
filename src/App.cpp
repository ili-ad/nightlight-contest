#include "App.h"
#include <Arduino.h>
#include "BuildConfig.h"
#if BUILD_HAS_DEBUG_SIM
#include "debug/DebugModes.h"
#endif
#include "behavior/LampStateMachine.h"
#include "debug/Telemetry.h"
#if BUILD_ENABLE_BOOT_ANIMATION
#include "effects/BootEffects.h"
#endif
#if BUILD_ENABLE_INTERLUDES
#include "effects/InterludeEffects.h"
#endif
#include "mapping/MapperC4001.h"
#include "mapping/MapperShared.h"
#include "mapping/RenderIntent.h"
#include "processing/AmbientGate.h"
#include "render/PixelBus.h"
#include "render/RendererRgbw.h"
#include "render/RenderIntentSmoother.h"
#include "sensors/AmbientBh1750.h"
#include "sensors/PresenceManager.h"

static LampStateMachine gStateMachine;
static Telemetry gTelemetry;
static PixelBus gPixelBus;
static RendererRgbw gRendererRgbw;
static MapperShared gMapper;
static MapperC4001 gMapperC4001;
static RenderIntentSmoother gIntentSmoother;
static AmbientBh1750 gAmbientSensor;
static AmbientGate gAmbientGate;
static PresenceManager gPresenceManager;

namespace {
  struct AppInputs {
    bool darkAllowed = false;
    float ambientLux = 0.0f;
    AmbientGateResult ambientGate;
    CorePresence presence;
    bool forceFaultSafe = false;
  };

  AppInputs readInputs() {
    AppInputs inputs;

    // 1) Read live hardware first.
    AmbientReading ambient = gAmbientSensor.read();
    inputs.ambientLux = ambient.luxSmoothed;
    if (!ambient.online) {
      inputs.ambientLux = ambient.luxRaw;
    }

    inputs.presence = gPresenceManager.readCore();
    const LampState currentState = gStateMachine.context().state;
    inputs.ambientGate = gAmbientGate.update(inputs.ambientLux,
                                             currentState,
                                             inputs.presence.presenceConfidence);
    inputs.darkAllowed = inputs.ambientGate.darkAllowed;
    inputs.ambientLux = inputs.ambientGate.gateLux;

    // 2) Optional explicit debug override.
    #if BUILD_HAS_DEBUG_SIM
      const DebugInputSample sim = DebugModes::sample(millis());
      if (sim.useSimulated) {
        inputs.darkAllowed = sim.darkAllowed;
        inputs.ambientLux = sim.ambientLux;
        inputs.presence = sim.presence;
      }
      // 3) Fault-safe forcing remains available in debug simulation mode.
      inputs.forceFaultSafe = sim.useSimulated && sim.forceFaultSafe;
    #else
      inputs.forceFaultSafe = false;
    #endif

    return inputs;
  }

  void advanceState(const AppInputs& inputs) {
    gStateMachine.update(inputs.darkAllowed, inputs.ambientLux, inputs.presence, inputs.forceFaultSafe);
  }

  RenderIntent buildRenderIntent(const BehaviorContext& context) {
    if (BuildConfig::kPresenceBackend == PresenceBackend::C4001) {
      return gMapperC4001.map(context, gPresenceManager.lastC4001Rich());
    }

    return gMapper.map(context);
  }

  bool shouldSmoothIntent(LampState state) {
    switch (state) {
      case LampState::BootAnimation:
      case LampState::InterludeGlitch:
        return false;
      case LampState::DayDormant:
      case LampState::NightIdle:
      case LampState::ActiveInterpretive:
      case LampState::Decay:
      case LampState::FaultSafe:
      default:
        return true;
    }
  }

  void renderRgbwFrame(const BehaviorContext& context, const RenderIntent& intent) {
    switch (context.state) {
      case LampState::BootAnimation: {
        #if BUILD_ENABLE_BOOT_ANIMATION
          BootFrame frame = BootEffects::sample(context.elapsedInStateMs());
          gRendererRgbw.renderBoot(gPixelBus, frame);
        #else
          gRendererRgbw.renderIntent(gPixelBus, intent);
        #endif
        break;
      }

      case LampState::InterludeGlitch: {
        #if BUILD_ENABLE_INTERLUDES
          InterludeFrame frame = InterludeEffects::marchingAnts(context.elapsedInStateMs());
          gRendererRgbw.renderInterlude(gPixelBus, frame);
        #else
          gRendererRgbw.renderIntent(gPixelBus, intent);
        #endif
        break;
      }

      case LampState::DayDormant:
      case LampState::NightIdle:
      case LampState::ActiveInterpretive:
      case LampState::Decay:
      case LampState::FaultSafe:
      default:
        gRendererRgbw.renderIntent(gPixelBus, intent);
        break;
    }
  }

  void renderFrame(const BehaviorContext& context, const RenderIntent& intent) {
    // Current Nano Every bench target is RGBW-only. Reintroduce RGB backend
    // dispatch here if/when a bench build actively uses it again.
    renderRgbwFrame(context, intent);
  }
}

void App::setup() {
  gTelemetry.begin();
  gStateMachine.begin();
  gAmbientSensor.begin();
  gPresenceManager.begin();
  gPixelBus.begin();

  gRendererRgbw.begin(gPixelBus);
  gIntentSmoother.reset();
}

void App::loop() {
  const AppInputs inputs = readInputs();
  advanceState(inputs);

  const BehaviorContext& context = gStateMachine.context();
  const RenderIntent rawIntent = buildRenderIntent(context);

  RenderIntent finalIntent = rawIntent;
  if (shouldSmoothIntent(context.state)) {
    finalIntent = gIntentSmoother.smooth(rawIntent);
  } else {
    gIntentSmoother.reset();
  }

  renderFrame(context, finalIntent);
  gPixelBus.show();
  gTelemetry.update(gStateMachine,
                    gPresenceManager.c4001LinkStatus(),
                    inputs.ambientGate,
                    gPresenceManager.lastC4001Rich(),
                    finalIntent);
}
