// Private sampled EE attribution. Buckets nest; never sum inclusive intervals.
#pragma once
#include <cstdio>
#include <vector>
namespace Vehicle_playground { namespace PipelineCost {
enum Kind {Empty,Corners,Prepare,LegacyPack,SharedPack,Append,Upload,Finish,Consume,Reconstruct,Emit,SolverPrepare,SolverWarm,Velocity,Normal,Friction,Position,History,Count};
inline const char* names[]={"empty_scope","corners","prepare_hint","legacy_pack","shared_body_pack","pair_append","upload_start","finish_readback","consume","reconstruct","emit","solver_prepare","solver_warm","velocity_solve","normal_impulse","friction","position_solve","solver_history"};
struct Bucket {unsigned long long ticks=0;unsigned calls=0;};
struct Row {int frame;Bucket buckets[Count];};
inline Bucket buckets[Count];inline std::vector<Row> rows;
inline bool enabled=false,active=false;
inline unsigned begin(){return active?CB::ticks():0;}
inline void end(Kind kind,unsigned start){if(active){buckets[kind].ticks+=unsigned(CB::ticks()-start);++buckets[kind].calls;}}
struct Scope {Kind kind;unsigned start;Scope(Kind k):kind(k),start(begin()){}~Scope(){end(kind,start);}};
template<class F>auto call(Kind k,F&& f)->decltype(f()){Scope scope(k);return f();}
struct Frame {
  Frame(){active=enabled&&CB::frame>=0&&CB::frame<CB::total&&CB::frame%16==0;
    if(active){for(auto& b:buckets)b={};for(int i=0;i<64;++i){Scope empty(Empty);}}}
  ~Frame(){if(active){Row row;row.frame=CB::frame;for(int i=0;i<Count;++i)row.buckets[i]=buckets[i];rows.push_back(row);}active=false;}
};
inline void configure(){FILE* f=fopen("pipeline-cost.cfg","r");enabled=f!=nullptr;if(f)fclose(f);rows.reserve(48);printf("PIPELINECOST enabled=%d stride=16\n",enabled);fflush(stdout);}
inline void flush(int phase){
  char name[64];snprintf(name,sizeof(name),"pipeline-cost-%d.csv",phase);FILE* f=fopen(name,"w");
  if(f){fprintf(f,"phase,frame,component,calls,ticks\n");for(const auto& row:rows)for(int i=0;i<Count;++i)fprintf(f,"%d,%d,%s,%u,%llu\n",phase,row.frame,names[i],row.buckets[i].calls,row.buckets[i].ticks);fclose(f);}
  printf("PIPELINECOST phase=%d sampled_frames=%u enabled=%d file=%d\n",phase,unsigned(rows.size()),enabled,f!=nullptr);fflush(stdout);rows.clear();
}
} }
