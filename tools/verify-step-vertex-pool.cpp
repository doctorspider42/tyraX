// Exact storage/admission oracle for the private packed pose pool.
#include "ps2rig/step_vertex_pool.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
struct V {float x,y,z;};
int main(){
  Tyra::StepVertexPool<V> pool;std::mt19937 rng(0x57e9);
  std::uint64_t compared=0;
  for(int frame=0;frame<12000;++frame){
    const int count=frame%19==0?0:int(rng()%257);
    std::vector<int> counts(count);std::size_t total=0;
    for(int& n:counts){n=std::array<int,6>{0,3,8,12,23,24}[rng()%6];total+=n;}
    assert(pool.begin(counts,24));assert(pool.vertices()==total);
    std::vector<std::array<V,24>> old0(count),old1(count);
    struct Record {V* from;V* to;};std::vector<Record> records;
    for(int i=0;i<count;++i){
      records.push_back({pool.previous(i),pool.predicted(i)});
      assert((records.back().from==nullptr)==(counts[i]==0));
      assert((records.back().to==nullptr)==(counts[i]==0));
      for(int k=0;k<counts[i];++k){
        std::uint32_t bits[6];for(auto& b:bits)b=rng();
        std::memcpy(&old0[i][k],bits,12);std::memcpy(&old1[i][k],bits+3,12);
        std::memcpy(records[i].from+k,&old0[i][k],12);std::memcpy(records[i].to+k,&old1[i][k],12);
      }
    }
    // Record-vector growth may relocate records; it must not relocate the pose pool.
    records.reserve(records.size()+513);
    for(int i=0;i<count;++i)for(int k=0;k<counts[i];++k){
      assert(std::memcmp(records[i].from+k,&old0[i][k],12)==0);
      assert(std::memcmp(records[i].to+k,&old1[i][k],12)==0);compared+=2;
    }
    const auto before=pool.vertices();assert(!pool.begin({8,-1,24},24));assert(pool.vertices()==before);
    assert(!pool.begin({25},24));assert(pool.vertices()==before);
  }
  assert(pool.begin({},24));assert(pool.vertices()==0);
  std::cout<<"Pose pool: "<<compared<<" bitwise vertex comparisons, invalid counts rejected, zero truncation\n";
}
