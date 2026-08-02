#pragma once

#include <vector>

#include "project.hpp"

// Shared, GL-agnostic endless-scroller belt math. This is the single host
// source of truth for how a Scroller (PrimitiveType::Scroller) lays its
// segments along the belt, how many clones it needs and where each clone sits
// at a given scroll distance. The editor viewport uses it to draw the live
// scrolling "ghost" preview, and templates.cpp uses it at build time to bake
// clone rest positions and the SCROLLERS / SCROLLER_CLONES side tables.
//
// Every query takes the scene's object list (member names resolve within it)
// plus the scroller object. The PS2 runtime (ScrollerDirector in
// scroller.gen.cpp) recomputes the same per-frame wrap every frame; the only
// formula duplicated there is wrapU + the visibility test below - keep the
// generated twin in sync (grep wrapU).
namespace scrollsim {

// Belt runs along the scroller's local +Z, rotated by its Euler `rotation`
// (degrees, X then Y then Z - the same order viewport::modelMatrix and the
// generated geometry builder use). Output is a unit vector.
void beltAxis(const float rotationDeg[3], float outAxis[3]);

// One resolved member of a segment: where its template lives in the scene, and
// which SLOT of ScrollSegment::objects named it (the slot is what keys the
// per-cell variation hash, so a dangling name must not shift the ones after it).
struct MemberRef {
    int object;  // index into `objects`
    int slot;    // index into seg.objects
};

// Members named by a segment, dangling names skipped. Order follows
// ScrollSegment::objects.
std::vector<MemberRef> segmentMembers(const std::vector<SceneObject>& objects,
                                      const ScrollSegment& seg);

// One flag per object: is it a member TEMPLATE of some Scroller in this scene?
//
// The game DEACTIVATES every such object (SCROLLER_HIDDEN) the moment the
// director's first update runs and draws sliding clones instead - so an
// authored member is a piece of authoring, not a piece of the world. Anything
// that bakes the scene as authored has to leave it out, or it contributes
// something nothing on screen accounts for: aobake burned the members' contact
// shadow into the terrain AO map and the lightmap atlas, which put a permanent
// dark patch at the belt origin cast by objects the player can never see. The
// clones are not eligible either - they move every frame, and a baked shadow
// cannot follow them - so a belt simply casts no baked light or shadow.
std::vector<char> memberTemplateFlags(const std::vector<SceneObject>& objects);

// Effective belt length of a segment: the authored `length` when > 0, else
// auto-measured from the members' extent projected on the belt axis (with a
// small floor so a zero-extent chunk still advances).
float segmentLength(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller, const ScrollSegment& seg);

// Sum of every segment's effective length (the belt's repeat period). Always
// >= a small floor so wrapping never divides by zero.
float patternLength(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller);

// Cumulative belt offset where each segment starts within one pattern period
// (baseOffsets[0] == 0). Size == scrollSegments.size().
std::vector<float> baseOffsets(const std::vector<SceneObject>& objects,
                               const SceneObject& scroller);

inline float windowMin(const SceneObject& s) { return -s.scrollBehind; }
inline float windowMax(const SceneObject& s) { return s.scrollAhead; }

// Total member objects in one pattern period (sum over segments). The per-cell
// baking cost.
int membersPerPattern(const std::vector<SceneObject>& objects,
                      const SceneObject& scroller);

// Copies of each segment the window geometry alone wants: ceil((ahead+behind)
// / patternLength) + 2 (>= 1). Before the scrollMaxClones cap.
int geometricCells(const std::vector<SceneObject>& objects,
                   const SceneObject& scroller);

// Copies per segment actually baked/previewed after clamping to scrollMaxClones
// (>= 1). This is what everyone should use - the editor preview, the baked
// clones and the generated director all tile with the SAME cell count so they
// stay in sync. Equals geometricCells unless the cap bit.
int cellsPerSegment(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller);

// True when the scrollMaxClones cap reduced the cell count below what the
// window wants (the belt will show a gap / recycle visibly). For the warning.
bool cloneCapped(const std::vector<SceneObject>& objects,
                 const SceneObject& scroller);

// The recycling wrap: bring a nominal belt offset into [wmin, wmin + span).
// span = cellsPerSegment * patternLength. Duplicated verbatim in the generated
// ScrollerDirector - keep in sync.
float wrapU(float nominal, float wmin, float span);

// One placed segment instance on the belt at a given scroll distance.
struct Placement {
    int segment;    // index into scroller.scrollSegments
    int copy;       // 0 .. cellsPerSegment-1
    float u;        // wrapped belt coordinate of the segment's start
    float phase;    // baseOffset[segment] + copy*patternLength (scroll-invariant)
    float segLen;   // this segment's effective length
    bool visible;   // the cell [u, u+segLen) overlaps [windowMin, windowMax]
    // Which repetition of the pattern this instance currently IS, counted along
    // the infinite belt: it holds still while the instance is on screen and
    // advances by cellsPerSegment every time the instance recycles to the
    // front. This integer is what makes an endless belt able to stop repeating
    // - it keeps growing even though `u` (and the runtime's folded scroll
    // accumulator) stay bounded, so it is the one input the per-cell variation
    // below hashes on. Derived from the UNFOLDED scroll distance.
    int cell;
};

// Every (segment, copy) placement at scroll distance `beltScroll` (belt units
// the content has advanced; = speed * elapsed seconds). Deterministic - the
// editor and the baked rest layout (beltScroll = 0) both read from here.
std::vector<Placement> placements(const std::vector<SceneObject>& objects,
                                  const SceneObject& scroller, float beltScroll);

// Total baked clones = sum over segments of cellsPerSegment * memberCount,
// AFTER the scrollMaxClones cap. For the editor cost readout and warnings.
int cloneCount(const std::vector<SceneObject>& objects, const SceneObject& scroller);

// Anti-z-fighting: a clone's scale with the member's belt-most local axis
// stretched by scroller.scrollOverlap, so consecutive pieces interpenetrate
// slightly instead of butting up with exactly coplanar end faces (which
// flicker on the GS and in GL alike). Picks the member local axis (after its
// rotation) most aligned with the belt and adds the overlap to that scale
// component. Used by the clone bake (templates.cpp) AND the viewport ghost
// preview so both show identical geometry.
void seamScale(const SceneObject& member, const float beltAxis[3], float overlap,
               float outScale[3]);

// The belt's horizontal perpendicular: the direction a member's per-cell
// lateral offset moves along. cross((0,1,0), axis), or +X for a vertical belt.
void sideAxis(const float beltAxis[3], float outSide[3]);

// ---------------------------------------------------------------------------
// Per-cell variation (docs/endless-scroller.md).
//
// A belt tiles a fixed pattern, so on its own it repeats with period
// cellsPerSegment*patternLength - and a player watching it for a minute sees
// that. The variation layer breaks the repeat without adding a single triangle:
// every member of every CELL (Placement::cell) resolves its own presence, yaw,
// lateral offset and scale from a hash of (belt seed, cell, member slot). No
// state, no generation, no allocation - just four hashes per clone per frame,
// and because `cell` counts along the infinite belt rather than around the
// pattern, the stream never comes back around.
//
// EVERYTHING BELOW HAS A TWIN in the generated ScrollerDirector
// (scroller.gen.cpp, templates.cpp) - the console must resolve the same cell to
// the same look the editor previewed. Change one, change both.
// ---------------------------------------------------------------------------

// 32-bit mixer. The channel separates the four draws a member makes per cell.
unsigned varyHash(int seed, int cell, unsigned key, int channel);
// varyHash mapped to [0,1).
float varyRand(int seed, int cell, unsigned key, int channel);

// Hash key of one member slot. Slot-based, not name-based: renaming a member
// must not reshuffle the world, while inserting one deliberately does.
inline unsigned memberKey(int segment, int slot) {
    return (unsigned)(segment + 1) * 0x9E3779B1u + (unsigned)(slot + 1) * 0x85EBCA77u;
}

// The variation parameters of one member slot, with its variant group already
// resolved against the rest of its segment. Baked into SCROLLER_CLONES so the
// runtime needs no segment list of its own.
struct MemberVary {
    unsigned key = 0;         // memberKey(segment, slot)
    float chance = 1.0f;      // fraction of cells this member shows in
    int variantIndex = 0;     // position within its variant group
    int variantCount = 0;     // group size; <= 1 = no group, `chance` decides
    unsigned variantKey = 0;  // hash key shared by the whole group
    float yawVary = 0.0f;     // +- degrees
    float offsetVary = 0.0f;  // +- world units along sideAxis
    float scaleVary = 0.0f;   // +- fraction of the authored scale
};

MemberVary memberVary(const ScrollSegment& seg, int segmentIndex, int slot);

// What one member looks like in one cell. Twin: the generated director.
struct CellAdjust {
    bool visible = true;
    float yaw = 0.0f;     // degrees to ADD to the member's authored Y rotation
    float offset = 0.0f;  // world units along sideAxis
    float scale = 1.0f;   // multiplier on the member's authored scale
};

CellAdjust cellAdjust(int varySeed, int cell, const MemberVary& v);

// True when any member of any segment asks for variation. The editor uses it
// for the cost readout; codegen uses it to skip emitting the variation pass.
bool hasVariation(const SceneObject& scroller);

// One member instance placed on the belt: everything the viewport ghost pass
// and the clone bake need, with the seam overlap and this cell's variation
// already folded in. `pl` comes from placements().
struct MemberInstance {
    int object;         // index into `objects` (the authored template)
    int slot;           // its slot in the segment
    bool visible;       // placement visibility AND this cell's variation
    float position[3];
    float rotation[3];
    float scale[3];
};

std::vector<MemberInstance> memberInstances(const std::vector<SceneObject>& objects,
                                            const SceneObject& scroller,
                                            const Placement& pl);

}  // namespace scrollsim
