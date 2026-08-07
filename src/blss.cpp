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

std::string emitGeneratedSource(const Net& n) {
    float flat[kNetFloats];
    netToFlat(n, flat);
    std::string s;
    s += "// GENERATED by tyrax-editor --blss-emit. Do not edit.\n";
    s += "// The trained BLSS network (docs/neural-upscaler.md): an MLP\n";
    s += "// 8 -> 12 -> 3, tanh hidden, logistic outputs. Handed to\n";
    s += "// RendererCoreBlss::setNet(), which runs it per 32x32 screen tile.\n";
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

    char buf[64];
    const auto table = [&](const char* name, int count, size_t at, int perLine) {
        s += "static const float " + std::string(name) + "[" + std::to_string(count) + "] = {\n   ";
        for (int i = 0; i < count; ++i) {
            std::snprintf(buf, sizeof(buf), " %.9gF,", static_cast<double>(flat[at + i]));
            s += buf;
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
    aA = static_cast<int>(clamp01(w[0]) * 128.0f);
    // The temporal pass is an EXPONENTIAL ACCUMULATOR, not a two-frame average:
    // the history is the previous frame's own composite, so wC = 1 means "keep
    // kTemporalMax/128 of everything that came before".
    //
    // It used to cap at 64 (a flat 50% mix), which is where the visible bob came
    // from. A 50% accumulator has a time constant of about one frame, so against
    // a jitter that alternates every frame it TRACKS the alternation instead of
    // averaging it out, and settles into a stationary +-1/3 pixel oscillation -
    // exactly what bob deinterlacing looks like. Per-frame PSNR cannot see it
    // (the mix of two phases is genuinely closer to the truth than either), which
    // is why the flicker metric in --blss-eval now exists.
    aC = static_cast<int>(clamp01(w[1]) * kTemporalMax);
    aD = static_cast<int>(clamp01(w[2]) * clamp01(sharpen) * 128.0f);
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
                const Sample& smp = samples[rng.next() % samples.size()];
                const float iw = std::max(smp.importance, 0.0f);
                if (iw <= 0.0f) continue;
                batchW += iw;

                // Forward, keeping the activations for the backward pass.
                float h[kHidden], o[kOutputs];
                for (int k = 0; k < kHidden; ++k) {
                    float a = net.b1[k];
                    for (int i = 0; i < kFeatures; ++i) a += net.w1[k][i] * smp.f.v[i];
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
                    for (int i = 0; i < kFeatures; ++i) grad.w1[k][i] += d * smp.f.v[i];
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
    return lastLoss;
}

// ------------------------------------------------------------- CLI plumbing ---

namespace {

struct CliOpts {
    std::string netPath = "blss.net";
    std::string outPath;
    std::string dumpDir;
    std::string assetDir = "examples";
    int frames = 48;
    int epochs = 400;
    Scale scale = Scale::X2Y2;
    float sharpen = 0.5f;
    uint32_t seed = 0xB1557u;
    // What the oracle is actually asked to minimise. Overridable because the
    // two weights are a JOINT trade (stability against sharpness against fill)
    // and the only honest way to set them is to sweep the pair and read all
    // three columns - which a rebuild per point makes miserable.
    Objective obj;
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
        else if (a == "--frames") o.frames = std::atoi(next("48").c_str());
        else if (a == "--epochs") o.epochs = std::atoi(next("400").c_str());
        else if (a == "--sharpen") o.sharpen = static_cast<float>(std::atof(next("0.5").c_str()));
        else if (a == "--flicker-weight")
            o.obj.flicker = static_cast<float>(std::atof(next("0.15").c_str()));
        else if (a == "--fill-weight")
            o.obj.fill = static_cast<float>(std::atof(next("6").c_str()));
        else if (a == "--seed") o.seed = static_cast<uint32_t>(std::strtoul(next("0").c_str(), nullptr, 0));
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
bool isHeldOut(int shot, int shotCount) {
    if (shotCount <= 2) return shot == shotCount - 1;
    return shot % 3 == 1;
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

WeightField netField(const Net& net, const Frame& fr) {
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
        for (int m = 0; m < kOutputs; ++m) wf.tile[k][m] = o[m];
    }
    return wf;
}

// A reconstruction method: tile weights for one frame. `truth` is passed so the
// oracle can be measured through the exact same loop as every fixed kernel -
// it ignores nothing else, and the oracle ignores nothing at all.
using Method = std::function<WeightField(const Frame&, const Image& truth)>;

// PSNR over one side of the split, closing the temporal loop: each frame's
// history is the previous frame's real composite, which is what the console has.
// `perShot` (optional, indexed by shot id) receives per-shot means, because a
// single average over a bestiary hides exactly the thing worth knowing - which
// cases the network helps and which it should have left alone.
double evalRecurrent(const std::vector<CorpusFrame>& corpus, int shotCount, bool wantHeldOut,
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
    for (const CorpusFrame& cf : corpus) {
        if (isHeldOut(cf.shot, shotCount) != wantHeldOut) continue;
        Frame fr = cf.frame;
        fr.history = (cf.shot == prevShot && prevOut.w == fr.outW) ? &prevOut : nullptr;
        const WeightField wf = weights(fr, cf.truth);
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
    return [k](const Frame& fr, const Image&) {
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

}  // namespace

int trainMain(int argc, char** argv) {
    const CliOpts o = parseCli(argc, argv);
    const std::string outPath = o.outPath.empty() ? o.netPath : o.outPath;

    std::vector<CorpusFrame> corpus = buildCorpus(o);
    if (corpus.empty()) {
        std::printf("blss: the corpus generator produced no frames\n");
        return 1;
    }
    const int shots = shotCountOf(corpus);

    std::printf("blss: labelling %zu frames with the oracle\n", corpus.size());
    std::vector<Sample> samples;
    double impSum = 0.0;
    for (const CorpusFrame& cf : corpus) {
        if (isHeldOut(cf.shot, shots)) continue;
        std::vector<float> imp;
        const WeightField wf = oracle(cf.frame, cf.truth, o.sharpen, &imp, o.obj);
        for (size_t k = 0; k < wf.tile.size(); ++k) {
            Sample s;
            s.f = cf.frame.features[k];
            s.target = wf.tile[k];
            s.importance = imp[k];
            impSum += imp[k];
            samples.push_back(s);
        }
    }
    if (samples.empty()) {
        std::printf("blss: no training samples (every shot held out?)\n");
        return 1;
    }
    // Normalise importance to mean 1 so the learning rate means the same thing
    // whatever the corpus looks like.
    const float impMean = static_cast<float>(impSum / samples.size());
    if (impMean > 0.0f)
        for (Sample& s : samples) s.importance /= impMean;

    std::printf("blss: training on %zu tiles from %d shots (objective: flicker %.3f, fill %.2f)\n",
                samples.size(), shots - 2, o.obj.flicker, o.obj.fill);
    TrainConfig tc;
    tc.epochs = o.epochs;
    tc.seed = o.seed;
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
        {"BLSS (trained)", [&net](const Frame& fr, const Image&) { return netField(net, fr); },
         "net"},
        {"oracle (upper bound)",
         [&o](const Frame& fr, const Image& truth) {
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

    for (int pass = 0; pass < 2; ++pass) {
        const bool heldOut = pass == 0;
        int frames = 0;
        std::vector<int> ids;
        for (const CorpusFrame& cf : corpus)
            if (isHeldOut(cf.shot, shots) == heldOut) {
                ++frames;
                if (std::find(ids.begin(), ids.end(), cf.shot) == ids.end()) ids.push_back(cf.shot);
            }
        if (!frames) continue;

        std::printf("\n  %s shots %s- %d frame(s), PSNR vs supersampled truth"
                    " (oracle objective: flicker %.3f, fill %.2f)\n",
                    heldOut ? "HELD-OUT" : "training", "", frames, o.obj.flicker, o.obj.fill);
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
            const double p = evalRecurrent(corpus, shots, heldOut, o.sharpen, r.m, o.dumpDir,
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
                const WeightField nf = netField(net, cf.frame);
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
    Net net;
    std::string err;
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
