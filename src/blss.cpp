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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include <stb_image.h>
#include <stb_image_write.h>

#include "blsscorpus.hpp"

namespace blss {

const char* const kFeatureNames[kFeatures] = {
    "motion", "depth", "depthGrad", "edgeDens",
    "texDetail", "coverage", "luma", "histAge"};
const char* const kOutputNames[kOutputs] = {"point", "temporal", "sharpen"};

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
    const float gx = px / kTile, gy = py / kTile;
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
    const float gx = cx / kTile, gy = cy / kTile;
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
        lumaAcc(stats.size(), 0.0f), detAcc(stats.size(), 0.0f),
        edgeAcc(stats.size(), 0.0f), dmin(stats.size(), 1e30f), dmax(stats.size(), 0.0f);

    for (const BagProxy& b : bags) {
        if (b.x1 <= b.x0 || b.y1 <= b.y0) continue;
        const float invNear = 1.0f / std::max(b.wNear, 1e-4f);
        const float invFar = 1.0f / std::max(b.wFar, 1e-4f);
        const int cx0 = clampi(static_cast<int>(std::floor(b.x0 / kTile)), 0, cols - 1);
        const int cx1 = clampi(static_cast<int>(std::floor((b.x1 - 1e-3f) / kTile)), 0, cols - 1);
        const int cy0 = clampi(static_cast<int>(std::floor(b.y0 / kTile)), 0, rows - 1);
        const int cy1 = clampi(static_cast<int>(std::floor((b.y1 - 1e-3f) / kTile)), 0, rows - 1);
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx) {
                const float tx0 = static_cast<float>(cx * kTile), tx1 = tx0 + kTile;
                const float ty0 = static_cast<float>(cy * kTile), ty1 = ty0 + kTile;
                const float ox = overlap(b.x0, b.x1, tx0, tx1);
                const float oy = overlap(b.y0, b.y1, ty0, ty1);
                if (ox <= 0.0f || oy <= 0.0f) continue;
                const size_t k = static_cast<size_t>(cy) * cols + cx;
                const float a = (ox * oy) / (static_cast<float>(kTile) * kTile);
                coverAcc[k] += a;
                depthAcc[k] += a * invNear;
                lumaAcc[k] += a * b.luma;
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
            s.luma = lumaAcc[k] / cw;
            s.texDetail = detAcc[k] / cw;
            s.depthMin = dmin[k] > 1e29f ? 0.0f : dmin[k];
            s.depthMax = dmax[k];
        }
        s.edge = std::min(1.0f, edgeAcc[k] / (2.0f * kTile));
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

            const float px = static_cast<float>(std::min(i * kTile, outW));
            const float py = static_cast<float>(std::min(j * kTile, outH));
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
                                    const ReprojField& reproj,
                                    const std::vector<uint8_t>& histAge) {
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
                f.motion() = clamp01(std::sqrt(mdu * mdu + mdv * mdv) / kTile);
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
            f.luma() = clamp01(s.luma);
            f.histAge() =
                k < histAge.size() ? std::min(1.0f, static_cast<float>(histAge[k]) / 8.0f) : 0.0f;
        }
    return out;
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
        h[k] = std::tanh(a);
    }
    for (int m = 0; m < kOutputs; ++m) {
        float a = b2[m];
        for (int k = 0; k < kHidden; ++k) a += w2[m][k] * h[k];
        out[m] = 1.0f / (1.0f + std::exp(-a));
    }
}

namespace {
constexpr uint32_t kNetVersion = 1;
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

bool load(Net& n, const std::string& path, std::string* err) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    char magic[4] = {};
    uint32_t ver = 0;
    float flat[kNetFloats];
    bool ok = std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "BLSS", 4) == 0;
    ok = ok && std::fread(&ver, sizeof(ver), 1, f) == 1 && ver == kNetVersion;
    ok = ok && std::fread(flat, sizeof(float), kNetFloats, f) == kNetFloats;
    std::fclose(f);
    if (!ok) {
        if (err) *err = path + " is not a BLSS net of version " + std::to_string(kNetVersion);
        return false;
    }
    flatToNet(n, flat);
    return true;
}

