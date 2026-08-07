#include "blsscorpus.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// The corpus generator (contract and rationale: blsscorpus.hpp; arithmetic:
// docs/blss-reconstruction.md). Four parts, in order: a deterministic software
// rasteriser, the material set, the scene bestiary, and generate() driving them.
//
// THE ONE RULE. The images below are the ground truth and the console's inputs -
// they may be as good as the host can make them. Everything the NETWORK is told
// about a frame must come out of a BagProxy: a screen bounding box, a w range
// and two material constants, because that is all the EE holds while it submits
// a frame (blss.hpp, "ONE SUBMITTED DRAW, AS THE EE SEES IT"). So `luma` here is
// a material brightness times a baked light term, never the mean of the pixels
// that were just rendered, and `texDetail` is the minification ratio of section
// 2, computed from the material's dimensions and the bag's screen area. A
// feature measured off the rendered image would train a network the console
// cannot run.

namespace blss {

namespace {

// ------------------------------------------------------------------ basics ---

constexpr float kPi = 3.14159265358979f;

struct V3 {
    float x = 0, y = 0, z = 0;
};

inline V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 norm(const V3& v) {
    const float l = std::sqrt(dot(v, v));
    return l > 1e-12f ? v * (1.0f / l) : V3{0, 0, 1};
}
inline V3 lerp(const V3& a, const V3& b, float t) { return a + (b - a) * t; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
// ITU-R 601 luma - the same weights blss.cpp's psnr()/oracle work in.
inline float lumaOf(const V3& c) { return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z; }

// xorshift32. Every random decision in this file is drawn from a stream seeded
// by mix32(cfg.seed, purpose), never from rand(): the corpus has to be the same
// bytes on every machine or the PSNR table in --blss-eval is not a regression
// test.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uni() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
    float range(float a, float b) { return a + (b - a) * uni(); }
};

// Mixes two ints into a well-distributed seed, so every stream (shot layout,
// procedural texture noise) derives from CorpusConfig::seed without
// correlating - dronegen's mixer, same reason.
inline uint32_t mix32(uint32_t a, uint32_t b) {
    uint32_t x = a * 0x9E3779B1u + b * 0x85EBCA77u + 0xC2B2AE3Du;
    x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
    return x ? x : 1u;
}

// The one directional light the whole corpus is lit by, plus its ambient floor.
// Baked into vertex colours at scene-build time, which is exactly what the PS2
// pipeline does (templates.cpp bakes the directional term per vertex): the
// rasteriser below only ever multiplies a texel by an interpolated vertex
// colour, so there is no per-pixel lighting the console could not afford.
// Pre-normalised as a literal - (-1, 2, -1)/sqrt(6) - to keep it a constexpr.
constexpr V3 kLightDir{-0.40825f, 0.81650f, -0.40825f};
constexpr float kAmbient = 0.34f;
constexpr float kDiffuse = 0.72f;

inline float shadeOf(const V3& n) {
    const float d = dot(n, kLightDir);
    return kAmbient + kDiffuse * (d > 0.0f ? d : 0.0f);
}

// Near plane, world units. Only used for clipping - the corpus never needs a far
// plane because the z-buffer stores 1/w.
constexpr float kNear = 0.06f;

// The background. Deliberately FLAT: the sky is not a submitted bag, so the
// feature vector describes those tiles as coverage 0, and a gradient there would
// hand the oracle contrast the network has no way to predict. Flat sky teaches
// the correct lesson instead - no coverage, do nothing.
constexpr uint8_t kSky[3] = {96, 116, 148};

// ---------------------------------------------------------------- textures ---

// One material. `luma` and the dimensions are the only things that reach the
// network (through BagProxy), and both are properties of the ASSET, measured
// once at load - a build-time bake, not a look at the frame.
//
// Dimensions are always powers of two, because that is all the GS can address:
// so REPEAT wrapping is a mask here, not a modulo, and the innermost loop of the
// whole corpus does not pay eight integer divides per bilinear tap.
struct Texture {
    int w = 0, h = 0;
    int wMask = 0, hMask = 0;
    std::vector<uint8_t> px;  // RGBA8, row-major
    bool cutout = false;      // alpha-tested (foliage); no blending anywhere
    float luma = 0.5f;        // 0..1 mean brightness of the opaque texels

    void setSize(int width, int height) {
        w = width, h = height;
        wMask = width - 1, hMask = height - 1;
        px.assign((size_t)width * height * 4, 0);
    }
};

inline bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

// Truncate-and-correct instead of std::floor: at the baseline x86-64 ISA floorf
// is an out-of-line libm call, and this runs once per texture tap of a 2048x1792
// supersampled frame.
inline int ifloor(float v) {
    const int i = (int)v;
    return (float)i > v ? i - 1 : i;
}

// Mean brightness over the texels that actually survive the alpha test - a
// foliage sheet is 70% hole, and counting the holes would report a leaf material
// as nearly black.
void measureLuma(Texture& t) {
    double acc = 0.0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < t.px.size(); i += 4) {
        if (t.cutout && t.px[i + 3] < 128) continue;
        acc += 0.299 * t.px[i] + 0.587 * t.px[i + 1] + 0.114 * t.px[i + 2];
        ++n;
    }
    t.luma = n ? (float)(acc / (255.0 * (double)n)) : 0.5f;
}

// Texel fetch, wrapped, NO MIPMAPS - the PS2 has none by default and the
// resulting minification aliasing is the signal the network is being trained to
// clean up. Returns 0..255 floats so the modulate below stays in one type.
inline void fetchNearest(const Texture& t, float u, float v, float out[4]) {
    const int x = ifloor(u * (float)t.w) & t.wMask;
    const int y = ifloor(v * (float)t.h) & t.hMask;
    const uint8_t* p = &t.px[((size_t)y * t.w + x) * 4];
    out[0] = p[0], out[1] = p[1], out[2] = p[2], out[3] = p[3];
}

inline void fetchBilinear(const Texture& t, float u, float v, float out[4]) {
    const float fx = u * (float)t.w - 0.5f, fy = v * (float)t.h - 0.5f;
    const int ix = ifloor(fx), iy = ifloor(fy);
    const float ax = fx - (float)ix, ay = fy - (float)iy;
    const int x0 = ix & t.wMask, x1 = (ix + 1) & t.wMask;
    const int y0 = iy & t.hMask, y1 = (iy + 1) & t.hMask;
    const uint8_t* a = &t.px[((size_t)y0 * t.w + x0) * 4];
    const uint8_t* b = &t.px[((size_t)y0 * t.w + x1) * 4];
    const uint8_t* c = &t.px[((size_t)y1 * t.w + x0) * 4];
    const uint8_t* d = &t.px[((size_t)y1 * t.w + x1) * 4];
    const float w00 = (1.0f - ax) * (1.0f - ay), w10 = ax * (1.0f - ay);
    const float w01 = (1.0f - ax) * ay, w11 = ax * ay;
    for (int k = 0; k < 4; ++k)
        out[k] = a[k] * w00 + b[k] * w10 + c[k] * w01 + d[k] * w11;
}

// --- procedural fallbacks ----------------------------------------------------
// --blss-train has to work in a clean checkout, so every material the bestiary
// needs exists procedurally; real PNGs from examples/ replace the opaque ones
// when the tree is there.

Texture makeChecker(int size, int cell) {
    Texture t;
    t.setSize(size, size);
    std::fill(t.px.begin(), t.px.end(), (uint8_t)255);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) & 1;
            uint8_t* p = &t.px[((size_t)y * size + x) * 4];
            p[0] = on ? 232 : 34;
            p[1] = on ? 228 : 38;
            p[2] = on ? 214 : 46;
        }
    measureLuma(t);
    return t;
}

