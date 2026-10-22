#pragma once
#ifdef WORLD_CELL_ENGINE
#include "debug/world_cell_sdk.hpp"
#else
#include "tyrax2-world-cell-plan.hpp"
#define WORLD_BODY_HOST
#include "tyrax2-world-body.hpp"
#undef WORLD_BODY_HOST
#endif

// Source ranges live only during compilation; queued DMA owns the final bytes.
namespace WorldCellDirect {
using WorldCellPlan::PassSource;
using WorldCellPlan::Topology;
using Quad = WorldCellPlan::Quad;
struct Range { unsigned first, count, parity; };

template<class Emit>
inline void partitions(const PassSource& s, Emit emit) {
  if (!s.count || s.capacity < 3 || s.capacity > 72 || !s.material.generation)
    throw std::invalid_argument("A44 direct incomplete source");
  for (unsigned k=0; k<(s.textured ? 3U : 2U); ++k)
    if (!s.streams[k].data || s.streams[k].count < s.count)
      throw std::invalid_argument("A44 direct short stream");
  if (s.topology == Topology::Triangles) {
    if (s.count%3 || s.run) throw std::invalid_argument("A44 direct list shape");
    const unsigned step=s.capacity/3*3;
    for (unsigned at=0; at<s.count;) {
      const unsigned count=std::min<std::uint32_t>(step,s.count-at);
      emit(Range{at,count,0}); at+=count;
    }
  } else if (s.topology == Topology::IndependentStrips) {
    if (s.run<3 || s.run%3 || (s.count%s.run && s.count%s.run<3))
      throw std::invalid_argument("A44 direct strip shape");
    for (unsigned base=0; base<s.count;) {
      const unsigned count=std::min<std::uint32_t>(s.run,s.count-base);
      for (unsigned at=0; at<count-2;) {
        const unsigned triangles=std::min<std::uint32_t>(s.capacity-2,count-2-at);
        emit(Range{base+at,triangles+2,at&1}); at+=triangles;
      }
      base+=count;
    }
  } else throw std::invalid_argument("A44 direct unknown topology");
}

inline unsigned geometryQwords(const PassSource& source) {
  std::uint64_t words=0;
  partitions(source,[&](Range r){
    words+=4+(source.textured ? 3U : 2U)*(r.count+1);
    if (words>65535) throw std::length_error("A44 direct geometry too large");
  });
  return static_cast<unsigned>(words);
}

// Same accepted CNT/REF format as the preserved flatten oracle, appended once.
inline void appendFragment(std::vector<Quad>& out, const void* chain, unsigned count,
                           WorldBody::Resolver resolve, unsigned limit,
                           void* context=nullptr) {
  if (!chain || !count || count>65535 || !limit || limit>65535 || out.size()>limit)
    throw std::invalid_argument("A44 direct fragment budget");
  const auto original=out.size();
  const auto* bytes=static_cast<const unsigned char*>(chain);
  try {
    for (unsigned at=0; at<count;) {
      Quad tag; std::memcpy(&tag,bytes+16*at++,sizeof(tag));
      const unsigned size=tag.words[0]&65535, id=(tag.words[0]>>28)&7;
      if ((tag.words[0]&0x8fff0000U) || (tag.words[1]&0x8000000fU))
        throw std::invalid_argument("A44 direct tag flags");
      const Quad* payload=nullptr;
      if (id==1) {
        if (tag.words[1] || size>count-at)
          throw std::invalid_argument("A44 direct inline payload");
        payload=reinterpret_cast<const Quad*>(bytes+16*at); at+=size;
      } else if (id==3) {
        if (size && (!resolve || !(payload=resolve(tag.words[1],size,context))))
          throw std::invalid_argument("A44 direct unreadable REF");
      } else throw std::invalid_argument("A44 direct unsupported tag");
      if (size+1>limit-out.size()) throw std::length_error("A44 direct body budget");
      out.push_back(Quad{{0,0,tag.words[2],tag.words[3]}});
      const auto offset=out.size(); out.resize(offset+size);
      if (size) std::memcpy(out.data()+offset,payload,size*sizeof(Quad));
    }
  } catch (...) { out.resize(original); throw; }
}

#ifdef WORLD_CELL_ENGINE
inline void appendSdk(const PassSource& source, const WorldCellSdk::Binding& binding,
                      std::vector<Quad>& body, unsigned budget) {
  if (!binding.bag || source.capacity!=WorldCellSdk::capacity(binding,source.topology) ||
      source.textured!=(binding.bag->texture!=nullptr) ||
      binding.program->getName()!=(source.textured ? Tyra::StaPipClipTextureColor : Tyra::StaPipClipColor))
    throw std::invalid_argument("A44 direct SDK binding");
  std::unique_ptr<packet2_t,WorldCellSdk::PacketDeleter> packet(
      packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true));
  if (!packet) throw std::bad_alloc();
  auto resolver=[](unsigned address,unsigned count,void*)->const Quad* {
    if (!count || count>65535 || address<0x100000 || (address&15) ||
        address>0x02000000U-count*16) return nullptr;
    return reinterpret_cast<const Quad*>(address);
  };
  prim_t primitive=binding.prim; primitive.type=PRIM_TRIANGLE;
  bool first=true;
  partitions(source,[&](Range r){
    Tyra::StaPipBagPackage package;
    package.bag=binding.bag; package.size=static_cast<u16>(r.count);
    package.vertices=reinterpret_cast<const Tyra::Vec4*>(source.streams[0].data+r.first);
    package.colors=reinterpret_cast<const Tyra::Vec4*>(source.streams[1].data+r.first);
    package.sts=source.textured ? reinterpret_cast<const Tyra::Vec4*>(source.streams[2].data+r.first) : nullptr;
    package.normals=nullptr; package.clipPlaneMask=63;
    Tyra::StaPipQBuffer buffer;
    buffer.setMaxVertCount(r.count); buffer.fillByPointer(package);
    buffer.stripped=false; buffer.nativeClipStrip=source.topology==Topology::IndependentStrips;
    buffer.nativeClipParity=static_cast<u8>(r.parity);
    packet2_reset(packet.get(),true);
    binding.program->addBufferDataToPacket(packet.get(),&buffer,&primitive,first);
    if (first) packet2_utils_vu_add_start_program(packet.get(),binding.program->getDestinationAddress());
    else packet2_utils_vu_add_continue_program(packet.get());
    const auto before=body.size();
    appendFragment(body,packet->base,packet2_get_qw_count(packet.get()),resolver,budget);
    if (body.size()-before!=4+(source.textured ? 3U : 2U)*(r.count+1))
      throw std::invalid_argument("A44 direct SDK output shape");
    first=false;
  });
}
#endif
}
