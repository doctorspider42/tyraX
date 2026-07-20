#include "menubake.hpp"

#include <cstdio>
#include <cstring>
#include <map>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <stb_image.h>  // implementation lives in app.cpp

#include <windows.h>

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

std::string windowsFontPath(const std::string& name) {
    char windir[MAX_PATH] = {};
    GetWindowsDirectoryA(windir, MAX_PATH);
    return std::string(windir) + "\\Fonts\\" + name;
}

// A font by path: project-relative when the path has a separator
// ("res/fonts/x.ttf"), a Windows font by bare file name ("impact.ttf"),
// falling back to the default chain when unset or unreadable.
Font* resolveFontPath(const std::string& fontPath, const std::string& projectDir) {
    if (!fontPath.empty()) {
        const bool projectRelative =
            fontPath.find('/') != std::string::npos ||
            fontPath.find('\\') != std::string::npos;
        const std::string full = projectRelative && !projectDir.empty()
                                     ? projectDir + "\\" + fontPath
                                     : windowsFontPath(fontPath);
        if (Font* f = loadFontFile(full)) return f;
    }
    for (const char* name : {"consolab.ttf", "arialbd.ttf", "arial.ttf"})
        if (Font* f = loadFontFile(windowsFontPath(name))) return f;
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

float textWidth(Font& f, const std::string& text, float pixelHeight) {
    const float scale = stbtt_ScaleForPixelHeight(&f.info, pixelHeight);
    float x = 0;
    size_t i = 0;
    int prev = 0;
    while (i < text.size()) {
        const int cp = nextCodepoint(text, i);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
        if (prev) x += scale * stbtt_GetCodepointKernAdvance(&f.info, prev, cp);
        x += scale * adv;
        prev = cp;
    }
    return x;
}

// Draws text with its baseline placed so the cap sits around y (top of the
// line box); x is the left edge, or the center when centered = true.
void drawText(Canvas& canvas, Font& f, float x, int yTop, const std::string& text,
              float pixelHeight, RGBA color, bool centered) {
    const float scale = stbtt_ScaleForPixelHeight(&f.info, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &lineGap);
    const int baseline = yTop + (int)(ascent * scale + 0.5f);

    if (centered) x -= textWidth(f, text, pixelHeight) * 0.5f;

    float penX = x;
    size_t i = 0;
    int prev = 0;
    while (i < text.size()) {
        const int cp = nextCodepoint(text, i);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, cp, &adv, &lsb);
        if (prev)
            penX += scale * stbtt_GetCodepointKernAdvance(&f.info, prev, cp);

        int gw = 0, gh = 0, gx = 0, gy = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&f.info, scale, scale,
                                                         cp, &gw, &gh, &gx, &gy);
        if (bitmap) {
            const int ox = (int)(penX + 0.5f) + gx;
            const int oy = baseline + gy;
            for (int by = 0; by < gh; ++by)
                for (int bx = 0; bx < gw; ++bx)
                    canvas.blend(ox + bx, oy + by, color, bitmap[by * gw + bx]);
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        penX += scale * adv;
        prev = cp;
    }
}

void pngWriteCallback(void* context, void* data, int size) {
    auto* out = (std::vector<unsigned char>*)context;
    out->insert(out->end(), (unsigned char*)data, (unsigned char*)data + size);
}

}  // namespace

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
        const std::string full = projectDir + "\\" + mi.path;
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
        const std::string full = p.dir + "\\" + menu.images[i].path;
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
                 accent, true);
        canvas.fillRect(16, y + menu.titleSize + 5, w - 16, y + menu.titleSize + 6,
                        separator);
        y += menu.titleSize + 18;
    } else {
        y += 2;
    }
    drawFlowSlot(MenuImage::AboveEntries, y);

    for (int i = 0; i < entries; ++i)
        drawText(canvas, *font, 56, l.row0Y + i * l.rowH + 2, menu.entries[i].label,
                 (float)menu.entrySize, kText, false);

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
            const float tw = textWidth(*font, labels[o], (float)menu.entrySize);
            drawText(canvas, *font, (float)(l.cellW - 4) - tw, top + 2, labels[o],
                     (float)menu.entrySize, kText, false);
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
        const float tw = textWidth(*font, labels[cur], (float)menu.entrySize);
        drawText(canvas, *font, (float)(pl.panelW - 28) - tw,
                 pl.row0Y + i * pl.rowH + 2, labels[cur], (float)menu.entrySize,
                 kText, false);
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
        const float lw = textWidth(*font, line, (float)text.size);
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
                     shadow, true);
        drawText(canvas, *font, w * 0.5f, y, line, (float)text.size, color, true);
        y += lineH;
    }
    return true;
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

}  // namespace menubake
