#!/usr/bin/env bash
set -euo pipefail

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "Error: arduino-cli is not installed or not on PATH." >&2
  echo "Install it from https://arduino.github.io/arduino-cli/ and retry." >&2
  exit 127
fi

FQBN="${ARDUINO_FQBN:-arduino:megaavr:nona4809}"

echo "Compiling with FQBN: ${FQBN}"

arduino-cli compile --fqbn "${FQBN}" NightlightContest.ino
arduino-cli compile --fqbn "${FQBN}" bench/CommissioningTopology112/CommissioningTopology112.ino

echo "Compile smoke test passed for both production and commissioning sketches."
