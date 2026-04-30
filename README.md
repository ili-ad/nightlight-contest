# Nightlight Contest

## Project layout (v2 reset)

- `archive/nightlight-v1/`: full legacy snapshot of the original app (`NightlightContest.ino` + the original `src/` tree).
- `bench/`: hardware and behavior bench sketches (kept intact).
- `src/`: new minimal v2 app scaffold.

## Intent

This repository is now in a deliberate reset posture:

1. Keep the v1 implementation archived and understandable.
2. Build a new, intentionally small app from proven `bench/` sketches.
3. Grow behavior and integrations incrementally in focused PRs.


## Production app notes

- The v2 production app uses clap mode control and C4001 radar tracking.
- Ambient lux / BH1750 behavior is not part of runtime behavior in production builds.
- Production pixel output is guarded by a current-limited brightness cap (`Profiles::output()` in `src/config/Profiles.*`), intended as a safety governor while final brightness tuning is done on a bench supply.
