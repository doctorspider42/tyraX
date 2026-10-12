#pragma once
#include "debug/world_input_compact.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include "debug/debug.hpp"
#include <packet2_utils.h>
#include <dma.h>
#include <vif_registers.h>
#include <cstdio>
#include <cstring>

namespace WorldInputPolicy {
inline bool enabled=false,initialized=false;
inline unsigned cells=0,compacted=0,fallback=0,oldWords=0,newWords=0;
inline void reset(){cells=compacted=fallback=oldWords=newWords=0;}
inline void witness(unsigned phase,int arm=-1){
  std::printf("INPUTFORMAT phase=%u arm=%d enabled=%u cells=%u compacted=%u fallback=%u originalWords=%u packedWords=%u\n",
              phase,arm,unsigned(enabled),cells,compacted,fallback,oldWords,newWords);std::fflush(stdout);
}
inline void initialize(){
  TYRA_ASSERT(!initialized && !Tyra::Vif1Queue::recordingFrame(),"Input policy invalid initialization");
  Tyra::Vif1Queue::synchronizeExternal();Tyra::Vif1Queue::drain();
  TYRA_ASSERT(VIF1_MODE==0,"Input policy requires original STMOD zero");
  auto* p=packet2_create(8,P2_TYPE_NORMAL,P2_MODE_CHAIN,true);
  TYRA_ASSERT(p,"Input policy init packet");
  packet2_chain_open_end(p,0,0);packet2_vif_nop(p,0);packet2_vif_nop(p,0);
  for(auto word:std::vector<WorldInputCompact::Word>{0x10000000,0,0,0,
      0x30000000,0,0,0,0,0x20000000,0,0})packet2_add_u32(p,word);
  packet2_chain_close_tag(p);dma_channel_send_packet2(p,DMA_CHANNEL_VIF1,true);
  dma_channel_wait(DMA_CHANNEL_VIF1,0);
  while(VIF1_STAT&0x1f000007){}
  TYRA_ASSERT(VIF1_MODE==0 && VIF1_MASK==0 && VIF1_R0==0 && VIF1_R1==0 &&
              VIF1_R2==0 && VIF1_R3==0,"Input policy zero prestate initialization");
  packet2_free(p);initialized=true;
  std::printf("INPUTINIT mode=0 mask=0 row=0 common=1\n");std::fflush(stdout);
}
template<class Quad>
inline void compactRange(std::vector<Quad>& body,std::size_t start,unsigned streams){
  TYRA_ASSERT(initialized && start<body.size(),"Input policy invalid cold range");
  std::vector<WorldInputCompact::Word> source((body.size()-start)*4);
  std::memcpy(source.data(),body.data()+start,source.size()*4);
  const auto result=WorldInputCompact::compile(source,streams,{{0,0,0,0},0});
  compacted+=result.compacted;fallback+=result.fallback;
  oldWords+=source.size();newWords+=result.words.size();
  body.resize(start);
  for(unsigned at=0;at<result.words.size();at+=4)
    body.push_back(Quad{{result.words[at],result.words[at+1],result.words[at+2],result.words[at+3]}});
}
template<class Quad>
inline bool equivalent(const std::vector<Quad>& encoded,const std::vector<Quad>& original){
  if(!enabled)return encoded==original;
  std::vector<WorldInputCompact::Word> words(encoded.size()*4);
  std::memcpy(words.data(),encoded.data(),words.size()*4);
  const auto raw=WorldInputCompact::canonicalize(words);
  return raw.size()==original.size()*4 && !std::memcmp(raw.data(),original.data(),raw.size()*4);
}
}
