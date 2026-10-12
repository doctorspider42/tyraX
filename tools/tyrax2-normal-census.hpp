#pragma once
#include <stdio.h>
#include <stdint.h>
namespace NormalCensus {
struct Counts {
  unsigned calls=0,hits=0,misses=0,conflicts=0,changed=0,cold=0;
  unsigned repeat=0,vertices=0,roots=0,outside=0,outsideVertices=0,overflow=0;
};
struct Seen { const void* ptr=nullptr; unsigned count=0,bbox=0,version=0; };
inline Counts totals[3];
inline Seen seen[512];
inline unsigned used=0,bucket=0;
inline bool missed=false;
inline unsigned currentCount=0;
inline void beginBag() { missed=false;currentCount=0; }
inline void observe(const void* ptr,unsigned count,unsigned bbox,unsigned version,
                    const void* old,unsigned oldCount,unsigned oldBox,unsigned oldVersion) {
  auto& c=totals[bucket];++c.calls;
  const bool hit=ptr==old && count==oldCount && bbox==oldBox && version==oldVersion;
  if(hit){++c.hits;return;}
  ++c.misses;missed=true;currentCount=count;c.vertices+=count;
  if(!old)++c.cold;else if(old!=ptr)++c.conflicts;else ++c.changed;
  unsigned i=0;
  while(i<used && seen[i].ptr!=ptr)++i;
  if(i<used && seen[i].count==count && seen[i].bbox==bbox && seen[i].version==version)
    ++c.repeat;
  if(i==512){++c.overflow;return;}
  if(i==used)++used;
  seen[i]={ptr,count,bbox,version};
}
inline void root(){++totals[bucket].roots;}
inline void outside(){if(missed){++totals[bucket].outside;totals[bucket].outsideVertices+=currentCount;}}
inline void dump(unsigned phase){
  for(unsigned b=0;b<3;++b){const auto& c=totals[b];
    printf("NORMALCENSUS phase=%u bucket=%u calls=%u hits=%u misses=%u conflicts=%u changed=%u cold=%u repeat=%u vertices=%u roots=%u outside=%u outsideVertices=%u overflow=%u\n",
      phase,b,c.calls,c.hits,c.misses,c.conflicts,c.changed,c.cold,c.repeat,c.vertices,c.roots,c.outside,c.outsideVertices,c.overflow);
    totals[b]={};
  }
  used=0;bucket=0;beginBag();
}
}
