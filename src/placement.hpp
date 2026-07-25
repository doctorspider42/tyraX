#pragma once

#include <functional>
#include <vector>

#include "aobake.hpp"  // ModelAabbFn - the same model-bounds callback
#include "project.hpp"

// Collision-aware object placement (docs/object-placement.md). Host-only, no
// GL - the decalproj/navmesh pattern: pure functions over the scene model, so
// the editor UI, the paste preview and any future batch tool all place objects
// the same way.
//
// The rule is deliberately simple and predictable: an object RESTS on the
// highest surface under its footprint - the terrain, or the top of another
// object it would otherwise sink into. No swept collision, no penetration
// solver; a level editor's "drop it on the floor", extended to props resting
// on other props.
namespace placement {

// Axis-aligned world bounds of what an object occupies.
struct Aabb {
    float mn[3] = {0.0f, 0.0f, 0.0f};
    float mx[3] = {0.0f, 0.0f, 0.0f};
};

// World AABB of an object: the rotated, scaled unit primitive, or a model's
// own local bounds when `modelAabb` can supply them (a model whose file is
// missing falls back to the unit box, so placement still does something
// sensible). Never fails - every object type has a placeable extent, markers
// included; whether it is something to rest ON is isSupport()'s question.
Aabb worldAabb(const SceneObject& o, const aobake::ModelAabbFn& modelAabb);

// True when an object is solid enough to stack on: the geometry primitives,
// save points and models, with collision left on (collisionMode 2 = "none"
// opts out). Markers, lights, emitters, decals, mirrors and portals are not
// surfaces - you would never mean to stand a crate on a point light.
bool isSupport(const SceneObject& o);

// Terrain height sampler (the viewport's bilinear one - the same function the
// game samples the heightmap with).
using HeightFn = std::function<float(float x, float z)>;

// The vertical offset that rests `o` on the surface under it: add it to
// o.position[1] and the object's underside touches the highest support below.
//   skip:     indices of `objects` to ignore (the object itself, the rest of a
//             pasted group, anything on a hidden layer).
//   ceilingY: supports whose top lies ABOVE this are ignored. FLT_MAX = "rest
//             on whatever is under the footprint, however tall" (insert /
//             paste); passing the object's own underside makes it a pure drop
//             (nothing can lift the object).
float restOffsetY(const SceneObject& o, const std::vector<SceneObject>& objects,
                  const std::vector<char>& skip,
                  const aobake::ModelAabbFn& modelAabb, const HeightFn& height,
                  float ceilingY);

// The same for a group moved as one rigid arrangement: the offset is the
// largest any member needs, so the group keeps its shape and no member sinks.
// `objects` is the scene; the group members are NOT part of it (a staged
// paste) or are excluded through `skip`.
float restOffsetYGroup(const std::vector<SceneObject>& group,
                       const std::vector<SceneObject>& objects,
                       const std::vector<char>& skip,
                       const aobake::ModelAabbFn& modelAabb,
                       const HeightFn& height, float ceilingY);

}  // namespace placement
