#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Procedural low-poly tree generator (Tools > Tree Generator), inspired by
// EZ-Tree (github.com/dgreenheck/ez-tree, MIT). Host-only, no GL - the
// stochtile/matbake pattern. Everything is deterministic in Params (the seed
// drives a private PCG stream), so the same parameters always produce the
// same mesh, textures and OBJ bytes.
//
// The output is a plain asset: writeAssets() drops <name>.obj/.mtl plus two
// generated PNGs (tiling bark, alpha-cutout leaf card) into res/models/trees/
// and the tree enters the scene as an ordinary Model object - no PS2 runtime
// involvement, the whole model/serialization/codegen chain stays untouched.
namespace treegen {

struct Params {
    uint32_t seed = 1234;

    // Trunk. Height is the tree's overall SIZE: everything else that carries a
    // world dimension (trunk radius, leaf card) is a fraction of it, so the
    // Height slider scales the tree instead of stretching it into a thinner
    // and thinner pole - which is what absolute radii used to do.
    float height = 7.0f;        // trunk length, world units
    float thickness = 0.043f;   // base radius as a fraction of height
    float flare = 1.5f;         // base widening multiplier (1 = none)
    float taper = 0.55f;        // radius fraction kept along a branch
    float gnarliness = 0.12f;   // random per-ring bend
    float sweep = 0.12f;        // upward pull per ring (negative = droop)

    // Crown architecture. "Spread" is the deciduous habit the recursion has
    // always produced: children spiral up each parent and the shape emerges.
    // "Conical" is the conifer habit - the trunk runs unbroken to the apex and
    // carries WHORLS of branches whose length falls off toward the top, which
    // is what actually makes a Christmas-tree silhouette. A cone cannot be
    // coaxed out of the spread rules: their length falloff is a jittered ratio
    // per generation, not a profile along the trunk.
    int crown = 0;              // 0 spread (broadleaf), 1 conical (conifer)
    int whorls = 9;             // conical: branch rings up the trunk

    // Branching. Level 0 is the trunk; children[L] branches sprout from every
    // level-L branch while L + 1 < levels. In conical mode children[0] is read
    // as the count PER WHORL instead.
    int levels = 3;             // 1..4
    int children[3] = {3, 3, 2};
    float branchAngle = 45.0f;  // child tilt away from the parent axis, deg
    float angleJitter = 10.0f;  // random +/- on the tilt, deg
    float lengthRatio = 0.60f;  // child length vs parent length
    float lengthTaper = 0.25f;  // children near the tip are this much shorter
    float radiusRatio = 0.58f;  // child base radius vs parent radius there
    float spawnStart = 0.30f;   // children sprout after this parent fraction

    // Tessellation - the poly-budget levers. Radial sides and length rings
    // interpolate from the trunk values down to the *Min values on the
    // outermost level.
    int sides = 6, sidesMin = 3;
    int rings = 5, ringsMin = 2;

    // Leaves (0 = bare tree). Quads spread over the last leafLevels branch
    // generations, each 2 triangles with the alpha-cutout leaf card.
    int leafCount = 130;
    float leafSize = 0.086f;    // quad width as a fraction of height
    float leafAspect = 1.15f;   // quad height / width
    int leafLevels = 2;

    // Procedural textures
    int barkStyle = 0;          // 0 rough bark, 1 birch, 2 plates
    int leafStyle = 0;          // 0 broadleaf cluster, 1 needles, 2 single leaf
    float barkColor[3] = {0.42f, 0.30f, 0.20f};   // highlights
    float barkColor2[3] = {0.24f, 0.16f, 0.10f};  // crevices
    float leafColor[3] = {0.32f, 0.52f, 0.18f};   // lit blade
    float leafColor2[3] = {0.15f, 0.33f, 0.10f};  // shaded blade
};

// Flat triangle lists, pos3 + normal3 + uv2 per vertex (24 floats per
// triangle). UVs are image-space (v = 0 at the top texture row) - the same
// convention objparser produces after its load-time flip; the OBJ writer
// flips v back to OBJ bottom-left space.
struct Mesh {
    std::vector<float> bark;
    std::vector<float> leaves;  // drawn with the alpha-cutout leaf card
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    int barkTriangles() const { return (int)bark.size() / 24; }
    int leafTriangles() const { return (int)leaves.size() / 24; }
    int triangles() const { return barkTriangles() + leafTriangles(); }
};

Mesh generate(const Params& p);

struct Image {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};

// Both bake at power-of-two sizes (GS-friendly). The leaf card uses hard
// binary alpha (0/255) so the palettized tRNS->CLUT path keeps the cutout
// intact, with opaque colors dilated into the transparent margin so bilinear
// sampling never rings dark fringes.
Image bakeBarkTexture(const Params& p, int size = 128);
Image bakeLeafTexture(const Params& p, int size = 128);

// Writes res/models/trees/<name>.obj + .mtl + -bark.png + -leaf.png under
// projectDir (creates the folder). `name` must already be filesystem-safe.
// On success outObjRel holds the project-relative OBJ path (forward slashes).
bool writeAssets(const std::string& projectDir, const std::string& name,
                 const Params& p, const Mesh& mesh, const Image& bark,
                 const Image& leaf, std::string* outObjRel,
                 std::string* outError);

// Tuned starting points for the Tree Generator UI (index 0 = default).
struct Preset {
    const char* name;
    Params params;
};
const std::vector<Preset>& presets();

}  // namespace treegen
