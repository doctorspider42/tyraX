#include "shadowbake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <utility>

#include "bakepar.hpp"
#include "bvh.hpp"
#include "decalproj.hpp"
#include "gibake.hpp"
#include "wire.hpp"

namespace fs = std::filesystem;

namespace shadowbake {
namespace {

constexpr uint32_t kCacheMagic = 0x4448534Du;  // "MSHD"
// Bumped whenever the bake's OUTPUT changes shape or value. It rides in the
// signature, so a bump stales every cache without anything else having to
// know.
constexpr uint32_t kCacheVersion = 5;  // 5: search past the fade; sane no-limit

constexpr float kPi = 3.14159265358979f;
// How far past the caster's own extent the receiver search starts. The common
// case is a crate resting ON the ground, where the caster's far side and the
// receiver are the same plane to the float - without this the search finds the
// crate's own bottom face and bakes the shadow at zero distance, i.e. with no
// penumbra at all.
constexpr float kCasterClearance = 0.01f;
// Lifts a shadow-ray origin off the surface it starts on.
constexpr float kRayBias = 0.002f;

// The lift used when asking "can this receiver point see the sun". Twenty-five
// times kRayBias, because this ray leaves along the LIGHT rather than along the
// surface normal, and a low sun grazes: at 18 degrees of elevation a lift of b
// clears a flat floor by only b*sin(18) = 0.31b. This much leaves ~15 mm on a
// metre-unit scene, far above the float epsilon the tree resolves at.
constexpr float kSunBias = 0.05f;
// How dark the tile's own colour is. The blend multiplies the receiver toward
// this, so it has to stay near black or it starts replacing what it shades;
// the sky's hue rides on top of it, which is what keeps shade cool instead of
// neutral grey. 0.10 reads as "lit by sky alone" against the ~0.5 the sun
// contributes, without flattening a dark receiver to nothing.
constexpr float kShadowTintValue = 0.10f;
// Where the shadow starts fading, as a fraction of the reach, and how much
// further than that the search still looks.
//
// `maxLength` has to exist - a 7-degree sun throws a shadow eight times the
// caster's height, and spreading 64 texels over that is what makes a long
// shadow look like a smear - but ending it at the projector's own face draws a
// STRAIGHT LINE across the ground, which reads as a bug and was reported as
// one. Two things are needed to get rid of that line and it took measuring the
// atlas to see the second: the alpha has to RAMP down (the penumbra widens with
// distance, so a real shadow does dissolve), and the search has to keep going
// AFTER the ramp starts, or a receiver that steps away leaves a texel with no
// hit at all sitting next to one at full strength - a hard edge no ramp can
// soften, because the ramp never runs.
constexpr float kReachFadeFrom = 0.55f;
constexpr float kSearchBeyond = 1.8f;
// Slack around the caster's silhouette, as a fraction of the tile. The
// penumbra opens outward from the geometric shadow, so a tile cut exactly to
// the silhouette clips its own soft edge - and the outer ring must stay at
// alpha 0 anyway, because bilinear filtering reaches across an atlas cell.
constexpr float kFootprintMargin = 0.12f;

struct V3 {
    float x, y, z;
};
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 normalize(V3 v) {
    const float l = std::sqrt(dot(v, v));
    return l > 1e-8f ? V3{v.x / l, v.y / l, v.z / l} : V3{0, 1, 0};
}

inline void mix64(uint64_t& h, uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}
inline void mixF(uint64_t& h, float f) {
    // The .tyra stores six significant digits, so hashing raw bits would make
    // every bake read as stale after one save/load round trip - the procgen
    // bakeHash lesson, paid for once already.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", (double)f);
    for (const char* c = buf; *c; ++c) mix64(h, (uint64_t)(unsigned char)*c);
}
inline void mixS(uint64_t& h, const std::string& s) {
    mix64(h, s.size());
    for (char c : s) mix64(h, (uint64_t)(unsigned char)c);
}

// One 32-bit hash of a texel's own identity. Every random choice a texel makes
// derives from this and from nothing else, which is what makes the bake
// independent of how the work was partitioned.
inline uint32_t texelSeed(int caster, int x, int y) {
    uint32_t h = 2166136261u;
    for (uint32_t v : {(uint32_t)caster, (uint32_t)x, (uint32_t)y}) {
        h = (h ^ v) * 16777619u;
        h ^= h >> 13;
    }
    return h;
}
inline float rand01(uint32_t h) { return (float)(h & 0xFFFFFFu) / 16777216.0f; }

// --- geometry ----------------------------------------------------------------

// World AABB of an object, from the same triangles the projection reads. An
// object with no triangles of its own (an animated model, a marker) reports
// false, which is how `plan` refuses it by name instead of baking nothing.
bool objectBounds(const Project& p, const SceneObject& o, V3& lo, V3& hi,
                  std::vector<float>* tris) {
    std::vector<float> local;
    std::vector<float>& t = tris ? *tris : local;
    const size_t before = t.size();
    if (!decalproj::objectTriangles(p, o, t)) return false;
    lo = {1e30f, 1e30f, 1e30f};
    hi = {-1e30f, -1e30f, -1e30f};
    for (size_t i = before; i + 2 < t.size(); i += 3) {
        lo = {std::fmin(lo.x, t[i]), std::fmin(lo.y, t[i + 1]),
              std::fmin(lo.z, t[i + 2])};
        hi = {std::fmax(hi.x, t[i]), std::fmax(hi.y, t[i + 1]),
              std::fmax(hi.z, t[i + 2])};
    }
    return hi.x >= lo.x;
}

// Euler angles (degrees, the project's Rz*Ry*Rx convention) that reproduce an
// orthonormal basis. The projector is handed to decalproj as an ordinary
// SceneObject, so this is how a basis becomes one - and the caller then reads
// the basis BACK through decalproj::projectorBasis rather than trusting this
// to be exact, because at gimbal lock the roll folds into yaw and the frame
// that comes out is a different (equally valid) one.
void eulerFromBasis(V3 ax, V3 ay, V3 az, float out[3]) {
    const float deg = 180.0f / kPi;
    const float sy = -ax.z;
    const float cy = std::sqrt(std::fmax(0.0f, 1.0f - sy * sy));
    if (cy > 1e-4f) {
        out[0] = std::atan2(ay.z, az.z) * deg;
        out[1] = std::asin(std::fmax(-1.0f, std::fmin(1.0f, sy))) * deg;
        out[2] = std::atan2(ax.y, ax.x) * deg;
    } else {
        // Looking straight down world -Z or +Z: pitch is +-90 and roll and yaw
        // are the same rotation. Put all of it in yaw.
        out[0] = 0.0f;
        out[1] = (sy > 0.0f ? 90.0f : -90.0f);
        out[2] = std::atan2(-ay.x, ay.y) * deg;
    }
}

}  // namespace

Options optionsOf(const ProjectSettings& s) {
    Options o;
    o.tileRes = (s.bakedShadowRes == 32 || s.bakedShadowRes == 128)
                    ? s.bakedShadowRes
                    : 64;
    o.sunAngleDeg = s.bakedShadowSunAngle;
    o.strength = s.bakedShadowStrength;
    o.maxLength = s.bakedShadowMaxLength;
    return o;
}

int Bake::triangles() const {
    size_t v = 0;
    for (const Group& g : groups) v += g.verts.size();
    return (int)(v / 15);  // 5 floats per vertex, 3 vertices per triangle
}

int Bake::vramWords() const {
    // kPageSize^2 RGBA32 = one whole page-aligned block per image
    // (docs/gs-vram.md): 256x256x4 bytes = 65536 words.
    return (int)pages.size() * kPageSize * kPageSize;
}

// --- the plan ----------------------------------------------------------------

std::string quickRefusal(const SceneObject& o) {
    // Everything that can move invalidates a bake the moment it does, and the
    // bake cannot tell afterwards. Say so by name rather than baking a shadow
    // that will be in the wrong place.
    if (o.type == PrimitiveType::Vehicle)
        return "it is a vehicle - use a blob or projected silhouette that follows it";
    if (o.physics) return "it is a physics body - a baked shadow cannot follow it";
    if (o.pickable) return "it can be carried - a baked shadow would stay behind";
    if (o.saveState)
        return "a loaded save repositions it, and the shadow would not follow";
    if (o.type == PrimitiveType::Model && isAnimatedModelPath(o.modelPath))
        return "animated models have no static shape to trace - use a blob or a "
               "projected silhouette";
    return std::string();
}

Plan plan(const Project& p, const SceneData& sc, const Options& opt) {
    Plan pl;
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    // The sun at the BAKED hour, resolved exactly the way gibake resolves it -
    // resolvedSettings already folds the ambience preset and its day/night
    // cycle, so the shadow decal and the GI lightmap cannot disagree about
    // where the light is.
    const V3 sun = normalize({rs.lightDir[0], rs.lightDir[1], rs.lightDir[2]});
    pl.sunDir[0] = sun.x, pl.sunDir[1] = sun.y, pl.sunDir[2] = sun.z;

    std::vector<int> wanted;
    for (int i = 0; i < (int)sc.objects.size(); ++i)
        if (sc.objects[i].shadowMode == 4) wanted.push_back(i);
    if (wanted.empty()) return pl;

    // A running day/night cycle is NOT a refusal - baking at one hour is a
    // legitimate thing to want, and `ambience::bakedHour` is exactly the hour
    // the GI bake freezes too. But the sun sweeps at runtime and these shadows
    // do not, so it is said out loud rather than left to be discovered.
    {
        const int ai = project::ambienceIndexFor(p, sc);
        if (ai >= 0 && p.ambiencePresets[ai].cycle.enabled)
            pl.warning =
                "this scene's day/night cycle moves the sun at runtime, and a "
                "baked shadow does not - it is traced at the cycle's baked "
                "hour and stays there";
    }

    // A light below the horizon throws no shadow anybody can stand in, and a
    // light AT it throws one of unbounded length. The runtime silhouette makes
    // the same call from the other side (docs/day-night-cycle.md clamps the
    // resolved elevation to +5 degrees), so this only fires on a hand-authored
    // direction.
    if (sun.y < 0.09f) {  // ~5 degrees
        pl.note =
            "the scene's light is at or below the horizon - a baked shadow "
            "would have no length";
        for (int i : wanted) pl.refused.push_back({sc.objects[i].name, pl.note});
        return pl;
    }

    for (int i : wanted) {
        const SceneObject& o = sc.objects[i];
        const auto refuse = [&](const std::string& why) {
            pl.refused.push_back({o.name, why});
        };
        if (const std::string q = quickRefusal(o); !q.empty()) {
            refuse(q);
            continue;
        }
        V3 lo{}, hi{};
        if (!objectBounds(p, o, lo, hi, nullptr)) {
            refuse("it has no static geometry to cast from");
            continue;
        }

        // --- the projector -----------------------------------------------
        // +Z looks AT the sun, so the tile is the view from the light and
        // decalproj keeps exactly the surfaces the sun can see.
        const V3 az = sun;
        // Any horizontal reference that is not parallel to the sun. The sun is
        // at least 5 degrees above the horizon here, so world +Y never is.
        const V3 ax = normalize(cross({0, 1, 0}, az));
        const V3 ay = cross(az, ax);

        const V3 centre = (lo + hi) * 0.5f;
        const V3 half = (hi - lo) * 0.5f;
        // Extent of the caster's box on the projector's own axes: the support
        // of an AABB along an arbitrary direction is the dot of its half-sizes
        // with the direction's absolute components.
        const auto support = [&](V3 a) {
            return std::fabs(a.x) * half.x + std::fabs(a.y) * half.y +
                   std::fabs(a.z) * half.z;
        };
        const float ex = support(ax), ey = support(ay), ez = support(az);

        // How far the shadow is allowed to run. The caster's own height is the
        // unit, because that is what a person means by "this shadow is too
        // long".
        //
        // 0 means "no limit", and that used to be literally 1e4 - which fed
        // straight into the penumbra term below and blew the footprint out to
        // hundreds of units, so the caster's silhouette fell below one texel
        // and the shadow all but vanished. "No limit" has to mean the largest
        // distance that can still land on something, not an arbitrary huge
        // number: across the map and back is that bound.
        const float sceneSpan =
            std::sqrt((float)sc.terrain.width * (float)sc.terrain.width +
                      (float)sc.terrain.depth * (float)sc.terrain.depth);
        float reach = (opt.maxLength > 0.0f) ? opt.maxLength * (hi.y - lo.y)
                                             : sceneSpan;
        if (reach < 0.5f) reach = 0.5f;
        // Search FURTHER than the shadow fades. Where a receiver steps away -
        // the edge of a quay, a stair, a drop to the water - the next surface
        // down is a long way further along the light, and a search that stops
        // at the fade distance finds nothing there at all. The texel beside it
        // found the upper surface at full strength, so the tile goes 140 to 0
        // across one row and the shadow ends on a straight line. Measured on
        // examples/showcase, which is where it was reported.
        const float search = reach * kSearchBeyond;
        // Depth spans the caster PLUS the search beyond it. The extra
        // clearance at the front keeps the +Z face outside the caster, so a
        // ray never starts inside the geometry it is about to skip.
        const float depth = 2.0f * ez + search + 2.0f * kCasterClearance;
        // The footprint has to hold the caster's own silhouette plus however
        // far the penumbra opens over `reach`, plus a margin that keeps the
        // tile's outer ring empty. Deliberately `reach` and not `search`: the
        // extra search depth must not cost tile resolution, because by then
        // the shadow has faded out anyway.
        const float penumbra =
            reach * std::tan(0.5f * opt.sunAngleDeg * kPi / 180.0f);
        float width = 2.0f * (ex + penumbra), height = 2.0f * (ey + penumbra);
        width *= 1.0f + 2.0f * kFootprintMargin;
        height *= 1.0f + 2.0f * kFootprintMargin;

        Caster c;
        c.object = i;
        c.name = o.name;
        c.layer = o.layer;
        // The projector's centre sits half a depth back from the caster's
        // front face, along the light.
        const V3 front = centre + az * (ez + kCasterClearance);
        const V3 pos = front - az * (depth * 0.5f);
        c.position[0] = pos.x, c.position[1] = pos.y, c.position[2] = pos.z;
        eulerFromBasis(ax, ay, az, c.rotation);
        c.scale[0] = width, c.scale[1] = height, c.scale[2] = depth;
        c.casterDepth = 2.0f * ez + 2.0f * kCasterClearance;
        c.fadeReach = reach;
        pl.casters.push_back(c);
    }
    return pl;
}

// --- the bake ----------------------------------------------------------------

namespace {

// The synthetic decal a Caster is handed to decalproj as.
SceneObject projectorObject(const Caster& c) {
    SceneObject d;
    d.type = PrimitiveType::Decal;
    d.decalProject = true;
    // A sentinel id rather than none. decalproj skips a receiver whose id
    // equals the projector's, and `ensureObjectIds` makes an empty id
    // impossible in a loaded project - but a projector that matched everything
    // with a blank id would project onto nothing at all, which is exactly the
    // kind of failure this feature cannot afford to have silently.
    d.id = "\x01shadow-projector";
    for (int k = 0; k < 3; ++k) {
        d.position[k] = c.position[k];
        d.rotation[k] = c.rotation[k];
        d.scale[k] = c.scale[k];
    }
    return d;
}

// K directions inside a cone of half-angle `half` around `axis`, laid out on
// the golden spiral and rotated by the texel's own hash. Deterministic by
// construction: no shared state, no counter.
void coneDirections(V3 axis, float half, int count, uint32_t seed,
                    std::vector<V3>& out) {
    out.clear();
    out.reserve(count);
    const V3 t = std::fabs(axis.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0};
    const V3 u = normalize(cross(t, axis));
    const V3 v = cross(axis, u);
    const float cosHalf = std::cos(half);
    const float phase = rand01(seed) * 2.0f * kPi;
    const float golden = kPi * (3.0f - std::sqrt(5.0f));
    for (int i = 0; i < count; ++i) {
        // Uniform over the spherical cap: cos t linear in the sample index.
        const float ct = 1.0f - (1.0f - cosHalf) * ((i + 0.5f) / count);
        const float st = std::sqrt(std::fmax(0.0f, 1.0f - ct * ct));
        const float a = phase + golden * i;
        out.push_back(normalize(axis * ct + u * (st * std::cos(a)) +
                                v * (st * std::sin(a))));
    }
}

}  // namespace

Bake bakeScene(const Project& p, int sceneIndex, const std::atomic<bool>* cancel,
               const ProgressFn& progress) {
    Bake out;
    if (sceneIndex < 0 || sceneIndex >= (int)p.scenes.size()) return out;
    const SceneData& sc = p.scenes[sceneIndex];
    const Options opt = optionsOf(p.settings);
    out.signature = signature(p, sc, opt);
    out.tileRes = opt.tileRes;
    out.valid = true;
    if (!p.settings.bakedShadows) return out;

    const Plan pl = plan(p, sc, opt);
    if (pl.empty()) return out;

    // THE TILE IS A MULTIPLY, NOT A WASH. The GS blend is Cs*a + Cd*(1-a), so a
    // BRIGHT source colour REPLACES the receiver instead of shading it - which
    // is the mistake this had at first: the tint was the sky at ambient
    // strength (a mid blue-grey) and at 0.85 alpha a shadow on grass came out
    // grey, not dark green. Reported as "these shadows are awfully grey", and
    // that is exactly what the arithmetic says it must look like.
    //
    // A near-BLACK source is what makes alpha-over an exact per-pixel
    // darkening - the trick the scene lightmap's occlusion pass already uses -
    // and darkening preserves hue: shaded grass stays green, brick stays red.
    // A little of the sky's colour is kept so the shade is cool rather than
    // neutral, but at a value low enough that it tints the multiply instead of
    // competing with the surface.
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    {
        // The sky's HUE at a fraction of its value. Normalizing by the
        // brightest channel first means a deep blue sky and a pale one give
        // the same gentle cast rather than the pale one washing out.
        float mx = 0.0f;
        for (int k = 0; k < 3; ++k) mx = std::fmax(mx, rs.skyColor[k]);
        for (int k = 0; k < 3; ++k) {
            const float hue = mx > 1e-4f ? rs.skyColor[k] / mx : 1.0f;
            float c = hue * kShadowTintValue;
            c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            out.tint[k] = (uint8_t)(c * 255.0f + 0.5f);
        }
    }

    // The scene as triangles, once, for the receiver search. gibake::build is
    // the single tessellation of a scene in this repo (primitives through
    // primmesh, static .obj through objparser, the terrain as a heightfield),
    // and reusing it is what stops the shadow landing on a surface the GI bake
    // does not believe in.
    const gibake::Scene scene = gibake::build(p, sc, gibake::settingsOf(p.settings));
    if (scene.empty()) return out;

    // Which receivers the projection may land on. Three exclusions, and each
    // one is a bug if it is left out (docs/shadows.md). Read ONCE - load()
    // decodes a cache file, and asking it per caster would make the bake's
    // cost quadratic in nothing useful.
    const gibake::Bake gi = gibake::load(p, sceneIndex);
    const auto lightmapped = [&](const SceneObject& o) {
        // With a fresh GI bake the sun's shadow is already in the lightmap of
        // every UNTEXTURED primitive - a decal on top of one darkens the same
        // shadow twice. Textured surfaces and models are exactly what the
        // lightmap cannot reach, which is the gap this feature exists to fill.
        if (!gi.valid || !gi.atlas.gi) return false;
        if (o.type == PrimitiveType::Model) return false;
        return o.materialPath.empty() && o.bakedLighting;
    };
    // The ground is never in a layer, so it always qualifies - except where a
    // fresh GI bake already carries the same sun shadow in the terrain map.
    const bool terrainFree =
        sc.terrain.enabled && !(gi.valid && gi.terrain.gi);

    const int res = opt.tileRes;
    const int perPage = (kPageSize / res) * (kPageSize / res);
    const float sunHalf = 0.5f * opt.sunAngleDeg * kPi / 180.0f;

    // Tiles are assigned in plan order, which is scene order, so the packing
    // is deterministic and a re-bake of an unchanged scene produces the same
    // bytes.
    //
    // Casters are walked GROUPED BY LAYER, and a new layer starts on a fresh
    // page. That wastes the tail of a page per layer and buys the property
    // that a page belongs to exactly ONE layer - without it a page would be
    // pinned by whichever layer is resident and could never be freed with the
    // layer that needs it. A project with no layers pays nothing: every caster
    // is in the same (empty) layer and the packing is unchanged.
    std::map<std::string, std::vector<float>> merged;  // key: layer|page
    std::map<std::string, int> mergedCasters;
    int nextCell = 0;
    const int total = (int)pl.casters.size();
    std::vector<int> order(total);
    for (int i = 0; i < total; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return pl.casters[a].layer < pl.casters[b].layer;
    });
    std::string layerRun;
    bool layerRunStarted = false;

