#include "animmerge.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "fbxparser.hpp"  // animimport:: - the format dispatch
#include "json.hpp"       // parseAiBoneMap reads a model reply

namespace animmerge {
namespace {

using glbparser::Baked;
using glbparser::Skel;
using glbparser::SkelChannel;
using glbparser::SkelClip;
using glbparser::SkelJoint;
using glbparser::SkelNode;
using glbparser::SkelPart;

// --- small 4x4 column-major helpers ---------------------------------------
// Same layout as SkelJoint::ibm, so a palette matrix multiplies straight in.

struct M4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

// T * R * S from a node's local components (the glTF composition order every
// other consumer in this codebase uses).
M4 trs(const float t[3], const float r[4], const float s[3]) {
    const float x = r[0], y = r[1], z = r[2], w = r[3];
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    M4 o;
    o.m[0] = (1 - 2 * (yy + zz)) * s[0];
    o.m[1] = (2 * (xy + wz)) * s[0];
    o.m[2] = (2 * (xz - wy)) * s[0];
    o.m[3] = 0;
    o.m[4] = (2 * (xy - wz)) * s[1];
    o.m[5] = (1 - 2 * (xx + zz)) * s[1];
    o.m[6] = (2 * (yz + wx)) * s[1];
    o.m[7] = 0;
    o.m[8] = (2 * (xz + wy)) * s[2];
    o.m[9] = (2 * (yz - wx)) * s[2];
    o.m[10] = (1 - 2 * (xx + yy)) * s[2];
    o.m[11] = 0;
    o.m[12] = t[0];
    o.m[13] = t[1];
    o.m[14] = t[2];
    o.m[15] = 1;
    return o;
}

M4 localOf(const SkelNode& n) {
    if (n.hasMatrix) {
        M4 o;
        std::memcpy(o.m, n.matrix, sizeof(o.m));
        return o;
    }
    return trs(n.t, n.r, n.s);
}

// Mean scale of the node's PARENT chain at bind - the factor between the
// node's local units and model space. A Mixamo rig keeps its bones in
// centimeters under a 0.01-scaled armature node, so a root-motion delta read
// from its channels is 100x the model-space meters; this is the number that
// converts. 1.0 on an unscaled rig, exactly.
float parentGlobalScale(const Skel& s, int node) {
    if (node < 0 || node >= (int)s.nodes.size()) return 1.0f;
    std::vector<int> chain;
    for (int i = s.nodes[(size_t)node].parent;
         i >= 0 && i < (int)s.nodes.size(); i = s.nodes[(size_t)i].parent) {
        chain.push_back(i);
        if (s.nodes[(size_t)i].parent == i) break;
    }
    M4 acc;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        acc = mul(acc, localOf(s.nodes[(size_t)*it]));
    float mean = 0.0f;
    for (int c = 0; c < 3; ++c)
        mean += std::sqrt(acc.m[c * 4 + 0] * acc.m[c * 4 + 0] +
                          acc.m[c * 4 + 1] * acc.m[c * 4 + 1] +
                          acc.m[c * 4 + 2] * acc.m[c * 4 + 2]);
    mean /= 3.0f;
    return mean > 1e-9f ? mean : 1.0f;
}

// A node's rest position in model space - what the hip-height ratio measures.
void restGlobalTranslation(const Skel& s, int node, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    if (node < 0 || node >= (int)s.nodes.size()) return;
    std::vector<int> chain;
    for (int i = node; i >= 0 && i < (int)s.nodes.size(); i = s.nodes[i].parent) {
        chain.push_back(i);
        if (s.nodes[i].parent == i) break;  // malformed file guard
    }
    M4 acc;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        acc = mul(acc, localOf(s.nodes[*it]));
    out[0] = acc.m[12];
    out[1] = acc.m[13];
    out[2] = acc.m[14];
}

// --- name matching ---------------------------------------------------------

std::string normalize(std::string name, const MergeOptions& o) {
    if (o.stripNamespace) {
        // "mixamorig:Hips", "Armature|Hips" - exporters prefix in both styles.
        const size_t pos = name.find_last_of(":|");
        if (pos != std::string::npos && pos + 1 < name.size())
            name = name.substr(pos + 1);
    }
    if (o.caseInsensitive)
        for (char& c : name)
            c = (char)std::tolower((unsigned char)c);
    return name;
}

// target-node lookup: the explicit boneMap first (a hand-made pair always
// wins), then exact name, then normalized.
class Resolver {
   public:
    Resolver(const Skel& target, const MergeOptions& o) : opts_(o) {
        for (size_t i = 0; i < target.nodes.size(); ++i) {
            exact_.emplace(target.nodes[i].name, (int)i);
            // First writer wins, so a collision resolves deterministically.
            norm_.emplace(normalize(target.nodes[i].name, o), (int)i);
        }
        // The map stores TARGET NAMES (stable across both files' reparses);
        // resolve them to indices once. A pair whose target name no longer
        // exists is skipped - the donor bone then falls through to the name
        // chain instead of silently landing on bone 0. LATER PAIRS WIN BOTH
        // ENDS: the mapper enforces one driver per donor and per target going
        // forward, but a .tyra written before that rule can still carry two
        // donors claiming one target - and the loser's own bone then held its
        // bind pose (the spine-under-the-pelvis report). Deduping here heals
        // stale files without anyone reopening the mapper.
        for (const auto& [from, to] : o.boneMap) {
            const auto ti = exact_.find(to);
            if (ti == exact_.end()) continue;
            for (auto it = mapped_.begin(); it != mapped_.end();)
                it = it->second == ti->second ? mapped_.erase(it)
                                              : std::next(it);
            mapped_[from] = ti->second;
        }
    }
    int resolve(const std::string& name) const {
        if (auto it = mapped_.find(name); it != mapped_.end()) return it->second;
        if (auto it = exact_.find(name); it != exact_.end()) return it->second;
        if (auto it = norm_.find(normalize(name, opts_)); it != norm_.end())
            return it->second;
        return -1;
    }

   private:
    MergeOptions opts_;
    std::map<std::string, int> mapped_;
    std::map<std::string, int> exact_;
    std::map<std::string, int> norm_;
};

// Which nodes are skinning bones, and which of those are ROOTS (no ancestor is
// itself a bone) - the hips of each disjoint skeleton.
void boneSets(const Skel& s, std::vector<char>& isBone,
              std::vector<char>& isRootBone) {
    isBone.assign(s.nodes.size(), 0);
    isRootBone.assign(s.nodes.size(), 0);
    for (const SkelJoint& j : s.palette)
        if (j.node >= 0 && j.node < (int)s.nodes.size()) isBone[(size_t)j.node] = 1;
    for (size_t i = 0; i < s.nodes.size(); ++i) {
        if (!isBone[i]) continue;
        bool ancestorIsBone = false;
        for (int p = s.nodes[i].parent; p >= 0 && p < (int)s.nodes.size();
             p = s.nodes[(size_t)p].parent) {
            if (isBone[(size_t)p]) {
                ancestorIsBone = true;
                break;
            }
            if (s.nodes[(size_t)p].parent == p) break;
        }
        isRootBone[i] = ancestorIsBone ? 0 : 1;
    }
}

bool constantChannel(const SkelChannel& ch, int stride, float eps) {
    if (ch.times.size() < 2) return true;
    for (size_t k = 1; k * (size_t)stride + (size_t)stride <= ch.values.size();
         ++k)
        for (int c = 0; c < stride; ++c)
            if (std::fabs(ch.values[k * (size_t)stride + (size_t)c] -
                          ch.values[(size_t)c]) > eps)
                return false;
    return true;
}

std::string uniqueClipName(const Skel& target, const std::string& want) {
    bool taken = false;
    for (const SkelClip& c : target.clips)
        if (c.name == want) taken = true;
    if (!taken) return want;
    for (int n = 1; n < 10000; ++n) {
        const std::string cand = want + "_" + std::to_string(n);
        bool hit = false;
        for (const SkelClip& c : target.clips)
            if (c.name == cand) hit = true;
        if (!hit) return cand;
    }
    return want;
}

// --- pose sampling (the preview bake) --------------------------------------

// Value of one channel at time t, linearly interpolated (STEP holds the left
// key), written into `out`. Mirrors what the engine's SkelInstance does.
void sampleChannel(const SkelChannel& ch, float t, int stride, float* out) {
    const size_t keys = ch.times.size();
    if (keys == 0) return;
    if (t <= ch.times.front() || keys == 1) {
        for (int c = 0; c < stride; ++c) out[c] = ch.values[(size_t)c];
        return;
    }
    if (t >= ch.times.back()) {
        const size_t base = (keys - 1) * (size_t)stride;
        for (int c = 0; c < stride; ++c) out[c] = ch.values[base + (size_t)c];
        return;
    }
    size_t hi = 1;
    while (hi < keys && ch.times[hi] < t) ++hi;
    const size_t lo = hi - 1;
    const float span = ch.times[hi] - ch.times[lo];
    const float f = span > 1e-9f ? (t - ch.times[lo]) / span : 0.0f;
    for (int c = 0; c < stride; ++c) {
        const float a = ch.values[lo * (size_t)stride + (size_t)c];
        const float b = ch.values[hi * (size_t)stride + (size_t)c];
        out[c] = ch.step ? a : a + (b - a) * f;
    }
}

void composeGlobals(const Skel& s, const std::vector<M4>& locals,
                    std::vector<M4>& globals);

// Global matrix per node at time t of `clip`. A node with no channel of a
// given path keeps its BIND component - the rule that preserves the target's
// own proportions when a donor track was stripped or never existed.
void poseGlobals(const Skel& s, const SkelClip& clip, float t,
                 const std::vector<std::vector<const SkelChannel*>>& byNode,
                 std::vector<M4>& globals) {
    const size_t n = s.nodes.size();
    // Locals first, globals second, because a PARENT IS NOT GUARANTEED TO COME
    // BEFORE ITS CHILD. ufbx orders its nodes that way, so an fbx-only version
    // of this could compose in one pass - but glTF node order is arbitrary,
    // and a single forward pass silently treats any child whose parent sits
    // later in the array as a root. That is invisible on most rigs and shows
    // up on the rest as vertices in the wrong place (measured: 0.42 units of
    // error against the parser's own bake on a 38-node character).
    std::vector<M4> locals(n);
    for (size_t i = 0; i < n; ++i) {
        const SkelNode& nd = s.nodes[i];
        if (nd.hasMatrix) {
            std::memcpy(locals[i].m, nd.matrix, sizeof(locals[i].m));
            continue;
        }
        float tt[3] = {nd.t[0], nd.t[1], nd.t[2]};
        float rr[4] = {nd.r[0], nd.r[1], nd.r[2], nd.r[3]};
        float ss[3] = {nd.s[0], nd.s[1], nd.s[2]};
        for (const SkelChannel* ch : byNode[i]) {
            if (ch->path == 0)
                sampleChannel(*ch, t, 3, tt);
            else if (ch->path == 1)
                sampleChannel(*ch, t, 4, rr);
            else
                sampleChannel(*ch, t, 3, ss);
        }
        const float len = std::sqrt(rr[0] * rr[0] + rr[1] * rr[1] +
                                    rr[2] * rr[2] + rr[3] * rr[3]);
        if (len > 1e-9f)
            for (int c = 0; c < 4; ++c) rr[c] /= len;
        locals[i] = trs(tt, rr, ss);
    }

    composeGlobals(s, locals, globals);
}

// Locals -> globals along the parent chains; parents are NOT guaranteed to
// precede children (the glTF lesson), hence the chain walk.
void composeGlobals(const Skel& s, const std::vector<M4>& locals,
                    std::vector<M4>& globals) {
    const size_t n = s.nodes.size();
    globals.assign(n, M4{});
    std::vector<char> done(n, 0);
    std::vector<int> chain;
    for (size_t i = 0; i < n; ++i) {
        if (done[i]) continue;
        // Walk up to the first resolved ancestor (or a root), then compose
        // back down - each node is composed exactly once overall.
        chain.clear();
        int cur = (int)i;
        while (cur >= 0 && cur < (int)n && !done[(size_t)cur]) {
            chain.push_back(cur);
            const int parent = s.nodes[(size_t)cur].parent;
            if (parent == cur) break;  // malformed file: self-parent
            cur = parent;
        }
        const bool haveBase = cur >= 0 && cur < (int)n && done[(size_t)cur];
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const int node = *it;
            const int parent = s.nodes[(size_t)node].parent;
            const bool hasParent =
                parent >= 0 && parent < (int)n && parent != node &&
                (done[(size_t)parent] || (haveBase && parent == cur));
            globals[(size_t)node] =
                hasParent ? mul(globals[(size_t)parent], locals[(size_t)node])
                          : locals[(size_t)node];
            done[(size_t)node] = 1;
        }
    }
}

}  // namespace

