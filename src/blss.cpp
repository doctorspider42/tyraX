// BLSS host side - see blss.hpp for the contract and
// docs/blss-reconstruction.md for the arithmetic this file is one half of.
//
// The whole file is written to be the TWIN of the engine's RendererCoreBlss:
// every sample, shift and clamp models what the GS does, not what a float
// pipeline would prefer. That is not pedantry - the oracle optimises this
// formula, so a divergence here trains the network for a machine that does not
// exist.

#include "blss.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <stb_image.h>
#include <stb_image_write.h>

#include "blsscorpus.hpp"

// The net that ships with the editor, and its provenance, both baked into the
// binary by cmake/embed_binary.cmake. A loose data file next to the exe was the
// alternative and it loses on the same grounds the app icon and the lunar
// colour map lose on: the editor is a single static executable that people copy
// around, and a default that is missing half the time is worse than no default.
#include "blssmeta_gen.hpp"
#include "blssnet_gen.hpp"

namespace blss {

const char* const kFeatureNames[kFeatures] = {"motion",    "depth",    "depthGrad",
                                              "edgeDens", "texDetail", "coverage"};
const char* const kOutputNames[kOutputs] = {"point", "temporal", "sharpen"};

// ------------------------------------------------------- the two sweep knobs ---

namespace detail {
int gTile = kTile;
int gActN = 0;
const float* gActTab = nullptr;
bool gJitter = true;
}  // namespace detail

void setJitter(bool on) { detail::gJitter = on; }

bool setTileSize(int px) {
    if (px <= 0 || (px & (px - 1)) != 0) return false;
    detail::gTile = px;
    return true;
}

std::string scaleName(Scale s) {
    return std::to_string(s.x) + "x" + std::to_string(s.y);
}

bool parseScale(const std::string& text, Scale* out) {
    // "4x4", "2x4", "1x2". Deliberately strict - a silently misread scale is a
    // table measured on a configuration nobody can name, which is the exact
    // failure this feature has published five times.
    const size_t x = text.find_first_of("xX");
    if (x == std::string::npos || x == 0 || x + 1 >= text.size()) return false;
    const std::string a = text.substr(0, x), b = text.substr(x + 1);
    if (a.find_first_not_of("0123456789") != std::string::npos) return false;
    if (b.find_first_not_of("0123456789") != std::string::npos) return false;
    const int sx = std::atoi(a.c_str()), sy = std::atoi(b.c_str());
    if (sx < 1 || sy < 1 || sx > 16 || sy > 16) return false;
    if (out) *out = Scale(sx, sy);
    return true;
}

namespace {
// The table itself, in both forms. The int16 half IS the contract (it is what
// the engine spells out as literals and what the hash is taken over); the float
// half is the same numbers dequantised once, so the hot path does not convert
// per lookup. Dequantisation is exact - an int16 is exactly representable and
// 2^-15 is a power of two - so the two agree by construction rather than by
// tolerance.
std::vector<int16_t> gActQ;
std::vector<float> gActF;

// round-half-AWAY-FROM-ZERO of tanh(a) * 32768. Spelled out rather than left to
// std::lround or a cast, because "which way does .5 go" is exactly the kind of
// unstated rule that makes two implementations of a table differ in one entry.
int16_t actEntry(double a) {
    const double v = std::tanh(a) * static_cast<double>(kActScale);
    const double r = v >= 0.0 ? std::floor(v + 0.5) : std::ceil(v - 0.5);
    if (r > 32767.0) return 32767;
    if (r < -32768.0) return -32768;
    return static_cast<int16_t>(r);
}
}  // namespace

bool setActTable(int n) {
    if (n < 0) return false;
    if (n == 0) {
        detail::gActN = 0;
        detail::gActTab = nullptr;
        gActQ.clear();
        gActF.clear();
        return true;
    }
    if (n % 2 != 0) return false;  // the midpoint must land on tanh(0) = 0
    gActQ.assign(static_cast<size_t>(n) + 1, 0);
    gActF.assign(static_cast<size_t>(n) + 1, 0.0f);
    const double step = 2.0 * static_cast<double>(kActRange) / static_cast<double>(n);
    for (int i = 0; i <= n; ++i) {
        // ODD SYMMETRY BY CONSTRUCTION, not by hoping the two ends round the
        // same way: only the upper half is computed and the lower half is its
        // negation, which is also how the engine's table is emitted (half the
        // literals) and why the entry count must be even.
        const int m = i >= n / 2 ? i : n - i;
        const int16_t up = actEntry(-static_cast<double>(kActRange) + step * m);
        gActQ[static_cast<size_t>(i)] = i >= n / 2 ? up : static_cast<int16_t>(-up);
        gActF[static_cast<size_t>(i)] =
            static_cast<float>(gActQ[static_cast<size_t>(i)]) / static_cast<float>(kActScale);
    }
    detail::gActN = n;
    detail::gActTab = gActF.data();
    return true;
}

uint32_t actTableHash() {
    if (detail::gActN <= 0) return 0;
    uint32_t h = 2166136261u;
    for (int16_t q : gActQ) {
        const uint16_t u = static_cast<uint16_t>(q);
        h = (h ^ static_cast<uint32_t>(u & 0xFF)) * 16777619u;
        h = (h ^ static_cast<uint32_t>((u >> 8) & 0xFF)) * 16777619u;
    }
    return h;
}

std::string emitActTable() {
    std::string s;
    if (detail::gActN <= 0) return s;
    const int n = detail::gActN;
    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "// BLSS activation table - docs/blss-reconstruction.md S5.\n"
                  "// tanh over [-%g, +%g], %d intervals, Q15, odd-symmetric:\n"
                  "// only the upper half is stored, T[i] = -T[n-i] below it.\n"
                  "// FNV-1a over all %d int16 entries (LE): 0x%08X\n",
                  static_cast<double>(kActRange), static_cast<double>(kActRange), n, n + 1,
                  actTableHash());
    s += buf;
    std::snprintf(buf, sizeof(buf), "static const short kBlssTanhHalf[%d] = {\n", n / 2 + 1);
    s += buf;
    for (int i = n / 2; i <= n; ++i) {
        if ((i - n / 2) % 12 == 0) s += "   ";
        std::snprintf(buf, sizeof(buf), " %6d,", static_cast<int>(gActQ[static_cast<size_t>(i)]));
        s += buf;
        if ((i - n / 2) % 12 == 11) s += "\n";
    }
    if ((n / 2 + 1) % 12 != 0) s += "\n";
    s += "};\n";
    return s;
}

namespace {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float overlap(float a0, float a1, float b0, float b1) {
    const float lo = a0 > b0 ? a0 : b0;
    const float hi = a1 < b1 ? a1 : b1;
    return hi > lo ? hi - lo : 0.0f;
}

// The GS interpolates and stores texture coordinates as 12.4 fixed point, so
// both samplers quantise to sixteenths first. Everything after that is integer.
constexpr int kSub = 16;

// GS point sampling: the integer part of the UV, region-clamped to the real
// extent of a non-power-of-two buffer (what GS_SET_CLAMP(2,2,...) does).
void sampleNearest(const Image& img, float u, float v, int out[3]) {
    const int uq = static_cast<int>(std::floor(u * kSub));
    const int vq = static_cast<int>(std::floor(v * kSub));
    const int x = clampi(uq >> 4, 0, img.w - 1);
    const int y = clampi(vq >> 4, 0, img.h - 1);
    const uint8_t* p = img.at(x, y);
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[2];
}

// GS bilinear: four taps around (UV - half a texel), weights from the four
// fractional bits, combined in integer with a truncating shift. The >>4 on a
// negative coordinate is an arithmetic shift on purpose - it floors, which is
// what the hardware's fixed-point walk does - and `& 15` then yields the
// positive fraction.
void sampleBilinear(const Image& img, float u, float v, int out[3]) {
    const int uq = static_cast<int>(std::floor(u * kSub)) - 8;
    const int vq = static_cast<int>(std::floor(v * kSub)) - 8;
    const int fx = uq & 15, fy = vq & 15;
    const int x0 = clampi(uq >> 4, 0, img.w - 1);
    const int y0 = clampi(vq >> 4, 0, img.h - 1);
    const int x1 = clampi((uq >> 4) + 1, 0, img.w - 1);
    const int y1 = clampi((vq >> 4) + 1, 0, img.h - 1);
    const uint8_t* p00 = img.at(x0, y0);
    const uint8_t* p10 = img.at(x1, y0);
    const uint8_t* p01 = img.at(x0, y1);
    const uint8_t* p11 = img.at(x1, y1);
    const int wx0 = 16 - fx, wy0 = 16 - fy;
    for (int c = 0; c < 3; ++c)
        out[c] = (p00[c] * wx0 * wy0 + p10[c] * fx * wy0 + p01[c] * wx0 * fy +
                  p11[c] * fx * fy) >>
                 8;
}

// Linear interpolation over the quad AS THE RASTERISER SEES IT. The engine
// emits each tile row as one TRIANGLE_STRIP in the order
// (i,j) (i,j+1) (i+1,j) (i+1,j+1) ..., so every quad's diagonal runs from
// (i,j+1) to (i+1,j) and the field is piecewise linear over two triangles -
// NOT bilinear. Modelling it as bilinear would put the host and the console a
// few percent apart in the middle of every tile, which is exactly where the
// oracle's labels live.
template <int N>
void triLerp(const float v00[N], const float v01[N], const float v10[N],
             const float v11[N], float fx, float fy, float out[N]) {
    if (fx + fy <= 1.0f) {
        const float w0 = 1.0f - fx - fy;
        for (int k = 0; k < N; ++k) out[k] = v00[k] * w0 + v01[k] * fy + v10[k] * fx;
    } else {
        const float w3 = fx + fy - 1.0f;
        for (int k = 0; k < N; ++k)
            out[k] = v11[k] * w3 + v01[k] * (1.0f - fx) + v10[k] * (1.0f - fy);
    }
}

}  // namespace

// ------------------------------------------------------------------ images ---

void Image::resize(int width, int height) {
    w = width;
    h = height;
    px.assign(static_cast<size_t>(w) * h * 4, 0);
}

bool readPng(Image& img, const std::string& path) {
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data) return false;
    img.w = w;
    img.h = h;
    img.px.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

bool readPngMemory(Image& img, const unsigned char* data, size_t n) {
    if (!data || n == 0) return false;
    int w = 0, h = 0, comp = 0;
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(n), &w, &h, &comp, 4);
    if (!px) return false;
    img.w = w;
    img.h = h;
    img.px.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

bool writePng(const Image& img, const std::string& path) {
    if (img.w <= 0 || img.h <= 0) return false;
    return stbi_write_png(path.c_str(), img.w, img.h, 4, img.px.data(), img.w * 4) != 0;
}

Image boxDown(const Image& src, int factor) {
    if (factor <= 1) return src;
    Image out(src.w / factor, src.h / factor);
    const int n = factor * factor;
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            int acc[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < factor; ++sy)
                for (int sx = 0; sx < factor; ++sx) {
                    const uint8_t* p = src.at(x * factor + sx, y * factor + sy);
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                }
            uint8_t* d = out.at(x, y);
            for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>(acc[c] / n);
        }
    return out;
}

double psnr(const Image& a, const Image& b) {
    if (a.w != b.w || a.h != b.h || a.w <= 0) return 0.0;
    double se = 0.0;
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < a.w; ++x) {
            const uint8_t* pa = a.at(x, y);
            const uint8_t* pb = b.at(x, y);
            for (int c = 0; c < 3; ++c) {
                const double d = static_cast<double>(pa[c]) - pb[c];
                se += d * d;
            }
        }
    const double mse = se / (static_cast<double>(a.w) * a.h * 3.0);
    if (mse <= 1e-12) return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

// ------------------------------------------------------------ weight field ---

void WeightField::resize(int c, int r) {
    cols = c;
    rows = r;
    tile.assign(static_cast<size_t>(c) * r, {});
}

namespace {
// A grid corner is the mean of the up-to-four tiles that touch it - the same
// averaging the engine does before it writes vertex alpha.
void cornerOf(const WeightField& f, int i, int j, float out[kOutputs]) {
    float acc[kOutputs] = {};
    int n = 0;
    for (int dy = -1; dy <= 0; ++dy)
        for (int dx = -1; dx <= 0; ++dx) {
            const int cx = i + dx, cy = j + dy;
            if (cx < 0 || cy < 0 || cx >= f.cols || cy >= f.rows) continue;
            const auto& t = f.at(cx, cy);
            for (int k = 0; k < kOutputs; ++k) acc[k] += t[k];
            ++n;
        }
    for (int k = 0; k < kOutputs; ++k) out[k] = n ? acc[k] / n : 0.0f;
}
}  // namespace

std::array<float, kOutputs> WeightField::sample(float px, float py) const {
    std::array<float, kOutputs> out{};
    if (cols <= 0 || rows <= 0) return out;
    const float gx = px / tileSize(), gy = py / tileSize();
    const int i = clampi(static_cast<int>(std::floor(gx)), 0, cols - 1);
    const int j = clampi(static_cast<int>(std::floor(gy)), 0, rows - 1);
    float v00[kOutputs], v01[kOutputs], v10[kOutputs], v11[kOutputs];
    cornerOf(*this, i, j, v00);
    cornerOf(*this, i, j + 1, v01);
    cornerOf(*this, i + 1, j, v10);
    cornerOf(*this, i + 1, j + 1, v11);
    triLerp<kOutputs>(v00, v01, v10, v11, gx - i, gy - j, out.data());
    return out;
}

void ReprojField::resize(int c, int r) {
    cols = c;
    rows = r;
    du.assign(static_cast<size_t>(c + 1) * (r + 1), 0.0f);
    dv.assign(static_cast<size_t>(c + 1) * (r + 1), 0.0f);
}

void ReprojField::sample(float px, float py, int outW, int outH, float* outDu,
                         float* outDv) const {
    *outDu = *outDv = 0.0f;
    if (cols <= 0 || rows <= 0) return;
    const float cx = std::min(std::max(px, 0.0f), static_cast<float>(outW));
    const float cy = std::min(std::max(py, 0.0f), static_cast<float>(outH));
    const float gx = cx / tileSize(), gy = cy / tileSize();
    const int i = clampi(static_cast<int>(std::floor(gx)), 0, cols - 1);
    const int j = clampi(static_cast<int>(std::floor(gy)), 0, rows - 1);
    const int stride = cols + 1;
    const auto pick = [&](int a, int b, float o[2]) {
        const size_t k = static_cast<size_t>(b) * stride + a;
        o[0] = du[k];
        o[1] = dv[k];
    };
    float v00[2], v01[2], v10[2], v11[2], out[2];
    pick(i, j, v00);
    pick(i, j + 1, v01);
    pick(i + 1, j, v10);
    pick(i + 1, j + 1, v11);
    triLerp<2>(v00, v01, v10, v11, gx - i, gy - j, out);
    *outDu = out[0];
    *outDv = out[1];
}

// ---------------------------------------------------------------- features ---

std::vector<TileStats> accumulate(int cols, int rows, int outW, int outH,
                                  const std::vector<BagProxy>& bags) {
    (void)outW;
    (void)outH;
    std::vector<TileStats> stats(static_cast<size_t>(cols) * rows);
    std::vector<float> coverAcc(stats.size(), 0.0f), depthAcc(stats.size(), 0.0f),
        detAcc(stats.size(), 0.0f),
        edgeAcc(stats.size(), 0.0f), dmin(stats.size(), 1e30f), dmax(stats.size(), 0.0f);

    for (const BagProxy& b : bags) {
        if (b.x1 <= b.x0 || b.y1 <= b.y0) continue;
        const float invNear = 1.0f / std::max(b.wNear, 1e-4f);
        const float invFar = 1.0f / std::max(b.wFar, 1e-4f);
        const int cx0 = clampi(static_cast<int>(std::floor(b.x0 / tileSize())), 0, cols - 1);
        const int cx1 = clampi(static_cast<int>(std::floor((b.x1 - 1e-3f) / tileSize())), 0, cols - 1);
        const int cy0 = clampi(static_cast<int>(std::floor(b.y0 / tileSize())), 0, rows - 1);
        const int cy1 = clampi(static_cast<int>(std::floor((b.y1 - 1e-3f) / tileSize())), 0, rows - 1);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx) {
                const float tx0 = static_cast<float>(cx * tileSize()), tx1 = tx0 + tileSize();
                const float ty0 = static_cast<float>(cy * tileSize()), ty1 = ty0 + tileSize();
                const float ox = overlap(b.x0, b.x1, tx0, tx1);
                const float oy = overlap(b.y0, b.y1, ty0, ty1);
                if (ox <= 0.0f || oy <= 0.0f) continue;
                const size_t k = static_cast<size_t>(cy) * cols + cx;
                const float a = (ox * oy) / (static_cast<float>(tileSize()) * tileSize());
                coverAcc[k] += a;
                depthAcc[k] += a * invNear;
                detAcc[k] += a * b.texDetail;
                dmin[k] = std::min(dmin[k], invFar);
                dmax[k] = std::max(dmax[k], invNear);
                // The bag's four bbox EDGES, not its area: an edge crossing the
                // tile is what a silhouette looks like from the EE's keyhole.
                float e = 0.0f;
                if (b.y0 >= ty0 && b.y0 < ty1) e += ox;
                if (b.y1 >= ty0 && b.y1 < ty1) e += ox;
                if (b.x0 >= tx0 && b.x0 < tx1) e += oy;
                if (b.x1 >= tx0 && b.x1 < tx1) e += oy;
                edgeAcc[k] += e;
            }
    }

    for (size_t k = 0; k < stats.size(); ++k) {
        TileStats& s = stats[k];
        const float cw = std::max(coverAcc[k], 1e-6f);
        s.cover = std::min(1.0f, coverAcc[k]);
        if (coverAcc[k] > 0.0f) {
            s.depthMean = depthAcc[k] / cw;
            s.texDetail = detAcc[k] / cw;
            s.depthMin = dmin[k] > 1e29f ? 0.0f : dmin[k];
            s.depthMax = dmax[k];
        }
        s.edge = std::min(1.0f, edgeAcc[k] / (2.0f * tileSize()));
    }
    return stats;
}

ReprojField buildReproj(int cols, int rows, int outW, int outH, int lowW, int lowH,
                        const Pinhole& cur, const Pinhole& prev,
                        const std::vector<TileStats>& stats) {
    ReprojField f;
    f.resize(cols, rows);
    const int stride = cols + 1;
    for (int j = 0; j <= rows; ++j)
        for (int i = 0; i <= cols; ++i) {
            // Representative depth: the covered tiles that touch this corner.
            float invW = 0.0f;
            float wsum = 0.0f;
            for (int dy = -1; dy <= 0; ++dy)
                for (int dx = -1; dx <= 0; ++dx) {
                    const int cx = i + dx, cy = j + dy;
                    if (cx < 0 || cy < 0 || cx >= cols || cy >= rows) continue;
                    const TileStats& s = stats[static_cast<size_t>(cy) * cols + cx];
                    if (s.cover <= 0.0f) continue;
                    invW += s.cover * s.depthMean;
                    wsum += s.cover;
                }
            const size_t k = static_cast<size_t>(j) * stride + i;
            if (wsum <= 0.0f) continue;  // sky: nothing to reproject
            invW /= wsum;
            const float w = 1.0f / std::max(invW, 1e-6f);

            const float px = static_cast<float>(std::min(i * tileSize(), outW));
            const float py = static_cast<float>(std::min(j * tileSize(), outH));
            const float sX = (2.0f * px / outW - 1.0f) * cur.tanHalfFovX;
            const float sY = (1.0f - 2.0f * py / outH) * cur.tanHalfFovY;
            float world[3], rel[3];
            for (int c = 0; c < 3; ++c) {
                const float dir = cur.fwd[c] + cur.right[c] * sX + cur.up[c] * sY;
                world[c] = cur.pos[c] + dir * w;
                rel[c] = world[c] - prev.pos[c];
            }
            const auto dot = [](const float a[3], const float b[3]) {
                return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
            };
            const float wPrev = dot(rel, prev.fwd);
            if (wPrev < 1e-3f) continue;  // behind the previous camera
            const float sXp = dot(rel, prev.right) / (wPrev * prev.tanHalfFovX);
            const float sYp = dot(rel, prev.up) / (wPrev * prev.tanHalfFovY);
            f.du[k] = (sXp * 0.5f + 0.5f) * lowW - px * lowW / outW;
            f.dv[k] = (0.5f - sYp * 0.5f) * lowH - py * lowH / outH;
        }
    return f;
}

std::vector<Features> buildFeatures(int cols, int rows,
                                    const std::vector<TileStats>& stats,
                                    const ReprojField& reproj) {
    std::vector<Features> out(static_cast<size_t>(cols) * rows);
    // Normalised depth first - depthGrad differences these, so it must see the
    // same numbers the network does.
    std::vector<float> nd(out.size(), 0.0f);
    for (size_t k = 0; k < out.size(); ++k)
        nd[k] = clamp01(stats[k].depthMean * kDepthRef);

    for (int cy = 0; cy < rows; ++cy)
        for (int cx = 0; cx < cols; ++cx) {
            const size_t k = static_cast<size_t>(cy) * cols + cx;
            const TileStats& s = stats[k];
            Features& f = out[k];

            // motion is the length of the MEAN of the tile's four corner
            // offsets, NOT the field sampled at the tile centre. The two differ
            // once the field is piecewise linear - triLerp at (0.5, 0.5) weights
            // only two of the four corners - and the engine takes the mean. This
            // is parity, not preference.
            const int rs = cols + 1;
            const size_t k00 = static_cast<size_t>(cy) * rs + cx, k10 = k00 + 1;
            const size_t k01 = k00 + rs, k11 = k01 + 1;
            if (k11 < reproj.du.size()) {
                const float mdu = (reproj.du[k00] + reproj.du[k10] + reproj.du[k01] +
                                   reproj.du[k11]) * 0.25f;
                const float mdv = (reproj.dv[k00] + reproj.dv[k10] + reproj.dv[k01] +
                                   reproj.dv[k11]) * 0.25f;
                f.motion() = clamp01(std::sqrt(mdu * mdu + mdv * mdv) / tileSize());
            }
            f.depth() = nd[k];

            float grad = clamp01((s.depthMax - s.depthMin) * kDepthRef);
            const int dx4[4] = {-1, 1, 0, 0};
            const int dy4[4] = {0, 0, -1, 1};
            for (int n = 0; n < 4; ++n) {
                const int nx = cx + dx4[n], ny = cy + dy4[n];
                if (nx < 0 || ny < 0 || nx >= cols || ny >= rows) continue;
                grad = std::max(grad, std::fabs(nd[static_cast<size_t>(ny) * cols + nx] - nd[k]));
            }
            f.depthGrad() = clamp01(grad);
            f.edgeDens() = clamp01(s.edge);
            f.texDetail() = clamp01(s.texDetail);
            f.coverage() = clamp01(s.cover);
        }
    return out;
}

// WHETHER THE REPROJECTED HISTORY CAN BE BELIEVED IN THIS TILE. The contract is
// in blss.hpp above the declaration.
//
// THIS USED TO BE A PRODUCT OF TWO FEATURE CHANNELS AND THAT WAS MEASURED WRONG
// BEFORE IT WAS EVER SWEPT. The first version returned
//
//     coverage < kMinCoverage ? 0 : (1 - motion) * (1 - depthGrad)
//
// on the reasoning that `motion` at 1.0 puts the history a whole tile away and
// `depthGrad` at 1.0 is a silhouette. Both readings are correct about what the
// channels MEAN and wrong about what they CONTAIN: on examples/upscaler-lab
// (`--blss-eval --features`, 5376 tiles) `motion` reads exactly 1.0 on 49.1% of
// tiles and `depthGrad` on 41.0%, so that product is exactly ZERO on most of the
// frame - and zero specifically on the moving, geometrically busy part, which is
// where the console's difference image lights up. A gate that switches the term
// off wherever the artefact is would have swept as "buys nothing", which is the
// same non-answer `--flicker-weight` gave twice.
//
// So the only per-tile test left is the one that is not a saturated channel:
// a tile with no geometry in it has no history to reproject. Outlier
// reprojections are handled where they belong, by the CLAMP on the charge
// itself (kAltClamp) rather than by a multiplier built out of proxies for them.
//
// HOST-ONLY. There is no engine twin of this and there must not be: it exists
// to gate the OBJECTIVE, which the console never evaluates
// (docs/blss-reconstruction.md, "What is NOT part of this contract").
float reprojConfidence(const Features& f) {
    return f.coverage() < kMinCoverage ? 0.0f : 1.0f;
}

