// Road traffic (docs/traffic.md): the lane graph. See roadlanes.hpp.
#include "roadlanes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "project.hpp"
#include "roadfurniture.hpp"

namespace roadlanes {
namespace {

struct P2 {
    float x, z;
};

// A road's centre line by arc length (the furniture's CentreLine, plus the
// nearest-arc projection the arm caps need).
class Centre {
public:
    explicit Centre(const std::vector<float>& pts) {
        closed_ = roadgen::isClosed(pts);
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
    bool closed() const { return closed_; }
    float length() const { return arc_.empty() ? 0.0f : arc_.back(); }
    void at(float s, float* x, float* z, float* tx, float* tz) const {
        *x = *z = 0.0f, *tx = 1.0f, *tz = 0.0f;
        if (xs_.size() < 2) return;
        const float L = length();
        if (closed_ && L > 0.0f) {
            s = std::fmod(s, L);
            if (s < 0.0f) s += L;
        }
        const size_t i = std::min(
            (size_t)(std::upper_bound(arc_.begin(), arc_.end(), s) - arc_.begin()),
            arc_.size() - 1);
        const size_t a = i == 0 ? 0 : i - 1, b = a + 1;
        const float span = arc_[b] - arc_[a];
        const float k = span > 0.0f ? std::clamp((s - arc_[a]) / span, 0.0f, 1.0f) : 0.0f;
        *x = xs_[a] + (xs_[b] - xs_[a]) * k;
        *z = zs_[a] + (zs_[b] - zs_[a]) * k;
        *tx = (xs_[b] - xs_[a]) / std::max(span, 1e-6f);
        *tz = (zs_[b] - zs_[a]) / std::max(span, 1e-6f);
    }
    // The arc of the centre-line point nearest (x, z).
    float nearest(float x, float z) const {
        float best = 1e30f, bs = 0.0f;
        for (size_t i = 0; i + 1 < xs_.size(); ++i) {
            const float dx = xs_[i + 1] - xs_[i], dz = zs_[i + 1] - zs_[i];
            const float l2 = dx * dx + dz * dz;
            float f = l2 > 0.0f ? ((x - xs_[i]) * dx + (z - zs_[i]) * dz) / l2 : 0.0f;
            f = std::clamp(f, 0.0f, 1.0f);
            const float ex = xs_[i] + dx * f - x, ez = zs_[i] + dz * f - z;
            const float d = ex * ex + ez * ez;
            if (d < best) {
                best = d;
                bs = arc_[i] + (arc_[i + 1] - arc_[i]) * f;
            }
        }
        return bs;
    }

private:
    std::vector<float> xs_, zs_, arc_;
    bool closed_ = false;
};

// Douglas-Peucker on an x, y, z polyline in the ground plane, keeping a point
// at least every `maxGap` units - straight lanes become a handful of points,
// curves keep theirs.
void simplify(std::vector<float>& pts, float tol, float maxGap) {
    const size_t n = pts.size() / 3;
    if (n <= 2) return;
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<size_t, size_t>> stack = {{0, n - 1}};
    while (!stack.empty()) {
        const auto [a, b] = stack.back();
        stack.pop_back();
        if (b <= a + 1) continue;
        const float ax = pts[a * 3], az = pts[a * 3 + 2];
        const float dx = pts[b * 3] - ax, dz = pts[b * 3 + 2] - az;
        const float l = std::hypot(dx, dz);
        float worst = -1.0f;
        size_t at = a;
        for (size_t i = a + 1; i < b; ++i) {
            const float px = pts[i * 3] - ax, pz = pts[i * 3 + 2] - az;
            const float d = l > 1e-6f ? std::fabs(px * dz - pz * dx) / l : std::hypot(px, pz);
            const float dy = std::fabs(pts[i * 3 + 1] - (pts[a * 3 + 1] + (pts[b * 3 + 1] - pts[a * 3 + 1]) *
                                                          ((float)(i - a) / (float)(b - a))));
            const float w = std::max(d, dy * 0.4f);
            if (w > worst) worst = w, at = i;
        }
        if (worst > tol || l > maxGap) {
            keep[at] = 1;
            stack.push_back({a, at});
            stack.push_back({at, b});
        }
    }
    std::vector<float> out;
    for (size_t i = 0; i < n; ++i)
        if (keep[i]) out.insert(out.end(), {pts[i * 3], pts[i * 3 + 1], pts[i * 3 + 2]});
    pts.swap(out);
}

float segDist2(P2 a, P2 b, P2 c, P2 d);
// Squared distance between two polylines (x, y, z).
float polyDist2(const std::vector<float>& p, const std::vector<float>& q) {
    float best = 1e30f;
    for (size_t i = 0; i + 1 < p.size() / 3; ++i)
        for (size_t j = 0; j + 1 < q.size() / 3; ++j) {
            const float d = segDist2({p[i * 3], p[i * 3 + 2]}, {p[i * 3 + 3], p[i * 3 + 5]},
                                     {q[j * 3], q[j * 3 + 2]}, {q[j * 3 + 3], q[j * 3 + 5]});
            if (d < best) best = d;
        }
    return best;
}
float ptSeg2(P2 p, P2 a, P2 b) {
    const float dx = b.x - a.x, dz = b.z - a.z;
    const float l2 = dx * dx + dz * dz;
    float f = l2 > 0.0f ? ((p.x - a.x) * dx + (p.z - a.z) * dz) / l2 : 0.0f;
    f = std::clamp(f, 0.0f, 1.0f);
    const float ex = a.x + dx * f - p.x, ez = a.z + dz * f - p.z;
    return ex * ex + ez * ez;
}
float segDist2(P2 a, P2 b, P2 c, P2 d) {
    auto cross = [](P2 o, P2 p, P2 q) { return (p.x - o.x) * (q.z - o.z) - (p.z - o.z) * (q.x - o.x); };
    const float d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
    if (((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0))) return 0.0f;
    return std::min(std::min(ptSeg2(a, c, d), ptSeg2(b, c, d)),
                    std::min(ptSeg2(c, a, b), ptSeg2(d, a, b)));
}

// One end of a lane stretch along a road: a node arm, or the road's own end.
struct Cut {
    float s = 0.0f;
    int node = -1, arm = -1;
};

}  // namespace

int lanesPerDirection(const roadgen::CrossingRoad& r) {
    if (r.lanes >= 2) return std::max(1, r.lanes / 2);
    if (r.lanes == 1) return 1;
    return std::clamp((int)(r.width / 7.0f), 1, 3);
}

Graph build(const std::vector<roadgen::CrossingRoad>& roads, const roadgen::CrossingPlan& plan,
            const std::vector<bool>& signalled, const roadgen::HeightFn& ground,
            const Options& opt) {
    Graph g;
    const float side = opt.leftHand ? -1.0f : 1.0f;
    // Lanes keep to this side of the direction of travel (dx, dz): right in
    // right-hand traffic - the side bakeMarkings paints a stop line on.
    auto sideOf = [&](float dx, float dz) { return P2{dz * side, -dx * side}; };
    auto heightAt = [&](const roadgen::CrossingRoad& r, float x, float z) {
        float y = ground ? ground(x, z) : 0.0f;
        if (!(y > -1.0e5f)) y = 0.0f;  // a scene without terrain glues to 0
        y += roadgen::rankLift(r.rank) + roadgen::kLift;
        if (r.elevation) y += r.elevation(x, z);
        return y;
    };

    g.nodes.resize(plan.crossings.size());
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        const roadgen::Crossing& c = plan.crossings[ci];
        NodeInfo& n = g.nodes[ci];
        n.crossing = (int)ci;
        n.x = c.shape.x;
        n.z = c.shape.z;
        n.signalled = ci < signalled.size() && signalled[ci];
        n.arms = c.arms;
        n.phases = n.signalled ? roadfurn::signalPhases((int)c.armList.size()) : 0;
    }
    auto usableNode = [&](const roadgen::Crossing& c) { return !c.patchDuplicate; };

