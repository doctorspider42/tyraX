// The editor's frame around the panels: the top bar (the logo menu, the
// project / scene breadcrumb, workspaces, search, run controls) and the status
// bar along the bottom (docs/editor-theme.md). The menus' contents stay in
// App::drawMainMenus (app.cpp); they open from the logo here.
//
// Layout of the top bar:
//
//   [TyraX v] | showcase / Aster [start] v  * 2 unsaved  [x Scene|o Logic|...]
//       [ Q Search objects, assets, commands...  Ctrl K ]
//                                  [Debug v] [PCSX2|PS2] [box] [> Run F5 v] [stop]
//
// The status bar carries what used to crowd the top bar: the build state and
// the LIVE / DBG / LOGIC / SESSION chips on the left, the scene's counts on the
// right. Every hit box keeps the id it had in the old toolbar (##tb_*,
// "Build status") so --ui-script scripts and docs that name them still work.
#include "app.hpp"
#include "app_internal.hpp"
#include "theme.hpp"
#include "uiscript.hpp"
#include "widgets.hpp"

#include "platform.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

ImU32 col(const ImVec4& c, float alpha = 1.0f) {
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alpha));
}

// A downward caret centred on (cx, cy), s = half width.
void caret(ImDrawList* dl, float cx, float cy, float s, ImU32 c) {
    dl->AddTriangleFilled(ImVec2(cx - s, cy - s * 0.45f), ImVec2(cx + s, cy - s * 0.45f),
                          ImVec2(cx, cy + s * 0.6f), c);
}

}  // namespace

void App::selectScene(int i) {
    if (i < 0 || i >= (int)project_.scenes.size() || project_.activeScene == i) return;
    project_.activeScene = i;
    clearSelection();
    cancelPastePlacement();  // staged copies belong to the old scene
    flowGraphObject_ = -1;
    flowPositionsApplied_ = false;
    applyProjectToViewport();  // terrain/lighting are per scene
}

// The top bar's first control: the mark, the wordmark and a caret, as ONE
// button ("TyraX menu") that opens the editor's menus. The ten menus are the
// popup's items, each an ordinary BeginMenu (App::drawMainMenus, app.cpp), so
// they are submenus here and nothing about their contents changed - only where
// they are reached from. That gives the bar back the width ten menu titles
// took. F10, or Alt tapped on its own, opens it from the keyboard.
void App::drawLogoMenu() {
    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float barH = ImGui::GetWindowHeight();
    const float h = ImGui::GetFrameHeight();
    const float y0 = IM_TRUNC((barH - h) * 0.5f);
    const float mark = scaled(26.0f);
    const float padX = scaled(6.0f);
    ImFont* face = ui::boldFont() ? ui::boldFont() : ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float tyraW = face->CalcTextSizeA(fs, FLT_MAX, 0.0f, "Tyra").x;
    const float xW = face->CalcTextSizeA(fs, FLT_MAX, 0.0f, "X").x;
    const float caretW = h * 0.36f;
    const float w = padX + mark + scaled(8.0f) + tyraW + xW + scaled(6.0f) + caretW + padX;
    const char* popupId = "##tyrax_menu";

    // Keyboard: F10 (unless the Live Debugger has it - pause/step) or a lone
    // Alt tap - pressed and released with no other key or button in between,
    // so Alt+drag in the viewport and Alt+letter never open it.
    bool open = logoMenuOpenRequest_;
    logoMenuOpenRequest_ = false;
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool debuggerOwnsF10 = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
        if (!debuggerOwnsF10 && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F10, false)) open = true;
        const bool altDown = ImGui::IsKeyDown(ImGuiKey_LeftAlt) || ImGui::IsKeyDown(ImGuiKey_RightAlt);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftAlt, false) || ImGui::IsKeyPressed(ImGuiKey_RightAlt, false)) {
            altTapArmed_ = !ImGui::IsAnyMouseDown() && !io.KeyCtrl && !io.KeyShift && !io.KeySuper;
            altTapStart_ = ImGui::GetTime();
        } else if (altTapArmed_) {
            bool other = ImGui::IsAnyMouseDown() || io.KeyCtrl || io.KeyShift || io.KeySuper;
            for (int k = ImGuiKey_NamedKey_BEGIN; !other && k < ImGuiKey_GamepadStart; ++k)
                if (k != ImGuiKey_LeftAlt && k != ImGuiKey_RightAlt && ImGui::IsKeyDown((ImGuiKey)k))
                    other = true;
            if (other) {
                altTapArmed_ = false;
            } else if (!altDown) {
                altTapArmed_ = false;
                if (ImGui::GetTime() - altTapStart_ < 0.6 && !io.WantTextInput) open = true;
            }
        }
    }

    ImGui::SetCursorPosY(y0);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("TyraX menu", ImVec2(w, h));
    // ImGui closes an open popup on the mouse-DOWN outside it, before this
    // button's release reports the click, so without remembering the state at
    // press time a second click would close and immediately reopen it.
    if (ImGui::IsItemActivated()) logoMenuOpenOnPress_ = logoMenuOpen_;
    if (pressed && !logoMenuOpenOnPress_) open = true;
    const bool hovered = ImGui::IsItemHovered();
    const float t = theme::hoverAnim(ImGui::GetItemID(), hovered, 12.0f);
    if (logoMenuOpen_ || t > 0.0f)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                          ImGui::GetColorU32(ImGuiCol_ButtonHovered, logoMenuOpen_ ? 1.0f : t), st.FrameRounding);
    // The "Tx" mark - the app icon itself (resources/icon.png).
    if (const unsigned int icon = appIconTexture())
        dl->AddImage((ImTextureID)(intptr_t)icon, ImVec2(p.x + padX, p.y + (h - mark) * 0.5f),
                     ImVec2(p.x + padX + mark, p.y + (h + mark) * 0.5f));
    float x = p.x + padX + mark + scaled(8.0f);
    const float ty = p.y + (h - fs) * 0.5f;
    dl->AddText(face, fs, ImVec2(x, ty), col(sem.text), "Tyra");
    x += tyraW;
    dl->AddText(face, fs, ImVec2(x, ty), col(sem.accent), "X");
    x += xW + scaled(6.0f);
    caret(dl, x + caretW * 0.5f, p.y + h * 0.5f, caretW * 0.32f, col(sem.textDim));
    if (hovered && !logoMenuOpen_)
        ImGui::SetTooltip("Menu: File, Edit, View, Layout, Scene, Project, Build, Session, Tools, Help (F10)");

    if (open && !ImGui::IsPopupOpen(popupId)) ImGui::OpenPopup(popupId);
    ImGui::SetNextWindowPos(ImVec2(p.x, p.y + h + scaled(4.0f)));
    ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(200.0f), 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    logoMenuOpen_ = ImGui::BeginPopup(popupId);
    if (logoMenuOpen_) {
        // Its items report under the bar's name, where the menus used to be:
        // '##MainMenuBar/Project' keeps naming the Project menu in scripts.
        uiscript::aliasCurrentWindow("##MainMenuBar");
        drawMainMenus();
        ImGui::EndPopup();
    }
}