// ----------------------------------------------------------------- network ---

namespace {
// Deterministic PRNG - no std::random, whose distributions are not portable
// across standard libraries and would make a trained net irreproducible.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uniform() { return static_cast<float>(next() >> 8) / 16777216.0f; }
    float symmetric() { return uniform() * 2.0f - 1.0f; }
};
}  // namespace

void Net::randomize(uint32_t seed) {
    Rng rng(seed);
    const float s1 = std::sqrt(1.0f / kFeatures);
    const float s2 = std::sqrt(1.0f / kHidden);
    for (int k = 0; k < kHidden; ++k) {
        for (int i = 0; i < kFeatures; ++i) w1[k][i] = rng.symmetric() * s1;
        b1[k] = 0.0f;
    }
    for (int m = 0; m < kOutputs; ++m) {
        for (int k = 0; k < kHidden; ++k) w2[m][k] = rng.symmetric() * s2;
        b2[m] = 0.0f;
    }
}

void Net::forward(const Features& f, float out[kOutputs]) const {
    float h[kHidden];
    for (int k = 0; k < kHidden; ++k) {
        float a = b1[k];
        for (int i = 0; i < kFeatures; ++i) a += w1[k][i] * f.v[i];
        h[k] = actTanh(a);
    }
    for (int m = 0; m < kOutputs; ++m) {
        float a = b2[m];
        for (int k = 0; k < kHidden; ++k) a += w2[m][k] * h[k];
        out[m] = actLogistic(a);
    }
}

namespace {
// 3: kFeatures went 8 -> 7 -> 6 as the recurrent `histAge` channel and then the
// photometric `luma` one were measured and removed (see blss.hpp). The file
// carries no topology, only a flat float run, so an older net would load
// silently into a differently shaped Net and produce nonsense - bump this with
// ANY change to kFeatures / kHidden / kOutputs.
constexpr uint32_t kNetVersion = 3;
constexpr size_t kNetFloats = kHidden * kFeatures + kHidden + kOutputs * kHidden + kOutputs;

void netToFlat(const Net& n, float* d) {
    size_t o = 0;
    for (int k = 0; k < kHidden; ++k)
        for (int i = 0; i < kFeatures; ++i) d[o++] = n.w1[k][i];
    for (int k = 0; k < kHidden; ++k) d[o++] = n.b1[k];
    for (int m = 0; m < kOutputs; ++m)
        for (int k = 0; k < kHidden; ++k) d[o++] = n.w2[m][k];
    for (int m = 0; m < kOutputs; ++m) d[o++] = n.b2[m];
}
void flatToNet(Net& n, const float* d) {
    size_t o = 0;
    for (int k = 0; k < kHidden; ++k)
        for (int i = 0; i < kFeatures; ++i) n.w1[k][i] = d[o++];
    for (int k = 0; k < kHidden; ++k) n.b1[k] = d[o++];
    for (int m = 0; m < kOutputs; ++m)
        for (int k = 0; k < kHidden; ++k) n.w2[m][k] = d[o++];
    for (int m = 0; m < kOutputs; ++m) n.b2[m] = d[o++];
}
}  // namespace

bool save(const Net& n, const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "cannot open " + path + " for write";
        return false;
    }
    float flat[kNetFloats];
    netToFlat(n, flat);
    const uint32_t ver = kNetVersion;
    bool ok = std::fwrite("BLSS", 1, 4, f) == 4;
    ok = ok && std::fwrite(&ver, sizeof(ver), 1, f) == 1;
    ok = ok && std::fwrite(flat, sizeof(float), kNetFloats, f) == kNetFloats;
    std::fclose(f);
    if (!ok && err) *err = "short write to " + path;
    return ok;
}

// The file is EXACTLY 8 + kNetFloats*4 bytes and nothing else is this format.
// The length check is new and it is not pedantry: the old reader took the first
// 500 bytes of whatever it was handed, so a net written by a future build with
// more weights - or a file that merely starts with "BLSS" - loaded silently as a
// prefix. It cannot change any published md5, because a file of the right length
// is unaffected by a check on the length.
constexpr size_t kNetBytes = 4 + sizeof(uint32_t) + kNetFloats * sizeof(float);

bool loadMemory(Net& n, const unsigned char* data, size_t bytes, std::string* err) {
    if (!data || bytes != kNetBytes || std::memcmp(data, "BLSS", 4) != 0) {
        if (err)
            *err = "not a BLSS net (want " + std::to_string(kNetBytes) + " bytes, got " +
                   std::to_string(bytes) + ")";
        return false;
    }
    uint32_t ver = 0;
    std::memcpy(&ver, data + 4, sizeof(ver));
    if (ver != kNetVersion) {
        if (err)
            *err = "BLSS net is version " + std::to_string(ver) + ", this build reads version " +
                   std::to_string(kNetVersion) + " (the topology changed - refit it)";
        return false;
    }
    float flat[kNetFloats];
    std::memcpy(flat, data + 8, sizeof(flat));
    flatToNet(n, flat);
    return true;
}

bool load(Net& n, const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    unsigned char buf[kNetBytes + 1];
    const size_t got = std::fread(buf, 1, sizeof(buf), f);
    std::fclose(f);
    std::string why;
    if (!loadMemory(n, buf, got, &why)) {
        if (err) *err = path + ": " + why;
        return false;
    }
    return true;
}

// --------------------------------------------------------------- provenance ---
//
// Deterministic key/value text, one field per line, no timestamp - see the
// Provenance comment in blss.hpp for why this is a sidecar and not a longer file
// header. Unknown keys are IGNORED rather than refused, so a sidecar written by
// a newer editor still tells an older one the four things it checks.

std::string provenancePath(const std::string& netPath) { return netPath + ".meta"; }

// WHERE THE FILE ACTUALLY LANDED, spelled so it cannot be read two ways.
//
// `--blss-train <projectDir>` resolves its default net against the PROJECT now
// (parseCli), not the cwd, which is the fix for a net that used to be written
// where the build never reads it. The half that makes that fix checkable is
// this one: the line the tool prints has to name a path the reader can paste,
// and `examples/showcase/blss.net` names a different file from every directory.
// That ambiguity cost a hardware round - the rebuild went on baking the shipped
// default and the only evidence was a relative path nobody could resolve.
//
// `weakly_canonical` rather than `canonical` because the net does not exist yet
// at the moment the caller formats the message, and a filesystem error must
// NEVER turn a successful train into a failure - a net that was fitted, written
// and then could not be pretty-printed is still a fitted net, so the plain
// string is the fallback.
std::string displayPath(const std::string& p) {
    if (p.empty()) return p;
    std::error_code ec;
    const std::filesystem::path abs = std::filesystem::weakly_canonical(p, ec);
    if (ec || abs.empty()) {
        const std::filesystem::path fb = std::filesystem::absolute(p, ec);
        if (ec) return p;
        return fb.lexically_normal().string();
    }
    return abs.string();
}

Provenance currentProvenance() {
    Provenance p;
    p.present = true;
    p.netVersion = kNetVersion;
    p.features = kFeatures;
    p.hidden = kHidden;
    p.outputs = kOutputs;
    p.tile = tileSize();
    p.actTable = detail::gActN;
    return p;
}

std::string formatProvenance(const Provenance& p) {
    std::string s =
        "# BLSS net provenance (docs/neural-upscaler.md). Written beside the net\n"
        "# it describes; the net's own bytes carry only magic + net-version, so\n"
        "# everything here is what a consumer needs to know that a net which\n"
        "# LOADS is also the right net. No timestamp and no editor version on\n"
        "# purpose - re-running `command` must reproduce this file byte for byte.\n"
        "blss-provenance 1\n";
    char buf[512];
    const auto line = [&](const char* fmt, auto... a) {
        std::snprintf(buf, sizeof(buf), fmt, a...);
        s += buf;
    };
    line("net-version %u\n", p.netVersion);
    line("features %d\n", p.features);
    line("hidden %d\n", p.hidden);
    line("outputs %d\n", p.outputs);
    line("tile %d\n", p.tile);
    if (p.actTable >= 0) line("act-table %d\n", p.actTable);
    if (p.scale.x > 0 && p.scale.y > 0) s += "scale " + scaleName(p.scale) + "\n";
    if (p.jitter >= 0) line("jitter %d\n", p.jitter);
    if (p.sharpen >= 0) line("sharpen %.9g\n", static_cast<double>(p.sharpen));
    if (p.frames > 0) line("frames %d\n", p.frames);
    if (p.shots > 0) line("shots %d\n", p.shots);
    if (p.epochs > 0) line("epochs %d\n", p.epochs);
    line("seed 0x%X\n", p.seed);
    if (!p.corpus.empty()) s += "corpus " + p.corpus + "\n";
    if (!p.command.empty()) s += "command " + p.command + "\n";
    return s;
}

Provenance parseProvenance(const char* text, size_t n) {
    Provenance p;
    if (!text || n == 0) return p;
    const std::string all(text, n);
    size_t at = 0;
    bool magic = false;
    while (at <= all.size()) {
        size_t eol = all.find('\n', at);
        if (eol == std::string::npos) eol = all.size();
        std::string ln = all.substr(at, eol - at);
        at = eol + 1;
        while (!ln.empty() && (ln.back() == '\r' || ln.back() == ' ')) ln.pop_back();
        if (ln.empty() || ln[0] == '#') continue;
        const size_t sp = ln.find(' ');
        const std::string key = ln.substr(0, sp);
        const std::string val = sp == std::string::npos ? std::string() : ln.substr(sp + 1);
        const auto i = [&] { return std::atoi(val.c_str()); };
        if (key == "blss-provenance") magic = true;
        else if (key == "net-version") p.netVersion = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
        else if (key == "features") p.features = i();
        else if (key == "hidden") p.hidden = i();
        else if (key == "outputs") p.outputs = i();
        else if (key == "tile") p.tile = i();
        else if (key == "act-table") p.actTable = i();
        else if (key == "scale") parseScale(val, &p.scale);
        else if (key == "jitter") p.jitter = i() ? 1 : 0;
        else if (key == "sharpen") p.sharpen = static_cast<float>(std::atof(val.c_str()));
        else if (key == "frames") p.frames = i();
        else if (key == "shots") p.shots = i();
        else if (key == "epochs") p.epochs = i();
        else if (key == "seed") p.seed = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 0));
        else if (key == "corpus") p.corpus = val;
        else if (key == "command") p.command = val;
    }
    p.present = magic;
    return p;
}

bool writeProvenance(const Provenance& p, const std::string& netPath, std::string* err) {
    const std::string path = provenancePath(netPath);
    const std::string body = formatProvenance(p);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = "cannot open " + path + " for write";
        return false;
    }
    const bool ok = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    std::fclose(f);
    if (!ok && err) *err = "short write to " + path;
    return ok;
}

Provenance readProvenance(const std::string& netPath) {
    FILE* f = std::fopen(provenancePath(netPath).c_str(), "rb");
    if (!f) return Provenance{};
    std::string body;
    char buf[1024];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) body.append(buf, got);
    std::fclose(f);
    return parseProvenance(body.data(), body.size());
}

NetIssues checkProvenance(const Provenance& p, const NetExpect& want) {
    NetIssues out;
    if (!p.present) {
        out.warn.push_back(
            "no provenance sidecar next to this net - its corpus, raster scale and sampler "
            "are unknown, so nothing here can be checked");
        return out;
    }
    const auto num = [](int v) { return std::to_string(v); };
    // FATAL: the weights are not a net of this shape. load() already refuses a
    // wrong net-version, so reaching here means the sidecar disagrees with the
    // file it sits next to - which is a sidecar from a different net.
    if (p.netVersion != kNetVersion)
        out.fatal.push_back("provenance says net-version " + num(static_cast<int>(p.netVersion)) +
                            ", this build is " + num(static_cast<int>(kNetVersion)));
    if (p.features != kFeatures || p.hidden != kHidden || p.outputs != kOutputs)
        out.fatal.push_back("provenance says topology " + num(p.features) + "-" + num(p.hidden) +
                            "-" + num(p.outputs) + ", this build is " + num(kFeatures) + "-" +
                            num(kHidden) + "-" + num(kOutputs));
    if (p.tile > 0 && p.tile != tileSize())
        out.fatal.push_back("fitted at tile " + num(p.tile) + " px, this build decides on " +
                            num(tileSize()) + " px tiles");
    // WARN: the net runs, it was just fitted for something else. Each of these
    // is a measured difference and none of them is a broken picture.
    if (p.actTable >= 0 && want.actTable >= 0 && p.actTable != want.actTable)
        out.warn.push_back("fitted against activation table " + num(p.actTable) +
                           ", this run evaluates it against " + num(want.actTable) +
                           " (the labels came from a different tanh)");
    if (want.scale.x > 0 && p.scale.x > 0 && p.scale != want.scale)
        out.warn.push_back("fitted at raster scale " + scaleName(p.scale) +
                           ", this project renders at " + scaleName(want.scale));
    if (want.jitter >= 0 && p.jitter >= 0 && p.jitter != want.jitter)
        out.warn.push_back(std::string("fitted with sub-pixel jitter ") +
                           (p.jitter ? "ON" : "OFF") + ", this project ships it " +
                           (want.jitter ? "ON" : "OFF") +
                           " - the sampler changes what there is to reconstruct, so the two"
                           " are different experiments (examples/procedural reads a ceiling"
                           " of +0.773 dB jittered and +0.345 unjittered)");
    return out;
}

bool defaultNet(Net& n, std::string* err) {
    std::string why;
    if (loadMemory(n, blssdefault::kDefaultNet, blssdefault::kDefaultNetSize, &why)) return true;
    if (err)
        *err = "the editor's built-in default network cannot be read (" + why +
               ") - resources/blss-default.net was not refitted after the topology moved";
    return false;
}

Provenance defaultProvenance() {
    return parseProvenance(reinterpret_cast<const char*>(blssdefault::kDefaultNetMeta),
                           blssdefault::kDefaultNetMetaSize);
}

namespace {

// ONE WEIGHT, AS A C++ FLOATING LITERAL THAT IS ACTUALLY ONE.
//
// `%.9g` renders 0.0f as "0" and 1.0f as "1", and "0F" is not a float literal -
// it is an integer followed by a user-defined-literal suffix that does not
// exist, so the generated header fails to compile with
// `unable to find numeric literal operator 'operator""F'`. Every bias starts at
// exactly 0 (Net::randomize), and templates.cpp emits a DEFAULT-CONSTRUCTED Net
// when a project has BLSS enabled and no blss.net - all 123 weights zero - so
// the "a missing net is not a build failure, just an untrained network and a
// warning banner" path in blssNetHeader could never once have worked. It was
// never executed, which is the same class of miss as the rest of this feature's
// history: something the docs asserted and nothing ran.
//
// %.9g is kept (it round-trips a float exactly and stays readable); the decimal
// point is added afterwards when the rendering carries neither '.' nor an
// exponent. Non-finite weights cannot be spelled as a literal at all, so they
// are written as zero and counted - the caller puts a banner at the top of the
// file rather than shipping a header that does not compile.
std::string floatLiteral(float v, int* nonFinite) {
    if (!std::isfinite(v)) {
        if (nonFinite) ++*nonFinite;
        return "0.0F";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(v));
    std::string t(buf);
    if (t.find('.') == std::string::npos && t.find('e') == std::string::npos &&
        t.find('E') == std::string::npos)
        t += ".0";
    return t + "F";
}

// Does this text parse as a C++ floating literal with an F suffix? Deliberately
// stricter than "it looked fine": the regression this guards is a header that
// only fails at the point where it is compiled inside a Docker PS2 build, days
// away from whoever changed the emitter.
bool isFloatLiteral(const std::string& t) {
    if (t.size() < 3 || t.back() != 'F') return false;
    const std::string body = t.substr(0, t.size() - 1);
    size_t i = (body[0] == '-' || body[0] == '+') ? 1 : 0;
    bool digits = false, dot = false, exp = false;
    for (; i < body.size(); ++i) {
        const char c = body[i];
        if (c >= '0' && c <= '9') { digits = true; continue; }
        if (c == '.' && !dot && !exp) { dot = true; continue; }
        if ((c == 'e' || c == 'E') && digits && !exp) {
            exp = true;
            if (i + 1 < body.size() && (body[i + 1] == '-' || body[i + 1] == '+')) ++i;
            if (i + 1 >= body.size()) return false;
            continue;
        }
        return false;
    }
    // An integer-looking body is exactly the bug: "0F" and "1F" are not floats.
    return digits && (dot || exp);
}

}  // namespace

namespace {
// The values that break the emitter are the boring ones: an all-zero net is
// precisely what a project with no blss.net emits, and 1 / -1 are what a
// saturated weight rounds to. Checked on every emit - it is eleven snprintfs
// against a header that is otherwise only ever compiled inside a Docker PS2
// build, days away from whoever changed the formatting.
bool literalProbesOk(std::string* bad) {
    static const float probes[] = {0.0f,  -0.0f, 1.0f,   -1.0f,   2.0f,    1e9f,
                                   1e-9f, 0.5f,  -0.25f, 3.4e38f, 1.0e-38f};
    for (float v : probes) {
        int nf = 0;
        const std::string lit = floatLiteral(v, &nf);
        if (!isFloatLiteral(lit)) {
            if (bad) *bad = lit + "' for " + std::to_string(v);
            return false;
        }
    }
    return true;
}
}  // namespace

bool selfTestEmitter(std::string* err) {
    std::string bad;
    if (!literalProbesOk(&bad)) {
        if (err) *err = "emitter produced '" + bad;
        return false;
    }
    // And the whole header for the untrained net, token by token: the failure
    // mode was 147 bad literals in a file nobody compiled on the host.
    Net zero;  // every weight and bias exactly 0
    const std::string src = emitGeneratedSource(zero);
    size_t at = src.find('{');
    while (at != std::string::npos) {
        const size_t end = src.find('}', at);
        if (end == std::string::npos) break;
        std::string tok;
        for (size_t i = at + 1; i <= end; ++i) {
            const char c = src[i];
            if (c == ',' || c == '}' || c == '\n' || c == ' ') {
                if (!tok.empty()) {
                    if (!isFloatLiteral(tok)) {
                        if (err) *err = "emitted header contains '" + tok + "', not a float literal";
                        return false;
                    }
                    tok.clear();
                }
            } else {
                tok += c;
            }
        }
        at = src.find('{', end);
    }
    return true;
}

std::string emitGeneratedSource(const Net& n) {
    float flat[kNetFloats];
    netToFlat(n, flat);
    int nonFinite = 0;
    std::string s;
    s += "// GENERATED by tyrax-editor --blss-emit. Do not edit.\n";
    s += "// The trained BLSS network (docs/neural-upscaler.md): an MLP\n";
    // Spelled from the constants, not typed: kHidden is swept, and a header
    // that claims a topology the tables do not have is worse than no comment.
    s += "// " + std::to_string(kFeatures) + " -> " + std::to_string(kHidden) + " -> " +
         std::to_string(kOutputs) + " (" + std::to_string(kNetFloats) +
         " weights), tanh hidden, logistic outputs.\n";
    s += "// Handed to RendererCoreBlss::setNet(), which runs it per 32x32 screen tile.\n";
    s += "// The engine's kHidden must match: vendor/tyra/engine/inc/renderer/core/blss.\n";
    s += "//\n// Inputs, in order:";
    for (int i = 0; i < kFeatures; ++i) {
        s += i ? ", " : " ";
        s += kFeatureNames[i];
    }
    s += "\n// Outputs, in order:";
    for (int m = 0; m < kOutputs; ++m) {
        s += m ? ", " : " ";
        s += kOutputNames[m];
    }
    s += "\n\n#pragma once\n\n";

    const auto table = [&](const char* name, int count, size_t at, int perLine) {
        s += "static const float " + std::string(name) + "[" + std::to_string(count) + "] = {\n   ";
        for (int i = 0; i < count; ++i) {
            s += " " + floatLiteral(flat[at + i], &nonFinite) + ",";
            if ((i + 1) % perLine == 0 && i + 1 < count) s += "\n   ";
        }
        s += "};\n\n";
    };
    size_t at = 0;
    table("BLSS_NET_W1", kHidden * kFeatures, at, kFeatures);
    at += kHidden * kFeatures;
    table("BLSS_NET_B1", kHidden, at, 6);
    at += kHidden;
    table("BLSS_NET_W2", kOutputs * kHidden, at, kHidden);
    at += kOutputs * kHidden;
    table("BLSS_NET_B2", kOutputs, at, 3);
    if (nonFinite > 0)
        s.insert(0, "// WARNING: " + std::to_string(nonFinite) +
                        " weight(s) were not finite and were written as 0.\n"
                        "// The .net file is corrupt - retrain rather than shipping this.\n");
    // The codegen path (templates.cpp) calls this function directly, not
    // emitMain, so the guard has to live here or it does not cover the case that
    // actually shipped broken. A legible #error beats
    // "unable to find numeric literal operator" from inside a Docker build.
    std::string bad;
    // No apostrophe and no quote in the message: an unpaired one inside a
    // #error draws a "missing terminating ' character" warning out of GCC on
    // every translation unit that reads the file - this line used to open one
    // and never close it. See errorSafe() in templates.cpp.
    if (!literalProbesOk(&bad))
        s.insert(0, "#error BLSS emitter is producing invalid float literals: " +
                        bad + "\n");
    return s;
}

// --------------------------------------------------------------- composite ---

