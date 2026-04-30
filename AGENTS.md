# Nightlight Contest Agent Notes

This repository drives a physical Arduino Nano Every light sculpture. Treat it as an embedded hardware project first.

## Prime directive

Prefer bench-proven, boring embedded code over clever abstraction.

If a standalone bench sketch works on physical hardware, use its construction/init/read pattern as the default reference unless a production change is hardware-tested and preserves behavior.

## Known-good visual/radar reference

`bench/anthurium_lite_smoke_v3/anthurium_lite_smoke_v3.ino` is the behavioral reference for Anthurium + C4001.

Do **not** overwrite it or use it as scratch space.

## Hardware contract

- LED chain: 112 total SK6812 RGBW pixels
- Pixel data pin: `D6`
- C4001 radar: I2C address `0x2B`
- Clap microphone: `A0`
- Ambient sensor: not installed in final build

Final topology:

- RightJ / J1: 12
- LeftJ / J2: 12
- FrontRing / O1: 44
- RearRing / O2: 44

Physical order: `RightJ -> LeftJ -> FrontRing -> RearRing`.

## Startup/runtime rules

- Startup progressive fill must always run, even with all sensors unplugged.
- No sensor initialization may block startup.
- After startup fill, app enters Anthurium interactive mode.
- Double clap cycles: `Anthurium -> Nightlight -> Off -> Anthurium`.

## Embedded safety rules

1. No hardware side effects in global/static constructors.
2. No profile/function calls inside global hardware object initializers.
3. Do not call blocking hardware init from render-critical paths (`App::loop`, scene `render`, `PixelOutput::show`, `C4001StableSource::read`).
4. `C4001StableSource::read(nowMs)` must return quickly.
5. C4001 init must be explicit, logged, and isolated.
6. Telemetry must keep flowing even when radar is offline.

Preferred C4001 construction pattern:

```cpp
namespace {
constexpr uint8_t kC4001I2cAddress = 0x2B;
DFRobot_C4001_I2C gC4001(&Wire, kC4001I2cAddress);
}
```

## Visual guidance

- Avoid whole-scene phase switching (all red/all blue/all black).
- Use continuous signals: range, speed, nearness, motion magnitude, charge, ingress, continuity.
- J/spadix strips should behave like delay lines/signal conveyors with historical mixed warm/cool content.
- Rings should behave like damped reservoirs with diffusion/decay.
- **Off** is the only fully black mode.

## Debugging priority

When production differs from a known-good bench sketch:

1. Trust working bench sketch behavior first.
2. Compare hardware constants.
3. Compare init order.
4. Compare blocking calls.
5. Compare data path.
6. Only then tune visuals.

## Production audit snapshot (startup/static fragility)

See `docs/startup-static-audit.md` for current findings and low-risk recommendations.
