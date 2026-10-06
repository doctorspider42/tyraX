#pragma once
#include "debug/closeout.hpp"
#include <vector>
namespace BatchTrial {
inline std::vector<short> originalMembership;
inline unsigned appliedPhase=~0U;
inline bool on=true;
inline bool desired(unsigned frame){unsigned p=frame/1800;if(p>2)p=2;return (p!=1)!=(Closeout::order==1);}
}