// White noise, one value per texel: the worst case a texture can present to a
// point sampler, and therefore the sharpest test of the grazing-angle shot.
Texture makeNoise(int size, uint32_t seed) {
    Texture t;
    t.setSize(size, size);
    std::fill(t.px.begin(), t.px.end(), (uint8_t)255);
    Rng r;
    r.seed(seed);
    for (size_t i = 0; i < t.px.size(); i += 4) {
        const uint8_t v = (uint8_t)(28.0f + 214.0f * r.uni());
        t.px[i] = v;
        t.px[i + 1] = (uint8_t)(v * 0.94f);
        t.px[i + 2] = (uint8_t)(v * 0.82f);
    }
    measureLuma(t);
    return t;
}

// Leaf sheet: opaque blobs on a fully transparent field. Hard 0/255 alpha,
// because the interesting case is the alpha TEST (a silhouette that moves by
// whole texels between frames), not blending.
Texture makeFoliage(int size, uint32_t seed) {
    Texture t;
    t.setSize(size, size);  // alpha 0 everywhere: the field is the hole
    Rng r;
    r.seed(seed);
    struct Leaf {
        float cx, cy, rx, ry, rot, tint;
    };
    std::vector<Leaf> leaves;
    for (int i = 0; i < 22; ++i)
        leaves.push_back({r.uni(), r.uni(), r.range(0.05f, 0.13f),
                          r.range(0.02f, 0.06f), r.range(0.0f, kPi),
                          r.range(0.55f, 1.0f)});
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const float u = ((float)x + 0.5f) / (float)size;
            const float v = ((float)y + 0.5f) / (float)size;
            for (const Leaf& l : leaves) {
                const float dx = u - l.cx, dy = v - l.cy;
                const float c = std::cos(l.rot), s = std::sin(l.rot);
                const float a = (dx * c + dy * s) / l.rx;
                const float b = (-dx * s + dy * c) / l.ry;
                if (a * a + b * b > 1.0f) continue;
                uint8_t* p = &t.px[((size_t)y * size + x) * 4];
                // A vein down the leaf's long axis - high-frequency detail
                // inside the cutout, so the shot exercises both at once.
                const float vein = std::fabs(b) < 0.18f ? 0.72f : 1.0f;
                p[0] = (uint8_t)(70.0f * l.tint * vein);
                p[1] = (uint8_t)(168.0f * l.tint * vein);
                p[2] = (uint8_t)(62.0f * l.tint * vein);
                p[3] = 255;
                break;
            }
        }
    t.cutout = true;
    measureLuma(t);
    return t;
}

// Horizontal banding plus per-texel grain: a plausible stand-in for a brick or
// panel wall, and a texture whose energy is anisotropic (bands alias at a
// grazing angle long before the grain does).
Texture makeBands(int size, uint32_t seed) {
    Texture t;
    t.setSize(size, size);
    std::fill(t.px.begin(), t.px.end(), (uint8_t)255);
    Rng r;
    r.seed(seed);
    for (int y = 0; y < size; ++y) {
        const bool mortar = (y % 16) < 3;
        const int row = y / 16;
        for (int x = 0; x < size; ++x) {
            const int sx = x + (row & 1 ? size / 8 : 0);
            const bool joint = mortar || (sx % 32) < 3;
            const float g = r.range(-14.0f, 14.0f);
            uint8_t* p = &t.px[((size_t)y * size + x) * 4];
            const float base = joint ? 176.0f : 122.0f;
            p[0] = (uint8_t)std::clamp(base + g + (joint ? 0.0f : 38.0f), 0.0f, 255.0f);
            p[1] = (uint8_t)std::clamp(base + g, 0.0f, 255.0f);
            p[2] = (uint8_t)std::clamp(base + g - (joint ? 0.0f : 16.0f), 0.0f, 255.0f);
        }
    }
    measureLuma(t);
    return t;
}

// The material table plus the names the bestiary refers to. `asset` holds the
// real PNGs, in scan order; every user of one falls back to a procedural
// material so the corpus is identical in shape with or without examples/.
struct Materials {
    std::vector<Texture> tex;
    int checker = -1, noise = -1, foliage = -1, bands = -1;
    std::vector<int> asset;

    // i-th real asset, or `fallback` when the tree had fewer than i+1 usable
    // PNGs.
    int assetOr(size_t i, int fallback) const {
        return i < asset.size() ? asset[i] : fallback;
    }
};

// Real materials out of the examples/ tree. Only res/materials and res/models
// are considered: those are the textures actually mapped onto geometry, whereas
// hud/menus/fonts art is 2D and its alpha channel is a mask, not a cutout.
std::vector<std::filesystem::path> scanAssetPngs(const std::string& root) {
    std::vector<std::filesystem::path> out;
    if (root.empty()) return out;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) return out;
    for (; it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec) || ec) continue;
        const std::filesystem::path& p = it->path();
        if (p.extension() != ".png" && p.extension() != ".PNG") continue;
        const std::string s = p.generic_string();
        // .res-baked / bin are build outputs (the same images, palettized or
        // atlased), and .layers are material-painting sources.
        if (s.find("/.res-baked/") != std::string::npos ||
            s.find("/bin/") != std::string::npos ||
            s.find(".layers/") != std::string::npos)
            continue;
        if (s.find("/res/materials/") == std::string::npos &&
            s.find("/res/models/") == std::string::npos)
            continue;
        out.push_back(p);
    }
    // The directory walk order is unspecified; the corpus is not.
    std::sort(out.begin(), out.end());
    return out;
}

Materials buildMaterials(const CorpusConfig& cfg) {
    Materials m;
    m.checker = (int)m.tex.size();
    m.tex.push_back(makeChecker(64, 8));
    m.noise = (int)m.tex.size();
    m.tex.push_back(makeNoise(128, mix32(cfg.seed, 0x4E01u)));
    m.foliage = (int)m.tex.size();
    m.tex.push_back(makeFoliage(64, mix32(cfg.seed, 0xF01Au)));
    m.bands = (int)m.tex.size();
    m.tex.push_back(makeBands(128, mix32(cfg.seed, 0xBA4Du)));

    // At most a handful: past that the extra materials only slow the corpus
    // down, and the aliasing cases are geometric anyway.
    constexpr size_t kMaxAssets = 4;
    int tried = 0, taken = 0;
    for (const std::filesystem::path& p : scanAssetPngs(cfg.assetDir)) {
        if (m.asset.size() >= kMaxAssets) break;
        ++tried;
        Image img;
        if (!readPng(img, p.string())) continue;
        // Power-of-two only, and not because of the mask above: the GS cannot
        // address anything else, so a non-POT PNG in the tree is not a material
        // the console would ever sample.
        if (!isPow2(img.w) || !isPow2(img.h)) continue;
        if (img.w < 8 || img.h < 8 || img.w > 1024 || img.h > 1024) continue;
        Texture t;
        t.setSize(img.w, img.h);
        t.px = img.px;
        // Alpha forced opaque: which quads are cutouts is the bestiary's
        // decision (foliage, below), and a decal or a partly transparent panel
        // texture would otherwise punch holes in a box for no training value.
        for (size_t i = 3; i < t.px.size(); i += 4) t.px[i] = 255;
        measureLuma(t);
        m.asset.push_back((int)m.tex.size());
        m.tex.push_back(std::move(t));
        ++taken;
    }
    if (cfg.verbose) {
        std::printf("[blss] materials: 4 procedural + %d from %s (%d PNG(s) tried)\n",
                    taken, cfg.assetDir.empty() ? "(no assetDir)" : cfg.assetDir.c_str(),
                    tried);
    }
    return m;
}

// ------------------------------------------------------------------ objects ---

// A vertex, in WORLD space with the light already baked into rgb. Objects never
// move in this corpus (the camera does), so there is no model matrix at all -
// one less thing that could disagree with the Pinhole the features are built
// from.
struct Vertex {
    V3 p;
    float u = 0, v = 0;
    V3 c{1, 1, 1};
};

// One drawn object == one BagProxy. Everything below the line is what the bag
// submission carries; it is all computed here, at build time, from the mesh and
// the material.
struct Object {
    std::vector<Vertex> vert;
    std::vector<int> idx;  // 3 per triangle
    int tex = -1;          // index into Materials::tex, -1 = untextured
    bool bilinear = true;
    bool cutout = false;
    // ---- bag payload
    float luma = 0.5f;       // material brightness x light, 0..1
    float texelArea = 0.0f;  // texels the material spans over this object
    V3 lo{}, hi{};           // world AABB, the bbox the EE transforms
};

