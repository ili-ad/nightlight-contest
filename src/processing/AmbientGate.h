#pragma once

struct AmbientGateResult {
  bool darkAllowed = false;
};

class AmbientGate {
public:
  AmbientGateResult update(float lux);

private:
  bool darkAllowed_ = false;
  bool initialized_ = false;
};
