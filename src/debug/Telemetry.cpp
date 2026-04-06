#include "Telemetry.h"
#include <Arduino.h>

void Telemetry::begin() {
  Serial.begin(115200);
}

void Telemetry::update(const LampStateMachine& stateMachine) {
  (void)stateMachine;
}