    // 1. Lanes: every road (not a railway) is cut at its node arms' caps into
    // stretches, and every stretch carries lanes in both directions.
    struct RoadEnd {
        int road;
        bool atStart;  // the road's first point (else its last)
        P2 pos, out;   // where, and the road's direction leaving the stretch
    };
    std::vector<RoadEnd> openEnds;
    // Per lane stretch end, which lanes arrive / leave there (for joins and
    // turns back): keyed by (road, atStart).
    std::vector<std::vector<int>> laneArrive(roads.size() * 2), laneLeave(roads.size() * 2);
    for (size_t ri = 0; ri < roads.size(); ++ri) {
        const roadgen::CrossingRoad& R = roads[ri];
        if (R.kind == 1 || R.points.size() < 4) continue;  // railways are not driven
        const Centre cl(R.points);
        const float L = cl.length();
        if (L < 1.0f) continue;
        // The nodes along this road, in order: each has its centre's arc and
        // the caps of its arms on either side (a road ending at the node has
        // an arm on one side only). The lanes run from one node's far cap to
        // the next node's near cap. Two nodes close enough for their patches
        // to overlap leave a stretch of length zero - the lanes still join
        // through it, which is what keeps a short link between two junctions
        // driven.
        struct Pass {
            float centre = 0.0f;
            Cut lo, hi;  // the caps below and above the centre (node -1 = none)
        };
        std::vector<Pass> passes;
        auto wrapDiff = [&](float s, float c) {
            float d = s - c;
            if (cl.closed()) {
                if (d > 0.5f * L) d -= L;
                if (d < -0.5f * L) d += L;
            }
            return d;
        };
        for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
            const roadgen::Crossing& c = plan.crossings[ci];
            if (!usableNode(c) || !c.has((int)ri)) continue;
            Pass ps;
            ps.centre = cl.nearest(c.shape.x, c.shape.z);
            ps.lo.node = ps.hi.node = -1;
            for (size_t ai = 0; ai < c.armList.size(); ++ai) {
                const roadgen::NodeArm& arm = c.armList[ai];
                if (arm.road != (int)ri) continue;
                const float s = cl.nearest(arm.capX, arm.capZ);
                const float d = wrapDiff(s, ps.centre);
                Cut& cut = d < 0.0f ? ps.lo : ps.hi;
                cut.s = ps.centre + d;
                cut.node = (int)ci;
                cut.arm = (int)ai;
            }
            if (ps.lo.node < 0 && ps.hi.node < 0) continue;
            passes.push_back(ps);
        }
        std::sort(passes.begin(), passes.end(),
                  [](const Pass& x, const Pass& y) { return x.centre < y.centre; });
        std::vector<std::pair<Cut, Cut>> stretches;
        auto stretch = [&](Cut from, Cut to) {
            if (to.s < from.s) {  // overlapping patches: meet in the middle
                const float m = 0.5f * (from.s + to.s);
                from.s = to.s = m;
            }
            stretches.push_back({from, to});
        };
        if (cl.closed() && passes.empty()) {
            stretches.push_back({{0.0f, -2, -1}, {L, -2, -1}});  // a loop with no node
        } else if (cl.closed()) {
            for (size_t k = 0; k < passes.size(); ++k) {
                const Pass& p0 = passes[k];
                const Pass& p1 = passes[(k + 1) % passes.size()];
                if (p0.hi.node < 0 || p1.lo.node < 0) continue;
                Cut to = p1.lo;
                if (to.s < p0.hi.s - 1e-3f || k + 1 == passes.size()) to.s += L;
                if (to.s - p0.hi.s > L) to.s -= L;
                stretch(p0.hi, to);
            }
        } else {
            Cut start{0.0f, -1, -1};
            bool open = true;  // the road's start is out on its own
            for (const Pass& ps : passes) {
                if (ps.lo.node >= 0 && open) stretch(start, ps.lo);
                open = ps.hi.node >= 0;  // else the road ends inside this node
                start = ps.hi;
            }
            if (open) stretch(start, Cut{L, -1, -1});
        }
        const int per = lanesPerDirection(R);
        const float h = 0.5f * R.width;
        const float lw = h / (float)per;
        for (const auto& st : stretches) {
            float s0 = st.first.s, s1 = st.second.s;
            if (s1 - s0 < 0.1f) {
                // Two nodes' patches touch: a lane a tenth of a unit long
                // carries the direction through.
                const float m = 0.5f * (s0 + s1);
                s0 = m - 0.05f;
                s1 = m + 0.05f;
            }
            for (int dir = 1; dir >= -1; dir -= 2) {
                const int firstLane = (int)g.lanes.size();
                for (int k = 0; k < per; ++k) {
                    Lane ln;
                    ln.road = (int)ri;
                    ln.dir = dir;
                    ln.index = k;
                    ln.perDir = per;
                    ln.offset = h - lw * ((float)k + 0.5f);
                    const Cut& a = dir > 0 ? st.first : st.second;
                    const Cut& b = dir > 0 ? st.second : st.first;
                    ln.fromNode = a.node >= 0 ? a.node : -1;
                    ln.fromArm = a.arm;
                    ln.toNode = b.node >= 0 ? b.node : -1;
                    ln.toArm = b.arm;
                    const float len = s1 - s0;
                    const int steps = len < 1.0f ? 1 : std::max(2, (int)std::ceil(len / 1.0f));
                    for (int i = 0; i <= steps; ++i) {
                        const float f = (float)i / (float)steps;
                        const float s = dir > 0 ? s0 + len * f : s1 - len * f;
                        float x, z, tx, tz;
                        cl.at(s, &x, &z, &tx, &tz);
                        const P2 sd = sideOf(tx * (float)dir, tz * (float)dir);
                        x += sd.x * ln.offset;
                        z += sd.z * ln.offset;
                        ln.pts.insert(ln.pts.end(), {x, heightAt(R, x, z), z});
                    }
                    simplify(ln.pts, 0.25f, 24.0f);  // 0.25 across, 0.6 in height
                    const int li = (int)g.lanes.size();
                    g.lanes.push_back(std::move(ln));
                    // A loop with no node, or an open end: remember it for the
                    // join / turn-back pass.
                    if (st.first.node == -2) {
                        laneArrive[ri * 2 + 1].push_back(li);  // arrives at "the end"
                        laneLeave[ri * 2 + 1].push_back(li);   // ... which is its own start
                    } else {
                        if (b.node == -1) laneArrive[ri * 2 + (dir > 0 ? 1 : 0)].push_back(li);
                        if (a.node == -1) laneLeave[ri * 2 + (dir > 0 ? 0 : 1)].push_back(li);
                    }
                }
                // The lanes of one direction side by side: what a lane change
                // moves between (index 0 at the kerb).
                for (int k = 0; k < per; ++k) {
                    Lane& l = g.lanes[(size_t)(firstLane + k)];
                    l.outer = k > 0 ? firstLane + k - 1 : -1;
                    l.inner = k + 1 < per ? firstLane + k + 1 : -1;
                }
            }
            for (int e = 0; e < 2; ++e) {
                const Cut& cut = e == 0 ? st.first : st.second;
                if (cut.node != -1 || cl.closed()) continue;
                float x, z, tx, tz;
                cl.at(cut.s, &x, &z, &tx, &tz);
                openEnds.push_back({(int)ri, e == 0, {x, z}, {e == 0 ? -tx : tx, e == 0 ? -tz : tz}});
            }
        }
    }
    g.laneOut.assign(g.lanes.size(), {});

