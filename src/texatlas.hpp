#pragma once

#include <string>
#include <vector>

#include "project.hpp"

// texatlas: build-time texture atlasing (docs/texture-atlasing.md). Small
// map_Kd textures are packed into shared 256x256 pages so the GS keeps one
// allocation (+one ~8 KB overhead) per page instead of one per texture, and
// draw batches switch textures less. Host-only; this module computes the
// deterministic PLAN - the single source of truth consumed by texbake (which
// composites the pages and rewrites the baked .mtl files) and by codegen
// (the boot-log line). The plan is a pure function of the project + the
// res/ files on disk, so both consumers agree within one build.
//
// Eligibility is conservative - a texture joins a page only when every
// consumer samples it with plain 0..1 UVs:
//   - referenced as map_Kd (same-directory token) by materials of static
//     .obj models whose textured submesh UVs stay within 0..1, or by
//     primitive-shaped objects (their generated UVs are 0..1 by
//     construction);
//   - NOT referenced by terrain (tiled UVs), emitters (VU1-fixed corner
//     UVs), decals/mirrors/portals (separate ST paths), or as a refl sphere
//     map (runtime-computed STs);
//   - baked size <= 128x128 (bigger shares nothing on a 256 page);
//   - no per-asset textureQuality override on any consumer (pinned quality
//     is deliberate; pages re-quantize as one image);
//   - the .mtl has no tiling factor (map_Kd -s).
//
// Pages are grouped by the .mtl's directory (the map_Kd resolution base),
// so the rewritten reference stays a same-directory token - no ".." paths
// over the PS2 host filesystem. Members keep a 2-texel edge-dilated gutter
// against bilinear bleed.
namespace texatlas {

// Why a texture is NOT on a page. Every rejection is recorded so the
// editor's Texture Atlas window can answer "why is this not atlased?" -
// the question the feature used to answer only by silence (an entire
// example project atlased nothing because its map_Kd tokens carried a
// subdirectory).
struct Excluded {
    std::string resRel;  // best-effort res-relative path (or the raw token)
    std::string reason;  // one short human sentence
};

struct Entry {
    std::string resRel;   // member: res-relative PNG ("res/models/x.png")
    std::string pageRel;  // its page: res-relative baked-only PNG
                          // ("res/models/tyra-atlas-0.png")
    int page = 0;         // global page index
    int x = 0, y = 0;     // placement in page pixels (gutter excluded)
    int w = 0, h = 0;     // baked size on the page
    float u0 = 0.0f, v0 = 0.0f, du = 1.0f, dv = 1.0f;  // consumer UV rect
};

struct Plan {
    int pageSize = 256;
    std::vector<Entry> entries;      // sorted deterministically
    std::vector<std::string> pages;      // res-relative page paths, by index
    std::vector<std::string> pageGroup;  // per page: the group it packs
    std::vector<Excluded> excluded;      // rejected candidates + why
    // page quantization: false = palettized 256-color page (the shared-CLUT
    // trade of the era), true = full color (project quantization is "none")
    bool fullColor = false;
    const Entry* find(const std::string& resRel) const {
        for (const Entry& e : entries)
            if (e.resRel == resRel) return &e;
        return nullptr;
    }
    bool empty() const { return entries.empty(); }
    // The group a page packs ("" when the plan carries no group info).
    const std::string& groupOf(int page) const {
        static const std::string none;
        return page >= 0 && page < (int)pageGroup.size() ? pageGroup[page]
                                                         : none;
    }
};

// Computes the plan. Empty when ProjectSettings::textureAtlas is off or
// fewer than two textures qualify.
Plan plan(const Project& p);

// "Texture atlas: N textures in M pages" ("" for an empty plan) - the boot
// log line codegen bakes into the game.
std::string info(const Plan& plan);

// What the plan does to GS VRAM, in KB, for the textures it touches: what
// its members would cost as individual allocations against what their pages
// cost. This is the number the feature is SOLD on, so it is computed rather
// than asserted - and it is not always a saving: a 4-bit project's members
// are 4 bpp each while a shared page is quantized to 256 colours, i.e. 8 bpp,
// so packing many tiny textures can cost more than it saves. The editor's
// Texture Atlas window and --atlas-report print both sides.
struct VramEstimate {
    int membersKb = 0;  // the members, unpacked, at the project's own depth
    int pagesKb = 0;    // their pages
    int savedKb = 0;    // membersKb - pagesKb (negative = the atlas costs)
};
VramEstimate vram(const Plan& plan, const Project& p);

}  // namespace texatlas
