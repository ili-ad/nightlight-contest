#include <Wire.h>
#include "DFRobot_C4001.h"

DFRobot_C4001_I2C radar(&Wire, 0x2B);

constexpr unsigned long kPrintMs = 150;

void setup() {
  Serial.begin(19200);
  delay(500);
  Wire.begin();

  while (!radar.begin()) {
    Serial.println("C4001 not found");
    delay(1000);
  }

  radar.setSensorMode(eSpeedMode);
  radar.setDetectThres(11, 1200, 10);
  radar.setFrettingDetection(eON);

  Serial.println("C4001 API audit probe ready");
  Serial.println("Accessible fields in this path: target_count, range_m, speed_mps, energy");
  Serial.println("Directional fields (angle/azimuth/lateral/xy/beam index) are not exposed in this I2C API path.");
}

void loop() {
  const unsigned long now = millis();
  static unsigned long lastPrint = 0;

  if (now - lastPrint < kPrintMs) {
    delay(10);
    return;
  }
  lastPrint = now;

  const uint8_t targets = radar.getTargetNumber();
  const float rangeM = radar.getTargetRange();
  const float speedMps = radar.getTargetSpeed();
  const uint32_t energy = radar.getTargetEnergy();

  Serial.print("targets=");
  Serial.print(targets);
  Serial.print(" range_m=");
  Serial.print(rangeM, 3);
  Serial.print(" speed_mps=");
  Serial.print(speedMps, 3);
  Serial.print(" energy=");
  Serial.println(energy);
}
