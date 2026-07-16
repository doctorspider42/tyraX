#include "savebake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <stb_image.h>  // implementation lives in app.cpp

#include "glbparser.hpp"
#include "objparser.hpp"

namespace savebake {

std::string displayTitle(const Project& p) {
    return p.saveTitle.empty() ? p.name : p.saveTitle;
}

HudText busyText() {
    HudText t;
    t.name = "save-busy";
    t.text = "Checking memory card (PS2)...\n"
             "Do not remove the memory card,\n"
             "reset or switch off the console.";
    t.size = 18;
    t.shadow = true;
    return t;
}

// --- icon.sys ---------------------------------------------------------------

static void putU16(std::vector<unsigned char>& b, size_t off, unsigned v) {
    b[off] = (unsigned char)(v & 0xFF);
    b[off + 1] = (unsigned char)((v >> 8) & 0xFF);
}

static void putU32(std::vector<unsigned char>& b, size_t off, unsigned v) {
    for (int i = 0; i < 4; ++i) b[off + i] = (unsigned char)((v >> (8 * i)) & 0xFF);
}

static void putF32(std::vector<unsigned char>& b, size_t off, float v) {
    unsigned u;
    std::memcpy(&u, &v, 4);
    putU32(b, off, u);
}

std::vector<unsigned char> iconSys(const Project& p) {
    std::vector<unsigned char> b(kIconSysBytes, 0);
    std::memcpy(b.data(), "PS2D", 4);

    // Title: the PS2 browser's OSD font renders only FULL-WIDTH Shift-JIS
    // characters - plain single-byte ASCII is valid S-JIS but draws as
    // nothing (verified in the PCSX2 BIOS browser), which is why every save
    // tool converts titles to their full-width forms. Digits/letters map
    // arithmetically into the 0x82 row; punctuation needs the 0x81-row
    // lookup. Unmappable bytes become a full-width '?'. '|' splits the two
    // browser lines - the u16 at 0x06 is the byte offset of line two.
    auto fullWidth = [](char c) -> unsigned {
        if (c == ' ') return 0x8140;
        if (c >= '0' && c <= '9') return 0x824F + (c - '0');
        if (c >= 'A' && c <= 'Z') return 0x8260 + (c - 'A');
        if (c >= 'a' && c <= 'z') return 0x8281 + (c - 'a');
        switch (c) {
            case '!': return 0x8149;
            case '"': return 0x8168;
            case '#': return 0x8194;
            case '$': return 0x8190;
            case '%': return 0x8193;
            case '&': return 0x8195;
            case '\'': return 0x8166;
            case '(': return 0x8169;
            case ')': return 0x816A;
            case '*': return 0x8196;
            case '+': return 0x817B;
            case ',': return 0x8143;
            case '-': return 0x815D;
            case '.': return 0x8144;
            case '/': return 0x815E;
            case ':': return 0x8146;
            case ';': return 0x8147;
            case '<': return 0x8183;
            case '=': return 0x8181;
            case '>': return 0x8184;
            case '?': return 0x8148;
            case '@': return 0x8197;
            case '[': return 0x816D;
            case '\\': return 0x815F;
            case ']': return 0x816E;
            case '^': return 0x814F;
            case '_': return 0x8151;
            case '`': return 0x814D;
            case '{': return 0x816F;
            case '}': return 0x8170;
            case '~': return 0x8160;
            default: return 0x8148;  // full-width '?'
        }
    };
    std::string title = displayTitle(p);
    std::string sjis;
    unsigned lineBreak = 0;
    for (char c : title) {
        if (sjis.size() + 2 > 67) break;  // 68-byte field incl. the NUL
        if (c == '|' && lineBreak == 0) {
            lineBreak = (unsigned)sjis.size();
            continue;
        }
        const unsigned fw = fullWidth(c);
        sjis += (char)(fw >> 8);
        sjis += (char)(fw & 0xFF);
    }
    putU16(b, 0x06, lineBreak);
    putU32(b, 0x0C, 0);  // background transparency (0 = fully transparent)

    // Background corner colors (u32 RGBA, 0..0x80) - a subtle dark gradient.
    const unsigned bgTop[4] = {0x14, 0x1A, 0x26, 0x00};
    const unsigned bgBot[4] = {0x08, 0x0A, 0x10, 0x00};
    for (int i = 0; i < 4; ++i) {
        putU32(b, 0x10 + i * 4, bgTop[i]);  // upper left
        putU32(b, 0x20 + i * 4, bgTop[i]);  // upper right
        putU32(b, 0x30 + i * 4, bgBot[i]);  // lower left
        putU32(b, 0x40 + i * 4, bgBot[i]);  // lower right
    }

    // Three directional lights + ambient (f32 vectors/colors) - the stock
    // neutral setup most retail icon.sys files ship.
    const float dirs[3][4] = {{0.5f, 0.5f, 0.5f, 0.0f},
                              {0.0f, -0.4f, -0.1f, 0.0f},
                              {-0.5f, -0.5f, 0.5f, 0.0f}};
    const float cols[4][4] = {{0.3f, 0.3f, 0.3f, 0.0f},
                              {0.4f, 0.4f, 0.4f, 0.0f},
                              {0.5f, 0.5f, 0.5f, 0.0f},
                              {0.5f, 0.5f, 0.5f, 0.0f}};  // last = ambient
    for (int l = 0; l < 3; ++l)
        for (int i = 0; i < 4; ++i) putF32(b, 0x50 + l * 16 + i * 4, dirs[l][i]);
    for (int l = 0; l < 4; ++l)
        for (int i = 0; i < 4; ++i) putF32(b, 0x80 + l * 16 + i * 4, cols[l][i]);

    std::memcpy(b.data() + 0xC0, sjis.c_str(), sjis.size() + 1);  // title[68]
    // One icon serves view/copy/delete (offsets 260/324/388, 64 bytes each).
    for (size_t off : {(size_t)260, (size_t)324, (size_t)388})
        std::memcpy(b.data() + off, "list.icn", 9);
    return b;
}

// --- list.icn ----------------------------------------------------------------

// Built-in placeholder texture: dark backdrop, accent border, centered diamond
// - recognizable in the browser without shipping any binary asset.
static void placeholderTexture(unsigned char* rgba /*128*128*4*/) {
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            unsigned char* px = rgba + (y * 128 + x) * 4;
            // vertical navy gradient
            px[0] = (unsigned char)(18 + y / 6);
            px[1] = (unsigned char)(26 + y / 4);
            px[2] = (unsigned char)(48 + y / 3);
            px[3] = 255;
            const int border = (x < 5 || x >= 123 || y < 5 || y >= 123) ? 1 : 0;
            const int dist = std::abs(x - 64) + std::abs(y - 64);
            if (border || dist < 34) {
                px[0] = 92;
                px[1] = 190;
                px[2] = 210;
            }
            if (dist < 26) {  // inner diamond, darker
                px[0] = 24;
                px[1] = 60;
                px[2] = 84;
            }
        }
    }
}

