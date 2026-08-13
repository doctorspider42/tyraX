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
        // chain instead of silently landing on bone 0.
        for (const auto& [from, to] : o.boneMap)
            if (auto it = exact_.find(to); it != exact_.end())
                mapped_.emplace(from, it->second);
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

bool merge(Skel& target, const Skel& donor, const ImportSpec& spec,
           MergeReport& report, std::string& error) {
    if (target.nodes.empty()) {
        error = "the target model has no skeleton";
        return false;
    }
    const MergeOptions& o = spec.options;
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
                const float* dLocal = donor.nodes[(size_t)ch.node].t;
                const float* tLocal = target.nodes[(size_t)targetNode].t;
                for (size_t k = 0; k + 3 <= out.values.size(); k += 3)
                    for (int c = 0; c < 3; ++c)
                        out.values[k + (size_t)c] =
                            tLocal[c] + (out.values[k + (size_t)c] - dLocal[c]) * ratio;
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
            const float* dl = donor.nodes[(size_t)ch.node].t;
            const SkelNode& tb = target.nodes[(size_t)tn];
            for (int c = 0; c < 3; ++c)
                locals[(size_t)tn].m[12 + c] =
                    tb.t[c] + (tv[c] - dl[c]) * ratio;
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

    std::set<std::string> donorNames, targetBones, usedD, usedT;
    std::vector<char> tIsBone, tIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    for (const SkelNode& nd : donor.nodes) donorNames.insert(nd.name);
    for (size_t i = 0; i < target.nodes.size(); ++i)
        if (tIsBone[i]) targetBones.insert(target.nodes[i].name);
    for (const json::Value& pr : pairs->arr) {
        const json::Value* sV = pr.find("s");
        const json::Value* tV = pr.find("t");
        if (!sV || !tV) continue;
        const std::string sN = sV->stringOr(""), tN = tV->stringOr("");
        if (!donorNames.count(sN) || !targetBones.count(tN)) continue;
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
