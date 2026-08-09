#include "blsscorpus.hpp"

#include "blssscene.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// The corpus generator (contract and rationale: blsscorpus.hpp; arithmetic:
// docs/blss-reconstruction.md). Four parts, in order: a deterministic software
// rasteriser, the material set, the scene bestiary, and generate() driving them.
//
// THE ONE RULE. The images below are the ground truth and the console's inputs -
// they may be as good as the host can make them. Everything the NETWORK is told
// about a frame must come out of a BagProxy: a screen bounding box, a w range
// and two material constants, because that is all the EE holds while it submits
// a frame (blss.hpp, "ONE SUBMITTED DRAW, AS THE EE SEES IT"). So `texDetail`
// is the minification ratio of section 2, computed from the material's
// dimensions and the bag's screen area - never anything measured off the pixels
// that were just rendered, which would train a network the console cannot run.
// The proxy carried a brightness too, until the measurement in blss.hpp's
// kFeatures note showed the EE could only ever fill it for single-coloured
// bags; a channel one side cannot produce is worse than no channel.

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

// The engine's RendererSettings::aspectRatio for the shipped 512x448 raster -
// the ratio of the PIXEL counts, which is what multiplies tan(fov/2) into the
// horizontal tangent (see cameraAt, Move::Path). The procedural bestiary keeps
// its own 4:3 display framing; a project shot uses this one, because a project
// shot is compared against a console frame.
constexpr float kRasterAspect = 512.0f / 448.0f;

// The background. Deliberately FLAT: the sky is not a submitted bag, so the
// feature vector describes those tiles as coverage 0, and a gradient there would
// hand the oracle contrast the network has no way to predict. Flat sky teaches
// the correct lesson instead - no coverage, do nothing.
constexpr uint8_t kSky[3] = {96, 116, 148};

// ---------------------------------------------------------------- textures ---

// One material. Its DIMENSIONS are the only thing that reaches the network
// (through BagProxy::texDetail), and they are a property of the ASSET - a
// build-time constant, not a look at the frame.
//
// Dimensions are always powers of two, because that is all the GS can address:
// so REPEAT wrapping is a mask here, not a modulo, and the innermost loop of the
// whole corpus does not pay eight integer divides per bilinear tap.
struct Texture {
    int w = 0, h = 0;
    int wMask = 0, hMask = 0;
    std::vector<uint8_t> px;  // RGBA8, row-major
    bool cutout = false;      // alpha-tested (foliage); no blending anywhere

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
    return t;
}

