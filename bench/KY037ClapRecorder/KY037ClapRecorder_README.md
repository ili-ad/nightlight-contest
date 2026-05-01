# KY037 Clap Recorder

This is a diagnostic companion to `KY037ClapSmoke.ino`. It does **not** try to detect a clap. It records the KY-037 analog microphone signal around a known visual cue so the final detector can be tuned from real sensor data instead of threshold guesswork.

## Upload

1. Open `KY037ClapRecorder.ino` in Arduino IDE.
2. Select the same board and port you use for Clap Smoke.
3. Confirm the hardware pins match your build:
   - KY-037 analog signal: `A0`
   - NeoPixel strip: pin `6`
   - 144 GRBW pixels
4. Upload.
5. Open Serial Monitor at `115200` baud.

## Run

The strip starts dim blue. Every 10 seconds it turns red. Clap when it turns red.

The sketch captures:

- about 256 ms before the red cue
- about 1.28 seconds after the red cue
- one sample every 1 ms

After each capture, the strip turns purple while it prints the log block.

You can also send `c` from Serial Monitor to force an immediate cue.

## What to send back

Copy complete blocks from:

```text
BEGIN_TRIAL,...
...
END_TRIAL,...
```

Five to ten trials would be ideal. Include a few normal claps, a few softer claps, and one or two intentional non-clap background-noise trials if practical.

## Useful fields

- `raw`: direct analogRead value from the KY-037 analog pin.
- `dev`: absolute deviation from the pre-cue baseline.
- `fast_env`: quick envelope, useful for clap detection.
- `slow_env`: slower envelope, useful for comparing against the current Clap Smoke smoothing.
- `p2p12`: local peak-to-peak movement over roughly 12 ms.
- `SUMMARY`: one-line stats for the trial.
- `SUGGESTION`: rough starting thresholds, not final detector values.
