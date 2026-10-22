/* Modified by TyraX: PRIVATE native replay validator, depth exactly one.
 * Stock source validator/arena CALL rejection remains unchanged. */
#pragma once
#include "vif1_chain_check.hpp"
namespace Tyra { namespace NativeCallCheck {
using TargetResolver=const uint32_t*(*)(uint32_t,uint32_t&,void*);
struct Walk {
  Vif1ChainCheck::Stream stream;
  Vif1ChainCheck::Resolver payload;
  TargetResolver target;
  void* context;
  uint32_t payloadQwords=0;
  bool walk(const uint32_t* words,uint32_t qwords,bool child) {
    auto& r=stream.result;
    if(!words || !qwords || qwords>65536 || (reinterpret_cast<uintptr_t>(words)&15)){
      r.error=Vif1ChainCheck::Error::Reference;return false;
    }
    for(uint32_t at=0;at<qwords;) {
      if(++r.tags>65536){r.error=Vif1ChainCheck::Error::TooLarge;return false;}
      r.tag=at;const uint32_t* t=words+at*4;
      const uint32_t id=(t[0]>>28)&7,size=t[0]&65535;
      if(t[0]&0x8c000000u){r.error=Vif1ChainCheck::Error::UnsupportedControl;return false;}
      if(id==5) {
        uint32_t n=0;
        if(child || size || t[2] || t[3] || (t[1]&0x8000000fu) || stream.result.pending || !target){r.error=Vif1ChainCheck::Error::UnsupportedTag;return false;}
        const uint32_t* sub=target(t[1],n,context);
        if(!walk(sub,n,true))return false;
        ++at;continue;
      }
      if(id==6) {
        if(!child || size || t[1] || t[2] || t[3] || at+1!=qwords || r.pending){r.error=Vif1ChainCheck::Error::UnsupportedTag;return false;}
        return true;
      }
      if(child && id!=1 && id!=3){r.error=Vif1ChainCheck::Error::UnsupportedTag;return false;}
      if(size>1048576-payloadQwords){r.error=Vif1ChainCheck::Error::PayloadBudget;return false;}
      payloadQwords+=size;const uint32_t* data=nullptr;bool end=false;
      if(id==1 || id==7) {
        if(size>qwords-at-1){r.error=Vif1ChainCheck::Error::InlineBounds;return false;}
        data=t+4;at+=size+1;end=id==7;
      } else if(id==0 || id==3) {
        ++r.refs;
        if(size && ((t[1]&0x8000000fu) || !payload || !(data=payload(t[1],size,context)))){r.error=Vif1ChainCheck::Error::Reference;return false;}
        ++at;end=id==0;
      } else {r.error=Vif1ChainCheck::Error::UnsupportedTag;return false;}
      stream.word(t[2]);stream.word(t[3]);
      for(uint32_t i=0;i<size*4 && r;++i)stream.word(data[i]);
      if(!r)return false;
      if(end) {
        if(r.pending)r.error=Vif1ChainCheck::Error::TruncatedPayload;
        else if(at!=qwords)r.error=Vif1ChainCheck::Error::TrailingData;
        return static_cast<bool>(r);
      }
    }
    r.error=Vif1ChainCheck::Error::MissingEnd;return false;
  }
};
inline Vif1ChainCheck::Result validate(const void* base,uint32_t qwords,
    Vif1ChainCheck::Resolver payload,TargetResolver target,void* context=nullptr){
  Walk w;w.payload=payload;w.target=target;w.context=context;
  w.walk(static_cast<const uint32_t*>(base),qwords,false);return w.stream.result;
}
} }
