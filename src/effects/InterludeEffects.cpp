#include "InterludeEffects.h"

InterludeFrame InterludeEffects::marchingAnts(uint32_t elapsedMs) {
  InterludeFrame frame;
  frame.spacing = 4;
  frame.offset = static_cast<uint8_t>((elapsedMs / 90UL) % frame.spacing);
  frame.level = 64;
  return frame;
}