#pragma once

#include "PresenceC4001.h"
#include "PresenceTypes.h"

class PresenceManager {
public:
  void begin();
  CorePresence readCore();

  const C4001PresenceRich& lastC4001Rich() const;
  const PresenceC4001::LinkStatus& c4001LinkStatus() const;

private:
  PresenceC4001 c4001_;
  CorePresence lastCore_{};
  C4001PresenceRich lastC4001Rich_{};
  PresenceC4001::LinkStatus lastC4001LinkStatus_{};
};
