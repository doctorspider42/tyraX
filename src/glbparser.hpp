#pragma once

#include <string>
#include <vector>

// Minimal glTF 2.0 binary (.glb) importer + morph-frame baker.
//
// The editor accepts animated models as .glb (Blender: File > Export >
// glTF 2.0, format "glTF Binary") because OBJ has no notion of animation.
// The PS2 engine's dynamic pipeline (DynPip) renders MD2-style morph frames
// (two vertex arrays + VU1 interpolation), so instead of teaching the PS2
// about skeletons (stage 2, see PROGRESS.md backlog), every animation clip
// is SAMPLED here on the PC: node TRS channels are evaluated at a fixed
// rate, vertices are CPU-skinned, and the resulting per-frame vertex/normal
// arrays are what the game (and the viewport preview) consume.
//
// Supported subset (Blender's default .glb export fits):
//  - GLB container with the whole buffer embedded (no external .bin/URIs)
//  - triangle primitives, indexed or not; POSITION/NORMAL/TEXCOORD_0 +
//    JOINTS_0/WEIGHTS_0 for skinned meshes
//  - skins (matrix-palette skinning, 4 joints per vertex) and rigid node
//    animation; channels: translation / rotation / scale, LINEAR / STEP
//    (CUBICSPLINE falls back to its keyframe values, linearly interpolated)
//  - materials: pbr baseColorFactor + baseColorTexture (embedded image;
//    PNG kept verbatim, other formats transcoded to PNG via stb)
// Not supported: sparse accessors, morph targets (weights channels are
// skipped), texture transforms, non-PNG-able images. Unsupported pieces
// degrade with a note in Baked::warnings instead of failing the import.
namespace glbparser {

// One named animation clip, baked into the shared frame list.
struct Clip {
    std::string name;
    int firstFrame = 0;
    int frameCount = 1;
};

// One draw batch: all triangles sharing a glTF material, expanded to a flat
// triangle list (the PS2 pipelines take flat arrays, no index buffers).
// vertexCount is constant across frames; UVs don't animate.
struct Part {
    std::string material;              // glTF material name ("" = default)
    float baseColor[4] = {1, 1, 1, 1}; // pbr baseColorFactor
    int image = -1;                    // index into Baked::images, -1 = none
    int vertexCount = 0;
    std::vector<float> uvs;        // vertexCount * (u, v) - image space
    std::vector<float> positions;  // frameCount * vertexCount * (x, y, z)
    std::vector<float> normals;    // frameCount * vertexCount * (x, y, z)
};

// An embedded texture, as PNG bytes (ready to write to disk / hand to stb).
struct Image {
    std::vector<unsigned char> png;
    std::string name;  // unique suggested basename, e.g. "body.png"
};

struct Baked {
    std::vector<Part> parts;
    std::vector<Clip> clips;  // >= 1; a static .glb gets one 1-frame "default"
    std::vector<Image> images;
    int frameCount = 1;   // total baked frames (all clips, concatenated)
    float fps = 12.0f;    // bake sample rate the clips were sampled at
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};  // frame-0 AABB, all parts
    std::vector<std::string> warnings;  // non-fatal import notes

    int totalVertexCount() const {
        int n = 0;
        for (const Part& p : parts) n += p.vertexCount;
        return n;
    }
};

// Parses a .glb and bakes every animation clip into morph frames at `fps`
// samples per second. Returns false (with `error` set) only when the file
// is unreadable/malformed or contains no triangles.
bool bake(const std::string& path, float fps, Baked& out, std::string& error);

// Serializes a bake into the .tanm binary consumed by the PS2 engine's
// TanmLoader (vendor/tyra .../loaders/3d/tanm_loader). `textureNames` maps
// Baked::images indices to the texture paths the GAME will load (relative
// to bin/, e.g. "models/robot_0.png"); it must match what the build writes
// next to the ELF. Keep the layout in sync with tanm_loader.cpp.
std::string writeTanm(const Baked& baked,
                      const std::vector<std::string>& textureNames);

