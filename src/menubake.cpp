#include "menubake.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_image.h>  // implementation lives in app.cpp

#include <filesystem>

#include "platform.hpp"

namespace menubake {

namespace {

// Panel palette - matches the built-in save menu sprites (save_assets.cpp).
struct RGBA {
    unsigned char r, g, b, a;
};
constexpr RGBA kBg{10, 14, 28, 225};
constexpr RGBA kText{235, 240, 245, 255};
constexpr RGBA kDim{150, 160, 175, 255};

// A loaded TTF (cached for the process lifetime, keyed by file path - bakes
// happen per build and per preview refresh, re-reading fonts every time
// would be wasteful).
struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

Font* loadFontFile(const std::string& path) {
    static std::map<std::string, Font> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.ok ? &it->second : nullptr;
    Font& f = cache[path];
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return nullptr;
    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    f.data.resize((size_t)size);
    const size_t got = std::fread(f.data.data(), 1, (size_t)size, fp);
    std::fclose(fp);
    if (got != (size_t)size) return nullptr;
    if (!stbtt_InitFont(&f.info, f.data.data(),
                        stbtt_GetFontOffsetForIndex(f.data.data(), 0)))
        return nullptr;
    f.ok = true;
    return &f;
}

// A font by path: project-relative when the path has a separator
// ("res/fonts/x.ttf"), a system font by bare file name ("impact.ttf"), falling
// back to the platform's default chain when unset or unreadable. A project
// authored on another OS therefore keeps working: its bare name simply does
// not resolve here and the fallback takes over, rather than the bake failing.
Font* resolveFontPath(const std::string& fontPath, const std::string& projectDir) {
    if (!fontPath.empty()) {
        const bool projectRelative =
            fontPath.find('/') != std::string::npos ||
            fontPath.find('\\') != std::string::npos;
        const std::string full =
            projectRelative && !projectDir.empty()
                ? (std::filesystem::path(projectDir) / fontPath).string()
                : platform::systemFontPath(fontPath);
        if (Font* f = loadFontFile(full)) return f;
    }
    for (const std::string& name : platform::fallbackFontFiles())
        if (Font* f = loadFontFile(platform::systemFontPath(name))) return f;
    return nullptr;
}

// The TTF behind a Project::fonts entry. An empty or stale name resolves to
// the default entry (Project::findFont), so deleting a font never breaks the
// texts still naming it - they fall back instead of failing the bake.
Font* resolveFontNamed(const Project& p, const std::string& fontName) {
    const GameFont* gf = p.findFont(fontName);
    return resolveFontPath(gf ? gf->fontPath : std::string(), p.dir);
}

Font* resolveFont(const GameMenu& menu, const Project& p) {
    return resolveFontNamed(p, menu.font);
}

// Minimal UTF-8 decode (labels may hold Polish diacritics, hints use U+25B2).
int nextCodepoint(const std::string& s, size_t& i) {
    const unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { i += 1; return c; }
    if ((c >> 5) == 0x6 && i + 1 < s.size()) {
        const int cp = ((c & 0x1F) << 6) | ((unsigned char)s[i + 1] & 0x3F);
        i += 2;
        return cp;
    }
    if ((c >> 4) == 0xE && i + 2 < s.size()) {
        const int cp = ((c & 0x0F) << 12) | (((unsigned char)s[i + 1] & 0x3F) << 6) |
                       ((unsigned char)s[i + 2] & 0x3F);
        i += 3;
        return cp;
    }
    i += 1;  // 4-byte and malformed sequences: skip
    return '?';
}

struct Canvas {
    std::vector<unsigned char>* px;
    int w, h;

    void blend(int x, int y, RGBA c, unsigned char cov) {
        if (x < 0 || y < 0 || x >= w || y >= h || cov == 0) return;
        unsigned char* p = px->data() + (size_t)(y * w + x) * 4;
        const int a = c.a * cov / 255;
        p[0] = (unsigned char)((c.r * a + p[0] * (255 - a)) / 255);
        p[1] = (unsigned char)((c.g * a + p[1] * (255 - a)) / 255);
        p[2] = (unsigned char)((c.b * a + p[2] * (255 - a)) / 255);
        const int outA = a + p[3] * (255 - a) / 255;
        p[3] = (unsigned char)outA;
    }

