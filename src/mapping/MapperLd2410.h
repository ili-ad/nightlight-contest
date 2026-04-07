#pragma once

#include "MapperShared.h"
#include "../behavior/BehaviorContext.h"
#include "../sensors/PresenceTypes.h"

class MapperLd2410 {
public:
  RenderIntent map(const BehaviorContext& context, const Ld2410PresenceRich& rich) const;

private:
  MapperShared mShared;
};
