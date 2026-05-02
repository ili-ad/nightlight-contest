/*
  WatchdogSmokeNanoEvery.ino

  Bench smoke test for Arduino Nano Every / ATmega4809 watchdog behavior.

  This v2 sketch avoids #if defined(WDT_PERIOD_8KCLK_gc), because
  WDT_PERIOD_8KCLK_gc is an enum constant in iom4809.h, not a preprocessor
  macro. The preprocessor cannot see enum names, so #if defined(...) gives a
  false negative even when normal C++ code can use the value.

  Instead, this sketch enables the watchdog with the raw ATmega4809 PERIOD
  field value for 8KCLK: 0x0B, which the ATmega4808/4809 datasheet defines as
  the ~8 second watchdog period.

  What it does:
    1. Prints reset flags at boot.
    2. Disables any stale watchdog state.
    3. Enables watchdog using raw ATmega4809 value 0x0B (~8s).
    4. Feeds the watchdog for 15 seconds.
    5. Deliberately wedges the CPU.
    6. If WDT is working, the board reboots and prints WDRF on next boot.

  Board: Arduino Nano Every
  Serial Monitor: 115200 baud
*/

#include <Arduino.h>
#include <avr/wdt.h>

static constexpr uint32_t kBaud = 115200;
static constexpr uint32_t kSerialWaitMs = 2000;
static constexpr bool kAutoTrigger = true;       // Set false to trigger only by typing 'h'.
static constexpr uint32_t kAutoHangAfterMs = 15000;
static constexpr uint32_t kTickMs = 1000;

// ATmega4808/4809 WDT.CTRLA PERIOD[3:0] values:
// 0x09 = ~2s, 0x0A = ~4s, 0x0B = ~8s.
// Use raw value to avoid depending on enum symbol visibility in this core.
static constexpr uint8_t kAtmega4809WdtPeriod8s = 0x0B;

static uint32_t gBootMs = 0;
static uint32_t gLastTickMs = 0;
static uint16_t gTick = 0;
static bool gDidHang = false;

static uint8_t readAndClearResetFlags() {
#if defined(__AVR_ATmega4809__) || defined(__AVR_ATmega4808__)
  const uint8_t flags = RSTCTRL.RSTFR;
  RSTCTRL.RSTFR = flags;  // ATmega4809 clears reset flags by writing 1s back.
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

static void enableWatchdogForNanoEvery() {
  wdt_reset();
  wdt_disable();
  delay(10);  // Let disable settle before re-enable on Nano Every / ATmega4809.
  wdt_reset();

#if defined(__AVR_ATmega4809__) || defined(__AVR_ATmega4808__)
  wdt_enable(kAtmega4809WdtPeriod8s);
  Serial.println(F("wdt=ATmega4809 raw PERIOD=0x0B (~8s)"));
#else
  // Classic AVR fallback, not expected for Nano Every.
  wdt_enable(WDTO_8S);
  Serial.println(F("wdt=classic AVR WDTO_8S"));
#endif
}

static void deliberateHang() {
  if (gDidHang) return;
  gDidHang = true;

  Serial.println(F("HANG: deliberate deadlock now; watchdog should reset the board."));
  Serial.println(F("If this is the last line forever, WDT did not fire."));
  Serial.flush();

  digitalWrite(LED_BUILTIN, HIGH);
  noInterrupts();
  while (true) {
    // Intentionally do not call wdt_reset().
    // WDT should run from its independent clock and reset the MCU.
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  const uint8_t flags = readAndClearResetFlags();

  Serial.begin(kBaud);
  const uint32_t serialStart = millis();
  while (!Serial && (millis() - serialStart < kSerialWaitMs)) {
    // Wait briefly for Serial Monitor, then continue even if unopened.
  }

  Serial.println();
  Serial.println(F("=== WatchdogSmokeNanoEvery v2 ==="));
  Serial.print(F("boot_ms="));
  Serial.println(millis());
  printResetFlags(flags);
  Serial.println(F("Commands: h=hang now, f=feed once, ?=help"));

  enableWatchdogForNanoEvery();

  gBootMs = millis();
  gLastTickMs = gBootMs;
  Serial.println(F("armed: feeding watchdog; auto-hang in 15s."));
}

void loop() {
  wdt_reset();

  while (Serial.available() > 0) {
    const char c = Serial.read();
    if (c == 'h' || c == 'H') {
      deliberateHang();
    } else if (c == 'f' || c == 'F') {
      wdt_reset();
      Serial.println(F("feed"));
    } else if (c == '?') {
      Serial.println(F("h: deliberate hang; f: feed watchdog; auto-hang after 15s"));
    }
  }

  const uint32_t now = millis();
  if (now - gLastTickMs >= kTickMs) {
    gLastTickMs = now;
    ++gTick;

    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print(F("tick="));
    Serial.print(gTick);
    Serial.print(F(" ms="));
    Serial.println(now - gBootMs);
  }

  if (kAutoTrigger && !gDidHang && (now - gBootMs >= kAutoHangAfterMs)) {
    deliberateHang();
  }
}
