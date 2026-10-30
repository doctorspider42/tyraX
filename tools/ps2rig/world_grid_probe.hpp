// Private admission oracle for an index excluding rigid bodies from world tests.
#pragma once
#include "physics/broad_phase.hpp"
#include <algorithm>
#include <cstdio>
namespace WorldGridProbe {
inline unsigned queries=0,verified=0,errors=0;
template<class Cache,class Objects>
inline void inspect(const Tyra::BroadBox& area,const std::vector<int>& out,
                    const Cache& cache,const Objects& objects,bool verify){
  ++queries;if(!verify)return;++verified;
  std::vector<int> expected,actual;
  auto overlap=[&](const Tyra::BroadBox& b){
    for(int k=0;k<3;++k){if(b.mn[k]>area.mx[k] || b.mx[k]<area.mn[k])return false;}
    return true;
  };
  for(size_t i=0;i<cache.size();++i)if(cache[i].present && !objects[i].data.physics && overlap(cache[i].box))expected.push_back(int(i));
  bool valid=std::is_sorted(out.begin(),out.end()) && std::adjacent_find(out.begin(),out.end())==out.end();
  for(int i:out){if(i<0 || size_t(i)>=cache.size() || !cache[size_t(i)].present || objects[size_t(i)].data.physics){valid=false;continue;}
    if(overlap(cache[size_t(i)].box))actual.push_back(i);}
  if(!valid || actual!=expected){if(errors++<4)printf("WORLDGRIDDIFF expected=%u actual=%u\n",unsigned(expected.size()),unsigned(actual.size()));}
}
inline void flush(int phase){
  char name[64];snprintf(name,sizeof(name),"world-grid-%d.csv",phase);
  if(FILE* f=fopen(name,"wb")){fprintf(f,"phase,queries,verified,errors\n%d,%u,%u,%u\n",phase,queries,verified,errors);fclose(f);}
  printf("WORLDGRID phase=%d queries=%u verified=%u errors=%u\n",phase,queries,verified,errors);fflush(stdout);
  queries=verified=errors=0;
}
}
