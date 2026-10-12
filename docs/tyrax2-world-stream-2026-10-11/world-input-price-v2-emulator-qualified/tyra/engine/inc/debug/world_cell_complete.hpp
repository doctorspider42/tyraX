#pragma once
#include "debug/world_runtime.hpp"
#include "renderer/3d/pipeline/static/core/stapip_program_name.hpp"
#include <draw_types.h>
#include <draw_sampling.h>

namespace WorldCellComplete {
inline bool active=false;
inline bool directCold=false,directAudit=false;
inline unsigned directCalls=0,materializedCalls=0,audits=0;
inline unsigned requested=0,submitted=0,fallback=0;
struct Cell {
 WorldRuntime::Attachment owner;
  WorldOwner::Key key;
  u64 textureEpoch=0;
  bool fogEnabled=false,compactInputs=false;
  Tyra::StaPipProgramName lastProgram=Tyra::StaPipClipColor;
  texwrap_t firstWrap{};
  texwrap_t lastWrap{};
  u32 wrapPrefixQwords=0;
};
}
