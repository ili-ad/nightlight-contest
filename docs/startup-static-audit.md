# Startup/static initialization audit (production `src/`)

Date: 2026-04-30

## Scope checked

- Global hardware objects
- Constructors with hardware API calls
- Constructors with cross-translation-unit runtime calls (`Profiles::*`)
- `Wire`, `gC4001.begin()`, `strip.begin()/show()`, `Serial` usage outside explicit setup/begin/service paths
- Hardware init in render-critical paths

## Findings

1. **C4001 global object is in the safe form**.
   - `src/sensors/C4001StableSource.cpp` uses a compile-time `0x2B` constant for the global `DFRobot_C4001_I2C` constructor.
   - No `Profiles::c4001()` call appears in the global hardware initializer.

2. **C4001 init is explicit and not in `read()`**.
   - `Wire.begin()` occurs in `C4001StableSource::begin()`.
   - `gC4001.begin()` and sensor mode/threshold config occur in `C4001StableSource::tryInit()`.
   - `C4001StableSource::read()` returns an offline track when not initialized/ready instead of attempting blocking init.

3. **App-level globals still exist, but no direct hardware side effects there**.
   - `LayoutMap gLayoutMap; PixelOutput gPixelOutput(gLayoutMap); App gApp(...)` are file-scope globals in `src/App.cpp`.
   - Hardware bring-up still happens in `App::setup()`.

4. **`LayoutMap` constructor reads `Profiles::topology()`**.
   - This is acceptable because it is pure static topology data.
   - Do not expand this pattern to hardware setup or runtime-dependent state.

5. **`PixelOutput` constructor is acceptable; hardware calls stay in `begin()`/`show()`**.
   - Constructor only instantiates `Adafruit_NeoPixel`.
   - `strip_.begin()`, `clear()`, and first `show()` are in `PixelOutput::begin()`.

6. **No new hardware init in render-critical paths found**.
   - Scene `render()` paths do not attempt sensor initialization.
   - `App::loop()` runtime mode switching can trigger explicit C4001 manual init (`r` command), which is logged.

## Low-risk recommendations (not required for this patch)

- Keep app telemetry independent from radar health (already present in current Anthurium telemetry prints).
- Keep startup fill path sensor-agnostic (already present; clap detector begin is deferred until startup completion).
- If startup fragility reappears, consider replacing App file-scope globals with function-local statics to reduce static init ordering risk.
