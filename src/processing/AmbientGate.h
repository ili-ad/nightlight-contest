#pragma once

struct AmbientGateResult {
  bool darkAllowed = false;
};

class AmbientGate {
public:
  AmbientGateResult update(float lux);
};