int resolveBoneName(const Skel& target, const std::string& donorName,
                    const MergeOptions& options) {
    return Resolver(target, options).resolve(donorName);
}

namespace {

// Bone-name tokens for the fuzzy suggester: split on separators AND camelCase
// boundaries, lowercase, collapse the side words - so "mixamorig:LeftUpLeg"
// and "UpperLeg_L" both carry an "l" token and comparable stems.
// The vocabulary map: every rig family names the same bone differently
// (Rigify upper_arm, UE upperarm, Mixamo Arm, DAZ ShldrBend...), and token
// overlap sees none of that. Both sides canonicalize BEFORE comparing, so
// the table only has to reach a shared word, not be complete.
const char* boneSynonym(const std::string& t) {
    struct Row { const char* from, *to; };
    static const Row kRows[] = {
        {"left", "l"}, {"right", "r"},
        {"pelvis", "hips"}, {"root", "hips"}, {"cog", "hips"},
        {"torso", "spine"}, {"chest", "spine"}, {"body", "spine"},
        {"collar", "clavicle"}, {"collarbone", "clavicle"},
        {"shoulder", "clavicle"}, {"shldr", "clavicle"},
        {"upperarm", "arm"}, {"uparm", "arm"}, {"bicep", "arm"},
        {"forearm", "lowerarm"}, {"elbow", "lowerarm"},
        {"wrist", "hand"},
        {"upleg", "thigh"}, {"upperleg", "thigh"}, {"hip", "thigh"},
        {"leg", "shin"}, {"lowerleg", "shin"}, {"calf", "shin"},
        {"knee", "shin"},
        {"ankle", "foot"}, {"ball", "toe"}, {"toebase", "toe"},
        {"skull", "head"},
        {"finger", "f"}, {"pinkie", "pinky"},
        {"upper", "up"}, {"lower", "low"},
    };
    for (const Row& r : kRows)
        if (t == r.from) return r.to;
    return nullptr;
}

std::vector<std::string> boneTokens(const std::string& raw) {
    // Namespace off first, same rule as normalize().
    std::string name = raw;
    if (const size_t pos = name.find_last_of(":|");
        pos != std::string::npos && pos + 1 < name.size())
        name = name.substr(pos + 1);
    std::vector<std::string> tokens;
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        // Numeric tokens lose leading zeros, so spine01 == spine_1 == Spine1.
        if (std::isdigit((unsigned char)cur[0])) {
            size_t z = 0;
            while (z + 1 < cur.size() && cur[z] == '0') ++z;
            cur = cur.substr(z);
        } else if (const char* syn = boneSynonym(cur)) {
            cur = syn;
        }
        tokens.push_back(cur);
        cur.clear();
    };
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!std::isalnum((unsigned char)c)) {
            flush();
            continue;
        }
        // Boundaries: an upper after a lower (camelCase), and any
        // letter<->digit switch, start a new token.
        const bool digitSwitch =
            !cur.empty() && (std::isdigit((unsigned char)c) !=
                             std::isdigit((unsigned char)cur.back()));
        if ((std::isupper((unsigned char)c) && !cur.empty() &&
             std::islower((unsigned char)cur.back())) ||
            digitSwitch)
            flush();
        cur += (char)std::tolower((unsigned char)c);
    }
    flush();
    return tokens;
}

// Longest common subsequence ratio of two short strings - the half of the
// score that survives single-token names ("joint1" vs "rig_joint1a"), where
// token overlap has nothing to count.
float lcsRatio(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return 0.0f;
    std::vector<int> prev(b.size() + 1, 0), cur(b.size() + 1, 0);
    for (size_t i = 1; i <= a.size(); ++i) {
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = a[i - 1] == b[j - 1]
                         ? prev[j - 1] + 1
                         : (prev[j] > cur[j - 1] ? prev[j] : cur[j - 1]);
        prev.swap(cur);
    }
    return 2.0f * (float)prev[b.size()] / (float)(a.size() + b.size());
}

// Token overlap (Dice) blended with the LCS of the joined names, under a hard
// side guard: a bone carrying "l" may never suggest one carrying "r" - a
// fuzzy score cannot be allowed to bend the wrong limb.
float boneScore(const std::vector<std::string>& a,
                const std::vector<std::string>& b) {
    if (a.empty() || b.empty()) return 0.0f;
    auto side = [](const std::vector<std::string>& t) {
        for (const std::string& s : t)
            if (s == "l" || s == "r") return s[0];
        return '\0';
    };
    const char sa = side(a), sb = side(b);
    if (sa != sb && sa && sb) return 0.0f;
    std::vector<std::string> bs = b;
    int hit = 0;
    std::string ja, jb;
    for (const std::string& t : a) ja += t;
    for (const std::string& t : b) jb += t;
    for (const std::string& t : a) {
        auto it = std::find(bs.begin(), bs.end(), t);
        if (it == bs.end()) continue;
        bs.erase(it);
        ++hit;
    }
    const float dice = 2.0f * (float)hit / (float)(a.size() + b.size());
    const float lcs = lcsRatio(ja, jb);
    return dice > lcs ? dice : lcs;
}

}  // namespace

// Everything one bone contributes to the matching, precomputed per skeleton:
// tokens, bone-tree shape and normalized bind geometry. `parentBone` chains
// bones across helper nodes, the same walk the canvas draws lines with.
struct BoneInfo {
    int node = -1;
    int parentBone = -1;  // index into the BoneInfo vector, -1 = root bone
    int depth = 0;        // along the BONE chain
    int subtree = 1;      // bones in this bone's subtree, self included
    int childCount = 0;   // direct bone children
    std::vector<std::string> tokens;
    char side = 0;      // 'l' / 'r' from tokens
    float geoSide = 0;  // sign of bind X when clearly off the middle
    float pos[3] = {0, 0, 0};  // bind position, centered + height-normalized
    float dir[3] = {0, 0, 0};  // toward the bone children (0 = leaf)
};

std::vector<BoneInfo> buildBoneInfos(const Skel& s) {
    std::vector<char> isBone, isRoot;
    boneSets(s, isBone, isRoot);
    std::vector<float> xyz;
    bindGlobals(s, xyz);

    std::vector<int> nodeToBone(s.nodes.size(), -1);
    std::vector<BoneInfo> bones;
    for (size_t i = 0; i < s.nodes.size(); ++i) {
        if (!isBone[i]) continue;
        nodeToBone[i] = (int)bones.size();
        BoneInfo b;
        b.node = (int)i;
        b.tokens = boneTokens(s.nodes[i].name);
        for (const std::string& t : b.tokens)
            if (t == "l" || t == "r") b.side = t[0];
        bones.push_back(std::move(b));
    }
    // Parent-bone chain (nodes are not ordered, walk the node parents).
    for (BoneInfo& b : bones) {
        int p = s.nodes[(size_t)b.node].parent;
        while (p >= 0 && p < (int)s.nodes.size() && nodeToBone[(size_t)p] < 0)
            p = s.nodes[(size_t)p].parent == p ? -1 : s.nodes[(size_t)p].parent;
        b.parentBone = p >= 0 && p < (int)s.nodes.size() ? nodeToBone[(size_t)p]
                                                         : -1;
    }
    // Depth / children / subtree (iterate depths - the vector is unordered).
    for (size_t bi = 0; bi < bones.size(); ++bi) {
        int d = 0;
        for (int p = bones[bi].parentBone; p >= 0; p = bones[(size_t)p].parentBone)
            ++d;
        bones[bi].depth = d;
        if (bones[bi].parentBone >= 0)
            ++bones[(size_t)bones[bi].parentBone].childCount;
    }
    for (size_t bi = 0; bi < bones.size(); ++bi)
        for (int p = bones[bi].parentBone; p >= 0; p = bones[(size_t)p].parentBone)
            ++bones[(size_t)p].subtree;
    // Geometry: center on the bone centroid, scale by the largest extent.
    float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
    for (const BoneInfo& b : bones)
        for (int c = 0; c < 3; ++c) {
            const float v = xyz[(size_t)b.node * 3 + (size_t)c];
            mn[c] = std::min(mn[c], v);
            mx[c] = std::max(mx[c], v);
        }
    float span = 1e-6f;
    for (int c = 0; c < 3; ++c) span = std::max(span, mx[c] - mn[c]);
    const float midX = (mn[0] + mx[0]) * 0.5f;
    for (BoneInfo& b : bones) {
        for (int c = 0; c < 3; ++c)
            b.pos[c] = (xyz[(size_t)b.node * 3 + (size_t)c] -
                        (mn[c] + mx[c]) * 0.5f) /
                       span;
        const float rawX = xyz[(size_t)b.node * 3] - midX;
        if (std::fabs(rawX) > span * 0.04f) b.geoSide = rawX > 0 ? 1.0f : -1.0f;
    }
    for (size_t bi = 0; bi < bones.size(); ++bi) {
        const int p = bones[bi].parentBone;
        if (p < 0) continue;
        float d[3];
        float len = 0;
        for (int c = 0; c < 3; ++c) {
            d[c] = bones[bi].pos[c] - bones[(size_t)p].pos[c];
            len += d[c] * d[c];
        }
        len = std::sqrt(len);
        if (len > 1e-6f)
            for (int c = 0; c < 3; ++c)
                bones[(size_t)p].dir[c] += d[c] / len;  // summed, then normed
    }
    for (BoneInfo& b : bones) {
        const float len = std::sqrt(b.dir[0] * b.dir[0] + b.dir[1] * b.dir[1] +
                                    b.dir[2] * b.dir[2]);
        if (len > 1e-6f)
            for (int c = 0; c < 3; ++c) b.dir[c] /= len;
    }
    return bones;
}

