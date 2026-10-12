#include "tyrax2-prepared-front.hpp"
#include "renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packages_bbox.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

// Host ABI bridge only; actual bbox construction/merging/classification is linked.
namespace Tyra {
void Vec4::copy(Vec4* out,const float* in){std::memcpy(out->xyzw,in,16);}
void Vec4::operator=(const Vec4& b){copy(this,b.xyzw);}
void Vec4::set(const float& a,const float& b,const float& c,const float& d){x=a;y=b;z=c;w=d;}
void M4x4::copy(M4x4* out,const M4x4& in){std::memcpy(out->data,in.data,64);}
Vec4 M4x4::operator*(const Vec4& v)const {
  Vec4 r;for(unsigned i=0;i<4;++i)r.xyzw[i]=data[i]*v.x+data[i+4]*v.y+data[i+8]*v.z+data[i+12]*v.w;
  return r;
}
Plane::Plane() : normal(0,0,0,1),distance(0){}
Plane::~Plane()=default;
float Plane::distanceTo(const Vec4& v)const{return distance+normal.x*v.x+normal.y*v.y+normal.z*v.z;}
}
using Bounds=Tyra::StaPipBagPackagesBBox;
using Owner=PreparedFront::Owner<Bounds,Tyra::Vec4>;
struct FailingBounds : Bounds {
  static bool fail;
  static const Tyra::Vec4* input(const Tyra::Vec4* p){if(fail)throw std::bad_alloc();return p;}
  FailingBounds(const Tyra::Vec4* p,unsigned n,unsigned c):Bounds(input(p),n,c){}
};
bool FailingBounds::fail=false;
static unsigned seed=103;
static float number(){seed=seed*1664525U+1013904223U;return float(int(seed%8193)-4096)/32.0F;}
static bool same(const Tyra::Vec4& a,const Tyra::Vec4& b){return std::memcmp(a.xyzw,b.xyzw,16)==0;}
static void equalBounds(Bounds& a,Bounds& b){
  assert(a.getVertexCount()==b.getVertexCount() && a.getPartsCount()==b.getPartsCount());
  for(unsigned j=0;j<8;++j)assert(same((*a.getMainBBox())[j],(*b.getMainBBox())[j]));
  for(unsigned i=0;i<a.getPartsCount();++i)for(unsigned j=0;j<8;++j)
    assert(same(a.getChildBBox1By3(i)[j],b.getChildBBox1By3(i)[j]));
  for(unsigned i=0;i<a.getPartsCount();++i){
    const unsigned n=std::min(3U,a.getPartsCount()-i);
    Tyra::Vec4 al,ah,bl,bh;a.getMergedMinMax(i,n,&al,&ah);b.getMergedMinMax(i,n,&bl,&bh);
    assert(same(al,bl) && same(ah,bh));
  }
}
int main(){
  unsigned cases=0,rejections=0,views=0;
  for(unsigned trial=0;trial<512;++trial){
    const unsigned count=3*(1+trial%256),capacity=3*(1+trial%37);
    std::vector<Tyra::Vec4> xyz;xyz.reserve(count);
    for(unsigned i=0;i<count;++i)xyz.emplace_back(number(),number(),number(),1);
    PreparedFront::Key k;k.generation=1;k.streams={reinterpret_cast<std::uintptr_t>(xyz.data()),0x2000,0,0};
    k.resources[2]=0x3000;k.count=count;k.capacity=capacity;k.bboxVersion=19;
    k.shape=PreparedFront::Precise|PreparedFront::FullClip|PreparedFront::MVP;
    k.programEpoch=7;k.programAddress=128;k.programClass=1;k.depthBits=0x4affffff;
    if(trial&1){k.shape|=PreparedFront::Textured;k.streams[2]=0x4000;k.resources[0]=0x5000;}
    if(trial&2){k.shape|=PreparedFront::SingleColor;k.streams[1]=0;k.resources[1]=0x6000;}
    if(trial&4){k.shape|=PreparedFront::Stripped;k.run=capacity;}
    Owner owner;assert(owner.compile(k));Bounds ordinary(xyz.data(),count,capacity);
    auto* prepared=owner.admit(k);assert(prepared && prepared!=&ordinary);equalBounds(*prepared,ordinary);
    for(unsigned view=0;view<24;++view){
      Tyra::Plane planes[6];for(auto& p:planes){p.normal.set(number(),number(),number(),1);p.distance=number();}
      Tyra::M4x4 m;for(auto& f:m.data)f=number();
      Tyra::Plane local[6];Tyra::CoreBBox::computeObjectSpacePlanes(local,planes,m);
      unsigned char ap=0,bp=0;
      assert(prepared->getMainBBox()->frustumCheckAABB(local,&ap)==ordinary.getMainBBox()->frustumCheckAABB(local,&bp));
      assert(ap==bp);
      assert(Tyra::CoreBBox::activePlaneMaskAABB(local,(*prepared->getMainBBox())[0],(*prepared->getMainBBox())[7])==
             Tyra::CoreBBox::activePlaneMaskAABB(local,(*ordinary.getMainBBox())[0],(*ordinary.getMainBBox())[7]));++views;
    }
    auto reject=[&](const PreparedFront::Key& bad){assert(!owner.admit(bad));++rejections;};
    auto bad=k;bad.generation++;reject(bad);
    for(unsigned i=0;i<4;++i){bad=k;bad.streams[i]^=16;reject(bad);bad=k;bad.content[i]++;reject(bad);}
    for(unsigned i=0;i<3;++i){bad=k;bad.resources[i]^=16;reject(bad);}
    for(unsigned i=0;i<8;++i){bad=k;bad.material[i]++;reject(bad);}
    for(unsigned which=0;which<9;++which){bad=k;
      switch(which){case 0:bad.count+=3;break;case 1:bad.capacity+=3;break;case 2:bad.run+=3;break;
      case 3:bad.bboxVersion++;break;case 4:bad.shape^=1;break;case 5:bad.programEpoch++;break;
      case 6:bad.programAddress++;break;case 7:bad.programClass++;break;case 8:bad.depthBits++;break;}reject(bad);}
    assert(!owner.compile(k) && owner.admit(k)==prepared);
    bad=k;bad.generation=2;bad.capacity=0;assert(!owner.compile(bad));assert(owner.admit(k)==prepared);
    bad=k;bad.generation=2;bad.streams[3]=0x7000;assert(!owner.compile(bad));assert(owner.admit(k)==prepared);
    bad=k;bad.generation=2;bad.shape|=64;assert(!owner.compile(bad));assert(owner.admit(k)==prepared);
    for(unsigned which=0;which<9;++which){bad=k;bad.generation=2;
      switch(which){case 0:bad.streams[0]=0;break;case 1:bad.streams[0]|=1;break;
      case 2:bad.count++;break;case 3:bad.capacity++;break;case 4:bad.programEpoch=0;break;
      case 5:bad.programAddress=2048;break;case 6:bad.resources[2]=0;break;
      case 7:bad.count=65538;break;case 8:bad.count=12291;bad.capacity=3;break;}
      assert(!owner.compile(bad) && owner.admit(k)==prepared);
    }
    PreparedFront::Owner<FailingBounds,Tyra::Vec4> failing;
    assert(failing.compile(k));auto* before=failing.admit(k);bad=k;bad.generation=2;
    FailingBounds::fail=true;bool caught=false;
    try{failing.compile(bad);}catch(const std::bad_alloc&){caught=true;}
    FailingBounds::fail=false;assert(caught && failing.admit(k)==before);assert(failing.compile(bad));
    for(auto& v:xyz){v.x+=17;v.z-=9;}
    bad=k;bad.content[0]++;bad.bboxVersion++;reject(bad);bad.generation=2;
    assert(owner.compile(bad));assert(!owner.admit(k));Bounds changed(xyz.data(),count,capacity);
    equalBounds(*owner.admit(bad),changed);
    owner.invalidate();assert(!owner.admit(bad) && !owner.compile(bad));bad.generation=3;assert(owner.compile(bad));
    PreparedFront::Attachment<Bounds,Tyra::Vec4> attachment;
    attachment.value=std::make_unique<Owner>();bad.generation=4;assert(attachment.value->compile(bad));
    auto copy=attachment;assert(!copy.value && attachment.value);
    copy=attachment;assert(!copy.value);attachment=attachment;assert(attachment.value);
    copy.value=std::make_unique<Owner>();assert(copy.value->compile(bad));copy=attachment;assert(!copy.value);
    Owner ceiling;bad.generation=std::numeric_limits<std::uint64_t>::max();assert(ceiling.compile(bad));
    auto wrapped=bad;wrapped.generation=0;assert(!ceiling.compile(wrapped) && ceiling.admit(bad));
    ++cases;
  }
  std::printf("PASS prepared frontend actual-bbox cases=%u changed-views=%u stale-controls=%u; no DMA/FPS claim\n",cases,views,rejections);
  return 0;
}