    auto dirAtEnd = [&](const Lane& l, bool end) {
        const std::vector<float>& p = l.pts;
        const size_t n = p.size() / 3;
        const size_t a = end ? n - 2 : 0, b = end ? n - 1 : 1;
        const float dx = p[b * 3] - p[a * 3], dz = p[b * 3 + 2] - p[a * 3 + 2];
        const float d = std::hypot(dx, dz);
        return P2{dx / std::max(d, 1e-6f), dz / std::max(d, 1e-6f)};
    };
    auto endPt = [](const Lane& l, bool end, float* o) {
        const size_t i = end ? l.pts.size() - 3 : 0;
        o[0] = l.pts[i], o[1] = l.pts[i + 1], o[2] = l.pts[i + 2];
    };
    // A cubic Bezier from the end of lane `from` to the start of `to`,
    // tangent to both.
    auto curve = [&](int from, int to, float tension) {
        float a[3], b[3];
        endPt(g.lanes[(size_t)from], true, a);
        endPt(g.lanes[(size_t)to], false, b);
        const P2 da = dirAtEnd(g.lanes[(size_t)from], true);
        const P2 db = dirAtEnd(g.lanes[(size_t)to], false);
        const float dist = std::hypot(b[0] - a[0], b[2] - a[2]);
        const float k = tension * dist;
        const float c1[2] = {a[0] + da.x * k, a[2] + da.z * k};
        const float c2[2] = {b[0] - db.x * k, b[2] - db.z * k};
        const int n = std::max(3, (int)std::ceil(dist / 1.0f));
        std::vector<float> pts;
        auto bare = [&](float x, float z) {
            const float y = ground ? ground(x, z) : 0.0f;
            return y > -1.0e5f ? y : 0.0f;
        };
        // Height over the bare ground, blended from one lane's to the other's.
        const float ga = a[1] - bare(a[0], a[2]);
        const float gb = b[1] - bare(b[0], b[2]);
        for (int i = 0; i <= n; ++i) {
            const float t = (float)i / (float)n, u = 1.0f - t;
            const float x = u * u * u * a[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t * t * t * b[0];
            const float z = u * u * u * a[2] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t * t * t * b[2];
            pts.insert(pts.end(), {x, bare(x, z) + ga + (gb - ga) * t, z});
        }
        if (dist < 0.05f) pts = {a[0], a[1], a[2], b[0], b[1], b[2]};
        else simplify(pts, 0.2f, 8.0f);
        return pts;
    };
    auto turnOf = [&](P2 din, P2 dout) {
        const P2 sd = sideOf(din.x, din.z);
        const float dot = din.x * dout.x + din.z * dout.z;
        if (dot < -0.866f) return (int)kBack;
        const float c = dout.x * sd.x + dout.z * sd.z;
        if (c > 0.5f) return (int)kNearSide;
        if (c < -0.5f) return (int)kFarSide;
        return (int)kStraight;
    };

    // 2. Connections through every node.
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci) {
        const roadgen::Crossing& c = plan.crossings[ci];
        if (!usableNode(c)) continue;
        const std::vector<unsigned char> yields = roadgen::giveWayArms(c, roads);
        // Per arm: the lanes arriving at it and leaving from it, by index.
        const size_t na = c.armList.size();
        std::vector<std::vector<int>> in(na), out(na);
        for (size_t li = 0; li < g.lanes.size(); ++li) {
            const Lane& l = g.lanes[li];
            if (l.toNode == (int)ci && l.toArm >= 0) in[(size_t)l.toArm].push_back((int)li);
            if (l.fromNode == (int)ci && l.fromArm >= 0) out[(size_t)l.fromArm].push_back((int)li);
        }
        auto byIndex = [&](std::vector<int>& v) {
            std::sort(v.begin(), v.end(), [&](int a, int b) {
                return g.lanes[(size_t)a].index < g.lanes[(size_t)b].index;
            });
        };
        for (auto& v : in) byIndex(v);
        for (auto& v : out) byIndex(v);
        const size_t firstConn = g.conns.size();
        auto add = [&](int from, int to, int arm, int turn) {
            for (int k : g.laneOut[(size_t)from])
                if (g.conns[(size_t)k].to == to) return;
            Connection cn;
            cn.node = (int)ci;
            cn.from = from;
            cn.to = to;
            cn.arm = arm;
            cn.turn = turn;
            cn.givesWay = yields[(size_t)arm] != 0;
            cn.rank = (cn.givesWay ? 0 : 2) + (turn == kFarSide ? 0 : 1);
            // A signalled node's phase for this approach: opposite arms
            // together at a four-way node, one arm at a time anywhere else.
            cn.group = g.nodes[ci].signalled ? roadfurn::signalPhase((int)na, arm) : -1;
            cn.pts = curve(from, to, 0.4f);
            g.laneOut[(size_t)from].push_back((int)g.conns.size());
            g.conns.push_back(std::move(cn));
        };
        for (size_t A = 0; A < na; ++A) {
            if (in[A].empty()) continue;
            const int nIn = (int)in[A].size();
            for (size_t B = 0; B < na; ++B) {
                if (B == A || out[B].empty()) continue;
                const int nOut = (int)out[B].size();
                const P2 din = dirAtEnd(g.lanes[(size_t)in[A][0]], true);
                const P2 dout = dirAtEnd(g.lanes[(size_t)out[B][0]], false);
                const int turn = turnOf(din, dout);
                if (turn == kBack) continue;  // no turning back inside a node
                if (turn == kStraight) {
                    for (int k = 0; k < nIn; ++k) add(in[A][(size_t)k], out[B][(size_t)std::min(k, nOut - 1)], (int)A, turn);
                } else if (turn == kNearSide) {
                    add(in[A][0], out[B][0], (int)A, turn);
                } else {
                    add(in[A][(size_t)(nIn - 1)], out[B][(size_t)(nOut - 1)], (int)A, turn);
                }
            }
            // A lane the rules gave no exit (the middle lane of three at a T)
            // takes the nearest lane of every way out.
            for (int k = 0; k < nIn; ++k) {
                const int li = in[A][(size_t)k];
                if (!g.laneOut[(size_t)li].empty()) continue;
                for (size_t B = 0; B < na; ++B) {
                    if (B == A || out[B].empty()) continue;
                    const P2 din = dirAtEnd(g.lanes[(size_t)li], true);
                    const P2 dout = dirAtEnd(g.lanes[(size_t)out[B][0]], false);
                    const int turn = turnOf(din, dout);
                    if (turn == kBack) continue;
                    add(li, out[B][(size_t)std::min(k, (int)out[B].size() - 1)], (int)A, turn);
                }
                if (g.laneOut[(size_t)li].empty())
                    g.warnings.push_back("node " + std::to_string(ci) + ": a lane of road " +
                                         roads[(size_t)g.lanes[(size_t)li].road].id +
                                         " arrives with no legal exit");
            }
        }
        // Conflicts: two connections of the node whose paths cross or come
        // within a car's width, unless they leave the same lane (a queue).
        for (size_t i = firstConn; i < g.conns.size(); ++i)
            for (size_t j = i + 1; j < g.conns.size(); ++j) {
                Connection& a = g.conns[i];
                Connection& b = g.conns[j];
                if (a.from == b.from) continue;
                const bool clash = a.to == b.to || polyDist2(a.pts, b.pts) < 2.2f * 2.2f;
                if (!clash) continue;
                a.conflicts.push_back((int)j);
                b.conflicts.push_back((int)i);
            }
    }