// Bilinear resample of an arbitrary RGBA image to the fixed 128x128 icon
// texture, alpha composited over the placeholder's backdrop tone.
static void resampleTo128(const unsigned char* src, int sw, int sh,
                          unsigned char* dst /*128*128*4*/) {
    for (int y = 0; y < 128; ++y) {
        for (int x = 0; x < 128; ++x) {
            const float fx = (x + 0.5f) * sw / 128.0f - 0.5f;
            const float fy = (y + 0.5f) * sh / 128.0f - 0.5f;
            int x0 = (int)fx, y0 = (int)fy;
            if (x0 < 0) x0 = 0;
            if (y0 < 0) y0 = 0;
            const int x1 = x0 + 1 < sw ? x0 + 1 : x0;
            const int y1 = y0 + 1 < sh ? y0 + 1 : y0;
            const float tx = fx - x0, ty = fy - y0;
            unsigned char* px = dst + (y * 128 + x) * 4;
            for (int c = 0; c < 4; ++c) {
                const float a = src[(y0 * sw + x0) * 4 + c] * (1 - tx) +
                                src[(y0 * sw + x1) * 4 + c] * tx;
                const float b = src[(y1 * sw + x0) * 4 + c] * (1 - tx) +
                                src[(y1 * sw + x1) * 4 + c] * tx;
                px[c] = (unsigned char)(a * (1 - ty) + b * ty + 0.5f);
            }
            if (px[3] < 255) {  // composite over a dark backdrop
                const int a = px[3];
                px[0] = (unsigned char)((px[0] * a + 20 * (255 - a)) / 255);
                px[1] = (unsigned char)((px[1] * a + 26 * (255 - a)) / 255);
                px[2] = (unsigned char)((px[2] * a + 40 * (255 - a)) / 255);
            }
        }
    }
}

