#pragma once
// Private A18 owned command qualification; never a production cache.
#include "renderer/core/paths/path1/frame_chain_arena.hpp"
#include "debug/debug.hpp"
#include <vif_registers.h>
#include <malloc.h>
#include <cstdio>
namespace CommandTape {
using namespace Tyra;
using U=uint32_t;
inline bool armed=false,valid=true;
inline U flushes=0,submissions=0,qwords=0,bytes=0,firstCall=~0u,mpg=0,calls=0;
inline void* memory=nullptr;
constexpr U capacity=4*1024*1024;
struct State {U stat,cycle,mode,base,ofst,tops,itop,itops,top,mask,row[4],col[4];};
inline State before{},after{};
alignas(16) inline U programBefore[4096],programAfter[4096],dataBefore[4096],dataAfter[4096];
inline const U* ram(U address,U qw,void*){
  if(address<0x1000 || address>=0x02000000 || qw>(0x02000000-address)/16)return nullptr;
  return reinterpret_cast<const U*>(uintptr_t(address));
}
inline const U* owned(U address,U qw,void*){
  U base=U(reinterpret_cast<uintptr_t>(memory));
  if(address<base || address-base>bytes || qw>(bytes-(address-base))/16)return nullptr;
  return reinterpret_cast<const U*>(uintptr_t(address));
}
inline bool snapshot(State& s,U* program,U* data){
  U vpu;asm volatile("cfc2 %0,$29":"=r"(vpu)::"memory");
  if((vpu&0x100) || (*reinterpret_cast<volatile U*>(0x10009000)&0x100) ||
      (*reinterpret_cast<volatile U*>(0x1000a000)&0x100))return false;
  s={U(VIF1_STAT),U(VIF1_CYCLE),U(VIF1_MODE),U(VIF1_BASE),U(VIF1_OFST),U(VIF1_TOPS),
     U(VIF1_ITOP),U(VIF1_ITOPS),U(VIF1_TOP),U(VIF1_MASK),
     {U(VIF1_R0),U(VIF1_R1),U(VIF1_R2),U(VIF1_R3)},
     {U(VIF1_C0),U(VIF1_C1),U(VIF1_C2),U(VIF1_C3)}};
  auto* p=reinterpret_cast<volatile const U*>(0x11008000);
  auto* d=reinterpret_cast<volatile const U*>(0x1100c000);
  for(U i=0;i<4096;++i){program[i]=p[i];data[i]=d[i];}
  return true;
}
inline void arm(){
  TYRA_ASSERT(!memory && !armed,"Command capture cannot be rearmed");
  memory=memalign(64,capacity);TYRA_ASSERT(memory,"Command capture allocation failed");
  armed=true;
}
inline void capture(const void* chain,U count){
  if(!armed)return;
  ++flushes;
  if(flushes!=1 || !snapshot(before,programBefore,dataBefore)){valid=false;return;}
  if(!Vif1ChainCheck::validate(chain,count,ram)){valid=false;return;}
  FrameChainArena arena(memory,capacity);
  auto copy=arena.copy(chain,count,ram,nullptr,U(reinterpret_cast<uintptr_t>(memory)));
  if(!copy.chain || copy.borrowedBytes){valid=false;return;}
  qwords=count;bytes=copy.bytes;
  if(!Vif1ChainCheck::validate(memory,qwords,owned)){valid=false;return;}
  const U* original=static_cast<const U*>(chain);
  const U* captured=static_cast<const U*>(memory);
  Vif1ChainCheck::Stream stream;
  auto word=[&](U x){
    if(!stream.result.pending){U op=(x>>24)&127;
      if(op==0x4a)++mpg;
      if(op==0x14 || op==0x15 || op==0x17){if(!calls)firstCall=x;++calls;}}
    stream.word(x);
  };
  for(U at=0;at<qwords;){
    const U* a=original+at*4;const U* b=captured+at*4;
    U id=(a[0]>>28)&7,n=a[0]&65535;
    if(a[0]!=b[0] || a[2]!=b[2] || a[3]!=b[3] || (id!=3 && a[1]!=b[1])){valid=false;return;}
    const U* src=id==3?ram(a[1],n,nullptr):a+4;
    const U* dst=id==3?owned(b[1],n,nullptr):b+4;
    if(n && (!src || !dst || memcmp(src,dst,n*16))){valid=false;return;}
    word(b[2]);word(b[3]);for(U i=0;i<n*4;++i)word(dst[i]);
    if(id!=1 && id!=3 && id!=7){valid=false;return;}
    at+=id==3?1:1+n;
    if(id==7){if(at!=qwords)valid=false;break;}
  }
  valid=valid && bool(stream.result) && !stream.result.pending;
}
inline void binary(const char* path,const void* p,U n){
  FILE* f=fopen(path,"wb");TYRA_ASSERT(f,"Command dump open failed");
  TYRA_ASSERT(fwrite(p,1,n,f)==n,"Command dump write failed");
  TYRA_ASSERT(fclose(f)==0,"Command dump close failed");
}
inline void finish(){
  armed=false;
  valid=valid && flushes==1 && submissions==1 && snapshot(after,programAfter,dataAfter);
  printf("COMMANDCAP valid=%u flushes=%u submissions=%u qwords=%u bytes=%u mpg=%u calls=%u first=%08x programEqual=%u base=%u ofst=%u tops=%u top=%u dbf=%u cycle=%u mode=%u\n",
    U(valid),flushes,submissions,qwords,bytes,mpg,calls,firstCall,U(!memcmp(programBefore,programAfter,16384)),
    before.base,before.ofst,before.tops,before.top,(before.stat>>7)&1,before.cycle,before.mode);
  fflush(stdout);
  if(valid){binary("host:command-closure.bin",memory,bytes);
    binary("host:command-program-before.bin",programBefore,16384);binary("host:command-program-after.bin",programAfter,16384);
    binary("host:command-data-before.bin",dataBefore,16384);binary("host:command-data-after.bin",dataAfter,16384);
    binary("host:command-state-before.bin",&before,sizeof(before));binary("host:command-state-after.bin",&after,sizeof(after));}
  TYRA_ASSERT(valid,"Unsupported captured command frame");
}
}
