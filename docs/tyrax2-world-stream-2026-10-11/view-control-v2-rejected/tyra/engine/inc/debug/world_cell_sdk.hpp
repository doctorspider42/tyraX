#pragma once
#ifdef WORLD_CELL_ENGINE
#include "debug/world_cell_plan.hpp"
#include "debug/world_body.hpp"
#else
#include "tyrax2-world-cell-plan.hpp"
#define WORLD_BODY_HOST
#include "tyrax2-world-body.hpp"
#undef WORLD_BODY_HOST
#endif
#include "renderer/3d/pipeline/static/core/stapip_vu1_program.hpp"

// Cold compiler only. Live view/material heads and DMA/GS leases are external.
namespace WorldCellSdk {
using Quad = WorldCellPlan::Quad;
struct Binding {
  Tyra::StaPipBag* bag;
  Tyra::StaPipVU1Program* program;
  prim_t prim;
  WorldCellPlan::MaterialEpoch material;
  std::uint64_t programEpoch;
  u16 vuBufferSize;
  bool nativeStripImage;
};
struct Pass {
  WorldCellPlan::MaterialEpoch material;
  std::uint64_t programEpoch;
  u32 programAddress;
  std::vector<Quad> commands;
};
struct Cell {
  std::uint64_t generation;
  std::vector<Pass> passes;
};
struct PacketDeleter {
  void operator()(packet2_t* packet) const { packet2_free(packet); }
};

inline std::uint32_t capacity(const Binding& binding,
                             WorldCellPlan::Topology topology) {
  if (!binding.program || !binding.programEpoch || binding.vuBufferSize <= 9)
    throw std::invalid_argument("A44 absent program/buffer binding");
  const auto name = binding.program->getName();
  if (name != Tyra::StaPipClipColor && name != Tyra::StaPipClipTextureColor)
    throw std::invalid_argument("A44 unsupported cell shader");
  const auto maximum = binding.program->getMaxVertCount(false, binding.vuBufferSize);
  if (topology == WorldCellPlan::Topology::Triangles)
    return (maximum / 6 / 3) * 3;
  if (topology != WorldCellPlan::Topology::IndependentStrips ||
      !binding.nativeStripImage)
    throw std::invalid_argument("A44 native strip image is not bound");
  return std::min(u32(72), u32(maximum));
}

inline Cell compile(const WorldCellPlan::Cell& source,
                    const std::vector<Binding>& bindings,
                    std::uint64_t commandByteBudget) {
  if (!source.generation || source.passes.empty() ||
      bindings.size() != source.passes.size() || !commandByteBudget)
    throw std::invalid_argument("A44 missing cell SDK binding/budget");
  Cell result{source.generation,{}};
  std::uint64_t bytes = 0;
  std::unique_ptr<packet2_t,PacketDeleter> packet(
      packet2_create(64,P2_TYPE_NORMAL,P2_MODE_CHAIN,true));
  if (!packet) throw std::bad_alloc();
  auto resolver=[](unsigned address,unsigned count,void*)->const Quad* {
    if (!count || count>65535 || address<0x100000 || (address&15) ||
        address>0x02000000U-count*16) return nullptr;
    return reinterpret_cast<const Quad*>(address);
  };
  for (std::size_t i=0;i<source.passes.size();++i) {
    const auto& pass=source.passes[i];
    const auto& binding=bindings[i];
    auto* bag=binding.bag;
    if (!pass.material.generation || !bag || !bag->color || !bag->color->many || bag->color->single ||
        bag->lighting || bag->billboard ||
        pass.material.generation!=binding.material.generation ||
        pass.material.slot!=binding.material.slot ||
        pass.textured!=(bag->texture!=nullptr) ||
        (bag->texture && (!bag->texture->texture ||
                          bag->texture->coordinatesAreNormals)))
      throw std::invalid_argument("A44 incompatible cell pass binding");
    const auto limit=capacity(binding,pass.topology);
    if (limit<3 || pass.parts.empty() ||
        binding.program->getName()!=(pass.textured ? Tyra::StaPipClipTextureColor
                                                  : Tyra::StaPipClipColor))
      throw std::invalid_argument("A44 cell pass capacity/program mismatch");
    Pass output{pass.material,binding.programEpoch,
                binding.program->getDestinationAddress(),{}};
    prim_t primitive=binding.prim;
    primitive.type=PRIM_TRIANGLE;
    for (const auto& part:pass.parts) {
      const auto count=part.streams[0].size();
      if (count<3 || count>limit || part.parity>1 ||
          part.streams[1].size()!=count ||
          (pass.textured && part.streams[2].size()!=count) ||
          (pass.topology==WorldCellPlan::Topology::Triangles &&
           (count%3 || part.parity)))
        throw std::invalid_argument("A44 source partition exceeds SDK capacity");
      Tyra::StaPipBagPackage package;
      package.bag=bag;package.size=static_cast<u16>(count);
      package.vertices=reinterpret_cast<const Tyra::Vec4*>(part.streams[0].data());
      package.colors=reinterpret_cast<const Tyra::Vec4*>(part.streams[1].data());
      package.sts=pass.textured ?
          reinterpret_cast<const Tyra::Vec4*>(part.streams[2].data()) : nullptr;
      package.normals=nullptr;package.clipPlaneMask=63;
      Tyra::StaPipQBuffer buffer;
      buffer.setMaxVertCount(static_cast<u32>(count));
      buffer.fillByPointer(package);
      buffer.stripped=false;
      buffer.nativeClipStrip=pass.topology==WorldCellPlan::Topology::IndependentStrips;
      buffer.nativeClipParity=static_cast<u8>(part.parity);
      packet2_reset(packet.get(),true);
      const bool first=output.commands.empty();
      binding.program->addBufferDataToPacket(packet.get(),&buffer,&primitive,first);
      if (first) packet2_utils_vu_add_start_program(packet.get(),output.programAddress);
      else packet2_utils_vu_add_continue_program(packet.get());
      auto fragment=WorldBody::flatten(packet->base,packet2_get_qw_count(packet.get()),resolver);
      const auto added=std::uint64_t(fragment.size())*sizeof(Quad);
      if (added>commandByteBudget-bytes ||
          fragment.size()>65535-output.commands.size())
        throw std::length_error("A44 compiled command budget exceeded");
      output.commands.insert(output.commands.end(),fragment.begin(),fragment.end());
      bytes+=added;
    }
    result.passes.push_back(std::move(output));
  }
  return result;
}
}
