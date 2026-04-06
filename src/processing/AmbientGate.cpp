#include "AmbientGate.h"

AmbientGateResult AmbientGate::update(float lux) {
  AmbientGateResult result;
  result.darkAllowed = (lux < 10.0f);
  return result;
}