    void fillRect(int x0, int y0, int x1, int y1, RGBA c) {
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) blend(x, y, c, 255);
    }
};

// --- Inline text icons ------------------------------------------------------
// Every baked text goes through textWidth/drawText below, so teaching those two
// about `{{name}}` placeholders (docs/text-icons.md) gives menus, HUD texts,
// loading screens and value strips icon support at once. Passing proj = null
// (the default) keeps a call site pure text - the built-in icon LABELS use that,
// so an icon can never recurse into itself.

// A decoded icon, cached for the process: bakes and editor previews ask for the
// same handful of icons over and over. Keyed by name; clearIconImageCache()
// drops it when the UI Editor repoints or regenerates one.
struct IconImg {
    std::vector<unsigned char> px;  // RGBA
    int w = 0, h = 0;
};

std::map<std::string, IconImg>& iconImgCache() {
    static std::map<std::string, IconImg> c;
    return c;
}

// The image for a TextIcon name: the project's PNG if it has one, else the
// built-in drawing. Null when neither exists (the token then stays literal).
const IconImg* iconImage(const Project& p, const std::string& name) {
    auto& cache = iconImgCache();
    auto it = cache.find(name);
    if (it != cache.end()) return it->second.w > 0 ? &it->second : nullptr;

    const TextIcon* icon = nullptr;
    for (const TextIcon& ic : p.textIcons)
        if (ic.name == name) { icon = &ic; break; }
    IconImg& img = cache[name];
    if (!icon) return nullptr;
    if (!icon->path.empty() && !p.dir.empty()) {
        const std::string full = p.dir + "\\" + icon->path;
        int w = 0, h = 0, comp = 0;
        if (unsigned char* px = stbi_load(full.c_str(), &w, &h, &comp, 4)) {
            img.px.assign(px, px + (size_t)w * h * 4);
            img.w = w;
            img.h = h;
            stbi_image_free(px);
            return &img;
        }
    }
    // No file yet (a fresh project before the first build): draw the built-in.
    if (bakeBuiltinIconRGBA(name, kIconBakeSize, img.px)) {
        img.w = img.h = kIconBakeSize;
        return &img;
    }
    img.px.clear();
    img.w = img.h = 0;
    return nullptr;
}

// Drawn height + advance of an icon inside text of `pixelHeight`. Icons are
// square and sit on the cap height, with a small gap so they never touch a
// neighbouring glyph.
float iconAdvance(const Project& p, const std::string& name, float pixelHeight) {
    float scale = 1.0f;
    for (const TextIcon& ic : p.textIcons)
        if (ic.name == name) { scale = ic.scale; break; }
    return pixelHeight * scale + pixelHeight * 0.12f;
}

// The runs of a text: plain when proj is null, icon-aware otherwise.
std::vector<TextRun> textRuns(const Project* proj, const std::string& text) {
    if (!proj) return {TextRun{text, ""}};
    std::vector<TextRun> runs = parseTextIcons(text, proj->input);
    for (TextRun& r : runs) {
        if (r.icon.empty() || iconImage(*proj, r.icon)) continue;
        // Not an icon name: give it one more chance as an ACTION name, which is
        // the `{{use}}` shorthand for `{{action:use}}`.
        const std::string viaAction = textIconForAction(r.icon, proj->input);
        if (!viaAction.empty() && iconImage(*proj, viaAction)) {
            r.action = r.icon;  // the token WAS an action - remember which
            r.icon = viaAction;
            continue;
        }
        // Neither: stay visible as text, so a typo shows up on screen instead
        // of silently vanishing.
        r.text = "{{" + r.icon + "}}";
        r.icon.clear();
    }
    return runs;
}

float textWidth(Font& f, const std::string& text, float pixelHeight,
                const Project* proj = nullptr) {
    const float scale = stbtt_ScaleForPixelHeight(&f.info, pixelHeight);
    float x = 0;
    for (const TextRun& run : textRuns(proj, text)) {
        if (!run.icon.empty()) {
            x += iconAdvance(*proj, run.icon, pixelHeight);
            continue;
        }
        size_t i = 0;
        int prev = 0;
        while (i < run.text.size()) {
            const int cp = nextCodepoint(run.text, i);
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
            if (prev) x += scale * stbtt_GetCodepointKernAdvance(&f.info, prev, cp);
            x += scale * adv;
            prev = cp;
        }
    }
    return x;
}

// Draws text with its baseline placed so the cap sits around y (top of the
// line box); x is the left edge, or the center when centered = true. With proj
// set, `{{name}}` placeholders draw the icon tinted in `color`.
// skipIcons: advance past icon tokens without drawing them. The drop-shadow
// pass of a HUD text uses it - the icons carry their own colors, so a dark
// offset copy underneath would only leak a colored fringe around them.
// skipActionIcons: skip only the tokens that resolved through an ACTION and
// leave every other icon composited in. That is what a prompt bakes with: the
// action glyphs become live slots the game fills, while a plain `{{cross}}` in
// the same string is not rebindable and has no reason to cost a runtime quad.
void drawText(Canvas& canvas, Font& f, float x, int yTop, const std::string& text,
              float pixelHeight, RGBA color, bool centered,
              const Project* proj = nullptr, bool skipIcons = false,
              bool skipActionIcons = false) {
    const float scale = stbtt_ScaleForPixelHeight(&f.info, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &lineGap);
    const int baseline = yTop + (int)(ascent * scale + 0.5f);
    const int capH = (int)(ascent * scale + 0.5f);

    if (centered) x -= textWidth(f, text, pixelHeight, proj) * 0.5f;

    float penX = x;
    for (const TextRun& run : textRuns(proj, text)) {
        if (!run.icon.empty()) {
            const IconImg* img = iconImage(*proj, run.icon);
            const float adv = iconAdvance(*proj, run.icon, pixelHeight);
            const float box = adv - pixelHeight * 0.12f;
            const bool skip =
                skipIcons || (skipActionIcons && !run.action.empty());
            if (img && !skip) {
                const int side = (int)(box + 0.5f);
                const int ox = (int)(penX + pixelHeight * 0.06f + 0.5f);
                const int oy = yTop + (capH - side) / 2;
                // Icons keep their OWN colors (the face buttons are the
                // DualShock palette, and a user PNG is whatever they drew), so
                // unlike glyphs they are not tinted with the text color.
                for (int y = 0; y < side; ++y)
                    for (int xx = 0; xx < side; ++xx) {
                        const float u = (xx + 0.5f) * img->w / side;
                        const float v = (y + 0.5f) * img->h / side;
                        int sx = (int)u, sy = (int)v;
                        if (sx < 0) sx = 0;
                        if (sy < 0) sy = 0;
                        if (sx >= img->w) sx = img->w - 1;
                        if (sy >= img->h) sy = img->h - 1;
                        const unsigned char* q =
                            &img->px[((size_t)sy * img->w + sx) * 4];
                        canvas.blend(ox + xx, oy + y, RGBA{q[0], q[1], q[2], 255},
                                     q[3]);
                    }
            }
            penX += adv;
            continue;
        }
        size_t i = 0;
        int prev = 0;
        while (i < run.text.size()) {
            const int cp = nextCodepoint(run.text, i);
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
            if (prev)
                penX += scale * stbtt_GetCodepointKernAdvance(&f.info, prev, cp);

            int gw = 0, gh = 0, gx = 0, gy = 0;
            unsigned char* bitmap = stbtt_GetCodepointBitmap(
                &f.info, scale, scale, cp, &gw, &gh, &gx, &gy);
            if (bitmap) {
                const int ox = (int)(penX + 0.5f) + gx;
                const int oy = baseline + gy;
                for (int by = 0; by < gh; ++by)
                    for (int bx = 0; bx < gw; ++bx)
                        canvas.blend(ox + bx, oy + by, color,
                                     bitmap[by * gw + bx]);
                stbtt_FreeBitmap(bitmap, nullptr);
            }
            penX += scale * adv;
            prev = cp;
        }
    }
}

void pngWriteCallback(void* context, void* data, int size) {
    auto* out = (std::vector<unsigned char>*)context;
    out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
}

// --- Built-in icon drawing --------------------------------------------------
// Analytic coverage, 4x4 supersampled: the icons are generated once per project
// at a fixed size and then scaled by every consumer, so a clean edge here is
// worth more than speed. All of them draw white; the caller tints.

// Signed distance to a line segment, used for every stroke (the X, the d-pad
// arms, the triangle edges).
float segDist(float px, float py, float ax, float ay, float bx, float by) {
    const float vx = bx - ax, vy = by - ay;
    const float len2 = vx * vx + vy * vy;
    float t = len2 > 0.0f ? ((px - ax) * vx + (py - ay) * vy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
    return std::sqrt(dx * dx + dy * dy);
}

// Coverage of a shape described by a signed-distance function: inside = d < 0.
// 4x4 samples per pixel, so a diagonal stroke lands soft instead of jagged.
// Writes `col` where it covers - the face buttons carry the DualShock colors, so
// unlike font glyphs these pixels are NOT tinted by the text they sit in.
template <typename Sdf>
void fillSdf(std::vector<unsigned char>& px, int size, Sdf sdf, RGBA col) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const float fx = (float)x + (sx + 0.5f) / 4.0f;
                    const float fy = (float)y + (sy + 0.5f) / 4.0f;
                    if (sdf(fx, fy) < 0.0f) ++hits;
                }
            if (hits == 0) continue;
            const int cov = hits * 255 / 16;
            unsigned char* p = px.data() + (size_t)(y * size + x) * 4;
            const int a = cov > p[3] ? cov : p[3];  // union with what is there
            p[0] = col.r;
            p[1] = col.g;
            p[2] = col.b;
            p[3] = (unsigned char)a;
        }
}

