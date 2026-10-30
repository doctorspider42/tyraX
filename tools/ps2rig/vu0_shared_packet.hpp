// Private shared-body SAT/CCD input format. No pair/contact truncation.
#pragma once
#include "physics/vu0_sat.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace Tyra {
struct alignas(16) Vu0SharedBody {float q[14][4]{};};
inline bool packVu0SharedBody(Vu0SharedBody& out,const ConvexView& view,
                              const CollisionObb& box,CollisionV3 motion){
  if(view.nv!=8||view.nn!=6||view.ne!=3)return false;
  auto put=[&](int i,CollisionV3 v){out.q[i][0]=v.x;out.q[i][1]=v.y;out.q[i][2]=v.z;out.q[i][3]=0;};
  put(0,box.centre);for(int k=0;k<3;++k)put(k+1,box.halfAxes[k]);put(4,motion);
  for(int k=0;k<6;++k)put(k+5,view.normals[k]);
  for(int k=0;k<3;++k)put(k+11,view.edges[k]);
  return true;
}
struct alignas(16) Vu0SharedResult {float interval[4],state[4];};
struct Vu0SharedPacket {
  static constexpr int MaxBodies=12,MaxJobs=24,JobBase=2,BodyBase=26,ResultBase=194,Words=1024;
  enum class Append {Added,Full,Invalid};
  alignas(16) unsigned jobs[MaxJobs][4]{};
  std::array<const Vu0SharedBody*,MaxBodies> bodies{};
  std::array<int,MaxBodies> keys{};
  int count=0,bodyCount=0;
  void clear(){count=bodyCount=0;}
  int find(int key)const{for(int i=0;i<bodyCount;++i)if(keys[i]==key)return i;return -1;}
  Append append(int a,const Vu0SharedBody* pa,int b,const Vu0SharedBody* pb,bool commonMotion){
    if(a<0||b<0||!pa||!pb||(a==b&&pa!=pb))return Append::Invalid;
    int ia=find(a),ib=find(b);
    if((ia>=0&&bodies[ia]!=pa)||(ib>=0&&bodies[ib]!=pb))return Append::Invalid;
    const int needed=int(ia<0)+int(ib<0&&b!=a);
    if(count==MaxJobs||bodyCount+needed>MaxBodies)return Append::Full;
    if(ia<0){ia=bodyCount++;keys[ia]=a;bodies[ia]=pa;}
    if(b==a)ib=ia;
    else if(ib<0){ib=bodyCount++;keys[ib]=b;bodies[ib]=pb;}
    auto& job=jobs[count++];job[0]=unsigned(BodyBase+ia*14);job[1]=unsigned(BodyBase+ib*14);
    job[2]=unsigned(commonMotion);job[3]=0;return Append::Added;
  }
  // Host verification staging only; EE uploader may copy each referenced record
  // directly into idle VU memory, avoiding a second CPU record copy.
  bool stage(unsigned* memory,std::size_t words)const{
    if(!memory||words<Words||count<=0)return false;
    memory[0]=unsigned(count);memory[1]=unsigned(bodyCount);memory[2]=memory[3]=0;
    const float constants[4]={1e30f,1e-10f,1e-9f,1e-6f};std::memcpy(memory+4,constants,16);
    std::memcpy(memory+JobBase*4,jobs,std::size_t(count)*16);
    for(int i=0;i<bodyCount;++i)std::memcpy(memory+(BodyBase+i*14)*4,bodies[i],sizeof(Vu0SharedBody));
    return true;
  }
};
static_assert(sizeof(Vu0SharedBody)==224&&sizeof(Vu0SharedResult)==32,"shared packet ABI");
static_assert(Vu0SharedPacket::JobBase+Vu0SharedPacket::MaxJobs==Vu0SharedPacket::BodyBase,"job bounds");
static_assert(Vu0SharedPacket::BodyBase+Vu0SharedPacket::MaxBodies*14==Vu0SharedPacket::ResultBase,"body bounds");
static_assert(Vu0SharedPacket::ResultBase+Vu0SharedPacket::MaxJobs*2<=256,"VU0 data budget");
inline Vu0SatResult legacySharedResult(const Vu0SharedResult& r){
  Vu0SatResult out{};out.normalDepth[3]=0;out.sweepEnter[3]=r.interval[0];
  out.flags[0]=r.interval[1];out.flags[1]=r.interval[2];out.flags[2]=r.interval[3];out.flags[3]=r.state[0];
  out.indices[0]=out.indices[1]=-1;out.indices[2]=r.state[1];return out;
}
inline bool finishVu0Shared(const Vu0SharedResult& r,const ConvexView& a,const ConvexView& b,
                            const CollisionObb& aa,const CollisionObb& bb,
                            CollisionV3 ma,CollisionV3 mb,ConvexContact& out,bool profile=false){
  const bool possible=r.interval[2]!=0 || (r.interval[3]!=0&&r.state[0]!=0&&r.interval[0]<=1&&r.interval[1]>=0);
  if(!possible&&std::isfinite(r.state[1])&&r.state[1]>=0&&r.state[1]<21&&a.nn==6&&b.nn==6&&a.ne==3&&b.ne==3){
    const int i=int(r.state[1]);
    const auto axis=i<6?a.normals[i]:i<12?b.normals[i-6]:cvCross(a.edges[(i-12)/3],b.edges[(i-12)%3]);
    if(convexSeparatedOnAxis(a,b,ma,mb,axis,&aa,&bb)){
      if(profile)++vu0SatMetrics.provedNegative;
      return false;
    }
  }
  if(profile){
    if(!possible)++vu0SatMetrics.confirmedNegative;
    else if(r.interval[2]==0)++vu0SatMetrics.confirmedCcd;
    else ++vu0SatMetrics.confirmedPositive;
  }
  return scalarMotionContactReference(a,b,ma,mb,out,&aa,&bb);
}
}