    for (int oi = 0; oi < total; ++oi) {
        const int ci = order[oi];
        if (cancel && cancel->load()) return out;
        // Reported at the TOP: most of the paths below end in a `continue`,
        // and a progress call at the bottom would stall the bar on exactly the
        // casters that were skipped.
        if (progress) progress((float)oi / (float)total);
        const Caster& c = pl.casters[ci];
        const SceneObject& src = sc.objects[c.object];
        if (!layerRunStarted || c.layer != layerRun) {
            if (layerRunStarted && nextCell % perPage)
                nextCell += perPage - (nextCell % perPage);  // to a page edge
            layerRun = c.layer;
            layerRunStarted = true;
        }
        const int page = nextCell / perPage;
        if (page >= kMaxPages) {
            out.truncated.push_back(
                {c.name, "the scene ran out of shadow atlas pages (" +
                             std::to_string(kMaxPages) + " x " +
                             std::to_string(kPageSize) + "px); this shadow was "
                             "not baked"});
            continue;
        }

        // --- the caster's own geometry, as its own BVH ---------------------
        // Only the CASTER occludes its tile. The scene tree would be the
        // physically fuller answer and is the wrong one here: two casters
        // standing near each other would each bake the other's shadow into
        // their own tile, and the two projections overlapping would darken the
        // ground twice.
        bvh::Tree casterTree;
        if (!decalproj::objectTriangles(p, src, casterTree.tv)) continue;
        casterTree.tn.assign(casterTree.tv.size(), 0.0f);
        bvh::build(casterTree);
        if (casterTree.empty()) continue;

        // The frame the projection will actually sample the tile in - read
        // back from decalproj rather than assumed, so a gimbal-locked Euler
        // extraction cannot mirror the image (see eulerFromBasis).
        const SceneObject proj = projectorObject(c);
        float org[3], bx[3], by[3], bz[3];
        decalproj::projectorBasis(proj, org, bx, by, bz);
        const V3 pos{org[0], org[1], org[2]};
        const V3 axX{bx[0], bx[1], bx[2]};
        const V3 axY{by[0], by[1], by[2]};
        const V3 axZ{bz[0], bz[1], bz[2]};

        std::vector<uint8_t> tile((size_t)res * res, 0);
        bakepar::parallelFor(res, cancel, [&](int lo, int hi) {
            std::vector<V3> dirs;
            for (int row = lo; row < hi; ++row) {
                // Image row 0 is the top, and the atlas convention this repo
                // already ships is that the row index runs WITH v (aobake's
                // texel loop). The decal's own ST is t = localY + 0.5, so row 0
                // is local y = -0.5.
                const float t = (row + 0.5f) / res;
                const float ly = t - 0.5f;
                for (int col = 0; col < res; ++col) {
                    // Keep the outer ring empty: bilinear filtering reaches
                    // one texel across an atlas cell boundary, and the
                    // neighbouring tile is a different shadow.
                    if (row < 2 || col < 2 || row >= res - 2 || col >= res - 2)
                        continue;
                    const float s = (col + 0.5f) / res;
                    const float lx = 0.5f - s;  // u runs with local -X

                    // Down the light from the projector's sun-side face,
                    // starting past the caster's own extent.
                    const V3 o = pos + axX * (lx * c.scale[0]) +
                                 axY * (ly * c.scale[1]) +
                                 axZ * (0.5f * c.scale[2]);
                    const V3 down = axZ * -1.0f;
                    const float from = c.casterDepth;
                    const float span = c.scale[2] - from;
                    if (span <= 0.0f) continue;
                    const V3 start = o + down * from;
                    const float dir[3] = {down.x, down.y, down.z};
                    const float from3[3] = {start.x, start.y, start.z};
                    bvh::Hit h;
                    if (!bvh::trace(scene.tree, from3, dir, span, true, h))
                        continue;
                    const V3 hit = start + down * h.t;

                    // How much of the sun this receiver point can see, through
                    // the caster alone. The cone is around the projector's own
                    // +Z rather than the raw sun vector: the two agree to the
                    // Euler round trip, and the tile has to be consistent with
                    // the frame it will be sampled in, not with an input.
                    const V3 origin = hit + axZ * kRayBias;
                    coneDirections(axZ, sunHalf, opt.samples,
                                   texelSeed(ci, col, row), dirs);
                    int blocked = 0;
                    const float ro[3] = {origin.x, origin.y, origin.z};
                    for (const V3& d : dirs) {
                        const float rd[3] = {d.x, d.y, d.z};
                        bvh::Hit sh;
                        if (bvh::trace(casterTree, ro, rd, 1e5f, true, sh))
                            ++blocked;
                    }
                    if (!blocked) continue;
                    // Dissolve into the reach limit rather than stopping on
                    // it. This engages ONLY where the limit is what ends the
                    // shadow: a texel whose receiver sits well inside the
                    // reach - ordinary ground close under the caster - has a
                    // small h.t and fades not at all, and with maxLength off
                    // the span is enormous so nothing ever reaches the ramp.
                    // The ramp runs from kReachFadeFrom of the reach all the
                    // way out to the search limit, NOT to the reach: a texel
                    // that steps off an edge and lands on the surface below
                    // has a much larger h.t than the one beside it, and the
                    // whole point is that it still gets SOME shadow rather
                    // than none. Applied whether or not a limit is set - with
                    // no limit the reach is the map's own diagonal, so the
                    // ramp only bites where the shadow has genuinely run out
                    // of map.
                    const float fadeFrom = c.fadeReach * kReachFadeFrom;
                    const float fadeTo = c.fadeReach * kSearchBeyond;
                    float fade = 1.0f;
                    if (h.t > fadeFrom)
                        fade = 1.0f - (h.t - fadeFrom) / (fadeTo - fadeFrom);
                    fade = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade);
                    const float a = 255.0f * opt.strength * fade *
                                    (float)blocked / (float)opt.samples;
                    if (a < 0.5f) continue;
                    tile[(size_t)row * res + col] = (uint8_t)(a + 0.5f);
                }
            }
        });
        if (cancel && cancel->load()) return out;