// Rounded-rect distance (r = corner radius); negative inside.
float roundRectDist(float px, float py, float cx, float cy, float hw, float hh,
                    float r) {
    const float dx = std::fabs(px - cx) - (hw - r);
    const float dy = std::fabs(py - cy) - (hh - r);
    const float ax = dx > 0.0f ? dx : 0.0f;
    const float ay = dy > 0.0f ? dy : 0.0f;
    const float outside = std::sqrt(ax * ax + ay * ay);
    const float inside = (dx > dy ? dx : dy);
    return (outside > 0.0f ? outside : inside) - r;
}

// The DualShock symbol colors. Only the four face buttons are colored on a real
// pad; the shoulder, d-pad and Start/Select glyphs are plain light grey, which
// also keeps them readable on any menu background.
constexpr RGBA kIconWhite{240, 244, 250, 255};
constexpr RGBA kIconCross{104, 138, 225, 255};     // blue
constexpr RGBA kIconCircle{228, 84, 92, 255};      // red
constexpr RGBA kIconSquare{224, 118, 186, 255};    // pink
constexpr RGBA kIconTriangle{92, 202, 168, 255};   // green

// Draws a label centered in the icon (shoulder buttons, Start/Select). Uses the
// default font chain - these icons ship with the editor, not with a project, so
// they must not depend on a project font.
void drawIconLabel(std::vector<unsigned char>& px, int size,
                   const char* label, float heightFrac) {
    Font* f = resolveFontPath("", "");
    if (!f) return;
    Canvas canvas{&px, size, size};
    const float pixels = size * heightFrac;
    const float w = textWidth(*f, label, pixels);
    int ascent = 0, descent = 0, lineGap = 0;
    const float scale = stbtt_ScaleForPixelHeight(&f->info, pixels);
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &lineGap);
    const int capH = (int)(ascent * scale + 0.5f);
    drawText(canvas, *f, (size - w) * 0.5f, (size - capH) / 2, label, pixels,
             kIconWhite, false);
}

}  // namespace

const std::vector<std::string>& builtinIconNames() {
    // The pad buttons, in kPadButtonNames order (input.hpp) - the order the
    // Input Map lists buttons in, so the two windows read the same way.
    static const std::vector<std::string> v = [] {
        std::vector<std::string> out;
        for (int i = 0; i < 16; ++i)
            out.push_back(textIconNameForPad(kPadButtonNames[i]));
        return out;
    }();
    return v;
}

bool bakeBuiltinIconRGBA(const std::string& name, int px,
                         std::vector<unsigned char>& out) {
    if (px < 8 || px > 256) return false;
    bool known = false;
    for (const std::string& n : builtinIconNames()) known |= (n == name);
    if (!known) return false;

    out.assign((size_t)px * px * 4, 0);
    const float s = (float)px;
    const float c = s * 0.5f;
    // Face buttons and the d-pad sit in a ring; the shoulder/Start/Select ones
    // are plates, which is how a real pad reads at a glance.
    const float ringR = s * 0.44f;
    const float ringT = s * 0.075f;  // ring stroke half-width
    auto ring = [&](float x, float y) {
        return std::fabs(std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) -
                         ringR) -
               ringT;
    };
    // Inner glyphs are drawn thinner than the ring: at 16px on a TV the ring
    // carries the shape and a fat inner stroke just fills the hole in.
    // Thin: the inner shapes are OUTLINES, and at this radius a fat stroke
    // closes the hole and turns the circle into a bullseye.
    const float stroke = s * 0.042f;  // inner glyph stroke half-width
    const float gr = s * 0.23f;       // inner glyph "radius" (axis-aligned)
    // Clearance rule: an inner glyph must stop short of the ring's inner edge
    // (ringR - ringT). A shape whose extreme points are DIAGONAL reaches
    // sqrt(2) further than its radius suggests, so the X and the square use a
    // smaller one - at gr they touched the ring and read as one blob.
    const float diagR = s * 0.16f;   // X arms (the widest diagonal reach)
    const float sqR = s * 0.185f;    // square half-side

    if (name == "cross") {
        fillSdf(out, px, ring, kIconCross);
        fillSdf(out, px, [&](float x, float y) {
            const float a = segDist(x, y, c - diagR, c - diagR, c + diagR,
                                    c + diagR);
            const float b = segDist(x, y, c - diagR, c + diagR, c + diagR,
                                    c - diagR);
            return (a < b ? a : b) - stroke * 1.2f;
        }, kIconCross);
    } else if (name == "circle") {
        fillSdf(out, px, ring, kIconCircle);
        fillSdf(out, px, [&](float x, float y) {
            return std::fabs(std::sqrt((x - c) * (x - c) + (y - c) * (y - c)) -
                             gr * 0.91f) -
                   stroke;
        }, kIconCircle);
    } else if (name == "square") {
        fillSdf(out, px, ring, kIconSquare);
        fillSdf(out, px, [&](float x, float y) {
            return std::fabs(roundRectDist(x, y, c, c, sqR, sqR, s * 0.02f)) -
                   stroke;
        }, kIconSquare);
    } else if (name == "triangle") {
        fillSdf(out, px, ring, kIconTriangle);
        fillSdf(out, px, [&](float x, float y) {
            // equilateral-ish, sitting on its base
            const float tr = gr * 0.92f;
            const float tx = c, ty = c - tr * 1.12f;
            const float bl = c - tr, br = c + tr, by = c + tr * 0.72f;
            const float d1 = segDist(x, y, tx, ty, br, by);
            const float d2 = segDist(x, y, br, by, bl, by);
            const float d3 = segDist(x, y, bl, by, tx, ty);
            const float d = d1 < d2 ? (d1 < d3 ? d1 : d3) : (d2 < d3 ? d2 : d3);
            return d - stroke;
        }, kIconTriangle);
    } else if (name.rfind("dpad", 0) == 0) {
        fillSdf(out, px, ring, kIconWhite);
        // A solid ARROW, not a cross with a highlighted arm: the four
        // directions have to be told apart at 16px on a TV, and a marked arm
        // reads as the same plus sign in all four icons. A bare "dpad" (no
        // direction) keeps the cross.
        int dx = 0, dy = 0;
        if (name == "dpadup") dy = -1;
        else if (name == "dpaddown") dy = 1;
        else if (name == "dpadleft") dx = -1;
        else if (name == "dpadright") dx = 1;
        if (dx == 0 && dy == 0) {
            fillSdf(out, px, [&](float x, float y) {
                const float a = segDist(x, y, c - gr, c, c + gr, c);
                const float b = segDist(x, y, c, c - gr, c, c + gr);
                return (a < b ? a : b) - s * 0.055f;
            }, kIconWhite);
        } else {
            // Filled triangle: tip along (dx,dy), base perpendicular to it.
            const float tipX = c + dx * gr, tipY = c + dy * gr;
            const float baseX = c - dx * gr * 0.75f, baseY = c - dy * gr * 0.75f;
            const float px1 = baseX - dy * gr * 0.85f;
            const float py1 = baseY - dx * gr * 0.85f;
            const float px2 = baseX + dy * gr * 0.85f;
            const float py2 = baseY + dx * gr * 0.85f;
            fillSdf(out, px, [&](float x, float y) {
                // Inside test by consistent winding, then a soft edge from the
                // nearest edge distance (all three edges wind the same way).
                auto side = [&](float ax, float ay, float bx, float by) {
                    return (bx - ax) * (y - ay) - (by - ay) * (x - ax);
                };
                const float s1 = side(tipX, tipY, px1, py1);
                const float s2 = side(px1, py1, px2, py2);
                const float s3 = side(px2, py2, tipX, tipY);
                const bool in = (s1 >= 0 && s2 >= 0 && s3 >= 0) ||
                                (s1 <= 0 && s2 <= 0 && s3 <= 0);
                return in ? -1.0f : 1.0f;
            }, kIconWhite);
        }
    } else {
        // L1/L2/L3/R1/R2/R3/Start/Select: the LABEL ONLY, no plate around it.
        // These draw at text height (15-24px on a TV), where a border stole so
        // much of the box that the letters inside it were unreadable - the label
        // alone can be half again as tall and actually says "R2".
        std::string label = name;
        for (char& ch : label) ch = (char)toupper((unsigned char)ch);
        if (label == "START") label = "STA";
        if (label == "SELECT") label = "SEL";
        drawIconLabel(out, px, label.c_str(),
                      label.size() >= 3 ? 0.52f : 0.74f);
    }
    return true;
}

