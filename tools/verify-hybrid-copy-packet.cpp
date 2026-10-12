#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <sys/mman.h>
#include "tyrax2-hybrid-copy-adapter.hpp"
struct Validator {
  using HybridCopyPacket=Tyra::HybridCopyPacket;
  // ACTUAL_VALIDATOR
};
int main(){
  void* memory=mmap(reinterpret_cast<void*>(0x100000),8192,PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
  assert(memory==reinterpret_cast<void*>(0x100000));
  Validator v;unsigned cases=0;
  for(unsigned words=2;words<=128;++words){
    Tyra::HybridCopyPacket p{0x100000,words,7};
    auto* tail=reinterpret_cast<std::uint64_t*>(p.address+(words-2)*16);
    tail[0]=0x1000000000008001ULL;tail[1]=14;tail[2]=1;tail[3]=0x61;
    assert(v.validPacket(p));++cases;
    for(unsigned i=0;i<4;++i){
      tail[i]^=1;assert(!v.validPacket(p));tail[i]^=1;++cases;
    }
  }
  for(auto p:{Tyra::HybridCopyPacket{0,80,7},{0xfffff,80,7},{0x2000000,80,7},
      {0x100001,80,7},{0x100000,0,7},{0x100000,1,7},{0x100000,129,7},
      {0x100000,0xffffffffu,7},{0x1fffffc0,128,7}}){
    assert(!v.validPacket(p));++cases;
  }
  assert(munmap(memory,8192)==0);
  std::printf("HYBRIDPACKET cases=%u actual-tail-validator=PASS\n",cases);
}