namespace {

// Everything the composite needs to sample for one output pixel, gathered once
// so the oracle can sweep weights without re-sampling. `base`, `point` and
// `blur` come from the low-res target, `hist` from the history.
struct Taps {
    int base[3];
    int point[3];
    int hist[3];
    int blur[3];
    // THE OTHER JITTER PHASE OF THE SAME CONTENT, motion-compensated: the three
    // low-res taps again, taken from the PREVIOUS frame's render at the
    // reprojected position. Only filled when the caller asks (the period-2
    // flicker term), because they are three extra samples per pixel and every
    // other consumer of this struct is the composite, which does not need them.
    //
    // They come from fr.prevLow ALWAYS, never from fr.history, and that is
    // deliberate: the period-2 model is about the two phases the LOW-RES SAMPLER
    // produces, and fr.history is the previous frame's finished composite in
    // --blss-eval and the upscaled prevLow only during labelling. Reading
    // prevLow directly makes the term mean the same thing in both.
    int pbase[3];
    int ppoint[3];
    int pblur[3];
    // How much that pair can be believed - 0 disables the term for this sample.
    // See reprojConfidence(); the per-sample half is the on-screen test.
    float conf = 0.0f;
};

void gatherTaps(const Frame& fr, int x, int y, Taps& t, bool wantPhasePair = false) {
    const int sx = scaleX(fr.scale), sy = scaleY(fr.scale);
    const float jx = jitterX(fr.phase), jy = jitterY(fr.phase);
    // Sampling undoes the jitter: content drawn at p + j is stored at p + j.
    const float u = (x + 0.5f) / sx + jx;
    const float v = (y + 0.5f) / sy + jy;
    sampleBilinear(fr.low, u, v, t.base);
    sampleNearest(fr.low, u, v, t.point);
    sampleBilinear(fr.low, u + 0.5f, v + 0.5f, t.blur);

    float du = 0.0f, dv = 0.0f;
    fr.reproj.sample(x + 0.5f, y + 0.5f, fr.outW, fr.outH, &du, &dv);
    const float pjx = jitterX(1 - fr.phase), pjy = jitterY(1 - fr.phase);
    if (fr.history) {
        sampleBilinear(*fr.history, x + 0.5f + du, y + 0.5f + dv, t.hist);
    } else {
        // No real history (training): stand in with the previous low-res render
        // upscaled, i.e. what the composite would have produced at all-zero
        // weights. Its own jitter phase is the other one.
        sampleBilinear(fr.prevLow, (x + 0.5f + du) / sx + pjx, (y + 0.5f + dv) / sy + pjy,
                       t.hist);
    }

    if (!wantPhasePair) return;
    const float px = x + 0.5f + du, py = y + 0.5f + dv;
    const float pu = px / sx + pjx, pv = py / sy + pjy;
    sampleBilinear(fr.prevLow, pu, pv, t.pbase);
    sampleNearest(fr.prevLow, pu, pv, t.ppoint);
    sampleBilinear(fr.prevLow, pu + 0.5f, pv + 0.5f, t.pblur);
    // Off the edge of the previous frame there is no history to have fused, so
    // there is nothing to charge for. sampleBilinear would clamp and invent a
    // difference out of the border row.
    if (px < 0.0f || py < 0.0f || px >= static_cast<float>(fr.outW) ||
        py >= static_cast<float>(fr.outH)) {
        t.conf = 0.0f;
        return;
    }
    const int ti = clampi(x / tileSize(), 0, std::max(fr.cols - 1, 0));
    const int tj = clampi(y / tileSize(), 0, std::max(fr.rows - 1, 0));
    const size_t k = static_cast<size_t>(tj) * fr.cols + ti;
    t.conf = k < fr.features.size() ? reprojConfidence(fr.features[k]) : 0.0f;
}

// THE PERIOD-2 ALTERNATION THIS CANDIDATE WOULD LEAVE, per channel, in 8-bit
// levels. Derived rather than sampled, which is what makes it usable inside the
// oracle's innermost loop and what makes it a statement about the ARTEFACT
// instead of about a frame-to-frame difference.
//
// THE DERIVATION. The console alternates two jitter phases forever. Write the
// composite of one frame as
//
//      out_t = S_p( (1-c) * P_p + c * out_{t-1} ),   p = t mod 2
//
// where c = aC/128 is the temporal blend factor the GS applies, P_p is the
// base+point result of phase p (passes 1 and 2), and S_p(v) = v + A_p is the
// unsharp mask (passes 4 and 5), which is applied AFTER the accumulator and is
// therefore never fed back through it. Solving the two-frame steady state:
//
//      out_0 - out_1 = [ (1-c) * (P_0 - P_1)  +  (A_0 - A_1) ] / (1 + c)
//
// Three things fall out of that one line, and all three are load-bearing:
//
//  - the base and point passes' phase difference IS damped by the accumulator,
//    by (1-c)/(1+c) - 1.00 at c = 0, 0.73 at alpha 20, 0.054 at the kTemporalMax
//    ceiling. So temporal weight is the only cure for it, and a fill term that
//    culls the temporal pass makes the bob WORSE, not better. That is the
//    mechanism the "the fill term fixed it" belief missed;
//  - the sharpen pass's phase difference is NOT damped at all - it is divided by
//    (1+c) and no more, because it lands after the feedback. The only way to
//    remove it is aD = 0;
//  - nothing in it is a difference against the history, so nothing in it is
//    minimised by out == history. FREEZING IS NOT EXPRESSIBLE HERE. The two ways
//    down are raising c (fusing the phases, which is the cure) and lowering
//    aA/aD (not amplifying them), and both are real.
//
// It is a LINEARISED estimate: the GS clamps every pass to 0..255 and this does
// not, because the derivation is only valid while the recursion is affine. At a
// saturated pixel it overstates the alternation, which is the safe direction for
// a penalty.
//
// HOW BIG AN ALTERNATION IS STILL AN ALTERNATION, in 8-bit levels. Above this
// the charge is a constant, so the oracle cannot buy anything by moving it - see
// the "why a clamp and not a gate" note in errRegion(). The scale is set by the
// artefact: the console measured 1.42/255 mean and 13.8 at p99 with a frozen
// camera, and on this corpus the jitter-on/jitter-off gap in the lag-1 column is
// 1.4-2.4 levels. 8 is comfortably above every one of those and far below what a
// disocclusion produces, which is tens.
constexpr double kAltClamp = 8.0;

inline float altAmplitude(const Taps& t, int aA, int aC, int aD, int ch) {
    const float c = static_cast<float>(aC) * (1.0f / 128.0f);
    const auto pointPass = [&](int base, int pt) {
        return aA > 0 ? base + (((pt - base) * aA) >> 7) : base;
    };
    const auto sharpenPass = [&](int base, int blur) {
        return aD > 0 ? ((base * aD) >> 7) - ((blur * aD) >> 7) : 0;
    };
    const float dP = static_cast<float>(pointPass(t.base[ch], t.point[ch]) -
                                        pointPass(t.pbase[ch], t.ppoint[ch]));
    const float dA = static_cast<float>(sharpenPass(t.base[ch], t.blur[ch]) -
                                        sharpenPass(t.pbase[ch], t.pblur[ch]));
    return ((1.0f - c) * dP + dA) / (1.0f + c);
}

// The five GS passes, in 8-bit, on already-gathered taps.
inline void blend(const Taps& t, int aA, int aC, int aD, int out[3]) {
    for (int c = 0; c < 3; ++c) {
        int v = t.base[c];
        if (aA > 0) v = clamp255((((t.point[c] - v) * aA) >> 7) + v);
        if (aC > 0) v = clamp255((((t.hist[c] - v) * aC) >> 7) + v);
        if (aD > 0) {
            v = clamp255(v + ((t.base[c] * aD) >> 7));
            v = clamp255(v - ((t.blur[c] * aD) >> 7));
        }
        out[c] = v;
    }
}

inline void alphaBytes(const float w[kOutputs], float sharpen, int& aA, int& aC, int& aD) {
    // The three scales live in blss.hpp (alphaScales) because the deadzone and
    // the engine's cornerAlpha() have to agree with them exactly.
    //
    // The temporal one is capped at kTemporalMax because the pass is an
    // EXPONENTIAL ACCUMULATOR, not a two-frame average: the history is the
    // previous frame's own composite, so wC = 1 means "keep kTemporalMax/128 of
    // everything that came before". It used to cap at 64 (a flat 50% mix), which
    // is where the visible bob came from - a 50% accumulator has a time constant
    // of about one frame, so against a jitter that alternates every frame it
    // TRACKS the alternation instead of averaging it out, and settles into a
    // stationary +-1/3 pixel oscillation. Per-frame PSNR cannot see it (the mix
    // of two phases is genuinely closer to the truth than either), which is why
    // the flicker metric in --blss-eval now exists.
    float s[kOutputs];
    alphaScales(sharpen, s);
    aA = static_cast<int>(clamp01(w[0]) * s[0]);
    aC = static_cast<int>(clamp01(w[1]) * s[1]);
    aD = static_cast<int>(clamp01(w[2]) * s[2]);
}

// WHICH PASSES ONE GRID CELL MAKES THE GS DRAW.
//
// This is the engine's skip rule, mirrored: `emitGrid` walks the cells of a row
// and breaks the triangle strip at any cell whose four corner alpha bytes are
// ALL zero, so a cell is drawn as soon as one corner is non-zero. Passes 4 and
// 5 are the two halves of the same unsharp mask and are drawn together, so
// sharpen costs twice what point or temporal do. Pass 1 is the opaque base -
// always drawn, and therefore free to any decision.
//
// The corner ALPHA BYTE is the test, not the weight. Truncation to a byte is
// what the hardware does and it is a step: at sharpen 0.5 a corner weight of
// 0.008 rounds to alpha 0 and costs nothing, 0.016 rounds to 1 and costs two
// whole passes for a change no one can see. An objective that penalised the
// weight smoothly would happily park there.
enum CellDrawn { kDrawPoint = 1, kDrawTemporal = 2, kDrawSharpen = 4 };

inline int cellDrawn(const float* c00, const float* c01, const float* c10, const float* c11,
                     float sharpen) {
    int anyA = 0, anyC = 0, anyD = 0;
    for (const float* c : {c00, c01, c10, c11}) {
        int aA, aC, aD;
        alphaBytes(c, sharpen, aA, aC, aD);
        anyA |= aA;
        anyC |= aC;
        anyD |= aD;
    }
    return (anyA ? kDrawPoint : 0) | (anyC ? kDrawTemporal : 0) | (anyD ? kDrawSharpen : 0);
}

// What that cell costs, in full-screen-cell draws: sharpen counts TWICE because
// passes 4 and 5 are the two halves of one unsharp mask and are always drawn
// together. 0 = the cell is skipped entirely and only the opaque base covers it.
inline int cellCost(int drawn) {
    return ((drawn & kDrawPoint) ? 1 : 0) + ((drawn & kDrawTemporal) ? 1 : 0) +
           ((drawn & kDrawSharpen) ? 2 : 0);
}

}  // namespace

Occupancy occupancy(const WeightField& wf, float sharpen) {
    Occupancy occ;
    if (wf.cols <= 0 || wf.rows <= 0) return occ;
    // The grid's cells are the tiles; a cell's corners are the corner-averaged
    // weights the runtime ships as vertex alpha.
    std::vector<std::array<float, kOutputs>> cc(
        static_cast<size_t>(wf.cols + 1) * (wf.rows + 1));
    for (int j = 0; j <= wf.rows; ++j)
        for (int i = 0; i <= wf.cols; ++i)
            cornerOf(wf, i, j, cc[static_cast<size_t>(j) * (wf.cols + 1) + i].data());

    const int stride = wf.cols + 1;
    long nA = 0, nC = 0, nD = 0;
    for (int cy = 0; cy < wf.rows; ++cy)
        for (int cx = 0; cx < wf.cols; ++cx) {
            const size_t k = static_cast<size_t>(cy) * stride + cx;
            const float* c00 = cc[k].data();
            const float* c01 = cc[k + stride].data();
            const float* c10 = cc[k + 1].data();
            const float* c11 = cc[k + stride + 1].data();
            const int drawn = cellDrawn(c00, c01, c10, c11, sharpen);
            nA += (drawn & kDrawPoint) ? 1 : 0;
            nC += (drawn & kDrawTemporal) ? 1 : 0;
            nD += (drawn & kDrawSharpen) ? 1 : 0;
        }
    const float cells = static_cast<float>(wf.cols) * wf.rows;
    occ.point = static_cast<float>(nA) / cells;
    occ.temporal = static_cast<float>(nC) / cells;
    occ.sharpen = static_cast<float>(nD) / cells;
    occ.passes = 1.0f + occ.point + occ.temporal + 2.0f * occ.sharpen;
    return occ;
}

void composite(const Frame& fr, const WeightField& wf, float sharpen, Image& out) {
    out.resize(fr.outW, fr.outH);
    for (int y = 0; y < fr.outH; ++y)
        for (int x = 0; x < fr.outW; ++x) {
            const auto w = wf.sample(x + 0.5f, y + 0.5f);
            int aA, aC, aD;
            alphaBytes(w.data(), sharpen, aA, aC, aD);
            Taps t;
            gatherTaps(fr, x, y, t);
            int rgb[3];
            blend(t, aA, aC, aD, rgb);
            uint8_t* d = out.at(x, y);
            d[0] = static_cast<uint8_t>(rgb[0]);
            d[1] = static_cast<uint8_t>(rgb[1]);
            d[2] = static_cast<uint8_t>(rgb[2]);
            d[3] = 255;
        }
}

void kernelOnly(const Frame& fr, Kernel k, float sharpen, Image& out) {
    WeightField wf;
    wf.resize(fr.cols, fr.rows);
    std::array<float, kOutputs> w{};
    switch (k) {
        case Kernel::Point: w[0] = 1.0f; break;
        case Kernel::Bilinear: break;  // the base pass alone
        case Kernel::Temporal: w[1] = 1.0f; break;
        case Kernel::Sharpen: w[2] = 1.0f; break;
    }
    for (auto& t : wf.tile) t = w;
    composite(fr, wf, sharpen, out);
}

// ------------------------------------------------------------------ oracle ---

WeightField oracle(const Frame& fr, const Image& truth, float sharpen,
                   std::vector<float>* importance, const Objective& obj) {
    const int cols = fr.cols, rows = fr.rows;
    WeightField wf;
    wf.resize(cols, rows);
    if (importance) importance->assign(static_cast<size_t>(cols) * rows, 0.0f);
    if (cols <= 0 || rows <= 0) return wf;

    // Every 3rd pixel in both axes. The weights that come out move well under a
    // quantisation step of the alpha byte, and the sweep below evaluates a 3x3
    // tile neighbourhood per candidate, so the sampling is what keeps it cheap.
    constexpr int kStep = 3;
    const int sw = (fr.outW + kStep - 1) / kStep;
    const int sh = (fr.outH + kStep - 1) / kStep;
    // The period-2 term needs the other jitter phase of every sample, which is
    // three more taps each. Gathered only when the term is switched on, so the
    // shipped fill-only objective pays nothing for it.
    const bool wantFlicker = obj.flicker > 0.0f;
    const bool wantPeriod2 = wantFlicker && obj.flickerForm == FlickerForm::Period2;
    std::vector<Taps> taps(static_cast<size_t>(sw) * sh);
    for (int sy = 0; sy < sh; ++sy)
        for (int sx = 0; sx < sw; ++sx)
            gatherTaps(fr, sx * kStep, sy * kStep, taps[static_cast<size_t>(sy) * sw + sx],
                       wantPeriod2);

    // Error over a rectangle of tiles, measured through the field the GS
    // ACTUALLY rasterises: tile weights -> corner means -> triangle
    // interpolation. This is the whole point of the rewrite. Fitting each tile
    // independently with a constant weight produces a label the runtime cannot
    // reproduce - the corner averaging pulls a tile's weights into its
    // neighbours - and measured worse than plain bilinear on two of seven
    // shots, which is impossible for something that can choose zero.
    std::vector<std::array<float, kOutputs>> cc;
    const auto errRegion = [&](int cx0, int cy0, int cx1, int cy1) {
        const int cnx = cx1 - cx0 + 2, cny = cy1 - cy0 + 2;
        cc.assign(static_cast<size_t>(cnx) * cny, {});
        for (int j = 0; j < cny; ++j)
            for (int i = 0; i < cnx; ++i)
                cornerOf(wf, cx0 + i, cy0 + j, cc[static_cast<size_t>(j) * cnx + i].data());

        const int x0 = cx0 * tileSize(), x1 = std::min((cx1 + 1) * tileSize(), fr.outW);
        const int y0 = cy0 * tileSize(), y1 = std::min((cy1 + 1) * tileSize(), fr.outH);
        double se = 0.0, seH = 0.0;
        long n = 0;
        for (int sy = (y0 + kStep - 1) / kStep; sy * kStep < y1; ++sy)
            for (int sx = (x0 + kStep - 1) / kStep; sx * kStep < x1; ++sx) {
                const int x = sx * kStep, y = sy * kStep;
                const float gx = (x + 0.5f) / tileSize(), gy = (y + 0.5f) / tileSize();
                const int i = clampi(static_cast<int>(gx), 0, cols - 1);
                const int j = clampi(static_cast<int>(gy), 0, rows - 1);
                const int li = i - cx0, lj = j - cy0;
                float w[kOutputs];
                triLerp<kOutputs>(cc[static_cast<size_t>(lj) * cnx + li].data(),
                                  cc[static_cast<size_t>(lj + 1) * cnx + li].data(),
                                  cc[static_cast<size_t>(lj) * cnx + li + 1].data(),
                                  cc[static_cast<size_t>(lj + 1) * cnx + li + 1].data(),
                                  gx - i, gy - j, w);
                int aA, aC, aD;
                alphaBytes(w, sharpen, aA, aC, aD);
                const Taps& tp = taps[static_cast<size_t>(sy) * sw + sx];
                int rgb[3];
                blend(tp, aA, aC, aD, rgb);
                const uint8_t* t = truth.at(x, y);
                for (int c = 0; c < 3; ++c) {
                    const double d = static_cast<double>(rgb[c]) - t[c];
                    se += d * d;
                    if (!wantFlicker) continue;
                    if (wantPeriod2) {
                        // THE PERIOD-2 TERM. altAmplitude() is the stationary
                        // alternation these alpha bytes would leave, in 8-bit
                        // levels; `conf` is 0 where there is no geometry to have
                        // a history for, and the CLAMP is what keeps a
                        // disocclusion from being read as a bob.
                        //
                        // WHY A CLAMP AND NOT A GATE. The phase pair comes from
                        // the previous low-res render at the reprojected
                        // position, so where the reprojection fails the two taps
                        // are different CONTENT and their difference is tens of
                        // levels, not the ~1.4 a quarter-pixel resample can
                        // produce. Squaring that would let a handful of
                        // disoccluded samples outvote the whole frame, and the
                        // only way the oracle could pay it is temporal weight
                        // where temporal weight GHOSTS - the old term's failure
                        // mode arriving by a new route. Clamping bounds the
                        // charge AND flattens its gradient there, so an outlier
                        // costs a constant and therefore buys nothing. Below the
                        // clamp it is still quadratic, so it carries the same
                        // units as the accuracy term above and
                        // --flicker-weight reads the same way in both forms.
                        if (tp.conf > 0.0f) {
                            const double a = std::min(
                                static_cast<double>(std::fabs(altAmplitude(tp, aA, aC, aD, c))),
                                kAltClamp);
                            seH += a * a;
                        }
                    } else {
                        // FlickerForm::Lag1, the original: how far this pixel
                        // lands from where the previous frame put the same
                        // content. Reachable, measured, and minimised by
                        // FREEZING - see FlickerForm in blss.hpp.
                        const double dh = static_cast<double>(rgb[c]) - tp.hist[c];
                        seH += dh * dh;
                    }
                }
                n += 3;
            }

        // THE FILL TERM. Every cell of the region is charged for the passes it
        // would make the GS draw, under the engine's own "any corner alpha
        // non-zero" rule - which is why the region is exactly the 3x3 the
        // caller sweeps: a tile's weight reaches the four corners it owns, and
        // those corners reach the nine cells around it, so this region contains
        // the WHOLE cost consequence of changing this tile and nothing else.
        //
        // Normalised per cell so it has the same units and the same magnitude
        // whether the region is 3x3 or clipped to 2x2 at the frame edge, and so
        // that `obj.fill` reads as "MSE per full-screen pass".
        double fill = 0.0;
        for (int j = 0; j + 1 < cny; ++j)
            for (int i = 0; i + 1 < cnx; ++i)
                fill += cellCost(cellDrawn(cc[static_cast<size_t>(j) * cnx + i].data(),
                                           cc[static_cast<size_t>(j + 1) * cnx + i].data(),
                                           cc[static_cast<size_t>(j) * cnx + i + 1].data(),
                                           cc[static_cast<size_t>(j + 1) * cnx + i + 1].data(),
                                           sharpen));
        const double cells = static_cast<double>(cnx - 1) * (cny - 1);
        const double fillTerm = cells > 0 ? obj.fill * fill / cells : 0.0;

        return n ? (se + obj.flicker * seH) / n + fillTerm : fillTerm;
    };

    // Coordinate descent over tiles, each candidate scored on its 3x3
    // neighbourhood. Starting from all-zero - which IS plain bilinear - and only
    // ever accepting an improvement is what makes the oracle a real upper bound
    // instead of a hope.
    //
    // It is NOT globally monotone, and it is worth knowing why: an accepted move
    // strictly improves its own 3x3 region, but a later tile's move shares four
    // corners with it and can raise the error there again. Measured over the
    // corpus that costs at most ~0.3 dB on one shot out of seven, everywhere
    // else the oracle comes out above every fixed kernel. Fixing it properly
    // means a global solve over the corner field, which is a lot of machinery
    // for a bound.
    static const float kCand[] = {0.0f,   0.125f, 0.25f, 0.375f, 0.5f,
                                  0.625f, 0.75f,  0.875f, 1.0f};

    // A SHARPEN OF 0 MAKES THE THIRD OUTPUT UNOBSERVABLE, so it is not swept.
    // alphaScales() (blss.hpp) returns a third scale of `k * 128`, so at k = 0
    // every candidate wD quantises to aD = 0: blend() skips passes 4/5 for all
    // nine of them and cellDrawn() charges none of them any fill. The sweep was
    // therefore spending a THIRD of the oracle's errRegion calls proving that
    // nine numbers score the same. Skipping it is bit-exact rather than an
    // approximation: the tile keeps the 0 it was initialised with (which is the
    // value the sweep would have kept - `best` starts at `t[m]` and only a
    // STRICT improvement replaces it), and the importance max() would have seen
    // worstE - bestE == 0, which never raises a maximum that starts at 0.
    const int sweptOutputs = sharpen > 0.0f ? kOutputs : kOutputs - 1;

    for (int sweep = 0; sweep < 2; ++sweep)
        for (int cy = 0; cy < rows; ++cy)
            for (int cx = 0; cx < cols; ++cx) {
                const int cx0 = std::max(0, cx - 1), cy0 = std::max(0, cy - 1);
                const int cx1 = std::min(cols - 1, cx + 1), cy1 = std::min(rows - 1, cy + 1);
                auto& t = wf.at(cx, cy);
                for (int m = 0; m < sweptOutputs; ++m) {
                    float best = t[m];
                    double bestE = errRegion(cx0, cy0, cx1, cy1);
                    double worstE = bestE;
                    for (float v : kCand) {
                        if (v == best) continue;
                        t[m] = v;
                        const double e = errRegion(cx0, cy0, cx1, cy1);
                        if (e < bestE) {
                            bestE = e;
                            best = v;
                        }
                        if (e > worstE) worstE = e;
                    }
                    t[m] = best;
                    // How much this tile's choice mattered. Tiles where every
                    // kernel is equally good must not get a vote in training.
                    if (importance && sweep == 1) {
                        float& imp = (*importance)[static_cast<size_t>(cy) * cols + cx];
                        imp = std::max(imp, static_cast<float>(worstE - bestE));
                    }
                }
            }
    return wf;
}

// ----------------------------------------------------------------- trainer ---

