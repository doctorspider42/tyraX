/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Added by TyraX: ONE source of truth for the GS depth range.
*/

#pragma once

#include <tamtypes.h>

namespace Tyra {

/**
 * The GS depth range the vertex path targets - the number four places used to
 * hardcode and one of them would inevitably drift from the others.
 *
 * WHY IT IS A RANGE AND NOT THE BUFFER'S BIT DEPTH. Vertices are sent as
 * packed XYZF2 (for GS hardware fog), whose Z field is **24 bits** - so a
 * PSMZ32 z buffer can never be filled past 24 and its top byte is dead. The
 * scale is therefore 24-bit by default, not 32.
 *
 * WHY IT IS NOT A CONSTANT EITHER. A colour buffer and the z buffer it is
 * tested against must share PAGE GEOMETRY on real hardware (32/24-bit pages
 * are 64x32 pixels, 16-bit ones 64x64), so a PSMCT16 framebuffer forces a
 * PSMZ16 z - and the GS stores a 16-bit z from the LOW bits of the vertex's
 * Z field, so the vertex path has to scale to 16 bits or the value wraps.
 * Sending 24-bit Z into a 16-bit buffer is exactly what makes models read
 * INSIDE-OUT (back faces winning the depth test), which is how this was
 * found.
 *
 * The cost of 16 bits is precision, and it is worth stating in units: the
 * world-space step at distance d is d^2 / (maxZ * near), so at near 0.1 a
 * 24-bit range resolves 0.006 units at d=100 and a 16-bit one 1.5 - which is
 * why a 16-bit project must also raise its near plane (docs/gs-vram.md).
 *
 * Set ONCE by RendererCoreGS when it decides the buffer formats; read by the
 * StaPip and DynPip packet builders, the post-fx depth-of-field solve and the
 * generated game's portal mask.
 */
struct RendererCoreDepth {
  /** 2^bits - 1: the largest z the vertex path may produce. */
  static u32 maxZ;

  /** maxZ / 2 - the VU1 scale that maps NDC -1..1 onto 0..maxZ. */
  static float scale;

  /** 24 (a 32/24-bit z buffer) or 16 (a 16-bit one). */
  static int bits;

  /** Called by RendererCoreGS::allocateVramBuffers once the formats are
   * known. Anything cached from `scale` before that point is wrong. */
  static void setBits(int t_bits);
};

}  // namespace Tyra
