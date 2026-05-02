/*
  C4001RescueSmoke.ino

  Aggressive bench rescue sketch for DFRobot C4001 / SEN0610 over I2C on
  Arduino Nano Every.

  Purpose:
    - Test whether a wedged C4001 can be revived without removing power.
    - Keep the ATmega4809 watchdog armed around every I2C/library call, so a
      blocked Wire transaction should reboot the Arduino instead of hanging forever.
    - Print a breadcrumb before every dangerous call. If the board watchdog-resets,
      the last line printed is the call that likely wedged.

  Wiring expected for I2C:
    C4001 D/T / green -> Nano Every SDA / A4
    C4001 C/R / blue  -> Nano Every SCL / A5
    C4001 +           -> 5V or 3.3V per your build
    C4001 -           -> GND

  Serial Monitor: 115200 baud, Newline not required.

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

  Notes:
    - This sketch intentionally does not auto-initialize the C4001 at boot.
    - The watchdog uses raw ATmega4809 WDT PERIOD value 0x0B (~8s), confirmed by
      your WatchdogSmokeNanoEvery_v2 test reporting WDRF.
    - Command 'g' is intentionally aggressive. It calls eRecoverSen, which the
      DFRobot library describes as "recover params". Use it only in bench/testing.
*/

#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_C4001.h>
#include <avr/wdt.h>

static constexpr uint32_t kBaud = 115200;
static constexpr uint8_t kAddr0 = 0x2A;
static constexpr uint8_t kAddr1 = 0x2B;
static constexpr uint8_t kC4001Address = kAddr1;  // your app uses 0x2B
static constexpr uint8_t kSdaPin = A4;
static constexpr uint8_t kSclPin = A5;
static constexpr uint8_t kAtmega4809WdtPeriod8s = 0x0B;
static constexpr uint32_t kSerialWaitMs = 2000;

// Match the working app's speed/range profile.
static constexpr uint16_t kDetectMinCm = 11;
static constexpr uint16_t kDetectMaxCm = 1200;
static constexpr uint16_t kDetectThreshold = 10;

DFRobot_C4001_I2C radar(&Wire, kC4001Address);

static uint8_t readAndClearResetFlags() {
#if defined(__AVR_ATmega4809__) || defined(__AVR_ATmega4808__)
  const uint8_t flags = RSTCTRL.RSTFR;
  RSTCTRL.RSTFR = flags;
  return flags;
#else
  return 0;
#endif
}

static void printResetFlags(uint8_t flags) {
  Serial.print(F("reset_flags=0x"));
  if (flags < 16) Serial.print('0');
  Serial.print(flags, HEX);
  Serial.print(F(" ["));
  bool any = false;
  if (flags & (1 << 0)) { Serial.print(F("PORF")); any = true; }
  if (flags & (1 << 1)) { if (any) Serial.print(' '); Serial.print(F("BORF")); any = true; }
  if (flags & (1 << 2)) { if (any) Serial.print(' '); Serial.print(F("EXTRF")); any = true; }
  if (flags & (1 << 3)) { if (any) Serial.print(' '); Serial.print(F("WDRF")); any = true; }
  if (flags & (1 << 4)) { if (any) Serial.print(' '); Serial.print(F("SWRF")); any = true; }
  if (flags & (1 << 5)) { if (any) Serial.print(' '); Serial.print(F("UPDIRF")); any = true; }
  if (!any) Serial.print(F("none/unknown"));
  Serial.println(F("]"));
}

static void enableWatchdog() {
  wdt_reset();
  wdt_disable();
  delay(10);
  wdt_reset();
#if defined(__AVR_ATmega4809__) || defined(__AVR_ATmega4808__)
  wdt_enable(kAtmega4809WdtPeriod8s);
  Serial.println(F("wdt=ATmega4809 raw PERIOD=0x0B (~8s)"));
#else
  wdt_enable(WDTO_8S);
  Serial.println(F("wdt=classic AVR WDTO_8S"));
#endif
}

static void feedDelay(uint32_t ms) {
  const uint32_t start = millis();
  while (millis() - start < ms) {
    wdt_reset();
    delay(10);
  }
}

static void banner(const __FlashStringHelper* msg) {
  Serial.print(F("-- "));
  Serial.println(msg);
  wdt_reset();
}

static void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F("  ? help"));
  Serial.println(F("  i raw I2C ping 0x2A/0x2B"));
  Serial.println(F("  x busclear + Wire restart"));
  Serial.println(F("  b begin | s status | t target"));
  Serial.println(F("  S start | O stop | m speed-mode"));
  Serial.println(F("  c stop/start cycle"));
  Serial.println(F("  r eResetSen rescue"));
  Serial.println(F("  w rewrite config/save/start"));
  Serial.println(F("  g AGGRESSIVE eRecoverSen params rescue"));
  Serial.println(F("  a full aggressive ladder"));
  Serial.println(F("  p 2min poll | P 10min poll | h hang"));
}

