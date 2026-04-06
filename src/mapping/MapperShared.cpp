#include "MapperShared.h"

RenderIntent MapperShared::map(const BehaviorContext& context) {
  RenderIntent intent;
  intent.effectId = static_cast<int>(context.state);
  return intent;
}