// The material table plus the names the bestiary refers to. `asset` holds the
// real PNGs, in scan order; every user of one falls back to a procedural
// material so the corpus is identical in shape with or without examples/.
struct Materials {
    std::vector<Texture> tex;
    int checker = -1, noise = -1, foliage = -1, bands = -1;
    std::vector<int> asset;
    // Project materials, by absolute path. A scene assigns the same handful of
    // .mtl files to dozens of objects, and the ground texture of a 144-chunk
    // terrain is one image, not 144.
    std::map<std::string, int> byPath;

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

// --- project materials -------------------------------------------------------

// Nearest power of two at or below `v`, clamped into what the GS can address.
int potBelow(int v) {
    int p = 1;
    while (p * 2 <= v && p * 2 <= 1024) p *= 2;
    return p < 8 ? 8 : p;
}

// The GS cannot address a non-power-of-two texture, so a project PNG that is
// not one is box-resampled to the nearest power of two at or below its size -
// which is what the texture bake would do to it on the way to the console
// anyway. Resampling rather than rejecting matters: the ground texture is
// usually the single biggest thing on screen, and a corpus that drew it flat
// would train the texDetail channel on a scene the game does not render.
Texture toTexture(const Image& img, bool keepAlpha) {
    Texture t;
    const int tw = isPow2(img.w) ? img.w : potBelow(img.w);
    const int th = isPow2(img.h) ? img.h : potBelow(img.h);
    t.setSize(tw, th);
    for (int y = 0; y < th; ++y) {
        const int sy0 = (int)((int64_t)y * img.h / th);
        const int sy1 = std::max(sy0 + 1, (int)((int64_t)(y + 1) * img.h / th));
        for (int x = 0; x < tw; ++x) {
            const int sx0 = (int)((int64_t)x * img.w / tw);
            const int sx1 = std::max(sx0 + 1, (int)((int64_t)(x + 1) * img.w / tw));
            int acc[4] = {0, 0, 0, 0}, n = 0;
            for (int sy = sy0; sy < sy1 && sy < img.h; ++sy)
                for (int sx = sx0; sx < sx1 && sx < img.w; ++sx) {
                    const uint8_t* p = img.at(sx, sy);
                    for (int k = 0; k < 4; ++k) acc[k] += p[k];
                    ++n;
                }
            uint8_t* d = &t.px[((size_t)y * tw + x) * 4];
            for (int k = 0; k < 4; ++k) d[k] = (uint8_t)(n ? acc[k] / n : 0);
            if (!keepAlpha) d[3] = 255;
        }
    }
    return t;
}

// Loads one project texture, cached by absolute path. -1 = untextured (missing
// file, unreadable PNG, or no texture assigned).
int projectTexture(Materials& m, const std::string& absPath, bool cutout) {
    if (absPath.empty()) return -1;
    const auto it = m.byPath.find(absPath);
    if (it != m.byPath.end()) return it->second;
    Image img;
    int idx = -1;
    if (readPng(img, absPath) && img.w >= 2 && img.h >= 2) {
        Texture t = toTexture(img, cutout);
        t.cutout = cutout;
            idx = (int)m.tex.size();
        m.tex.push_back(std::move(t));
    }
    m.byPath.emplace(absPath, idx);
    return idx;
}

// ...and one whose PNG never existed as a file. An animated model's texture is
// embedded in the .glb; the build writes it out next to the ELF, so the console
// is textured and the corpus must be too - a part called untextured here would
// report texDetail = 0 against a console twin that reports a real minification
// ratio. Cached by (scene, index) rather than by path, since there is no path.
int embeddedTexture(Materials& m, const ProjectScene& ps, int index, bool cutout) {
    if (index < 0 || index >= (int)ps.embedded.size()) return -1;
    char key[96];
    std::snprintf(key, sizeof(key), "@glb:%p:%d", (const void*)&ps, index);
    const auto it = m.byPath.find(key);
    if (it != m.byPath.end()) return it->second;
    Image img;
    int idx = -1;
    const std::vector<unsigned char>& png = ps.embedded[(size_t)index];
    if (readPngMemory(img, png.data(), png.size()) && img.w >= 2 && img.h >= 2) {
        Texture t = toTexture(img, cutout);
        t.cutout = cutout;
        idx = (int)m.tex.size();
        m.tex.push_back(std::move(t));
    }
    m.byPath.emplace(key, idx);
    return idx;
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

// One drawn object == one submitted BAG, and one bag is SEVERAL BagProxies -
// see `part` below. Everything under the line is what the bag submission
// carries; it is all computed here, at build time, from the mesh and the
// material.
struct Object {
    std::vector<Vertex> vert;
    std::vector<int> idx;  // 3 per triangle
    int tex = -1;          // index into Materials::tex, -1 = untextured
    bool bilinear = true;
    bool cutout = false;
    // ---- bag payload
    float texelArea = 0.0f;  // texels the material spans over this object
    V3 lo{}, hi{};           // world AABB of the whole bag
    // ONE BOX PER VU1 PACKAGE, in submission order - the exact shape
    // StaPipBagPackagesBBox hands StaPipCore, which hands it to
    // RendererCoreBlss::addBagBox. Filled by finishObject(); a single entry
    // equal to lo/hi is the no-split case.
    std::vector<std::pair<V3, V3>> part;
    // A camera-centred shell (the sky dome, the star field, the sun and moon)
    // is DRAWN and never DESCRIBED - the twin of PipelineInfoBag::blssProxy.
    bool proxy = true;
    // Not submitted past this distance from the camera (drawDistance / the
    // terrain's streaming view distance). 0 = always submitted.
    float viewDist = 0.0f;
    V3 centre{};
};

// VERTICES PER PROXY, and it is derived rather than chosen. StaPipCore splits a
// bag into packages of `maxVertCount` vertices and computes one bounding box per
// `maxVertCount / 3` of them; maxVertCount comes out of
// StaPipVU1Program::getMaxVertCount:
//
//   bufferSize   = (VU1_STAPIP_DBUFFER_END - VU1_STAPIP_LAST_ITEM_ADDR - 1)/2 - 1
//                = (944 - 22)/2 - 1 = 460
//   res          = (460 - 9) / (elementsPerVertex + reglistCount)
//   maxVertCount = res / 9 * 9                       (whole triangles, twice)
//
// For the Cull-TC program - textured, per-vertex colours, which is what a
// generated game's static geometry mostly is - that is (451 / 6) / 9 * 9 = 72,
// so 72 / 3 = 24 vertices, eight triangles, per proxy. The untextured and
// single-colour classes come out at 36 and 30; 24 is the finest of the three and
// therefore the one that cannot flatter the corpus by describing it more
// coarsely than the console does.
constexpr int kProxyVerts = 24;

// ...and the same cap the engine applies (RendererCoreBlss::kMaxProxiesPerBag).
// Above it, CONSECUTIVE parts merge, which can only enlarge a box and never
// move it - so the worst case degrades toward the whole-bag proxy rather than
// lying about where the geometry is.
constexpr int kMaxProxiesPerBag = 32;

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

// World AABB + the one bag material constant left. `texelArea` is the
// material's texel count, the numerator of the minification ratio of section 2 -
// the denominator is the bag's screen area, and only their ratio reaches the
// network.
//
// PARITY, NOT PREFERENCE: the RAW texture area, with no UV span, because that is
// all the engine can pass - `stapip_core.cpp` hands BLSS `texW * texH` and has
// no idea how many times the material tiles over the surface. Folding the UV
// span in here (which it used to) makes this channel a genuinely better
// aliasing predictor and a genuinely different quantity from the one the
// console computes: a floor tiling 100x trained at texDetail 1.0 and ran at
// ~0.03. A weaker feature both sides agree on beats a strong feature only one
// side has - the same judgement that deleted `luma` outright once it turned out
// the EE could not produce it at all (blss.hpp, kFeatures).
//
// The fix that makes it strong again is a baked per-material UV-repeat constant
// the engine can multiply in - docs/neural-upscaler.md's "bake real texture
// detail" follow-up.
void finishObject(Object& o, const Materials& m, bool packageSplit = true) {
    if (o.vert.empty()) return;
    o.lo = o.hi = o.vert[0].p;
    for (const Vertex& v : o.vert) {
        o.lo.x = std::min(o.lo.x, v.p.x), o.hi.x = std::max(o.hi.x, v.p.x);
        o.lo.y = std::min(o.lo.y, v.p.y), o.hi.y = std::max(o.hi.y, v.p.y);
        o.lo.z = std::min(o.lo.z, v.p.z), o.hi.z = std::max(o.hi.z, v.p.z);
    }
    if (o.tex >= 0) {
        const Texture& t = m.tex[(size_t)o.tex];
        o.texelArea = (float)t.w * (float)t.h;
        o.cutout = t.cutout;
    }
    o.centre = (o.lo + o.hi) * 0.5f;

    // The package split, over the DRAWN vertex order (o.idx), because that is
    // the order the EE uploads and therefore the order the boxes are cut in.
    o.part.clear();
    const int drawn = (int)o.idx.size();
    if (!packageSplit || drawn <= kProxyVerts) {
        o.part.emplace_back(o.lo, o.hi);
        return;
    }
    const int parts = (drawn + kProxyVerts - 1) / kProxyVerts;
    const int group = parts <= kMaxProxiesPerBag
                          ? 1
                          : (parts + kMaxProxiesPerBag - 1) / kMaxProxiesPerBag;
    const int stride = kProxyVerts * group;
    o.part.reserve((size_t)((drawn + stride - 1) / stride));
    for (int base = 0; base < drawn; base += stride) {
        const int end = std::min(base + stride, drawn);
        V3 lo = o.vert[(size_t)o.idx[base]].p, hi = lo;
        for (int k = base + 1; k < end; ++k) {
            const V3& p = o.vert[(size_t)o.idx[k]].p;
            lo.x = std::min(lo.x, p.x), hi.x = std::max(hi.x, p.x);
            lo.y = std::min(lo.y, p.y), hi.y = std::max(hi.y, p.y);
            lo.z = std::min(lo.z, p.z), hi.z = std::max(hi.z, p.z);
        }
        o.part.emplace_back(lo, hi);
    }
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
// `extra` is the frame's ANIMATED objects - the poses picked for this frame out
// of the pre-built pose table. It is a pointer list rather than a second
// std::vector<Object> because the poses live in per-part tables and gathering
// them by value once per frame would copy a character's mesh for nothing.
// Drawn in the SAME pass as the static set, after it: the z buffer sorts them,
// and a second renderScene() call would clear the target.
void renderScene(const std::vector<Object>& objs, const Materials& mats,
                 const Pinhole& cam, int rw, int rh, float offX, float offY,
                 Image& img, std::vector<float>& zb,
                 const std::vector<const Object*>* extra = nullptr) {
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

    std::vector<const Object*> all;
    all.reserve(objs.size() + (extra ? extra->size() : 0));
    for (const Object& o : objs) all.push_back(&o);
    if (extra)
        for (const Object* o : *extra) all.push_back(o);

    for (const Object* objPtr : all) {
        const Object& o = *objPtr;
        // The console's own submission test: past its draw distance a bag is
        // not drawn, so it is not in the ground truth either (bagList applies
        // the same test to the proxy list - the two must agree).
        if (o.viewDist > 0.0f) {
            const V3 d{o.centre.x - cam.pos[0], o.centre.y - cam.pos[1],
                       o.centre.z - cam.pos[2]};
            if (dot(d, d) > o.viewDist * o.viewDist) continue;
        }
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

// One PACKAGE of the object, as the EE would submit it: its world AABB
// transformed, clipped to the near plane and projected into OUTPUT pixels
// (unjittered - the bag list describes the frame, not the raster offset it
// happened to be drawn with). Twelve edges of eight corners, which is the whole
// cost of a proxy on the EE.
bool bagOf(const Object& o, const V3& blo, const V3& bhi, const Materials& mats,
           const Pinhole& cam, int outW, int outH, BagProxy& out) {
    const float sxScale = 0.5f * (float)outW / cam.tanHalfFovX;
    const float syScale = 0.5f * (float)outH / cam.tanHalfFovY;
    struct P {
        float vx, vy, w;
    };
    P corner[8];
    for (int c = 0; c < 8; ++c) {
        const V3 p{(c & 1) ? bhi.x : blo.x, (c & 2) ? bhi.y : blo.y,
                   (c & 4) ? bhi.z : blo.z};
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
    // A BOX THAT STRADDLES THE EYE AND STILL FILLS THE FRAME DESCRIBES NOTHING.
    // Twinned with RendererCoreBlss::addBagBox, where it earns its keep: a
    // generated game's sky dome is a 90-unit sphere centred on the camera, so
    // every one of its package boxes wraps the eye, and one of them was handing
    // all 196 tiles "fully covered, at the nearest representable depth" -
    // depth, depthGrad and coverage pinned at 1 across the whole frame. The
    // bbox is the frame BY CONSTRUCTION there and wNear is the clip constant,
    // not a measurement.
    //
    // It is a NO-OP on this corpus and that is deliberate rather than lucky:
    // nothing here encloses the camera (the floors are zero-thickness quads at
    // y = 0 under an eye at y > 1, the walls are zero-thickness in x), so the
    // rule can be stated on both sides without any corpus frame changing by a
    // single byte - which is what keeps a fold table comparable across it.
    if (wn <= kNear * 1.0001f && x0 <= 0.0f && y0 <= 0.0f && x1 >= (float)outW &&
        y1 >= (float)outH)
        return false;
    out.x0 = std::max(x0, 0.0f);
    out.y0 = std::max(y0, 0.0f);
    out.x1 = std::min(x1, (float)outW);
    out.y1 = std::min(y1, (float)outH);
    out.wNear = std::max(wn, kNear);
    out.wFar = std::max(wf, out.wNear);
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

// THE PROXY BUDGET, and it is the fifth rule of the twin contract
// (docs/blss-reconstruction.md section 2 - read it there, it is the engine
// agent's file and it is the normative statement).
//
// A proxy's ONLY effect is on the tiles its screen bbox overlaps, and the grid
// resolves nothing finer than a kTile square. So describing a bag with more
// boxes than it covers tiles buys nothing the accumulator can represent, while
// costing a projection (eight corners, twelve edges) and a full tile update per
// extra box. The cap is therefore the bag's own tile footprint, counted with
// addBag's arithmetic so the two sides agree on the boundary case:
//
//     tiles = (cx1 - cx0 + 1) * (cy1 - cy0 + 1)   of the WHOLE bag's box
//     cap   = clamp(tiles, 1, kMaxProxiesPerBag)
//           = kMaxProxiesPerBag  when the whole box describes nothing at all
//     group = ceil(parts / cap)                   the existing consecutive merge
//
// It is CAMERA-DEPENDENT, which is why it lives here and not in finishObject():
// the same bag is worth 32 boxes across the screen and 1 in the distance.
//
// Two things it deliberately is not. It is not a screen-AREA floor - that was
// considered on the engine side and REJECTED, because a rule that can take a
// distant bag's proxy count to zero hands its tiles `coverage = 0`, which the
// network reads as "there is nothing here" rather than "there is something
// small here". The cap is never below 1. And it is not a fidelity-free change:
// merging consecutive parts can only ENLARGE a box, never move it, so the worst
// case degrades toward the whole-bag proxy this feature spent eleven commits
// getting away from - which is exactly why it ships off until measured.
//
// Returns the number of consecutive parts that merge into one proxy.
int proxyGroupSize(const Object& o, const Materials& mats, const Pinhole& cam, int outW,
                   int outH) {
    const int parts = (int)o.part.size();
    if (parts <= 1) return 1;
    // The corpus' own grid, derived the way generate() derives it.
    const int tile = tileSize();
    const int cols = std::max(outW / tile, 1), rows = std::max(outH / tile, 1);
    BagProxy whole;
    int cap = kMaxProxiesPerBag;
    if (bagOf(o, o.lo, o.hi, mats, cam, outW, outH, whole)) {
        // addBag's clamps and its -1e-3f, so a box ending exactly on a tile
        // boundary does not claim the tile after it. Getting this wrong is a
        // one-tile disagreement that no PSNR column can see.
        const auto clampTile = [](int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
        const int cx0 = clampTile((int)std::floor(whole.x0 / (float)tile), cols - 1);
        const int cx1 = clampTile((int)std::floor((whole.x1 - 1e-3f) / (float)tile), cols - 1);
        const int cy0 = clampTile((int)std::floor(whole.y0 / (float)tile), rows - 1);
        const int cy1 = clampTile((int)std::floor((whole.y1 - 1e-3f) / (float)tile), rows - 1);
        const int tiles = (cx1 - cx0 + 1) * (cy1 - cy0 + 1);
        cap = std::min(std::max(tiles, 1), kMaxProxiesPerBag);
    }
    return (parts + cap - 1) / cap;
}

std::vector<BagProxy> bagList(const std::vector<Object>& objs, const Materials& mats,
                              const Pinhole& cam, int outW, int outH,
                              const std::vector<const Object*>* extra = nullptr,
                              bool proxyBudget = false) {
    std::vector<BagProxy> out;
    out.reserve(objs.size() + (extra ? extra->size() : 0));
    BagProxy b;
    std::vector<const Object*> all;
    all.reserve(objs.size() + (extra ? extra->size() : 0));
    for (const Object& o : objs) all.push_back(&o);
    if (extra)
        for (const Object* o : *extra) all.push_back(o);
    for (const Object* objPtr : all) {
        const Object& o = *objPtr;
        // A bag the console never submits describes nothing and is drawn by
        // nothing - see Object::proxy and Object::viewDist. Both tests are here
        // rather than at render time so the two lists cannot disagree; the
        // renderer applies viewDist itself for the same reason.
        if (!o.proxy) continue;
        if (o.viewDist > 0.0f) {
            const V3 d{o.centre.x - cam.pos[0], o.centre.y - cam.pos[1],
                       o.centre.z - cam.pos[2]};
            if (dot(d, d) > o.viewDist * o.viewDist) continue;
        }
        const int group = proxyBudget ? proxyGroupSize(o, mats, cam, outW, outH) : 1;
        if (group <= 1) {
            for (const std::pair<V3, V3>& p : o.part)
                if (bagOf(o, p.first, p.second, mats, cam, outW, outH, b))
                    out.push_back(b);
            continue;
        }
        // The consecutive-part merge, the same one finishObject() applies to the
        // fixed cap: union the world AABBs of `group` consecutive parts. The
        // parts partition consecutive vertex RANGES, so the union of their boxes
        // IS the box over the union range - this is the same merge, applied per
        // frame because the budget is per frame.
        const int parts = (int)o.part.size();
        for (int base = 0; base < parts; base += group) {
            const int end = std::min(base + group, parts);
            V3 lo = o.part[(size_t)base].first, hi = o.part[(size_t)base].second;
            for (int k = base + 1; k < end; ++k) {
                lo.x = std::min(lo.x, o.part[(size_t)k].first.x);
                lo.y = std::min(lo.y, o.part[(size_t)k].first.y);
                lo.z = std::min(lo.z, o.part[(size_t)k].first.z);
                hi.x = std::max(hi.x, o.part[(size_t)k].second.x);
                hi.y = std::max(hi.y, o.part[(size_t)k].second.y);
                hi.z = std::max(hi.z, o.part[(size_t)k].second.z);
            }
            if (bagOf(o, lo, hi, mats, cam, outW, outH, b)) out.push_back(b);
        }
    }
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
    // A POLYLINE of (eye, look-at) keys - what a project scene produces. Every
    // move blssscene builds, authored or automatic, arrives in this one form,
    // so there is a single evaluator rather than a second copy of the five
    // above that could drift from them.
    Path,
};

struct Shot {
    // Owning, because a project's shot names are built at runtime. The
    // bestiary's literals cost one small allocation each at corpus build.
    std::string name;
    std::string moveName;
    Move move = Move::Static;
    std::vector<Object> objs;
    V3 eye0{}, eye1{};
    float yaw0 = 0, yaw1 = 0;      // radians; also the orbit angle
    float pitch0 = 0, pitch1 = 0;  // negative = looking down
    V3 centre{};                   // orbit only
    float orbitR = 6.0f, orbitH = 2.0f;
    // Move::Path only. Shared objects: a project scene contributes several
    // shots over ONE geometry set, and copying a 200 000-triangle scene six
    // times is minutes of wall clock and a gigabyte for nothing.
    const std::vector<Object>* shared = nullptr;
    // ...and the scene's ANIMATED parts, one finished Object per part per
    // console frame. Shared for the same reason and indexed per frame: the
    // frame loop picks pose i and pose i-1 and hands them to renderScene and
    // bagList, so the ground truth and the proxy list describe the same pose.
    const std::vector<std::vector<Object>>* animShared = nullptr;
    std::vector<float> pathEye, pathLook;  // 3 per key
    float ease = 0.0f;                     // 1 = smoothstep in t (the whip)
    float fovDeg = 60.0f;

    // Which member of a union corpus this shot belongs to (CorpusFrame::group).
    // 0 for the bestiary and for a single project.
    int group = 0;
    // SceneShot::frames - 0 = an equal share of --frames, > 0 = exactly this
    // many. The bestiary never sets it, so its split is unchanged.
    int frames = 0;

    const std::vector<Object>& geometry() const { return shared ? *shared : objs; }
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

    // A project shot: a polyline of (eye, look-at) keys, sampled at u.
    if (s.move == Move::Path) {
        const int n = (int)(s.pathEye.size() / 3);
        Pinhole p;
        if (n <= 0) return p;
        if (s.ease > 0.0f)
            u = lerpf(u, u * u * (3.0f - 2.0f * u), clamp01(s.ease));
        const float f = clamp01(u) * (float)(n - 1);
        const int i0 = std::min((int)f, n - 1);
        const int i1 = std::min(i0 + 1, n - 1);
        const float k = f - (float)i0;
        const auto key = [&](const std::vector<float>& a, int i) {
            return V3{a[(size_t)i * 3], a[(size_t)i * 3 + 1], a[(size_t)i * 3 + 2]};
        };
        const V3 eye = lerp(key(s.pathEye, i0), key(s.pathEye, i1), k);
        const V3 look = lerp(key(s.pathLook, i0), key(s.pathLook, i1), k);
        V3 fwd = norm(look - eye);
        V3 wup{0, 1, 0};
        if (std::fabs(fwd.y) > 0.999f) wup = {0, 0, 1};
        const V3 right = norm(cross(wup, fwd));
        const V3 up = cross(fwd, right);
        p.pos[0] = eye.x, p.pos[1] = eye.y, p.pos[2] = eye.z;
        p.right[0] = right.x, p.right[1] = right.y, p.right[2] = right.z;
        p.up[0] = up.x, p.up[1] = up.y, p.up[2] = up.z;
        p.fwd[0] = fwd.x, p.fwd[1] = fwd.y, p.fwd[2] = fwd.z;
        // THE ENGINE'S OWN PROJECTION, not the bestiary's. RendererCoreBlss
        // takes tanHalfFovY = tan(fov/2) and tanHalfFovX = that times
        // RendererSettings::aspectRatio, which for the shipped 512x448 raster
        // is 512/448 - the RASTER ratio, not the 4:3 display one the procedural
        // shots assume. Getting this wrong would put every bag proxy in the
        // wrong tile column, which is the one error no feature table can see.
        const float tang = std::tan(s.fovDeg * 0.5f * kPi / 180.0f);
        p.tanHalfFovY = tang;
        p.tanHalfFovX = tang * kRasterAspect;
        return p;
    }

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
        // (see addQuad) - a wall lit from behind would be flat ambient, and a
        // flat wall has nothing to reconstruct exactly where the motion peaks.
        pieceWall(s.objs, m, m.bands, {-14.0f, 0.0f, 14.0f}, {14.0f, 0.0f, 14.0f}, 6.0f,
                  20.0f, 4.0f, {0.9f, 0.88f, 0.86f});
        // The -X wall faces away from the light and is therefore on ambient
        // alone, so it gets the CHECKER: at 34% brightness a mid-contrast
        // material has nothing left to alias, and a shot whose left half is a
        // black slab is a shot half wasted (the network no longer has a
        // brightness channel, but the GROUND TRUTH still has contrast, and a
        // dark slab has none for the oracle to reconstruct).
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

    // ------------------------------------------------------------------------
    // 7.. - THE SECOND HALF OF THE BESTIARY, and it exists because a measurement
    // asked for it rather than because the list looked short.
    //
    // Leave-one-shot-out cross-validation (`--blss-eval --cv`) put the shipped
    // 2-of-7 split next to the seven single-shot folds it is a draw from, and the
    // gap was the finding: holding out ONE shot and training on six scores
    // +0.31 dB over bilinear, holding out TWO and training on five scores +0.10
    // with four times the spread. One shot of training data was worth 0.2 dB and
    // most of the variance the docs had been attributing to the seed - i.e. the
    // network was data-starved, and the corpus was the binding constraint, not
    // the objective or the topology.
    //
    // So these six add the CONTENT AND CAMERA CLASSES the first seven had no
    // representative of: lateral parallax, a pitch through the horizon, close
    // geometry that saturates the depth channel, extreme minification that does
    // not, curved silhouettes at several distances, and cutouts under motion
    // rather than standing still. They are appended, never inserted, and they
    // draw their randomness from fresh mix32 purposes - so shots 0..6 render
    // BIT-IDENTICALLY with or without them, which is what makes a before/after
    // fold table a comparison instead of two different experiments.
    // ------------------------------------------------------------------------

    // 7 - a corridor, dollied down. Everything is within a few units of the eye,
    // which is where `depth` saturates (it reads 1.0 for anything closer than
    // kDepthRef = 8), so this is the shot that says what the net does when that
    // channel is a constant - and it is also what an indoor PS2 game looks like
    // for most of its running time.
    {
        Shot s;
        s.name = "corridor";
        s.moveName = "dolly-down";
        s.move = Move::Dolly;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5107u));
        pieceFloor(s.objs, m, m.assetOr(0, m.checker), 24.0f, 24.0f, /*bilinear=*/true,
                   {0.7f, 0.7f, 0.72f});
        pieceWall(s.objs, m, m.assetOr(1, m.noise), {-2.2f, 0.0f, -6.0f}, {-2.2f, 0.0f, 26.0f},
                  3.2f, 24.0f, 2.0f, {0.95f, 0.92f, 0.88f});
        pieceWall(s.objs, m, m.bands, {2.2f, 0.0f, 26.0f}, {2.2f, 0.0f, -6.0f}, 3.2f, 24.0f,
                  2.0f, {0.9f, 0.9f, 0.95f});
        // A ceiling, because a corridor without one is a trench: it is the half
        // of the frame that would otherwise be sky, and sky teaches nothing here.
        pieceWall(s.objs, m, m.checker, {-2.2f, 3.2f, -6.0f}, {-2.2f, 3.2f, 26.0f}, 0.0f, 24.0f,
                  1.0f, {0.55f, 0.55f, 0.6f});
        {
            Object o;
            o.tex = m.checker;
            o.bilinear = true;
            addQuad(o, {-2.2f, 3.2f, -6.0f}, {2.2f, 3.2f, -6.0f}, {2.2f, 3.2f, 26.0f},
                    {-2.2f, 3.2f, 26.0f}, 0, 0, 4.0f, 24.0f, {0.5f, 0.5f, 0.54f});
            finishObject(o, m);
            s.objs.push_back(std::move(o));
        }
        pieceBoxes(s.objs, m, r, 4, 1.6f, 12.0f);
        s.eye0 = {0.0f, 1.5f, -4.0f};
        s.eye1 = {0.0f, 1.5f, 12.0f};
        s.pitch0 = s.pitch1 = -0.02f;
        shots.push_back(std::move(s));
    }

    // 8 - a STRAFE past a box field. Every other move in the bestiary is a
    // rotation, a zoom or a whip; a sideways translation is the one that produces
    // real parallax, so near geometry slides across far geometry and the tile's
    // single representative depth reprojects the two of them to the same place.
    // This is the disocclusion case, and it is where a temporal weight that is
    // not gated on depthGrad ghosts.
    {
        Shot s;
        s.name = "strafe-field";
        s.moveName = "dolly-lateral";
        s.move = Move::Dolly;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5108u));
        pieceFloor(s.objs, m, m.checker, 70.0f, 35.0f, /*bilinear=*/false,
                   {0.78f, 0.78f, 0.8f});
        pieceBoxes(s.objs, m, r, 5, 3.0f, 4.0f);
        pieceBoxes(s.objs, m, r, 4, 8.0f, 16.0f);
        piecePoles(s.objs, m, r, 6);
        s.eye0 = {-5.0f, 1.5f, -2.0f};
        s.eye1 = {5.0f, 1.5f, -2.0f};
        s.yaw0 = s.yaw1 = 0.0f;  // look straight ahead while translating sideways
        s.pitch0 = s.pitch1 = -0.05f;
        shots.push_back(std::move(s));
    }

    // 9 - a PITCH through the horizon, floor to sky. Coverage sweeps from 1 to
    // nearly 0 over the shot, which is the one thing that makes the empty-tile
    // path (kMinCoverage, and the sky-ghosting bug it was written for) a moving
    // target rather than a corner of the frame.
    {
        Shot s;
        s.name = "pitch-sky";
        s.moveName = "pitch-up";
        s.move = Move::Pan;
        Rng r;
        r.seed(mix32(cfg.seed, 0x5109u));
        pieceFloor(s.objs, m, m.checker, 90.0f, 45.0f, /*bilinear=*/false,
                   {0.82f, 0.8f, 0.78f});
        pieceBoxes(s.objs, m, r, 5, 5.0f, 10.0f);
        pieceFoliage(s.objs, m, r, 6);
        s.eye0 = s.eye1 = {0.0f, 1.6f, -2.0f};
        s.yaw0 = s.yaw1 = 0.05f;
        s.pitch0 = -0.42f;  // looking at the floor a few units ahead
        s.pitch1 = 0.34f;   // ...and up into empty sky
        shots.push_back(std::move(s));
    }

    // 10 - the OPPOSITE end of the depth range from the corridor: a plain that
    // runs to the horizon with everything far away, so `depth` spends the shot
    // BELOW saturation and the minification ratio is extreme. Slow dolly, so the
    // history is nearly perfect and the temporal weight should be free to take
    // it - the counterexample to the whip.
    {
        Shot s;
        s.name = "distant-plain";
        s.moveName = "slow dolly";
        s.move = Move::Dolly;
        Rng r;
        r.seed(mix32(cfg.seed, 0x510Au));
        pieceFloor(s.objs, m, m.noise, 400.0f, 400.0f, /*bilinear=*/true,
                   {0.86f, 0.86f, 0.84f});
        for (int i = 0; i < 7; ++i) {
            Object o;
            o.tex = m.assetOr((size_t)(i % 2), m.bands);
            o.bilinear = true;
            const float d = 30.0f + 22.0f * (float)i;
            const float sz = r.range(1.4f, 3.2f);
            addBox(o, {r.range(-0.35f, 0.35f) * d, sz * 0.5f, d}, {sz, sz * 0.5f, sz},
                   r.range(1.0f, 3.0f), {r.range(0.7f, 1.0f), r.range(0.7f, 1.0f), 0.9f});
            finishObject(o, m);
            s.objs.push_back(std::move(o));
        }
        s.eye0 = {0.0f, 2.2f, -6.0f};
        s.eye1 = {0.0f, 2.2f, 4.0f};
        s.pitch0 = s.pitch1 = -0.03f;
        shots.push_back(std::move(s));
    }

    // 11 - curved silhouettes at SEVERAL distances at once. `boxes-sphere` has
    // exactly one ball, always about the same size on screen; a fold that holds
    // it out has nothing else curved to learn from, which is visible in the fold
    // table as the shot with the largest margin (the net simply has not seen it).
    {
        Shot s;
        s.name = "sphere-field";
        s.moveName = "orbit-wide";
        s.move = Move::Orbit;
        Rng r;
        r.seed(mix32(cfg.seed, 0x510Bu));
        pieceFloor(s.objs, m, m.bands, 50.0f, 25.0f, /*bilinear=*/true, {0.72f, 0.74f, 0.78f});
        for (int i = 0; i < 6; ++i) {
            Object o;
            o.tex = i % 2 ? m.assetOr(3, m.bands) : -1;
            o.bilinear = true;
            const float rad = r.range(0.35f, 1.5f);
            addSphere(o, {r.range(-6.0f, 6.0f), rad, r.range(-5.0f, 7.0f)}, rad,
                      i % 2 ? 14 : 8, i % 2 ? 8 : 5,
                      {r.range(0.7f, 1.0f), r.range(0.7f, 1.0f), r.range(0.7f, 1.0f)});
            finishObject(o, m);
            s.objs.push_back(std::move(o));
        }
        s.centre = {0.0f, 1.0f, 1.0f};
        s.orbitR = 9.0f;
        s.orbitH = 3.4f;
        s.yaw0 = 2.1f;
        s.yaw1 = 3.3f;
        shots.push_back(std::move(s));
    }

    // 12 - cutouts UNDER MOTION. `foliage` is static, so the only thing that
    // moves in it is the jitter and the temporal weight can go to the ceiling for
    // free; a fold trained on that alone learns "leaves mean history", which is
    // wrong the moment the player walks. Same material, a dolly through it.
    {
        Shot s;
        s.name = "foliage-walk";
        s.moveName = "dolly-through";
        s.move = Move::Dolly;
        Rng r;
        r.seed(mix32(cfg.seed, 0x510Cu));
        pieceFloor(s.objs, m, m.noise, 50.0f, 25.0f, /*bilinear=*/true, {0.6f, 0.64f, 0.58f});
        pieceFoliage(s.objs, m, r, 16);
        pieceBoxes(s.objs, m, r, 3, 5.0f, 9.0f);
        s.eye0 = {-1.2f, 1.6f, -5.0f};
        s.eye1 = {1.2f, 1.6f, 4.0f};
        s.yaw0 = -0.08f;
        s.yaw1 = 0.10f;
        s.pitch0 = s.pitch1 = -0.06f;
        shots.push_back(std::move(s));
    }

    return shots;
}

// --------------------------------------------------- the project as a corpus ---

// blssscene's neutral form -> this file's Objects. The only thing that happens
// here is a change of representation: no geometry is merged, split, culled or
// re-lit, because every one of those would make the corpus describe a frame the
// console does not draw.
Object objectOf(const ProjectScene& ps, const SceneMesh& sm, Materials& mats,
                bool packageSplit) {
    Object o;
    o.vert.reserve(sm.vert.size());
    for (const SceneVert& sv : sm.vert) {
        Vertex v;
        v.p = {sv.p[0], sv.p[1], sv.p[2]};
        v.u = sv.u, v.v = sv.v;
        v.c = {sv.c[0], sv.c[1], sv.c[2]};
        o.vert.push_back(v);
    }
    o.idx = sm.idx;
    o.tex = sm.embeddedTex >= 0 && sm.embeddedTex < (int)ps.embedded.size()
                ? embeddedTexture(mats, ps, sm.embeddedTex, sm.cutout)
                : projectTexture(mats, sm.texture, sm.cutout);
    o.bilinear = sm.bilinear;
    o.cutout = sm.cutout;
    o.proxy = sm.proxy;
    o.viewDist = sm.viewDist;
    finishObject(o, mats, packageSplit);
    // finishObject recomputes the centre from the mesh; the scene walker's
    // is the same box, so this is belt and braces rather than a fix.
    o.centre = {sm.centre[0], sm.centre[1], sm.centre[2]};
    return o;
}

std::vector<Object> objectsOf(const ProjectScene& ps, Materials& mats,
                              bool packageSplit) {
    std::vector<Object> objs;
    objs.reserve(ps.mesh.size());
    for (const SceneMesh& sm : ps.mesh)
        objs.push_back(objectOf(ps, sm, mats, packageSplit));
    return objs;
}

// THE ANIMATED HALF, one finished Object per (part, console frame). Building
// every pose up front rather than skinning inside the frame loop is what keeps
// the loop a pure function of its frame index: a worker only ever INDEXES this
// table, never writes to it, so `--threads 1` and `--threads 32` still produce
// the same corpus. It costs a few MB per animated part and buys the whole
// determinism contract back for free.
//
// The package split runs per pose, because the boxes the console cuts are cut
// over the SKINNED vertices - `StaPipCore` recomputes a bag's package bboxes
// whenever `bboxVersion` changes, which `updateAndRenderAnimObjects` bumps on
// every re-skin. A single set of boxes taken from the bind pose would be the
// whole-bag proxy problem again, one level down.
std::vector<std::vector<Object>> animObjectsOf(const ProjectScene& ps, Materials& mats,
                                               bool packageSplit) {
    std::vector<std::vector<Object>> out;
    out.reserve(ps.anim.size());
    for (const AnimMesh& am : ps.anim) {
        std::vector<Object> poses;
        poses.reserve(am.pose.size());
        for (const SceneMesh& sm : am.pose) poses.push_back(objectOf(ps, sm, mats, packageSplit));
        out.push_back(std::move(poses));
    }
    return out;
}

// The project's scenes, as the corpus' shot table. `geometry` is filled first
// and never resized afterwards - every shot of a scene POINTS at one geometry
// set, because copying a real scene once per camera move is minutes of wall
// clock for nothing.
// APPENDS this project's shots, so a union corpus is a concatenation and
// nothing else. `geometry` and `anim` are the caller's owning stores and they
// grow across members - a Shot holds POINTERS into them, so they must never be
// cleared or reallocated-with-move once a shot points at them, which is why
// they are reserved by the caller and only ever pushed to here.
void buildProjectShots(const CorpusConfig& cfg, Materials& mats,
                       std::vector<std::unique_ptr<std::vector<Object>>>& geometry,
                       std::vector<std::unique_ptr<std::vector<std::vector<Object>>>>& anim,
                       const std::vector<ProjectScene>& scenes, int group,
                       const std::string& groupName, std::vector<Shot>& shots) {
    const size_t base = geometry.size();
    for (const ProjectScene& ps : scenes) {
        geometry.push_back(
            std::make_unique<std::vector<Object>>(objectsOf(ps, mats, cfg.packageSplit)));
        anim.push_back(std::make_unique<std::vector<std::vector<Object>>>(
            animObjectsOf(ps, mats, cfg.packageSplit)));
    }

    for (size_t i = 0; i < scenes.size(); ++i) {
        const ProjectScene& ps = scenes[i];
        for (const SceneShot& ss : ps.shot) {
            if (ss.keys() < 1) continue;
            Shot s;
            s.name = ss.name;
            s.moveName = ss.move;
            s.move = Move::Path;
            s.shared = geometry[base + i].get();
            s.animShared = anim[base + i].get();
            s.pathEye = ss.eye;
            s.pathLook = ss.look;
            s.ease = ss.ease;
            s.fovDeg = ss.fovDeg > 5.0f ? ss.fovDeg : 60.0f;
            s.group = group;
            s.frames = ss.frames;
            shots.push_back(std::move(s));
        }
    }
    (void)groupName;
}

}  // namespace

// -------------------------------------------------------------------- entry ---

namespace {

// ONE FRAME'S WORTH OF RASTER SCRATCH, owned by a worker rather than by a frame.
// The supersampled target and its z-buffer are 15 MB each at the shipped 4x, so
// reallocating them per frame was already worth avoiding when this loop was
// serial; per WORKER is the same trick with the thread count in front of it.
struct RenderScratch {
    Image big;
    std::vector<float> zb;
};

// Split [0, n) across `threads` workers and run `body(i, scratch)`. Worker w
// takes i = w, w + workers, w + 2*workers, ... - a fixed partition, so which
// worker computes item i is a function of the core count and NOTHING ELSE
// touches item i. Same rule as blss.cpp's parallelFor and matbake's sampler,
// for the same reason: this loop produces the training corpus, every published
// number on this feature came off a seeded run of it, and a corpus that came
// out different on a bigger machine would have quietly invalidated all of them.
template <class F>
void parallelFrames(int n, int threads, const F& body) {
    if (n <= 0) return;
    int workers = threads;
    if (workers <= 0) {
        workers = (int)std::thread::hardware_concurrency();
        if (workers < 1) workers = 1;
    }
    // Each worker holds ~30 MB of raster scratch at 4x supersample, on top of a
    // corpus that is already ~2.3 MB per frame, so the cap applies to an
    // explicit `--threads 64` as much as to a 64-core machine. Same ceiling as
    // blss.cpp's parallelFor.
    if (workers > 32) workers = 32;
    if (workers > n) workers = n;
    if (workers <= 1) {
        RenderScratch sc;
        for (int i = 0; i < n; ++i) body(i, sc);
        return;
    }
    const auto run = [&](int w) {
        RenderScratch sc;
        for (int i = w; i < n; i += workers) body(i, sc);
    };
    std::vector<std::thread> pool;
    pool.reserve((size_t)workers - 1);
    for (int w = 1; w < workers; ++w) pool.emplace_back(run, w);
    run(0);
    for (std::thread& t : pool) t.join();
}

}  // namespace

// DETERMINISM SURVIVES THE THREAD COUNT, and it is the frame loop below that
// has to earn it. The serial version carried `prevLow` from one iteration to
// the next, which is a dependency on the ORDER frames were rendered in; the
// parallel one re-renders the predecessor's low-res target from its own camera
// and its own jitter phase instead. That is the same image by construction -
// renderScene() is a pure function of (geometry, materials, camera, size,
// raster offset) and clears both its colour target and its z-buffer - and it
// costs 1.5% more work, because a low-res render is 1/64 of the supersampled
// truth that dominates the frame. What it buys is that frame i is computed from
// i alone, so `--threads 1` and `--threads 32` produce byte-identical corpora,
// byte-identical labels, and a byte-identical blss.net.
std::vector<CorpusFrame> generate(const CorpusConfig& cfg) {
    std::vector<CorpusFrame> out;
    const int outW = cfg.outW, outH = cfg.outH;
    // The tile grid. FLOOR, where the engine's configure() takes a CEILING
    // ((outW + kTile - 1) / kTile) - identical at every size that divides, and
    // the two shipping ones do (512/32 = 16, 448/32 = 14; 512/64 = 8,
    // 448/64 = 7). At a size that does NOT divide, the corpus would build a
    // smaller grid than the console and the last partial tile column would be
    // described by nothing, so say so rather than silently measure a different
    // frame - see docs/blss-reconstruction.md, "Symbols".
    const int tile = tileSize();
    const int cols = outW / tile, rows = outH / tile;
    if (cfg.verbose && (outW % tile || outH % tile))
        std::printf(
            "[blss] WARNING: tile %d does not divide %dx%d - the corpus grid is "
            "%dx%d, the engine's would be %dx%d. This configuration is NOT a "
            "twin.\n",
            tile, outW, outH, cols, rows, (outW + tile - 1) / tile,
            (outH + tile - 1) / tile);
    if (cfg.frames <= 0 || cols <= 0 || rows <= 0) return out;
    const int ss = std::max(cfg.supersample, 1);
    const int sx = scaleX(cfg.scale), sy = scaleY(cfg.scale);
    const int lowW = std::max(outW / sx, 1), lowH = std::max(outH / sy, 1);
    // The raster scale has to DIVIDE the output or the two twins are describing
    // different frames: the engine allocates lowW = outW / scaleX and composites
    // from it with `(px << 4) / scaleX`, so a remainder is a column of output
    // pixels sampled off the end of the low-res target. Every scale worth
    // sweeping divides both shipped output sizes (512 and 448 take 1, 2 and 4;
    // Pal576i's 512x512 takes 1, 2, 4 and 8), so this fires only on a typo.
    if (cfg.verbose && (outW % sx || outH % sy))
        std::printf(
            "[blss] WARNING: scale %s does not divide %dx%d - the low-res target is "
            "%dx%d and %d x %d output pixel(s) sample past its edge. This "
            "configuration is NOT a twin.\n",
            scaleName(cfg.scale).c_str(), outW, outH, lowW, lowH, outW % sx, outH % sy);

    const auto t0 = std::chrono::steady_clock::now();
    Materials mats = buildMaterials(cfg);

    // THE SCENE SOURCE, and it is the only fork in this file. Everything below
    // - the jitter, the supersampled truth, the bag list, the features - runs
    // on whichever list of shots comes out of here, which is what makes a
    // project-trained net and a procedurally-trained one comparable.
    // Stable addresses, because a Shot points into these and they grow once per
    // member of a union corpus - see buildProjectShots().
    std::vector<std::unique_ptr<std::vector<Object>>> projectGeometry;  // outlives `shots`
    std::vector<std::unique_ptr<std::vector<std::vector<Object>>>> projectAnim;  // ... so does this
    std::vector<Shot> shots;
    ProjectBlss pb;
    // The corpus SOURCES, in order. One entry is the ordinary
    // `--blss-train <projectDir>`; several is the union corpus.
    std::vector<std::string> sources = cfg.projectDirs;
    if (sources.empty() && !cfg.projectDir.empty()) sources.push_back(cfg.projectDir);
    std::vector<std::string> groupNames;   // one per LOADED member
    std::vector<int> groupShots;           // ...and how many shots it gave
    bool jitterSeen = false, jitterConflict = false;
    for (const std::string& dir : sources) {
        // THE BESTIARY AS A UNION MEMBER, spelled `bestiary` where a directory
        // would go. It is not a project and there is nothing on disk to point
        // at, but the question "is the built-in corpus worth adding to a
        // cross-project training set" is exactly a leave-one-project-out
        // question with the bestiary as one more group - and answering it any
        // other way means two runs whose training sets differ in more than the
        // one thing being tested.
        if (dir == "bestiary" || dir == "BESTIARY") {
            const size_t before = shots.size();
            std::vector<Shot> b = buildShots(cfg, mats);
            if (!cfg.packageSplit)
                for (Shot& s : b)
                    for (Object& o : s.objs) {
                        o.part.clear();
                        o.part.emplace_back(o.lo, o.hi);
                    }
            for (Shot& s : b) {
                s.group = static_cast<int>(groupNames.size());
                shots.push_back(std::move(s));
            }
            groupNames.push_back("bestiary");
            groupShots.push_back(static_cast<int>(shots.size() - before));
            continue;
        }
        std::string err;
        ProjectBlss mine;
        const std::vector<ProjectScene> scenes =
            loadProject(dir, &err, cfg.verbose, cfg.animated, &mine, cfg.shotPlan);
        if (!err.empty()) std::printf("[blss] project: %s\n", err.c_str());
        if (scenes.empty()) {
            if (sources.size() > 1) {
                // A union corpus NEVER falls back: a member that quietly became
                // the bestiary would put procedural content into a table whose
                // whole claim is which project the frames came from.
                std::printf("[blss] union member '%s' has no drawable scene - DROPPED\n",
                            dir.c_str());
                continue;
            }
            // The documented fallback: a project that will not load, or that
            // has nothing to draw, still produces a corpus - just not its own.
            std::printf(
                "[blss] project '%s' has no drawable scene - falling back to the "
                "procedural bestiary\n",
                dir.c_str());
            continue;
        }
        // THE SAMPLER HAS TO BE ONE SAMPLER. A union corpus renders every member
        // through one blss::setJitter(), so members that disagree would be two
        // incomparable distributions in one table. First member wins and the
        // disagreement is said out loud.
        if (jitterSeen && mine.jitter != pb.jitter) jitterConflict = true;
        if (!jitterSeen) pb = mine;
        jitterSeen = true;
        // THE RENDERER DRAWS NO EMITTERS, AND A PROJECT THAT HAS THEM MUST BE
        // TOLD SO HERE, where its ground truth is about to be manufactured.
        //
        // The emitters exist in this file only for the COVERAGE COUNTER
        // (billboardOf / emitterCentres / countEmitter, the --blss-coverage
        // path); renderScene() takes geometry and has no blending at all. So on
        // a project whose overdraw IS its particles the truth images are bare
        // sky and flat ground, and every PSNR in the table describes a frame the
        // game never displays. That is not a rounding error: on
        // examples/upscaler-lab - 3 072 alpha-blended billboards, measured at
        // 1.63x on a real PS2 - `--blss-eval` reads +0.00 dB of headroom and
        // prints "THIS SCENE WILL NOT BENEFIT. Leave the upscaler off", while
        // `--blss-coverage` on the same directory calls it a win. The two verbs
        // disagree about what is in the scene, and the quality one is the one
        // that is wrong.
        //
        // Drawing them is a real piece of work and is filed in docs/backlog.md
        // rather than half-landed (it needs a blending path in a rasteriser
        // whose stated invariant is that it has none, back-to-front sorting, a
        // particle-simulation twin, and a refit of every published net). Until
        // then the honest thing is that no number leaves this tool without the
        // caveat attached - a silently wrong verdict is worse than a missing
        // one, which is the rule this whole feature was rebuilt around.
        int liveEmitters = 0;
        for (const ProjectScene& s : scenes)
            for (const SceneEmitter& e : s.emitter)
                if (e.enabled) ++liveEmitters;
        if (liveEmitters > 0)
            std::printf(
                "[blss] WARNING: '%s' has %d enabled emitter(s) and the corpus renderer draws "
                "NONE of them.\n"
                "[blss]          Its ground truth is the scene without its particles, so every "
                "PSNR and every\n"
                "[blss]          verdict below describes a frame the game does not display. "
                "Use --blss-coverage\n"
                "[blss]          for a project whose overdraw is its emitters "
                "(docs/backlog.md).\n",
                dir.c_str(), liveEmitters);
        const size_t before = shots.size();
        buildProjectShots(cfg, mats, projectGeometry, projectAnim, scenes,
                          static_cast<int>(groupNames.size()), dir, shots);
        groupNames.push_back(dir);
        groupShots.push_back(static_cast<int>(shots.size() - before));
    }
    if (shots.empty()) {
        shots = buildShots(cfg, mats);
        // `--no-package-split`: one proxy per object, which is what the
        // bestiary's hand-chunked floors and walls were built around and what
        // every fold table published before the split existed was measured on.
        if (!cfg.packageSplit)
            for (Shot& s : shots)
                for (Object& o : s.objs) {
                    o.part.clear();
                    o.part.emplace_back(o.lo, o.hi);
                }
    }

    // THE SAMPLER THIS CORPUS WILL BE FITTED AGAINST, resolved here and never
    // again: before the first frame is rendered, after the project (if any) has
    // said how it will be built. `--no-jitter` wins over the project, the
    // project wins over the default, and the bestiary - which has no project to
    // ask - keeps jitter on, because that is the configuration every fold table
    // on docs/neural-upscaler.md was measured with.
    //
    // It has to be said out loud for the same reason `--tile` does: two runs of
    // this tool that differ only in the sampler produce two incomparable tables,
    // and nothing in blss.net records which one it was.
    const bool wantJitter =
        cfg.jitter >= 0 ? cfg.jitter != 0 : (pb.found ? pb.jitter : true);
    setJitter(wantJitter);
    if (jitterConflict && cfg.jitter < 0)
        std::printf(
            "[blss] WARNING: the union members do NOT agree about blssJitter and this run "
            "renders all of them at '%s' (the first member's). Pass --jitter/--no-jitter to "
            "say which sampler you meant - a table mixing the two is two experiments.\n",
            pb.jitter ? "on" : "off");
    if (cfg.verbose && cfg.still)
        std::printf(
            "[blss] STILL FIXTURE (--still): every frame of a shot is the shot's FIRST "
            "camera and FIRST pose, so only the jitter phase advances. This is the host "
            "twin of the console's frozen-camera experiment and it is a METRIC fixture, "
            "not a corpus - do not train on it, and read only the period-2 table.\n");
    if (cfg.verbose && cfg.proxyBudget)
        std::printf(
            "[blss] PROXY BUDGET ON (--proxy-budget): a bag gets at most as many proxies "
            "as it covers tiles, capped at %d and merged consecutively. THE ENGINE'S "
            "TYRA_BLSS_PROXY_BUDGET IS 0 - this run describes the frame more coarsely "
            "than the console does, which is a MEASUREMENT and not a twin.\n",
            kMaxProxiesPerBag);
    // THE SAMPLER IS ANNOUNCED IN BOTH DIRECTIONS, and the reason is the eighth
    // entry of "measured is not optimised" (docs/neural-upscaler.md): this page
    // carried a row whose margins were taken jitter-off and whose ceiling was
    // taken jitter-on, and nothing in either run said which. `--tile` and
    // `--scale` have had an announcement line since they became sweepable;
    // blssJitter became the third such knob and did not get one, so a jitter-ON
    // run printed nothing at all.
    if (cfg.verbose && !wantJitter)
        std::printf(
            "[blss] sub-pixel jitter OFF%s - both phases sample the same "
            "position, so the temporal pass sees a genuine still frame and the "
            "quincunx supersample is gone.\n",
            cfg.jitter >= 0 ? " (--no-jitter)" : " (the project's blssJitter)");
    else if (cfg.verbose)
        std::printf(
            "[blss] sub-pixel jitter ON%s - the two phases are a quincunx pair, so the "
            "temporal pass has a genuine second sample to fuse. A table taken here is NOT "
            "comparable with one taken at --no-jitter.\n",
            cfg.jitter >= 0 ? " (--jitter)"
                            : (pb.found ? " (the project's blssJitter)"
                                        : " (the bestiary has no project to ask)"));

    // Frames spread round-robin over the shots, remainder to the first ones. A
    // shot that got a single frame would have no history at all and teach the
    // temporal channel nothing, so with fewer frames than shots the tail shots
    // are simply not rendered rather than degenerating.
    //
    // EXPLICIT COUNTS FIRST (Shot::frames, from the project's training-shot
    // plan), then the rest share what is left. Every shot asking for 0 is the
    // old behaviour exactly, which is what a default plan produces - so this
    // does not move a single published number.
    const int shotCount = (int)shots.size();
    std::vector<int> perShot((size_t)shotCount, 0);
    int asked = 0, freeShots = 0;
    for (const Shot& s : shots) {
        if (s.frames > 0) asked += s.frames;
        else ++freeShots;
    }
    if (asked > cfg.frames) {
        // NEVER DROP A SHOT SILENTLY. The window prints the per-shot counts it
        // expects, so a clamp nobody announced makes its preview a lie - and a
        // shot that got zero frames is a fold that does not exist.
        std::printf(
            "blss: the shot plan asks for %d explicit frame(s) and --frames is %d - scaling the "
            "explicit counts down to fit. Raise --frames to %d to get the plan as authored.\n",
            asked, cfg.frames, asked + freeShots);
        int given = 0, k = 0;
        for (int i = 0; i < shotCount; ++i)
            if (shots[(size_t)i].frames > 0) {
                // Proportional, floor, at least one frame each - a shot the
                // author named explicitly must still be shot.
                int n = (int)((long long)shots[(size_t)i].frames * cfg.frames / asked);
                if (n < 1) n = 1;
                perShot[(size_t)i] = n;
                given += n;
                ++k;
            }
        (void)k;
        // Whatever the floor left over goes to the shots that asked for nothing,
        // and if there is nothing left they get nothing - which the line above
        // has already said out loud.
        int rest = cfg.frames - given;
        for (int i = 0; i < shotCount && rest > 0; ++i)
            if (shots[(size_t)i].frames <= 0) {
                perShot[(size_t)i] = 1;
                --rest;
            }
    } else if (cfg.frames < shotCount) {
        for (int i = 0; i < cfg.frames; ++i) perShot[(size_t)i] = 1;
    } else {
        const int spare = cfg.frames - asked;
        const int base = freeShots > 0 ? spare / freeShots : 0;
        int rem = freeShots > 0 ? spare % freeShots : 0;
        for (int i = 0; i < shotCount; ++i) {
            if (shots[(size_t)i].frames > 0) {
                perShot[(size_t)i] = shots[(size_t)i].frames;
                continue;
            }
            perShot[(size_t)i] = base + (rem > 0 ? 1 : 0);
            if (rem > 0) --rem;
        }
    }
    // A shot with no frames is a fold that does not exist and a row the window
    // will not find. Say which ones and why, once.
    {
        int starved = 0;
        for (int i = 0; i < shotCount; ++i)
            if (perShot[(size_t)i] <= 0) ++starved;
        if (starved)
            std::printf(
                "blss: %d of %d shot(s) got NO frames at --frames %d - they are not in this "
                "corpus and there is no fold for them.\n",
                starved, shotCount, cfg.frames);
    }

    if (cfg.verbose) {
        size_t proxies = 0, animParts = 0;
        for (const Shot& s : shots)
            for (const Object& o : s.geometry())
                if (o.proxy) proxies += o.part.size();
        for (const auto& scene : projectAnim)
            for (const auto& poses : *scene)
                if (!poses.empty()) {
                    animParts += 1;
                    proxies += poses[0].part.size();
                }
        std::string source = "procedural bestiary";
        if (groupNames.size() == 1) source = groupNames[0];
        else if (groupNames.size() > 1)
            source = "UNION of " + std::to_string(groupNames.size()) + " project(s)";
        std::printf(
            "[blss] corpus: %s, %d frame(s) over %d shot(s), %dx%d out, %dx%d low, "
            "%dx supersample, %dx%d tiles, seed 0x%X\n",
            source.c_str(),
            cfg.frames, shotCount, outW, outH, lowW, lowH, ss, cols, rows, cfg.seed);
        // WHICH MEMBER CONTRIBUTED WHAT, because frames are spread evenly over
        // SHOTS: a member with twice the scenes gets twice the frames, and a
        // union mean nobody can decompose is a mean about its largest member.
        if (groupNames.size() > 1)
            for (size_t g = 0; g < groupNames.size(); ++g)
                std::printf("[blss]   group %zu  %-40s %2d shot(s)\n", g, groupNames[g].c_str(),
                            groupShots[g]);
        // How finely the frame can be described AT ALL - the one number the
        // console's own instrument prints next to its feature spread, and the
        // one that was 2 on the day this feature was found running on a
        // distribution nothing had measured.
        // With the budget on this is the UNCAPPED count - the cap is per frame
        // and per camera, so there is no one number for it here. The per-frame
        // count is what --blss-eval --features reports.
        std::printf("[blss] %zu bag proxy(ies)%s over %s, %s\n", proxies,
                    cfg.proxyBudget ? " before the budget" : "",
                    projectGeometry.empty() ? "the bestiary"
                                            : "the project scenes",
                    cfg.packageSplit ? "one per VU1 package (the engine's split)"
                                     : "one per object (--no-package-split)");
        // Said out loud because for its whole life this corpus rendered NEITHER
        // the pixels nor the proxies of an animated model, while the console
        // rendered both - so "0 animated part(s)" on a project that has them is
        // the signature of that bug coming back.
        if (!projectAnim.empty())
            std::printf(
                "[blss] %zu animated part(s), posed per console frame (their proxies "
                "are in the count above)\n",
                animParts);
    }

    // THE WORK LIST, in the order the serial loop produced it: shot 0's frames,
    // then shot 1's, ... Building it up front is what turns "a loop with a
    // carried variable" into "n independent items", and keeping the order means
    // a corpus index still means what every held-out split and every fold table
    // already assumed it means.
    struct FrameJob {
        int shot = 0;   // which shot
        int i = 0;      // frame index within it
        int n = 0;      // frames in that shot
    };
    std::vector<FrameJob> jobs;
    jobs.reserve((size_t)cfg.frames);
    for (int si = 0; si < shotCount; ++si)
        for (int i = 0; i < perShot[(size_t)si]; ++i) jobs.push_back({si, i, perShot[(size_t)si]});

    out.resize(jobs.size());
    std::vector<double> shotMs((size_t)shotCount, 0.0);
    std::mutex report;
    int done = 0, reported = -1;
    // HOW MANY PROXIES A FRAME ACTUALLY CARRIES, which the build-time count
    // above cannot say once the budget is on (the cap is per camera). It is the
    // number the console's own BLSSGRID line reports, so the two are directly
    // comparable - which is the whole point of having it.
    size_t bagTotal = 0;

    parallelFrames((int)jobs.size(), cfg.threads, [&](int j, RenderScratch& sc) {
        const FrameJob& job = jobs[(size_t)j];
        const Shot& shot = shots[(size_t)job.shot];
        const std::vector<Object>& geo = shot.geometry();
        const int i = job.i, n = job.n;
        const auto ts = std::chrono::steady_clock::now();

        // `--still` freezes the shot at its first camera: t and tPrev are both
        // 0, so `prev` below is `cur` by construction, every reprojection offset
        // is zero and the only thing that still advances between consecutive
        // frames of this shot is the jitter phase. See CorpusConfig::still.
        const float t = cfg.still ? 0.0f : (n > 1 ? (float)i / (float)(n - 1) : 0.0f);
        const float tPrev = cfg.still ? 0.0f : (n > 1 ? (float)(i - 1) / (float)(n - 1) : 0.0f);
        const Pinhole cur = cameraAt(shot, t);
        // Frame 0 has no predecessor: prev == cur makes every reprojection
        // offset zero and prevLow its own render, which is exactly what the
        // console holds on a scene cut.
        const Pinhole prev = i > 0 ? cameraAt(shot, tPrev) : cur;

        // THE FRAME'S POSE, and its predecessor's. Frame index IS console frame
        // index (blssscene.hpp, kAnimFps), so this is a lookup and not an
        // evaluation - which is what keeps the loop a pure function of j. Frame
        // 0 reuses its own pose for the predecessor, the same way it reuses its
        // own camera: on a scene cut the console's history is this frame.
        std::vector<const Object*> animNow, animPrev;
        if (shot.animShared) {
            animNow.reserve(shot.animShared->size());
            animPrev.reserve(shot.animShared->size());
            for (const std::vector<Object>& poses : *shot.animShared) {
                if (poses.empty()) continue;
                const int last = (int)poses.size() - 1;
                // ...and it freezes the ANIMATION too, which is the half that
                // makes the period-2 metric readable: a camera-derived warp
                // cannot compensate a deforming mesh, so a moving model is a
                // difference the metric charges to the artefact.
                const int now = cfg.still ? 0 : std::min(i, last);
                const int was = cfg.still ? 0 : std::min(i > 0 ? i - 1 : 0, last);
                animNow.push_back(&poses[(size_t)now]);
                animPrev.push_back(&poses[(size_t)was]);
            }
        }

        CorpusFrame& cf = out[(size_t)j];
        cf.shot = job.shot;
        cf.shotName = shot.name;
        cf.moveName = shot.moveName;
        cf.group = shot.group;
        cf.groupName = shot.group < (int)groupNames.size()
                           ? groupNames[(size_t)shot.group]
                           : std::string();
        Frame& fr = cf.frame;
        fr.cols = cols, fr.rows = rows;
        fr.outW = outW, fr.outH = outH;
        fr.scale = cfg.scale;
        fr.phase = i % kJitterPhases;

        // Ground truth: rendered at ss x the display size and box-resolved.
        // Point samples on a regular grid, so it is not a perfect band-limit of
        // a checkerboard at the horizon - but it is the same definition the
        // oracle and psnr() are measured against, and 16 samples per output
        // pixel is far past what the console will ever see.
        renderScene(geo, mats, cur, outW * ss, outH * ss, 0.0f, 0.0f, sc.big, sc.zb, &animNow);
        cf.truth = ss > 1 ? boxDown(sc.big, ss) : sc.big;
        // The "no BLSS" baseline: full resolution, one sample, no jitter.
        renderScene(geo, mats, cur, outW, outH, 0.0f, 0.0f, cf.native, sc.zb, &animNow);
        // What the console actually has: the jittered low-res target. The
        // jitter is in LOW-RES pixels and this render's pixels ARE low-res
        // pixels, so it goes straight in as the raster offset.
        renderScene(geo, mats, cur, lowW, lowH, jitterX(fr.phase), jitterY(fr.phase),
                    fr.low, sc.zb, &animNow);
        // ...and the predecessor's, RE-RENDERED rather than carried over from
        // the previous iteration - that carry is the only thing that made this
        // loop serial. Same camera, same jitter phase, same pure function, so
        // the same image: see the determinism note above.
        if (i > 0)
            renderScene(geo, mats, prev, lowW, lowH, jitterX((i - 1) % kJitterPhases),
                        jitterY((i - 1) % kJitterPhases), fr.prevLow, sc.zb, &animPrev);
        else
            fr.prevLow = fr.low;

        // ...and what the EE knows ABOUT it: one bag per drawn object, from
        // the unjittered display-resolution projection.
        const std::vector<BagProxy> bags =
            bagList(geo, mats, cur, outW, outH, &animNow, cfg.proxyBudget);
        const std::vector<TileStats> stats = accumulate(cols, rows, outW, outH, bags);
        // History size is the DISPLAY size: the runtime's history is the other
        // display framebuffer (docs/blss-reconstruction.md section 6), so one
        // history texel is one output pixel.
        fr.reproj = buildReproj(cols, rows, outW, outH, outW, outH, cur, prev, stats);
        // No per-tile state crosses this line any more. It used to: the
        // recurrent histAge counter had to be aged AFTER the features were
        // built, because the console's network sees the counter as it stood at
        // the end of the PREVIOUS frame, and ageing first would have handed the
        // corpus a one-frame lookahead the hardware does not have. The channel
        // was measured and removed (blss.hpp, kFeatures), and that whole
        // ordering hazard went with it - buildFeatures is now a pure function of
        // this frame. It is also what lets this loop be a parallelFrames at all.
        fr.features = buildFeatures(cols, rows, stats, fr.reproj);

        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts)
                .count();
        // The only shared state, and it is bookkeeping: a CPU-time total per
        // shot and a progress line. Neither reaches a pixel, a feature or a
        // label, so neither can move the corpus.
        std::lock_guard<std::mutex> lock(report);
        shotMs[(size_t)job.shot] += ms;
        bagTotal += bags.size();
        ++done;
        if (!cfg.verbose) return;
        const int pct = done * 20 / (int)jobs.size();  // a line every 5%
        if (pct == reported) return;
        reported = pct;
        std::printf("[blss]   rendered %d of %zu frame(s)\n", done, jobs.size());
        std::fflush(stdout);
    });

    if (cfg.verbose) {
        // Per shot, the summed CPU time of its frames - NOT wall clock, which
        // stopped being a per-shot quantity the moment the shots overlapped.
        for (int si = 0; si < shotCount; ++si) {
            const int n = perShot[(size_t)si];
            if (n <= 0) continue;
            std::printf("[blss]   shot %d %-18s %-13s %2d frame(s)  %7.0f ms cpu (%.0f ms/frame)\n",
                        si, shots[(size_t)si].name.c_str(), shots[(size_t)si].moveName.c_str(), n,
                        shotMs[(size_t)si], shotMs[(size_t)si] / (double)n);
        }
        const double s = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
        std::printf("[blss] corpus ready: %zu frame(s), %zu tile sample(s), %.1f proxies/frame,"
                    " %.1f s\n",
                    out.size(), out.size() * (size_t)(cols * rows),
                    out.empty() ? 0.0 : (double)bagTotal / (double)out.size(), s);
    }
    return out;
}

// ============================================================== coverage =====
//
// "Will this scene get FASTER" is one number - how many times over the scene
// paints the screen - and the contract is in blsscorpus.hpp. What follows is
// the counter, the emitter model, and the walk that drives them.

namespace {

// A counted draw: world positions, triangles, and the console's own submission
// test. No materials, no UVs, no colours - none of them change a fragment
// COUNT, and skipping them means the estimator never decodes a PNG, which is
// most of what makes it seconds rather than minutes.
struct CovMesh {
    std::vector<V3> p;
    std::vector<int> idx;
    float viewDist = 0.0f;
    V3 centre{};
};

CovMesh covOf(const SceneMesh& sm) {
    CovMesh m;
    m.p.reserve(sm.vert.size());
    for (const SceneVert& v : sm.vert) m.p.push_back({v.p[0], v.p[1], v.p[2]});
    m.idx = sm.idx;
    m.viewDist = sm.viewDist;
    m.centre = {sm.centre[0], sm.centre[1], sm.centre[2]};
    return m;
}

// FRAGMENTS OF ONE PROJECTED TRIANGLE, clamped to the target - the same exact
// span solve renderScene does, with the inner pixel loop deleted. A count is a
// sum of span LENGTHS, so a sprite covering the whole screen costs one division
// per scanline instead of 57 344 depth compares; that is the single reason a
// 3 000-billboard haze bank can be counted inside a window.
uint64_t countTri(float ax, float ay, float bx, float by, float cx2, float cy2, int rw,
                  int rh) {
    float x[3] = {ax, bx, cx2}, y[3] = {ay, by, cy2};
    float det = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (det < 0.0f) {  // double-sided, exactly like the rasteriser above
        std::swap(x[1], x[2]);
        std::swap(y[1], y[2]);
        det = -det;
    }
    if (det < 1e-7f) return 0;

    int x0 = (int)std::floor(std::min(x[0], std::min(x[1], x[2])) - 0.5f);
    int x1 = (int)std::ceil(std::max(x[0], std::max(x[1], x[2])) + 0.5f);
    int y0 = (int)std::floor(std::min(y[0], std::min(y[1], y[2])) - 0.5f);
    int y1 = (int)std::ceil(std::max(y[0], std::max(y[1], y[2])) + 0.5f);
    x0 = std::max(x0, 0), y0 = std::max(y0, 0);
    x1 = std::min(x1, rw - 1), y1 = std::min(y1, rh - 1);
    if (x0 > x1 || y0 > y1) return 0;

    const float ea[3] = {-(y[1] - y[0]), -(y[2] - y[1]), -(y[0] - y[2])};
    const float eb[3] = {x[1] - x[0], x[2] - x[1], x[0] - x[2]};
    const float ec[3] = {(y[1] - y[0]) * x[0] - (x[1] - x[0]) * y[0],
                         (y[2] - y[1]) * x[1] - (x[2] - x[1]) * y[1],
                         (y[0] - y[2]) * x[2] - (x[0] - x[2]) * y[2]};
    uint64_t n = 0;
    for (int py = y0; py <= y1; ++py) {
        const float yc = (float)py + 0.5f;
        float lo = (float)x0 + 0.5f, hi = (float)x1 + 0.5f;
        bool empty = false;
        for (int e = 0; e < 3; ++e) {
            const float k = eb[e] * yc + ec[e];
            if (ea[e] > 1e-12f)
                lo = std::max(lo, -k / ea[e]);
            else if (ea[e] < -1e-12f)
                hi = std::min(hi, -k / ea[e]);
            else if (k < 0.0f) {
                empty = true;
                break;
            }
        }
        if (empty) continue;
        int xs = (int)std::ceil(lo - 0.5f);
        int xe = (int)std::floor(hi - 0.5f);
        xs = std::max(xs, x0), xe = std::min(xe, x1);
        if (xs <= xe) n += (uint64_t)(xe - xs + 1);
    }
    return n;
}

// A triangle soup under one camera. `viewDist` is the console's own submission
// test, applied here for the same reason bagList applies it: a bag the game
// never submits costs no fill.
uint64_t countMesh(const CovMesh& m, const Pinhole& cam, int rw, int rh) {
    if (m.viewDist > 0.0f) {
        const V3 d{m.centre.x - cam.pos[0], m.centre.y - cam.pos[1], m.centre.z - cam.pos[2]};
        if (dot(d, d) > m.viewDist * m.viewDist) return 0;
    }
    const float sxScale = 0.5f * (float)rw / cam.tanHalfFovX;
    const float syScale = 0.5f * (float)rh / cam.tanHalfFovY;
    const float cx = 0.5f * (float)rw, cy = 0.5f * (float)rh;
    uint64_t n = 0;
    for (size_t f = 0; f + 2 < m.idx.size(); f += 3) {
        CVert in[3];
        for (int k = 0; k < 3; ++k) {
            const V3& src = m.p[(size_t)m.idx[f + k]];
            const V3 rel{src.x - cam.pos[0], src.y - cam.pos[1], src.z - cam.pos[2]};
            in[k].vx = rel.x * cam.right[0] + rel.y * cam.right[1] + rel.z * cam.right[2];
            in[k].vy = rel.x * cam.up[0] + rel.y * cam.up[1] + rel.z * cam.up[2];
            in[k].w = rel.x * cam.fwd[0] + rel.y * cam.fwd[1] + rel.z * cam.fwd[2];
        }
        CVert poly[4];
        const int pn = clipNear(in, poly);
        if (pn == 0) continue;
        float sx[4], sy[4];
        for (int k = 0; k < pn; ++k) {
            const float iw = 1.0f / poly[k].w;
            sx[k] = poly[k].vx * iw * sxScale + cx;
            sy[k] = -poly[k].vy * iw * syScale + cy;
        }
        for (int fan = 0; fan + 2 < pn; ++fan)
            n += countTri(sx[0], sy[0], sx[fan + 1], sy[fan + 1], sx[fan + 2], sy[fan + 2], rw,
                          rh);
    }
    return n;
}

// --- the emitter half, which is MODELLED and says so ------------------------
//
// A billboard's half extents are `m00` and `m11` in updateParticles(), and both
// are the emitter's `size` scaled by a per-kind curve of the particle's life
// fraction. Averaged over a uniformly distributed life, those curves are the
// constants below - the same arithmetic templates.cpp runs per particle per
// frame, integrated once. Getting the fog one wrong would be a factor of three.
struct CovBillboard {
    float halfW = 0.5f, halfH = 0.5f;
};
CovBillboard billboardOf(const SceneEmitter& e) {
    CovBillboard b;
    float mul = 1.0f;
    switch (e.kind) {
        case 0: mul = 0.9f; break;   // fire:   size * (0.5 + 0.8 t)
        case 1: mul = 1.1f; break;   // smoke:  size * (1.6 - t)
        case 2: mul = 3.0f; break;   // fog:    size * 3
        case 3: mul = 0.35f; break;  // sparks: size * 0.35
        case 4:                      // rain: a thin streak, width != height
            b.halfW = e.size * 0.06f;
            b.halfH = e.size * 0.5f;
            return b;
        default:                     // custom: 1 -> Grow over the life
            mul = 1.0f + (e.grow - 1.0f) * 0.5f;
            break;
    }
    b.halfW = b.halfH = e.size * mul;
    return b;
}

// How far a particle gets from where it spawned, per kind, over its life. The
// runtime spawns on the emitter's own XZ rectangle and integrates a velocity;
// this spreads the pool through a box instead. It is the coarsest thing in the
// estimate and it is second order - the distance to the CAMERA and the
// billboard's own size are what set the pixels - but a fire whose flames all
// sat at the emitter's y would read as one hot spot rather than a column.
float emitterRise(const SceneEmitter& e) {
    switch (e.kind) {
        case 0: return 1.4f;   // fire:   ~1.8 u/s over ~0.8 s
        case 1: return 2.0f;   // smoke:  ~0.75 u/s over ~2.75 s
        case 2: return 0.1f;   // fog:    hugs the ground
        case 3: return 1.5f;   // sparks
        case 4: return e.box[1];  // rain falls the emitter's own height
        default: return std::min(e.speed * e.life * 0.5f, 40.0f);  // custom
    }
}

// The pool, as world-space centres. Deterministic (a fixed low-discrepancy
// sequence, never rand()), so pressing the button twice gives the same answer -
// the same rule the corpus itself is built on.
std::vector<V3> emitterCentres(const SceneEmitter& e) {
    int n = e.count;
    if (n < 1) n = 1;
    if (n > 256) n = 256;  // the runtime's own clamp (buildParticles)
    const float rise = emitterRise(e);
    std::vector<V3> out;
    out.reserve((size_t)n);
    // Halton (2, 3) over XZ and a straight stratification up Y: the pool is
    // small and a hashed uniform draw clumps visibly at n = 32.
    const auto halton = [](int i, int base) {
        float f = 1.0f, r = 0.0f;
        while (i > 0) {
            f /= (float)base;
            r += f * (float)(i % base);
            i /= base;
        }
        return r;
    };
    for (int i = 0; i < n; ++i) {
        const float hx = halton(i + 1, 2), hz = halton(i + 1, 3);
        const float hy = ((float)i + 0.5f) / (float)n;
        V3 c;
        c.x = e.pos[0] + (hx - 0.5f) * e.box[0];
        c.z = e.pos[2] + (hz - 0.5f) * e.box[2];
        c.y = e.kind == 4 ? e.pos[1] - hy * rise : e.pos[1] + hy * rise;
        out.push_back(c);
    }
    return out;
}

// One emitter's fill under one camera: `count` camera-facing quads, which is
// exactly what the VU1 billboard program expands each centre into.
uint64_t countEmitter(const std::vector<V3>& centres, const CovBillboard& b, const Pinhole& cam,
                      int rw, int rh) {
    const V3 right{cam.right[0], cam.right[1], cam.right[2]};
    const V3 up{cam.up[0], cam.up[1], cam.up[2]};
    CovMesh q;
    q.p.reserve(centres.size() * 4);
    q.idx.reserve(centres.size() * 6);
    for (const V3& c : centres) {
        const V3 rw2 = right * b.halfW, uh = up * b.halfH;
        const int base = (int)q.p.size();
        q.p.push_back(c - rw2 - uh);
        q.p.push_back(c + rw2 - uh);
        q.p.push_back(c + rw2 + uh);
        q.p.push_back(c - rw2 + uh);
        const int t[6] = {base, base + 1, base + 2, base, base + 2, base + 3};
        q.idx.insert(q.idx.end(), t, t + 6);
    }
    return countMesh(q, cam, rw, rh);
}

// One job of the count: which shot, which frame within it.
struct CovJob {
    int shot = 0, frame = 0, frames = 1;
};

}  // namespace

CoverageReport measureCoverage(const CoverageConfig& cfg, const std::atomic<bool>* cancel) {
    CoverageReport rep;
    // Echoed before any early return: a caller pricing a FAILED report gets the
    // raster it asked about rather than a zero that reads as "no screen".
    rep.outW = cfg.outW;
    rep.outH = cfg.outH;
    if (cfg.projectDir.empty()) {
        rep.err = "no project directory";
        return rep;
    }
    std::string err;
    const std::vector<ProjectScene> scenes =
        loadProject(cfg.projectDir, &err, cfg.verbose, /*animated=*/true, nullptr);
    if (!err.empty()) {
        rep.err = err;
        return rep;
    }
    if (scenes.empty()) {
        rep.err = "the project loads but has nothing to draw";
        return rep;
    }
    if (cancel && cancel->load()) {
        rep.err = "cancelled";
        return rep;
    }

    // The raster keeps the OUTPUT's aspect, so the projection is the console's
    // and only the pixel pitch differs - which a ratio cannot see.
    const int rw = std::max(32, cfg.raster);
    const int rh = std::max(16, (int)std::lround((double)rw * (double)cfg.outH / (double)cfg.outW));
    const double pixels = (double)rw * (double)rh;
    const int perShot = std::max(1, cfg.framesPerShot);

    // Flatten the project into (geometry, emitters, shots) once. The shots of
    // one scene share its geometry, exactly as buildProjectShots arranges it.
    struct CovScene {
        std::vector<CovMesh> mesh;
        std::vector<std::vector<CovMesh>> anim;   // [part][pose]
        std::vector<std::vector<V3>> emitCentre;  // [emitter]
        std::vector<CovBillboard> emitBill;
    };
    std::vector<CovScene> geo;
    geo.reserve(scenes.size());
    struct CovShot {
        int scene = 0;
        const SceneShot* shot = nullptr;
    };
    std::vector<CovShot> shots;
    for (size_t si = 0; si < scenes.size(); ++si) {
        const ProjectScene& ps = scenes[si];
        CovScene cs;
        cs.mesh.reserve(ps.mesh.size());
        for (const SceneMesh& sm : ps.mesh) {
            if (sm.cutout) rep.sawCutout = true;
            cs.mesh.push_back(covOf(sm));
        }
        for (const AnimMesh& am : ps.anim) {
            if (am.pose.empty()) continue;
            rep.sawAnimated = true;
            std::vector<CovMesh> poses;
            poses.reserve(am.pose.size());
            for (const SceneMesh& sm : am.pose) poses.push_back(covOf(sm));
            cs.anim.push_back(std::move(poses));
        }
        for (const SceneEmitter& e : ps.emitter) {
            // A disabled emitter starts hidden and draws nothing until a flow
            // node shows it, so it is not fill - but it is a reason the number
            // could be wrong later, which the report carries.
            if (!e.enabled) {
                rep.sawDisabledEmitter = true;
                continue;
            }
            cs.emitCentre.push_back(emitterCentres(e));
            cs.emitBill.push_back(billboardOf(e));
            ++rep.emitters;
            rep.billboards += (int)cs.emitCentre.back().size();
        }
        rep.triangles += ps.triangles();
        geo.push_back(std::move(cs));
        for (const SceneShot& ss : ps.shot)
            if (ss.keys() >= 1) shots.push_back({(int)si, &ss});
    }
    rep.scenes = (int)scenes.size();
    if (shots.empty()) {
        rep.err = "the project has no camera move to measure";
        return rep;
    }

    std::vector<CovJob> jobs;
    jobs.reserve(shots.size() * (size_t)perShot);
    for (size_t s = 0; s < shots.size(); ++s)
        for (int f = 0; f < perShot; ++f) jobs.push_back({(int)s, f, perShot});
    // Per-job slots, written by the job that owns them: which worker computes
    // job i is a function of the core count and nothing else touches job i, so
    // the answer does not depend on the machine (blsscorpus' parallelFrames
    // rule, and for the same reason - a number that moved with the core count
    // would be one nobody could reproduce).
    std::vector<double> jobGeom(jobs.size(), 0.0), jobEmit(jobs.size(), 0.0);

    const auto camOf = [&](const CovShot& cs, int f) {
        Shot s;
        s.move = Move::Path;
        s.pathEye = cs.shot->eye;
        s.pathLook = cs.shot->look;
        s.ease = cs.shot->ease;
        s.fovDeg = cs.shot->fovDeg > 5.0f ? cs.shot->fovDeg : 60.0f;
        const float t = perShot > 1 ? (float)f / (float)(perShot - 1) : 0.5f;
        return cameraAt(s, t);
    };

    const auto body = [&](size_t i) {
        if (cancel && cancel->load()) return;
        const CovJob& j = jobs[i];
        const CovShot& cs = shots[(size_t)j.shot];
        const CovScene& g = geo[(size_t)cs.scene];
        const Pinhole cam = camOf(cs, j.frame);
        uint64_t frags = 0;
        for (const CovMesh& m : g.mesh) frags += countMesh(m, cam, rw, rh);
        for (const std::vector<CovMesh>& poses : g.anim) {
            const size_t pose = std::min((size_t)j.frame, poses.size() - 1);
            frags += countMesh(poses[pose], cam, rw, rh);
        }
        jobGeom[i] = (double)frags / pixels;
        uint64_t ef = 0;
        for (size_t e = 0; e < g.emitCentre.size(); ++e)
            ef += countEmitter(g.emitCentre[e], g.emitBill[e], cam, rw, rh);
        jobEmit[i] = (double)ef / pixels;
    };

    int workers = cfg.threads > 0 ? cfg.threads : (int)std::thread::hardware_concurrency();
    if (workers < 1) workers = 1;
    if (workers > 32) workers = 32;
    if (workers > (int)jobs.size()) workers = (int)jobs.size();
    if (workers <= 1) {
        for (size_t i = 0; i < jobs.size(); ++i) body(i);
    } else {
        const auto run = [&](int w) {
            for (size_t i = (size_t)w; i < jobs.size(); i += (size_t)workers) body(i);
        };
        std::vector<std::thread> pool;
        pool.reserve((size_t)workers - 1);
        for (int w = 1; w < workers; ++w) pool.emplace_back(run, w);
        run(0);
        for (std::thread& t : pool) t.join();
    }
    if (cancel && cancel->load()) {
        rep.err = "cancelled";
        return rep;
    }

    rep.shots.resize(shots.size());
    for (size_t s = 0; s < shots.size(); ++s) {
        rep.shots[s].scene = scenes[(size_t)shots[s].scene].name;
        rep.shots[s].name = shots[s].shot->name;
        rep.shots[s].move = shots[s].shot->move;
    }
    std::vector<double> all;
    all.reserve(jobs.size());
    for (size_t i = 0; i < jobs.size(); ++i) {
        CoverageShot& cs = rep.shots[(size_t)jobs[i].shot];
        cs.geom += jobGeom[i];
        cs.emit += jobEmit[i];
        cs.peak = std::max(cs.peak, jobGeom[i] + jobEmit[i]);
        ++cs.frames;
        all.push_back(jobGeom[i] + jobEmit[i]);
        rep.geomMean += jobGeom[i];
        rep.emitMean += jobEmit[i];
    }
    for (CoverageShot& cs : rep.shots)
        if (cs.frames > 0) {
            cs.geom /= (double)cs.frames;
            cs.emit /= (double)cs.frames;
        }
    rep.frames = (int)jobs.size();
    rep.geomMean /= (double)jobs.size();
    rep.emitMean /= (double)jobs.size();
    rep.mean = rep.geomMean + rep.emitMean;
    std::sort(all.begin(), all.end());
    // Nearest-rank p95, floored at the last sample - with 30-odd frames the
    // interpolated variants differ by less than the model's own uncertainty and
    // "the 95th percentile is one of the frames you rendered" is easier to
    // defend than a number that is not.
    const size_t k = std::min(all.size() - 1, (size_t)std::ceil(0.95 * (double)all.size()) - 1);
    rep.p95 = all[k];
    rep.ok = true;
    if (cfg.verbose)
        std::printf("[blss] coverage: %.1f full-screen coverages (geometry %.1f + emitters %.1f), "
                    "p95 %.1f, %d frame(s)\n",
                    rep.mean, rep.geomMean, rep.emitMean, rep.p95, rep.frames);
    return rep;
}

}  // namespace blss
