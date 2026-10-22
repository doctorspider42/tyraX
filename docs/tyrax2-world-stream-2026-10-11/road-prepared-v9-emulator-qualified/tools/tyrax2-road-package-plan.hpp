#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

// Private cold package plan, not a DMA owner or a replacement shader.
namespace RoadPackagePlan {
enum class Route { Outside, Cull, Guard, Clip };
struct Range { unsigned first, count, parity; };
template<class Vertex> struct Box { Vertex low, high; };
template<class Vertex> struct Job {
  unsigned first, count, firstPart, parts;
  Box<Vertex> box;
  std::vector<Range> clips;
};
template<class Vertex> struct Plan {
  unsigned count=0, capacity=0, run=0;
  std::vector<Job<Vertex>> jobs;
  std::vector<Box<Vertex>> groups;
};

// The original packager's MERGED one-third bounds, not tighter source bounds.
template<class Vertex,class Bounds>
Plan<Vertex> compile(Bounds& bounds,unsigned count,unsigned capacity,unsigned run) {
  if (!count || count>65535 || count%3 || capacity<3 || capacity>999 ||
      capacity%3 || run<3 || run>capacity || run%3 ||
      (count%run && count%run<3) || bounds.getVertexCount()!=count ||
      bounds.getPartsCount()!=(count+capacity/3-1)/(capacity/3))
    throw std::invalid_argument("road prepared package shape");
  Plan<Vertex> plan;plan.count=count;plan.capacity=capacity;plan.run=run;
  for(unsigned at=0;at<count;) {
    const unsigned size=std::min(run,count-at), firstPart=at/(capacity/3);
    const unsigned end=(at+size-1)/(capacity/3);
    const unsigned parts=size<=capacity/3 ? end-firstPart+1 :
                         (size+capacity/3-1)/(capacity/3);
    Job<Vertex> job{at,size,firstPart,parts,{}, {}};
    bounds.getMergedMinMax(firstPart,parts,&job.box.low,&job.box.high);
    const unsigned step=std::min(72U,capacity)-2;
    for(unsigned triangle=0;triangle<size-2;) {
      const unsigned n=std::min(step,size-2-triangle);
      job.clips.push_back({at+triangle,n+2,triangle&1});triangle+=n;
    }
    plan.jobs.push_back(std::move(job));at+=size;
  }
  if(run==capacity) {
    for(unsigned i=0;i<plan.jobs.size();i+=8) {
      Box<Vertex> b;
      const unsigned start=(i/8)*24;
      bounds.getMergedMinMax(start,std::min<unsigned>(24U,bounds.getPartsCount()-start),
                             &b.low,&b.high);
      plan.groups.push_back(b);
    }
  }
  return plan;
}

// classify returns -1 outside, 0 partial, +1 inside using actual engine math.
// mask uses all EIGHT VU/near/far planes; only six bits enter the VU payload.
template<class Vertex,class Classify,class Mask,class Emit>
void select(const Plan<Vertex>& plan,Classify classify,Mask mask,Emit emit) {
  int coarse=0;
  for(unsigned i=0;i<plan.jobs.size();++i) {
    if(!plan.groups.empty() && !(i&7))coarse=classify(plan.groups[i/8]);
    const auto& job=plan.jobs[i];
    const int route=!plan.groups.empty() && coarse ? coarse : classify(job.box);
    const unsigned crossing=route==0 ? mask(job.box) : 0;
    emit(job,route<0 ? Route::Outside : route>0 ? Route::Cull :
             !crossing ? Route::Guard : Route::Clip,crossing&63U);
  }
}

// Skip does not change continuation. The caller resets this at the SAME bag
// boundary as the ordinary renderer, not at each REF or each flush.
struct Continuation {
  int last=-1;
  bool start(int program) {
    const bool changed=last!=program;last=program;return changed;
  }
};
template<class Vertex,class Emit>
void dispatch(const Job<Vertex>& job,Route route,unsigned mask,
              int cullProgram,int clipProgram,Continuation& continuation,Emit emit) {
  if(route==Route::Outside)return;
  if(route!=Route::Clip) {
    emit(Range{job.first,job.count,0},cullProgram,0U,continuation.start(cullProgram));
  } else {
    for(const auto& range:job.clips)
      emit(range,clipProgram,mask,continuation.start(clipProgram));
  }
}
}
