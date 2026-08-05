#include "savebake.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <stb_image.h>  // implementation lives in app.cpp
#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "glbparser.hpp"
#include "objparser.hpp"

namespace savebake {

namespace {
void pngWrite(void* context, void* data, int size) {
    auto* out = (std::vector<unsigned char>*)context;
    out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
}
}  // namespace

bool spinnerPNG(std::vector<unsigned char>& png) {
    const int C = kSpinnerCell, N = kSpinnerFrames;
    const int W = C * N;
    std::vector<unsigned char> rgba((size_t)W * C * 4, 0);
    const float mid = (float)C * 0.5f - 0.5f;
    const float ring = (float)C * 0.33f;  // where the dots sit
    const float dot = (float)C * 0.105f;  // dot radius
    for (int f = 0; f < N; ++f)
        for (int j = 0; j < N; ++j) {
            // The head is the current frame's dot; the rest trail off behind
            // it, which is what reads as rotation across a strip of stills.
            const int back = (f - j + N) % N;
            const float bright = 1.0f - (float)back / (float)N * 0.85f;
            const float a = 6.2831853f * (float)j / (float)N - 1.5707963f;
            const float cxp = mid + ring * cosf(a), cyp = mid + ring * sinf(a);
            const int x0 = std::max(0, (int)(cxp - dot - 1.5f));
            const int x1 = std::min(C - 1, (int)(cxp + dot + 1.5f));
            const int y0 = std::max(0, (int)(cyp - dot - 1.5f));
            const int y1 = std::min(C - 1, (int)(cyp + dot + 1.5f));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float dx = (float)x - cxp, dy = (float)y - cyp;
                    const float d = std::sqrt(dx * dx + dy * dy);
                    // 1px of feather, so a 24px sprite does not look chewed
                    float cov = dot - d + 0.5f;
                    cov = std::min(1.0f, std::max(0.0f, cov));
                    if (cov <= 0.0f) continue;
                    const float v = cov * bright;
                    unsigned char* px =
                        &rgba[(((size_t)y * W) + (size_t)f * C + x) * 4];
                    const unsigned char lum =
                        (unsigned char)(255.0f * std::min(1.0f, v * 1.0f));
                    if (lum <= px[3]) continue;  // dots never overlap, but be safe
                    px[0] = px[1] = px[2] = 255;
                    px[3] = lum;
                }
        }
    png.clear();
    return stbi_write_png_to_func(pngWrite, &png, W, C, 4, rgba.data(), W * 4) != 0;
}

// What the GS will accept as a texture dimension. Anything else trips the
// engine's own assert at load time (texture.cpp), which halts the game before
// it has drawn a frame.
static bool powerOfTwoDim(int v) {
    return v == 8 || v == 16 || v == 32 || v == 64 || v == 128 || v == 256 ||
           v == 512;
}

SpinnerInfo spinnerInfo(const Project& p) {
    SpinnerInfo s;
    s.resPath = "res/hud/save-spinner.png";
    s.frames = kSpinnerFrames;
    s.cellW = s.cellH = kSpinnerCell;
    s.sheetW = kSpinnerCell * kSpinnerFrames;
    s.sheetH = kSpinnerCell;
    if (p.saveSpinnerImage.empty()) return s;

    const std::filesystem::path full =
        std::filesystem::path(p.dir) / p.saveSpinnerImage;
    int w = 0, h = 0, comp = 0;
    if (!stbi_info(full.string().c_str(), &w, &h, &comp)) {
        s.warning = "cannot read " + p.saveSpinnerImage;
        return s;
    }
    const int frames = std::max(1, std::min(p.saveSpinnerFrames, 64));
    if (!powerOfTwoDim(w) || !powerOfTwoDim(h)) {
        s.warning = p.saveSpinnerImage + " is " + std::to_string(w) + "x" +
                    std::to_string(h) +
                    " - both sides must be 8/16/32/64/128/256/512 or the "
                    "console refuses the texture";
        return s;
    }
    if (w % frames != 0) {
        s.warning = std::to_string(w) + " px does not divide into " +
                    std::to_string(frames) + " frames";
        return s;
    }
    s.resPath = p.saveSpinnerImage;
    s.frames = frames;
    s.sheetW = w;
    s.sheetH = h;
    s.cellW = w / frames;
    s.cellH = h;
    s.custom = true;
    return s;
}