// Appends one quad. a,b,c,d run round the rim and the FRONT face - the one the
// baked light is computed for - is the side from which they read CLOCKWISE, which
// is the winding every builder below uses. Winding never affects visibility here
// (the rasteriser is double-sided), only which side of a sheet is lit, so getting
// it backwards costs a surface its directional light and nothing else - which is
// exactly how the first version of this file shipped a floor lit by ambient
// alone. Normals are per face, so a box has hard edges and a sphere, being many
// small quads, reads smooth.
void addQuad(Object& o, const V3& a, const V3& b, const V3& c, const V3& d,
             float u0, float v0, float u1, float v1, const V3& albedo) {
    const float sh = shadeOf(norm(cross(d - a, b - a)));
    const V3 col{std::min(1.0f, albedo.x * sh), std::min(1.0f, albedo.y * sh),
                 std::min(1.0f, albedo.z * sh)};
    const int base = (int)o.vert.size();
    o.vert.push_back({a, u0, v0, col});
    o.vert.push_back({b, u1, v0, col});
    o.vert.push_back({c, u1, v1, col});
    o.vert.push_back({d, u0, v1, col});
    const int t[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
    o.idx.insert(o.idx.end(), t, t + 6);
}

void addBox(Object& o, const V3& c, const V3& half, float uvRep, const V3& albedo) {
    const float x0 = c.x - half.x, x1 = c.x + half.x;
    const float y0 = c.y - half.y, y1 = c.y + half.y;
    const float z0 = c.z - half.z, z1 = c.z + half.z;
    const float r = uvRep;
    addQuad(o, {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0}, 0, 0, r, r, albedo);
    addQuad(o, {x1, y0, z1}, {x0, y0, z1}, {x0, y1, z1}, {x1, y1, z1}, 0, 0, r, r, albedo);
    addQuad(o, {x0, y0, z1}, {x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, 0, 0, r, r, albedo);
    addQuad(o, {x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0}, 0, 0, r, r, albedo);
    addQuad(o, {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1}, 0, 0, r, r, albedo);
    addQuad(o, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0}, {x0, y0, z0}, 0, 0, r, r, albedo);
}

// Deliberately coarse: 14x8 quads is a PS2-plausible ball, and the faceted
// silhouette is the point - a curved edge that is neither axis-aligned nor
// straight is the case a fixed reconstruction kernel handles worst.
void addSphere(Object& o, const V3& c, float r, int slices, int stacks,
               const V3& albedo) {
    for (int st = 0; st < stacks; ++st) {
        const float t0 = kPi * (float)st / (float)stacks;
        const float t1 = kPi * (float)(st + 1) / (float)stacks;
        for (int sl = 0; sl < slices; ++sl) {
            const float p0 = 2.0f * kPi * (float)sl / (float)slices;
            const float p1 = 2.0f * kPi * (float)(sl + 1) / (float)slices;
            auto pt = [&](float th, float ph) {
                return V3{c.x + r * std::sin(th) * std::cos(ph), c.y + r * std::cos(th),
                          c.z + r * std::sin(th) * std::sin(ph)};
            };
            // Rim order theta-first, so the winding reads clockwise from OUTSIDE
            // and the outward normal is the lit one (the sphere is the one
            // surface here whose natural parameter order runs the other way).
            // That transposes u and v on the ball, which no feature can see.
            addQuad(o, pt(t0, p0), pt(t1, p0), pt(t1, p1), pt(t0, p1),
                    (float)st / (float)stacks, (float)sl / (float)slices,
                    (float)(st + 1) / (float)stacks, (float)(sl + 1) / (float)slices,
                    albedo);
        }
    }
}

// World AABB + the two bag material constants. `luma` is the mean vertex
// brightness (albedo x the baked directional light) times the material's own
// mean brightness - a product of two build-time constants, which is exactly the
// per-bag scalar the EE would be handed. `texelArea` is the material's texel
// count over this object's UV bounding rect, the twin of using the screen
// BOUNDING BOX as the pixel area in section 2: both are bounding-rect
// estimates, and only their ratio reaches the network.
//
// KNOWN DIVERGENCE, and it needs settling before anyone trusts this channel.
// The engine's feed (stapip_core.cpp) passes texW * texH with no UV span, so a
// bag that tiles its texture 40 times reports 1600x less texel area than the
// same surface does here. The two agree exactly for UVs in 0..1 and disagree
// wildly for any tiled floor or wall - which is most of what aliases. This side
// is the one that predicts aliasing correctly (a floor really does cram
// uvRep^2 texture copies into the screen); the fix belongs on the engine side,
// as a per-material UV-repeat constant baked by the editor - the same "editor
// bake" follow-up docs/neural-upscaler.md already flags for texDetail.
void finishObject(Object& o, const Materials& m) {
    if (o.vert.empty()) return;
    o.lo = o.hi = o.vert[0].p;
    float u0 = o.vert[0].u, u1 = u0, v0 = o.vert[0].v, v1 = v0;
    double lumaAcc = 0.0;
    for (const Vertex& v : o.vert) {
        o.lo.x = std::min(o.lo.x, v.p.x), o.hi.x = std::max(o.hi.x, v.p.x);
        o.lo.y = std::min(o.lo.y, v.p.y), o.hi.y = std::max(o.hi.y, v.p.y);
        o.lo.z = std::min(o.lo.z, v.p.z), o.hi.z = std::max(o.hi.z, v.p.z);
        u0 = std::min(u0, v.u), u1 = std::max(u1, v.u);
        v0 = std::min(v0, v.v), v1 = std::max(v1, v.v);
        lumaAcc += lumaOf(v.c);
    }
    float lum = (float)(lumaAcc / (double)o.vert.size());
    if (o.tex >= 0) {
        const Texture& t = m.tex[(size_t)o.tex];
        lum *= t.luma;
        // PARITY, NOT PREFERENCE: the raw texture area, with NO UV span, because
        // that is all the engine can pass - `stapip_core.cpp` hands BLSS
        // `texW * texH` and has no idea how many times the material tiles over
        // the surface. Folding the UV span in here (which it used to) makes this
        // channel a genuinely better aliasing predictor and a genuinely
        // different quantity from the one the console computes: a floor tiling
        // 100x trained at texDetail 1.0 and ran at ~0.03. A weaker feature both
        // sides agree on beats a strong feature only one side has.
        //
        // The fix that makes it strong again is a baked per-material UV-repeat
        // constant the engine can multiply in - docs/neural-upscaler.md's
        // "bake real texture detail" follow-up. Until then, u0/u1/v0/v1 are
        // computed above only for the cutout/luma work.
        o.texelArea = (float)t.w * (float)t.h;
        o.cutout = t.cutout;
    }
    o.luma = clamp01(lum);
}

// ---------------------------------------------------------------- rasteriser ---

// View-space vertex, the form the near clip works in.
struct CVert {
    float vx = 0, vy = 0, w = 0;  // right/up/forward components
    float u = 0, v = 0;
    float r = 0, g = 0, b = 0;
};

inline CVert toView(const Pinhole& cam, const Vertex& src) {
    const V3 rel{src.p.x - cam.pos[0], src.p.y - cam.pos[1], src.p.z - cam.pos[2]};
    CVert o;
    o.vx = rel.x * cam.right[0] + rel.y * cam.right[1] + rel.z * cam.right[2];
    o.vy = rel.x * cam.up[0] + rel.y * cam.up[1] + rel.z * cam.up[2];
    o.w = rel.x * cam.fwd[0] + rel.y * cam.fwd[1] + rel.z * cam.fwd[2];
    o.u = src.u, o.v = src.v;
    o.r = src.c.x, o.g = src.c.y, o.b = src.c.z;
    return o;
}

inline CVert mixV(const CVert& a, const CVert& b, float t) {
    CVert o;
    o.vx = lerpf(a.vx, b.vx, t);
    o.vy = lerpf(a.vy, b.vy, t);
    o.w = lerpf(a.w, b.w, t);
    o.u = lerpf(a.u, b.u, t);
    o.v = lerpf(a.v, b.v, t);
    o.r = lerpf(a.r, b.r, t);
    o.g = lerpf(a.g, b.g, t);
    o.b = lerpf(a.b, b.b, t);
    return o;
}

// Sutherland-Hodgman against w >= kNear, the only clip plane needed: the raster
// loop clamps to the target rect, so a triangle that reaches past the sides
// simply costs nothing extra, but one that crosses the eye plane would project
// to garbage. Returns 0, 3 or 4 vertices.
int clipNear(const CVert in[3], CVert out[4]) {
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const CVert& a = in[i];
        const CVert& b = in[(i + 1) % 3];
        const bool ina = a.w >= kNear, inb = b.w >= kNear;
        if (ina) out[n++] = a;
        if (ina != inb) {
            const float d = b.w - a.w;
            const float t = std::fabs(d) > 1e-9f ? (kNear - a.w) / d : 0.0f;
            out[n++] = mixV(a, b, t);
        }
    }
    return n >= 3 ? n : 0;
}

