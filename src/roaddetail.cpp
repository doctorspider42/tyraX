#include "roaddetail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

namespace roaddetail {
namespace {

// --- deterministic hashing (never a running RNG shared across roads) --------

uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

uint64_t hashString(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

// A small counter-based stream: the i-th draw is a pure function of (key, i).
struct Rng {
    uint64_t key;
    uint64_t n = 0;
    explicit Rng(uint64_t k) : key(k) {}
    float next() { return (float)(mix64(key ^ mix64(++n)) >> 40) / 16777216.0f; }
};

float hash01(uint32_t a, uint32_t b, uint32_t c) {
    const uint64_t h = mix64(((uint64_t)a << 40) ^ ((uint64_t)b << 20) ^ c);
    return (float)(h >> 40) / 16777216.0f;
}

// Smooth value noise over integer lattice points, `scale` pixels per cell.
float vnoise(uint32_t seed, float x, float y, float scale) {
    const float fx = x / scale, fy = y / scale;
    const int ix = (int)std::floor(fx), iy = (int)std::floor(fy);
    const float tx = fx - (float)ix, ty = fy - (float)iy;
    auto h = [&](int a, int b) { return hash01(seed, (uint32_t)(a + 4096), (uint32_t)(b + 4096)); };
    const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
    const float a = h(ix, iy) + (h(ix + 1, iy) - h(ix, iy)) * sx;
    const float b = h(ix, iy + 1) + (h(ix + 1, iy + 1) - h(ix, iy + 1)) * sx;
    return a + (b - a) * sy;
}

// --- the atlas -------------------------------------------------------------

// Palette indices (see palette()).
enum : unsigned char {
    kClear = 0,
    kIronDark = 1,
    kIronMid = 2,
    kIronLight = 3,
    kIronWorn = 4,
    kHole = 5,
    kTarFresh = 6,
    kTarMid = 7,
    kTarOld = 8,
    kTarOlder = 9,
    kCrackCore = 10,
    kCrackSoft = 11,
    kOilCore = 12,
    kOilMid = 13,
    kOilEdge = 14,
    kSeal = 15,
};

struct Canvas {
    std::vector<unsigned char> idx = std::vector<unsigned char>(
        (size_t)kAtlasSize * kAtlasSize, kClear);
    void set(int x, int y, unsigned char c) {
        if (x < 0 || y < 0 || x >= kAtlasSize || y >= kAtlasSize) return;
        idx[(size_t)y * kAtlasSize + x] = c;
    }
    unsigned char get(int x, int y) const { return idx[(size_t)y * kAtlasSize + x]; }
};

void drawManholeRound(Canvas& cv, const Cell& c) {
    const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f;
    for (int y = c.y + 1; y < c.y + c.h - 1; ++y)
        for (int x = c.x + 1; x < c.x + c.w - 1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float r = std::hypot(dx, dy);
            if (r > 14.5f) continue;
            unsigned char col;
            if (r > 13.0f) {
                col = kIronMid;  // the frame, worn on the leading edge
                if (hash01(11, (uint32_t)x, (uint32_t)y) > 0.88f) col = kIronWorn;
            } else if (r > 12.2f) {
                col = kHole;  // the seating gap
            } else {
                // Cast-iron cover: a raised diamond grid, a ring and a hub.
                const int u = (int)std::floor((dx + dy) / 2.5f), v = (int)std::floor((dx - dy) / 2.5f);
                col = ((u + v) & 1) ? kIronLight : kIronDark;
                if (r > 10.6f) col = kIronDark;                   // plain band
                if (r > 5.0f && r < 6.4f) col = kIronLight;       // ring
                if (r <= 5.0f) col = (r > 3.6f) ? kIronDark : kIronMid;  // hub
                // Two pick holes on the hub's axis.
                if (std::hypot(std::fabs(dx) - 8.0f, dy) < 1.2f) col = kHole;
                // Polished by tyres: speckles of bare metal.
                if (hash01(13, (uint32_t)x, (uint32_t)y) > 0.93f) col = kIronWorn;
            }
            cv.set(x, y, col);
        }
}

void drawManholeSquare(Canvas& cv, const Cell& c) {
    const int x0 = c.x + 1, y0 = c.y + 1, x1 = c.x + c.w - 2, y1 = c.y + c.h - 2;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int d = std::min({x - x0, x1 - x, y - y0, y1 - y});
            unsigned char col;
            if (d < 2) {
                col = kIronMid;
                if (hash01(21, (uint32_t)x, (uint32_t)y) > 0.88f) col = kIronWorn;
            } else if (d < 3) {
                col = kHole;
            } else {
                // Raised parallel chequer bars.
                const int lx = x - x0 - 3, ly = y - y0 - 3;
                const bool rowA = ((ly / 4) & 1) == 0;
                const int k = rowA ? lx : lx + 2;
                col = (k % 4 < 2 && ly % 4 != 3) ? kIronLight : kIronDark;
                if (d < 5) col = kIronDark;
                if (hash01(23, (uint32_t)x, (uint32_t)y) > 0.94f) col = kIronWorn;
            }
            cv.set(x, y, col);
        }
    // Corner bolts.
    const int bx[2] = {x0 + 5, x1 - 5}, by[2] = {y0 + 5, y1 - 5};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            cv.set(bx[i], by[j], kIronWorn);
            cv.set(bx[i] + 1, by[j], kIronWorn);
            cv.set(bx[i], by[j] + 1, kIronWorn);
            cv.set(bx[i] + 1, by[j] + 1, kIronWorn);
        }
}

void drawGully(Canvas& cv, const Cell& c) {
    // U (the cell's width) runs ALONG the kerb: the slots run across it.
    const int x0 = c.x + 1, y0 = c.y + 1, x1 = c.x + c.w - 2, y1 = c.y + c.h - 2;
    const int midY = (y0 + y1) / 2;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int d = std::min({x - x0, x1 - x, y - y0, y1 - y});
            unsigned char col;
            if (d < 2) {
                col = kIronLight;  // the frame
                if (hash01(31, (uint32_t)x, (uint32_t)y) > 0.9f) col = kIronWorn;
            } else if (y == midY) {
                col = kIronMid;  // the middle rib
            } else {
                col = ((x - x0) % 3 == 2) ? kIronMid : kHole;  // bars and slots
                if (col == kHole && hash01(33, (uint32_t)x, (uint32_t)y) > 0.85f)
                    col = kTarFresh;  // grit in the slots
            }
            cv.set(x, y, col);
        }
}

