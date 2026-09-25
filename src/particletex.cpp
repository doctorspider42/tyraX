#include "particletex.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

namespace particletex {
namespace {

uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float lattice(int seed, int x, int y) {
    return (float)(hash3((uint32_t)seed, (uint32_t)x, (uint32_t)y) & 0xFFFFFFu) / 16777215.0f;
}

float smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise, 0..1.
float noise(int seed, float x, float y) {
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const float fx = smooth(x - ix), fy = smooth(y - iy);
    const float a = lattice(seed, ix, iy), b = lattice(seed, ix + 1, iy);
    const float c = lattice(seed, ix, iy + 1), d = lattice(seed, ix + 1, iy + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// Four-octave fBm, 0..1.
float fbm(int seed, float x, float y) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < 4; ++o) {
        sum += amp * noise(seed + o * 131, x, y);
        norm += amp;
        x *= 2.03f, y *= 2.03f;
        amp *= 0.5f;
    }
    return sum / norm;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float smoothstep(float e0, float e1, float x) {
    if (e1 <= e0) return x < e0 ? 0.0f : 1.0f;
    return smooth(clamp01((x - e0) / (e1 - e0)));
}

// The flame colour ramp by temperature 0..1: deep red -> orange -> yellow ->
// white. `heat` widens the white core.
void flameColor(float t, float heat, float* rgb) {
    struct Stop { float t, r, g, b; };
    const float w = 0.78f - 0.28f * heat;  // where white starts
    const Stop s[] = {{0.0f, 0.35f, 0.04f, 0.01f},
                      {0.30f, 0.85f, 0.20f, 0.03f},
                      {0.55f, 1.00f, 0.52f, 0.08f},
                      {w, 1.00f, 0.86f, 0.35f},
                      {1.0f, 1.00f, 0.98f, 0.88f}};
    t = clamp01(t);
    for (int i = 0; i < 4; ++i) {
        if (t > s[i + 1].t && i < 3) continue;  // not this segment yet
        const float k = s[i + 1].t > s[i].t ? clamp01((t - s[i].t) / (s[i + 1].t - s[i].t)) : 1.0f;
        rgb[0] = s[i].r + (s[i + 1].r - s[i].r) * k;
        rgb[1] = s[i].g + (s[i + 1].g - s[i].g) * k;
        rgb[2] = s[i].b + (s[i + 1].b - s[i].b) * k;
        return;
    }
}

bool writeIfChanged(const std::filesystem::path& path, const std::string& bytes) {
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream cur;
            cur << in.rdbuf();
            if (cur.str() == bytes) return true;
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)out;
}

}  // namespace

std::vector<unsigned char> generate(const ParticleTexGen& g) {
    std::vector<unsigned char> px;
    if (g.kind < 1 || g.kind > 3) return px;
    const int n = g.size == 32 || g.size == 128 ? g.size : 64;
    px.assign((size_t)n * n * 4, 0);
    const float soft = clamp01(g.softness), detail = clamp01(g.detail);
    const float sc = g.scale > 0.1f ? g.scale : 0.1f;
    const float turb = clamp01(g.turbulence), heat = clamp01(g.heat);
    const int seed = g.seed;
    for (int py = 0; py < n; ++py) {
        for (int pxl = 0; pxl < n; ++pxl) {
            // -1..1, +y UP (row 0 is the top of the image)
            const float x = ((pxl + 0.5f) / n) * 2.0f - 1.0f;
            const float y = 1.0f - ((py + 0.5f) / n) * 2.0f;
            float r = 0.0f, gg = 0.0f, b = 0.0f, a = 0.0f;
            if (g.kind == 1) {
                // Smoke: a billowy puff - the radius itself is warped by the
                // noise, so the silhouette is a cloud and not a disc.
                const float nz = fbm(seed, (x + 3.1f) * 2.2f / sc, (y + 7.7f) * 2.2f / sc);
                const float lobes = fbm(seed + 53, (x + 1.3f) * 1.3f / sc, (y - 4.2f) * 1.3f / sc);
                const float rad = std::sqrt(x * x + y * y) * 1.12f +
                                  (lobes - 0.5f) * 1.1f * detail + (nz - 0.5f) * 0.35f * detail;
                const float inner = 0.9f - 0.8f * soft;
                const float fall = 1.0f - smoothstep(inner, 1.0f, rad);
                const float billow = std::pow(clamp01(nz * 1.35f - 0.1f), 1.0f + detail);
                const float body = fall * (1.0f - detail + detail * 1.5f * billow);
                a = clamp01(body * 0.9f);
                // lit from above: the top of the puff is brighter
                const float shade = 0.72f + 0.28f * clamp01(0.5f + 0.5f * y) + 0.12f * (nz - 0.5f);
                r = g.color[0] * shade, gg = g.color[1] * shade, b = g.color[2] * shade;
            } else if (g.kind == 2) {
                // Flame: a teardrop base-down, its sides licked sideways by a
                // noise field that scrolls upward, fading into tongues at the tip.
                const float f = 0.5f + 0.5f * y;  // 0 bottom .. 1 top
                const float nz = fbm(seed, (x + 5.0f) * 2.6f / sc, (f * 3.2f + 11.0f) / sc);
                const float nz2 = fbm(seed + 97, (x - 2.0f) * 5.0f / sc, (f * 6.0f) / sc);
                const float xs = x + (nz - 0.5f) * 1.1f * turb * (0.25f + f);
                float w = 0.62f * std::pow(clamp01(1.0f - f), 0.55f);
                w *= smoothstep(-0.02f, 0.22f, f) * 0.35f + 0.65f;  // rounded foot
                const float d = w > 1e-4f ? std::fabs(xs) / w : 9.0f;
                const float edge = 1.0f - smoothstep(0.55f - 0.45f * soft, 1.0f, d);
                const float tongues = 1.0f - detail * 0.6f * smoothstep(0.35f, 1.0f, f) * (1.0f - nz2 * 1.4f);
                const float base = smoothstep(0.0f, 0.10f, f);  // no hard floor line
                const float I = clamp01(edge * clamp01(tongues) * base);
                // temperature: hottest low in the middle
                const float temp = clamp01(I * (1.15f - 0.75f * f) * (1.0f - 0.35f * d));
                float c[3];
                flameColor(temp, heat, c);
                a = I;
                r = c[0] * I, gg = c[1] * I, b = c[2] * I;  // premultiplied
            } else {
                // Glow / spark: a hot core inside a soft halo.
                const float rad = std::sqrt(x * x + y * y);
                const float core = std::exp(-std::pow(rad / (0.08f + 0.22f * heat), 2.0f));
                const float halo = std::exp(-rad * rad * (6.0f - 4.5f * soft)) * 0.55f;
                const float v = clamp01((core + halo) * (1.0f - smoothstep(0.85f, 1.0f, rad)));
                a = v;
                const float white = core * 0.8f;
                r = (g.color[0] + (1.0f - g.color[0]) * white) * v;
                gg = (g.color[1] + (1.0f - g.color[1]) * white) * v;
                b = (g.color[2] + (1.0f - g.color[2]) * white) * v;
            }
            // Every kind reaches exactly zero before the quad's edge.
            const float box = std::max(std::fabs(x), std::fabs(y));
            const float frame = 1.0f - smoothstep(0.92f, 1.0f, box);
            a *= frame;
            if (g.kind != 1) r *= frame, gg *= frame, b *= frame;
            unsigned char* o = &px[((size_t)py * n + pxl) * 4];
            o[0] = (unsigned char)std::lround(clamp01(r) * 255.0f);
            o[1] = (unsigned char)std::lround(clamp01(gg) * 255.0f);
            o[2] = (unsigned char)std::lround(clamp01(b) * 255.0f);
            o[3] = (unsigned char)std::lround(clamp01(a) * 255.0f);
        }
    }
    return px;
}

std::string fileStem(const std::string& effectName) {
    std::string s;
    for (char c : effectName) {
        const unsigned char u = (unsigned char)c;
        if (std::isalnum(u)) s += (char)std::tolower(u);
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "particle" : s;
}

std::string writeAssets(const std::string& projectDir, const std::string& effectName,
                        const ParticleTexGen& g, std::string* err) {
    namespace fs = std::filesystem;
    const std::vector<unsigned char> px = generate(g);
    if (px.empty()) {
        if (err) *err = "no procedural texture kind selected";
        return "";
    }
    const int n = (int)std::lround(std::sqrt((double)(px.size() / 4)));
    const std::string stem = fileStem(effectName);
    const fs::path dir = fs::path(projectDir) / kDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string png;
    stbi_write_png_to_func(
        [](void* ctx, void* data, int size) {
            static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), (size_t)size);
        },
        &png, n, n, 4, px.data(), n * 4);
    if (png.empty() || !writeIfChanged(dir / (stem + ".png"), png)) {
        if (err) *err = "cannot write " + (dir / (stem + ".png")).string();
        return "";
    }
    const std::string mtl =
        "# Generated by TyraX (Particle Editor) - regenerated from the effect's recipe\n"
        "newmtl " + stem + "\nKd 1 1 1\nmap_Kd " + stem + ".png\n";
    if (!writeIfChanged(dir / (stem + ".mtl"), mtl)) {
        if (err) *err = "cannot write " + (dir / (stem + ".mtl")).string();
        return "";
    }
    return std::string(kDir) + "/" + stem + ".mtl";
}

}  // namespace particletex