// Raster-space vertex. `uw`/`vw` are u/w and v/w: UVs are perspective-correct
// (the GS interpolates STQ when FST = 0), while the Gouraud COLOUR is affine in
// screen space, which is what the GS actually does - keeping that split means
// the ground truth has the same shading gradients the console would produce.
struct RVert {
    float x = 0, y = 0, iw = 0, uw = 0, vw = 0, r = 0, g = 0, b = 0;
};

// One render pass. `rw`/`rh` is any resolution and `offX`/`offY` any sub-pixel
// raster offset in RENDER pixels - that single parameter is the whole jitter
// story (the GS applies it through XYOFFSET, so nothing but the projected
// position changes) and it is equally the knob a supersampling grid would turn.
// `zb` is scratch owned by the caller: at 4x supersample it is 15 MB, which is
// not worth reallocating 48 times.
void renderScene(const std::vector<Object>& objs, const Materials& mats,
                 const Pinhole& cam, int rw, int rh, float offX, float offY,
                 Image& img, std::vector<float>& zb) {
    img.resize(rw, rh);
    // Clear one row, then replicate it: the supersampled target is 15 MB and a
    // pixel-at-a-time clear of it was a measurable slice of the whole corpus.
    for (int x = 0; x < rw; ++x) {
        uint8_t* p = &img.px[(size_t)x * 4];
        p[0] = kSky[0], p[1] = kSky[1], p[2] = kSky[2], p[3] = 255;
    }
    for (int y = 1; y < rh; ++y)
        std::memcpy(&img.px[(size_t)y * rw * 4], img.px.data(), (size_t)rw * 4);
    zb.assign((size_t)rw * rh, 0.0f);  // 1/w, 0 = infinitely far

    const float sxScale = 0.5f * (float)rw / cam.tanHalfFovX;
    const float syScale = 0.5f * (float)rh / cam.tanHalfFovY;
    const float cx = 0.5f * (float)rw + offX;
    const float cy = 0.5f * (float)rh + offY;

    for (const Object& o : objs) {
        const Texture* tex = o.tex >= 0 ? &mats.tex[(size_t)o.tex] : nullptr;
        const bool bilin = o.bilinear;
        const bool cutout = o.cutout;
        for (size_t f = 0; f + 2 < o.idx.size(); f += 3) {
            CVert in[3];
            for (int k = 0; k < 3; ++k)
                in[k] = toView(cam, o.vert[(size_t)o.idx[f + k]]);
            CVert poly[4];
            const int pn = clipNear(in, poly);
            if (pn == 0) continue;
            RVert rv[4];
            for (int k = 0; k < pn; ++k) {
                const float iw = 1.0f / poly[k].w;
                rv[k].x = poly[k].vx * iw * sxScale + cx;
                rv[k].y = -poly[k].vy * iw * syScale + cy;
                rv[k].iw = iw;
                rv[k].uw = poly[k].u * iw;
                rv[k].vw = poly[k].v * iw;
                rv[k].r = poly[k].r, rv[k].g = poly[k].g, rv[k].b = poly[k].b;
            }
            for (int fanIdx = 0; fanIdx + 2 < pn; ++fanIdx) {
                RVert v0 = rv[0], v1 = rv[fanIdx + 1], v2 = rv[fanIdx + 2];
                float det = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
                // Double-sided on purpose: a corpus has no reason to hide the
                // back of a foliage quad or the underside of a floor, and the
                // z-buffer already resolves what is in front.
                if (det < 0.0f) {
                    std::swap(v1, v2);
                    det = -det;
                }
                if (det < 1e-7f) continue;  // degenerate / sub-sample sliver

                int x0 = (int)std::floor(std::min(v0.x, std::min(v1.x, v2.x)) - 0.5f);
                int x1 = (int)std::ceil(std::max(v0.x, std::max(v1.x, v2.x)) + 0.5f);
                int y0 = (int)std::floor(std::min(v0.y, std::min(v1.y, v2.y)) - 0.5f);
                int y1 = (int)std::ceil(std::max(v0.y, std::max(v1.y, v2.y)) + 0.5f);
                x0 = std::max(x0, 0), y0 = std::max(y0, 0);
                x1 = std::min(x1, rw - 1), y1 = std::min(y1, rh - 1);
                if (x0 > x1 || y0 > y1) continue;

                // Edge functions e(x,y) = A*x + B*y + C, >= 0 inside (after the
                // winding fix above).
                const float ea[3] = {-(v1.y - v0.y), -(v2.y - v1.y), -(v0.y - v2.y)};
                const float eb[3] = {v1.x - v0.x, v2.x - v1.x, v0.x - v2.x};
                const float ec[3] = {
                    (v1.y - v0.y) * v0.x - (v1.x - v0.x) * v0.y,
                    (v2.y - v1.y) * v1.x - (v2.x - v1.x) * v1.y,
                    (v0.y - v2.y) * v2.x - (v0.x - v2.x) * v2.y};

                // Screen-space gradient of each interpolated attribute, solved
                // once per triangle: the inner loop is then 6 adds and a depth
                // compare, with no barycentric division per pixel.
                const float att0[6] = {v0.iw, v0.uw, v0.vw, v0.r, v0.g, v0.b};
                const float att1[6] = {v1.iw, v1.uw, v1.vw, v1.r, v1.g, v1.b};
                const float att2[6] = {v2.iw, v2.uw, v2.vw, v2.r, v2.g, v2.b};
                const float invDet = 1.0f / det;
                float gx[6], gy[6];
                for (int k = 0; k < 6; ++k) {
                    const float d1 = att1[k] - att0[k], d2 = att2[k] - att0[k];
                    gx[k] = (d1 * (v2.y - v0.y) - d2 * (v1.y - v0.y)) * invDet;
                    gy[k] = (d2 * (v1.x - v0.x) - d1 * (v2.x - v0.x)) * invDet;
                }

                for (int y = y0; y <= y1; ++y) {
                    const float yc = (float)y + 0.5f;
                    // The exact x span, from solving each edge for x. At 2048
                    // px wide (the supersampled pass) testing every pixel of
                    // the bounding box is most of the cost of a big triangle.
                    float lo = (float)x0 + 0.5f, hi = (float)x1 + 0.5f;
                    bool empty = false;
                    for (int e = 0; e < 3; ++e) {
                        const float k = eb[e] * yc + ec[e];
                        if (ea[e] > 1e-12f) {
                            lo = std::max(lo, -k / ea[e]);
                        } else if (ea[e] < -1e-12f) {
                            hi = std::min(hi, -k / ea[e]);
                        } else if (k < 0.0f) {
                            empty = true;
                            break;
                        }
                    }
                    if (empty) continue;
                    int xs = (int)std::ceil(lo - 0.5f);
                    int xe = (int)std::floor(hi - 0.5f);
                    xs = std::max(xs, x0), xe = std::min(xe, x1);
                    if (xs > xe) continue;

                    float a[6];
                    const float dx = (float)xs + 0.5f - v0.x, dy = yc - v0.y;
                    for (int k = 0; k < 6; ++k)
                        a[k] = att0[k] + gx[k] * dx + gy[k] * dy;

                    size_t pi = (size_t)y * rw + xs;
                    uint8_t* dst = &img.px[pi * 4];
                    for (int x = xs; x <= xe; ++x) {
                        if (a[0] > zb[pi]) {  // 1/w, larger = nearer
                            float cr, cg, cb;
                            bool write = true;
                            if (tex) {
                                const float w = 1.0f / a[0];
                                float t[4];
                                if (bilin)
                                    fetchBilinear(*tex, a[1] * w, a[2] * w, t);
                                else
                                    fetchNearest(*tex, a[1] * w, a[2] * w, t);
                                // The GS alpha test, the cutout rule that makes
                                // foliage work. No blending anywhere in this
                                // corpus - a cutout either covers the pixel or
                                // it does not, which is also what makes its
                                // silhouette alias.
                                write = !cutout || t[3] >= 128.0f;
                                cr = t[0] * a[3], cg = t[1] * a[4], cb = t[2] * a[5];
                            } else {
                                cr = 255.0f * a[3], cg = 255.0f * a[4],
                                cb = 255.0f * a[5];
                            }
                            if (write) {
                                // MODULATE with the GS's 0..255 clamp.
                                dst[0] = (uint8_t)(cr < 0 ? 0 : (cr > 255 ? 255 : cr));
                                dst[1] = (uint8_t)(cg < 0 ? 0 : (cg > 255 ? 255 : cg));
                                dst[2] = (uint8_t)(cb < 0 ? 0 : (cb > 255 ? 255 : cb));
                                dst[3] = 255;
                                zb[pi] = a[0];
                            }
                        }
                        a[0] += gx[0], a[1] += gx[1], a[2] += gx[2];
                        a[3] += gx[3], a[4] += gx[4], a[5] += gx[5];
                        ++pi;
                        dst += 4;
                    }
                }
            }
        }
    }
}

