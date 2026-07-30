#pragma once

#include <string>
#include <vector>

#include "project.hpp"

// RUNTIME procedural generation: the half of docs/procedural-generation.md that
// does NOT bake. A volume in runtime mode has its graph COMPILED into the game
// (src/gen/procedural.gen.cpp) and evaluated on the EE, so the world can be
// different every boot - and so the geometry never has to exist on the disc.
//
// This module is the host side of that: the honest capability check and the
// C++ emitter. It has no GL, no ImGui and no templates.cpp dependency, which is
// the livelogic.cpp arrangement and for the same reason - the interesting parts
// (what is supported, what a graph compiles to) are then exercisable from a
// harness.
//
// THE RULE THIS FEATURE LIVES BY: a baked volume pays nothing at runtime and
// can use every node in the library, because the editor has the whole project
// in RAM. A runtime volume pays load time, RAM and a much smaller vocabulary,
// because the console has a heightmap, a few models and 32 MB. `capability()`
// is where that difference is written down; it must never claim a node the
// emitter cannot actually produce, so both read the SAME table (kRuntimeNodes).
namespace procrt {

// Why a graph cannot run on the console, in reading order. Empty = it compiles.
struct Issue {
    int nodeId = 0;  // 0 = graph-wide
    std::string text;
};

// Everything about `g` that the runtime cannot do. Called by the Procedural
// window (which shows it under the budget bar) and by codegen (which refuses to
// emit a volume with issues rather than generating code that does not compile).
std::vector<Issue> capability(const ProcGraph& g);

// Is this node type runnable on the console at all? The add-menu greys the rest
// out while a volume is in runtime mode.
bool nodeSupported(const std::string& type);

// One runtime volume the build has to emit.
struct Volume {
    int scene = 0;
    int objectIndex = 0;  // index into scenes[scene].objects
    std::string name;     // the volume object's name (flow nodes address it)
    std::vector<std::string> assets;   // .obj asset paths, in graph order
    std::vector<std::string> prefabs;  // prefab names, in graph order
    bool hasBlocks = false;            // owns a Blocks Fill node
    int blockColumns = 0;              // nx*nz of that node's lattice
    int blockLevels = 0;
    std::vector<Issue> issues;  // non-empty = skipped by codegen, with a warning
};

// Every runtime volume in the project, in (scene, object) order - the order the
// generated VOLUMES table uses, so an index means the same thing on both sides.
std::vector<Volume> volumes(const Project& p);

// Index of a runtime volume in the EMITTED table (the one the generated
// VOLUMES array uses), or -1 when there is no such volume or it was skipped.
// Volumes with capability issues are not emitted, so this is the only correct
// way to name one from codegen - counting `volumes()` would be off by every
// skipped entry.
int volumeIndexOf(const Project& p, int scene, const std::string& objectName);

// The generated pair: `inc/procedural.gen.hpp` (always emitted - it is the
// on/off seam, so a project with no runtime volume folds every call away) and
// `src/gen/procedural.gen.cpp`. `warnings` collects what was skipped and why.
struct Emitted {
    std::string header;
    std::string source;
    std::vector<std::string> warnings;
};
Emitted emit(const Project& p);

}  // namespace procrt
