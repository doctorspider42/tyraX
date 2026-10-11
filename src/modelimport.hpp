#pragma once

#include <array>
#include <string>
#include <vector>

// The ONE model importer (docs/model-import.md). Every source format - .obj,
// .glb, .fbx - is read into the same neutral description (Source), shaped by
// the same options (split, pivot, clean-up, reduction, texture size), and
// written out in the same canonical form:
//
//   static  -> <name>.obj + <name>.mtl + its textures as PNG, side by side
//   animated -> the original .glb/.fbx (skeleton + clips) + <name>.mtl + PNGs
//
// so a static model is ALWAYS a Wavefront model whatever it came from (the
// whole static pipeline - viewport, bakes, collision, .tmdl - already reads
// .obj and nothing else), and every model ALWAYS has a .mtl next to it, even
// an empty-looking one, which is what the Material Editor and the per-object
// material picker edit. Whether a placed object is static or animated is a
// property of the ENTITY (Model / Animated model), not of the file that was
// imported: a .glb can feed both.
//
// Host-only: no GL, no ImGui, no Project - the treegen/aobake shape, so the
// whole importer runs from a harness and from the import window's worker
// thread alike.
namespace modelimport {

struct Image {
    std::string name;               // unique file name, "wood.png"
    std::vector<unsigned char> png; // full-resolution PNG bytes
    int w = 0, h = 0;
    // Downscaled RGBA for the import preview (<= kPreviewTex per side).
    std::vector<unsigned char> preview;
    int pw = 0, ph = 0;
};
constexpr int kPreviewTex = 128;

struct Material {
    std::string name;  // as authored (sanitized for .mtl when written)
    float kd[3] = {1, 1, 1};
    float ke[3] = {0, 0, 0};
    float glowRange = 0.0f, glowLight = 1.0f;
    std::string refl;  // "@sky" or "" (sphere-map files are not carried)
    float reflStrength = 0.0f;
    bool reflRounded = false;
    int image = -1;              // index into Source::images
    std::string missingTexture;  // referenced but not found ("" = fine)
};

// One material's triangles: 8 floats per corner (x y z nx ny nz u v), UVs in
// IMAGE space (v down) like objparser and glbparser.
struct Piece {
    int material = -1;
    std::vector<float> verts;
};

// The finest unit the source can be split into (an obj `o`/`g`, a mesh node).
struct Object {
    std::string name;
    int group = 0;  // index into Source::groups (the top-level object)
    std::vector<Piece> pieces;
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    int tris = 0;
};

struct Source {
    std::string path;
    std::string format;  // "obj", "glb", "fbx"
    std::vector<Object> objects;
    std::vector<std::string> groups;  // top-level object names
    std::vector<Material> materials;
    std::vector<Image> images;
    bool skinned = false;   // a deforming skin (can only ship animated)
    std::vector<std::string> clips;  // clips with real motion
    bool canAnimate() const { return skinned || !clips.empty(); }
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};
    int tris = 0;
    std::vector<std::string> warnings;
    int missingTextures() const;
};

// Reads any supported model. `textureDirs` are searched (recursively, by file
// name, extension-agnostic) for textures that are not where the file says -
// the source's own folder is always searched first. False + error only when
// nothing usable could be read.
bool load(const std::string& path, const std::vector<std::string>& textureDirs,
          Source& out, std::string& error);

enum class Split { None = 0, TopLevel = 1, EveryMesh = 2 };
enum class Pivot { Keep = 0, BottomCenter = 1, Center = 2 };

struct Options {
    std::string baseName;  // file stem of the result ("" = source stem)
    Split split = Split::None;
    Pivot pivot = Pivot::Keep;
    bool cleanup = true;       // drop degenerate and duplicate triangles
    float keep = 1.0f;         // triangle reduction: fraction kept (0.05..1)
    int maxTexture = 512;      // longest texture side written (0 = as is)
    bool writeStatic = true;   // <name>.obj (+ split models)
    bool writeAnimated = false;// keep the .glb/.fbx skeleton + clips
    // Per planned model: false = leave it out. Empty = all.
    std::vector<char> include;
};

// One model the import will write.
struct Planned {
    std::string name;                 // unique file stem
    std::vector<Piece> pieces;        // pivot already applied
    std::array<float, 3> origin{};    // where the pivot sits in source space
    float mn[3] = {0, 0, 0}, mx[3] = {0, 0, 0};  // after the pivot
    int tris = 0;
    int listVerts = 0;   // what VU1 receives as a triangle list
    int stripVerts = 0;  // ... as the build's triangle strips (= list if none)
};

// Applies split / clean-up / reduction / pivot. Pure; cheap enough to call on
// every option change for ordinary assets.
std::vector<Planned> plan(const Source& src, const Options& opt);

struct Result {
    std::vector<std::string> staticModels;  // project-relative .obj paths
    std::vector<std::array<float, 3>> origins;  // per static model
    std::string animatedModel;  // project-relative .glb/.fbx ("" = none)
    std::string material;       // project-relative .mtl
    int textures = 0;
    std::vector<std::string> notes;
};

// Writes the planned models into <projectDir>/<destFolderRel>/ (normally
// "res/models", or "res/models/<name>" for a split import). Every texture is
// written as a PNG next to the .mtl; existing files with the same name are
// replaced.
bool write(const Source& src, const std::vector<Planned>& models,
           const Options& opt, const std::string& projectDir,
           const std::string& destFolderRel, Result& out, std::string& error);

// The static twin of an animated model already in the project: writes
// <stem>.obj (+ .mtl/textures unless they already exist) next to it, bind
// pose, one model. Used when a .glb/.fbx is placed as a STATIC Model.
// Returns the project-relative .obj ("" + error on failure).
std::string staticTwin(const std::string& projectDir, const std::string& modelRel,
                       std::string& error);

// "<dir>/<stem>.obj" for a .glb/.fbx ("" for anything else).
std::string staticTwinPath(const std::string& modelRel);
// "<dir>/<stem>.mtl" next to any model.
std::string siblingMaterialPath(const std::string& modelRel);

// File-name-safe version of an authored name ("Baño.001" -> "ba_o-001").
std::string safeName(const std::string& in);

}  // namespace modelimport
