// Host tests of the exact A19 predicate/oracle used by the private target.
#include "tyrax2-list-join.hpp"
#include <cassert>

struct V { unsigned words[4]; };
struct Color { V* many; };
struct Bag { bool stripped; Color* color; void* texture; void* lighting; };
struct Package {
  Bag* bag = nullptr;
  const V *vertices = nullptr, *sts = nullptr, *colors = nullptr, *normals = nullptr;
  unsigned short size = 0;
  unsigned char clipPlaneMask = 0;
};
struct Buffer {
  Bag* bag = nullptr;
  const V *vertices = nullptr, *sts = nullptr, *colors = nullptr, *normals = nullptr;
  unsigned size = 0, clipPlaneMask = 0;
  bool stripped = false;
  V storage[4][24]{};
  void fillByPointer(const Package& p) {
    bag=p.bag; vertices=p.vertices; sts=p.sts; colors=p.colors; normals=p.normals;
    size=p.size; clipPlaneMask=p.clipPlaneMask; stripped=p.bag->stripped;
  }
  void gather(const Package* const* parts, unsigned count) {
    bag=parts[0]->bag; size=clipPlaneMask=0; stripped=false;
    for(unsigned i=0;i<count;++i){
      const auto& p=*parts[i];
      const V* src[]={p.vertices,p.sts,p.colors,p.normals};
      for(unsigned s=0;s<4;++s)if(src[s])std::memcpy(storage[s]+size,src[s],p.size*16);
      size+=p.size;clipPlaneMask|=p.clipPlaneMask;
    }
    vertices=storage[0];sts=storage[1];colors=storage[2];normals=storage[3];
  }
};

int main(int argc, char**) {
  V streams[4][24]{};
  for(unsigned s=0;s<4;++s)for(unsigned i=0;i<24;++i)
    for(unsigned w=0;w<4;++w)streams[s][i].words[w]=(s<<24)|(i<<8)|w;
  unsigned tests=0;
  for(unsigned flags=0;flags<8;++flags)for(unsigned n=2;n<=3;++n){
    Color color{flags&2?streams[2]:nullptr};
    Bag bag{false,&color,flags&1?streams[1]:nullptr,flags&4?streams[3]:nullptr};
    Package p[3];
    const Package* parts[]={&p[0],&p[1],&p[2]};
    for(unsigned i=0;i<3;++i)p[i]={&bag,streams[0]+i*6,
      flags&1?streams[1]+i*6:nullptr,flags&2?streams[2]+i*6:nullptr,
      flags&4?streams[3]+i*6:nullptr,6,static_cast<unsigned char>(1<<i)};
    Package joined;
    assert(ListJoin::describe(parts,n,joined));++tests;
    assert(joined.size==n*6 && joined.clipPlaneMask==(1u<<n)-1);
    Buffer buffer;buffer.gather(parts,n);
    if(argc>1 && flags==7 && n==3)buffer.storage[2][0].words[0]^=1;
    ListJoin::reset(true);ListJoin::checkAndApply(buffer,parts,n);
    assert(ListJoin::calls==1 && ListJoin::accepted==1 && buffer.vertices==streams[0]);
    const unsigned used=1+!!(flags&1)+!!(flags&2)+!!(flags&4);
    assert(ListJoin::bytes==n*6*16*used);++tests;
    // Fresh copies in later frames must observe mutations, not cached content.
    streams[0][0].words[0]^=0x1234;buffer.gather(parts,n);
    ListJoin::reset(true);ListJoin::checkAndApply(buffer,parts,n);
    assert(buffer.vertices[0].words[0]==streams[0][0].words[0]);++tests;
    for(unsigned s=0;s<4;++s){
      if(s && !(flags&(1u<<(s-1))))continue;
      Package changed=p[1];
      const V** ptrs[]={&p[1].vertices,&p[1].sts,&p[1].colors,&p[1].normals};
      ++*ptrs[s];assert(!ListJoin::describe(parts,n,joined));p[1]=changed;++tests;
      *ptrs[s]=nullptr;assert(!ListJoin::describe(parts,n,joined));p[1]=changed;++tests;
    }
    Bag other=bag;p[1].bag=&other;assert(!ListJoin::describe(parts,n,joined));p[1].bag=&bag;++tests;
    bag.stripped=true;assert(!ListJoin::describe(parts,n,joined));bag.stripped=false;++tests;
    p[1].size=0;assert(!ListJoin::describe(parts,n,joined));p[1].size=6;++tests;
    p[1].size=5;assert(!ListJoin::describe(parts,n,joined));p[1].size=6;++tests;
    assert(!ListJoin::describe(parts,1,joined));++tests;
    buffer.gather(parts,n);ListJoin::reset(false);ListJoin::checkAndApply(buffer,parts,n);
    assert(!ListJoin::calls && buffer.vertices==buffer.storage[0]);++tests;
  }
  std::printf("LISTJOIN HOST PASS checks=%u\n",tests);
}