static uint8_t pingAddress(uint8_t addr) {
  Serial.print(F("ping: 0x"));
  if (addr < 16) Serial.print('0');
  Serial.print(addr, HEX);
  Serial.print(F(" call"));
  Serial.println();
  Wire.beginTransmission(addr);
  const uint8_t err = Wire.endTransmission();
  Serial.print(F("ping: 0x"));
  if (addr < 16) Serial.print('0');
  Serial.print(addr, HEX);
  Serial.print(F(" err="));
  Serial.println(err);
  return err;
}

static void doPingBoth() {
  pingAddress(kAddr0);
  feedDelay(50);
  pingAddress(kAddr1);
}

static void printStatus() {
  banner(F("status: call"));
  sSensorStatus_t st = radar.getStatus();
  Serial.print(F("status: "));
  Serial.print((int)st.workStatus);
  Serial.print('/');
  Serial.print((int)st.workMode);
  Serial.print('/');
  Serial.println((int)st.initStatus);
}

static bool doBegin() {
  banner(F("begin: call"));
  const bool ok = radar.begin();
  Serial.print(F("begin: ok="));
  Serial.println(ok ? 1 : 0);
  return ok;
}

static int doTargetRead() {
  banner(F("target: call"));
  const int n = radar.getTargetNumber();
  float range = 0.0f;
  float speed = 0.0f;
  if (n > 0) {
    range = radar.getTargetRange();
    speed = radar.getTargetSpeed();
  }
  Serial.print(F("target: n="));
  Serial.print(n);
  Serial.print(F(" cm="));
  Serial.print((int)(range * 100.0f));
  Serial.print(F(" v="));
  Serial.println((int)(speed * 100.0f));
  return n;
}

static void i2cBusClear() {
  Serial.print(F("busclear: before sda/scl="));
  Serial.print(digitalRead(kSdaPin));
  Serial.print('/');
  Serial.println(digitalRead(kSclPin));

  Wire.end();
  pinMode(kSdaPin, INPUT_PULLUP);
  pinMode(kSclPin, INPUT_PULLUP);
  feedDelay(5);

  for (uint8_t i = 0; i < 24 && digitalRead(kSdaPin) == LOW; ++i) {
    pinMode(kSclPin, OUTPUT);
    digitalWrite(kSclPin, LOW);
    delayMicroseconds(6);
    pinMode(kSclPin, INPUT_PULLUP);
    delayMicroseconds(6);
    wdt_reset();
  }

  // STOP condition: SDA low while SCL high, then release SDA.
  pinMode(kSdaPin, OUTPUT);
  digitalWrite(kSdaPin, LOW);
  delayMicroseconds(6);
  pinMode(kSclPin, INPUT_PULLUP);
  delayMicroseconds(6);
  pinMode(kSdaPin, INPUT_PULLUP);
  delayMicroseconds(6);

  Serial.print(F("busclear: after sda/scl="));
  Serial.print(digitalRead(kSdaPin));
  Serial.print('/');
  Serial.println(digitalRead(kSclPin));

  Wire.begin();
  feedDelay(80);
  Serial.println(F("busclear: Wire.begin done"));
}

static void doStartOnly() {
  banner(F("start: eStartSen call"));
  radar.setSensor(eStartSen);
  feedDelay(700);
  printStatus();
}

static void doStopOnly() {
  banner(F("stop: eStopSen call"));
  radar.setSensor(eStopSen);
  feedDelay(700);
  printStatus();
}

static bool doSpeedModeOnly() {
  banner(F("mode: eSpeedMode call"));
  const bool ok = radar.setSensorMode(eSpeedMode);
  Serial.print(F("mode: ok="));
  Serial.println(ok ? 1 : 0);
  feedDelay(200);
  printStatus();
  return ok;
}

static void doStopStartCycle() {
  doStopOnly();
  doStartOnly();
  doTargetRead();
}

static void doRewriteConfig() {
  Serial.println(F("rewrite: stop -> speed -> detect -> save -> start"));
  doStopOnly();

  banner(F("rewrite: eSpeedMode"));
  const bool modeOk = radar.setSensorMode(eSpeedMode);
  Serial.print(F("rewrite: modeOk="));
  Serial.println(modeOk ? 1 : 0);
  feedDelay(200);

  banner(F("rewrite: setDetectThres"));
  const bool thOk = radar.setDetectThres(kDetectMinCm, kDetectMaxCm, kDetectThreshold);
  Serial.print(F("rewrite: thOk="));
  Serial.println(thOk ? 1 : 0);
  feedDelay(200);

  banner(F("rewrite: eSaveParams"));
  radar.setSensor(eSaveParams);
  feedDelay(900);

  doStartOnly();
  printStatus();
  doTargetRead();
}

