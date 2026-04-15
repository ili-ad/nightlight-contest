# ARCH-058 · Move C4001 stable-track truth upstream and feed both state + scene from one signal

## Why this exists

The bench probe has now demonstrated a stable, intelligible signal path: the raw white cursor is noisy, but the magenta cursor is behaviorally correct and holds through potholes instead of flashing like a bodega sign. The production app still splits truth in two places:

- `App::loop()` advances the lamp state machine from `CorePresence`
- `MapperC4001` separately derives active-scene drive from `lastC4001Rich()` plus its own local tracking/hold logic

That split is the architectural root of the current pain. Even when the active scene is visually stabilized, the state machine can still chatter if it is listening to a rougher upstream signal. This ticket replaces that dual-truth model with a single canonical C4001 stable track generated in `PresenceC4001`, then consumed by **both** the state machine and the active scene.

This is the shortest path out of the woods that preserves the existing day/night structure and scene pipeline.

## Primary outcome

After this change there must be exactly **one** production truth for C4001 target continuity:

- acquisition
- closer-target takeover
- soft-reject handling
- no-target grace
- read-failure hold
- hold → fade → empty lifecycle

That truth must live upstream in `PresenceC4001` and be exposed as a stable-track payload. `CorePresence` must be derived from it. `MapperC4001` must consume it. The mapper must stop acting as a second tracker.

## Non-goals for this ticket

Do **not** broaden scope into a feature festival.

Not in scope here:

- microphone / clap control
- turning the startup tracer into a general scene system
- adding new render backends or sensor backends
- redesigning Anthurium choreography
- retuning ambient-gate policy beyond what calmer presence inputs naturally improve
- adding verbose new telemetry

A follow-up ticket can promote the boot tracer into a reusable scene once this signal path is stable and flash-fit.

## Design decisions

### 1) Stable track moves upstream

Create a dedicated C4001 stable-track payload and make it the canonical source of truth. The exact type shape may vary, but it must carry at least:

- `hasTrack`
- `rangeM`
- `smoothedRangeM`
- `speedMps`
- `energyNorm`
- `ageMs`
- `visibility` or equivalent fade strength
- `phase`
- `rejectReason`

This payload should be attached to the C4001-rich path so it is available to `MapperC4001` without recreating the track there.

### 2) Stable track behavior must match the bench-proven intent

Port the behavior that made the probe useful:

- **Acquire gate**: do not immediately fall in love with a static far wall. Bias acquisition toward the useful approach corridor rather than any valid target the chip happens to report.
- **Closer coherent takeover**: if the current lock is farther and a repeated, coherent, closer candidate appears, allow takeover without requiring total loss of the old target.
- **Hold then fade**: on soft rejects, no-target potholes, or brief read failures, keep the last good range steady and decay visibility rather than dragging the effective range toward zero.
- **Pothole vs cliff**: brief absence should look like inertia; sustained absence should eventually commit to empty.

### 3) State and scene must read the same truth

`CorePresence` must be built from the stable track rather than directly from raw target existence. This means:

- `present` is driven by stable-track existence / visibility, not by `targetNumber > 0` alone
- `presenceConfidence` follows stable-track visibility / continuity
- `distanceHint` is derived from stable-track range
- `motionHint` is derived from stable-track speed magnitude

The lamp state machine should therefore calm down automatically without any special new state hacks.

### 4) Mapper becomes artistic, not forensic

`MapperC4001` should stop maintaining its own second truth engine. It may keep scene-specific shaping, smoothing, charge mapping, and composition, but it must consume the upstream stable track as the source sample.

In other words:

- `PresenceC4001` decides **what the track is**
- `MapperC4001` decides **what to do artistically with that track**

Do not keep two separate hold/decay policies alive.

### 5) Keep the current app structure, but trim redundant smoothing

Preserve the good parts of the current architecture:

- `AmbientGate`
- `LampStateMachine`
- `RendererRgbw`
- `AnthuriumScene`
- `PixelBus`

But stop smoothing the active signal twice.

`AnthuriumScene` already performs internal smoothing, deadbanding, and brightness slew limiting. The top-level `RenderIntentSmoother` should not also be smoothing `ActiveInterpretive` / `Decay` if that extra layer costs flash and muddies debugging.

## File-by-file called shot

### `src/PresenceTypes.h`

Add a dedicated stable-track payload for the C4001 path. It may be a nested struct on `C4001PresenceRich` or a standalone type referenced from it, but do **not** spray equivalent fields across multiple structs.

Required semantics:

- explicit track phase enum or byte with stable, documented meanings
- effective range separate from raw range
- sample age in ms
- visibility / fade strength carried forward explicitly
- latest reject reason carried forward explicitly

Do **not** remove existing raw fields. Bench/debug work still needs raw sensor visibility.

### `src/sensors/PresenceC4001.h`

Add the private state required for the upstream stable track. This almost certainly includes:

- current stable-track state
- pending closer-candidate state
- helper methods for acquisition, takeover, hold/fade, and committing empty

Keep the public surface small. This ticket is not asking for a public filter framework.

### `src/sensors/PresenceC4001.cpp`

This is the core of the change.

Implement the stable-track lifecycle here and make `read()` produce:

