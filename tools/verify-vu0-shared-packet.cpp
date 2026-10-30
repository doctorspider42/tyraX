// Independent stream/order/memory oracle for the private shared-body packet.
#include "ps2rig/vu0_shared_packet.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
using namespace Tyra;
int main(){
  { // Unsupported shapes leave the destination untouched for EE fallback.
    CollisionV3 vertices[8]{},normals[6]{},edges[3]{};
    for(int i=0;i<6;++i)normals[i]={float(i+1),float(i+2),float(i+3)};
    for(int i=0;i<3;++i)edges[i]={float(i+11),float(i+12),float(i+13)};
    CollisionObb box{};box.centre={1,2,3};for(int i=0;i<3;++i)box.halfAxes[i]=edges[i];
    ConvexView view{vertices,8,normals,6,edges,3};Vu0SharedBody packed;
    assert(packVu0SharedBody(packed,view,box,{4,5,6}));
    assert(packed.q[0][0]==1&&packed.q[4][2]==6&&packed.q[10][0]==6&&packed.q[13][2]==15);
    for(const auto& q:packed.q)assert(q[3]==0);
    const auto before=packed;view.nv=7;assert(!packVu0SharedBody(packed,view,box,{}));
    assert(!std::memcmp(&packed,&before,sizeof(packed)));
  }
  std::vector<Vu0SharedBody> body(64);
  for(int b=0;b<64;++b)for(int q=0;q<14;++q)for(int k=0;k<4;++k)body[b].q[q][k]=float(b*1000+q*4+k);
  Vu0SharedPacket packet;std::vector<std::array<int,3>> expected;unsigned checked=0,packets=0;
  auto flush=[&]{
    if(!packet.count)return;
    constexpr unsigned Sentinel=0xccaabbdd;std::vector<unsigned> memory(1040,Sentinel);
    assert(packet.stage(memory.data(),memory.size()));assert(packet.count==int(expected.size()));
    for(int i=0;i<packet.count;++i){const auto& e=expected[i];const unsigned* job=memory.data()+8+i*4;
      assert(job[0]==unsigned(Vu0SharedPacket::BodyBase+packet.find(e[0])*14));
      assert(job[1]==unsigned(Vu0SharedPacket::BodyBase+packet.find(e[1])*14));assert(job[2]==unsigned(e[2]));
      assert(std::memcmp(memory.data()+job[0]*4,&body[e[0]],sizeof(Vu0SharedBody))==0);
      assert(std::memcmp(memory.data()+job[1]*4,&body[e[1]],sizeof(Vu0SharedBody))==0);++checked;
    }
    for(std::size_t i=Vu0SharedPacket::ResultBase*4;i<memory.size();++i)assert(memory[i]==Sentinel);
    ++packets;packet.clear();expected.clear();
  };
  auto add=[&](int a,int b,bool common){auto r=packet.append(a,&body[a],b,&body[b],common);
    if(r==Vu0SharedPacket::Append::Full){flush();r=packet.append(a,&body[a],b,&body[b],common);}
    assert(r==Vu0SharedPacket::Append::Added);expected.push_back({a,b,int(common)});
  };
  std::mt19937 random(0x53485244);
  for(int i=0;i<50000;++i)add(int(random()%64),int(random()%64),bool(random()&1));
  flush();
  for(int i=0;i<Vu0SharedPacket::MaxJobs;++i)add(0,1+i%11,false);
  assert(packet.count==24&&packet.bodyCount==12);const int before=packet.count;
  assert(packet.append(0,&body[0],1,&body[1],false)==Vu0SharedPacket::Append::Full&&packet.count==before);
  assert(packet.append(0,&body[2],1,&body[1],false)==Vu0SharedPacket::Append::Invalid&&packet.count==before);
  flush();assert(!packet.stage(nullptr,0));
  std::cout<<"Shared packet: "<<checked<<" ordered jobs in "<<packets<<" packets, no drops or writes into results/guard\n";
}
