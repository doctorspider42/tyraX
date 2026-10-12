#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace ObjectHead {
struct alignas(16) Word {
  std::array<std::uint32_t,4> w{};
  bool operator==(const Word& b)const{return w==b.w;}
};
enum Shape : unsigned {Textured=1,Single=2,Spot=4,Clip=8};
struct Facts {
  std::uint64_t generation=0;
  std::uint32_t epoch=0,shape=0;
  std::array<std::uint32_t,32> fixed{};
  bool operator==(const Facts& b)const {
    return generation==b.generation && epoch==b.epoch && shape==b.shape && fixed==b.fixed;
  }
};
struct ClipReference {
  Word tag;
  std::uint32_t epoch=0;
};
struct Live {
  const Word* mvp=nullptr;
  const Word* spot=nullptr;
  const Word* color=nullptr;
  std::uint32_t fogScale=0,fogOffset=0;
  std::array<std::uint32_t,2> tex0{};
};

// Post-wrap fragment only. The caller retains FLUSHE, wrap and all DMA fences.
class Image {
  Facts facts_{};
  std::vector<Word> words_;
  unsigned mvp_=0,spot_=0,color_=0,options_=0;
  static bool disjoint(const Word* source,unsigned n,const Word* destination,unsigned m){
    const auto a=reinterpret_cast<std::uintptr_t>(source),b=reinterpret_cast<std::uintptr_t>(destination);
    const auto na=std::uintptr_t(n)*sizeof(Word),nb=std::uintptr_t(m)*sizeof(Word);
    if(a>std::numeric_limits<std::uintptr_t>::max()-na ||
       b>std::numeric_limits<std::uintptr_t>::max()-nb)return false;
    return a+na<=b || b+nb<=a;
  }
  static bool unpack(const Word* src,unsigned size,unsigned& at,unsigned address,
                     unsigned count,unsigned& payload){
    if(at>=size || size-at<1+count)return false;
    const auto& w=src[at].w;
    if(w[0]!=(0x10000000U|count) || w[1] || w[2]!=0x01000101U ||
       w[3]!=(0x6c000000U|(count<<16)|0x4000U|address))return false;
    payload=at+1;at+=1+count;return true;
  }
 public:
  bool compile(const Facts& facts,const Word* src,unsigned count,
               const ClipReference& reference={}){
    if(!src || !facts.generation || !facts.epoch || (facts.shape&~15U) ||
       facts.generation<=facts_.generation || !count || count>32)return false;
    unsigned at=0,mvp=0,spot=0,color=0,options=0,alpha=0;
    if(!unpack(src,count,at,0,4,mvp))return false;
    if((facts.shape&Spot) && !unpack(src,count,at,12,3,spot))return false;
    if(facts.shape&Clip){
      const auto& w=reference.tag.w;
      if(reference.epoch!=facts.epoch || at>=count || !(src[at]==reference.tag) ||
         ((w[0]>>28)&15U)!=3 || (w[0]&0x0fff0000U) || !(w[0]&0xffffU) ||
         (w[0]&0xffffU)>32 || !w[1] || (w[1]&0xf000000fU) || w[2] || w[3])return false;
      ++at;
    }
    if((facts.shape&Single) && !unpack(src,count,at,7,1,color))return false;
    const unsigned optCount=(facts.shape&Textured)?4:3;
    if(!unpack(src,count,at,8,optCount,options) || !unpack(src,count,at,21,1,alpha) || at!=count)return false;
    if(src[options].w[0]!=unsigned(bool(facts.shape&Single)) ||
       src[options].w[1]!=((facts.shape&Spot)?0xffffffffU:0U) ||
       src[options+1].w[2]!=0x14 || src[options+1].w[3] ||
       src[options+2].w[2]!=0x47 || src[options+2].w[3] ||
       ((facts.shape&Textured) && (src[options+3].w[2]!=6 || src[options+3].w[3])) ||
       src[alpha].w[2]!=0x42 || src[alpha].w[3])return false;
    // Publish only after independent storage has been allocated successfully.
    std::vector<Word> next(src,src+count);
    words_=std::move(next);facts_=facts;
    mvp_=mvp;spot_=spot;color_=color;options_=options;return true;
  }
  bool write(const Facts& facts,const Live& live,Word* destination,unsigned capacity)const {
    if(words_.empty() || !(facts_==facts) || !destination || capacity<words_.size() ||
       !live.mvp || ((facts.shape&Spot) && !live.spot) ||
       ((facts.shape&Single) && !live.color))return false;
    if(!disjoint(live.mvp,4,destination,size()) ||
       ((facts.shape&Spot) && !disjoint(live.spot,3,destination,size())) ||
       ((facts.shape&Single) && !disjoint(live.color,1,destination,size())))return false;
    std::copy(words_.begin(),words_.end(),destination);
    std::copy(live.mvp,live.mvp+4,destination+mvp_);
    if(facts.shape&Spot)std::copy(live.spot,live.spot+3,destination+spot_);
    if(facts.shape&Single)destination[color_]=*live.color;
    destination[options_].w[2]=live.fogScale;
    destination[options_].w[3]=live.fogOffset;
    if(facts.shape&Textured){
      destination[options_+3].w[0]=live.tex0[0];
      destination[options_+3].w[1]=live.tex0[1];
    }
    return true;
  }
  unsigned size()const{return unsigned(words_.size());}
};
}
