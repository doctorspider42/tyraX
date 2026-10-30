// Private body-index oracle. No I/O occurs inside the collision query interval.
#pragma once
#include "debug/body_sweep.hpp"
namespace Vehicle_playground { namespace BodySweepProbe {
inline bool capture=false;
inline unsigned queries=0,errors=0;
inline std::vector<Tyra::BroadBox> boxes;
inline std::vector<int> expected;
struct Query {Tyra::BroadBox area;std::vector<int> hits;};
struct Snapshot {int frame;std::vector<Tyra::BroadBox> boxes;std::vector<Query> queries;};
inline std::vector<Snapshot> snapshots;
inline int current=-1;
inline bool valid(const Tyra::BroadBox& b){
  for(int k=0;k<3;++k)if(!std::isfinite(b.mn[k])||!std::isfinite(b.mx[k])||b.mn[k]>b.mx[k])return false;
  return true;
}
inline void begin(int count,bool selected){
  capture=selected&&Tyra::vu0SatVerify;current=-1;
  if(!capture)return;
  boxes.resize(size_t(count));
  if(CB::frame==300||CB::frame==600){snapshots.push_back({CB::frame,{},{}});current=int(snapshots.size())-1;snapshots.back().boxes.resize(size_t(count));}
}
inline void set(int i,const Tyra::BroadBox& box){
  if(!capture)return;
  boxes[size_t(i)]=box;if(current>=0)snapshots[size_t(current)].boxes[size_t(i)]=box;
}
inline void inspect(const Tyra::BroadBox& area,const std::vector<int>& out){
  if(!capture)return;
  ++queries;expected.clear();const bool invalidQuery=!valid(area);
  for(size_t i=0;i<boxes.size();++i){const auto& b=boxes[i];bool hit=true;
    if(!invalidQuery&&valid(b))for(int k=0;k<3;++k)if(b.mn[k]>area.mx[k]||b.mx[k]<area.mn[k]){hit=false;break;}
    if(hit)expected.push_back(int(i));
  }
  if(out!=expected && errors++<4)printf("BODYSWEEPDIFF frame=%d expected=%u actual=%u\n",CB::frame,unsigned(expected.size()),unsigned(out.size()));
  if(current>=0)snapshots[size_t(current)].queries.push_back({area,out});
}
inline void flush(int phase){
  char name[96];snprintf(name,sizeof(name),"body-sweep-%d.csv",phase);FILE* f=fopen(name,"wb");if(!f)++errors;
  if(f){fprintf(f,"phase,queries,errors,snapshots\n%d,%u,%u,%u\n",phase,queries,errors,unsigned(snapshots.size()));fclose(f);}
  for(const auto& s:snapshots){snprintf(name,sizeof(name),"body-sweep-%d-%d.bin",phase,s.frame);FILE* b=fopen(name,"wb");if(!b){++errors;continue;}
    const unsigned h[4]={0x57535954u,1u,unsigned(s.boxes.size()),unsigned(s.queries.size())};fwrite(h,sizeof(h),1,b);fwrite(s.boxes.data(),sizeof(Tyra::BroadBox),s.boxes.size(),b);
    for(const auto& q:s.queries){const unsigned n=q.hits.size();fwrite(&q.area,sizeof(q.area),1,b);fwrite(&n,4,1,b);fwrite(q.hits.data(),4,n,b);}fclose(b);
  }
  printf("BODYSWEEP phase=%d queries=%u errors=%u snapshots=%u file=%d\n",phase,queries,errors,unsigned(snapshots.size()),f!=nullptr);fflush(stdout);
  queries=errors=0;snapshots.clear();current=-1;
}
class Index {
  Tyra::BroadPhase grid_;
  Tyra::BodySweep sweep_;
  bool selected_=false;int phase_=-1;
 public:
  void begin(int count){
    selected_=!CB::baseline;
    if(phase_!=CB::phase){grid_=Tyra::BroadPhase(Tyra::BroadPhase::kGrid,8.0F);sweep_.clear();phase_=CB::phase;}
    if(selected_)sweep_.begin(count);else grid_.begin(count);
    BodySweepProbe::begin(count,selected_);
  }
  void set(int i,const Tyra::BroadBox& box,bool moving){if(selected_)sweep_.set(i,box,moving);else grid_.set(i,box,moving);BodySweepProbe::set(i,box);}
  void commit(){if(selected_)sweep_.commit();else grid_.commit();}
  void queryBox(const Tyra::BroadBox& box,std::vector<int>& out){if(selected_)sweep_.queryBox(box,out);else grid_.queryBox(box,out);BodySweepProbe::inspect(box,out);}
};
} } // Vehicle_playground::BodySweepProbe
