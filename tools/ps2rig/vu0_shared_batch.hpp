// Private scene scheduler: VU0 owns its data memory between start and finish.
#pragma once
#include "vu0_shared_packet.hpp"
#include "math/vu0_owner.hpp"
#include "debug/debug.hpp"
extern unsigned Vu0SharedSeparation_CodeStart,Vu0SharedSeparation_CodeEnd;
namespace Tyra {
class Vu0SharedBatch {
  bool active=false;int count=0;
  static void wait(){unsigned status;do{asm volatile("cfc2 %0,$29":"=r"(status)::"memory");}while(status&1);}
  static void copy(volatile void* destination,const volatile void* source,int qwords){
    auto dst=static_cast<volatile unsigned char*>(destination);auto src=static_cast<const volatile unsigned char*>(source);
    asm volatile("1:\n lq $8,0(%0)\n sq $8,0(%1)\n addiu %0,%0,16\n addiu %1,%1,16\n addiu %2,%2,-1\n bnez %2,1b\n nop\n"
      :"+r"(src),"+r"(dst),"+r"(qwords) :: "$8","memory");
  }
public:
  bool begin(){
    if(active)return false;
    wait();const auto* first=&Vu0SharedSeparation_CodeStart;
    if(vu0MicroOwner!=first){const unsigned words=&Vu0SharedSeparation_CodeEnd-first;if(!words||words>1024)return false;
      auto dst=reinterpret_cast<volatile unsigned*>(0x11000000);for(unsigned i=0;i<words;++i)dst[i]=first[i];
      asm volatile("sync.l":::"memory");vu0MicroOwner=first;}
    return true;
  }
  void start(const Vu0SharedPacket& packet){
    TYRA_ASSERT(!active&&packet.count>0&&packet.count<=Vu0SharedPacket::MaxJobs,"Shared VU0: invalid packet");
    wait();auto data=reinterpret_cast<volatile unsigned*>(0x11004000);
    alignas(16) const unsigned control[4]={unsigned(packet.count),unsigned(packet.bodyCount),0,0};
    alignas(16) const float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};
    copy(data,control,1);copy(data+4,constants,1);copy(data+8,packet.jobs,packet.count);
    for(int i=0;i<packet.bodyCount;++i)copy(data+(Vu0SharedPacket::BodyBase+i*14)*4,packet.bodies[i],14);
    count=packet.count;active=true;vu0SatMetrics.jobs+=count;++vu0SatMetrics.batches;
    asm volatile("sync.l\n vcallms 0":::"memory");
  }
  void finish(Vu0SharedResult* results){
    if(!active)return;
    unsigned start,stop,status;
    asm volatile("mfc0 %0,$9\n cfc2 %1,$29":"=r"(start),"=r"(status)::"memory");
    vu0SatMetrics.busyAtFinish+=(status&1)!=0;wait();asm volatile("mfc0 %0,$9":"=r"(stop)::"memory");
    vu0SatMetrics.waitTicks+=stop-start;copy(results,reinterpret_cast<volatile unsigned*>(0x11004000)+Vu0SharedPacket::ResultBase*4,count*2);active=false;
  }
  void observeOverlap()const{if(active){unsigned status;asm volatile("cfc2 %0,$29":"=r"(status)::"memory");vu0SatMetrics.busyWhileQuerying+=(status&1)!=0;}}
  ~Vu0SharedBatch(){if(active)wait();}
};
}
