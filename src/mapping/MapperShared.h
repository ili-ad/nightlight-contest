#pragma once

#include "RenderIntent.h"
#include "../behavior/BehaviorContext.h"

class MapperShared {
public:
  RenderIntent map(const BehaviorContext& context);
};
