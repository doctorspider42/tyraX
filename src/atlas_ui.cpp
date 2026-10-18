// -------------------------------------------------------------------------
// Tools > Texture Atlas (docs/texture-atlasing.md): what the build packed,
// what it refused and why, what it costs in GS VRAM - and the two decisions
// the author owns, per texture: keep it out of every page, or put it in a
// named group instead of letting the folder layout decide.
//
// It exists because the feature used to be one checkbox and one line in the
// boot log. Nothing said which textures shared a page - so nothing warned
// that a page had merged two rooms' props into one allocation that must then
// be resident whenever either room is - and nothing said when a texture was
// silently disqualified. On the shipped night-walk example the answer to
// "what did it pack?" was "nothing at all", and it took reading the packer to
// find out.
//
// Its own translation unit for the reason every other *_ui.cpp is one
// (app.cpp is the build's critical path). These are App:: members.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <imgui.h>

#include <stb_image.h>

#include "gl_loader.h"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "texatlas.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"

namespace fs = std::filesystem;

namespace {

// Every texture an object's material reaches, res-relative. Used to answer
// "which scenes does this page serve?" - the question a page cannot answer
// for itself, and the one that decides whether a page is a good idea: a page
// is ONE allocation, so members from two different scenes (or two streaming
// layers) hold each other resident.
void collectObjectTextures(const Project& p, const SceneObject& o,
                           std::set<std::string>& out) {
    const fs::path root(p.dir);
    auto add = [&](const std::string& definingRel, const std::string& tok) {
        if (tok.empty()) return;
        std::string t = tok;
        for (char& c : t)
            if (c == '\\') c = '/';
        const std::string rel = (fs::path(definingRel).parent_path() / t)
                                    .lexically_normal()
                                    .generic_string();
        if (rel.rfind("res/", 0) == 0) out.insert(rel);
    };
    if (!o.materialPath.empty()) {
        std::vector<objparser::MtlMaterial> mats;
        if (objparser::loadMtl((root / o.materialPath).string(), mats))
            for (const objparser::MtlMaterial& m : mats)
                add(o.materialPath, m.texture);
    }
    if (!o.modelPath.empty() && fs::path(o.modelPath).extension() == ".obj") {
        objparser::Model m;
        if (objparser::load((root / o.modelPath).string(), m,
                            o.materialPath.empty()
                                ? ""
                                : (root / o.materialPath).string()))
            for (const objparser::Submesh& s : m.submeshes)
                add(o.materialPath.empty() ? o.modelPath : o.materialPath,
                    s.texture);
    }
}

ImU32 col32(const ImVec4& c, float a = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * a));
}

// A dashed rectangle: the free slots of a page (ImDrawList has no dash).
void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash) {
    auto line = [&](ImVec2 p, ImVec2 q) {
        const float len = std::max(std::fabs(q.x - p.x), std::fabs(q.y - p.y));
        const int n = std::max(1, (int)(len / (dash * 2.0f)));
        for (int i = 0; i < n; ++i) {
            const float t0 = (float)i / n, t1 = t0 + 0.5f / n;
            dl->AddLine(ImVec2(p.x + (q.x - p.x) * t0, p.y + (q.y - p.y) * t0),
                        ImVec2(p.x + (q.x - p.x) * t1, p.y + (q.y - p.y) * t1), col);
        }
    };
    line(a, ImVec2(b.x, a.y));
    line(ImVec2(b.x, a.y), b);
    line(b, ImVec2(a.x, b.y));
    line(ImVec2(a.x, b.y), a);
}

// The free slots still open on one page, in the size of its most common
// member (gutter included): the plan only records what it placed, so the
// answer to "how many more fit before a second page" is read off an
// occupancy grid scanned on that slot size - the shelf packer lays equal
// members out on exactly that grid.
std::vector<std::array<int, 4>> freeSlotsOf(const texatlas::Plan& plan, int page) {
    constexpr int G = 2;  // texatlas' gutter
    const int S = plan.pageSize;
    std::map<std::pair<int, int>, int> sizes;
    for (const texatlas::Entry& e : plan.entries)
        if (e.page == page) ++sizes[{e.w + 2 * G, e.h + 2 * G}];
    if (sizes.empty() || S <= 0) return {};
    std::pair<int, int> slot = sizes.begin()->first;
    for (const auto& [sz, n] : sizes)
        if (n > sizes[slot]) slot = sz;
    std::vector<char> used((size_t)S * S, 0);
    for (const texatlas::Entry& e : plan.entries) {
        if (e.page != page) continue;
        for (int y = std::max(0, e.y - G); y < std::min(S, e.y + e.h + G); ++y)
            for (int x = std::max(0, e.x - G); x < std::min(S, e.x + e.w + G); ++x)
                used[(size_t)y * S + x] = 1;
    }
    std::vector<std::array<int, 4>> out;
    const int cw = slot.first, ch = slot.second;
    for (int y = 0; y + ch <= S; y += ch)
        for (int x = 0; x + cw <= S; x += cw) {
            bool free = true;
            for (int yy = y; yy < y + ch && free; ++yy)
                for (int xx = x; xx < x + cw; ++xx)
                    if (used[(size_t)yy * S + xx]) {
                        free = false;
                        break;
                    }
            if (free) out.push_back({x, y, cw, ch});
        }
    return out;
}

}  // namespace

