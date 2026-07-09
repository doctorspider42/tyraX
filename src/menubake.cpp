#include "menubake.hpp"

#include <cstdio>
#include <cstring>

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

// A loaded TTF (kept for the process lifetime - bakes happen per build and
// per preview refresh, re-reading the font every time would be wasteful).
struct Font {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};

Font& systemFont() {
    static Font font = [] {
        Font f;
        char windir[MAX_PATH] = {};
        GetWindowsDirectoryA(windir, MAX_PATH);
        const char* candidates[] = {"consolab.ttf", "arialbd.ttf", "arial.ttf"};
        for (const char* name : candidates) {
            const std::string path = std::string(windir) + "\\Fonts\\" + name;
            FILE* fp = std::fopen(path.c_str(), "rb");
            if (!fp) continue;
            std::fseek(fp, 0, SEEK_END);
            const long size = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            f.data.resize((size_t)size);
            const size_t got = std::fread(f.data.data(), 1, (size_t)size, fp);
            std::fclose(fp);
            if (got != (size_t)size) continue;
            if (stbtt_InitFont(&f.info, f.data.data(),
                               stbtt_GetFontOffsetForIndex(f.data.data(), 0))) {
                f.ok = true;
                break;
            }
        }
        return f;
    }();
    return font;
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

float textWidth(const std::string& text, float pixelHeight) {
    Font& f = systemFont();
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
void drawText(Canvas& canvas, float x, int yTop, const std::string& text,
              float pixelHeight, RGBA color, bool centered) {
    Font& f = systemFont();
    const float scale = stbtt_ScaleForPixelHeight(&f.info, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &lineGap);
    const int baseline = yTop + (int)(ascent * scale + 0.5f);

    if (centered) x -= textWidth(text, pixelHeight) * 0.5f;

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

PanelLayout panelLayout(const GameMenu& menu, const std::string& projectDir) {
    PanelLayout l;
    l.panelW = (menu.panelW == 128 || menu.panelW == 512) ? menu.panelW : 256;
    int entries = (int)menu.entries.size();
    if (entries < 0) entries = 0;
    if (entries > kMaxEntries) entries = kMaxEntries;

    const auto fit = fitImages(menu, projectDir);
    const int aboveTitle = flowBlockHeight(menu, fit, MenuImage::AboveTitle);
    const int aboveEntries = flowBlockHeight(menu, fit, MenuImage::AboveEntries);
    const int belowEntries = flowBlockHeight(menu, fit, MenuImage::BelowEntries);

    // title block: text at +8, separator at +31 (36 tall); hidden = small pad
    const int titleBlock = menu.showTitle ? 44 : 10;
    l.row0Y = aboveTitle + titleBlock + aboveEntries;
    l.contentH = l.row0Y + entries * kRowH + belowEntries + 22;  // hints + pad
    if (l.contentH > 512) {
        l.contentH = 512;  // PS2 texture cap - the bake clips, editor warns
        l.clipped = true;
    }
    l.canvasH = 64;
    while (l.canvasH < l.contentH) l.canvasH *= 2;
    return l;
}

std::string panelFileName(const std::string& menuName) {
    std::string s;
    for (char c : menuName) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_')
            s += (char)tolower((unsigned char)c);
        else
            s += '-';
    }
    if (s.empty()) s = "menu";
    return s + ".png";
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

bool bakePanelRGBA(const GameMenu& menu, const std::string& projectDir,
                   std::vector<unsigned char>& out, int& w, int& h) {
    if (!systemFont().ok) return false;

    const int entries = (int)menu.entries.size() > kMaxEntries
                            ? kMaxEntries
                            : (int)menu.entries.size();
    const PanelLayout l = panelLayout(menu, projectDir);
    const auto fit = fitImages(menu, projectDir);
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
        if (menu.images[i].path.empty() || projectDir.empty()) continue;
        int comp = 0;
        const std::string full = projectDir + "\\" + menu.images[i].path;
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
        drawText(canvas, w * 0.5f, y, menu.title, 18.0f, accent, true);
        canvas.fillRect(16, y + 23, w - 16, y + 24, separator);
        y += 36;
    } else {
        y += 2;
    }
    drawFlowSlot(MenuImage::AboveEntries, y);

    for (int i = 0; i < entries; ++i)
        drawText(canvas, 56, l.row0Y + i * kRowH + 2, menu.entries[i].label,
                 15.0f, kText, false);

    int below = l.row0Y + entries * kRowH + 4;
    drawFlowSlot(MenuImage::BelowEntries, below);

    drawText(canvas, w * 0.5f, content - 18, "X OK    \xE2\x96\xB2 BACK", 11.0f,
             kDim, true);

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

bool bakePanelPNG(const GameMenu& menu, const std::string& projectDir,
                  std::vector<unsigned char>& png) {
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!bakePanelRGBA(menu, projectDir, rgba, w, h)) return false;
    png.clear();
    return stbi_write_png_to_func(pngWriteCallback, &png, w, h, 4, rgba.data(),
                                  w * 4) != 0;
}

}  // namespace menubake
