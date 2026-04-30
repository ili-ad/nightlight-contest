# Nightlight Contest

## Project layout (v2 reset)

- `archive/nightlight-v1/`: full legacy snapshot of the original app (`NightlightContest.ino` + the original `src/` tree).
- `bench/`: hardware and behavior bench sketches (kept intact and used for commissioning diagnostics).
- `src/`: new minimal v2 app scaffold.

## Final production hardware contract

- **LED count:** 112
- **LED type:** 5V SK6812 RGBW
- **Data pin:** D6
- **Physical order:** J1, J2, front ring, rear ring
- **Physical direction contract:**
  - J1 top → bottom
  - J2 bottom → top
  - Front ring 6 o’clock clockwise back to 6
  - Rear ring 6 o’clock counterclockwise back to 6
- **Sensors in final build:**
  - C4001 radar remains active
  - Clap mic remains active on A0
  - No ambient light sensor in production build

## Final production behavior contract

- Startup behavior: progressive topology fill intro.
- Runtime modes: Anthurium (interactive), Nightlight, Off.
- Clap cycle: Anthurium → Nightlight → Off → Anthurium.

## Bench commissioning sketch (final topology)

Use `bench/CommissioningTopology112/CommissioningTopology112.ino` as the hardware verification sketch for the final topology.

Included checks:
1. Progressive physical fill 0 → 111.
2. Segment fill for J1, J2, front ring, rear ring.
3. Direction test per segment.
4. Low all-white RGBW test.
5. Low-level R/G/B/W channel test.

This sketch is intentionally isolated under `bench/` and does not modify the production app surface in `NightlightContest.ino` / `src/`.

## Power safety rule

- Keep supply voltage fixed at **5.00V**.
- Raise only the **bench supply current limit** during diagnostics.
- Keep commissioning brightness conservative (e.g., 24; max 32) until thermal/current margins are re-verified.
