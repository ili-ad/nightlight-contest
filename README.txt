C4001RebootSmoke
================

Bench smoke test for the DFRobot C4001 / SEN0610 over I2C on Arduino Nano Every.

Put bench/C4001RebootSmoke/C4001RebootSmoke.ino into the repo's bench folder,
open it in Arduino IDE, select Arduino Nano Every, and upload.

Serial Monitor: 115200 baud.

Commands:
  ?  help
  b  begin/probe C4001
  s  status read
  t  target read
  x  I2C bus clear + Wire restart
  r  C4001 eResetSen reboot sequence, then begin/mode/status
  c  C4001 stop/start cycle, then status
  m  set speed/ranging mode, then status
  p  2-minute poll soak at 100 ms cadence
  h  deliberate CPU hang to confirm watchdog reset

Important behavior:
  - The sketch does not auto-initialize the radar at boot. This avoids boot-looping
    immediately if the radar is in a bad state.
  - The Nano Every watchdog is armed using raw PERIOD=0x0B (~8s), which your
    WatchdogSmokeNanoEvery_v2 bench test confirmed by reporting WDRF.
  - If any I2C operation blocks forever, the watchdog should reset the Arduino and
    the next boot should print WDRF.

Suggested smoke sequence after a clean power cycle:
  b
  s
  t
  r
  s
  t
  p

Suggested smoke sequence if the radar/app has fallen into the bad state:
  1. Press the Arduino reset button, do NOT power-cycle the C4001.
  2. Upload/run this sketch if needed.
  3. Try: x, b, s, r, s, t
  4. If the sketch resets with WDRF during a command, note the last printed line.
  5. If nothing revives it until full power-cycle, the C4001 needs hardware power control for production.
