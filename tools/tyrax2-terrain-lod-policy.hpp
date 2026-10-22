#pragma once
#include "terrain_lod_key.hpp"
#include <cstdio>
namespace TerrainLodPolicy {
inline bool enabled=false,forcing=false,qualified=false;
inline TerrainLodKey::Key forced={1,1,1,1,1};
inline unsigned skipped=0,events=0;
inline void reset(){skipped=events=0;}
inline void witness(unsigned phase){
  printf("LODNOP phase=%u enabled=%u events=%u skipped=%u qualified=%u\n",
    phase,unsigned(enabled),events,skipped,unsigned(qualified));fflush(stdout);
}
}