static void pushU32(std::vector<unsigned char>& b, unsigned v) {
    for (int i = 0; i < 4; ++i) b.push_back((unsigned char)((v >> (8 * i)) & 0xFF));
}

static void pushS16(std::vector<unsigned char>& b, int v) {
    b.push_back((unsigned char)(v & 0xFF));
    b.push_back((unsigned char)((v >> 8) & 0xFF));
}

// f16 in icon files: s16 read back as value / 4096.
static void pushF16(std::vector<unsigned char>& b, float v) {
    pushS16(b, (int)(v * 4096.0f));
}

std::vector<unsigned char> iconTextureRGBA(const Project& p) {
    std::vector<unsigned char> rgba(128 * 128 * 4);
    bool loaded = false;
    if (!p.saveIcon.empty()) {
        const std::string full =
            (std::filesystem::path(p.dir) / p.saveIcon).string();
        int w = 0, h = 0, comp = 0;
        if (unsigned char* px = stbi_load(full.c_str(), &w, &h, &comp, 4)) {
            resampleTo128(px, w, h, rgba.data());
            stbi_image_free(px);
            loaded = true;
        }
    }
    if (!loaded) placeholderTexture(rgba.data());
    return rgba;
}

// --- icon geometry -----------------------------------------------------------
// All sources reduce to this: a flat triangle list with `shapes` position
// sets (the icon format's morph frames), one normal/uv/color per vertex,
// and the single 128x128 texture. Everything is already in icon space:
// y grows DOWNWARD, models stand on y = 0 and reach up into negative y.
namespace {
struct IconGeo {
    int shapes = 1;
    std::vector<float> pos;     // shapes * verts * 3
    std::vector<float> normal;  // verts * 3
    std::vector<float> uv;      // verts * 2
    std::vector<unsigned char> rgba;     // verts * 4
    std::vector<unsigned char> texture;  // 128 * 128 * 4
    int verts() const { return (int)(normal.size() / 3); }
};

// Fit shape 0's world-space vertices into icon space and mirror the Y axis
// (world y-up -> icon y-down). Applies to every shape with one transform so
// the animation doesn't swim. Mirroring flips the winding - the caller's
// triangles must already be emitted in reversed order (or two-sided).
void fitToIconSpace(IconGeo& g) {
    if (g.pos.empty()) return;
    float mn[3], mx[3];
    for (int a = 0; a < 3; ++a) mn[a] = mx[a] = g.pos[a];
    for (size_t i = 0; i < g.pos.size(); i += 3)
        for (int a = 0; a < 3; ++a) {
            mn[a] = std::min(mn[a], g.pos[i + a]);
            mx[a] = std::max(mx[a], g.pos[i + a]);
        }
    const float ext =
        std::max({mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2], 0.001f});
    const float s = 3.6f / ext;
    const float cx = (mn[0] + mx[0]) * 0.5f, cz = (mn[2] + mx[2]) * 0.5f;
    for (size_t i = 0; i < g.pos.size(); i += 3) {
        g.pos[i + 0] = (g.pos[i + 0] - cx) * s;
        g.pos[i + 1] = -(g.pos[i + 1] - mn[1]) * s;  // feet at 0, up = -y
        g.pos[i + 2] = (g.pos[i + 2] - cz) * s;
    }
    for (size_t i = 1; i < g.normal.size(); i += 3) g.normal[i] = -g.normal[i];
}

