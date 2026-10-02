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

// A noise field that loops: sampled at the offset t*P and at (t-1)*P and
// cross-faded by t, so t = 0 and t = 1 give the same image.
// Rescaled around 0.5 so the mid-loop frames (two fields averaged) keep the
// contrast of frame 0 instead of going flat.
template <class F>
float looped(float t, float period, F f) {
    const float v = (1.0f - t) * f(t * period) + t * f((t - 1.0f) * period);
    const float k = std::sqrt((1.0f - t) * (1.0f - t) + t * t);
    return 0.5f + (v - 0.5f) / k;
}

std::string framePath(const std::string& mtlPath, int k) {
    if (k <= 0) return mtlPath;
    const size_t dot = mtlPath.rfind(".mtl");
    if (dot == std::string::npos) return mtlPath;
    return mtlPath.substr(0, dot) + "-f" + std::to_string(k) + ".mtl";
}

std::vector<unsigned char> generate(const ParticleTexGen& g, int frame) {
    std::vector<unsigned char> px;
    if (g.kind < 1 || g.kind > 3) return px;
    const int frames = g.frames > 1 ? g.frames : 1;
    const float T = (float)(((frame % frames) + frames) % frames) / (float)frames;
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
                // the billows roll upward through the loop
                const float nz = looped(T, 1.6f, [&](float o) {
                    return fbm(seed, (x + 3.1f) * 2.2f / sc, (y + 7.7f - o) * 2.2f / sc);
                });
                const float lobes = looped(T, 0.8f, [&](float o) {
                    return fbm(seed + 53, (x + 1.3f) * 1.3f / sc, (y - 4.2f - o) * 1.3f / sc);
                });
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
                // noise field that climbs through the loop, splitting into
                // separate tongues near the tip; the whole flame breathes
                // (its height pulses) so a flipbook reads as fire, not as a
                // wobbling candle.
                const float breath = 0.86f + 0.14f * std::sin(6.2831853f * T + 0.37f * (float)(seed % 17));
                const float f = (0.5f + 0.5f * y) / breath;  // 0 bottom .. 1 top
                const float nz = looped(T, 2.0f, [&](float o) {
                    return fbm(seed, (x + 5.0f) * 2.4f / sc, (f * 3.0f + 11.0f - o) / sc);
                });
                const float nz2 = looped(T, 3.0f, [&](float o) {
                    return fbm(seed + 97, (x - 2.0f) * 4.2f / sc, (f * 5.0f - o) / sc);
                });
                const float xs = x + (nz - 0.5f) * 1.7f * turb * (0.2f + f * 1.1f);
                float w = 0.74f * std::pow(clamp01(1.0f - f), 0.6f);
                w *= smoothstep(-0.02f, 0.22f, f) * 0.3f + 0.7f;  // rounded foot
                const float d = w > 1e-4f ? std::fabs(xs) / w : 9.0f;
                const float edge = 1.0f - smoothstep(0.5f - 0.4f * soft, 1.0f, d);
                // tongues: above mid height only where the second field is
                // high enough - the flame breaks into licks instead of a cone
                const float cut = 0.25f + 0.75f * f;
                const float lick = smoothstep(cut - 0.35f, cut + 0.05f, nz2 * 1.25f);
                const float tongues = 1.0f - detail * smoothstep(0.3f, 0.9f, f) * (1.0f - lick);
                const float base = smoothstep(0.0f, 0.10f, f);  // no hard floor line
                const float I = clamp01(edge * clamp01(tongues) * base) *
                                (1.0f - smoothstep(0.92f, 1.05f, f));
                // temperature: hottest low in the middle
                const float temp = clamp01(I * (1.02f - 0.75f * f) * (1.0f - 0.45f * d));
                float c[3];
                flameColor(temp, heat, c);
                a = I;
                r = c[0] * I, gg = c[1] * I, b = c[2] * I;  // premultiplied
            } else {
                // Glow / spark: a hot core inside a soft halo.
                const float rad = std::sqrt(x * x + y * y);
                // the core breathes through the loop
                const float pulse = 1.0f + 0.25f * std::sin(6.2831853f * T);
                const float core = std::exp(-std::pow(rad / ((0.08f + 0.22f * heat) * pulse), 2.0f));
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

int bakeEffect(Project& p, ParticleEffect& fx, std::string* err) {
    int baked = 0;
    for (int li = 0; li <= (int)fx.layers.size(); ++li) {
        ParticleLayer& L = li == 0 ? static_cast<ParticleLayer&>(fx) : fx.layers[(size_t)li - 1];
        if (L.texGen.kind == 0) continue;
        const std::string mtl =
            writeAssets(p.dir, project::particleLayerStem(fx, li), L.texGen, err);
        if (mtl.empty()) return -1;
        L.materialPath = mtl;
        // A soft alpha ramp does not survive the palettized (CLUT) bake.
        for (int k = 0; k < std::max(1, L.texGen.frames); ++k)
            p.textureQuality[framePath(mtl, k)] = "none";
        ++baked;
    }
    return baked;
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
    if (g.kind < 1 || g.kind > 3) {
        if (err) *err = "no procedural texture kind selected";
        return "";
    }
    const std::string stem = fileStem(effectName);
    const fs::path dir = fs::path(projectDir) / kDir;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const int frames = g.frames > 1 ? g.frames : 1;
    for (int k = 0; k < frames; ++k) {
        const std::vector<unsigned char> px = generate(g, k);
        const int n = (int)std::lround(std::sqrt((double)(px.size() / 4)));
        const std::string fstem = k == 0 ? stem : stem + "-f" + std::to_string(k);
        std::string png;
        stbi_write_png_to_func(
            [](void* ctx, void* data, int size) {
                static_cast<std::string*>(ctx)->append(static_cast<const char*>(data),
                                                       (size_t)size);
            },
            &png, n, n, 4, px.data(), n * 4);
        if (png.empty() || !writeIfChanged(dir / (fstem + ".png"), png)) {
            if (err) *err = "cannot write " + (dir / (fstem + ".png")).string();
            return "";
        }
        const std::string mtl =
            "# Generated by TyraX (Particle Editor) - regenerated from the effect's recipe\n"
            "newmtl " + fstem + "\nKd 1 1 1\nmap_Kd " + fstem + ".png\n";
        if (!writeIfChanged(dir / (fstem + ".mtl"), mtl)) {
            if (err) *err = "cannot write " + (dir / (fstem + ".mtl")).string();
            return "";
        }
    }
    return std::string(kDir) + "/" + stem + ".mtl";
}

}  // namespace particletex
