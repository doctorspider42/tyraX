// -------------------------------------------------------------------------
// Tools > Particle Editor (docs/particles.md): the project's particle library.
// An effect is defined once - behaviour, look, texture - and emitters and a
// vehicle's tyre smoke name it. Textures can be generated here: smoke puffs,
// flames and glows baked into res/materials/particles by particletex.cpp.
//
// App:: members declared in app.hpp, own translation unit (the prefab_ui.cpp
// precedent). The library is Section::Particles; a linked emitter's fields are
// copied in by project::applyParticleEffects inside commitChange().
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "gl_loader.h"
#include "particletex.hpp"

#include <imgui.h>

namespace {

const char* const kKinds[] = {"Fire", "Smoke", "Fog", "Sparks", "Rain", "Custom"};
const char* const kTexKinds[] = {"None (pick a material)", "Smoke puff", "Flame",
                                 "Glow / spark"};

int usersOf(const Project& p, const std::string& name, int* vehicles) {
    int n = 0, v = 0;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) n += o.particleEffect == name;
    for (const Prefab& pf : p.prefabs)
        for (const SceneObject& o : pf.objects) n += o.particleEffect == name;
    for (const VehicleDef& d : p.vehicles) v += d.smokeEffect == name;
    if (vehicles) *vehicles = v;
    return n;
}

std::string uniqueName(const Project& p, const std::string& base) {
    for (int k = 1;; ++k) {
        const std::string n = k == 1 ? base : base + " " + std::to_string(k);
        if (!project::findParticleEffect(p, n)) return n;
    }
}

float frand(unsigned& s) {
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) * (1.0f / 16777216.0f);
}

}  // namespace

void App::openParticleEditor(const std::string& effect) {
    showParticles_ = true;
    pendingFocusWindow_ = "Particle Editor";
    for (size_t k = 0; k < project_.particleEffects.size(); ++k)
        if (project_.particleEffects[k].name == effect) particleSel_ = (int)k;
}

bool App::particleBakeTexture(ParticleEffect& fx) {
    std::string err;
    const std::string mtl = particletex::writeAssets(project_.dir, fx.name, fx.texGen, &err);
    if (mtl.empty()) {
        particleStatus_ = "Texture not written: " + err;
        return false;
    }
    fx.materialPath = mtl;
    // A soft alpha ramp does not survive the palettized (CLUT) bake - pin the
    // library's textures to full colour. 64x64 RGBA32 is 16 KB of GS VRAM.
    for (int k = 0; k < std::max(1, fx.texGen.frames); ++k)
        project_.textureQuality[particletex::framePath(mtl, k)] = "none";
    viewport_.invalidateAssets();  // the viewport caches textures by path
    particleStatus_ = "Wrote " + mtl;
    return true;
}