bool bakeBuiltinIconPNG(const std::string& name, int px,
                        std::vector<unsigned char>& png) {
    std::vector<unsigned char> rgba;
    if (!bakeBuiltinIconRGBA(name, px, rgba)) return false;
    png.clear();
    stbi_write_png_to_func(pngWriteCallback, &png, px, px, 4, rgba.data(), px * 4);
    return !png.empty();
}

namespace {

// Fitted (drawn) size of every menu image, PNG headers only (stbi_info) -
// the full decode happens at bake time. Index-aligned with menu.images;
// {0,0} = missing/unreadable file.
struct FittedImage {
    int w = 0, h = 0;
};

std::vector<FittedImage> fitImages(const GameMenu& menu,
                                   const std::string& projectDir) {
    std::vector<FittedImage> out(menu.images.size());
    if (projectDir.empty()) return out;
    const int panelW = menu.panelW;
    for (size_t i = 0; i < menu.images.size(); ++i) {
        const MenuImage& mi = menu.images[i];
        if (mi.path.empty()) continue;
        int w = 0, h = 0, comp = 0;
        const std::string full =
            (std::filesystem::path(projectDir) / mi.path).string();
        if (!stbi_info(full.c_str(), &w, &h, &comp) || w <= 0 || h <= 0) continue;
        if (mi.slot == MenuImage::Background) {
            out[i] = {panelW, 0};  // stretched over the content at bake time
            continue;
        }
        // fit to the panel (downscale only), then the user scale on top,
        // then hard caps so one image cannot blow past the texture limit
        const float maxW = (float)(panelW - (mi.slot == MenuImage::Overlay ? 0 : 32));
        float s = 1.0f;
        if (w > maxW) s = maxW / w;
        s *= (mi.scale > 0.05f ? mi.scale : 0.05f);
        float fw = w * s, fh = h * s;
        if (fw > panelW) { fh *= panelW / fw; fw = (float)panelW; }
        if (fh > 320.0f) { fw *= 320.0f / fh; fh = 320.0f; }
        out[i] = {(int)fw, (int)fh};
    }
    return out;
}

int flowBlockHeight(const GameMenu& menu, const std::vector<FittedImage>& fit,
                    int slot) {
    int h = 0;
    for (size_t i = 0; i < menu.images.size(); ++i)
        if (menu.images[i].slot == slot && fit[i].h > 0) h += fit[i].h + 8;
    return h;
}

}  // namespace

PanelLayout panelLayout(const GameMenu& menu, const Project& p) {
    PanelLayout l;
    l.panelW = (menu.panelW == 128 || menu.panelW == 512) ? menu.panelW : 256;
    int entries = (int)menu.entries.size();
    if (entries < 0) entries = 0;
    if (entries > kMaxEntries) entries = kMaxEntries;

    const auto fit = fitImages(menu, p.dir);
    const int aboveTitle = flowBlockHeight(menu, fit, MenuImage::AboveTitle);
    const int aboveEntries = flowBlockHeight(menu, fit, MenuImage::AboveEntries);
    const int belowEntries = flowBlockHeight(menu, fit, MenuImage::BelowEntries);

    // text sizes drive the geometry: title block = text at +8 + separator,
    // row pitch = entry size + breathing room (15px -> the classic 24)
    l.rowH = menu.entrySize + 9;
    const int titleBlock = menu.showTitle ? menu.titleSize + 26 : 10;
    l.row0Y = aboveTitle + titleBlock + aboveEntries;
    l.contentH = l.row0Y + entries * l.rowH + belowEntries + 22;  // hints + pad
    if (l.contentH > 512) {
        l.contentH = 512;  // PS2 texture cap - the bake clips, editor warns
        l.clipped = true;
    }
    l.canvasH = 64;
    while (l.canvasH < l.contentH) l.canvasH *= 2;
    return l;
}

namespace {
std::string sanitizeName(const std::string& name, const char* fallback) {
    std::string s;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            s += (char)tolower((unsigned char)c);
        else
            s += '-';
    }
    if (s.empty()) s = fallback;
    return s;
}
}  // namespace

std::string panelFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + ".png";
}

std::string valueStripFileName(const std::string& menuName) {
    return sanitizeName(menuName, "menu") + "-values.png";
}

std::string textFileName(const std::string& textName) {
    return "text-" + sanitizeName(textName, "text") + ".png";
}

std::string iconFileName(const std::string& iconName) {
    return "icon-" + sanitizeName(iconName, "icon") + ".png";
}

void clearIconImageCache() { iconImgCache().clear(); }