float train(Net& net, const std::vector<Sample>& samples, const TrainConfig& cfg) {
    if (samples.empty()) return 0.0f;
    net.randomize(cfg.seed);

    // INPUT STANDARDISATION, FOLDED BACK INTO THE FIRST LAYER. OFF BY DEFAULT,
    // AND THE MEASUREMENT THAT TURNED IT OFF IS THE DIAGNOSIS OF THIS NETWORK.
    //
    // The channels do not remotely share a scale over the corpus
    // (--blss-eval --features prints the table): texDetail lives in 0.00 .. 0.20
    // with sd 0.14, motion is zero in a quarter of all tiles, coverage is 1.0 on
    // seven tenths of them. One learning rate and one weight decay serve all of them, so the
    // narrow channels get smaller gradients and are the first thing decay pulls
    // to zero - and texDetail is the channel most correlated with what the oracle
    // asks of the temporal weight. That looks exactly like an optimisation
    // defect, so it was fixed, and the fix MADE GENERALISATION WORSE:
    //
    //   leave-one-shot-out, 7 shots, 84 frames, 3 seeds, decay 1e-5
    //                        raw     standardised
    //   held-out margin     +0.31       -0.15   dB over bilinear
    //   spread over folds    0.39        1.18
    //   in-distribution     +0.93       +1.05   (per-fold mean)
    //
    // Better fit on the shots it trained on, worse on the shot it did not, with
    // four times the spread: this network is variance-limited, not
    // optimisation-limited, and anything that makes the fit EASIER makes the
    // feature worse. That is what sent the search to weight decay (TrainConfig)
    // and to the corpus (blsscorpus.hpp) instead. Kept, because the next person
    // to notice those channel scales should find this table rather than re-run it.
    //
    // The mechanism, for when someone does want it: it has to leave the ENGINE
    // alone, because buildFeatures() is twinned with RendererCoreBlss, which
    // computes the raw channels on the EE and knows nothing about any statistics.
    // So it is applied only while fitting and then FOLDED INTO w1/b1, which is
    // exact for an affine map:
    //
    //     h = tanh( sum_i w_i * (x_i - mu_i)/sd_i + b )
    //       = tanh( sum_i (w_i/sd_i) * x_i + (b - sum_i w_i*mu_i/sd_i) )
    //
    // so the net that comes out eats RAW features, saves and emits unchanged, and
    // the console cannot tell this happened. Statistics come from the training
    // samples only - a fold that standardised against its held-out shot would be
    // leaking, which is the one mistake a cross-validation harness must not make.
    float mu[kFeatures] = {}, sd[kFeatures] = {};
    if (cfg.standardise) {
        double sum[kFeatures] = {}, sq[kFeatures] = {};
        for (const Sample& s : samples)
            for (int i = 0; i < kFeatures; ++i) {
                sum[i] += s.f.v[i];
                sq[i] += static_cast<double>(s.f.v[i]) * s.f.v[i];
            }
        const double inv = 1.0 / static_cast<double>(samples.size());
        for (int i = 0; i < kFeatures; ++i) {
            const double m = sum[i] * inv;
            const double var = std::max(0.0, sq[i] * inv - m * m);
            const double s = std::sqrt(var);
            // A channel that does not move cannot be standardised - dividing by
            // its noise would amplify exactly nothing into a large weight. Leave
            // it alone and let the report say it is dead.
            mu[i] = s > 1e-4 ? static_cast<float>(m) : 0.0f;
            sd[i] = s > 1e-4 ? static_cast<float>(s) : 1.0f;
        }
    } else {
        for (int i = 0; i < kFeatures; ++i) sd[i] = 1.0f;
    }
    std::vector<std::array<float, kFeatures>> xs(samples.size());
    for (size_t s = 0; s < samples.size(); ++s)
        for (int i = 0; i < kFeatures; ++i)
            xs[s][static_cast<size_t>(i)] = (samples[s].f.v[i] - mu[i]) / sd[i];

    Net m{}, v{};  // Adam moments, same shape as the net
    float* pw = reinterpret_cast<float*>(&net);
    float* pm = reinterpret_cast<float*>(&m);
    float* pv = reinterpret_cast<float*>(&v);
    static_assert(sizeof(Net) == kNetFloats * sizeof(float),
                  "Net must be a flat float block for the Adam loop below");

    Rng rng(cfg.seed ^ 0x9E3779B9u);
    const float b1 = 0.9f, b2 = 0.999f, eps = 1e-8f;
    int step = 0;
    float lastLoss = 0.0f;

    Net grad{};
    float* pg = reinterpret_cast<float*>(&grad);
    const int batches = std::max<int>(1, static_cast<int>(samples.size()) / cfg.batch);

    for (int epoch = 0; epoch < cfg.epochs; ++epoch) {
        double epochLoss = 0.0;
        double epochW = 0.0;
        for (int b = 0; b < batches; ++b) {
            std::memset(pg, 0, sizeof(Net));
            double batchW = 0.0;
            for (int s = 0; s < cfg.batch; ++s) {
                const size_t pick = rng.next() % samples.size();
                const Sample& smp = samples[pick];
                const float* x = xs[pick].data();  // standardised; identity if off
                const float iw = std::max(smp.importance, 0.0f);
                if (iw <= 0.0f) continue;
                batchW += iw;

                // Forward, keeping the activations for the backward pass.
                float h[kHidden], o[kOutputs];
                for (int k = 0; k < kHidden; ++k) {
                    float a = net.b1[k];
                    for (int i = 0; i < kFeatures; ++i) a += net.w1[k][i] * x[i];
                    // THE TABLE REACHES TRAINING TOO, deliberately. The
                    // console evaluates whatever activation this fits against,
                    // so fitting on std::tanh and running on a table would be
                    // the same train/run mismatch the corpus exists to avoid -
                    // small here, but the gradients below (1 - h^2 and
                    // o*(1-o)) stay the ANALYTIC ones either way, which is
                    // correct: they are the derivative of the function being
                    // approximated, and a step-function's true derivative is
                    // zero almost everywhere.
                    h[k] = actTanh(a);
                }
                for (int mo = 0; mo < kOutputs; ++mo) {
                    float a = net.b2[mo];
                    for (int k = 0; k < kHidden; ++k) a += net.w2[mo][k] * h[k];
                    o[mo] = actLogistic(a);
                }

                // MSE on the oracle weights, weighted by how much this tile's
                // choice actually changed its error.
                float dOut[kOutputs];
                for (int mo = 0; mo < kOutputs; ++mo) {
                    const float e = o[mo] - smp.target[mo];
                    epochLoss += static_cast<double>(iw) * e * e;
                    dOut[mo] = iw * 2.0f * e * o[mo] * (1.0f - o[mo]);  // through logistic
                }
                float dH[kHidden] = {};
                for (int mo = 0; mo < kOutputs; ++mo) {
                    for (int k = 0; k < kHidden; ++k) {
                        grad.w2[mo][k] += dOut[mo] * h[k];
                        dH[k] += dOut[mo] * net.w2[mo][k];
                    }
                    grad.b2[mo] += dOut[mo];
                }
                for (int k = 0; k < kHidden; ++k) {
                    const float d = dH[k] * (1.0f - h[k] * h[k]);  // through tanh
                    for (int i = 0; i < kFeatures; ++i) grad.w1[k][i] += d * x[i];
                    grad.b1[k] += d;
                }
            }
            if (batchW <= 0.0) continue;
            epochW += batchW;
            ++step;
            const float scale = 1.0f / static_cast<float>(batchW);
            const float bc1 = 1.0f - std::pow(b1, static_cast<float>(step));
            const float bc2 = 1.0f - std::pow(b2, static_cast<float>(step));
            for (size_t i = 0; i < kNetFloats; ++i) {
                const float g = pg[i] * scale + cfg.weightDecay * pw[i];
                pm[i] = b1 * pm[i] + (1.0f - b1) * g;
                pv[i] = b2 * pv[i] + (1.0f - b2) * g * g;
                pw[i] -= cfg.lr * (pm[i] / bc1) / (std::sqrt(pv[i] / bc2) + eps);
            }
        }
        lastLoss = epochW > 0.0 ? static_cast<float>(epochLoss / epochW) : 0.0f;
        if (cfg.verbose && (epoch % 50 == 0 || epoch == cfg.epochs - 1))
            std::printf("  epoch %4d  loss %.6f\n", epoch, lastLoss);
    }

    // Unfold the standardisation into the first layer, so what leaves this
    // function is a net over the RAW features the engine computes. Exact, not an
    // approximation: an affine input map is a change of basis of layer 1.
    if (cfg.standardise)
        for (int k = 0; k < kHidden; ++k) {
            float shift = 0.0f;
            for (int i = 0; i < kFeatures; ++i) {
                net.w1[k][i] /= sd[i];
                shift += net.w1[k][i] * mu[i];
            }
            net.b1[k] -= shift;
        }
    return lastLoss;
}

// ------------------------------------------------------------- CLI plumbing ---

namespace {

struct CliOpts {
    std::string netPath = "blss.net";
    // Did the user actually ASK for that net? `--blss-eval` without `-i` is the
    // first thing anyone runs on a fresh project - "is there anything here to
    // reconstruct" - and the answer, the oracle row, does not involve a network
    // at all. Refusing to run without a trained net made the documented first
    // step impossible to perform, so a missing default is now a net-free
    // evaluation and only an EXPLICIT `-i` that cannot be opened is an error.
    bool netGiven = false;
    std::string outPath;
    std::string dumpDir;
    std::string assetDir = "examples";
    // `--blss-train <projectDir>` / `--blss-eval <projectDir>`: train on the
    // USER'S OWN SCENE instead of the procedural bestiary. A positional
    // argument rather than a flag because it is the thing you are training ON,
    // not a knob - and because "train this project" is the shape the docs and
    // the panel already promise.
    std::string projectDir;
    // EVERY positional after the first: a UNION CORPUS over several projects.
    //
    //   tyrax-editor --blss-eval a b c --cv --cv-groups
    //
    // This is the corpus the "can one net ship for every project" question needs
    // and the one nobody had - see CorpusConfig::projectDirs. `projectDir` is
    // still the first entry, so every single-project invocation is unchanged.
    std::vector<std::string> projectDirs;
    // `--cv-groups`: LEAVE-ONE-PROJECT-OUT. The held-out set is still one shot -
    // a fold row has to be one kind of content or it says nothing - but the
    // TRAINING set becomes the complement of that shot's whole PROJECT, so no
    // camera move of the project being scored is in it.
    //
    // The distinction against plain `--cv` is the whole experiment: leave-one-
    // shot-out asks "does this net generalise to a seventh camera move of a
    // scene it has already seen", and every project-corpus number this feature
    // ever published was that question. "Can I ship ONE net" is this one.
    bool cvGroups = false;
    // `--no-package-split`: one bag proxy per object instead of one per VU1
    // package. Off is what the console does; on reproduces every fold table
    // published before the split existed. See CorpusConfig::packageSplit.
    bool packageSplit = true;
    // `--proxy-budget`: cap a bag's proxy count at the tiles it covers - the
    // fifth rule of the twin contract, off on BOTH sides. See
    // CorpusConfig::proxyBudget and docs/blss-reconstruction.md section 2.
    bool proxyBudget = false;
    // `--emitter-proxy`: describe particle emitters with a bag proxy - the
    // SIXTH rule of the twin contract, off on BOTH sides (the engine's half is
    // TYRA_BLSS_EMITTER_PROXY). It changes what the network is SHOWN, not what
    // the corpus draws - see CorpusConfig::emitterProxy before quoting a PSNR
    // taken with it on.
    bool emitterProxy = false;
    // `--no-anim`: leave animated models out of the project corpus, the way it
    // worked before they were added. The console draws and describes them, so
    // this is a reproduction switch, not a setting - see CorpusConfig::animated.
    bool animated = true;
    // `--ignore-shot-plan`: do not read the project's training-shot plan
    // (Project::blssShots) - six automatic moves, takes on, no authored
    // vantages, an equal frame share. A reproduction switch like the two above:
    // it is how a fold table taken before the plan existed stays runnable on a
    // project that has since authored one. See CorpusConfig::shotPlan.
    bool shotPlan = true;
    // `--still`: freeze each shot at its first camera and first pose so only the
    // jitter phase advances - the host twin of the console's frozen-camera
    // experiment. A FIXTURE FOR THE PERIOD-2 TABLE ONLY; see CorpusConfig::still.
    bool still = false;
    // `--no-jitter` / `--jitter`: -1 = follow the project's own blssJitter (and
    // on for the bestiary). NOT a sweep knob like --tile: the console really can
    // be built either way, and on hardware the jittered build is the one that
    // still bobs, so a project with blssJitter off must be FITTED with it off.
    int jitter = -1;
    // 12 frames per shot over the 13-shot bestiary. Frames are split evenly, so
    // this number and the shot count have to move together: at the old default of
    // 48 the tail shots got three frames each, and a shot with three frames
    // teaches the temporal channel almost nothing (its history is one frame deep
    // and the first frame of a shot has none at all).
    int frames = 156;
    int epochs = 400;
    // --scale WxH: the raster scale, arbitrary now (blss.hpp, struct Scale).
    // A SWEEP KNOB above the two the project format can express: the ENGINE is
    // generic (setRasterScale takes any positive pair), but `blssScale` is an
    // int with 0 = 2x2 and 1 = 1x2, so anything else measures a configuration
    // the console could run and no project can currently ASK for. `--scale-1x2`
    // is the same setting spelled the old way and stays reachable.
    Scale scale = Scale::X2Y2;
    float sharpen = 0.5f;
    uint32_t seed = 0xB1557u;
    // What the oracle is actually asked to minimise. Overridable because the
    // two weights are a JOINT trade (stability against sharpness against fill)
    // and the only honest way to set them is to sweep the pair and read all
    // three columns - which a rebuild per point makes miserable.
    Objective obj;
    // --cv: leave-one-shot-out cross-validation instead of the single split.
    // See crossValidate(). --cv-seeds repeats the whole thing on N corpora, so
    // the table carries a spread and not just a mean.
    bool cv = false;
    int cvSeeds = 1;
    // Hold out only the FIRST N shots in turn (0 = all of them). The corpus grew
    // after the folds below were measured, and "same held-out content, more
    // training content" is the only before/after that means anything - a fold
    // table whose rows changed is a different experiment, not a comparison.
    int cvFolds = 0;
    float weightDecay = 0.0f;  // 0 = TrainConfig's default
    // --all-shots: fit the whole corpus instead of the split's training side.
    // For the net you SHIP, not for the net you measure - see trainMain.
    bool allShots = false;
    bool standardise = false;  // --standardise for the A/B, see train()
    // --features: what the six input channels actually look like over the
    // corpus. A channel that is constant is a channel the net cannot use, and
    // nothing printed that until this flag existed.
    bool featureStats = false;
    // --probe "<BLSSFEAT line>": a feature vector measured ON THE CONSOLE,
    // placed in the corpus' own distribution. This is the instrument the
    // feature was missing for its entire life - the corpus could describe
    // itself and the console could not be compared to it, so the network was
    // fitted to one distribution and run on another for eleven commits. Paste
    // the engine's own debugView-2 line straight in; see probeReport().
    std::string probe;
    // --drop-feature <name|index>[,...]: zero these input channels over the
    // whole corpus, which is exactly what removing them from the vector would
    // do to a trained net. The honest way to ask "does this channel earn its
    // keep" without editing kFeatures on both twins first.
    std::array<bool, kFeatures> dropped{};
    // --deadzone A: the inference-time snap-to-zero, in GS alpha bytes
    // (kDeadzoneAlpha). Overridable for the same reason the objective's weights
    // are: it trades quality against fill, and the only honest way to set it is
    // to sweep it under --cv and read BOTH columns. 0 turns it off.
    float deadzone = kDeadzoneAlpha;
    // --threads N: how many worker threads the corpus renderer and the oracle
    // use. 0 = every core. THIS CHANGES THE WALL CLOCK AND NOTHING ELSE - both
    // loops hand item i to a fixed worker and let it touch only item i - and
    // `--threads 1` is the check, not a comment: it must produce a
    // byte-identical blss.net to a run on every core. The trainer itself is
    // sequential SGD and ignores this.
    int threads = 0;
    // --deadzone-sweep a,b,c: every value in one --cv run. THE DEADZONE DOES
    // NOT TOUCH TRAINING - the labels never see it, it is applied to the net's
    // answer - so N deadzones over the same folds are N honest rows of one
    // sweep at the cost of N evaluations rather than N trainings. That is the
    // difference between a sweep that takes twenty minutes and one that takes
    // two hours, which is the difference between sweeping it and guessing.
    std::vector<float> deadzoneSweep;
    // --tile N: the decision tile edge, in output pixels (blss.hpp, tileSize()).
    // A SWEEP KNOB, not a setting: the engine's kTile is a compile-time
    // constant, so any value but 32 measures a configuration the console cannot
    // currently run. It is here because 32 -> 64 is the largest EE saving
    // available to this feature (224 tile inferences -> 56, 255 grid corners ->
    // 72) and nobody had ever measured what it costs.
    int tile = kTile;
    // --act-table N: replace tanh/exp with an N-interval shared table
    // (blss.hpp, actTanh). 0 = libm; 512 is the default and the shipped value.
    // THIS NUMBER IS THE ENGINE'S TYRA_BLSS_ACT_TABLE
    // (renderer_core_blss.cpp). They are one number in two files and they move
    // in the same commit - a one-sided flip is silent twin divergence.
    int actTable = kEngineActTable;
};

std::string joinArgs(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& a : v) {
        if (!s.empty()) s += ' ';
        s += a;
    }
    return s;
}

// The invocation, for the provenance sidecar - argv[0] deliberately replaced by
// the tool's name rather than recorded, because the exe path differs per machine
// and per worktree and the sidecar has to be byte-reproducible for a CI diff to
// mean anything.
std::string commandLine(int argc, char** argv) {
    std::string s = "tyrax-editor";
    for (int i = 1; i < argc; ++i) {
        s += ' ';
        s += argv[i];
    }
    return s;
}

// WHICH NET A VERB RUNS, and it is deliberately the same order templates.cpp
// bakes in: an explicit `-i`, else the project's own blss.net, else the net the
// editor ships. An explicit `-i` that cannot be opened stays a hard error - you
// asked for that net - while a missing default is now answered rather than
// reported, because there IS a default.
enum class NetSource { None, Explicit, Project, Default };

NetSource resolveNet(const CliOpts& o, Net& net, std::string* err) {
    if (o.netGiven) return load(net, o.netPath, err) ? NetSource::Explicit : NetSource::None;
    if (load(net, o.netPath, nullptr)) return NetSource::Project;
    return defaultNet(net, err) ? NetSource::Default : NetSource::None;
}

// One line a caller can parse plus, when they exist, the provenance complaints.
// The complaints are the reason this prints at all: a net that loads is not the
// same thing as a net that was fitted for the run it is about to be used in.
void announceNet(NetSource src, const CliOpts& o) {
    const Provenance p =
        src == NetSource::Default ? defaultProvenance() : readProvenance(o.netPath);
    const char* kind = src == NetSource::Default ? "default"
                       : src == NetSource::Explicit ? "explicit"
                                                    : "project";
    std::printf("[blss] net source=%s path=%s corpus=%s scale=%s jitter=%s\n", kind,
                src == NetSource::Default ? "(built into the editor)" : o.netPath.c_str(),
                p.corpus.empty() ? "?" : p.corpus.c_str(),
                p.scale.x > 0 ? scaleName(p.scale).c_str() : "?",
                p.jitter < 0 ? "?" : (p.jitter ? "on" : "off"));
    const NetIssues iss = checkProvenance(p, NetExpect{o.scale, o.jitter, detail::gActN});
    for (const std::string& f : iss.fatal) std::printf("[blss] net REFUSED: %s\n", f.c_str());
    for (const std::string& w : iss.warn) std::printf("[blss] net warning: %s\n", w.c_str());
}

CliOpts parseCli(int argc, char** argv) {
    CliOpts o;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](const char* def) -> std::string {
            return i + 1 < argc ? std::string(argv[++i]) : std::string(def);
        };
        if (a == "-o" || a == "--out") o.outPath = next("");
        else if (a == "-i" || a == "--net") {
            o.netPath = next("blss.net");
            o.netGiven = true;
        }
        else if (a == "--dump") o.dumpDir = next(".");
        else if (a == "--assets") o.assetDir = next("examples");
        else if (a == "--frames") o.frames = std::atoi(next("156").c_str());
        else if (a == "--epochs") o.epochs = std::atoi(next("400").c_str());
        else if (a == "--sharpen") o.sharpen = static_cast<float>(std::atof(next("0.5").c_str()));
        else if (a == "--flicker-weight")
            o.obj.flicker = static_cast<float>(std::atof(next("0.15").c_str()));
        // WHICH stability quantity that weight buys. `lag1` is the original term
        // - kept reachable because its sweep is recorded in blss.hpp and setting
        // a weight to zero is not the same as deleting the measurement.
        else if (a == "--flicker-form") {
            const std::string f = next("period2");
            if (f == "lag1") o.obj.flickerForm = FlickerForm::Lag1;
            else if (f == "period2") o.obj.flickerForm = FlickerForm::Period2;
            else std::printf("blss: --flicker-form: no form '%s' (lag1 | period2)\n", f.c_str());
        }
        else if (a == "--fill-weight")
            o.obj.fill = static_cast<float>(std::atof(next("16").c_str()));
        else if (a == "--deadzone")
            o.deadzone = static_cast<float>(std::atof(next("4").c_str()));
        else if (a == "--deadzone-sweep") {
            const std::string list = next("");
            size_t at = 0;
            while (at <= list.size()) {
                const size_t comma = list.find(',', at);
                const std::string one = list.substr(at, comma - at);
                if (!one.empty()) o.deadzoneSweep.push_back(static_cast<float>(std::atof(one.c_str())));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        }
        else if (a == "--seed") o.seed = static_cast<uint32_t>(std::strtoul(next("0").c_str(), nullptr, 0));
        else if (a == "--cv") o.cv = true;
        else if (a == "--cv-groups") { o.cv = true; o.cvGroups = true; }
        else if (a == "--cv-seeds") o.cvSeeds = std::max(1, std::atoi(next("1").c_str()));
        else if (a == "--cv-folds") o.cvFolds = std::max(0, std::atoi(next("0").c_str()));
        else if (a == "--weight-decay")
            o.weightDecay = static_cast<float>(std::atof(next("1e-5").c_str()));
        else if (a == "--standardise") o.standardise = true;
        else if (a == "--all-shots") o.allShots = true;
        else if (a == "--features") o.featureStats = true;
        else if (a == "--probe") {
            o.probe = next("");
            o.featureStats = true;  // the probe is read against that table
        }
        else if (a == "--drop-feature") {
            const std::string list = next("");
            size_t at = 0;
            while (at <= list.size()) {
                const size_t comma = list.find(',', at);
                std::string one = list.substr(at, comma - at);
                if (!one.empty()) {
                    int idx = -1;
                    for (int c = 0; c < kFeatures; ++c)
                        if (one == kFeatureNames[c]) idx = c;
                    if (idx < 0 && one.find_first_not_of("0123456789") == std::string::npos)
                        idx = std::atoi(one.c_str());
                    if (idx >= 0 && idx < kFeatures) o.dropped[static_cast<size_t>(idx)] = true;
                    else std::printf("blss: --drop-feature: no channel '%s'\n", one.c_str());
                }
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
        }
        // Clamped HERE rather than only inside the two loops, so the count the
        // tool prints is the count it will actually run: both parallelFor and
        // parallelFrames cap at 32 (a corpus worker owns ~30 MB of raster
        // scratch), and "on 999 thread(s)" would have been a lie.
        else if (a == "--threads")
            o.threads = std::clamp(std::atoi(next("0").c_str()), 0, 32);
        else if (a == "--tile") o.tile = std::atoi(next("32").c_str());
        else if (a == "--act-table") o.actTable = std::atoi(next("512").c_str());
        // `--scale-1x2` is the shipped mode's own flag and stays. `--scale WxH`
        // is the SWEEP knob - see CliOpts::scale.
        else if (a == "--scale-1x2") o.scale = Scale::X1Y2;
        else if (a == "--scale") {
            const std::string want = next("2x2");
            if (!parseScale(want, &o.scale))
                std::printf(
                    "blss: --scale '%s' refused (want WxH, two integers 1..16, e.g. 4x4)\n",
                    want.c_str());
        }
        else if (a == "--no-package-split") o.packageSplit = false;
        else if (a == "--proxy-budget") o.proxyBudget = true;
        else if (a == "--emitter-proxy") o.emitterProxy = true;
        else if (a == "--no-anim") o.animated = false;
        else if (a == "--ignore-shot-plan") o.shotPlan = false;
        else if (a == "--still") o.still = true;
        else if (a == "--no-jitter") o.jitter = 0;
        else if (a == "--jitter") o.jitter = 1;
        // The positional arguments: the project(s) to train on. The first bare
        // word is `projectDir` so `--blss-train ~/game --cv` reads the way it
        // looks; every further one appends, which is the union corpus.
        else if (!a.empty() && a[0] != '-') {
            if (o.projectDir.empty()) o.projectDir = a;
            o.projectDirs.push_back(a);
        }
        else std::printf("blss: ignoring unknown argument '%s'\n", a.c_str());
    }
    // A NET BELONGS TO THE PROJECT IT WAS FITTED ON, so the default path is
    // resolved against that project and not against the current directory.
    //
    // This is a real bug being fixed, not a tidy-up, and it cost a hardware
    // round: `--blss-train <projectDir>` wrote `./blss.net` while
    // `templates::blssBake()` reads `<projectDir>/blss.net`, so the documented
    // "train, then rebuild" flow trained a network, put it somewhere the build
    // never looks, and silently kept baking the shipped default - with a boot
    // log that said `network = the editor's built-in default` and was telling
    // the exact truth nobody thought to read. The read side had the same
    // asymmetry: `--blss-eval <projectDir>` reported `net source=default` for a
    // project that had a perfectly good net sitting next to its `.tyra`.
    //
    // It was invisible from the BLSS window because that job runs with cwd =
    // the project directory AND passes an explicit `-o`, so both spellings
    // named one file. Only the shell has ever been able to see it.
    //
    // Exactly one project positional, because that is when "the project's net"
    // has a referent. A union corpus (`--blss-train a b bestiary`) fits a net
    // that belongs to none of its members - writing it into the first one would
    // be a guess - and the bestiary alone has no project at all; both keep the
    // old cwd-relative default, which is what the published commands for them
    // already pair with an explicit `-o`. An explicit `-i`/`-o` always wins.
    if (o.projectDirs.size() == 1 && !o.netGiven) {
        o.netPath =
            (std::filesystem::path(o.projectDir) / "blss.net").lexically_normal().string();
    }
    return o;
}

// Keeping whole SHOTS out (not random frames) is the point: neighbouring frames
// of one camera move are near-duplicates, so a random split would leak and
// report a flattering number.
//
// The split STRIDES the bestiary rather than taking the tail, and that is not
// cosmetic. The corpus ends with its two easiest shots - a flat field and a whip
// pan, i.e. the two cases whose correct answer is "do nothing" - so holding out
// the last two measured a frame where even a perfect oracle beat plain bilinear
// by 0.2 dB. That is a report about the split, not about the method. A stride
// keeps a hard shot and an easy one on both sides.
//
// AND IT IS STILL ONE DRAW. Every held-out decibel this feature ever quoted came
// out of this one function, and a 2-of-7 (now 4-of-13) partition is a sample of
// size one: re-running the whole cycle at four seeds moved it from -0.23 to
// +0.26 dB, which was read as seed noise and is mostly the split. Use
// `--blss-eval --cv` for anything you intend to act on - it holds out each shot
// in turn, which is seven estimates and, more usefully, seven answers to WHICH
// content the net helps. This split remains what --blss-train fits on, because
// training needs one training set, not seven.
bool isHeldOut(int shot, int shotCount) {
    if (shotCount <= 2) return shot == shotCount - 1;
    return shot % 3 == 1;
}

// WHICH SHOTS A PASS LOOKS AT, indexed by shot id. Everything below takes one of
// these rather than a bool, because the shipped split is only ONE draw out of
// the seven the corpus can give you: leave-one-shot-out cross-validation needs
// `shot == f` and `shot != f` for every f, and that is the whole point of --cv.
using ShotMask = std::vector<char>;