std::vector<BoneSuggestion> suggestBoneMap(
    const Skel& target, const Skel& donor, const MergeOptions& options,
    const std::map<std::string, std::string>* aliases) {
    Resolver res(target, options);
    const std::vector<BoneInfo> tb = buildBoneInfos(target);
    const std::vector<BoneInfo> db = buildBoneInfos(donor);
    std::map<int, int> tNodeToBone;
    for (size_t i = 0; i < tb.size(); ++i) tNodeToBone[tb[i].node] = (int)i;

    // mapped[donor bone] = target bone; seeded by the name chain, grown by
    // the accepted suggestions each round (that is what feeds consistency).
    std::vector<int> mapped(db.size(), -1);
    std::vector<char> taken(tb.size(), 0);
    std::vector<int> unmatched;
    for (size_t i = 0; i < db.size(); ++i) {
        const int hit = res.resolve(donor.nodes[(size_t)db[i].node].name);
        const auto it = hit >= 0 ? tNodeToBone.find(hit) : tNodeToBone.end();
        if (it != tNodeToBone.end()) {
            mapped[i] = it->second;
            taken[(size_t)it->second] = 1;
        } else {
            unmatched.push_back((int)i);
        }
    }

    float maxSubtree = 1.0f;
    for (const BoneInfo& b : db) maxSubtree = std::max(maxSubtree, (float)b.subtree);

    auto pairScore = [&](const BoneInfo& d, const BoneInfo& t) {
        // Hard side guard first - token side, then clear geometric side.
        if (d.side && t.side && d.side != t.side) return 0.0f;
        if (d.geoSide != 0 && t.geoSide != 0 && d.geoSide != t.geoSide &&
            !d.side && !t.side)
            return 0.0f;
        float name = boneScore(d.tokens, t.tokens);
        if (aliases) {
            const auto it =
                aliases->find(canonicalBoneKey(donor.nodes[(size_t)d.node].name));
            if (it != aliases->end() &&
                it->second == target.nodes[(size_t)t.node].name)
                name = std::max(name, 0.97f);
        }
        float structS = 1.0f;
        structS -= std::min(1.0f, std::fabs((float)(d.depth - t.depth)) / 6.0f) * 0.5f;
        structS -= std::min(1.0f, std::fabs((float)(d.subtree - t.subtree)) /
                                      maxSubtree) * 0.3f;
        if (d.childCount != t.childCount) structS -= 0.2f;
        structS = std::max(0.0f, structS);
        float dx = d.pos[0] - t.pos[0], dy = d.pos[1] - t.pos[1],
              dz = d.pos[2] - t.pos[2];
        const float posD = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float posS = std::max(0.0f, 1.0f - posD * 1.5f);
        const bool dLeaf = d.dir[0] == 0 && d.dir[1] == 0 && d.dir[2] == 0;
        const bool tLeaf = t.dir[0] == 0 && t.dir[1] == 0 && t.dir[2] == 0;
        const float dot = d.dir[0] * t.dir[0] + d.dir[1] * t.dir[1] +
                          d.dir[2] * t.dir[2];
        const float dirS = (dLeaf || tLeaf) ? 0.5f : (dot + 1.0f) * 0.5f;
        const float geo = 0.6f * posS + 0.4f * dirS;
        // Consistency: my nearest mapped ancestor must map to an ancestor of
        // the candidate. Roots pair with roots.
        float cons = 0.4f;  // no mapped ancestor yet - neutral
        int a = d.parentBone;
        int hops = 0;
        while (a >= 0 && mapped[(size_t)a] < 0 && hops < 4) {
            a = db[(size_t)a].parentBone;
            ++hops;
        }
        if (d.parentBone < 0) {
            cons = t.parentBone < 0 ? 1.0f : 0.0f;
        } else if (a >= 0 && mapped[(size_t)a] >= 0) {
            cons = 0.0f;
            int ta = t.parentBone;
            for (int h = 0; ta >= 0 && h < 4 + hops; ++h) {
                if (ta == mapped[(size_t)a]) {
                    cons = 1.0f;
                    break;
                }
                ta = tb[(size_t)ta].parentBone;
            }
        }
        return 0.5f * name + 0.15f * structS + 0.2f * geo + 0.15f * cons;
    };

    // Iterate: each round's accepted pairs anchor the next round's
    // consistency, which is how a chain with useless names walks itself down
    // from an anchored root. Deterministic: stable sort, index tie-breaks.
    std::vector<BoneSuggestion> out;
    for (int round = 0; round < 8; ++round) {
        struct Cand {
            int d, t;
            float score;
        };
        std::vector<Cand> cands;
        for (int di : unmatched) {
            if (mapped[(size_t)di] >= 0) continue;
            for (size_t ti = 0; ti < tb.size(); ++ti) {
                if (taken[ti]) continue;
                const float sc = pairScore(db[(size_t)di], tb[ti]);
                if (sc >= 0.5f) cands.push_back({di, (int)ti, sc});
            }
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a,
                                                        const Cand& b) {
            if (a.score != b.score) return a.score > b.score;
            if (a.d != b.d) return a.d < b.d;
            return a.t < b.t;
        });
        bool any = false;
        for (const Cand& c : cands) {
            if (mapped[(size_t)c.d] >= 0 || taken[(size_t)c.t]) continue;
            mapped[(size_t)c.d] = c.t;
            taken[(size_t)c.t] = 1;
            out.push_back({donor.nodes[(size_t)db[(size_t)c.d].node].name,
                           target.nodes[(size_t)tb[(size_t)c.t].node].name,
                           c.score});
            any = true;
        }
        if (!any) break;
    }
    return out;
}

std::string canonicalBoneKey(const std::string& name) {
    std::string key;
    for (const std::string& t : boneTokens(name)) {
        if (!key.empty()) key += '.';
        key += t;
    }
    return key;
}

std::string AffixRule::describe() const {
    auto q = [](const std::string& s) { return "'" + s + "'"; };
    std::string d;
    if (!stripPrefix.empty() || !stripSuffix.empty()) {
        d = "strip ";
        if (!stripPrefix.empty()) d += q(stripPrefix);
        if (!stripPrefix.empty() && !stripSuffix.empty()) d += "+";
        if (!stripSuffix.empty()) d += q(stripSuffix);
    } else {
        d = "add ";
        if (!addPrefix.empty()) d += q(addPrefix);
        if (!addPrefix.empty() && !addSuffix.empty()) d += "+";
        if (!addSuffix.empty()) d += q(addSuffix);
    }
    return d;
}

bool detectAffixRule(const Skel& target, const Skel& donor,
                     const MergeOptions& options, AffixRule& out) {
    Resolver res(target, options);
    std::vector<char> tIsBone, tIsRoot, dIsBone, dIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    boneSets(donor, dIsBone, dIsRoot);
    std::vector<std::string> unmatched, targets;
    for (size_t i = 0; i < donor.nodes.size(); ++i)
        if (dIsBone[i] && res.resolve(donor.nodes[i].name) < 0)
            unmatched.push_back(donor.nodes[i].name);
    for (size_t i = 0; i < target.nodes.size(); ++i)
        if (tIsBone[i]) targets.push_back(target.nodes[i].name);
    if (unmatched.size() < 3) return false;

    // Vote: every (donor, target) containment yields one candidate rule.
    std::map<std::string, int> votes;
    for (const std::string& dn : unmatched)
        for (const std::string& tn : targets) {
            if (tn.size() >= 3 && dn.size() > tn.size()) {
                const size_t at = dn.find(tn);
                if (at != std::string::npos)
                    ++votes["D\x01" + dn.substr(0, at) + "\x01" +
                            dn.substr(at + tn.size())];
            }
            if (dn.size() >= 3 && tn.size() > dn.size()) {
                const size_t at = tn.find(dn);
                if (at != std::string::npos)
                    ++votes["A\x01" + tn.substr(0, at) + "\x01" +
                            tn.substr(at + dn.size())];
            }
        }
    std::string best;
    int bestVotes = 0;
    for (const auto& [rule, v] : votes)
        if (v > bestVotes) best = rule, bestVotes = v;
    if (bestVotes < 3) return false;

    AffixRule rule;
    const size_t s1 = best.find('\x01'), s2 = best.find('\x01', s1 + 1);
    if (best[0] == 'D') {
        rule.stripPrefix = best.substr(2, s2 - 2);
        rule.stripSuffix = best.substr(s2 + 1);
    } else {
        rule.addPrefix = best.substr(2, s2 - 2);
        rule.addSuffix = best.substr(s2 + 1);
    }
    // The honest count: how many pairs it really produces.
    rule.matches = (int)applyAffixRule(target, donor, options, rule).size();
    if (rule.matches < 3 || rule.matches < (int)unmatched.size() / 3)
        return false;
    out = rule;
    return true;
}

std::vector<std::pair<std::string, std::string>> applyAffixRule(
    const Skel& target, const Skel& donor, const MergeOptions& options,
    const AffixRule& rule) {
    Resolver res(target, options);
    std::vector<char> tIsBone, tIsRoot, dIsBone, dIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    boneSets(donor, dIsBone, dIsRoot);
    std::set<std::string> targetBones;
    for (size_t i = 0; i < target.nodes.size(); ++i)
        if (tIsBone[i]) targetBones.insert(target.nodes[i].name);
    std::vector<std::pair<std::string, std::string>> pairs;
    std::set<std::string> usedTargets;
    for (size_t i = 0; i < donor.nodes.size(); ++i) {
        if (!dIsBone[i]) continue;
        const std::string& dn = donor.nodes[i].name;
        if (res.resolve(dn) >= 0) continue;
        std::string tn;
        if (!rule.stripPrefix.empty() || !rule.stripSuffix.empty()) {
            if (dn.size() <= rule.stripPrefix.size() + rule.stripSuffix.size())
                continue;
            if (dn.rfind(rule.stripPrefix, 0) != 0) continue;
            if (!rule.stripSuffix.empty() &&
                dn.substr(dn.size() - rule.stripSuffix.size()) !=
                    rule.stripSuffix)
                continue;
            tn = dn.substr(rule.stripPrefix.size(),
                           dn.size() - rule.stripPrefix.size() -
                               rule.stripSuffix.size());
        } else {
            tn = rule.addPrefix + dn + rule.addSuffix;
        }
        if (!targetBones.count(tn) || usedTargets.count(tn)) continue;
        usedTargets.insert(tn);
        pairs.emplace_back(dn, tn);
    }
    return pairs;
}

void bindGlobals(const Skel& skel, std::vector<float>& xyz) {
    // Bind pose = no channels: reuse the pose composer with an empty bucket
    // list, so this cannot drift from how the merge itself poses nodes.
    static const SkelClip kNone{};
    std::vector<std::vector<const SkelChannel*>> byNode(skel.nodes.size());
    std::vector<M4> globals;
    poseGlobals(skel, kNone, 0.0f, byNode, globals);
    xyz.resize(skel.nodes.size() * 3);
    for (size_t i = 0; i < globals.size(); ++i) {
        xyz[i * 3 + 0] = globals[i].m[12];
        xyz[i * 3 + 1] = globals[i].m[13];
        xyz[i * 3 + 2] = globals[i].m[14];
    }
}

float compatibility(const Skel& target, const Skel& donor,
                    const MergeOptions& options) {
    Resolver res(target, options);
    std::set<std::string> animated;
    for (const SkelClip& c : donor.clips)
        for (const SkelChannel& ch : c.channels)
            if (ch.node >= 0 && ch.node < (int)donor.nodes.size())
                animated.insert(donor.nodes[(size_t)ch.node].name);
    if (animated.empty()) return 0.0f;
    int matched = 0;
    for (const std::string& n : animated)
        if (res.resolve(n) >= 0) ++matched;
    return (float)matched / (float)animated.size();
}

// ===========================================================================
// The full retargeter (docs/animation-import.md, "How the retarget works").
// Everything below works in WORLD space, which is what buys the three
// properties at once: per-bone axis conventions cancel (a world delta knows
// nothing about which local axis runs along the bone), units cancel (world
// space IS model space), and the A-pose/T-pose difference reduces to one
// per-bone reference rotation computed from bind bone directions.

namespace {

void quatMulQ(const float a[4], const float b[4], float out[4]) {
    const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
    out[0] = x, out[1] = y, out[2] = z, out[3] = w;
}

void quatNorm(float q[4]) {
    const float len =
        std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-12f)
        for (int c = 0; c < 4; ++c) q[c] /= len;
    else
        q[0] = q[1] = q[2] = 0, q[3] = 1;
}

// Rotation quat of an affine M4, scale divided out column-wise.
void quatFromM4(const M4& m, float q[4]) {
    float c[3][3];
    for (int col = 0; col < 3; ++col) {
        const float len =
            std::sqrt(m.m[col * 4] * m.m[col * 4] +
                      m.m[col * 4 + 1] * m.m[col * 4 + 1] +
                      m.m[col * 4 + 2] * m.m[col * 4 + 2]);
        const float inv = len > 1e-12f ? 1.0f / len : 0.0f;
        for (int row = 0; row < 3; ++row) c[col][row] = m.m[col * 4 + row] * inv;
    }
    const float tr = c[0][0] + c[1][1] + c[2][2];
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q[3] = 0.25f * s;
        q[0] = (c[1][2] - c[2][1]) / s;
        q[1] = (c[2][0] - c[0][2]) / s;
        q[2] = (c[0][1] - c[1][0]) / s;
    } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
        const float s = std::sqrt(1.0f + c[0][0] - c[1][1] - c[2][2]) * 2.0f;
        q[3] = (c[1][2] - c[2][1]) / s;
        q[0] = 0.25f * s;
        q[1] = (c[1][0] + c[0][1]) / s;
        q[2] = (c[2][0] + c[0][2]) / s;
    } else if (c[1][1] > c[2][2]) {
        const float s = std::sqrt(1.0f + c[1][1] - c[0][0] - c[2][2]) * 2.0f;
        q[3] = (c[2][0] - c[0][2]) / s;
        q[0] = (c[1][0] + c[0][1]) / s;
        q[1] = 0.25f * s;
        q[2] = (c[2][1] + c[1][2]) / s;
    } else {
        const float s = std::sqrt(1.0f + c[2][2] - c[0][0] - c[1][1]) * 2.0f;
        q[3] = (c[0][1] - c[1][0]) / s;
        q[0] = (c[2][0] + c[0][2]) / s;
        q[1] = (c[2][1] + c[1][2]) / s;
        q[2] = 0.25f * s;
    }
    quatNorm(q);
}