void drawPatch(Canvas& cv, const Cell& c, uint32_t seed, unsigned char fill,
               unsigned char fleck, unsigned char fleck2) {
    for (int y = c.y + 1; y < c.y + c.h - 1; ++y)
        for (int x = c.x + 1; x < c.x + c.w - 1; ++x) {
            const float d = (float)std::min({x - c.x - 1, c.x + c.w - 2 - x, y - c.y - 1,
                                             c.y + c.h - 2 - y});
            // Ragged edge: the cut follows the patch's rectangle loosely.
            const float t = 0.5f + 3.0f * vnoise(seed, (float)x, (float)y, 3.0f) +
                            1.0f * vnoise(seed + 7, (float)x, (float)y, 1.3f);
            if (d < t) continue;
            unsigned char col = fill;
            if (d < t + 1.2f) {
                col = kSeal;  // the bitumen seal round the joint
            } else {
                const float m = vnoise(seed + 3, (float)x, (float)y, 6.0f);
                const float s = hash01(seed + 5, (uint32_t)x, (uint32_t)y);
                if (m > 0.68f) col = fleck;
                if (s > 0.96f) col = fleck2;
            }
            cv.set(x, y, col);
        }
}

// A crack: a wandering line across the cell's width with a soft halo and an
// optional branch, tapering into hairlines at both ends.
void drawCrack(Canvas& cv, const Cell& c, uint32_t seed, bool branches) {
    const float cy = c.y + c.h * 0.5f;
    const float amp = (c.h - 6) * 0.5f;
    auto yAt = [&](float x) {
        return cy + amp * (2.0f * vnoise(seed, x, 0.0f, (float)c.w / 5.0f) - 1.0f) * 0.8f +
               (vnoise(seed + 1, x, 0.0f, 2.5f) - 0.5f) * 1.5f;
    };
    auto stamp = [&](int x, int y, bool core) {
        if (x <= c.x || y <= c.y || x >= c.x + c.w - 1 || y >= c.y + c.h - 1) return;
        if (core) {
            cv.set(x, y, kCrackCore);
        } else if (cv.get(x, y) == kClear) {
            cv.set(x, y, kCrackSoft);
        }
    };
    const int xa = c.x + 2, xb = c.x + c.w - 3;
    int prevY = (int)std::lround(yAt((float)xa));
    for (int x = xa; x <= xb; ++x) {
        const int y = (int)std::lround(yAt((float)x));
        const float e = std::min(x - xa, xb - x) / (float)(c.w) ;
        const bool core = e > 0.08f;
        // Join vertical steps so the line never breaks.
        const int lo = std::min(prevY, y), hi = std::max(prevY, y);
        for (int yy = lo; yy <= hi; ++yy) {
            stamp(x, yy, core);
            if (e > 0.2f) stamp(x, yy + 1, true);  // two texels wide mid-crack
        }
        if (e > 0.18f) {
            stamp(x, lo - 1, false);
            stamp(x, hi + (e > 0.2f ? 2 : 1), false);
        }
        prevY = y;
    }
    if (!branches) return;
    for (int b = 0; b < 3; ++b) {
        int x = xa + (int)((0.2f + 0.6f * hash01(seed, 77, (uint32_t)b)) * (xb - xa));
        float y = yAt((float)x);
        const float dir = hash01(seed, 78, (uint32_t)b) > 0.5f ? 1.0f : -1.0f;
        const int len = 5 + (int)(hash01(seed, 79, (uint32_t)b) * 8.0f);
        for (int k = 0; k < len; ++k) {
            x += 1;
            y += dir * (0.5f + 0.5f * hash01(seed, 80 + (uint32_t)k, (uint32_t)b));
            stamp(x, (int)std::lround(y), k < len / 2);
        }
    }
}

