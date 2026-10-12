#include "debug/object_head.hpp"
#include <packet2_utils.h>
#include <cassert>
#include <cstdio>
using u32=std::uint32_t;
// Only the accessed pipeline fields are host shims; lod_t is the copied SDK type.
struct PipelineInfoBag {
  unsigned zTestType=0;
  bool dateLit=false;
  unsigned char additiveBlendFix=0,subtractiveBlendFix=0;
};
#include "actual-hot-facts.inc"
int main(){
  unsigned controls=0;
  for(unsigned shape=0;shape<16;++shape)for(unsigned trial=0;trial<64;++trial){
    lod_t lod{};PipelineInfoBag info;
    lod.calculation=trial&1;lod.max_level=trial&7;lod.mag_filter=trial&1;
    lod.min_filter=trial&7;lod.mipmap_select=trial&1;lod.l=trial&3;lod.k=float(trial)/16;
    info.zTestType=trial%3;info.dateLit=trial&1;info.additiveBlendFix=trial;
    info.subtractiveBlendFix=trial*2;
    ObjectHead::ClipReference ref;ref.epoch=7;ref.tag.w={0x3000000fU,0x10000,0,0};
    const auto f=hotHeadFacts(&lod,&info,7,shape,2,ref);
    auto require=[&](const ObjectHead::Facts& changed){assert(!(changed==f));++controls;};
    auto changedLod=lod;changedLod.calculation^=1;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    changedLod=lod;changedLod.max_level^=1;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    changedLod=lod;changedLod.mag_filter^=1;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    changedLod=lod;changedLod.min_filter^=1;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    changedLod=lod;changedLod.mipmap_select^=1;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    changedLod=lod;changedLod.l^=1;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    changedLod=lod;changedLod.k+=0.0625F;require(hotHeadFacts(&changedLod,&info,7,shape,2,ref));
    auto changedInfo=info;changedInfo.zTestType^=1;require(hotHeadFacts(&lod,&changedInfo,7,shape,2,ref));
    changedInfo=info;changedInfo.dateLit=!info.dateLit;require(hotHeadFacts(&lod,&changedInfo,7,shape,2,ref));
    changedInfo=info;changedInfo.additiveBlendFix^=1;require(hotHeadFacts(&lod,&changedInfo,7,shape,2,ref));
    changedInfo=info;changedInfo.subtractiveBlendFix^=1;require(hotHeadFacts(&lod,&changedInfo,7,shape,2,ref));
    require(hotHeadFacts(&lod,&info,8,shape,2,ref));
    require(hotHeadFacts(&lod,&info,7,shape^1,2,ref));
    require(hotHeadFacts(&lod,&info,7,shape,3,ref));
    if(shape&ObjectHead::Clip)for(unsigned word=0;word<4;++word){
      auto r=ref;r.tag.w[word]^=1;require(hotHeadFacts(&lod,&info,7,shape,2,r));
    }
  }
  printf("PASS actual semantic facts: %u fixed-state/epoch/shape/clip mutation controls; pipeline host shim, no native/FPS claim\n",controls);
}