        // An entirely empty tile means the caster throws nothing anybody can
        // see - a prop in a hole, or a reach shorter than the drop to the
        // floor. Spend no cell and no triangles on it.
        bool any = false;
        for (uint8_t a : tile)
            if (a) {
                any = true;
                break;
            }
        if (!any) continue;

        // --- the projection ------------------------------------------------
        decalproj::Receivers rx;
        rx.accept = [&](const SceneObject& o) {
            if (o.id == src.id) return false;  // never onto the caster itself
            // A shadow belongs to its caster's layer, so a receiver in another
            // one would keep a shadow after the thing that throws it streamed
            // out - or lose the floor from under a shadow that stayed.
            if (!o.layer.empty() && o.layer != c.layer) return false;
            return !lightmapped(o);
        };
        rx.terrain = terrainFree;
        decalproj::DecalMesh mesh = decalproj::project(p, sc, proj, rx);
        if (mesh.verts.empty()) continue;

        // Drop the triangles that fall on FULLY LIT texels. decalproj emits
        // every receiver surface inside the projector volume, which is the
        // right answer for an authored decal and wasteful here: the volume is
        // a long box down the light and most of what it contains is lit
        // ground beside the shadow. Those triangles cost ELF bytes, a raster
        // scan and a texture read to draw nothing.
        //
        // The test is CONSERVATIVE and exact rather than a sample: take each
        // triangle's UV bounding box in texel space, grow it by one texel for
        // the bilinear filter's reach, and keep the triangle if any texel in
        // that box carries alpha. A sampled test would eat a thin shadow that
        // crosses a big triangle without covering a corner.
        //
        // The second test is about DEPTH, and the tile cannot answer it. A
        // tile is one image with one depth per texel, but the projector is a
        // long prism down the light that happily contains a SECOND receiver
        // behind the first: the ground on the far side of a wall the shadow
        // just climbed. decalproj fills the whole box, so that ground gets
        // printed with the same alpha and the shadow reads as passing through
        // the wall. So ask the scene directly - can this point see the sun
        // once the caster itself is discounted - and drop what some third
        // object already shades. Two trees, no third structure: a hit in the
        // scene that is nearer than the caster's own is somebody else's.
        const auto litPastCaster = [&](const V3& at) {
            const V3 o = at + axZ * kSunBias;
            const float ro[3] = {o.x, o.y, o.z};
            const float rd[3] = {axZ.x, axZ.y, axZ.z};
            bvh::Hit hs;
            if (!bvh::trace(scene.tree, ro, rd, 1e5f, true, hs))
                return true;  // clear line to the sun
            bvh::Hit hc;
            if (!bvh::trace(casterTree, ro, rd, 1e5f, true, hc))
                return false;  // blocked, and not by the caster
            return hs.t >= hc.t - kSunBias;  // the caster is what blocks first
        };
        {
            std::vector<float> kept;
            kept.reserve(mesh.verts.size());
            for (size_t t = 0; t + 14 < mesh.verts.size(); t += 15) {
                float u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
                for (int k = 0; k < 3; ++k) {
                    const float u = mesh.verts[t + k * 5 + 3];
                    const float v = mesh.verts[t + k * 5 + 4];
                    u0 = std::fmin(u0, u); u1 = std::fmax(u1, u);
                    v0 = std::fmin(v0, v); v1 = std::fmax(v1, v);
                }
                const auto clampT = [&](float f) {
                    const int i = (int)std::floor(f * res);
                    return i < 0 ? 0 : (i >= res ? res - 1 : i);
                };
                const int x0 = clampT(u0) - 1, x1 = clampT(u1) + 1;
                const int y0 = clampT(v0) - 1, y1 = clampT(v1) + 1;
                bool ink = false;
                for (int y = std::max(0, y0); y <= std::min(res - 1, y1) && !ink; ++y)
                    for (int x = std::max(0, x0); x <= std::min(res - 1, x1); ++x)
                        if (tile[(size_t)y * res + x]) { ink = true; break; }
                if (!ink) continue;
                // Corners plus centroid, and the triangle survives if ANY of
                // them is lit. Conservative on purpose: a triangle straddling
                // the wall's own shadow edge keeps its shadow rather than
                // losing it, because this is an all-or-nothing per-triangle
                // decision and dropping a real shadow is the worse error.
                const V3 p0{mesh.verts[t + 0], mesh.verts[t + 1],
                            mesh.verts[t + 2]};
                const V3 p1{mesh.verts[t + 5], mesh.verts[t + 6],
                            mesh.verts[t + 7]};
                const V3 p2{mesh.verts[t + 10], mesh.verts[t + 11],
                            mesh.verts[t + 12]};
                const V3 mid = (p0 + p1 + p2) * (1.0f / 3.0f);
                if (!litPastCaster(p0) && !litPastCaster(p1) &&
                    !litPastCaster(p2) && !litPastCaster(mid))
                    continue;
                kept.insert(kept.end(), mesh.verts.begin() + (long)t,
                            mesh.verts.begin() + (long)(t + 15));
            }
            mesh.verts.swap(kept);
        }
        if (mesh.verts.empty()) continue;
        const int tris = (int)(mesh.verts.size() / 15);
        if (mesh.truncated || tris > kMaxTrisPerCaster) {
            out.truncated.push_back(
                {c.name, "its projection needed " + std::to_string(tris) +
                             " triangles (the cap is " +
                             std::to_string(kMaxTrisPerCaster) +
                             ") - shrink the shadow's reach or simplify what it "
                             "falls on"});
            continue;
        }

