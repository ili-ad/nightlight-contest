#pragma once

struct AmbientReading {
  bool online = false;
  float luxRaw = 0.0f;
  float luxSmoothed = 0.0f;
};

class AmbientBh1750 {
public:
  void begin();
  AmbientReading read();
};