ShotMask heldOutMask(int shots) {
    ShotMask m(static_cast<size_t>(std::max(shots, 0)), 0);
    for (int s = 0; s < shots; ++s) m[static_cast<size_t>(s)] = isHeldOut(s, shots) ? 1 : 0;
    return m;
}
ShotMask singleShotMask(int shots, int shot) {
    ShotMask m(static_cast<size_t>(std::max(shots, 0)), 0);
    if (shot >= 0 && shot < shots) m[static_cast<size_t>(shot)] = 1;
    return m;
}
ShotMask complementMask(const ShotMask& m) {
    ShotMask c(m.size(), 0);
    for (size_t i = 0; i < m.size(); ++i) c[i] = m[i] ? 0 : 1;
    return c;
}
bool inMask(const ShotMask& m, int shot) {
    return shot >= 0 && static_cast<size_t>(shot) < m.size() && m[static_cast<size_t>(shot)] != 0;
}

// Fixed work per worker, matmul-style: item i is always handled by the same
// arithmetic whatever the core count, and `body` may only touch item i, so the
// RESULT does not depend on how many threads ran. Determinism has to survive
// parallelism here - a cross-validation table that moved with the machine it ran
// on would be worthless. (Same rule as matbake.cpp's sampler.)
//
// `threads` is `--threads`, 0 for every core the machine has. It is a WALL
// CLOCK knob and nothing else, and `--threads 1` is how that is checked rather
// than asserted: one thread and N threads must write byte-identical blss.net
// files, because every number this feature has published came off a seeded run.
template <class F>
void parallelFor(int n, int threads, const F& body) {
    if (n <= 0) return;
    unsigned hw = threads > 0 ? static_cast<unsigned>(threads)
                              : std::thread::hardware_concurrency();
    if (hw < 1) hw = 1;
    if (hw > 32) hw = 32;
    const int workers = std::min<int>(static_cast<int>(hw), n);
    if (workers <= 1) {
        for (int i = 0; i < n; ++i) body(i);
        return;
    }
    const auto run = [&](int w) {
        for (int i = w; i < n; i += workers) body(i);
    };
    std::vector<std::thread> pool;
    pool.reserve(static_cast<size_t>(workers - 1));
    for (int w = 1; w < workers; ++w) pool.emplace_back(run, w);
    run(0);
    for (std::thread& t : pool) t.join();
}

// Mean and (population) standard deviation, because a mean without a spread is
// how this feature quoted "+0.18 dB" twice from single runs that were noise.
struct Spread {
    double mean = 0, sd = 0;
    int n = 0;
};
Spread spreadOf(const std::vector<double>& v) {
    Spread s;
    s.n = static_cast<int>(v.size());
    if (v.empty()) return s;
    for (double x : v) s.mean += x;
    s.mean /= v.size();
    for (double x : v) s.sd += (x - s.mean) * (x - s.mean);
    s.sd = std::sqrt(s.sd / v.size());
    return s;
}

TrainConfig trainConfigOf(const CliOpts& o) {
    TrainConfig tc;
    tc.epochs = o.epochs;
    tc.seed = o.seed;
    if (o.weightDecay > 0.0f) tc.weightDecay = o.weightDecay;
    tc.standardise = o.standardise;
    return tc;
}

// THE TWO PROCESS-WIDE KNOBS, SET ONCE AND ANNOUNCED. Both are globals in
// blss.hpp because they reach code with no config to thread them through, so
// the one rule that keeps them honest is that they are written HERE, before any
// corpus, oracle or net exists, and never again - no worker thread ever sees
// them change. Announced because a table of decibels whose tile size is not on
// the page is a table nobody can reproduce; this feature has published five
// numbers measured on a configuration nobody wrote down.
void applySweepKnobs(const CliOpts& o) {
    if (!o.shotPlan)
        std::printf(
            "blss: --ignore-shot-plan - the project's training-shot plan is NOT read. Six "
            "automatic moves, takes on, no authored vantages, an equal frame share. A "
            "MEASUREMENT configuration: it reproduces a corpus taken before the plan existed, "
            "and it is not what the project asks the build for.\n");
    if (o.tile != kTile) {
        if (!setTileSize(o.tile)) {
            std::printf("blss: --tile %d refused (must be a positive power of two)\n", o.tile);
        } else {
            std::printf(
                "blss: TILE %d (shipped is %d) - a MEASUREMENT configuration: the "
                "engine's RendererCoreBlss::kTile is a compile-time constant and "
                "still %d.\n",
                o.tile, kTile, kTile);
        }
    }
    // The raster scale is not a global - it rides in CorpusConfig and Frame -
    // but it is announced here for the same reason the two above are: a fold
    // table measured at 4x4 and a fold table measured at 2x2 are two different
    // experiments and nothing in blss.net records which one it was.
    if (o.scale != Scale::X2Y2) {
        const bool projectCanAsk = o.scale == Scale::X1Y2;
        std::printf(
            "blss: RASTER SCALE %s (shipped is %s)%s\n", scaleName(o.scale).c_str(),
            scaleName(Scale::X2Y2).c_str(),
            projectCanAsk
                ? " - the project setting blssScale 1."
                : " - a MEASUREMENT configuration: the engine's setRasterScale() takes it,\n"
                  "      but `blssScale` is an int with 0 = 2x2 and 1 = 1x2, so no project can"
                  " ask for it yet.");
    }
    if (o.actTable > 0) {
        if (!setActTable(o.actTable)) {
            std::printf("blss: --act-table %d refused (must be a positive even count)\n",
                        o.actTable);
        } else {
            std::printf(
                "blss: activation TABLE, %d intervals over [-%g, +%g], Q15, nearest, "
                "hash 0x%08X - tanh AND the logistic, no libm and no divide. The "
                "engine twin runs the same table (TYRA_BLSS_ACT_TABLE).\n",
                o.actTable, static_cast<double>(kActRange), static_cast<double>(kActRange),
                actTableHash());
        }
    }
}

// HOW MANY ENABLED EMITTERS THE LAST CORPUS WALKED PAST WITHOUT DRAWING.
//
// A file-scope int rather than a parameter on all four call sites because it is
// a property of the corpus the verb just built, every verb builds exactly one,
// and the verbs are single-threaded up to the point they print. The corpus
// itself is threaded; this is written once, before any worker starts.
//
// It exists so the CAVEAT reaches the VERDICT. `generate()` already printed a
// four-line warning naming the count - at the top of a run that then prints two
// PSNR tables, an alternation table and a one-line machine-readable verdict, by
// which point the warning has scrolled away and the window's parser never saw
// it at all. See CorpusInfo in blsscorpus.hpp for the measurement that says why
// this matters more than it looks.
int gCorpusEmitters = 0;

std::vector<CorpusFrame> buildCorpus(const CliOpts& o) {
    CorpusConfig cc;
    cc.frames = o.frames;
    cc.scale = o.scale;
    cc.seed = o.seed;
    cc.assetDir = o.assetDir;
    cc.projectDir = o.projectDir;
    // One positional is the ordinary project corpus and goes down the old path;
    // two or more is the union, and generate() then never falls back to the
    // bestiary for a member that will not load.
    if (o.projectDirs.size() > 1) cc.projectDirs = o.projectDirs;
    cc.packageSplit = o.packageSplit;
    cc.proxyBudget = o.proxyBudget;
    cc.emitterProxy = o.emitterProxy;
    cc.animated = o.animated;
    cc.shotPlan = o.shotPlan;
    cc.still = o.still;
    cc.jitter = o.jitter;
    cc.threads = o.threads;
    if (o.threads > 0)
        std::printf("blss: rendering %d corpus frames at %dx%d on %d thread(s)\n", cc.frames,
                    cc.outW, cc.outH, o.threads);
    else
        std::printf("blss: rendering %d corpus frames at %dx%d on every core\n", cc.frames,
                    cc.outW, cc.outH);
    CorpusInfo ci;
    std::vector<CorpusFrame> corpus = generate(cc, &ci);
    gCorpusEmitters = ci.emitters;
    // --drop-feature: hold a channel at zero everywhere. A constant input is
    // worth exactly what a deleted one is - the net's weights on it are only
    // ever touched by weight decay - so this measures "would kFeatures - 1
    // score the same" without first editing kFeatures on BOTH twins, the
    // emitter and kNetVersion.
    bool anyDropped = false;
    for (int c = 0; c < kFeatures; ++c) anyDropped = anyDropped || o.dropped[static_cast<size_t>(c)];
    if (anyDropped) {
        std::printf("blss: holding at zero:");
        for (int c = 0; c < kFeatures; ++c)
            if (o.dropped[static_cast<size_t>(c)]) std::printf(" %s", kFeatureNames[c]);
        std::printf("\n");
        for (CorpusFrame& cf : corpus)
            for (Features& f : cf.frame.features)
                for (int c = 0; c < kFeatures; ++c)
                    if (o.dropped[static_cast<size_t>(c)]) f.v[c] = 0.0f;
    }
    return corpus;
}

int shotCountOf(const std::vector<CorpusFrame>& c) {
    int n = 0;
    for (const auto& f : c) n = std::max(n, f.shot + 1);
    return n;
}

// "fold 4 lost 0.3 dB" is a fact about the grazing wall, not about fold 4.
std::string shotNameOf(const std::vector<CorpusFrame>& c, int shot) {
    for (const CorpusFrame& f : c)
        if (f.shot == shot) return std::string(f.shotName) + " " + f.moveName;
    return "shot " + std::to_string(shot);
}

// WHICH UNION MEMBER EACH SHOT CAME FROM, indexed by shot id. All zeros for a
// single project or the bestiary, which is what makes --cv-groups degenerate
// into "hold out everything and train on nothing" there rather than quietly
// measuring something else - crossValidate() refuses that case out loud.
std::vector<int> shotGroupsOf(const std::vector<CorpusFrame>& c, int shots) {
    std::vector<int> g(static_cast<size_t>(std::max(shots, 0)), 0);
    for (const CorpusFrame& f : c)
        if (f.shot >= 0 && f.shot < shots) g[static_cast<size_t>(f.shot)] = f.group;
    return g;
}
std::vector<std::string> groupNamesOf(const std::vector<CorpusFrame>& c) {
    std::vector<std::string> n;
    for (const CorpusFrame& f : c) {
        if (f.group < 0) continue;
        if (static_cast<size_t>(f.group) >= n.size()) n.resize(static_cast<size_t>(f.group) + 1);
        if (n[static_cast<size_t>(f.group)].empty()) n[static_cast<size_t>(f.group)] = f.groupName;
    }
    return n;
}
// Every shot of one group - the set a leave-one-PROJECT-out fold must not train
// on.
ShotMask groupMask(const std::vector<int>& groupOf, int group) {
    ShotMask m(groupOf.size(), 0);
    for (size_t i = 0; i < groupOf.size(); ++i) m[i] = groupOf[i] == group ? 1 : 0;
    return m;
}

// THE HOST'S INFERENCE. Twinned with RendererCoreBlss::runNet(), including the
// two things that happen to the network's raw answer before it becomes a weight:
// the coverage gate and the deadzone.
WeightField netField(const Net& net, const Frame& fr, float sharpen, float deadAlpha) {
    WeightField wf;
    wf.resize(fr.cols, fr.rows);
    for (size_t k = 0; k < wf.tile.size() && k < fr.features.size(); ++k) {
        // An empty tile is not a decision the network is entitled to make - see
        // kMinCoverage. Without this the net happily asked for full temporal
        // reconstruction of the sky, which is free in PSNR (a flat colour blends
        // with itself) and ghosts the moment the camera turns.
        if (fr.features[k].coverage() < kMinCoverage) continue;
        float o[kOutputs];
        net.forward(fr.features[k], o);
        // A logistic cannot say zero, and the GS charges a whole pass for the
        // 0.02 it says instead - see kDeadzoneAlpha. Per TILE, before the corner
        // averaging, which is exactly where the engine does it.
        applyDeadzone(o, sharpen, deadAlpha);
        for (int m = 0; m < kOutputs; ++m) wf.tile[k][m] = o[m];
    }
    return wf;
}

// PERIOD-2 ALTERNATION, MEASURED ON THE PICTURE - the metric the flicker column
// could never be, and the reason this feature reported an improvement while the
// television bobbed.
//
// A LAG-1 DIFFERENCE CANNOT SEE THE ARTEFACT. `flicker` above is
// mean |O_t - O_{t-1}|, and that number is large for a picture alternating
// between two images AND large for a picture panning smoothly. It has no way to
// separate them, so it moves with the camera move and not with the bug - which
// is why `--flicker-weight` could be swept twice and read "buys nothing" while
// 30.8% of a real PS2's frame alternated every frame.
//
// A SECOND DIFFERENCE CAN. For a signal that returns to where it was two frames
// ago - which IS the bob - O_t - 2*O_{t-1} + O_{t-2} = 2*(P - Q), four times the
// alternation amplitude; for anything moving at a constant rate it is exactly
// zero, and for smooth motion it is the curvature, which is small. So this
// returns |O_t - 2*O_{t-1} + O_{t-2}| / 4: the amplitude of the period-2
// component, in 8-bit levels.
//
// MOTION-COMPENSATED, or it measures the camera instead. Every corpus shot moves,
// so the three frames are three different views and a raw second difference is
// dominated by parallax. Both predecessors are warped into frame t first: O_{t-1}
// through frame t's own reprojection field, O_{t-2} through that and then frame
// t-1's, composed by sampling the second field at the position the first landed
// on. What is left after that is alternation the camera motion does not explain.
//
// AND MOTION COMPENSATION IS NOT FREE, WHICH IS THE MEASUREMENT THAT SHAPED THIS
// FUNCTION. The first build of it gated on nothing but on-screen-ness, and it
// could not tell the two known cases apart on examples/upscaler-lab - the fixture
// a human called "like an earthquake" with jitter on and "steady", byte-identical
// on hardware, with it off:
//
//   ungated, 36 frames, held-out split, shipped net
//                    jitter ON   jitter OFF
//     native full-res    3.564        3.564     <- the floor, and it is the whole number
//     bilinear           3.346        2.964
//     BLSS               2.687        2.398
//
// A floor of 3.56 levels under an artefact of 1.42 (what the console measured),
// and the KNOWN-STILL arm reading 2.40 rather than the ~0 the console captured.
// The floor is the warp's own error: this reprojection field is per tile CORNER
// (255 UVs for the frame, blss.hpp), half these tiles move a full tile per frame,
// and a SECOND difference warps twice, so both predecessors contribute. The
// artefact was riding on top of an error an order of magnitude larger than
// itself, which is the fifth entry of "Measured is not optimised" arriving again
// - this time in a metric written specifically to avoid it.
//
// SO THE GATE IS ON THE WARP, and it is the honest twin of the console
// experiment: the bob was measured with a FROZEN camera, and what a frozen camera
// is, on footage that moves, is the pixels whose warp is short enough to trust.
// A pixel counts when BOTH of its warps are under `kP2Gate` output pixels. That
// is not a fudge factor - the whole point of the number is to compare the same
// content across frames, and past a pixel or two of per-tile-corner warp it is
// not the same content. Every bucket is accumulated in one pass so the table can
// print the gate's own sweep: a gated number whose gate is not shown is a magic
// constant, and this page has been burned by those.
//
// THE SAMPLING TRAP THIS AVOIDS BY CONSTRUCTION: an even-strided sampler always
// lands on the same jitter phase and reports a perfectly still picture (it hid
// this artefact once already, at 40 frames on a 50 Hz console). Three
// CONSECUTIVE frames cannot do that - the stride is 1.
constexpr int kP2Gates = 4;
// Output pixels of warp, per predecessor. The last is "no gate", i.e. the
// ungated number the table above was measured with, kept so the sweep always
// carries its own null result.
constexpr float kP2Gate[kP2Gates] = {1.0f, 2.0f, 4.0f, 1e9f};
// WHICH BUCKET IS "THE" NUMBER - the one the cross-validation column reports and
// the one any claim on docs/neural-upscaler.md is made with. Chosen by the
// validation sweep printed under the --blss-eval table: it is the tightest gate
// that still speaks for a usable slice of the frame.
constexpr int kP2Report = 0;
// "This pixel visibly alternates", in 8-bit levels of amplitude. 2/255 is the
// threshold the console capture reported its 16.3% against, so the two numbers
// mean the same thing.
constexpr double kP2Hot = 2.0;

struct Period2 {
    double sum[kP2Gates] = {};
    long n[kP2Gates] = {};
    long hot[kP2Gates] = {};

    double mean(int g) const { return n[g] ? sum[g] / static_cast<double>(n[g]) : 0.0; }
    // What fraction of the on-screen frame this gate's number speaks for.
    double coverage(int g) const {
        return n[kP2Gates - 1] ? static_cast<double>(n[g]) / n[kP2Gates - 1] : 0.0;
    }
    double hotFraction(int g) const {
        return n[g] ? static_cast<double>(hot[g]) / n[g] : 0.0;
    }
    void add(const Period2& o) {
        for (int g = 0; g < kP2Gates; ++g) {
            sum[g] += o.sum[g];
            n[g] += o.n[g];
            hot[g] += o.hot[g];
        }
    }
};

void period2Alternation(const Image& cur, const Image& prev, const Image& prev2,
                        const ReprojField& toPrev, const ReprojField& prevToPrev2, int outW,
                        int outH, Period2& acc) {
    if (cur.w != prev.w || cur.h != prev.h || cur.w != prev2.w || cur.h != prev2.h) return;
    const auto onScreen = [&](float px, float py) {
        return px >= 0.0f && py >= 0.0f && px < static_cast<float>(outW) &&
               py < static_cast<float>(outH);
    };
    for (int y = 0; y < cur.h; ++y)
        for (int x = 0; x < cur.w; ++x) {
            float du1 = 0.0f, dv1 = 0.0f;
            toPrev.sample(x + 0.5f, y + 0.5f, outW, outH, &du1, &dv1);
            const float px = x + 0.5f + du1, py = y + 0.5f + dv1;
            if (!onScreen(px, py)) continue;
            float du2 = 0.0f, dv2 = 0.0f;
            prevToPrev2.sample(px, py, outW, outH, &du2, &dv2);
            const float qx = px + du2, qy = py + dv2;
            if (!onScreen(qx, qy)) continue;
            // The longer of the two hops is what the gate tests: a short first
            // warp followed by a long second one is still two different views.
            const float w1len = std::sqrt(du1 * du1 + dv1 * dv1);
            const float w2len = std::sqrt(du2 * du2 + dv2 * dv2);
            const float warp = std::max(w1len, w2len);
            int w1[3], w2[3];
            sampleBilinear(prev, px, py, w1);
            sampleBilinear(prev2, qx, qy, w2);
            const uint8_t* c = cur.at(x, y);
            for (int ch = 0; ch < 3; ++ch) {
                const double a =
                    std::fabs(static_cast<double>(c[ch]) - 2.0 * w1[ch] + w2[ch]) * 0.25;
                for (int g = 0; g < kP2Gates; ++g) {
                    if (warp > kP2Gate[g]) continue;
                    acc.sum[g] += a;
                    ++acc.n[g];
                    if (a >= kP2Hot) ++acc.hot[g];
                }
            }
        }
}

// A reconstruction method: tile weights for one frame. `truth` is passed so the
// oracle can be measured through the exact same loop as every fixed kernel -
// it ignores nothing else, and the oracle ignores nothing at all. `idx` is the
// frame's index in the corpus, which lets --cv serve the oracle row out of the
// labels it already computed instead of solving every frame seven more times.
using Method = std::function<WeightField(const Frame&, const Image& truth, size_t idx)>;

// PSNR over one side of the split, closing the temporal loop: each frame's
// history is the previous frame's real composite, which is what the console has.
// `perShot` (optional, indexed by shot id) receives per-shot means, because a
// single average over a bestiary hides exactly the thing worth knowing - which
// cases the network helps and which it should have left alone.
//
// PARALLEL OVER SHOT RUNS, WITH THE ACCUMULATION LEFT SERIAL - and the split is
// what makes this a wall-clock change and nothing else.
//
// The temporal chain resets wherever `cf.shot` changes: that is the only place
// `history` becomes null and the only place a flicker pair is dropped. So a
// maximal RUN of consecutive masked frames carrying the same shot id is an
// independent unit - independent by the code's own rule rather than by an
// assumption about how the corpus was ordered, which is why the runs are found
// here instead of trusting that frames come grouped by shot (they do; see the
// work list in blsscorpus.cpp).
//
// The workers therefore produce PER-FRAME numbers and touch nothing shared, and
// the six running sums are folded in afterwards in corpus order. Every
// accumulator sees exactly the addends the serial loop gave it, in exactly the
// same sequence, so the table is bit-identical at any thread count - the same
// contract --threads already carries for the corpus and the oracle. That
// matters here because a plain --blss-eval was ~80% one serial oracle.
double evalRecurrent(const std::vector<CorpusFrame>& corpus, int shotCount, const ShotMask& want,
                     float sharpen, const Method& weights, const std::string& dumpDir,
                     const char* dumpName, std::vector<double>* perShot,
                     double* flicker, Occupancy* occ = nullptr, int threads = 1,
                     Period2* alternation = nullptr) {
    // The frames this pass looks at, in corpus order, cut into single-shot runs.
    // `runStart` indexes `idxs` and carries a trailing end marker.
    std::vector<size_t> idxs, runStart;
    for (size_t i = 0; i < corpus.size(); ++i) {
        if (!inMask(want, corpus[i].shot)) continue;
        if (idxs.empty() || corpus[idxs.back()].shot != corpus[i].shot)
            runStart.push_back(idxs.size());
        idxs.push_back(i);
    }
    runStart.push_back(idxs.size());

    // One frame's worth of everything the loop below sums. Nothing here is a
    // running total, which is the point.
    struct FrameEval {
        double psnr = 0;
        Occupancy occ;
        double flick = 0;
        bool hasFlick = false;
        // The period-2 amplitude needs THREE consecutive frames, so it starts
        // one frame later than the lag-1 column and is dropped one extra time
        // per shot. Accumulated per frame and summed below, so the reduction
        // is pixel-weighted rather than frame-weighted - which matters here,
        // because the warp gate counts a different number of pixels per frame.
        Period2 alt;
    };
    std::vector<FrameEval> fe(idxs.size());

    parallelFor(static_cast<int>(runStart.size()) - 1, threads, [&](int r) {
        Image prevOut, prevOut2, out;
        bool havePrev = false;
        const size_t runBegin = runStart[static_cast<size_t>(r)];
        for (size_t k = runBegin; k < runStart[static_cast<size_t>(r) + 1]; ++k) {
            const size_t idx = idxs[k];
            const CorpusFrame& cf = corpus[idx];
            Frame fr = cf.frame;
            fr.history = (havePrev && prevOut.w == fr.outW) ? &prevOut : nullptr;
            const WeightField wf = weights(fr, cf.truth, idx);
            FrameEval& e = fe[k];
            // OCCUPANCY: how much of the screen this method's weights actually
            // make the GS draw. It belongs next to PSNR for the same reason
            // flicker does - the whole performance case for BLSS is that passes
            // 2..5 cover a minority of the frame, and until this was printed the
            // network was quietly asking for all of them everywhere.
            e.occ = occupancy(wf, sharpen);
            composite(fr, wf, sharpen, out);
            e.psnr = psnr(out, cf.truth);
            // FLICKER: mean per-pixel change between consecutive frames of one
            // shot. This exists because per-frame PSNR is blind to temporal
            // instability - averaging two jitter phases is genuinely closer to
            // the truth than either, so a reconstruction that OSCILLATES between
            // them scores well and looks like bob deinterlacing on a television.
            // That is exactly how the 50% temporal cap shipped. Compare a
            // method's flicker against the `native` row, which is the honest
            // floor for a given camera move.
            if (havePrev && prevOut.w == out.w && prevOut.h == out.h) {
                double d = 0.0;
                for (int y = 0; y < out.h; ++y)
                    for (int x = 0; x < out.w; ++x) {
                        const uint8_t* a = out.at(x, y);
                        const uint8_t* b = prevOut.at(x, y);
                        for (int c = 0; c < 3; ++c)
                            d += std::fabs(static_cast<double>(a[c]) - b[c]);
                    }
                e.flick = d / (static_cast<double>(out.w) * out.h * 3.0);
                e.hasFlick = true;
            }
            // PERIOD-2: the one that can actually see the bob. Needs the two
            // predecessors AND the reprojection field of the previous frame, so
            // that O_{t-2} can be warped all the way into this view; k-1 is the
            // previous frame of THIS shot by construction (a run never spans a
            // shot change).
            if (alternation && k >= runBegin + 2)
                period2Alternation(out, prevOut, prevOut2, fr.reproj,
                                   corpus[idxs[k - 1]].frame.reproj, fr.outW, fr.outH, e.alt);
            // The dumped frame is the first one this pass accepts, which is the
            // first frame of run 0 - the same image the serial loop wrote at
            // `n == 1`, written by the worker that owns it.
            if (!dumpDir.empty() && k == 0 && dumpName)
                writePng(out, dumpDir + "/blss-" + dumpName + ".png");
            // Rotate rather than copy: `out` is fully overwritten by the next
            // composite(), so its old content is free to be thrown away here.
            std::swap(prevOut2, prevOut);
            std::swap(prevOut, out);
            havePrev = true;
        }
    });

    double sum = 0.0;
    int n = 0;
    double occA = 0.0, occC = 0.0, occD = 0.0;
    std::vector<double> shotSum(shotCount, 0.0);
    std::vector<int> shotN(shotCount, 0);
    double flickSum = 0.0;
    int flickN = 0;
    Period2 altAcc;
    for (size_t k = 0; k < idxs.size(); ++k) {
        const CorpusFrame& cf = corpus[idxs[k]];
        const FrameEval& e = fe[k];
        occA += e.occ.point;
        occC += e.occ.temporal;
        occD += e.occ.sharpen;
        sum += e.psnr;
        ++n;
        if (cf.shot < shotCount) {
            shotSum[cf.shot] += e.psnr;
            ++shotN[cf.shot];
        }
        if (e.hasFlick) {
            flickSum += e.flick;
            ++flickN;
        }
        altAcc.add(e.alt);
    }
    if (perShot) {
        perShot->assign(shotCount, 0.0);
        for (int s = 0; s < shotCount; ++s)
            (*perShot)[s] = shotN[s] ? shotSum[s] / shotN[s] : 0.0;
    }
    if (flicker) *flicker = flickN ? flickSum / flickN : 0.0;
    if (alternation) *alternation = altAcc;
    if (occ) {
        const double inv = n ? 1.0 / n : 0.0;
        occ->point = static_cast<float>(occA * inv);
        occ->temporal = static_cast<float>(occC * inv);
        occ->sharpen = static_cast<float>(occD * inv);
        occ->passes = 1.0f + occ->point + occ->temporal + 2.0f * occ->sharpen;
    }
    return n ? sum / n : 0.0;
}