        // --- into the atlas -------------------------------------------------
        const int cell = nextCell++;
        const int cols = kPageSize / res;
        const int cx = (cell % perPage) % cols, cy = (cell % perPage) / cols;
        if ((int)out.pages.size() <= page)
            out.pages.resize(page + 1,
                             Page{std::vector<uint8_t>(
                                 (size_t)kPageSize * kPageSize, 0)});
        for (int row = 0; row < res; ++row)
            std::memcpy(&out.pages[page].alpha[(size_t)(cy * res + row) *
                                                   kPageSize +
                                               cx * res],
                        &tile[(size_t)row * res], (size_t)res);

        // Fold the cell's rect into the UVs. This is the whole cost of
        // atlasing: two multiply-adds per vertex, here, at bake time.
        const float sc_ = (float)res / (float)kPageSize;
        const float ou = (float)(cx * res) / (float)kPageSize;
        const float ov = (float)(cy * res) / (float)kPageSize;
        for (size_t v = 0; v + 4 < mesh.verts.size(); v += 5) {
            mesh.verts[v + 3] = ou + mesh.verts[v + 3] * sc_;
            mesh.verts[v + 4] = ov + mesh.verts[v + 4] * sc_;
        }
        const std::string key = c.layer + "|" + std::to_string(page);
        std::vector<float>& dst = merged[key];
        dst.insert(dst.end(), mesh.verts.begin(), mesh.verts.end());
        ++mergedCasters[key];
    }

    for (auto& kv : merged) {
        const size_t bar = kv.first.rfind('|');
        Group g;
        g.layer = kv.first.substr(0, bar);
        g.page = std::atoi(kv.first.c_str() + bar + 1);
        g.verts = std::move(kv.second);
        g.casters = mergedCasters[kv.first];
        // One VU1 package is one bounding box, so triangles that sit near each
        // other should travel together - otherwise a merged bag's packages each
        // span the whole scene and nothing off screen can be culled. Sorting by
        // a coarse world cell is enough: the packages are built from
        // consecutive runs of this array.
        const size_t triCount = g.verts.size() / 15;
        std::vector<size_t> order(triCount);
        for (size_t i = 0; i < triCount; ++i) order[i] = i;
        const auto cellKey = [&](size_t tri) {
            const float* v = &g.verts[tri * 15];
            const float cx = (v[0] + v[5] + v[10]) / 3.0f;
            const float cz = (v[2] + v[7] + v[12]) / 3.0f;
            return std::pair<int, int>((int)std::floor(cz / 8.0f),
                                       (int)std::floor(cx / 8.0f));
        };
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return cellKey(a) < cellKey(b); });
        std::vector<float> sorted;
        sorted.reserve(g.verts.size());
        for (size_t i : order)
            sorted.insert(sorted.end(), g.verts.begin() + (long)(i * 15),
                          g.verts.begin() + (long)(i * 15 + 15));
        g.verts.swap(sorted);
        out.groups.push_back(std::move(g));
    }
    std::sort(out.groups.begin(), out.groups.end(),
              [](const Group& a, const Group& b) {
                  return a.layer != b.layer ? a.layer < b.layer : a.page < b.page;
              });

    if (progress) progress(1.0f);
    return out;
}