// Minimal-arc rotation taking unit vector a onto unit vector b.
void quatArc(const float a[3], const float b[3], float q[4]) {
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    if (d > 1.0f - 1e-6f) {
        q[0] = q[1] = q[2] = 0, q[3] = 1;
        return;
    }
    if (d < -1.0f + 1e-6f) {
        // Opposite: pick any perpendicular axis.
        float ax[3] = {1, 0, 0};
        if (std::fabs(a[0]) > 0.9f) ax[0] = 0, ax[1] = 1;
        float c[3] = {a[1] * ax[2] - a[2] * ax[1], a[2] * ax[0] - a[0] * ax[2],
                      a[0] * ax[1] - a[1] * ax[0]};
        const float len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        for (int i = 0; i < 3; ++i) q[i] = c[i] / (len > 1e-9f ? len : 1.0f);
        q[3] = 0;
        return;
    }
    q[0] = a[1] * b[2] - a[2] * b[1];
    q[1] = a[2] * b[0] - a[0] * b[2];
    q[2] = a[0] * b[1] - a[1] * b[0];
    q[3] = 1.0f + d;
    quatNorm(q);
}

// The twist component of q about `axis` (swing-twist decomposition).
void quatTwist(const float q[4], const float axis[3], float out[4]) {
    const float d = q[0] * axis[0] + q[1] * axis[1] + q[2] * axis[2];
    out[0] = axis[0] * d, out[1] = axis[1] * d, out[2] = axis[2] * d;
    out[3] = q[3];
    quatNorm(out);
}

void quatHalf(const float q[4], float out[4]) {  // sqrt: half the angle
    out[0] = q[0], out[1] = q[1], out[2] = q[2], out[3] = q[3] + 1.0f;
    quatNorm(out);
}

void quatConj(const float q[4], float out[4]) {
    out[0] = -q[0], out[1] = -q[1], out[2] = -q[2], out[3] = q[3];
}

M4 m4FromQuat(const float q[4]) {
    const float t[3] = {0, 0, 0}, sc[3] = {1, 1, 1};
    return trs(t, q, sc);
}

// General affine inverse (the parent chain carries scale on scaled rigs).
M4 invertAffine(const M4& m) {
    const float a00 = m.m[0], a01 = m.m[4], a02 = m.m[8];
    const float a10 = m.m[1], a11 = m.m[5], a12 = m.m[9];
    const float a20 = m.m[2], a21 = m.m[6], a22 = m.m[10];
    float det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) +
                a02 * (a10 * a21 - a11 * a20);
    if (std::fabs(det) < 1e-20f) det = det < 0 ? -1e-20f : 1e-20f;
    const float d = 1.0f / det;
    M4 r;
    r.m[0] = (a11 * a22 - a12 * a21) * d;
    r.m[4] = (a02 * a21 - a01 * a22) * d;
    r.m[8] = (a01 * a12 - a02 * a11) * d;
    r.m[1] = (a12 * a20 - a10 * a22) * d;
    r.m[5] = (a00 * a22 - a02 * a20) * d;
    r.m[9] = (a02 * a10 - a00 * a12) * d;
    r.m[2] = (a10 * a21 - a11 * a20) * d;
    r.m[6] = (a01 * a20 - a00 * a21) * d;
    r.m[10] = (a00 * a11 - a01 * a10) * d;
    r.m[3] = r.m[7] = r.m[11] = 0;
    const float tx = m.m[12], ty = m.m[13], tz = m.m[14];
    r.m[12] = -(r.m[0] * tx + r.m[4] * ty + r.m[8] * tz);
    r.m[13] = -(r.m[1] * tx + r.m[5] * ty + r.m[9] * tz);
    r.m[14] = -(r.m[2] * tx + r.m[6] * ty + r.m[10] * tz);
    r.m[15] = 1;
    return r;
}

M4 rotYM4(float rad) {
    M4 r;
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0] = c, r.m[2] = -s, r.m[8] = s, r.m[10] = c;
    return r;
}

// diag(-1,1,1) - the YZ mirror. M*G*M of a proper rotation is proper again.
M4 mirrorM4() {
    M4 r;
    r.m[0] = -1;
    return r;
}

// The horizontal direction a rig faces, read from its own feet: centroid of
// the "toe" bones minus centroid of the "foot" bones (by canonical tokens).
// False when the rig has no readable feet - the caller then assumes aligned.
bool feetForward(const Skel& s, const std::vector<M4>& bind, float out[2]) {
    std::vector<char> isBone, isRoot;
    boneSets(s, isBone, isRoot);
    float toe[3] = {0, 0, 0}, foot[3] = {0, 0, 0};
    int nToe = 0, nFoot = 0;
    for (size_t i = 0; i < s.nodes.size(); ++i) {
        if (!isBone[i]) continue;
        bool isToe = false, isFoot = false;
        for (const std::string& t : boneTokens(s.nodes[i].name)) {
            if (t == "toe") isToe = true;
            if (t == "foot") isFoot = true;
        }
        if (isToe && !isFoot) {
            for (int c = 0; c < 3; ++c) toe[c] += bind[i].m[12 + c];
            ++nToe;
        } else if (isFoot) {
            for (int c = 0; c < 3; ++c) foot[c] += bind[i].m[12 + c];
            ++nFoot;
        }
    }
    if (!nToe || !nFoot) return false;
    const float dx = toe[0] / nToe - foot[0] / nFoot;
    const float dz = toe[2] / nToe - foot[2] / nFoot;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4f) return false;
    out[0] = dx / len, out[1] = dz / len;
    return true;
}

// Everything the per-sample work needs, computed once per import.
struct RetargetCtx {
    bool full = false;
    bool mirror = false;
    float gapDeg = 0, facingDeg = 0, heightRatio = 1;
    M4 face;  // the world yaw (facing) applied to the donor
    struct Pair {
        int dNode = -1, tNode = -1;
        float refQ[4] = {0, 0, 0, 1};  // target-bind -> donor-bind-pose arc
        bool isRoot = false;
    };
    std::vector<Pair> pairs;
    std::vector<int> tPairOf;  // target node -> pair index, -1
    std::vector<M4> dBind, tBind;  // donor bind already mirrored + faced
    std::vector<int> tTopo;        // target nodes, parents before children
    // Unmapped target bones sitting between two mapped ones - they take half
    // their child's twist, and the merge must EMIT channels for them (they
    // are not pairs, which is exactly how the first version lost them).
    struct Twist {
        int u = -1, child = -1;
        float axis[3] = {1, 0, 0};
    };
    std::vector<Twist> twist;
};

M4 donorXform(const RetargetCtx& ctx, const M4& g) {
    if (!ctx.mirror) return mul(ctx.face, g);
    const M4 mi = mirrorM4();
    return mul(ctx.face, mul(mi, mul(g, mi)));
}