void App::drawParticleEditorWindow() {
    if (!showParticles_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(720), scaled(560)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Particle Editor", &showParticles_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }

    // Edits commit when no widget is held: a drag must not become sixty undo
    // steps (applyParticleEffects rewrites the linked emitters in the SCENES,
    // which is what the undo history snapshots).
    static std::string committedSection;
    static const Project* committedFor = nullptr;
    if (committedFor != &project_ || committedSection.empty()) {
        committedSection = project::sectionJson(project_, project::Section::Particles);
        committedFor = &project_;
    }
    bool sceneEdit = false;  // a verb that touched the scenes directly

    auto& lib = project_.particleEffects;
    if (particleSel_ >= (int)lib.size()) particleSel_ = (int)lib.size() - 1;

    // --- toolbar -------------------------------------------------------------
    if (ImGui::Button("New effect...")) ImGui::OpenPopup("##fxnew");
    if (ImGui::BeginPopup("##fxnew")) {
        for (int k = 0; k < 6; ++k)
            if (ImGui::MenuItem(kKinds[k])) {
                ParticleEffect fx = project::particlePreset(k);
                fx.id = project::newObjectId();
                fx.name = uniqueName(project_, fx.name);
                if (fx.texGen.kind != 0) particleBakeTexture(fx);
                lib.push_back(fx);
                particleSel_ = (int)lib.size() - 1;
            }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(particleSel_ < 0);
    if (ImGui::Button("Duplicate") && particleSel_ >= 0) {
        ParticleEffect fx = lib[(size_t)particleSel_];
        fx.id = project::newObjectId();
        fx.name = uniqueName(project_, fx.name);
        if (fx.texGen.kind != 0) particleBakeTexture(fx);
        lib.push_back(fx);
        particleSel_ = (int)lib.size() - 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete") && particleSel_ >= 0) {
        // Linked emitters keep the look they last copied; vehicles fall back
        // to the built-in smoke. The texture file stays - it is an asset.
        project::renameParticleEffectRefs(project_, lib[(size_t)particleSel_].name, "");
        lib.erase(lib.begin() + particleSel_);
        particleSel_ = std::min(particleSel_, (int)lib.size() - 1);
        sceneEdit = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Place in scene") && particleSel_ >= 0) {
        const std::string name = lib[(size_t)particleSel_].name;
        addObject(PrimitiveType::Emitter, false);
        if (!project_.objects().empty()) project_.objects().back().particleEffect = name;
        sceneEdit = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Adds an emitter linked to this effect where objects are inserted.");
    ImGui::EndDisabled();
    if (!particleStatus_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", particleStatus_.c_str());
    }
    ImGui::Separator();

    // --- the list ------------------------------------------------------------
    ImGui::BeginChild("##fxlist", ImVec2(scaled(190), 0), ImGuiChildFlags_Borders);
    if (lib.empty()) ImGui::TextDisabled("No effects yet.");
    for (size_t k = 0; k < lib.size(); ++k) {
        int veh = 0;
        const int n = usersOf(project_, lib[k].name, &veh);
        char label[256];
        std::snprintf(label, sizeof label, "%s##fxrow%zu", lib[k].name.c_str(), k);
        if (ImGui::Selectable(label, particleSel_ == (int)k)) particleSel_ = (int)k;
        if (n + veh > 0 && ImGui::IsItemHovered())
            ImGui::SetTooltip("%d emitter(s), %d vehicle(s)", n, veh);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- the selected effect -------------------------------------------------
    ImGui::BeginChild("##fxbody", ImVec2(0, 0));
    if (particleSel_ >= 0) {
        ParticleEffect& fx = lib[(size_t)particleSel_];

        // Name: a rename retargets every emitter and vehicle that names it.
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof nameBuf, "%s", fx.name.c_str());
        if (ImGui::InputText("Name", nameBuf, sizeof nameBuf)) {
            if (particleRenameFrom_.empty()) particleRenameFrom_ = fx.name;
            fx.name = nameBuf;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !particleRenameFrom_.empty()) {
            std::string to = fx.name;
            fx.name = particleRenameFrom_;  // uniqueness is judged without itself
            if (to.empty() || (to != fx.name && project::findParticleEffect(project_, to)))
                to = uniqueName(project_, to.empty() ? "Effect" : to);
            fx.name = to;
            project::renameParticleEffectRefs(project_, particleRenameFrom_, to);
            particleRenameFrom_.clear();
            sceneEdit = true;
        }
        int veh = 0;
        const int users = usersOf(project_, fx.name, &veh);
        ImGui::TextDisabled("Used by %d emitter(s), %d vehicle(s)", users, veh);

        ImGui::SeparatorText("Behaviour");
        ImGui::Combo("Motion", &fx.kind, kKinds, 6);
        prefHelp("Fire .. Rain are the built-in motions; Custom uses the\n"
                 "speed / spread / gravity knobs below.");
        ImGui::DragInt("Count", &fx.count, 1.0f, 1, 256);
        ImGui::DragFloat("Size", &fx.size, 0.02f, 0.05f, 8.0f, "%.2f");
        ImGui::ColorEdit3("Tint", fx.color);
        ImGui::Checkbox("Additive (glows)", &fx.additive);
        prefHelp("Adds light instead of covering: fire, sparks, magic.\n"
                 "Leave off for smoke, dust and fog.");
        if (fx.kind == 2 || fx.kind == 5)
            ImGui::SliderFloat("Opacity", &fx.opacity, 0.0f, 1.0f, "%.2f");
        if (fx.kind == 5) {
            ImGui::DragFloat("Speed", &fx.speed, 0.05f, 0.0f, 50.0f, "%.2f u/s");
            ImGui::DragFloat("Spread", &fx.spread, 0.5f, 0.0f, 90.0f, "%.0f deg");
            ImGui::DragFloat("Gravity", &fx.gravity, 0.1f, -30.0f, 50.0f, "%.1f u/s2");
            ImGui::DragFloat("Weight", &fx.weight, 0.02f, 0.05f, 10.0f, "%.2f");
            ImGui::DragFloat("Lifetime", &fx.life, 0.05f, 0.1f, 10.0f, "%.2f s");
            ImGui::DragFloat("Grow", &fx.grow, 0.02f, 0.1f, 4.0f, "%.2f x");
            ImGui::Checkbox("Die on terrain", &fx.dieOnGround);
        }

        ImGui::SeparatorText("Texture");
        ParticleTexGen& g = fx.texGen;
        const ParticleTexGen before = g;
        ImGui::Combo("Generate", &g.kind, kTexKinds, 4);
        bool recipeCommitted = g.kind != before.kind;
        if (g.kind == 0) {
            SceneObject tmp;
            tmp.type = PrimitiveType::Emitter;
            tmp.materialPath = fx.materialPath;
            if (drawMaterialCombo(tmp)) fx.materialPath = tmp.materialPath;
        } else {
            int sizeIdx = g.size == 32 ? 0 : (g.size == 128 ? 2 : 1);
            const char* sizes[] = {"32 x 32", "64 x 64", "128 x 128"};
            if (ImGui::Combo("Resolution", &sizeIdx, sizes, 3)) {
                g.size = sizeIdx == 0 ? 32 : (sizeIdx == 2 ? 128 : 64);
                recipeCommitted = true;
            }
            prefHelp("GS VRAM per frame: 4 / 16 / 64 KB (full colour -\n"
                     "a soft alpha ramp does not survive a palette).");
            int frameIdx = g.frames >= 8 ? 3 : (g.frames >= 4 ? 2 : (g.frames >= 2 ? 1 : 0));
            const char* frameNames[] = {"1 (still)", "2", "4", "8"};
            if (ImGui::Combo("Frames", &frameIdx, frameNames, 4)) {
                g.frames = 1 << frameIdx;
                recipeCommitted = true;
            }
            prefHelp("A flipbook: the noise moves through a seamless loop and the\n"
                     "console swaps the frames - one draw, only the texture changes.");
            if (g.frames > 1) {
                ImGui::SliderFloat("Frames / s", &g.fps, 1.0f, 30.0f, "%.0f");
                recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
            }
            ImGui::TextDisabled("GS VRAM: %d KB", g.size * g.size * 4 * std::max(1, g.frames) / 1024);
            auto knob = [&](const char* label, float* v, float lo, float hi) {
                ImGui::SliderFloat(label, v, lo, hi, "%.2f");
                recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
            };
            ImGui::DragInt("Seed", &g.seed, 1.0f, 0, 99999);
            recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
            knob("Softness", &g.softness, 0.0f, 1.0f);
            knob("Detail", &g.detail, 0.0f, 1.0f);
            knob("Noise scale", &g.scale, 0.3f, 3.0f);
            if (g.kind == 2) knob("Turbulence", &g.turbulence, 0.0f, 1.0f);
            if (g.kind == 2 || g.kind == 3) knob("Heat", &g.heat, 0.0f, 1.0f);
            if (g.kind != 2) {
                ImGui::ColorEdit3("Texture colour", g.color);
                recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
            }
            if (ImGui::Button("Regenerate")) recipeCommitted = true;
            ImGui::SameLine();
            ImGui::TextDisabled("%s", fx.materialPath.empty() ? "(not written yet)"
                                                               : fx.materialPath.c_str());
        }
        // Re-bake on RELEASE, not per frame of a drag: it writes two files.
        if (g.kind != 0 && recipeCommitted) particleBakeTexture(fx);

        // --- previews --------------------------------------------------------
        if (g.kind != 0 && (!particleTexValid_ || particleTexFor_ != g)) {
            const int frames = std::max(1, g.frames);
            while ((int)particleTexIds_.size() < frames) {
                unsigned int id = 0;
                glGenTextures(1, &id);
                particleTexIds_.push_back(id);
            }
            for (int k = 0; k < frames; ++k) {
                const std::vector<unsigned char> px = particletex::generate(g, k);
                glBindTexture(GL_TEXTURE_2D, particleTexIds_[(size_t)k]);
                glUploadTexRgba(g.size, g.size, px.data());
            }
            particleTexFor_ = g;
            particleTexValid_ = true;
        }
        const bool haveTex = g.kind != 0 && particleTexValid_;
        // the frame the console would show now (same arithmetic as the game)
        const int frameNow =
            haveTex && g.frames > 1 ? (int)(ImGui::GetTime() * g.fps) % g.frames : 0;
        const unsigned int particleTexId_ =
            haveTex ? particleTexIds_[(size_t)frameNow] : 0u;
        const float box = scaled(180);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 bg = IM_COL32(28, 32, 44, 255);

        ImGui::SeparatorText("Preview");
        // the texture itself, on a dark card
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##fxtex", ImVec2(box * 0.6f, box));
        dl->AddRectFilled(p0, ImVec2(p0.x + box * 0.6f, p0.y + box), bg, 4.0f);
        if (haveTex)
            dl->AddImage((ImTextureID)(intptr_t)particleTexId_,
                         ImVec2(p0.x + 4, p0.y + box * 0.5f - box * 0.28f),
                         ImVec2(p0.x + box * 0.6f - 4, p0.y + box * 0.5f + box * 0.28f));
        ImGui::SameLine();

        // an animated 2D approximation of the motion
        ImVec2 q0 = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##fxanim", ImVec2(w > box ? w : box, box));
        const ImVec2 q1(q0.x + (w > box ? w : box), q0.y + box);
        dl->AddRectFilled(q0, q1, bg, 4.0f);
        dl->PushClipRect(q0, q1, true);
        {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt > 0.05f) dt = 0.05f;
            // the knobs each motion actually uses (Custom's own; the presets'
            // approximated from the runtime's constants)
            float speed = 1.2f, spread = 25.0f, grav = 0.0f, life = 1.5f, grow = 1.0f;
            float alphaPeak = 0.6f;
            switch (fx.kind) {
                case 0: speed = 2.0f, spread = 12.0f, grav = -1.0f, life = 0.9f, grow = 0.6f, alphaPeak = 0.7f; break;
                case 1: speed = 0.9f, spread = 20.0f, grav = -0.3f, life = 2.6f, grow = 1.6f, alphaPeak = 0.35f; break;
                case 2: speed = 0.2f, spread = 90.0f, grav = 0.0f, life = 4.0f, grow = 1.0f, alphaPeak = fx.opacity * 0.47f; break;
                case 3: speed = 4.0f, spread = 40.0f, grav = 9.8f, life = 0.7f, grow = 1.0f, alphaPeak = 0.86f; break;
                case 4: speed = 6.0f, spread = 2.0f, grav = 9.8f, life = 1.0f, grow = 1.0f, alphaPeak = 0.55f; break;
                default: speed = fx.speed, spread = fx.spread, grav = fx.gravity, life = fx.life, grow = fx.grow, alphaPeak = fx.opacity; break;
            }
            const int n = std::min(fx.count, 128);
            if ((int)particlePreview_.size() != n) particlePreview_.assign((size_t)n, {0, 0, 0, 0, 0, 1, 0});
            const float unit = box / 5.0f;  // screen px per world unit
            const float ox = (q0.x + q1.x) * 0.5f;
            const float oy = fx.kind == 4 ? q0.y + 6.0f : q1.y - box * 0.18f;
            for (size_t i = 0; i < particlePreview_.size(); ++i) {
                auto& d = particlePreview_[i];
                if (d.life <= 0.0f) {
                    if (frand(particlePreviewRng_) > dt * n / life) continue;
                    const float a = (frand(particlePreviewRng_) * 2.0f - 1.0f) * spread * 0.01745f;
                    const float sp = speed * (0.8f + 0.4f * frand(particlePreviewRng_));
                    const float down = fx.kind == 4 ? -1.0f : 1.0f;
                    d.x = (frand(particlePreviewRng_) - 0.5f) * (fx.kind == 4 ? 4.0f : 0.4f);
                    d.y = 0.0f;
                    d.vx = std::sin(a) * sp;
                    d.vy = std::cos(a) * sp * down;
                    d.maxLife = d.life = life * (0.75f + 0.5f * frand(particlePreviewRng_));
                    // only fog puffs turn on the console (updateParticles)
                    d.spin = fx.kind == 2 ? frand(particlePreviewRng_) * 6.28f : 0.0f;
                }
                d.life -= dt;
                d.vy -= grav * dt;
                const float drag = 1.0f - std::min(0.9f, dt * 0.6f / std::max(0.05f, fx.weight));
                if (fx.kind == 5) d.vx *= drag, d.vy *= drag;
                d.x += d.vx * dt;
                d.y += d.vy * dt;
                if (fx.kind == 2) d.spin += ((i & 1) ? 0.3f : -0.3f) * dt;
                if (d.life <= 0.0f) continue;
                const float t = d.life / d.maxLife;  // 1 -> 0
                const float sz = fx.size * unit * (1.0f + (grow - 1.0f) * (1.0f - t)) *
                                 (fx.kind == 3 ? 0.35f : 1.0f);
                const float alpha = alphaPeak * (fx.kind == 2 ? (t < 0.5f ? t * 2 : (1 - t) * 2)
                                                              : std::min(1.0f, t * 1.5f));
                const float cx = ox + d.x * unit, cy = oy - d.y * unit;
                const ImU32 col = ImGui::ColorConvertFloat4ToU32(
                    ImVec4(fx.color[0], fx.color[1], fx.color[2], alpha));
                if (haveTex) {
                    const float c = std::cos(d.spin), s = std::sin(d.spin);
                    // odd slots mirrored, as on the console (not rain, not fog)
                    const float mir = ((i & 1) && fx.kind != 4 && fx.kind != 2) ? -1.0f : 1.0f;
                    auto corner = [&](float u, float v) {
                        u *= mir;
                        return ImVec2(cx + (u * c - v * s) * sz, cy + (u * s + v * c) * sz);
                    };
                    dl->AddImageQuad((ImTextureID)(intptr_t)particleTexId_, corner(-1, -1),
                                     corner(1, -1), corner(1, 1), corner(-1, 1), ImVec2(0, 0),
                                     ImVec2(1, 0), ImVec2(1, 1), ImVec2(0, 1), col);
                } else {
                    dl->AddCircleFilled(ImVec2(cx, cy), sz * 0.7f, col);
                }
            }
        }
        dl->PopClipRect();
    } else {
        ImGui::TextDisabled("Pick or create an effect.");
    }
    ImGui::EndChild();

    if (sceneEdit ||
        (!ImGui::IsAnyItemActive() &&
         project::sectionJson(project_, project::Section::Particles) != committedSection)) {
        commitChange();
        committedSection = project::sectionJson(project_, project::Section::Particles);
    }
    ImGui::End();
}