// Duplicates shape 0 into `shapes` sets, each yawed by a sine sway - the
// idle motion for sources with no animation of their own (the flat quad
// and .obj models). Small angles morph cleanly; the loop is seamless.
void applySway(IconGeo& g, int shapes) {
    if (shapes <= 1 || g.pos.empty()) return;
    const std::vector<float> base(g.pos.begin(), g.pos.begin() + g.verts() * 3);
    g.pos.resize((size_t)g.verts() * 3 * shapes);
    g.shapes = shapes;
    for (int k = 0; k < shapes; ++k) {
        const float a = 0.22f * sinf(6.2831853f * k / shapes);  // ~12.5 deg
        const float c = cosf(a), sn = sinf(a);
        float* out = &g.pos[(size_t)k * g.verts() * 3];
        for (int v = 0; v < g.verts(); ++v) {
            const float x = base[v * 3], y = base[v * 3 + 1], z = base[v * 3 + 2];
            out[v * 3 + 0] = x * c + z * sn;
            out[v * 3 + 1] = y;
            out[v * 3 + 2] = -x * sn + z * c;
        }
    }
}

// The flat two-sided quad (the default icon and every fallback).
IconGeo quadGeo(const Project& p) {
    IconGeo g;
    const float W = 1.8f, H = 3.6f;
    const float quad[4][3] = {
        {-W, H, 0.0f}, {W, H, 0.0f}, {W, 0.0f, 0.0f}, {-W, 0.0f, 0.0f}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    // world space here (y up); fitToIconSpace mirrors it
    const int tris[4][3] = {{0, 1, 2}, {0, 2, 3}, {2, 1, 0}, {3, 2, 0}};
    for (int t = 0; t < 4; ++t)
        for (int vi = 0; vi < 3; ++vi) {
            const int v = tris[t][vi];
            for (int a = 0; a < 3; ++a) g.pos.push_back(quad[v][a]);
            g.normal.insert(g.normal.end(), {0.0f, 0.0f, t < 2 ? -1.0f : 1.0f});
            g.uv.insert(g.uv.end(), {uv[v][0], uv[v][1]});
            g.rgba.insert(g.rgba.end(), {0xFF, 0xFF, 0xFF, 0xFF});
        }
    g.texture = iconTextureRGBA(p);
    fitToIconSpace(g);
    applySway(g, kQuadShapes);
    return g;
}

// Loads an image file into the 128x128 icon texture. False = unreadable.
bool textureFromFile(const std::string& path, std::vector<unsigned char>& out) {
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!px) return false;
    out.resize(128 * 128 * 4);
    resampleTo128(px, w, h, out.data());
    stbi_image_free(px);
    return true;
}

// Texture for a model that ships none of its own: the user-picked image if
// any, else near-white - the browser multiplies texture x vertex color, so
// a neutral base lets the model's kd/baseColor tints come through cleanly
// (the diamond placeholder would muddy every face).
void modelFallbackTexture(const Project& p, std::vector<unsigned char>& out) {
    if (!p.saveIcon.empty()) {
        out = iconTextureRGBA(p);
        return;
    }
    out.assign(128 * 128 * 4, 235);
    for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
}