- raw rich sample fields for diagnostics
- stable-track fields for production truth
- `CorePresence` derived from the stable track

#### Acquisition / takeover policy

Use a production-biased window aimed at the actual nightlight approach use case.

Recommended policy:

- preferred corridor centered on roughly `0.75 m .. 3.20 m`
- do **not** hard-ban sub-`0.75 m` if the chip is coherent there; allow a stricter near candidate path rather than pretending closer readings can never be useful
- do not eagerly acquire static far-wall reports when a closer repeated candidate is emerging
- allow coherent closer takeover when the current track is farther and a nearer candidate repeats within a narrow band over consecutive polls

Do not obsess over exact helper names. The important thing is the state machine of the sensor contract, not the paint color on the helper functions.

#### Hold / fade policy

Preserve the bench behavior:

- soft rejects and brief no-target / read-failure windows should keep the last effective range steady
- visibility should decay after the hold window
- range should not be marched toward zero during fade
- only sustained absence should clear the track to empty

The visible app should therefore stop bright/dim flashing from single-frame potholes.

#### `CorePresence` derivation

Rework `buildCoreFromRich(...)` or equivalent so that `CorePresence` reflects the stable track, not raw target existence.

This is mandatory. If `CorePresence` still uses a rougher truth than the scene pipeline, the architecture remains split and this ticket has failed.

### `src/mapping/MapperC4001.h`

Remove the mapper-local responsibility for track continuity.

`MapperC4001` may still keep artistic smoothing state, but it should no longer own a second canonical hold/decay engine for C4001 continuity.

If `C4001TrackFilter` remains useful for bench-only tools, it may stay in the repo, but it should not remain the production truth engine inside `MapperC4001` after this ticket.

### `src/mapping/MapperC4001.cpp`

Refactor to consume the upstream stable track from `C4001PresenceRich`.

Keep:

- range-to-charge mapping
- scene charge / ingress / field / energy shaping
- scene-drive smoothing that is genuinely artistic
- scene composition into `RenderIntent`

Stop doing:

- production hold/decay decisions
- second-copy dropout classification as the authoritative truth
- separate production notion of effective range that can diverge from the stable track

`sceneDropoutPhase`, `sceneRejectReason`, `sceneTargetRangeM`, `sceneTargetRangeSmoothedM`, and `sceneSampleAgeMs` should all come from the same upstream stable-track contract.

### `src/App.cpp`

Keep the overall structure.

But for `ActiveInterpretive` and `Decay`, do not stack top-level `RenderIntentSmoother` on top of Anthurium’s own smoothing unless there is a compelling flash-neutral reason. Default posture for this ticket should be:

- bypass top-level intent smoothing in active/decay
- preserve or allow it for simpler idle/day paths if still useful

The active scene should be driven by the stable track plus Anthurium’s native smoothing, not by an extra generic syrup layer.

### `src/BuildConfig.h`

Add any new constants required for the upstream stable-track policy. Keep them tight and purpose-driven.

Expected additions:

- preferred near/far acquisition corridor
- harder near minimum if needed
- closer-takeover coherence count / band
- stable-track hold ms
- stable-track fade rate or fade-clear timing

Also tighten production size posture:

- do not add new telemetry profiles
- default contest/lamp production path should not pay for verbose telemetry
- if flash fit becomes tight, cut generic smoothing / telemetry first, not the stable-track contract

### `src/debug/Telemetry.*`

Avoid scope creep. No new verbose telemetry required.

If existing tiny/probe telemetry needs a field source swap because the mapper no longer owns effective-range truth, keep the line format compact and source it from the new stable-track-driven intent. Do not build a paragraph generator into the firmware.

## Constraints

- Preserve the current day/night structure.
- Preserve the ability to have multiple scenes later.
- Do not break the bench probes.
- Do not add new backends or expand abstraction layers.
- Keep the Nano Every contest build inside flash limits.

## Acceptance criteria

The ticket is done only when all of the following are true:

1. **Single-frame potholes do not produce visible active-scene flashing.**
2. **State transitions do not chatter while the stable track is still alive.**
3. **The active scene listens to the same effective target truth that the state machine listens to.**
4. **`MapperC4001` is no longer the primary owner of production track continuity.**
5. **The production build still fits on the Nano Every.**
6. **Bench probes still remain usable for raw-vs-effective debugging.**

## Manual verification sequence

Run these hardware checks after implementation:

1. Empty room, sensor pointed at a far wall.
2. Walk in from ~3 m and pause around ~1.5 m.
3. Move a hand in/out in the close zone.
4. Intentionally cause partial occlusion / awkward near-field motion.
5. Step away and confirm hold → fade → empty rather than flash.
6. Repeat in both dark-allowed and day-blocked conditions.

Expected behavior:

- raw diagnostics may still hop
- effective target should remain behaviorally legible
- scene should feel inertial rather than panicked
- day/night transitions should remain coherent

## Implementation notes

This is not a request to throw away the current app. It is a request to replace the part of it that currently lets two different truths coexist.

The desired result is:

`C4001 raw read → stable track → CorePresence + MapperC4001 → state + scene`

not:

`C4001 raw read → one truth for state, another truth for scene`

That architectural simplification is the point.
