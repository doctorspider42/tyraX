// The Motor District's seven roads at one lateral budget, priced against the
// branch-tip surface: what the reduction removes, and the error it costs at
// every dense sample of the reference.
#include "roadgen.hpp"
#include "ref_roadgen.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static int hmW, hmD;
static float terrW, terrD;
static std::vector<float> heights;

static float terrainHeight(float x, float z) {
    if (hmW < 2 || hmD < 2 || (int)heights.size() != hmW * hmD) return 0.0f;
    const float w = terrW, d = terrD;
    float gx = (x + w * 0.5f) / w * (hmW - 1);
    float gz = (z + d * 0.5f) / d * (hmD - 1);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > hmW - 1.001f) gx = hmW - 1.001f;
    if (gz > hmD - 1.001f) gz = hmD - 1.001f;
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return heights[(size_t)b * hmW + a]; };
    if (fx + fz <= 1.0f)
        return h(ix, iz) + fx * (h(ix + 1, iz) - h(ix, iz)) +
               fz * (h(ix, iz + 1) - h(ix, iz));
    return h(ix + 1, iz + 1) +
           (1.0f - fz) * (h(ix + 1, iz) - h(ix + 1, iz + 1)) +
           (1.0f - fx) * (h(ix, iz + 1) - h(ix + 1, iz + 1));
}

static bool degenerate(const roadgen::Vertex& a, const roadgen::Vertex& b,
                       const roadgen::Vertex& c) {
    auto same = [](const roadgen::Vertex& x, const roadgen::Vertex& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    return same(a, b) || same(b, c) || same(a, c);
}

// Barycentric lookup in XZ over a triangle LIST. A tightly looping road
// overlaps itself in XZ - the Ring road passes over its own start - so the
// covering triangle is the one closest in ARC LENGTH (v), which identifies
// which part of the ribbon this is; a merge can only ever move v by a
// vanishing amount, while the far side of the loop is a hundred texture
// repeats away. Ties break on y.
struct Sample {
    float y, u, v;
};
static bool lookup(const std::vector<roadgen::Vertex>& mesh, float x, float z,
                   float refY, float refV, Sample* out) {
    bool found = false;
    float bestKey = 0.0f;
    for (size_t i = 0; i + 2 < mesh.size(); i += 3) {
        const auto& a = mesh[i];
        const auto& b = mesh[i + 1];
        const auto& c = mesh[i + 2];
        const float den = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(den) < 1e-9f) continue;
        const float w0 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / den;
        const float w1 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / den;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
        Sample s{w0 * a.y + w1 * b.y + w2 * c.y, w0 * a.u + w1 * b.u + w2 * c.u,
                 w0 * a.v + w1 * b.v + w2 * c.v};
        const float key =
            std::fabs(s.v - refV) * 1000.0f + std::fabs(s.y - refY);
        if (!found || key < bestKey) {
            found = true;
            bestKey = key;
            *out = s;
        }
    }
    return found;
}