// A constant weight field - every fixed kernel is one of these.
Method fixedMethod(Kernel k) {
    return [k](const Frame& fr, const Image&, size_t) {
        WeightField wf;
        wf.resize(fr.cols, fr.rows);
        std::array<float, kOutputs> w{};
        switch (k) {
            case Kernel::Point: w[0] = 1.0f; break;
            case Kernel::Bilinear: break;
            case Kernel::Temporal: w[1] = 1.0f; break;
            case Kernel::Sharpen: w[2] = 1.0f; break;
        }
        for (auto& t : wf.tile) t = w;
        return wf;
    };
}

// ------------------------------------------------------- oracle labels, once ---

// The oracle's answer for a frame does NOT depend on which side of a split the
// frame is on - it is a per-frame optimisation against that frame's own truth.
// So cross-validation labels every frame once and each fold merely selects,
// which is what makes seven folds cost barely more than one split. Per frame,
// cols*rows samples in tile order; importance is raw here and normalised by
// gatherSamples() once the fold is known.
using FrameLabels = std::vector<Sample>;

std::vector<FrameLabels> labelCorpus(const std::vector<CorpusFrame>& corpus, float sharpen,
                                     const Objective& obj, int threads,
                                     const ShotMask* want = nullptr) {
    std::vector<FrameLabels> labels(corpus.size());
    parallelFor(static_cast<int>(corpus.size()), threads, [&](int i) {
        const CorpusFrame& cf = corpus[static_cast<size_t>(i)];
        if (want && !inMask(*want, cf.shot)) return;
        std::vector<float> imp;
        const WeightField wf = oracle(cf.frame, cf.truth, sharpen, &imp, obj);
        FrameLabels& out = labels[static_cast<size_t>(i)];
        out.resize(wf.tile.size());
        for (size_t k = 0; k < wf.tile.size(); ++k) {
            out[k].f = cf.frame.features[k];
            out[k].target = wf.tile[k];
            out[k].importance = imp[k];
        }
    });
    return labels;
}

// The labelled tiles of the shots in `want`, importance normalised to mean 1 so
// the learning rate means the same thing whatever the fold looks like.
std::vector<Sample> gatherSamples(const std::vector<FrameLabels>& labels,
                                  const std::vector<CorpusFrame>& corpus, const ShotMask& want) {
    std::vector<Sample> out;
    double impSum = 0.0;
    for (size_t i = 0; i < corpus.size() && i < labels.size(); ++i) {
        if (!inMask(want, corpus[i].shot)) continue;
        for (const Sample& s : labels[i]) {
            out.push_back(s);
            impSum += s.importance;
        }
    }
    const float mean = out.empty() ? 0.0f : static_cast<float>(impSum / out.size());
    if (mean > 0.0f)
        for (Sample& s : out) s.importance /= mean;
    return out;
}

// The oracle row, served from the labels instead of re-solved. Same field, and
// the reason --cv can print an upper bound per fold for free.
Method labelMethod(const std::vector<FrameLabels>& labels) {
    return [&labels](const Frame& fr, const Image&, size_t idx) {
        WeightField wf;
        wf.resize(fr.cols, fr.rows);
        if (idx < labels.size())
            for (size_t k = 0; k < wf.tile.size() && k < labels[idx].size(); ++k)
                wf.tile[k] = labels[idx][k].target;
        return wf;
    };
}

Method netMethod(const Net& net, float sharpen, float deadAlpha) {
    return [&net, sharpen, deadAlpha](const Frame& fr, const Image&, size_t) {
        return netField(net, fr, sharpen, deadAlpha);
    };
}

// ------------------------------------------------------- cross-validation ---

// ONE FOLD: train on every shot but `shot`, measure on `shot`.
struct FoldResult {
    int shot = 0;
    int group = 0;  // which union member the held-out shot came from
    double blss = 0, bilinear = 0, oracleBound = 0, native = 0;  // held-out shot, dB
    double inBlss = 0, inBilinear = 0;                           // the trained-on shots
    double flicker = 0;
    // PERIOD-2 ALTERNATION on the held-out shot, and the same quantity for the
    // native render, which is this metric's own floor. The margin column cannot
    // see the bob and neither can `flicker`; these two are what a stability
    // change has to move. See period2Alternation().
    Period2 alt, nativeAlt, bilinearAlt;
    Occupancy occ;
    // The same net, evaluated at each swept deadzone (index 0 is the run's own
    // --deadzone, so `blss`/`occ` above are dzBlss[0]/dzOcc[0]). One training
    // run, N rows.
    std::vector<double> dzBlss;
    std::vector<Occupancy> dzOcc;
    double margin() const { return blss - bilinear; }
    double inMargin() const { return inBlss - inBilinear; }
};

// The deadzones one --cv run measures: whatever --deadzone-sweep listed, with
// the run's own --deadzone first so the tables above never change meaning.
std::vector<float> deadzonesOf(const CliOpts& o) {
    std::vector<float> v;
    v.push_back(o.deadzone);
    for (float d : o.deadzoneSweep)
        if (d != o.deadzone) v.push_back(d);
    return v;
}

// The period-2 metric's own floor over a set of shots: the same measurement on
// the NATIVE render, which is full-resolution, one sample and unjittered and
// therefore has no alternation to have. What it reports is bilinear resampling
// error in the two warps plus the real curvature of the camera move, so a
// reconstruction is only bobbing to the extent it sits above this.
Period2 nativeAlternation(const std::vector<CorpusFrame>& corpus, const ShotMask& want) {
    Period2 acc;
    const CorpusFrame* prev1 = nullptr;
    const CorpusFrame* prev2 = nullptr;
    for (const CorpusFrame& cf : corpus) {
        if (!inMask(want, cf.shot)) continue;
        if (prev1 && prev2 && prev1->shot == cf.shot && prev2->shot == cf.shot)
            period2Alternation(cf.native, prev1->native, prev2->native, cf.frame.reproj,
                               prev1->frame.reproj, cf.frame.outW, cf.frame.outH, acc);
        prev2 = prev1;
        prev1 = &cf;
    }
    return acc;
}

double nativePsnr(const std::vector<CorpusFrame>& corpus, const ShotMask& want) {
    double sum = 0.0;
    int n = 0;
    for (const CorpusFrame& cf : corpus)
        if (inMask(want, cf.shot)) {
            sum += psnr(cf.native, cf.truth);
            ++n;
        }
    return n ? sum / n : 0.0;
}

// LEAVE-ONE-SHOT-OUT over one corpus. Seven folds, seven independent estimates
// of "does this beat bilinear on content it has not seen", instead of the single
// draw the shipped 2-of-7 split gives - and, more usefully, seven answers to
// WHICH content it fails on.
std::vector<FoldResult> crossValidateOnce(const std::vector<CorpusFrame>& corpus, int shots,
                                          const CliOpts& o, uint32_t seed,
                                          double* labelSeconds = nullptr,
                                          double* foldSeconds = nullptr,
                                          const std::vector<int>* groupOf = nullptr) {
    const auto clock0 = std::chrono::steady_clock::now();
    const std::vector<FrameLabels> labels = labelCorpus(corpus, o.sharpen, o.obj, o.threads);
    const auto clock1 = std::chrono::steady_clock::now();
    if (labelSeconds) *labelSeconds += std::chrono::duration<double>(clock1 - clock0).count();
    const int nFolds = o.cvFolds > 0 ? std::min(o.cvFolds, shots) : shots;

    // THE TWO BILINEAR ROWS ARE A PER-CORPUS CONSTANT, computed once here rather
    // than once per fold. With an all-zero weight field alphaBytes() yields
    // aA = aC = aD = 0 and blend() hands back `base` untouched, so a frame's
    // bilinear composite does not read the history at all - which makes its PSNR
    // independent of the shot chain, of the fold, and of which side of the split
    // the frame landed on.
    //
    // Every fold used to re-composite its TWELVE training shots to rediscover
    // that: 13 folds x 13 shots against 13 distinct answers, and it was ~44% of
    // the evaluation work in a --cv run. Bit-exact, because the mean below sums
    // the same per-frame values in the same corpus order evalRecurrent did.
    std::vector<double> bilinearPsnr(corpus.size(), 0.0);
    parallelFor(static_cast<int>(corpus.size()), o.threads, [&](int i) {
        const CorpusFrame& cf = corpus[static_cast<size_t>(i)];
        WeightField zero;
        zero.resize(cf.frame.cols, cf.frame.rows);
        Image out;
        composite(cf.frame, zero, o.sharpen, out);
        bilinearPsnr[static_cast<size_t>(i)] = psnr(out, cf.truth);
    });
    const auto meanBilinear = [&](const ShotMask& want) {
        double sum = 0.0;
        int n = 0;
        for (size_t i = 0; i < corpus.size(); ++i)
            if (inMask(want, corpus[i].shot)) {
                sum += bilinearPsnr[i];
                ++n;
            }
        return n ? sum / n : 0.0;
    };

    std::vector<FoldResult> folds(static_cast<size_t>(nFolds));
    // The fold loop used to print NOTHING - the trainer's verbosity is off in
    // here (tc.verbose below) and a fold's rows only appear in the table at the
    // very end - so a progress bar driving off this tool's output went blank for
    // minutes. One line per completed fold is a real fraction. The count is
    // completion order, not the fold index: folds run in parallel, a bar wants a
    // fraction rather than an identity, and this line is not a measurement.
    std::atomic<int> foldsDone{0};
    std::mutex foldPrint;
    // Folds are independent, so they run in parallel; each writes only its own
    // slot and trains its own net from its own sample vector.
    parallelFor(nFolds, o.threads, [&](int f) {
        FoldResult& r = folds[static_cast<size_t>(f)];
        r.shot = f;
        r.group = groupOf && f < static_cast<int>(groupOf->size()) ? (*groupOf)[static_cast<size_t>(f)] : 0;
        const ShotMask test = singleShotMask(shots, f);
        // LEAVE-ONE-PROJECT-OUT: the held-out shot is still ONE shot, so the row
        // is still one kind of content, but the training set loses the whole
        // project it belongs to. Without this the net has already seen eleven
        // other camera moves of the same scene, which is the question
        // "generalises to a seventh move", not "generalises to a new project".
        const ShotMask train =
            (o.cvGroups && groupOf) ? complementMask(groupMask(*groupOf, r.group))
                                    : complementMask(test);

        const std::vector<Sample> samples = gatherSamples(labels, corpus, train);
        TrainConfig tc = trainConfigOf(o);
        tc.seed = seed;
        tc.verbose = false;
        Net net;
        blss::train(net, samples, tc);

        double fl = 0.0;
        // Every deadzone on the list, same net, same held-out shot. Only the
        // first one feeds the fold tables; the rest are the sweep.
        const std::vector<float> dz = deadzonesOf(o);
        r.dzBlss.resize(dz.size());
        r.dzOcc.resize(dz.size());
        for (size_t d = 0; d < dz.size(); ++d) {
            double dfl = 0.0;
            Period2 dalt;
            r.dzBlss[d] = evalRecurrent(corpus, shots, test, o.sharpen,
                                        netMethod(net, o.sharpen, dz[d]), "", nullptr, nullptr,
                                        &dfl, &r.dzOcc[d], 1, &dalt);
            if (d == 0) {
                fl = dfl;
                r.alt = dalt;
            }
        }
        r.blss = r.dzBlss[0];
        r.occ = r.dzOcc[0];
        r.flicker = fl;
        // The two controls the alternation column is read against: the plain
        // half-res upscale (which alternates because the LOW-RES RENDER does,
        // with nothing fusing it - the honest "how much bob is there to remove"
        // number) and the native render (the metric's own residual).
        evalRecurrent(corpus, shots, test, o.sharpen, fixedMethod(Kernel::Bilinear), "", nullptr,
                      nullptr, &fl, nullptr, 1, &r.bilinearAlt);
        r.nativeAlt = nativeAlternation(corpus, test);
        r.bilinear = meanBilinear(test);
        r.oracleBound = evalRecurrent(corpus, shots, test, o.sharpen, labelMethod(labels), "",
                                      nullptr, nullptr, &fl);
        r.native = nativePsnr(corpus, test);
        // The in-distribution control. If this collapses, the fold did not train
        // and its held-out number says nothing about generalisation.
        r.inBlss = evalRecurrent(corpus, shots, train, o.sharpen,
                                 netMethod(net, o.sharpen, o.deadzone), "", nullptr,
                                 nullptr, &fl);
        r.inBilinear = meanBilinear(train);
        {
            const int k = foldsDone.fetch_add(1) + 1;
            std::lock_guard<std::mutex> lk(foldPrint);
            std::printf("[blss] fold %d of %d\n", k, nFolds);
        }
    });
    if (foldSeconds)
        *foldSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - clock1)
                            .count();
    return folds;
}

// Seeds for --cv-seeds N. The first is whatever --seed says, so `--cv` alone
// reproduces the shipped corpus; the rest are derived from it, so the whole
// table is one number away from being re-runnable.
uint32_t cvSeedAt(uint32_t base, int i) {
    if (i == 0) return base;
    uint32_t h = base ^ (0x9E3779B9u * static_cast<uint32_t>(i + 1));
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h ? h : 1u;
}

int crossValidate(const CliOpts& o) {
    std::printf(
        "\nblss: leave-one-%s-out cross-validation, %d seed(s) x every shot held out in turn\n"
        "      %d frames, %d epochs, decay %.0e, %s inputs, %d hidden unit(s)\n"
        "      objective: flicker %.3f (%s), fill %.2f, sharpen %.2f;"
        " inference deadzone %.1f alpha\n",
        o.cvGroups ? "PROJECT" : "shot",
        o.cvSeeds, o.frames, o.epochs,
        static_cast<double>(trainConfigOf(o).weightDecay),
        o.standardise ? "standardised" : "raw", kHidden, o.obj.flicker,
        o.obj.flickerForm == FlickerForm::Period2 ? "period2" : "lag1", o.obj.fill, o.sharpen,
        o.deadzone);

    std::vector<std::vector<FoldResult>> all;  // [seed][fold]
    std::vector<uint32_t> seeds;
    std::vector<std::string> shotNames;
    std::vector<int> groupOf;                 // shot id -> union member
    std::vector<std::string> groupNames;
    int shots = 0;
    // The same three-phase accounting --blss-train prints, for the same reason:
    // which phase dominates moves with the frame count, the fold count and the
    // core count, and nobody can check a speedup that is not reported.
    const auto tStart = std::chrono::steady_clock::now();
    const auto since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    };
    double tCorpus = 0.0, tLabel = 0.0, tFolds = 0.0;
    for (int si = 0; si < o.cvSeeds; ++si) {
        CliOpts so = o;
        so.seed = cvSeedAt(o.seed, si);
        seeds.push_back(so.seed);
        const auto tCorpus0 = std::chrono::steady_clock::now();
        std::vector<CorpusFrame> corpus = buildCorpus(so);
        tCorpus += since(tCorpus0);
        if (corpus.empty()) {
            std::printf("blss: the corpus generator produced no frames\n");
            return 1;
        }
        shots = shotCountOf(corpus);
        if (shotNames.empty())
            for (int s = 0; s < shots; ++s) shotNames.push_back(shotNameOf(corpus, s));
        if (groupOf.empty()) {
            groupOf = shotGroupsOf(corpus, shots);
            groupNames = groupNamesOf(corpus);
            // On a union corpus two members can easily name a scene the same
            // thing ("main orbit"), so the fold rows carry the member's last
            // path component - a table of thirty rows in which three say
            // "main orbit" identifies nothing.
            if (groupNames.size() > 1)
                for (int s = 0; s < shots && s < static_cast<int>(shotNames.size()); ++s) {
                    const std::string& g = groupNames[static_cast<size_t>(groupOf[static_cast<size_t>(s)])];
                    const size_t cut = g.find_last_of("/\\");
                    const std::string leaf = cut == std::string::npos ? g : g.substr(cut + 1);
                    shotNames[static_cast<size_t>(s)] = leaf + "/" + shotNames[static_cast<size_t>(s)];
                }
        }
        // A leave-one-PROJECT-out run over ONE project would train every fold on
        // nothing. Say so instead of printing a table of zeros.
        if (o.cvGroups && groupNames.size() < 2) {
            std::printf(
                "blss: --cv-groups needs a UNION corpus - pass two or more project "
                "directories. With one source every fold's training set is empty.\n");
            return 1;
        }
        std::printf("blss: seed 0x%X - labelling %zu frames, then %d fold(s) over %d shot(s)\n",
                    so.seed, corpus.size(), o.cvFolds > 0 ? std::min(o.cvFolds, shots) : shots,
                    shots);
        all.push_back(crossValidateOnce(corpus, shots, so, so.seed, &tLabel, &tFolds, &groupOf));
    }
    if (all.empty() || shots <= 0 || all[0].empty()) return 1;
    // Folds may be fewer than shots (--cv-folds): the rows of the table are the
    // shots that get held out, the training set is always everything else.
    const int nFolds = static_cast<int>(all[0].size());

    // --- the table that matters: held-out margin over plain bilinear, per fold.
    std::printf("\n  BLSS - bilinear on the HELD-OUT shot, dB (positive = the network helped)\n");
    std::printf("  %-27s", "held-out shot");
    for (uint32_t s : seeds) std::printf("  seed %-8X", s);
    std::printf("      mean     sd\n  %s\n",
                std::string(29 + seeds.size() * 15 + 17, '-').c_str());

    std::vector<double> everyFold;
    int losses = 0;
    for (int f = 0; f < nFolds; ++f) {
        std::printf("  %-2d %-24s", f, shotNames[static_cast<size_t>(f)].c_str());
        std::vector<double> perSeed;
        for (size_t si = 0; si < all.size(); ++si) {
            const double m = all[si][static_cast<size_t>(f)].margin();
            perSeed.push_back(m);
            everyFold.push_back(m);
            if (m < 0.0) ++losses;
            std::printf("  %+12.2f ", m);
        }
        const Spread sp = spreadOf(perSeed);
        std::printf(" %+8.2f  %5.2f\n", sp.mean, sp.sd);
    }
    std::printf("  %s\n", std::string(29 + seeds.size() * 15 + 17, '-').c_str());

    // Mean over folds, per seed: this is the number the single split was trying
    // to estimate with one draw.
    std::printf("  %-27s", "mean over folds");
    std::vector<double> seedMeans;
    for (size_t si = 0; si < all.size(); ++si) {
        std::vector<double> v;
        for (int f = 0; f < nFolds; ++f) v.push_back(all[si][static_cast<size_t>(f)].margin());
        const Spread sp = spreadOf(v);
        seedMeans.push_back(sp.mean);
        std::printf("  %+12.2f ", sp.mean);
    }
    const Spread overall = spreadOf(everyFold);
    const Spread bySeed = spreadOf(seedMeans);
    std::printf(" %+8.2f  %5.2f\n", overall.mean, bySeed.sd);
    // Passes belong on the SUMMARY line, not only in the table below, because a
    // margin bought at 5.00 passes is not a win - it is plain bilinear plus every
    // kernel everywhere, which costs more fill than the half-res render saved.
    // Reading the dB column alone is how this feature would make the same mistake
    // a fifth time.
    std::vector<double> everyPasses;
    for (const auto& seedRun : all)
        for (const FoldResult& r : seedRun) everyPasses.push_back(r.occ.passes);
    const Spread pas = spreadOf(everyPasses);
    std::printf("\n  overall %+.2f dB, sd %.2f over %d fold-runs; %d of %d BELOW bilinear;"
                " %.2f passes (sd %.2f)\n",
                overall.mean, overall.sd, overall.n, losses, overall.n, pas.mean, pas.sd);
    std::printf("  (sd of the per-seed fold-mean: %.2f - that is what one --blss-eval run"
                " is estimating)\n", bySeed.sd);

    // --- PER PROJECT, which is the only summary a union corpus is entitled to.
    // A mean over every fold of a union is dominated by whichever member has the
    // most shots AND by whichever members have no headroom at all - and most
    // example projects have none (`--blss-eval` says +0.01 to +0.1 dB oracle on
    // half of them), so an average over them is an average over ties. The
    // decision has to be read off the rows that can discriminate anything, which
    // is what the `ceiling` column is for: a project whose ceiling is +0.05 dB
    // cannot report a meaningful margin in either direction.
    if (groupNames.size() > 1) {
        std::printf("\n  PER PROJECT - the held-out margin of the CROSS-PROJECT net,"
                    " against that project's own bilinear\n");
        std::printf("  %-34s %6s %9s %7s %9s %8s\n", "held-out project", "folds", "margin", "sd",
                    "ceiling", "passes");
        std::printf("  %s\n", std::string(36 + 6 + 9 + 7 + 9 + 8 + 5, '-').c_str());
        for (size_t g = 0; g < groupNames.size(); ++g) {
            std::vector<double> m;
            double ceil = 0.0, pas = 0.0;
            int n = 0;
            for (const auto& seedRun : all)
                for (const FoldResult& r : seedRun)
                    if (static_cast<size_t>(r.group) == g) {
                        m.push_back(r.margin());
                        ceil += r.oracleBound - r.bilinear;
                        pas += r.occ.passes;
                        ++n;
                    }
            if (!n) continue;
            const Spread sp = spreadOf(m);
            std::printf("  %-34s %6d %+9.2f %7.2f %+9.2f %8.2f\n",
                        groupNames[g].c_str(), n, sp.mean, sp.sd, ceil / n, pas / n);
        }
        std::printf("\n  `ceiling` is the ORACLE's margin over the same bilinear on the same"
                    " folds - the best any\n  per-tile weighting can reach. A project whose"
                    " ceiling is near zero cannot discriminate\n  between two nets, and its row"
                    " is a tie however it reads.\n");
    }

    // --- absolutes, averaged over seeds. Which content it helps, and at what
    // fill: a margin alone cannot say whether a fold is hard or merely cheap.
    std::printf("\n  per fold, mean over %zu seed(s): PSNR on the held-out shot\n", seeds.size());
    std::printf("  %-27s %8s %8s %8s %8s %8s %8s %8s\n", "held-out shot", "native", "bilin",
                "BLSS", "oracle", "passes", "flicker", "in-dist");
    std::printf("  %s\n", std::string(29 + 8 * 9, '-').c_str());
    for (int f = 0; f < nFolds; ++f) {
        double nat = 0, bil = 0, net = 0, orc = 0, pas = 0, fli = 0, ind = 0;
        for (const auto& seedRun : all) {
            const FoldResult& r = seedRun[static_cast<size_t>(f)];
            nat += r.native;
            bil += r.bilinear;
            net += r.blss;
            orc += r.oracleBound;
            pas += r.occ.passes;
            fli += r.flicker;
            ind += r.inMargin();
        }
        const double k = 1.0 / static_cast<double>(all.size());
        std::printf("  %-2d %-24s %8.2f %8.2f %8.2f %8.2f %8.2f %8.2f %+8.2f\n", f,
                    shotNames[static_cast<size_t>(f)].c_str(), nat * k, bil * k, net * k, orc * k,
                    pas * k, fli * k, ind * k);
    }
    std::printf("\n  in-dist is BLSS - bilinear on that fold's TRAINING shots: the control"
                " that says\n  the fold trained at all. A held-out number under a collapsed"
                " in-dist number means nothing.\n");

    // --- PERIOD-2 ALTERNATION, per fold. This is the column a stability change
    // has to move, and none of the tables above it can see the artefact: the
    // margin is a still-image metric (a reconstruction that OSCILLATES between
    // the two jitter phases scores BETTER than one that fuses them, because the
    // average of two phases is genuinely closer to the truth than either) and
    // `flicker` is a lag-1 difference, which reads the camera move rather than
    // the bug. See period2Alternation().
    //
    // Three numbers per fold, and the two controls are what make the first one
    // mean anything: `native` is the metric's own residual on an unjittered
    // one-sample render, and `bilinear` is the plain half-res upscale with
    // nothing fusing the phases - i.e. how much bob there is to remove.
    {
        Period2 tn, tb, tk;
        std::printf("\n  PERIOD-2 ALTERNATION on the held-out shot, 8-bit levels, warp gate"
                    " %.0f px (jitter is %s)\n",
                    static_cast<double>(kP2Gate[kP2Report]), jitterEnabled() ? "ON" : "OFF");
        std::printf("  %-27s %10s %10s %10s %10s\n", "held-out shot", "native", "bilinear", "BLSS",
                    "BLSS/native");
        std::printf("  %s\n", std::string(29 + 44, '-').c_str());
        for (int f = 0; f < nFolds; ++f) {
            Period2 na, ba, ka;
            for (const auto& seedRun : all) {
                const FoldResult& r = seedRun[static_cast<size_t>(f)];
                na.add(r.nativeAlt);
                ba.add(r.bilinearAlt);
                ka.add(r.alt);
            }
            tn.add(na);
            tb.add(ba);
            tk.add(ka);
            const double nm = na.mean(kP2Report);
            std::printf("  %-2d %-24s %10.3f %10.3f %10.3f %10.2f\n", f,
                        shotNames[static_cast<size_t>(f)].c_str(), nm, ba.mean(kP2Report),
                        ka.mean(kP2Report), nm > 1e-9 ? ka.mean(kP2Report) / nm : 0.0);
        }
        const double tnm = tn.mean(kP2Report);
        std::printf("  %s\n", std::string(29 + 44, '-').c_str());
        std::printf("  %-27s %10.3f %10.3f %10.3f %10.2f\n", "mean over folds", tnm,
                    tb.mean(kP2Report), tk.mean(kP2Report),
                    tnm > 1e-9 ? tk.mean(kP2Report) / tnm : 0.0);
        // What the gate kept, and the same "how much of the picture" number the
        // console capture reported - without these the mean above is a magic
        // constant applied to an unstated fraction of the frame.
        std::printf("  gate keeps %.1f%% of the on-screen frame; pixels at or above %.0f/255:"
                    " BLSS %.1f%%, bilinear %.1f%%, native %.1f%%\n",
                    100.0 * tk.coverage(kP2Report), kP2Hot, 100.0 * tk.hotFraction(kP2Report),
                    100.0 * tb.hotFraction(kP2Report), 100.0 * tn.hotFraction(kP2Report));
    }

    // --- the inference deadzone, over the SAME nets. Nothing here was
    // re-trained: the deadzone is applied to the network's answer, so every row
    // is the same 39 fold-runs evaluated again. Margin AND passes, together,
    // because the whole point of the knob is that it trades one for the other -
    // and a decibel bought at five passes is not a win.
    const std::vector<float> dz = deadzonesOf(o);
    if (dz.size() > 1) {
        std::printf("\n  INFERENCE DEADZONE sweep - same nets, evaluation only"
                    " (%zu fold-run(s) per row)\n", everyFold.size());
        std::printf("  %10s %10s %10s %8s %8s %8s %14s\n", "alpha", "margin", "passes", "point",
                    "temp", "sharp", "below bilinear");
        std::printf("  %s\n", std::string(74, '-').c_str());
        for (size_t d = 0; d < dz.size(); ++d) {
            std::vector<double> m, p;
            double pt = 0, tp = 0, sh = 0;
            int below = 0;
            for (const auto& seedRun : all)
                for (const FoldResult& r : seedRun) {
                    if (d >= r.dzBlss.size()) continue;
                    const double mg = r.dzBlss[d] - r.bilinear;
                    m.push_back(mg);
                    p.push_back(r.dzOcc[d].passes);
                    pt += r.dzOcc[d].point;
                    tp += r.dzOcc[d].temporal;
                    sh += r.dzOcc[d].sharpen;
                    if (mg < 0.0) ++below;
                }
            const Spread sm = spreadOf(m), sp2 = spreadOf(p);
            const double k = m.empty() ? 0.0 : 1.0 / static_cast<double>(m.size());
            std::printf("  %10.1f %+10.2f %10.2f %7.1f%% %7.1f%% %7.1f%% %10d/%d%s\n",
                        static_cast<double>(dz[d]), sm.mean, sp2.mean, pt * k * 100.0,
                        tp * k * 100.0, sh * k * 100.0, below, sm.n,
                        d == 0 ? "  <-- this build" : "");
        }
    }
    std::printf("\nblss: timing - corpus %.1f s, oracle %.1f s, folds %.1f s (%.1f s total)\n",
                tCorpus, tLabel, tFolds, since(tStart));
    std::printf("\n");
    return 0;
}

