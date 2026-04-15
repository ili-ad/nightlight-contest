#include "App.h"

#include <Arduino.h>

void App::setup() {
  Serial.begin(115200);
  Serial.println("Nightlight v2 scaffold boot");
}

void App::loop() {
  delay(1000);
}