namespace {

// ONE WEIGHT, AS A C++ FLOATING LITERAL THAT IS ACTUALLY ONE.
//
// `%.9g` renders 0.0f as "0" and 1.0f as "1", and "0F" is not a float literal -
// it is an integer followed by a user-defined-literal suffix that does not
// exist, so the generated header fails to compile with
// `unable to find numeric literal operator 'operator""F'`. Every bias starts at
// exactly 0 (Net::randomize), and templates.cpp emits a DEFAULT-CONSTRUCTED Net
// when a project has BLSS enabled and no blss.net - all 147 weights zero - so
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
    if (!literalProbesOk(&bad))
        s.insert(0, "#error BLSS emitter is producing invalid float literals ('" + bad + ")\n");
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
};

void gatherTaps(const Frame& fr, int x, int y, Taps& t) {
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
    if (fr.history) {
        sampleBilinear(*fr.history, x + 0.5f + du, y + 0.5f + dv, t.hist);
    } else {
        // No real history (training): stand in with the previous low-res render
        // upscaled, i.e. what the composite would have produced at all-zero
        // weights. Its own jitter phase is the other one.
        const float pjx = jitterX(1 - fr.phase), pjy = jitterY(1 - fr.phase);
        sampleBilinear(fr.prevLow, (x + 0.5f + du) / sx + pjx, (y + 0.5f + dv) / sy + pjy,
                       t.hist);
    }
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
    std::vector<Taps> taps(static_cast<size_t>(sw) * sh);
    for (int sy = 0; sy < sh; ++sy)
        for (int sx = 0; sx < sw; ++sx)
            gatherTaps(fr, sx * kStep, sy * kStep, taps[static_cast<size_t>(sy) * sw + sx]);

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

        const int x0 = cx0 * kTile, x1 = std::min((cx1 + 1) * kTile, fr.outW);
        const int y0 = cy0 * kTile, y1 = std::min((cy1 + 1) * kTile, fr.outH);
        double se = 0.0, seH = 0.0;
        // The flicker term ships at weight 0 (see kFlickerWeight); hoisting the
        // test keeps its samples out of the oracle's innermost loop.
        const bool wantFlicker = obj.flicker > 0.0f;
        long n = 0;
        for (int sy = (y0 + kStep - 1) / kStep; sy * kStep < y1; ++sy)
            for (int sx = (x0 + kStep - 1) / kStep; sx * kStep < x1; ++sx) {
                const int x = sx * kStep, y = sy * kStep;
                const float gx = (x + 0.5f) / kTile, gy = (y + 0.5f) / kTile;
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
                    // The flicker term: how far this pixel lands from where the
                    // PREVIOUS frame put the same content. `tp.hist` is the
                    // reprojected history the composite already sampled, so
                    // camera motion is not penalised - only instability is.
                    if (wantFlicker) {
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
    for (int sweep = 0; sweep < 2; ++sweep)
        for (int cy = 0; cy < rows; ++cy)
            for (int cx = 0; cx < cols; ++cx) {
                const int cx0 = std::max(0, cx - 1), cy0 = std::max(0, cy - 1);
                const int cx1 = std::min(cols - 1, cx + 1), cy1 = std::min(rows - 1, cy + 1);
                auto& t = wf.at(cx, cy);
                for (int m = 0; m < kOutputs; ++m) {
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
    // The eight channels do not remotely share a scale over the corpus
    // (--blss-eval --features prints the table): texDetail lives in 0.00 .. 0.20
    // with sd 0.14, luma never exceeds 0.48, coverage is 1.0 on seven tenths of
    // all tiles. One learning rate and one weight decay serve all of them, so the
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
                    h[k] = std::tanh(a);
                }
                for (int mo = 0; mo < kOutputs; ++mo) {
                    float a = net.b2[mo];
                    for (int k = 0; k < kHidden; ++k) a += net.w2[mo][k] * h[k];
                    o[mo] = 1.0f / (1.0f + std::exp(-a));
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
    std::string outPath;
    std::string dumpDir;
    std::string assetDir = "examples";
    // 12 frames per shot over the 13-shot bestiary. Frames are split evenly, so
    // this number and the shot count have to move together: at the old default of
    // 48 the tail shots got three frames each, and a shot with three frames
    // teaches the temporal channel almost nothing (its history is one frame deep
    // and the first frame of a shot has none at all).
    int frames = 156;
    int epochs = 400;
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
    // --features: what the eight input channels actually look like over the
    // corpus. A channel that is constant is a channel the net cannot use, and
    // nothing printed that until this flag existed.
    bool featureStats = false;
    // --deadzone A: the inference-time snap-to-zero, in GS alpha bytes
    // (kDeadzoneAlpha). Overridable for the same reason the objective's weights
    // are: it trades quality against fill, and the only honest way to set it is
    // to sweep it under --cv and read BOTH columns. 0 turns it off.
    float deadzone = kDeadzoneAlpha;
    // --deadzone-sweep a,b,c: every value in one --cv run. THE DEADZONE DOES
    // NOT TOUCH TRAINING - the labels never see it, it is applied to the net's
    // answer - so N deadzones over the same folds are N honest rows of one
    // sweep at the cost of N evaluations rather than N trainings. That is the
    // difference between a sweep that takes twenty minutes and one that takes
    // two hours, which is the difference between sweeping it and guessing.
    std::vector<float> deadzoneSweep;
};

CliOpts parseCli(int argc, char** argv) {
    CliOpts o;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](const char* def) -> std::string {
            return i + 1 < argc ? std::string(argv[++i]) : std::string(def);
        };
        if (a == "-o" || a == "--out") o.outPath = next("");
        else if (a == "-i" || a == "--net") o.netPath = next("blss.net");
        else if (a == "--dump") o.dumpDir = next(".");
        else if (a == "--assets") o.assetDir = next("examples");
        else if (a == "--frames") o.frames = std::atoi(next("156").c_str());
        else if (a == "--epochs") o.epochs = std::atoi(next("400").c_str());
        else if (a == "--sharpen") o.sharpen = static_cast<float>(std::atof(next("0.5").c_str()));
        else if (a == "--flicker-weight")
            o.obj.flicker = static_cast<float>(std::atof(next("0.15").c_str()));
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
        else if (a == "--cv-seeds") o.cvSeeds = std::max(1, std::atoi(next("1").c_str()));
        else if (a == "--cv-folds") o.cvFolds = std::max(0, std::atoi(next("0").c_str()));
        else if (a == "--weight-decay")
            o.weightDecay = static_cast<float>(std::atof(next("1e-5").c_str()));
        else if (a == "--standardise") o.standardise = true;
        else if (a == "--all-shots") o.allShots = true;
        else if (a == "--features") o.featureStats = true;
        else if (a == "--scale-1x2") o.scale = Scale::X1Y2;
        else std::printf("blss: ignoring unknown argument '%s'\n", a.c_str());
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
template <class F>
void parallelFor(int n, const F& body) {
    if (n <= 0) return;
    unsigned hw = std::thread::hardware_concurrency();
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

std::vector<CorpusFrame> buildCorpus(const CliOpts& o) {
    CorpusConfig cc;
    cc.frames = o.frames;
    cc.scale = o.scale;
    cc.seed = o.seed;
    cc.assetDir = o.assetDir;
    std::printf("blss: rendering %d corpus frames at %dx%d (this is the slow part)\n",
                cc.frames, cc.outW, cc.outH);
    return generate(cc);
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
double evalRecurrent(const std::vector<CorpusFrame>& corpus, int shotCount, const ShotMask& want,
                     float sharpen, const Method& weights, const std::string& dumpDir,
                     const char* dumpName, std::vector<double>* perShot,
                     double* flicker, Occupancy* occ = nullptr) {
    double sum = 0.0;
    int n = 0;
    double occA = 0.0, occC = 0.0, occD = 0.0;
    std::vector<double> shotSum(shotCount, 0.0);
    std::vector<int> shotN(shotCount, 0);
    Image prevOut, out;
    int prevShot = -1;
    double flickSum = 0.0;
    int flickN = 0;
    for (size_t idx = 0; idx < corpus.size(); ++idx) {
        const CorpusFrame& cf = corpus[idx];
        if (!inMask(want, cf.shot)) continue;
        Frame fr = cf.frame;
        fr.history = (cf.shot == prevShot && prevOut.w == fr.outW) ? &prevOut : nullptr;
        const WeightField wf = weights(fr, cf.truth, idx);
        // OCCUPANCY: how much of the screen this method's weights actually make
        // the GS draw. It belongs next to PSNR for the same reason flicker does
        // - the whole performance case for BLSS is that passes 2..5 cover a
        // minority of the frame, and until this was printed the network was
        // quietly asking for all of them everywhere.
        const Occupancy fo = occupancy(wf, sharpen);
        occA += fo.point;
        occC += fo.temporal;
        occD += fo.sharpen;
        composite(fr, wf, sharpen, out);
        const double p = psnr(out, cf.truth);
        sum += p;
        ++n;
        if (cf.shot < shotCount) {
            shotSum[cf.shot] += p;
            ++shotN[cf.shot];
        }
        // FLICKER: mean per-pixel change between consecutive frames of one
        // shot. This exists because per-frame PSNR is blind to temporal
        // instability - averaging two jitter phases is genuinely closer to the
        // truth than either, so a reconstruction that OSCILLATES between them
        // scores well and looks like bob deinterlacing on a television. That is
        // exactly how the 50% temporal cap shipped. Compare a method's flicker
        // against the `native` row, which is the honest floor for a given
        // camera move.
        if (cf.shot == prevShot && prevOut.w == out.w && prevOut.h == out.h) {
            double d = 0.0;
            for (int y = 0; y < out.h; ++y)
                for (int x = 0; x < out.w; ++x) {
                    const uint8_t* a = out.at(x, y);
                    const uint8_t* b = prevOut.at(x, y);
                    for (int c = 0; c < 3; ++c)
                        d += std::fabs(static_cast<double>(a[c]) - b[c]);
                }
            flickSum += d / (static_cast<double>(out.w) * out.h * 3.0);
            ++flickN;
        }
        prevOut = out;
        prevShot = cf.shot;
        if (!dumpDir.empty() && n == 1 && dumpName)
            writePng(out, dumpDir + "/blss-" + dumpName + ".png");
    }
    if (perShot) {
        perShot->assign(shotCount, 0.0);
        for (int s = 0; s < shotCount; ++s)
            (*perShot)[s] = shotN[s] ? shotSum[s] / shotN[s] : 0.0;
    }
    if (flicker) *flicker = flickN ? flickSum / flickN : 0.0;
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
                                     const Objective& obj, const ShotMask* want = nullptr) {
    std::vector<FrameLabels> labels(corpus.size());
    parallelFor(static_cast<int>(corpus.size()), [&](int i) {
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
    double blss = 0, bilinear = 0, oracleBound = 0, native = 0;  // held-out shot, dB
    double inBlss = 0, inBilinear = 0;                           // the trained-on shots
    double flicker = 0;
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
                                          const CliOpts& o, uint32_t seed) {
    const std::vector<FrameLabels> labels = labelCorpus(corpus, o.sharpen, o.obj);
    const int nFolds = o.cvFolds > 0 ? std::min(o.cvFolds, shots) : shots;
    std::vector<FoldResult> folds(static_cast<size_t>(nFolds));
    // Folds are independent, so they run in parallel; each writes only its own
    // slot and trains its own net from its own sample vector.
    parallelFor(nFolds, [&](int f) {
        FoldResult& r = folds[static_cast<size_t>(f)];
        r.shot = f;
        const ShotMask test = singleShotMask(shots, f);
        const ShotMask train = complementMask(test);

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
            r.dzBlss[d] = evalRecurrent(corpus, shots, test, o.sharpen,
                                        netMethod(net, o.sharpen, dz[d]), "", nullptr, nullptr,
                                        &dfl, &r.dzOcc[d]);
            if (d == 0) fl = dfl;
        }
        r.blss = r.dzBlss[0];
        r.occ = r.dzOcc[0];
        r.flicker = fl;
        r.bilinear = evalRecurrent(corpus, shots, test, o.sharpen, fixedMethod(Kernel::Bilinear),
                                   "", nullptr, nullptr, &fl);
        r.oracleBound = evalRecurrent(corpus, shots, test, o.sharpen, labelMethod(labels), "",
                                      nullptr, nullptr, &fl);
        r.native = nativePsnr(corpus, test);
        // The in-distribution control. If this collapses, the fold did not train
        // and its held-out number says nothing about generalisation.
        r.inBlss = evalRecurrent(corpus, shots, train, o.sharpen,
                                 netMethod(net, o.sharpen, o.deadzone), "", nullptr,
                                 nullptr, &fl);
        r.inBilinear = evalRecurrent(corpus, shots, train, o.sharpen,
                                     fixedMethod(Kernel::Bilinear), "", nullptr, nullptr, &fl);
    });
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
        "\nblss: leave-one-shot-out cross-validation, %d seed(s) x every shot held out in turn\n"
        "      %d frames, %d epochs, decay %.0e, %s inputs, %d hidden unit(s)\n"
        "      objective: flicker %.3f, fill %.2f, sharpen %.2f; inference deadzone %.1f alpha\n",
        o.cvSeeds, o.frames, o.epochs,
        static_cast<double>(trainConfigOf(o).weightDecay),
        o.standardise ? "standardised" : "raw", kHidden, o.obj.flicker, o.obj.fill, o.sharpen,
        o.deadzone);

    std::vector<std::vector<FoldResult>> all;  // [seed][fold]
    std::vector<uint32_t> seeds;
    std::vector<std::string> shotNames;
    int shots = 0;
    for (int si = 0; si < o.cvSeeds; ++si) {
        CliOpts so = o;
        so.seed = cvSeedAt(o.seed, si);
        seeds.push_back(so.seed);
        std::vector<CorpusFrame> corpus = buildCorpus(so);
        if (corpus.empty()) {
            std::printf("blss: the corpus generator produced no frames\n");
            return 1;
        }
        shots = shotCountOf(corpus);
        if (shotNames.empty())
            for (int s = 0; s < shots; ++s) shotNames.push_back(shotNameOf(corpus, s));
        std::printf("blss: seed 0x%X - labelling %zu frames, then %d fold(s) over %d shot(s)\n",
                    so.seed, corpus.size(), o.cvFolds > 0 ? std::min(o.cvFolds, shots) : shots,
                    shots);
        all.push_back(crossValidateOnce(corpus, shots, so, so.seed));
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
    std::printf("\n");
    return 0;
}

// ----------------------------------------------------- feature diagnostics ---

// WHAT THE EIGHT INPUTS ACTUALLY LOOK LIKE. A channel that never moves is a
// channel the 147 weights cannot use, and no amount of training fixes it - this
// is the same class of bug as a term missing from the objective, one level
// further down. Prints, per feature: the distribution over the corpus, and how
// strongly it correlates with what the ORACLE asked for (weighted by importance,
// because tiles where every kernel is equal do not get a vote in training
// either).
int featureReport(const CliOpts& o) {
    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) return 1;
    const int shots = shotCountOf(corpus);
    std::printf("blss: labelling %zu frames to correlate features against the oracle\n",
                corpus.size());
    const std::vector<FrameLabels> labels = labelCorpus(corpus, o.sharpen, o.obj);

    struct Acc {
        double n = 0, sum = 0, sq = 0, lo = 1e30, hi = -1e30, at0 = 0, at1 = 0;
    };
    std::vector<Acc> all(kFeatures);
    std::vector<std::vector<Acc>> byShot(static_cast<size_t>(shots),
                                         std::vector<Acc>(kFeatures));
    // Importance-weighted covariance of each feature with each oracle output.
    std::vector<std::array<double, kOutputs>> cov(kFeatures, std::array<double, kOutputs>{});
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
    return 0;
}

}  // namespace

int trainMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    const std::string outPath = o.outPath.empty() ? o.netPath : o.outPath;
    // Cheap, and it fails here rather than three commands later inside a Docker
    // PS2 build: a net whose weights cannot be spelled as C++ literals is not a
    // trained net, it is a broken generated project.
    std::string emitErr;
    if (!selfTestEmitter(&emitErr)) {
        std::printf("blss: %s - the generated header would not compile\n", emitErr.c_str());
        return 1;
    }

    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) {
        std::printf("blss: the corpus generator produced no frames\n");
        return 1;
    }
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
    const std::vector<FrameLabels> labels = labelCorpus(corpus, o.sharpen, o.obj, &trained);
    std::vector<Sample> samples = gatherSamples(labels, corpus, trained);
    if (samples.empty()) {
        std::printf("blss: no training samples (every shot held out?)\n");
        return 1;
    }

    std::printf("blss: training on %zu tiles from %d shots (objective: flicker %.3f, fill %.2f)\n",
                samples.size(), trainShots, o.obj.flicker, o.obj.fill);
    const TrainConfig tc = trainConfigOf(o);
    Net net;
    const float loss = train(net, samples, tc);

    std::string err;
    if (!save(net, outPath, &err)) {
        std::printf("blss: %s\n", err.c_str());
        return 1;
    }
    std::printf("blss: final loss %.6f, wrote %s\n", loss, outPath.c_str());
    return 0;
}

int evalMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    // Two diagnostics that train their OWN nets and therefore ignore -i: the
    // cross-validation table (which is the honest answer to "does this
    // generalise", the single split being one draw) and the input-channel
    // report (which is the honest answer to "can it, with these features").
    if (o.cv) return crossValidate(o);
    if (o.featureStats) return featureReport(o);
    Net net;
    std::string err;
    if (!load(net, o.netPath, &err)) {
        std::printf("blss: %s\n  (run --blss-train first)\n", err.c_str());
        return 1;
    }
    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) return 1;
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
    const std::vector<Row> rows = {
        {"point", fixedMethod(Kernel::Point), "point"},
        {"bilinear", fixedMethod(Kernel::Bilinear), "bilinear"},
        {"temporal", fixedMethod(Kernel::Temporal), "temporal"},
        {"sharpen", fixedMethod(Kernel::Sharpen), "sharpen"},
        {"BLSS (trained)", netMethod(net, o.sharpen, o.deadzone), "net"},
        {"oracle (upper bound)",
         [&o](const Frame& fr, const Image& truth, size_t) {
             return oracle(fr, truth, o.sharpen, nullptr, o.obj);
         },
         "oracle"},
    };

    // Native = the scene rendered at full resolution, one sample. The honest
    // "no BLSS" reference, and NOT a ceiling: the ground truth is supersampled,
    // so a good temporal reconstruction can beat a 1-sample native render.
    const auto nativeOf = [&](bool heldOut, std::vector<double>* perShot, double* flicker) {
        double sum = 0.0;
        int n = 0;
        std::vector<double> ss(shots, 0.0);
        std::vector<int> sn(shots, 0);
        double flickSum = 0.0;
        int flickN = 0;
        int prevShot = -1;
        const Image* prevNative = nullptr;
        for (const CorpusFrame& cf : corpus) {
            if (isHeldOut(cf.shot, shots) != heldOut) continue;
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
            prevNative = &cf.native;
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
        return n ? sum / n : 0.0;
    };

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
        const double nat = nativeOf(heldOut, &ps, &fl);
        std::printf("  %-22s %8.3f %8.2f %7s %7s %7s %7s  ", "native full-res", nat, fl, "-", "-",
                    "-", "-");
        for (int s : ids) std::printf("%8.3f", ps[s]);
        std::printf("\n");

        for (const Row& r : rows) {
            Occupancy occ;
            const double p = evalRecurrent(corpus, shots, want, o.sharpen, r.m, o.dumpDir,
                                           heldOut ? r.dump : nullptr, &ps, &fl, &occ);
            std::printf("  half-res + %-11s %8.3f %8.2f %6.1f%% %6.1f%% %6.1f%% %7.2f  ", r.name, p,
                        fl, occ.point * 100.0f, occ.temporal * 100.0f, occ.sharpen * 100.0f,
                        occ.passes);
            for (int s : ids) std::printf("%8.3f", ps[s]);
            std::printf("%s\n", std::strcmp(r.name, "BLSS (trained)") == 0 ? "   <-- the network"
                                                                           : "");
        }
    }

    // What the network actually decided, as a picture: one pixel per tile,
    // R = point, G = temporal, B = sharpen.
    if (!o.dumpDir.empty()) {
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
        std::printf("\n  wrote comparison PNGs to %s\n", o.dumpDir.c_str());
    }
    std::printf("\n");
    return 0;
}

int emitMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    std::string err;
    if (!selfTestEmitter(&err)) {
        std::printf("blss: %s - the generated header would not compile\n", err.c_str());
        return 1;
    }
    Net net;
    if (!load(net, o.netPath, &err)) {
        std::printf("blss: %s\n", err.c_str());
        return 1;
    }
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
    std::printf("blss: wrote %s\n", o.outPath.c_str());
    return 0;
}

}  // namespace blss