// res/models .obj: triangles + map_Kd texture, sway animation. The mirror in
// fitToIconSpace flips winding, so triangles are emitted reversed here.
bool objGeo(const Project& p, const std::string& rel, int frames, IconGeo& g,
            IconInfo& info) {
    const std::filesystem::path full = std::filesystem::path(p.dir) / rel;
    objparser::Model m;
    if (!objparser::load(full.string(), m)) {
        info.warning = "cannot read " + rel;
        return false;
    }
    if (m.vertexCount() > kMaxIconTris * 3) {
        info.warning = rel + " has " + std::to_string(m.vertexCount() / 3) +
                       " tris (icon cap " + std::to_string(kMaxIconTris) + ")";
        return false;
    }
    std::string texPath;
    for (const objparser::Submesh& sm : m.submeshes) {
        const unsigned char kd[3] = {(unsigned char)(sm.kd[0] * 255.0f + 0.5f),
                                     (unsigned char)(sm.kd[1] * 255.0f + 0.5f),
                                     (unsigned char)(sm.kd[2] * 255.0f + 0.5f)};
        if (texPath.empty() && !sm.texture.empty())
            texPath = (full.parent_path() / sm.texture).string();
        const size_t tris = sm.verts.size() / 24;
        for (size_t t = 0; t < tris; ++t)
            for (int vi = 2; vi >= 0; --vi) {  // reversed: the Y mirror flips it back
                const float* v = &sm.verts[(t * 3 + vi) * 8];
                g.pos.insert(g.pos.end(), {v[0], v[1], v[2]});
                g.normal.insert(g.normal.end(), {v[3], v[4], v[5]});
                g.uv.insert(g.uv.end(), {v[6], v[7]});
                g.rgba.insert(g.rgba.end(), {kd[0], kd[1], kd[2], 0xFF});
            }
    }
    if (texPath.empty() || !textureFromFile(texPath, g.texture))
        modelFallbackTexture(p, g.texture);
    fitToIconSpace(g);
    applySway(g, frames);
    info.source = std::filesystem::path(rel).filename().string();
    return true;
}

