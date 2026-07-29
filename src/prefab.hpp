#pragma once

#include <string>
#include <vector>

#include "project.hpp"

// Prefabs: a group of scene objects (with their flow graphs) saved once and
// stamped into the world as many times as you like - by hand in the editor, by
// a procedural graph, or by a flow node while the game runs. See
// docs/prefabs.md.
//
// The `Prefab` struct itself lives in project.hpp (it is part of the model);
// this header is the VERBS, plus the one predicate the whole runtime story
// hangs on - prefab::memberMerges.
//
// Where the rest of the pieces live:
//   - project.cpp     Section::Prefabs, save/load
//   - prefab_ui.cpp   the Prefabs window
//   - templates.cpp   inc/prefab_data.gen.hpp + the runtime spawner
//
// Nothing here touches GL or ImGui, so the whole thing is exercisable from a
// host harness (the placement/decalproj pattern).

namespace prefab {

// How many prefab instances the generated game can hold at once. Every live
// instance takes a record - a volume that scatters prefabs spends one PER
// POINT - and the runtime refuses the rest with "instance pool full".
//
// It lives here rather than as a literal in templates.cpp because the EDITOR
// has to know it: a runtime volume whose graph yields more points than this
// previews the whole world and builds a fraction of it on the console, which
// looks like a generation bug and is a budget. codegen emits it as
// MAX_PREFAB_INSTANCES; procgen warns against it.
constexpr int kMaxRuntimeInstances = 48;

// Can this member be merged into the instance's shared geometry bag at
// runtime? Merged members cost NOTHING per instance beyond their triangles -
// one submit for the whole prefab instead of one per member, which is the
// difference between a prefab being usable on this machine and not (a PS2
// static submit costs ~0.7-1.5 ms of fixed EE overhead whatever the vertex
// count). A member is mergeable when it is plain static geometry: a primitive
// or a static .obj, no graph, no scripts, nothing that gives it an identity
// something else can address at runtime.
//
// The complement is spawned as a real object through the ordinary spawn pool,
// so a prefab can still carry a door that opens, a pickup, a light or an
// emitter - it just pays a slot and a draw call for it.
bool memberMerges(const SceneObject& o);

// A member with no geometry at all (markers, areas, volumes) - spawned but
// never drawn, and skipped by the geometry cost estimate.
bool memberIsMarker(const SceneObject& o);

// Captures `sel` (indices into s.objects) as a prefab named `name`. The
// origin is the selection's horizontal centre at its LOWEST point, which is
// what makes "click the ground to place it" land the way a person expects.
// Flow graphs come along untouched; object-name references inside them are
// NOT rewritten (see docs/prefabs.md - a prefab that talks to the world keeps
// talking to the world).
Prefab capture(const SceneData& s, const std::vector<int>& sel,
               const std::string& name);

// The world-space objects one instance of `pf` produces at (x, y, z) with a
// yaw in degrees. Ids are left EMPTY (project::ensureObjectIds stamps them);
// names get `suffix` appended so an instance is findable and a second one does
// not collide. `scale` multiplies the whole instance uniformly.
std::vector<SceneObject> instantiate(const Prefab& pf, float x, float y,
                                     float z, float yaw, float scale,
                                     const std::string& suffix);

// Bounding box of the prefab in its own local frame (markers included, so the
// box is what a placement preview should draw).
void bounds(const Prefab& pf, float outMin[3], float outMax[3]);

// Prefab by name (nullptr when absent). Names are the reference form
// everywhere, so this is the one resolver.
const Prefab* find(const Project& p, const std::string& name);
Prefab* find(Project& p, const std::string& name);

// A name not yet taken, derived from `wanted` ("room" -> "room 2").
std::string uniqueName(const Project& p, const std::string& wanted);

// Renames a prefab and retargets every reference to it (flow node params,
// procedural Pick Prefab rows). Returns false when `to` is empty or taken.
bool rename(Project& p, const std::string& from, const std::string& to);

// --- bake to a model -------------------------------------------------------
// Flattens a prefab's MERGEABLE members into one static `.obj` (+ a generated
// `.mtl`) under res/models/, and returns what happened.
//
// Why this exists: a prefab instance costs a record from the runtime pool
// (kMaxRuntimeInstances above), so scattering hundreds of them is not possible
// however cheap each one is. The same shape baked to a model costs NOTHING per
// instance - Pick Asset merges it straight into the chunk bags - so "assemble a
// thing out of primitives, then scatter it by the hundred" goes through here.
//
// It is a ONE-WAY bake and the result is dumb geometry: no scripts, no lights,
// no physics, no per-member identity, nothing addressable at runtime. The
// prefab stays untouched as the source; this writes a new asset beside it.
// Members that cannot merge are named in `skipped` rather than silently
// dropped - a light or a scripted door has no representation in a .obj.
struct BakeReport {
    std::string modelPath;  // "res/models/<name>.obj" ("" = nothing written)
    std::string mtlPath;
    int members = 0;    // merged members that contributed geometry
    int triangles = 0;
    int materials = 0;  // newmtl entries written
    std::vector<std::string> skipped;   // "<member> (why)"
    std::vector<std::string> warnings;
    std::string error;  // non-empty = nothing was written
};
BakeReport bakeToModel(const Project& p, const Prefab& pf);

// Prefabs some scene can spawn: named by a Spawn Prefab flow node anywhere in
// the scene, or by a Pick Prefab row in one of its procedural volumes. This is
// what decides whose members' models/materials a scene has to ship, so it is
// the single source both the codegen asset scan and the Prefabs window's
// "used by" readout call.
std::vector<std::string> referencedBy(const Project& p, const SceneData& s);

}  // namespace prefab
