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

// Object indices (into `objects`) named by a segment, dangling names skipped.
// Order follows ScrollSegment::objects.
std::vector<int> segmentMembers(const std::vector<SceneObject>& objects,
                                const ScrollSegment& seg);

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

}  // namespace scrollsim