// The page as the PLAN describes it, composited here from the member PNGs -
// NOT the file the last build wrote. That distinction is the whole point: the
// baked page lags every edit, so a re-grouped project showed page 0 with its
// previous thirty members while its list underneath had two, and a page the
// plan had just invented showed nothing at all. A preview that contradicts the
// list beside it is worse than no preview.
void App::rebuildAtlasPreviews() {
    for (auto& [page, tex] : atlasPagePreview_)
        if (tex.tex) {
            const GLuint id = tex.tex;
            glDeleteTextures(1, &id);
        }
    atlasPagePreview_.clear();
    const int S = atlasPlan_.pageSize;
    if (S <= 0) return;
    for (size_t pi = 0; pi < atlasPlan_.pages.size(); ++pi) {
        // Mid grey rather than black: a page is mostly empty at these sizes
        // and black reads as "nothing was packed".
        std::vector<unsigned char> page((size_t)S * S * 4, 60);
        for (size_t i = 3; i < page.size(); i += 4) page[i] = 255;
        bool any = false;
        for (const texatlas::Entry& e : atlasPlan_.entries) {
            if (e.page != (int)pi) continue;
            int w = 0, h = 0, comp = 0;
            const std::string full =
                (fs::path(project_.dir) / e.resRel).string();
            unsigned char* px = stbi_load(full.c_str(), &w, &h, &comp, 4);
            if (!px) continue;
            // The bake resizes a member to its power-of-two size; the plan
            // already carries that size, so the preview uses the same one.
            std::vector<unsigned char> fit;
            const unsigned char* src = px;
            if (w != e.w || h != e.h) {
                fit = pngquant::resizeRGBA(px, w, h, e.w, e.h);
                src = fit.data();
            }
            for (int y = 0; y < e.h; ++y) {
                const int dy = e.y + y;
                if (dy < 0 || dy >= S) continue;
                for (int x = 0; x < e.w; ++x) {
                    const int dx = e.x + x;
                    if (dx < 0 || dx >= S) continue;
                    std::memcpy(&page[((size_t)dy * S + dx) * 4],
                                &src[((size_t)y * e.w + x) * 4], 4);
                }
            }
            stbi_image_free(px);
            any = true;
        }
        if (!any) continue;
        HudTexture entry;
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUploadTexRgba(S, S, page.data());
        entry = {tex, S, S};
        atlasPagePreview_[(int)pi] = entry;
    }
}