// Bilinear-sample `src` (sw x sh RGBA) into the canvas rect - used for the
// custom menu image in both placements.
static void drawImageScaled(Canvas& canvas, const unsigned char* src, int sw,
                            int sh, int dx, int dy, int dw, int dh) {
    if (dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const float v = (y + 0.5f) * sh / dh - 0.5f;
        const int y0 = v < 0 ? 0 : (int)v;
        const int y1 = y0 + 1 >= sh ? sh - 1 : y0 + 1;
        const float fy = v - y0 < 0 ? 0 : v - y0;
        for (int x = 0; x < dw; ++x) {
            const float u = (x + 0.5f) * sw / dw - 0.5f;
            const int x0 = u < 0 ? 0 : (int)u;
            const int x1 = x0 + 1 >= sw ? sw - 1 : x0 + 1;
            const float fx = u - x0 < 0 ? 0 : u - x0;
            float px[4];
            for (int c = 0; c < 4; ++c) {
                const float t0 = src[(y0 * sw + x0) * 4 + c] * (1 - fx) +
                                 src[(y0 * sw + x1) * 4 + c] * fx;
                const float t1 = src[(y1 * sw + x0) * 4 + c] * (1 - fx) +
                                 src[(y1 * sw + x1) * 4 + c] * fx;
                px[c] = t0 * (1 - fy) + t1 * fy;
            }
            canvas.blend(dx + x, dy + y,
                         RGBA{(unsigned char)px[0], (unsigned char)px[1],
                              (unsigned char)px[2], (unsigned char)px[3]},
                         255);
        }
    }
}

bool bakePanelRGBA(const GameMenu& menu, const Project& p,
                   std::vector<unsigned char>& out, int& w, int& h) {
    Font* font = resolveFont(menu, p);
    if (!font) return false;

    const int entries = (int)menu.entries.size() > kMaxEntries
                            ? kMaxEntries
                            : (int)menu.entries.size();
    const PanelLayout l = panelLayout(menu, p);
    const auto fit = fitImages(menu, p.dir);
    w = l.panelW;
    h = l.canvasH;
    const int content = l.contentH;
    out.assign((size_t)w * h * 4, 0);
    Canvas canvas{&out, w, h};

    auto clamp255 = [](float v) {
        return (unsigned char)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f);
    };
    const RGBA accent{clamp255(menu.accent[0]), clamp255(menu.accent[1]),
                      clamp255(menu.accent[2]), 255};
    const RGBA separator{70, 90, 120, 255};

    // Decoded fresh per bake - bakes are rare. Index-aligned with images.
    std::vector<unsigned char*> pixels(menu.images.size(), nullptr);
    std::vector<int> srcW(menu.images.size(), 0), srcH(menu.images.size(), 0);
    for (size_t i = 0; i < menu.images.size(); ++i) {
        if (menu.images[i].path.empty() || p.dir.empty()) continue;
        int comp = 0;
        const std::string full = p.filePath(menu.images[i].path);
        pixels[i] = stbi_load(full.c_str(), &srcW[i], &srcH[i], &comp, 4);
    }

    // Draws every image of a flow slot at the running y cursor (centered,
    // nudged by its offset), advancing the cursor by each block.
    auto drawFlowSlot = [&](int slot, int& y) {
        for (size_t i = 0; i < menu.images.size(); ++i) {
            const MenuImage& mi = menu.images[i];
            if (mi.slot != slot || !pixels[i] || fit[i].h <= 0) continue;
            drawImageScaled(canvas, pixels[i], srcW[i], srcH[i],
                            (w - fit[i].w) / 2 + (int)mi.offset[0],
                            y + (int)mi.offset[1], fit[i].w, fit[i].h);
            y += fit[i].h + 8;
        }
    };

    // background layer(s) under everything, dark wash for text contrast
    bool hasBackground = false;
    for (size_t i = 0; i < menu.images.size(); ++i) {
        if (menu.images[i].slot != MenuImage::Background || !pixels[i]) continue;
        drawImageScaled(canvas, pixels[i], srcW[i], srcH[i], 0, 0, w, content);
        hasBackground = true;
    }
    if (hasBackground) {
        RGBA wash = kBg;
        wash.a = 150;
        canvas.fillRect(0, 0, w, content, wash);
    } else {
        canvas.fillRect(0, 0, w, content, kBg);
    }

    canvas.fillRect(0, 0, w, 2, accent);  // border
    canvas.fillRect(0, content - 2, w, content, accent);
    canvas.fillRect(0, 0, 2, content, accent);
    canvas.fillRect(w - 2, 0, w, content, accent);

    int y = 8;
    drawFlowSlot(MenuImage::AboveTitle, y);
    if (menu.showTitle) {
        drawText(canvas, *font, w * 0.5f, y, menu.title, (float)menu.titleSize,
                 accent, true, &p);
        canvas.fillRect(16, y + menu.titleSize + 5, w - 16, y + menu.titleSize + 6,
                        separator);
        y += menu.titleSize + 18;
    } else {
        y += 2;
    }
    drawFlowSlot(MenuImage::AboveEntries, y);

    for (int i = 0; i < entries; ++i)
        drawText(canvas, *font, 56, l.row0Y + i * l.rowH + 2, menu.entries[i].label,
                 (float)menu.entrySize, kText, false, &p);

    int below = l.row0Y + entries * l.rowH + 4;
    drawFlowSlot(MenuImage::BelowEntries, below);

    drawText(canvas, *font, w * 0.5f, content - 18, "X OK    \xE2\x96\xB2 BACK",
             11.0f, kDim, true);

    // overlays: in front of everything, freeform top-left position
    for (size_t i = 0; i < menu.images.size(); ++i) {
        const MenuImage& mi = menu.images[i];
        if (mi.slot != MenuImage::Overlay || !pixels[i] || fit[i].h <= 0) continue;
        drawImageScaled(canvas, pixels[i], srcW[i], srcH[i], (int)mi.offset[0],
                        (int)mi.offset[1], fit[i].w, fit[i].h);
    }

    for (unsigned char* p : pixels)
        if (p) stbi_image_free(p);
    return true;
}

bool bakePanelPNG(const GameMenu& menu, const Project& p,
                  std::vector<unsigned char>& png) {
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!bakePanelRGBA(menu, p, rgba, w, h)) return false;
    png.clear();
    return stbi_write_png_to_func(pngWriteCallback, &png, w, h, 4, rgba.data(),
                                  w * 4) != 0;
}

// --- Toggle / Choice value strips --------------------------------------------

std::vector<std::string> entryOptionLabels(const MenuEntry& entry) {
    std::vector<std::string> labels;
    if (entry.action == MenuEntry::Toggle) {
        labels = entry.options;
        if (labels.size() < 2) labels = {"Off", "On"};
    } else if (entry.action == MenuEntry::Choice) {
        labels = entry.options;
        if (labels.empty()) labels = {"-"};
    }
    if ((int)labels.size() > kMaxOptions) labels.resize(kMaxOptions);
    return labels;
}

