// Modified by TyraX: bounded, opt-in EE timeline. No pipeline barriers.
#pragma once
#include <tamtypes.h>

namespace Tyra { namespace HardwareTrace {
extern bool active;
u32 ticks();
void configure();
void beginFrame();
void endFrame();
void record(const char* label, u32 start, u32 end, u32 value = 0);
void state(const char* label);
struct Scope {
  const char* label;
  u32 start;
  explicit Scope(const char* name) : label(name), start(active ? ticks() : 0) {}
  ~Scope() { if (active) record(label, start, ticks()); }
};
} }
