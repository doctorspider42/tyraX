// The Foot IK / neural gait window (Tools > Foot IK, docs/foot-ik.md).
//
// App:: methods declared in app.hpp, own TU - the credits_ui.cpp precedent.
// It is a separate window from the Animation Editor on purpose: a rig is a
// property of the SKELETON and a clip edit is a property of one clip, so the
// two panels answer different questions about the same asset.
//
// Everything with any judgement in it lives in footik.cpp (host-only, no GL,
// harness-testable). This file is widgets: pick bones, see what is wrong, and
// nothing else.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "app.hpp"
#include "app_internal.hpp"
#include "fbxparser.hpp"  // animimport::parseSkel (the extension dispatch)
#include "footik.hpp"

const App::RigInfo& App::rigInfo(const std::string& relPath) {
    auto it = rigInfoCache_.find(relPath);
    if (it != rigInfoCache_.end()) return it->second;

    RigInfo info;
    const std::filesystem::path full =
        std::filesystem::path(project_.dir) / relPath;
    if (animimport::parseSkel(full.string(), info.skel, info.error))
        info.ok = true;
    return rigInfoCache_.emplace(relPath, std::move(info)).first->second;
}

AnimRig& App::animRigFor(const std::string& model) {
    for (AnimRig& r : project_.animRigs)
        if (r.model == model) return r;
    AnimRig r;
    r.model = model;
    project_.animRigs.push_back(std::move(r));
    return project_.animRigs.back();
}

void App::pruneAnimRigs() {
    auto& v = project_.animRigs;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const AnimRig& r) { return r.isDefault(); }),
            v.end());
}

