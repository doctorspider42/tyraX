// Private zero-radius Coulomb work counter; output only after the phase fence.
#pragma once
#include <cstdio>
namespace Vehicle_playground { namespace ZeroFrictionProbe {
inline unsigned frames=0;inline unsigned long long visits=0,skipped=0;inline bool enabled=false;
inline void record(bool mode,unsigned count,unsigned saved){
  if(!CB::ready||CB::finished)return;
  enabled=mode;++frames;visits+=count;skipped+=saved;
}
inline void flush(int phase){
  char name[64];snprintf(name,sizeof(name),"zero-friction-%d.csv",phase);FILE* f=fopen(name,"w");
  if(f){fprintf(f,"phase,frames,enabled,visits,skipped\n%d,%u,%d,%llu,%llu\n",phase,frames,int(enabled),visits,skipped);fclose(f);}
  printf("ZEROFRICTION phase=%d enabled=%d visits=%llu skipped=%llu file=%d\n",phase,int(enabled),visits,skipped,f!=nullptr);fflush(stdout);
  frames=0;visits=0;skipped=0;
}
} }
