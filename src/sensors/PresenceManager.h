#pragma once

#include "PresenceC4001.h"
#include "PresenceTypes.h"

class PresenceManager {
public:
  void begin();
  CorePresence readCore();

  const C4001PresenceRich& lastC4001Rich() const;

private:
  PresenceC4001 c4001_;
  CorePresence lastCore_{};
  C4001PresenceRich lastC4001Rich_{};
};
