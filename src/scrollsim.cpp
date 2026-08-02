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

std::vector<MemberRef> segmentMembers(const std::vector<SceneObject>& objects,
                                      const ScrollSegment& seg) {
    std::vector<MemberRef> refs;
    for (size_t slot = 0; slot < seg.objects.size(); ++slot)
        for (size_t i = 0; i < objects.size(); ++i)
            if (objects[i].name == seg.objects[slot].name) {
                refs.push_back({(int)i, (int)slot});
                break;
            }
    return refs;
}

std::vector<char> memberTemplateFlags(const std::vector<SceneObject>& objects) {
    std::vector<char> flags(objects.size(), 0);
    for (const SceneObject& sc : objects) {
        if (sc.type != PrimitiveType::Scroller) continue;
        for (const ScrollSegment& seg : sc.scrollSegments)
            for (const MemberRef& ref : segmentMembers(objects, seg))
                flags[(size_t)ref.object] = 1;
    }
    return flags;
}

float segmentLength(const std::vector<SceneObject>& objects,
                    const SceneObject& scroller, const ScrollSegment& seg) {
    if (seg.length > 0.0f) return seg.length;
    float axis[3];
    beltAxis(scroller.rotation, axis);
    bool any = false;
    float lo = 0.0f, hi = 0.0f;
    for (const ScrollMember& member : seg.objects) {
        const SceneObject* o = findByName(objects, member.name);
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
    // The fold is also what would make the belt eternally periodic, so count it:
    // `folds` is the integer the fold threw away, and adding it back is what
    // recovers each instance's absolute cell index below. The generated
    // director keeps the same counter (SCROLLERS folds_) for the same reason.
    int folds = 0;
    if (span > 1e-6f) {
        const float f = std::floor(beltScroll / span);
        beltScroll -= span * f;
        folds = (int)f;
    }
    for (size_t j = 0; j < segs.size(); ++j) {
        const float segLen = segmentLength(objects, scroller, segs[j]);
        for (int m = 0; m < cells; ++m) {
            Placement pl;
            pl.segment = (int)j;
            pl.copy = m;
            pl.phase = base[j] + m * pat;
            pl.segLen = segLen;
            const float nominal = pl.phase - beltScroll;
            const int lap =
                span > 1e-6f ? (int)std::floor((nominal - wmin) / span) : 0;
            pl.u = wrapU(nominal, wmin, span);
            pl.visible = (pl.u < wmax) && (pl.u + segLen > wmin);
            pl.cell = m - (lap - folds) * cells;
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

void sideAxis(const float axis[3], float out[3]) {
    // cross((0,1,0), axis) = (axis.z, 0, -axis.x)
    float x = axis[2], z = -axis[0];
    const float len = std::sqrt(x * x + z * z);
    if (len > 1e-6f) {
        out[0] = x / len;
        out[1] = 0.0f;
        out[2] = z / len;
    } else {  // belt points straight up/down - any horizontal will do
        out[0] = 1.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
    }
}

// --- per-cell variation --------------------------------------------------
// Twin: sc_varyHash / sc_cellAdjust in the generated ScrollerDirector
// (templates.cpp). The console must resolve a cell exactly the way the editor
// previewed it, so these are duplicated verbatim rather than approximated.

// The four draws a member makes per cell. Distinct channels, so raising a
// member's yaw range does not also re-roll whether it appears at all.
enum : int { kChanChance = 0, kChanVariant = 1, kChanYaw = 2, kChanOffset = 3,
             kChanScale = 4 };

unsigned varyHash(int seed, int cell, unsigned key, int channel) {
    unsigned h = (unsigned)seed * 0x9E3779B1u + 0x85EBCA6Bu;
    h ^= (unsigned)cell * 0xC2B2AE35u;
    h ^= key + (h << 6) + (h >> 2);
    h ^= (unsigned)channel * 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    h *= 0x9E3779B1u;
    h ^= h >> 16;
    return h;
}

float varyRand(int seed, int cell, unsigned key, int channel) {
    return (float)(varyHash(seed, cell, key, channel) >> 8) * (1.0f / 16777216.0f);
}

MemberVary memberVary(const ScrollSegment& seg, int segmentIndex, int slot) {
    MemberVary v;
    if (slot < 0 || slot >= (int)seg.objects.size()) return v;
    const ScrollMember& m = seg.objects[(size_t)slot];
    v.key = memberKey(segmentIndex, slot);
    v.chance = m.chance;
    v.yawVary = m.yawVary;
    v.offsetVary = m.offsetVary;
    v.scaleVary = m.scaleVary;
    if (m.variant > 0) {
        // Members of this segment sharing the variant id are alternatives: the
        // group draws ONE index per cell, so the group's key must not depend on
        // the slot (every member has to see the same draw).
        for (size_t k = 0; k < seg.objects.size(); ++k) {
            if (seg.objects[k].variant != m.variant) continue;
            if ((int)k == slot) v.variantIndex = v.variantCount;
            ++v.variantCount;
        }
        v.variantKey = memberKey(segmentIndex, 0) ^
                       ((unsigned)m.variant * 0x45D9F3B3u);
    }
    return v;
}

CellAdjust cellAdjust(int varySeed, int cell, const MemberVary& v) {
    CellAdjust a;
    if (v.variantCount > 1) {
        // Exactly one alternative per cell. Deliberately independent of the
        // per-member `chance`: a group that also rolled chance would leave
        // holes the author did not ask for.
        const unsigned pick =
            varyHash(varySeed, cell, v.variantKey, kChanVariant) % (unsigned)v.variantCount;
        a.visible = (int)pick == v.variantIndex;
    } else if (v.chance < 1.0f) {
        a.visible = varyRand(varySeed, cell, v.key, kChanChance) < v.chance;
    }
    if (!a.visible) return a;  // an absent member's transform is never read
    if (v.yawVary != 0.0f)
        a.yaw = (varyRand(varySeed, cell, v.key, kChanYaw) * 2.0f - 1.0f) * v.yawVary;
    if (v.offsetVary != 0.0f)
        a.offset =
            (varyRand(varySeed, cell, v.key, kChanOffset) * 2.0f - 1.0f) * v.offsetVary;
    if (v.scaleVary != 0.0f)
        a.scale =
            1.0f + (varyRand(varySeed, cell, v.key, kChanScale) * 2.0f - 1.0f) * v.scaleVary;
    return a;
}

bool hasVariation(const SceneObject& scroller) {
    for (const ScrollSegment& s : scroller.scrollSegments)
        for (const ScrollMember& m : s.objects)
            if (!scrollMemberIsPlain(m)) return true;
    return false;
}

std::vector<MemberInstance> memberInstances(const std::vector<SceneObject>& objects,
                                            const SceneObject& scroller,
                                            const Placement& pl) {
    std::vector<MemberInstance> out;
    if (pl.segment < 0 || pl.segment >= (int)scroller.scrollSegments.size()) return out;
    const ScrollSegment& seg = scroller.scrollSegments[(size_t)pl.segment];
    float axis[3], side[3];
    beltAxis(scroller.rotation, axis);
    sideAxis(axis, side);
    for (const MemberRef& ref : segmentMembers(objects, seg)) {
        const SceneObject& src = objects[(size_t)ref.object];
        const CellAdjust adj = cellAdjust(scroller.scrollVarySeed, pl.cell,
                                          memberVary(seg, pl.segment, ref.slot));
        MemberInstance mi;
        mi.object = ref.object;
        mi.slot = ref.slot;
        mi.visible = pl.visible && adj.visible;
        // Seam overlap first, then this cell's uniform scale jitter - the
        // overlap is a fixed world-space bite into the neighbour, so scaling
        // after it would make a shrunk copy overlap less than an enlarged one.
        seamScale(src, axis, scroller.scrollOverlap, mi.scale);
        for (int a = 0; a < 3; ++a) mi.scale[a] *= adj.scale;
        for (int a = 0; a < 3; ++a) mi.rotation[a] = src.rotation[a];
        mi.rotation[1] += adj.yaw;
        for (int a = 0; a < 3; ++a)
            mi.position[a] = src.position[a] + pl.u * axis[a] + adj.offset * side[a];
        out.push_back(mi);
    }
    return out;
}

}  // namespace scrollsim
