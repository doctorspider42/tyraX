// -------------------------------------------------------------------------
// Tools > Particle Editor (docs/particles.md): the project's particle library.
// An effect is defined once - behaviour, look, texture - and emitters and a
// vehicle's tyre smoke name it. Textures can be generated here: smoke puffs,
// flames and glows baked into res/materials/particles by particletex.cpp.
//
// Layout: library | animated preview (the hero) | inspector (name, layer
// stack, Behaviour / Texture / Placement tabs) with Place in scene as the
// one primary action.
//
// App:: members declared in app.hpp, own translation unit (the prefab_ui.cpp
// precedent). The library is Section::Particles; a linked emitter's fields are
// copied in by project::applyParticleEffects inside commitChange().
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "gl_loader.h"
#include "particletex.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"
#include "update.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace {

void openDocPage(const char* page) {
    platform::openUrl(std::string("https://github.com/") + update::kRepo + "/blob/main/docs/" + page +
                      ".md");
}

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
    const int n = particletex::bakeEffect(project_, fx, &err);
    if (n < 0) {
        particleStatus_ = "Texture not written: " + err;
        return false;
    }
    viewport_.invalidateAssets();  // the viewport caches textures by path
    particleStatus_ = n ? "Textures written to " + std::string(particletex::kDir) : "";
    return true;
}

