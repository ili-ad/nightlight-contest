# ARCH-059 · Finish C4001 stable-track parity with bench behavior

## Why this follow-up exists

PR #59 successfully moved the C4001 continuity engine upstream into `PresenceC4001` and eliminated the architectural split where state and scene could listen to different truths.

That is real progress.

However, the implementation does **not yet** match the most valuable behavior proven by the bench sketch. It ports the continuity engine upstream, but it still derives the stable track from the existing `C4001TrackFilter` and the old target-acceptance policy. That leaves several important gaps relative to the bench behavior that actually calmed the lamp:

1. decay still scales the stored range toward zero instead of keeping range steady and fading visibility
2. upstream acceptance still lacks a nearer-candidate takeover path and preferred corridor bias
3. `CorePresence` is still synthesized from range/speed-derived confidence rather than from an explicit stable-track visibility / continuity value
4. top-level active/decay intent smoothing is still layered over Anthurium’s own native smoothing

This follow-up closes that behavioral gap.

## Relationship to ARCH-058

ARCH-058 was the architecture move.

ARCH-059 is the behavioral parity move.

Do **not** undo the upstream relocation from PR #59. Build on it.

## Primary outcome

After this follow-up, the production lamp should behave like the successful bench probe in the ways that matter:

- raw readings may still hop
- the effective target should not flash or lurch toward zero on potholes
- closer coherent targets should be able to take over from stale far locks
- state transitions should reflect stable-track continuity rather than raw target churn

## Specific gaps in PR #59 that this ticket addresses

### Gap 1 · Range still decays toward zero

The current upstream stable track is fed through the existing `C4001TrackFilter`, whose decay path multiplies `rangeM` and `smoothedRangeM` by the decay scale. That means the effective target can still visually drift toward zero during decay.

That is **not** the desired bench behavior.

Desired behavior:

- during hold / fade, effective range remains anchored to the last accepted range
- only visibility / influence decays
- the target does not slide toward the origin just because a few frames were missed

### Gap 2 · Acceptance policy still uses the old gate

The current upstream move preserved the older `acceptTargetSample(...)` logic. That means the app still lacks the bench-side improvements that were meant to stop far-wall dominance and permit coherent nearer takeover.

Desired behavior:

- acquisition should be biased toward the useful approach corridor rather than any valid far reflector
- a repeated nearer candidate should be allowed to steal the track from a stale farther lock
- do not require full target disappearance before nearer takeover is possible

### Gap 3 · No explicit stable visibility / influence field

The current stable-track payload includes `hasTrack`, phase, age, range, and scene-driving fields, but not an explicit visibility / influence scalar.

That omission forces `CorePresence` to re-synthesize confidence from range/speed instead of reading the continuity contract directly.

Desired behavior:

- carry an explicit stable visibility / influence field on the upstream stable track
- derive `presenceConfidence` directly from that field
- let state transitions reflect continuity / fade rather than a reconstructed guess

### Gap 4 · Redundant active/decay smoothing still present

The current app still applies top-level `RenderIntentSmoother` during `ActiveInterpretive` and `Decay`, even though Anthurium already applies internal smoothing, deadbanding, and brightness slew.

This is not the main bug, but it is avoidable overhang and may cost flash while making debugging murkier.

Desired behavior:

- bypass the generic top-level intent smoother for `ActiveInterpretive` and `Decay`
- keep or preserve it for simpler non-procedural states if useful

## File-by-file called shot

### `src/mapping/C4001TrackFilter.h` and `src/mapping/C4001TrackFilter.cpp`

Refactor the production filter semantics so decay no longer scales range toward zero.

Required behavioral change:

- hold / soft-reject / link-issue phases keep the last accepted range steady
- decay phase reduces influence / visibility, not range
- when influence reaches zero, the filter clears to empty

If this requires adding a visibility / influence field to `Sample` or `Output`, do it cleanly and document the invariant.

Do **not** preserve the old `range *= scale` behavior just because it already compiles.

### `src/sensors/PresenceTypes.h`

Extend the stable-track payload with an explicit visibility / influence value.

Expected additions:

- `stableVisibility` or equivalent
- any renamed / clarified stable-track semantics needed to make the contract legible

The goal is to let upstream continuity be communicated directly rather than inferred later.

### `src/sensors/PresenceC4001.h` and `src/sensors/PresenceC4001.cpp`

Keep the upstream stable-track ownership from PR #59, but improve the acquisition / takeover policy.

#### Required changes

Implement an approach-biased target-selection policy more like the bench sketch:

- prefer the useful corridor around roughly `0.75 m .. 3.20 m`
- do not fall in love with a static far target too easily
- allow a repeated, coherent nearer candidate to take over from a farther stable track
- preserve the existing speed-cap / sanity checks

Do not let this balloon into a full multi-target tracker. The chip does not expose enough geometry on this path for that. This is simply better arbitration of the one target reading we do have.

#### `CorePresence` derivation

Rework `buildCoreFromStableTrack(...)` so that:

- `presenceConfidence` is driven by stable visibility / influence
- `present` is tied to stable-track existence / influence rather than raw target presence alone
- distance and motion hints still come from the stable track

### `src/mapping/MapperC4001.cpp`

Continue consuming the upstream stable track.

Required behavior after the filter change:

- use stable range directly for scene target position
- use stable visibility / influence indirectly through scene-driving fields, not by re-inventing continuity locally
- do not add another tracker or another continuity policy here

### `src/App.cpp`

Change `shouldSmoothIntent(...)` so `ActiveInterpretive` and `Decay` do **not** pass through the generic `RenderIntentSmoother`.

Desired behavior:

- Anthurium’s own internal smoothing remains the active-scene smoothing layer
- generic intent smoothing remains available for simpler states if still wanted

## Constraints

- Preserve the upstream relocation delivered by PR #59
- Do not reintroduce dual truth between state and scene
- Keep bench probes working
- Avoid scope creep into microphone/clap or a general scene system
- Keep the Nano Every build viable

## Acceptance criteria

This ticket is done only when all of the following are true:

1. **Dropout fade does not drag the effective target range toward zero.**
2. **The lamp can prefer or take over to a coherent nearer target instead of clinging to a stale far-wall lock.**
3. **`CorePresence.presenceConfidence` is driven by explicit stable-track continuity / visibility.**
4. **`ActiveInterpretive` and `Decay` do not receive an extra generic smoothing layer on top of Anthurium.**
5. **No second continuity engine is reintroduced in the mapper.**

## Manual verification sequence

1. Point the sensor into an empty room with a far wall.
2. Approach from ~3 m and pause around ~1.5 m.
3. Move a hand or body in the close zone.
4. Intentionally create brief occlusions / potholes.
5. Step away and observe hold → fade → empty.

Expected behavior:

- the effective target may fade, but it should not visibly run toward zero range during dropout
- nearer coherent motion should be able to capture the lamp from a stale farther lock
- active scene should feel inertial, not panicked

## Implementation posture

This is a targeted follow-up, not an excuse to redesign the app again.

Keep the good architectural move from PR #59.
Finish the behavioral move that the bench sketch actually proved.
