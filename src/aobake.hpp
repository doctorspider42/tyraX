#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "objparser.hpp"
#include "project.hpp"

// Baked ambient occlusion (docs/ambient-occlusion.md). Host-only, no GL - the
// decalproj/navmesh pattern. Three independent bakes, all folded into the same
// vertex colors the directional light already bakes into:
//
//  - terrainAO(): heightmap self-occlusion (ravines, foot of hills), a u8 grid
//    per terrain vertex. This is the EDITOR VIEWPORT's copy - it multiplies the
//    grid into its terrain chunk colors - while the console reads the per-texel
//    terrainAOMap below. A preview and its subject, so the term that decides
//    the answer (tangentSin, the horizon measured above the surface's own
//    tangent plane rather than above the horizontal) is shared; the azimuth
//    COUNT is not, because one is a per-cell grid and the other a 256^2 image.
//  - collectOccluders(): solid scene objects reduced to analytic occluders
//    (oriented boxes / spheres). The RESPONSE to these is computed per vertex
//    on the EE at scene load (templates.cpp aoOccludersAt/aoGroundAt) and per
//    fragment in the viewport shader (viewport.cpp FS) - those two are twins;
//    this function is the single source of the occluder SHAPES.
//  - modelAO(): raycast self-occlusion for imported .obj models, one u8 per
//    obj position index. texbake writes it as a "<model>.obj.ao" sidecar into
//    the .res-baked mirror (read by the engine's LeanObjLoader); the viewport
//    bakes the same values into its model vertex colors.
namespace aobake {

// One analytic occluder approximating a solid scene object. axis[k] is the
// object's local axis k in world space (rows of the world->local rotation);
// half = local half-extents. sphere: half[0] is the radius, axes unused.
struct Occluder {
    float pos[3] = {0, 0, 0};
    float axis[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float half[3] = {0.5f, 0.5f, 0.5f};
    bool sphere = false;
    int objIndex = -1;  // authored scene-object index (self-exclusion)
};

// modelAabb: fill the local-space AABB of a Model object's mesh; return false
// to skip it (missing file, animated .glb - those relight dynamically and are
// never occluders).
using ModelAabbFn =
    std::function<bool(const SceneObject&, float mn[3], float mx[3])>;

// Solid authored objects -> occluder list, in object order. Markers, lights,
// decals, mirrors, portals and animated models cast nothing.
std::vector<Occluder> collectOccluders(const std::vector<SceneObject>& objects,
                                       const ModelAabbFn& modelAabb);

// One emissive light source: an object whose material both glows (Ke) and
// declares a reach ("# tyra-glow-light" - Material Editor > Glow > Lights up
// surroundings). docs/emissive-materials.md. Geometrically identical to an
// Occluder - the SAME analytic box/sphere - because the response is the same
// distance-to-shape query; only the formula on top differs (light added
// instead of occlusion subtracted).
struct Emitter {
    Occluder shape;
    float color[3] = {1, 1, 1};  // the material's emission color (Ke, clamped)
    float range = 0.0f;          // world units the light reaches
    float bright = 1.0f;         // brightness at the emitter's surface
};

// The emission an object's assigned .mtl declares (its FIRST material - the
// entry primitives take their surface from).
struct MaterialGlow {
    float ke[3] = {0, 0, 0};  // Ke, the emission floor
    float color[3] = {1, 1, 1};  // the AUTHORED glow color - what it lights
                                 // the room with (Ke carries the white-hot
                                 // core, which is exposure, not hue)
    float kd[3] = {1, 1, 1};  // Kd - the lightmap bake folds it into the light
    bool textured = false;    // has a map_Kd (see the lightmap light channel)
    float range = 0.0f;       // "# tyra-glow-light" reach, 0 = lights nothing
    float light = 1.0f;       // ...and its strength
    bool glows() const { return ke[0] > 0.0f || ke[1] > 0.0f || ke[2] > 0.0f; }
};
// Per-path cache of the above. collectEmitters READS .mtl files, so the
// viewport (which rebuilds its emitter set every frame) must hand in the same
// instance every time and clear it when materials change on disk; one-shot
// callers like codegen can pass nullptr.
using GlowCache = std::map<std::string, MaterialGlow>;

// Emissive-light emitters of a scene, in object order. Unlike occluders these
// ignore castShadow (a glowing sign that casts no shadow still lights the wall
// behind it) but need the material, hence projectDir for path resolution. An
// object whose material has no Ke or no reach contributes nothing.
std::vector<Emitter> collectEmitters(const std::string& projectDir,
                                     const std::vector<SceneObject>& objects,
                                     const ModelAabbFn& modelAabb,
                                     GlowCache* cache = nullptr);

// Shadow rays per emitter (docs/emissive-materials.md, "Shadows"). These are
// AREA sources, so one ray to the nearest surface point can only ever answer
// "lit" or "black" and paints a hard rectangular edge. N rays spread across the
// emitter's silhouette turn that into a penumbra: the unblocked FRACTION is a
// visibility multiplier, and the band widens with distance from the caster
// exactly as a real one does.
//
// Ray 0 is always the nearest-surface-point ray, so N = 1 reproduces the old
// hard shadow bit for bit - which is what the generated game's per-vertex path
// keeps (kEmisShadowSamplesVertex, see the divergence note in the docs).
constexpr int kEmisShadowSamples = 8;
constexpr int kEmisShadowSamplesVertex = 1;

// Deterministic Vogel-disk offsets for rays 1..N-1, in units of the emitter's
// projected half-extent perpendicular to the ray. No RNG - the same bake twice
// is the same bytes twice. Every offset is distinct in BOTH axes, which is what
// makes a straight shadow edge resolve into N levels instead of the 3 a square
// grid would give.
constexpr float kEmisShadowDisk[7][2] = {
    {0.267261f, 0.000000f},   {-0.341335f, 0.312691f},
    {0.052247f, -0.595326f},  {0.430231f, 0.561160f},
    {-0.789527f, -0.139656f}, {0.747909f, -0.475759f},
    {-0.250161f, 0.930586f},
};

// Sub-samples per texel per axis in the per-texel bakes. A texel is the mean
// over its own footprint, not a point sample at its centre: shadow edges here
// are very nearly hard (a fixture 0.3 units off a wall throws a penumbra of
// centimetres, far under one texel), and point-sampling one writes a
// full-amplitude step between neighbouring texels that bilinear filtering then
// reconstructs as a visible staircase. Averaging band-limits the edge to what
// the grid can carry. Host-side cost only - the bake is kSuper^2 times the
// samples, the console draws exactly what it drew before.
constexpr int kSuper = 4;

// ...but the GI light channel samples on a 2x2 sub-grid instead. Every GI
// sample is already the mean over `rays` hemisphere directions, so it costs
// hundreds of rays where an analytic emitter costs one slab test - and the
// remaining aliasing it would fix is a shadow edge that 4 hemisphere gathers
// already resolve. 4x4 here turned a 20-second bake into a 80-second one for
// no visible difference (measured).
constexpr int kSuperGi = 2;

// Alpha floor for every texel of a lightmap image. NOT cosmetic: StaPip draws
// with the GS alpha test set to "pass only when alpha != 0" (the cutout rule
// that makes foliage and decals work - stapip_qbuffer_renderer.cpp). Both
// lightmap passes sample the SAME texture, so a texel whose OCCLUSION is zero
// used to fail that test and throw the additive LIGHT pass away with it: the
// baked light was silently clipped to wherever the ambient occlusion happened
// to be non-zero, punching hard, texel-aligned holes into every lit surface.
// The engine's PNG loader scales alpha 0..255 -> 0..128 by integer division,
// so 1 would still land on 0; 2 is the smallest value that survives, and it
// darkens by 1/128 - under one framebuffer level.
constexpr uint8_t kMinLightmapAlpha = 2;

// Host reference of the emissive-light response at a surface point: the
// per-channel light this emitter adds. Twin of emissiveLightAt in the
// generated game (templates.cpp) and emissiveLight in the viewport fragment
// shader - change one, change all three.
// blockers (optional): occluders that SHADOW this emitter - the light is scaled
// by the fraction of `shadowSamples` rays that reach the emitter. Pass the same
// shapes collectOccluders returns; the receiver's own occluder must already be
// out of the list (the rays start on it) and the emitter's own is skipped here
// (they end on it). The shapes are analytic, so a wall still throws a rectangle
// rather than its silhouette - only the EDGE of that rectangle softens.
void emitterLightAt(const Emitter& em, const float wp[3], const float n[3],
                    float outRgb[3],
                    const std::vector<const Occluder*>* blockers = nullptr,
                    int shadowSamples = kEmisShadowSamples);

// True when the segment from `origin` along unit `dir` for `maxT` units enters
// the shape. Twin of emisShadowed in the generated game and shadowHit in the
// viewport shader.
bool shapeBlocksRay(const Occluder& oc, const float origin[3],
                    const float dir[3], float maxT);

// Host reference of the occluder response formula at a surface point
// (0..1 occlusion; range = aoRadius). The generated game (aoOccluderAt in
// templates.cpp, per vertex at load) and the viewport fragment shader
// (aoOcclusion) are twins of THIS function - change one, change all three.
// The textured AO mode rasterizes it into maps/atlases here on the host.
float occluderOcclusionAt(const Occluder& oc, const float wp[3],
                          const float n[3], float range);

// A light source that REPLACES the emissive-emitter gather in the two
// per-texel bakes below. This is the seam baked global illumination plugs into
// (gibake, docs/global-illumination.md): the lightmap's RGB channel stops
// meaning "baked emissive light" and starts meaning "incoming light, all
// sources, all bounces" - same image, same two passes, same VRAM.
//   wp/n  = the surface point and its normal
//   seed  = a stable per-sample identity (the texel's atlas coordinate, or the
//           probe's index in the importance pre-pass) - the callee rotates its
//           sample spiral by it, so a texel's rays are a property of that texel
//           and the bake is bit-identical at any core count
//   out   = light in the same units emitterLightAt writes (1.0 = full white)
using LightFn = std::function<void(const float wp[3], const float n[3],
                                   uint32_t seed, float outRgb[3])>;

// --- the AO textures (how the bake ships) -----------------------------------

// A square, power-of-two terrain lightmap. Exactly the two channels the
// primitive atlas below carries, for the same reason (one RGBA32 image the GS
// reads twice, the pass's vertex color picking the channels):
//   alpha = strength * occlusion (255 = darken fully), read by an alpha-over
//     pass with BLACK vertex colors - an exact per-pixel multiply
//     (Cd * (1 - a));
//   light = baked emissive light in framebuffer units, read by an additive
//     pass whose vertex color carries the terrain's own base tint.
// Either channel may be empty; the image ships when at least one has content.
struct AoImage {
    int size = 0;  // 0 = nothing to bake
    std::vector<uint8_t> alpha;  // size*size
    std::vector<uint8_t> light;  // size*size*3
    bool hasAlpha = false;
    bool hasLight = false;
    // The light channel came from the GI integrator rather than the emissive
    // emitters, so it carries ALL the incoming light: the generated terrain
    // must then drop its own ambient + directional + point-light shade, or
    // the scene is lit twice.
    bool gi = false;
};

// Terrain lightmap covering the full terrain extent: per-texel heightmap
// self-occlusion (the same horizon scan as terrainAO, on bilinear heights)
// plus the occluder contact term into the alpha, and the emissive light the
// `ems` reach - shadowed by `occs` - into the RGB.
// aoOn = the scene's ambient-occlusion preference; with it off (or no
// emitters) the corresponding channel is simply not baked, so a scene pays
// only for the passes it actually needs.
AoImage terrainAOMap(const std::vector<float>& heights, int w, int d,
                     float width, float depth,
                     const std::vector<Occluder>& occs,
                     const std::vector<Emitter>& ems, float radiusWorld,
                     float strength, bool aoOn, const LightFn* gi = nullptr);

// One atlas region: normalized UV rect (inset by half a texel against
// bilinear bleed). A primitive's base-texture UVs map into it 1:1.
struct AtlasRect {
    float u0 = 0, v0 = 0, du = 0, dv = 0;
};

// Per-scene LIGHTMAP atlas for the primitives (box/sphere/cylinder/cone/
// plane/save point - types whose generated UV layout is known analytically).
// Regions follow the generated builders' emission order exactly: box 6 faces
// (+X,-X,+Y,-Y,+Z,-Z), sphere 1, cylinder 3 (side, +Y cap, -Y cap), cone 2
// (side, base), plane 2 (top, bottom). Deterministic - codegen emits the
// rects and texbake writes the pixels from two independent calls.
//
// ONE RGBA32 image carries both bakes, because both are per-texel functions of
// the same surface point and the GS can read the same texture twice:
//   A   = ambient occlusion -> an alpha-over pass with BLACK vertex colors,
//         which is an exact per-pixel multiply (Cd * (1 - a));
//   RGB = baked emissive light -> an additive pass with WHITE vertex colors
//         (texturing is MODULATE, so the vertex color picks which channels of
//         the shared texture that pass sees).
// Going per texel is what kills the Gouraud artifacts: baked light lands on
// vertices otherwise, and a two-triangle box face shows the diagonal split as
// a hard seam under any strong gradient (docs/emissive-materials.md).
struct SceneLightAtlas {
    int size = 0;                  // atlas dimension, 0 = no atlas
    std::vector<uint8_t> alpha;    // size*size occlusion alpha
    std::vector<uint8_t> light;    // size*size*3 emissive light, framebuffer
                                   // units (the additive pass adds it raw)
    std::vector<int> firstRegion;  // per authored object, -1 = not in atlas
    // Per authored object: its light channel has content, so the game must
    // draw the additive pass for it AND leave the emissive light out of its
    // vertex colors. 0 for a TEXTURED receiver even when emitters reach it -
    // a flat add would blow out dark texels (the vertex path multiplies the
    // texture instead), so those keep the per-vertex light.
    std::vector<char> lit;
    std::vector<AtlasRect> rects;  // flat regions, builder order
    // The light channel came from the GI integrator: every `lit` object must
    // then drop its ambient + directional + point + emissive vertex shade,
    // because the atlas now carries all of it (docs/global-illumination.md).
    bool gi = false;
};
SceneLightAtlas bakeSceneLightAtlas(const Project& p, const SceneData& sc,
                                    const ModelAabbFn& modelAabb,
                                    const LightFn* gi = nullptr);

// Terrain self-occlusion: an 8-direction horizon scan over the heightmap
// (w x d vertex grid, row-major [z*w+x], cell size stepX/stepZ world units).
// Scans up to radiusWorld out. Returns one byte per grid vertex, 255 = open
// sky, 0 = fully occluded.
std::vector<uint8_t> terrainAO(const std::vector<float>& heights, int w, int d,
                               float stepX, float stepZ, float radiusWorld);

// Raycast self-occlusion for an imported .obj model: a deterministic
// cosine-weighted hemisphere (24 rays) around the averaged vertex normal,
// traced against the model's own triangles (XZ-grid accelerated). Returns one
// byte per obj position index (objparser::Submesh::posIdx), 255 = open.
// Model-local, placement-independent - baked once per asset.
// CURRENTLY UNUSED (owner call, 2026-07): per-vertex occlusion on authored
// low-poly meshes reads as triangulated shading, so texbake no longer writes
// the .aov sidecars and the game stages models AO-off. Kept - with the
// LeanObjLoader sidecar reader - for a future per-model lightmap-unwrap path.
std::vector<uint8_t> modelAO(const objparser::Model& m);

// Sidecar IO ("TXAO" + u32 LE count + bytes). The engine-side reader lives in
// lean_obj_loader.cpp - keep the format in sync.
bool writeModelAoSidecar(const std::string& path,
                         const std::vector<uint8_t>& ao);

// Fast local-space AABB of an .obj (scans `v` lines only) - the ModelAabbFn
// for codegen, where parsing every placed model fully would be waste.
bool objAabb(const std::string& path, float mn[3], float mx[3]);

}  // namespace aobake
