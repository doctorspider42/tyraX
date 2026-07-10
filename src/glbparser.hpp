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

}  // namespace glbparser