static void doSensorReset() {
  Serial.println(F("reset: begin-before"));
  doBegin();
  printStatus();

  banner(F("reset: eResetSen call"));
  radar.setSensor(eResetSen);
  // The library itself has reset delay; this extra pause lets the module settle.
  feedDelay(2200);

  Serial.println(F("reset: begin-after"));
  const bool ok = doBegin();
  if (ok) {
    doSpeedModeOnly();
    sSensorStatus_t st = radar.getStatus();
    if (st.workStatus == 0) {
      Serial.println(F("reset: status says stopped -> start"));
      radar.setSensor(eStartSen);
      feedDelay(700);
    }
    printStatus();
    doTargetRead();
  }
}

static void doRecoverParams() {
  Serial.println(F("RECOVER: aggressive eRecoverSen params rescue"));
  Serial.println(F("RECOVER: this may restore config defaults; bench only."));

  banner(F("recover: begin-before"));
  doBegin();

  banner(F("recover: eRecoverSen call"));
  radar.setSensor(eRecoverSen);
  feedDelay(1800);

  banner(F("recover: begin-after"));
  doBegin();

  doRewriteConfig();
}

static void doAggressiveLadder() {
  Serial.println(F("LADDER: busclear -> ping -> begin -> reset -> cycle -> rewrite -> recover"));
  i2cBusClear();
  doPingBoth();

  if (doBegin()) {
    printStatus();
    doTargetRead();
  }

  doSensorReset();
  doStopStartCycle();
  doRewriteConfig();
  doRecoverParams();
  Serial.println(F("LADDER: done"));
}

static void doPollSoak(uint32_t durationMs) {
  Serial.print(F("poll: ms="));
  Serial.print(durationMs);
  Serial.println(F(" at 100ms; prints every 50 polls"));
  uint16_t okReads = 0;
  uint16_t polls = 0;
  const uint32_t start = millis();
  while (millis() - start < durationMs) {
    ++polls;
    const int n = radar.getTargetNumber();
    if (n >= 0) ++okReads;
    if ((polls % 50) == 0) {
      Serial.print(F("poll: i="));
      Serial.print(polls);
      Serial.print(F(" n="));
      Serial.print(n);
      Serial.print(F(" ok="));
      Serial.println(okReads);
    }
    feedDelay(100);
  }
  Serial.println(F("poll: done"));
}

static void deliberateHang() {
  Serial.println(F("hang: deliberate CPU deadlock; WDT should reset in ~8s"));
  noInterrupts();
  while (true) {}
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  const uint8_t flags = readAndClearResetFlags();

  Serial.begin(kBaud);
  const uint32_t start = millis();
  while (!Serial && (millis() - start < kSerialWaitMs)) {}

  Serial.println();
  Serial.println(F("=== C4001RescueSmoke ==="));
  printResetFlags(flags);
  Wire.begin();
  feedDelay(80);
  enableWatchdog();
  printHelp();
  Serial.println(F("Ready. No automatic radar init; type a command."));
}

void loop() {
  wdt_reset();
  digitalWrite(LED_BUILTIN, (millis() / 500) % 2);

  while (Serial.available() > 0) {
    const char c = Serial.read();
    switch (c) {
      case '?': printHelp(); break;
      case 'i': case 'I': doPingBoth(); break;
      case 'x': case 'X': i2cBusClear(); break;
      case 'b': case 'B': doBegin(); break;
      case 's': printStatus(); break;
      case 't': case 'T': doTargetRead(); break;
      case 'S': doStartOnly(); break;
      case 'O': case 'o': doStopOnly(); break;
      case 'm': case 'M': doSpeedModeOnly(); break;
      case 'c': case 'C': doStopStartCycle(); break;
      case 'r': case 'R': doSensorReset(); break;
      case 'w': case 'W': doRewriteConfig(); break;
      case 'g': case 'G': doRecoverParams(); break;
      case 'a': case 'A': doAggressiveLadder(); break;
      case 'p': doPollSoak(120000UL); break;
      case 'P': doPollSoak(600000UL); break;
      case 'h': case 'H': deliberateHang(); break;
      case '\n': case '\r': case ' ': break;
      default:
        Serial.print(F("unknown: "));
        Serial.println(c);
        break;
    }
    wdt_reset();
  }
}
