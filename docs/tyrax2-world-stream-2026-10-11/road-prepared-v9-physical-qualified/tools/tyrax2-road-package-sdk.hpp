#pragma once
#include "debug/world_cell_sdk.hpp"
#include "renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h"

// Cold SDK compiler. This owns bytes, but grants no DMA/texture lease.
namespace RoadPackageSdk {
using Quad=WorldCellPlan::Quad;
struct Body {
  std::array<std::array<Quad,3>,2> heads;
  std::vector<Quad> geometry;
  std::array<Quad,2> kicks;
  unsigned count=0,address=0;
  std::uint64_t epoch=0;
  bool clip=false,textured=false;
};
inline unsigned reverseMask(unsigned mask) {
  return ((mask&1)<<5)|((mask&2)<<3)|((mask&4)<<1)|
         ((mask&8)>>1)|((mask&16)>>3)|((mask&32)>>5);
}
inline std::array<Quad,3> liveHead(const Body& body,unsigned mask,bool start) {
  if(mask>63 || !body.epoch || !body.count)
    throw std::invalid_argument("road package live header binding");
  auto head=body.heads[start];
  if(body.clip) {
    const unsigned field=63U<<VU1_STAPIP_CLIP_MASK_SHIFT;
    head[1].words[3]=(head[1].words[3]&~field)|
      (reverseMask(mask?mask:63)<<VU1_STAPIP_CLIP_MASK_SHIFT);
  } else if(mask)throw std::invalid_argument("road cull package has clip mask");
  return head;
}
inline Body compile(Tyra::StaPipBag* bag,Tyra::StaPipVU1Program* program,
                    prim_t primitive,std::uint64_t epoch,unsigned bufferSize,
                    unsigned first,unsigned count,bool nativeClip,unsigned parity,
                    unsigned budget) {
  if(!bag || !program || !epoch || !budget || !bag->vertices || !bag->color ||
     !bag->color->many || bag->color->single || bag->lighting || bag->billboard ||
     !count || count<3 || count>bag->count || first>bag->count-count || parity>1 ||
     (!nativeClip && parity) || bufferSize<=9 ||
     (bag->texture && (!bag->texture->texture || !bag->texture->coordinates ||
                       bag->texture->coordinatesAreNormals)))
    throw std::invalid_argument("road package source binding");
  const bool textured=bag->texture!=nullptr;
  const auto name=program->getName();
  if(name!=(nativeClip ? (textured?Tyra::StaPipClipTextureColor:Tyra::StaPipClipColor) :
                        (textured?Tyra::StaPipCullTextureColor:Tyra::StaPipCullColor)) ||
     count>program->getMaxVertCount(false,bufferSize) || (nativeClip && count>72))
    throw std::invalid_argument("road package program/capacity binding");
  Tyra::StaPipBagPackage package;
  package.bag=bag;package.size=count;package.vertices=bag->vertices+first;
  package.colors=reinterpret_cast<const Tyra::Vec4*>(bag->color->many+first);
  package.sts=textured?bag->texture->coordinates+first:nullptr;
  package.normals=nullptr;package.clipPlaneMask=63;
  Tyra::StaPipQBuffer buffer;buffer.setMaxVertCount(count);buffer.fillByPointer(package);
  buffer.stripped=!nativeClip;buffer.nativeClipStrip=nativeClip;buffer.nativeClipParity=parity;
  primitive.type=PRIM_TRIANGLE;
  std::unique_ptr<packet2_t,WorldCellSdk::PacketDeleter> packet(
    packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true));
  if(!packet)throw std::bad_alloc();
  auto resolver=[](unsigned address,unsigned n,void*)->const Quad* {
    if(!n || n>65535 || address<0x100000 || (address&15) || address>0x02000000U-n*16)
      return nullptr;
    return reinterpret_cast<const Quad*>(address);
  };
  Body body;body.count=count;body.address=program->getDestinationAddress();
  body.epoch=epoch;body.clip=nativeClip;body.textured=textured;
  for(unsigned start=0;start<2;++start) {
    packet2_reset(packet.get(),true);
    program->addBufferDataToPacket(packet.get(),&buffer,&primitive,bool(start));
    if(start)packet2_utils_vu_add_start_program(packet.get(),body.address);
    else packet2_utils_vu_add_continue_program(packet.get());
    auto flat=WorldBody::flatten(packet->base,packet2_get_qw_count(packet.get()),resolver);
    if(flat.size()!=4+(textured?3U:2U)*(count+1) || flat.size()>budget ||
       !(flat[0]==Quad{{0,0,0x01000101,0x6c02c000}}))
      throw std::invalid_argument("road package SDK shape/budget");
    std::copy_n(flat.begin(),3,body.heads[start].begin());body.kicks[start]=flat.back();
    std::vector<Quad> geometry(flat.begin()+3,flat.end()-1);
    if(!start)body.geometry=std::move(geometry);
    else if(geometry!=body.geometry)
      throw std::invalid_argument("road package state changes geometry");
    const Quad kick{{0,0,0x11000000,start?(0x14000000|body.address):0x17000000}};
    if(!(body.kicks[start]==kick))throw std::invalid_argument("road package SDK kick");
  }
  return body;
}
}
