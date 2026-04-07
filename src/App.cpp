#include "App.h"
#include "BuildConfig.h"
#include "behavior/LampStateMachine.h"
#include "debug/Telemetry.h"
#include "effects/BootEffects.h"
#include "effects/InterludeEffects.h"
#include "mapping/MapperShared.h"
#include "mapping/RenderIntent.h"
#include "render/PixelBus.h"
#include "render/RendererRgb.h"
#include "render/RendererRgbw.h"
#include "sensors/AmbientBh1750.h"
#include "sensors/PresenceManager.h"

static LampStateMachine gStateMachine;
static Telemetry gTelemetry;
static PixelBus gPixelBus;
static RendererRgb gRendererRgb;
static RendererRgbw gRendererRgbw;
static MapperShared gMapper;
static AmbientBh1750 gAmbientSensor;
static PresenceManager gPresenceManager;

namespace {
  struct AppInputs {
    bool darkAllowed = false;
    float ambientLux = 0.0f;
    CorePresence presence;
  };

  uint8_t toLevel(float normalized) {
    if (normalized <= 0.0f) {
      return 0;
    }
    if (normalized >= 1.0f) {
      return 255;
    }
    return static_cast<uint8_t>(normalized * 255.0f);
  }

  AppInputs readInputs() {
    AppInputs inputs;

    AmbientReading ambient = gAmbientSensor.read();
    inputs.ambientLux = ambient.luxSmoothed;
    if (!ambient.online) {
      inputs.ambientLux = ambient.luxRaw;
    }

    inputs.darkAllowed = (inputs.ambientLux < BuildConfig::kDarkEnterLux);
    inputs.presence = gPresenceManager.readCore();

    return inputs;
  }

  void advanceState(const AppInputs& inputs) {
    gStateMachine.update(inputs.darkAllowed, inputs.ambientLux, inputs.presence);
  }

  RenderIntent buildRenderIntent(const BehaviorContext& context) {
    return gMapper.map(context);
  }

  void renderRgbFrame(const BehaviorContext& context, const RenderIntent& intent) {
    switch (context.state) {
      case LampState::BootAnimation: {
        BootFrame frame = BootEffects::sample(context.elapsedInStateMs());
        gRendererRgb.renderBoot(gPixelBus, frame);
        break;
      }

      case LampState::InterludeGlitch: {
        InterludeFrame frame = InterludeEffects::marchingAnts(context.elapsedInStateMs());
        gRendererRgb.renderInterlude(gPixelBus, frame);
        break;
      }

      case LampState::DayDormant:
      case LampState::NightIdle:
      case LampState::ActiveInterpretive:
      case LampState::Decay:
      case LampState::FaultSafe:
      default:
        gRendererRgb.renderIdle(gPixelBus, toLevel(intent.whiteLevel));
        break;
    }
  }

  void renderRgbwFrame(const BehaviorContext& context, const RenderIntent& intent) {
    switch (context.state) {
      case LampState::BootAnimation: {
        BootFrame frame = BootEffects::sample(context.elapsedInStateMs());
        gRendererRgbw.renderBoot(gPixelBus, frame);
        break;
      }

      case LampState::InterludeGlitch: {
        InterludeFrame frame = InterludeEffects::marchingAnts(context.elapsedInStateMs());
        gRendererRgbw.renderInterlude(gPixelBus, frame);
        break;
      }

      case LampState::DayDormant:
      case LampState::NightIdle:
      case LampState::ActiveInterpretive:
      case LampState::Decay:
      case LampState::FaultSafe:
      default:
        gRendererRgbw.renderIdle(gPixelBus, toLevel(intent.whiteLevel));
        break;
    }
  }

  void renderFrame(const BehaviorContext& context, const RenderIntent& intent) {
    if (BuildConfig::kRenderBackend == RenderBackend::RGB) {
      renderRgbFrame(context, intent);
      return;
    }

    renderRgbwFrame(context, intent);
  }
}

void App::setup() {
  gTelemetry.begin();
  gStateMachine.begin();
  gAmbientSensor.begin();
  gPresenceManager.begin();
  gPixelBus.begin();

  gRendererRgb.begin(gPixelBus);
  gRendererRgbw.begin(gPixelBus);
}

void App::loop() {
  const AppInputs inputs = readInputs();
  advanceState(inputs);

  const BehaviorContext& context = gStateMachine.context();
  const RenderIntent intent = buildRenderIntent(context);

  renderFrame(context, intent);
  gPixelBus.show();
  gTelemetry.update(gStateMachine);
}