std::string displayTitle(const Project& p) {
    return p.saveTitle.empty() ? p.name : p.saveTitle;
}

// ORDER IS THE ENUM: applyMotion switches on the index, so entries may be
// appended but never reordered - the key is what the .tyra stores, and a
// project written by another editor must not change meaning here.
const std::vector<IconMotion>& iconMotions() {
    static const std::vector<IconMotion> motions = {
        {"sway", "Sway",
         "A gentle turn left and right. The original motion, and what an icon "
         "with no setting still does."},
        {"bounce", "Bounce",
         "Hops, squashing as it lands - the classic PS2 save icon. The "
         "landing frame IS the loop point, so it never stutters."},
        {"spin", "Spin",
         "A full turn per loop. Wants most of the 8 shapes: with few shapes "
         "the browser lerps THROUGH the model between them rather than "
         "around it."},
        {"pulse", "Pulse",
         "Breathes in and out about its centre. The quietest of these, and "
         "the one that reads well on a flat image icon."},
        {"tilt", "Tilt",
         "Rocks about its feet like a metronome. Suits something with a "
         "clear base; a floating shape looks like it is falling over."},
        {"none", "None",
         "No motion. The icon ships as a single shape, which is also the "
         "smallest a list.icn can be."},
    };
    return motions;
}

