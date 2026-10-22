#pragma once
#include "world_input_compact.hpp"
#include "debug/debug.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
#include <dma.h>
#include <packet2_utils.h>
#include <vif_registers.h>
#include <kernel.h>
#include <cstdio>
#include <memory>

namespace WorldInputNative {
using WorldInputCompact::Word;
using WorldInputCompact::State;
inline unsigned comparisons=0, compacted=0, fallback=0;
inline Word ticks(){Word v;asm volatile("mfc0 %0,$9":"=r"(v)::"memory");return v;}
inline State state(){return State{{VIF1_R0,VIF1_R1,VIF1_R2,VIF1_R3},VIF1_MASK};}
inline bool equal(State a,State b){return a.row==b.row && a.mask==b.mask;}
inline void idle(){
  const Word start=ticks();
  while((*reinterpret_cast<volatile u32*>(0x10009000)&0x100) || (VIF1_STAT&0x1f000007))
    TYRA_ASSERT(Word(ticks()-start)<589824000u,"Input native DMA/VIF timeout");
}
inline void send(packet2_t* packet,const std::vector<Word>& words){
  TYRA_ASSERT(words.size()%4==0 && words.size()/4+2<1024,"Input native packet capacity");
  idle();packet2_reset(packet,true);packet2_chain_open_end(packet,0,0);
  packet2_vif_nop(packet,0);packet2_vif_nop(packet,0);
  for(const auto word:words)packet2_add_u32(packet,word);
  packet2_chain_close_tag(packet);
  dma_channel_send_packet2(packet,DMA_CHANNEL_VIF1,true);idle();
}
inline std::vector<std::vector<Word>> fragments(const std::vector<Word>& body){
  std::vector<std::vector<Word>> result;
  std::size_t first=0;
  for(std::size_t at=0;at<body.size();){
    const Word code=body[at++];const unsigned op=code>>24;
    if(!code || op==1 || op==0x11)continue;
    if(op==0x30){at+=4;continue;}
    if(op==0x20){++at;continue;}
    if(op==0x6c || op==0x78){at+=((code>>16)&255)*(op==0x6c?4:3);continue;}
    TYRA_ASSERT(op==0x14 || op==0x17,"Input native unexpected opcode");
    TYRA_ASSERT(at%4==0,"Input native kick alignment");
    result.emplace_back(body.begin()+first,body.begin()+at);
    result.back().back()=0;result.back()[result.back().size()-2]=0x10000000;
    first=at;
  }
  TYRA_ASSERT(first==body.size(),"Input native trailing commands");return result;
}
inline void run(){
  using Tyra::Vif1Queue;
  TYRA_ASSERT(!Vif1Queue::recordingFrame(),"Input native must run before frame recording");
  Vif1Queue::synchronizeExternal();Vif1Queue::drain();idle();
  std::unique_ptr<packet2_t,void(*)(packet2_t*)> packet(
    packet2_create(1024,P2_TYPE_NORMAL,P2_MODE_CHAIN,true),packet2_free);
  TYRA_ASSERT(packet,"Input native packet allocation");
  send(packet.get(),{0,0,0x10000000,0});
  TYRA_ASSERT(VIF1_MODE==0,"Input native requires STMOD zero");
  const State initial=state();const Word cycle=VIF1_CYCLE,tops=VIF1_TOPS;
  auto* vu=reinterpret_cast<volatile u32*>(0x1100c000);
  std::vector<Word> saved(4096),reference(4096);
  for(unsigned i=0;i<4096;++i)saved[i]=vu[i];
  FILE* file=std::fopen("host:world-input-sdk.bin","rb");
  TYRA_ASSERT(file,"Input native SDK input open");
  TYRA_ASSERT(std::fseek(file,0,SEEK_END)==0,"Input native input seek");
  const long bytes=std::ftell(file);
  TYRA_ASSERT(bytes==219424 && std::fseek(file,0,SEEK_SET)==0,"Input native dump length");
  std::vector<Word> dump(static_cast<unsigned>(bytes)/4);
  TYRA_ASSERT(std::fread(dump.data(),4,dump.size(),file)==dump.size(),"Input native dump read");
  TYRA_ASSERT(std::fclose(file)==0 && dump[0]==0x43534957 && dump[1]==1 && dump[2]==60,
              "Input native dump header");
  for(unsigned arm=0;arm<2;++arm){
    const State incoming=arm?State{{0xdeadbeef,0x80000000,0x7fc12345,0x12345678},0xabcdef01}:initial;
    std::vector<Word> setup;WorldInputCompact::rowMask(setup,incoming);
    setup.insert(setup.end(),{0,0,0x10000000,0});send(packet.get(),setup);
    TYRA_ASSERT(equal(state(),incoming),"Input native initial ROW/MASK setup");
    std::size_t at=9;
    for(unsigned record=0;record<60;++record){
      TYRA_ASSERT(at+6<=dump.size(),"Input native short record");
      const unsigned streams=dump[at+4]?3:2,length=dump[at+5];at+=6;
      TYRA_ASSERT(length && length%4==0 && length<=dump.size()-at,"Input native short body");
      const std::vector<Word> source(dump.begin()+at,dump.begin()+at+length);at+=length;
      const auto packed=WorldInputCompact::compile(source,streams,incoming);
      compacted+=packed.compacted;fallback+=packed.fallback;
      const auto ordinary=fragments(source),compact=fragments(packed.words);
      TYRA_ASSERT(ordinary.size()==compact.size(),"Input native partition mismatch");
      for(unsigned part=0;part<ordinary.size();++part){
        for(unsigned i=0;i<4096;++i)vu[i]=0xd00d0000u^i;
        asm volatile("sync":::"memory");send(packet.get(),ordinary[part]);
        for(unsigned i=0;i<4096;++i)reference[i]=vu[i];
        TYRA_ASSERT(equal(state(),incoming),"Input native ordinary ROW/MASK changed");
        for(unsigned i=0;i<4096;++i)vu[i]=0xd00d0000u^i;
        asm volatile("sync":::"memory");send(packet.get(),compact[part]);
        for(unsigned i=0;i<4096;++i)
          TYRA_ASSERT(reference[i]==vu[i],"Input native VU word mismatch");
        TYRA_ASSERT(equal(state(),incoming) && VIF1_MODE==0 && VIF1_TOPS==tops,
                    "Input native compact state mismatch");++comparisons;
      }
    }
    TYRA_ASSERT(dump.size()-at==7 && dump[at]==0x454e4957,"Input native trailing dump");
  }
  for(unsigned i=0;i<4096;++i)vu[i]=saved[i];
  asm volatile("sync":::"memory");
  std::vector<Word> restore;WorldInputCompact::rowMask(restore,initial);
  restore.insert(restore.end(),{0x01000000|cycle,0,0x10000000,0});send(packet.get(),restore);
  TYRA_ASSERT(equal(state(),initial) && VIF1_MODE==0 && VIF1_CYCLE==cycle && VIF1_TOPS==tops,
              "Input native final state mismatch");
  for(unsigned i=0;i<4096;++i)TYRA_ASSERT(vu[i]==saved[i],"Input native final VU restore mismatch");
  TYRA_ASSERT(comparisons==540 && compacted==150 && fallback==1230,"Input native activation counts");
  std::printf("VIFINPUTDONE comparisons=%u compacted=%u fallback=%u vuWords=4096 states=2 restored=1 shaderExecuted=0 componentOnly=1\n",
              comparisons,compacted,fallback);std::fflush(stdout);
}
}