bool menuHasValueEntries(const GameMenu& menu) {
    const int entries = (int)menu.entries.size() > kMaxEntries
                            ? kMaxEntries
                            : (int)menu.entries.size();
    for (int i = 0; i < entries; ++i)
        if (menu.entries[i].action == MenuEntry::Toggle ||
            menu.entries[i].action == MenuEntry::Choice)
            return true;
    return false;
}

ValueStripLayout valueStripLayout(const GameMenu& menu) {
    ValueStripLayout l;
    // Narrow panels get a narrow strip so the value cannot cover the label.
    const int panelW = (menu.panelW == 128 || menu.panelW == 512) ? menu.panelW : 256;
    l.cellW = panelW == 128 ? 64 : 128;
    l.cellH = menu.entrySize + 9;  // = PanelLayout::rowH
    l.pitch = l.cellH + 8;         // transparent gap - bilinear bleed guard
    const int entries = (int)menu.entries.size() > kMaxEntries
                            ? kMaxEntries
                            : (int)menu.entries.size();
    l.firstCell.assign((size_t)entries, -1);
    for (int i = 0; i < entries; ++i) {
        const auto labels = entryOptionLabels(menu.entries[i]);
        if (labels.empty()) continue;
        if ((l.cells + (int)labels.size()) * l.pitch > 512) {
            l.clipped = true;  // cap: cells past 512px would assert on the PS2
            continue;
        }
        l.firstCell[i] = l.cells;
        l.cells += (int)labels.size();
    }
    l.canvasH = 64;
    while (l.canvasH < l.cells * l.pitch) l.canvasH *= 2;
    if (l.canvasH > 512) l.canvasH = 512;
    return l;
}

bool bakeValueStripRGBA(const GameMenu& menu, const Project& p,
                        std::vector<unsigned char>& out, int& w, int& h) {
    if (!menuHasValueEntries(menu)) return false;
    Font* font = resolveFont(menu, p);
    if (!font) return false;

    const ValueStripLayout l = valueStripLayout(menu);
    w = l.cellW;
    h = l.canvasH;
    out.assign((size_t)w * h * 4, 0);
    Canvas canvas{&out, w, h};

    const int entries = (int)l.firstCell.size();
    for (int i = 0; i < entries; ++i) {
        if (l.firstCell[i] < 0) continue;
        const auto labels = entryOptionLabels(menu.entries[i]);
        for (int o = 0; o < (int)labels.size(); ++o) {
            // Right-aligned inside the cell (inset 4px), same y offset as the
            // entry labels in the panel rows (drawText's yTop + 2).
            const int top = (l.firstCell[i] + o) * l.pitch;
            const float tw =
                textWidth(*font, labels[o], (float)menu.entrySize, &p);
            drawText(canvas, *font, (float)(l.cellW - 4) - tw, top + 2, labels[o],
                     (float)menu.entrySize, kText, false, &p);
        }
    }
    return true;
}

bool bakeValueStripPNG(const GameMenu& menu, const Project& p,
                       std::vector<unsigned char>& png) {
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!bakeValueStripRGBA(menu, p, rgba, w, h)) return false;
    png.clear();
    return stbi_write_png_to_func(pngWriteCallback, &png, w, h, 4, rgba.data(),
                                  w * 4) != 0;
}

void overlayValuePreview(const GameMenu& menu, const Project& p,
                         const std::vector<int>& current,
                         std::vector<unsigned char>& rgba, int w, int h) {
    Font* font = resolveFont(menu, p);
    if (!font) return;
    const PanelLayout pl = panelLayout(menu, p);
    const ValueStripLayout vl = valueStripLayout(menu);
    Canvas canvas{&rgba, w, h};
    const int entries = (int)vl.firstCell.size();
    for (int i = 0; i < entries; ++i) {
        if (vl.firstCell[i] < 0) continue;
        const auto labels = entryOptionLabels(menu.entries[i]);
        int cur = i < (int)current.size() ? current[i] : 0;
        if (cur < 0) cur = 0;
        if (cur >= (int)labels.size()) cur = (int)labels.size() - 1;
        // The game places the cell's right edge 24px from the panel's right
        // border, text inset 4px -> right-aligned at panelW - 28.
        const float tw =
            textWidth(*font, labels[cur], (float)menu.entrySize, &p);
        drawText(canvas, *font, (float)(pl.panelW - 28) - tw,
                 pl.row0Y + i * pl.rowH + 2, labels[cur], (float)menu.entrySize,
                 kText, false, &p);
    }
}

// --- HUD texts ----------------------------------------------------------------

namespace {

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    lines.push_back(cur);
    while (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    return lines;
}

int pow2Dim(int v) {
    int d = 8;
    while (d < v && d < 512) d *= 2;
    return d;
}

}  // namespace

bool textLayout(const HudText& text, const Project& p, int& w, int& h) {
    Font* font = resolveFontNamed(p, text.font);
    if (!font) return false;
    const auto lines = splitLines(text.text);
    const int lineH = text.size + 4;
    float maxW = 8.0f;
    for (const auto& line : lines) {
        const float lw = textWidth(*font, line, (float)text.size, &p);
        if (lw > maxW) maxW = lw;
    }
    // +2px for the shadow offset and glyph overhang
    w = pow2Dim((int)(maxW + 0.5f) + 4);
    h = pow2Dim((int)lines.size() * lineH + 4);
    return true;
}

bool bakeTextRGBA(const HudText& text, const Project& p,
                  std::vector<unsigned char>& out, int& w, int& h) {
    Font* font = resolveFontNamed(p, text.font);
    if (!font) return false;
    if (!textLayout(text, p, w, h)) return false;
    out.assign((size_t)w * h * 4, 0);
    Canvas canvas{&out, w, h};

    auto clamp255 = [](float v) {
        return (unsigned char)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f);
    };
    const RGBA color{clamp255(text.color[0]), clamp255(text.color[1]),
                     clamp255(text.color[2]), 255};
    const RGBA shadow{10, 12, 16, 210};

    const auto lines = splitLines(text.text);
    const int lineH = text.size + 4;
    // Centered in the canvas both ways, so the sprite's center anchor centers
    // the visible content regardless of the pow2 padding.
    int y = (h - (int)lines.size() * lineH) / 2;
    for (const auto& line : lines) {
        if (text.shadow)
            drawText(canvas, *font, w * 0.5f + 1, y + 1, line, (float)text.size,
                     shadow, true, &p, /*skipIcons=*/true);
        drawText(canvas, *font, w * 0.5f, y, line, (float)text.size, color, true,
                 &p);
        y += lineH;
    }
    return true;
}

// --- Interaction prompts ----------------------------------------------------
// Same bake as a HUD text with one difference: EVERY action token is left as a
// hole and its box reported, so the game can blit the CURRENT binding's glyph
// there (see the header). "Press {{use}} to open" and "{{use}} use /
// {{throw}} throw" both work - the walk below mirrors drawText's pen exactly,
// so a slot lands wherever the glyph would have been baked, on whichever line.