// Drawn inside the main menu bar, after the logo menu. The bar is taller than one
// control (drawMenuBar pushes a larger FramePadding for it), so every control
// here is placed on a centred row at y0 rather than at the bar's top.
void App::drawToolbar() {
    if (!hasProject_) return;

    const theme::Semantics& sem = theme::semantics();
    const ImGuiStyle& st = ImGui::GetStyle();
    const bool busy = runner_.busy();
    const bool ps2Ready = !project_.ps2LinkIp.empty();
    const bool targetReady = !runOnPs2_ || ps2Ready;
    const bool debugProfile = project_.settings.buildProfile == "debug";
    const char* noIpTip = "Set 'PS2 (ps2link) IP' in Edit > Preferences first.";

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float barH = ImGui::GetWindowHeight();
    const float h = ImGui::GetFrameHeight();
    const float y0 = IM_TRUNC((barH - h) * 0.5f);
    const float round = st.FrameRounding;
    const float padX = st.FramePadding.x;
    const float textDy = (h - ImGui::GetTextLineHeight()) * 0.5f;
    const float gapS = scaled(6.0f);    // inside a control
    const float gap = scaled(6.0f);     // between controls of one group
    const float gapGroup = scaled(16.0f);
    const float stroke = ImMax(1.5f, h * 0.07f);

    // Next control: `lead` px after the previous item, on the centred row.
    auto place = [&](float lead) {
        ImGui::SameLine(0.0f, lead);
        ImGui::SetCursorPosY(y0);
        return ImGui::GetCursorScreenPos();
    };
    // The faded hover / immediate press fill every hand-drawn control shares.
    // Call right after the control's InvisibleButton.
    auto hoverFill = [&](ImVec2 p, float w, bool enabled) {
        const bool held = enabled && ImGui::IsItemActive();
        const float t = theme::hoverAnim(ImGui::GetItemID(),
                                         enabled && ImGui::IsItemHovered(), 12.0f);
        if (held || t > 0.0f)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                              ImGui::GetColorU32(held ? ImGuiCol_ButtonActive
                                                      : ImGuiCol_ButtonHovered,
                                                 held ? 1.0f : t),
                              round);
    };
    auto text = [&](float x, float y, ImU32 c, const char* s) {
        dl->AddText(ImVec2(x, y + textDy), c, s);
    };

    // --- breadcrumb: project / scene --------------------------------------
    {
        ImVec2 p = place(gapGroup);
        dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.2f), ImVec2(p.x + 1.0f, p.y + h * 0.8f),
                          col(sem.border));
        ImGui::Dummy(ImVec2(1.0f, h));

        // Project name: what the old Project panel header block said (name,
        // location, terrain, target) is its tooltip; a click opens the
        // project's preferences.
        const char* name = project_.name.c_str();
        const float nameW = ImGui::CalcTextSize(name).x + padX * 2.0f;
        p = place(gapGroup - padX);
        if (ImGui::InvisibleButton("##tb_project", ImVec2(nameW, h)))
            showProjectPrefs_ = true;
        hoverFill(p, nameW, true);
        text(p.x + padX, p.y, col(sem.textDim), name);
        if (ImGui::IsItemHovered()) {
            const SceneData& sc = project_.active();
            ImGui::SetTooltip(
                "%s\n%s\nTerrain: %s%d x %d (scene %s)\nTarget: %s\n\nClick for "
                "Project Preferences.",
                project_.name.c_str(), project_.dir.c_str(),
                sc.terrain.enabled ? "" : "none - world ", sc.terrain.width,
                sc.terrain.depth, sc.name.c_str(), project_.elfName().c_str());
        }

        p = place(0.0f);
        const float slashW = ImGui::CalcTextSize("/").x;
        text(p.x, p.y, col(sem.textDim, 0.55f), "/");
        ImGui::Dummy(ImVec2(slashW, h));

        // Scene picker: the active scene, a "start" badge on the scene the game
        // boots into, a caret. The popup is the old Project > Scenes list plus
        // its two buttons.
        const SceneData& sc = project_.active();
        const bool isStart = project_.activeScene == project_.startScene;
        const char* badge = "start";
        const float badgeW = isStart ? ImGui::CalcTextSize(badge).x * 0.86f + scaled(10.0f) : 0.0f;
        const float caretW = h * 0.42f;
        const float sceneW = padX + ImGui::CalcTextSize(sc.name.c_str()).x +
                             (isStart ? gapS + badgeW : 0.0f) + gapS + caretW + padX;
        p = place(gap - padX * 0.5f);
        if (ImGui::InvisibleButton("Scene picker", ImVec2(sceneW, h)))
            ImGui::OpenPopup("scene_menu");
        dl->AddRectFilled(p, ImVec2(p.x + sceneW, p.y + h), col(sem.surface, 0.55f), round);
        hoverFill(p, sceneW, true);
        float x = p.x + padX;
        {
            ui::FontScope bold(ui::boldFont());
            text(x, p.y, col(sem.text), sc.name.c_str());
            x += ImGui::CalcTextSize(sc.name.c_str()).x + gapS;
        }
        if (isStart) {
            const float fs = ImGui::GetFontSize() * 0.86f;
            const float bh = fs + scaled(4.0f);
            const float by = p.y + (h - bh) * 0.5f;
            dl->AddRectFilled(ImVec2(x, by), ImVec2(x + badgeW, by + bh), col(sem.ok, 0.14f),
                              scaled(4.0f));
            dl->AddText(ImGui::GetFont(), fs, ImVec2(x + scaled(5.0f), by + scaled(2.0f)),
                        col(sem.ok), badge);
            x += badgeW + gapS;
        }
        caret(dl, x + caretW * 0.5f, p.y + h * 0.5f, caretW * 0.32f, col(sem.textDim));
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("scene_menu"))
            ImGui::SetTooltip("Switch scene. Each scene has its own objects and flow\n"
                              "graphs; the game starts in the start scene.");
        ImGui::SetNextWindowPos(ImVec2(p.x, p.y + h + scaled(4.0f)));
        if (ImGui::BeginPopup("scene_menu")) {
            ImGui::TextDisabled("Scenes");
            for (int i = 0; i < (int)project_.scenes.size(); ++i) {
                ImGui::PushID(i + 3000);
                const std::string label = project_.scenes[i].name +
                                          (i == project_.startScene ? "  (start)" : "");
                if (ImGui::MenuItem(label.c_str(), nullptr, project_.activeScene == i))
                    selectScene(i);
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New scene...")) {
                openNewScenePopup_ = true;
                newSceneError_.clear();
            }
            if (ImGui::MenuItem("Scene preferences...")) openScenePreferences();
            if (project_.scenes.size() > 1 &&
                ImGui::MenuItem("Delete this scene..."))
                deleteScenePending_ = project_.activeScene;
            ImGui::EndPopup();
        }

        // Unsaved edits: a warn dot + "3 unsaved", which is also the save
        // button. Shown only while there is something to save, like a document
        // title's dot. The count is unsavedEditCount(); a dirty project with a
        // count of 0 (a layout change, or undone back to the saved state) still
        // says "Unsaved".
        const bool saveAllowed = session_.role() != session::Session::Role::Client;
        if (dirty_ && saveAllowed) {
            const int n = unsavedEditCount();
            char lblBuf[32];
            if (n > 0) std::snprintf(lblBuf, sizeof(lblBuf), "%d unsaved", n);
            else std::snprintf(lblBuf, sizeof(lblBuf), "Unsaved");
            const char* lbl = lblBuf;
            const float dotR = scaled(3.5f);
            const float w = padX + dotR * 2.0f + gapS + ImGui::CalcTextSize(lbl).x + padX;
            p = place(gap);
            if (ImGui::InvisibleButton("##tb_save", ImVec2(w, h))) saveAll("Saved");
            hoverFill(p, w, true);
            dl->AddCircleFilled(ImVec2(p.x + padX + dotR, p.y + h * 0.5f), dotR, col(sem.warn));
            text(p.x + padX + dotR * 2.0f + gapS, p.y, col(sem.textDim), lbl);
            if (ImGui::IsItemHovered()) {
                const std::string save = ui::ShortcutChord("S", nullptr, "+");
                if (n > 0)
                    ImGui::SetTooltip("%d edit%s since the last save - click to save (%s)", n,
                                      n == 1 ? "" : "s", save.c_str());
                else
                    ImGui::SetTooltip("Unsaved changes - click to save (%s)", save.c_str());
            }
        }
    }

    // --- right cluster widths (needed first: the workspaces fit around them) --
    // The game this project built is up on the selected target: Run reads
    // "Running 00:42" and a click on it stops the game (F5 / the caret's Run
    // still rebuild and restart it).
    const Runner::GameStatus game = runner_.gameStatus();
    const bool gameUp = !busy && game.running && game.onPs2 == runOnPs2_;
    char elapsed[24];
    {
        const long t = (long)game.seconds;
        if (t >= 3600)
            std::snprintf(elapsed, sizeof(elapsed), "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
        else
            std::snprintf(elapsed, sizeof(elapsed), "%02ld:%02ld", t / 60, t % 60);
    }
    const char* runKey = gameUp ? elapsed : runOnPs2_ ? "F6" : "F5";
    const char* runLabel = busy ? "Building" : gameUp ? "Running" : "Run";
    const float dotD = scaled(6.0f);
    const float caretW = h * 0.42f;
    const float profW = padX + dotD + gapS + ImGui::CalcTextSize("Release").x + gapS + caretW + padX * 0.5f;
    const float segPad = scaled(3.0f);
    const float segW1 = ImGui::CalcTextSize("PCSX2").x + padX * 2.0f;
    const float segW2 = ImGui::CalcTextSize("PS2").x + padX * 2.0f;
    const float segW = segW1 + segW2 + segPad * 2.0f;
    const float glyph = h * 0.36f;
    const float runW = padX * 1.3f + glyph + gapS + ImGui::CalcTextSize(runLabel).x +
                       (busy ? 0.0f : gapS + ImGui::CalcTextSize(runKey).x) + padX * 1.3f;
    const float runCaretW = h * 0.62f;
    const float clusterW = profW + gap + segW + gap + h + gap + runW + runCaretW + gap + h;
    // titleBarInsetRight: the Windows caption buttons own the bar's right end.
    const float barRight = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - titleBarInsetRight();
    const float clusterX = barRight - scaled(10.0f) - clusterW;

    // --- search widths: a field-looking button that opens the command palette
    // (palette_ui.cpp) - magnifier, hint, keycap - or just the magnifier.
    const bool hasEllipsis = ImGui::GetFont()->IsGlyphInFont((ImWchar)0x2026);
    const std::string hintLong =
        std::string("Search objects, assets, commands") + (hasEllipsis ? "\xE2\x80\xA6" : "...");
    const std::string hintShort = std::string("Search") + (hasEllipsis ? "\xE2\x80\xA6" : "...");
    const std::string searchKey = ui::ShortcutChord("K", nullptr, " ", true);
    const float capH = h * 0.62f;
    const float capW = ui::Keycap(nullptr, ImVec2(), capH, searchKey.c_str());
    const float glassW = h * 0.62f;
    const float searchMinW =
        padX + glassW + gapS + ImGui::CalcTextSize(hintShort.c_str()).x + gapS * 2.0f + capW + padX * 0.6f;
    const float searchFullW =
        padX + glassW + gapS + ImGui::CalcTextSize(hintLong.c_str()).x + gapS * 2.0f + capW + padX * 0.6f;
    const float searchMaxW = ImMax(searchFullW, scaled(420.0f));
    float searchW = h;  // what the search button gets; h = the magnifier only

    // --- workspaces: Scene / Logic / Cinema / UI & HUD -------------------------
    // Each is one of the project's window layouts (the Layout menu), picked by
    // its built-in recipe, so a project's own arrangement of each survives.
    // Each has an icon saying what it is for (scene, graph, film, screen). Room is shared out in this
    // order: the workspace labels, then a wide search field. Short of that the
    // search drops to its magnifier, then the labels go and the marks stay,
    // and with less still the switcher goes (the Layout menu has the same).
    {
        struct Ws {
            const char* id;
            const char* label;
            LayoutRecipe recipe;
            ui::Glyph glyph;
        };
        const Ws ws[4] = {{"Scene##ws", "Scene", LayoutRecipe::Default, ui::Glyph::Box},
                          {"Logic##ws", "Logic", LayoutRecipe::Debugger, ui::Glyph::Nodes},
                          {"Cinema##ws", "Cinema", LayoutRecipe::Director, ui::Glyph::Film},
                          {"UI & HUD##ws", "UI & HUD", LayoutRecipe::MenuDesigner, ui::Glyph::Screen}};
        const int activeRecipe = project_.windowLayouts.empty()
                                     ? -1
                                     : project_.windowLayouts[project_.activeLayout].recipe;
        const float gr = h * 0.19f;
        const float segPadW = scaled(3.0f);
        float fullW = segPadW * 2.0f, iconW = segPadW * 2.0f;
        for (const Ws& w : ws) {
            fullW += padX * 1.6f + gr * 2.0f + gapS + ImGui::CalcTextSize(w.label).x;
            iconW += h;
        }
        ImGui::SameLine(0.0f, 0.0f);
        const float curX = ImGui::GetCursorScreenPos().x + gapGroup;
        // Room for the switcher AND the search, which sits between it and the
        // right cluster with a group gap on each side.
        const float room = clusterX - gapGroup - curX;
        const float searchSlot = gapGroup * 2.0f;
        bool labels = false, show = false;
        if (room >= fullW + searchSlot + searchMinW) {
            labels = show = true;
            searchW = searchMinW;  // widened below, once the free space is known
        } else if (room >= fullW + searchSlot + h) {
            labels = show = true;
        } else if (room >= iconW + searchSlot + h) {
            show = true;
        }
        if (show) {
            const ImVec2 base = place(gapGroup);
            const float totalW = labels ? fullW : iconW;
            dl->AddRectFilled(base, ImVec2(base.x + totalW, base.y + h), col(sem.field), round);
            dl->AddRect(base, ImVec2(base.x + totalW, base.y + h), col(sem.border), round);
            float x = base.x + segPadW;
            const float ih = h - segPadW * 2.0f;
            for (const Ws& w : ws) {
                const float sw = labels ? padX * 1.6f + gr * 2.0f + gapS + ImGui::CalcTextSize(w.label).x : h - 0.0f;
                int layout = -1;
                for (int i = 0; i < (int)project_.windowLayouts.size(); ++i)
                    if (project_.windowLayouts[i].recipe == (int)w.recipe) {
                        layout = i;
                        break;
                    }
                const bool on = activeRecipe == (int)w.recipe;
                ImGui::SetCursorScreenPos(ImVec2(x, base.y + segPadW));
                if (ImGui::InvisibleButton(w.id, ImVec2(sw, ih)) && layout >= 0) switchLayout(layout);
                const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered() && !on, 12.0f);
                if (on || t > 0.0f)
                    dl->AddRectFilled(ImVec2(x, base.y + segPadW), ImVec2(x + sw, base.y + segPadW + ih),
                                      col(sem.surface, on ? 1.0f : 0.5f * t), round - segPadW * 0.5f);
                const float gx = labels ? x + padX * 0.8f + gr : x + sw * 0.5f;
                ui::DrawGlyph(dl, w.glyph, ImVec2(gx, base.y + h * 0.5f), gr * 2.0f,
                              on ? col(sem.accent) : col(sem.textDim, 0.85f));
                if (labels)
                    dl->AddText(ImVec2(gx + gr + gapS, base.y + textDy), on ? col(sem.text) : col(sem.textDim),
                                w.label);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s workspace - the \"%s\" window layout%s", w.label,
                                      layout >= 0 ? project_.windowLayouts[layout].name.c_str() : "?",
                                      layout >= 0 ? "" : " was deleted (Layout menu)");
                x += sw;
            }
            ImGui::SetCursorScreenPos(base);
            ImGui::Dummy(ImVec2(totalW, h));
        }
    }
    // Search: opens the command palette. The wide field is centred in the free
    // space between the switcher and the right cluster; the magnifier alone
    // sits just left of the cluster. When even that does not fit it follows
    // whatever is on its left (and the window edge clips the rest). In SCREEN
    // space: the menu bar offsets SameLine(x) by its own inner margin.
    {
        ImGui::SameLine(0.0f, 0.0f);
        const float curX = ImGui::GetCursorScreenPos().x + gapGroup;
        const float freeR = clusterX - gapGroup;
        const bool wide = searchW > h;
        float x;
        if (wide) {
            searchW = ImClamp(freeR - curX, searchMinW, searchMaxW);
            x = curX + ImMax(0.0f, (freeR - curX - searchW) * 0.5f);
        } else {
            x = ImMax(freeR - h, curX);
        }
        ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetWindowPos().y + y0));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("Search##tb_palette", ImVec2(searchW, h))) {
            paletteOpen_ = true;
            paletteFocus_ = true;
            paletteQuery_[0] = '\0';
            paletteSel_ = 0;
        }
        if (wide) {
            const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
            dl->AddRectFilled(p, ImVec2(p.x + searchW, p.y + h), col(sem.field), round);
            dl->AddRect(p, ImVec2(p.x + searchW, p.y + h),
                        t > 0.0f ? col(sem.textDim, 0.3f + 0.4f * t) : col(sem.border), round);
        } else {
            hoverFill(p, h, true);
        }
        const ImU32 glassCol = wide ? col(sem.textDim) : col(sem.text);
        const ImVec2 c = wide ? ImVec2(p.x + padX + glassW * 0.4f, p.y + h * 0.46f)
                              : ImVec2(p.x + h * 0.46f, p.y + h * 0.46f);
        const float gs = wide ? 0.85f : 1.0f;  // a quieter glass inside the field
        dl->AddCircle(c, h * 0.2f * gs, glassCol, 16, stroke);
        dl->AddLine(ImVec2(c.x + h * 0.14f * gs, c.y + h * 0.14f * gs),
                    ImVec2(c.x + h * 0.28f * gs, c.y + h * 0.28f * gs), glassCol, stroke);
        if (wide) {
            const float capX = p.x + searchW - padX * 0.6f - capW;
            ui::Keycap(dl, ImVec2(capX, p.y + (h - capH) * 0.5f), capH, searchKey.c_str());
            const float tx = p.x + padX + glassW + gapS;
            const bool fitsLong = tx + ImGui::CalcTextSize(hintLong.c_str()).x <= capX - gapS;
            dl->PushClipRect(ImVec2(tx, p.y), ImVec2(capX - gapS, p.y + h), true);
            text(tx, p.y, col(sem.textDim), fitsLong ? hintLong.c_str() : hintShort.c_str());
            dl->PopClipRect();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Search commands, objects, windows, docs (%s)",
                              ui::ShortcutChord("K", nullptr, "+").c_str());
    }

    // The right cluster: right-aligned, unless what is on its left already
    // reaches there - then it simply follows. It ends where the Windows
    // caption buttons begin (titleBarInsetRight), or Run sat under them.
    {
        ImGui::SameLine(0.0f, 0.0f);
        const float curX = ImGui::GetCursorScreenPos().x;
        const float endX = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() -
                           titleBarInsetRight() - scaled(10.0f);
        ImGui::SetCursorScreenPos(ImVec2(ImMax(endX - clusterW, curX + gap), ImGui::GetWindowPos().y + y0));
    }

    // Build profile pill. The one control on the bar whose CURRENT VALUE must
    // be readable at a glance - "why is there no LIVE chip" is answered by
    // seeing "Release" here - so it is a labelled pill, not a glyph.
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##tb_profile", ImVec2(profW, h)))
            ImGui::OpenPopup("profile_menu");
        dl->AddRectFilled(p, ImVec2(p.x + profW, p.y + h), col(sem.surface, 0.55f), round);
        hoverFill(p, profW, true);
        const float cy = p.y + h * 0.5f;
        dl->AddCircleFilled(ImVec2(p.x + padX + dotD * 0.5f, cy), dotD * 0.5f,
                            debugProfile ? col(sem.warn) : col(sem.textDim));
        text(p.x + padX + dotD + gapS, p.y, col(sem.text), debugProfile ? "Debug" : "Release");
        caret(dl, p.x + profW - padX * 0.5f - caretW * 0.5f, cy, caretW * 0.32f, col(sem.textDim));
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("profile_menu"))
            ImGui::SetTooltip(
                "Build profile (same setting as Project > Preferences > Build & debug).\n"
                "Debug carries the devkit channels, the overlays, -g and the "
                "unstripped\nELF the crash reporter needs - Live Link, the Live "
                "Debugger and Live\nLogic exist in debug builds only. Release "
                "carries none of it.");
        ImGui::SetNextWindowPos(ImVec2(p.x, p.y + h + scaled(4.0f)));
        if (ImGui::BeginPopup("profile_menu")) {
            if (ImGui::MenuItem("Release", nullptr, !debugProfile) && debugProfile) {
                project_.settings.buildProfile = "release";
                commitChange();
            }
            if (ImGui::MenuItem("Debug", nullptr, debugProfile) && !debugProfile) {
                project_.settings.buildProfile = "debug";
                commitChange();
            }
            ImGui::EndPopup();
        }
    }

    // Run target: a two-way segmented control instead of a menu, because which
    // machine F5 drives is state you want to SEE, not look up.
    {
        ImVec2 p = place(gap);
        const ImVec2 base = p;
        dl->AddRectFilled(base, ImVec2(base.x + segW, base.y + h), col(sem.field), round);
        dl->AddRect(base, ImVec2(base.x + segW, base.y + h), col(sem.border), round);
        const float ih = h - segPad * 2.0f;
        auto seg = [&](const char* id, const char* label, float x, float w, bool on,
                       bool enabled, const char* tip) {
            ImGui::SetCursorScreenPos(ImVec2(x, base.y + segPad));
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, ih)) && enabled;
            const float t = theme::hoverAnim(ImGui::GetItemID(),
                                             enabled && ImGui::IsItemHovered() && !on, 12.0f);
            if (on)
                dl->AddRectFilled(ImVec2(x, base.y + segPad),
                                  ImVec2(x + w, base.y + segPad + ih), col(sem.surface),
                                  round - segPad * 0.5f);
            else if (t > 0.0f)
                dl->AddRectFilled(ImVec2(x, base.y + segPad),
                                  ImVec2(x + w, base.y + segPad + ih),
                                  col(sem.surface, 0.5f * t), round - segPad * 0.5f);
            const ImU32 c = !enabled ? col(sem.textDim, 0.5f) : on ? col(sem.text) : col(sem.textDim);
            dl->AddText(ImVec2(x + padX, base.y + (h - ImGui::GetTextLineHeight()) * 0.5f), c, label);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
            return clicked;
        };
        if (seg("PCSX2##tb_target", "PCSX2", base.x + segPad, segW1, !runOnPs2_, true,
                "Run in the PCSX2 emulator (F5)") && runOnPs2_) {
            runOnPs2_ = false;
            saveGlobalConfig();
        }
        if (seg("PS2##tb_target", "PS2", base.x + segPad + segW1, segW2, runOnPs2_, ps2Ready,
                ps2Ready ? "Run on a real PlayStation 2 over ps2link (F6)" : noIpTip) &&
            !runOnPs2_) {
            runOnPs2_ = true;
            saveGlobalConfig();
        }
        // Leave the cursor after the whole control, as one item.
        ImGui::SetCursorScreenPos(base);
        ImGui::Dummy(ImVec2(segW, h));
    }

    // Build only: an isometric box - the build's output.
    {
        const ImVec2 p = place(gap);
        const bool en = !busy;
        if (ImGui::InvisibleButton("##tb_build", ImVec2(h, h)) && en)
            runner_.buildAndRun(projectForBuild(), false);
        hoverFill(p, h, en);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Build (no run) (Ctrl+Shift+B)");
        const ImU32 c = en ? col(sem.text) : col(sem.textDim);
        const float pad = h * 0.26f;
        const ImVec2 a(p.x + pad, p.y + pad), b(p.x + h - pad, p.y + h - pad);
        const float w = b.x - a.x, hh = b.y - a.y, cx = a.x + w * 0.5f, sh = hh * 0.26f;
        const ImVec2 hex[6] = {ImVec2(cx, a.y),     ImVec2(b.x, a.y + sh), ImVec2(b.x, b.y - sh),
                               ImVec2(cx, b.y),     ImVec2(a.x, b.y - sh), ImVec2(a.x, a.y + sh)};
        dl->AddPolyline(hex, 6, c, ImDrawFlags_Closed, stroke);
        const ImVec2 mid(cx, a.y + sh * 2.0f);
        dl->AddLine(mid, hex[5], c, stroke);
        dl->AddLine(mid, hex[1], c, stroke);
        dl->AddLine(mid, hex[3], c, stroke);
    }

    // Run: the one filled button on the bar, in the target's colour (the
    // theme's "ok" = the emulator, its accent = the real console). While a
    // build runs it turns into a quiet "Building" with a spinner - Stop next
    // to it cancels.
    ImVec2 runPos;
    {
        runPos = place(gap);
        const ImVec2& p = runPos;
        const bool en = !busy && targetReady;
        const ImVec4 fill = runOnPs2_ ? sem.accent : sem.ok;
        if (ImGui::InvisibleButton("Run###tb_run", ImVec2(runW, h)) && en) {
            if (!gameUp) runSelectedTarget(true);
            else if (runOnPs2_) runner_.stopPs2(project_);
            else runner_.stopEmulator(project_);
        }
        // `expect-checked '##tb_run'` = the game is running (scripts, docs).
        uiscript::markLastItemChecked(gameUp);
        const bool held = en && ImGui::IsItemActive();
        const bool hovered = en && ImGui::IsItemHovered();
        const float t = theme::hoverAnim(ImGui::GetItemID(), hovered, 12.0f);
        const ImDrawFlags leftOnly = ImDrawFlags_RoundCornersLeft;
        if (gameUp) {
            // Quiet while the game runs: the target's colour moves to the dot
            // and a hover turns the whole button into Stop.
            dl->AddRectFilled(p, ImVec2(p.x + runW, p.y + h), col(sem.surface), round, leftOnly);
            if (t > 0.0f)
                dl->AddRectFilled(p, ImVec2(p.x + runW, p.y + h), col(sem.danger, 0.16f * t), round,
                                  leftOnly);
        } else if (en) {
            dl->AddRectFilled(p, ImVec2(p.x + runW, p.y + h), col(fill), round, leftOnly);
            if (held || t > 0.0f)
                dl->AddRectFilled(p, ImVec2(p.x + runW, p.y + h),
                                  held ? IM_COL32(0, 0, 0, 40) : IM_COL32(255, 255, 255, (int)(34 * t)),
                                  round, leftOnly);
        } else {
            dl->AddRectFilled(p, ImVec2(p.x + runW, p.y + h), col(sem.surface), round, leftOnly);
        }
        const ImU32 ink = gameUp ? (hovered ? col(sem.danger) : col(sem.text))
                          : en   ? col(sem.ground) : col(sem.textDim);
        float x = p.x + padX * 1.3f;
        const float cy = p.y + h * 0.5f;
        if (gameUp && hovered) {
            const float s = glyph * 0.8f;
            dl->AddRectFilled(ImVec2(x + (glyph - s) * 0.5f, cy - s * 0.5f),
                              ImVec2(x + (glyph + s) * 0.5f, cy + s * 0.5f), col(sem.danger), s * 0.18f);
        } else if (gameUp) {
            const float pulse = 0.55f + 0.45f * (0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 3.0f));
            dl->AddCircleFilled(ImVec2(x + glyph * 0.5f, cy), glyph * 0.32f, col(fill, pulse));
        } else if (busy) {
            const float r = glyph * 0.45f;
            const float a0 = (float)ImGui::GetTime() * 5.0f;
            dl->PathArcTo(ImVec2(x + glyph * 0.5f, cy), r, a0, a0 + 4.2f, 20);
            dl->PathStroke(col(sem.accent), 0, stroke);
        } else {
            dl->AddTriangleFilled(ImVec2(x + glyph * 0.1f, cy - glyph * 0.5f),
                                  ImVec2(x + glyph * 0.1f, cy + glyph * 0.5f),
                                  ImVec2(x + glyph, cy), ink);
        }
        x += glyph + gapS;
        {
            ui::FontScope bold(ui::boldFont());
            // Hovered while running: "Stop", in the room "Running" takes.
            text(x, p.y, ink, gameUp && hovered ? "Stop" : runLabel);
            x += ImGui::CalcTextSize(runLabel).x + gapS;
        }
        if (!busy) {
            ui::FontScope mono(ui::monoFont());
            const ImVec4 inkV = gameUp ? sem.text : en ? sem.ground : sem.textDim;
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.82f,
                        ImVec2(x, p.y + (h - ImGui::GetFontSize() * 0.82f) * 0.5f),
                        col(inkV, 0.6f), runKey);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", busy ? "Building - Stop cancels it."
                                    : gameUp ? (runOnPs2_ ? "The game is running on the PS2 - click to stop it.\n"
                                                            "F6 rebuilds and restarts it."
                                                          : "PCSX2 is running this project's game - click to "
                                                            "close it.\nF5 rebuilds and restarts it.")
                                    : !targetReady ? noIpTip
                                    : runOnPs2_    ? "Build && Run on PS2 (F6)"
                                                   : "Build && Run in PCSX2 (F5)");

        // The caret half: run variants.
        const ImVec2 cp = place(0.0f);
        if (ImGui::InvisibleButton("Run options###tb_run_more", ImVec2(runCaretW, h)))
            ImGui::OpenPopup("run_menu");
        const ImDrawFlags rightOnly = ImDrawFlags_RoundCornersRight;
        const float ct = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        dl->AddRectFilled(cp, ImVec2(cp.x + runCaretW, cp.y + h),
                          en && !gameUp ? col(fill) : col(sem.surface), round, rightOnly);
        if (ct > 0.0f)
            dl->AddRectFilled(cp, ImVec2(cp.x + runCaretW, cp.y + h),
                              IM_COL32(255, 255, 255, (int)(34 * ct)), round, rightOnly);
        dl->AddLine(ImVec2(cp.x, cp.y + h * 0.22f), ImVec2(cp.x, cp.y + h * 0.78f),
                    en && !gameUp ? IM_COL32(0, 0, 0, 60) : col(sem.border), 1.0f);
        caret(dl, cp.x + runCaretW * 0.5f, cp.y + h * 0.5f, runCaretW * 0.2f,
              gameUp ? col(sem.textDim) : ink);
        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("run_menu"))
            ImGui::SetTooltip("Run options...");
    }

    // Stop: cancels a running build, else stops the selected target - closes
    // PCSX2 (only the instance booting THIS project's ELF), or on the console
    // kills the file server + resets ps2link.
    {
        const ImVec2 p = place(gap);
        const bool en = busy || targetReady;
        if (ImGui::InvisibleButton("Stop###tb_stop", ImVec2(h, h)) && en) {
            if (busy) runner_.cancel();
            else if (runOnPs2_) runner_.stopPs2(project_);
            else runner_.stopEmulator(project_);
        }
        hoverFill(p, h, en);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", busy           ? "Cancel build"
                                    : !targetReady ? noIpTip
                                    : runOnPs2_    ? "Stop the game on the PS2"
                                                   : "Stop PCSX2");
        const float s = h * 0.3f;
        const ImVec2 c(p.x + h * 0.5f, p.y + h * 0.5f);
        dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f),
                          en ? col(sem.danger) : col(sem.textDim), s * 0.18f);
    }

    ImGui::SetNextWindowPos(ImVec2(runPos.x, runPos.y + h + scaled(4.0f)));
    if (ImGui::BeginPopup("run_menu")) {
        const char* noBuildKey = runOnPs2_ ? "Ctrl+F6" : "Ctrl+F5";
        if (ImGui::MenuItem("Run", runKey, false, !busy && targetReady))
            runSelectedTarget(true);
        if (ImGui::MenuItem("Run without build", noBuildKey, false, !busy && targetReady))
            runSelectedTarget(false);
        // No "Debug" entry: Run opens the Debugger by itself whenever the
        // project is built with the debugger on (runSelectedTarget).
        ImGui::Separator();
        if (ImGui::MenuItem("Emulator (PCSX2)", nullptr, !runOnPs2_)) {
            runOnPs2_ = false;
            saveGlobalConfig();
        }
        if (ImGui::MenuItem("PlayStation 2 (ps2link)", nullptr, runOnPs2_, ps2Ready)) {
            runOnPs2_ = true;
            saveGlobalConfig();
        }
        if (!ps2Ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", noIpTip);
        ImGui::EndPopup();
    }
}

