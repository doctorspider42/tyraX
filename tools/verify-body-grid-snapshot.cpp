// Replay physical body/query bounds against an independent all-pairs oracle.
#include "physics/broad_phase.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

static bool overlaps(const Tyra::BroadBox& a,const Tyra::BroadBox& b){
  for(int k=0;k<3;++k)if(a.mn[k]>b.mx[k] || a.mx[k]<b.mn[k])return false;
  return true;
}
int main(int argc,char** argv){
  assert(argc>1);static_assert(sizeof(Tyra::BroadBox)==24 && sizeof(unsigned)==4);
  unsigned checked=0;
  for(int file=1;file<argc;++file){
    std::ifstream in(argv[file],std::ios::binary);unsigned h[6]{};
    in.read(reinterpret_cast<char*>(h),sizeof(h));
    assert(in && h[0]==0x47524944u && h[1]==1 && h[2]>0 && h[2]<100000 && h[3]<100000);
    std::vector<Tyra::BroadBox> boxes(h[2]),queries(h[3]);
    in.read(reinterpret_cast<char*>(boxes.data()),std::streamsize(boxes.size()*sizeof(boxes[0])));
    in.read(reinterpret_cast<char*>(queries.data()),std::streamsize(queries.size()*sizeof(queries[0])));
    assert(in && in.peek()==std::char_traits<char>::eof());
    for(const auto* list:{&boxes,&queries})for(const auto& b:*list)for(int k=0;k<3;++k)
      assert(std::isfinite(b.mn[k]) && std::isfinite(b.mx[k]) && b.mn[k]<=b.mx[k]);
    for(float cell:{8.f,4.f,2.f,1.f}){
      Tyra::BroadPhase grid(Tyra::BroadPhase::kGrid,cell);grid.begin(int(boxes.size()));
      for(size_t i=0;i<boxes.size();++i)grid.set(int(i),boxes[i],false);
      grid.commit();
      for(const auto& q:queries){std::vector<int> out,expected;grid.queryBox(q,out);
        assert(std::is_sorted(out.begin(),out.end()) && std::adjacent_find(out.begin(),out.end())==out.end());
        out.erase(std::remove_if(out.begin(),out.end(),[&](int i){assert(i>=0 && size_t(i)<boxes.size());return !overlaps(q,boxes[size_t(i)]);}),out.end());
        for(size_t i=0;i<boxes.size();++i)if(overlaps(q,boxes[i]))expected.push_back(int(i));
        assert(out==expected);++checked;
      }
    }
    printf("snapshot phase=%u frame=%u objects=%u queries=%u: four cell sizes preserve all pairs/order\n",h[4],h[5],h[2],h[3]);
  }
  printf("%u captured query/cell combinations passed\n",checked);
}
