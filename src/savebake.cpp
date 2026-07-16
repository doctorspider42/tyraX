#include "savebake.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <stb_image.h>  // implementation lives in app.cpp

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

std::vector<unsigned char> iconIcn(const Project& p) {
    const std::vector<unsigned char> rgba = iconTextureRGBA(p);

    std::vector<unsigned char> b;
    b.reserve(kIcnBytes);

    // Icon header: one animation shape, uncompressed texture, 12 vertices
    // (a quad, both windings - the browser's backface culling can't hide it).
    pushU32(b, 0x010000);    // magic
    pushU32(b, 1);           // animation shapes
    pushU32(b, 0x07);        // texture type: uncompressed 128x128 BGR555
    pushU32(b, 0x3F800000);  // constant (1.0f)
    pushU32(b, 12);          // vertex count

    // Browser icon space: y grows downward, models stand on y = 0 - the quad
    // spans upward into negative y. UV origin is the texture's top-left.
    const float W = 1.8f, H = 3.6f;
    const float quad[4][2] = {{-W, -H}, {W, -H}, {W, 0.0f}, {-W, 0.0f}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const int tris[4][3] = {{0, 1, 2}, {0, 2, 3}, {2, 1, 0}, {3, 2, 0}};
    for (int t = 0; t < 4; ++t) {
        for (int vi = 0; vi < 3; ++vi) {
            const int v = tris[t][vi];
            pushF16(b, quad[v][0]);  // vertex (one set - one shape)
            pushF16(b, quad[v][1]);
            pushF16(b, 0.0f);
            pushS16(b, 0);
            pushF16(b, 0.0f);  // normal: toward the default camera
            pushF16(b, 0.0f);
            pushF16(b, t < 2 ? -1.0f : 1.0f);
            pushS16(b, 0);
            pushF16(b, uv[v][0]);
            pushF16(b, uv[v][1]);
            for (int c = 0; c < 4; ++c) b.push_back(0xFF);  // vertex RGBA
        }
    }

    // Animation segment: a single static frame on shape 0.
    pushU32(b, 0x01);        // id tag
    pushU32(b, 1);           // frame length
    pushU32(b, 0x3F800000);  // anim speed = 1.0f
    pushU32(b, 0);           // play offset
    pushU32(b, 1);           // frame count
    pushU32(b, 0);           // frame: shape id
    pushU32(b, 0);           // frame: key count
    pushU32(b, 0);           // frame: unknown
    pushU32(b, 0);           // frame: unknown

    // Texture: 128x128 BGR555 (blue in the high bits), little endian.
    for (int i = 0; i < 128 * 128; ++i) {
        const unsigned char* px = &rgba[i * 4];
        const unsigned v = ((px[2] >> 3) << 10) | ((px[1] >> 3) << 5) | (px[0] >> 3);
        b.push_back((unsigned char)(v & 0xFF));
        b.push_back((unsigned char)((v >> 8) & 0xFF));
    }
    return b;
}

}  // namespace savebake