void drawStain(Canvas& cv, const Cell& c, uint32_t seed) {
    const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f;
    const float R = std::min(c.w, c.h) * 0.5f - 2.0f;
    for (int y = c.y + 1; y < c.y + c.h - 1; ++y)
        for (int x = c.x + 1; x < c.x + c.w - 1; ++x) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float a = std::atan2(dy, dx);
            // A lobed outline: angular noise, sampled on a circle so it wraps.
            const float wob = vnoise(seed, 8.0f + 6.0f * std::cos(a), 8.0f + 6.0f * std::sin(a), 3.0f);
            const float r = std::hypot(dx, dy) / (R * (0.62f + 0.38f * wob));
            const float n = vnoise(seed + 9, (float)x, (float)y, 2.5f) * 0.25f;
            const float k = r + n;
            unsigned char col = kClear;
            if (k < 0.45f) col = kOilCore;
            else if (k < 0.78f) col = kOilMid;
            else if (k < 1.0f) col = kOilEdge;
            if (col != kClear) cv.set(x, y, col);
        }
    // A few satellite drips.
    for (int d = 0; d < 4; ++d) {
        const float a = 6.2831853f * hash01(seed, 90, (uint32_t)d);
        const float rr = R * (0.85f + 0.2f * hash01(seed, 91, (uint32_t)d));
        const int x = (int)(cx + std::cos(a) * rr), y = (int)(cy + std::sin(a) * rr);
        if (x > c.x && y > c.y && x < c.x + c.w - 1 && y < c.y + c.h - 1) cv.set(x, y, kOilMid);
    }
}

bool writeFile(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)out;
}

// --- placement -------------------------------------------------------------

// Triangles tagged with who owns them: a road index, or one of the blockers.
enum : int { kOwnerPatch = -1, kOwnerPaint = -2, kOwnerSpill = -3 };

class Cover {
public:
    void add(const std::vector<roadgen::Vertex>& tris, int owner) {
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            T tri;
            for (int k = 0; k < 3; ++k) {
                tri.x[k] = tris[t + k].x;
                tri.y[k] = tris[t + k].y;
                tri.z[k] = tris[t + k].z;
            }
            tri.owner = owner;
            tris_.push_back(tri);
        }
    }
    void build() {
        if (tris_.empty()) return;
        float mnx = 1e30f, mnz = 1e30f, mxx = -1e30f, mxz = -1e30f;
        for (const T& t : tris_)
            for (int k = 0; k < 3; ++k) {
                mnx = std::min(mnx, t.x[k]);
                mxx = std::max(mxx, t.x[k]);
                mnz = std::min(mnz, t.z[k]);
                mxz = std::max(mxz, t.z[k]);
            }
        minX_ = mnx;
        minZ_ = mnz;
        nx_ = std::max(1, (int)((mxx - mnx) / kCell) + 1);
        nz_ = std::max(1, (int)((mxz - mnz) / kCell) + 1);
        cells_.assign((size_t)nx_ * nz_, {});
        for (size_t i = 0; i < tris_.size(); ++i) {
            const T& t = tris_[i];
            const int ix0 = cellX(std::min({t.x[0], t.x[1], t.x[2]}));
            const int ix1 = cellX(std::max({t.x[0], t.x[1], t.x[2]}));
            const int iz0 = cellZ(std::min({t.z[0], t.z[1], t.z[2]}));
            const int iz1 = cellZ(std::max({t.z[0], t.z[1], t.z[2]}));
            for (int iz = iz0; iz <= iz1; ++iz)
                for (int ix = ix0; ix <= ix1; ++ix)
                    cells_[(size_t)iz * nx_ + ix].push_back((unsigned)i);
        }
    }
    // The highest surface of `road` at (x, z) (kNone when it does not cover
    // the point); *other is set when anything that is not `road` covers it.
    float probe(int road, float x, float z, bool* other) const {
        float best = roadgen::Surface::kNone;
        if (other) *other = false;
        if (nx_ <= 0) return best;
        const int ix = (int)std::floor((x - minX_) / kCell);
        const int iz = (int)std::floor((z - minZ_) / kCell);
        if (ix < 0 || iz < 0 || ix >= nx_ || iz >= nz_) return best;
        for (unsigned i : cells_[(size_t)iz * nx_ + ix]) {
            const T& t = tris_[i];
            const float den = (t.z[1] - t.z[2]) * (t.x[0] - t.x[2]) +
                              (t.x[2] - t.x[1]) * (t.z[0] - t.z[2]);
            if (std::fabs(den) < 1e-6f) continue;
            const float wa = ((t.z[1] - t.z[2]) * (x - t.x[2]) + (t.x[2] - t.x[1]) * (z - t.z[2])) / den;
            const float wb = ((t.z[2] - t.z[0]) * (x - t.x[2]) + (t.x[0] - t.x[2]) * (z - t.z[2])) / den;
            const float wc = 1.0f - wa - wb;
            if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f) continue;
            if (t.owner != road) {
                if (other) *other = true;
                continue;
            }
            const float y = wa * t.y[0] + wb * t.y[1] + wc * t.y[2];
            if (y > best) best = y;
        }
        return best;
    }