    // 3. Open ends: two roads joined end to end (no node) carry on into each
    // other; any other end is a dead end, and its lanes turn back.
    std::vector<char> joined(openEnds.size(), 0);
    for (size_t i = 0; i < openEnds.size(); ++i)
        for (size_t j = i + 1; j < openEnds.size() && !joined[i]; ++j) {
            if (joined[j] || openEnds[i].road == openEnds[j].road) continue;
            const RoadEnd& a = openEnds[i];
            const RoadEnd& b = openEnds[j];
            const float reach = 0.5f * std::max(roads[(size_t)a.road].width, roads[(size_t)b.road].width) + 1.0f;
            if (std::hypot(a.pos.x - b.pos.x, a.pos.z - b.pos.z) > reach) continue;
            if (a.out.x * b.out.x + a.out.z * b.out.z > -0.5f) continue;  // not facing each other
            joined[i] = joined[j] = 1;
            const std::vector<int>& aIn = laneArrive[(size_t)a.road * 2 + (a.atStart ? 0 : 1)];
            const std::vector<int>& bOut = laneLeave[(size_t)b.road * 2 + (b.atStart ? 0 : 1)];
            const std::vector<int>& bIn = laneArrive[(size_t)b.road * 2 + (b.atStart ? 0 : 1)];
            const std::vector<int>& aOut = laneLeave[(size_t)a.road * 2 + (a.atStart ? 0 : 1)];
            auto join = [&](const std::vector<int>& from, const std::vector<int>& to) {
                for (int f : from) {
                    int best = -1;
                    for (int t : to)
                        if (best < 0 || std::abs(g.lanes[(size_t)t].index - g.lanes[(size_t)f].index) <
                                            std::abs(g.lanes[(size_t)best].index - g.lanes[(size_t)f].index))
                            best = t;
                    if (best < 0) continue;
                    Connection cn;
                    cn.from = f;
                    cn.to = best;
                    cn.rank = 3;
                    cn.pts = curve(f, best, 0.33f);
                    g.laneOut[(size_t)f].push_back((int)g.conns.size());
                    g.conns.push_back(std::move(cn));
                }
            };
            join(aIn, bOut);
            join(bIn, aOut);
        }
    for (size_t i = 0; i < openEnds.size(); ++i) {
        if (joined[i]) continue;
        const RoadEnd& e = openEnds[i];
        const std::vector<int>& arrive = laneArrive[(size_t)e.road * 2 + (e.atStart ? 0 : 1)];
        if (arrive.empty()) continue;
        // A dead end: its lanes go nowhere. A car on one stops at the end and
        // is recycled once nobody is looking (a street a lane wide has no
        // room for a car to turn round in, and a tight U-turn is where two
        // cars ended up inside each other in the host simulation).
        ++g.deadEnds;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "road %s: dead end at (%.1f, %.1f)",
                      roads[(size_t)e.road].id.c_str(), e.pos.x, e.pos.z);
        g.warnings.push_back(buf);
    }
    // A loop with no node: each lane carries on into itself.
    for (size_t ri = 0; ri < roads.size(); ++ri) {
        const std::vector<int>& arrive = laneArrive[ri * 2 + 1];
        const std::vector<int>& leave = laneLeave[ri * 2 + 1];
        if (arrive.empty() || arrive != leave) continue;
        for (int f : arrive) {
            if (!g.laneOut[(size_t)f].empty()) continue;
            Connection cn;
            cn.from = f;
            cn.to = f;
            cn.rank = 3;
            cn.pts = curve(f, f, 0.33f);
            g.laneOut[(size_t)f].push_back((int)g.conns.size());
            g.conns.push_back(std::move(cn));
        }
    }
    for (size_t li = 0; li < g.lanes.size(); ++li)
        if (g.laneOut[li].empty() && g.lanes[li].toNode >= 0)
            g.warnings.push_back("lane " + std::to_string(li) + " (road " +
                                 roads[(size_t)g.lanes[li].road].id + ") ends at node " +
                                 std::to_string(g.lanes[li].toNode) + " with no legal exit");
    return g;
}