// --- signature + cache -------------------------------------------------------

uint64_t signature(const Project& p, const SceneData& sc, const Options& opt) {
    uint64_t h = 0xcbf29ce484222325ull;
    mix64(h, kCacheVersion);
    const ProjectSettings rs = project::resolvedSettings(p, sc);
    for (int k = 0; k < 3; ++k) {
        mixF(h, rs.lightDir[k]);
        mixF(h, rs.skyColor[k]);
    }
    mixF(h, rs.brightness);
    mix64(h, (uint64_t)opt.tileRes);
    mixF(h, opt.sunAngleDeg);
    mixF(h, opt.strength);
    mixF(h, opt.maxLength);
    mix64(h, (uint64_t)opt.samples);
    mix64(h, p.settings.bakedShadows ? 1 : 0);
    // A fresh GI bake takes receivers OUT of the projection, so which one is
    // in force is part of what this bake is.
    mix64(h, p.settings.giEnabled ? 1 : 0);
    mix64(h, sc.terrain.enabled ? 1 : 0);
    mix64(h, (uint64_t)sc.terrain.width);
    mix64(h, (uint64_t)sc.terrain.depth);
    mix64(h, (uint64_t)sc.hmW);
    mix64(h, (uint64_t)sc.hmD);
    for (float v : sc.heights) mixF(h, v);

    std::map<std::string, int> seen;
    const auto mixFile = [&](const std::string& rel) {
        if (rel.empty() || !seen.emplace(rel, 1).second) return;
        mixS(h, rel);
        uint64_t fh = 0, fsz = 0;
        if (wire::hashFile((fs::path(p.dir) / rel).string(), fh, fsz)) {
            mix64(h, fh);
            mix64(h, fsz);
        }
    };
    for (const SceneObject& o : sc.objects) {
        // ONLY the objects that take part: a caster, or something that could
        // be a receiver. A marker, a light, a camera, a comment, an area, the
        // player's spawn - none of them can change where a shadow lands, and
        // hashing them made every bake stale on any edit anywhere in the
        // scene. Found the hard way: nudging the PLAYER SPAWN silently threw
        // the whole scene's shadows away, and because a stale cache emits
        // nothing at all, the next build shipped a game with no shadows and
        // no complaint. The light DIRECTION is hashed above, from the resolved
        // settings, so a moved lamp still cannot be missed.
        const bool casts = o.shadowMode == 4;
        bool receives = false;
        switch (o.type) {
            case PrimitiveType::Box:
            case PrimitiveType::Sphere:
            case PrimitiveType::Cylinder:
            case PrimitiveType::Cone:
            case PrimitiveType::Model:
            case PrimitiveType::SavePoint:
            case PrimitiveType::Plane:
                receives = true;  // decalproj::isReceiverType, in its terms
                break;
            default:
                break;
        }
        if (!casts && !receives) continue;
        mix64(h, (uint64_t)o.type);
        mix64(h, (uint64_t)o.shadowMode);
        mixS(h, o.id);
        mixS(h, o.layer);
        for (int k = 0; k < 3; ++k) {
            mixF(h, o.position[k]);
            mixF(h, o.rotation[k]);
            mixF(h, o.scale[k]);
        }
        mix64(h, (uint64_t)o.primDetail);
        mix64(h, o.primRings ? 1 : 0);
        mix64(h, o.physics ? 1 : 0);
        mix64(h, o.pickable ? 1 : 0);
        mix64(h, o.saveState ? 1 : 0);
        mix64(h, o.bakedLighting ? 1 : 0);
        mixS(h, o.materialPath);
        mixFile(o.modelPath);
    }
    return h;
}