RetargetCtx buildRetargetCtx(const Skel& target, const Skel& donor,
                             const MergeOptions& o) {
    RetargetCtx ctx;
    ctx.mirror = o.mirror;

    // Bind globals. The donor's get the mirror folded in before anything
    // reads directions off them.
    {
        static const SkelClip kNone{};
        std::vector<std::vector<const SkelChannel*>> byNode;
        std::vector<M4> g;
        byNode.assign(donor.nodes.size(), {});
        poseGlobals(donor, kNone, 0.0f, byNode, g);
        ctx.dBind.resize(g.size());
        const M4 mi = mirrorM4();
        for (size_t i = 0; i < g.size(); ++i)
            ctx.dBind[i] = ctx.mirror ? mul(mi, mul(g[i], mi)) : g[i];
        byNode.assign(target.nodes.size(), {});
        poseGlobals(target, kNone, 0.0f, byNode, ctx.tBind);
    }

    // Facing: told, or read from both rigs' feet.
    float yaw = 0.0f;
    if (o.facingOverride >= 0) {
        yaw = (float)o.facingOverride * 3.14159265f / 180.0f;
    } else {
        float fd[2], ft[2];
        if (feetForward(donor, ctx.dBind, fd) &&
            feetForward(target, ctx.tBind, ft))
            yaw = std::atan2(ft[1], ft[0]) - std::atan2(fd[1], fd[0]);
    }
    ctx.facingDeg = yaw * 180.0f / 3.14159265f;
    // Wrap to (-180, 180] for reporting; the matrix does not care.
    while (ctx.facingDeg > 180.0f) ctx.facingDeg -= 360.0f;
    while (ctx.facingDeg <= -180.0f) ctx.facingDeg += 360.0f;
    ctx.face = rotYM4(yaw);
    for (M4& g : ctx.dBind) g = mul(ctx.face, g);

    // The mapped pairs, mirror swapping each side bone onto its counterpart.
    Resolver res(target, o);
    std::vector<char> dIsBone, dIsRoot, tIsBone, tIsRoot;
    boneSets(donor, dIsBone, dIsRoot);
    boneSets(target, tIsBone, tIsRoot);
    std::map<std::string, int> tByKey;
    for (size_t i = 0; i < target.nodes.size(); ++i)
        if (tIsBone[i]) tByKey.emplace(canonicalBoneKey(target.nodes[i].name), (int)i);
    auto sideSwap = [&](int tn) {
        std::string key = canonicalBoneKey(target.nodes[(size_t)tn].name);
        bool swapped = false;
        size_t pos = 0;
        // token-wise: ".l." <-> ".r." plus the edges
        auto flip = [&](const std::string& from, const std::string& to) {
            std::string k2 = "." + key + ".";
            const std::string f = "." + from + ".", t = "." + to + ".";
            const size_t at = k2.find(f);
            if (at == std::string::npos) return false;
            k2 = k2.substr(0, at) + t + k2.substr(at + f.size());
            key = k2.substr(1, k2.size() - 2);
            return true;
        };
        swapped = flip("l", "r") || flip("r", "l");
        (void)pos;
        if (!swapped) return tn;  // a center bone mirrors onto itself
        const auto it = tByKey.find(key);
        return it != tByKey.end() ? it->second : tn;
    };
    ctx.tPairOf.assign(target.nodes.size(), -1);
    for (size_t i = 0; i < donor.nodes.size(); ++i) {
        if (!dIsBone[i]) continue;
        int tn = res.resolve(donor.nodes[i].name);
        if (tn < 0 || !tIsBone[(size_t)tn]) continue;
        if (ctx.mirror) tn = sideSwap(tn);
        if (ctx.tPairOf[(size_t)tn] >= 0) continue;  // one driver per bone
        RetargetCtx::Pair pr;
        pr.dNode = (int)i;
        pr.tNode = tn;
        pr.isRoot = dIsRoot[i] && tIsRoot[(size_t)tn];
        ctx.tPairOf[(size_t)tn] = (int)ctx.pairs.size();
        ctx.pairs.push_back(pr);
    }

    // Bind orientation gap - what picks the path. 2*acos|q1.q2| per pair.
    for (const auto& pr : ctx.pairs) {
        float qd[4], qt[4];
        quatFromM4(ctx.dBind[(size_t)pr.dNode], qd);
        quatFromM4(ctx.tBind[(size_t)pr.tNode], qt);
        float d = std::fabs(qd[0] * qt[0] + qd[1] * qt[1] + qd[2] * qt[2] +
                            qd[3] * qt[3]);
        if (d > 1.0f) d = 1.0f;
        const float ang = 2.0f * std::acos(d) * 180.0f / 3.14159265f;
        ctx.gapDeg = std::max(ctx.gapDeg, ang);
    }
    ctx.full = ctx.mirror || std::fabs(ctx.facingDeg) > 1.0f || ctx.gapDeg > 3.0f;

    // Per-pair reference rotation: the minimal arc taking the TARGET's bind
    // bone direction onto the DONOR's - the "rotate the target's rest into
    // the donor's rest pose" half of the formula. Bone direction = toward
    // the centroid of the bone children; a leaf inherits its parent pair's.
    auto boneDir = [&](const Skel& s, const std::vector<M4>& bind, int node,
                       const std::vector<char>& isBone, float out[3]) {
        float c[3] = {0, 0, 0};
        int cnt = 0;
        for (size_t i = 0; i < s.nodes.size(); ++i) {
            if (!isBone[i]) continue;
            // bone child = nearest bone ancestor is `node`
            int p = s.nodes[i].parent;
            while (p >= 0 && p < (int)s.nodes.size() && !isBone[(size_t)p])
                p = s.nodes[(size_t)p].parent == p ? -1 : s.nodes[(size_t)p].parent;
            if (p != node) continue;
            for (int k = 0; k < 3; ++k)
                c[k] += bind[i].m[12 + k] - bind[(size_t)node].m[12 + k];
            ++cnt;
        }
        if (!cnt) return false;
        const float len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (len < 1e-5f) return false;
        for (int k = 0; k < 3; ++k) out[k] = c[k] / len;
        return true;
    };
    // parents first so a leaf can inherit
    std::map<int, int> dPairOf;
    for (size_t k = 0; k < ctx.pairs.size(); ++k)
        dPairOf[ctx.pairs[k].dNode] = (int)k;
    for (auto& pr : ctx.pairs) {
        float dd[3], dt[3];
        if (boneDir(donor, ctx.dBind, pr.dNode, dIsBone, dd) &&
            boneDir(target, ctx.tBind, pr.tNode, tIsBone, dt)) {
            quatArc(dt, dd, pr.refQ);
        } else {
            // leaf: inherit the nearest mapped donor ancestor's arc
            int p = donor.nodes[(size_t)pr.dNode].parent;
            while (p >= 0) {
                const auto it = dPairOf.find(p);
                if (it != dPairOf.end()) {
                    std::memcpy(pr.refQ, ctx.pairs[(size_t)it->second].refQ,
                                sizeof(pr.refQ));
                    break;
                }
                p = donor.nodes[(size_t)p].parent == p
                        ? -1
                        : donor.nodes[(size_t)p].parent;
            }
        }
    }

    // Height ratio from the bone spans (world space, unit-free).
    auto heightOf = [](const Skel& s, const std::vector<M4>& bind) {
        std::vector<char> isBone, isRoot;
        boneSets(s, isBone, isRoot);
        float mn = 1e9f, mx = -1e9f;
        for (size_t i = 0; i < s.nodes.size(); ++i)
            if (isBone[i]) {
                mn = std::min(mn, bind[i].m[13]);
                mx = std::max(mx, bind[i].m[13]);
            }
        return mx > mn ? mx - mn : 1.0f;
    };
    const float hd = heightOf(donor, ctx.dBind);
    ctx.heightRatio = std::clamp(heightOf(target, ctx.tBind) /
                                     (hd > 1e-5f ? hd : 1.0f),
                                 0.05f, 20.0f);

    // Target topo order (parents first) - node parents, not bone parents.
    {
        ctx.tTopo.reserve(target.nodes.size());
        std::vector<char> done(target.nodes.size(), 0);
        std::vector<int> chain;
        for (size_t i = 0; i < target.nodes.size(); ++i) {
            if (done[i]) continue;
            chain.clear();
            int cur = (int)i;
            while (cur >= 0 && cur < (int)target.nodes.size() &&
                   !done[(size_t)cur]) {
                chain.push_back(cur);
                const int p = target.nodes[(size_t)cur].parent;
                cur = p == cur ? -1 : p;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                ctx.tTopo.push_back(*it);
                done[(size_t)*it] = 1;
            }
        }
    }
    // Twist candidates: unmapped bone u, mapped parent bone, exactly one
    // mapped bone child, non-degenerate child offset (the twist axis).
    {
        std::vector<char> tIsBone2, tIsRoot2;
        boneSets(target, tIsBone2, tIsRoot2);
        auto boneParentOf = [&](int node) {
            int p = target.nodes[(size_t)node].parent;
            while (p >= 0 && p < (int)target.nodes.size() && !tIsBone2[(size_t)p])
                p = target.nodes[(size_t)p].parent == p
                        ? -1
                        : target.nodes[(size_t)p].parent;
            return p;
        };
        for (size_t u = 0; u < target.nodes.size(); ++u) {
            if (!tIsBone2[u] || ctx.tPairOf[u] >= 0) continue;
            const int p = boneParentOf((int)u);
            if (p < 0 || ctx.tPairOf[(size_t)p] < 0) continue;
            int child = -1, childCount = 0;
            for (size_t i = 0; i < target.nodes.size(); ++i)
                if (tIsBone2[i] && boneParentOf((int)i) == (int)u) {
                    child = (int)i;
                    ++childCount;
                }
            if (childCount != 1 || ctx.tPairOf[(size_t)child] < 0) continue;
            RetargetCtx::Twist tw;
            tw.u = (int)u;
            tw.child = child;
            const SkelNode& cb = target.nodes[(size_t)child];
            const float len = std::sqrt(cb.t[0] * cb.t[0] + cb.t[1] * cb.t[1] +
                                        cb.t[2] * cb.t[2]);
            if (len < 1e-5f) continue;
            for (int c = 0; c < 3; ++c) tw.axis[c] = cb.t[c] / len;
            ctx.twist.push_back(tw);
        }
    }
    return ctx;
}

// One retargeted pose: donor globals at some time -> target LOCALS. The
// heart of both the resampling merge and the test-pose preview - one
// function, so what the canvas shows is what the bake writes.
void retargetLocals(const RetargetCtx& ctx, const Skel& target,
                    const Skel& donor, const std::vector<M4>& donorGlobals,
                    bool retargetRoot, std::vector<M4>& locals) {
    locals.resize(target.nodes.size());
    for (size_t i = 0; i < target.nodes.size(); ++i)
        locals[i] = localOf(target.nodes[i]);
    std::vector<M4> G(target.nodes.size());
    for (int node : ctx.tTopo) {
        const int parent = target.nodes[(size_t)node].parent;
        const M4 parentG = parent >= 0 && parent != node ? G[(size_t)parent] : M4{};
        const int pi = ctx.tPairOf[(size_t)node];
        if (pi >= 0) {
            const RetargetCtx::Pair& pr = ctx.pairs[(size_t)pi];
            const M4 gd = donorXform(ctx, donorGlobals[(size_t)pr.dNode]);
            const M4 delta = mul(gd, invertAffine(ctx.dBind[(size_t)pr.dNode]));
            const M4 desired =
                mul(delta, mul(m4FromQuat(pr.refQ), ctx.tBind[(size_t)node]));
            M4 L = mul(invertAffine(parentG), desired);
            float q[4];
            quatFromM4(L, q);
            const SkelNode& b = target.nodes[(size_t)node];
            if (pr.isRoot && retargetRoot) {
                // Root position: world delta scaled by the height ratio,
                // brought into the parent's frame. World space, so the
                // centimeters-under-a-scaled-armature case needs nothing.
                float wp[3];
                for (int c = 0; c < 3; ++c)
                    wp[c] = ctx.tBind[(size_t)node].m[12 + c] +
                            (gd.m[12 + c] -
                             ctx.dBind[(size_t)pr.dNode].m[12 + c]) *
                                ctx.heightRatio;
                const M4 ip = invertAffine(parentG);
                float lt[3];
                for (int r = 0; r < 3; ++r)
                    lt[r] = ip.m[r] * wp[0] + ip.m[4 + r] * wp[1] +
                            ip.m[8 + r] * wp[2] + ip.m[12 + r];
                locals[(size_t)node] = trs(lt, q, b.s);
            } else {
                locals[(size_t)node] = trs(b.t, q, b.s);
            }
        }
        G[(size_t)node] = parent >= 0 && parent != node
                              ? mul(parentG, locals[(size_t)node])
                              : locals[(size_t)node];
    }

    // Twist redistribution: an unmapped bone u sitting between two mapped
    // ones hands half of its child's twist up the chain. Exact: L_u*H and
    // H^-1*L_c compose to the original product, and H turns about the
    // child's offset axis, so no joint moves - the skin just rolls
    // gradually instead of snapping at one bone. Candidates live on the ctx
    // so the merge can emit channels for them too.
    for (const RetargetCtx::Twist& tw : ctx.twist) {
        const SkelNode& cb = target.nodes[(size_t)tw.child];
        float qBind[4] = {cb.r[0], cb.r[1], cb.r[2], cb.r[3]};
        float qNow[4], qBindC[4], qDelta[4];
        quatFromM4(locals[(size_t)tw.child], qNow);
        quatConj(qBind, qBindC);
        quatMulQ(qNow, qBindC, qDelta);
        float t4[4], h[4], hc[4];
        quatTwist(qDelta, tw.axis, t4);
        quatHalf(t4, h);
        quatConj(h, hc);
        const SkelNode& ub = target.nodes[(size_t)tw.u];
        float qu[4] = {ub.r[0], ub.r[1], ub.r[2], ub.r[3]};
        float quNew[4], qcNew[4];
        quatMulQ(qu, h, quNew);
        quatMulQ(hc, qNow, qcNew);
        locals[(size_t)tw.u] = trs(ub.t, quNew, ub.s);
        locals[(size_t)tw.child] = trs(cb.t, qcNew, cb.s);
    }
}

// RDP keep-mask, the fbxparser reduction re-stated for this TU.
void rdpMask(const std::vector<float>& times, const std::vector<float>& vals,
             int stride, int lo, int hi, float eps, std::vector<char>& keep) {
    if (hi - lo < 2) return;
    float worst = 0.0f;
    int worstIdx = -1;
    const float t0 = times[(size_t)lo], t1 = times[(size_t)hi];
    for (int i = lo + 1; i < hi; ++i) {
        const float f = (t1 - t0) > 1e-9f ? (times[(size_t)i] - t0) / (t1 - t0) : 0.0f;
        for (int c = 0; c < stride; ++c) {
            const float interp =
                vals[(size_t)lo * stride + (size_t)c] +
                (vals[(size_t)hi * stride + (size_t)c] -
                 vals[(size_t)lo * stride + (size_t)c]) * f;
            const float err =
                std::fabs(vals[(size_t)i * stride + (size_t)c] - interp);
            if (err > worst) worst = err, worstIdx = i;
        }
    }
    if (worstIdx >= 0 && worst > eps) {
        keep[(size_t)worstIdx] = 1;
        rdpMask(times, vals, stride, lo, worstIdx, eps, keep);
        rdpMask(times, vals, stride, worstIdx, hi, eps, keep);
    }
}

constexpr float kRetargetFps = 24.0f;  // the FBX importer's own rate