// --- merged title bar (platform.hpp, docs/editor-theme.md "Title bar") -------

namespace {
// Windows 11's caption button width at 100 %. Height = the whole bar.
constexpr float kCaptionButtonW = 46.0f;
}  // namespace

float App::titleBarInsetLeft() const {
    return titleBarMerged_ ? platform::titleBarInsetLeft(window_) : 0.0f;
}

float App::titleBarInsetRight() const {
    return titleBarMerged_ && platform::titleBarHasCaptionButtons()
               ? 3.0f * scaled(kCaptionButtonW)
               : 0.0f;
}

// Called inside the main menu bar after everything else on it. Draws the
// minimize / maximize / close buttons where the platform leaves none
// (Windows), then reports the bar to the platform layer: its height and
// whether the pointer is over a bare part of it, which the OS-side mouse-down
// hook (NSEvent monitor / WndProc) turns into a window drag or a double-click
// zoom. "Bare" = this window hovered, no widget hovered or held, no popup open
// (a click there must close the open menu, not move the window).
void App::drawTitleBarControls() {
    if (!titleBarMerged_) return;
    const ImVec2 wp = ImGui::GetWindowPos();
    const float barW = ImGui::GetWindowWidth();
    const float barH = ImGui::GetWindowHeight();

    if (platform::titleBarHasCaptionButtons()) {
        // UNVERIFIED ON WINDOWS, the only platform that draws these.
        const theme::Semantics& sem = theme::semantics();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float w = scaled(kCaptionButtonW);
        const float g = scaled(5.0f);  // glyph half-extent
        const float th = ImMax(1.0f, scaled(1.0f));
        const bool maximized = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED) == GLFW_TRUE;
        static const char* const ids[3] = {"Minimize##caption", "Maximize##caption",
                                           "Close##caption"};
        dl->PushClipRect(wp, ImVec2(wp.x + barW, wp.y + barH), false);
        float x = wp.x + barW - 3.0f * w;
        for (int i = 0; i < 3; ++i, x += w) {
            ImGui::SetCursorScreenPos(ImVec2(x, wp.y));
            const bool clicked = ImGui::InvisibleButton(ids[i], ImVec2(w, barH));
            const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
            const bool held = ImGui::IsItemActive();
            const ImVec2 a(x, wp.y), b(x + w, wp.y + barH);
            if (i == 2 && hot)  // close: the system red with a white glyph
                dl->AddRectFilled(a, b, held ? IM_COL32(148, 20, 30, 255) : IM_COL32(196, 43, 28, 255));
            else if (hot)
                dl->AddRectFilled(a, b, ImGui::GetColorU32(held ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered));
            const ImU32 c = i == 2 && hot ? IM_COL32_WHITE : col(sem.text);
            const ImVec2 m(IM_TRUNC(x + w * 0.5f) + 0.5f, IM_TRUNC(wp.y + barH * 0.5f) + 0.5f);
            if (i == 0) {
                dl->AddLine(ImVec2(m.x - g, m.y), ImVec2(m.x + g, m.y), c, th);
            } else if (i == 1 && !maximized) {
                dl->AddRect(ImVec2(m.x - g, m.y - g), ImVec2(m.x + g, m.y + g), c, 0.0f, 0, th);
            } else if (i == 1) {  // restore: a square with a second one behind it
                const float o = g * 0.4f;
                dl->AddRect(ImVec2(m.x - g, m.y - g + o), ImVec2(m.x + g - o, m.y + g), c, 0.0f, 0, th);
                const ImVec2 back[4] = {ImVec2(m.x - g + o, m.y - g + o), ImVec2(m.x - g + o, m.y - g),
                                        ImVec2(m.x + g, m.y - g), ImVec2(m.x + g, m.y + g - o)};
                dl->AddPolyline(back, 4, c, 0, th);
                dl->AddLine(back[3], ImVec2(m.x + g - o, m.y + g - o), c, th);
            } else {
                dl->AddLine(ImVec2(m.x - g, m.y - g), ImVec2(m.x + g, m.y + g), c, th);
                dl->AddLine(ImVec2(m.x + g, m.y - g), ImVec2(m.x - g, m.y + g), c, th);
            }
            if (!clicked) continue;
            if (i == 0) glfwIconifyWindow(window_);
            else if (i == 1 && maximized) glfwRestoreWindow(window_);
            else if (i == 1) glfwMaximizeWindow(window_);
            else requestExit();  // the same unsaved-changes prompt as File > Exit
        }
        dl->PopClipRect();
    }

    platform::TitleBarFrame frame;
    frame.height = barH;
    frame.overEmpty = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                      !ImGui::IsAnyItemActive() &&
                      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId |
                                                  ImGuiPopupFlags_AnyPopupLevel);
    platform::updateTitleBar(window_, frame);}