int iconMotionIndex(const std::string& key) {
    const std::vector<IconMotion>& m = iconMotions();
    for (size_t i = 0; i < m.size(); ++i)
        if (key == m[i].key) return (int)i;
    return 0;  // "" and anything unknown: the pre-setting sway
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

// Duplicates shape 0 into `shapes` displaced sets - the idle motion for
// sources with no animation of their own (the flat quad, .obj models, and a
// .glb that carries no clip). Every motion is a closed loop over k/shapes so
// the last shape crossfades back into shape 0 with no jolt.
//
// Remember icon space is Y-DOWN (fitToIconSpace puts the feet at y=0 and up
// at -y), so "jump" means SUBTRACTING from y.
void applyMotion(IconGeo& g, int shapes, int motion, float amount) {
    if (shapes <= 1 || g.pos.empty()) return;
    if (motion == 5) return;  // "none": one shape, nothing to duplicate
    const int nv = g.verts();
    const std::vector<float> base(g.pos.begin(), g.pos.begin() + nv * 3);
    g.pos.resize((size_t)nv * 3 * shapes);
    g.shapes = shapes;

    // Model extent, for the amplitudes that should scale with the icon rather
    // than with whatever units the source model happened to use.
    float mnY = base[1], mxY = base[1];
    for (int v = 0; v < nv; ++v) {
        mnY = std::min(mnY, base[v * 3 + 1]);
        mxY = std::max(mxY, base[v * 3 + 1]);
    }
    const float height = std::max(0.001f, mxY - mnY);
    const float midY = (mnY + mxY) * 0.5f;
    const float amp = std::max(0.0f, amount);

    for (int k = 0; k < shapes; ++k) {
        const float t = (float)k / (float)shapes;  // 0..1 around the loop
        const float TAU = 6.2831853f;
        float* out = &g.pos[(size_t)k * nv * 3];
        for (int v = 0; v < nv; ++v) {
            float x = base[v * 3], y = base[v * 3 + 1], z = base[v * 3 + 2];
            switch (motion) {
                case 1: {  // bounce: one hop per loop, squashing on landing
                    // |sin| peaks mid-loop and touches down at both ends, so
                    // the contact frame IS the loop point - no visible seam.
                    const float h = std::fabs(sinf(3.14159265f * t));
                    const float squash = 1.0f - 0.16f * amp * (1.0f - h);
                    const float widen = 1.0f + 0.10f * amp * (1.0f - h);
                    y = midY + (y - midY) * squash;
                    y -= h * height * 0.30f * amp;  // up = -y
                    x *= widen;
                    z *= widen;
                    break;
                }
                case 2: {  // spin: a full turn, distributed over the shapes
                    const float a = TAU * t;
                    const float c = cosf(a), sn = sinf(a);
                    const float nx = x * c + z * sn;
                    z = -x * sn + z * c;
                    x = nx;
                    break;
                }
                case 3: {  // pulse: breathe about the model's centre
                    const float s = 1.0f + 0.12f * amp * sinf(TAU * t);
                    x *= s;
                    z *= s;
                    y = midY + (y - midY) * s;
                    break;
                }
                case 4: {  // tilt: rock about the feet, like a metronome
                    const float a = 0.20f * amp * sinf(TAU * t);
                    const float c = cosf(a), sn = sinf(a);
                    const float ry = y - mnY;  // pivot at the feet
                    const float nx = x * c - ry * sn;
                    y = mnY + (x * sn + ry * c);
                    x = nx;
                    break;
                }
                default: {  // 0 - sway: the original ~12.5 deg yaw rock
                    const float a = 0.22f * amp * sinf(TAU * t);
                    const float c = cosf(a), sn = sinf(a);
                    const float nx = x * c + z * sn;
                    z = -x * sn + z * c;
                    x = nx;
                    break;
                }
            }
            out[v * 3 + 0] = x;
            out[v * 3 + 1] = y;
            out[v * 3 + 2] = z;
        }
    }
}

// The motion a project asks for, and how many shapes it should get. "none"
// collapses to a single shape, which is also the smallest list.icn possible.
int motionShapes(int motion, int frames) {
    if (motion == 5) return 1;
    return std::max(1, std::min(frames, (int)kMaxIconShapes));
}

// The flat two-sided quad (the default icon and every fallback).
IconGeo quadGeo(const Project& p, int frames) {
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
    const int m = iconMotionIndex(p.saveIconMotion);
    applyMotion(g, motionShapes(m, frames), m, p.saveIconMotionAmount);
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
    const int mo = iconMotionIndex(p.saveIconMotion);
    applyMotion(g, motionShapes(mo, frames), mo, p.saveIconMotionAmount);
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
    // A .glb is not required to animate. With no clip there is nothing to
    // sample, so the model becomes a static source that takes the idle motion
    // like an .obj - and, less visibly, clips[0] is no longer read off an
    // empty vector.
    const bool hasClip = !baked.clips.empty();
    const glbparser::Clip* clip = hasClip ? &baked.clips[0] : nullptr;
    if (hasClip)
        for (const glbparser::Clip& c : baked.clips)
            if (!p.saveIconClip.empty() && c.name == p.saveIconClip) clip = &c;
    const int shapes =
        hasClip ? std::max(1, std::min({frames, clip->frameCount,
                                        (int)kMaxIconShapes}))
                : 1;
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
        const int f = hasClip ? clip->firstFrame +
                                    (int)((long long)k * clip->frameCount / shapes)
                              : 0;
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
        const int f0 = hasClip ? clip->firstFrame : 0;
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
    const std::string file = std::filesystem::path(rel).filename().string();
    if (hasClip) {
        info.source = file + " (" + clip->name + ")";
    } else {
        // No clip to play, so it animates like any other static source.
        const int m = iconMotionIndex(p.saveIconMotion);
        applyMotion(g, motionShapes(m, frames), m, p.saveIconMotionAmount);
        info.source = file + " (no clip)";
    }
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
    return quadGeo(p, frames);
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

std::vector<std::vector<unsigned char>> iconPreviewFrames(const Project& p,
                                                          int size) {
    IconInfo info;
    const IconGeo g = buildGeo(p, info);
    std::vector<std::vector<unsigned char>> frames;
    if (size <= 0 || g.verts() < 3) return frames;
    const int nv = g.verts();

    // ONE transform for every shape, derived from the bbox over ALL of them.
    // Per-shape fitting would rescale the model each frame and the sway would
    // read as breathing rather than turning.
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i + 2 < g.pos.size(); i += 3)
        for (int a = 0; a < 3; ++a) {
            mn[a] = std::min(mn[a], g.pos[i + a]);
            mx[a] = std::max(mx[a], g.pos[i + a]);
        }
    const float ext = std::max({mx[0] - mn[0], mx[1] - mn[1], 0.001f});
    const float sc = (float)size * 0.84f / ext;
    const float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f;
    const float half = (float)size * 0.5f;

    // Icon space is y-DOWN already (fitToIconSpace mirrors it), which is also
    // the image's row order - so y maps straight through with no second flip.
    // The camera sits on +z, so the larger z of two fragments is the nearer.
    const float L[3] = {-0.34f, -0.72f, 0.60f};

    for (int k = 0; k < g.shapes; ++k) {
        std::vector<unsigned char> img((size_t)size * size * 4);
        std::vector<float> zbuf((size_t)size * size, -1e30f);
        for (int y = 0; y < size; ++y) {  // the browser's dark backdrop
            const float t = (float)y / (float)(size > 1 ? size - 1 : 1);
            for (int x = 0; x < size; ++x) {
                unsigned char* px = &img[((size_t)y * size + x) * 4];
                px[0] = (unsigned char)(20 + 14 * t);
                px[1] = (unsigned char)(24 + 20 * t);
                px[2] = (unsigned char)(42 + 32 * t);
                px[3] = 255;
            }
        }
        const float* sp = &g.pos[(size_t)k * nv * 3];
        for (int t = 0; t + 2 < nv; t += 3) {
            float X[3], Y[3], Z[3];
            for (int c = 0; c < 3; ++c) {
                X[c] = (sp[(size_t)(t + c) * 3 + 0] - cx) * sc + half;
                Y[c] = (sp[(size_t)(t + c) * 3 + 1] - cy) * sc + half;
                Z[c] = sp[(size_t)(t + c) * 3 + 2];
            }
            const float area = (X[1] - X[0]) * (Y[2] - Y[0]) -
                               (X[2] - X[0]) * (Y[1] - Y[0]);
            if (std::fabs(area) < 1e-6f) continue;  // degenerate
            int x0 = (int)std::floor(std::min({X[0], X[1], X[2]}));
            int x1 = (int)std::ceil(std::max({X[0], X[1], X[2]}));
            int y0 = (int)std::floor(std::min({Y[0], Y[1], Y[2]}));
            int y1 = (int)std::ceil(std::max({Y[0], Y[1], Y[2]}));
            x0 = std::max(0, x0); y0 = std::max(0, y0);
            x1 = std::min(size - 1, x1); y1 = std::min(size - 1, y1);
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float px_ = (float)x + 0.5f, py = (float)y + 0.5f;
                    float w[3];
                    w[0] = ((X[1] - px_) * (Y[2] - py) -
                            (X[2] - px_) * (Y[1] - py)) / area;
                    w[1] = ((X[2] - px_) * (Y[0] - py) -
                            (X[0] - px_) * (Y[2] - py)) / area;
                    w[2] = 1.0f - w[0] - w[1];
                    if (w[0] < 0.0f || w[1] < 0.0f || w[2] < 0.0f) continue;
                    const float z = w[0] * Z[0] + w[1] * Z[1] + w[2] * Z[2];
                    float& zb = zbuf[(size_t)y * size + x];
                    if (z <= zb) continue;
                    zb = z;
                    // Normals and colours are per-vertex of shape 0; the sway
                    // only moves positions, so they are shared by every shape.
                    float n[3] = {0, 0, 0}, uv[2] = {0, 0};
                    float col[3] = {0, 0, 0};
                    for (int c = 0; c < 3; ++c) {
                        const size_t v = (size_t)(t + c);
                        for (int a = 0; a < 3; ++a) {
                            n[a] += w[c] * g.normal[v * 3 + a];
                            col[a] += w[c] * (float)g.rgba[v * 4 + a];
                        }
                        uv[0] += w[c] * g.uv[v * 2];
                        uv[1] += w[c] * g.uv[v * 2 + 1];
                    }
                    const float nl = std::sqrt(std::max(
                        1e-8f, n[0] * n[0] + n[1] * n[1] + n[2] * n[2]));
                    // abs(): icons are drawn two-sided, so a back face lights
                    // like a front one instead of going flat black.
                    const float d = std::fabs((n[0] * L[0] + n[1] * L[1] +
                                               n[2] * L[2]) / nl);
                    const float shade = 0.34f + 0.66f * d;
                    float tex[3] = {255.0f, 255.0f, 255.0f};
                    if (g.texture.size() >= 128 * 128 * 4) {
                        auto wrap = [](float u) {
                            u -= std::floor(u);
                            return std::min(127, std::max(0, (int)(u * 128.0f)));
                        };
                        const size_t ti =
                            ((size_t)wrap(uv[1]) * 128 + wrap(uv[0])) * 4;
                        for (int a = 0; a < 3; ++a)
                            tex[a] = (float)g.texture[ti + a];
                    }
                    unsigned char* px = &img[((size_t)y * size + x) * 4];
                    for (int a = 0; a < 3; ++a) {
                        const float v = col[a] * tex[a] / 255.0f * shade;
                        px[a] = (unsigned char)std::min(255.0f, std::max(0.0f, v));
                    }
                    px[3] = 255;
                }
        }
        frames.push_back(std::move(img));
    }
    return frames;
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
