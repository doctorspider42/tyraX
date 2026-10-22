#include "renderer/3d/pipeline/static/core/stapip_pool_color_table.hpp"
#include <array>
#include <cassert>
#include <cstdio>
using namespace Tyra::ExperimentalPoolTable;
int main() {
 unsigned checks=0;
 for(unsigned members=1;members<=16;++members) {
  std::array<ColorBits,16> table{};
  std::array<ColorBits,1536> backing{};
  std::array<uint32_t,1536> ready{};
  for(unsigned i=0;i<members;++i) for(unsigned j=0;j<4;++j) table[i].word[j]=i*17+j;
  View v;v.colors=table.data();v.members=members;v.total=members*96;v.generation=1;
  v.expanded=backing.data();v.ready=ready.data();
  for(unsigned offset=0;offset<v.total;offset+=3) {
   const unsigned count=(v.total-offset)<75 ? v.total-offset : 75;
   assert(materializeRange(&v,backing.data()+offset,count));
   for(unsigned k=0;k<count;++k) {
    assert(ready[offset+k]==1);
    assert(!std::memcmp(&backing[offset+k],&table[(offset+k)/96],16));++checks;
   }
  }
  // Changed generation: untouched source remains stale until a real fallback read.
  ++v.generation;table[0].word[0]^=123;
  assert(ready[0]==1);
  assert(materializeRange(&v,backing.data()+72,v.total-72<48 ? v.total-72 : 48));
  for(unsigned k=0;k<v.total;++k) {
   if(k>=72 && k<120 && k<v.total) assert(ready[k]==2);else assert(ready[k]==1);
  }
  assert(!materializeRange(&v,backing.data()+v.total-1,2));
  ColorBits engineOwned{};assert(materializeRange(&v,&engineOwned,3));
  assert(materializeRange(nullptr,nullptr,0));
 }
 std::printf("PASS_ACTUAL_HEADER_LAZY_RANGE %u differential color checks\n",checks);
}