namespace {
roadgen::HeightFn sceneGround(const SceneData& sc) {
    return [&sc](float x, float z) {
        return sc.terrain.enabled ? roadgen::terrainHeight(sc.heights, sc.hmW, sc.hmD,
                                                           (float)sc.terrain.width,
                                                           (float)sc.terrain.depth, x, z)
                                  : -1000000.0f;
    };
}
}  // namespace

Graph buildScene(const Project& p, int scene) {
    const SceneData& sc = p.scenes[(size_t)scene];
    const roadgen::HeightFn ground = sceneGround(sc);
    std::vector<int> idx;
    const std::vector<roadgen::CrossingRoad> roads =
        project::crossingRoads(sc.objects, &idx, p.dir, ground);
    const roadgen::CrossingPlan plan = roadgen::planCrossings(roads, sc.roadJunctions, false);
    std::vector<roadfurn::Settings> fs;
    for (int oi : idx) fs.push_back(sc.objects[(size_t)oi].roadFurniture);
    std::vector<bool> sig(plan.crossings.size(), false);
    for (size_t ci = 0; ci < plan.crossings.size(); ++ci)
        sig[ci] = roadfurn::nodeSignalled(plan.crossings[ci], roads, fs);
    Options opt;
    opt.leftHand = p.settings.traffic.leftHand;
    opt.speed = p.settings.traffic.speed;
    return build(roads, plan, sig, ground, opt);
}