// The bar along the bottom of the window: what the editor and the running game
// are doing (left) and what the scene holds (right). A viewport side bar, so
// the dockspace above it shrinks instead of being covered.
void App::drawStatusBar() {
    const theme::Semantics& sem = theme::semantics();
    const float h = ImGui::GetFrameHeight();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, sem.ground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(scaled(12.0f), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoSavedSettings;
    const bool open =
        ImGui::BeginViewportSideBar("##StatusBar", ImGui::GetMainViewport(), ImGuiDir_Down, h, flags);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    if (!open) {
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float round = ImGui::GetStyle().FrameRounding;
    const float gap = scaled(8.0f);
    const float gapGroup = scaled(18.0f);
    const ImU32 colDim = col(sem.textDim);
    bool first = true;
    auto next = [&](float lead) {
        if (!first) ImGui::SameLine(0.0f, lead);
        first = false;
        ImGui::SetCursorPosY(0.0f);
        return ImGui::GetCursorScreenPos();
    };
    // A status chip: dot (or spinner) + label, one hit box. Returns clicked.
    auto chip = [&](const char* id, float lead, ImU32 c, const char* label, const char* tip,
                    bool spinner = false) {
        const ImVec2 p = next(lead);
        const float dotR = h * 0.13f;
        const float padX = scaled(6.0f);
        const float w = padX + dotR * 2.0f + scaled(6.0f) + ImGui::CalcTextSize(label).x + padX;
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        if (t > 0.0f)
            dl->AddRectFilled(ImVec2(p.x, p.y + h * 0.12f), ImVec2(p.x + w, p.y + h * 0.88f),
                              ImGui::GetColorU32(ImGuiCol_ButtonHovered, t), round);
        const ImVec2 mid(p.x + padX + dotR, p.y + h * 0.5f);
        if (spinner) {
            const float a0 = (float)ImGui::GetTime() * 5.0f;
            dl->PathArcTo(mid, dotR * 1.3f, a0, a0 + 4.2f, 16);
            dl->PathStroke(c, 0, ImMax(1.5f, h * 0.06f));
        } else {
            dl->AddCircleFilled(mid, dotR, c);
        }
        dl->AddText(ImVec2(p.x + padX + dotR * 2.0f + scaled(6.0f),
                           p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                    c, label);
        if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return clicked;
    };

    // --- left: build state, then the live channels --------------------------
    if (hasProject_) {
        const bool busy = runner_.busy();
        const bool failed = runner_.state() == Runner::State::Failed;
        // Build status. A REAL label rather than a "##" id: an InvisibleButton
        // draws nothing either way, and a label-less widget is one --ui-script
        // can never target.
        if (busy || failed) {
            if (chip("Build status", 0.0f, busy ? col(sem.accent) : col(sem.danger),
                     busy ? "BUILDING" : "BUILD FAILED",
                     busy ? "Building - Stop cancels it (or Build > Cancel Build).\n"
                            "Click for the Output panel."
                          : "The last build failed. Click for the Output panel -\n"
                            "the first error is in there.",
                     busy))
                pendingFocusWindow_ = "Output";
        }

        // Live Link: also the on/off switch (same as Build > Live Link). Debug
        // profile only - the poller exists in debug builds alone.
        if (project_.settings.buildProfile == "debug") {
            const bool on = project_.settings.liveLink;
            ImU32 c = colDim;
            const char* label = "LIVE off";
            const char* tip =
                "Live Link is off (project setting) - the game is built without "
                "the poller\nand the editor writes no snapshots. Click to enable "
                "(then Build & Run).";
            if (on) {
                switch (liveLinkState_) {
                    case LiveLinkState::Live:
                        c = col(sem.ok);
                        label = "LIVE";
                        tip = "Live Link: object edits (move/rotate/scale/recolor,"
                              " add/delete) stream\ninto the running game. Flow "
                              "GRAPHS are not object edits - those are\nLive "
                              "Logic's (the LOGIC chip). Click to turn off "
                              "(project setting).";
                        break;
                    case LiveLinkState::RebuildNeeded:
                        c = col(sem.warn);
                        label = "LIVE (rebuild)";
                        tip = "Live Link: the scene changed in a way the session "
                              "can't absorb (model/\nmaterial swaps, lights, "
                              "projected decals, mirrors, layers, objects with\n"
                              "logic...) - Build & Run (F5) to resync. Click to "
                              "turn off.";
                        break;
                    default:  // Off (transient) / NoBuild
                        label = "LIVE (build)";
                        tip = "Live Link is on, but the last build has no poller "
                              "yet - Build & Run\n(F5) once and edits start "
                              "streaming. Click to turn off.";
                        break;
                }
            }
            if (chip("##tb_livelink", gapGroup, c, label, tip)) {
                project_.settings.liveLink = !on;
                commitChange();
            }
        }

        // Live Debugger: green = the game reports its graphs, orange = halted.
        if (project_.settings.buildProfile == "debug" && project_.settings.liveDebug) {
            ImU32 c = colDim;
            char label[48] = "DBG";
            const char* tip =
                "Live Debugger: waiting for a game to report. Build & Run (F5) "
                "and the\ngraph starts lighting up as it runs. Click to open the "
                "Debugger panel (F9).";
            switch (dbgState_) {
                case DbgState::Running:
                    c = col(sem.ok);
                    std::snprintf(label, sizeof(label), "DBG %.0f fps", dbgFps_);
                    tip = "Live Debugger: the running game is reporting every "
                          "flow-graph node it\nfires - watch the Flow Graph light "
                          "up. Click to open the Debugger (F9).";
                    break;
                case DbgState::Halted:
                    c = IM_COL32(245, 130, 90, 255);
                    std::snprintf(label, sizeof(label), "DBG halted @ %u", dbgSnap_.frame);
                    tip = "Live Debugger: the game is stopped (breakpoint or "
                          "Pause). F10 continues,\nF11 steps one frame. Click to "
                          "open the Debugger panel.";
                    break;
                case DbgState::Stale:
                    c = col(sem.warn);
                    std::snprintf(label, sizeof(label), "DBG (rebuild)");
                    tip = "Live Debugger: the running game was built from "
                          "different graphs, so node\nnumbering no longer matches. "
                          "Build & Run (F5) to resync.";
                    break;
                default: break;
            }
            if (chip("##tb_livedbg", gap, c, label, tip)) showDebugger_ = true;
        }

        // Live Logic: only once graphs differ from the build.
        if (project_.settings.buildProfile == "debug" &&
            (liveLogicState_ == LogicState::Patched || liveLogicState_ == LogicState::Blocked ||
             liveLogicState_ == LogicState::OffStale)) {
            const bool offStale = liveLogicState_ == LogicState::OffStale;
            const bool blocked = liveLogicState_ == LogicState::Blocked;
            char label[48];
            if (offStale) std::snprintf(label, sizeof(label), "LOGIC (off)");
            else if (blocked) std::snprintf(label, sizeof(label), "LOGIC (rebuild)");
            else std::snprintf(label, sizeof(label), "LOGIC (%d)", liveLogicPatchCount_);
            const char* tip =
                offStale  ? "A flow graph changed since the build, and Live Logic "
                            "is OFF - the\nrunning game is still on its compiled "
                            "graphs. (LIVE stays green:\nLive Link streams object "
                            "edits, never graph logic.)\nBuild & Run (F5), or turn "
                            "Live Logic on in Project Preferences > Build & debug."
                : blocked ? "Live Logic: an edited graph uses nodes the interpreter "
                            "cannot run\n(audio, AI, animation, spawning, runtime "
                            "text, branching nodes...)\n- Build & Run (F5). Click "
                            "for the list."
                          : "Live Logic: these graphs were compiled by the editor and "
                            "are running\nin the game right now, with no rebuild. "
                            "Click to see them.";
            if (chip("##tb_livelogic", gap, (blocked || offStale) ? col(sem.warn) : col(sem.special),
                     label, tip))
                showDebugger_ = true;
        }

        // Collaboration session: hidden when idle.
        const auto sst = session_.state();
        if (sst != session::Session::State::Idle && sst != session::Session::State::Error) {
            char label[32] = "SYNC";
            ImU32 c = col(sem.warn);
            const char* tip = "Session is starting up / transferring the project.";
            switch (sst) {
                case session::Session::State::Listening: {
                    const int others = (int)sessionPeers_.size() - 1;
                    std::snprintf(label, sizeof(label), "SESSION (%d)", others < 0 ? 0 : others);
                    c = col(sem.ok);
                    tip = "Hosting a live session - click for peers / kick / close.";
                    break;
                }
                case session::Session::State::Live:
                    std::snprintf(label, sizeof(label), "JOINED");
                    c = col(sem.accent);
                    tip = "Joined a live session - the host owns saving. Click for "
                          "the Session window.";
                    break;
                default:
                    std::snprintf(label, sizeof(label), "SYNC");
                    break;
            }
            if (chip("##tb_session", gapGroup, c, label, tip)) showSessionWindow_ = true;
        }
    }

    // The last one-line message (Saved, Exported, ...).
    if (!statusMessage_.empty()) {
        const ImVec2 p = next(gapGroup);
        const char* s = statusMessage_.c_str();
        dl->AddText(ImVec2(p.x, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), colDim, s);
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize(s).x, h));
    }

    // --- right: what the scene holds, and the interface scale ---------------
    char right[160] = "";
    if (hasProject_) {
        const SceneData& sc = project_.active();
        const int n = (int)sc.objects.size();
        const int sel = (int)selection_.size();
        if (sel > 0)
            std::snprintf(right, sizeof(right), "%s  ·  %d object%s  ·  %d selected  ·  UI %d%%",
                          sc.name.c_str(), n, n == 1 ? "" : "s", sel,
                          (int)(uiScaleApplied_ * 100.0f + 0.5f));
        else
            std::snprintf(right, sizeof(right), "%s  ·  %d object%s  ·  UI %d%%", sc.name.c_str(), n,
                          n == 1 ? "" : "s", (int)(uiScaleApplied_ * 100.0f + 0.5f));
    } else {
        std::snprintf(right, sizeof(right), "UI %d%%", (int)(uiScaleApplied_ * 100.0f + 0.5f));
    }
    {
        const float w = ImGui::CalcTextSize(right).x;
        const float x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - w;
        const ImVec2 wp = ImGui::GetWindowPos();
        dl->AddText(ImVec2(wp.x + x, wp.y + (h - ImGui::GetTextLineHeight()) * 0.5f), colDim, right);
    }
    ImGui::End();
}

// The icon rail along the left edge: one button per Project panel page, then
// (at the bottom) the AI Assistant and Project Preferences. A viewport side bar
// like the status bar, so the dockspace shrinks around it. Each button's label
// is its page name ("Outliner", "Assets"...) - the --ui-script target.
void App::drawRail() {
    const theme::Semantics& sem = theme::semantics();
    const float w = IM_TRUNC(ImGui::GetFrameHeight() * 1.9f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, sem.ground);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, scaled(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoSavedSettings;
    const bool open =
        ImGui::BeginViewportSideBar("##Rail", ImGui::GetMainViewport(), ImGuiDir_Left, w, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (!open || !hasProject_) {
        ImGui::End();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float btn = IM_TRUNC(w * 0.8f);
    const float x = ImGui::GetWindowPos().x + (w - btn) * 0.5f;
    const float round = ImGui::GetStyle().FrameRounding + scaled(2.0f);
    auto railButton = [&](const char* label, ui::Glyph g, bool active, float y) {
        const ImVec2 p(x, y);
        ImGui::SetCursorScreenPos(p);
        const bool clicked = ImGui::InvisibleButton(label, ImVec2(btn, btn));
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        if (active) {
            dl->AddRectFilled(p, ImVec2(p.x + btn, p.y + btn), ImGui::GetColorU32(sem.surface), round);
            // A short accent bar on the window edge marks the open page.
            const float bx = ImGui::GetWindowPos().x;
            dl->AddRectFilled(ImVec2(bx, p.y + btn * 0.28f), ImVec2(bx + scaled(3.0f), p.y + btn * 0.72f),
                              ImGui::GetColorU32(sem.accent), scaled(2.0f));
        } else if (t > 0.0f) {
            ImVec4 hc = sem.surface;
            hc.w *= 0.7f * t;
            dl->AddRectFilled(p, ImVec2(p.x + btn, p.y + btn), ImGui::GetColorU32(hc), round);
        }
        ImVec4 ink = active ? sem.accent : sem.textDim;
        if (!active && t > 0.0f) ink = ImVec4(ink.x + (sem.text.x - ink.x) * t, ink.y + (sem.text.y - ink.y) * t,
                                               ink.z + (sem.text.z - ink.z) * t, 1.0f);
        ui::DrawGlyph(dl, g, ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.44f,
                      ImGui::GetColorU32(ink));
        if (ImGui::IsItemHovered())  // the text before "##", never the ID suffix
            ImGui::SetTooltip("%.*s", (int)(ImGui::FindRenderedTextEnd(label) - label), label);
        return clicked;
    };

    struct Page {
        const char* label;
        ui::Glyph glyph;
    };
    static const Page kPages[] = {{"Outliner", ui::Glyph::List},
                                  {"Layers", ui::Glyph::Layers},
                                  {"Assets", ui::Glyph::Box},
                                  {"Audio", ui::Glyph::Music},
                                  {"Scripts", ui::Glyph::Code}};
    float y = ImGui::GetWindowPos().y + scaled(6.0f);
    for (int i = 0; i < (int)IM_ARRAYSIZE(kPages); ++i) {
        if (railButton(kPages[i].label, kPages[i].glyph, leftPage_ == i, y)) {
            leftPage_ = i;
            pendingFocusWindow_ = "Project";  // a docked-behind panel comes forward
        }
        y += btn + scaled(4.0f);
    }

    float yb = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - btn - scaled(8.0f);
    if (railButton("Project Preferences##rail", ui::Glyph::Sliders, false, yb))
        openProjectPreferences();
    yb -= btn + scaled(4.0f);
    if (railButton("AI Assistant##rail", ui::Glyph::Sparkle, showAiChat_, yb)) showAiChat_ = !showAiChat_;
    ImGui::End();
}
