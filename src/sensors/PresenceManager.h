#pragma once

#include "PresenceTypes.h"

class PresenceManager {
public:
  void begin();
  CorePresence readCore();
};
