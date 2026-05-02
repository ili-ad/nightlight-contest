/*
  C4001RebootSmoke.ino

  Bench smoke test for DFRobot C4001 / SEN0610 over I2C on Arduino Nano Every.

  Purpose:
    - Prove whether the C4001 can be revived by software commands while keeping
      the Nano Every watchdog armed.
    - If an I2C call wedges, the watchdog should reset the Nano Every rather
      than leaving the board dead.

  Wiring expected for I2C:
    C4001 D/T / green -> Nano Every SDA / A4
    C4001 C/R / blue  -> Nano Every SCL / A5
    C4001 +           -> 5V or 3.3V per your build
    C4001 -           -> GND

  Serial Monitor: 115200 baud, Newline not required.

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

  Notes:
    - This sketch intentionally does not auto-initialize the C4001 at boot.
      That lets you boot into the prompt even if the radar is in a bad state.
    - The watchdog uses the smoke-tested ATmega4809 raw WDT PERIOD value 0x0B
      (~8 seconds). If an I2C transaction blocks, the board should reboot and
      print WDRF on the next boot.
*/

#include <Arduino.h>
#include <Wire.h>
#include <DFRobot_C4001.h>
#include <avr/wdt.h>

static constexpr uint32_t kBaud = 115200;
static constexpr uint8_t kC4001Address = 0x2B;
static constexpr uint8_t kSdaPin = A4;
static constexpr uint8_t kSclPin = A5;
static constexpr uint8_t kAtmega4809WdtPeriod8s = 0x0B;
static constexpr uint32_t kSerialWaitMs = 2000;

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

static void printHelp() {
  Serial.println(F("Commands: ? help | b begin | s status | t target | x busclear | r reset | c stop/start | m speed-mode | p poll-soak | h hang"));
}

static void printStatus() {
  Serial.println(F("status: call"));
  sSensorStatus_t st = radar.getStatus();
  Serial.print(F("status: "));
  Serial.print((int)st.workStatus);
  Serial.print('/');
  Serial.print((int)st.workMode);
  Serial.print('/');
  Serial.println((int)st.initStatus);
}

static bool doBegin() {
  Serial.println(F("begin: call"));
  const bool ok = radar.begin();
  Serial.print(F("begin: ok="));
  Serial.println(ok ? 1 : 0);
  return ok;
}

static void doTargetRead() {
  Serial.println(F("target: call"));
  const int n = radar.getTargetNumber();
  float range = 0.0f;
  float speed = 0.0f;
  if (n > 0) {
    range = radar.getTargetRange();
    speed = radar.getTargetSpeed();
  }
  Serial.print(F("target: n="));
  Serial.print(n);
  Serial.print(F(" range_cm="));
  Serial.print((int)(range * 100.0f));
  Serial.print(F(" speed_cm_s="));
  Serial.println((int)(speed * 100.0f));
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

  for (uint8_t i = 0; i < 16 && digitalRead(kSdaPin) == LOW; ++i) {
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
  feedDelay(60);
  Serial.println(F("busclear: Wire.begin done"));
}

static void doSpeedMode() {
  Serial.println(F("mode: eSpeedMode call"));
  const bool ok = radar.setSensorMode(eSpeedMode);
  Serial.print(F("mode: ok="));
  Serial.println(ok ? 1 : 0);
  feedDelay(150);
  printStatus();
}

static void doStopStart() {
  Serial.println(F("cycle: eStopSen call"));
  radar.setSensor(eStopSen);
  feedDelay(500);
  printStatus();
  Serial.println(F("cycle: eStartSen call"));
  radar.setSensor(eStartSen);
  feedDelay(500);
  printStatus();
}

static void doSensorReset() {
  Serial.println(F("reset: begin-before"));
  doBegin();
  printStatus();

  Serial.println(F("reset: eResetSen call"));
  radar.setSensor(eResetSen);
  // DFRobot library already delays for reset; this extra delay is cheap and explicit.
  feedDelay(1800);

  Serial.println(F("reset: begin-after"));
  const bool ok = doBegin();
  if (ok) {
    doSpeedMode();
    sSensorStatus_t st = radar.getStatus();
    if (st.workStatus == 0) {
      Serial.println(F("reset: sensor was stopped, eStartSen call"));
      radar.setSensor(eStartSen);
      feedDelay(500);
    }
    printStatus();
  }
}

static void doPollSoak() {
  Serial.println(F("poll: 2min at 100ms; prints every 50 polls"));
  uint16_t okReads = 0;
  for (uint16_t i = 1; i <= 1200; ++i) {
    const int n = radar.getTargetNumber();
    if (n >= 0) ++okReads;
    if ((i % 50) == 0) {
      Serial.print(F("poll: i="));
      Serial.print(i);
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
  Serial.println(F("=== C4001RebootSmoke ==="));
  printResetFlags(flags);
  Wire.begin();
  feedDelay(60);
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
      case 'b': case 'B': doBegin(); break;
      case 's': case 'S': printStatus(); break;
      case 't': case 'T': doTargetRead(); break;
      case 'x': case 'X': i2cBusClear(); break;
      case 'r': case 'R': doSensorReset(); break;
      case 'c': case 'C': doStopStart(); break;
      case 'm': case 'M': doSpeedMode(); break;
      case 'p': case 'P': doPollSoak(); break;
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
