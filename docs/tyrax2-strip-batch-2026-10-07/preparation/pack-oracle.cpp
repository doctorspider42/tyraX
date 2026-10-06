#include "strip_pack.hpp"
#include <array>
#include <map>
#include <random>
#include <cassert>
#include <iostream>
using Tri=std::array<size_t,3>;
std::map<Tri,size_t> triangles(const std::vector<size_t>& labels,unsigned run){
 std::map<Tri,size_t> m;
 for(size_t first=0;first<labels.size();first+=run)
  for(size_t i=first;i+2<std::min(labels.size(),first+run);++i){
   size_t a=labels[i],b=labels[i+1],c=labels[i+2];
   if(a==b||a==c||b==c)continue;
   // The new vertex remains the provoking vertex for GS flat shading.
   ++m[Tri{std::min(a,b),std::max(a,b),c}];
  }
 return m;
}
int main(){
 std::mt19937 rng(20261007);size_t cases=0,vertices=0,saved=0;
 for(unsigned run:{6u,9u,12u,75u})for(unsigned test=0;test<10000;++test){
  std::vector<size_t> labels;std::vector<std::pair<size_t,size_t>> spans;
  const unsigned members=1+rng()%48;size_t serial=0;
  for(unsigned s=0;s<members;++s){
   unsigned count=3*(1+rng()%(run/3));size_t first=labels.size();
   for(unsigned k=0;k<count;++k){size_t x=++serial;if(k&&rng()%8==0)x=labels.back();labels.push_back(x);}
   spans.emplace_back(first,count);while(labels.size()%run)labels.push_back(labels.back());
  }
  auto packed=StripPack::indices(spans,run);assert(packed.size()%3==0);
  std::vector<size_t> output;for(size_t i:packed){assert(i<labels.size());output.push_back(labels[i]);}
  assert(triangles(labels,run)==triangles(output,run));
  vertices+=labels.size();if(output.size()<labels.size())saved+=labels.size()-output.size();++cases;
 }
 assert(StripPack::indices({},75).empty());assert(StripPack::indices({{0,2}},75).empty());assert(StripPack::indices({{0,3}},5).empty());
 std::cout<<"PASS cases="<<cases<<" sourceVertices="<<vertices<<" retainedSavings="<<saved<<" exactTriangleAttributes=1 provokingVertexPreserved=1\n";
}