std::string cachePath(const Project& p, int sceneIndex) {
    return (fs::path(p.dir) / ".res-baked" / "shadow" /
            ("scene" + std::to_string(sceneIndex) + ".shadow"))
        .string();
}

namespace {

template <class T>
void wr(std::ostream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <class T>
bool rd(std::istream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return (bool)f;
}
template <class V>
void wrVec(std::ostream& f, const V& v) {
    const uint32_t n = (uint32_t)v.size();
    wr(f, n);
    if (n)
        f.write(reinterpret_cast<const char*>(v.data()),
                (std::streamsize)(n * sizeof(typename V::value_type)));
}
template <class V>
bool rdVec(std::istream& f, V& v) {
    uint32_t n = 0;
    if (!rd(f, n)) return false;
    if (n > 64u * 1024u * 1024u) return false;
    v.assign(n, typename V::value_type{});
    if (n)
        f.read(reinterpret_cast<char*>(v.data()),
               (std::streamsize)(n * sizeof(typename V::value_type)));
    return (bool)f;
}
void wrStr(std::ostream& f, const std::string& s) {
    const uint32_t n = (uint32_t)s.size();
    wr(f, n);
    if (n) f.write(s.data(), (std::streamsize)n);
}
bool rdStr(std::istream& f, std::string& s) {
    uint32_t n = 0;
    if (!rd(f, n)) return false;
    if (n > 4096u) return false;
    s.assign(n, '\0');
    if (n) f.read(&s[0], (std::streamsize)n);
    return (bool)f;
}

}  // namespace

bool write(const std::string& path, const Bake& b) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    wr(f, kCacheMagic);
    wr(f, kCacheVersion);
    wr(f, b.signature);
    wr(f, (int32_t)b.tileRes);
    for (int k = 0; k < 3; ++k) wr(f, b.tint[k]);
    wr(f, (uint32_t)b.pages.size());
    for (const Page& pg : b.pages) wrVec(f, pg.alpha);
    wr(f, (uint32_t)b.groups.size());
    for (const Group& g : b.groups) {
        wrStr(f, g.layer);
        wr(f, (int32_t)g.page);
        wr(f, (int32_t)g.casters);
        wrVec(f, g.verts);
    }
    return (bool)f;
}