bool promptLayout(const HudText& text, const Project& p, int& w, int& h,
                  std::vector<PromptIconSlot>& slots) {
    slots.clear();
    Font* font = resolveFontNamed(p, text.font);
    if (!font) return false;
    if (!textLayout(text, p, w, h)) return false;

    int ascent = 0, descent = 0, lineGap = 0;
    const float scale = stbtt_ScaleForPixelHeight(&font->info, (float)text.size);
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &lineGap);
    const int capH = (int)(ascent * scale + 0.5f);

    const auto lines = splitLines(text.text);
    const int lineH = text.size + 4;
    int yTop = (h - (int)lines.size() * lineH) / 2;
    for (const auto& line : lines) {
        // The glyphs occupy their slots in the layout either way, so the full
        // width (icons included) is what centres the line - as in the bake.
        const float full = textWidth(*font, line, (float)text.size, &p);
        float penX = w * 0.5f - full * 0.5f;
        for (const TextRun& run : textRuns(&p, line)) {
            if (run.icon.empty()) {
                // Kerning restarts at every run boundary in drawText, so a
                // per-run width sums to the same pen position.
                penX += textWidth(*font, run.text, (float)text.size, nullptr);
                continue;
            }
            const float adv = iconAdvance(p, run.icon, (float)text.size);
            if (!run.action.empty()) {
                PromptIconSlot s;
                s.action = run.action;
                s.size = (int)(adv - text.size * 0.12f + 0.5f);
                s.x = (int)(penX + text.size * 0.06f + 0.5f);
                s.y = yTop + (capH - s.size) / 2;
                slots.push_back(s);
            }
            penX += adv;
        }
        yTop += lineH;
    }
    return true;
}

bool bakePromptRGBA(const HudText& text, const Project& p,
                    std::vector<unsigned char>& out, int& w, int& h,
                    std::vector<PromptIconSlot>& slots) {
    Font* font = resolveFontNamed(p, text.font);
    if (!font) return false;
    if (!promptLayout(text, p, w, h, slots)) return false;
    out.assign((size_t)w * h * 4, 0);
    Canvas canvas{&out, w, h};

    auto clamp255 = [](float v) {
        return (unsigned char)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f);
    };
    const RGBA color{clamp255(text.color[0]), clamp255(text.color[1]),
                     clamp255(text.color[2]), 255};
    const RGBA shadow{10, 12, 16, 210};

    const auto lines = splitLines(text.text);
    const int lineH = text.size + 4;
    int y = (h - (int)lines.size() * lineH) / 2;
    for (const auto& line : lines) {
        // The shadow pass skips ALL icons as usual (a dark copy under a colored
        // glyph only leaks a fringe); the main pass skips the ACTION ones only,
        // so they become the game's live slots and any other icon still bakes.
        if (text.shadow)
            drawText(canvas, *font, w * 0.5f + 1, y + 1, line, (float)text.size,
                     shadow, true, &p, /*skipIcons=*/true);
        drawText(canvas, *font, w * 0.5f, y, line, (float)text.size, color, true,
                 &p, /*skipIcons=*/false, /*skipActionIcons=*/true);
        y += lineH;
    }
    return true;
}

bool bakePromptPNG(const HudText& text, const Project& p,
                   std::vector<unsigned char>& png,
                   std::vector<PromptIconSlot>& slots) {
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!bakePromptRGBA(text, p, rgba, w, h, slots)) return false;
    png.clear();
    stbi_write_png_to_func(pngWriteCallback, &png, w, h, 4, rgba.data(), w * 4);
    return !png.empty();
}

bool bakeTextPNG(const HudText& text, const Project& p,
                 std::vector<unsigned char>& png) {
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!bakeTextRGBA(text, p, rgba, w, h)) return false;
    png.clear();
    return stbi_write_png_to_func(pngWriteCallback, &png, w, h, 4, rgba.data(),
                                  w * 4) != 0;
}

std::string atlasFileName(const std::string& fontName) {
    return "atlas-" + sanitizeName(fontName, "font") + ".png";
}

bool atlasLayout(const GameFont& font, const Project& p, AtlasLayout& out) {
    // The entry itself carries the source path - never look it up by name,
    // so an unsaved Font Manager edit previews with its own font.
    Font* f = resolveFontPath(font.fontPath, p.dir);
    if (!f) return false;

    const int size = font.atlasSize < 8 ? 8 : font.atlasSize > 48 ? 48
                                                                  : font.atlasSize;
    const float scale = stbtt_ScaleForPixelHeight(&f->info, (float)size);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&f->info, &ascent, &descent, &lineGap);
    const int baseline = (int)(ascent * scale + 0.5f);

    out.baseSize = size;
    out.lineH = (int)((ascent - descent + lineGap) * scale + 0.5f);
    out.glyphs.assign(kAtlasCharCount, AtlasGlyph{});

    int maxW = 1, maxH = 1;
    for (int i = 0; i < kAtlasCharCount; ++i) {
        const int cp = kAtlasFirstChar + i;
        AtlasGlyph& g = out.glyphs[i];
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        g.advance = (int)(adv * scale + 0.5f);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetCodepointBitmapBox(&f->info, cp, scale, scale, &x0, &y0, &x1,
                                    &y1);
        g.w = x1 - x0;
        g.h = y1 - y0;
        g.xoff = x0;
        g.yoff = baseline + y0;
        if (g.w > maxW) maxW = g.w;
        if (g.h > maxH) maxH = g.h;
    }
    // Uniform grid with a 1px gutter, so bilinear sampling at the cell edge
    // never drags in the neighboring glyph (the engine's debug strip pads its
    // cells for the same reason).
    out.cellW = maxW + 1;
    out.cellH = maxH + 1;

    // Power-of-two sheet holding all 95 cells, by smallest area - and, on a
    // tie, the squarest. Pow2 rounding often makes several shapes cost the
    // exact same pixels (64x512 and 128x128 can both be 32k), and an atlas
    // stays resident in VRAM once shown, so ties are worth breaking well: the
    // squarer sheet leaves headroom before the 512 cap starts dropping glyphs.
    out.texW = 512;
    out.texH = 512;
    out.cols = out.texW / out.cellW;
    out.clipped = true;
    int bestArea = 0, bestSpan = 0;
    for (int tw = 8; tw <= 512; tw *= 2) {
        const int cols = tw / out.cellW;
        if (cols < 1) continue;
        const int rows = (kAtlasCharCount + cols - 1) / cols;
        int th = 8;
        while (th < rows * out.cellH) th *= 2;
        if (th > 512) continue;
        const int area = tw * th;
        const int span = tw > th ? tw : th;
        if (bestArea && (area > bestArea || (area == bestArea && span >= bestSpan)))
            continue;
        bestArea = area;
        bestSpan = span;
        out.texW = tw;
        out.texH = th;
        out.cols = cols;
        out.clipped = false;
    }
    if (out.cols < 1) out.cols = 1;

    for (int i = 0; i < kAtlasCharCount; ++i) {
        AtlasGlyph& g = out.glyphs[i];
        g.u = (i % out.cols) * out.cellW;
        g.v = (i / out.cols) * out.cellH;
        if (g.v + out.cellH > out.texH) {
            g.w = g.h = 0;  // past the cap: draws as a blank
            out.clipped = true;
        }
    }
    return true;
}