private:
    static constexpr float kCell = 2.0f;
    struct T {
        float x[3], y[3], z[3];
        int owner;
    };
    int cellX(float x) const { return std::clamp((int)((x - minX_) / kCell), 0, nx_ - 1); }
    int cellZ(float z) const { return std::clamp((int)((z - minZ_) / kCell), 0, nz_ - 1); }
    std::vector<T> tris_;
    std::vector<std::vector<unsigned>> cells_;
    float minX_ = 0, minZ_ = 0;
    int nx_ = 0, nz_ = 0;
};

// A road's centre line resampled by arc length.
struct Frame {
    float x, z, tx, tz;
};
class CentreLine {
public:
    explicit CentreLine(const std::vector<float>& pts) {
        const int n = (int)(pts.size() / 2);
        if (n < 2) return;
        const int steps = (n - 1) * 48;
        for (int k = 0; k <= steps; ++k) {
            float x, z;
            roadgen::splineAt(pts, (float)k / (float)steps, &x, &z);
            if (!xs_.empty()) {
                const float d = std::hypot(x - xs_.back(), z - zs_.back());
                if (d < 1e-5f) continue;
                arc_.push_back(arc_.back() + d);
            } else {
                arc_.push_back(0.0f);
            }
            xs_.push_back(x);
            zs_.push_back(z);
        }
    }
    float length() const { return arc_.empty() ? 0.0f : arc_.back(); }
    Frame at(float s) const {
        Frame f{0, 0, 1, 0};
        if (xs_.size() < 2) return f;
        const size_t i = std::min(
            (size_t)(std::upper_bound(arc_.begin(), arc_.end(), s) - arc_.begin()),
            arc_.size() - 1);
        const size_t a = i == 0 ? 0 : i - 1, b = a + 1;
        const float span = arc_[b] - arc_[a];
        const float k = span > 0.0f ? std::clamp((s - arc_[a]) / span, 0.0f, 1.0f) : 0.0f;
        f.x = xs_[a] + (xs_[b] - xs_[a]) * k;
        f.z = zs_[a] + (zs_[b] - zs_[a]) * k;
        f.tx = (xs_[b] - xs_[a]) / std::max(span, 1e-6f);
        f.tz = (zs_[b] - zs_[a]) / std::max(span, 1e-6f);
        return f;
    }

private:
    std::vector<float> xs_, zs_, arc_;
};

// Cells of one kind.
std::vector<int> cellsOf(int kind) {
    std::vector<int> out;
    const std::vector<Cell>& cs = cells();
    for (size_t i = 0; i < cs.size(); ++i)
        if (cs[i].kind == kind) out.push_back((int)i);
    return out;
}

}  // namespace

const char* kindName(int kind) {
    switch (kind) {
        case kManholeRound: return "manhole (round)";
        case kManholeSquare: return "manhole (square)";
        case kGully: return "gully";
        case kPatch: return "patch";
        case kCrack: return "crack";
        case kStain: return "oil stain";
        default: return "?";
    }
}

const std::vector<Cell>& cells() {
    static const std::vector<Cell> kCells = {
        {kManholeRound, 0, 0, 32, 32},
        {kManholeSquare, 32, 0, 32, 32},
        {kGully, 64, 0, 32, 20},
        {kStain, 96, 0, 32, 32},
        {kPatch, 0, 32, 64, 32},
        {kPatch, 64, 32, 64, 32},
        {kPatch, 0, 64, 48, 32},
        {kStain, 48, 64, 32, 32},
        {kCrack, 80, 64, 48, 16},
        {kCrack, 80, 80, 48, 16},
        {kCrack, 0, 96, 128, 16},
        {kCrack, 0, 112, 128, 16},
    };
    return kCells;
}

