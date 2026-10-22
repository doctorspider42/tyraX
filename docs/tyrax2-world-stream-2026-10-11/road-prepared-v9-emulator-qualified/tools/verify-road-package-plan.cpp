#include "tyrax2-road-package-plan.hpp"
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <tuple>

namespace Tyra {
void Vec4::copy(Vec4* out,const float* in){std::memcpy(out->xyzw,in,16);}
void Vec4::operator=(const Vec4& b){copy(this,b.xyzw);}
void Vec4::set(const float& a,const float& b,const float& c,const float& d){x=a;y=b;z=c;w=d;}
void M4x4::copy(M4x4* out,const M4x4& in){std::memcpy(out->data,in.data,64);}
Vec4 M4x4::operator*(const Vec4& v)const {
  Vec4 r;for(unsigned i=0;i<4;++i)r.xyzw[i]=data[i]*v.x+data[i+4]*v.y+data[i+8]*v.z+data[i+12]*v.w;
  return r;
}
Plane::Plane():normal(0,0,0,1),distance(0){}
Plane::~Plane()=default;
float Plane::distanceTo(const Vec4& v)const{return distance+normal.x*v.x+normal.y*v.y+normal.z*v.z;}
}
using namespace RoadPackagePlan;
using Vertex=Tyra::Vec4;
using Bounds=Tyra::StaPipBagPackagesBBox;
using Record=std::tuple<unsigned,unsigned,unsigned,int,unsigned,bool>;
static unsigned rng=1907;
static float number(){rng=rng*1664525U+1013904223U;return float(int(rng%2049)-1024)/16;}
static int verdict(Tyra::CoreBBoxFrustum v) {
  return v==Tyra::OUTSIDE_FRUSTUM ? -1 : v==Tyra::IN_FRUSTUM ? 1 : 0;
}
int main() {
  unsigned cases=0,views=0,routeCounts[4]{},controls=0,commands=0;
  unsigned lostNearFar=0,redundantStarts=0,allClipChanges=0;
  for(unsigned cap=3;cap<=300;cap+=3)for(unsigned r=3;r<=cap;r+=3) {
    if(r!=3 && r!=cap && r!=cap/3 && r!=66 && r!=75)continue;
    // Long inputs exercise eight-package groups and short trailing runs.
    for(unsigned tail: {0U,3U}) {
      const unsigned count=r*19+tail;
      std::vector<Vertex> xyz;xyz.reserve(count);
      for(unsigned i=0;i<count;++i) {
        const float centre=float(int(i/r)%7-3)*20;
        xyz.emplace_back(centre+number()/8,number()/8,number()/8,1);
      }
      Bounds bounds(xyz.data(),count,cap);
      auto plan=compile<Vertex>(bounds,count,cap,r);
      for(const auto& job:plan.jobs) {
        assert(job.first+job.count<=count && job.count>=3);
        unsigned triangles=0;
        for(const auto& fragment:job.clips) {
          assert(fragment.count<=std::min(72U,cap));
          assert(fragment.first==job.first+triangles && fragment.parity==(triangles&1));
          triangles+=fragment.count-2;
        }
        assert(triangles==job.count-2);
      }
      Tyra::StaPipColorBag color;Tyra::StaPipBag bag;
      bag.count=count;bag.vertices=xyz.data();bag.color=&color;
      Tyra::StaPipBagPackager ordinary;ordinary.setMaxVertCount(cap);
      ordinary.renderBBox=&bounds;ordinary.capturePlaneMasks=true;
      for(unsigned view=0;view<20;++view) {
        Tyra::Plane screen[6],guard[8];
        const float shift=number(),extent=5+float(view%5)*10;
        const float cosine=std::cos(float(view)*0.31F),sine=std::sin(float(view)*0.31F);
        screen[0].normal.set(cosine,0,sine,0);screen[1].normal.set(-cosine,0,-sine,0);
        screen[2].normal.set(0,1,0,0);screen[3].normal.set(0,-1,0,0);
        screen[4].normal.set(-sine,0,cosine,0);screen[5].normal.set(sine,0,-cosine,0);
        for(unsigned j=0;j<6;++j) {
          screen[j].distance=extent+(j==0?shift:j==1?-shift:0);
          guard[j].normal=screen[j].normal;guard[j].distance=screen[j].distance+20;
        }
        // Exact boundary and near/far-only crossings must NOT become guard.
        guard[6].normal=screen[4].normal;guard[7].normal=screen[5].normal;
        guard[6].distance=view%4==0 ? 0 : 20;guard[7].distance=view%4==1 ? 0 : 20;
        ordinary.objectSpacePlanes=screen;ordinary.clipObjectSpacePlanes=guard;
        unsigned short n=0;auto* packages=ordinary.create(&n,&bag,r);
        assert(n==plan.jobs.size());
        unsigned index=0;Continuation current;int oldLast=-1;
        std::vector<Record> expected,actual;
        select(plan,[&](const Box<Vertex>& b){return verdict(Tyra::CoreBBox::frustumCheckAABB(screen,b.low,b.high,nullptr));},
          [&](const Box<Vertex>& b){return Tyra::CoreBBox::activePlaneMaskAABB(guard,b.low,b.high,8);},
          [&](const Job<Vertex>& job,Route route,unsigned mask) {
            const auto& p=packages[index++];
            const Route old=p.isInFrustum==Tyra::OUTSIDE_FRUSTUM ? Route::Outside :
              p.isInFrustum==Tyra::IN_FRUSTUM ? Route::Cull : p.guardBandOnly ? Route::Guard : Route::Clip;
            assert(route==old && mask==p.clipPlaneMask);
            if(route==Route::Clip && !mask)++lostNearFar;
            if(route!=Route::Clip)++allClipChanges;
            assert(job.first==unsigned(p.vertices-xyz.data()) && job.count==p.size);
            assert(job.firstPart==p.indexOf1By3BBox);++routeCounts[unsigned(route)];
            auto appendOld=[&](unsigned first,unsigned size,unsigned parity,int program,unsigned bits){
              expected.emplace_back(first,size,parity,program,bits,oldLast!=program);oldLast=program;
            };
            if(old==Route::Cull || old==Route::Guard)appendOld(job.first,p.size,0,101,0);
            if(old==Route::Clip) {
              const unsigned step=std::min(72U,cap)-2;
              for(unsigned t=0;t<unsigned(p.size)-2;t+=step)
                appendOld(job.first+t,std::min(step,unsigned(p.size)-2-t)+2,t&1,202,p.clipPlaneMask);
            }
            dispatch(job,route,mask,101,202,current,[&](Range a,int program,unsigned bits,bool start){
              actual.emplace_back(a.first,a.count,a.parity,program,bits,start);
              if(!start)++redundantStarts;
            });
          });
        assert(index==n && expected==actual);commands+=actual.size();++views;
      }
      auto reject=[&](unsigned c,unsigned capacity,unsigned run) {
        bool caught=false;try{compile<Vertex>(bounds,c,capacity,run);}catch(const std::invalid_argument&){caught=true;}
        assert(caught);++controls;
      };
      reject(0,cap,r);reject(count+3,cap,r);reject(count,0,r);
      reject(count,cap,cap+3);reject(count,cap,1);reject(count,cap+1,r);
      // Cold facts are copied: they survive source-bounds replacement.
      const float before=plan.jobs[0].box.low.x;
      for(auto& v:xyz)v.x+=1000;
      bounds.recalculate(xyz.data(),cap);assert(plan.jobs[0].box.low.x==before);
      ++cases;
    }
  }
  for(auto n:routeCounts)assert(n>0);
  assert(lostNearFar && redundantStarts && allClipChanges);
  // Outside must preserve the program selected before it, including guard.
  Continuation c;assert(c.start(101));assert(!c.start(101));assert(c.start(202));assert(!c.start(202));
  std::printf("PASS road package plans=%u moving-views=%u commands=%u controls=%u routes=%u/%u/%u/%u; shortcut-counterexamples nearFar=%u alwaysStart=%u allClip=%u; actual packager, no SDK/DMA/PS2 price\n",
    cases,views,commands,controls,routeCounts[0],routeCounts[1],routeCounts[2],routeCounts[3],
    lostNearFar,redundantStarts,allClipChanges);
}