bool bakeAtlasRGBA(const GameFont& font, const Project& p,
                   std::vector<unsigned char>& out, AtlasLayout& layout) {
    // The entry itself carries the source path - never look it up by name,
    // so an unsaved Font Manager edit previews with its own font.
    Font* f = resolveFontPath(font.fontPath, p.dir);
    if (!f) return false;
    if (!atlasLayout(font, p, layout)) return false;

    out.assign((size_t)layout.texW * layout.texH * 4, 0);
    Canvas canvas{&out, layout.texW, layout.texH};
    const float scale = stbtt_ScaleForPixelHeight(&f->info, (float)layout.baseSize);
    // White glyphs: the runtime tints the sprite, so one atlas serves every
    // color a font is ever drawn in (and the drop shadow is a second, dark
    // pass over the same pixels).
    const RGBA white{255, 255, 255, 255};

    for (int i = 0; i < kAtlasCharCount; ++i) {
        const AtlasGlyph& g = layout.glyphs[i];
        if (g.w <= 0 || g.h <= 0) continue;
        int gw = 0, gh = 0, gx = 0, gy = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmap(
            &f->info, scale, scale, kAtlasFirstChar + i, &gw, &gh, &gx, &gy);
        if (!bmp) continue;
        for (int by = 0; by < gh; ++by)
            for (int bx = 0; bx < gw; ++bx)
                canvas.blend(g.u + bx, g.v + by, white, bmp[by * gw + bx]);
        stbtt_FreeBitmap(bmp, nullptr);
    }
    return true;
}

bool bakeAtlasPNG(const GameFont& font, const Project& p,
                  std::vector<unsigned char>& png) {
    std::vector<unsigned char> rgba;
    AtlasLayout layout;
    if (!bakeAtlasRGBA(font, p, rgba, layout)) return false;
    png.clear();
    return stbi_write_png_to_func(pngWriteCallback, &png, layout.texW,
                                  layout.texH, 4, rgba.data(),
                                  layout.texW * 4) != 0;
}

namespace {

// One icon's pixels at `cell` x `cell`: the user's PNG when it exists (so
// overriding an icon is just replacing the file), otherwise the built-in
// drawing. Empty when neither is available - the icon is then dropped from the
// atlas and its token renders as literal text.
bool iconPixels(const TextIcon& icon, const std::string& projectDir, int cell,
                std::vector<unsigned char>& out) {
    if (!icon.path.empty() && !projectDir.empty()) {
        const std::string full = projectDir + "\\" + icon.path;
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load(full.c_str(), &w, &h, &comp, 4);
        if (px) {
            out.assign((size_t)cell * cell * 4, 0);
            Canvas canvas{&out, cell, cell};
            drawImageScaled(canvas, px, w, h, 0, 0, cell, cell);
            stbi_image_free(px);
            return true;
        }
    }
    return bakeBuiltinIconRGBA(icon.name, cell, out);
}

}  // namespace

IconAtlasLayout iconAtlasLayout(const Project& p) {
    IconAtlasLayout l;
    // 32px cells: the icons draw at text height (15-24px on a PS2 screen), so a
    // bigger sheet would only cost VRAM. Icons without usable pixels are left
    // out here AND by the baker - same loop, so rects never drift.
    l.cell = 32;
    for (const TextIcon& ic : p.textIcons) {
        if (ic.name.empty()) continue;
        bool dup = false;
        for (const IconAtlasEntry& e : l.icons) dup |= (e.name == ic.name);
        if (dup) continue;
        std::vector<unsigned char> px;
        if (!iconPixels(ic, p.dir, l.cell, px)) continue;
        l.icons.push_back(IconAtlasEntry{ic.name, 0, 0, l.cell, l.cell});
    }
    if (l.icons.empty()) {
        l.texW = l.texH = 0;
        return l;
    }
    // Squarish grid, both dimensions pow2 and capped at the 512px PS2 limit.
    int cols = 1;
    while (cols * cols < (int)l.icons.size()) ++cols;
    l.texW = 32;
    while (l.texW < cols * l.cell && l.texW < 512) l.texW *= 2;
    l.cols = l.texW / l.cell;
    if (l.cols < 1) l.cols = 1;
    const int rows = ((int)l.icons.size() + l.cols - 1) / l.cols;
    l.texH = 32;
    while (l.texH < rows * l.cell && l.texH < 512) l.texH *= 2;
    for (size_t i = 0; i < l.icons.size(); ++i) {
        l.icons[i].u = (int)(i % (size_t)l.cols) * l.cell;
        l.icons[i].v = (int)(i / (size_t)l.cols) * l.cell;
        if (l.icons[i].v + l.cell > l.texH) l.clipped = true;
    }
    return l;
}

bool bakeIconAtlasRGBA(const Project& p, std::vector<unsigned char>& out,
                       IconAtlasLayout& layout) {
    layout = iconAtlasLayout(p);
    if (layout.icons.empty() || layout.texW <= 0) return false;
    out.assign((size_t)layout.texW * layout.texH * 4, 0);
    for (const IconAtlasEntry& e : layout.icons) {
        if (e.v + layout.cell > layout.texH) break;  // clipped
        const TextIcon* src = nullptr;
        for (const TextIcon& ic : p.textIcons)
            if (ic.name == e.name) { src = &ic; break; }
        if (!src) continue;
        std::vector<unsigned char> px;
        if (!iconPixels(*src, p.dir, layout.cell, px)) continue;
        for (int y = 0; y < layout.cell; ++y)
            for (int x = 0; x < layout.cell; ++x) {
                const unsigned char* s =
                    px.data() + (size_t)(y * layout.cell + x) * 4;
                unsigned char* d =
                    out.data() +
                    (size_t)((e.v + y) * layout.texW + (e.u + x)) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
    }
    return true;
}

bool bakeIconAtlasPNG(const Project& p, std::vector<unsigned char>& png) {
    IconAtlasLayout layout;
    std::vector<unsigned char> rgba;
    if (!bakeIconAtlasRGBA(p, rgba, layout)) return false;
    png.clear();
    stbi_write_png_to_func(pngWriteCallback, &png, layout.texW, layout.texH, 4,
                           rgba.data(), layout.texW * 4);
    return !png.empty();
}

}  // namespace menubake