const std::vector<uint32_t>& palette() {
    // The road texture's asphalt is ~59 grey: fresh tar reads darker, old
    // patches bleached lighter, iron a touch cooler. Alpha is data only for the
    // cracks and the oil (StaPip drops alpha 0; the bag blends the rest).
    static const std::vector<uint32_t> kPalette = {
        0x00000000u,  // clear
        0x222224FFu,  // iron, dark
        0x38383AFFu,  // iron, mid
        0x545352FFu,  // iron, raised
        0x706E6AFFu,  // iron, polished by tyres
        0x0A0A0AFFu,  // hole / slot
        0x1A1A1CFFu,  // tar, fresh
        0x28282AFFu,  // tar, mid
        0x4E4D4AFFu,  // tar, old
        0x5E5C58FFu,  // tar, older
        0x080808E6u,  // crack core
        0x0E0E0E78u,  // crack halo
        0x0C0A08AAu,  // oil, core
        0x100E0C6Eu,  // oil, mid
        0x14121037u,  // oil, edge
        0x121212FFu,  // bitumen seal
    };
    return kPalette;
}

std::vector<unsigned char> generateAtlas() {
    Canvas cv;
    const std::vector<Cell>& cs = cells();
    uint32_t patchSeed = 101, crackSeed = 201, stainSeed = 301;
    int patchNo = 0, crackNo = 0;
    for (const Cell& c : cs) {
        switch (c.kind) {
            case kManholeRound: drawManholeRound(cv, c); break;
            case kManholeSquare: drawManholeSquare(cv, c); break;
            case kGully: drawGully(cv, c); break;
            case kPatch: {
                // Fresh dark tar, an old bleached patch, a mid one.
                static const unsigned char fills[3][3] = {
                    {kTarFresh, kTarMid, kSeal},
                    {kTarOld, kTarOlder, kTarMid},
                    {kTarMid, kTarFresh, kTarOld}};
                const unsigned char* f = fills[patchNo % 3];
                drawPatch(cv, c, patchSeed, f[0], f[1], f[2]);
                patchSeed += 17;
                ++patchNo;
                break;
            }
            case kCrack:
                drawCrack(cv, c, crackSeed, crackNo % 2 == 1);
                crackSeed += 13;
                ++crackNo;
                break;
            case kStain:
                drawStain(cv, c, stainSeed);
                stainSeed += 11;
                break;
            default: break;
        }
    }
    const std::vector<uint32_t>& pal = palette();
    std::vector<unsigned char> rgba((size_t)kAtlasSize * kAtlasSize * 4);
    for (size_t i = 0; i < cv.idx.size(); ++i) {
        const uint32_t p = pal[cv.idx[i]];
        rgba[i * 4 + 0] = (unsigned char)(p >> 24);
        rgba[i * 4 + 1] = (unsigned char)(p >> 16);
        rgba[i * 4 + 2] = (unsigned char)(p >> 8);
        rgba[i * 4 + 3] = (unsigned char)p;
    }
    return rgba;
}

std::string ensureAtlas(const std::string& projectDir) {
    namespace fs = std::filesystem;
    const fs::path png = fs::path(projectDir) / kAtlasPng;
    const fs::path mtl = fs::path(projectDir) / kAtlasMtl;
    std::error_code ec;
    if (fs::exists(png, ec) && fs::exists(mtl, ec)) return "";
    fs::create_directories(png.parent_path(), ec);
    if (!fs::exists(png, ec)) {
        const std::vector<unsigned char> px = generateAtlas();
        std::string bytes;
        stbi_write_png_to_func(
            [](void* ctx, void* data, int size) {
                static_cast<std::string*>(ctx)->append(static_cast<const char*>(data),
                                                       (size_t)size);
            },
            &bytes, kAtlasSize, kAtlasSize, 4, px.data(), kAtlasSize * 4);
        if (bytes.empty() || !writeFile(png, bytes)) return "cannot write " + png.string();
    }
    if (!fs::exists(mtl, ec)) {
        const std::string text = std::string("# Generated by TyraX (road details atlas, "
                                             "docs/roads.md \"Road details\")\nnewmtl ") +
                                 kAtlasStem + "\nKd 1 1 1\nmap_Kd " + kAtlasStem + ".png\n";
        if (!writeFile(mtl, text)) return "cannot write " + mtl.string();
    }
    return "";
}

bool any(const std::vector<roadgen::CrossingRoad>& roads) {
    for (const roadgen::CrossingRoad& r : roads)
        if (r.details > 0.0f && r.points.size() >= 4) return true;
    return false;
}