// ----------------------------------------------------- feature diagnostics ---

// WHAT THE SIX INPUTS ACTUALLY LOOK LIKE. A channel that never moves is a
// channel the 123 weights cannot use, and no amount of training fixes it - this
// is the same class of bug as a term missing from the objective, one level
// further down. Prints, per feature: the distribution over the corpus, and how
// strongly it correlates with what the ORACLE asked for (weighted by importance,
// because tiles where every kernel is equal do not get a vote in training
// either).
// ONE CHANNEL AS THE CONSOLE MEASURED IT. The engine's debugView-2 line prints
// min/mean/max per tile grid, so a probe carries a band, not a point - and the
// band is the interesting part: a min that equals its max is a channel saying
// the same thing in every tile of the frame, which is what "no per-tile
// decision is happening" looks like from the outside.
struct ProbeChannel {
    bool given = false;
    float lo = 0, mid = 0, hi = 0;
};

// Parses `motion=0.000/0.183/0.941 depth=...`, i.e. exactly what
// RendererCoreBlss::logFeatureSpread() writes into the game's log. A leading
// `BLSSFEAT` / `LOG:` and any punctuation between fields are ignored, so the
// line can be pasted straight out of bin/log.txt. `name=value` (one number) is
// accepted too, for a hand-written vector.
std::array<ProbeChannel, kFeatures> parseProbe(const std::string& text, int* unknown) {
    std::array<ProbeChannel, kFeatures> out{};
    *unknown = 0;
    size_t at = 0;
    while (at < text.size()) {
        const size_t eq = text.find('=', at);
        if (eq == std::string::npos) break;
        // The key is the run of name characters immediately before the '='.
        size_t ks = eq;
        while (ks > at && (std::isalnum(static_cast<unsigned char>(text[ks - 1])) ||
                           text[ks - 1] == '_'))
            --ks;
        const std::string key = text.substr(ks, eq - ks);
        size_t vs = eq + 1;
        size_t ve = vs;
        while (ve < text.size() && (std::isdigit(static_cast<unsigned char>(text[ve])) ||
                                    text[ve] == '.' || text[ve] == '-' || text[ve] == '+' ||
                                    text[ve] == '/' || text[ve] == 'e' || text[ve] == 'E'))
            ++ve;
        const std::string val = text.substr(vs, ve - vs);
        at = ve > eq ? ve : eq + 1;
        if (key.empty() || val.empty()) continue;
        int idx = -1;
        for (int c = 0; c < kFeatures; ++c)
            if (key == kFeatureNames[c]) idx = c;
        if (idx < 0) {
            // The short spellings the FIRST, throwaway instrument used (the one
            // that was deleted after a single reading). Accepted so the vectors
            // recorded in commit 0f33c17b's message can still be placed in a
            // corpus, which is the whole reason that instrument is permanent now.
            static const struct {
                const char* alias;
                int idx;
            } kAliases[] = {{"grad", 2}, {"edge", 3}, {"detail", 4}, {"cover", 5}};
            for (const auto& a : kAliases)
                if (key == a.alias) idx = a.idx;
        }
        if (idx < 0) {
            // RETIRED CHANNELS, skipped in silence rather than counted as
            // garbage: an old console log or an old commit message still has
            // `histAge` and `luma` in it, and a probe that refused to read the
            // rest of the line would make the historical vectors unplaceable -
            // which is exactly the failure that put this tool in the tree.
            static const char* const kRetired[] = {"histAge", "hist", "luma"};
            bool retired = false;
            for (const char* r : kRetired) retired = retired || key == r;
            if (retired) continue;
            ++*unknown;
            continue;
        }
        std::vector<float> nums;
        size_t p = 0;
        while (p <= val.size()) {
            const size_t slash = val.find('/', p);
            const std::string one = val.substr(p, slash - p);
            if (!one.empty()) nums.push_back(static_cast<float>(std::atof(one.c_str())));
            if (slash == std::string::npos) break;
            p = slash + 1;
        }
        if (nums.empty()) continue;
        ProbeChannel& pc = out[static_cast<size_t>(idx)];
        pc.given = true;
        if (nums.size() >= 3) {
            pc.lo = nums[0];
            pc.mid = nums[1];
            pc.hi = nums[2];
        } else if (nums.size() == 2) {
            pc.lo = nums[0];
            pc.hi = nums[1];
            pc.mid = 0.5f * (nums[0] + nums[1]);
        } else {
            pc.lo = pc.mid = pc.hi = nums[0];
        }
    }
    return out;
}

// Fraction of `sorted` at or below v.
double percentileOf(const std::vector<float>& sorted, float v) {
    if (sorted.empty()) return 0.0;
    const size_t n = static_cast<size_t>(
        std::upper_bound(sorted.begin(), sorted.end(), v) - sorted.begin());
    return 100.0 * static_cast<double>(n) / static_cast<double>(sorted.size());
}

// HOW MUCH CORPUS SITS WHERE THE CONSOLE IS - the fraction of tiles within
// kSupportBand of v, and the number the verdict is actually read off.
//
// A percentile cannot answer this and quietly lies at both ends: histAge = 1.0
// is the 100th percentile of the corpus AND 4.6% of its tiles, while
// texDetail = 1.0 is also the 100th percentile and 0.0% of them. The network is
// fitted by a weighted MSE, so what decides whether it INTERPOLATES or
// EXTRAPOLATES at a value is how much of the gradient came from near that
// value - which is this, not the rank.
constexpr double kSupportBand = 0.05;
double supportOf(const std::vector<float>& sorted, float v) {
    if (sorted.empty()) return 0.0;
    const double lo = static_cast<double>(v) - kSupportBand;
    const double hi = static_cast<double>(v) + kSupportBand;
    return percentileOf(sorted, static_cast<float>(hi)) -
           percentileOf(sorted, static_cast<float>(std::nextafter(lo, -1e30)));
}

int featureReport(const CliOpts& o) {
    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) return 1;
    const int shots = shotCountOf(corpus);
    std::printf("blss: labelling %zu frames to correlate features against the oracle\n",
                corpus.size());
    const std::vector<FrameLabels> labels = labelCorpus(corpus, o.sharpen, o.obj, o.threads);

    struct Acc {
        double n = 0, sum = 0, sq = 0, lo = 1e30, hi = -1e30, at0 = 0, at1 = 0;
    };
    std::vector<Acc> all(kFeatures);
    std::vector<std::vector<Acc>> byShot(static_cast<size_t>(shots),
                                         std::vector<Acc>(kFeatures));
    // Importance-weighted covariance of each feature with each oracle output.
    std::vector<std::array<double, kOutputs>> cov(kFeatures, std::array<double, kOutputs>{});
    // Every value of every channel, kept so --probe can place a console
    // measurement at a PERCENTILE rather than merely inside a min/max box: the
    // range says a value is representable, the percentile says whether the net
    // ever saw one.
    std::vector<std::vector<float>> raw(kFeatures);
    if (!o.probe.empty())
        for (int c = 0; c < kFeatures; ++c)
            raw[static_cast<size_t>(c)].reserve(corpus.size() * 256);
    std::vector<double> fMean(kFeatures, 0.0), fVar(kFeatures, 0.0);
    std::array<double, kOutputs> tMean{}, tVar{};
    double wSum = 0.0;

    for (size_t i = 0; i < corpus.size(); ++i)
        for (const Sample& s : labels[i]) {
            const double w = std::max(0.0f, s.importance);
            wSum += w;
            for (int c = 0; c < kFeatures; ++c) {
                const double v = s.f.v[c];
                Acc& a = all[static_cast<size_t>(c)];
                a.n += 1;
                a.sum += v;
                a.sq += v * v;
                a.lo = std::min(a.lo, v);
                a.hi = std::max(a.hi, v);
                a.at0 += v <= 1e-4 ? 1 : 0;
                a.at1 += v >= 1.0 - 1e-4 ? 1 : 0;
                if (!o.probe.empty()) raw[static_cast<size_t>(c)].push_back(s.f.v[c]);
                Acc& b = byShot[static_cast<size_t>(corpus[i].shot)][static_cast<size_t>(c)];
                b.n += 1;
                b.sum += v;
                b.sq += v * v;
                fMean[static_cast<size_t>(c)] += w * v;
            }
            for (int m = 0; m < kOutputs; ++m) tMean[static_cast<size_t>(m)] += w * s.target[m];
        }
    if (wSum <= 0.0) return 1;
    for (int c = 0; c < kFeatures; ++c) fMean[static_cast<size_t>(c)] /= wSum;
    for (int m = 0; m < kOutputs; ++m) tMean[static_cast<size_t>(m)] /= wSum;
    for (size_t i = 0; i < corpus.size(); ++i)
        for (const Sample& s : labels[i]) {
            const double w = std::max(0.0f, s.importance);
            for (int c = 0; c < kFeatures; ++c) {
                const double d = s.f.v[c] - fMean[static_cast<size_t>(c)];
                fVar[static_cast<size_t>(c)] += w * d * d;
                for (int m = 0; m < kOutputs; ++m)
                    cov[static_cast<size_t>(c)][static_cast<size_t>(m)] +=
                        w * d * (s.target[m] - tMean[static_cast<size_t>(m)]);
            }
            for (int m = 0; m < kOutputs; ++m) {
                const double d = s.target[m] - tMean[static_cast<size_t>(m)];
                tVar[static_cast<size_t>(m)] += w * d * d;
            }
        }

    std::printf("\n  input channels over %zu frames / %.0f tiles"
                " (corr = importance-weighted, vs the oracle's answer)\n",
                corpus.size(), all[0].n);
    std::printf("  %-11s %7s %7s %7s %7s %7s %7s  %7s %7s %7s\n", "feature", "mean", "sd", "min",
                "max", "%at 0", "%at 1", "r:point", "r:temp", "r:sharp");
    std::printf("  %s\n", std::string(13 + 6 * 8 + 3 * 8 + 2, '-').c_str());
    for (int c = 0; c < kFeatures; ++c) {
        const Acc& a = all[static_cast<size_t>(c)];
        const double mean = a.sum / a.n;
        const double sd = std::sqrt(std::max(0.0, a.sq / a.n - mean * mean));
        std::printf("  %-11s %7.3f %7.3f %7.3f %7.3f %6.1f%% %6.1f%%", kFeatureNames[c], mean, sd,
                    a.lo, a.hi, 100.0 * a.at0 / a.n, 100.0 * a.at1 / a.n);
        for (int m = 0; m < kOutputs; ++m) {
            const double den = std::sqrt(fVar[static_cast<size_t>(c)] * tVar[static_cast<size_t>(m)]);
            std::printf(" %+7.3f",
                        den > 0 ? cov[static_cast<size_t>(c)][static_cast<size_t>(m)] / den : 0.0);
        }
        std::printf("\n");
    }

    std::printf("\n  per-shot mean of each channel - a channel that is the same number in every"
                " column\n  cannot tell the shots apart, which is what it exists to do\n");
    std::printf("  %-11s", "feature");
    for (int s = 0; s < shots; ++s) std::printf(" %8s", shotNameOf(corpus, s).substr(0, 8).c_str());
    std::printf("   spread\n  %s\n", std::string(13 + shots * 9 + 9, '-').c_str());
    for (int c = 0; c < kFeatures; ++c) {
        std::printf("  %-11s", kFeatureNames[c]);
        std::vector<double> means;
        for (int s = 0; s < shots; ++s) {
            const Acc& b = byShot[static_cast<size_t>(s)][static_cast<size_t>(c)];
            const double m = b.n ? b.sum / b.n : 0.0;
            means.push_back(m);
            std::printf(" %8.3f", m);
        }
        std::printf("   %6.3f\n", spreadOf(means).sd);
    }
    std::printf("\n");

    if (!o.probe.empty()) {
        int unknown = 0;
        const std::array<ProbeChannel, kFeatures> probe = parseProbe(o.probe, &unknown);
        for (int c = 0; c < kFeatures; ++c)
            std::sort(raw[static_cast<size_t>(c)].begin(), raw[static_cast<size_t>(c)].end());

        std::printf("  A MEASURED CONSOLE VECTOR, PLACED IN THE CORPUS ABOVE.\n"
                    "  Paste the engine's own `BLSSFEAT` line (BLSS debug view 2, in the game's\n"
                    "  bin/log.txt) - it prints min/mean/max per channel over the tile grid.\n"
                    "  `spread` is that band; a channel with no spread is the same number in every\n"
                    "  tile of the frame, which is a network making no per-tile decision at all.\n"
                    "  `pct` is where the console's MEAN falls in the corpus; `supp` is how much of\n"
                    "  the corpus lies within +-0.05 of it, which is what decides whether the net\n"
                    "  interpolates or extrapolates there; `band` is how much of the corpus lies\n"
                    "  inside the console's OWN min..max - i.e. how much of what was taught this\n"
                    "  frame is even able to reach.\n\n");
        std::printf("  %-11s %21s %8s %17s %7s %7s %7s  %s\n", "feature", "console min/mean/max",
                    "spread", "corpus min..max", "pct", "supp", "band", "verdict");
        std::printf("  %s\n", std::string(13 + 22 + 9 + 18 + 8 * 3 + 26, '-').c_str());
        int outOfRange = 0, constants = 0, tails = 0, missing = 0;
        for (int c = 0; c < kFeatures; ++c) {
            const ProbeChannel& p = probe[static_cast<size_t>(c)];
            const std::vector<float>& v = raw[static_cast<size_t>(c)];
            std::printf("  %-11s", kFeatureNames[c]);
            if (!p.given) {
                std::printf(" %21s %8s %17s %7s %7s %7s  %s\n", "-", "-", "-", "-", "-", "-",
                            "not in the probe");
                ++missing;
                continue;
            }
            const double lo = v.empty() ? 0.0 : v.front();
            const double hi = v.empty() ? 0.0 : v.back();
            const double pct = percentileOf(v, p.mid);
            const double supp = supportOf(v, p.mid);
            const double band = percentileOf(v, p.hi) - percentileOf(v, p.lo);
            const bool isConst = (p.hi - p.lo) <= 1e-4f;
            const bool below = static_cast<double>(p.mid) < lo - 1e-4;
            const bool above = static_cast<double>(p.mid) > hi + 1e-4;
            // 1% of the tiles is 1% of the gradient: below that the net has
            // essentially not been taught this value and is extrapolating.
            const bool tail = !below && !above && supp < 1.0;
            std::string verdict;
            if (below || above) {
                verdict = above ? "OUT OF RANGE (above corpus)" : "OUT OF RANGE (below corpus)";
                ++outOfRange;
            } else if (tail) {
                verdict = "no support - net extrapolates";
                ++tails;
            } else {
                verdict = "in distribution";
            }
            if (isConst) {
                verdict = "CONSTANT; " + verdict;
                ++constants;
            }
            char band3[32];
            std::snprintf(band3, sizeof(band3), "%.3f/%.3f/%.3f", static_cast<double>(p.lo),
                          static_cast<double>(p.mid), static_cast<double>(p.hi));
            char range[32];
            std::snprintf(range, sizeof(range), "%.3f..%.3f", lo, hi);
            std::printf(" %21s %8.3f %17s %6.1f%% %6.1f%% %6.1f%%  %s\n", band3,
                        static_cast<double>(p.hi - p.lo), range, pct, supp, band,
                        verdict.c_str());
        }
        std::printf("\n  %d channel(s) out of the corpus' range, %d constant across the frame,"
                    " %d with <1%% support,\n  %d absent from the probe",
                    outOfRange, constants, tails, missing);
        if (unknown > 0) std::printf(", %d unrecognised key(s) ignored", unknown);
        std::printf(".\n");
        if (outOfRange == 0 && constants == 0 && tails == 0 && missing == 0)
            std::printf("  Every channel the console produced is inside what the corpus taught.\n");
        std::printf("\n");
    }
    return 0;
}

}  // namespace

int trainMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    applySweepKnobs(o);
    // --still is a fixture for the period-2 metric, not a corpus: every frame of
    // a shot is the SAME frame, so a fit would see `shotCount` distinct examples
    // repeated `frames/shotCount` times and report a training loss that means
    // nothing. Refused rather than warned - a net written out of this run would
    // be indistinguishable from a real one on disk.
    if (o.still) {
        std::printf(
            "blss: --still is a MEASUREMENT fixture and cannot be trained on - every "
            "frame of a shot is one frozen pose. Use it with --blss-eval and read the "
            "period-2 table.\n");
        return 1;
    }
    const std::string outPath = o.outPath.empty() ? o.netPath : o.outPath;
    // Cheap, and it fails here rather than three commands later inside a Docker
    // PS2 build: a net whose weights cannot be spelled as C++ literals is not a
    // trained net, it is a broken generated project.
    std::string emitErr;
    if (!selfTestEmitter(&emitErr)) {
        std::printf("blss: %s - the generated header would not compile\n", emitErr.c_str());
        return 1;
    }

    // WHERE THE MINUTES GO, printed rather than guessed. Two of the three
    // phases below are threaded and one is not, so which of them dominates
    // moves with the frame count, the epoch count and the core count - and
    // "the oracle is nearly all of it" was true of this tool exactly once.
    const auto tStart = std::chrono::steady_clock::now();
    const auto since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    };

    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) {
        std::printf("blss: the corpus generator produced no frames\n");
        return 1;
    }
    const double tCorpus = since(tStart);
    const int shots = shotCountOf(corpus);

    // By default the split's held-out shots are kept OUT, so that a later
    // `--blss-eval -i` on the same corpus is measuring content this net has not
    // seen. That costs the shipped net real quality - it is fitted on 9 of the
    // 13 shots, and cross-validation measured every extra shot as worth a
    // fraction of a decibel (leave-one-out +0.31 dB against leave-two-out +0.10
    // on identical content). `--all-shots` fits the whole corpus, which is what
    // you want for the net you actually bake into a game; the price is that
    // --blss-eval's held-out columns then mean nothing, and --cv is the only
    // honest measurement left.
    const ShotMask trained =
        o.allShots ? ShotMask(static_cast<size_t>(shots), 1) : complementMask(heldOutMask(shots));
    int trainShots = 0;
    for (char c : trained) trainShots += c ? 1 : 0;

    std::printf("blss: labelling %zu frames with the oracle\n", corpus.size());
    const auto tLabel0 = std::chrono::steady_clock::now();
    const std::vector<FrameLabels> labels =
        labelCorpus(corpus, o.sharpen, o.obj, o.threads, &trained);
    std::vector<Sample> samples = gatherSamples(labels, corpus, trained);
    if (samples.empty()) {
        std::printf("blss: no training samples (every shot held out?)\n");
        return 1;
    }
    const double tLabel = since(tLabel0);

    std::printf("blss: training on %zu tiles from %d shots (objective: flicker %.3f, fill %.2f)\n",
                samples.size(), trainShots, o.obj.flicker, o.obj.fill);
    const auto tFit0 = std::chrono::steady_clock::now();
    const TrainConfig tc = trainConfigOf(o);
    Net net;
    const float loss = train(net, samples, tc);
    const double tFit = since(tFit0);

    std::string err;
    if (!save(net, outPath, &err)) {
        std::printf("blss: %s\n", err.c_str());
        return 1;
    }
    // THE SIDECAR IS WRITTEN BY THE TRAINER, NOT BY WHOEVER RAN IT. A net's
    // corpus, sampler and raster scale are known here and nowhere else, and the
    // editor window's own `blss.net.args` file only exists when the window was
    // the thing that trained - a net from a shell had no provenance at all. See
    // the Provenance comment in blss.hpp for why this is a sidecar.
    Provenance prov = currentProvenance();
    prov.scale = o.scale;
    prov.jitter = jitterEnabled() ? 1 : 0;
    prov.sharpen = o.sharpen;
    prov.frames = static_cast<int>(corpus.size());
    prov.shots = shots;
    prov.epochs = o.epochs;
    prov.seed = o.seed;
    prov.corpus = o.projectDirs.empty() ? std::string("bestiary") : joinArgs(o.projectDirs);
    prov.command = commandLine(argc, argv);
    std::string provErr;
    if (!writeProvenance(prov, outPath, &provErr))
        std::printf("blss: WARNING - %s (the net has no provenance)\n", provErr.c_str());
    std::printf("blss: final loss %.6f, wrote %s (+ %s)\n", loss,
                displayPath(outPath).c_str(),
                displayPath(provenancePath(outPath)).c_str());
    // The three phases, named, so the next person to make this faster optimises
    // the one that is actually slow. Corpus and labelling are threaded; the fit
    // is sequential SGD (each Adam step reads the weights the previous one
    // wrote) and is the only phase `--threads` cannot touch.
    std::printf("blss: timing - corpus %.1f s, oracle %.1f s, fit %.1f s (%.1f s total)\n",
                tCorpus, tLabel, tFit, since(tStart));
    return 0;
}

int evalMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    applySweepKnobs(o);
    // --cv trains a net per fold, so it inherits --still's refusal for the same
    // reason --blss-train does: a fold whose training side is a handful of
    // frozen poses repeated is not a fold. The period-2 table --still exists for
    // is printed by the plain evaluation below.
    if (o.cv && o.still) {
        std::printf(
            "blss: --still cannot be combined with --cv - every fold would train on "
            "frozen poses. Drop --cv and read the period-2 table.\n");
        return 1;
    }
    // Two diagnostics that train their OWN nets and therefore ignore -i: the
    // cross-validation table (which is the honest answer to "does this
    // generalise", the single split being one draw) and the input-channel
    // report (which is the honest answer to "can it, with these features").
    if (o.cv) return crossValidate(o);
    if (o.featureStats) return featureReport(o);

    // NET-FREE EVALUATION, AND IT IS THE FIRST THING THIS TOOL SHOULD BE ABLE TO
    // DO. "Will this scene benefit at all" is answered by the ORACLE row - the
    // best any per-tile weighting can reach under the exact GS composite - and
    // no part of that number involves a trained network. The settings panel has
    // told users to "run the Evaluate tab on your project BEFORE turning this
    // on" since the day it shipped, and until now that was impossible: this
    // function loaded blss.net first and bailed, so a fresh project could not
    // perform its own documented first step.
    //
    // So: an explicit `-i` that cannot be opened is still an error (you asked
    // for that net, it is not there). A DEFAULT blss.net that is not there is
    // not - the table simply omits the trained row and everything else, verdict
    // included, is unchanged.
    // ...and since a default net SHIPS, "no -i" no longer means "no net": it
    // means the net this project would be BUILT with, which is the project's own
    // `blss.net` if it has one and the editor's built-in default if it does not
    // (the same order templates.cpp bakes in). That is the whole point of the
    // Evaluate tab - it must measure what the game will run.
    Net net;
    std::string err;
    const NetSource src = resolveNet(o, net, &err);
    if (src == NetSource::None && o.netGiven) {
        std::printf("blss: %s\n  (run --blss-train first)\n", err.c_str());
        return 1;
    }
    const bool haveNet = src != NetSource::None;
    if (!haveNet)
        std::printf("blss: no network available (%s) - evaluating NET-FREE. The oracle row is"
                    " the scene's own ceiling and needs no network.\n",
                    err.c_str());
    else
        announceNet(src, o);

    const auto tStart = std::chrono::steady_clock::now();
    const auto since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    };
    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) return 1;
    const double tCorpus = since(tStart);
    const auto tEval0 = std::chrono::steady_clock::now();
    const int shots = shotCountOf(corpus);
    if (!o.dumpDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(o.dumpDir, ec);
    }

    struct Row {
        const char* name;
        Method m;
        const char* dump;
    };
    std::vector<Row> rows = {
        {"point", fixedMethod(Kernel::Point), "point"},
        {"bilinear", fixedMethod(Kernel::Bilinear), "bilinear"},
        {"temporal", fixedMethod(Kernel::Temporal), "temporal"},
        {"sharpen", fixedMethod(Kernel::Sharpen), "sharpen"},
    };
    if (haveNet) rows.push_back({"BLSS (trained)", netMethod(net, o.sharpen, o.deadzone), "net"});
    rows.push_back({"oracle (upper bound)",
                    [&o](const Frame& fr, const Image& truth, size_t) {
                        return oracle(fr, truth, o.sharpen, nullptr, o.obj);
                    },
                    "oracle"});

    // Native = the scene rendered at full resolution, one sample. The honest
    // "no BLSS" reference, and NOT a ceiling: the ground truth is supersampled,
    // so a good temporal reconstruction can beat a 1-sample native render.
    const auto nativeOf = [&](bool heldOut, std::vector<double>* perShot, double* flicker,
                              Period2* alternation = nullptr) {
        double sum = 0.0;
        int n = 0;
        std::vector<double> ss(shots, 0.0);
        std::vector<int> sn(shots, 0);
        double flickSum = 0.0;
        int flickN = 0;
        // THE FLOOR FOR THE PERIOD-2 COLUMN, and the row that makes the rest of
        // it readable. `native` is a full-resolution, ONE-SAMPLE, UNJITTERED
        // render, so it has no alternation to have: whatever this measures is
        // the metric's own residual - bilinear resampling error in the two warps
        // plus the real curvature of the camera move. Every other row is only
        // bobbing to the extent that it sits above this.
        Period2 altAcc;
        int prevShot = -1;
        const CorpusFrame* prev1 = nullptr;
        const CorpusFrame* prev2 = nullptr;
        for (const CorpusFrame& cf : corpus) {
            if (isHeldOut(cf.shot, shots) != heldOut) continue;
            const Image* prevNative = prev1 ? &prev1->native : nullptr;
            if (alternation && prev1 && prev2 && cf.shot == prevShot && prev2->shot == prevShot)
                period2Alternation(cf.native, prev1->native, prev2->native, cf.frame.reproj,
                                   prev1->frame.reproj, cf.frame.outW, cf.frame.outH, altAcc);
            if (prevNative && cf.shot == prevShot && prevNative->w == cf.native.w) {
                double d = 0.0;
                for (int y = 0; y < cf.native.h; ++y)
                    for (int x = 0; x < cf.native.w; ++x) {
                        const uint8_t* a = cf.native.at(x, y);
                        const uint8_t* b = prevNative->at(x, y);
                        for (int c = 0; c < 3; ++c)
                            d += std::fabs(static_cast<double>(a[c]) - b[c]);
                    }
                flickSum += d / (static_cast<double>(cf.native.w) * cf.native.h * 3.0);
                ++flickN;
            }
            prev2 = prev1;
            prev1 = &cf;
            prevShot = cf.shot;
            const double p = psnr(cf.native, cf.truth);
            sum += p;
            ++n;
            ss[cf.shot] += p;
            ++sn[cf.shot];
            if (!o.dumpDir.empty() && heldOut && n == 1) {
                writePng(cf.native, o.dumpDir + "/blss-native.png");
                writePng(cf.truth, o.dumpDir + "/blss-truth.png");
            }
        }
        if (perShot) {
            perShot->assign(shots, 0.0);
            for (int s = 0; s < shots; ++s) (*perShot)[s] = sn[s] ? ss[s] / sn[s] : 0.0;
        }
        if (flicker) *flicker = flickN ? flickSum / flickN : 0.0;
        if (alternation) *alternation = altAcc;
        return n ? sum / n : 0.0;
    };

    // What the verdict below is computed from. Frame-weighted over BOTH splits,
    // because the two partition one corpus and neither half on its own is an
    // answer about the scene - the same arithmetic as blssui::summarise(), which
    // the window has been doing on the parsed table.
    struct SplitSum {
        int frames = 0;
        double native = 0, bilinear = 0, net = 0, oracle = 0;
        double netPasses = 0, oraclePasses = 0;
        bool haveNet = false, haveOracle = false;
    };
    std::vector<SplitSum> splitSums;

    const ShotMask held = heldOutMask(shots), trained = complementMask(held);
    for (int pass = 0; pass < 2; ++pass) {
        const bool heldOut = pass == 0;
        const ShotMask& want = heldOut ? held : trained;
        int frames = 0;
        std::vector<int> ids;
        for (const CorpusFrame& cf : corpus)
            if (inMask(want, cf.shot) ) {
                ++frames;
                if (std::find(ids.begin(), ids.end(), cf.shot) == ids.end()) ids.push_back(cf.shot);
            }
        if (!frames) continue;

        std::printf("\n  %s shots %s- %d frame(s), PSNR vs supersampled truth"
                    " (oracle objective: flicker %.3f, fill %.2f;"
                    " %d hidden, deadzone %.1f)\n",
                    heldOut ? "HELD-OUT" : "training", "", frames, o.obj.flicker, o.obj.fill,
                    kHidden, o.deadzone);
        // point/temp/sharp are the fraction of grid CELLS each pass draws, and
        // `passes` the mean full-screen passes per frame including the base -
        // 1.00 is plain bilinear, 5.00 is every kernel everywhere.
        std::printf("  %-22s %9s %8s %7s %7s %7s %7s ", "", "overall", "flicker", "point",
                    "temp", "sharp", "passes");
        for (int s : ids) std::printf("  shot%-2d", s);
        std::printf("\n  %s\n", std::string(24 + 19 + 32 + ids.size() * 8, '-').c_str());

        std::vector<double> ps;
        double fl = 0.0;
        // Collected next to the table and printed UNDER it rather than as a
        // column: the window parses these rows by position (blss_ui.cpp), and a
        // new column in the middle of them is a silently misread table.
        std::vector<std::pair<std::string, Period2>> altRows;
        Period2 natAlt;
        const double nat = nativeOf(heldOut, &ps, &fl, &natAlt);
        std::printf("  %-22s %8.3f %8.2f %7s %7s %7s %7s  ", "native full-res", nat, fl, "-", "-",
                    "-", "-");
        for (int s : ids) std::printf("%8.3f", ps[s]);
        std::printf("\n");

        SplitSum sum;
        sum.frames = frames;
        sum.native = nat;
        for (const Row& r : rows) {
            Occupancy occ;
            Period2 alt;
            const double p = evalRecurrent(corpus, shots, want, o.sharpen, r.m, o.dumpDir,
                                           heldOut ? r.dump : nullptr, &ps, &fl, &occ, o.threads,
                                           &alt);
            altRows.emplace_back(r.name, alt);
            std::printf("  half-res + %-11s %8.3f %8.2f %6.1f%% %6.1f%% %6.1f%% %7.2f  ", r.name, p,
                        fl, occ.point * 100.0f, occ.temporal * 100.0f, occ.sharpen * 100.0f,
                        occ.passes);
            for (int s : ids) std::printf("%8.3f", ps[s]);
            std::printf("%s\n", std::strcmp(r.name, "BLSS (trained)") == 0 ? "   <-- the network"
                                                                           : "");
            if (std::strcmp(r.name, "bilinear") == 0) sum.bilinear = p;
            if (std::strcmp(r.name, "BLSS (trained)") == 0) {
                sum.net = p;
                sum.netPasses = occ.passes;
                sum.haveNet = true;
            }
            if (std::strcmp(r.name, "oracle (upper bound)") == 0) {
                sum.oracle = p;
                sum.oraclePasses = occ.passes;
                sum.haveOracle = true;
            }
        }
        splitSums.push_back(sum);

        // --- PERIOD-2 ALTERNATION. The `flicker` column above is a lag-1
        // difference and is structurally unable to tell a picture alternating
        // between two images from a picture panning smoothly; this one is the
        // motion-compensated second difference, which is zero for anything
        // moving at a constant rate and four times the amplitude for a bob.
        // See period2Alternation().
        //
        // EVERY COLUMN IS A DIFFERENT WARP GATE, and printing the whole sweep is
        // the point rather than clutter: motion compensation has its own error,
        // that error is what the `native` row measures, and at the loosest gate
        // it is LARGER THAN THE ARTEFACT. A single gated number would be a magic
        // constant; the sweep shows the reader where it stops mattering.
        std::printf("\n  period-2 alternation, %s split - motion-compensated"
                    " |O(t) - 2*O(t-1) + O(t-2)| / 4, in 8-bit levels\n",
                    heldOut ? "held-out" : "training");
        std::printf("  a pixel counts when BOTH of its warps are under the column's length;"
                    " `any` is ungated\n");
        std::printf("  %-22s", "");
        for (int g = 0; g < kP2Gates; ++g) {
            char h[16];
            if (g + 1 == kP2Gates) std::snprintf(h, sizeof h, "any");
            else std::snprintf(h, sizeof h, "<=%gpx", static_cast<double>(kP2Gate[g]));
            std::printf(" %9s", h);
        }
        std::printf("\n  %s\n", std::string(24 + 10 * kP2Gates, '-').c_str());
        std::printf("  %-22s", "native full-res");
        for (int g = 0; g < kP2Gates; ++g) std::printf(" %9.3f", natAlt.mean(g));
        std::printf("   <-- the floor: no jitter, one sample, so this is the metric's"
                    " own residual\n");
        for (const auto& ar : altRows) {
            std::printf("  half-res + %-11s", ar.first.c_str());
            for (int g = 0; g < kP2Gates; ++g) std::printf(" %9.3f", ar.second.mean(g));
            std::printf("\n");
        }
        std::printf("  %s\n", std::string(24 + 10 * kP2Gates, '-').c_str());
        std::printf("  %-22s", "gate keeps, % of frame");
        for (int g = 0; g < kP2Gates; ++g)
            std::printf(" %8.1f%%", 100.0 * natAlt.coverage(g));
        std::printf("\n  jitter is %s\n", jitterEnabled() ? "ON" : "OFF");
    }

    // ------------------------------------------------------------- the verdict
    // WILL THIS SCENE BENEFIT AT ALL - the answer this table has always
    // contained and always buried in its last row. The ORACLE row is the scene's
    // own ceiling: no network can beat the best per-tile weighting under the
    // exact GS composite, so a ceiling near zero is a fact about the content that
    // no amount of training moves.
    {
        double w = 0, bil = 0, netP = 0, orc = 0, nat = 0, netPas = 0, orcPas = 0;
        int frames = 0;
        bool anyNet = false, anyOracle = false;
        for (const SplitSum& s : splitSums) {
            if (!s.haveOracle) continue;
            const double sw = s.frames > 0 ? static_cast<double>(s.frames) : 1.0;
            bil += sw * s.bilinear;
            orc += sw * s.oracle;
            nat += sw * s.native;
            orcPas += sw * s.oraclePasses;
            if (s.haveNet) {
                netP += sw * s.net;
                netPas += sw * s.netPasses;
                anyNet = true;
            }
            anyOracle = true;
            frames += s.frames;
            w += sw;
        }
        if (anyOracle && w > 0.0) {
            bil /= w;
            orc /= w;
            nat /= w;
            orcPas /= w;
            netP /= w;
            netPas /= w;
            const double ceiling = orc - bil, margin = netP - bil;
            // Matches blssui's kNoHeadroomDb, and the wording matches the
            // window's three branches, because there must be exactly one answer
            // to "will this scene benefit" however you asked the question.
            constexpr double kNoHeadroomDb = 0.10;
            std::printf("\n  THE ANSWER - frame-weighted over both splits, %d frame(s)\n", frames);
            if (ceiling < kNoHeadroomDb && gCorpusEmitters > 0) {
                // THE ONE BRANCH THAT MUST NOT BE CONFIDENT. "Nothing to
                // reconstruct" is a claim about the CONTENT, and the content
                // this corpus rendered is the scene with its particles removed -
                // which on `examples/showcase` is 4.4 % of the fill the game
                // pays for (--blss-coverage: 0.67 geometry coverages against
                // 14.57 emitter). A near-zero ceiling measured on the 4 % that
                // was drawn says nothing about the 96 % that was not, and this
                // is the exact sentence the BLSS window quotes verbatim.
                std::printf("  NO VERDICT: this scene is %d enabled emitter(s) the corpus did not"
                            " draw.\n", gCorpusEmitters);
                std::printf("  The oracle reaches %+.2f dB over plain bilinear at %.2f passes ON"
                            " THE GEOMETRY ALONE,\n  which would read as \"nothing to"
                            " reconstruct\" - but the particles are missing from the\n  ground"
                            " truth, not from the game. Run --blss-coverage here: if the emitter"
                            " half of\n  that count dominates, this number is a measurement of the"
                            " part of the frame that\n  happens to be geometry."
                            " See docs/backlog.md.\n",
                            ceiling, orcPas);
            } else if (ceiling < kNoHeadroomDb) {
                std::printf("  THIS SCENE WILL NOT BENEFIT. Leave the upscaler off.\n");
                std::printf("  The ORACLE - the best any per-tile weighting can do under the exact"
                            " GS composite,\n  which no network can beat - scores %+.2f dB over"
                            " plain bilinear here, at %.2f passes.\n"
                            "  There is nothing to reconstruct: a half-resolution render of this"
                            " content, blown up\n  bilinearly, is already as close to the"
                            " supersampled truth as the composite can get.\n"
                            "  That is a fact about the scene, and no amount of training moves"
                            " it.\n",
                            ceiling, orcPas);
            } else if (anyNet && margin < 0.0) {
                std::printf("  THE NETWORK YOU HAVE IS WORSE THAN NOT USING IT: %+.2f dB.\n",
                            margin);
                std::printf("  It costs %.2f passes to score below the one pass plain bilinear"
                            " costs. The scene\n  itself has room - the oracle reaches %+.2f dB -"
                            " so this is the NET, not the content.\n",
                            netPas, ceiling);
            } else if (anyNet) {
                std::printf("  %+.2f dB over plain bilinear, at %.2f passes.\n", margin, netPas);
                std::printf("  The scene's own ceiling is %+.2f dB at %.2f passes (the oracle), so"
                            " the network has\n  captured %.0f%% of what is there to capture."
                            " 1.00 passes IS plain bilinear and 5.00\n  is every kernel everywhere,"
                            " so read the two together.\n",
                            ceiling, orcPas, ceiling > 0.0 ? 100.0 * margin / ceiling : 0.0);
            } else {
                std::printf("  THIS SCENE HAS ROOM: the oracle reaches %+.2f dB over plain"
                            " bilinear, at %.2f passes.\n", ceiling, orcPas);
                std::printf("  That is the CEILING, not a promise - it is what the best per-tile"
                            " weighting can do\n  under the exact GS composite. Train a net on this"
                            " corpus and re-run with -i to find\n  out how much of it a network"
                            " actually captures.\n");
            }
            // ONE LINE, ALWAYS, PARSED WITHOUT HEURISTICS. Every field exists
            // net-free, which is the point: headroom is the oracle's margin over
            // bilinear and `passes` is what the oracle pays for it, so a caller
            // can decide whether to bother training before any net exists.
            //
            // `emitters` is APPENDED, and that is the compatible way to do it:
            // blssui::parseEval reads this line key=value and ignores keys it
            // does not know (src/blss_ui.cpp), unlike the TABLES above it, which
            // are read by column position and must never gain a column in the
            // middle. It is the machine-readable half of the "NO VERDICT" branch
            // - a caller that only reads this line still learns that `headroom`
            // was measured on a frame missing N emitters' worth of fill.
            std::printf("[blss] verdict headroom=%+.3f passes=%.2f bilinear=%.3f oracle=%.3f"
                        " native=%.3f emitters=%d\n",
                        ceiling, orcPas, bil, orc, nat, gCorpusEmitters);
        }
    }

    // What the network actually decided, as a picture: one pixel per tile,
    // R = point, G = temporal, B = sharpen. Net-free there is no decision to
    // draw, and the Compare tab simply has one image fewer.
    if (!o.dumpDir.empty() && haveNet) {
        for (const CorpusFrame& cf : corpus)
            if (isHeldOut(cf.shot, shots)) {
                const WeightField nf = netField(net, cf.frame, o.sharpen, o.deadzone);
                Image vis(cf.frame.cols, cf.frame.rows);
                for (int cy = 0; cy < cf.frame.rows; ++cy)
                    for (int cx = 0; cx < cf.frame.cols; ++cx) {
                        const auto& w = nf.at(cx, cy);
                        uint8_t* d = vis.at(cx, cy);
                        for (int m = 0; m < kOutputs; ++m)
                            d[m] = static_cast<uint8_t>(clamp01(w[m]) * 255.0f);
                        d[3] = 255;
                    }
                writePng(vis, o.dumpDir + "/blss-net-weights.png");
                break;
            }
    }
    if (!o.dumpDir.empty()) std::printf("\n  wrote comparison PNGs to %s\n", o.dumpDir.c_str());
    // The two phases, named, the way --blss-train reports its three. It is how
    // anyone checks that the evaluation loop got faster, and which half to look
    // at when it did not: the corpus render is threaded, and so, now, is the
    // per-method evaluation (over shot runs - see evalRecurrent).
    const double tEval = since(tEval0), tTotal = since(tStart);
    std::printf("blss: timing - corpus %.1f s, eval %.1f s (%.1f s total)\n", tCorpus, tEval,
                tTotal);
    std::printf("\n");
    return 0;
}

int emitMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    applySweepKnobs(o);
    std::string err;
    if (!selfTestEmitter(&err)) {
        std::printf("blss: %s - the generated header would not compile\n", err.c_str());
        return 1;
    }
    // `--blss-emit --act-table N` emits the ACTIVATION TABLE instead of the
    // net, because the table is the half of the contract the engine has to
    // carry as literals. Generating it from a formula on both sides is what the
    // math doc specifies and the hash is how that gets checked - but if two
    // libms ever disagree about one entry's rounding, this is the escape hatch
    // that ends the argument: paste these numbers.
    if (o.actTable > 0) {
        const std::string tab = emitActTable();
        if (o.outPath.empty()) {
            std::fputs(tab.c_str(), stdout);
            return 0;
        }
        FILE* tf = std::fopen(o.outPath.c_str(), "wb");
        if (!tf) {
            std::printf("blss: cannot write %s\n", o.outPath.c_str());
            return 1;
        }
        std::fwrite(tab.data(), 1, tab.size(), tf);
        std::fclose(tf);
        std::printf("blss: wrote %s (activation table, hash 0x%08X)\n",
                    displayPath(o.outPath).c_str(), actTableHash());
        return 0;
    }

    // Same fallback order as --blss-eval and as the bake. It also makes this
    // verb the cheapest check that the SHIPPED default is still readable by this
    // build: `--blss-emit -o <tmp>` in a directory with no blss.net loads the
    // embedded net, runs its provenance against the compiled-in topology and
    // fails loudly if kNetVersion moved without resources/blss-default.net being
    // refitted. That is the CI gate for the default (docs/neural-upscaler.md).
    Net net;
    const NetSource from = resolveNet(o, net, &err);
    if (from == NetSource::None) {
        std::printf("blss: %s\n", err.c_str());
        return 1;
    }
    announceNet(from, o);
    const std::string src = emitGeneratedSource(net);
    if (o.outPath.empty()) {
        std::fputs(src.c_str(), stdout);
        return 0;
    }
    FILE* f = std::fopen(o.outPath.c_str(), "wb");
    if (!f) {
        std::printf("blss: cannot write %s\n", o.outPath.c_str());
        return 1;
    }
    std::fwrite(src.data(), 1, src.size(), f);
    std::fclose(f);
    std::printf("blss: wrote %s\n", displayPath(o.outPath).c_str());
    return 0;
}

}  // namespace blss
