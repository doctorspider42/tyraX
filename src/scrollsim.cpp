#include "scrollsim.hpp"

#include <cmath>

namespace scrollsim {

namespace {
constexpr float kMinSegment = 0.5f;  // floor so a chunk always advances the belt
constexpr float kPi = 3.14159265358979323846f;

const SceneObject* findByName(const std::vector<SceneObject>& objects,
                              const std::string& name) {
    if (name.empty()) return nullptr;
    for (const SceneObject& o : objects)
        if (o.name == name) return &o;
    return nullptr;
}
}  // namespace

void beltAxis(const float rot[3], float out[3]) {
    // R = Rz * Ry * Rx applied to (0,0,1) - matches viewport::modelMatrix and
    // the generated geometry builder (scale, then X, Y, Z rotations).
    const float d2r = kPi / 180.0f;
    const float cx = std::cos(rot[0] * d2r), sx = std::sin(rot[0] * d2r);
    const float cy = std::cos(rot[1] * d2r), sy = std::sin(rot[1] * d2r);
    const float cz = std::cos(rot[2] * d2r), sz = std::sin(rot[2] * d2r);
    out[0] = cz * sy * cx + sz * sx;
    out[1] = sz * sy * cx - cz * sx;
    out[2] = cy * cx;
    const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
    if (len > 1e-6f) {
        out[0] /= len;
        out[1] /= len;
        out[2] /= len;
    } else {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 1.0f;
    }
}

std::vector<int> segmentMembers(const std::vector<SceneObject>& objects,
                                const ScrollSegment& seg) {
    std::vector<int> idx;
    for (const std::string& name : seg.objects)
        for (size_t i = 0; i < objects.size(); ++i)
            if (objects[i].name == name) {
                idx.push_back((int)i);
                break;
            }
    return idx;
}

float segmentLength(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller, const ScrollSegment& seg) {
    if (seg.length > 0.0f) return seg.length;
    float axis[3];
    beltAxis(scroller.rotation, axis);
    bool any = false;
    float lo = 0.0f, hi = 0.0f;
    for (const std::string& name : seg.objects) {
        const SceneObject* o = findByName(objects, name);
        if (!o) continue;
        const float along = o->position[0] * axis[0] + o->position[1] * axis[1] +
                            o->position[2] * axis[2];
        // rough half-extent of the object along the belt (largest scale axis)
        float r = o->scale[0];
        if (o->scale[1] > r) r = o->scale[1];
        if (o->scale[2] > r) r = o->scale[2];
        r *= 0.5f;
        if (!any) {
            lo = along - r;
            hi = along + r;
            any = true;
        } else {
            if (along - r < lo) lo = along - r;
            if (along + r > hi) hi = along + r;
        }
    }
    const float span = any ? (hi - lo) : 0.0f;
    return span > kMinSegment ? span : kMinSegment;
}

float patternLength(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller) {
    float total = 0.0f;
    for (const ScrollSegment& s : scroller.scrollSegments)
        total += segmentLength(objects, scroller, s);
    return total > kMinSegment ? total : kMinSegment;
}

std::vector<float> baseOffsets(const std::vector<SceneObject>& objects,
                               const SceneObject& scroller) {
    std::vector<float> offs;
    offs.reserve(scroller.scrollSegments.size());
    float acc = 0.0f;
    for (const ScrollSegment& s : scroller.scrollSegments) {
        offs.push_back(acc);
        acc += segmentLength(objects, scroller, s);
    }
    return offs;
}

int membersPerPattern(const std::vector<SceneObject>& objects,
                      const SceneObject& scroller) {
    int total = 0;
    for (const ScrollSegment& s : scroller.scrollSegments)
        total += (int)segmentMembers(objects, s).size();
    return total;
}

int geometricCells(const std::vector<SceneObject>& objects,
                   const SceneObject& scroller) {
    const float pat = patternLength(objects, scroller);
    const float win = scroller.scrollAhead + scroller.scrollBehind;
    int cells = (int)std::ceil(win / pat) + 2;
    if (cells < 1) cells = 1;
    return cells;
}

int cellsPerSegment(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller) {
    const int geo = geometricCells(objects, scroller);
    const int members = membersPerPattern(objects, scroller);
    if (members <= 0) return geo;  // nothing baked; cap irrelevant
    int cap = scroller.scrollMaxClones / members;  // whole pattern layers that fit
    if (cap < 1) cap = 1;
    return geo < cap ? geo : cap;
}

bool cloneCapped(const std::vector<SceneObject>& objects, const SceneObject& scroller) {
    return cellsPerSegment(objects, scroller) < geometricCells(objects, scroller);
}

float wrapU(float nominal, float wmin, float span) {
    if (span <= 1e-6f) return wmin;
    float x = nominal - wmin;
    x -= span * std::floor(x / span);  // positive fmod into [0, span)
    return wmin + x;
}

std::vector<Placement> placements(const std::vector<SceneObject>& objects,
                                  const SceneObject& scroller, float beltScroll) {
    std::vector<Placement> out;
    const auto& segs = scroller.scrollSegments;
    if (segs.empty()) return out;
    const float pat = patternLength(objects, scroller);
    const int cells = cellsPerSegment(objects, scroller);
    const float span = cells * pat;
    const float wmin = windowMin(scroller), wmax = windowMax(scroller);
    const std::vector<float> base = baseOffsets(objects, scroller);
    // Fold the scroll distance into one period before using it. The layout is
    // periodic in beltScroll with period `span`, so this changes nothing that
    // is drawn - but it keeps the value small, and float precision is the
    // whole point: a belt that has been running for hours arrives here with a
    // beltScroll big enough that `nominal - wmin` quantizes to a step coarser
    // than one frame's movement, and the belt visibly stutters and eventually
    // freezes. The generated ScrollerDirector folds its own accumulator the
    // same way (grep sc_wrapU / "Keep the accumulator bounded").
    if (span > 1e-6f) beltScroll -= span * std::floor(beltScroll / span);
    for (size_t j = 0; j < segs.size(); ++j) {
        const float segLen = segmentLength(objects, scroller, segs[j]);
        for (int m = 0; m < cells; ++m) {
            Placement pl;
            pl.segment = (int)j;
            pl.copy = m;
            pl.phase = base[j] + m * pat;
            pl.segLen = segLen;
            pl.u = wrapU(pl.phase - beltScroll, wmin, span);
            pl.visible = (pl.u < wmax) && (pl.u + segLen > wmin);
            out.push_back(pl);
        }
    }
    return out;
}

int cloneCount(const std::vector<SceneObject>& objects, const SceneObject& scroller) {
    const int cells = cellsPerSegment(objects, scroller);
    int total = 0;
    for (const ScrollSegment& s : scroller.scrollSegments)
        total += cells * (int)segmentMembers(objects, s).size();
    return total;
}

void seamScale(const SceneObject& member, const float beltAxis[3], float overlap,
               float outScale[3]) {
    for (int a = 0; a < 3; ++a) outScale[a] = member.scale[a];
    if (overlap <= 0.0f) return;
    // The member's local axes in world space are the columns of its rotation
    // matrix R = Rz*Ry*Rx (same convention as beltAxis). Rotate each basis
    // vector and pick the one most parallel to the belt.
    const float d2r = kPi / 180.0f;
    const float cx = std::cos(member.rotation[0] * d2r), sx = std::sin(member.rotation[0] * d2r);
    const float cy = std::cos(member.rotation[1] * d2r), sy = std::sin(member.rotation[1] * d2r);
    const float cz = std::cos(member.rotation[2] * d2r), sz = std::sin(member.rotation[2] * d2r);
    const float axes[3][3] = {
        // R * (1,0,0), R * (0,1,0), R * (0,0,1)
        {cz * cy, sz * cy, -sy},
        {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx},
        {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx},
    };
    int best = 2;
    float bestDot = 0.0f;
    for (int a = 0; a < 3; ++a) {
        const float d = std::fabs(axes[a][0] * beltAxis[0] + axes[a][1] * beltAxis[1] +
                                  axes[a][2] * beltAxis[2]);
        if (d > bestDot) {
            bestDot = d;
            best = a;
        }
    }
    outScale[best] += overlap;
}

}  // namespace scrollsim