// The full-retarget merge: resample, retarget, reduce, emit.
bool mergeRetargeted(Skel& target, const Skel& donor, const ImportSpec& spec,
                     const RetargetCtx& ctx, MergeReport& report,
                     std::string& error) {
    if (ctx.pairs.empty()) {
        error = "no bone of the source rig matched this model's skeleton";
        return false;
    }
    // Unmatched (for the panel), same census the copy path reports.
    {
        Resolver res(target, spec.options);
        std::vector<char> dIsBone, dIsRoot;
        boneSets(donor, dIsBone, dIsRoot);
        std::set<std::string> seen;
        for (size_t i = 0; i < donor.nodes.size(); ++i)
            if (dIsBone[i] && res.resolve(donor.nodes[i].name) < 0 &&
                seen.insert(donor.nodes[i].name).second &&
                (int)report.unmatched.size() < kMaxReportedUnmatched)
                report.unmatched.push_back(donor.nodes[i].name);
    }

    int clipsConsidered = 0;
    for (const SkelClip& src : donor.clips) {
        if (!spec.clips.empty() &&
            std::find(spec.clips.begin(), spec.clips.end(), src.name) ==
                spec.clips.end())
            continue;
        ++clipsConsidered;

        const int samples =
            src.duration > 0.0f
                ? (int)std::lround(src.duration * kRetargetFps) + 1
                : 1;
        std::vector<std::vector<const SkelChannel*>> byNode(donor.nodes.size());
        for (const SkelChannel& ch : src.channels)
            if (ch.node >= 0 && ch.node < (int)donor.nodes.size())
                byNode[(size_t)ch.node].push_back(&ch);

        // quat samples per pair (twist bones appended after) + root motion
        std::vector<std::vector<float>> rot(ctx.pairs.size() +
                                            ctx.twist.size());
        std::vector<float> rootPos;
        std::vector<float> times((size_t)samples);
        std::vector<M4> dG, locals;
        for (int f = 0; f < samples; ++f) {
            const float t = samples > 1
                                ? src.duration * (float)f / (float)(samples - 1)
                                : 0.0f;
            times[(size_t)f] = t;
            poseGlobals(donor, src, t, byNode, dG);
            retargetLocals(ctx, target, donor, dG, spec.options.retargetRoot,
                           locals);
            for (size_t k = 0; k < ctx.pairs.size(); ++k) {
                float q[4];
                quatFromM4(locals[(size_t)ctx.pairs[k].tNode], q);
                // hemisphere continuity, the runtime lerps raw components
                if (f > 0) {
                    const float* prev = &rot[k][(size_t)(f - 1) * 4];
                    if (q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] +
                            q[3] * prev[3] < 0.0f)
                        for (int c = 0; c < 4; ++c) q[c] = -q[c];
                }
                rot[k].insert(rot[k].end(), q, q + 4);
                if (ctx.pairs[k].isRoot) {
                    const M4& L = locals[(size_t)ctx.pairs[k].tNode];
                    rootPos.insert(rootPos.end(),
                                   {L.m[12], L.m[13], L.m[14]});
                }
            }
            for (size_t k = 0; k < ctx.twist.size(); ++k) {
                const size_t slot = ctx.pairs.size() + k;
                float q[4];
                quatFromM4(locals[(size_t)ctx.twist[k].u], q);
                if (f > 0) {
                    const float* prev = &rot[slot][(size_t)(f - 1) * 4];
                    if (q[0] * prev[0] + q[1] * prev[1] + q[2] * prev[2] +
                            q[3] * prev[3] < 0.0f)
                        for (int c = 0; c < 4; ++c) q[c] = -q[c];
                }
                rot[slot].insert(rot[slot].end(), q, q + 4);
            }
        }

        SkelClip out;
        out.duration = src.duration;
        auto emit = [&](int node, int path, const std::vector<float>& vals,
                        int stride, const float* bindRef, float eps) {
            // constant-at-bind channels carry nothing
            bool differs = false;
            for (size_t i = 0; i < vals.size() && !differs; ++i)
                differs =
                    std::fabs(vals[i] - bindRef[i % (size_t)stride]) > eps;
            if (!differs) return;
            std::vector<char> keep((size_t)samples, 0);
            keep.front() = keep.back() = 1;
            rdpMask(times, vals, stride, 0, samples - 1, eps, keep);
            SkelChannel ch;
            ch.node = node;
            ch.path = path;
            for (int i = 0; i < samples; ++i) {
                if (!keep[(size_t)i]) continue;
                ch.times.push_back(times[(size_t)i]);
                for (int c = 0; c < stride; ++c)
                    ch.values.push_back(vals[(size_t)i * stride + (size_t)c]);
            }
            out.channels.push_back(std::move(ch));
            ++report.tracksMatched;
        };
        size_t rootAt = 0;
        for (size_t k = 0; k < ctx.pairs.size(); ++k) {
            const SkelNode& b = target.nodes[(size_t)ctx.pairs[k].tNode];
            // A quat may sit at -bind (double cover); compare via |dot|.
            bool differs = false;
            for (int f = 0; f < samples && !differs; ++f) {
                const float* q = &rot[k][(size_t)f * 4];
                const float d = std::fabs(q[0] * b.r[0] + q[1] * b.r[1] +
                                          q[2] * b.r[2] + q[3] * b.r[3]);
                differs = d < 1.0f - 1e-6f;
            }
            if (differs)
                emit(ctx.pairs[k].tNode, 1, rot[k], 4, b.r, 1e-4f);
            if (ctx.pairs[k].isRoot) rootAt = k;
        }
        for (size_t k = 0; k < ctx.twist.size(); ++k) {
            const SkelNode& b = target.nodes[(size_t)ctx.twist[k].u];
            emit(ctx.twist[k].u, 1, rot[ctx.pairs.size() + k], 4, b.r, 1e-4f);
        }
        if (!rootPos.empty() && spec.options.retargetRoot) {
            const SkelNode& rb = target.nodes[(size_t)ctx.pairs[rootAt].tNode];
            emit(ctx.pairs[rootAt].tNode, 0, rootPos, 3, rb.t, 1e-4f);
        }
        if (out.channels.empty()) continue;
        out.name = uniqueClipName(target, spec.prefix + src.name);
        report.addedClips.push_back(out.name);
        target.clips.push_back(std::move(out));
        ++report.clipsAdded;
    }
    if (report.clipsAdded == 0) {
        error = clipsConsidered == 0
                    ? "no matching clips in the source file"
                    : "the retargeted clips came out empty";
        return false;
    }
    return true;
}

}  // namespace

RetargetInfo retargetInfo(const Skel& target, const Skel& donor,
                          const MergeOptions& options) {
    const RetargetCtx ctx = buildRetargetCtx(target, donor, options);
    RetargetInfo info;
    info.full = ctx.full;
    info.bindGapDeg = ctx.gapDeg;
    info.facingDeg = ctx.facingDeg;
    return info;
}

bool merge(Skel& target, const Skel& donor, const ImportSpec& spec,
           MergeReport& report, std::string& error) {
    if (target.nodes.empty()) {
        error = "the target model has no skeleton";
        return false;
    }
    const MergeOptions& o = spec.options;
    // Which path? Identical binds copy channels verbatim (bit-exact - the
    // property the harness pins); different binds, a facing turn or a mirror
    // take the full retargeter.
    {
        const RetargetCtx ctx = buildRetargetCtx(target, donor, o);
        report.fullRetarget = ctx.full;
        report.bindGapDeg = ctx.gapDeg;
        report.facingDeg = ctx.facingDeg;
        if (ctx.full)
            return mergeRetargeted(target, donor, spec, ctx, report, error);
    }
    Resolver res(target, o);
    std::vector<char> isBone, isRootBone;
    boneSets(target, isBone, isRootBone);

    std::set<std::string> unmatchedSeen;
    int clipsConsidered = 0;

    for (const SkelClip& src : donor.clips) {
        if (!spec.clips.empty() &&
            std::find(spec.clips.begin(), spec.clips.end(), src.name) ==
                spec.clips.end())
            continue;
        ++clipsConsidered;
        SkelClip merged;
        merged.duration = src.duration;

        for (const SkelChannel& ch : src.channels) {
            if (ch.node < 0 || ch.node >= (int)donor.nodes.size()) continue;
            const std::string& donorName = donor.nodes[(size_t)ch.node].name;
            const int targetNode = res.resolve(donorName);
            if (targetNode < 0) {
                ++report.tracksDropped;
                if (unmatchedSeen.insert(donorName).second &&
                    (int)report.unmatched.size() < kMaxReportedUnmatched)
                    report.unmatched.push_back(donorName);
                continue;
            }
            if (o.skeletonTracksOnly && !isBone[(size_t)targetNode]) {
                ++report.tracksDropped;
                continue;
            }
            // Scale: dropped by default. The target keeps its bind scale.
            if (ch.path == 2 && o.ignoreScale) {
                ++report.scaleStripped;
                continue;
            }
            // Translation: this is the policy that keeps the TARGET's bone
            // lengths. A dropped channel is not a loss of information - the
            // pose evaluator falls back to the target's own bind translation,
            // which is exactly what "keep your own proportions" means.
            if (ch.path == 0) {
                bool strip = false;
                switch (o.translation) {
                    case TranslationMode::RootBonesOnly:
                        strip = isBone[(size_t)targetNode] &&
                                !isRootBone[(size_t)targetNode];
                        break;
                    case TranslationMode::AnimatedOnly:
                        strip = constantChannel(ch, 3, 1e-5f);
                        break;
                    case TranslationMode::CopyAll:
                        break;
                }
                if (strip) {
                    ++report.translationStripped;
                    continue;
                }
            }

            SkelChannel out = ch;
            out.node = targetNode;

            // Root motion: re-express the donor's hip travel around the
            // TARGET's rest hip and scale it by the two rigs' hip heights, so
            // a clip authored on a tall rig does not make a short one skate.
            if (o.retargetRoot && ch.path == 0 &&
                isRootBone[(size_t)targetNode] && !out.values.empty()) {
                float donorRest[3], targetRest[3];
                restGlobalTranslation(donor, ch.node, donorRest);
                restGlobalTranslation(target, targetNode, targetRest);
                float ratio = 1.0f;
                if (std::fabs(donorRest[1]) > 1e-3f &&
                    std::fabs(targetRest[1]) > 1e-3f)
                    ratio = std::clamp(targetRest[1] / donorRest[1], 0.05f, 20.0f);
                // The delta is read in the DONOR's local units and written in
                // the TARGET's - and those are different spaces the moment a
                // rig keeps its bones under a scaled armature node (Mixamo:
                // centimeters under a 0.01 parent). Convert through model
                // space, or a 10 cm hip sway lands as 10 meters (the exact
                // reported blow-up on Superhero_Female x Female.fbx). Both
                // factors are exactly 1.0 on unscaled rigs, so a same-unit
                // pair merges bit-identically to before.
                const float unit = parentGlobalScale(donor, ch.node) /
                                   parentGlobalScale(target, targetNode);
                const float* dLocal = donor.nodes[(size_t)ch.node].t;
                const float* tLocal = target.nodes[(size_t)targetNode].t;
                for (size_t k = 0; k + 3 <= out.values.size(); k += 3)
                    for (int c = 0; c < 3; ++c)
                        out.values[k + (size_t)c] =
                            tLocal[c] + (out.values[k + (size_t)c] - dLocal[c]) *
                                            ratio * unit;
                report.rootMotionScale = ratio;
                ++report.rootTracksRetargeted;
            }

            merged.channels.push_back(std::move(out));
            ++report.tracksMatched;
        }

        if (merged.channels.empty()) continue;  // nothing landed - skip it
        merged.name = uniqueClipName(target, spec.prefix + src.name);
        report.addedClips.push_back(merged.name);
        target.clips.push_back(std::move(merged));
        ++report.clipsAdded;
    }

    if (report.clipsAdded == 0) {
        error = clipsConsidered == 0
                    ? "no matching clips in the source file"
                    : "no bone of the source rig matched this model's skeleton";
        return false;
    }
    return true;
}

