#pragma once

#include "RenderIntent.h"
#include "../behavior/BehaviorContext.h"

class MapperShared {
public:
  RenderIntent map(const BehaviorContext& context);

private:
  RenderIntent mapDayDormant(const BehaviorContext& context) const;
  RenderIntent mapNightIdle(const BehaviorContext& context) const;
  RenderIntent mapActiveInterpretive(const BehaviorContext& context) const;
  RenderIntent mapDecay(const BehaviorContext& context) const;
  RenderIntent mapFaultSafe(const BehaviorContext& context) const;
};
