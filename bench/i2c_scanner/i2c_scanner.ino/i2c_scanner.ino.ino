#include <Wire.h>

void setup() {
  Serial.begin(19200);
  delay(500);

  Wire.begin();

  Serial.println();
  Serial.println("I2C scanner starting...");
}

void loop() {
  byte count = 0;

  Serial.println("Scanning...");

  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Found device at 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
      count++;
    } else if (error == 4) {
      Serial.print("Unknown error at 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }

  if (count == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print("Total devices found: ");
    Serial.println(count);
  }

  Serial.println();
  delay(2000);
}