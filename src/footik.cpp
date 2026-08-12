#include "footik.hpp"

#include <cctype>

namespace footik {

const char* slotLabel(Slot s) {
    switch (s) {
        case Slot::LeftHip: return "Left hip";
        case Slot::LeftKnee: return "Left knee";
        case Slot::LeftAnkle: return "Left ankle";
        case Slot::RightHip: return "Right hip";
        case Slot::RightKnee: return "Right knee";
        case Slot::RightAnkle: return "Right ankle";
        case Slot::Count: break;
    }
    return "";
}

std::string& slotField(FootIkRig& r, Slot s) {
    switch (s) {
        case Slot::LeftHip: return r.leftHip;
        case Slot::LeftKnee: return r.leftKnee;
        case Slot::LeftAnkle: return r.leftAnkle;
        case Slot::RightHip: return r.rightHip;
        case Slot::RightKnee: return r.rightKnee;
        case Slot::RightAnkle: return r.rightAnkle;
        case Slot::Count: break;
    }
    return r.leftHip;
}

const std::string& slotField(const FootIkRig& r, Slot s) {
    return slotField(const_cast<FootIkRig&>(r), s);
}

int findNode(const Skeleton& skel, const std::string& name) {
    if (name.empty()) return -1;
    int found = -1;
    for (size_t i = 0; i < skel.size(); ++i) {
        if (skel.name(i) != name) continue;
        if (found >= 0) return -2;  // ambiguous
        found = (int)i;
    }
    return found;
}

Report validate(const FootIkRig& r, const Skeleton& skel) {
    Report rep;
    for (int i = 0; i < (int)Slot::Count; ++i) {
        const Slot s = (Slot)i;
        const std::string& name = slotField(r, s);
        rep.node[i] = findNode(skel, name);
        if (rep.node[i] == -1)
            rep.problems.push_back(
                std::string(slotLabel(s)) +
                (name.empty() ? ": no bone selected."
                              : ": '" + name + "' is not in the model."));
        else if (rep.node[i] == -2)
            rep.problems.push_back(std::string(slotLabel(s)) + ": '" + name +
                                   "' names more than one bone - the console "
                                   "binds by name and cannot tell them apart.");
    }
    rep.complete = rep.problems.empty();
    if (!rep.complete) return rep;

    // A leg has to be a real descendant chain, or the analytic two-bone solve
    // is rotating joints that do not move the ankle it is aiming.
    auto below = [&](int child, int parent) {
        for (int n = child; n >= 0 && n < (int)skel.size(); n = skel.parent(n))
            if (n == parent) return true;
        return false;
    };
    struct Chain { Slot hip, knee, ankle; const char* side; };
    const Chain chainOf[2] = {
        {Slot::LeftHip, Slot::LeftKnee, Slot::LeftAnkle, "Left"},
        {Slot::RightHip, Slot::RightKnee, Slot::RightAnkle, "Right"}};
    rep.chains = true;
    for (const Chain& c : chainOf) {
        const int hip = rep.node[(int)c.hip], knee = rep.node[(int)c.knee],
                  ankle = rep.node[(int)c.ankle];
        if (!below(knee, hip)) {
            rep.chains = false;
            rep.problems.push_back(std::string(c.side) +
                                   " leg: the knee is not below the hip in the "
                                   "model's hierarchy.");
        }
        if (!below(ankle, knee)) {
            rep.chains = false;
            rep.problems.push_back(std::string(c.side) +
                                   " leg: the ankle is not below the knee in "
                                   "the model's hierarchy.");
        }
    }
    // The same bone in two slots solves to nonsense rather than reporting a
    // missing name, so it is worth its own line.
    for (int a = 0; a < (int)Slot::Count; ++a)
        for (int b = a + 1; b < (int)Slot::Count; ++b)
            if (rep.node[a] == rep.node[b]) {
                rep.chains = false;
                rep.problems.push_back(std::string(slotLabel((Slot)a)) +
                                       " and " + slotLabel((Slot)b) +
                                       " are the same bone.");
            }
    return rep;
}

namespace {

// Names are compared with punctuation, case and separators removed, so
// "thigh.L", "Thigh_L" and "thighl" are one spelling. That is what lets one
// pattern table cover Blender, Mixamo, Unreal and hand-built rigs.
std::string compact(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c)) out.push_back((char)std::tolower(c));
    return out;
}

// A pattern matches a bone when the compacted name IS it or ENDS with it -
// "mixamorigleftupleg" ends with "leftupleg". Prefix matching would make
// "leftleg" (the Mixamo knee) match "leftlegroot" and similar decoy bones, and
// the earlier patterns in a slot's list are the more specific spellings.
bool matches(const std::string& compacted, const char* pattern) {
    const std::string p = pattern;
    if (compacted == p) return true;
    return compacted.size() > p.size() &&
           compacted.compare(compacted.size() - p.size(), p.size(), p) == 0;
}

}  // namespace

int autoDetect(const Skeleton& skel, FootIkRig& out) {
    // Per slot, most specific spelling first. A slot takes the FIRST bone in
    // the file that matches any of its patterns, and patterns are tried in
    // order over the whole skeleton, so "leftupleg" beats "upperlegl" for a
    // Mixamo rig that happens to carry both spellings somewhere.
    struct Rule { Slot slot; std::vector<const char*> patterns; };
    const std::vector<Rule> rules = {
        {Slot::LeftHip,
         {"leftupleg", "upperlegleft", "upperlegl", "thighleft", "thighl",
          "lthigh", "legleft1", "hipl", "lefthip"}},
        {Slot::LeftKnee,
         {"leftleg", "lowerlegleft", "lowerlegl", "calfleft", "calfl",
          "shinleft", "shinl", "legleft2", "kneel", "leftknee"}},
        {Slot::LeftAnkle,
         {"leftfoot", "footleft", "footl", "ankleleft", "anklel", "lankle",
          "legleft3", "leftankle"}},
        {Slot::RightHip,
         {"rightupleg", "upperlegright", "upperlegr", "thighright", "thighr",
          "rthigh", "legright1", "hipr", "righthip"}},
        {Slot::RightKnee,
         {"rightleg", "lowerlegright", "lowerlegr", "calfright", "calfr",
          "shinright", "shinr", "legright2", "kneer", "rightknee"}},
        {Slot::RightAnkle,
         {"rightfoot", "footright", "footr", "ankleright", "ankler", "rankle",
          "legright3", "rightankle"}},
    };
    // Compact every name once - a humanoid skeleton is ~60 bones and this runs
    // per pattern per bone.
    std::vector<std::string> compacted(skel.size());
    for (size_t i = 0; i < skel.size(); ++i) compacted[i] = compact(skel.name(i));

    int filled = 0;
    for (const Rule& rule : rules) {
        std::string found;
        for (const char* pattern : rule.patterns) {
            for (size_t i = 0; i < skel.size() && found.empty(); ++i) {
                if (skel.name(i).empty()) continue;
                if (matches(compacted[i], pattern)) found = skel.name(i);
            }
            if (!found.empty()) break;
        }
        slotField(out, rule.slot) = found;
        if (!found.empty()) ++filled;
    }
    return filled;
}

}  // namespace footik
