#pragma once
#include "debug/prepared_front.hpp"
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp"

namespace FrontRuntime {
inline bool active=false;
inline unsigned liveParts=0,peakParts=0,liveOwners=0;
constexpr unsigned PartBudget=8192;
using BaseOwner=PreparedFront::Owner<Tyra::StaPipBagPackagesBBox,Tyra::Vec4>;
class Owner {
  BaseOwner facts;
  unsigned parts=0;
 public:
  ~Owner(){liveParts-=parts;if(parts)--liveOwners;}
  bool compile(const PreparedFront::Key& k){
    if(!PreparedFront::valid(k))return false;
    const unsigned next=(k.count+k.capacity/3-1)/(k.capacity/3);
    // Include the still-live old bounds in replacement's peak reservation.
    if(next>PartBudget-liveParts)return false;
    if(!facts.compile(k))return false;
    peakParts=std::max(peakParts,liveParts+next);
    liveParts=liveParts-parts+next;if(!parts)++liveOwners;parts=next;return true;
  }
  const PreparedFront::Key& key()const{return facts.key();}
  Tyra::StaPipBagPackagesBBox* admit(const PreparedFront::Key& k)const{return facts.admit(k);}
};
struct Attachment {
  std::unique_ptr<Owner> value;
  Attachment()=default;
  Attachment(const Attachment&){}
  Attachment& operator=(const Attachment& b){if(this!=&b)value.reset();return *this;}
  Attachment(Attachment&&)=default;
  Attachment& operator=(Attachment&&)=default;
};
inline unsigned sources=0,draws=0,parts=0,sourceHash=2166136261U;
inline unsigned trialDraws=0,trialParts=0;
inline void word(unsigned v){sourceHash^=v;sourceHash*=16777619U;}
inline void reset(){sources=draws=parts=0;sourceHash=2166136261U;}
}
