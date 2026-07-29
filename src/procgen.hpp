#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "procgraph.hpp"
#include "project.hpp"

// Procedural graph EVALUATION. Host-only, no GL, no ImGui - the decalproj /
// aobake / navmesh pattern: one deterministic function of (project, scene,
// scatter volume, graph) that the editor viewport and the bake both call, so
// what you preview is bit-for-bit what ships.
//
// Nothing here runs on the PS2. The console receives finished geometry
// (procbake) and never learns that a graph existed.
//
// Three properties this module is built around, in order of how much pain
// their absence causes:
//
//  1. DETERMINISM. Every random draw comes from hash(graph seed, node id,
//     point key, channel) - never from a running counter. So the result does
//     not depend on evaluation order, on how many unrelated nodes sit in the
//     graph, or on how many threads ran. Adding an unconnected node in another
//     branch cannot reshuffle the forest.
//  2. PREFIX STABILITY. Generators emit points from a fixed low-discrepancy
//     sequence and density decides how MANY of them are used. Raising density
//     therefore adds points between the existing ones instead of producing a
//     different layout - which is also what makes progressive preview honest
//     (10 % of the points is the first 10 % of the final ones) and what lets a
//     manual override stay attached to its instance (ProcOverride::key).
//  3. CACHING. Each node's output is memoized under a hash of its parameters
//     and its inputs' hashes, so dragging a slider at the end of a 20-node
//     graph re-runs one node.
namespace procgen {

// A 2D scalar field over a world-space XZ rectangle (the scatter volume's
// axis-aligned footprint). Masks are the "how much" channel of the whole
// system: density, thinning, any attribute.
struct Mask {
    int w = 0, h = 0;
    float originX = 0.0f, originZ = 0.0f;  // world corner of texel (0,0)
    float sizeX = 0.0f, sizeZ = 0.0f;      // world extent covered
    std::vector<float> v;                  // w*h, row-major, 0..1

    float sample(float x, float z) const;
};

// A Catmull-Rom curve through control points, in world space.
struct Curve {
    std::vector<float> pts;  // x,y,z per control point
    bool closed = false;

    int count() const { return (int)(pts.size() / 3); }
    // Point at parameter u in [0,1] over the whole curve (arc length is
    // approximated by segment count - good enough for placement).
    void at(float u, float out[3]) const;
    // Tangent direction at u (normalized, XZ-dominant).
    void tangent(float u, float out[3]) const;
    // Shortest distance from (x,z) to the curve in the XZ plane.
    float distanceXZ(float x, float z) const;
};

// One generated instance: what the preview draws and the bake merges.
struct Instance {
    float pos[3] = {0, 0, 0};
    float rot[3] = {0, 0, 0};  // degrees, applied X then Y then Z
    float scale = 1.0f;        // uniform - merged geometry, PS2 economy
    int asset = -1;            // index into Result::assets, -1 = no asset yet
    // Index into Result::prefabs (-1 = none). A point carries an asset OR a
    // prefab: a prefab instance is a group of objects, not geometry to merge
    // into a neighbour's chunk, so the two travel in separate fields rather
    // than sharing one index space that every consumer would have to decode.
    int prefab = -1;
    // Visible-face mask written by Blocks Fill (procattr::kFaces, 6 bits). 63 =
    // "every face" and is what a point that never met that node carries, so a
    // merger can honour it unconditionally.
    unsigned char faces = 63;
    uint64_t key = 0;          // stable identity (see ProcOverride)
};

// What a whole evaluation produced. `type` says which payload is filled: an
// isolated mask or curve node previews as itself (UX-01), everything else
// comes out as instances.
struct Result {
    ProcType type = ProcType::Points;
    std::vector<Instance> instances;
    std::shared_ptr<const Mask> mask;
    std::shared_ptr<const Curve> curve;
    std::vector<std::string> assets;  // model paths, index = Instance::asset
    std::vector<std::string> prefabs;  // prefab names, index = Instance::prefab

