#pragma once
#include "debug/closeout.hpp"
#include <vector>
namespace BatchTrial {
inline constexpr bool member(short entry){return entry>=0||entry==-2;}
inline std::vector<short> originalMembership;
inline unsigned appliedPhase=~0U;
inline bool on=true,captured=false,soloPrepared=false;
inline unsigned soloParts=0,soloVertices=0;
inline bool desired(unsigned frame){unsigned p=frame/1800;if(p>2)p=2;return (p!=1)!=(Closeout::order==1);}
}