void App::drawFootIkWindow() {
    if (!showFootIk_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(760), scaled(640)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Foot IK", &showFootIk_)) {
        ImGui::End();
        return;
    }

    auto helpMarker = [](const char* tip) {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };

    bool changed = false;
    // The section-comparison guard every panel owning a project::Section
    // carries: a hand-set `changed` is forgettable by the next widget someone
    // adds, a serialized comparison across the whole body is not.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::AnimRigs);
    auto commitIfEdited = [&]() {
        if (changed || project::sectionJson(project_, project::Section::AnimRigs) !=
                           beforeSection)
            commitChange();
    };

    std::vector<std::string> models;
    for (const std::string& m : listAnimatedModelFiles())
        models.push_back("res/models/" + m);
    if (models.empty()) {
        ImGui::TextDisabled(
            "No animated models in this project.\n\n"
            "Project > Assets > Import model... and pick a .glb or .fbx.");
        ImGui::End();
        return;
    }
    if (footIkModel_.empty() ||
        std::find(models.begin(), models.end(), footIkModel_) == models.end())
        footIkModel_ = models.front();

    ImGui::SetNextItemWidth(scaled(300));
    if (ImGui::BeginCombo("Model", footIkModel_.c_str())) {
        for (const std::string& m : models)
            if (ImGui::Selectable(m.c_str(), m == footIkModel_))
                footIkModel_ = m;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    helpMarker(
        "A rig belongs to the model asset, not to a scene object: every\n"
        "instance of this character shares it. An object only carries the\n"
        "on/off switch (Properties > Foot IK).");

    const RigInfo& ri = rigInfo(footIkModel_);
    if (!ri.ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Unusable model: %s",
                           ri.error.c_str());
        ImGui::End();
        return;
    }
    if (ri.skel.nodes.empty()) {
        ImGui::TextDisabled("This model has no skeleton to bind.");
        ImGui::End();
        return;
    }

    ImGui::Separator();

    const AnimRig* existing = project_.findAnimRig(footIkModel_);
    if (!existing || existing->legs.empty()) {
        ImGui::TextWrapped(
            "This model has no leg chains bound yet. Detect them from the bone "
            "names, then check what was found before switching the solver on.");
        ImGui::Spacing();
        if (ImGui::Button("Detect legs from bone names")) {
            AnimRig guess = footik::autoDetect(footIkModel_, ri.skel);
            AnimRig& r = animRigFor(footIkModel_);
            const bool wasEnabled = r.enabled, wasNet = r.netEnabled;
            guess.enabled = wasEnabled;  // detection is not a switch
            guess.netEnabled = wasNet;
            r = guess;
            changed = true;
        }
        ImGui::SameLine();
        helpMarker(
            "Recognises Mixamo (LeftUpLeg/LeftLeg/LeftFoot/LeftToeBase),\n"
            "Blender Rigify (thigh/shin/foot), Unreal (thigh_l/calf_l/foot_l)\n"
            "and the plain thigh/knee/ankle spellings. The pelvis is the\n"
            "deepest common ancestor of the two hips, whatever it is called.\n\n"
            "A detection never turns the solver on - a guess is a starting\n"
            "point you confirm, and a solver nobody looked at is how a\n"
            "character ships with a knee bending the wrong way.");
        commitIfEdited();
        ImGui::End();
        return;
    }

    AnimRig& rig = animRigFor(footIkModel_);
    const footik::Resolved resolved = footik::resolve(rig, ri.skel);

    // --- what is wrong with it, first, before anything can be switched on ---
    if (!resolved.problems.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.25f, 1.0f));
        for (const std::string& p : resolved.problems)
            ImGui::BulletText("%s", p.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled(
            "The build skips a rig it cannot resolve - the character animates "
            "exactly as it does today.");
        ImGui::Spacing();
    }

    const bool solvable = resolved.ok();
    ImGui::BeginDisabled(!solvable);
    bool enabled = rig.enabled;
    if (ImGui::Checkbox("Foot IK on this model", &enabled)) {
        rig.enabled = enabled;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    helpMarker(
        "Objects using this model also need Properties > Foot IK on. Two\n"
        "switches because a rig is worth binding once and using on some\n"
        "instances only - a distant crowd is cheaper without it (an\n"
        "instance running the solver cannot share its skinned mesh with\n"
        "the others, see docs/foot-ik.md).");

    ImGui::Spacing();

    // --- the bone bindings --------------------------------------------------
    auto boneCombo = [&](const char* label, std::string& value, bool optional) {
        // Every bone is offered: a rig that names its shin "LeftLeg" must be
        // fixable by hand when the detector reads it as the hip.
        ImGui::SetNextItemWidth(scaled(190));
        const char* shown = value.empty() ? "(none)" : value.c_str();
        bool edited = false;
        if (ImGui::BeginCombo(label, shown)) {
            if (optional && ImGui::Selectable("(none)", value.empty())) {
                value.clear();
                edited = true;
            }
            for (const glbparser::SkelNode& n : ri.skel.nodes) {
                if (n.name.empty()) continue;
                if (ImGui::Selectable(n.name.c_str(), n.name == value)) {
                    value = n.name;
                    edited = true;
                }
            }
            ImGui::EndCombo();
        }
        if (edited) changed = true;
        return edited;
    };

    if (ImGui::CollapsingHeader("Bones", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (size_t i = 0; i < rig.legs.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::Text("Leg %d", (int)i + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("remove")) {
                rig.legs.erase(rig.legs.begin() + (long)i);
                changed = true;
                ImGui::PopID();
                break;
            }
            AnimRigLeg& leg = rig.legs[i];
            boneCombo("Hip", leg.hip, false);
            ImGui::SameLine();
            boneCombo("Knee", leg.knee, false);
            boneCombo("Ankle", leg.ankle, false);
            ImGui::SameLine();
            boneCombo("Toe", leg.toe, true);
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (rig.legs.size() < 4 && ImGui::SmallButton("+ leg")) {
            rig.legs.push_back(AnimRigLeg());
            changed = true;
        }
        ImGui::Spacing();
        boneCombo("Pelvis", rig.pelvis, true);
        ImGui::SameLine();
        helpMarker(
            "Lowered when a foot has to reach further down than the leg can.\n"
            "Without it a character descending stairs does the splits: the\n"
            "trailing foot reaches for a tread the leg cannot touch.\n\n"
            "It must be an ancestor of every hip, or lowering it would not\n"
            "move the legs at all.");

        if (ImGui::SmallButton("Re-detect from bone names")) {
            AnimRig guess = footik::autoDetect(footIkModel_, ri.skel);
            guess.enabled = rig.enabled;
            guess.netEnabled = rig.netEnabled;
            guess.netPath = rig.netPath;
            guess.netWeight = rig.netWeight;
            rig = guess;
            changed = true;
        }
    }

    // --- the numbers --------------------------------------------------------
    if (ImGui::CollapsingHeader("Solver", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::DragFloat("Sole offset", &rig.soleOffset, 0.002f, 0.0f, 1.0f,
                             "%.3f"))
            changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("measure")) {
            rig.soleOffset = footik::measureSoleOffset(ri.skel, resolved);
            changed = true;
        }
        ImGui::SameLine();
        helpMarker(
            "How far the sole sits below the ankle joint, in MODEL units.\n"
            "\"measure\" reads the lowest vertex actually weighted to the\n"
            "foot in the bind pose - a boot and a bare foot differ by\n"
            "centimetres, and centimetres are exactly what a sunk heel is.");

        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::DragFloat("Max lift", &rig.maxLift, 0.01f, 0.0f, 3.0f, "%.2f"))
            changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::DragFloat("Max pelvis drop", &rig.maxDrop, 0.01f, 0.0f, 3.0f,
                             "%.2f"))
            changed = true;

        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::SliderFloat("Follow surface", &rig.normalBlend, 0.0f, 1.0f,
                               "%.2f"))
            changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::SliderFloat("Max roll", &rig.maxRollDeg, 0.0f, 89.0f, "%.0f deg"))
            changed = true;
        ImGui::SameLine();
        helpMarker(
            "How far the foot tilts onto the surface it found. Measured\n"
            "against the object's own up, so a flat floor is exactly zero\n"
            "and the clip's authored foot angle survives untouched.");

        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::DragFloat("Smoothing", &rig.smoothing, 0.2f, 0.5f, 60.0f,
                             "%.1f /s"))
            changed = true;
        ImGui::SameLine();
        helpMarker(
            "Critically damped follow rate. The ground under a walker is a\n"
            "step function - lower this and the foot lags behind the stairs,\n"
            "raise it and it pops on every tread edge.");

        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::DragFloat("Trace up", &rig.traceUp, 0.02f, 0.0f, 5.0f, "%.2f"))
            changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::DragFloat("Trace down", &rig.traceDown, 0.02f, 0.0f, 5.0f,
                             "%.2f"))
            changed = true;
        ImGui::SameLine();
        helpMarker(
            "How far above and below the clip's own sole position the world\n"
            "is searched. A miss means \"nothing to stand on\" and the foot\n"
            "keeps the animation - which is what you want over a ledge.");
    }

    // --- the learned corrector ---------------------------------------------
    if (ImGui::CollapsingHeader("Gait net (experimental)")) {
        ImGui::TextWrapped(
            "A small network reads the ground ahead and rewrites the stride "
            "before the solver plants the feet - where the next foot is "
            "heading, how the weight shifts, and how fast the clip should be "
            "playing. That last one is what no solver can produce.");
        ImGui::Spacing();

        bool netOn = rig.netEnabled;
        ImGui::BeginDisabled(!solvable);
        if (ImGui::Checkbox("Run a trained net on this model", &netOn)) {
            rig.netEnabled = netOn;
            changed = true;
        }
        ImGui::EndDisabled();

        char buf[256];
        snprintf(buf, sizeof(buf), "%s", rig.netPath.c_str());
        ImGui::SetNextItemWidth(scaled(320));
        if (ImGui::InputText("Weights (.tnet)", buf, sizeof(buf))) {
            rig.netPath = buf;
            changed = true;
        }
        ImGui::SameLine();
        helpMarker(
            "Empty = res/models/<model>.tnet next to the model.\n"
            "A missing or mismatched file is never fatal: the character\n"
            "walks without it and the log says so.");

        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::SliderFloat("Net weight", &rig.netWeight, 0.0f, 1.0f, "%.2f"))
            changed = true;

        ImGui::Spacing();
        ImGui::TextDisabled("Joints the net may bend (legs are always included):");
        for (size_t i = 0; i < rig.netJoints.size(); ++i) {
            ImGui::PushID(1000 + (int)i);
            boneCombo("##netjoint", rig.netJoints[i], false);
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                rig.netJoints.erase(rig.netJoints.begin() + (long)i);
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (rig.netJoints.size() < 16 && ImGui::SmallButton("+ joint")) {
            rig.netJoints.push_back(std::string());
            changed = true;
        }
    }

    // --- what the rig actually measures, so a bad binding is visible --------
    if (solvable) {
        footik::Pose bind;
        footik::evalPose(ri.skel, -1, 0.0f, bind);
        ImGui::Separator();
        ImGui::TextDisabled("Bind pose:");
        for (size_t i = 0; i < resolved.legs.size(); ++i) {
            const footik::Resolved::Leg& l = resolved.legs[i];
            auto pos = [&](int n, int c) { return bind[(size_t)n * 16 + 12 + c]; };
            auto dist = [&](int a, int b) {
                const float dx = pos(a, 0) - pos(b, 0);
                const float dy = pos(a, 1) - pos(b, 1);
                const float dz = pos(a, 2) - pos(b, 2);
                return std::sqrt(dx * dx + dy * dy + dz * dz);
            };
            const float thigh = dist(l.hip, l.knee), shin = dist(l.knee, l.ankle);
            ImGui::BulletText("leg %d: thigh %.3f + shin %.3f = reach %.3f",
                              (int)i + 1, thigh, shin, thigh + shin);
        }
        ImGui::TextDisabled(
            "A reach far off the model's own height usually means the hip and "
            "knee are bound to the wrong bones.");
    }

    commitIfEdited();
    ImGui::End();
}