// The window answers three questions in the order an author asks them: is it
// on and did it pay (header chip + KPI strip), what landed where (the page as
// the hero, members as slots you can hover and click), and what was refused
// and why (under the page list, never behind a tab). The two decisions that
// are the author's - keep a texture out, group it - are made for ONE texture
// at a time in the inspector, instead of as a control row repeated for every
// member (thirty rows of 99 widgets that ran past the window's right edge).
void App::drawTextureAtlasWindow() {
    if (!showTextureAtlas_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1100), scaled(720)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Texture Atlas", &showTextureAtlas_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const bool on = project_.settings.textureAtlas;

    // Straight to the tab the switch is on, and in FRONT of this window -
    // which is where the click came from and would otherwise cover it.
    auto openRenderingPrefs = [&]() {
        showProjectPrefs_ = true;
        prefsFocusTab_ = "Rendering";
        prefsScrollToItem_ = "Texture atlasing";
        pendingFocusWindow_ = "Project Preferences";
        // Floating, this window would cover the Preferences tab: they open
        // over it instead (prefsOverFloating_).
        if (!ImGui::IsWindowDocked()) {
            prefsOverFloating_ = true;
            prefsOverPos_ = ImGui::GetWindowPos();
            prefsOverSize_ = ImGui::GetWindowSize();
        }
    };

    // --- header: state + the two actions ------------------------------------
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(on ? "the plan the next build bakes" : "off for this project");
        const char* chip = on ? "Atlasing on" : "Atlasing off";
        const float chipW = ImGui::CalcTextSize(chip).x * 0.82f + fs;
        const float btnPad = ImGui::GetStyle().FramePadding.x * 2.6f;
        const float prefsW = ImGui::CalcTextSize("Rendering prefs").x + btnPad;
        const float recW = ImGui::CalcTextSize("Recompute").x + btnPad;
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                                 right - chipW - prefsW - (on ? recW + sp : 0.0f) - sp * 2.0f));
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f -
                                   ImGui::GetStyle().ItemSpacing.y));
        ui::Badge(chip, on ? 0 : 4);
        ImGui::EndGroup();
        ImGui::SameLine();
        if (ui::GhostButton("Rendering prefs")) openRenderingPrefs();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Project Preferences > Graphics > Advanced > Texture atlasing.");
        if (on) {
            ImGui::SameLine();
            if (ImGui::Button("Recompute")) atlasPlanDirty_ = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Re-read the textures from disk and plan again.");
        }
    }

    if (!on) {
        if (ui::EmptyState(ui::Glyph::Layers, "Texture atlasing is off",
                           "It packs small model textures into shared 256x256 pages: "
                           "one GS allocation per page instead of one per texture, and "
                           "fewer texture switches per frame.",
                           "Turn it on in Rendering prefs"))
            openRenderingPrefs();
        ImGui::End();
        return;
    }

    // The plan is a pure function of the project + the files on disk, and it
    // reads every candidate image, so it is computed on demand rather than
    // per frame. Anything that can change it - a rebuild, a control below -
    // sets atlasPlanDirty_.
    if (atlasPlanDirty_) {
        atlasPlan_ = texatlas::plan(project_);
        atlasVram_ = texatlas::vram(atlasPlan_, project_);
        rebuildAtlasPreviews();
        atlasFreeSlots_.clear();
        for (size_t pi = 0; pi < atlasPlan_.pages.size(); ++pi)
            atlasFreeSlots_.push_back(freeSlotsOf(atlasPlan_, (int)pi));
        atlasPlanDirty_ = false;
    }
    const texatlas::Plan& plan = atlasPlan_;
    if (atlasPageSel_ >= (int)plan.pages.size()) atlasPageSel_ = 0;
    auto membersOf = [&](int page) {
        std::vector<const texatlas::Entry*> out;
        for (const texatlas::Entry& e : plan.entries)
            if (e.page == page) out.push_back(&e);
        return out;
    };
    auto fileOf = [](const std::string& rel) { return fs::path(rel).filename().string(); };
    auto previewOf = [&](int page) -> ImTextureID {
        auto it = atlasPagePreview_.find(page);
        return it == atlasPagePreview_.end() ? ImTextureID{}
                                             : (ImTextureID)(intptr_t)it->second.tex;
    };
    const texatlas::Excluded* selExcluded = nullptr;
    for (const texatlas::Excluded& x : plan.excluded)
        if (x.resRel == atlasSel_) selExcluded = &x;
    const texatlas::Entry* selEntry = plan.find(atlasSel_);
    if (selEntry) atlasPageSel_ = selEntry->page;
    if (!selEntry && !selExcluded) {
        // Nothing (or something gone) selected: the page's first member.
        const auto ms = membersOf(atlasPageSel_);
        atlasSel_ = ms.empty() ? std::string() : ms.front()->resRel;
        selEntry = ms.empty() ? nullptr : ms.front();
    }

    // --- KPI strip: the trade-off in three numbers ---------------------------
    {
        const float h = fs * 4.4f;
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float avail = ImGui::GetContentRegionAvail().x;
        const float w0 = (avail - sp * 2.0f) * 0.48f, w1 = (avail - sp * 2.0f) * 0.26f;
        ImFont* bold = ui::boldFont() ? ui::boldFont() : ImGui::GetFont();
        const float bigFs = fs * 1.55f;
        auto caption = [&](ImDrawList* dl, ImVec2 p, const char* t, ImU32 c) {
            std::string up = t;
            for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
            dl->AddText(bold, fs * 0.78f, p, c, up.c_str());
        };
        auto bigW = [&](const char* t) { return bold->CalcTextSizeA(bigFs, FLT_MAX, 0.0f, t).x; };
        const ImVec2 pad(fs * 0.9f, fs * 0.7f);
        const int saved = atlasVram_.savedKb;
        const bool pays = saved >= 0;
        // GS VRAM: before -> after, the difference as the big number
        if (ui::BeginCard("##atlas_kpi_vram", ImVec2(w0, h))) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetWindowPos(), b(a.x + w0, a.y + h);
            const float rr = ImGui::GetStyle().ChildRounding;
            if (!plan.empty()) {
                const ImVec4& tone = pays ? sem.ok : sem.warn;
                dl->AddRectFilled(a, b, col32(tone, 0.07f), rr);
                dl->AddRect(a, b, col32(tone, 0.35f), rr);
                caption(dl, ImVec2(a.x + pad.x, a.y + pad.y), "GS VRAM", col32(tone));
                char before[32], after[32], delta[32];
                std::snprintf(before, sizeof before, "%d KB", atlasVram_.membersKb);
                std::snprintf(after, sizeof after, "%d KB", atlasVram_.pagesKb);
                std::snprintf(delta, sizeof delta, "%s%d KB", pays ? "-" : "+", std::abs(saved));
                const float y = a.y + pad.y + fs * 1.5f;
                const ImVec2 bs = ImGui::CalcTextSize(before);
                const float ty = y + (bigFs - fs) * 0.6f;
                dl->AddText(ImVec2(a.x + pad.x, ty), col32(sem.textDim), before);
                dl->AddLine(ImVec2(a.x + pad.x, ty + bs.y * 0.55f),
                            ImVec2(a.x + pad.x + bs.x, ty + bs.y * 0.55f), col32(sem.textDim));
                float x = a.x + pad.x + bs.x + fs * 0.6f;
                dl->AddText(ImVec2(x, ty), col32(sem.textDim), ">");
                x += fs * 1.1f;
                dl->AddText(bold, bigFs, ImVec2(x, y), col32(sem.text), after);
                dl->AddText(bold, bigFs, ImVec2(b.x - pad.x - bigW(delta), a.y + pad.y), col32(tone),
                            delta);
                const char* sub = pays ? "predicted saving" : "predicted cost";
                dl->AddText(ImVec2(b.x - pad.x - ImGui::CalcTextSize(sub).x, a.y + pad.y + bigFs + fs * 0.4f),
                            col32(sem.textDim), sub);
            } else {
                caption(dl, ImVec2(a.x + pad.x, a.y + pad.y), "GS VRAM", col32(sem.textDim));
                dl->AddText(ImVec2(a.x + pad.x, a.y + pad.y + fs * 1.6f), col32(sem.textDim),
                            "Nothing qualified for a page - no change.");
            }
            ImGui::Dummy(ImVec2(1, 1));
            if (ImGui::IsWindowHovered() && !pays && !plan.empty())
                ImGui::SetTooltip(
                    "A page is a full 256x256 allocation whatever it holds, so it\n"
                    "only pays once enough textures share it: about eight 64x64\n"
                    "members at 4 bits per pixel, about sixteen at 8. Below that\n"
                    "atlasing still buys fewer allocations and texture switches.");
        }
        ui::EndCard();
        ImGui::SameLine();
        auto smallCard = [&](const char* id, const char* cap, const std::string& big,
                             const std::string& small, ImU32 bigCol) {
            if (ui::BeginCard(id, ImVec2(w1, h))) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 a = ImGui::GetWindowPos();
                caption(dl, ImVec2(a.x + pad.x, a.y + pad.y), cap, col32(sem.textDim));
                const float y = a.y + pad.y + fs * 1.5f;
                dl->AddText(bold, bigFs, ImVec2(a.x + pad.x, y), bigCol, big.c_str());
                dl->AddText(ImVec2(a.x + pad.x + bigW(big.c_str()) + fs * 0.5f, y + (bigFs - fs) * 0.6f),
                            col32(sem.textDim), small.c_str());
                ImGui::Dummy(ImVec2(1, 1));
            }
            ui::EndCard();
        };
        smallCard("##atlas_kpi_alloc", "Allocations",
                  std::to_string(plan.entries.size()) + " > " + std::to_string(plan.pages.size()),
                  plan.pages.size() == 1 ? "page" : "pages", col32(sem.text));
        ImGui::SameLine();
        smallCard("##atlas_kpi_refused", "Refused", std::to_string(plan.excluded.size()),
                  plan.excluded.empty() ? "every texture qualified" : "listed under the pages",
                  col32(plan.excluded.empty() ? sem.text : sem.warn));
    }

    // Which scene each texture is used by - the question a page cannot answer
    // for itself: a page is ONE allocation, so members from two scenes keep
    // each other resident.
    std::map<std::string, std::set<std::string>> scenesOfTexture;
    for (const SceneData& sc : project_.scenes)
        for (const SceneObject& o : sc.objects) {
            std::set<std::string> texs;
            collectObjectTextures(project_, o, texs);
            for (const std::string& t : texs) scenesOfTexture[t].insert(sc.name);
        }

    // --- three columns -------------------------------------------------------
    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float leftW = scaled(236), rightW = scaled(330);
    const float spx = ImGui::GetStyle().ItemSpacing.x;
    const float centerW =
        std::max(scaled(200), ImGui::GetContentRegionAvail().x - leftW - rightW - spx * 2.0f);
    std::string hoverNow;  // the member row under the cursor this frame
    auto rowBg = [&](ImVec2 p, float w, float h, bool sel) {
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ImGui::GetStyle().FrameRounding;
        if (sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col32(sem.accent, 0.14f), r);
        else if (t > 0.0f) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col32(sem.field, t), r);
    };

    // Left: pages, then what was refused.
    if (ui::BeginCard("##atlas_pages", ImVec2(leftW, bodyH))) {
        ui::Caption("Pages", std::to_string(plan.pages.size()).c_str());
        if (plan.pages.empty()) ImGui::TextDisabled("No pages - see below why.");
        for (size_t pi = 0; pi < plan.pages.size(); ++pi) {
            const auto ms = membersOf((int)pi);
            const int freeN = pi < atlasFreeSlots_.size() ? (int)atlasFreeSlots_[pi].size() : 0;
            const int cap = (int)ms.size() + freeN;
            const std::string stem = fs::path(plan.pages[pi]).stem().string();
            const std::string label = stem + "##pg" + std::to_string(pi);
            const float rh = fs * 3.4f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, rh))) {
                atlasPageSel_ = (int)pi;
                atlasSel_ = ms.empty() ? std::string() : ms.front()->resRel;
            }
            const bool selPage = atlasPageSel_ == (int)pi;
            uiscript::markLastItemChecked(selPage);
            rowBg(p, w, rh, selPage);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                const std::string& grp = plan.groupOf((int)pi);
                const bool named = grp.size() > 1 && grp[0] == '@';
                ImGui::SetTooltip("%s%s%s", plan.pages[pi].c_str(), named ? "\ngroup " : "",
                                  named ? grp.substr(1).c_str() : "");
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding;
            const float th = rh - fs * 0.8f;
            const ImVec2 ta(p.x + fs * 0.4f, p.y + fs * 0.4f);
            if (ImTextureID tex = previewOf((int)pi))
                dl->AddImageRounded(tex, ta, ImVec2(ta.x + th, ta.y + th), ImVec2(0, 0),
                                    ImVec2(1, 1), IM_COL32_WHITE, r);
            else
                dl->AddRectFilled(ta, ImVec2(ta.x + th, ta.y + th), col32(sem.field), r);
            const float tx = ta.x + th + fs * 0.6f;
            {
                ui::FontScope b(ui::boldFont());
                dl->AddText(ImVec2(tx, p.y + fs * 0.35f), col32(sem.text), stem.c_str());
            }
            char sub[64];
            std::snprintf(sub, sizeof sub, "%d-bit - %d member%s", plan.bitsOf((int)pi),
                          (int)ms.size(), ms.size() == 1 ? "" : "s");
            ImFont* mono = ui::monoFont() ? ui::monoFont() : ImGui::GetFont();
            dl->AddText(mono, fs * 0.85f, ImVec2(tx, p.y + fs * 1.35f), col32(sem.textDim), sub);
            // capacity meter: members against what one page holds
            char capTxt[32];
            std::snprintf(capTxt, sizeof capTxt, "%d/%d", (int)ms.size(), cap);
            const float cw = mono->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0.0f, capTxt).x;
            const float my = p.y + fs * 2.6f, mx1 = p.x + w - fs * 0.5f - cw - fs * 0.4f;
            dl->AddRectFilled(ImVec2(tx, my), ImVec2(mx1, my + fs * 0.22f), col32(sem.field),
                              fs * 0.11f);
            if (cap > 0)
                dl->AddRectFilled(ImVec2(tx, my),
                                  ImVec2(tx + (mx1 - tx) * (float)ms.size() / (float)cap,
                                         my + fs * 0.22f),
                                  col32(sem.ok), fs * 0.11f);
            dl->AddText(mono, fs * 0.85f, ImVec2(mx1 + fs * 0.4f, my - fs * 0.38f),
                        col32(sem.textDim), capTxt);
        }
        ImGui::Spacing();
        ImGui::Spacing();
        ui::Caption("Not atlased", std::to_string(plan.excluded.size()).c_str());
        if (plan.excluded.empty()) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            const char* why =
                "Nothing was refused. A texture the packer skips shows here with the "
                "reason - too big, tiled, kept out, used by terrain or particles.";
            const float wrap = w - fs * 1.4f;
            const ImVec2 ts = ImGui::CalcTextSize(why, nullptr, false, wrap);
            dashedRect(ImGui::GetWindowDrawList(), p, ImVec2(p.x + w, p.y + ts.y + fs * 1.4f),
                       col32(sem.textDim, 0.5f), fs * 0.25f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.7f, p.y + fs * 0.7f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            ImGui::TextDisabled("%s", why);
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ts.y + fs * 1.6f));
            ImGui::Dummy(ImVec2(1, 1));
        }
        for (const texatlas::Excluded& x : plan.excluded) {
            const std::string label = fileOf(x.resRel) + "##x" + x.resRel;
            const float rh = fs * 2.5f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, rh))) atlasSel_ = x.resRel;
            const bool sel = atlasSel_ == x.resRel;
            uiscript::markLastItemChecked(sel);
            rowBg(p, w, rh, sel);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s\n%s", x.resRel.c_str(), x.reason.c_str());
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->PushClipRect(p, ImVec2(p.x + w, p.y + rh), true);
            dl->AddText(ImVec2(p.x + fs * 0.5f, p.y + fs * 0.25f), col32(sem.text),
                        fileOf(x.resRel).c_str());
            dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(p.x + fs * 0.5f, p.y + fs * 1.3f),
                        col32(sem.warn), x.reason.c_str());
            dl->PopClipRect();
        }
    }
    ui::EndCard();
    ImGui::SameLine();

    // Centre: the page as the hero.
    if (ui::BeginCard("##atlas_page", ImVec2(centerW, bodyH))) {
        if (plan.pages.empty()) {
            ui::EmptyState(ui::Glyph::Layers, "No pages",
                           plan.excluded.empty()
                               ? "No model or primitive texture is small enough and used "
                                 "with plain 0..1 UVs - nothing to pack."
                               : "Every candidate was refused - the list on the left says "
                                 "why for each one.");
        } else {
            const int pi = atlasPageSel_;
            const auto ms = membersOf(pi);
            const std::string& grp = plan.groupOf(pi);
            {
                ImGui::AlignTextToFramePadding();
                {
                    ui::FontScope b(ui::boldFont());
                    ImGui::TextUnformatted(fileOf(plan.pages[pi]).c_str());
                }
                ImGui::SameLine();
                const bool named = grp.size() > 1 && grp[0] == '@';
                char meta[256];
                std::snprintf(meta, sizeof meta, "%s/ - %dx%d - %d-bit - one palette%s%s",
                              fs::path(plan.pages[pi]).parent_path().generic_string().c_str(),
                              plan.pageSize, plan.pageSize, plan.bitsOf(pi),
                              named ? " - group " : "", named ? grp.substr(1).c_str() : "");
                {
                    ui::FontScope m(ui::monoFont());
                    ImGui::TextDisabled("%s", meta);
                }
                const char* views[] = {"Baked", "Slots"};
                const float segW = ImGui::CalcTextSize("BakedSlots").x +
                                   ImGui::GetStyle().FramePadding.x * 4.0f + 6.0f;
                ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                                         ImGui::GetWindowContentRegionMax().x - segW));
                ui::Segmented("##atlas_view", views, 2, &atlasView_);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Baked: the page composited from the plan's members -\n"
                                      "what the next build writes. Slots: where each sits.");
            }
            // Cross-scene warning: the "different parish" problem.
            std::set<std::string> pageScenes;
            for (const texatlas::Entry* e : ms)
                for (const std::string& s : scenesOfTexture[e->resRel]) pageScenes.insert(s);
            if (pageScenes.size() > 1) {
                std::string names;
                for (const std::string& s : pageScenes) names += (names.empty() ? "" : ", ") + s;
                ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
                ui::StatusLine(1, ("Used by " + std::to_string(pageScenes.size()) +
                                   " scenes: " + names + " - one allocation keeps them all resident")
                                      .c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip(
                        "This page is one VRAM allocation, so every scene listed\n"
                        "here keeps all of its members resident. Give the members\n"
                        "of each scene their own Group if that matters.");
            }

            // The canvas: the page at the largest size that fits.
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            const float D = std::max(scaled(64), std::min(avail.x, avail.y) - fs * 0.2f);
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const ImVec2 a(cp.x + (avail.x - D) * 0.5f, cp.y);
            const ImVec2 b(a.x + D, a.y + D);
            ImGui::SetCursorScreenPos(a);
            ImGui::InvisibleButton("Atlas page canvas", ImVec2(D, D));
            const bool canvasHovered = ImGui::IsItemHovered();
            const bool canvasClicked = ImGui::IsItemClicked();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, b, col32(sem.field), ImGui::GetStyle().FrameRounding);
            const float k = D / (float)plan.pageSize;
            if (atlasView_ == 0)
                if (ImTextureID tex = previewOf(pi)) dl->AddImage(tex, a, b);
            if (pi < (int)atlasFreeSlots_.size())
                for (const auto& s : atlasFreeSlots_[pi])
                    dashedRect(dl, ImVec2(a.x + (s[0] + 2) * k, a.y + (s[1] + 2) * k),
                               ImVec2(a.x + (s[0] + s[2] - 2) * k, a.y + (s[1] + s[3] - 2) * k),
                               col32(sem.textDim, 0.45f), fs * 0.2f);
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            for (size_t i = 0; i < ms.size(); ++i) {
                const texatlas::Entry& e = *ms[i];
                const ImVec2 ra(a.x + e.x * k, a.y + e.y * k);
                const ImVec2 rb(a.x + (e.x + e.w) * k, a.y + (e.y + e.h) * k);
                const bool over = canvasHovered && mouse.x >= ra.x && mouse.x < rb.x &&
                                  mouse.y >= ra.y && mouse.y < rb.y;
                if (atlasView_ == 1) {
                    dl->AddRectFilled(ra, rb, col32(sem.surface));
                    dl->AddRect(ra, rb, col32(sem.textDim, 0.5f));
                    char idx[8];
                    std::snprintf(idx, sizeof idx, "%02d", (int)i);
                    ui::FontScope m(ui::monoFont());
                    const ImVec2 ts = ImGui::CalcTextSize(idx);
                    if (ts.x < rb.x - ra.x)
                        dl->AddText(ImVec2((ra.x + rb.x - ts.x) * 0.5f, (ra.y + rb.y - ts.y) * 0.5f),
                                    col32(sem.textDim), idx);
                }
                if (over || e.resRel == atlasHover_)
                    dl->AddRectFilled(ra, rb, col32(sem.accent, 0.25f));
                if (e.resRel == atlasSel_)
                    dl->AddRect(ImVec2(ra.x - 2, ra.y - 2), ImVec2(rb.x + 2, rb.y + 2),
                                col32(sem.accent), 3.0f, 0, 2.0f);
                if (over) {
                    ImGui::SetTooltip("%s\n%dx%d at %d,%d", e.resRel.c_str(), e.w, e.h, e.x, e.y);
                    if (canvasClicked) atlasSel_ = e.resRel;
                }
            }
            // How many more fit: the capacity half of the trade-off.
            if (pi < (int)atlasFreeSlots_.size()) {
                const auto& fr = atlasFreeSlots_[pi];
                char note[96];
                if (fr.empty())
                    std::snprintf(note, sizeof note, "Page full - the next member opens a second page");
                else
                    std::snprintf(note, sizeof note, "%d free %dx%d slot%s before a second page",
                                  (int)fr.size(), fr[0][2] - 4, fr[0][3] - 4,
                                  fr.size() == 1 ? "" : "s");
                const ImVec2 ts = ImGui::CalcTextSize(note);
                const ImVec2 pb(b.x - fs * 0.6f, b.y - fs * 0.6f);
                const ImVec2 pa(pb.x - ts.x - fs * 1.2f, pb.y - ts.y - fs * 0.7f);
                if (pa.x > a.x) {
                    ui::OverlayPanel(dl, pa, pb);
                    dl->AddText(ImVec2(pa.x + fs * 0.6f, pa.y + fs * 0.35f), col32(sem.text, 0.8f),
                                note);
                }
            }
        }
    }
    ui::EndCard();
    ImGui::SameLine();

    // Right: the ONE texture the decisions are about, then the member list.
    if (ui::BeginCard("##atlas_member", ImVec2(0, bodyH))) {
        const std::string rel = atlasSel_;
        if (rel.empty()) {
            ui::EmptyState(ui::Glyph::Box, "Nothing selected",
                           "Pick a slot on the page or a refused texture.");
        } else {
            // identity: swatch + name + where it sits
            const float sw = fs * 2.8f;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float r = ImGui::GetStyle().FrameRounding;
            ImTextureID tex = selEntry ? previewOf(selEntry->page) : ImTextureID{};
            if (tex && selEntry) {
                const float S = (float)plan.pageSize;
                dl->AddImageRounded(tex, p, ImVec2(p.x + sw, p.y + sw),
                                    ImVec2(selEntry->x / S, selEntry->y / S),
                                    ImVec2((selEntry->x + selEntry->w) / S,
                                           (selEntry->y + selEntry->h) / S),
                                    IM_COL32_WHITE, r);
            } else {
                dl->AddRectFilled(p, ImVec2(p.x + sw, p.y + sw), col32(sem.field), r);
                ui::DrawGlyph(dl, ui::Glyph::Box, ImVec2(p.x + sw * 0.5f, p.y + sw * 0.5f),
                              sw * 0.45f, col32(sem.textDim));
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x + sw + fs * 0.7f, p.y + fs * 0.15f));
            ImGui::BeginGroup();
            {
                ui::FontScope b(ui::boldFont());
                ImGui::TextUnformatted(fileOf(rel).c_str());
            }
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
            if (selEntry) {
                ui::FontScope m(ui::monoFont());
                ImGui::TextDisabled("%dx%d at %d,%d on %s", selEntry->w, selEntry->h,
                                    selEntry->x, selEntry->y,
                                    fs::path(selEntry->pageRel).stem().string().c_str());
            } else if (selExcluded) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
                ImGui::TextUnformatted(selExcluded->reason.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::SetCursorScreenPos(
                ImVec2(p.x, std::max(ImGui::GetCursorScreenPos().y, p.y + sw) + fs * 0.6f));
            ImGui::Separator();
            ImGui::Spacing();

            // The two decisions. Writing marks the project dirty and re-plans.
            Project::AtlasControl ctl;
            if (auto it = project_.atlasControl.find(rel); it != project_.atlasControl.end())
                ctl = it->second;
            bool changed = false;
            {
                ui::InspectorRows rows(scaled(84), scaled(104));
                bool keep = ctl.keepOut;
                if (ui::Switch("Keep out of pages (Gets its own allocation and palette again)",
                               &keep)) {
                    ctl.keepOut = keep;
                    changed = true;
                }
                char buf[64] = "";
                std::snprintf(buf, sizeof(buf), "%s", ctl.group.c_str());
                if (ui::InputText("Group", buf, sizeof(buf))) {
                    ctl.group = buf;
                    changed = true;
                }
                const bool groupHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip);
                // The default, said where it applies: an empty Group packs by
                // the .mtl's folder.
                if (!buf[0] && !ImGui::IsItemActive()) {
                    const std::string dir =
                        selEntry ? fs::path(selEntry->pageRel).parent_path().filename().string()
                                 : fs::path(rel).parent_path().filename().string();
                    const ImVec2 m = ImGui::GetItemRectMin();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(m.x + ImGui::GetStyle().FramePadding.x,
                               m.y + ImGui::GetStyle().FramePadding.y),
                        col32(sem.textDim, 0.7f), ("by folder: " + dir).c_str());
                }
                if (groupHovered)
                    ImGui::SetTooltip(
                        "Pack this texture with everything carrying the SAME group\n"
                        "name, instead of with its .mtl's folder. Group by what is on\n"
                        "screen together - a page shared by two rooms keeps both\n"
                        "resident.");
                // Page depth: a REQUEST for the whole group, highest wins.
                const float labelW = std::clamp(ImGui::GetContentRegionAvail().x / 3.0f,
                                                scaled(84), scaled(104));
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Page depth");
                ImGui::SameLine(labelW);
                const char* depths[] = {"Project", "4-bit", "8-bit", "Full"};
                int bitIdx = ctl.pageBits == 4    ? 1
                             : ctl.pageBits == 8  ? 2
                             : ctl.pageBits == 32 ? 3
                                                  : 0;
                if (ui::Segmented("##atlas_depth", depths, 4, &bitIdx, -1.0f)) {
                    ctl.pageBits = bitIdx == 1 ? 4 : bitIdx == 2 ? 8 : bitIdx == 3 ? 32 : 0;
                    changed = true;
                }
            }
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            if (selEntry)
                ImGui::Text("A page is quantized as one image: its depth is the highest any "
                            "member asks for. This page: %d-bit.",
                            plan.bitsOf(selEntry->page));
            else
                ImGui::TextUnformatted("A page is quantized as one image: its depth is the "
                                       "highest any member asks for.");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            if (changed) {
                if (ctl.keepOut || !ctl.group.empty() || ctl.pageBits != 0)
                    project_.atlasControl[rel] = ctl;
                else
                    project_.atlasControl.erase(rel);
                commitChange();
                atlasPlanDirty_ = true;
            }
        }

        // The members of the page on show, filterable, name + position.
        ImGui::Spacing();
        ImGui::Spacing();
        if (!plan.pages.empty()) {
            const auto ms = membersOf(atlasPageSel_);
            const float filterW = scaled(120);
            const float y0 = ImGui::GetCursorPosY();
            ui::Caption("Members", std::to_string(ms.size()).c_str());
            const float y1 = ImGui::GetCursorPosY();
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - filterW,
                                       y0 - fs * 0.3f));
            ImGui::SetNextItemWidth(filterW);
            ImGui::InputTextWithHint("##atlas_filter", "Filter", atlasFilter_, sizeof atlasFilter_);
            ImGui::SetCursorPosY(std::max(y1, ImGui::GetCursorPosY()));
            ImGui::BeginChild("##atlas_members", ImVec2(0, 0));
            std::string filt = atlasFilter_;
            for (char& c : filt) c = (char)std::tolower((unsigned char)c);
            for (const texatlas::Entry* e : ms) {
                const std::string name = fileOf(e->resRel);
                if (!filt.empty()) {
                    std::string ln = name;
                    for (char& c : ln) c = (char)std::tolower((unsigned char)c);
                    if (ln.find(filt) == std::string::npos) continue;
                }
                const float rh = ImGui::GetFrameHeight();
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                if (ImGui::InvisibleButton((name + "##m" + e->resRel).c_str(), ImVec2(w, rh)))
                    atlasSel_ = e->resRel;
                const bool sel = atlasSel_ == e->resRel;
                uiscript::markLastItemChecked(sel);
                if (ImGui::IsItemHovered()) hoverNow = e->resRel;
                rowBg(p, w, rh, sel);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float s = rh * 0.6f;
                const ImVec2 sa(p.x + fs * 0.4f, p.y + (rh - s) * 0.5f);
                if (ImTextureID tex = previewOf(e->page)) {
                    const float S = (float)plan.pageSize;
                    dl->AddImageRounded(tex, sa, ImVec2(sa.x + s, sa.y + s),
                                        ImVec2(e->x / S, e->y / S),
                                        ImVec2((e->x + e->w) / S, (e->y + e->h) / S),
                                        IM_COL32_WHITE, 2.0f);
                }
                ui::FontScope m(ui::monoFont());
                dl->AddText(ImVec2(sa.x + s + fs * 0.6f, p.y + (rh - fs) * 0.5f),
                            col32(sem.text, sel ? 1.0f : 0.8f), name.c_str());
                char pos[24];
                std::snprintf(pos, sizeof pos, "%d,%d", e->x, e->y);
                const float pw = ImGui::CalcTextSize(pos).x;
                dl->AddText(ImVec2(p.x + w - pw - fs * 0.5f, p.y + (rh - fs) * 0.5f),
                            col32(sem.textDim), pos);
            }
            ImGui::EndChild();
        }
    }
    ui::EndCard();
    atlasHover_ = hoverNow;
    ImGui::End();
}
