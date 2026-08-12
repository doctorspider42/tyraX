// Tools > Foot IK - the window that binds a model's legs and tunes the
// ground solver (docs/foot-ik.md). App:: methods declared in app.hpp, own TU
// (the credits_ui.cpp / facts_ui.cpp precedent).
//
// The shape of this window IS the feature's design: a rig is per MODEL ASSET
// and the switch is per INSTANCE, so the window has a model picker at the top
// and the list of scene objects using that model at the bottom. Everything a
// crowd shares is authored once in the middle; who pays for it is the column of
// checkboxes at the end. That is what lets a future NPC opt in without touching
// the binding, which is the whole reason the tool exists.
//
// Everything here is host-side authoring: no solving happens in the editor (the
// solver is in the generated game - see templates.cpp's applyFootIk), and the
// viewport deliberately keeps showing the authored clip so the raw animation
// stays easy to inspect.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "animedit.hpp"
#include "app.hpp"
#include "app_internal.hpp"
#include "footik.hpp"
#include "imgui.h"
#include "project.hpp"

FootIkRig& App::footIkRigFor(const std::string& model) {
    for (FootIkRig& r : project_.footIkRigs)
        if (r.model == model) return r;
    FootIkRig r;
    r.model = model;
    // A model with no rig yet: seed the sole offset from the project's world
    // scale, so a metric project's 8 cm default means 8 cm at any unit scale.
    const float ups = project_.settings.unitsPerMeter > 0.0001f
                          ? project_.settings.unitsPerMeter
                          : 1.0f;
    r.soleOffset *= ups;
    r.probeUp *= ups;
    r.probeDown *= ups;
    r.plantDistance *= ups;
    r.releaseDistance *= ups;
    r.maxPelvis *= ups;
    r.toeClearance *= ups;
    r.descendReach *= ups;
    project_.footIkRigs.push_back(std::move(r));
    return project_.footIkRigs.back();
}

void App::pruneFootIkRigs() {
    auto& v = project_.footIkRigs;
    for (FootIkRig& r : v)
        r.clips.erase(std::remove_if(r.clips.begin(), r.clips.end(),
                                     [](const FootIkClipRule& c) {
                                         return c.isDefault();
                                     }),
                      r.clips.end());
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const FootIkRig& r) { return r.isDefault(); }),
            v.end());
}

bool App::footIkRigBound(const std::string& model) const {
    const FootIkRig* r = project_.findFootIkRig(model);
    return r && r->enabled;
}