bool read(const std::string& path, Bake& b) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0, version = 0;
    if (!rd(f, magic) || !rd(f, version)) return false;
    if (magic != kCacheMagic || version != kCacheVersion) return false;
    if (!rd(f, b.signature)) return false;
    int32_t v32 = 0;
    if (!rd(f, v32)) return false;
    b.tileRes = v32;
    for (int k = 0; k < 3; ++k)
        if (!rd(f, b.tint[k])) return false;
    uint32_t n = 0;
    if (!rd(f, n) || n > 64u) return false;
    b.pages.assign(n, Page());
    for (Page& pg : b.pages)
        if (!rdVec(f, pg.alpha)) return false;
    if (!rd(f, n) || n > 4096u) return false;
    b.groups.assign(n, Group());
    for (Group& g : b.groups) {
        if (!rdStr(f, g.layer) || !rd(f, v32)) return false;
        g.page = v32;
        if (!rd(f, v32)) return false;
        g.casters = v32;
        if (!rdVec(f, g.verts)) return false;
    }
    b.valid = true;
    return true;
}

Bake load(const Project& p, int sceneIndex) {
    Bake b;
    if (sceneIndex < 0 || sceneIndex >= (int)p.scenes.size()) return b;
    if (!p.settings.bakedShadows) return b;
    if (!read(cachePath(p, sceneIndex), b)) return Bake();
    if (b.signature != signature(p, p.scenes[sceneIndex], optionsOf(p.settings)))
        return Bake();
    return b;
}

