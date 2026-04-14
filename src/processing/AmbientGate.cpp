#include "AmbientGate.h"

#include "../BuildConfig.h"

AmbientGateResult AmbientGate::update(float lux) {
  if (!initialized_) {
    darkAllowed_ = (lux <= BuildConfig::kDarkEnterLux);
    initialized_ = true;
  } else if (darkAllowed_) {
    if (lux >= BuildConfig::kDarkExitLux) {
      darkAllowed_ = false;
    }
  } else if (lux <= BuildConfig::kDarkEnterLux) {
    darkAllowed_ = true;
  }

  AmbientGateResult result;
  result.darkAllowed = darkAllowed_;
  return result;
}
