#include "App.h"
#include "BuildConfig.h"
#include "behavior/LampStateMachine.h"
#include "debug/Telemetry.h"
#include "effects/BootEffects.h"
#include "effects/IdleEffects.h"
#include "effects/InterludeEffects.h"
#include "render/PixelBus.h"
#include "render/RendererRgb.h"
#include "render/RendererRgbw.h"

static LampStateMachine gStateMachine;
static Telemetry gTelemetry;
static PixelBus gPixelBus;
static RendererRgb gRendererRgb;
static RendererRgbw gRendererRgbw;

namespace {
  void renderRgbState(const BehaviorContext& context) {
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
      case LampState::FaultSafe:
        gRendererRgb.renderIdle(gPixelBus, 0);
        break;

      case LampState::NightIdle: {
        IdleFrame frame = IdleEffects::sample();
        gRendererRgb.renderIdle(gPixelBus, frame.level);
        break;
      }

      case LampState::ActiveInterpretive:
        gRendererRgb.renderIdle(gPixelBus, 20);
        break;

      case LampState::Decay:
        gRendererRgb.renderIdle(gPixelBus, 10);
        break;
    }
  }

  void renderRgbwState(const BehaviorContext& context) {
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
      case LampState::FaultSafe:
        gRendererRgbw.renderIdle(gPixelBus, 0);
        break;

      case LampState::NightIdle: {
        IdleFrame frame = IdleEffects::sample();
        gRendererRgbw.renderIdle(gPixelBus, frame.level);
        break;
      }

      case LampState::ActiveInterpretive:
        gRendererRgbw.renderIdle(gPixelBus, 20);
        break;

      case LampState::Decay:
        gRendererRgbw.renderIdle(gPixelBus, 10);
        break;
    }
  }
}

void App::setup() {
  gTelemetry.begin();
  gStateMachine.begin();
  gPixelBus.begin();

  gRendererRgb.begin(gPixelBus);
  gRendererRgbw.begin(gPixelBus);
}

void App::loop() {
  gStateMachine.update();

  const BehaviorContext& context = gStateMachine.context();

  if (BuildConfig::kRenderBackend == RenderBackend::RGB) {
    renderRgbState(context);
  } else {
    renderRgbwState(context);
  }

  gPixelBus.show();
  gTelemetry.update(gStateMachine);
}