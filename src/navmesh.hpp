#pragma once

#include <cstdint>
#include <vector>

#include "project.hpp"

// NavMesh bake: a walkable-cell grid derived from the terrain heightmap and
// the blocking scene objects. PURE HOST-SIDE, like decalproj: it runs in the
// editor (viewport overlay) and in codegen (templates.cpp bakes the bitmap
// into inc/nav_data.gen.hpp). The PS2 never bakes anything - it only runs A*
// over the finished bitmap on the EE, and only in scenes whose flow graphs
// use the AI nodes. In 2002 this is the part everyone wrote by hand.
//
// Walkability of a cell (center-sampled):
//  - inside the playable bounds (the game clamps walkers to size/2 - 1),
//  - terrain slope <= navMaxSlope (gradient of the same bilinear heightmap
//    the game samples in terrainHeightAtScene),
//  - no blocking object over it. Blockers mirror collidePlayer's box mode:
//    collidable geometry types with collision != none, as an axis-aligned
//    box (models: the mesh AABB; the game ignores rotation in box mode, so
//    the bake does too), inflated by navAgentRadius in XZ. An object low
//    enough to step onto (top <= ground + 0.5) or high enough to walk under
//    (bottom >= ground + 1.8) does not block. Mesh-collision objects
//    (collision == 1: ramps/stairs) never block - that mode exists to be
//    walked on.
namespace navmesh {

// Nav grids are capped at kMaxCellsPerAxis^2 cells so the PS2-side A*
// working arrays stay small and static; larger maps get bigger cells.
constexpr int kMaxCellsPerAxis = 128;
constexpr float kAgentHeight = 1.8f;  // walk-corridor height (player eye)
constexpr float kStepHeight = 0.5f;   // matches collidePlayer's step-onto rule

struct NavGrid {
    int w = 0, d = 0;                 // cells along X / Z
    float originX = 0.0f, originZ = 0.0f;  // world corner of cell (0, 0)
    float cellW = 0.0f, cellD = 0.0f;      // world units per cell
    std::vector<uint8_t> walkable;    // w * d, row-major (z * w + x), 1 = walkable

    bool at(int x, int z) const {
        return x >= 0 && x < w && z >= 0 && z < d && walkable[z * w + x] != 0;
    }
};

// Bakes the grid for one scene using the project's nav preferences
// (ProjectSettings::navCellSize / navMaxSlope / navAgentRadius). Model AABBs
// come from the .obj parser; animated .glb objects fall back to their unit
// box (scale) - see docs/navigation-ai.md.
NavGrid bake(const Project& p, const SceneData& s);

}  // namespace navmesh