void refreshPoseBounds(Skel& skel) {
    if (skel.parts.empty() || skel.nodes.empty()) return;
    std::vector<char> isBone, isRootBone;
    boneSets(skel, isBone, isRootBone);
    std::vector<M4> globals;
    std::vector<M4> palette(skel.palette.size());
    bool first = true;
    for (const SkelClip& clip : skel.clips) {
        std::vector<std::vector<const SkelChannel*>> byNode(skel.nodes.size());
        for (const SkelChannel& ch : clip.channels)
            if (ch.node >= 0 && ch.node < (int)skel.nodes.size())
                byNode[(size_t)ch.node].push_back(&ch);
        // 6 Hz, the sparse rate parseSkel already uses for this box.
        const int steps = clip.duration > 0.0f
                              ? (int)std::lround(clip.duration * 6.0) + 1
                              : 1;
        for (int f = 0; f < steps; ++f) {
            const float t = steps > 1 ? clip.duration * (float)f / (float)(steps - 1)
                                      : 0.0f;
            poseGlobals(skel, clip, t, byNode, globals);
            for (size_t j = 0; j < skel.palette.size(); ++j) {
                const SkelJoint& joint = skel.palette[j];
                M4 ibm;
                std::memcpy(ibm.m, joint.ibm, sizeof(ibm.m));
                const M4& g = (joint.node >= 0 && joint.node < (int)globals.size())
                                  ? globals[(size_t)joint.node]
                                  : ibm;
                palette[j] = mul(g, ibm);
            }
            for (const SkelPart& sp : skel.parts)
                for (int v = 0; v < sp.vertexCount; ++v) {
                    float acc[12] = {};
                    float wsum = 0.0f;
                    for (int k = 0; k < 4; ++k) {
                        const unsigned char w = sp.weights[(size_t)v * 4 + (size_t)k];
                        if (!w) continue;
                        const unsigned char slot = sp.joints[(size_t)v * 4 + (size_t)k];
                        if (slot >= palette.size()) continue;
                        const float fw = (float)w / 255.0f;
                        wsum += fw;
                        for (int col = 0; col < 4; ++col)
                            for (int row = 0; row < 3; ++row)
                                acc[col * 3 + row] += palette[slot].m[col * 4 + row] * fw;
                    }
                    const float* P = &sp.positions[(size_t)v * 3];
                    float wp[3];
                    if (wsum <= 1e-6f) {
                        wp[0] = P[0], wp[1] = P[1], wp[2] = P[2];
                    } else {
                        const float inv = 1.0f / wsum;
                        for (int i = 0; i < 12; ++i) acc[i] *= inv;
                        for (int row = 0; row < 3; ++row)
                            wp[row] = acc[0 * 3 + row] * P[0] + acc[1 * 3 + row] * P[1] +
                                      acc[2 * 3 + row] * P[2] + acc[3 * 3 + row];
                    }
                    for (int c = 0; c < 3; ++c) {
                        if (first || wp[c] < skel.min[c]) skel.min[c] = wp[c];
                        if (first || wp[c] > skel.max[c]) skel.max[c] = wp[c];
                    }
                    first = false;
                }
        }
    }
}

const glbparser::Skel* SkelCache::get(const std::string& path,
                                      std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // Size + raw mtime ticks: compared for EQUALITY only, so the file_clock
    // epoch trap (implementation-defined, in the future on this libstdc++)
    // cannot bite - no 0-means-absent, no cross-clock comparison.
    const long long size = (long long)fs::file_size(path, ec);
    if (ec) {
        error = "cannot read '" + path + "'";
        entries_.erase(path);
        return nullptr;
    }
    const long long mtime =
        (long long)fs::last_write_time(path, ec).time_since_epoch().count();
    auto it = entries_.find(path);
    if (it == entries_.end() || it->second.size != size ||
        it->second.mtime != mtime) {
        Entry e;
        e.size = size;
        e.mtime = mtime;
        e.ok = animimport::parseSkel(path, e.skel, e.error);
        it = entries_.insert_or_assign(path, std::move(e)).first;
    }
    if (!it->second.ok) {
        error = it->second.error;
        return nullptr;
    }
    return &it->second.skel;
}

bool applyImports(const std::vector<ImportSpec>& imports, Skel& target,
                  std::vector<std::string>* warnings, SkelCache* cache) {
    auto warn = [&](const std::string& m) {
        if (warnings) warnings->push_back(m);
    };
    bool merged = false;
    for (const ImportSpec& spec : imports) {
        Skel parsed;
        const Skel* donorP = nullptr;
        std::string error;
        if (cache) {
            donorP = cache->get(spec.path, error);
        } else if (animimport::parseSkel(spec.path, parsed, error)) {
            donorP = &parsed;
        }
        if (!donorP) {
            warn("animation import '" + spec.path + "': " + error);
            continue;
        }
        const Skel& donor = *donorP;
        MergeReport report;
        if (!merge(target, donor, spec, report, error)) {
            warn("animation import '" + spec.path + "': " + error);
            continue;
        }
        merged = true;
        if (!report.unmatched.empty())
            warn("animation import '" + spec.path + "': " +
                 std::to_string(report.unmatched.size()) +
                 " source bone(s) had no counterpart (first: " +
                 report.unmatched.front() + ")");
    }
    return merged;
}

bool skelToBaked(const Skel& skel, float fps, Baked& out, std::string& error) {
    out = Baked{};
    if (skel.parts.empty()) {
        error = "no geometry in the model";
        return false;
    }
    if (fps < 1.0f) fps = 1.0f;
    out.fps = fps;

    // Frame plan: one clip after another, exactly the layout the parsers'
    // own bake produces (Baked::clips index into one shared frame list).
    std::vector<int> frameCounts;
    int total = 0;
    for (const SkelClip& c : skel.clips) {
        const int n = c.duration > 0.0f
                          ? (int)std::lround(c.duration * (double)fps) + 1
                          : 1;
        frameCounts.push_back(n < 1 ? 1 : n);
        total += frameCounts.back();
    }
    if (frameCounts.empty()) {
        frameCounts.push_back(1);
        total = 1;
    }
    out.frameCount = total;

    out.images = skel.images;
    for (const SkelPart& sp : skel.parts) {
        glbparser::Part p;
        p.material = sp.material;
        std::memcpy(p.baseColor, sp.baseColor, sizeof(p.baseColor));
        p.image = sp.image;
        p.vertexCount = sp.vertexCount;
        p.uvs = sp.uvs;
        p.positions.assign((size_t)total * (size_t)sp.vertexCount * 3, 0.0f);
        p.normals.assign((size_t)total * (size_t)sp.vertexCount * 3, 0.0f);
        out.parts.push_back(std::move(p));
    }

    // The motion root the in-place edit and the viewport's root-removal read:
    // the first root BONE, i.e. the hips (fbxparser picks the same thing by a
    // different route - it has a ufbx scene to ask).
    std::vector<char> isBone, isRootBone;
    boneSets(skel, isBone, isRootBone);
    int motionRoot = -1;
    for (size_t i = 0; i < skel.nodes.size() && motionRoot < 0; ++i)
        if (isRootBone[i]) motionRoot = (int)i;

    std::vector<M4> globals;
    std::vector<M4> palette(skel.palette.size());
    int frameBase = 0;
    for (size_t ci = 0; ci < skel.clips.size(); ++ci) {
        const SkelClip& clip = skel.clips[ci];
        const int frames = frameCounts[ci];
        glbparser::Clip bc;
        bc.name = clip.name;
        bc.firstFrame = frameBase;
        bc.frameCount = frames;
        out.clips.push_back(bc);

        // Channels bucketed per node once per clip, not per frame.
        std::vector<std::vector<const SkelChannel*>> byNode(skel.nodes.size());
        for (const SkelChannel& ch : clip.channels)
            if (ch.node >= 0 && ch.node < (int)skel.nodes.size())
                byNode[(size_t)ch.node].push_back(&ch);

        for (int f = 0; f < frames; ++f) {
            const float t = frames > 1 ? clip.duration * (float)f / (float)(frames - 1)
                                       : 0.0f;
            poseGlobals(skel, clip, t, byNode, globals);
            for (size_t j = 0; j < skel.palette.size(); ++j) {
                const SkelJoint& joint = skel.palette[j];
                M4 ibm;
                std::memcpy(ibm.m, joint.ibm, sizeof(ibm.m));
                const M4& g = (joint.node >= 0 && joint.node < (int)globals.size())
                                  ? globals[(size_t)joint.node]
                                  : globals.empty() ? ibm : globals[0];
                palette[j] = mul(g, ibm);
            }

            Baked::RootMotionSample rm;
            if (motionRoot >= 0 && motionRoot < (int)globals.size()) {
                rm.x = globals[(size_t)motionRoot].m[12];
                rm.z = globals[(size_t)motionRoot].m[14];
            }
            out.rootMotion.push_back(rm);

            for (size_t pi = 0; pi < skel.parts.size(); ++pi) {
                const SkelPart& sp = skel.parts[pi];
                glbparser::Part& dp = out.parts[pi];
                const size_t base =
                    (size_t)(frameBase + f) * (size_t)sp.vertexCount * 3;
                for (int v = 0; v < sp.vertexCount; ++v) {
                    // Linear blend skinning over the 4 palette slots.
                    float acc[12] = {};
                    float wsum = 0.0f;
                    for (int k = 0; k < 4; ++k) {
                        const unsigned char w = sp.weights[(size_t)v * 4 + (size_t)k];
                        if (!w) continue;
                        const unsigned char slot = sp.joints[(size_t)v * 4 + (size_t)k];
                        if (slot >= palette.size()) continue;
                        const float fw = (float)w / 255.0f;
                        wsum += fw;
                        const M4& m = palette[slot];
                        // Accumulate the 4x3 affine part (columns 0..3, rows 0..2)
                        // into a column-major 4x3 block - the bottom row of a
                        // skinning matrix is always (0,0,0,1).
                        for (int col = 0; col < 4; ++col)
                            for (int row = 0; row < 3; ++row)
                                acc[col * 3 + row] += m.m[col * 4 + row] * fw;
                    }
                    if (wsum <= 1e-6f) {
                        // Unweighted vertex: leave it in bind pose rather than
                        // collapsing it onto the origin.
                        for (int c = 0; c < 3; ++c) {
                            dp.positions[base + (size_t)v * 3 + (size_t)c] =
                                sp.positions[(size_t)v * 3 + (size_t)c];
                            dp.normals[base + (size_t)v * 3 + (size_t)c] =
                                sp.normals[(size_t)v * 3 + (size_t)c];
                        }
                        continue;
                    }
                    const float inv = 1.0f / wsum;
                    for (int i = 0; i < 12; ++i) acc[i] *= inv;
                    const float* P = &sp.positions[(size_t)v * 3];
                    const float* N = &sp.normals[(size_t)v * 3];
                    for (int row = 0; row < 3; ++row) {
                        dp.positions[base + (size_t)v * 3 + (size_t)row] =
                            acc[0 * 3 + row] * P[0] + acc[1 * 3 + row] * P[1] +
                            acc[2 * 3 + row] * P[2] + acc[3 * 3 + row];
                        dp.normals[base + (size_t)v * 3 + (size_t)row] =
                            acc[0 * 3 + row] * N[0] + acc[1 * 3 + row] * N[1] +
                            acc[2 * 3 + row] * N[2];
                    }
                    float* nrm = &dp.normals[base + (size_t)v * 3];
                    const float len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] +
                                                nrm[2] * nrm[2]);
                    if (len > 1e-9f)
                        nrm[0] /= len, nrm[1] /= len, nrm[2] /= len;
                }
            }
        }
        frameBase += frames;
    }

    // Frame-0 AABB across all parts (the .glb convention every consumer
    // assumes - it is what sizes the preview camera).
    bool first = true;
    for (const glbparser::Part& p : out.parts)
        for (int v = 0; v < p.vertexCount; ++v) {
            const float* pos = &p.positions[(size_t)v * 3];
            for (int c = 0; c < 3; ++c) {
                if (first || pos[c] < out.min[c]) out.min[c] = pos[c];
                if (first || pos[c] > out.max[c]) out.max[c] = pos[c];
            }
            first = false;
        }
    out.warnings = skel.warnings;
    return true;
}

bool mergedSkel(const std::string& modelPath,
                const std::vector<ImportSpec>& imports, Skel& out,
                std::string& error, SkelCache* cache) {
    if (cache) {
        const Skel* base = cache->get(modelPath, error);
        if (!base) return false;
        out = *base;  // merge mutates, the cache entry must stay pristine
    } else if (!animimport::parseSkel(modelPath, out, error)) {
        return false;
    }
    applyImports(imports, out, &out.warnings, cache);
    return true;
}

