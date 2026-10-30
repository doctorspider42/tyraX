// Private EE critical-path attribution; no file I/O before the phase fence.
#pragma once
#include <cstdio>
namespace Vehicle_playground { namespace MicroCost {
enum Kind {Corners,Prepare,Pack,Upload,Finish,Consume,Reconstruct,Emit,Count};
inline const char* names[]={"corners","prepare","pack","upload_start","finish_readback","consume","reconstruct","emit"};
struct Bucket {unsigned long long ticks=0;unsigned calls=0;};
inline Bucket buckets[Count];
inline bool enabled=false;
inline unsigned begin(){return enabled?CB::ticks():0;}
inline void end(Kind k,unsigned start){if(enabled){buckets[k].ticks+=unsigned(CB::ticks()-start);++buckets[k].calls;}}
struct Scope {Kind kind;unsigned start;Scope(Kind k):kind(k),start(begin()){}~Scope(){end(kind,start);}};
template<class F>auto call(Kind kind,F&& f)->decltype(f()){Scope scope(kind);return f();}
inline bool pack(Tyra::Vu0SatJob& job,const Tyra::ConvexView& a,const Tyra::ConvexView& b,
    const Tyra::CollisionObb& aa,const Tyra::CollisionObb& bb,Tyra::CollisionV3 ma,Tyra::CollisionV3 mb){
  return call(Pack,[&]{return Tyra::packVu0Sat(job,a,b,aa,bb,ma,mb);});
}
inline bool finish(const Tyra::Vu0SatResult& r,const Tyra::Vu0SatJob& job,
    const Tyra::ConvexView& a,const Tyra::ConvexView& b,Tyra::CollisionV3 ma,Tyra::CollisionV3 mb,Tyra::ConvexContact& c){
  return call(Reconstruct,[&]{return Tyra::finishVu0Sat(r,job,a,b,ma,mb,c);});
}
inline void configure(){FILE* f=fopen("micro-cost.cfg","r");enabled=f!=nullptr;if(f)fclose(f);printf("MICROCOST mode=%s\n",enabled?"profile":"control");fflush(stdout);}
inline void flush(int phase){
  char path[80];snprintf(path,sizeof(path),"micro-cost-%d.csv",phase);FILE* f=fopen(path,"w");
  if(f){fprintf(f,"phase,enabled,component,calls,ticks\n");for(int k=0;k<Count;++k)fprintf(f,"%d,%d,%s,%u,%llu\n",phase,int(enabled),names[k],buckets[k].calls,buckets[k].ticks);fclose(f);}
  printf("MICROCOST phase=%d enabled=%d file=%d\n",phase,int(enabled),f!=nullptr);fflush(stdout);
  for(auto& b:buckets)b={};
}
} }