void App::drawFootIkWindow() {
    if (!showFootIk_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(760), scaled(660)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Foot IK", &showFootIk_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    // The section guard (app.hpp rule 1): `changed` is set by hand per widget,
    // so the next widget someone adds silently forgets it. Comparing the
    // section's serialized form across the whole body cannot be forgotten. The
    // per-object switches at the bottom edit SCENES, which the undo snapshot
    // covers on its own, so they set `changed` directly.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::FootIkRigs);
    auto commitIfEdited = [&] {
        pruneFootIkRigs();
        if (changed || project::sectionJson(project_,
                                            project::Section::FootIkRigs) !=
                           beforeSection)
            commitChange();
    };

    std::vector<std::string> models;
    for (const std::string& m : listAnimatedModelFiles())
        models.push_back("res/models/" + m);
    if (models.empty()) {
        ImGui::TextDisabled(
            "No animated models in this project.\n\n"
            "Project > Assets > Import model... and pick a .glb or .fbx, then\n"
            "come back here to tell the solver which bones are legs.");
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (footIkModel_.empty() ||
        std::find(models.begin(), models.end(), footIkModel_) == models.end())
        footIkModel_ = models.front();

    ImGui::SetNextItemWidth(scaled(300));
    if (ImGui::BeginCombo("Model", footIkModel_.c_str())) {
        for (const std::string& m : models) {
            // A bound model is worth spotting in the list without opening it.
            const std::string label =
                m + (footIkRigBound(m) ? "  [bound]" : "");
            if (ImGui::Selectable(label.c_str(), m == footIkModel_) &&
                m != footIkModel_)
                footIkModel_ = m;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "The rig belongs to the model ASSET: every instance of this\n"
            "character shares the binding below, and each scene object\n"
            "decides for itself whether to run the solver.");

    const GlbInfo& info = glbInfo(footIkModel_);
    if (!info.ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                           "Cannot read this model: %s",
                           info.error.empty() ? "unknown error"
                                              : info.error.c_str());
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (info.bones.empty()) {
        ImGui::TextDisabled(
            "This model carries no skeleton, so it has no legs to bind.");
        commitIfEdited();
        ImGui::End();
        return;
    }

    FootIkRig& rig = footIkRigFor(footIkModel_);
    const footik::Skeleton skel{&info.bones, &info.boneParents};
    const footik::Report report = footik::validate(rig, skel);

    ImGui::Separator();
    changed |= ImGui::Checkbox("Solve this model's feet", &rig.enabled);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Master switch for the binding. Off = every instance of this model\n"
            "animates exactly as authored, whatever their own switches say.");
    ImGui::SameLine();
    if (rig.enabled && report.ok())
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1.0f), "rig OK");
    else if (rig.enabled)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "rig incomplete - the game will leave the animation "
                           "untouched");

    if (ImGui::BeginTabBar("##footik-tabs")) {
        // --- Rig: which bones are legs -------------------------------------
        if (ImGui::BeginTabItem("Rig")) {
            if (ImGui::Button("Auto-detect leg bones")) {
                const int filled = footik::autoDetect(skel, rig);
                footIkDetected_ = filled;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Guesses the six bones from their names - Mixamo, Blender\n"
                    "Rigify, Unreal, the plain English spellings and the\n"
                    "numbered Leg_Left_1..3 style. A guess is a starting point:\n"
                    "check it below before you switch the solver on.");
            ImGui::SameLine();
            if (footIkDetected_ >= 0)
                ImGui::TextDisabled("filled %d of 6 slots", footIkDetected_);
            else
                ImGui::TextDisabled("or map the six bones by hand");

            ImGui::PushID("footik-bones");
            for (int i = 0; i < (int)footik::Slot::Count; ++i) {
                const footik::Slot s = (footik::Slot)i;
                std::string& bone = footik::slotField(rig, s);
                const std::string current = bone.empty() ? "<none>" : bone;
                ImGui::SetNextItemWidth(scaled(260));
                if (ImGui::BeginCombo(footik::slotLabel(s), current.c_str())) {
                    if (ImGui::Selectable("<none>", bone.empty()) &&
                        !bone.empty()) {
                        bone.clear();
                        changed = true;
                    }
                    for (size_t b = 0; b < info.bones.size(); ++b) {
                        const std::string& name = info.bones[b];
                        if (name.empty()) continue;
                        // A Selectable's LABEL is its ImGui id, and a skeleton
                        // may legitimately repeat a name - suffix with the node
                        // index so two rows cannot collide (app.hpp's knob rule).
                        const std::string id =
                            name + "##b" + std::to_string(b);
                        if (ImGui::Selectable(id.c_str(), name == bone) &&
                            name != bone) {
                            bone = name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (i == (int)footik::Slot::LeftAnkle) ImGui::Separator();
            }
            ImGui::PopID();

            if (!report.problems.empty()) {
                ImGui::Spacing();
                for (const std::string& p : report.problems)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s",
                                       p.c_str());
                ImGui::TextDisabled(
                    "Bones are bound by NAME, so a re-export that reorders the\n"
                    "hierarchy is safe - but a renamed bone has to be re-picked.");
            }
            ImGui::EndTabItem();
        }

        // --- Tuning ---------------------------------------------------------
        if (ImGui::BeginTabItem("Tuning")) {
            auto drag = [&](const char* label, float* v, float step, float lo,
                            float hi, const char* fmt, const char* tip) {
                ImGui::SetNextItemWidth(scaled(160));
                ImGui::DragFloat(label, v, step, lo, hi, fmt);
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                if (tip) {
                    ImGui::SameLine();
                    prefHelp(tip);
                }
            };
            ImGui::SeparatorText("Sole");
            drag("Sole below ankle", &rig.soleOffset, 0.005f, 0.0f, 2.0f,
                 "%.3f model units",
                 "Ankle-to-floor gap, in the MODEL's own units, so it follows "
                 "each instance's scale. Raise it if shoes sink into the "
                 "ground, lower it if they hover above it.");
            ImGui::SeparatorText("Probes");
            drag("Probe above", &rig.probeUp, 0.01f, 0.01f, 5.0f, "%.2f",
                 "How far above the animated sole the ground search starts.");
            drag("Probe below", &rig.probeDown, 0.01f, 0.01f, 5.0f, "%.2f",
                 "How far below the animated sole the ground search reaches. "
                 "A descent adds its own reach on top of this (see below), so "
                 "this only has to cover level ground and slopes.");
            ImGui::SeparatorText("Contact");
            drag("Plant distance", &rig.plantDistance, 0.005f, 0.0f, 1.0f,
                 "%.3f",
                 "How close the animated sole must come to the ground before "
                 "the foot may lock to it.");
            drag("Release distance", &rig.releaseDistance, 0.005f,
                 rig.plantDistance, 2.0f, "%.3f",
                 "How far the animation may pull away from a planted foot "
                 "before it unlocks.");
            if (rig.releaseDistance < rig.plantDistance)
                rig.releaseDistance = rig.plantDistance;
            drag("Max pelvis correction", &rig.maxPelvis, 0.01f, 0.0f, 2.0f,
                 "%.2f",
                 "How far the hips may sink so the lower leg can reach its "
                 "contact. This is a reach safeguard, not a second ground "
                 "magnet: only a planted foot that needs more reach lowers it.");
            drag("Max foot tilt", &rig.maxFootAngle, 1.0f, 0.0f, 80.0f,
                 "%.0f deg",
                 "Caps the pitch/roll a planted ankle takes from the surface "
                 "normal. 0 keeps the authored ankle orientation; 35 follows "
                 "ordinary slopes while refusing extreme triangle normals.");
            ImGui::SeparatorText("Steps");
            drag("Toe clearance", &rig.toeClearance, 0.005f, 0.0f, 1.0f, "%.3f",
                 "Extra gap requested above a higher surface detected ahead of "
                 "an airborne shoe, so a curb or a stair riser does not "
                 "swallow the toe. 0 disables swing clearance.");
            drag("Descent reach", &rig.descendReach, 0.01f, 0.0f, 3.0f, "%.2f",
                 "Extra downward reach a DESCENDING character may add on top "
                 "of the plant/pelvis bands - the amount that decides whether "
                 "a foot finds the next step down or finishes its stride in "
                 "the air. Spent only where a raycast already proved support "
                 "that far below, so it cannot pull a foot through the floor. "
                 "Roughly the tallest step the character should walk down; 0 "
                 "restores the level-ground behaviour exactly.");
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Distances are project world units, except the sole offset.\n"
                "The first visible frame establishes the pose; correction\n"
                "starts on the next one.");
            ImGui::EndTabItem();
        }

        // --- Per-clip rules -------------------------------------------------
        if (ImGui::BeginTabItem("Clips")) {
            ImGui::TextDisabled(
                "A rig is a property of the skeleton; whether a shoe should\n"
                "stop at the floor is a property of the MOTION. Turn the\n"
                "solver off for a jump, a sit or a climb, and give a run\n"
                "wider tolerances than a walk.");
            ImGui::Spacing();
            const std::vector<std::string> clips = effectiveClips(footIkModel_);
            if (clips.empty()) {
                ImGui::TextDisabled("This model carries no animation clips.");
            } else if (ImGui::BeginTable("##footik-clips", 4,
                                         ImGuiTableFlags_SizingStretchProp |
                                             ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Clip");
                ImGui::TableSetupColumn("Solve");
                ImGui::TableSetupColumn("Contact x");
                ImGui::TableSetupColumn("Clearance x");
                ImGui::TableHeadersRow();
                for (const std::string& effective : clips) {
                    // Rules are stored under the SOURCE name, like the
                    // Animation Editor's clip edits, so renaming a clip there
                    // cannot orphan a rule.
                    const std::string source = animedit::sourceName(
                        project_, footIkModel_, effective);
                    const FootIkClipRule* existing = rig.findClip(source);
                    FootIkClipRule row = existing ? *existing : FootIkClipRule{};
                    row.clip = source;
                    bool rowChanged = false;
                    ImGui::TableNextRow();
                    ImGui::PushID(source.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(effective.c_str());
                    if (source != effective && ImGui::IsItemHovered())
                        ImGui::SetTooltip("source clip: %s", source.c_str());
                    ImGui::TableNextColumn();
                    if (ImGui::Checkbox("##solve", &row.solve))
                        rowChanged = true;
                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled(!row.solve);
                    ImGui::SetNextItemWidth(scaled(90));
                    ImGui::DragFloat("##plant", &row.plantScale, 0.01f, 0.1f,
                                     4.0f, "%.2f");
                    rowChanged |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Multiplies this clip's plant, release and descent\n"
                            "reach. A run covers more ground per frame than a\n"
                            "walk, so its contact bands want to be wider.");
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(scaled(90));
                    ImGui::DragFloat("##clearance", &row.clearanceScale, 0.01f,
                                     0.0f, 4.0f, "%.2f");
                    rowChanged |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Multiplies this clip's toe clearance. 0 leaves a\n"
                            "clip's own foot lift alone entirely.");
                    ImGui::EndDisabled();
                    ImGui::PopID();
                    if (!rowChanged) continue;
                    // Write back: keep the row while it says something, drop it
                    // the moment it is back to the rig's own behaviour (prune
                    // does the same on commit, this keeps the list tidy live).
                    auto it = std::find_if(rig.clips.begin(), rig.clips.end(),
                                           [&](const FootIkClipRule& c) {
                                               return c.clip == source;
                                           });
                    if (row.isDefault()) {
                        if (it != rig.clips.end()) rig.clips.erase(it);
                    } else if (it != rig.clips.end()) {
                        *it = row;
                    } else {
                        rig.clips.push_back(row);
                    }
                    changed = true;
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        // --- The learned assist --------------------------------------------
        if (ImGui::BeginTabItem("Neural assist")) {
            changed |= ImGui::Checkbox("Neural landing prediction (VU0)",
                                       &rig.neuralAssist);
            ImGui::SameLine();
            prefHelp(
                "Experimental learned layer INSIDE the solver, not a "
                "replacement for it. A deterministic 20-16-6 network reads the "
                "motion history, the slope-removed ahead probes and the "
                "locomotion grade, then advises a raycast-ranked foothold fan, "
                "bounded stair clearance, early release and how much of the "
                "descent reach to spend. Collision and the procedural limits "
                "still approve every plant, so it cannot invent a surface.");
            ImGui::BeginDisabled(!rig.neuralAssist);
            ImGui::SetNextItemWidth(scaled(160));
            ImGui::DragFloat("Neural influence", &rig.neuralStrength, 0.01f,
                             0.0f, 1.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            prefHelp(
                "Scales every learned output. 0 still evaluates the network "
                "for telemetry but applies nothing, which is the honest way to "
                "A/B it against the procedural solver.");
            ImGui::EndDisabled();
            ImGui::Spacing();
            ImGui::TextDisabled(
                "The multiply-accumulate work runs in VU0 macro mode: no VU0\n"
                "micro memory, no model file, no allocation in the loop. The\n"
                "committed weights are reproduced by\n"
                "tools/train-foot-neural.py - see docs/foot-ik.md.");
            ImGui::EndTabItem();
        }

        // --- Who runs it ----------------------------------------------------
        if (ImGui::BeginTabItem("Instances")) {
            ImGui::TextDisabled(
                "Which characters in this project run the solver. An instance\n"
                "that does needs its own corrected pose and cannot share a\n"
                "skinned mesh with lockstep copies, so a distant extra is\n"
                "cheaper without it. The flow graph's Set Foot IK node flips\n"
                "the same switch while the game runs.");
            ImGui::Spacing();
            if (!rig.enabled)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "The rig above is off, so none of these "
                                   "solve yet.");
            int rows = 0;
            for (size_t si = 0; si < project_.scenes.size(); ++si) {
                SceneData& sc = project_.scenes[si];
                bool header = false;
                for (SceneObject& o : sc.objects) {
                    if (o.modelPath != footIkModel_) continue;
                    if (!header) {
                        ImGui::SeparatorText(sc.name.c_str());
                        header = true;
                    }
                    ++rows;
                    ImGui::PushID((int)si * 4096 + rows);
                    if (ImGui::Checkbox(o.name.c_str(), &o.footIk))
                        changed = true;
                    ImGui::PopID();
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "(%s)", o.type == PrimitiveType::Player ? "player"
                                                                : "model");
                }
            }
            if (rows == 0)
                ImGui::TextDisabled(
                    "No object in this project uses this model yet.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    commitIfEdited();
    ImGui::End();
}
