#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// Readers for the MakeHuman CC0 data set the Character Generator is built on
// (docs/character-generator.md). Host-only, no GL, no Project dependency - the
// treegen/matbake pattern, so the whole module is exercisable from a small
// host harness.
//
// **Data only.** The MakeHuman program is AGPL and none of it is used here;
// the files these readers parse (base mesh, morph targets, proxy meshes, rig,
// vertex weights, skins) were explicitly released as CC0 in 2020. deps.ps1
// fetches them into vendor/mh-assets, README.md carries the credits.
//
// Everything is in MakeHuman's own space: **decimetres**, Y up, +Z forward,
// origin at the hips (the default mesh spans y = -8.45 .. 8.50, i.e. a
// 1.70 m human). Converting to game units is the caller's job - chargen does
// it once, at the end.
namespace mhdata {

// The base mesh (hm08): 19158 vertices, 18486 quads. Only the `body` group is
// rendered; the rest are "helper" cages used for fitting clothes and 125
// eight-vertex "joint" cubes whose centroids are where the rig's joints sit.
// Every morph target and every proxy indexes THIS vertex array, so the array
// must be kept whole - never compact it before morphing.
struct BaseMesh {
    // Quads throughout the MakeHuman meshes, but the readers accept triangles
    // too so a hand-made or converted proxy still loads.
    struct Face {
        int n = 4;  // 3 or 4 corners
        int v[4];   // 0-based position indices
        int t[4];   // 0-based uv indices (-1 = the face carried no uv)
    };

    std::vector<float> pos;         // vertCount * 3
    std::vector<float> uv;          // uvCount * 2, OBJ space (v up)
    std::vector<Face> faces;
    std::vector<int> faceGroup;     // per face, an index into `groups`
    std::vector<std::string> groups;

    int vertCount() const { return (int)pos.size() / 3; }
    // Index of a named group, or -1. Group names are things like "body",
    // "helper-tights", "joint-l-eye".
    int groupIndex(const std::string& name) const;
};

// Parses a MakeHuman .obj (quads, `v`/`vt`/`g`/`f a/b`, no normals). Also
// reads the proxy meshes, which use the same subset.
bool loadBaseMesh(const std::string& path, BaseMesh& out, std::string& error);

// One morph target: a sparse set of per-vertex offsets against the base mesh.
// Applying it at weight w adds w * delta to each listed vertex.
struct Target {
    std::vector<int> index;    // base vertex indices
    std::vector<float> delta;  // index.size() * 3
};

bool loadTarget(const std::string& path, Target& out, std::string& error);

// Adds `weight * target` into `pos` (a full base-mesh position array).
// Out-of-range indices are skipped: targets and base mesh are versioned
// together upstream, but a hand-copied mismatch should not corrupt memory.
void applyTarget(const Target& t, float weight, std::vector<float>& pos);

// A proxy mesh binding: each proxy vertex rides on a triangle of the base
// mesh (three vertex indices + barycentric weights) plus an offset expressed
// in units of the base mesh's own measurements, so the proxy follows every
// morph without knowing what the morph was. This is what makes a 741-vertex
// PS2-budget body track the full 19158-vertex MakeHuman rig.
struct Proxy {
    std::vector<int> ref;       // vertCount * 3 base vertex indices
    std::vector<float> weight;  // vertCount * 3 barycentric weights
    std::vector<float> offset;  // vertCount * 3, in reference-distance units

    // The three measurements the offsets are expressed in: a vertex pair and
    // the distance that pair spans in the *default* (unmorphed) base mesh.
    int scaleVert[3][2] = {{0, 0}, {0, 0}, {0, 0}};
    float scaleDist[3] = {1.0f, 1.0f, 1.0f};

    // Base-mesh vertices this asset covers up. Clothes list the body they
    // hide, so the torso does not poke through a shirt; a proxy body has none.
    // Sorted, so membership is a binary search.
    std::vector<int> deleteVerts;

    std::string objFile;  // the .obj holding this proxy's topology and UVs
    std::string name;

    int vertCount() const { return (int)ref.size() / 3; }
    bool deletes(int baseVert) const {
        return std::binary_search(deleteVerts.begin(), deleteVerts.end(), baseVert);
    }
};

// Reads a `.proxy` (body proxies) or `.mhclo` (clothes, hair) - the same
// format, and the reason a shirt fits a generated body for free: both are
// nothing but a barycentric binding to the reference mesh.
bool loadProxy(const std::string& path, Proxy& out, std::string& error);

// Evaluates the proxy against a (morphed) base-mesh position array.
// Returns vertCount * 3 positions in the same space as `basePos`.
std::vector<float> fitProxy(const Proxy& proxy, const std::vector<float>& basePos);

// The rig (.mhskel). Bones name their head/tail *joints*, and a joint is a
// list of base-mesh vertices - so the skeleton is re-derived from the morphed
// mesh instead of being posed to fit it.
struct Skeleton {
    struct Bone {
        std::string name;
        std::string parent;  // "" = root
        std::string head;    // joint name
        std::string tail;    // joint name
    };

    std::vector<Bone> bones;
    std::map<std::string, std::vector<int>> joints;  // joint -> base vertices

    const Bone* find(const std::string& name) const;
};

bool loadSkeleton(const std::string& path, Skeleton& out, std::string& error);

// Centroid of a joint's vertex cube in the given base-mesh positions.
// Returns false when the joint is unknown or its vertex list is empty.
bool jointPos(const Skeleton& skel, const std::string& joint,
              const std::vector<float>& basePos, float out[3]);

// Per-bone vertex weights on the base mesh (.mhw). Bones absent from the map
// simply have no influence.
struct Weights {
    std::map<std::string, std::vector<std::pair<int, float>>> bone;
};

bool loadWeights(const std::string& path, Weights& out, std::string& error);

}  // namespace mhdata
