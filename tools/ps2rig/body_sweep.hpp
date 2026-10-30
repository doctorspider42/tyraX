// Private body-index experiment: linear storage, exact inclusive admission.
#pragma once
#include "physics/broad_phase.hpp"
#include <algorithm>
#include <limits>
namespace Tyra {
class BodySweep {
  //32 bytes per sorted row: bounds, original body index, prefix maximum.
  struct Entry {BroadBox box;int index;float prefix;};
  static_assert(sizeof(Entry)==32,"compact sorted query rows");
  std::vector<BroadBox> boxes_;
  std::vector<unsigned char> present_;
  std::vector<Entry> sorted_;
  std::vector<int> always_;
  int axis_=0;
  static bool valid(const BroadBox& b){
    for(int k=0;k<3;++k)if(!std::isfinite(b.mn[k]) || !std::isfinite(b.mx[k]) || b.mn[k]>b.mx[k])return false;
    return true;
  }
 public:
  void clear(){boxes_.clear();present_.clear();sorted_.clear();always_.clear();axis_=0;}
  void begin(int count){if(count<0)count=0;boxes_.resize(size_t(count));present_.assign(size_t(count),0);}
  void set(int index,const BroadBox& b,bool=false){
    if(index<0 || size_t(index)>=boxes_.size())return;
    boxes_[size_t(index)]=b;present_[size_t(index)]=1;
  }
  void commit(){
    sorted_.clear();always_.clear();axis_=0;
    float lo[3]={1e30F,1e30F,1e30F},hi[3]={-1e30F,-1e30F,-1e30F};
    for(size_t i=0;i<boxes_.size();++i)if(present_[i]){
      const auto& b=boxes_[i];
      if(!valid(b)){always_.push_back(int(i));continue;}
      sorted_.push_back({b,int(i),0});
      for(int k=0;k<3;++k){const float c=b.mn[k]*.5F+b.mx[k]*.5F;lo[k]=std::min(lo[k],c);hi[k]=std::max(hi[k],c);}
    }
    for(int k=1;k<3;++k)if(hi[k]*.5F-lo[k]*.5F>hi[axis_]*.5F-lo[axis_]*.5F)axis_=k;
    const int axis=axis_;
    std::sort(sorted_.begin(),sorted_.end(),[axis](const Entry& a,const Entry& b){
      if(a.box.mn[axis]!=b.box.mn[axis])return a.box.mn[axis]<b.box.mn[axis];
      return a.index<b.index;
    });
    float prefix=std::numeric_limits<float>::lowest();
    for(auto& e:sorted_){prefix=std::max(prefix,e.box.mx[axis]);e.prefix=prefix;}
  }
  void queryBox(const BroadBox& area,std::vector<int>& out)const{
    out.clear();
    if(!valid(area)){for(size_t i=0;i<present_.size();++i)if(present_[i])out.push_back(int(i));return;}
    out.insert(out.end(),always_.begin(),always_.end());
    const int axis=axis_;
    auto first=std::lower_bound(sorted_.begin(),sorted_.end(),area.mn[axis],[](const Entry& e,float x){return e.prefix<x;});
    auto last=std::upper_bound(sorted_.begin(),sorted_.end(),area.mx[axis],[axis](float x,const Entry& e){return x<e.box.mn[axis];});
    // Prefix maxima can only retain extra earlier rows, never skip an overlap.
    for(auto i=first;i<last;++i){const auto& b=i->box;bool hit=true;
      for(int k=0;k<3;++k)if(b.mn[k]>area.mx[k] || b.mx[k]<area.mn[k]){hit=false;break;}
      if(hit)out.push_back(i->index);
    }
    std::sort(out.begin(),out.end());
  }
};
} // namespace Tyra