void toSim(const Graph& g, const Options& opt, float green, float amber, float allRed,
           TfGraph& out) {
    out = TfGraph();
    out.green = green;
    out.amber = amber;
    out.allRed = allRed;
    out.speed = opt.speed;
    const int L = (int)g.lanes.size();
    auto addPts = [&](const std::vector<float>& p, TfSeg& s) {
        s.first = (int)(out.ownPts.size() / 3);
        s.count = (int)(p.size() / 3);
        out.ownPts.insert(out.ownPts.end(), p.begin(), p.end());
    };
    for (int li = 0; li < L; ++li) {
        TfSeg s;
        addPts(g.lanes[(size_t)li].pts, s);
        s.kind = 0;
        s.inner = g.lanes[(size_t)li].inner;
        s.outer = g.lanes[(size_t)li].outer;
        s.nextFirst = (int)out.next.size();
        for (int k : g.laneOut[(size_t)li]) out.next.push_back(L + k);
        s.nextCount = (int)g.laneOut[(size_t)li].size();
        out.segs.push_back(s);
    }
    for (size_t ci = 0; ci < g.conns.size(); ++ci) {
        const Connection& c = g.conns[ci];
        TfSeg s;
        addPts(c.pts, s);
        s.kind = 1;
        s.node = c.node;
        s.group = c.group;
        s.rank = c.rank;
        s.turn = c.turn;
        s.nextFirst = (int)out.next.size();
        out.next.push_back(c.to);
        s.nextCount = 1;
        s.confFirst = (int)out.conf.size();
        for (int k : c.conflicts) out.conf.push_back(L + k);
        s.confCount = (int)c.conflicts.size();
        out.segs.push_back(s);
    }
    for (size_t ni = 0; ni < g.nodes.size(); ++ni) {
        TfNode n;
        n.signal = g.nodes[ni].signalled ? 1 : 0;
        n.phases = g.nodes[ni].signalled ? g.nodes[ni].phases : 2;
        // Neighbouring signals do not change together (a fixed, per-node
        // phase offset; no green wave).
        const float cyc = (float)(n.phases > 1 ? n.phases : 2) * (green + amber + allRed);
        n.offset = std::fmod((float)ni * 7.31f, cyc > 0.0f ? cyc : 1.0f);
        out.nodes.push_back(n);
    }
    out.use();
    out.finish(3.0f);
}