// ----------------------------------------------------------------- bags ------

// The object, as the EE would submit it: its world AABB transformed, clipped to
// the near plane and projected into OUTPUT pixels (unjittered - the bag list
// describes the frame, not the raster offset it happened to be drawn with).
// Twelve edges of eight corners, which is the whole cost of a bag on the EE.
bool bagOf(const Object& o, const Materials& mats, const Pinhole& cam, int outW,
           int outH, BagProxy& out) {
    const float sxScale = 0.5f * (float)outW / cam.tanHalfFovX;
    const float syScale = 0.5f * (float)outH / cam.tanHalfFovY;
    struct P {
        float vx, vy, w;
    };
    P corner[8];
    for (int c = 0; c < 8; ++c) {
        const V3 p{(c & 1) ? o.hi.x : o.lo.x, (c & 2) ? o.hi.y : o.lo.y,
                   (c & 4) ? o.hi.z : o.lo.z};
        const V3 rel{p.x - cam.pos[0], p.y - cam.pos[1], p.z - cam.pos[2]};
        corner[c].vx = rel.x * cam.right[0] + rel.y * cam.right[1] + rel.z * cam.right[2];
        corner[c].vy = rel.x * cam.up[0] + rel.y * cam.up[1] + rel.z * cam.up[2];
        corner[c].w = rel.x * cam.fwd[0] + rel.y * cam.fwd[1] + rel.z * cam.fwd[2];
    }
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    float wn = 1e30f, wf = -1e30f;
    auto take = [&](const P& p) {
        const float iw = 1.0f / p.w;
        const float sx = p.vx * iw * sxScale + 0.5f * (float)outW;
        const float sy = -p.vy * iw * syScale + 0.5f * (float)outH;
        x0 = std::min(x0, sx), x1 = std::max(x1, sx);
        y0 = std::min(y0, sy), y1 = std::max(y1, sy);
        wn = std::min(wn, p.w), wf = std::max(wf, p.w);
    };
    bool any = false;
    for (int c = 0; c < 8; ++c) {
        for (int bit = 0; bit < 3; ++bit) {
            if (c & (1 << bit)) continue;
            const P& a = corner[c];
            const P& b = corner[c | (1 << bit)];
            const bool ina = a.w >= kNear, inb = b.w >= kNear;
            if (!ina && !inb) continue;
            if (ina) take(a), any = true;
            if (inb) take(b), any = true;
            if (ina != inb) {
                const float d = b.w - a.w;
                const float t = std::fabs(d) > 1e-9f ? (kNear - a.w) / d : 0.0f;
                take({lerpf(a.vx, b.vx, t), lerpf(a.vy, b.vy, t), kNear});
                any = true;
            }
        }
    }
    if (!any) return false;                                            // behind us
    if (x1 <= 0.0f || y1 <= 0.0f || x0 >= (float)outW || y0 >= (float)outH)
        return false;                                                  // off screen
    out.x0 = std::max(x0, 0.0f);
    out.y0 = std::max(y0, 0.0f);
    out.x1 = std::min(x1, (float)outW);
    out.y1 = std::min(y1, (float)outH);
    out.wNear = std::max(wn, kNear);
    out.wFar = std::max(wf, out.wNear);
    out.luma = o.luma;
    // Section 2's minification ratio: texels per screen pixel over 4, clamped.
    // NOT clamped up off a thin bbox - a pole one pixel wide really does cram
    // its whole texture into a column, and that is the case the feature exists
    // to flag.
    if (o.tex >= 0 && mats.tex[(size_t)o.tex].w > 0) {
        const float area = std::max((out.x1 - out.x0) * (out.y1 - out.y0), 1.0f);
        out.texDetail = clamp01(std::sqrt(o.texelArea / area) * 0.25f);
    } else {
        out.texDetail = 0.0f;
    }
    return true;
}

std::vector<BagProxy> bagList(const std::vector<Object>& objs, const Materials& mats,
                              const Pinhole& cam, int outW, int outH) {
    std::vector<BagProxy> out;
    out.reserve(objs.size());
    BagProxy b;
    for (const Object& o : objs)
        if (bagOf(o, mats, cam, outW, outH, b)) out.push_back(b);
    return out;
}

// ---------------------------------------------------------------- the shots ---

// Static, Pan and Dolly are the SAME interpolation - they differ only in which
// endpoints the shot bothers to vary (nothing, the yaw, the eye) - and they are
// spelled out anyway because "which camera move is this" is the first question
// asked of a training frame that scores badly.
enum class Move {
    Static,  // fixed camera: only the jitter moves, so history is perfect
    Pan,     // eye fixed, yaw sweeps
    Orbit,   // eye circles a centre, always looking at it
    Dolly,   // eye translates (into the scene, or along a wall)
    Whip,    // a pan so fast that reprojection cannot help at all
};

struct Shot {
    const char* name = "";
    const char* moveName = "";
    Move move = Move::Static;
    std::vector<Object> objs;
    V3 eye0{}, eye1{};
    float yaw0 = 0, yaw1 = 0;      // radians; also the orbit angle
    float pitch0 = 0, pitch1 = 0;  // negative = looking down
    V3 centre{};                   // orbit only
    float orbitR = 6.0f, orbitH = 2.0f;
};

