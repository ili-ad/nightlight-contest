#pragma once

#include "MapperShared.h"
#include "../behavior/BehaviorContext.h"
#include "../sensors/PresenceTypes.h"

class MapperC4001 {
public:
  RenderIntent map(const BehaviorContext& context, const C4001PresenceRich& rich) const;

private:
  MapperShared mShared;
};