// res/models .glb: the chosen clip sampled into `frames` morph shapes - a
// real animated icon. Parts merge into one list; the texture comes from the
// biggest textured part (icons carry a single 128x128 image).
bool glbGeo(const Project& p, const std::string& rel, int frames, IconGeo& g,
            IconInfo& info) {
    const std::filesystem::path full = std::filesystem::path(p.dir) / rel;
    glbparser::Baked baked;
    std::string err;
    if (!glbparser::bake(full.string(), 12.0f, baked, err)) {
        info.warning = rel + ": " + err;
        return false;
    }
    if (baked.totalVertexCount() > kMaxIconTris * 3) {
        info.warning = rel + " has " +
                       std::to_string(baked.totalVertexCount() / 3) +
                       " tris (icon cap " + std::to_string(kMaxIconTris) + ")";
        return false;
    }
    const glbparser::Clip* clip = &baked.clips[0];
    for (const glbparser::Clip& c : baked.clips)
        if (!p.saveIconClip.empty() && c.name == p.saveIconClip) clip = &c;
    const int shapes = std::max(1, std::min({frames, clip->frameCount,
                                             (int)kMaxIconShapes}));
    g.shapes = shapes;

    // Texture: the image referenced by the most vertices.
    int texImage = -1, texVotes = -1;
    for (const glbparser::Part& part : baked.parts)
        if (part.image >= 0 && part.vertexCount > texVotes) {
            texImage = part.image;
            texVotes = part.vertexCount;
        }
    if (texImage >= 0) {
        const glbparser::Image& img = baked.images[texImage];
        int w = 0, h = 0, comp = 0;
        if (unsigned char* px = stbi_load_from_memory(
                img.png.data(), (int)img.png.size(), &w, &h, &comp, 4)) {
            g.texture.resize(128 * 128 * 4);
            resampleTo128(px, w, h, g.texture.data());
            stbi_image_free(px);
        }
    }
    if (g.texture.empty()) modelFallbackTexture(p, g.texture);

    // Merge parts; shape k samples the clip evenly (last shape lands just
    // before the loop point so shape N-1 -> 0 crossfades seamlessly).
    const int totalVerts = baked.totalVertexCount();
    g.pos.resize((size_t)shapes * totalVerts * 3);
    for (int k = 0; k < shapes; ++k) {
        const int f = clip->firstFrame +
                      (int)((long long)k * clip->frameCount / shapes);
        float* out = &g.pos[(size_t)k * totalVerts * 3];
        size_t o = 0;
        for (const glbparser::Part& part : baked.parts) {
            const size_t n = (size_t)part.vertexCount * 3;
            // reversed triangles (the Y mirror in fitToIconSpace flips winding)
            for (size_t t = 0; t < (size_t)part.vertexCount / 3; ++t)
                for (int vi = 2; vi >= 0; --vi) {
                    const float* v = &part.positions[f * n + (t * 3 + vi) * 3];
                    out[o++] = v[0];
                    out[o++] = v[1];
                    out[o++] = v[2];
                }
        }
    }
    for (const glbparser::Part& part : baked.parts) {
        const unsigned char tint[3] = {
            (unsigned char)(part.baseColor[0] * 255.0f + 0.5f),
            (unsigned char)(part.baseColor[1] * 255.0f + 0.5f),
            (unsigned char)(part.baseColor[2] * 255.0f + 0.5f)};
        const size_t n = (size_t)part.vertexCount * 3;
        const int f0 = clip->firstFrame;
        for (size_t t = 0; t < (size_t)part.vertexCount / 3; ++t)
            for (int vi = 2; vi >= 0; --vi) {
                const size_t v = t * 3 + vi;
                g.normal.insert(g.normal.end(),
                                {part.normals[f0 * n + v * 3],
                                 part.normals[f0 * n + v * 3 + 1],
                                 part.normals[f0 * n + v * 3 + 2]});
                g.uv.insert(g.uv.end(),
                            {part.uvs[v * 2], part.uvs[v * 2 + 1]});
                const bool tinted = part.image != texImage || texImage < 0;
                g.rgba.insert(g.rgba.end(),
                              {tinted ? tint[0] : (unsigned char)0xFF,
                               tinted ? tint[1] : (unsigned char)0xFF,
                               tinted ? tint[2] : (unsigned char)0xFF, 0xFF});
            }
    }
    fitToIconSpace(g);
    info.source = std::filesystem::path(rel).filename().string() + " (" +
                  clip->name + ")";
    return true;
}

IconGeo buildGeo(const Project& p, IconInfo& info) {
    info = IconInfo{};
    info.source = "flat image";
    const int frames =
        std::max(1, std::min(p.saveIconFrames, (int)kMaxIconShapes));
    if (!p.saveIconModel.empty()) {
        std::string ext =
            std::filesystem::path(p.saveIconModel).extension().string();
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        IconGeo g;
        const bool ok = ext == ".glb" ? glbGeo(p, p.saveIconModel, frames, g, info)
                                      : objGeo(p, p.saveIconModel, frames, g, info);
        if (ok) return g;
        // fall through to the quad; info.warning explains why
        info.source = "flat image (fallback)";
    }
    return quadGeo(p);
}
}  // namespace

// The animation timeline: shapes at a fixed tick spacing. Each frame's keys
// are that shape's WEIGHT envelope over the timeline (the browser lerps
// between keys and blends the shapes by weight - see mymcplus/ps2icon.py):
// a tent peaking at the shape's own tick makes consecutive shapes crossfade,
// and shape 0 closes the loop with a rising tail at the timeline end.
constexpr int kTicksPerShape = 8;

static std::vector<std::pair<float, float>> shapeKeys(int i, int shapes) {
    const float T = (float)kTicksPerShape, L = T * shapes;
    if (shapes == 1) return {{0.0f, 1.0f}};
    if (i == 0) return {{0.0f, 1.0f}, {T, 0.0f}, {L - T, 0.0f}, {L, 1.0f}};
    return {{T * (i - 1), 0.0f}, {T * i, 1.0f}, {T * (i + 1), 0.0f}};
}

