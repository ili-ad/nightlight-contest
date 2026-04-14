#include "PresenceManager.h"

#include "../BuildConfig.h"

void PresenceManager::begin() {
  lastCore_ = {};
  lastC4001Rich_ = {};

  if (BuildConfig::kPresenceBackend == PresenceBackend::C4001) {
    c4001_.begin();
  }
}

CorePresence PresenceManager::readCore() {
  if (BuildConfig::kPresenceBackend == PresenceBackend::C4001) {
    const PresenceC4001::Snapshot snapshot = c4001_.read();
    lastCore_ = snapshot.core;
    lastC4001Rich_ = snapshot.rich;
    return lastCore_;
  }

  lastCore_ = {};
  return lastCore_;
}

const C4001PresenceRich& PresenceManager::lastC4001Rich() const {
  return lastC4001Rich_;
}