// Yaw is measured from +Z toward +X, pitch is up. Left-handed basis: with
// fwd = +Z and up = +Y, right comes out +X, which is the convention the ray in
// blss.hpp's Pinhole comment assumes (sX grows to the right of the screen).
Pinhole cameraAt(const Shot& s, float t) {
    float u = t;
    // The whip is eased so its angular velocity PEAKS mid-shot: a linear sweep
    // would give every frame the same hopeless offset, while the ease produces
    // frames where history is fine, frames where it is useless, and the two
    // transitions - which is what the temporal weight has to learn to gate on.
    if (s.move == Move::Whip) u = t * t * (3.0f - 2.0f * t);
    V3 eye = lerp(s.eye0, s.eye1, u);
    const float yaw = lerpf(s.yaw0, s.yaw1, u);
    const float pitch = lerpf(s.pitch0, s.pitch1, u);
    V3 fwd;
    if (s.move == Move::Orbit) {
        eye = s.centre + V3{std::cos(yaw) * s.orbitR, s.orbitH, std::sin(yaw) * s.orbitR};
        fwd = norm(s.centre - eye);
    } else {
        fwd = norm({std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                    std::cos(yaw) * std::cos(pitch)});
    }
    V3 wup{0, 1, 0};
    if (std::fabs(fwd.y) > 0.999f) wup = {0, 0, 1};
    const V3 right = norm(cross(wup, fwd));
    const V3 up = cross(fwd, right);
    Pinhole p;
    p.pos[0] = eye.x, p.pos[1] = eye.y, p.pos[2] = eye.z;
    p.right[0] = right.x, p.right[1] = right.y, p.right[2] = right.z;
    p.up[0] = up.x, p.up[1] = up.y, p.up[2] = up.z;
    p.fwd[0] = fwd.x, p.fwd[1] = fwd.y, p.fwd[2] = fwd.z;
    // The Pinhole defaults: a 60 degree horizontal field over a 4:3 DISPLAY.
    // 512x448 PAL pixels are not square, which is why the two tangents are not
    // in the ratio of the pixel counts - the console has the same asymmetry.
    return p;
}

// --- scene pieces, composable so the whip shot can pan across all of them ----

// Big surfaces are submitted in CHUNKS, one bag each, the way a PS2 scene
// actually submits them (Tyra draws per mesh, and floors and terrain are chunked
// for culling and for the clipper's vertex budget). This is not cosmetic. A bag
// carries ONE screen bbox and ONE depth range, so a single quad under the whole
// camera would report its NEAREST w and its ENTIRE texel count for every tile it
// touches - depth, depthGrad and texDetail would then be wrong over most of the
// screen, and worst of all confidently wrong in the foreground, where the
// checker is magnified rather than minified. Chunking is what makes those
// channels mean anything, and the runtime gets the same granularity for free.
constexpr int kFloorChunks = 8;
constexpr int kWallChunks = 6;

void pieceFloor(std::vector<Object>& objs, const Materials& m, int tex, float ext,
                float uvRep, bool bilinear, const V3& albedo) {
    for (int j = 0; j < kFloorChunks; ++j)
        for (int i = 0; i < kFloorChunks; ++i) {
            auto edge = [&](int k) { return -ext + 2.0f * ext * (float)k / kFloorChunks; };
            auto uvAt = [&](int k) { return uvRep * (float)k / kFloorChunks; };
            const float x0 = edge(i), x1 = edge(i + 1);
            const float z0 = edge(j), z1 = edge(j + 1);
            Object o;
            o.tex = tex;
            o.bilinear = bilinear;
            addQuad(o, {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1}, uvAt(i),
                    uvAt(j), uvAt(i + 1), uvAt(j + 1), albedo);
            finishObject(o, m);
            objs.push_back(std::move(o));
        }
}

void pieceBoxes(std::vector<Object>& objs, const Materials& m, Rng& r, int count,
                float spread, float zBase) {
    for (int i = 0; i < count; ++i) {
        Object o;
        o.tex = m.assetOr((size_t)(i % 3), i & 1 ? m.bands : m.checker);
        o.bilinear = true;
        const float sz = r.range(0.5f, 1.4f);
        addBox(o, {r.range(-spread, spread), sz * 0.5f, zBase + r.range(-spread, spread)},
               {sz * 0.5f, sz * 0.5f, sz * 0.5f}, r.range(1.0f, 2.0f),
               {r.range(0.6f, 1.0f), r.range(0.6f, 1.0f), r.range(0.6f, 1.0f)});
        finishObject(o, m);
        objs.push_back(std::move(o));
    }
}

// Poles about a pixel wide: at a 60 degree horizontal field over 512 output
// pixels, a 0.03-unit post subtends 13.3/distance pixels, so the 6..30 unit
// spread below lands between roughly 2 pixels and half a pixel. Untextured, so
// the aliasing is purely geometric - the case a spatial kernel cannot fix and
// only the temporal one can.
void piecePoles(std::vector<Object>& objs, const Materials& m, Rng& r, int count) {
    for (int i = 0; i < count; ++i) {
        Object o;
        o.tex = -1;
        const float d = 6.0f + 24.0f * ((float)i / (float)std::max(count - 1, 1));
        const float x = r.range(-0.42f, 0.42f) * d;
        const float hgt = r.range(2.2f, 3.6f);
        addBox(o, {x, hgt * 0.5f, d}, {0.015f, hgt * 0.5f, 0.015f}, 1.0f,
               {r.range(0.55f, 0.95f), r.range(0.5f, 0.9f), r.range(0.45f, 0.85f)});
        finishObject(o, m);
        objs.push_back(std::move(o));
    }
}

// Crossed alpha-cutout quads, the classic PS2 shrub. Two quads per plant in one
// object: they are one draw on the console, so they are one bag here.
void pieceFoliage(std::vector<Object>& objs, const Materials& m, Rng& r, int count) {
    for (int i = 0; i < count; ++i) {
        Object o;
        o.tex = m.foliage;
        o.bilinear = true;
        const float x = r.range(-7.0f, 7.0f);
        const float z = r.range(3.0f, 20.0f);
        const float s = r.range(0.7f, 1.5f);
        const V3 albedo{r.range(0.8f, 1.0f), r.range(0.85f, 1.0f), r.range(0.8f, 1.0f)};
        addQuad(o, {x - s, 0, z}, {x + s, 0, z}, {x + s, 2 * s, z}, {x - s, 2 * s, z},
                0, 1, 1, 0, albedo);
        addQuad(o, {x, 0, z - s}, {x, 0, z + s}, {x, 2 * s, z + s}, {x, 2 * s, z - s},
                0, 1, 1, 0, albedo);
        finishObject(o, m);
        objs.push_back(std::move(o));
    }
}

// Chunked along its length, for the reason above - a wall seen edge-on spans the
// entire depth range of the shot, and one bag cannot say so.
void pieceWall(std::vector<Object>& objs, const Materials& m, int tex, const V3& a,
               const V3& b, float height, float uRep, float vRep, const V3& albedo) {
    for (int i = 0; i < kWallChunks; ++i) {
        const V3 p0 = lerp(a, b, (float)i / kWallChunks);
        const V3 p1 = lerp(a, b, (float)(i + 1) / kWallChunks);
        const float u0 = uRep * (float)i / kWallChunks;
        const float u1 = uRep * (float)(i + 1) / kWallChunks;
        Object o;
        o.tex = tex;
        o.bilinear = true;
        addQuad(o, p0, p1, {p1.x, p1.y + height, p1.z}, {p0.x, p0.y + height, p0.z}, u0,
                vRep, u1, 0.0f, albedo);
        finishObject(o, m);
        objs.push_back(std::move(o));
    }
}

