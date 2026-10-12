#include "tyrax2-clip-ring-input.hpp"
#include "tyrax2-coarse-strip-input.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
struct V { unsigned bits[4]; };
struct Color { bool many; };
struct Bag { bool texture,lighting;Color* color; };
struct Geometry {
  Bag* bag;V *vertices,*sts,*colors,*normals;
  unsigned size,clipPlaneMask,nativeClipParity;
  bool nativeClipStrip,coarseClipStrip,partialStripOrigin;
};
int main(){
  unsigned cases=0;
  std::array<bool,7> rejected{};
  for(unsigned streams=0;streams<8;++streams)
    for(unsigned first=0;first<8;++first)
      for(unsigned count=3;count<=72;++count)
        for(unsigned mask: {0U,1U,31U,63U}){
          const unsigned triangles=count-2;
          std::array<std::vector<V>,4> data;
          for(unsigned c=0;c<4;++c){
            data[c].resize(first+count);
            for(unsigned i=0;i<first+count;++i)
              for(unsigned lane=0;lane<4;++lane)data[c][i].bits[lane]=1009*i+71*c+lane;
          }
          Color color{bool(streams&2)};Bag bag{bool(streams&1),bool(streams&4),&color};
          Geometry pkg{&bag,data[0].data()+first,data[1].data()+first,
            data[2].data()+first,data[3].data()+first,count,mask,first&1,true,true,true};
          ClipConsumer::observe=true;CoarseStrip::reset();
          CoarseStrip::expect(pkg,first&1);
          std::array<std::vector<V>,4> expanded;
          for(unsigned c=0;c<4;++c)
            for(unsigned t=0;t<triangles;++t)
              for(unsigned corner=0;corner<3;++corner){
                const unsigned local=((first+t)&1U) && corner<2?1U-corner:corner;
                expanded[c].push_back(data[c][first+t+local]);
              }
          Geometry list=pkg;list.size=3*triangles;list.nativeClipStrip=false;
          list.vertices=expanded[0].data();list.sts=expanded[1].data();
          list.colors=expanded[2].data();list.normals=expanded[3].data();
          CoarseStrip::Digest expandedExpected;
          CoarseStrip::ordered(expandedExpected,list,false,0);
          assert(CoarseStrip::same(expandedExpected,CoarseStrip::source));
          for(unsigned t=0;t<triangles;t+=7){
            Geometry piece=pkg;piece.size=std::min(7U,triangles-t)+2;
            piece.nativeClipParity=(first+t)&1;
            piece.vertices+=t;piece.sts+=t;piece.colors+=t;piece.normals+=t;
            CoarseStrip::input(piece);
          }
          assert(CoarseStrip::same(CoarseStrip::source,CoarseStrip::delivered));
          assert(CoarseStrip::sourcePackages==1 && CoarseStrip::deliveryJobs>0 &&
            CoarseStrip::residual.triangles==0);
          const auto expected=CoarseStrip::source;
          auto check=[&](Geometry bad,unsigned which){
            CoarseStrip::reset();CoarseStrip::input(bad);
            rejected[which]=rejected[which] || !CoarseStrip::same(expected,CoarseStrip::delivered);
          };
          Geometry bad=pkg;bad.nativeClipParity^=1;check(bad,0);
          bad=pkg;bad.clipPlaneMask^=1;check(bad,1);
          bad=pkg;bad.size--;check(bad,2);
          CoarseStrip::reset();CoarseStrip::input(pkg);CoarseStrip::input(pkg);
          rejected[3]|=!CoarseStrip::same(expected,CoarseStrip::delivered);
          if(streams&2){data[2][first].bits[0]^=1;check(pkg,4);data[2][first].bits[0]^=1;}
          bad=pkg;bad.coarseClipStrip=false;check(bad,5);
          // A non-source clip job must remain separately observable in every arm.
          bad=list;bad.coarseClipStrip=false;bad.partialStripOrigin=false;
          CoarseStrip::reset();CoarseStrip::input(bad);
          CoarseStrip::Digest listExpected;
          CoarseStrip::ordered(listExpected,bad,false,0);
          assert(CoarseStrip::same(listExpected,CoarseStrip::residual));
          assert(CoarseStrip::delivered.triangles==0);
          CoarseStrip::reset();ClipConsumer::observe=false;
          CoarseStrip::expect(pkg,first&1);CoarseStrip::input(pkg);
          rejected[6]|=CoarseStrip::sourcePackages==0 && CoarseStrip::deliveryJobs==0 &&
            CoarseStrip::source.triangles==0 && CoarseStrip::delivered.triangles==0;
          ++cases;
        }
  assert(std::all_of(rejected.begin(),rejected.end(),[](bool b){return b;}));
  std::cout<<"PASS coarse source/delivery cases="<<cases<<" controls=7\n";
}