void posedPreview(const Skel& target, const Skel& donor,
                  const MergeOptions& options, int donorClip, float t,
                  std::vector<float>& donorXyz, std::vector<float>& targetXyz) {
    donorXyz.clear();
    targetXyz.clear();
    if (donorClip < 0 || donorClip >= (int)donor.clips.size()) return;
    const SkelClip& clip = donor.clips[(size_t)donorClip];

    // Donor: its own clip, posed exactly like the preview bake poses it.
    std::vector<std::vector<const SkelChannel*>> byNode(donor.nodes.size());
    for (const SkelChannel& ch : clip.channels)
        if (ch.node >= 0 && ch.node < (int)donor.nodes.size())
            byNode[(size_t)ch.node].push_back(&ch);
    std::vector<M4> dg;
    poseGlobals(donor, clip, t, byNode, dg);
    donorXyz.resize(donor.nodes.size() * 3);
    for (size_t i = 0; i < dg.size(); ++i) {
        donorXyz[i * 3 + 0] = dg[i].m[12];
        donorXyz[i * 3 + 1] = dg[i].m[13];
        donorXyz[i * 3 + 2] = dg[i].m[14];
    }

    // Same fork as the merge: differing binds pose through the full
    // retargeter (ONE function with the bake, retargetLocals), identical
    // ones through the borrowed-rotation fast path below.
    {
        const RetargetCtx ctx = buildRetargetCtx(target, donor, options);
        if (ctx.full) {
            std::vector<M4> locals, tg;
            retargetLocals(ctx, target, donor, dg, options.retargetRoot,
                           locals);
            composeGlobals(target, locals, tg);
            targetXyz.resize(target.nodes.size() * 3);
            for (size_t i = 0; i < tg.size(); ++i) {
                targetXyz[i * 3 + 0] = tg[i].m[12];
                targetXyz[i * 3 + 1] = tg[i].m[13];
                targetXyz[i * 3 + 2] = tg[i].m[14];
            }
            // The donor draws mirrored + faced too, so the two figures are
            // comparable at a glance.
            for (size_t i = 0; i < dg.size(); ++i) {
                const M4 g = donorXform(ctx, dg[i]);
                donorXyz[i * 3 + 0] = g.m[12];
                donorXyz[i * 3 + 1] = g.m[13];
                donorXyz[i * 3 + 2] = g.m[14];
            }
            return;
        }
    }

    // Target: bind locals, with the mapped donor ROTATIONS borrowed in -
    // which is what the merged clip does to it (default policy). The root
    // bone additionally takes the donor's retargeted translation.
    Resolver res(target, options);
    std::vector<char> tIsBone, tIsRoot, dIsBone, dIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    boneSets(donor, dIsBone, dIsRoot);
    std::vector<M4> locals(target.nodes.size());
    for (size_t i = 0; i < target.nodes.size(); ++i)
        locals[i] = localOf(target.nodes[i]);
    for (const SkelChannel& ch : clip.channels) {
        if (ch.node < 0 || ch.node >= (int)donor.nodes.size()) continue;
        const int tn = res.resolve(donor.nodes[(size_t)ch.node].name);
        if (tn < 0 || target.nodes[(size_t)tn].hasMatrix) continue;
        const SkelNode& b = target.nodes[(size_t)tn];
        if (ch.path == 1) {
            float rr[4] = {b.r[0], b.r[1], b.r[2], b.r[3]};
            sampleChannel(ch, t, 4, rr);
            const float len = std::sqrt(rr[0] * rr[0] + rr[1] * rr[1] +
                                        rr[2] * rr[2] + rr[3] * rr[3]);
            if (len > 1e-9f)
                for (int c = 0; c < 4; ++c) rr[c] /= len;
            float tt[3] = {b.t[0], b.t[1], b.t[2]};
            // Keep whatever translation an earlier channel wrote.
            tt[0] = locals[(size_t)tn].m[12];
            tt[1] = locals[(size_t)tn].m[13];
            tt[2] = locals[(size_t)tn].m[14];
            locals[(size_t)tn] = trs(tt, rr, b.s);
        } else if (ch.path == 0 && dIsRoot[(size_t)ch.node] &&
                   tIsRoot[(size_t)tn] && options.retargetRoot) {
            float tv[3];
            sampleChannel(ch, t, 3, tv);
            float donorRest[3], targetRest[3];
            restGlobalTranslation(donor, ch.node, donorRest);
            restGlobalTranslation(target, tn, targetRest);
            float ratio = 1.0f;
            if (std::fabs(donorRest[1]) > 1e-3f &&
                std::fabs(targetRest[1]) > 1e-3f)
                ratio = std::clamp(targetRest[1] / donorRest[1], 0.05f, 20.0f);
            // Same unit conversion as merge() - see the comment there.
            const float unit = parentGlobalScale(donor, ch.node) /
                               parentGlobalScale(target, tn);
            const float* dl = donor.nodes[(size_t)ch.node].t;
            const SkelNode& tb = target.nodes[(size_t)tn];
            for (int c = 0; c < 3; ++c)
                locals[(size_t)tn].m[12 + c] =
                    tb.t[c] + (tv[c] - dl[c]) * ratio * unit;
        }
    }
    std::vector<M4> tg;
    composeGlobals(target, locals, tg);
    targetXyz.resize(target.nodes.size() * 3);
    for (size_t i = 0; i < tg.size(); ++i) {
        targetXyz[i * 3 + 0] = tg[i].m[12];
        targetXyz[i * 3 + 1] = tg[i].m[13];
        targetXyz[i * 3 + 2] = tg[i].m[14];
    }
}

std::string aiMapPrompt(const Skel& target, const Skel& donor,
                        const MergeOptions& options) {
    Resolver res(target, options);
    std::vector<char> tIsBone, tIsRoot, dIsBone, dIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    boneSets(donor, dIsBone, dIsRoot);
    std::vector<float> dXyz, tXyz;
    bindGlobals(donor, dXyz);
    bindGlobals(target, tXyz);

    std::ostringstream out;
    out << "You are matching the bones of two skeleton rigs so animation can "
           "be retargeted.\n"
           "Reply with ONLY this JSON, nothing else:\n"
           "{\"pairs\": [{\"s\": \"<source bone>\", \"t\": \"<target "
           "bone>\"}]}\n"
           "Rules: map ONLY the bones listed under MAP THESE; use target "
           "names from TARGET SKELETON exactly; skip a bone you are unsure "
           "of; NEVER pair a left-side bone with a right-side one; each "
           "target bone at most once.\n\n";
    auto dump = [&](const char* title, const Skel& sk,
                    const std::vector<char>& isBone,
                    const std::vector<float>& xyz) {
        out << title << " (name, parent, bind position x y z):\n";
        for (size_t i = 0; i < sk.nodes.size(); ++i) {
            if (!isBone[i]) continue;
            int p = sk.nodes[i].parent;
            while (p >= 0 && p < (int)sk.nodes.size() && !isBone[(size_t)p])
                p = sk.nodes[(size_t)p].parent == p ? -1
                                                    : sk.nodes[(size_t)p].parent;
            char line[256];
            snprintf(line, sizeof line, "  %s | parent: %s | %.2f %.2f %.2f\n",
                     sk.nodes[i].name.c_str(),
                     p >= 0 ? sk.nodes[(size_t)p].name.c_str() : "-",
                     xyz[i * 3], xyz[i * 3 + 1], xyz[i * 3 + 2]);
            out << line;
        }
    };
    dump("SOURCE SKELETON", donor, dIsBone, dXyz);
    out << "\n";
    dump("TARGET SKELETON", target, tIsBone, tXyz);

    out << "\nALREADY MATCHED (context, do not repeat):\n";
    std::vector<std::string> unmatched;
    for (size_t i = 0; i < donor.nodes.size(); ++i) {
        if (!dIsBone[i]) continue;
        const int tn = res.resolve(donor.nodes[i].name);
        if (tn >= 0)
            out << "  " << donor.nodes[i].name << " -> "
                << target.nodes[(size_t)tn].name << "\n";
        else
            unmatched.push_back(donor.nodes[i].name);
    }
    out << "\nMAP THESE:\n";
    for (const std::string& u : unmatched) out << "  " << u << "\n";
    return out.str();
}

std::vector<std::pair<std::string, std::string>> parseAiBoneMap(
    const std::string& reply, const Skel& target, const Skel& donor) {
    std::vector<std::pair<std::string, std::string>> out;
    // First balanced top-level {...}, string-aware - models fence and chat.
    int depth = 0;
    size_t start = std::string::npos, end = std::string::npos;
    bool inStr = false;
    for (size_t i = 0; i < reply.size(); ++i) {
        const char c = reply[i];
        if (inStr) {
            if (c == '\\') ++i;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (c == '}') {
            if (depth > 0 && --depth == 0) {
                end = i;
                break;
            }
        }
    }
    if (start == std::string::npos || end == std::string::npos) return out;
    json::Value root;
    if (!json::parse(reply.substr(start, end - start + 1), root)) return out;
    const json::Value* pairs = root.find("pairs");
    if (!pairs || pairs->type != json::Value::Type::Array) return out;

    // Lenient lookup: models strip namespaces ("mixamorig:") and drift on
    // case, and a strict comparison threw EVERY pair away on a real rig
    // ("AI proposed nothing usable"). Exact name first, then the normalized
    // form - mapped back to the REAL names, which is what the pair stores.
    MergeOptions norm;  // defaults: strip namespace + lowercase
    std::map<std::string, std::string> donorByNorm, targetByNorm;
    std::set<std::string> donorNames, targetBones, usedD, usedT;
    std::vector<char> tIsBone, tIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    for (const SkelNode& nd : donor.nodes) {
        donorNames.insert(nd.name);
        donorByNorm.emplace(normalize(nd.name, norm), nd.name);
    }
    for (size_t i = 0; i < target.nodes.size(); ++i)
        if (tIsBone[i]) {
            targetBones.insert(target.nodes[i].name);
            targetByNorm.emplace(normalize(target.nodes[i].name, norm),
                                 target.nodes[i].name);
        }
    auto realName = [&](const std::string& given,
                        const std::set<std::string>& exact,
                        const std::map<std::string, std::string>& byNorm) {
        if (exact.count(given)) return given;
        const auto it = byNorm.find(normalize(given, norm));
        return it != byNorm.end() ? it->second : std::string();
    };
    for (const json::Value& pr : pairs->arr) {
        const json::Value* sV = pr.find("s");
        const json::Value* tV = pr.find("t");
        if (!sV || !tV) continue;
        const std::string sN = realName(sV->stringOr(""), donorNames, donorByNorm);
        const std::string tN =
            realName(tV->stringOr(""), targetBones, targetByNorm);
        if (sN.empty() || tN.empty()) continue;
        if (usedD.count(sN) || usedT.count(tN)) continue;
        usedD.insert(sN);
        usedT.insert(tN);
        out.emplace_back(sN, tN);
    }
    return out;
}

bool bakedWithImports(const std::string& modelPath,
                      const std::vector<ImportSpec>& imports, float fps,
                      Baked& out, std::string& error, SkelCache* cache) {
    // No imports: the parser's own bake, untouched. A model nobody has
    // imported into must be unaffected by any of this.
    if (imports.empty()) return animimport::bake(modelPath, fps, out, error);

    Skel skel;
    if (cache) {
        const Skel* base = cache->get(modelPath, error);
        if (!base) return false;
        skel = *base;
    } else if (!animimport::parseSkel(modelPath, skel, error)) {
        return false;
    }
    // No refreshPoseBounds here on purpose: those bounds exist for the
    // console's culling and collision, and skelToBaked computes what the
    // preview needs from the frames it is about to bake anyway. Skipping the
    // extra full-skeleton skinning pass is most of what keeps opening a model
    // with imports as cheap as opening one without.
    applyImports(imports, skel, &skel.warnings, cache);
    return skelToBaked(skel, fps, out, error);
}

}  // namespace animmerge
