#pragma once
#include "debug/world_runtime.hpp"
#include "renderer/3d/pipeline/static/core/stapip_program_name.hpp"
#include <draw_types.h>

namespace WorldCellComplete {
inline bool active=false;
inline unsigned requested=0,submitted=0,fallback=0;
struct Cell {
 WorldRuntime::Attachment owner;
  WorldOwner::Key key;
  u64 textureEpoch=0;
  bool fogEnabled=false;
  Tyra::StaPipProgramName lastProgram=Tyra::StaPipClipColor;
  texwrap_t firstWrap{};
  texwrap_t lastWrap{};
  u32 wrapPrefixQwords=0;
};
}