// ---------------------------------------------------------------------------
// Stage 2: skeletal serialization (.tskl). Instead of sampling clips into
// morph frames, the node hierarchy, skin palette, bind-pose mesh and raw
// keyframe tracks are exported; the PS2 engine evaluates poses and skins on
// the EE at runtime (vendor/tyra .../loaders/3d/tskl_loader + SkelInstance).
// bake() stays as the editor-side preview/validation path - both share one
// .glb parse, so what the viewport shows is what the console computes.

// One node of the glTF hierarchy with its bind-pose local transform.
struct SkelNode {
    int parent = -1;
    bool hasMatrix = false;  // matrix nodes are never animated (glTF spec)
    float matrix[16] = {};
    float t[3] = {0, 0, 0};
    float r[4] = {0, 0, 0, 1};  // x, y, z, w quaternion
    float s[3] = {1, 1, 1};
};

// One matrix-palette slot: joint global * ibm skins the verts bound to it.
// Rigid (unskinned) mesh nodes get a slot with an identity ibm.
struct SkelJoint {
    int node = 0;
    float ibm[16] = {};  // inverse bind matrix, column-major
};

// One keyframe track: `node`'s translation / rotation / scale over time.
struct SkelChannel {
    int node = 0;
    int path = 0;  // 0 translation, 1 rotation (quat), 2 scale
    int step = 0;  // 1 = STEP interpolation (hold left key), 0 = linear
    std::vector<float> times;   // seconds, rebased so the clip starts at 0
    std::vector<float> values;  // keyCount * (path == 1 ? 4 : 3) floats
};

struct SkelClip {
    std::string name;
    float duration = 0.0f;  // seconds
    std::vector<SkelChannel> channels;
};

// A decimated variant of a part's mesh (same attribute layout, fewer
// triangles). Generated at bake time by generateSkelLods().
struct SkelLod {
    int vertexCount = 0;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<unsigned char> joints;
    std::vector<unsigned char> weights;
};

// One draw batch (all triangles of one glTF material) in bind pose, expanded
// to a flat triangle list, with per-vertex palette bindings.
struct SkelPart {
    std::string material;
    float baseColor[4] = {1, 1, 1, 1};
    int image = -1;  // index into Skel::images, -1 = none
    int vertexCount = 0;
    std::vector<float> positions;       // vertexCount * 3
    std::vector<float> normals;         // vertexCount * 3
    std::vector<float> uvs;             // vertexCount * 2
    std::vector<unsigned char> joints;  // vertexCount * 4 palette slots
    std::vector<unsigned char> weights; // vertexCount * 4, sums to 255
    std::vector<SkelLod> lods;          // [0] ~50% verts, [1] ~25% (optional)
};

struct Skel {
    std::vector<SkelNode> nodes;
    std::vector<SkelJoint> palette;
    std::vector<SkelPart> parts;
    std::vector<SkelClip> clips;  // >= 1; static .glb gets a 0s "default"
    std::vector<Image> images;
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};  // pose AABB, all-clips union
    std::vector<std::string> warnings;

    int totalVertexCount() const {
        int n = 0;
        for (const SkelPart& p : parts) n += p.vertexCount;
        return n;
    }
    // Rough PS2 RAM footprint: model data as the engine keeps it + one
    // instance's skinned output buffers (the import-status estimate).
    size_t ps2Bytes() const;
};

// Parses a .glb into the skeletal representation above. Same support matrix
// and failure conditions as bake().
bool parseSkel(const std::string& path, Skel& out, std::string& error);

// Generates the distance LODs (SkelPart::lods) by quadric-error half-edge
// collapse on the bind-pose mesh: ~50% and ~25% of the base vertex count.
// Attributes (normals, uvs, skin bindings) ride along unchanged - a collapse
// snaps one vertex onto another, it never invents blended attributes. Parts
// too small to gain anything are skipped (no lods emitted).
void generateSkelLods(Skel& skel);

// Serializes to the .tskl binary consumed by the PS2 engine's TsklLoader.
// `textureNames` maps Skel::images indices to game-relative texture paths,
// exactly like writeTanm. Keep the layout in sync with tskl_loader.cpp.
std::string writeTskl(const Skel& skel,
                      const std::vector<std::string>& textureNames);

}  // namespace glbparser
