// The Draw road tool's decisions (docs/roads.md "Drawing roads"). See
// roaddraw.hpp - the viewport, the --draw-road CLI, the AI tool and
// --vehicle-check all call these.
#include "roaddraw.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include "json.hpp"
#include "project.hpp"
#include "roadbridge.hpp"
#include "roadpresets.hpp"
#include "roadrail.hpp"

namespace roaddraw {
namespace {

constexpr float kPi = 3.14159265358979f;
// Consecutive points closer than this are one point (a double-click's two
// presses, a click on the point just placed).
constexpr float kDuplicate = 0.25f;
// A drawing continues a road it starts on when it leaves the road's end within
// this angle of the road's own direction there.
constexpr float kExtendCos = 0.7071f;  // 45 degrees

float len2(float x, float z) { return x * x + z * z; }

// Rotates the unit vector (rx, rz) by `deg` (counter-clockwise seen from +Y
// is x -> z here: the same sense atan2(z, x) measures).
void rotate(float rx, float rz, float deg, float* ox, float* oz) {
    const float a = deg * kPi / 180.0f, c = std::cos(a), s = std::sin(a);
    *ox = rx * c - rz * s;
    *oz = rx * s + rz * c;
}

// The direction (dx, dz) snapped to a whole number of `step` degrees from the
// reference (rx, rz). Returns the step count x step.
float snapAngle(float dx, float dz, float rx, float rz, float step, float* ox, float* oz) {
    const float rl = std::sqrt(len2(rx, rz));
    if (!(rl > 1e-6f)) rx = 1.0f, rz = 0.0f;
    else rx /= rl, rz /= rl;
    const float rel = std::atan2(rx * dz - rz * dx, rx * dx + rz * dz) * 180.0f / kPi;
    const float st = std::max(1.0f, step);
    const float snapped = std::round(rel / st) * st;
    rotate(rx, rz, snapped, ox, oz);
    return snapped;
}

}  // namespace

const char* kindName(int kind) {
    switch (kind) {
        case kGrid: return "grid";
        case kAngle: return "angle";
        case kEnd: return "road end";
        case kCentre: return "road centre";
        case kClose: return "loop";
        default: return "free";
    }
}

Snapper::Snapper(std::vector<roadgen::CrossingRoad> roads) : roads_(std::move(roads)) {
    lines_.resize(roads_.size());
    for (size_t r = 0; r < roads_.size(); ++r) {
        const std::vector<float>& pts = roads_[r].points;
        Line& l = lines_[r];
        l.halfW = 0.5f * std::max(0.1f, roads_[r].width);
        l.closed = roadgen::isClosed(pts);
        const int n = (int)(pts.size() / 2);
        if (n < 2) continue;
        // roadgen's centreLine: kSampleStep pieces per segment, through the
        // same Catmull-Rom sample (splineAt is that sample over the whole
        // polyline, t = segment index / (n - 1)).
        for (int seg = 0; seg < n - 1; ++seg) {
            const float ax = pts[(size_t)seg * 2], az = pts[(size_t)seg * 2 + 1];
            const float bx = pts[(size_t)seg * 2 + 2], bz = pts[(size_t)seg * 2 + 3];
            const float len = std::hypot(bx - ax, bz - az);
            const int steps = len > roadgen::kSampleStep ? (int)(len / roadgen::kSampleStep) + 1 : 1;
            for (int k = seg == 0 ? 0 : 1; k <= steps; ++k) {
                float x = 0.0f, z = 0.0f;
                if (k == steps) {
                    x = bx, z = bz;  // the spline passes through its controls
                } else if (k == 0) {
                    x = ax, z = az;
                } else {
                    roadgen::splineAt(pts, ((float)seg + (float)k / (float)steps) / (float)(n - 1),
                                      &x, &z);
                }
                l.xz.push_back(x);
                l.xz.push_back(z);
            }
        }
    }
}

float Snapper::nearest(int r, float x, float z, float* px, float* pz, float* tx, float* tz) const {
    const std::vector<float>& p = lines_[(size_t)r].xz;
    float best = 1e30f;
    for (size_t i = 0; i + 3 < p.size(); i += 2) {
        const float ax = p[i], az = p[i + 1];
        const float dx = p[i + 2] - ax, dz = p[i + 3] - az;
        const float l2 = len2(dx, dz);
        if (!(l2 > 1e-12f)) continue;
        const float t = std::clamp(((x - ax) * dx + (z - az) * dz) / l2, 0.0f, 1.0f);
        const float qx = ax + dx * t, qz = az + dz * t;
        const float d = len2(qx - x, qz - z);
        if (d < best) {
            best = d;
            *px = qx;
            *pz = qz;
            const float l = std::sqrt(l2);
            *tx = dx / l;
            *tz = dz / l;
        }
    }
    return std::sqrt(best);
}

Snap Snapper::resolve(const std::vector<Snap>& placed, float cx, float cz, const Options& o) const {
    Snap out;
    out.x = cx;
    out.z = cz;
    const Snap* last = placed.empty() ? nullptr : &placed.back();

    // 0. Back on the drawing's own first point: a closed loop.
    if (placed.size() >= 3 &&
        std::hypot(cx - placed[0].x, cz - placed[0].z) <= std::max(1.5f, o.tolerance)) {
        out.kind = kClose;
        out.x = placed[0].x;
        out.z = placed[0].z;
        return out;
    }

    // The reference an angle is measured from: the previous segment, else the
    // road the drawing started on, else world +X.
    float refX = 1.0f, refZ = 0.0f;
    if (placed.size() >= 2) {
        refX = placed.back().x - placed[placed.size() - 2].x;
        refZ = placed.back().z - placed[placed.size() - 2].z;
    } else if (last && last->road >= 0) {
        refX = last->tx;
        refZ = last->tz;
    }

    if (o.roadSnap) {
        // 1. An open END of a road: exactly on it (a corner, or a road carried
        // on). Ends win over centre lines - near an end, both are in reach.
        int bestRoad = -1, bestEnd = -1;
        float bestD = 1e30f;
        for (int r = 0; r < (int)roads_.size(); ++r) {
            const Line& l = lines_[(size_t)r];
            if (l.closed || l.xz.size() < 4) continue;
            const std::vector<float>& pts = roads_[(size_t)r].points;
            for (int e = 0; e < 2; ++e) {
                const float ex = e == 0 ? pts[0] : pts[pts.size() - 2];
                const float ez = e == 0 ? pts[1] : pts.back();
                const float d = std::hypot(cx - ex, cz - ez);
                if (d <= l.halfW + o.tolerance && d < bestD) {
                    // Not the end the drawing is already standing on.
                    if (last && last->kind == kEnd && last->road == r && last->end == e) continue;
                    bestD = d, bestRoad = r, bestEnd = e;
                }
            }
        }
        if (bestRoad >= 0) {
            const std::vector<float>& xz = lines_[(size_t)bestRoad].xz;
            const size_t m = xz.size();
            out.kind = kEnd;
            out.road = bestRoad;
            out.end = bestEnd;
            out.x = bestEnd == 0 ? roads_[(size_t)bestRoad].points[0]
                                 : roads_[(size_t)bestRoad].points[roads_[(size_t)bestRoad].points.size() - 2];
            out.z = bestEnd == 0 ? roads_[(size_t)bestRoad].points[1]
                                 : roads_[(size_t)bestRoad].points.back();
            // Outward: away from the road's body.
            float tx = bestEnd == 0 ? xz[0] - xz[2] : xz[m - 2] - xz[m - 4];
            float tz = bestEnd == 0 ? xz[1] - xz[3] : xz[m - 1] - xz[m - 3];
            const float tl = std::sqrt(len2(tx, tz));
            out.tx = tl > 1e-6f ? tx / tl : 1.0f;
            out.tz = tl > 1e-6f ? tz / tl : 0.0f;
            return out;
        }

        // 2. A centre line: a T when the drawing ends here, a crossing when it
        // carries on. The road the drawing is standing on is skipped, so the
        // next point can leave it.
        int cr = -1;
        float cd = 1e30f, cpx = 0, cpz = 0, ctx = 1, ctz = 0;
        for (int r = 0; r < (int)roads_.size(); ++r) {
            if (lines_[(size_t)r].xz.size() < 4) continue;
            if (last && last->road == r) continue;
            float px, pz, tx, tz;
            const float d = nearest(r, cx, cz, &px, &pz, &tx, &tz);
            if (d <= lines_[(size_t)r].halfW + o.tolerance && d < cd)
                cd = d, cr = r, cpx = px, cpz = pz, ctx = tx, ctz = tz;
        }
        if (cr >= 0) {
            out.kind = kCentre;
            out.road = cr;
            out.x = cpx;
            out.z = cpz;
            out.tx = ctx;
            out.tz = ctz;
            // With an angle step, aim from the last point at a whole step from
            // THIS road and land where that ray meets its centre line, so a
            // 90-degree T or crossing is exact.
            if (o.angleSnap && last) {
                float dx, dz;
                const float ang = snapAngle(cx - last->x, cz - last->z, ctx, ctz, o.angleStep, &dx, &dz);
                const std::vector<float>& p = lines_[(size_t)cr].xz;
                float bestT = 1e30f, hx = 0, hz = 0;
                bool hit = false;
                for (size_t i = 0; i + 3 < p.size(); i += 2) {
                    const float ax = p[i], az = p[i + 1];
                    const float ex = p[i + 2] - ax, ez = p[i + 3] - az;
                    const float den = dx * ez - dz * ex;
                    if (std::fabs(den) < 1e-9f) continue;
                    const float qx = ax - last->x, qz = az - last->z;
                    const float t = (qx * ez - qz * ex) / den;   // along the ray
                    const float u = (qx * dz - qz * dx) / den;   // along the piece
                    if (t <= 0.0f || u < -1e-4f || u > 1.0001f) continue;
                    const float ix = last->x + dx * t, iz = last->z + dz * t;
                    const float dc = std::hypot(ix - cx, iz - cz);
                    if (dc < bestT) bestT = dc, hx = ix, hz = iz, hit = true;
                }
                if (hit && bestT <= lines_[(size_t)cr].halfW + o.tolerance) {
                    out.x = hx;
                    out.z = hz;
                    out.angled = true;
                    out.angle = ang;
                }
            }
            return out;
        }
    }

    // 3. Free: an angle step from the reference, then the grid.
    if (o.angleSnap && last) {
        const float dx = cx - last->x, dz = cz - last->z;
        float l = std::sqrt(len2(dx, dz));
        if (l > 1e-4f) {
            float ux, uz;
            out.angle = snapAngle(dx, dz, refX, refZ, o.angleStep, &ux, &uz);
            // The step's own direction keeps the cursor's distance along it.
            l = std::max(0.0f, ux * dx + uz * dz);
            if (o.gridSnap && o.grid > 0.0f) l = std::max(o.grid, std::round(l / o.grid) * o.grid);
            out.x = last->x + ux * l;
            out.z = last->z + uz * l;
            out.kind = kAngle;
            out.angled = true;
            return out;
        }
    }
    if (o.gridSnap && o.grid > 0.0f) {
        out.x = std::round(cx / o.grid) * o.grid;
        out.z = std::round(cz / o.grid) * o.grid;
        out.kind = kGrid;
    }
    return out;
}

Plan finish(const Snapper& s, const std::vector<Snap>& placedIn, const Options& o,
            const std::function<bool(int road)>& sameLook) {
    Plan plan;
    std::vector<Snap> placed;
    for (const Snap& p : placedIn) {
        if (!placed.empty() && std::hypot(p.x - placed.back().x, p.z - placed.back().z) < kDuplicate) {
            // Keep the more specific snap of the two.
            if (p.kind >= kEnd) placed.back() = p;
            continue;
        }
        placed.push_back(p);
    }
    bool closed = false;
    if (!placed.empty() && placed.back().kind == kClose) {
        closed = true;
        placed.pop_back();
    }
    if ((int)placed.size() < (closed ? 3 : 2)) {
        plan.error = closed ? "a loop needs at least three points" : "a road needs at least two points";
        return plan;
    }
    char buf[160];
    for (size_t i = 0; i < placed.size(); ++i) {
        const Snap& p = placed[i];
        if (p.kind == kEnd || p.kind == kCentre) {
            const bool endOfDrawing = i == 0 || i + 1 == placed.size();
            const char* what = p.kind == kEnd ? "end of" : endOfDrawing ? "T on" : "crossing";
            std::snprintf(buf, sizeof buf, "%spoint %zu: %s %s", plan.summary.empty() ? "" : "; ",
                          i + 1, what, s.road(p.road).id.c_str());
            plan.summary += buf;
        }
    }

    // Extension: a drawing that leaves a road's END in line with it, and looks
    // the same, is more of that road.
    auto extendable = [&](const Snap& at, float dx, float dz) {
        if (!o.extendEnds || closed || at.kind != kEnd || at.road < 0 || !sameLook) return false;
        const float l = std::sqrt(len2(dx, dz));
        if (!(l > 1e-6f)) return false;
        return (at.tx * dx + at.tz * dz) / l >= kExtendCos && sameLook(at.road);
    };
    std::vector<Snap> seq = placed;
    int extendAt = -1;  // 0 = the drawing's start is on the road, 1 = its end
    if (extendable(seq[0], seq[1].x - seq[0].x, seq[1].z - seq[0].z)) {
        extendAt = 0;
    } else {
        const size_t n = seq.size();
        if (extendable(seq[n - 1], seq[n - 2].x - seq[n - 1].x, seq[n - 2].z - seq[n - 1].z)) {
            std::reverse(seq.begin(), seq.end());
            extendAt = 1;
        }
    }
    if (extendAt >= 0) {
        // seq[0] is the road's end; the rest is new.
        const int r = seq[0].road;
        const std::vector<float>& rp = s.road(r).points;
        std::vector<float> pts;
        if (seq[0].end == 1) {
            pts = rp;
            for (size_t i = 1; i < seq.size(); ++i) pts.insert(pts.end(), {seq[i].x, seq[i].z});
        } else {
            for (size_t i = seq.size() - 1; i >= 1; --i) pts.insert(pts.end(), {seq[i].x, seq[i].z});
            pts.insert(pts.end(), rp.begin(), rp.end());
        }
        plan.extend = r;
        plan.points = std::move(pts);
        plan.heights.clear();
        for (size_t i = 1; i < seq.size(); ++i) plan.heights.push_back(seq[i].height);
        plan.ok = true;
        std::snprintf(buf, sizeof buf, "extends %s %s by %zu point(s)", s.road(r).id.c_str(),
                      seq[0].end == 1 ? "past its last point" : "before its first point",
                      seq.size() - 1);
        plan.summary = plan.summary.empty() ? buf : std::string(buf) + "; " + plan.summary;
        return plan;
    }

    for (const Snap& p : placed) {
        plan.points.push_back(p.x);
        plan.points.push_back(p.z);
        plan.heights.push_back(p.height);
    }
    if (closed) {
        plan.points.push_back(placed[0].x);
        plan.points.push_back(placed[0].z);
    }
    plan.closed = closed;
    plan.ok = true;
    std::snprintf(buf, sizeof buf, "new road, %zu point(s)%s", placed.size(), closed ? ", closed loop" : "");
    plan.summary = plan.summary.empty() ? buf : std::string(buf) + "; " + plan.summary;
    return plan;
}

int commit(std::vector<SceneObject>& objects, const std::vector<int>& roadObject, const Plan& plan,
           const roadpresets::Preset& preset, const std::string& projectDir, float unitsPerMeter) {
    if (!plan.ok) return -1;
    bool bridge = false;
    for (float h : plan.heights) bridge |= h > 0.0f;
    if (plan.extend >= 0) {
        if (plan.extend >= (int)roadObject.size()) return -1;
        const int oi = roadObject[(size_t)plan.extend];
        SceneObject& o = objects[(size_t)oi];
        const int before = roadgen::controlCount(o.roadPoints);
        const bool appended = plan.points.size() >= o.roadPoints.size() &&
                              std::equal(o.roadPoints.begin(), o.roadPoints.end(), plan.points.begin());
        const int added = roadgen::controlCount(plan.points) - before;
        if (o.roadBridge || bridge) {
            std::vector<float> h = o.roadHeights;
            h.resize((size_t)before, 0.0f);
            std::vector<float> extra(plan.heights.begin(), plan.heights.end());
            extra.resize((size_t)std::max(0, added), 0.0f);
            if (appended) {
                h.insert(h.end(), extra.begin(), extra.end());
            } else {
                std::reverse(extra.begin(), extra.end());
                h.insert(h.begin(), extra.begin(), extra.end());
            }
            o.roadBridge = true;
            o.roadHeights = std::move(h);
        }
        o.roadPoints = plan.points;
        roadbridge::onPointsReshaped(o);
        return oi;
    }
    SceneObject o;
    o.type = PrimitiveType::Road;
    o.id = project::newObjectId();
    const std::string base = preset.key.empty() ? std::string("road") : preset.key;
    for (int k = 1;; ++k) {
        const std::string name = base + "-" + std::to_string(k);
        bool taken = false;
        for (const SceneObject& q : objects) taken |= q.name == name;
        if (!taken) {
            o.name = name;
            break;
        }
    }
    o.roadPoints = plan.points;
    o.position[0] = plan.points[0];
    o.position[1] = 0.0f;
    o.position[2] = plan.points[1];
    roadpresets::ensureMaterials(projectDir, preset);
    roadpresets::apply(preset, o, unitsPerMeter);
    if (bridge) {
        o.roadBridge = true;
        o.roadHeights = plan.heights;
        o.roadHeights.resize((size_t)roadgen::controlCount(o.roadPoints), 0.0f);
    }
    objects.push_back(std::move(o));
    return (int)objects.size() - 1;
}

bool verticalHandleY(const float o[3], const float d[3], float px, float pz, float* y) {
    // Closest approach of the ray o + t d and the line (px, s, pz).
    const float a = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    const float b = d[1];
    const float w0[3] = {o[0] - px, o[1], o[2] - pz};
    const float dd = d[0] * w0[0] + d[1] * w0[1] + d[2] * w0[2];
    const float e = w0[1];
    const float den = a - b * b;  // = dx^2 + dz^2 (c = 1)
    if (!(den > 1e-6f * std::max(a, 1e-12f))) return false;
    const float t = (b * e - dd) / den;
    if (t <= 0.0f) return false;
    *y = (a * e - b * dd) / den;
    return std::isfinite(*y);
}

float bridgeHeightFor(float y, float groundY, bool snap) {
    float h = std::clamp(y - groundY, 0.0f, roadbridge::kMaxHeight);
    if (snap) h = std::round(h * 10.0f) / 10.0f;
    return h;
}

// --- --vehicle-check "road drawing" ------------------------------------------

void check(void (*verdict)(bool ok, const char* what)) {
    std::printf("-- road drawing --\n");
    auto road = [](const char* id, std::vector<float> pts, float width) {
        roadgen::CrossingRoad r;
        r.id = id;
        r.points = std::move(pts);
        r.width = width;
        r.intersection = "res/materials/roads/road-junction.mtl";
        return r;
    };
    auto near = [](float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; };
    Options opt;
    // One 8-wide street along X, 0..60.
    const roadgen::CrossingRoad main = road("main", {0, 0, 30, 0, 60, 0}, 8);
    const Snapper sn({main});

    // Ends and centre lines.
    {
        const Snap e = sn.resolve({}, 61.5f, 1.0f, opt);
        verdict(e.kind == kEnd && e.road == 0 && e.end == 1 && e.x == 60.0f && e.z == 0.0f &&
                    near(e.tx, 1.0f),
                "a click near a road's end lands exactly on it, facing outward");
        const Snap c = sn.resolve({}, 21.0f, 2.5f, opt);
        verdict(c.kind == kCentre && c.road == 0 && near(c.z, 0.0f) && near(c.x, 21.0f, 0.05f),
                "a click on a road lands on its centre line");
        const Snap f = sn.resolve({}, 21.0f, 9.5f, opt);
        verdict(f.kind == kFree && f.x == 21.0f && f.z == 9.5f,
                "a click clear of every road stays where it is");
        Options off = opt;
        off.roadSnap = false;
        verdict(sn.resolve({}, 21.0f, 2.5f, off).kind == kFree, "road snapping can be turned off");
    }
    // Angle steps.
    {
        std::vector<Snap> placed(2);
        placed[0].x = 0, placed[0].z = 20;
        placed[1].x = 10, placed[1].z = 20;
        const Snap a = sn.resolve(placed, 20.0f, 21.0f, opt);
        verdict(a.kind == kAngle && near(a.z, 20.0f) && near(a.x, 20.0f, 0.02f),
                "a nearly straight segment snaps onto the previous one's line");
        const Snap r = sn.resolve(placed, 10.6f, 29.0f, opt);
        verdict(near(r.x, 10.0f) && near(r.angle, 90.0f), "a nearly square turn snaps to 90 degrees");
        const Snap q = sn.resolve(placed, 19.0f, 24.5f, opt);
        verdict(near(q.angle, 30.0f) && near(std::atan2(q.z - 20.0f, q.x - 10.0f) * 180.0f / kPi, 30.0f, 0.01f),
                "angles snap in 15-degree steps");
        Options shift = opt;
        shift.angleSnap = false;
        const Snap fr = sn.resolve(placed, 20.0f, 21.5f, shift);
        verdict(fr.kind == kFree && fr.x == 20.0f && fr.z == 21.5f, "Shift (no angle step) leaves the point free");
        Options grid = shift;
        grid.gridSnap = true;
        const Snap g = sn.resolve({}, 5.1f, 16.9f, grid);
        verdict(g.kind == kGrid && g.x == 4.0f && g.z == 16.0f, "the grid snaps a free point to its cells");
    }
    // A T: drawn down onto the street, ending on it. The last click is a
    // little off square and off the centre line; the angle step relative to
    // the STREET lands it exactly on the centre line, square to it.
    {
        std::vector<Snap> placed;
        placed.push_back(sn.resolve({}, 25.0f, 40.0f, opt));
        placed.push_back(sn.resolve(placed, 26.2f, 2.4f, opt));
        verdict(placed[1].kind == kCentre && placed[1].angled && near(placed[1].x, 25.0f) &&
                    near(placed[1].z, 0.0f),
                "the end of a T lands square on the street's centre line");
        const Plan p = finish(sn, placed, opt, {});
        std::vector<roadgen::CrossingRoad> roads = {main, road("stem", p.points, 6)};
        const roadgen::CrossingPlan cp = roadgen::planCrossings(roads, {});
        std::printf("  T: %s; %zu node(s)", p.summary.c_str(), cp.crossings.size());
        for (const roadgen::Crossing& c : cp.crossings) std::printf(" [%d arms, kind %d]", c.arms, c.kind);
        std::printf("\n");
        verdict(p.ok && p.extend < 0 && cp.crossings.size() == 1 && cp.crossings[0].arms == 3 &&
                    cp.crossings[0].kind == roadgen::kCrossPatch,
                "a drawn T makes one three-armed node patch");
    }
    // A crossing: drawn through the street, a point clicked on it.
    {
        std::vector<Snap> placed;
        placed.push_back(sn.resolve({}, 40.0f, -20.0f, opt));
        placed.push_back(sn.resolve(placed, 40.5f, 1.4f, opt));
        placed.push_back(sn.resolve(placed, 40.3f, 20.0f, opt));
        verdict(placed[1].kind == kCentre && near(placed[1].x, 40.0f) && near(placed[1].z, 0.0f) &&
                    near(placed[2].x, 40.0f),
                "a point clicked on a road while drawing through it is on its centre line");
        const Plan p = finish(sn, placed, opt, {});
        std::vector<roadgen::CrossingRoad> roads = {main, road("cross", p.points, 6)};
        const roadgen::CrossingPlan cp = roadgen::planCrossings(roads, {});
        verdict(p.ok && cp.crossings.size() == 1 && cp.crossings[0].arms == 4,
                "drawing through a road makes one four-armed crossing");
    }
    // From a road's end: in line = the road extended; square = a corner.
    {
        std::vector<Snap> placed;
        placed.push_back(sn.resolve({}, 60.8f, -1.0f, opt));
        placed.push_back(sn.resolve(placed, 90.0f, 2.0f, opt));
        verdict(placed[0].kind == kEnd && near(placed[1].z, 0.0f),
                "leaving a road's end, the angle is measured from the road");
        const Plan ext = finish(sn, placed, opt, [](int) { return true; });
        verdict(ext.ok && ext.extend == 0 && ext.points.size() == 8 && near(ext.points[6], 90.0f, 0.05f) &&
                    ext.points[7] == 0.0f,
                "a drawing carrying a road on in line extends that road");
        const Plan other = finish(sn, placed, opt, [](int) { return false; });
        verdict(other.ok && other.extend < 0, "a road of another look is a new road instead");

        std::vector<Snap> corner;
        corner.push_back(sn.resolve({}, 60.5f, 0.5f, opt));
        corner.push_back(sn.resolve(corner, 61.0f, 30.0f, opt));
        const Plan cp = finish(sn, corner, opt, [](int) { return true; });
        std::vector<roadgen::CrossingRoad> roads = {main, road("corner", cp.points, 8)};
        const roadgen::CrossingPlan nodes = roadgen::planCrossings(roads, {});
        verdict(cp.ok && cp.extend < 0 && nodes.crossings.size() == 1 && nodes.crossings[0].arms == 2,
                "a square turn off a road's end is a new road meeting it in a corner node");

        // Ending ON a road's start, in line: the road is extended backwards.
        std::vector<Snap> back;
        back.push_back(sn.resolve({}, -30.0f, 0.3f, opt));
        back.push_back(sn.resolve(back, -0.7f, 0.2f, opt));
        const Plan bp = finish(sn, back, opt, [](int) { return true; });
        verdict(back[1].kind == kEnd && bp.ok && bp.extend == 0 && near(bp.points[0], -30.0f, 0.05f) &&
                    bp.points.size() == 8,
                "a drawing ending in line on a road's first point is prepended to it");
    }
    // A loop, and what is refused.
    {
        Options free = opt;
        free.roadSnap = false;
        std::vector<Snap> placed;
        placed.push_back(sn.resolve({}, 0.0f, 50.0f, free));
        placed.push_back(sn.resolve(placed, 20.0f, 50.0f, free));
        placed.push_back(sn.resolve(placed, 20.0f, 70.0f, free));
        placed.push_back(sn.resolve(placed, 0.6f, 50.4f, free));
        const Plan p = finish(sn, placed, free, {});
        verdict(placed[3].kind == kClose && p.ok && p.closed && roadgen::isClosed(p.points) &&
                    roadgen::controlCount(p.points) == 3,
                "back on the first point closes a loop");
        std::vector<Snap> one;
        one.push_back(sn.resolve({}, 5.0f, 50.0f, free));
        one.push_back(sn.resolve(one, 5.1f, 50.1f, free));
        verdict(!finish(sn, one, free, {}).ok, "a drawing of one point (a double-click) makes nothing");
    }
    // Presets: every field, and the materials on disk.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "tyrax-roaddraw-check";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        bool everyField = true, files = true, idempotent = true;
        for (const roadpresets::Preset& p : roadpresets::builtins()) {
            std::vector<SceneObject> objs;
            Plan plan;
            plan.ok = true;
            plan.points = {0, 0, 40, 0};
            const int oi = commit(objs, {}, plan, p, dir.string(), 1.0f);
            const SceneObject& o = objs[(size_t)oi];
            const bool f = roadpresets::matches(p, o, 1.0f) && o.roadWidth > 0.0f &&
                           o.roadTexture == p.surface && o.roadKerb == p.kerb &&
                           o.roadPavement == p.pavement && o.roadFurniture == p.furniture &&
                           o.roadKind == p.kind && o.roadRank == p.rank &&
                           o.name == p.key + "-1" && !o.id.empty();
            everyField &= f;
            for (const std::string* m : {&p.surface, &p.intersection, &p.pavementMaterial}) {
                if (m->empty()) continue;
                const fs::path mp = dir / *m;
                const bool ok = fs::exists(mp, ec) &&
                                fs::exists(fs::path(mp).replace_extension(".png"), ec);
                if (!ok) std::printf("  missing material %s for %s\n", m->c_str(), p.key.c_str());
                files &= ok;
            }
            idempotent &= roadpresets::ensureMaterials(dir.string(), p).empty();
            if (!f) std::printf("  preset %s did not apply every field\n", p.key.c_str());
        }
        verdict(roadpresets::builtins().size() == 8, "eight built-in presets");
        verdict(everyField, "a preset sets every road field it owns, on a road named after it");
        verdict(files, "every material a preset names exists after it is applied");
        verdict(idempotent, "an existing material is never written again");
        roadtex::RoadTexParams country;
        verdict(roadpresets::materialRecipe("road-country", &country) && country.shoulder > 0.0f &&
                    roadtex::fromText(roadtex::toText(country)) == country,
                "the country road's verges round-trip through its recipe");
        // The railway's bed follows the project's gauge, like Kind = Railway.
        SceneObject rail;
        roadpresets::apply(*roadpresets::find({}, "railway"), rail, 2.0f);
        verdict(near(rail.roadRailGauge, 2.87f) && near(rail.roadWidth, 7.2f, 0.01f),
                "the railway preset scales its gauge and bed with the units per metre");
        // A project preset saved from a road round-trips and applies back.
        SceneObject src;
        roadpresets::apply(roadpresets::builtins()[2], src, 1.0f);
        src.roadWidth = 11.0f;
        src.roadFurniture.trees.spacing = 7.0f;
        std::vector<roadpresets::Preset> list = {roadpresets::fromRoad(src, "My Boulevard")};
        json::Value v;
        std::vector<roadpresets::Preset> back;
        const bool parsed = json::parse(roadpresets::toJson(list), v);
        roadpresets::fromJson(v, back);
        SceneObject dst;
        if (!back.empty()) roadpresets::apply(back[0], dst, 1.0f);
        verdict(parsed && back.size() == 1 && back[0] == list[0] && back[0].key == "my-boulevard" &&
                    roadpresets::matches(back[0], src, 1.0f) && dst.roadWidth == 11.0f &&
                    dst.roadFurniture.trees.spacing == 7.0f,
                "a project preset saved from a road round-trips and applies back");
        verdict(roadpresets::find(list, "my boulevard") == &list[0] &&
                    roadpresets::find(list, "City street") == &roadpresets::builtins()[0],
                "presets are found by key or name");
        fs::remove_all(dir, ec);
    }
    // The extension commit keeps the bridge heights in step.
    {
        std::vector<SceneObject> objs(1);
        objs[0].type = PrimitiveType::Road;
        objs[0].roadPoints = {0, 0, 30, 0};
        objs[0].roadBridge = true;
        objs[0].roadHeights = {0.0f, 4.0f};
        Plan p;
        p.ok = true;
        p.extend = 0;
        p.points = {-20, 0, 0, 0, 30, 0};
        p.heights = {2.0f};
        roadpresets::Preset none;
        commit(objs, {0}, p, none, "", 1.0f);
        verdict(objs[0].roadHeights.size() == 3 && objs[0].roadHeights[0] == 2.0f &&
                    objs[0].roadHeights[2] == 4.0f,
                "extending a bridge before its first point shifts its heights with it");
    }
    // The bridge handle: the deck height a vertical drag reads.
    {
        const float eye[3] = {0.0f, 10.0f, -20.0f};
        float d[3] = {5.0f - 0.0f, 6.0f - 10.0f, 0.0f + 20.0f};
        float y = 0.0f;
        const bool ok = verticalHandleY(eye, d, 5.0f, 0.0f, &y);
        verdict(ok && near(y, 6.0f), "a ray through a point on the handle reads that point's height");
        // A ray passing beside the line reads the height where it passes closest.
        const float d2[3] = {1.0f, 0.0f, 0.0f};
        const float o2[3] = {-10.0f, 3.5f, 2.0f};
        verdict(verticalHandleY(o2, d2, 0.0f, 0.0f, &y) && near(y, 3.5f),
                "a ray passing beside the handle reads its closest point");
        const float down[3] = {0.0f, -1.0f, 0.0f};
        verdict(!verticalHandleY(eye, down, 0.0f, -20.0f, &y), "looking straight down reads nothing");
        verdict(bridgeHeightFor(5.04f, 1.0f, true) == 4.0f && bridgeHeightFor(-3.0f, 1.0f, false) == 0.0f &&
                    bridgeHeightFor(100.0f, 0.0f, false) == roadbridge::kMaxHeight,
                "a handle height is clamped to 0..30 and snaps to 0.1");
    }
}

}  // namespace roaddraw