    int candidates = 0;       // points the generators produced before filtering
    int nodesEvaluated = 0;   // re-run this pass (GRAF-03's visible number)
    int nodesCached = 0;      // served from the cache
    float fraction = 1.0f;    // density fraction this pass used (see Options)
    bool canceled = false;
    int overridesApplied = 0;
    int overridesOrphaned = 0;  // stored overrides whose point no longer exists
    std::vector<std::string> warnings;
    double millis = 0.0;
};

struct Options {
    // Progressive preview (GRAF-05): 0..1 fraction of the full point count.
    // Prefix stability makes a coarse pass a subset of the full one, so the
    // cheap preview never lies about the layout.
    float fraction = 1.0f;
    // Evaluate up to this node instead of the Output node (0 = Output) - the
    // "show me what this node produced" debugging tool.
    int previewNode = 0;
    // Bumped by the caller whenever anything the graph READS changes (the
    // volume transform, the terrain, other objects). Mixed into every node
    // hash, so the cache cannot serve a stale answer after a scene edit.
    uint64_t contextSerial = 0;
    // Evaluate as if ProcGraph::seed were this (0 = use the graph's own). The
    // seed simulator: a RUNTIME volume can be told to roll a fresh seed on the
    // console, so "what does this graph actually produce" is a question about a
    // distribution, not about one number - and the only honest way to answer it
    // before the game boots is to run the other worlds here. Never affects
    // bakeHash: this is a way of LOOKING at the graph, not an edit to it.
    uint32_t seedOverride = 0;
    // Cooperative cancel for the worker thread; checked between nodes and
    // inside the generators' inner loops.
    std::atomic<bool>* cancel = nullptr;
};

// Per-node memo + parsed-mesh cache, owned by the caller and reused across
// evaluations (that is the whole point). Safe to keep for the lifetime of a
// project; drop it when the project changes.
class Cache {
public:
    void clear();
    size_t entries() const { return nodes_.size(); }

    // Internals. Public only because the evaluator's node functions live in an
    // anonymous namespace inside procgen.cpp and so cannot be befriended -
    // nothing outside that file may touch these.
    struct Entry;
    std::map<int, std::shared_ptr<Entry>> nodes_;
};

// Evaluates `volume`'s graph. `volume` is the Scatter object: its transform is
// the region (position = center, scale = box size, rotation Y = yaw of the
// footprint). Never throws; problems land in Result::warnings.
Result evaluate(const Project& p, const SceneData& s, const SceneObject& volume,
                const Options& opt, Cache* cache = nullptr);

// The triangle soup of one asset (a .obj under the project dir), 8 floats per
// vertex (pos, normal, uv) per material submesh - what the bake merges and the
// preview draws. Shared with procbake so both see the same geometry.
struct AssetMesh {
    struct Part {
        std::string material;  // usemtl name ("" = none)
        std::vector<float> verts;
    };
    std::vector<Part> parts;
    // Material libraries as the .obj referenced them (names relative to the
    // .obj's own directory) - which is why a baked chunk mesh is written INTO
    // that directory: the same mtllib line then resolves unchanged.
    std::vector<std::string> mtlLibs;
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    int triangles() const {
        size_t n = 0;
        for (const Part& p : parts) n += p.verts.size() / 24;
        return (int)n;
    }
};

// Loads (and caches, by path) an asset mesh. Returns nullptr when the file
// cannot be read.
std::shared_ptr<const AssetMesh> assetMesh(const Project& p,
                                           const std::string& relPath);

// Hash of everything a bake of this volume depends on: the graph, the volume
// transform, the terrain, and every object the graph can read. Stored in
// ProcGraph::bakedHash so the build knows whether the baked chunks are stale.
uint64_t bakeHash(const Project& p, const SceneData& s, const SceneObject& volume);

// The deterministic per-point random stream. Exposed because procbake needs the
// same numbers (and because a test can pin them).
float rand01(uint32_t seed, int nodeId, uint64_t key, int channel);

}  // namespace procgen