std::vector<Shot> buildShots(const CorpusConfig& cfg, const Materials& m) {
    std::vector<Shot> shots;

    // 0 - the classic. A checkerboard running to the horizon, sampled NEAREST
    // (no mipmaps: minification aliasing is the training signal), dollied into
    // so every frame changes the minification ratio.
    {
        Shot s;
        s.name = "floor-horizon";
        s.moveName = "dolly-in";
        s.move = Move::Dolly;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5100u));
        pieceFloor(s.objs, m, m.checker, 200.0f, 100.0f, /*bilinear=*/false,
                   {0.95f, 0.95f, 0.95f});
        pieceBoxes(s.objs, m, r, 4, 6.0f, 22.0f);
        s.eye0 = {0.0f, 1.35f, -16.0f};
        s.eye1 = {0.0f, 1.05f, -2.0f};
        s.pitch0 = s.pitch1 = -0.045f;  // horizon just under the top of frame
        shots.push_back(std::move(s));
    }

    // 1 - curved and hard silhouettes together: a faceted sphere between
    // textured boxes, orbited so the silhouettes sweep across the tile grid.
    {
        Shot s;
        s.name = "boxes-sphere";
        s.moveName = "orbit";
        s.move = Move::Orbit;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5101u));
        pieceFloor(s.objs, m, m.checker, 60.0f, 30.0f, /*bilinear=*/false,
                   {0.8f, 0.82f, 0.85f});
        pieceBoxes(s.objs, m, r, 5, 2.6f, 0.0f);
        {
            Object o;
            o.tex = m.assetOr(3, m.bands);
            o.bilinear = true;
            addSphere(o, {0.0f, 1.15f, 0.0f}, 1.15f, 14, 8, {0.9f, 0.86f, 0.8f});
            finishObject(o, m);
            s.objs.push_back(std::move(o));
        }
        s.centre = {0.0f, 1.0f, 0.0f};
        s.orbitR = 6.2f;
        s.orbitH = 2.6f;
        s.yaw0 = 0.25f;
        s.yaw1 = 1.15f;
        shots.push_back(std::move(s));
    }

    // 2 - sub-pixel geometry, panned. Thin poles are the case where a wrong
    // reconstruction does not blur, it makes the post flicker in and out.
    {
        Shot s;
        s.name = "poles";
        s.moveName = "pan";
        s.move = Move::Pan;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5102u));
        pieceFloor(s.objs, m, m.checker, 80.0f, 40.0f, /*bilinear=*/false,
                   {0.7f, 0.72f, 0.76f});
        piecePoles(s.objs, m, r, 18);
        s.eye0 = s.eye1 = {0.0f, 1.5f, 0.0f};
        s.yaw0 = -0.22f;
        s.yaw1 = 0.22f;
        s.pitch0 = s.pitch1 = -0.06f;
        shots.push_back(std::move(s));
    }

    // 3 - alpha-cutout foliage, STATIC camera. The only thing that changes
    // between frames is the jitter, so this is the shot where the temporal
    // weight should go to the ceiling: two phases averaged is a real 2x
    // supersample of a silhouette the GS cannot antialias any other way.
    {
        Shot s;
        s.name = "foliage";
        s.moveName = "static";
        s.move = Move::Static;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5103u));
        pieceFloor(s.objs, m, m.checker, 60.0f, 24.0f, /*bilinear=*/true,
                   {0.6f, 0.66f, 0.6f});
        pieceFoliage(s.objs, m, r, 14);
        s.eye0 = s.eye1 = {0.0f, 1.6f, -3.0f};
        s.pitch0 = s.pitch1 = -0.08f;
        shots.push_back(std::move(s));
    }

    // 4 - a high-frequency wall at a grazing angle, dollied along. The texel
    // footprint is enormously anisotropic here, which is precisely where a
    // single trilinear-less bilinear tap is worst and where texDetail (a
    // MATERIAL constant, no per-pixel derivative) has to carry the warning.
    {
        Shot s;
        s.name = "grazing-wall";
        s.moveName = "dolly-along";
        s.move = Move::Dolly;
        pieceFloor(s.objs, m, m.noise, 80.0f, 60.0f, /*bilinear=*/true,
                   {0.75f, 0.75f, 0.78f});
        pieceWall(s.objs, m, m.assetOr(1, m.noise), {-2.0f, 0.0f, -10.0f},
                  {-2.0f, 0.0f, 40.0f}, 5.0f, 40.0f, 4.0f, {0.95f, 0.92f, 0.9f});
        pieceWall(s.objs, m, m.bands, {3.5f, 0.0f, -10.0f}, {3.5f, 0.0f, 40.0f}, 5.0f,
                  30.0f, 3.0f, {0.9f, 0.9f, 0.95f});
        s.eye0 = {0.0f, 1.7f, -6.0f};
        s.eye1 = {0.0f, 1.7f, 8.0f};
        s.yaw0 = s.yaw1 = -0.06f;  // wall almost edge-on
        shots.push_back(std::move(s));
    }

    // 5 - the do-nothing case, and it is not filler. A big untextured flat area
    // has nothing to reconstruct: bilinear IS ground truth there, and a net that
    // has never seen one will happily sharpen and ghost a blank wall. It also
    // gives the trainer its low-importance tiles (oracle spread near zero), so
    // the 147 weights are spent where the kernels actually differ.
    {
        Shot s;
        s.name = "flat";
        s.moveName = "slow pan";
        s.move = Move::Pan;
        pieceFloor(s.objs, m, -1, 60.0f, 1.0f, true, {0.52f, 0.5f, 0.48f});
        pieceWall(s.objs, m, -1, {-20.0f, 0.0f, 9.0f}, {20.0f, 0.0f, 9.0f}, 9.0f, 1.0f,
                  1.0f, {0.62f, 0.6f, 0.58f});
        s.eye0 = s.eye1 = {0.0f, 1.7f, 0.0f};
        s.yaw0 = -0.10f;
        s.yaw1 = 0.10f;
        shots.push_back(std::move(s));
    }

    // 6 - the whip. Everything at once, swept 150 degrees over the shot: at ~7
    // frames the peak is over 30 degrees per frame, half a screen width, so the
    // reprojected history is simply not there. The net has to learn that there
    // exists a motion magnitude past which the temporal weight must be zero -
    // otherwise it ghosts every camera turn in a real game.
    {
        Shot s;
        s.name = "whip";
        s.moveName = "whip pan";
        s.move = Move::Whip;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5106u));
        pieceFloor(s.objs, m, m.checker, 120.0f, 60.0f, /*bilinear=*/false,
                   {0.85f, 0.85f, 0.88f});
        pieceBoxes(s.objs, m, r, 4, 7.0f, 6.0f);
        piecePoles(s.objs, m, r, 8);
        pieceFoliage(s.objs, m, r, 6);
        // Three walls around the camera, not one: a 150 degree sweep starts and
        // ends pointing at nothing, and a frame of empty sky teaches the
        // temporal weight nothing about fast motion. Wound so each faces inward
        // (see addQuad) - a wall lit from behind would be flat ambient and the
        // luma channel would go quiet exactly where the motion peaks.
        pieceWall(s.objs, m, m.bands, {-14.0f, 0.0f, 14.0f}, {14.0f, 0.0f, 14.0f}, 6.0f,
                  20.0f, 4.0f, {0.9f, 0.88f, 0.86f});
        // The -X wall faces away from the light and is therefore on ambient
        // alone, so it gets the CHECKER: at 34% brightness a mid-contrast
        // material has nothing left to alias, and a shot whose left half is a
        // black slab is a shot half wasted.
        pieceWall(s.objs, m, m.checker, {-12.0f, 0.0f, -8.0f}, {-12.0f, 0.0f, 16.0f},
                  6.0f, 18.0f, 4.0f, {1.0f, 1.0f, 1.0f});
        pieceWall(s.objs, m, m.noise, {12.0f, 0.0f, 16.0f}, {12.0f, 0.0f, -8.0f}, 6.0f,
                  18.0f, 4.0f, {0.9f, 0.9f, 0.86f});
        s.eye0 = s.eye1 = {0.0f, 1.8f, -4.0f};
        s.yaw0 = -1.35f;
        s.yaw1 = 1.35f;
        s.pitch0 = s.pitch1 = -0.05f;
        shots.push_back(std::move(s));
    }

    return shots;
}

// ------------------------------------------------------------------ histAge ---

