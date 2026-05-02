WatchdogSmokeNanoEvery v2

Copy bench/WatchdogSmokeNanoEvery into your NightlightContest/bench folder.
Open WatchdogSmokeNanoEvery.ino and upload to Arduino Nano Every.
Open Serial Monitor at 115200.

Expected sequence:
1. It prints reset flags and ticks for ~15 seconds.
2. It prints HANG and deliberately deadlocks.
3. The watchdog should reset the board after roughly 8 seconds.
4. On reboot, reset_flags should include WDRF.

If it never reboots after HANG, the watchdog is not active.

This v2 uses raw ATmega4809 WDT PERIOD value 0x0B (~8s) rather than
checking WDT_PERIOD_8KCLK_gc with #if defined(), because that watchdog
period name is an enum constant, not a preprocessor macro.