Result build(const SceneInput& in) {
    Result res;
    if (!in.roads || !any(*in.roads)) return res;
    const std::vector<roadgen::CrossingRoad>& roads = *in.roads;

    // Who owns which piece of drawn surface.
    Cover cover;
    for (size_t i = 0; i < roads.size(); ++i) {
        const roadgen::CrossingRoad& r = roads[i];
        if (r.points.size() < 4) continue;
        std::vector<roadgen::Vertex> mesh;
        const float lift = roadgen::rankLift(r.rank);
        roadgen::tessellate(
            r.points, r.width,
            [&](float x, float z) { return (in.ground ? in.ground(x, z) : 0.0f) + lift; },
            mesh, {}, r.sampleStep);
        cover.add(mesh, (int)i);
    }
    cover.add(in.patches, kOwnerPatch);
    cover.add(in.paint, kOwnerPaint);
    if (in.plan)
        for (const roadgen::CrossingDecal& d : in.plan->decals) {
            std::vector<roadgen::Vertex> tris;
            for (const roadgen::SpillVertex& v : d.verts) tris.push_back({v.x, 0.0f, v.z, 0, 0});
            cover.add(tris, kOwnerSpill);
        }
    cover.build();

    struct Placed {
        float x, z, r;
    };
    std::vector<Placed> placed;
    std::vector<Decal> accepted;
    const std::vector<Cell>& cs = cells();

    // The footprint test: the whole rectangle on `road` alone, and nothing but
    // `road` within kClearance of it.
    auto fits = [&](const Decal& d) {
        const float bx = -d.az, bz = d.ax;
        for (int pass = 0; pass < 2; ++pass) {
            const float hu = d.hu + (pass ? kClearance : 0.0f);
            const float hv = d.hv + (pass ? kClearance : 0.0f);
            const int nu = std::max(2, (int)std::ceil(2.0f * hu / 0.25f) + 1);
            const int nv = std::max(2, (int)std::ceil(2.0f * hv / 0.25f) + 1);
            for (int j = 0; j < nv; ++j)
                for (int i = 0; i < nu; ++i) {
                    const float s = -1.0f + 2.0f * (float)i / (float)(nu - 1);
                    const float t = -1.0f + 2.0f * (float)j / (float)(nv - 1);
                    const float x = d.x + d.ax * s * hu + bx * t * hv;
                    const float z = d.z + d.az * s * hu + bz * t * hv;
                    bool other = false;
                    const float y = cover.probe(d.road, x, z, &other);
                    if (other) return 2;
                    if (pass == 0 && y == roadgen::Surface::kNone) return 1;
                }
        }
        return 0;
    };
    auto tryPlace = [&](Decal d) {
        ++res.candidates;
        const float rad = std::hypot(d.hu, d.hv);
        for (const Placed& p : placed)
            if (std::hypot(p.x - d.x, p.z - d.z) < p.r + rad + 0.1f) {
                ++res.rejected;
                ++res.rejectedOverlap;
                return;
            }
        if (const int why = fits(d)) {
            ++res.rejected;
            ++(why == 1 ? res.rejectedOffRoad : res.rejectedClearance);
            return;
        }
        placed.push_back({d.x, d.z, rad});
        accepted.push_back(d);
    };

    for (size_t ri = 0; ri < roads.size(); ++ri) {
        const roadgen::CrossingRoad& r = roads[ri];
        if (!(r.details > 0.0f) || r.points.size() < 4) continue;
        const float dens = std::clamp(r.details, 0.0f, 1.0f);
        const CentreLine line(r.points);
        const float L = line.length();
        if (L < 1.0f) continue;
        const float hc = 0.5f * roadgen::edgeFadeFor(r.width, r.edgeFade).coreWidth;
        const int lanes = std::clamp((int)std::lround(2.0f * hc / 3.5f), 1, 6);
        const uint64_t roadKey =
            mix64(hashString(r.id) ^ mix64((uint64_t)(uint32_t)r.detailSeed + 0x51u));
        auto laneCentre = [&](int j) { return -hc + (2.0f * hc) * ((float)j + 0.5f) / (float)lanes; };
        // A decal at arc s, lateral `lat`, its U axis turned `turn` radians
        // from the road's direction.
        auto make = [&](int kind, int cell, float s, float lat, float turn, float hu, float hv) {
            const Frame f = line.at(s);
            const float lx = -f.tz, lz = f.tx;  // the road's left
            Decal d;
            d.road = (int)ri;
            d.kind = kind;
            d.cell = cell;
            d.x = f.x + lx * lat;
            d.z = f.z + lz * lat;
            const float c = std::cos(turn), sn = std::sin(turn);
            d.ax = f.tx * c - f.tz * sn;
            d.az = f.tz * c + f.tx * sn;
            d.hu = hu;
            d.hv = hv;
            // Its reach across the road must stay inside the opaque core.
            const float reach = std::fabs(d.ax * lx + d.az * lz) * hu +
                                std::fabs(-d.az * lx + d.ax * lz) * hv;
            if (std::fabs(lat) + reach > hc - 0.05f) return;
            tryPlace(d);
        };
        auto pick = [](const std::vector<int>& v, float u) {
            return v[std::min(v.size() - 1, (size_t)(u * (float)v.size()))];
        };

        // Manholes: one every 20..60 units, in a lane.
        {
            Rng g(roadKey ^ 0x1001u);
            const float spacing = 20.0f + 40.0f * (1.0f - dens);
            for (float s = spacing * (0.3f + 0.5f * g.next()); s < L;
                 s += spacing * (0.75f + 0.5f * g.next())) {
                const bool square = g.next() < 0.25f;
                const int lane = std::min(lanes - 1, (int)(g.next() * (float)lanes));
                const float lat = laneCentre(lane) + (g.next() - 0.5f) * 0.6f;
                const float turn = square ? 0.0f : g.next() * 6.2831853f;
                const float h = square ? 0.4f : 0.45f;
                make(square ? kManholeSquare : kManholeRound,
                     square ? cellsOf(kManholeSquare)[0] : cellsOf(kManholeRound)[0], s, lat,
                     turn, h, h);
            }
        }
        // Gullies: both edges, just inside the kerb line - kerbed roads only.
        if (r.kerb) {
            const int cell = cellsOf(kGully)[0];
            const float hu = 0.36f, hv = hu * (float)cs[(size_t)cell].h / (float)cs[(size_t)cell].w;
            const float spacing = 20.0f + 20.0f * (1.0f - dens);
            for (int side = -1; side <= 1; side += 2) {
                Rng g(roadKey ^ (side < 0 ? 0x2001u : 0x2002u));
                for (float s = spacing * (0.2f + 0.6f * g.next()); s < L;
                     s += spacing * (0.85f + 0.3f * g.next()))
                    make(kGully, cell, s, (float)side * (hc - hv - 0.06f), 0.0f, hu, hv);
            }
        }
        // Repair patches: along the lane, or a trench cut across it.
        {
            Rng g(roadKey ^ 0x3001u);
            const std::vector<int> pc = cellsOf(kPatch);
            const float spacing = 12.0f + 48.0f * (1.0f - dens);
            for (float s = spacing * g.next(); s < L; s += spacing * (0.5f + g.next())) {
                const int cell = pick(pc, g.next());
                const float len = 1.8f + 2.4f * g.next();
                const float hu = 0.5f * len;
                const float hv = hu * (float)cs[(size_t)cell].h / (float)cs[(size_t)cell].w;
                const bool across = g.next() < 0.3f;
                const float turn = (across ? 1.5707963f : 0.0f) + (g.next() - 0.5f) * 0.3f;
                const float lat = (g.next() * 2.0f - 1.0f) * hc;
                make(kPatch, cell, s, lat, turn, hu, hv);
            }
        }
        // Cracks: any direction.
        {
            Rng g(roadKey ^ 0x4001u);
            const std::vector<int> cc = cellsOf(kCrack);
            const float spacing = 8.0f + 32.0f * (1.0f - dens);
            for (float s = spacing * g.next(); s < L; s += spacing * (0.5f + g.next())) {
                const int cell = pick(cc, g.next());
                const Cell& c = cs[(size_t)cell];
                const bool longCell = c.w >= 96;
                const float len = longCell ? 3.0f + 2.0f * g.next() : 1.4f + 1.0f * g.next();
                const float hu = 0.5f * len, hv = hu * (float)c.h / (float)c.w;
                const float turn = g.next() * 6.2831853f;
                const float lat = (g.next() * 2.0f - 1.0f) * hc;
                make(kCrack, cell, s, lat, turn, hu, hv);
            }
        }
        // Oil stains: where cars stand and drip - near a lane's centre.
        {
            Rng g(roadKey ^ 0x5001u);
            const std::vector<int> sc = cellsOf(kStain);
            const float spacing = 15.0f + 45.0f * (1.0f - dens);
            for (float s = spacing * g.next(); s < L; s += spacing * (0.5f + g.next())) {
                const int cell = pick(sc, g.next());
                const int lane = std::min(lanes - 1, (int)(g.next() * (float)lanes));
                const float lat = laneCentre(lane) + (g.next() - 0.5f) * 1.2f;
                const float h = 0.35f + 0.4f * g.next();
                make(kStain, cell, s, lat, g.next() * 6.2831853f, h, h);
            }
        }
    }

    // Chunks: whole decals grouped by kDetailCell cell, in a fixed order.
    struct Keyed {
        int cz, cx;
        size_t i;
    };
    std::vector<Keyed> order;
    for (size_t i = 0; i < accepted.size(); ++i)
        order.push_back({(int)std::floor(accepted[i].z / kDetailCell),
                         (int)std::floor(accepted[i].x / kDetailCell), i});
    std::stable_sort(order.begin(), order.end(), [](const Keyed& a, const Keyed& b) {
        return a.cz != b.cz ? a.cz < b.cz : a.cx < b.cx;
    });

    std::vector<roadgen::Vertex> one;
    int chunkStart = 0, curCz = 0, curCx = 0;
    bool open = false;
    for (const Keyed& k : order) {
        const Decal& d = accepted[k.i];
        const Cell& c = cs[(size_t)d.cell];
        const float bx = -d.az, bz = d.ax;
        auto world = [&](float s, float t, float* x, float* z) {
            *x = d.x + d.ax * s * d.hu + bx * t * d.hv;
            *z = d.z + d.az * s * d.hu + bz * t * d.hv;
        };
        auto yAt = [&](float s, float t) {
            float x, z;
            world(s, t, &x, &z);
            return cover.probe(d.road, x, z, nullptr);
        };
        // Split until every piece follows the surface to kFlatness.
        int n = 1;
        for (; n < 8; n *= 2) {
            bool flat = true;
            for (int j = 0; j < n && flat; ++j)
                for (int i = 0; i < n && flat; ++i) {
                    auto S = [&](float u) { return -1.0f + 2.0f * ((float)i + u) / (float)n; };
                    auto Tt = [&](float v) { return -1.0f + 2.0f * ((float)j + v) / (float)n; };
                    const float y0 = yAt(S(0), Tt(0)), y1 = yAt(S(1), Tt(0));
                    const float y2 = yAt(S(1), Tt(1)), y3 = yAt(S(0), Tt(1));
                    static const float probes[7][2] = {{0.5f, 0.5f}, {0.5f, 0.0f}, {1.0f, 0.5f},
                                                       {0.5f, 1.0f}, {0.0f, 0.5f}, {0.75f, 0.25f},
                                                       {0.25f, 0.75f}};
                    for (const auto& p : probes) {
                        const float u = p[0], v = p[1];
                        const float lin = u >= v ? y0 + u * (y1 - y0) + v * (y2 - y1)
                                                 : y0 + u * (y2 - y3) + v * (y3 - y0);
                        if (std::fabs(yAt(S(u), Tt(v)) - lin) > kFlatness) flat = false;
                    }
                }
            if (flat) break;
        }
        one.clear();
        bool off = false;
        const float inv = 1.0f / (float)kAtlasSize;
        for (int j = 0; j < n && !off; ++j)
            for (int i = 0; i < n && !off; ++i) {
                float qx[4], qy[4], qz[4], qu[4], qv[4];
                static const int corner[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
                for (int m = 0; m < 4; ++m) {
                    const float fs = (float)(i + corner[m][0]) / (float)n;
                    const float ft = (float)(j + corner[m][1]) / (float)n;
                    const float s = -1.0f + 2.0f * fs, t = -1.0f + 2.0f * ft;
                    world(s, t, &qx[m], &qz[m]);
                    const float y = cover.probe(d.road, qx[m], qz[m], nullptr);
                    if (y == roadgen::Surface::kNone) off = true;
                    qy[m] = y + kDetailLift;
                    // Texel centres of the cell's edge pixels (its transparent
                    // margin), so bilinear filtering never reads a neighbour.
                    qu[m] = ((float)c.x + 0.5f + fs * (float)(c.w - 1)) * inv;
                    qv[m] = ((float)c.y + 0.5f + ft * (float)(c.h - 1)) * inv;
                }
                const float crs = (qx[1] - qx[0]) * (qz[2] - qz[0]) - (qz[1] - qz[0]) * (qx[2] - qx[0]);
                static const int o[6] = {0, 1, 2, 0, 2, 3}, rv[6] = {0, 2, 1, 0, 3, 2};
                for (int m = 0; m < 6; ++m) {
                    const int idx = crs >= 0.0f ? o[m] : rv[m];
                    one.push_back({qx[idx], qy[idx], qz[idx], qu[idx], qv[idx]});
                }
            }
        if (off || one.empty()) {
            ++res.rejected;
            continue;
        }
        const int size = (int)res.tris.size() - chunkStart;
        if (open && (k.cz != curCz || k.cx != curCx || size + (int)one.size() > kChunkBudget)) {
            res.chunkSizes.push_back(size);
            chunkStart = (int)res.tris.size();
        }
        open = true;
        curCz = k.cz;
        curCx = k.cx;
        res.tris.insert(res.tris.end(), one.begin(), one.end());
        res.decals.push_back(d);
    }
    if (open && (int)res.tris.size() > chunkStart)
        res.chunkSizes.push_back((int)res.tris.size() - chunkStart);
    return res;
}

}  // namespace roaddetail