// The one recurrent channel, and the corpus owns its state (blss.hpp says so).
// A tile is "still the same tile" while its reprojection stays inside half a
// tile and its coverage and depth hold - past that, last frame's pixels are
// different content and the counter has to restart or the net is told to trust
// history that does not exist.
void ageTiles(std::vector<uint8_t>& age, const std::vector<TileStats>& cur,
              const std::vector<TileStats>& prev, const ReprojField& reproj, int cols,
              int rows, bool firstOfShot) {
    if (firstOfShot || prev.size() != cur.size()) {
        std::fill(age.begin(), age.end(), (uint8_t)0);
        return;
    }
    // THE THRESHOLDS ARE THE ENGINE'S, verbatim - see section 4 of
    // docs/blss-reconstruction.md. They started out different on the two sides
    // (0.10 / 0.10 / 0.5 here against 0.02 / 0.05 / 0.25 there), which silently
    // fed the network a recurrent channel at training time that the console
    // would never reproduce. Change them in one place and the other stops
    // matching, so change the doc first.
    const int rs = cols + 1;
    for (int ty = 0; ty < rows; ++ty)
        for (int tx = 0; tx < cols; ++tx) {
            const size_t i = (size_t)ty * cols + tx;
            // motion, exactly as buildFeatures derives it: the length of the
            // mean of the tile's four corner offsets, over the tile edge.
            const size_t k00 = (size_t)ty * rs + tx, k10 = k00 + 1;
            const size_t k01 = k00 + rs, k11 = k01 + 1;
            float motion = 0.0f;
            if (k11 < reproj.du.size()) {
                const float mdu =
                    (reproj.du[k00] + reproj.du[k10] + reproj.du[k01] + reproj.du[k11]) * 0.25f;
                const float mdv =
                    (reproj.dv[k00] + reproj.dv[k10] + reproj.dv[k01] + reproj.dv[k11]) * 0.25f;
                motion = std::sqrt(mdu * mdu + mdv * mdv) / (float)kTile;
            }
            const float depth = std::min(1.0f, std::max(0.0f, cur[i].depthMean * kDepthRef));
            const float prevDepth =
                std::min(1.0f, std::max(0.0f, prev[i].depthMean * kDepthRef));
            const bool changed = std::fabs(depth - prevDepth) > 0.02f ||
                                 std::fabs(cur[i].cover - prev[i].cover) > 0.05f ||
                                 motion > 0.25f;
            age[i] = changed ? 0 : (uint8_t)std::min(255, (int)age[i] + 1);
        }
}

}  // namespace

// -------------------------------------------------------------------- entry ---

std::vector<CorpusFrame> generate(const CorpusConfig& cfg) {
    std::vector<CorpusFrame> out;
    const int outW = cfg.outW, outH = cfg.outH;
    const int cols = outW / kTile, rows = outH / kTile;
    if (cfg.frames <= 0 || cols <= 0 || rows <= 0) return out;
    const int ss = std::max(cfg.supersample, 1);
    const int sx = scaleX(cfg.scale), sy = scaleY(cfg.scale);
    const int lowW = std::max(outW / sx, 1), lowH = std::max(outH / sy, 1);

    const auto t0 = std::chrono::steady_clock::now();
    const Materials mats = buildMaterials(cfg);
    std::vector<Shot> shots = buildShots(cfg, mats);

    // Frames spread round-robin over the shots, remainder to the first ones. A
    // shot that got a single frame would have no history at all and teach the
    // temporal channel nothing, so with fewer frames than shots the tail shots
    // are simply not rendered rather than degenerating.
    const int shotCount = (int)shots.size();
    std::vector<int> perShot((size_t)shotCount, 0);
    if (cfg.frames < shotCount) {
        for (int i = 0; i < cfg.frames; ++i) perShot[(size_t)i] = 1;
    } else {
        const int base = cfg.frames / shotCount, rem = cfg.frames % shotCount;
        for (int i = 0; i < shotCount; ++i) perShot[(size_t)i] = base + (i < rem ? 1 : 0);
    }

    if (cfg.verbose) {
        std::printf(
            "[blss] corpus: %d frame(s) over %d shot(s), %dx%d out, %dx%d low, "
            "%dx supersample, %dx%d tiles, seed 0x%X\n",
            cfg.frames, shotCount, outW, outH, lowW, lowH, ss, cols, rows, cfg.seed);
    }

    out.reserve((size_t)cfg.frames);
    std::vector<float> zb;   // raster scratch, reused across every pass
    Image big;               // the supersampled render, likewise
    for (int si = 0; si < shotCount; ++si) {
        const int n = perShot[(size_t)si];
        if (n <= 0) continue;
        const Shot& shot = shots[(size_t)si];
        const auto ts = std::chrono::steady_clock::now();

        std::vector<uint8_t> age((size_t)cols * rows, 0);
        std::vector<TileStats> prevStats;
        Image prevLow;
        for (int i = 0; i < n; ++i) {
            const float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
            const float tPrev = n > 1 ? (float)(i - 1) / (float)(n - 1) : 0.0f;
            const Pinhole cur = cameraAt(shot, t);
            // Frame 0 has no predecessor: prev == cur makes every reprojection
            // offset zero and prevLow its own render, which is exactly what the
            // console holds on a scene cut.
            const Pinhole prev = i > 0 ? cameraAt(shot, tPrev) : cur;

            CorpusFrame cf;
            cf.shot = si;
            Frame& fr = cf.frame;
            fr.cols = cols, fr.rows = rows;
            fr.outW = outW, fr.outH = outH;
            fr.scale = cfg.scale;
            fr.phase = i % kJitterPhases;

            // Ground truth: rendered at ss x the display size and box-resolved.
            // Point samples on a regular grid, so it is not a perfect
            // band-limit of a checkerboard at the horizon - but it is the same
            // definition the oracle and psnr() are measured against, and 16
            // samples per output pixel is far past what the console will ever
            // see.
            renderScene(shot.objs, mats, cur, outW * ss, outH * ss, 0.0f, 0.0f, big, zb);
            cf.truth = ss > 1 ? boxDown(big, ss) : big;
            // The "no BLSS" baseline: full resolution, one sample, no jitter.
            renderScene(shot.objs, mats, cur, outW, outH, 0.0f, 0.0f, cf.native, zb);
            // What the console actually has: the jittered low-res target. The
            // jitter is in LOW-RES pixels and this render's pixels ARE low-res
            // pixels, so it goes straight in as the raster offset.
            renderScene(shot.objs, mats, cur, lowW, lowH, jitterX(fr.phase),
                        jitterY(fr.phase), fr.low, zb);
            fr.prevLow = i > 0 ? prevLow : fr.low;

            // ...and what the EE knows ABOUT it: one bag per drawn object, from
            // the unjittered display-resolution projection.
            const std::vector<BagProxy> bags = bagList(shot.objs, mats, cur, outW, outH);
            const std::vector<TileStats> stats =
                accumulate(cols, rows, outW, outH, bags);
            // History size is the DISPLAY size: the runtime's history is the
            // other display framebuffer (docs/blss-reconstruction.md section 6),
            // so one history texel is one output pixel.
            fr.reproj = buildReproj(cols, rows, outW, outH, outW, outH, cur, prev, stats);
            // Features FIRST, then the age counters - and that order is load
            // bearing. RendererCoreBlss::composite() runs buildFeatures(),
            // runNet() and only then updateHistAge(), so the console's network
            // sees the counter as it stood at the END OF THE PREVIOUS FRAME. Age
            // the tiles first and the corpus would hand the net a channel that
            // already knows about the change this frame - a one-frame lookahead
            // the hardware does not have, which is the sort of thing that trains
            // beautifully and then ghosts on the console.
            fr.features = buildFeatures(cols, rows, stats, fr.reproj, age);
            ageTiles(age, stats, prevStats, fr.reproj, cols, rows, i == 0);

            prevStats = stats;
            prevLow = fr.low;
            out.push_back(std::move(cf));
        }
        if (cfg.verbose) {
            const double ms = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - ts)
                                  .count();
            std::printf("[blss]   shot %d %-14s %-11s %2d frame(s)  %7.0f ms (%.0f ms/frame)\n",
                        si, shot.name, shot.moveName, n, ms, ms / (double)n);
        }
    }

    if (cfg.verbose) {
        const double s = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
        std::printf("[blss] corpus ready: %zu frame(s), %zu tile sample(s), %.1f s\n",
                    out.size(), out.size() * (size_t)(cols * rows), s);
    }
    return out;
}

}  // namespace blss
