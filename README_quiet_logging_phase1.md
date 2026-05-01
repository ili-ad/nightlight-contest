# Quiet Logging Phase 1

This package gates the development serial logging that was useful while validating the native 44-pixel front ring, but should be off by default before adding rear-ring and spadix behavior.

It edits only:

```text
src/App.cpp
src/scenes/AnthuriumScene.cpp
```

## What changes

Default production behavior becomes quiet:

- App telemetry is disabled by default.
- `shadow44` summary logs are disabled by default.
- full 44-pixel `shadow44_*_luma` dumps are disabled by default.

Essential event logs remain, including startup/mode/radar recovery events.

## Apply

From the repository root:

```powershell
python .\apply_quiet_logging_phase1.py
```

Then compile/upload as usual.

## Re-enable debug logs locally

In `src/App.cpp`:

```cpp
#define NIGHTLIGHT_ENABLE_TELEMETRY 0
```

change `0` to `1` for compact app telemetry.

In `src/scenes/AnthuriumScene.cpp`:

```cpp
#define ANTHURIUM_ENABLE_SHADOW44_SUMMARY 0
#define ANTHURIUM_ENABLE_SHADOW44_DUMPS 0
```

change summary to `1` for compact `shadow44` summaries, and dumps to `1` only for full 44-pixel luma dumps.

## Notes

This does not change visual rendering, radar handling, startup behavior, Nightlight, Off, or the current native 44 front-ring output.
