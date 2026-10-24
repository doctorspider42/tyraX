#pragma once

#include <string>
#include <vector>

#include "procgen.hpp"
#include "project.hpp"

// Baking a procedural graph down to something a PlayStation 2 can afford.
//
// Merged output writes ordinary chunk meshes. Instanced output shares source
// geometry and stores placements separately; its total placement triangle sum
// is neither resident geometry nor a per-frame GPU measurement.
namespace procbake {

// What one bake produced (or would produce - see estimate()).
struct Report {
    int volumes = 0;      // scatter volumes baked
    int chunks = 0;       // chunk objects written
    int instances = 0;
    int triangles = 0;  // all placements at near detail
    int sharedTriangles = 0;  // instanced: unique near + authored far meshes
    size_t vertexBytes = 0;  // rough PS2 RAM for the merged geometry
    // Output "Draw as: Instanced": no chunks; `models` distinct source models,
    // vertexBytes = one shared copy of each + the per-instance table.
    bool instanced = false;
    int models = 0;
    bool overBudget = false;
    std::vector<std::string> warnings;
    std::string error;  // non-empty = nothing was written
};

// Estimated cost of an already-evaluated result, without writing anything -
// the live budget readout (BAKE-03). `volume` supplies the Output settings.
Report estimate(const Project& p, const SceneObject& volume,
                const procgen::Result& r);

// Bakes one Scatter volume: evaluates its graph at full density, writes the
// merged chunk meshes, reconciles the generated chunk objects in `s` (matched
// by name, so ids/live-link identity survive a re-bake) and stamps the
// volume's procGraph.bakedHash. Emptying a graph removes its chunks and files.
// Addressed by volume ID, not by reference: the bake inserts and erases scene
// objects, which would invalidate a reference into s.objects.
Report bakeVolume(Project& p, SceneData& s, const std::string& volumeId,
                  procgen::Cache* cache = nullptr);

// Freeze a current bake (baking first if stale), or resume automatic updates.
// Runtime volumes cannot freeze. Frozen volumes reject explicit rebakes too.
Report setFrozen(Project& p, SceneData& s, const std::string& volumeId,
                 bool frozen, procgen::Cache* cache = nullptr);

// Every Scatter volume in every scene. With `force` false only volumes whose
// bakedHash no longer matches procgen::bakeHash are re-baked (so an ordinary
// build is free when nothing procedural changed). Frozen volumes are skipped
// even with force=true. Mutates the project: callers
// in the editor must follow with commitChange(), headless ones with save().
Report bakeAll(Project& p, bool force);

// True when any volume in the project has a stale bake - the toolbar/window
// indicator and the build hook read this.
bool anyStale(const Project& p);

// Deletes a volume's generated chunk objects and their .obj files (used when
// the volume itself is deleted, and by "clear bake").
void clearVolume(Project& p, SceneData& s, const std::string& volumeId);

}  // namespace procbake
