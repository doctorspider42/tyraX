/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Added by TyraX: process-wide unique id source (see comment below)
*/

#pragma once

#include <tamtypes.h>

namespace Tyra {

// Modified by TyraX: replaces the former `rand() % 1000000` id scheme
// used by Sprite / Mesh / MeshFrame / MeshMaterial / MeshMaterialFrame.
//
// Those ids share ONE lookup namespace in TextureRepository: a texture is
// bound to a sprite or material by `addLink(id)` and resolved at draw time by
// the FIRST texture whose link set contains that id (getBySpriteId /
// getByMeshMaterialId). rand() is never seeded and draws from only 1e6 values,
// so two objects could be handed the same id; the sprite/material then drew
// with the wrong texture. Opening a game menu allocates a burst of new sprites
// at once, which made a collision with the always-present debug-HUD glyph
// likely - the reported "HUD garbles when the menu opens" bug.
//
// A monotonic counter hands out ids that are unique for the lifetime of the
// run. Every one of these objects is constructed on the EE main thread, so no
// synchronization is needed. Id 0 is skipped: the texture-buffer allocator
// uses a {0, ...} sentinel to mean "not uploaded".
inline u32 generateUniqueId() {
  static u32 next = 1;
  return next++;
}

}  // namespace Tyra