std::string describe(const Graph& g, const std::vector<roadgen::CrossingRoad>& roads) {
    std::ostringstream o;
    char b[256];
    float total = 0.0f;
    for (const Lane& l : g.lanes)
        for (size_t i = 3; i < l.pts.size(); i += 3)
            total += std::hypot(l.pts[i] - l.pts[i - 3], l.pts[i + 2] - l.pts[i - 1]);
    size_t lanePts = 0, connPts = 0;
    for (const Lane& l : g.lanes) lanePts += l.pts.size() / 3;
    for (const Connection& c : g.conns) connPts += c.pts.size() / 3;
    std::snprintf(b, sizeof(b),
                  "  %zu lanes (%.0f units, %zu points), %zu connections (%zu points), %d dead end(s)\n",
                  g.lanes.size(), total, lanePts, g.conns.size(), connPts, g.deadEnds);
    o << b;
    for (size_t ri = 0; ri < roads.size(); ++ri) {
        int n = 0, per = 0;
        for (const Lane& l : g.lanes)
            if (l.road == (int)ri) ++n, per = l.perDir;
        if (n == 0) continue;
        std::snprintf(b, sizeof(b), "  road %s: %d lane stretch(es), %d per direction\n",
                      roads[ri].id.c_str(), n, per);
        o << b;
    }
    static const char* kTurn[] = {"straight", "near-side", "far-side", "back"};
    for (const NodeInfo& n : g.nodes) {
        int conns = 0;
        for (const Connection& c : g.conns) conns += c.node == n.crossing;
        if (conns == 0) continue;
        char ctl[48];
        if (n.signalled) std::snprintf(ctl, sizeof(ctl), "traffic lights (%d phases)", n.phases);
        else std::snprintf(ctl, sizeof(ctl), "priority");
        std::snprintf(b, sizeof(b), "  node %d at (%.1f, %.1f): %d arms, %s, %d connections\n",
                      n.crossing, n.x, n.z, n.arms, ctl, conns);
        o << b;
        for (const Connection& c : g.conns) {
            if (c.node != n.crossing) continue;
            const Lane& f = g.lanes[(size_t)c.from];
            const Lane& t = g.lanes[(size_t)c.to];
            std::snprintf(b, sizeof(b),
                          "    arm %d: %s lane %d -> %s lane %d, %s, %s rank %d%s, crosses %zu\n",
                          c.arm, roads[(size_t)f.road].id.c_str(), f.index,
                          roads[(size_t)t.road].id.c_str(), t.index, kTurn[c.turn],
                          c.givesWay ? "gives way" : "priority", c.rank,
                          c.group >= 0 ? (c.group == 0 ? ", phase A" : c.group == 1 ? ", phase B"
                                                    : c.group == 2 ? ", phase C" : ", phase D+")
                                       : "",
                          c.conflicts.size());
            o << b;
        }
    }
    for (const std::string& w : g.warnings) o << "  warning: " << w << "\n";
    return o.str();
}

}  // namespace roadlanes