// The crack metric. At a lateral cut that one span makes and its neighbour
// merges through, the two spans meet at a T-vertex: the dense side puts the
// sampled vertex there, the merged side puts its plane. Sample every covering
// candidate triangle at a reference VERTEX position and report the spread -
// that spread IS the gap the player would see. Triangles from the far side of
// a self-overlapping loop are excluded by arc length, as in lookup().
static float crackAt(const std::vector<roadgen::Vertex>& mesh, float x, float z,
                     float refV) {
    float lo = 0.0f, hi = 0.0f;
    bool any = false;
    for (size_t i = 0; i + 2 < mesh.size(); i += 3) {
        const auto& a = mesh[i];
        const auto& b = mesh[i + 1];
        const auto& c = mesh[i + 2];
        const float den = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(den) < 1e-9f) continue;
        const float w0 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / den;
        const float w1 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / den;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
        const float vv = w0 * a.v + w1 * b.v + w2 * c.v;
        if (std::fabs(vv - refV) > 0.5f) continue;
        const float yy = w0 * a.y + w1 * b.y + w2 * c.y;
        if (!any) {
            any = true;
            lo = hi = yy;
        } else {
            lo = std::min(lo, yy);
            hi = std::max(hi, yy);
        }
    }
    return any ? hi - lo : 0.0f;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::ifstream in(argv[1]);
    if (!in) return 2;
    in >> hmW >> hmD >> terrW >> terrD;
    heights.resize((size_t)hmW * hmD);
    for (auto& h : heights) in >> h;
    int nroads = 0;
    in >> nroads;
    std::string line;
    std::getline(in, line);

    long totRefTri = 0, totTri = 0, totPkg = 0, totRefPkg = 0;
    long totStrip = 0, totRefStrip = 0;
    double worstY = 0.0, worstU = 0.0, worstV = 0.0, worstCrack = 0.0;
    double sumY2 = 0.0;
    long samples = 0, missed = 0;

    std::printf("%-22s %8s %8s %7s %7s %9s %9s %9s\n", "road", "tris_ref",
                "tris", "pkg_ref", "pkg", "maxdY", "maxdU", "maxdV");
    for (int r = 0; r < nroads; ++r) {
        std::getline(in, line);
        const size_t b1 = line.find('|'), b2 = line.find('|', b1 + 1);
        const std::string name = line.substr(0, b1);
        const float width = std::stof(line.substr(b1 + 1, b2 - b1 - 1));
        const int np = std::stoi(line.substr(b2 + 1));
        std::vector<float> pts((size_t)np * 2);
        for (auto& p : pts) in >> p;
        std::getline(in, line);

        std::vector<roadgen::Vertex> list, strip;
        std::vector<int> chunkSizes;
        roadgen::tessellate(pts, width, terrainHeight, list);
        roadgen::tessellateStrips(pts, width, terrainHeight, strip, &chunkSizes);

        std::vector<roadref::Vertex> refList;
        std::vector<roadref::Vertex> refStrip;
        std::vector<int> refChunks;
        roadref::tessellate(pts, width, terrainHeight, refList);
        roadref::tessellateStrips(pts, width, terrainHeight, refStrip,
                                  &refChunks);

        auto countTris = [](const std::vector<roadgen::Vertex>& v,
                            const std::vector<int>& chunks, int* pkgs) {
            int tris = 0;
            *pkgs = 0;
            size_t at = 0;
            for (int cs : chunks) {
                const size_t end = at + (size_t)cs;
                *pkgs += (int)(((size_t)cs + roadgen::kStripRun - 1) /
                               (size_t)roadgen::kStripRun);
                for (size_t rr = at; rr < end;) {
                    const size_t left = end - rr;
                    const size_t len = left < (size_t)roadgen::kStripRun
                                           ? left
                                           : (size_t)roadgen::kStripRun;
                    for (size_t k = 0; k + 2 < len; ++k)
                        if (!degenerate(v[rr + k], v[rr + k + 1], v[rr + k + 2]))
                            ++tris;
                    rr += len;
                }
                at = end;
            }
            return tris;
        };
        int pkgs = 0;
        const int tris = countTris(strip, chunkSizes, &pkgs);
        // The reference shares the vertex layout; reinterpret it through the
        // same counter rather than duplicating it for a renamed struct.
        std::vector<roadgen::Vertex> refStripAs(refStrip.size());
        for (size_t k = 0; k < refStrip.size(); ++k)
            refStripAs[k] = {refStrip[k].x, refStrip[k].y, refStrip[k].z,
                             refStrip[k].u, refStrip[k].v};
        int refPkgs = 0;
        const int refTris = countTris(refStripAs, refChunks, &refPkgs);

        // The crack metric, at every reference VERTEX.
        double wc = 0.0;
        for (size_t i = 0; i < refList.size(); ++i)
            wc = std::max(wc, (double)crackAt(list, refList[i].x, refList[i].z,
                                              refList[i].v));
        worstCrack = std::max(worstCrack, wc);

        // Error at every reference triangle's centroid.
        double wy = 0.0, wu = 0.0, wv = 0.0;
        for (size_t i = 0; i + 2 < refList.size(); i += 3) {
            const auto& a = refList[i];
            const auto& b = refList[i + 1];
            const auto& c = refList[i + 2];
            const float x = (a.x + b.x + c.x) / 3.0f;
            const float z = (a.z + b.z + c.z) / 3.0f;
            const float y = (a.y + b.y + c.y) / 3.0f;
            const float u = (a.u + b.u + c.u) / 3.0f;
            const float v = (a.v + b.v + c.v) / 3.0f;
            Sample s;
            if (!lookup(list, x, z, y, v, &s)) {
                ++missed;
                continue;
            }
            const double dy = std::fabs(s.y - y);
            const double du = std::fabs(s.u - u);
            const double dv = std::fabs(s.v - v);
            wy = std::max(wy, dy);
            wu = std::max(wu, du);
            wv = std::max(wv, dv);
            sumY2 += dy * dy;
            ++samples;
        }
        worstY = std::max(worstY, wy);
        worstU = std::max(worstU, wu);
        worstV = std::max(worstV, wv);
        std::printf("%-22s %8d %8d %7d %7d %9.5f %9.5f %9.5f\n", name.c_str(),
                    refTris, tris, refPkgs, pkgs, wy, wu, wv);
        totRefTri += refTris;
        totTri += tris;
        totPkg += pkgs;
        totRefPkg += refPkgs;
        totStrip += (long)strip.size();
        totRefStrip += (long)refStrip.size();
    }
    std::printf("%-22s %8ld %8ld %7ld %7ld %9.5f %9.5f %9.5f  crack %.5f\n",
                "TOTAL", totRefTri, totTri, totRefPkg, totPkg, worstY, worstU,
                worstV, worstCrack);
    std::printf("strip vertices %ld -> %ld (%.3fx)   triangles %.3fx   "
                "packages %.3fx   rmsdY %.6f   samples %ld missed %ld\n",
                totRefStrip, totStrip,
                totRefStrip ? (double)totStrip / (double)totRefStrip : 0.0,
                totRefTri ? (double)totTri / (double)totRefTri : 0.0,
                totRefPkg ? (double)totPkg / (double)totRefPkg : 0.0,
                samples ? std::sqrt(sumY2 / (double)samples) : 0.0, samples,
                missed);
    return 0;
}
