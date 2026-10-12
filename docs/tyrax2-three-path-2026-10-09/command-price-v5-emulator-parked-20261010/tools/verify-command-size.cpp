#include "tyrax2-command-size.hpp"
#include "../vendor/tyra/engine/inc/renderer/core/paths/path1/frame_chain_arena.hpp"
#include <cassert>
#include <cstdio>

alignas(64) uint32_t source[8192*4]{}, output[262144]{}, payload[256]{};
const uint32_t* resolve(uint32_t address,uint32_t qwords,void*) {
  return address==0x1000 && qwords<=64 ? payload : nullptr;
}
int main() {
  unsigned checks=0;
  for(unsigned groups=1;groups<=48;++groups)for(unsigned size=0;size<=64;++size){
    unsigned qw=0,refs=0;
    for(unsigned g=0;g<groups;++g){
      source[qw*4]=(3u<<28)|size;source[qw*4+1]=0x1000;++qw;refs+=size;
      source[qw*4]=(1u<<28)|(g%3);source[qw*4+1]=0;qw+=1+g%3;
    }
    source[qw*4]=7u<<28;source[qw*4+1]=0;++qw;
    const auto required=CommandTape::closureAllocation(source,qw,sizeof(output));
    assert(required==((qw+refs)*16+63u)/64*64);
    Tyra::FrameChainArena arena(output,required);
    auto snapshot=arena.copy(source,qw,resolve,nullptr,0x8000);
    assert(snapshot.chain && !snapshot.borrowedBytes && snapshot.bytes==(qw+refs)*16);
    assert(required>=snapshot.bytes && required-snapshot.bytes<64);
    assert(CommandTape::closureAllocation(source,qw,required)==required);
    assert(!CommandTape::closureAllocation(source,qw,required-1));
    ++checks;
  }
  assert(!CommandTape::closureAllocation(nullptr,1,64));
  assert(!CommandTape::closureAllocation(source,0,64));
  assert(!CommandTape::closureAllocation(source,65537,4*1024*1024));
  source[0]=7u<<28;assert(CommandTape::closureAllocation(source,1,64)==64);
  assert(!CommandTape::closureAllocation(source,2,64));
  source[0]=(1u<<28)|2;assert(!CommandTape::closureAllocation(source,1,64));
  source[0]=3u<<28;source[1]=0x1001;assert(!CommandTape::closureAllocation(source,1,64));
  source[1]=0x1000;assert(!CommandTape::closureAllocation(source,1,64));
  source[0]=(3u<<28)|65535;assert(!CommandTape::closureAllocation(source,1,64));
  for(unsigned id : {0u,2u,4u,5u,6u}){
    source[0]=id<<28;assert(!CommandTape::closureAllocation(source,1,64));
  }
  source[0]=(7u<<28)|0x80000000u;assert(!CommandTape::closureAllocation(source,1,64));
  std::printf("PASS command sizing: %u actual-arena comparisons plus malformed/boundary controls\n",checks);
}