StaleReport bakeStale(const Project& p,
                      const std::function<void(const std::string&)>& log) {
    StaleReport rep;
    const auto say = [&](const std::string& s) {
        if (log) log(s);
    };
    if (!p.settings.bakedShadows) return rep;
    const std::atomic<bool> never{false};
    const Options opt = optionsOf(p.settings);
    for (int si = 0; si < (int)p.scenes.size(); ++si) {
        const std::string& name = p.scenes[si].name;
        Bake have;
        const bool fresh = read(cachePath(p, si), have) &&
                           have.signature == signature(p, p.scenes[si], opt);
        if (fresh) {
            say("fresh     " + name);
            ++rep.kept;
            continue;
        }
        const Bake b = bakeScene(p, si, &never, nullptr);
        if (!b.valid || !write(cachePath(p, si), b)) {
            say("error     " + name);
            ++rep.failed;
            continue;
        }
        say("baked shadows " + name + ": " + std::to_string(b.groups.size()) +
            " draw(s), " + std::to_string(b.pages.size()) + " page(s), " +
            std::to_string(b.triangles()) + " tris");
        for (const Refusal& r : b.truncated) say("  skipped " + r.name + ": " + r.why);
        ++rep.baked;
    }
    return rep;
}

// --- the asynchronous baker --------------------------------------------------

void Baker::start(const Project& p, std::vector<int> scenes) {
    cancel();
    if (scenes.empty())
        for (int i = 0; i < (int)p.scenes.size(); ++i) scenes.push_back(i);
    cancel_ = false;
    running_ = true;
    progress_ = 0.0f;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_ = "Preparing...";
    }
    worker_ = std::thread(&Baker::run, this, p, scenes);
}

void Baker::cancel() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    running_ = false;
}

std::string Baker::status() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_;
}

void Baker::run(Project p, std::vector<int> scenes) {
    const int n = (int)scenes.size();
    for (int i = 0; i < n && !cancel_.load(); ++i) {
        const int si = scenes[i];
        {
            std::lock_guard<std::mutex> lk(mutex_);
            status_ = "Baking shadows in " +
                      (si < (int)p.scenes.size() ? p.scenes[si].name
                                                 : std::to_string(si)) +
                      " (" + std::to_string(i + 1) + "/" + std::to_string(n) + ")";
        }
        const Bake b =
            bakeScene(p, si, &cancel_, [&](float t) { progress_ = (i + t) / n; });
        if (cancel_.load()) break;
        if (b.valid) write(cachePath(p, si), b);
        version_.fetch_add(1);
        progress_ = (float)(i + 1) / n;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_ = cancel_.load() ? "Cancelled" : "Done";
    }
    version_.fetch_add(1);
    running_ = false;
}

}  // namespace shadowbake