static int animSegmentBytes(int shapes) {
    int b = 20;
    for (int i = 0; i < shapes; ++i)
        b += 8 + (int)shapeKeys(i, shapes).size() * 8;
    return b;
}

IconInfo iconInfo(const Project& p) {
    IconInfo info;
    const IconGeo g = buildGeo(p, info);
    info.triangles = g.verts() / 3;
    info.shapes = g.shapes;
    info.bytes = 20 + g.verts() * (g.shapes * 8 + 16) +
                 animSegmentBytes(g.shapes) + 128 * 128 * 2;
    return info;
}

std::vector<unsigned char> iconIcn(const Project& p, IconInfo* infoOut) {
    IconInfo info;
    const IconGeo g = buildGeo(p, info);

    std::vector<unsigned char> b;
    b.reserve(20 + g.verts() * (g.shapes * 8 + 16) + animSegmentBytes(g.shapes) +
              128 * 128 * 2);

    pushU32(b, 0x010000);    // magic
    pushU32(b, g.shapes);    // animation shapes
    pushU32(b, 0x07);        // texture type: uncompressed 128x128 BGR555
    pushU32(b, 0x3F800000);  // constant (1.0f)
    pushU32(b, g.verts());   // vertex count

    for (int v = 0; v < g.verts(); ++v) {
        for (int k = 0; k < g.shapes; ++k) {
            const float* pos = &g.pos[((size_t)k * g.verts() + v) * 3];
            pushF16(b, pos[0]);
            pushF16(b, pos[1]);
            pushF16(b, pos[2]);
            pushS16(b, 0);
        }
        pushF16(b, g.normal[v * 3]);
        pushF16(b, g.normal[v * 3 + 1]);
        pushF16(b, g.normal[v * 3 + 2]);
        pushS16(b, 0);
        pushF16(b, g.uv[v * 2]);
        pushF16(b, g.uv[v * 2 + 1]);
        for (int c = 0; c < 4; ++c) b.push_back(g.rgba[v * 4 + c]);
    }

    // Animation segment. Each frame's keys are its shape's weight envelope
    // over the timeline (tent around the shape's own tick - shapeKeys);
    // key 0 sits inline where older docs saw "two unknown" u32s.
    pushU32(b, 0x01);                        // id tag
    pushU32(b, g.shapes * kTicksPerShape);   // frame length (timeline ticks)
    pushU32(b, 0x3F800000);                  // anim speed = 1.0f
    pushU32(b, 0);                           // play offset
    pushU32(b, g.shapes);                    // frame count
    for (int k = 0; k < g.shapes; ++k) {
        const auto keys = shapeKeys(k, g.shapes);
        pushU32(b, (unsigned)k);            // shape id
        pushU32(b, (unsigned)keys.size());  // key count
        for (const auto& [t, v] : keys) {
            unsigned tu, vu;
            std::memcpy(&tu, &t, 4);
            std::memcpy(&vu, &v, 4);
            pushU32(b, tu);
            pushU32(b, vu);
        }
    }

    // Texture: 128x128 BGR555 (blue in the high bits), little endian.
    for (int i = 0; i < 128 * 128; ++i) {
        const unsigned char* px = &g.texture[i * 4];
        const unsigned v = ((px[2] >> 3) << 10) | ((px[1] >> 3) << 5) | (px[0] >> 3);
        b.push_back((unsigned char)(v & 0xFF));
        b.push_back((unsigned char)((v >> 8) & 0xFF));
    }
    info.triangles = g.verts() / 3;
    info.shapes = g.shapes;
    info.bytes = (int)b.size();
    if (infoOut) *infoOut = info;
    return b;
}

}  // namespace savebake
