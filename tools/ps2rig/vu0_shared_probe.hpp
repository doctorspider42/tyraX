// Private hardware admission of the shared-body image, before scene timings.
#pragma once
#include "vu0_shared_packet.hpp"
#include "vu0_sat_probe.hpp"
#include "vu0_sat_capture.hpp"
#ifdef _EE
#include "math/vu0_owner.hpp"
#include "vu0_shared_sony.hpp"
extern unsigned Vu0SharedSeparation_CodeStart,Vu0SharedSeparation_CodeEnd;
namespace Vu0SharedProbe {
using namespace Tyra;
inline void wait(){unsigned status;do{asm volatile("cfc2 %0,$29":"=r"(status)::"memory");}while(status&1);}
inline void copy(volatile void* destination,const volatile void* source,int qwords){
  auto dst=static_cast<volatile unsigned char*>(destination);auto src=static_cast<const volatile unsigned char*>(source);
  asm volatile("1:\n lq $8,0(%0)\n sq $8,0(%1)\n addiu %0,%0,16\n addiu %1,%1,16\n addiu %2,%2,-1\n bnez %2,1b\n nop\n"
    :"+r"(src),"+r"(dst),"+r"(qwords) :: "$8","memory");
}
inline bool run(){
  Vu0SatCapture::Dump saved;
  FILE* capture=fopen(FileUtils::fromCwd("vu0-sat-failure.bin").c_str(),"rb");
  const bool valid=capture&&fread(&saved,sizeof(saved),1,capture)==1&&fgetc(capture)==EOF&&Vu0SatCapture::valid(saved);
  if(capture)fclose(capture);
  if(!valid){printf("VUSHARED saved capture missing or invalid\n");return false;}
  for(int image=0;image<2;++image){
    wait();const unsigned* code=image?SharedSonyCode:&Vu0SharedSeparation_CodeStart;
    const unsigned words=image?unsigned(sizeof(SharedSonyCode)/4):unsigned(&Vu0SharedSeparation_CodeEnd-code);
    if(!words||words>1024)return false;
    auto micro=reinterpret_cast<volatile unsigned*>(0x11000000);
    for(unsigned i=0;i<words;++i)micro[i]=code[i];
    asm volatile("sync.l":::"memory");vu0MicroOwner=code;
    unsigned seed=0x53485244,hits=0,rejects=0,checked=0;
    for(int first=0;first<12000;first+=6){
      Vu0SatProbe::Case cases[6]{};Vu0SharedBody bodies[12];Vu0SharedPacket packet;
      for(int i=0;i<6;++i){auto& c=cases[i];Vu0SatProbe::make(c,seed,first+i);
        if(first==0&&i==0)c=saved.c;
        if(first+i>=6000)c.motion[0]=c.motion[1]={};
        for(int k=0;k<2;++k)if(!packVu0SharedBody(bodies[2*i+k],c.view(k),c.box[k],c.motion[k]))return false;}
      for(int j=0;j<24;++j){const int i=j%6;const auto a=cases[i].motion[0],b=cases[i].motion[1];
        if(packet.append(2*i,&bodies[2*i],2*i+1,&bodies[2*i+1],a.x==b.x&&a.y==b.y&&a.z==b.z)!=Vu0SharedPacket::Append::Added)return false;}
      auto data=reinterpret_cast<volatile unsigned*>(0x11004000);
      alignas(16) const unsigned control[4]={unsigned(packet.count),unsigned(packet.bodyCount),0,0};
      alignas(16) const float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
      copy(data,control,1);copy(data+4,constants,1);copy(data+8,packet.jobs,packet.count);
      for(int i=0;i<packet.bodyCount;++i)copy(data+(Vu0SharedPacket::BodyBase+i*14)*4,packet.bodies[i],14);
      for(int i=242*4;i<1024;++i)data[i]=0xccaabbdd;
      asm volatile("sync.l\n vcallms 0":::"memory");wait();
      Vu0SharedResult results[24];copy(results,data+Vu0SharedPacket::ResultBase*4,48);
      for(int i=242*4;i<1024;++i)if(data[i]!=0xccaabbdd){printf("VUSHARED guard image=%d first=%d\n",image,first);return false;}
      for(int j=0;j<24;++j){const auto& c=cases[j%6];const auto& r=results[j];ConvexContact actual{},reference{};
        const bool expected=scalarMotionContactReference(c.view(0),c.view(1),c.motion[0],c.motion[1],reference,&c.box[0],&c.box[1]);
        const bool hit=finishVu0Shared(r,c.view(0),c.view(1),c.box[0],c.box[1],c.motion[0],c.motion[1],actual);
        if(hit!=expected||(hit&&std::memcmp(&actual,&reference,sizeof(actual)))){
          printf("VUSHARED failure image=%d case=%d job=%d expected=%d hit=%d\n",image,first+j%6,j,expected,hit);fflush(stdout);return false;}
        if(first==0&&j==0){
          const auto comparison=compareVu0Sat(hit,actual,saved.referenceHit,saved.reference,
            std::sqrt(std::min(cvDot(c.box[0].halfAxes[0],c.box[0].halfAxes[0])+cvDot(c.box[0].halfAxes[1],c.box[0].halfAxes[1])+cvDot(c.box[0].halfAxes[2],c.box[0].halfAxes[2]),
                               cvDot(c.box[1].halfAxes[0],c.box[1].halfAxes[0])+cvDot(c.box[1].halfAxes[1],c.box[1].halfAxes[1])+cvDot(c.box[1].halfAxes[2],c.box[1].halfAxes[2]))));
          printf("VUSHARED replay image=%d accepted=%d\n",image,comparison.accepted);fflush(stdout);if(!comparison.accepted)return false;}
        const bool raw=r.interval[2]!=0||(r.interval[3]!=0&&r.state[0]!=0&&r.interval[0]<=1&&r.interval[1]>=0);
        if((first+j%6)%11==9&&!raw)return false;
        if((first+j%6)%11==7&&first<6000&&!raw)return false;
        hits+=hit;rejects+=!raw;++checked;
      }
    }
    printf("VUSHARED image=%d words=%u cases=%u hits=%u raw_rejects=%u failures=0\n",image,words,checked,hits,rejects);fflush(stdout);
    if(rejects<1000)return false;
  }
  return true;
}
}
#endif
