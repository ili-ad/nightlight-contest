#include "PresenceManager.h"

#include "../BuildConfig.h"

void PresenceManager::begin() {
  lastCore_ = {};
  lastC4001Rich_ = {};
  lastC4001LinkStatus_ = {};

  if (BuildConfig::kPresenceBackend == PresenceBackend::C4001) {
    c4001_.begin();
    lastC4001LinkStatus_ = c4001_.linkStatus();
  }
}

CorePresence PresenceManager::readCore() {
  if (BuildConfig::kPresenceBackend == PresenceBackend::C4001) {
    const PresenceC4001::Snapshot snapshot = c4001_.read();
    lastCore_ = snapshot.core;
    lastC4001Rich_ = snapshot.rich;
    lastC4001LinkStatus_ = c4001_.linkStatus();
    return lastCore_;
  }

  lastCore_ = {};
  return lastCore_;
}

const C4001PresenceRich& PresenceManager::lastC4001Rich() const {
  return lastC4001Rich_;
}

const PresenceC4001::LinkStatus& PresenceManager::c4001LinkStatus() const {
  return lastC4001LinkStatus_;
}
