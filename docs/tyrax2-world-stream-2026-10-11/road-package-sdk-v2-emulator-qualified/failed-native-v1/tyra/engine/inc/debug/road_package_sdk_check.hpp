#pragma once
#include "debug/road_package_sdk.hpp"
#include <cstdio>
#include <cstring>

namespace RoadPackageSdkCheck {
inline bool done=false;
inline unsigned bodies=0,checks=0,controls=0,owned=0;
inline void run(Tyra::StaPipBag* source,Tyra::StaPipVU1Program* cullPlain,
                Tyra::StaPipVU1Program* clipPlain,Tyra::StaPipVU1Program* cullTexture,
                Tyra::StaPipVU1Program* clipTexture,prim_t prim,
                std::uint64_t epoch,unsigned bufferSize) {
  if(done)return;
  using RoadPackageSdk::Quad;
  std::array<std::vector<Quad>,3> raw;
  const void* streams[]={source->vertices,source->color->many,source->texture->coordinates};
  for(unsigned s=0;s<3;++s) {
    raw[s].resize(156);
    for(unsigned i=0;i<156;++i)
      std::memcpy(&raw[s][i],static_cast<const unsigned char*>(streams[s])+16*(i%source->count),16);
  }
  Tyra::StaPipColorBag colors;colors.many=reinterpret_cast<const Tyra::Color*>(raw[1].data());
  Tyra::StaPipTextureBag texture=*source->texture;
  texture.coordinates=reinterpret_cast<const Tyra::Vec4*>(raw[2].data());
  Tyra::StaPipBag bag;bag.info=source->info;bag.color=&colors;bag.count=156;
  bag.vertices=reinterpret_cast<const Tyra::Vec4*>(raw[0].data());
  auto resolver=[](unsigned address,unsigned n,void*)->const Quad* {
    if(!n || n>65535 || address<0x100000 || (address&15) || address>0x02000000U-n*16)return nullptr;
    return reinterpret_cast<const Quad*>(address);
  };
  std::unique_ptr<packet2_t,WorldCellSdk::PacketDeleter> packet(
    packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true));
  if(!packet)throw std::bad_alloc();
  for(unsigned textured=0;textured<2;++textured)for(unsigned clip=0;clip<2;++clip)
  for(unsigned n:{3U,6U,66U,72U})for(unsigned first:{0U,3U})
  for(unsigned parity=0;parity<(clip?2U:1U);++parity) {
    bag.texture=textured?&texture:nullptr;
    auto* program=textured?(clip?clipTexture:cullTexture):(clip?clipPlain:cullPlain);
    auto body=RoadPackageSdk::compile(&bag,program,prim,epoch,bufferSize,first,n,clip,parity,4096);
    ++bodies;
    for(unsigned mask=0;mask<(clip?64U:1U);++mask)for(unsigned start=0;start<2;++start) {
      Tyra::StaPipBagPackage package;
      package.bag=&bag;package.size=n;package.vertices=bag.vertices+first;
      package.colors=reinterpret_cast<const Tyra::Vec4*>(colors.many+first);
      package.sts=textured?texture.coordinates+first:nullptr;package.normals=nullptr;
      package.clipPlaneMask=mask;
      Tyra::StaPipQBuffer buffer;buffer.setMaxVertCount(n);buffer.fillByPointer(package);
      buffer.stripped=!clip;buffer.nativeClipStrip=clip;buffer.nativeClipParity=parity;
      packet2_reset(packet.get(),true);prim_t primitive=prim;primitive.type=PRIM_TRIANGLE;
      program->addBufferDataToPacket(packet.get(),&buffer,&primitive,bool(start));
      if(start)packet2_utils_vu_add_start_program(packet.get(),program->getDestinationAddress());
      else packet2_utils_vu_add_continue_program(packet.get());
      const auto expected=WorldBody::flatten(packet->base,packet2_get_qw_count(packet.get()),resolver);
      const auto head=RoadPackageSdk::liveHead(body,mask,start);
      std::vector<Quad> actual(head.begin(),head.end());
      actual.insert(actual.end(),body.geometry.begin(),body.geometry.end());actual.push_back(body.kicks[start]);
      if(expected!=actual)throw std::runtime_error("road package split differs from actual SDK");
      const unsigned effective=clip ? RoadPackageSdk::reverseMask(mask?mask:63)<<10 :
        (program->emitsStateFlag(start)?VU1_STAPIP_EMIT_STATE_FLAG:0);
      if(actual[1].words[3]!=(n|effective) ||
         ((actual[2].words[1]>>15)&7U)!=(clip?PRIM_TRIANGLE:PRIM_TRIANGLE_STRIP) ||
         (clip && actual[2].words[3]!=(2U|parity)))
        throw std::runtime_error("road package independent header decode");
      for(unsigned s=0;s<(textured?3U:2U);++s) {
        const unsigned at=3+s*(n+1),logical=s==0?0:(!textured || s==2?1:2);
        const Quad unpack{{0,0,0x01000101,0x6c00c000|(n<<16)|(2+s*n)}};
        if(!(actual[at]==unpack) || std::memcmp(actual.data()+at+1,raw[logical].data()+first,n*16))
          throw std::runtime_error("road package independent source decode");
      }
      ++checks;
    }
    auto reject=[&](auto action) {
      bool caught=false;try{action();}catch(const std::exception&){caught=true;}
      if(!caught)throw std::runtime_error("road package bad binding accepted");++controls;
    };
    reject([&]{RoadPackageSdk::liveHead(body,64,true);});
    reject([&]{RoadPackageSdk::compile(&bag,program,prim,0,bufferSize,first,n,clip,parity,4096);});
    reject([&]{RoadPackageSdk::compile(&bag,program,prim,epoch,bufferSize,first,n,clip,parity,1);});
    reject([&]{RoadPackageSdk::compile(&bag,program,prim,epoch,bufferSize,155,n,clip,parity,4096);});
    auto snapshot=body.geometry;
    const auto old=raw[0][first];raw[0][first].words[0]^=0x100;
    if(body.geometry!=snapshot)throw std::runtime_error("road body borrowed producer data");
    raw[0][first]=old;++owned;
  }
  done=true;
  std::printf("ROADSDKDONE bodies=%u sdkComparisons=%u controls=%u owned=%u masks=64 nativeExecuted=1 gpuExecuted=0 performance=0\n",bodies,checks,controls,owned);
  std::fflush(stdout);
}
}
