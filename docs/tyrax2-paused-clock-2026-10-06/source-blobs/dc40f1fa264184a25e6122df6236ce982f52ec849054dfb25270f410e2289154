/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: ONE source of truth for the GS depth range.
*/

#include "renderer/core/gs/renderer_core_depth.hpp"

namespace Tyra {

// 24 bits by default: the packed XYZF2 vertex format's Z field is that wide,
// whatever the z buffer holds.
u32 RendererCoreDepth::maxZ = 0xFFFFFF;
float RendererCoreDepth::scale = static_cast<float>(0xFFFFFF) / 2.0F;
int RendererCoreDepth::bits = 24;

void RendererCoreDepth::setBits(int t_bits) {
  bits = t_bits;
  maxZ = (t_bits >= 32) ? 0xFFFFFFFFu : ((1u << t_bits) - 1u);
  scale = static_cast<float>(maxZ) / 2.0F;
}

}  // namespace Tyra