void App::drawParticleEditorWindow() {
    if (!showParticles_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1180), scaled(760)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Particle Editor", &showParticles_)) {
        ImGui::End();
        return;
    }
    if (!hasProject_) {
        ImGui::TextDisabled("Open a project first.");
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();

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
    if (particleSel_ < 0 && !lib.empty()) particleSel_ = 0;

    auto newEffect = [&](int kind) {
        ParticleEffect fx = project::particlePreset(kind);
        fx.id = project::newObjectId();
        fx.name = uniqueName(project_, fx.name);
        particleBakeTexture(fx);
        lib.push_back(fx);
        particleSel_ = (int)lib.size() - 1;
        particleLayerSel_ = 0;
        particleHidden_.clear();
    };
    auto duplicate = [&](int k) {
        ParticleEffect fx = lib[(size_t)k];
        fx.id = project::newObjectId();
        fx.name = uniqueName(project_, fx.name);
        particleBakeTexture(fx);
        lib.push_back(fx);
        particleSel_ = (int)lib.size() - 1;
        particleHidden_.clear();
    };
    int deleteAt = -1;

    // --- header: count, New effect, docs -------------------------------------
    {
        const float rowH = ImGui::GetFrameHeight();
        const float y0 = ImGui::GetCursorPosY();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d effect%s", (int)lib.size(), lib.size() == 1 ? "" : "s");
        if (!particleStatus_.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("- %s", particleStatus_.c_str());
        }
        const char* newLabel = "+ New effect";
        const float nw = ImGui::CalcTextSize(newLabel).x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - nw - rowH - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetCursorPosY(y0);
        if (ui::SecondaryButton(newLabel, nw)) ImGui::OpenPopup("##fxnew");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Starts from a motion preset, with a matching texture baked.");
        if (ImGui::BeginPopup("##fxnew")) {
            for (int k = 0; k < 6; ++k)
                if (ImGui::MenuItem(kKinds[k])) newEffect(k);
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosY(y0);
        if (ui::HelpButton("Particle docs", rowH, "Open docs/particles.md")) openDocPage("particles");
    }

    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float listW = scaled(220), inspW = scaled(350);
    const float gap = ImGui::GetStyle().ItemSpacing.x;

    // --- library ---------------------------------------------------------------
    ImGui::BeginChild("##fxlist", ImVec2(listW, bodyH), ImGuiChildFlags_None);
    ui::Caption("Library");
    {
        const float noteH = ImGui::GetTextLineHeightWithSpacing() * 3.2f;
        ImGui::BeginChild("##fxrows", ImVec2(0, -noteH));
        const float rowH = ImGui::GetFontSize() * 2.6f;
        for (size_t k = 0; k < lib.size(); ++k) {
            const ParticleEffect& e = lib[k];
            int veh = 0;
            const int n = usersOf(project_, e.name, &veh);
            char label[256], sub[96];
            std::snprintf(label, sizeof label, "%s##fxrow%zu", e.name.c_str(), k);
            const int layers = (int)e.layers.size() + 1;
            if (veh > 0)
                std::snprintf(sub, sizeof sub, "%d layer%s - %d emitter%s, %d vehicle%s", layers,
                              layers == 1 ? "" : "s", n, n == 1 ? "" : "s", veh, veh == 1 ? "" : "s");
            else
                std::snprintf(sub, sizeof sub, "%d layer%s - %d emitter%s", layers, layers == 1 ? "" : "s",
                              n, n == 1 ? "" : "s");
            const bool sel = particleSel_ == (int)k;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton(label, ImVec2(w, rowH)) && !sel) {
                particleSel_ = (int)k;
                particleLayerSel_ = 0;
                particleHidden_.clear();
            }
            uiscript::markLastItemChecked(sel);
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Duplicate")) duplicate((int)k);
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                if (ImGui::MenuItem("Delete")) deleteAt = (int)k;
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            const bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding + 1.0f;
            if (sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(sem.surface), r);
            else if (hov)
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.5f)), r);
            // The effect's glow: its main layer's tint on a dark tile.
            const float d = rowH - scaled(10);
            const ImVec2 t0(p.x + scaled(6), p.y + scaled(5));
            dl->AddRectFilled(t0, ImVec2(t0.x + d, t0.y + d), ImGui::GetColorU32(sem.ground), r + 2.0f);
            const ImVec2 c(t0.x + d * 0.5f, t0.y + d * 0.5f);
            for (int i = 5; i >= 1; --i)
                dl->AddCircleFilled(c, d * (0.16f + 0.065f * (float)i),
                                    ImGui::ColorConvertFloat4ToU32(
                                        ImVec4(e.color[0], e.color[1], e.color[2], e.additive ? 0.22f : 0.16f)),
                                    24);
            const float tx = t0.x + d + scaled(10);
            const float fs = ImGui::GetFontSize();
            dl->PushClipRect(p, ImVec2(p.x + w - 2.0f, p.y + rowH), true);
            {
                ui::FontScope bold(sel ? ui::boldFont() : nullptr);
                dl->AddText(ImVec2(tx, p.y + rowH * 0.5f - fs), ImGui::GetColorU32(sem.text), e.name.c_str());
            }
            dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(tx, p.y + rowH * 0.5f + 1.0f),
                        ImGui::GetColorU32(sem.textDim), sub);
            dl->PopClipRect();
        }
        if (lib.empty()) {
            if (ui::EmptyState(ui::Glyph::Sparkle, "No effects yet",
                               "Start from a motion preset; its texture is generated for you.",
                               "New fire effect"))
                newEffect(0);
        }
        ImGui::EndChild();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Defined once, used by every emitter and by vehicle tyre smoke. Renames follow.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, gap);

    if (particleSel_ < 0) {
        ImGui::BeginChild("##fxbody", ImVec2(0, bodyH));
        ui::EmptyState(ui::Glyph::Sparkle, "Pick or create an effect",
                       "An effect is defined here once; emitters and vehicle tyre smoke name it.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    ParticleEffect& fx = lib[(size_t)particleSel_];
    if (particleLayerSel_ > (int)fx.layers.size()) particleLayerSel_ = 0;
    const int layerCount = (int)fx.layers.size() + 1;
    if ((int)particleHidden_.size() != layerCount) particleHidden_.assign((size_t)layerCount, 0);
    auto layerAt = [&](int li) -> ParticleLayer& {
        return li == 0 ? static_cast<ParticleLayer&>(fx) : fx.layers[(size_t)li - 1];
    };
    auto layerName = [&](int li) -> std::string {
        if (li == 0) return "Main";
        const std::string& l = fx.layers[(size_t)li - 1].label;
        return l.empty() ? "Layer " + std::to_string(li) : l;
    };

    // GL textures of every layer's generated recipe (one per flipbook frame).
    if ((int)particleTex_.size() < layerCount) particleTex_.resize((size_t)layerCount);
    for (int li = 0; li < layerCount; ++li) {
        const ParticleTexGen& lg = layerAt(li).texGen;
        ParticleTexCache& tc = particleTex_[(size_t)li];
        if (lg.kind == 0) {
            tc.valid = false;
            continue;
        }
        if (tc.valid && tc.recipe == lg) continue;
        const int frames = std::max(1, lg.frames);
        while ((int)tc.ids.size() < frames) {
            unsigned int id = 0;
            glGenTextures(1, &id);
            tc.ids.push_back(id);
        }
        for (int k = 0; k < frames; ++k) {
            const std::vector<unsigned char> px = particletex::generate(lg, k);
            glBindTexture(GL_TEXTURE_2D, tc.ids[(size_t)k]);
            glUploadTexRgba(lg.size, lg.size, px.data());
        }
        tc.recipe = lg;
        tc.valid = true;
    }
    // the frame the console would show now (same arithmetic as the game)
    auto texNow = [&](int li) -> unsigned int {
        const ParticleTexCache& tc = particleTex_[(size_t)li];
        if (!tc.valid) return 0u;
        const ParticleTexGen& lg = tc.recipe;
        const int f = lg.frames > 1 ? (int)(ImGui::GetTime() * lg.fps) % lg.frames : 0;
        return tc.ids[(size_t)f];
    };

    // --- preview: the hero ------------------------------------------------------
    const float previewW = ImMax(scaled(200), ImGui::GetContentRegionAvail().x - inspW - gap);
    ImGui::BeginChild("##fxpreview", ImVec2(previewW, bodyH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        const ImVec2 q0 = ImGui::GetCursorScreenPos();
        const ImVec2 sz = ImGui::GetContentRegionAvail();
        ImGui::SetNextItemAllowOverlap();  // the overlay controls sit on top
        ImGui::InvisibleButton("Particle preview canvas", sz);
        const ImVec2 q1(q0.x + sz.x, q0.y + sz.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ImGui::GetStyle().FrameRounding + 3.0f;
        const float groundY = q1.y - sz.y * 0.2f;
        dl->AddRectFilled(q0, q1, ImGui::GetColorU32(sem.ground), r);
        dl->AddRectFilledMultiColor(ImVec2(q0.x, groundY), ImVec2(q1.x, q1.y - r),
                                    ImGui::GetColorU32(sem.field), ImGui::GetColorU32(sem.field),
                                    ImGui::GetColorU32(sem.ground), ImGui::GetColorU32(sem.ground));
        dl->AddRect(q0, q1, ImGui::GetColorU32(sem.border), r);
        dl->PushClipRect(q0, q1, true);
        if ((int)particlePreview_.size() != layerCount) particlePreview_.resize((size_t)layerCount);
        float dt = particlePaused_ ? 0.0f : ImGui::GetIO().DeltaTime;
        if (dt > 0.05f) dt = 0.05f;
        const float unit = ImMin(sz.x, sz.y) / 5.5f;  // screen px per world unit
        for (int li = 0; li < layerCount; ++li) {
            const ParticleLayer& P = layerAt(li);
            std::vector<ParticlePreviewDot>& dots = particlePreview_[(size_t)li];
            if (particleHidden_[(size_t)li] || (particleSolo_ && li != particleLayerSel_)) continue;
            // the knobs each motion actually uses (Custom's own; the presets'
            // approximated from the runtime's constants)
            float speed = 1.2f, spread = 25.0f, grav = 0.0f, life = 1.5f, grow = 1.0f;
            float alphaPeak = 0.6f;
            switch (P.kind) {
                case 0: speed = 0.9f, spread = 12.0f, grav = -2.6f, life = 0.8f, grow = 0.6f, alphaPeak = 0.75f; break;
                case 1: speed = 0.9f, spread = 20.0f, grav = -0.3f, life = 2.6f, grow = 1.6f, alphaPeak = 0.35f; break;
                case 2: speed = 0.2f, spread = 90.0f, grav = 0.0f, life = 4.0f, grow = 1.0f, alphaPeak = P.opacity * 0.47f; break;
                case 3: speed = 4.0f, spread = 40.0f, grav = 9.8f, life = 0.7f, grow = 1.0f, alphaPeak = 0.86f; break;
                case 4: speed = 6.0f, spread = 2.0f, grav = 9.8f, life = 1.0f, grow = 1.0f, alphaPeak = 0.55f; break;
                default: speed = P.speed, spread = P.spread, grav = P.gravity, life = P.life, grow = P.grow, alphaPeak = P.opacity; break;
            }
            const int n = std::min(P.count, 128);
            if ((int)dots.size() != n) dots.assign((size_t)n, {0, 0, 0, 0, 0, 1, 0});
            const float ox = (q0.x + q1.x) * 0.5f + (li ? P.offset[0] * unit : 0.0f);
            const float oy = (P.kind == 4 ? q0.y + 6.0f : groundY) - (li ? P.offset[1] * unit : 0.0f);
            const float spawnW = 0.4f * (li ? std::max(0.1f, P.area[0]) : 1.0f);
            const unsigned int tex = texNow(li);
            for (size_t i = 0; i < dots.size(); ++i) {
                auto& d = dots[i];
                if (d.life <= 0.0f) {
                    if (dt <= 0.0f || frand(particlePreviewRng_) > dt * n / life) continue;
                    const float a = (frand(particlePreviewRng_) * 2.0f - 1.0f) * spread * 0.01745f;
                    const float sp = speed * (0.8f + 0.4f * frand(particlePreviewRng_));
                    const float down = P.kind == 4 ? -1.0f : 1.0f;
                    d.x = (frand(particlePreviewRng_) - 0.5f) * (P.kind == 4 ? 4.0f : spawnW);
                    d.y = 0.0f;
                    d.vx = std::sin(a) * sp;
                    d.vy = std::cos(a) * sp * down;
                    d.maxLife = d.life = life * (0.75f + 0.5f * frand(particlePreviewRng_));
                    // only fog puffs turn on the console (updateParticles)
                    d.spin = P.kind == 2 ? frand(particlePreviewRng_) * 6.28f : 0.0f;
                }
                d.life -= dt;
                d.vy -= grav * dt;
                const float drag = 1.0f - std::min(0.9f, dt * 0.6f / std::max(0.05f, P.weight));
                if (P.kind == 5) d.vx *= drag, d.vy *= drag;
                if (P.kind == 0) d.vx -= d.x * 2.2f * dt;  // the column pulls in
                d.x += d.vx * dt;
                d.y += d.vy * dt;
                if (P.kind == 2) d.spin += ((i & 1) ? 0.3f : -0.3f) * dt;
                if (d.life <= 0.0f) continue;
                const float t = d.life / d.maxLife;  // 1 -> 0
                const float s = P.size * unit * (1.0f + (grow - 1.0f) * (1.0f - t)) *
                                (P.kind == 3 ? 0.35f : 1.0f);
                const float alpha = alphaPeak * (P.kind == 2 ? (t < 0.5f ? t * 2 : (1 - t) * 2)
                                                             : std::min(1.0f, t * 1.5f));
                const float cx = ox + d.x * unit, cy = oy - d.y * unit;
                const ImU32 col =
                    ImGui::ColorConvertFloat4ToU32(ImVec4(P.color[0], P.color[1], P.color[2], alpha));
                if (tex) {
                    const float c = std::cos(d.spin), sn = std::sin(d.spin);
                    // odd slots mirrored, as on the console (not rain, not fog)
                    const float mir = ((i & 1) && P.kind != 4 && P.kind != 2) ? -1.0f : 1.0f;
                    const float tall = P.kind == 0 ? 1.45f : 1.0f;  // flames are taller
                    auto corner = [&](float u, float v) {
                        u *= mir;
                        v *= tall;
                        return ImVec2(cx + (u * c - v * sn) * s, cy + (u * sn + v * c) * s);
                    };
                    dl->AddImageQuad((ImTextureID)(intptr_t)tex, corner(-1, -1), corner(1, -1),
                                     corner(1, 1), corner(-1, 1), ImVec2(0, 0), ImVec2(1, 0),
                                     ImVec2(1, 1), ImVec2(0, 1), col);
                } else {
                    dl->AddCircleFilled(ImVec2(cx, cy), s * 0.7f, col);
                }
            }
        }
        dl->PopClipRect();

        // Overlay: All layers | <layer> only, pause, restart.
        const float pad = scaled(10);
        const float oh = ui::OverlayRowHeight();
        const std::string soloText = layerName(particleLayerSel_) + " only";
        const char* segLabels[2] = {"All layers", soloText.c_str()};
        float segW = ui::OverlayPad() * 2.0f;
        for (const char* l : segLabels) segW += ui::OverlayTextWidth(l);
        const char* pauseText = particlePaused_ ? "Play" : "Pause";
        const float btnW = ui::OverlayTextWidth("Pause") + ui::OverlayTextWidth("Restart");
        const ImVec2 c0(q0.x + pad, q0.y + pad);
        const ImVec2 c1(c0.x + segW + btnW + scaled(8), c0.y + oh + ui::OverlayPad() * 2.0f);
        ui::OverlayPanel(dl, c0, c1);
        ImGui::SetCursorScreenPos(ImVec2(c0.x + ui::OverlayPad(), c0.y + ui::OverlayPad()));
        if (ui::OverlayTextButton("All layers", "All layers", !particleSolo_)) particleSolo_ = false;
        uiscript::markLastItemChecked(!particleSolo_);
        ImGui::SameLine(0.0f, 0.0f);
        if (ui::OverlayTextButton("Solo layer", soloText.c_str(), particleSolo_)) particleSolo_ = true;
        uiscript::markLastItemChecked(particleSolo_);
        ImGui::SameLine(0.0f, scaled(8));
        ui::OverlayDivider(dl, ImGui::GetCursorScreenPos().x - scaled(4), c0.y + scaled(6), c1.y - scaled(6));
        if (ui::OverlayTextButton("Pause preview", pauseText, particlePaused_)) particlePaused_ = !particlePaused_;
        ImGui::SameLine(0.0f, 0.0f);
        if (ui::OverlayTextButton("Restart preview", "Restart", false)) {
            for (auto& v : particlePreview_) v.clear();
            particlePaused_ = false;
        }
        // The honest pill.
        {
            const char* pill = "2D approximation - exact motion runs on a placed emitter";
            const ImVec2 ts = ImGui::CalcTextSize(pill);
            const float pw = ts.x + scaled(20);
            if (pw < sz.x - (c1.x - q0.x) - pad * 2.0f) {
                const ImVec2 p0(q1.x - pad - pw, q0.y + pad);
                const ImVec2 p1(q1.x - pad, p0.y + oh + ui::OverlayPad() * 2.0f);
                ui::OverlayPanel(dl, p0, p1);
                dl->AddText(ImVec2(p0.x + scaled(10), (p0.y + p1.y - ts.y) * 0.5f),
                            ImGui::GetColorU32(sem.textDim), pill);
            }
        }
        // The selected layer's generated texture and its cost.
        {
            const ParticleLayer& L = layerAt(particleLayerSel_);
            const ParticleTexGen& g = L.texGen;
            const ParticleTexCache& tc = particleTex_[(size_t)particleLayerSel_];
            const float th = scaled(52);
            std::string title, detail;
            const std::string mat = L.materialPath.empty()
                                        ? std::string("(not written yet)")
                                        : std::filesystem::path(L.materialPath).filename().string();
            if (g.kind == 0) {
                title = L.materialPath.empty() ? std::string("No texture") : mat;
                detail = "picked material";
            } else {
                title = mat + " - " + kTexKinds[g.kind];
                char b[128];
                const int frames = std::max(1, g.frames);
                if (frames > 1)
                    std::snprintf(b, sizeof b, "%dx%d - %d frames @ %.0f fps - %d KB GS", g.size, g.size,
                                  frames, g.fps, g.size * g.size * 4 * frames / 1024);
                else
                    std::snprintf(b, sizeof b, "%dx%d - still - %d KB GS", g.size, g.size,
                                  g.size * g.size * 4 / 1024);
                detail = b;
            }
            float tw = ImGui::CalcTextSize(title.c_str()).x;
            {
                ui::FontScope mono(ui::monoFont());
                tw = ImMax(tw, ImGui::CalcTextSize(detail.c_str()).x * 0.9f);
            }
            const float cw = th + scaled(16) + tw + scaled(14);
            const ImVec2 k0(q0.x + pad, q1.y - pad - th - scaled(16));
            const ImVec2 k1(k0.x + cw, q1.y - pad);
            if (cw < sz.x - pad * 2.0f) {
                ui::OverlayPanel(dl, k0, k1);
                const ImVec2 t0(k0.x + scaled(8), k0.y + scaled(8));
                dl->AddRectFilled(t0, ImVec2(t0.x + th, t0.y + th), ImGui::GetColorU32(sem.ground), 3.0f);
                if (tc.valid) {
                    const int frames = (int)std::min<size_t>(4, std::max(1, g.frames));
                    const int grid = frames > 1 ? 2 : 1;
                    const float cs = th / (float)grid;
                    for (int f = 0; f < frames; ++f) {
                        const ImVec2 a(t0.x + (f % grid) * cs, t0.y + (f / grid) * cs);
                        dl->AddImage((ImTextureID)(intptr_t)tc.ids[(size_t)f], a, ImVec2(a.x + cs, a.y + cs),
                                     ImVec2(0, 0), ImVec2(1, 1),
                                     ImGui::ColorConvertFloat4ToU32(
                                         ImVec4(L.color[0], L.color[1], L.color[2], 1.0f)));
                    }
                }
                const float tx = t0.x + th + scaled(10);
                dl->AddText(ImVec2(tx, t0.y + scaled(6)), ImGui::GetColorU32(sem.text), title.c_str());
                ui::FontScope mono(ui::monoFont());
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f,
                            ImVec2(tx, t0.y + scaled(8) + ImGui::GetFontSize()),
                            ImGui::GetColorU32(sem.textDim), detail.c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, gap);

    // --- inspector ---------------------------------------------------------------
    ImGui::BeginChild("##fxinspector", ImVec2(0, bodyH), ImGuiChildFlags_None);
    const float footerH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 3.0f;
    bool removeLayer = false;
    ImGui::BeginChild("##fxbody", ImVec2(0, -footerH));
    {
        // Name: a rename retargets every emitter and vehicle that names it.
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof nameBuf, "%s", fx.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, ImGui::GetStyle().FramePadding.y));
        ImGui::PushFont(ui::boldFont(), ImGui::GetFontSize() * 1.25f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool edited = ImGui::InputText("##Name", nameBuf, sizeof nameBuf);
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (edited) {
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
        ImGui::TextDisabled("Used by %d emitter%s - %d vehicle%s", users, users == 1 ? "" : "s", veh,
                            veh == 1 ? "" : "s");
        ImGui::Dummy(ImVec2(0, scaled(4)));

        // --- layer stack -------------------------------------------------------
        {
            char cnt[8];
            std::snprintf(cnt, sizeof cnt, "%d", layerCount);
            const float y = ImGui::GetCursorPosY();
            ui::Caption("Layers", cnt);
            const char* addL = "+ Layer";
            const float aw = ImGui::CalcTextSize(addL).x + ImGui::GetStyle().FramePadding.x * 2.6f;
            ImGui::SetCursorPos(ImVec2(ImGui::GetContentRegionMax().x - aw, y - scaled(3)));
            if (ui::GhostButton(addL, aw)) ImGui::OpenPopup("##fxaddlayer");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Each layer is one more emitter of this effect around the\n"
                                  "placed one - a campfire is flame + embers + glow + smoke.\n"
                                  "Every layer is its own draw (one submit) on the console.");
            if (ImGui::BeginPopup("##fxaddlayer")) {
                for (int k = 0; k < 6; ++k)
                    if (ImGui::MenuItem(kKinds[k])) {
                        const ParticleEffect preset = project::particlePreset(k);
                        ParticleLayer L = preset;
                        L.label = preset.name;
                        fx.layers.push_back(L);
                        particleLayerSel_ = (int)fx.layers.size();
                        particleHidden_.push_back(0);
                        particleBakeTexture(fx);
                    }
                ImGui::EndPopup();
            }
            ImGui::SetCursorPosY(y + ImGui::GetFontSize() + scaled(8));
        }
        const float rowH = ImGui::GetFrameHeight() + scaled(6);
        for (int li = 0; li < layerCount && li <= (int)fx.layers.size(); ++li) {
            const ParticleLayer& P = layerAt(li);
            const bool sel = particleLayerSel_ == li;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding + 1.0f;
            if (sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), ImGui::GetColorU32(sem.surface), r);
            const float eyeS = rowH;
            const std::string eyeLabel = "Show " + layerName(li) + "##fxeye" + std::to_string(li);
            ImGui::PushID(li);
            if (ui::EyeToggle(eyeLabel.c_str(), !particleHidden_[(size_t)li], eyeS, true))
                particleHidden_[(size_t)li] = !particleHidden_[(size_t)li];
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Shows or hides this layer in the preview only.");
            ImGui::PopID();
            ImGui::SameLine(0.0f, 0.0f);
            const std::string lab = layerName(li) + "##fxlayer" + std::to_string(li);
            if (ImGui::InvisibleButton(lab.c_str(), ImVec2(ImMax(1.0f, w - eyeS), rowH))) {
                particleLayerSel_ = li;
                if (particleTab_ == 2 && li == 0) particleTab_ = 0;
            }
            uiscript::markLastItemChecked(sel);
            if (li > 0 && ImGui::BeginPopupContextItem()) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                if (ImGui::MenuItem("Remove layer")) {
                    particleLayerSel_ = li;
                    removeLayer = true;
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            const float fs = ImGui::GetFontSize();
            float x = p.x + eyeS + scaled(2);
            const float cy = p.y + rowH * 0.5f;
            dl->AddCircleFilled(ImVec2(x + fs * 0.5f, cy), fs * 0.48f,
                                ImGui::ColorConvertFloat4ToU32(ImVec4(P.color[0], P.color[1], P.color[2], 1.0f)),
                                20);
            x += fs * 1.5f;
            const bool hidden = particleHidden_[(size_t)li] != 0;
            dl->AddText(ImVec2(x, cy - fs * 0.5f), ImGui::GetColorU32(hidden ? sem.textDim : sem.text),
                        layerName(li).c_str());
            // Right side: motion, count, blend badge.
            const char* blend = P.additive ? "add" : "alpha";
            const float bfs = fs * 0.82f;
            const float bw = ImGui::GetFont()->CalcTextSizeA(bfs, FLT_MAX, 0.0f, blend).x + bfs;
            const float bx = p.x + w - bw - scaled(6);
            const ImVec4 bc = P.additive ? sem.warn : sem.textDim;
            dl->AddRectFilled(ImVec2(bx, cy - bfs * 0.6f), ImVec2(bx + bw, cy + bfs * 0.6f),
                              ImGui::GetColorU32(P.additive ? ImVec4(bc.x, bc.y, bc.z, 0.16f) : sem.field),
                              bfs * 0.6f);
            dl->AddText(ImGui::GetFont(), bfs, ImVec2(bx + bfs * 0.5f, cy - bfs * 0.5f),
                        ImGui::GetColorU32(bc), blend);
            char cnt[16];
            std::snprintf(cnt, sizeof cnt, "%d", P.count);
            float cw;
            {
                ui::FontScope mono(ui::monoFont());
                cw = ImGui::CalcTextSize(cnt).x;
                dl->AddText(ImVec2(bx - scaled(10) - cw, cy - fs * 0.5f), ImGui::GetColorU32(sem.textDim), cnt);
            }
            const char* kind = kKinds[std::clamp(P.kind, 0, 5)];
            const float kw = ImGui::CalcTextSize(kind).x;
            dl->AddText(ImVec2(bx - scaled(22) - cw - kw, cy - fs * 0.5f), ImGui::GetColorU32(sem.textDim), kind);
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + scaled(2)));
        }
        ImGui::Dummy(ImVec2(0, scaled(4)));

        ParticleLayer& L = layerAt(particleLayerSel_);
        if (ui::BeginQuietTabBar("##fxtabs")) {
            static int lastFrame = -10;
            const bool force = lastFrame != ImGui::GetFrameCount() - 1;
            static const char* const kTabs[] = {"Behaviour##fxtab", "Texture##fxtab", "Placement##fxtab"};
            for (int i = 0; i < 3; ++i) {
                const ImGuiTabItemFlags f = force && i == particleTab_ ? ImGuiTabItemFlags_SetSelected : 0;
                if (ui::QuietTab(kTabs[i], i == particleTab_, f) && !force) particleTab_ = i;
            }
            ui::EndQuietTabBar();
            lastFrame = ImGui::GetFrameCount();
        }
        ImGui::Dummy(ImVec2(0, scaled(2)));
        ui::InspectorRows rows(scaled(80), scaled(110));
        if (particleTab_ == 0) {
            ui::Combo("Motion", &L.kind, kKinds, 6);
            ui::RowHelp("Fire .. Rain are the built-in motions; Custom uses the\n"
                        "speed / spread / gravity knobs below.");
            ui::SliderInt("Count", &L.count, 1, 256);
            ui::SliderFloat("Size", &L.size, 0.02f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ui::ColorHex("Tint", L.color);
            if (L.kind == 2 || L.kind == 5) ui::SliderFloat("Opacity", &L.opacity, 0.0f, 1.0f, "%.2f");
            if (L.kind == 5) {
                ui::DragFloat("Speed", &L.speed, 0.05f, 0.0f, 50.0f, "%.2f u/s");
                ui::DragFloat("Spread", &L.spread, 0.5f, 0.0f, 90.0f, "%.0f deg");
                ui::DragFloat("Gravity", &L.gravity, 0.1f, -30.0f, 50.0f, "%.1f u/s2");
                ui::DragFloat("Weight", &L.weight, 0.02f, 0.05f, 10.0f, "%.2f");
                ui::DragFloat("Lifetime", &L.life, 0.05f, 0.1f, 10.0f, "%.2f s");
                ui::DragFloat("Grow", &L.grow, 0.02f, 0.1f, 4.0f, "%.2f x");
                ui::Switch("Die on terrain", &L.dieOnGround);
            }
            ImGui::Dummy(ImVec2(0, scaled(4)));
            ui::Switch("Additive (Glows - overlapping layers saturate toward yellow)",
                       &L.additive);
        } else if (particleTab_ == 1) {
            ParticleTexGen& g = L.texGen;
            const ParticleTexGen before = g;
            ui::Combo("Generate", &g.kind, kTexKinds, 4);
            bool recipeCommitted = g.kind != before.kind;
            if (g.kind == 0) {
                SceneObject tmp;
                tmp.type = PrimitiveType::Emitter;
                tmp.materialPath = L.materialPath;
                if (drawMaterialCombo(tmp)) L.materialPath = tmp.materialPath;
            } else {
                int sizeIdx = g.size == 32 ? 0 : (g.size == 128 ? 2 : 1);
                const char* sizes[] = {"32 x 32", "64 x 64", "128 x 128"};
                if (ui::Combo("Resolution", &sizeIdx, sizes, 3)) {
                    g.size = sizeIdx == 0 ? 32 : (sizeIdx == 2 ? 128 : 64);
                    recipeCommitted = true;
                }
                ui::RowHelp("GS VRAM per frame: 4 / 16 / 64 KB (full colour -\n"
                            "a soft alpha ramp does not survive a palette).");
                int frameIdx = g.frames >= 8 ? 3 : (g.frames >= 4 ? 2 : (g.frames >= 2 ? 1 : 0));
                const char* frameNames[] = {"1 (still)", "2", "4", "8"};
                if (ui::Combo("Frames", &frameIdx, frameNames, 4)) {
                    g.frames = 1 << frameIdx;
                    recipeCommitted = true;
                }
                ui::RowHelp("A flipbook: the noise moves through a seamless loop and the\n"
                            "console swaps the frames - one draw, only the texture changes.");
                if (g.frames > 1) {
                    ui::SliderFloat("Frames / s", &g.fps, 1.0f, 30.0f, "%.0f");
                    recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                }
                auto knob = [&](const char* label, float* v, float lo, float hi, const char* tip) {
                    ui::SliderFloat(label, v, lo, hi, "%.2f");
                    recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                    ui::RowHelp(tip);
                };
                ui::Caption("Bake inputs");
                ui::DragInt("Seed", &g.seed, 1.0f, 0, 99999);
                recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                knob("Softness", &g.softness, 0.0f, 1.0f, "How far the edge fades before the quad's border.");
                knob("Detail", &g.detail, 0.0f, 1.0f, "Fine noise on top of the big shapes.");
                knob("Noise scale", &g.scale, 0.3f, 3.0f, "Size of the noise lobes: small = many little puffs.");
                if (g.kind == 2) knob("Turbulence", &g.turbulence, 0.0f, 1.0f, "How hard the flame's sides are licked sideways.");
                if (g.kind == 2 || g.kind == 3) knob("Heat", &g.heat, 0.0f, 1.0f, "How much of the shape is the white-hot core.");
                if (g.kind != 2) {
                    ui::ColorHex("Texture colour", g.color);
                    recipeCommitted |= ImGui::IsItemDeactivatedAfterEdit();
                }
                ImGui::Dummy(ImVec2(0, scaled(4)));
                if (ui::SecondaryButton("Regenerate")) recipeCommitted = true;
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", L.materialPath.empty() ? "(not written yet)" : L.materialPath.c_str());
            }
            // Re-bake on RELEASE, not per frame of a drag: it writes files.
            if (g.kind != 0 && recipeCommitted) particleBakeTexture(fx);
        } else {
            if (particleLayerSel_ == 0) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("The main layer IS the emitter: it sits where the emitter is and "
                                    "spawns over its scale. Pick another layer to move it.");
                ImGui::PopTextWrapPos();
            } else {
                char lb[64];
                std::snprintf(lb, sizeof lb, "%s", L.label.c_str());
                if (ui::InputText("Layer name", lb, sizeof lb)) L.label = lb;
                ui::AxisDrag3("Offset", L.offset, 0.02f, -20.0f, 20.0f, "%.2f");
                ui::RowHelp("Where this layer sits relative to the emitter (world axes).");
                ui::AxisDrag3("Area", L.area, 0.02f, 0.0f, 10.0f, "%.2f x");
                ui::RowHelp("Spawn area as a multiple of the emitter's scale.");
                ImGui::Dummy(ImVec2(0, scaled(6)));
                if (ui::DangerButton("Remove layer")) removeLayer = true;
            }
        }
    }
    ImGui::EndChild();

    // --- footer: Duplicate, Place in scene -------------------------------------
    ImGui::Separator();
    {
        const float pw = ImGui::CalcTextSize("Place in scene").x + ImGui::GetStyle().FramePadding.x * 4.0f;
        if (ui::GhostButton("Duplicate")) duplicate(particleSel_);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - pw);
        if (ui::PrimaryButton("Place in scene", pw)) {
            const std::string name = lib[(size_t)particleSel_].name;
            addObject(PrimitiveType::Emitter, false);
            if (!project_.objects().empty()) project_.objects().back().particleEffect = name;
            sceneEdit = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Adds an emitter linked to this effect where objects are inserted.");
    }
    ImGui::EndChild();

    if (removeLayer && particleLayerSel_ > 0 && particleLayerSel_ <= (int)lib[(size_t)particleSel_].layers.size()) {
        ParticleEffect& e = lib[(size_t)particleSel_];
        e.layers.erase(e.layers.begin() + (particleLayerSel_ - 1));
        particleLayerSel_ = 0;
        particleHidden_.clear();
        sceneEdit = true;
    }
    if (deleteAt >= 0 && deleteAt < (int)lib.size()) {
        // Linked emitters keep the look they last copied; vehicles fall back
        // to the built-in smoke. The texture files stay - they are assets.
        project::renameParticleEffectRefs(project_, lib[(size_t)deleteAt].name, "");
        lib.erase(lib.begin() + deleteAt);
        particleSel_ = std::min(particleSel_, (int)lib.size() - 1);
        particleLayerSel_ = 0;
        particleHidden_.clear();
        sceneEdit = true;
    }

    if (sceneEdit ||
        (!ImGui::IsAnyItemActive() &&
         project::sectionJson(project_, project::Section::Particles) != committedSection)) {
        commitChange();
        committedSection = project::sectionJson(project_, project::Section::Particles);
    }
    ImGui::End();
}
