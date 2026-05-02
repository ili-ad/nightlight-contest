C4001RescueSmoke
================

Aggressive bench rescue sketch for the DFRobot C4001 / SEN0610 over I2C on
Arduino Nano Every.

Put bench/C4001RescueSmoke/C4001RescueSmoke.ino into the repo's bench folder,
open it in Arduino IDE, select Arduino Nano Every, and upload.

Serial Monitor: 115200 baud.

This sketch intentionally does NOT auto-initialize the radar at boot. It arms the
ATmega4809 watchdog with raw PERIOD=0x0B (~8s), then waits for commands.

Commands:
  ?  help
  i  raw I2C ping 0x2A and 0x2B
  x  I2C bus clear + Wire restart
  b  begin/probe C4001
  s  status read
  t  target read
  S  eStartSen only
  O  eStopSen only
  m  set eSpeedMode, then status
  c  stop/start cycle, then status/target
  r  eResetSen reboot sequence, then begin/mode/start/status/target
  w  rewrite working config: stop, speed mode, detect threshold, save, start
  g  aggressive recover params: eRecoverSen, then reconfigure/start/status
  a  aggressive rescue ladder: busclear, ping, begin, reset, cycle, rewrite, recover
  p  2-minute poll soak at 100ms cadence
  P  10-minute poll soak at 100ms cadence
  h  deliberate CPU hang to confirm watchdog reset

Suggested sequence after clean power cycle:
  i
  b
  s
  t
  r
  s
  t
  p

Suggested sequence after the sculpture app falls into the bad state:
  1. Do NOT power-cycle the whole piece.
  2. Press the Arduino reset button or upload this sketch, leaving the C4001 powered.
  3. Try: i, x, i, b, s, r, s, t
  4. If still dead, try: c, w, s, t
  5. Last bench-only resort: g, s, t or a

Interpreting results:
  - If the board resets with WDRF, the last printed line tells you which I2C/library
    call wedged.
  - If i/x/b/s/r all fail until a full power cycle, software cannot revive the sensor
    in this state; production needs hardware power switching or UART.
  - If r/w/g revive it, that exact sequence can be ported back into the sculpture app.

Warning:
  Command 'g' calls eRecoverSen, described by the DFRobot library as "recover params".
  Treat it as bench-only until proven harmless in your setup.
