// Private A24: only the inspected textured many-colour, unlit clip ABI.
#pragma once
#include <algorithm>
namespace ClipTrial {
inline unsigned mode=0;
inline bool observe=false;
struct Witness {
  unsigned frames=0,clipCalls=0,eligible=0,merged=0,removed=0,maxInput=0,bypassBags=0;
};
inline Witness current{}, witnesses[10]{};
inline unsigned planeCount(unsigned mask) {
  mask &= 63;
  if (!mask) mask=63;
  unsigned count=0;
  for (; mask; mask>>=1) count += mask&1;
  return count;
}
inline unsigned capacity(unsigned mask,unsigned half,unsigned nominal,unsigned oldCap) {
  const unsigned fixed=2+9+91;
  if (half<=fixed) return oldCap;
  const unsigned perVertex=3+3*(planeCount(mask)+1);
  const unsigned bound=((half-fixed)/perVertex/3)*3;
  return std::max(oldCap,std::min(nominal,bound));
}
inline unsigned footprint(unsigned mask,unsigned vertices) {
  return 2+9+(3+3*(planeCount(mask)+1))*vertices;
}
}
