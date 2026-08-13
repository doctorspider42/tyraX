#include "animmerge.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fbxparser.hpp"  // animimport:: - the format dispatch

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
    (void)clip;
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
        if (cur == "left") cur = "l";
        else if (cur == "right") cur = "r";
        tokens.push_back(cur);
        cur.clear();
    };
    for (size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (!std::isalnum((unsigned char)c)) {
            flush();
            continue;
        }
        // camelCase boundary: an upper after a lower starts a new token.
        if (std::isupper((unsigned char)c) && !cur.empty() &&
            std::islower((unsigned char)cur.back()))
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

std::vector<BoneSuggestion> suggestBoneMap(const Skel& target,
                                           const Skel& donor,
                                           const MergeOptions& options) {
    Resolver res(target, options);
    std::vector<char> tIsBone, tIsRoot, dIsBone, dIsRoot;
    boneSets(target, tIsBone, tIsRoot);
    boneSets(donor, dIsBone, dIsRoot);

    // Free target bones: not already the home of a name-resolved donor bone.
    std::vector<char> taken(target.nodes.size(), 0);
    std::vector<int> unmatched;
    for (size_t i = 0; i < donor.nodes.size(); ++i) {
        if (!dIsBone[i]) continue;
        const int hit = res.resolve(donor.nodes[i].name);
        if (hit >= 0)
            taken[(size_t)hit] = 1;
        else
            unmatched.push_back((int)i);
    }

    // Score every (unmatched donor, free target-bone) pair, then assign
    // greedily best-first so no target bone is suggested twice.
    struct Cand {
        int donor, target;
        float score;
    };
    std::vector<Cand> cands;
    for (int di : unmatched) {
        const auto dt = boneTokens(donor.nodes[(size_t)di].name);
        for (size_t ti = 0; ti < target.nodes.size(); ++ti) {
            if (!tIsBone[ti] || taken[ti]) continue;
            const float sc = boneScore(dt, boneTokens(target.nodes[ti].name));
            if (sc >= 0.5f) cands.push_back({di, (int)ti, sc});
        }
    }
    std::stable_sort(cands.begin(), cands.end(),
                     [](const Cand& a, const Cand& b) { return a.score > b.score; });
    std::vector<BoneSuggestion> out;
    std::vector<char> donorDone(donor.nodes.size(), 0);
    for (const Cand& c : cands) {
        if (donorDone[(size_t)c.donor] || taken[(size_t)c.target]) continue;
        donorDone[(size_t)c.donor] = 1;
        taken[(size_t)c.target] = 1;
        out.push_back({donor.nodes[(size_t)c.donor].name,
                       target.nodes[(size_t)c.target].name, c.score});
    }
    return out;
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

bool applyImports(const std::vector<ImportSpec>& imports, Skel& target,
                  std::vector<std::string>* warnings) {
    auto warn = [&](const std::string& m) {
        if (warnings) warnings->push_back(m);
    };
    bool merged = false;
    for (const ImportSpec& spec : imports) {
        Skel donor;
        std::string error;
        if (!animimport::parseSkel(spec.path, donor, error)) {
            warn("animation import '" + spec.path + "': " + error);
            continue;
        }
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

bool bakedWithImports(const std::string& modelPath,
                      const std::vector<ImportSpec>& imports, float fps,
                      Baked& out, std::string& error) {
    // No imports: the parser's own bake, untouched. A model nobody has
    // imported into must be unaffected by any of this.
    if (imports.empty()) return animimport::bake(modelPath, fps, out, error);

    Skel skel;
    if (!animimport::parseSkel(modelPath, skel, error)) return false;
    // No refreshPoseBounds here on purpose: those bounds exist for the
    // console's culling and collision, and skelToBaked computes what the
    // preview needs from the frames it is about to bake anyway. Skipping the
    // extra full-skeleton skinning pass is most of what keeps opening a model
    // with imports as cheap as opening one without.
    applyImports(imports, skel, &skel.warnings);
    return skelToBaked(skel, fps, out, error);
}

}  // namespace animmerge
