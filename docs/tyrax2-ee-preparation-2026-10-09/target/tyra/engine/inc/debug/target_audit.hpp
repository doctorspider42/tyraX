#pragma once
#include "debug/target_reject.hpp"
namespace TargetAudit {
inline bool enabled=false;
inline float size=0;
inline unsigned tested=0,rejected=0;
inline bool active() { return enabled; }
}
