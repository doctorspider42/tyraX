// -------------------------------------------------------------------------
// The 2D-authoring windows: UI Editor (HUD images/texts/screen stack),
// Loading Screen, Splash, Font Manager, button icons, Input Map, the
// Animation Editor and the GI-bake / Tree-generator tool windows.
//
// Split out of app.cpp so the editor builds in parallel: it was one 26k-line
// translation unit and therefore the whole build's critical path. These are
// still App:: members declared in app.hpp - the assetbrowser.cpp precedent.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "aisupport.hpp"
#include "animedit.hpp"
#include "decalproj.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "gl_loader.h"
#include "hudanim.hpp"
#include "fbxparser.hpp"
#include "gigpu.hpp"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "update.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "uvunwrap.hpp"
#include "stochtile.hpp"
#include "templates.hpp"
#include "wavconvert.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#include "platform.hpp"

const App::HudTexture* App::hudTexture(const std::string& relPath) {
    auto it = hudTexCache_.find(relPath);
    if (it != hudTexCache_.end()) return it->second.tex ? &it->second : nullptr;

    HudTexture entry;
    const std::string full = (std::filesystem::path(project_.dir) / relPath).string();
    int w = 0, h = 0, comp = 0;
    if (unsigned char* pixels = stbi_load(full.c_str(), &w, &h, &comp, 4)) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUploadTexRgba(w, h, pixels);
        stbi_image_free(pixels);
        entry = {tex, w, h};
    }
    hudTexCache_[relPath] = entry;
    return entry.tex ? &hudTexCache_[relPath] : nullptr;
}

// The embedded built-in USE prompt sprite as a GL texture (lazy; process
// lifetime). Shown in the viewport overlay while no custom image overrides it.
const App::HudTexture* App::builtinUseTexture() {
    if (builtinUseTex_.tex) return &builtinUseTex_;
    size_t n = 0;
    const unsigned char* png = templates::usePromptPng(n);
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels =
        stbi_load_from_memory(png, (int)n, &w, &h, &comp, 4);
    if (!pixels) return nullptr;
    glGenTextures(1, &builtinUseTex_.tex);
    glBindTexture(GL_TEXTURE_2D, builtinUseTex_.tex);
    glUploadTexRgba(w, h, pixels);
    stbi_image_free(pixels);
    builtinUseTex_.w = w;
    builtinUseTex_.h = h;
    return &builtinUseTex_;
}

// The built-in drawing of a text icon as a GL texture (lazy, process lifetime).
// The manager needs this because the icon PNGs are generated at the first build:
// before that there is no file to preview, and after a "restore default" the
// file is deliberately gone until the next build.
const App::HudTexture* App::builtinIconTexture(const std::string& iconName) {
    auto it = builtinIconTex_.find(iconName);
    if (it != builtinIconTex_.end())
        return it->second.tex ? &it->second : nullptr;

    HudTexture entry;
    std::vector<unsigned char> rgba;
    const int px = menubake::kIconBakeSize;
    if (menubake::bakeBuiltinIconRGBA(iconName, px, rgba)) {
        glGenTextures(1, &entry.tex);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        glUploadTexRgba(px, px, rgba.data());
        entry.w = entry.h = px;
    }
    builtinIconTex_[iconName] = entry;
    return entry.tex ? &builtinIconTex_[iconName] : nullptr;
}

// Restore one icon to how a fresh project would have it: the generated path and
// scale 1, with the PNG removed so the built-in drawing comes back (the next
// build re-creates it; the preview falls back to the drawing meanwhile).
void App::restoreDefaultTextIcon(TextIcon& icon) {
    const std::string def = "res/hud/" + menubake::iconFileName(icon.name);
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(project_.dir) / def, ec);
    if (!icon.path.empty() && icon.path != def)
        hudTexCache_.erase(icon.path);
    hudTexCache_.erase(def);
    icon.path = def;
    icon.scale = 1.0f;
    menubake::clearIconImageCache();
}

// A HUD text as a GL texture for the viewport overlay, re-baked when its
// content changes (keyed by name; a handful of small textures at most).
const App::HudTexture* App::hudTextTexture(const HudText& t) {
    // The font reference alone is not enough: re-pointing that entry at another
    // TTF changes the bake without touching the text, so the source path is
    // part of the key too.
    const GameFont* gf = project_.findFont(t.font);
    const std::string key = t.text + "\x1f" + t.font + "\x1f" +
                            (gf ? gf->fontPath : std::string()) + "\x1f" +
                            std::to_string(t.size) + "\x1f" +
                            std::to_string(t.shadow) + "\x1f" +
                            std::to_string(t.color[0]) + "," +
                            std::to_string(t.color[1]) + "," +
                            std::to_string(t.color[2]);
    TextTexture& entry = textTexCache_[t.name];
    if (entry.tex && entry.key == key) return &entry.hud;
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!menubake::bakeTextRGBA(t, project_, rgba, w, h)) return nullptr;
    if (!entry.tex) glGenTextures(1, &entry.tex);
    glBindTexture(GL_TEXTURE_2D, entry.tex);
    glUploadTexRgba(w, h, rgba.data());
    entry.key = key;
    entry.hud = {entry.tex, w, h};
    return &entry.hud;
}

// Shared TTF picker combo (menus, HUD texts): default chain / fonts imported
// into the project (res/fonts) / a curated set of stock Windows fonts
// (existence-checked) / import a new TTF. Returns true when fontPath changed.
bool App::fontSourceCombo(std::string& fontPath) {
    bool changed = false;
    const std::string defaultLabel =
        std::string("Default (") + platform::defaultFontLabel() + ")";
    std::string current = defaultLabel;
    if (!fontPath.empty())
        current = std::filesystem::path(fontPath).filename().string();
    ImGui::SetNextItemWidth(scaled(200.0f));
    // ui::BeginCombo is the stock combo outside an InspectorRows scope (the
    // menu and HUD editors) and a label-left row inside one (Font Manager).
    if (ui::BeginCombo("Source", current.c_str())) {
        if (ImGui::Selectable(defaultLabel.c_str(), fontPath.empty())) {
            fontPath.clear();
            changed = true;
        }
        // fonts shipped inside the project (res/fonts)
        const std::filesystem::path fontsDir =
            std::filesystem::path(project_.dir) / "res" / "fonts";
        std::error_code ec;
        if (std::filesystem::exists(fontsDir, ec)) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(fontsDir, ec)) {
                std::string ext = entry.path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".ttf" && ext != ".otf") continue;
                const std::string rel =
                    "res/fonts/" + entry.path().filename().string();
                if (ImGui::Selectable(
                        (entry.path().filename().string() + "  [project]").c_str(),
                        fontPath == rel)) {
                    fontPath = rel;
                    changed = true;
                }
            }
        }
        // stock system fonts, stored as a bare file name and resolved through
        // platform::systemFontPath at bake time. Existence-checked, so the list
        // only ever offers what this machine actually has.
        for (const platform::SystemFont& sf : platform::systemFonts()) {
            if (platform::systemFontPath(sf.file).empty()) continue;
            if (ImGui::Selectable(sf.label, fontPath == sf.file)) {
                fontPath = sf.file;
                changed = true;
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Import TTF into the project...")) {
            const std::string src = pickTtfFile();
            if (!src.empty()) {
                const std::filesystem::path srcPath(src);
                const std::string fileName =
                    sanitizeAssetName(srcPath.filename().string());
                std::filesystem::create_directories(fontsDir, ec);
                std::filesystem::copy_file(
                    srcPath, fontsDir / fileName,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    fontPath = "res/fonts/" + fileName;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    const char* tip = "Fonts imported into the project (res/fonts) travel\n"
                      "with it; Windows fonts depend on this machine.\n"
                      "Only the rasterized pixels ever reach the PS2 -\n"
                      "the TTF itself never ships.";
    if (!ui::RowHelp(tip)) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }
    return changed;
}

std::string App::ensureFontForPath(const std::string& relPath) {
    for (const GameFont& f : project_.fonts)
        if (f.fontPath == relPath) return f.name;
    std::string base = std::filesystem::path(relPath).stem().string();
    if (base.empty()) base = "font";
    std::string name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const GameFont& f : project_.fonts) taken |= (f.name == name);
        if (!taken) break;
        name = base + "-" + std::to_string(n);
    }
    GameFont f;
    f.name = name;
    f.fontPath = relPath;
    project_.fonts.push_back(f);
    return name;
}

void App::renameFont(int index, const std::string& newName) {
    if (index < 0 || index >= (int)project_.fonts.size()) return;
    const std::string oldName = project_.fonts[index].name;
    if (newName == oldName) return;
    project_.fonts[index].name = newName;

    // References store the name, so follow it everywhere. An empty reference
    // means "the default entry" and must stay empty - rewriting it would pin
    // the text to a name and break that fallback.
    auto follow = [&](std::string& ref) {
        if (ref == oldName) ref = newName;
    };
    for (HudText& t : project_.hudTexts) follow(t.font);
    for (LoadingScreenDef& ls : project_.loadingScreens)
        for (HudText& t : ls.texts) follow(t.font);
    for (GameMenu& m : project_.menus) follow(m.font);
    for (SceneData& sc : project_.scenes)
        for (SceneObject& o : sc.objects)
            for (FlowNode& fn : o.flowGraph.nodes) {
                const FlowNodeType* ft = flowNodeType(fn.type);
                if (ft && ft->strKind == FlowParamKind::FontName) follow(fn.str);
            }
}

// Input actions are referenced by name (preset bindings, On Action nodes, menu
// rebind rows), so a rename has to follow into all three or the reference
// silently stops resolving.
void App::renameInputAction(int index, const std::string& newName) {
    if (index < 0 || index >= (int)project_.input.actions.size()) return;
    const std::string oldName = project_.input.actions[index].name;
    if (newName == oldName || newName.empty()) return;
    project_.input.actions[index].name = newName;
    for (InputPreset& pr : project_.input.presets)
        for (InputBinding& b : pr.bindings)
            if (b.action == oldName) b.action = newName;
    for (GameMenu& m : project_.menus)
        for (MenuEntry& e : m.entries)
            if (e.bindAction == oldName) e.bindAction = newName;
    for (SceneData& sc : project_.scenes)
        for (SceneObject& o : sc.objects)
            for (FlowNode& fn : o.flowGraph.nodes) {
                const FlowNodeType* ft = flowNodeType(fn.type);
                if (ft && ft->strKind == FlowParamKind::InputActionName &&
                    fn.str == oldName)
                    fn.str = newName;
            }
}

// Presets are referenced by name from the Set Input Preset node only.
void App::renameInputPreset(int index, const std::string& newName) {
    if (index < 0 || index >= (int)project_.input.presets.size()) return;
    const std::string oldName = project_.input.presets[index].name;
    if (newName == oldName || newName.empty()) return;
    project_.input.presets[index].name = newName;
    for (SceneData& sc : project_.scenes)
        for (SceneObject& o : sc.objects)
            for (FlowNode& fn : o.flowGraph.nodes)
                if (fn.type == "SetInputPreset" && fn.str == oldName)
                    fn.str = newName;
}

// The `{{ }}` button that sits next to every text field a placeholder can go
// into. It is the legend for the syntax (docs/text-icons.md) - the tokens this
// project actually understands, each with the glyph it draws - and inserting one
// beats remembering how it was spelled.
bool App::textTokenPicker(const char* id, std::string& text) {
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::SmallButton("{{ }}")) ImGui::OpenPopup("##tokens");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Insert a button glyph. An ACTION token follows whatever it is\n"
            "bound to; an ICON token pins one specific button or image.\n"
            "See docs/text-icons.md.");

    if (ImGui::BeginPopup("##tokens")) {
        const float sz = scaled(20.0f);
        // The row for one token: its glyph, the token itself, and a note.
        auto row = [&](const std::string& token, const std::string& iconName,
                       const char* note) {
            const HudTexture* t = nullptr;
            for (const TextIcon& ic : project_.textIcons)
                if (ic.name == iconName && !ic.path.empty()) {
                    t = hudTexture(ic.path);
                    break;
                }
            if (!t) t = builtinIconTexture(iconName);
            if (t)
                ImGui::Image((ImTextureID)(intptr_t)t->tex, ImVec2(sz, sz));
            else
                ImGui::Dummy(ImVec2(sz, sz));
            ImGui::SameLine();
            if (ImGui::Selectable(token.c_str(), false, 0,
                                  ImVec2(scaled(150.0f), 0.0f))) {
                text += token;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (note && *note) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", note);
            }
        };

        ImGui::TextDisabled("Actions - follow the current binding");
        ImGui::Separator();
        for (const InputAction& a : project_.input.actions) {
            const InputBinding b = project_.input.resolve(a.name);
            row("{{" + a.name + "}}", textIconNameForPad(b.pad),
                b.pad.empty() ? "(unbound)" : b.pad.c_str());
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Icons - one fixed button or image");
        ImGui::Separator();
        for (const TextIcon& ic : project_.textIcons)
            row("{{" + ic.name + "}}", ic.name, nullptr);
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// Tools > UI Editor > Button icons. The registry of `{{name}}` placeholders
// (docs/text-icons.md): every text in the project - HUD texts, menu titles and
// labels, loading screens, Display Text nodes - substitutes them for an image.
// The pad-button set is seeded and its PNGs are generated at build, so
// overriding one is just pointing it at your own file.
void App::drawTextIconsModal() {
    ImGui::SetNextWindowSize(ImVec2(scaled(600.0f), scaled(440.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::BeginPopupModal("Button icons")) return;

    bool changed = false;
    // Icons ride in the Hud section (project.cpp writeHudSection), so the same
    // belt-and-braces comparison the UI Editor uses covers this popup too.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Hud);
    ImGui::TextWrapped(
        "Write {{name}} in any text to splice an icon in. {{action:jump}} draws "
        "whatever that action is bound to right now (Tools > Input Map), so a "
        "prompt stays correct after the player rebinds. An unknown name stays "
        "on screen as literal text.");
    ImGui::Spacing();

    ImGui::BeginChild("##iconlist", ImVec2(0, -scaled(72.0f)),
                      ImGuiChildFlags_Borders);
    const float row = scaled(28.0f);
    for (int i = 0; i < (int)project_.textIcons.size(); ++i) {
        TextIcon& ic = project_.textIcons[i];
        ImGui::PushID(i);

        // Preview: the project's PNG when it exists, otherwise the built-in
        // drawing - the PNGs are only generated at the first build, so without
        // the fallback a fresh project would show an empty column.
        const HudTexture* prev =
            ic.path.empty() ? nullptr : hudTexture(ic.path);
        const bool fromFile = prev != nullptr;
        if (!prev) prev = builtinIconTexture(ic.name);
        if (prev)
            ImGui::Image((ImTextureID)(intptr_t)prev->tex, ImVec2(row, row));
        else
            ImGui::Dummy(ImVec2(row, row));
        if (ImGui::IsItemHovered()) {
            // Bigger, on the dark tooltip background - the row thumbnail is
            // 28px and these are 48px drawings.
            ImGui::BeginTooltip();
            ImGui::Image((ImTextureID)(intptr_t)prev->tex,
                         ImVec2(scaled(96.0f), scaled(96.0f)));
            ImGui::TextUnformatted(fromFile ? ic.path.c_str()
                                            : "built-in drawing (no PNG yet)");
            ImGui::EndTooltip();
        }
        ImGui::SameLine();

        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s", ic.name.c_str());
            ImGui::SetNextItemWidth(scaled(110.0f));
            if (ImGui::InputText("##name", buf, sizeof(buf))) {
                // The name IS the placeholder, so it must stay unique - a clash
                // would make {{x}} ambiguous.
                std::string want = buf;
                bool clash = want.empty();
                for (int k = 0; k < (int)project_.textIcons.size(); ++k)
                    if (k != i && project_.textIcons[k].name == want) clash = true;
                if (!clash) ic.name = want;
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("{{%s}}", ic.name.c_str());
        ImGui::SameLine();

        ImGui::SetNextItemWidth(scaled(70.0f));
        if (ImGui::DragFloat("##scale", &ic.scale, 0.02f, 0.2f, 4.0f, "%.2fx"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drawn height relative to the text it sits in.");
        ImGui::SameLine();

        if (ImGui::SmallButton("PNG...")) {
            const std::string src = pickPngFile();
            if (!src.empty()) {
                const std::filesystem::path srcPath(src);
                const std::string fileName =
                    sanitizeAssetName(srcPath.filename().string());
                const std::filesystem::path destDir =
                    std::filesystem::path(project_.dir) / "res" / "hud";
                std::error_code ec;
                std::filesystem::create_directories(destDir, ec);
                std::filesystem::copy_file(
                    srcPath, destDir / fileName,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    ic.path = "res/hud/" + fileName;
                    hudTexCache_.erase(ic.path);
                    menubake::clearIconImageCache();
                    changed = true;
                }
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", ic.path.empty() ? "(no image yet)"
                                                    : ic.path.c_str());
        ImGui::SameLine();

        // Per-icon reset: only a built-in name has a default to go back to.
        const std::string defPath = "res/hud/" + menubake::iconFileName(ic.name);
        const bool isBuiltin = builtinIconTexture(ic.name) != nullptr;
        ImGui::BeginDisabled(!isBuiltin);
        if (ImGui::SmallButton("Default")) {
            restoreDefaultTextIcon(ic);
            changed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                isBuiltin
                    ? "Back to the built-in glyph: resets the scale, points the\n"
                      "icon at %s and deletes that file so the\n"
                      "next build redraws it."
                    : "Only the built-in pad-button icons have a default to\n"
                      "restore - this one is yours.",
                defPath.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            project_.textIcons.erase(project_.textIcons.begin() + i);
            menubake::clearIconImageCache();
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (ImGui::Button("+ Add icon")) {
        TextIcon ic;
        std::string base = "icon";
        ic.name = base;
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const TextIcon& e : project_.textIcons) taken |= (e.name == ic.name);
            if (!taken) break;
            ic.name = base + "-" + std::to_string(n);
        }
        project_.textIcons.push_back(std::move(ic));
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restore pad buttons")) {
        // Only adds what is missing (project::ensureTextIcons).
        project::ensureTextIcons(project_);
        menubake::clearIconImageCache();
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-adds any missing pad-button icon. Never touches\n"
                          "an icon you already have.");
    ImGui::SameLine();
    if (ImGui::Button("Regenerate built-in PNGs")) {
        // Deletes the generated files so the next build redraws them - the way
        // back after overwriting one by hand.
        std::error_code ec;
        for (const TextIcon& ic : project_.textIcons) {
            if (ic.path != "res/hud/" + menubake::iconFileName(ic.name)) continue;
            std::filesystem::remove(
                std::filesystem::path(project_.dir) / ic.path, ec);
            hudTexCache_.erase(ic.path);
        }
        menubake::clearIconImageCache();
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Deletes the generated icon-*.png files; the next\n"
                          "build redraws them. Use it to undo a hand-edit.");
    ImGui::SameLine();
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

    // Not on the undo stack (like the rest of the UI Editor), but still unsaved
    // work - marked, not an immediate write.
    if (changed || project::sectionJson(project_, project::Section::Hud) != beforeSection)
        commitChange();
    ImGui::EndPopup();
}

// One font in the Font Manager's list: an "Aa" specimen tile rasterized in
// that font, the name, and a dim second line. The label is the ID and the
// --ui-script name. Returns clicked.
static bool fontListRow(const char* label, const char* sub, bool selected,
                        ImTextureID aa, ImVec2 aaSize) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = fs * 2.9f;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(w > 1 ? w : 1, h));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding + 2.0f;
    const ImVec2 q(p.x + w, p.y + h);
    if (selected) {
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.14f)), r);
    } else if (t > 0.0f) {
        dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.8f * t)), r);
    }
    const float tile = h - fs * 0.8f;
    const ImVec2 t0(p.x + fs * 0.4f, p.y + (h - tile) * 0.5f);
    const ImVec2 t1(t0.x + tile, t0.y + tile);
    dl->AddRectFilled(t0, t1, ImGui::GetColorU32(sem.ground), r);
    dl->AddRect(t0, t1, ImGui::GetColorU32(sem.border), r);
    if (aa && aaSize.x > 0 && aaSize.y > 0) {
        float s = (tile * 0.7f) / (aaSize.x > aaSize.y ? aaSize.x : aaSize.y);
        const ImVec2 sz(aaSize.x * s, aaSize.y * s);
        const ImVec2 a0(t0.x + (tile - sz.x) * 0.5f, t0.y + (tile - sz.y) * 0.5f);
        dl->AddImage(aa, a0, ImVec2(a0.x + sz.x, a0.y + sz.y));
    }
    const float x = t1.x + fs * 0.6f;
    dl->PushClipRect(ImVec2(x, p.y), ImVec2(q.x - fs * 0.3f, q.y), true);
    {
        ui::FontScope bold(ui::boldFont());
        dl->AddText(ImVec2(x, p.y + h * 0.5f - fs * 1.05f), ImGui::GetColorU32(sem.text), label,
                    ImGui::FindRenderedTextEnd(label));
    }
    if (sub && *sub)
        dl->AddText(ImGui::GetFont(), fs * 0.86f, ImVec2(x, p.y + h * 0.5f + fs * 0.1f),
                    ImGui::GetColorU32(sem.textDim), sub);
    dl->PopClipRect();
    return pressed;
}

// Tools > Font Manager: the project's typefaces. Every text (HUD texts, menus,
// loading screens, Display Text nodes) names one of these, so restyling a
// project is an edit here rather than a hunt through every text.
//
// Layout (docs/font-manager.md): fonts | specimen (the hero) + the atlas that
// ships | inspector. fonts[0] is the fallback for unset references, which is
// why the last entry can never be deleted.
void App::drawFontManagerWindow() {
    if (!showFontManager_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1100.0f), scaled(720.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Font Manager", &showFontManager_)) {
        ImGui::End();
        return;
    }
    if (project_.fonts.empty()) project_.fonts.push_back(GameFont{});
    if (fontSel_ < 0 || fontSel_ >= (int)project_.fonts.size()) fontSel_ = 0;
    const theme::Semantics& sem = theme::semantics();

    bool changed = false;
    // Belt and braces: fonts ride in the Hud section (writeHudSection), and a
    // rename retargets references in the SCENES, which undo covers on its own.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Hud);

    // Fonts dropped into res/fonts show up in Source as [project]; until
    // there is one, the list says how to get one in.
    bool hasProjectTtf = false;
    {
        std::error_code ec;
        const std::filesystem::path fontsDir =
            std::filesystem::path(project_.dir) / "res" / "fonts";
        if (std::filesystem::exists(fontsDir, ec))
            for (const auto& e : std::filesystem::directory_iterator(fontsDir, ec)) {
                std::string ext = e.path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext == ".ttf" || ext == ".otf") hasProjectTtf = true;
            }
    }
    auto sourceLabel = [&](const GameFont& gf) {
        if (gf.fontPath.empty()) return std::string(platform::defaultFontLabel());
        for (const platform::SystemFont& sf : platform::systemFonts())
            if (gf.fontPath == sf.file) return std::string(sf.label);
        return std::filesystem::path(gf.fontPath).filename().string();
    };

    const float listW = scaled(220.0f), inspW = scaled(310.0f);
    const float fullH = ImGui::GetContentRegionAvail().y;
    auto vline = [&](float x, float y0, float y1) {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, y0), ImVec2(x, y1),
                                            ImGui::GetColorU32(sem.border));
    };

    // ---- left: the fonts ----------------------------------------------------
    ImGui::BeginChild("fontlist", ImVec2(listW, fullH), ImGuiChildFlags_AlwaysUseWindowPadding);
    for (size_t i = 0; i < project_.fonts.size(); ++i) {
        const GameFont& gf = project_.fonts[i];
        ImGui::PushID((int)i);
        HudText aa;
        aa.name = "##fontaa" + std::to_string(i);
        aa.text = "Aa";
        aa.font = gf.name;
        aa.size = 20;
        aa.shadow = false;
        const HudTexture* aaTex = hudTextTexture(aa);
        std::string sub = sourceLabel(gf);
        if (i == 0) sub += " \xC2\xB7 project default";
        if (fontListRow(gf.name.c_str(), sub.c_str(), fontSel_ == (int)i,
                        aaTex ? (ImTextureID)(intptr_t)aaTex->tex : (ImTextureID)0,
                        aaTex ? ImVec2((float)aaTex->w, (float)aaTex->h) : ImVec2(0, 0)))
            fontSel_ = (int)i;
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ui::GhostButton("+ Add font")) {
        GameFont nf;
        std::string base = "font";
        std::string name = base;
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const GameFont& e : project_.fonts) taken |= (e.name == name);
            if (!taken) break;
            name = base + "-" + std::to_string(n);
        }
        nf.name = name;
        project_.fonts.push_back(nf);
        fontSel_ = (int)project_.fonts.size() - 1;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A new entry on the default face - pick its Source on the right.");
    if (!hasProjectTtf) {
        // How a custom face gets in, said where you look for one.
        const float cardH = ImGui::GetTextLineHeightWithSpacing() * 4.2f;
        const float y = ImGui::GetWindowHeight() - cardH - ImGui::GetStyle().WindowPadding.y;
        if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
        if (ui::BeginCard("##fontshint", ImVec2(0, 0))) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextWrapped("Drop a .ttf or .otf into res/fonts/ - it shows up in "
                               "Source as [project].");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        }
        ui::EndCard();
    }
    ImGui::EndChild();
    {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        vline(b.x, a.y, b.y);
    }
    ImGui::SameLine(0.0f, 0.0f);

    GameFont& f = project_.fonts[fontSel_];

    // The baked atlas - what a Display Text font ships - as a texture, so it
    // can be SHOWN, not just measured. Glyphs bake white; the palette depth is
    // simulated on the alpha (16 levels for 4-bit), which is what the CLUT
    // does to white glyphs. Re-baked only when a bake-affecting field changes.
    {
        const std::string key = project_.dir + "\x1f" + f.fontPath + "\x1f" +
                                std::to_string(f.atlasSize) + "\x1f" + f.quant;
        if (key != fontAtlasTexKey_) {
            fontAtlasTexKey_ = key;
            std::vector<unsigned char> rgba;
            menubake::AtlasLayout lay;
            if (menubake::bakeAtlasRGBA(f, project_, rgba, lay) &&
                rgba.size() == (size_t)lay.texW * lay.texH * 4) {
                if (f.quant == "4bit")
                    for (size_t i = 3; i < rgba.size(); i += 4)
                        rgba[i] = (unsigned char)(((rgba[i] + 8) / 17) * 17);
                if (!fontAtlasTex_) glGenTextures(1, &fontAtlasTex_);
                glBindTexture(GL_TEXTURE_2D, fontAtlasTex_);
                glUploadTexRgba(lay.texW, lay.texH, rgba.data());
                fontAtlasLayout_ = lay;
                const int bpp = f.quant == "none" ? 32 : f.quant == "8bit" ? 8 : 4;
                // +8 KB per allocation (the engine's alignment tax), +CLUT for
                // the palettized modes - the same sum the build reports.
                fontAtlasKb_ = (lay.texW * lay.texH * bpp / 8 + 8192 +
                                (bpp == 32 ? 0 : 8192)) / 1024;
            } else {
                fontAtlasLayout_ = menubake::AtlasLayout();
                fontAtlasLayout_.glyphs.clear();
                fontAtlasKb_ = 0;
            }
        }
    }
    const menubake::AtlasLayout& lay = fontAtlasLayout_;
    const bool atlasOk = fontAtlasTex_ && !lay.glyphs.empty();

    const std::vector<int> atlasFonts = project_.atlasFontIndices();
    const bool usedDynamically =
        std::find(atlasFonts.begin(), atlasFonts.end(), fontSel_) != atlasFonts.end();

    // ---- centre: the specimen (hero) and the atlas that ships -------------
    const float midW = ImGui::GetContentRegionAvail().x - inspW;
    ImGui::BeginChild("fontpreview", ImVec2(midW > 1 ? midW : 1, fullH),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        const float atlasH = scaled(210.0f);
        const float gap = ImGui::GetStyle().ItemSpacing.y;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float specH = avail.y - atlasH - gap > scaled(160.0f) ? avail.y - atlasH - gap
                                                                  : scaled(160.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 s0 = ImGui::GetCursorScreenPos();
        const ImVec2 s1(s0.x + avail.x, s0.y + specH);
        const float rr = ImGui::GetStyle().FrameRounding + 4.0f;
        // The backdrop is game content standing in for a scene: a dusk
        // gradient from the theme's accent into the ground, or the black of
        // an empty PS2 frame.
        if (fontPreviewMode_ == 1) {
            dl->AddRectFilled(s0, s1, IM_COL32(0, 0, 0, 255), rr);
        } else {
            const ImVec4 top(sem.accentMuted.x * 0.55f + sem.ground.x * 0.45f,
                             sem.accentMuted.y * 0.55f + sem.ground.y * 0.45f,
                             sem.accentMuted.z * 0.55f + sem.ground.z * 0.45f, 1.0f);
            const ImU32 cTop = ImGui::GetColorU32(top), cBot = ImGui::GetColorU32(sem.ground);
            dl->AddRectFilled(s0, s1, cBot, rr);
            dl->AddRectFilledMultiColor(ImVec2(s0.x, s0.y + rr), ImVec2(s1.x, s1.y - rr), cTop, cTop,
                                        cBot, cBot);
            dl->AddRectFilled(s0, ImVec2(s1.x, s0.y + rr + 1.0f), cTop, rr,
                              ImDrawFlags_RoundCornersTop);
        }
        dl->AddRect(s0, s1, ImGui::GetColorU32(sem.border), rr);

        // Display scale: the specimen sizes are PS2 pixels (a 640-wide frame),
        // blown up so the panel reads like the TV would.
        const float k = std::max(1.0f, avail.x / 560.0f);
        const char* title = project_.name.empty() ? "Observatory" : project_.name.c_str();
        const char* small = "Sample 0123 gjpq";
        const int sizes[3] = {36, 20, 14};
        const char* lines[3] = {title, fontSample_[0] ? fontSample_ : " ", small};
        const ImU32 tint = ImGui::GetColorU32(ImVec4(f.color[0], f.color[1], f.color[2], 1.0f));
        int missing = 0;
        // As PS2: lay the string down glyph by glyph from the baked atlas,
        // scaled from the atlas size to the text size - the runtime's path,
        // so a small atlas blown up to a title shows its softness here.
        auto atlasText = [&](ImVec2 pos, const char* s, float px, bool draw) {
            const float sc = px / (float)(lay.baseSize > 0 ? lay.baseSize : 16);
            float pen = 0.0f;
            for (const unsigned char* c = (const unsigned char*)s; *c; ++c) {
                if (*c < menubake::kAtlasFirstChar || *c > menubake::kAtlasLastChar) {
                    if (draw && (*c >= 0xC0 || *c < 0x80)) ++missing;  // code points
                    continue;
                }
                const menubake::AtlasGlyph& g = lay.glyphs[(size_t)(*c - menubake::kAtlasFirstChar)];
                if (draw && g.w > 0 && g.h > 0) {
                    const ImVec2 a(pos.x + (pen + g.xoff) * sc * k, pos.y + g.yoff * sc * k);
                    const ImVec2 b(a.x + g.w * sc * k, a.y + g.h * sc * k);
                    const ImVec2 uv0((float)g.u / lay.texW, (float)g.v / lay.texH);
                    const ImVec2 uv1((float)(g.u + g.w) / lay.texW, (float)(g.v + g.h) / lay.texH);
                    if (f.shadow)
                        dl->AddImage((ImTextureID)(intptr_t)fontAtlasTex_, ImVec2(a.x + k, a.y + k),
                                     ImVec2(b.x + k, b.y + k), uv0, uv1, IM_COL32(0, 0, 0, 170));
                    dl->AddImage((ImTextureID)(intptr_t)fontAtlasTex_, a, b, uv0, uv1, tint);
                }
                pen += g.advance;
            }
            return pen * sc * k;
        };
        float blockH = 0.0f;
        for (int i = 0; i < 3; ++i) blockH += sizes[i] * k * (i ? 1.45f : 1.6f);
        float y = s0.y + (specH - blockH) * 0.5f;
        dl->PushClipRect(s0, s1, true);
        // Lines are centred: the static bake centres its text in a pow2
        // canvas, so the sprite's centre is the only point that is known.
        const float cx = s0.x + avail.x * 0.5f;
        for (int i = 0; i < 3; ++i) {
            const float lineH = sizes[i] * k;
            const float cy = y + lineH * 0.6f;
            if (fontPreviewMode_ == 2) {
                if (atlasOk) {
                    const float tw = atlasText(ImVec2(0, 0), lines[i], (float)sizes[i], false);
                    atlasText(ImVec2(cx - tw * 0.5f, cy - lineH * 0.5f), lines[i],
                              (float)sizes[i], true);
                }
            } else {
                HudText t;
                t.name = "##fontspec" + std::to_string(i);
                t.text = lines[i];
                t.font = f.name;
                t.size = sizes[i];
                t.shadow = f.shadow;
                for (int c = 0; c < 3; ++c) t.color[c] = f.color[c];
                if (const HudTexture* tex = hudTextTexture(t)) {
                    const ImVec2 at(cx - tex->w * k * 0.5f, cy - tex->h * k * 0.5f);
                    dl->AddImage((ImTextureID)(intptr_t)tex->tex, at,
                                 ImVec2(at.x + tex->w * k, at.y + tex->h * k));
                }
            }
            y += sizes[i] * k * (i ? 1.45f : 1.6f);
        }
        dl->PopClipRect();

        // View pill, top-left on the specimen.
        ImGui::SetCursorScreenPos(ImVec2(s0.x + scaled(12.0f), s0.y + scaled(12.0f)));
        static const char* kModes[] = {"On the scene", "On black", "As PS2"};
        ui::Segmented("##fontview", kModes, 3, &fontPreviewMode_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("As PS2 draws every glyph from the baked atlas at its\n"
                              "palette depth - what a Display Text node shows.");
        if (fontPreviewMode_ == 2) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s atlas", f.quant == "none" ? "full-colour"
                                            : f.quant == "8bit" ? "8-bit" : "4-bit");
            if (missing > 0) {
                ImGui::SameLine();
                ui::Badge((std::to_string(missing) + " not in atlas").c_str(), 1);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("The atlas holds printable ASCII only - other\n"
                                      "characters are dropped at draw time.");
            }
        }
        // The editable sample line, inside the specimen's bottom edge.
        const float fh = ImGui::GetFrameHeight();
        ImGui::SetCursorScreenPos(ImVec2(s0.x + scaled(10.0f), s1.y - fh - scaled(10.0f)));
        ImGui::SetNextItemWidth(avail.x - scaled(20.0f));
        ImGui::InputTextWithHint("##fontsample", "Type a line to try", fontSample_,
                                 sizeof fontSample_);
        ImGui::SetCursorScreenPos(ImVec2(s0.x, s1.y + gap));

        // The atlas card: the real image beside what it costs.
        const float imgH = atlasH;
        float imgW = 0.0f;
        if (atlasOk) {
            imgW = imgH * (float)lay.texW / (float)lay.texH;
            if (imgW > avail.x * 0.4f) imgW = avail.x * 0.4f;
            const ImVec2 a0 = ImGui::GetCursorScreenPos();
            const ImVec2 a1(a0.x + imgW, a0.y + imgH);
            dl->AddRectFilled(a0, a1, IM_COL32(0, 0, 0, 255), rr);
            // Fit the texture inside the tile, aspect kept.
            const float fit = std::min((imgW - scaled(12.0f)) / lay.texW,
                                       (imgH - scaled(12.0f)) / lay.texH);
            const ImVec2 sz(lay.texW * fit, lay.texH * fit);
            const ImVec2 i0(a0.x + (imgW - sz.x) * 0.5f, a0.y + (imgH - sz.y) * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)fontAtlasTex_, i0, ImVec2(i0.x + sz.x, i0.y + sz.y));
            dl->AddRect(a0, a1, ImGui::GetColorU32(sem.border), rr);
            ImGui::Dummy(ImVec2(imgW, imgH));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The glyph atlas exactly as baked (%dx%d).", lay.texW, lay.texH);
            ImGui::SameLine();
        }
        if (ui::BeginCard("##fontatlascard", ImVec2(0, imgH))) {
            ui::Caption("Glyph atlas - what ships");
            if (!atlasOk) {
                ui::StatusLine(2, "No usable TTF found for this font.");
            } else {
                {
                    ui::FontScope mono(ui::monoFont());
                    ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.6f);
                    ImGui::Text("~%d KB", fontAtlasKb_);
                    ImGui::PopFont();
                }
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(usedDynamically ? "GS VRAM while text is shown"
                                                    : "if a Display Text node used it");
                // ~1.33 MB of texture VRAM once the frame and z buffers are
                // placed; a resident atlas never leaves it.
                const float frac = fontAtlasKb_ / 1362.0f;
                const ImVec2 b0 = ImGui::GetCursorScreenPos();
                const float bw = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("of ~1.33 MB").x -
                                 scaled(10.0f);
                const float bh = scaled(5.0f);
                ImDrawList* cdl = ImGui::GetWindowDrawList();
                const float by = b0.y + ImGui::GetTextLineHeight() * 0.5f - bh * 0.5f;
                cdl->AddRectFilled(ImVec2(b0.x, by), ImVec2(b0.x + bw, by + bh),
                                   ImGui::GetColorU32(sem.field), bh);
                cdl->AddRectFilled(ImVec2(b0.x, by),
                                   ImVec2(b0.x + bw * std::clamp(frac, 0.02f, 1.0f), by + bh),
                                   ImGui::GetColorU32(frac > 0.25f ? sem.warn : sem.ok), bh);
                ImGui::Dummy(ImVec2(bw, ImGui::GetTextLineHeight()));
                ImGui::SameLine();
                ImGui::TextDisabled("of ~1.33 MB");
                {
                    ui::FontScope mono(ui::monoFont());
                    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextWrapped("%dx%d \xC2\xB7 %d glyphs (ASCII) \xC2\xB7 baked white, tinted at runtime",
                                       lay.texW, lay.texH, menubake::kAtlasCharCount);
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();
                }
                if (lay.clipped)
                    ui::StatusLine(1, "Glyphs past the 512 px cap were dropped - lower the glyph size.");
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                // The static-vs-Display-Text rule, stated once.
                ImGui::TextWrapped(usedDynamically
                                       ? "Display Text nodes draw with this font, so its atlas is baked. "
                                         "Static texts rasterize straight from the TTF at build."
                                       : "No Display Text node uses this font, so no atlas ships. "
                                         "Static texts rasterize straight from the TTF at build.");
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
        }
        ui::EndCard();
    }
    ImGui::EndChild();
    {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        vline(b.x, a.y, b.y);
    }
    ImGui::SameLine(0.0f, 0.0f);

    // ---- right: the inspector -----------------------------------------------
    ImGui::BeginChild("fontinspector", ImVec2(0, fullH), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        const float footH = ImGui::GetFrameHeightWithSpacing() + scaled(14.0f);
        ImGui::BeginChild("##fontinspbody", ImVec2(0, -footH));
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.2f);
            ImGui::TextUnformatted(f.name.c_str());
            ImGui::PopFont();
        }
        if (fontSel_ == 0) {
            ImGui::SameLine();
            const float bw = ImGui::CalcTextSize("project default").x + scaled(14.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - bw);
            ui::Badge("project default", 3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Every text that names no font, or a deleted one,\nuses this entry.");
        }
        ImGui::Spacing();
        const float capW = std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(84.0f),
                                      scaled(110.0f));
        auto helpUnder = [&](const char* text) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + capW);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.9f);
            ImGui::TextWrapped("%s", text);
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        };
        {
            ui::InspectorRows rows(capW, capW);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", f.name.c_str());
            if (ui::InputText("Name", buf, sizeof(buf))) {
                std::string want = buf;
                // Names are the reference key, so they must stay unique and
                // non-empty; a clash would silently redirect other texts here.
                bool clash = want.empty();
                for (size_t i = 0; i < project_.fonts.size(); ++i)
                    if ((int)i != fontSel_ && project_.fonts[i].name == want) clash = true;
                if (!clash) renameFont(fontSel_, want);
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= fontSourceCombo(f.fontPath);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ui::Caption("Display Text look", "runtime text only");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Static texts keep their own colour and shadow;\n"
                                  "these apply to text a Display Text node draws.");
            changed |= ui::ColorHex("Colour##font", f.color);
            if (ui::Switch("Drop shadow (A dark copy under each glyph for contrast)##font", &f.shadow))
                changed = true;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ui::Caption("Atlas");
            if (ui::SliderInt("Glyph size", &f.atlasSize, 8, 48, "%d px"))
                f.atlasSize = std::clamp(f.atlasSize, 8, 48);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::RowHelp("Height the glyphs are rasterized at. Display Text scales\n"
                        "from this, so it trades sharpness against VRAM.");
            helpUnder("Sharpness vs VRAM - not the on-screen size");
        }
        {
            static const char* kQuant[] = {"4bit", "8bit", "none"};
            static const char* kQuantLbl[] = {"4-bit", "8-bit", "Full"};
            int qi = f.quant == "8bit" ? 1 : f.quant == "none" ? 2 : 0;
            const float rowX = ImGui::GetCursorPosX();
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::TextUnformatted("Colours");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetCursorPosX(rowX + capW);
            if (ui::Segmented("##fontquant", kQuantLbl, 3, &qi, -1.0f)) {
                f.quant = kQuant[qi];
                changed = true;
            }
            helpUnder(qi == 0   ? "16 colours is plenty for white glyphs"
                      : qi == 1 ? "256 levels of edge - twice the 4-bit VRAM"
                                : "Full colour - ~8x the VRAM of 4-bit");
        }
        ImGui::EndChild();

        // Footer: the one destructive action, with its reason when refused.
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, scaled(4.0f)));
        const bool canDelete = project_.fonts.size() > 1;
        const float dw = ImGui::CalcTextSize("Delete").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        if (!canDelete) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("The project default cannot be deleted");
            ImGui::SameLine();
        }
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dw);
        if (ui::DangerButton("Delete##font", dw, canDelete)) {
            // Texts naming it fall back to the default entry (Project::findFont),
            // so deleting never breaks a bake.
            project_.fonts.erase(project_.fonts.begin() + fontSel_);
            if (fontSel_ >= (int)project_.fonts.size())
                fontSel_ = (int)project_.fonts.size() - 1;
            changed = true;
        }
        if (canDelete && ImGui::IsItemHovered())
            ImGui::SetTooltip(fontSel_ == 0 ? "Texts with no font of their own move to the next entry."
                                            : "Texts using it fall back to the project default.");
    }
    ImGui::EndChild();

    if (changed || project::sectionJson(project_, project::Section::Hud) != beforeSection)
        commitChange();
    ImGui::End();
}

// One action's bindings inside one preset: a pad-button, a keyboard-key and a
// mouse-button picker, each with a "(none)" entry - an action may be bound to
// any combination, and all of them fire it.
// --- Tools > Input Map --------------------------------------------------------
namespace {
// Shared with the UI Editor / Loading Screens (defined with them below).
ImU32 tint(const ImVec4& c, float a = 1.0f);
void listHead(const char* left, const char* right = nullptr);

// The play context an action's role belongs to: two actions only clash when
// they share a button inside one context (Cross is Jump on foot, Fly up in
// noclip and Confirm in menus - three modes, no clash). Roles are fixed, so
// this is a static map. Custom actions (no role) are their own group.
enum ImContext { CtxFoot = 0, CtxNoclip, CtxMenus, CtxVehicle, CtxCustom, CtxCount };

int roleContext(int role) {
    switch (role) {
        case InputAction::RoleJump:
        case InputAction::RoleUse:
        case InputAction::RoleThrow:
        case InputAction::RoleSprint:
        case InputAction::RoleMoveForward:
        case InputAction::RoleMoveBack:
        case InputAction::RoleMoveLeft:
        case InputAction::RoleMoveRight: return CtxFoot;
        case InputAction::RoleFlyUp:
        case InputAction::RoleFlyDown: return CtxNoclip;
        case InputAction::RoleConfirm:
        case InputAction::RoleBack:
        case InputAction::RoleMenu:
        case InputAction::RoleAlt:
        case InputAction::RoleMenuUp:
        case InputAction::RoleMenuDown:
        case InputAction::RoleMenuLeft:
        case InputAction::RoleMenuRight: return CtxMenus;
        case InputAction::RoleVehThrottle:
        case InputAction::RoleVehBrake:
        case InputAction::RoleVehHandbrake:
        case InputAction::RoleVehNitrous:
        case InputAction::RoleVehCamera:
        case InputAction::RoleVehRearView: return CtxVehicle;
        default: return CtxCustom;
    }
}

const char* contextName(int c) {
    static const char* n[CtxCount] = {"On foot", "Noclip", "Menus", "Vehicle", "Custom"};
    return n[c];
}

bool isMoveRole(int role) {
    return role >= InputAction::RoleMoveForward && role <= InputAction::RoleMoveRight;
}

// The face buttons keep their REAL pad colours where the name stands for the
// button (theme::padColors(), the controller drawing's tokens).
ImVec4 padColour(const std::string& pad) {
    const theme::PadColors& p = theme::padColors();
    if (pad == "Cross") return p.cross;
    if (pad == "Circle") return p.circle;
    if (pad == "Square") return p.square;
    if (pad == "Triangle") return p.triangle;
    return theme::semantics().text;
}

// A keycap in a matrix cell: the binding on a raised pill, or a dim dash.
void keycap(ImDrawList* dl, ImVec2 p, float h, const char* text, ImVec4 col, bool none, bool warn) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize() * 0.9f;
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    if (none) {
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x, p.y + (h - ts.y) * 0.5f), tint(sem.textDim, 0.5f), text);
        return;
    }
    const float ph = IM_TRUNC(h * 0.78f), pad = fs * 0.55f;
    const ImVec2 a(p.x, p.y + (h - ph) * 0.5f), b(p.x + ts.x + pad * 2.0f, a.y + ph);
    dl->AddRectFilled(a, b, tint(sem.surface), 5.0f);
    dl->AddRect(a, b, warn ? tint(sem.warn, 0.9f) : tint(sem.border, 1.4f), 5.0f);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + pad, p.y + (h - ts.y) * 0.5f), tint(col), text);
}

// The Pad cell of the matrix: the button's glyph and its name on a pill (warn
// edge + sign on a clash), "stick" next to the L3 glyph for a move action
// with no button, a dim dash for nothing.
void padCell(ImDrawList* dl, ImVec2 p, float h, const std::string& pad, bool stick, bool warn) {
    const theme::Semantics& sem = theme::semantics();
    const int bi = padButtonIndex(pad);
    const float fs = ImGui::GetFontSize() * 0.9f;
    if (bi < 0 && !stick) {
        const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, "\xE2\x80\x94");
        dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x, p.y + (h - ts.y) * 0.5f), tint(sem.textDim, 0.5f),
                    "\xE2\x80\x94");
        return;
    }
    const char* text = bi >= 0 ? ui::PadItemName(bi) : "stick";
    const float ph = IM_TRUNC(h * 0.78f), gh = IM_TRUNC(ph * 0.66f), padX = fs * 0.4f;
    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    const float gw = ui::PadButtonGlyphAt(nullptr, ImVec2(), bi >= 0 ? bi : 10, gh);
    const ImVec2 a(p.x, p.y + (h - ph) * 0.5f), b(p.x + padX + gw + fs * 0.35f + ts.x + padX, a.y + ph);
    dl->AddRectFilled(a, b, tint(sem.surface), 5.0f);
    dl->AddRect(a, b, warn ? tint(sem.warn, 0.7f) : tint(sem.border, 1.4f), 5.0f);
    ui::PadButtonGlyphAt(dl, ImVec2(a.x + padX, p.y + (h - gh) * 0.5f), bi >= 0 ? bi : 10, gh);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(a.x + padX + gw + fs * 0.35f, p.y + (h - ts.y) * 0.5f),
                tint(bi >= 0 ? padColour(pad) : sem.textDim), text);
    if (warn) {
        // A small warning triangle after the pill.
        const float t = fs * 0.75f, cx = b.x + fs * 0.4f + t * 0.5f, cy = p.y + h * 0.5f;
        dl->AddTriangle(ImVec2(cx, cy - t * 0.5f), ImVec2(cx + t * 0.55f, cy + t * 0.45f),
                        ImVec2(cx - t * 0.55f, cy + t * 0.45f), tint(sem.warn), 1.3f);
        dl->AddLine(ImVec2(cx, cy - t * 0.15f), ImVec2(cx, cy + t * 0.12f), tint(sem.warn), 1.3f);
    }
}

// HID usage code for an ImGui key the picker offers (0 = not offered).
int hidOfImGuiKey(ImGuiKey k) {
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 0x04 + (k - ImGuiKey_A);
    if (k >= ImGuiKey_1 && k <= ImGuiKey_9) return 0x1E + (k - ImGuiKey_1);
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return 0x3A + (k - ImGuiKey_F1);
    switch (k) {
        case ImGuiKey_0: return 0x27;
        case ImGuiKey_Enter: return 0x28;
        case ImGuiKey_Escape: return 0x29;
        case ImGuiKey_Backspace: return 0x2A;
        case ImGuiKey_Tab: return 0x2B;
        case ImGuiKey_Space: return 0x2C;
        case ImGuiKey_Minus: return 0x2D;
        case ImGuiKey_Equal: return 0x2E;
        case ImGuiKey_RightArrow: return 0x4F;
        case ImGuiKey_LeftArrow: return 0x50;
        case ImGuiKey_DownArrow: return 0x51;
        case ImGuiKey_UpArrow: return 0x52;
        case ImGuiKey_LeftCtrl: return 0xE0;
        case ImGuiKey_LeftShift: return 0xE1;
        case ImGuiKey_LeftAlt: return 0xE2;
        case ImGuiKey_RightCtrl: return 0xE4;
        case ImGuiKey_RightShift: return 0xE5;
        case ImGuiKey_RightAlt: return 0xE6;
        default: return 0;
    }
}

// The host gamepad (GLFW's SDL mapping) as PS2 pad buttons, in
// kPadButtonNames order; the triggers count as pressed past half travel.
// False when no mapped gamepad is connected.
bool hostPadButtons(bool out[16]) {
    for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
        if (!glfwJoystickIsGamepad(jid)) continue;
        GLFWgamepadstate st;
        if (!glfwGetGamepadState(jid, &st)) continue;
        const unsigned char* b = st.buttons;
        out[0] = b[GLFW_GAMEPAD_BUTTON_A];            // Cross
        out[1] = b[GLFW_GAMEPAD_BUTTON_X];            // Square
        out[2] = b[GLFW_GAMEPAD_BUTTON_Y];            // Triangle
        out[3] = b[GLFW_GAMEPAD_BUTTON_B];            // Circle
        out[4] = b[GLFW_GAMEPAD_BUTTON_DPAD_UP];
        out[5] = b[GLFW_GAMEPAD_BUTTON_DPAD_DOWN];
        out[6] = b[GLFW_GAMEPAD_BUTTON_DPAD_LEFT];
        out[7] = b[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT];
        out[8] = b[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER];  // L1
        out[9] = st.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.0f;  // L2 (-1..1)
        out[10] = b[GLFW_GAMEPAD_BUTTON_LEFT_THUMB];  // L3
        out[11] = b[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER];
        out[12] = st.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.0f;
        out[13] = b[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB];
        out[14] = b[GLFW_GAMEPAD_BUTTON_START];
        out[15] = b[GLFW_GAMEPAD_BUTTON_BACK];  // Select
        return true;
    }
    return false;
}

}  // namespace

bool App::inputBindingRow(InputPreset& preset, const InputAction& action) {
    bool changed = false;
    InputBinding& b = preset.at(action.name);
    const theme::Semantics& sem = theme::semantics();
    const float capW = ImGui::CalcTextSize("Press a button...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    // Each field shares its line with its Press to set button (the design's
    // shape): the row is laid out against a content edge pulled in by the
    // button's width, then the button follows on the same line. The label
    // column stays put - it is clamped to its max at any usable width.
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float gapX = ImGui::GetStyle().ItemSpacing.x;
    auto narrow = [&](bool on) {
        static float crMax = 0.0f, wrMax = 0.0f;
        if (on) {
            crMax = win->ContentRegionRect.Max.x;
            wrMax = win->WorkRect.Max.x;
            win->ContentRegionRect.Max.x -= capW + gapX;
            win->WorkRect.Max.x -= capW + gapX;
        } else {
            win->ContentRegionRect.Max.x = crMax;
            win->WorkRect.Max.x = wrMax;
        }
    };
    auto buttonBeside = [&] {
        ImGui::SameLine(0.0f, gapX);
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - capW);
    };

    // Pad: the list, or "press to set" from a controller on this machine.
    {
        const bool stick = b.pad.empty() && isMoveRole(action.role);
        narrow(true);
        if (ui::PadButtonCombo("Pad button", &b.pad, true, nullptr, stick ? "(stick)" : "(none)")) changed = true;
        if (stick) ui::RowHelp("The analog stick drives it; a pad button is optional.");
        narrow(false);
    }
    {
        const bool capturing = inputCapture_ == 1;
        buttonBeside();
        if (ImGui::Button(capturing ? "Press a button...##padcap" : "Press to set##pad", ImVec2(capW, 0))) {
            inputCapture_ = capturing ? 0 : 1;
            inputCaptureNoPad_ = false;
            if (inputCapture_ == 1) {
                bool now[16] = {};
                inputCaptureNoPad_ = !hostPadButtons(now);
                for (int i = 0; i < 16; ++i) inputCapturePrev_[i] = now[i];
            }
        }
        if (capturing) {
            bool now[16] = {};
            if (!hostPadButtons(now)) {
                inputCaptureNoPad_ = true;
            } else {
                for (int i = 0; i < 16; ++i) {
                    if (now[i] && !inputCapturePrev_[i]) {
                        b.pad = kPadButtonNames[i];
                        inputCapture_ = 0;
                        changed = true;
                        break;
                    }
                    inputCapturePrev_[i] = now[i];
                }
            }
        }
        if (inputCapture_ == 1 && inputCaptureNoPad_ && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("No controller connected - click the button on the drawn pad.");
    }

    {
        const char* keyLabel = b.key ? inputKeyLabel(b.key) : "";
        narrow(true);
        if (ui::BeginCombo("Keyboard key", *keyLabel ? keyLabel : "(none)")) {
            if (ImGui::Selectable("(none)", b.key == 0)) {
                b.key = 0;
                changed = true;
            }
            for (const InputKeyName& k : inputKeyNames())
                if (ImGui::Selectable(k.label, b.key == k.code)) {
                    b.key = k.code;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ui::RowHelp("A USB keyboard on the console (experimental).");
        narrow(false);
        const bool capturing = inputCapture_ == 2;
        buttonBeside();
        if (ImGui::Button(capturing ? "Press a key...##keycap" : "Press to set##key", ImVec2(capW, 0)))
            inputCapture_ = capturing ? 0 : 2;
        if (capturing) {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
                if (ImGui::IsKeyPressed((ImGuiKey)k, false))
                    if (const int hid = hidOfImGuiKey((ImGuiKey)k)) {
                        b.key = hid;
                        inputCapture_ = 0;
                        changed = true;
                        break;
                    }
        }
    }

    narrow(true);
    if (ui::Combo("Mouse button", &b.mouse, "(none)\0Left\0Right\0Middle\0")) changed = true;
    narrow(false);
    // Press to set: the next click ON the button, with whichever mouse button,
    // is the binding (b.mouse: 1 left, 2 right, 3 middle). Capturing on the
    // button itself rather than anywhere keeps the click from also landing on
    // some other widget; a click elsewhere or Esc cancels.
    {
        const bool capturing = inputCapture_ == 3;
        buttonBeside();
        const bool pressed = ImGui::Button(capturing ? "Click here...##mousecap" : "Press to set##mouse",
                                           ImVec2(capW, 0));
        const bool over = ImGui::IsItemHovered();
        if (capturing) {
            int got = 0;
            if (pressed) got = 1;  // a left click completes as the button's own press
            else if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) got = 2;
            else if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) got = 3;
            if (got) {
                b.mouse = got;
                inputCapture_ = 0;
                changed = true;
            } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                       (!over && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                                  ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                  ImGui::IsMouseClicked(ImGuiMouseButton_Middle)))) {
                inputCapture_ = 0;
            }
            if (inputCapture_ == 3 && over)
                ImGui::SetTooltip("Left, right or middle click - Esc cancels");
        } else if (pressed) {
            inputCapture_ = 3;
        }
    }
    return changed;
}

// Tools > Input Map (docs/input-bindings.md). The map as a map: every action's
// pad / keyboard / mouse binding in one matrix grouped by play context, a
// controller diagram for one context, and the selected action's binding with
// a plain clash check. Presets are tabs in the header; the project-wide
// switches sit in the header and the footer, not under an action. A player's
// own rebinds happen in-game through a menu Rebind key row and never touch
// this.
void App::drawInputMapWindow() {
    if (!showInputMap_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1100.0f), scaled(700.0f)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Input Map", &showInputMap_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

    InputMap& im = project_.input;
    if (im.presets.empty()) im.presets.push_back(InputPreset{});
    if (inputPresetSel_ < 0 || inputPresetSel_ >= (int)im.presets.size()) inputPresetSel_ = 0;
    if (im.activePreset < 0 || im.activePreset >= (int)im.presets.size()) im.activePreset = 0;
    if (inputActionSel_ >= (int)im.actions.size()) inputActionSel_ = (int)im.actions.size() - 1;

    bool changed = false;
    // Belt and braces, as elsewhere: the window edits the Input section AND
    // one Settings field (sprint speed), so both go into the comparison.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Input) +
        project::sectionJson(project_, project::Section::Settings);
    InputPreset& preset = im.presets[inputPresetSel_];
    auto bindingOf = [&](const InputAction& a) {
        const InputBinding* b = preset.find(a.name);
        return b ? *b : InputBinding{a.name, "", 0, 0};
    };

    // --- header: count, preset tabs, find, the master switch ------------------
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d actions", (int)im.actions.size());
        ImGui::SameLine(0.0f, fs);
        for (int i = 0; i < (int)im.presets.size(); ++i) {
            ImGui::PushID(i);
            std::string lbl = im.presets[i].name;
            if (i == im.activePreset) lbl += "  \xC2\xB7 at start";
            if (ui::FilterChip(lbl.c_str(), i == inputPresetSel_)) inputPresetSel_ = i;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Edit this preset's bindings. Right-click: rename,\n"
                                  "make it the preset at game start, delete.");
            if (ImGui::BeginPopupContextItem("##presetmenu")) {
                inputPresetSel_ = i;
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s", im.presets[i].name.c_str());
                ImGui::SetNextItemWidth(scaled(160.0f));
                // Flow-graph Set Input Preset nodes reference presets by name.
                if (ImGui::InputText("Preset name", buf, sizeof(buf))) renameInputPreset(i, buf);
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ImGui::MenuItem("Preset at game start", nullptr, im.activePreset == i)) {
                    im.activePreset = i;
                    changed = true;
                }
                if (ImGui::MenuItem("Delete preset", nullptr, false, im.presets.size() > 1)) {
                    im.presets.erase(im.presets.begin() + i);
                    if (inputPresetSel_ >= (int)im.presets.size()) inputPresetSel_ = (int)im.presets.size() - 1;
                    if (im.activePreset >= (int)im.presets.size()) im.activePreset = 0;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::SameLine();
        }
        if (ui::FilterChip("+ Preset", false)) {
            // A new preset starts as a copy of the edited one - the point of a
            // preset is a few deliberate differences, not a blank slate.
            InputPreset np = im.presets[inputPresetSel_];
            const std::string base = "preset";
            np.name = base;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const InputPreset& pr : im.presets) taken |= (pr.name == np.name);
                if (!taken) break;
                np.name = base + "-" + std::to_string(n);
            }
            im.presets.push_back(std::move(np));
            inputPresetSel_ = (int)im.presets.size() - 1;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A copy of the preset you are editing (e.g. Southpaw).");
        const float swW = ImGui::CalcTextSize("Players can rebind").x + IM_TRUNC(fs * 1.05f) * 1.8f +
                          ImGui::GetStyle().ItemInnerSpacing.x;
        const float findW = scaled(200.0f);
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + fs, right - swW - findW - fs * 1.2f));
        ImGui::SetNextItemWidth(findW);
        ImGui::InputTextWithHint("##imfind", "Find action or button...", inputFind_, sizeof(inputFind_));
        ImGui::SameLine(0.0f, fs);
        if (ui::Switch("Players can rebind", &im.allowRebind)) changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Master switch for the menu \"Rebind key\" rows (pad only).\n"
                              "Off = the rows show bindings read-only.");
    }
    ImGui::Dummy(ImVec2(0.0f, fs * 0.15f));

    const float bodyH = ImGui::GetContentRegionAvail().y;
    // The controller needs ~540 px for its two callout columns.
    const float rightW = ImMin(scaled(560.0f), ImGui::GetContentRegionAvail().x * 0.5f);

    // Per-context pad clashes in this preset: an action's button also bound
    // to another action of the SAME context.
    auto clashOf = [&](int ai) {
        const InputAction& a = im.actions[ai];
        const InputBinding b = bindingOf(a);
        if (b.pad.empty()) return -1;
        const int ctx = roleContext(a.role);
        for (int j = 0; j < (int)im.actions.size(); ++j) {
            if (j == ai || roleContext(im.actions[j].role) != ctx) continue;
            if (bindingOf(im.actions[j]).pad == b.pad) return j;
        }
        return -1;
    };

    // --- left: the binding matrix --------------------------------------------------
    ImGui::BeginChild("##im_left", ImVec2(ImGui::GetContentRegionAvail().x - rightW - ImGui::GetStyle().ItemSpacing.x,
                                          bodyH));
    {
        const float footH = ImGui::GetFrameHeightWithSpacing() * 1.4f;
        const float colPad = scaled(108.0f), colKey = scaled(108.0f), colMouse = scaled(84.0f), colRe = scaled(36.0f);
        const float gap = fs * 0.6f;
        auto colX = [&](float w0) {
            const float total = ImGui::GetContentRegionAvail().x;
            const float xRe = w0 + total - colRe;
            const float xMouse = xRe - gap - colMouse;
            const float xKey = xMouse - gap - colKey;
            const float xPad = xKey - gap - colPad;
            return std::array<float, 4>{xPad, xKey, xMouse, xRe};
        };
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const auto cx = colX(p.x);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float sfs = IM_TRUNC(fs * 0.86f);
            ImFont* f = ImGui::GetFont();
            const ImU32 c = tint(sem.textDim, 0.85f);
            dl->AddText(f, sfs, ImVec2(p.x + fs * 0.6f, p.y), c, "Action");
            dl->AddText(f, sfs, ImVec2(cx[0], p.y), c, "Pad");
            dl->AddText(f, sfs, ImVec2(cx[1], p.y), c, "Keyboard");
            dl->AddText(f, sfs, ImVec2(cx[2], p.y), c, "Mouse");
            dl->AddText(f, sfs, ImVec2(cx[3], p.y), c, "Re");
            ImGui::Dummy(ImVec2(0.0f, sfs + fs * 0.3f));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) ||
                (ImGui::IsWindowHovered() &&
                 ImGui::IsMouseHoveringRect(ImVec2(cx[3], p.y), ImVec2(cx[3] + colRe, p.y + sfs))))
                ImGui::SetTooltip("Re: the player may rebind it in-game.");
            ImGui::Separator();
        }
        ImGui::BeginChild("##im_matrix", ImVec2(0, ImMax(fs * 4.0f, ImGui::GetContentRegionAvail().y - footH)));
        const std::string q = [&] {
            std::string s = inputFind_;
            for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
            return s;
        }();
        auto lc = [](std::string s) {
            for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
            return s;
        };
        int shown = 0;
        for (int ctx = 0; ctx < CtxCount; ++ctx) {
            std::vector<int> rows;
            bool allFixed = true;
            for (int i = 0; i < (int)im.actions.size(); ++i) {
                const InputAction& a = im.actions[i];
                if (roleContext(a.role) != ctx) continue;
                if (!q.empty()) {
                    const InputBinding b = bindingOf(a);
                    const std::string hay = lc(a.label + " " + a.name + " " + b.pad + " " +
                                               inputKeyLabel(b.key) + " " + inputMouseLabel(b.mouse) +
                                               (b.pad.empty() && isMoveRole(a.role) ? " stick" : ""));
                    if (hay.find(q) == std::string::npos) continue;
                }
                rows.push_back(i);
                allFixed &= !a.rebindable;
            }
            if (rows.empty()) continue;
            listHead(contextName(ctx), ctx == CtxMenus && allFixed ? "fixed - never rebindable"
                                       : ctx == CtxCustom          ? "fire with On Action"
                                                                   : nullptr);
            for (int i : rows) {
                ++shown;
                const InputAction& a = im.actions[i];
                const InputBinding b = bindingOf(a);
                ImGui::PushID(i);
                const float h = IM_TRUNC(ImGui::GetFrameHeight() + fs * 0.2f);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                const bool sel = i == inputActionSel_;
                const std::string label = a.label.empty() ? a.name : a.label;
                if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, h))) {
                    inputActionSel_ = i;
                    inputCapture_ = 0;
                    // The diagram follows the picked action's mode.
                    const int c = roleContext(a.role);
                    if (c == CtxFoot || c == CtxNoclip) inputDiagramCtx_ = 0;
                    else if (c == CtxMenus) inputDiagramCtx_ = 1;
                    else if (c == CtxVehicle) inputDiagramCtx_ = 2;
                }
                uiscript::markLastItemChecked(sel);
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 q2(p.x + w, p.y + h);
                const float rr = ImGui::GetStyle().FrameRounding;
                if (sel) {
                    dl->AddRectFilled(p, q2, tint(sem.accent, 0.16f), rr);
                    dl->AddRect(p, q2, tint(sem.accent, 0.33f), rr);
                } else if (hov) {
                    dl->AddRectFilled(p, q2, tint(sem.surface, 0.8f), rr);
                }
                const auto cx = colX(p.x);
                const float ty = p.y + (h - fs) * 0.5f;
                dl->PushClipRect(p, ImVec2(cx[0] - gap, q2.y), true);
                dl->AddText(ImVec2(p.x + fs * 0.6f, ty), tint(sem.text, 0.92f), label.c_str());
                {
                    const float lw = ImGui::CalcTextSize(label.c_str()).x;
                    ImFont* mf = ui::monoFont() ? ui::monoFont() : ImGui::GetFont();
                    const float mfs = IM_TRUNC(fs * 0.82f);
                    dl->AddText(mf, mfs, ImVec2(p.x + fs * 0.6f + lw + fs * 0.5f, p.y + (h - mfs) * 0.5f),
                                tint(sem.textDim, 0.75f), a.name.c_str());
                }
                dl->PopClipRect();
                const bool stick = b.pad.empty() && isMoveRole(a.role);
                const bool clash = clashOf(i) >= 0;
                padCell(dl, ImVec2(cx[0], p.y), h, b.pad, stick, clash);
                const char* kl = b.key ? inputKeyLabel(b.key) : "";
                keycap(dl, ImVec2(cx[1], p.y), h, *kl ? kl : "\xE2\x80\x94", sem.text, !*kl, false);
                static const char* kMouse[] = {"\xE2\x80\x94", "Left", "Right", "Middle"};
                keycap(dl, ImVec2(cx[2], p.y), h, kMouse[ImClamp(b.mouse, 0, 3)], sem.text, b.mouse == 0, false);
                const ImVec2 dc(cx[3] + fs * 0.4f, p.y + h * 0.5f);
                if (a.rebindable && im.allowRebind)
                    dl->AddCircleFilled(dc, fs * 0.24f, tint(sem.ok));
                else
                    dl->AddCircle(dc, fs * 0.24f, tint(sem.textDim, 0.6f), 0, 1.2f);
                if (hov && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && clash)
                    ImGui::SetTooltip("%s is also %s - same mode (%s), both fire.", b.pad.c_str(),
                                      im.actions[clashOf(i)].label.c_str(), contextName(roleContext(a.role)));
                ImGui::PopID();
            }
        }
        if (im.actions.empty()) {
            if (ui::EmptyState(ui::Glyph::Sliders, "No input actions",
                               "Every gameplay button reads a named action. Restore the built-in set "
                               "(jump, use, sprint, menus, vehicle).",
                               "Restore built-ins")) {
                project::ensureInputActions(project_);
                changed = true;
            }
        } else if (shown == 0) {
            ImGui::TextDisabled("Nothing matches \"%s\".", inputFind_);
        }
        ImGui::EndChild();  // ##im_matrix

        // Footer: custom actions, built-ins, sprint speed.
        ImGui::Separator();
        if (ImGui::Button("+ Custom action")) {
            InputAction na;
            const std::string base = "action";
            na.name = base;
            for (int n = 2;; ++n) {
                bool taken = false;
                for (const InputAction& e : im.actions) taken |= (e.name == na.name);
                if (!taken) break;
                na.name = base + "-" + std::to_string(n);
            }
            na.label = na.name;
            im.actions.push_back(std::move(na));
            inputActionSel_ = (int)im.actions.size() - 1;
            inputFind_[0] = 0;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("No role: fire it with the On Action node.");
        const float sprintW = scaled(150.0f);
        const float restoreW = ImGui::CalcTextSize("Restore built-ins").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + fs,
                              ImGui::GetWindowContentRegionMax().x - restoreW - sprintW - fs * 0.8f));
        ImGui::SetNextItemWidth(sprintW);
        if (ImGui::DragFloat("##sprint", &project_.settings.sprintMultiplier, 0.02f, 1.0f, 4.0f, "Sprint %.2fx"))
            changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Sprint speed: walk-speed multiplier while the sprint action\n"
                              "is held. 1.00x switches sprinting off without unbinding.");
        ImGui::SameLine();
        if (ui::GhostButton("Restore built-ins")) {
            // Only adds what is missing (project::ensureInputActions) - existing
            // bindings are never overwritten.
            project::ensureInputActions(project_);
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Re-adds any missing built-in action (jump, use, sprint,\n"
                              "menu navigation...) with its default binding. Never\n"
                              "changes an action you already have.");
    }
    ImGui::EndChild();  // ##im_left

    // --- right: controller diagram + the selected action ---------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##im_right", ImVec2(0, bodyH));
    {
        // The selected action's context drives the diagram until the user
        // picks one; Noclip shares the on-foot picture.
        static const char* kCtx[] = {"On foot", "Menus", "Vehicle"};
        ImGui::AlignTextToFramePadding();
        ui::Caption("Controller");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - scaled(210.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - fs * 0.3f);
        ui::Segmented("##imctx", kCtx, 3, &inputDiagramCtx_, scaled(210.0f));
        const int dctx = inputDiagramCtx_ == 0 ? CtxFoot : inputDiagramCtx_ == 1 ? CtxMenus : CtxVehicle;

        // The controller (ui::PadDiagram, Annotated): a callout pill per bound
        // button of this context in fixed slots, the selected action's button
        // ringed, two actions of one context on one button in warn.
        std::vector<int> onBtn[16];
        for (int i = 0; i < (int)im.actions.size(); ++i) {
            const InputAction& a = im.actions[i];
            if (roleContext(a.role) != dctx) continue;
            const int bi = padButtonIndex(bindingOf(a).pad);
            if (bi >= 0) onBtn[bi].push_back(i);
        }
        auto nameOf = [&](int ai) {
            const InputAction& a = im.actions[ai];
            return a.label.empty() ? a.name : a.label;
        };
        // What the sticks do in this mode (the axis roles have no button).
        const char* stickRole[2] = {nullptr, nullptr};
        if (dctx == CtxFoot) {
            stickRole[0] = "Move";
            stickRole[1] = "Look";
        } else if (dctx == CtxVehicle) {
            stickRole[0] = "Steer";
            stickRole[1] = "Look";
        }
        std::string calloutText[16];
        std::vector<ui::PadCallout> callouts;
        ui::PadState ps;
        const bool capturing = inputCapture_ == 1 && inputActionSel_ >= 0 && inputActionSel_ < (int)im.actions.size();
        ps.capture = capturing;
        for (int bi = 0; bi < 16; ++bi) {
            std::string t;
            bool sel = false;
            for (int ai : onBtn[bi]) {
                t += (t.empty() ? "" : " / ") + nameOf(ai);
                sel |= ai == inputActionSel_;
            }
            const int stick = bi == 10 ? 0 : bi == 13 ? 1 : -1;
            ui::PadCallout c;
            c.button = bi;
            if (stick >= 0 && stickRole[stick]) {
                c.kind = t.empty() ? ui::PadCallout::Role : ui::PadCallout::Normal;
                t = t.empty() ? std::string(stickRole[stick]) : std::string(stickRole[stick]) + " \xc2\xb7 " + t;
            }
            if (t.empty()) continue;
            if (onBtn[bi].size() > 1) {
                c.kind = ui::PadCallout::Conflict;
                ps.conflict |= 1u << bi;
            } else if (sel) {
                c.kind = ui::PadCallout::Selected;
            }
            if (sel) ps.selected |= 1u << bi;
            calloutText[bi] = t;
            callouts.push_back(c);
        }
        for (ui::PadCallout& c : callouts) c.text = calloutText[c.button].c_str();
        ui::PadDiagramOut po;
        ui::PadDiagram("##inputpad", ui::PadMode::Annotated, ImGui::GetContentRegionAvail().x, ps, &po,
                       callouts.data(), (int)callouts.size());
        if (po.clicked >= 0) {
            if (capturing) {
                // "Click one here": the fallback when no pad is connected.
                preset.at(im.actions[inputActionSel_].name).pad = kPadButtonNames[po.clicked];
                inputCapture_ = 0;
                changed = true;
            } else if (!onBtn[po.clicked].empty()) {
                // Select the action on it - the next one when it is already selected.
                const std::vector<int>& v = onBtn[po.clicked];
                int next = v[0];
                for (size_t k = 0; k < v.size(); ++k)
                    if (v[k] == inputActionSel_) next = v[(k + 1) % v.size()];
                inputActionSel_ = next;
            }
        }
        if (po.hovered >= 0 && !capturing && !onBtn[po.hovered].empty())
            ImGui::SetTooltip("%s: %s - click to select it", kPadButtonNames[po.hovered],
                              calloutText[po.hovered].c_str());
        if (capturing) {
            ImGui::PushTextWrapPos(0.0f);
            const std::string an = nameOf(inputActionSel_);
            if (po.hovered >= 0)
                ImGui::TextColored(sem.accent, "%s \xe2\x86\x92 %s", kPadButtonNames[po.hovered], an.c_str());
            else
                ImGui::TextColored(sem.textDim, "Press a button for %s on any connected pad - or click one here.",
                                   an.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Dummy(ImVec2(0, fs * 0.2f));
        ImGui::Separator();

        // The selected action.
        if (inputActionSel_ >= 0 && inputActionSel_ < (int)im.actions.size()) {
            InputAction& a = im.actions[inputActionSel_];
            bool del = false;
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted(a.label.empty() ? a.name.c_str() : a.label.c_str());
            }
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
            if (ui::MoreButton("Action actions", ImGui::GetFrameHeight())) ImGui::OpenPopup("##actmore");
            if (ImGui::BeginPopup("##actmore")) {
                if (ImGui::MenuItem("Delete action")) del = true;
                ImGui::EndPopup();
            }
            ImGui::TextDisabled("%s \xC2\xB7 %s", a.name.c_str(), contextName(roleContext(a.role)));
            ImGui::PushItemWidth(-FLT_MIN);
            ui::InspectorRows rows(scaled(76), scaled(110));
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s", a.label.c_str());
                if (ui::InputText("Label", buf, sizeof(buf))) a.label = buf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ui::RowHelp("What an in-game rebind row shows.");
            }
            {
                // The name is the reference key (flow nodes, menu rows), so a
                // rename has to follow into them - and must stay unique.
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s", a.name.c_str());
                if (ui::InputText("Name", buf, sizeof(buf))) {
                    std::string want = buf;
                    bool clash = want.empty();
                    for (size_t i = 0; i < im.actions.size(); ++i)
                        if ((int)i != inputActionSel_ && im.actions[i].name == want) clash = true;
                    if (!clash) renameInputAction(inputActionSel_, want);
                }
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ui::RowHelp("Referenced by name (flow nodes, menu rows); renames follow.");
            }
            {
                // Role: which built-in behavior the action drives. Reassigning
                // is allowed (that is how you move "sprint" onto a custom one).
                static const char* kRoleLabels =
                    "(custom - flow graph only)\0Jump\0Use\0Throw\0Sprint\0"
                    "Fly up (noclip)\0Fly down (noclip)\0Menu confirm\0Menu back\0"
                    "Pause menu\0Alternate (load slot)\0Menu up\0Menu down\0"
                    "Menu left\0Menu right\0Move forward\0Move back\0Move left\0"
                    "Move right\0Vehicle throttle\0Vehicle brake\0Vehicle handbrake\0"
                    "Vehicle nitrous\0Vehicle camera\0Vehicle rear view\0";
                if (ui::Combo("Role", &a.role, kRoleLabels)) changed = true;
                ui::RowHelp("What the generated game uses this action for. Only one\n"
                            "action per role reaches the game (the first one wins).");
                // A duplicated role is silently dropped at codegen - say so here.
                const int owner = im.roleIndex(a.role);
                if (a.role != InputAction::RoleNone && owner != inputActionSel_) {
                    char msg[160];
                    std::snprintf(msg, sizeof(msg), "\"%s\" already owns this role - this one is inert",
                                  im.actions[owner].label.c_str());
                    ui::StatusLine(1, msg);
                }
            }
            ui::Caption(("Binding - " + preset.name).c_str());
            changed |= inputBindingRow(preset, a);
            if (ui::Switch("Player may rebind it in-game (Pad only; keep menu navigation fixed)", &a.rebindable))
                changed = true;

            // Clash check: the same pad button in the other actions of this
            // preset, said plainly - different modes are fine.
            const InputBinding b = bindingOf(a);
            if (!b.pad.empty()) {
                std::string also, same;
                const int ctx = roleContext(a.role);
                for (int j = 0; j < (int)im.actions.size(); ++j) {
                    if (j == inputActionSel_ || bindingOf(im.actions[j]).pad != b.pad) continue;
                    const InputAction& o2 = im.actions[j];
                    const std::string nm = o2.label.empty() ? o2.name : o2.label;
                    const int c2 = roleContext(o2.role);
                    if (c2 == ctx) same += (same.empty() ? "" : ", ") + nm;
                    else {
                        std::string cn = contextName(c2);
                        for (char& ch : cn) ch = (char)std::tolower((unsigned char)ch);
                        also += (also.empty() ? "" : ", ") + nm + " (" + cn + ")";
                    }
                }
                ImGui::Spacing();
                ImGui::PushTextWrapPos(0.0f);
                if (!same.empty()) {
                    ui::StatusLine(1, (b.pad + " clashes with " + same + " - same mode, both fire.").c_str());
                } else if (!also.empty()) {
                    ImGui::TextDisabled("%s also: %s. Different modes, no clash.", b.pad.c_str(), also.c_str());
                } else {
                    ImGui::TextDisabled("%s does nothing else in this preset.", b.pad.c_str());
                }
                ImGui::PopTextWrapPos();
            }
            ImGui::PopItemWidth();
            if (del) {
                const std::string gone = im.actions[inputActionSel_].name;
                im.actions.erase(im.actions.begin() + inputActionSel_);
                for (InputPreset& pr : im.presets)
                    for (size_t i = 0; i < pr.bindings.size();)
                        if (pr.bindings[i].action == gone)
                            pr.bindings.erase(pr.bindings.begin() + i);
                        else
                            ++i;
                if (inputActionSel_ >= (int)im.actions.size()) inputActionSel_ = (int)im.actions.size() - 1;
                inputCapture_ = 0;
                changed = true;
            }
        } else {
            ImGui::TextDisabled("Select an action in the matrix.");
        }
    }
    ImGui::EndChild();  // ##im_right

    if (changed ||
        project::sectionJson(project_, project::Section::Input) +
                project::sectionJson(project_, project::Section::Settings) !=
            beforeSection)
        commitChange();
    ImGui::End();
}

void App::rebuildTreePreview() {
    treeMesh_ = treegen::generate(treeParams_);
    treeBarkTex_ = treegen::bakeBarkTexture(treeParams_);
    treeLeafTex_ = treegen::bakeLeafTexture(treeParams_);
    ++treePreviewVersion_;
    treePreviewDirty_ = false;
}

// Tools > Global Illumination (docs/global-illumination.md).
//
// The bake is deliberately NOT part of the build: it takes seconds to minutes,
// and a build that silently re-bakes lighting is a build nobody runs. So this
// window owns the whole loop - the quality knobs, the per-scene staleness
// readout, and the button. Everything downstream (codegen, texbake, the
// viewport) only ever READS the cache in .res-baked/gi/.
// Polled every frame and from nowhere else: a bake that finishes has to reach
// the VIEWPORT, and applyProjectToViewport is event-driven - without this the
// preview would keep showing the previous bake until the user happened to
// touch something else. It must not hang off any window being open.
void App::giBakerPoll() {
    if (hasProject_ && giBakerSeen_ != giBaker_.version()) {
        giBakerSeen_ = giBaker_.version();
        applyProjectToViewport();
    }
}

// Per-scene GI cache state for the Ambience Editor (the GI tab's scene list
// and the "stale" mark on its window tab, visible from every tab). Reading a
// cache parses the file and the signature walks the scene, so this is
// recomputed when the project or the baker moved, and at most once a second
// otherwise - not every frame.
void App::refreshGiStates(bool force) {
    const double now = ImGui::GetTime();
    const uint64_t ver = giBaker_.version();
    if (!force && giStatesSerial_ == modelEditSerial_ && giStatesVersion_ == ver &&
        giStates_.size() == project_.scenes.size() && now - giStatesAt_ < 1.0)
        return;
    if (!force && giBaker_.running() && giStates_.size() == project_.scenes.size() && now - giStatesAt_ < 1.0)
        return;
    giStatesAt_ = now;
    giStatesSerial_ = modelEditSerial_;
    giStatesVersion_ = ver;
    const ProjectSettings& st = project_.settings;
    const gibake::Settings gst = gibake::settingsOf(st);
    giStates_.assign(project_.scenes.size(), GiSceneState{});
    for (int si = 0; si < (int)project_.scenes.size(); ++si) {
        gibake::Bake b;
        GiSceneState& s = giStates_[si];
        s.present = gibake::read(gibake::cachePath(project_, si), b);
        s.fresh = s.present && st.giEnabled &&
                  b.signature == gibake::signature(project_, project_.scenes[si], gst);
        if (s.present) {
            s.atlas = b.atlas.size;
            s.ground = b.terrain.size;
            for (int k = 0; k < 3; ++k) s.dim[k] = b.probes.dim[k];
        }
    }
}

// The "Global illumination" tab of the Ambience Editor (drawAmbienceWindow).
// Two columns: the settings (quality preset + label-left sliders with their
// meaning written under each, probes with what the grid costs) and a Bake
// column that never scrolls away - the state card with the one primary action
// or the running bake's stage, elapsed time and Cancel, the scenes with their
// state, and the two bake options pinned to the bottom.
void App::drawGiBakeSection() {
    ProjectSettings& st = project_.settings;
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    bool changed = false, giSwitched = false;
    refreshGiStates(false);

    // A bake that just finished: remember how long it took.
    if (giBaker_.running()) {
        if (!giWasRunning_) giBakeStartedAt_ = ImGui::GetTime();
        giWasRunning_ = true;
    } else if (giWasRunning_) {
        giWasRunning_ = false;
        giLastBakeSecs_ = (float)(ImGui::GetTime() - giBakeStartedAt_);
        refreshGiStates(true);
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float bakeW = ImClamp(avail.x * 0.38f, scaled(280), scaled(380));
    // In a side column the Bake column goes under the settings and the two
    // scroll as one page.
    const bool stacked = avail.x < scaled(640);
    if (stacked) {
        ImGui::BeginChild("##gi_stack", ImVec2(0, avail.y));
        avail.x = ImGui::GetContentRegionAvail().x;
    }
    const float settingsH = stacked ? fs * 44.0f : avail.y;
    const float bakeH = stacked ? fs * 24.0f : avail.y;

    auto drawSettings = [&] {
    // --- settings -----------------------------------------------------------------
    ui::BeginCard("##gi_settings", ImVec2(stacked ? avail.x : ImMax(scaled(240), avail.x - bakeW - gap), settingsH));
    {
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Baked global illumination");
        }
        // The master switch, at the header's right end (its old label = ID).
        const float sw = IM_TRUNC(IM_TRUNC(fs * 1.05f) * 1.8f);
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - sw);
        if (ui::Switch("##Enable baked global illumination", &st.giEnabled)) changed = giSwitched = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Static geometry gets a baked multi-bounce lightmap; everything that moves\n"
                              "gets its light from a probe grid. Off = the classic ambient + directional.");
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextWrapped("Sky, sun, emissive materials and baked point lights become one integrator with "
                           "bounces. Costs the PS2 nothing at runtime - it ships as pixels.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::BeginChild("##gi_settings_body", ImVec2(0, 0));
        ImGui::BeginDisabled(!st.giEnabled || giBaker_.running());
        const float labelW = scaled(110);
        ui::InspectorRows rows(labelW, labelW);
        // One dim line of meaning under a slider, aligned with the field.
        auto meaning = [&](const char* text) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelW + gap);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
            ImGui::PushFont(nullptr, fs * 0.88f);
            ImGui::TextWrapped("%s", text);
            ImGui::PopFont();
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(1.0f, fs * 0.25f));
        };
        {
            // Quality presets: Draft for blocking out, Final for the shipped bake;
            // any other ray/bounce pair reads as Custom. Derived from the values,
            // so nothing new is stored.
            const float y0 = ImGui::GetCursorPosY();
            ui::Caption("Quality");
            const ImVec2 after = ImGui::GetCursorPos();
            int q = st.giRays == 32 && st.giBounces == 1 ? 0 : (st.giRays == 128 && st.giBounces == 2 ? 1 : 2);
            const char* qs[] = {"Draft", "Final", "Custom"};
            const float segW = ImMin(scaled(230), ImGui::GetContentRegionAvail().x * 0.55f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - segW, y0 - ImGui::GetStyle().FramePadding.y));
            if (ui::Segmented("##giquality", qs, 3, &q, segW)) {
                if (q == 0) st.giRays = 32, st.giBounces = 1;
                if (q == 1) st.giRays = 128, st.giBounces = 2;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draft = 32 rays, 1 bounce (fast, noisy). Final = 128 rays, 2 bounces.");
            ImGui::SetCursorPos(ImVec2(after.x, ImMax(after.y, ImGui::GetCursorPosY())));
        }
        if (ui::SliderInt("Rays per texel", &st.giRays, 16, 512)) changed = true;
        meaning("More = less noise in the bounce, linearly more bake time.");
        if (ui::SliderInt("Bounces", &st.giBounces, 0, 6)) changed = true;
        meaning("0 = direct + sky only · 1 already bleeds colour · 2 is the useful default.");
        if (ui::SliderFloat("Sky light", &st.giSkyLight, 0.0f, 3.0f, "%.2f")) changed = true;
        meaning("At 1.0 an open surface gets about what the flat Ambient gave it.");
        if (ui::SliderFloat("Sun light", &st.giSunLight, 0.0f, 3.0f, "%.2f")) changed = true;
        meaning("Multiplier on the scene light, which GI shadows.");
        if (ui::SliderFloat("Ambient floor", &st.giAmbientFloor, 0.0f, 0.3f, "%.3f")) changed = true;
        meaning("Keeps a sealed room with no light readable.");

        ImGui::Spacing();
        {
            const float y0 = ImGui::GetCursorPosY();
            ui::Caption("Light probes");
            const ImVec2 after = ImGui::GetCursorPos();
            const float sw2 = IM_TRUNC(IM_TRUNC(fs * 1.05f) * 1.8f);
            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowContentRegionMax().x - sw2, y0 - ImGui::GetStyle().FramePadding.y));
            // A child window's own position: InspectorRows' row layout does not
            // apply after a SetCursorPos on the caption line (it is a SameLine-
            // like placement), so this draws as the bare switch.
            if (ui::Switch("##Bake a probe grid", &st.giProbes)) changed = true;
            ImGui::SetCursorPos(after);
        }
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextWrapped("What lights the player, NPCs, animated and physics objects. Without probes they "
                           "keep the flat ambient while the static world gets GI.");
        ImGui::PopStyleColor();
        ImGui::BeginDisabled(!st.giProbes);
        if (ui::SliderFloat("Spacing", &st.giProbeSpacing, 1.0f, 12.0f, "%.1f u")) changed = true;
        if (ui::SliderFloat("Level height", &st.giProbeHeight, 0.5f, 8.0f, "%.1f u")) changed = true;
        if (ui::SliderInt("Levels", &st.giProbeLevels, 1, 12)) changed = true;
        prefHelp("Vertical layers above the lowest ground; raised automatically where the\n"
                 "terrain climbs higher than they reach.");
        {
            // What the grid costs, for the active scene: the same sizing the
            // bake uses (gibake::bakeProbes) before its relief top-up and cap.
            const TerrainConfig& t = project_.active().terrain;
            const float sp = ImMax(0.1f, st.giProbeSpacing);
            const int nx = (int)std::ceil((float)t.width / sp) + 1, nz = (int)std::ceil((float)t.depth / sp) + 1;
            const long long n = (long long)nx * st.giProbeLevels * nz;
            char grid[48], cnt[32], ram[32];
            std::snprintf(grid, sizeof(grid), "%d × %d × %d", nx, st.giProbeLevels, nz);
            std::snprintf(cnt, sizeof(cnt), "%lld", n);
            std::snprintf(ram, sizeof(ram), "%.1f KB", n * 13.0 / 1024.0);
            const float tw = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
            ImGui::Spacing();
            ui::StatTile("Grid", grid, 4, tw);
            ImGui::SameLine(0.0f, gap);
            ui::StatTile("Probes", cnt, n > 24000 ? 1 : 4, tw);
            if (ImGui::IsItemHovered() && n > 24000)
                ImGui::SetTooltip("Over the 24 000-probe cap - the bake halves the grid until it fits.");
            ImGui::SameLine(0.0f, gap);
            ui::StatTile("EE RAM", ram, 4, tw);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("12 bytes + 1 per probe, compiled into the game.");
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        // The probes the active scene's bake holds, in the viewport - view
        // state, so it stays usable while a bake runs or GI is switched off.
        ImGui::Spacing();
        ui::Switch("Show probes in viewport", &giShowProbes_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Each baked probe as a small diamond, its tips shaded with the light\n"
                              "that probe sampled in that direction. Small dark ones are dead\n"
                              "(inside geometry - they weigh nothing).");
        {
            int live = 0;
            const int n = viewport_.giProbeCount(&live);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + labelW + gap);
            if (n > 0)
                ImGui::TextDisabled("%d baked · %d live · %d dead", n, live, n - live);
            else
                ImGui::TextDisabled("No probes baked for this scene.");
        }
        ImGui::EndChild();
    }
    ui::EndCard();
    if (changed) commitChange();
    // The switch changes the PICTURE rather than the next bake, and
    // commitChange does not touch the viewport - unticking it must take the
    // baked light off screen now. The sliders only alter a future bake.
    if (giSwitched) {
        applyProjectToViewport();
        refreshGiStates(true);
    }
    };
    auto drawBake = [&] {
    // --- bake column ----------------------------------------------------------------
    ui::BeginCard("##gi_bake", ImVec2(0, bakeH));
    {
        std::string gpuWhy;
        const bool gpuHere = gigpu::available(&gpuWhy);
        const bool useGpu = gpuBakes_ && gpuHere;
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Bake");
        }
        const char* eng = useGpu ? "GPU" : "CPU";
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(eng).x - fs);
        ui::Badge(eng, useGpu ? 3 : 4);

        const float footH = ImGui::GetFrameHeightWithSpacing() * 2.0f + fs * 2.6f;
        ImGui::BeginChild("##gi_bake_body", ImVec2(0, -footH));
        const int cur = project_.activeScene;
        const bool haveCur = cur >= 0 && cur < (int)giStates_.size();
        int staleCount = 0;
        for (const GiSceneState& s : giStates_) staleCount += st.giEnabled && !s.fresh;

        // State card.
        ui::BeginCard("##gi_state", ImVec2(0, 0));
        if (giBaker_.running()) {
            const float pr = giBaker_.progress();
            const std::string status = giBaker_.status();  // "Baking main (1/2)"
            int idx = 1, total = 1;
            const size_t lp = status.rfind('(');
            if (lp != std::string::npos) std::sscanf(status.c_str() + lp, "(%d/%d)", &idx, &total);
            // Stage from the bake's own phase layout (gibake::bakeScene: prepare
            // 0-5%, bounces 5-40%, lightmap + ground 40-70%, probes 70-100% of
            // each scene). The fraction is all the worker reports.
            const float local = ImClamp(pr * (float)ImMax(1, total) - (float)(idx - 1), 0.0f, 1.0f);
            char stage[64];
            if (local < 0.05f) std::snprintf(stage, sizeof(stage), "preparing");
            else if (local < 0.40f) {
                const int nb = ImMax(1, st.giBounces);
                const int b = ImClamp((int)((local - 0.05f) / 0.35f * nb) + 1, 1, nb);
                std::snprintf(stage, sizeof(stage), "bounce %d of %d", b, nb);
            } else if (local < 0.70f) std::snprintf(stage, sizeof(stage), "lightmap");
            else std::snprintf(stage, sizeof(stage), "probes");
            char pct[16], el[32];
            std::snprintf(pct, sizeof(pct), "%d%%", (int)(pr * 100.0f));
            std::snprintf(el, sizeof(el), "%.1f s elapsed", ImGui::GetTime() - giBakeStartedAt_);
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted(status.c_str());
            }
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(pct).x);
            ImGui::TextColored(sem.accent, "%s", pct);
            ui::ProgressInline(pr, nullptr, nullptr);
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::TextDisabled("%s", stage);
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(el).x);
                ImGui::TextDisabled("%s", el);
            }
            if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, 0))) giBaker_.cancel();
        } else {
            const GiSceneState* s = haveCur ? &giStates_[cur] : nullptr;
            const std::string nm = project_.active().name;
            if (!st.giEnabled) ui::StatusLine(4, "GI is off - the scenes use the classic lighting");
            else if (s && s->fresh) ui::StatusLine(0, (nm + " - baked, up to date").c_str());
            else if (s && s->present) ui::StatusLine(1, (nm + " - stale, the scene or settings changed").c_str());
            else ui::StatusLine(1, (nm + " - not baked yet").c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled(!st.giEnabled);
            const float bw = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
            if (ui::PrimaryButton("Bake this scene", bw)) startGiBake(false);
            ImGui::SameLine(0.0f, gap);
            if (ui::GhostButton("Bake all scenes", bw)) startGiBake(true);
            ImGui::EndDisabled();
            if (st.giEnabled && staleCount > 0) ImGui::TextDisabled("%d scene(s) need a bake", staleCount);
        }
        ui::EndCard();

        ImGui::Spacing();
        ui::Caption("Scenes");
        for (int si = 0; si < (int)giStates_.size() && si < (int)project_.scenes.size(); ++si) {
            const GiSceneState& s = giStates_[si];
            ImGui::PushID(si);
            const bool baking = giBaker_.running() &&
                                giBaker_.status().find(project_.scenes[si].name) != std::string::npos;
            const int kind = baking ? 3 : (!st.giEnabled ? 4 : (s.fresh ? 0 : 1));
            ui::StatusLine(kind, project_.scenes[si].name.c_str());
            if (si == cur) {
                ImGui::SameLine(0.0f, fs * 0.3f);
                ImGui::TextDisabled("· current");
            }
            const char* badge = baking ? "baking" : (s.fresh ? "baked" : (s.present ? "stale" : "not baked"));
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(badge).x - fs);
            ui::Badge(badge, kind);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fs * 0.9f);
            ui::FontScope mono(ui::monoFont());
            if (s.present)
                ImGui::TextDisabled("%slightmap %d · ground %d · probes %dx%dx%d", baking ? "was: " : "", s.atlas,
                                    s.ground, s.dim[0], s.dim[1], s.dim[2]);
            else
                ImGui::TextDisabled("-");
            ImGui::PopID();
        }
        if (giBaker_.running()) {
            ImGui::Spacing();
            char note[160];
            if (giLastBakeSecs_ > 0.0f)
                std::snprintf(note, sizeof(note),
                              "The viewport keeps showing the previous bake until this one finishes. Last bake: %.1f s.",
                              giLastBakeSecs_);
            else
                std::snprintf(note, sizeof(note), "The viewport keeps showing the previous bake until this one finishes.");
            ui::Note(3, note);
        }
        ImGui::EndChild();

        // Pinned options.
        ImGui::Separator();
        ui::InspectorRows rows(scaled(80), scaled(120));
        // GPU or CPU is ONE editor-wide choice (Edit > Preferences > Build >
        // Use the GPU for bakes): this says which and links there.
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled(useGpu ? "Bakes on the GPU" : gpuHere ? "Bakes on the CPU" : "Bakes on the CPU (no GPU here)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", gpuHere ? "Set once for every bake in Edit > Preferences > Build > Use the GPU for bakes."
                                            : ("No GPU bake on this machine: " + gpuWhy).c_str());
        ImGui::SameLine();
        if (ImGui::TextLink("Change##gpubakes")) {
            prefCategory_ = 3;  // Build
            openEditorPrefsPopup_ = true;
        }
        // ONE name for giAutoBake everywhere (Project Preferences > Build & debug says
        // the same and links here).
        ImGui::BeginDisabled(!st.giEnabled);
        if (ui::Switch("Re-bake stale global illumination before every build (only scenes whose cache no "
                       "longer matches)",
                       &st.giAutoBake))
            commitChange();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("A changed big scene can cost minutes. Also in Project Preferences > Build & debug.");
    }
    ui::EndCard();
    };
    // Stacked, the Bake card comes FIRST: the tool's one action must not be
    // the thing below the fold.
    if (stacked) {
        drawBake();
        drawSettings();
    } else {
        drawSettings();
        ImGui::SameLine(0.0f, gap);
        drawBake();
    }
    if (stacked) ImGui::EndChild();
}

// --- Baked lighting tab ------------------------------------------------------

// Everything the project's model-AO state depends on, in one number: the
// project-wide knobs plus every per-asset override. Cheap - it never touches
// the file system, which is the point (the SCAN parses every .obj).
static uint64_t modelAoIntentOf(const Project& p) {
    uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&h](uint64_t v) {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    };
    const modelao::Params prm = modelao::paramsOf(p.settings);
    mix(prm.enabled ? 1 : 0);
    mix((uint64_t)prm.rays);
    uint32_t bits = 0;
    std::memcpy(&bits, &prm.strength, sizeof bits);
    mix(bits);
    std::memcpy(&bits, &prm.dist, sizeof bits);
    mix(bits);
    for (const auto& [asset, mode] : p.modelAoMode) {
        for (unsigned char c : asset) mix(c);
        mix((uint64_t)mode);
    }
    return h;
}

// Polled every frame from drawUI and from nowhere else - the giBakerPoll rule:
// a bake that finishes has to reach the VIEWPORT whether or not the tab is
// open, and toggling the setting has to invalidate the textures it affects
// even if the panel was never looked at.
//
// It starts a run only when the INTENT changes, because a scan parses every
// .obj under res/models. That is also why the worker owns the plan: the UI
// thread never walks the project's models.
void App::modelAoPoll() {
    if (!hasProject_) return;
    const modelao::Params prm = modelao::paramsOf(project_.settings);
    const uint64_t intent = modelAoIntentOf(project_);
    if (intent != modelAoIntent_) {
        modelAoIntent_ = intent;
        if (prm.enabled || !project_.modelAoMode.empty())
            modelAoBaker_.start(project_, prm);
        else
            viewport_.setModelAoMaps({}, 0.0f);
        // A strength edit changes nothing on disk, so no bake will land to
        // push it - the table is re-set here with the new strength, and
        // setModelAoMaps is a no-op when neither actually moved.
        if (modelAoSeen_ == modelAoBaker_.version() && !modelAoBaker_.running())
            viewport_.setModelAoMaps(modelAoBaker_.maps(), prm.strength);
    }
    if (modelAoSeen_ == modelAoBaker_.version()) return;
    modelAoSeen_ = modelAoBaker_.version();
    viewport_.setModelAoMaps(modelAoBaker_.maps(), prm.strength);
}

// The bake starters. The tabs' buttons and the Build menu's Bake items (and so
// the command palette) call these, so a bake started from anywhere is the same
// bake. Each reads the project from disk-backed state, hence the save first.
void App::startGiBake(bool allScenes) {
    if (giBaker_.running() || !project_.settings.giEnabled) return;
    const bool useGpu = gpuBakes_ && gigpu::available(nullptr);
    saveProject();
    giBaker_.start(project_, allScenes ? std::vector<int>{} : std::vector<int>{project_.activeScene},
                   useGpu);
}

void App::startShadowBake(bool allScenes) {
    if (shadowBaker_.running() || !project_.settings.bakedShadows) return;
    saveProject();
    shadowBaker_.start(project_, allScenes ? std::vector<int>{} : std::vector<int>{project_.activeScene});
}

void App::startModelAoBake() {
    if (modelAoBaker_.running()) return;
    modelAoBaker_.start(project_, modelao::paramsOf(project_.settings));
}

int App::prelitPendingCount() {
    if (!hasProject_ || project_.scenes.empty()) return 0;
    const SceneData& sc = project_.active();
    int n = 0;
    for (const PrelitStatus& r : prelitStatuses())
        if (sc.objects[r.index].prelitWanted && !(r.prelit && r.fresh)) ++n;
    return n;
}

void App::startPrelitBake(bool onlyStale) {
    if (litBaker_.running() || !hasProject_ || project_.scenes.empty()) return;
    const SceneData& sc = project_.active();
    std::vector<int> objs;
    for (const PrelitStatus& r : prelitStatuses()) {
        if (!sc.objects[r.index].prelitWanted) continue;
        if (onlyStale && r.prelit && r.fresh) continue;
        objs.push_back(r.index);
    }
    if (objs.empty()) return;
    saveProject();  // the bake reads the models off disk
    litBaker_.start(project_, project_.activeScene, objs, litBakeParams_);
}

// The "Baked lighting" tab of the Ambience Editor (drawAmbienceWindow):
// everything whose light is computed on the HOST at build and ships as pixels
// rather than as a scene table. It lives beside the GI tab because that window
// is where a scene's light is authored.
//
// Its sections do NOT share a scope, and pretending they did is what this tab
// used to get wrong: model AO is a property of an ASSET and pre-lit is per
// OBJECT, while scene AO belongs to an ambience PRESET. They are together
// because they answer one question - "what is baked into this project's
// light" - which is the question somebody opens this tab with. Each section
// names whose setting it is in its own header, so the scope is on screen
// instead of implied by which tab you are on.
//
// A list of sections on purpose. Add the next one by appending a call here.
void App::drawBakedLightingSection() {
    drawSceneAoSection();
    ImGui::Spacing();
    drawModelAoSection();
    ImGui::Spacing();
    drawPrelitSection();
    ImGui::Spacing();
    drawShadowBakeSection();
}

// Baked shadow decals (docs/shadows.md). Per OBJECT, like pre-lit above it -
// an object asks for one in Properties > Shadow and this is where the
// project-wide quality and the bake itself live. Built as a near-twin of
// drawGiBakeSection, deliberately: it is the same shape of thing (an explicit,
// content-hashed, cached bake with a stale readout and a pre-build opt-in), and
// a second vocabulary for it would read as a second mechanism.
void App::drawShadowBakeSection() {
    ProjectSettings& st = project_.settings;
    bool changed = false;
    ImGui::SeparatorText("Baked shadows (projected decals)");

    bool switched = false;
    if (ImGui::Checkbox("Enable baked shadow decals", &st.bakedShadows))
        changed = switched = true;
    prefHelp(
        "The static directional shadow: traced once on this machine, projected "
        "onto whatever is under the caster and drawn as ordinary triangles. "
        "Mark a caster with Properties > Shadow > Baked.");
    ImGui::TextDisabled(
        "Reaches textured walls and models, which a lightmap cannot.\n"
        "One submit per atlas page, whatever the shadow count.");

    ImGui::BeginDisabled(!st.bakedShadows || shadowBaker_.running());
    ImGui::SeparatorText("Quality");
    {
        // The tile size decides BOTH the sharpness and how many shadows fit on
        // a page, so the combo says the second number rather than making
        // anyone work it out: 256/res squared per page.
        const int sizes[] = {32, 64, 128};
        const char* labels[] = {"32 px (64 per page)", "64 px (16 per page)",
                                "128 px (4 per page)"};
        int sel = st.bakedShadowRes == 32 ? 0 : (st.bakedShadowRes == 128 ? 2 : 1);
        ImGui::SetNextItemWidth(scaled(180.0f));
        if (ImGui::Combo("Shadow detail", &sel, labels, 3)) {
            st.bakedShadowRes = sizes[sel];
            changed = true;
        }
    }
    prefHelp("Texels per shadow. A page is 256x256 and costs 32 KB of the "
             "~1 MB texture budget whatever is on it, so this is really a "
             "choice of how many shadows one page holds.");
    {
        const int sizes[] = {0, 64, 128};
        const char* labels[] = {"Off (decals on the ground)", "64 px per chunk",
                                "128 px per chunk"};
        int sel = st.bakedShadowGround == 64 ? 1 : (st.bakedShadowGround == 128 ? 2 : 0);
        ImGui::SetNextItemWidth(scaled(180.0f));
        if (ImGui::Combo("Ground shadows", &sel, labels, 3)) {
            st.bakedShadowGround = sizes[sel];
            changed = true;
        }
    }
    prefHelp("Every caster's shadow on the terrain as one 4-bit map per\n"
             "terrain chunk - sharp and cheap in VRAM - instead of decals.\n"
             "Decals still land on walls, slabs and models.");
    if (st.bakedShadowGround != 0) {
        if (ImGui::Checkbox("Fold into the terrain colour", &st.bakedShadowGroundFold))
            changed = true;
        prefHelp("The ground maps darken the terrain's vertex colours instead\n"
                 "of drawing as one more pass over the shaded cells: no pass\n"
                 "at all, but one sample per terrain vertex, so shadow edges\n"
                 "are softer and coarser, more so on far, lower-detail chunks.");
    }
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Softness", &st.bakedShadowSunAngle, 0.5f, 8.0f,
                           "%.1f deg"))
        changed = true;
    prefHelp("The light's angular size - how fast the edge opens up as the "
             "shadow gets further from what casts it. The real sun is 0.5; 2 "
             "reads better on a PS2 and hides the tile's own texel count.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Strength", &st.bakedShadowStrength, 0.0f, 1.0f, "%.2f"))
        changed = true;
    prefHelp("How dark full shadow gets. Never black: the shadow is tinted "
             "with the scene's own sky colour, so this is how much of the sky "
             "is left in it.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Max length", &st.bakedShadowMaxLength, 0.0f, 12.0f,
                           "%.1f x height"))
        changed = true;
    prefHelp("How far a shadow stretches, in multiples of the caster's own "
             "height. A low sun throws one hundreds of units long and every "
             "texel of the tile goes into it. The shadow FADES OUT over this "
             "distance rather than stopping on it, and the search runs on "
             "further still, so one that leaves a quay edge carries on down "
             "instead of ending on a straight line. 0 = across the map.");
    ImGui::EndDisabled();

    if (changed) commitChange();
    // The switch changes the PICTURE rather than the next bake, and
    // commitChange does not touch the viewport - unticking it has to take the
    // shadows off screen now, not at the next scene change (the giEnabled
    // precedent). The quality sliders deliberately do not refresh: they alter
    // what a re-bake would produce.
    if (switched) applyProjectToViewport();

    ImGui::SeparatorText("Scenes");
    const shadowbake::Options opt = shadowbake::optionsOf(st);
    int staleCount = 0, totalCasters = 0;
    // Casters that asked for a shadow and did not get one. The bake has always
    // recorded these; only the CLI ever said so, which left the panel showing
    // a clean green "baked" for a scene where a caster silently cast nothing.
    std::vector<std::string> refusals;
    if (ImGui::BeginTable("shadowscenes", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Scene");
        ImGui::TableSetupColumn("Bake");
        ImGui::TableSetupColumn("Contents");
        ImGui::TableHeadersRow();
        for (int si = 0; si < (int)project_.scenes.size(); ++si) {
            // How many objects ASKED, straight off the model - cheap, and it is
            // what makes "not baked" readable: a scene nobody marked is not a
            // scene with a missing bake.
            int asked = 0;
            for (const SceneObject& o : project_.scenes[si].objects)
                if (o.shadowMode == 4) ++asked;
            totalCasters += asked;
            shadowbake::Bake b;
            const bool present =
                shadowbake::read(shadowbake::cachePath(project_, si), b);
            const bool fresh =
                present && st.bakedShadows &&
                b.signature ==
                    shadowbake::signature(project_, project_.scenes[si], opt);
            if (asked > 0 && !fresh) ++staleCount;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(project_.scenes[si].name.c_str());
            ImGui::TableNextColumn();
            if (asked == 0)
                ImGui::TextDisabled("no casters");
            else if (fresh)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "baked");
            else if (present)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "stale");
            else
                ImGui::TextDisabled("not baked");
            ImGui::TableNextColumn();
            // The budget, and all three numbers that can run out: VRAM pages,
            // ELF bytes and the submit count. This is the panel's job - the
            // feature's whole cost story is "which of these do I hit first".
            if (present && fresh) {
                // How many casters the bake actually PRODUCED, not how many
                // asked. They differ whenever one is refused, and reporting
                // `asked` here said "5 caster(s), baked" for a scene where two
                // of the five cast nothing at all - which is the one reading
                // that makes a refusal impossible to notice.
                int made = 0;
                for (const shadowbake::Group& g : b.groups) made += g.casters;
                if (made < asked)
                    ImGui::TextColored(
                        ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                        "%d of %d caster(s), %d draw(s), %d KB VRAM, %d KB ELF",
                        made, asked, (int)b.groups.size(),
                        b.vramWords() * 4 / 1024, b.elfBytes() / 1024);
                else
                    ImGui::Text(
                        "%d caster(s), %d draw(s), %d KB VRAM, %d KB ELF", made,
                        (int)b.groups.size(), b.vramWords() * 4 / 1024,
                        b.elfBytes() / 1024);
                for (const shadowbake::Refusal& r : b.truncated)
                    refusals.push_back(project_.scenes[si].name + " / " +
                                       r.name + ": " + r.why);
            }
            else if (asked > 0)
                ImGui::Text("%d caster(s)", asked);
            else
                ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }

    // Named, never silently dropped - the rule the bake log already followed.
    // A caster is refused when its projection would cost more triangles than
    // one shadow may, which a big composite model (a whole street block, lamps
    // included) hits easily under a low sun: the reach is long, so the shadow
    // lands on a lot of geometry.
    if (!refusals.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f),
                           "%d caster(s) cast nothing:", (int)refusals.size());
        for (const std::string& r : refusals)
            ImGui::TextWrapped("  %s", r.c_str());
        ImGui::TextDisabled(
            "Lower Max length, or split the model so each piece casts its "
            "own.");
    }

    ImGui::Spacing();
    if (shadowBaker_.running()) {
        ImGui::ProgressBar(shadowBaker_.progress(), ImVec2(-FLT_MIN, 0.0f));
        ImGui::TextUnformatted(shadowBaker_.status().c_str());
        if (ImGui::Button("Cancel##shadowbake", ImVec2(scaled(140.0f), 0)))
            shadowBaker_.cancel();
    } else {
        ImGui::BeginDisabled(!st.bakedShadows);
        if (ImGui::Button("Bake this scene##shadow", ImVec2(scaled(140.0f), 0)))
            startShadowBake(false);
        ImGui::SameLine();
        if (ImGui::Button("Bake all scenes##shadow", ImVec2(scaled(140.0f), 0)))
            startShadowBake(true);
        ImGui::EndDisabled();
        if (st.bakedShadows && staleCount > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                               "%d scene(s) need a bake", staleCount);
        }
    }
    ImGui::BeginDisabled(!st.bakedShadows);
    if (ImGui::Checkbox("Re-bake automatically (while editing and before every build)##shadow",
                        &st.bakedShadowAutoBake))
        commitChange();
    ImGui::EndDisabled();
    prefHelp("Any edit to a caster or anything a shadow lands on makes the\n"
             "scene's bake stale, and a stale bake draws nothing. With this on,\n"
             "the active scene re-bakes in the background a second after your\n"
             "last edit (the old shadows stay on screen meanwhile), and every\n"
             "stale scene re-bakes before a build. A bake takes seconds.");
    if (totalCasters == 0 && st.bakedShadows)
        ImGui::TextDisabled(
            "Nothing casts one yet: pick Baked in Properties > Shadow.");
    // The one thing that makes a perfectly good bake look wrong in the game,
    // and it cannot be seen from the bake itself. Asked cheaply - a preset
    // lookup, not a plan() - so it can sit here every frame; plan() carries
    // the same sentence for the CLI.
    if (st.bakedShadows && totalCasters > 0) {
        const int ai = project::ambienceIndexFor(project_, project_.active());
        if (ai >= 0 && project_.ambiencePresets[ai].cycle.enabled)
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                               "The day/night cycle moves the sun; these "
                               "shadows stay at the baked hour.");
    }
}

// Scene ambient occlusion (docs/ambient-occlusion.md). Per ambience PRESET,
// which is why the header names the one being edited - the same preset the
// Presets tab has selected. Moved here from that tab so every baked-light
// control is in one place; the numbers and their meaning are unchanged.
void App::drawSceneAoSection() {
    ImGui::SeparatorText("Scene AO (contact shadows)");
    if (project_.ambiencePresets.empty()) {
        ImGui::TextDisabled("This project has no ambience preset to hold it.");
        return;
    }
    // Self-contained on purpose: the tab must say something the moment it is
    // opened. With nothing selected in the Presets tab it edits the project's
    // DEFAULT preset - the one a scene gets unless it asks for another - and
    // the combo is here so switching never means leaving the tab you came to.
    int sel = selectedAmbience_;
    if (sel < 0 || sel >= (int)project_.ambiencePresets.size())
        sel = (project_.defaultAmbience >= 0 &&
               project_.defaultAmbience < (int)project_.ambiencePresets.size())
                  ? project_.defaultAmbience
                  : 0;
    AmbiencePreset& a = project_.ambiencePresets[sel];
    bool changed = false;

    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::BeginCombo("Preset", a.name.c_str())) {
        for (int i = 0; i < (int)project_.ambiencePresets.size(); ++i) {
            const bool cur = i == sel;
            if (ImGui::Selectable(project_.ambiencePresets[i].name.c_str(), cur))
                selectedAmbience_ = i;
            if (cur) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    prefHelp("Scene AO is one of an ambience preset's settings, so a scene "
             "picks it up with the rest of its mood. This is the same "
             "selection the Presets tab shows.");
    if (ImGui::Checkbox("Bake ambient occlusion", &a.aoEnabled)) changed = true;
    prefHelp(
        "Soft contact shadows where geometry meets: terrain self-shadowing "
        "(ravines, the foot of hills) and darkening where objects touch the "
        "ground and each other - baked at build into per-pixel AO textures (a "
        "terrain map + a primitive lightmap atlas) and drawn as extra blended "
        "passes. Which objects cast is per object: Properties > Cast shadow. "
        "Imported models cast but do not receive; their own self-occlusion is "
        "Model AO below.");

    ImGui::BeginDisabled(!a.aoEnabled);
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("AO strength", &a.aoStrength, 0.0f, 1.0f, "%.2f"))
        changed = true;
    prefHelp("How dark full occlusion gets (0 = invisible).");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::DragFloat("AO radius", &a.aoRadius, 0.05f, 0.1f, 50.0f, "%.2f"))
        changed = true;
    prefHelp(
        "World units the contact darkening reaches from an occluder; terrain "
        "self-shadowing scans 3x this. Set it larger than the things receiving "
        "it and they darken as a lump instead of at their base.");
    if (a.aoRadius < 0.1f) a.aoRadius = 0.1f;
    ImGui::EndDisabled();

    if (a.aoEnabled)
        ImGui::TextDisabled(
            "Static bake: a moved object re-shades itself at runtime, but its "
            "cast shadow stays where the scene was built.");
    if (changed) commitChange();
}

void App::drawModelAoSection() {
    ProjectSettings& st = project_.settings;
    bool changed = false;

    ImGui::SeparatorText("Model AO");
    if (ImGui::Checkbox("Bake model AO into textures", &st.modelAo))
        changed = true;
    prefHelp(
        "Each .obj model's own surface occlusion, multiplied into the texture "
        "it already ships. Transform-invariant, so every instance shares it - "
        "and it costs no extra VRAM.");

    ImGui::BeginDisabled(!st.modelAo);
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Strength", &st.modelAoStrength, 0.0f, 1.0f, "%.2f"))
        changed = true;
    prefHelp("How dark full occlusion gets. Changing it re-multiplies; it does "
             "not re-bake.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderInt("Rays per texel", &st.modelAoRays, 8, 512))
        changed = true;
    prefHelp("More = less noise, linearly more bake time.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Distance", &st.modelAoDist, 0.0f, 20.0f,
                           st.modelAoDist > 0.0f ? "%.2f u" : "auto"))
        changed = true;
    prefHelp("How far the occlusion reaches, in world units. 0 = a quarter of "
             "the model's own size.");
    ImGui::EndDisabled();

    if (changed) commitChange();

    ImGui::Spacing();
    if (modelAoBaker_.running()) {
        ImGui::ProgressBar(modelAoBaker_.progress(), ImVec2(-FLT_MIN, 0.0f));
        ImGui::TextUnformatted(modelAoBaker_.status().c_str());
        if (ImGui::Button("Cancel##modelao", ImVec2(scaled(140.0f), 0)))
            modelAoBaker_.cancel();
    } else if (ImGui::Button("Re-scan##modelao", ImVec2(scaled(140.0f), 0))) {
        startModelAoBake();
    }

    const modelao::Report rep = modelAoBaker_.report();
    if (rep.rows.empty()) {
        ImGui::TextDisabled("No model assets scanned yet.");
        return;
    }
    if (ImGui::BeginTable("modelao", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Asset");
        ImGui::TableSetupColumn("AO");
        ImGui::TableSetupColumn("Bake");
        ImGui::TableHeadersRow();
        std::string lastModel;
        for (size_t i = 0; i < rep.rows.size(); ++i) {
            const modelao::Row& r = rep.rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.modelRel.c_str());
            ImGui::TableNextColumn();
            if (r.baked)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "baked");
            else if (r.eligible)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "%s",
                                   r.status.c_str());
            else
                ImGui::TextDisabled("%s", r.status.c_str());
            if (ImGui::IsItemHovered() && !r.textureRel.empty())
                ImGui::SetTooltip("%s", r.textureRel.c_str());
            ImGui::TableNextColumn();
            // One combo per MODEL, on its first row: the override is keyed by
            // the asset, so a model with two textures must not offer two.
            if (r.modelRel == lastModel) {
                ImGui::TextDisabled("-");
                continue;
            }
            lastModel = r.modelRel;
            auto it = project_.modelAoMode.find(r.modelRel);
            int mode = it == project_.modelAoMode.end() ? 0 : it->second;
            const char* kModes[] = {"Default", "On", "Off"};
            ImGui::PushID((int)i);
            ImGui::SetNextItemWidth(scaled(100.0f));
            if (ImGui::Combo("##mode", &mode, kModes, 3)) {
                if (mode == 0)
                    project_.modelAoMode.erase(r.modelRel);
                else
                    project_.modelAoMode[r.modelRel] = mode;
                commitChange();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%d baked, %d pending, %d skipped", rep.baked,
                        rep.pending, rep.skipped);
}

// --- Pre-lit models (docs/prelit-models.md, "Managing pre-lit objects") ------

// Everything the two panels draw from, computed at most once per edit.
//
// It cannot be computed per frame: litbake::signature's scene half is
// gibake::signature, which content-hashes the heightmap and every model and
// material file the bake reads. So the answer is cached against the one key
// that can change it - the scene, the model serial and the bake parameters -
// and, while a widget is being dragged, not recomputed at all.
const std::vector<App::PrelitStatus>& App::prelitStatuses() {
    if (!hasProject_ || project_.scenes.empty()) {
        prelitStatus_.clear();
        prelitStatusKey_ = ~0ull;
        return prelitStatus_;
    }
    uint64_t key = 0xcbf29ce484222325ull;
    auto mix = [&key](uint64_t v) {
        key ^= v + 0x9e3779b97f4a7c15ull + (key << 6) + (key >> 2);
    };
    mix((uint64_t)project_.activeScene);
    mix(modelEditSerial_);
    mix((uint64_t)litBakeParams_.size);
    mix((uint64_t)litBakeParams_.rays);
    if (key == prelitStatusKey_) return prelitStatus_;
    // A drag bumps one of those every frame. Answer when the hand comes off.
    if (ImGui::IsAnyItemActive() && prelitStatusKey_ != ~0ull)
        return prelitStatus_;
    prelitStatusKey_ = key;
    prelitStatus_.clear();

    const SceneData& sc = project_.active();
    const std::set<std::string> refs =
        project::runtimeRefNames(project_, sc.objects);
    const std::vector<char> flags =
        litbake::freshFlags(project_, sc, litBakeParams_);
    for (size_t i = 0; i < sc.objects.size(); ++i) {
        const SceneObject& o = sc.objects[i];
        // Eligible = a STATIC model, which is what litbake bakes. An object
        // that is already pre-lit is listed whatever it is now, or turning a
        // model into something else would hide the row that reverts it.
        const bool eligible = o.type == PrimitiveType::Model &&
                              !o.modelPath.empty() &&
                              !isAnimatedModelPath(o.modelPath);
        if (!eligible && !o.prelit && !o.prelitWanted) continue;
        PrelitStatus st;
        st.index = (int)i;
        st.prelit = o.prelit;
        st.fresh = i < flags.size() && flags[i] != 0;
        st.movable = project::objectRuntimeMovable(o, refs);
        if (o.prelit) {
            int w = 0, h = 0, comp = 0;
            const std::string png =
                (std::filesystem::path(project_.dir) / litbake::outputPngRel(o))
                    .string();
            if (stbi_info(png.c_str(), &w, &h, &comp)) st.texSize = w;
        }
        prelitStatus_.push_back(st);
    }
    return prelitStatus_;
}

const App::PrelitStatus* App::prelitStatusFor(int objIndex) {
    for (const PrelitStatus& s : prelitStatuses())
        if (s.index == objIndex) return &s;
    return nullptr;
}

// Polled every frame from drawUI, never from a window body: a batch started
// from the tab must land even if the tab was closed, the selection moved or
// the object's Properties panel is showing something else entirely.
//
// The whole batch is ONE undo step - it is one operation the user asked for.
void App::litBakerPoll() {
    if (!hasProject_) return;
    std::vector<litbake::Baker::Done> done;
    if (!litBaker_.take(done)) return;
    const int si = litBaker_.sceneIndex();
    if (si < 0 || si >= (int)project_.scenes.size()) return;
    int ok = 0;
    std::string firstErr, lastName;
    for (litbake::Baker::Done& d : done) {
        const std::string e = litbake::applyToObject(
            project_, project_.scenes[si], d.objectIndex, d.result, d.sig);
        if (!e.empty()) {
            if (firstErr.empty()) firstErr = e;
            continue;
        }
        ++ok;
        if (d.objectIndex >= 0 &&
            d.objectIndex < (int)project_.scenes[si].objects.size())
            lastName = project_.scenes[si].objects[d.objectIndex].name;
    }
    if (ok) {
        commitChange();
        prelitStatusKey_ = ~0ull;
        // A RE-bake writes the same file with new pixels, so the viewport's
        // cached texture is now a lie. (A first bake writes a new path and
        // would have been picked up anyway.)
        viewport_.invalidateAssets();
    }
    if (!firstErr.empty())
        statusMessage_ = "Pre-light failed: " + firstErr;
    else if (ok == 1)
        statusMessage_ = "Pre-lit " + lastName;
    else if (ok > 1)
        statusMessage_ = "Pre-lit " + std::to_string(ok) + " objects";
}

void App::revertPrelit(int objIndex) {
    if (!hasProject_ || project_.scenes.empty()) return;
    SceneData& sc = project_.active();
    if (objIndex < 0 || objIndex >= (int)sc.objects.size()) return;
    litbake::revertObject(sc.objects[objIndex]);
    commitChange();
    prelitStatusKey_ = ~0ull;
    viewport_.invalidateAssets();
    statusMessage_ = "Reverted " + sc.objects[objIndex].name +
                     " to its source material";
}

// The second section of the "Baked lighting" tab: the scene's pre-lit objects,
// what their textures still agree with, and the batch bake.
//
// It is per SCENE where Model AO above is per ASSET, and that is the whole
// difference between the two mechanisms: a model's self-occlusion is
// transform-invariant and free, a pre-lit texture is particular to where the
// object stands and costs one texture each.
void App::drawPrelitSection() {
    ImGui::SeparatorText("Pre-lit models");
    if (!hasProject_ || project_.scenes.empty()) {
        ImGui::TextDisabled("No scene.");
        return;
    }
    SceneData& sc = project_.active();
    ImGui::TextDisabled(
        "Scene \"%s\". The scene's light baked INTO an object's own texture -\n"
        "the only per-pixel static light a TEXTURED surface can take here.",
        sc.name.c_str());

    // Both labels are unique IN THE WHOLE TAB on purpose: Model AO above has a
    // "Rays per texel" of its own, a label IS an ImGui id, and a duplicate is
    // additionally a name --ui-script could never tell apart.
    ImGui::SetNextItemWidth(scaled(110.0f));
    ImGui::DragInt("Pre-lit size", &litBakeParams_.size, 8.0f, 32, 512, "%d px");
    prefHelp(
        "Output texture size. Each pre-lit object costs size^2 x 4 bytes of "
        "GS VRAM. Changing it makes every baked object stale.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(110.0f));
    ImGui::DragInt("Pre-lit rays", &litBakeParams_.rays, 1.0f, 8, 512,
                   "%d rays");
    prefHelp("Hemisphere rays per texel. More = less noise, linearly more "
             "bake time.");

    const std::vector<PrelitStatus>& rows = prelitStatuses();
    if (rows.empty()) {
        ImGui::TextDisabled("No static models in this scene.");
        return;
    }

    int wanted = 0, pending = 0, prelitCount = 0;
    double vramKb = 0.0;
    for (const PrelitStatus& r : rows) {
        const SceneObject& o = sc.objects[r.index];
        if (o.prelitWanted) ++wanted;
        if (o.prelitWanted && !(r.prelit && r.fresh)) ++pending;
        if (r.prelit) {
            ++prelitCount;
            const double px = r.texSize > 0 ? r.texSize : litBakeParams_.size;
            vramKb += px * px * 4.0 / 1024.0;
        }
    }

    if (ImGui::BeginTable("prelitobjects", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Ship pre-lit");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("VRAM", ImGuiTableColumnFlags_WidthFixed,
                                scaled(60.0f));
        ImGui::TableHeadersRow();
        for (const PrelitStatus& r : rows) {
            SceneObject& o = sc.objects[r.index];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            // The checkbox carries the object's NAME as its label, ##-suffixed
            // by index so two objects called the same thing are still two
            // widgets. A label-less checkbox would have been an untargetable
            // row for --ui-script, i.e. a control nothing but a human can ever
            // press - and unticking this one runs a Revert.
            const std::string rowLabel =
                o.name + "##prelitrow" + std::to_string(r.index);
            bool want = o.prelitWanted;
            if (ImGui::Checkbox(rowLabel.c_str(), &want)) {
                // Ticking states an intention and bakes nothing - the bake is
                // minutes and belongs on a button. UNticking is the Revert:
                // the object goes back to its source material immediately,
                // because leaving a pre-lit texture on an object nobody wants
                // pre-lit is exactly the state this panel exists to end.
                if (!want && o.prelit) {
                    revertPrelit(r.index);
                } else {
                    o.prelitWanted = want;
                    commitChange();
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Ship this object pre-lit. Ticking only says so - press "
                    "Bake pending below.\nUnticking reverts it to its source "
                    "material.");

            ImGui::TableNextColumn();
            if (r.prelit && r.fresh)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "pre-lit");
            else if (r.prelit)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "stale");
            else
                ImGui::TextDisabled("not baked");
            if (r.movable) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                                   "moves at runtime");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "A pre-lit texture GLUES the light to the surface.\n"
                        "This object can be moved at run time, so it would\n"
                        "carry contact shadows that match nothing.");
            }

            ImGui::TableNextColumn();
            if (r.prelit) {
                const double px = r.texSize > 0 ? r.texSize : litBakeParams_.size;
                ImGui::Text("%.0f KB", px * px * 4.0 / 1024.0);
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (litBaker_.running()) {
        ImGui::ProgressBar(litBaker_.progress(), ImVec2(-FLT_MIN, 0.0f));
        ImGui::TextUnformatted(litBaker_.status().c_str());
        if (ImGui::Button("Cancel##prelitbatch", ImVec2(scaled(140.0f), 0)))
            litBaker_.cancel();
    } else {
        auto startBatch = [&](bool onlyStale) { startPrelitBake(onlyStale); };
        char lbl[64];
        std::snprintf(lbl, sizeof lbl, "Bake pending (%d)###prelitpending",
                      pending);
        ImGui::BeginDisabled(pending == 0);
        if (ImGui::Button(lbl, ImVec2(scaled(160.0f), 0))) startBatch(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        std::snprintf(lbl, sizeof lbl, "Re-bake all (%d)###prelitall", wanted);
        ImGui::BeginDisabled(wanted == 0);
        if (ImGui::Button(lbl, ImVec2(scaled(160.0f), 0))) startBatch(false);
        ImGui::EndDisabled();
        if (wanted == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("tick an object above first");
        } else if (pending == 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f),
                               "everything is fresh");
        }
        if (!litBaker_.error().empty())
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "%s",
                               litBaker_.error().c_str());
    }

    // The cost, stated where the decision is made. ~1.33 MB is the GS budget
    // (docs/gs-vram.md) and every one of these textures is pinned for good.
    if (prelitCount)
        ImGui::TextDisabled("%d pre-lit texture(s) ~ %.0f KB of the ~1.33 MB "
                            "GS budget",
                            prelitCount, vramKb);
    else
        ImGui::TextDisabled("Nothing pre-lit in this scene - 0 KB.");
    // The same switch Project Preferences > Build & debug carries, offered where the
    // person is looking at the staleness it answers. Project-wide, not per
    // scene - a build compiles every scene.
    if (ImGui::Checkbox("Re-bake stale objects before every build",
                        &project_.settings.prelitAutoBake))
        commitChange();
    prefHelp("Only the stale ones, all scenes; a build with everything fresh "
             "costs nothing. Also in Project > Preferences > Build & debug.");
    ImGui::TextDisabled("Headless: --bake-prelit <projectDir> [scene]");
}

// --- Tree Generator helpers -------------------------------------------------

// One overlay toggle in a pill (Tree Generator and Animation Editor previews).
// `text` null = a divider.
struct OverlayPillItem {
    const char* label;
    const char* text;
    bool active;
};

// A row of overlay text buttons on one translucent card at `at`. Each button
// reports its state to --ui-script (expect-checked). Returns the clicked
// index, or -1; *outMax receives the card's bottom-right corner.
static int overlayPill(ImVec2 at, const OverlayPillItem* items, int n, ImVec2* outMax = nullptr) {
    const float pad = ui::OverlayPad();
    float w = pad * 2.0f;
    for (int i = 0; i < n; ++i)
        w += items[i].text ? ui::OverlayTextWidth(items[i].text) : pad * 2.0f + 1.0f;
    const float h = ui::OverlayRowHeight() + pad * 2.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ui::OverlayPanel(dl, at, ImVec2(at.x + w, at.y + h));
    int clicked = -1;
    float x = at.x + pad;
    for (int i = 0; i < n; ++i) {
        if (!items[i].text) {
            ui::OverlayDivider(dl, x + pad, at.y + pad * 2.0f, at.y + h - pad * 2.0f);
            x += pad * 2.0f + 1.0f;
            continue;
        }
        ImGui::SetCursorScreenPos(ImVec2(x, at.y + pad));
        if (ui::OverlayTextButton(items[i].label, items[i].text, items[i].active)) clicked = i;
        uiscript::markLastItemChecked(items[i].active);
        x += ui::OverlayTextWidth(items[i].text);
    }
    if (outMax) *outMax = ImVec2(at.x + w, at.y + h);
    return clicked;
}

// A preset's silhouette, drawn in its own bark and leaf colours: what you
// are choosing, without rendering six trees.
static void drawTreeSilhouette(ImDrawList* dl, ImVec2 c, float s, int preset,
                               const treegen::Params& p) {
    auto col = [](const float* rgb, float k = 1.0f) {
        return ImGui::GetColorU32(ImVec4(std::min(1.0f, rgb[0] * k), std::min(1.0f, rgb[1] * k),
                                         std::min(1.0f, rgb[2] * k), 1.0f));
    };
    const ImU32 bark = col(p.barkColor, 1.15f), leaf = col(p.leafColor, 1.35f);
    const float tw = s * 0.07f;
    const ImVec2 base(c.x, c.y + s * 0.45f);
    switch (preset) {
        case 1:  // birch: slim pale trunk, narrow crown
            dl->AddRectFilled(ImVec2(c.x - tw * 0.6f, c.y - s * 0.1f), ImVec2(c.x + tw * 0.6f, base.y), bark);
            dl->AddEllipseFilled(ImVec2(c.x, c.y - s * 0.12f), ImVec2(s * 0.17f, s * 0.32f), leaf);
            break;
        case 2:  // spruce: stacked cone
            dl->AddRectFilled(ImVec2(c.x - tw * 0.5f, c.y + s * 0.3f), ImVec2(c.x + tw * 0.5f, base.y), bark);
            dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.45f), ImVec2(c.x - s * 0.24f, c.y + s * 0.32f),
                                  ImVec2(c.x + s * 0.24f, c.y + s * 0.32f), leaf);
            break;
        case 3:  // poplar: a column
            dl->AddRectFilled(ImVec2(c.x - tw * 0.5f, c.y + s * 0.2f), ImVec2(c.x + tw * 0.5f, base.y), bark);
            dl->AddEllipseFilled(ImVec2(c.x, c.y - s * 0.08f), ImVec2(s * 0.11f, s * 0.36f), leaf);
            break;
        case 4: {  // dead tree: bare forks
            const float lw = std::max(1.5f, s * 0.04f);
            dl->AddLine(base, ImVec2(c.x, c.y - s * 0.05f), bark, lw * 1.4f);
            dl->AddLine(ImVec2(c.x, c.y + s * 0.05f), ImVec2(c.x - s * 0.2f, c.y - s * 0.35f), bark, lw);
            dl->AddLine(ImVec2(c.x, c.y - s * 0.05f), ImVec2(c.x + s * 0.18f, c.y - s * 0.4f), bark, lw);
            dl->AddLine(ImVec2(c.x - s * 0.1f, c.y - s * 0.15f), ImVec2(c.x - s * 0.24f, c.y - s * 0.12f), bark, lw * 0.8f);
            break;
        }
        case 5:  // bush: low and wide
            dl->AddEllipseFilled(ImVec2(c.x, c.y + s * 0.28f), ImVec2(s * 0.3f, s * 0.16f), leaf);
            break;
        default:  // oak and anything spread: trunk + round crown
            if (p.crown == 1) {
                dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.45f), ImVec2(c.x - s * 0.24f, c.y + s * 0.32f),
                                      ImVec2(c.x + s * 0.24f, c.y + s * 0.32f), leaf);
            } else {
                dl->AddRectFilled(ImVec2(c.x - tw * 0.7f, c.y), ImVec2(c.x + tw * 0.7f, base.y), bark);
                if (p.leafCount > 0)
                    dl->AddEllipseFilled(ImVec2(c.x, c.y - s * 0.12f), ImVec2(s * 0.3f, s * 0.26f), leaf);
            }
            break;
    }
}

// The `.tree` sidecar: the generator's Params beside the .obj they made, so
// a tree can be re-opened and adjusted instead of rebuilt from memory.
// Editor-only (texbake never ships it); travels with its model.
template <class F>
static void forEachTreeField(treegen::Params& p, F&& f) {
    f("height", &p.height), f("thickness", &p.thickness), f("flare", &p.flare);
    f("taper", &p.taper), f("gnarliness", &p.gnarliness), f("sweep", &p.sweep);
    f("branchAngle", &p.branchAngle), f("angleJitter", &p.angleJitter);
    f("lengthRatio", &p.lengthRatio), f("lengthTaper", &p.lengthTaper);
    f("radiusRatio", &p.radiusRatio), f("spawnStart", &p.spawnStart);
    f("leafSize", &p.leafSize), f("leafAspect", &p.leafAspect);
    for (int i = 0; i < 3; ++i) {
        const std::string k = std::to_string(i);
        f(("barkColor" + k).c_str(), &p.barkColor[i]);
        f(("barkColor2_" + k).c_str(), &p.barkColor2[i]);
        f(("leafColor" + k).c_str(), &p.leafColor[i]);
        f(("leafColor2_" + k).c_str(), &p.leafColor2[i]);
    }
}
template <class F>
static void forEachTreeInt(treegen::Params& p, F&& f) {
    f("crown", &p.crown), f("whorls", &p.whorls), f("levels", &p.levels);
    f("children0", &p.children[0]), f("children1", &p.children[1]), f("children2", &p.children[2]);
    f("sides", &p.sides), f("sidesMin", &p.sidesMin), f("rings", &p.rings);
    f("ringsMin", &p.ringsMin), f("leafCount", &p.leafCount), f("leafLevels", &p.leafLevels);
    f("barkStyle", &p.barkStyle), f("leafStyle", &p.leafStyle);
}

static std::string treeParamsJson(treegen::Params p, const std::string& preset) {
    std::string s = "{\n  \"tyraxTree\": 1,\n  \"preset\": \"" + json::escape(preset) +
                    "\",\n  \"seed\": " + std::to_string(p.seed);
    char buf[64];
    forEachTreeField(p, [&](const char* k, float* v) {
        std::snprintf(buf, sizeof buf, "%.6g", *v);
        s += ",\n  \"" + std::string(k) + "\": " + buf;
    });
    forEachTreeInt(p, [&](const char* k, int* v) {
        s += ",\n  \"" + std::string(k) + "\": " + std::to_string(*v);
    });
    return s + "\n}\n";
}

static bool treeParamsFromJson(const std::string& text, treegen::Params& out, std::string* preset) {
    json::Value v;
    if (!json::parse(text, v) || !v.find("tyraxTree")) return false;
    treegen::Params p;  // absent keys keep the defaults
    if (const json::Value* s = v.find("seed")) p.seed = (uint32_t)s->numberOr(p.seed);
    forEachTreeField(p, [&](const char* k, float* f) {
        if (const json::Value* e = v.find(k)) *f = (float)e->numberOr(*f);
    });
    forEachTreeInt(p, [&](const char* k, int* i) {
        if (const json::Value* e = v.find(k)) *i = (int)e->numberOr(*i);
    });
    if (preset) {
        const json::Value* pr = v.find("preset");
        *preset = pr ? pr->stringOr("") : "";
    }
    out = p;
    return true;
}

// The asset name a save would use: `base`, or base-2, base-3... when files of
// that name exist - saving never clobbers an earlier tree.
static std::string treeOutputName(const std::string& projectDir, const std::string& base) {
    namespace fs = std::filesystem;
    std::string name = base;
    for (int n = 2; fs::exists(fs::path(projectDir) / "res" / "models" / "trees" / (name + ".obj")); ++n)
        name = base + "-" + std::to_string(n);
    return name;
}

static std::string treePresetSlug(const char* presetName) {
    std::string s = presetName;
    for (char& c : s) c = c == ' ' ? '-' : (char)std::tolower((unsigned char)c);
    return s;
}

// Tools > Tree Generator: author a low-poly tree procedurally (treegen) with a
// live turntable preview, then bake it into res/models/trees as an ordinary
// Model object. Deterministic in the seed, so the same knobs always give the
// same tree - "5 oak variants" is 5 seeds, not a mesh library.
//
// Layout (docs/tree-generator.md): presets + seed | the tree (hero) |
// parameter tabs, and a footer that states what a save writes.
void App::drawTreeGeneratorWindow() {
    if (!showTreeGenerator_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(1200.0f), scaled(800.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Tree Generator", &showTreeGenerator_)) {
        ImGui::End();
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    const theme::Semantics& sem = theme::semantics();
    treegen::Params& p = treeParams_;
    bool dirty = false;  // a parameter changed this frame
    const std::vector<treegen::Preset>& presets = treegen::presets();
    if (treePreset_ < 0 || treePreset_ >= (int)presets.size()) treePreset_ = 0;
    const float upm = project_.settings.unitsPerMeter > 0.0f ? project_.settings.unitsPerMeter : 1.0f;

    const float footerH = ImGui::GetFrameHeight() + scaled(22.0f);
    const float bodyH = ImGui::GetContentRegionAvail().y - footerH;
    const float railW = scaled(200.0f), paramW = scaled(330.0f);
    auto vline = [&]() {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y),
                                            ImGui::GetColorU32(sem.border));
    };
    auto dimWrapped = [&](const char* text) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.9f);
        ImGui::TextWrapped("%s", text);
        ImGui::PopFont();
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    };

    // ---- left rail: start from a preset, then the seed --------------------
    ImGui::BeginChild("treeparams", ImVec2(railW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ui::Caption("Start from");
    {
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float tileW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const float tileH = tileW * 0.92f;
        for (int i = 0; i < (int)presets.size(); ++i) {
            if (i % 2) ImGui::SameLine(0.0f, gap);
            const ImVec2 t0 = ImGui::GetCursorScreenPos();
            const bool sel = treePreset_ == i;
            if (ImGui::InvisibleButton(presets[i].name, ImVec2(tileW, tileH))) {
                // A preset is a shape, not a seed: keep the seed.
                const std::string oldSlug = treePresetSlug(presets[treePreset_].name);
                treePreset_ = i;
                const uint32_t keepSeed = p.seed;
                p = presets[i].params;
                p.seed = keepSeed;
                if (!treeNameTouched_ || oldSlug == treeName_)
                    std::snprintf(treeName_, sizeof treeName_, "%s",
                                  treePresetSlug(presets[i].name).c_str());
                dirty = true;
            }
            uiscript::markLastItemChecked(sel);
            const float hov = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 t1(t0.x + tileW, t0.y + tileH);
            const float rr = ImGui::GetStyle().FrameRounding + 3.0f;
            ImVec4 bg = sem.surface;
            bg.w = sel ? 1.0f : 0.45f + 0.4f * hov;
            dl->AddRectFilled(t0, t1, ImGui::GetColorU32(bg), rr);
            dl->AddRect(t0, t1, ImGui::GetColorU32(sel ? sem.accent : sem.border), rr, 0,
                        sel ? 1.5f : 1.0f);
            const float fs = ImGui::GetFontSize();
            drawTreeSilhouette(dl, ImVec2(t0.x + tileW * 0.5f, t0.y + (tileH - fs) * 0.45f),
                               (tileH - fs * 1.6f), i, presets[i].params);
            const ImVec2 ts = ImGui::CalcTextSize(presets[i].name);
            dl->AddText(ImVec2(t0.x + (tileW - ts.x) * 0.5f, t1.y - fs * 1.35f),
                        ImGui::GetColorU32(sel ? sem.text : sem.textDim), presets[i].name);
        }
    }
    dimWrapped("A preset is a shape - it keeps your seed.");
    ImGui::Spacing();
    ui::Caption("Seed");
    {
        const float diceW = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - diceW - ImGui::GetStyle().ItemSpacing.x);
        int seed = (int)p.seed;
        if (ImGui::InputInt("##Seed", &seed, 0, 0)) {
            p.seed = (uint32_t)(seed < 0 ? 0 : seed);
            dirty = true;
        }
        ImGui::SameLine();
        const ImVec2 d0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("Roll", ImVec2(diceW, diceW))) {
            // xorshift the current seed for a new-but-reproducible value
            uint32_t s = p.seed ? p.seed : 0x1234567u;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            p.seed = s;
            dirty = true;
        }
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rr = ImGui::GetStyle().FrameRounding;
        dl->AddRectFilled(d0, ImVec2(d0.x + diceW, d0.y + diceW),
                          ImGui::GetColorU32(hov ? sem.surface : sem.field), rr);
        dl->AddRect(d0, ImVec2(d0.x + diceW, d0.y + diceW), ImGui::GetColorU32(sem.border), rr);
        // a die: rounded square with five pips
        const float m = diceW * 0.27f, pr = diceW * 0.05f;
        const ImVec2 a(d0.x + m, d0.y + m), b(d0.x + diceW - m, d0.y + diceW - m);
        dl->AddRect(a, b, ImGui::GetColorU32(sem.text), diceW * 0.08f, 0, 1.2f);
        const ImVec2 pips[5] = {{a.x + (b.x - a.x) * 0.28f, a.y + (b.y - a.y) * 0.28f},
                                {b.x - (b.x - a.x) * 0.28f, a.y + (b.y - a.y) * 0.28f},
                                {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f},
                                {a.x + (b.x - a.x) * 0.28f, b.y - (b.y - a.y) * 0.28f},
                                {b.x - (b.x - a.x) * 0.28f, b.y - (b.y - a.y) * 0.28f}};
        for (const ImVec2& q : pips) dl->AddCircleFilled(q, pr, ImGui::GetColorU32(sem.text));
        if (hov) ImGui::SetTooltip("Roll a new seed (shape stays, layout varies)");
    }
    dimWrapped("Same shape family, new layout. Five variants = five seeds (~50 KB each).");

    // Trees this project already has from the generator: re-open to adjust.
    {
        static std::vector<std::string> saved;
        static double scannedAt = -10.0;
        static std::string scannedDir;
        if (ImGui::GetTime() - scannedAt > 1.0 || scannedDir != project_.dir) {
            scannedAt = ImGui::GetTime();
            scannedDir = project_.dir;
            saved.clear();
            std::error_code ec;
            const std::filesystem::path dir =
                std::filesystem::path(project_.dir) / "res" / "models" / "trees";
            if (std::filesystem::is_directory(dir, ec))
                for (const auto& e : std::filesystem::directory_iterator(dir, ec))
                    if (e.path().extension() == ".tree") saved.push_back(e.path().stem().string());
            std::sort(saved.begin(), saved.end());
        }
        if (!saved.empty()) {
            ImGui::Spacing();
            ui::Caption("In this project", std::to_string(saved.size()).c_str());
            for (const std::string& stem : saved) {
                ImGui::PushID(stem.c_str());
                if (ui::ListRow(stem.c_str(), nullptr, false, ui::Glyph::Box))
                    openTreeInGenerator("res/models/trees/" + stem + ".obj");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open res/models/trees/%s.obj's parameters.\n"
                                      "Saving makes a new variant - placed copies stay as they are.",
                                      stem.c_str());
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    vline();
    ImGui::SameLine(0.0f, 0.0f);

    // ---- centre: the tree ------------------------------------------------------
    const float midW = ImGui::GetContentRegionAvail().x - paramW;
    ImGui::BeginChild("treepreview", ImVec2(midW > 1 ? midW : 1, bodyH),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int pw = (int)avail.x < 1 ? 1 : (int)avail.x;
        const int ph = (int)avail.y < 1 ? 1 : (int)avail.y;
        if (treeGenSpin_) {
            treeGenAngle_ += io.DeltaTime * 22.0f;
            if (treeGenAngle_ > 360.0f) treeGenAngle_ -= 360.0f;
        }
        const ImVec2 imgPos = ImGui::GetCursorScreenPos();
        const ImVec2 imgMax(imgPos.x + pw, imgPos.y + ph);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rr = ImGui::GetStyle().FrameRounding + 4.0f;

        if (!treeShowImpostor_) {
            Viewport::TreePreviewDesc desc;
            desc.version = treePreviewVersion_;
            desc.bark = &treeMesh_.bark;
            desc.leaves = &treeMesh_.leaves;
            desc.barkRgba = treeBarkTex_.rgba.data();
            desc.barkW = treeBarkTex_.w;
            desc.barkH = treeBarkTex_.h;
            if (!treeMesh_.leaves.empty()) {
                desc.leafRgba = treeLeafTex_.rgba.data();
                desc.leafW = treeLeafTex_.w;
                desc.leafH = treeLeafTex_.h;
            }
            for (int i = 0; i < 3; ++i)
                desc.center[i] = (treeMesh_.min[i] + treeMesh_.max[i]) * 0.5f;
            desc.minY = treeMesh_.min[1];
            const float dx = treeMesh_.max[0] - treeMesh_.min[0];
            const float dy = treeMesh_.max[1] - treeMesh_.min[1];
            const float dz = treeMesh_.max[2] - treeMesh_.min[2];
            // Frame the TREE, not its bounding sphere: the view is wider than
            // tall, so the height decides - the sphere of a 7 m oak left the
            // crown a fifth of the view over a field of floor.
            const float aspect = (float)pw / (float)ph;
            const float halfW = 0.5f * std::max(dx, dz) / std::max(1.0f, aspect);
            // The preview camera sits at 2.4 radii with a 45 deg field, so the
            // view's half height is ~one radius: 1.15 leaves a margin.
            desc.radius = 1.15f * std::max(0.5f * dy, halfW) + 0.02f;
            if (desc.radius < 0.01f) desc.radius = 0.01f;
            desc.angleDeg = treeGenAngle_;
            desc.pitchDeg = treeGenPitch_;
            desc.zoom = treeGenZoom_;
            desc.panX = treeGenPanX_;
            desc.panY = treeGenPanY_;
            desc.displayMode = treeGenDisplayMode_;
            if (const uint32_t tex = viewport_.renderTreePreview(pw, ph, desc))
                dl->AddImageRounded((ImTextureID)(intptr_t)tex, imgPos, imgMax, ImVec2(0, 1),
                                    ImVec2(1, 0), IM_COL32_WHITE, rr);
        } else {
            // The distant card the bake would make, captured on the CPU: the
            // view the turntable angle faces, as players see it past six tree
            // heights.
            if (treeImpVersion_ != treePreviewVersion_ || treeImpViewsBuilt_ != treeImpostorViews_ ||
                !treeImpTex_) {
                std::vector<unsigned char> px;
                std::string err;
                if (treegen::captureImpostor(treeMesh_, treeBarkTex_, treeLeafTex_, treeImpSize_,
                                             treeImpostorViews_, px, &err)) {
                    if (!treeImpTex_) glGenTextures(1, &treeImpTex_);
                    glBindTexture(GL_TEXTURE_2D, treeImpTex_);
                    glUploadTexRgba(treeImpSize_ * 4, treeImpSize_ * (treeImpostorViews_ / 4), px.data());
                }
                treeImpVersion_ = treePreviewVersion_;
                treeImpViewsBuilt_ = treeImpostorViews_;
            }
            ImVec4 bg = sem.ground;
            dl->AddRectFilled(imgPos, imgMax, ImGui::GetColorU32(bg), rr);
            const int views = treeImpViewsBuilt_ > 0 ? treeImpViewsBuilt_ : 8;
            int view = (int)std::lround(treeGenAngle_ / 360.0f * views) % views;
            if (view < 0) view += views;
            const float rows = (float)(views / 4);
            const ImVec2 uv0((view % 4) * 0.25f, (view / 4) / rows);
            const ImVec2 uv1(uv0.x + 0.25f, uv0.y + 1.0f / rows);
            const float side = std::min((float)pw, (float)ph) * 0.86f;
            const ImVec2 c0(imgPos.x + (pw - side) * 0.5f, imgPos.y + (ph - side) * 0.5f);
            dl->AddRect(c0, ImVec2(c0.x + side, c0.y + side), ImGui::GetColorU32(sem.border), 0.0f);
            if (treeImpTex_)
                dl->AddImage((ImTextureID)(intptr_t)treeImpTex_, c0, ImVec2(c0.x + side, c0.y + side), uv0, uv1);
            char info[96];
            std::snprintf(info, sizeof info, "view %d of %d \xC2\xB7 %d px card \xC2\xB7 2 triangles",
                          view + 1, views, treeImpSize_);
            const ImVec2 ts = ImGui::CalcTextSize(info);
            dl->AddText(ImVec2(c0.x + (side - ts.x) * 0.5f, c0.y + side + scaled(6.0f)),
                        ImGui::GetColorU32(sem.textDim), info);
        }
        dl->AddRect(imgPos, imgMax, ImGui::GetColorU32(sem.border), rr);

        // The whole view is the orbit handle; the overlays sit on top of it.
        ImGui::SetCursorScreenPos(imgPos);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Tree preview##tree_prev_in", ImVec2((float)pw, (float)ph), ui::kPreviewButtons);
        const bool hovered = ImGui::IsItemHovered();
        const ui::PreviewNav nav = ui::PreviewNavigation();
        if (nav.wheel != 0.0f)
            treeGenZoom_ = std::clamp(treeGenZoom_ * std::pow(1.15f, nav.wheel), 0.2f, 12.0f);
        if (nav.orbiting()) {
            treeGenAngle_ += nav.orbit.x * 0.5f;
            treeGenPitch_ = std::clamp(treeGenPitch_ + nav.orbit.y * 0.4f, -30.0f, 85.0f);
            treeGenSpin_ = false;  // grabbing the camera stops the turntable
        }
        {
            const float k = 2.0f / ((float)ph * treeGenZoom_);  // a view height = ~2 radii
            treeGenPanX_ = std::clamp(treeGenPanX_ - nav.pan.x * k, -5.0f, 5.0f);
            treeGenPanY_ = std::clamp(treeGenPanY_ + nav.pan.y * k, -5.0f, 5.0f);
        }
        if (nav.recentre) treeGenPanX_ = treeGenPanY_ = 0.0f;
        if (hovered && ImGui::IsMouseDoubleClicked(0)) {
            treeGenAngle_ = 40.0f;
            treeGenPitch_ = 18.0f;
            treeGenZoom_ = 1.0f;
            treeGenPanX_ = treeGenPanY_ = 0.0f;
        }

        // Top-left: the view pill.
        const float m = scaled(12.0f);
        const OverlayPillItem pill[] = {
            {"Spin", "Spin", treeGenSpin_},
            {"Wireframe", "Wireframe", treeGenDisplayMode_ == 1},
            {nullptr, nullptr, false},
            {"Near", "Near", !treeShowImpostor_},
            {"Impostor", "Impostor", treeShowImpostor_},
        };
        switch (overlayPill(ImVec2(imgPos.x + m, imgPos.y + m), pill, 5)) {
            case 0: treeGenSpin_ = !treeGenSpin_; break;
            case 1: treeGenDisplayMode_ = treeGenDisplayMode_ == 1 ? 0 : 1; break;
            case 3: treeShowImpostor_ = false; break;
            case 4: treeShowImpostor_ = true; break;
            default: break;
        }

        // Top-right: the PS2 budget, judged against the docs' thresholds.
        {
            const int tris = treeMesh_.triangles();
            const int barkT = treeMesh_.barkTriangles(), leafT = treeMesh_.leafTriangles();
            const int budget = 1800;
            const float cw = scaled(230.0f), ch = scaled(66.0f);
            const ImVec2 c0(imgMax.x - m - cw, imgPos.y + m), c1(imgMax.x - m, imgPos.y + m + ch);
            ui::OverlayPanel(dl, c0, c1);
            const float pad = scaled(10.0f);
            const ImVec4 numCol = tris > 3000 ? sem.danger : tris > budget ? sem.warn : sem.ok;
            char num[16];
            std::snprintf(num, sizeof num, "%d", tris);
            float x = c0.x + pad;
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.35f);
                dl->AddText(ImVec2(x, c0.y + pad * 0.6f), ImGui::GetColorU32(numCol), num);
                x += ImGui::CalcTextSize(num).x + scaled(6.0f);
                ImGui::PopFont();
            }
            dl->AddText(ImVec2(x, c0.y + pad * 0.6f + scaled(4.0f)), ImGui::GetColorU32(sem.text), "triangles");
            {
                ui::FontScope mono(ui::monoFont());
                char bud[32];
                std::snprintf(bud, sizeof bud, "budget %d", budget);
                const ImVec2 bs = ImGui::CalcTextSize(bud);
                dl->AddText(ImVec2(c1.x - pad - bs.x, c0.y + pad * 0.6f + scaled(4.0f)),
                            ImGui::GetColorU32(sem.textDim), bud);
            }
            const float by = c0.y + scaled(36.0f), bh = scaled(5.0f), bw = cw - pad * 2.0f;
            const float scale = bw / (float)std::max(budget, tris);
            auto rgb = [](const float* c) { return ImGui::GetColorU32(ImVec4(c[0] * 1.3f, c[1] * 1.3f, c[2] * 1.3f, 1.0f)); };
            dl->AddRectFilled(ImVec2(c0.x + pad, by), ImVec2(c0.x + pad + bw, by + bh), ImGui::GetColorU32(sem.field), bh);
            dl->AddRectFilled(ImVec2(c0.x + pad, by), ImVec2(c0.x + pad + barkT * scale, by + bh), rgb(p.barkColor), bh);
            dl->AddRectFilled(ImVec2(c0.x + pad + barkT * scale, by),
                              ImVec2(c0.x + pad + (barkT + leafT) * scale, by + bh), rgb(p.leafColor), bh);
            char leg[64];
            std::snprintf(leg, sizeof leg, "%d bark   %d leaves", barkT, leafT);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.88f, ImVec2(c0.x + pad, by + bh + scaled(5.0f)),
                        ImGui::GetColorU32(sem.textDim), leg);
            ImGui::SetCursorScreenPos(c0);
            ImGui::InvisibleButton("##treebudget", ImVec2(cw, ch));
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The PS2 is comfortable under ~1800 triangles a tree;\n"
                                  "past 3000 trim leaves or Detail. A distant impostor\n"
                                  "costs 2 triangles once the tree is far away.");
        }

        // Bottom: what you are looking at, and how to move it.
        {
            char cap[96];
            if (std::fabs(upm - 1.0f) < 1e-4f)
                std::snprintf(cap, sizeof cap, "%.1f m tall \xC2\xB7 right-drag to orbit \xC2\xB7 middle-drag to pan",
                              treeMesh_.max[1] - treeMesh_.min[1]);
            else
                std::snprintf(cap, sizeof cap, "%.1f m tall (%.1f u) \xC2\xB7 right-drag to orbit \xC2\xB7 middle-drag to pan",
                              (treeMesh_.max[1] - treeMesh_.min[1]) / upm, treeMesh_.max[1] - treeMesh_.min[1]);
            ui::FontScope mono(ui::monoFont());
            const ImVec2 ts = ImGui::CalcTextSize(cap);
            const float pad = scaled(8.0f);
            const ImVec2 c0(imgPos.x + (pw - ts.x) * 0.5f - pad, imgMax.y - m - ts.y - pad);
            ui::OverlayPanel(dl, c0, ImVec2(c0.x + ts.x + pad * 2.0f, c0.y + ts.y + pad));
            dl->AddText(ImVec2(c0.x + pad, c0.y + pad * 0.5f), ImGui::GetColorU32(sem.textDim), cap);
        }
    }
    ImGui::EndChild();
    vline();
    ImGui::SameLine(0.0f, 0.0f);

    // ---- right: the parameters, one job per tab ---------------------------
    ImGui::BeginChild("treetabs", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        static const char* kTabs[] = {"Trunk##treetab", "Branches##treetab", "Leaves##treetab",
                                      "Look##treetab", "Detail##treetab"};
        static int shownFrame = -10;
        const bool force = ImGui::GetFrameCount() - shownFrame > 1;
        if (ui::BeginQuietTabBar("##treetabbar")) {
            for (int i = 0; i < 5; ++i) {
                const ImGuiTabItemFlags fl = force && i == treeTab_ ? ImGuiTabItemFlags_SetSelected : 0;
                if (ui::QuietTab(kTabs[i], i == treeTab_, fl) && !force) treeTab_ = i;
            }
            ui::EndQuietTabBar();
        }
        shownFrame = ImGui::GetFrameCount();
        ImGui::Spacing();
        const treegen::Params& ref = presets[treePreset_].params;
        ImGui::BeginChild("##treetabbody", ImVec2(0, 0));
        const float capW = std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, scaled(90.0f), scaled(120.0f));
        auto helpUnder = [&](const char* text) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + capW);
            dimWrapped(text);
        };
        {
            ui::InspectorRows rows(capW, capW);
            switch (treeTab_) {
                case 0: {
                    const char* hfmt = std::fabs(upm - 1.0f) < 1e-4f ? "%.1f m" : "%.1f u";
                    dirty |= ui::SliderFloat("Height", &p.height, 0.5f, 20.0f, hfmt);
                    helpUnder("Scales the whole tree - girth and leaves follow");
                    // Thickness and leaf size are FRACTIONS of height (shown
                    // as percent), so Height scales the whole tree instead of
                    // stretching it thinner.
                    float thickPct = p.thickness * 100.0f;
                    if (ui::SliderFloat("Thickness", &thickPct, 0.5f, 15.0f, "%.1f%% of h")) {
                        p.thickness = thickPct * 0.01f;
                        dirty = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Trunk base radius as a share of height (= %.2f units)",
                                          p.thickness * p.height);
                    dirty |= ui::SliderFloat("Root flare", &p.flare, 1.0f, 3.0f, "%.2f");
                    dirty |= ui::SliderFloat("Taper", &p.taper, 0.1f, 0.95f, "%.2f");
                    dirty |= ui::SliderFloat("Gnarliness", &p.gnarliness, 0.0f, 0.5f, "%.2f");
                    dirty |= ui::SliderFloat("Upward pull", &p.sweep, -0.3f, 0.5f, "%.2f");
                    break;
                }
                case 1: {
                    const char* kCrown[] = {"Spread (broadleaf)", "Conical (conifer)"};
                    dirty |= ui::Combo("Crown", &p.crown, kCrown, 2);
                    ui::RowHelp("Spread: branches spiral up every parent, the shape emerges.\n"
                                "Conical: the trunk runs to the apex and carries whorls of\n"
                                "boughs that shorten toward the top - a spruce/fir habit.");
                    dirty |= ui::SliderInt("Levels", &p.levels, 1, 4);
                    int first = 0;
                    if (p.crown == 1) {
                        dirty |= ui::SliderInt("Whorls", &p.whorls, 2, 20);
                        dirty |= ui::SliderInt("Per whorl", &p.children[0], 1, 8);
                        first = 1;
                    }
                    for (int i = first; i + 1 < p.levels && i < 3; ++i) {
                        char lbl[32];
                        std::snprintf(lbl, sizeof(lbl), "Children L%d", i);
                        dirty |= ui::SliderInt(lbl, &p.children[i], 0, 12);
                    }
                    dirty |= ui::SliderFloat("Branch angle", &p.branchAngle, 5.0f, 90.0f, "%.0f deg");
                    if (p.crown == 1)
                        ui::RowHelp("Conical: the tilt at mid-trunk. Lower whorls droop past it,\n"
                                    "the ones near the apex sweep up.");
                    dirty |= ui::SliderFloat("Angle jitter", &p.angleJitter, 0.0f, 30.0f, "%.0f deg");
                    dirty |= ui::SliderFloat("Length ratio", &p.lengthRatio, 0.2f, 0.95f, "%.2f");
                    if (p.crown != 1)
                        dirty |= ui::SliderFloat("Length taper", &p.lengthTaper, 0.0f, 0.9f, "%.2f");
                    dirty |= ui::SliderFloat("Radius ratio", &p.radiusRatio, 0.2f, 0.9f, "%.2f");
                    dirty |= ui::SliderFloat("Spawn start", &p.spawnStart, 0.0f, 0.8f, "%.2f");
                    break;
                }
                case 2: {
                    dirty |= ui::SliderInt("Count", &p.leafCount, 0, 600);
                    helpUnder("Two triangles per leaf card - the biggest budget lever");
                    float leafPct = p.leafSize * 100.0f;
                    if (ui::SliderFloat("Size", &leafPct, 1.0f, 30.0f, "%.1f%% of h")) {
                        p.leafSize = leafPct * 0.01f;
                        dirty = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Leaf card width as a share of height (= %.2f units)",
                                          p.leafSize * p.height);
                    dirty |= ui::SliderFloat("Aspect", &p.leafAspect, 0.5f, 2.5f, "%.2f");
                    dirty |= ui::SliderInt("On outer levels", &p.leafLevels, 1, 4);
                    const char* kLeaf[] = {"Broadleaf", "Needles", "Single leaf", "Baked broadleaf"};
                    dirty |= ui::Combo("Foliage", &p.leafStyle, kLeaf, 4);
                    break;
                }
                case 3: {
                    const char* kBark[] = {"Rough", "Birch", "Plates"};
                    dirty |= ui::Combo("Bark", &p.barkStyle, kBark, 3);
                    dirty |= ui::ColorHex("Bark light", p.barkColor);
                    dirty |= ui::ColorHex("Bark dark", p.barkColor2);
                    dirty |= ui::ColorHex("Leaf light", p.leafColor);
                    dirty |= ui::ColorHex("Leaf dark", p.leafColor2);
                    break;
                }
                default: {
                    dirty |= ui::SliderInt("Trunk sides", &p.sides, 3, 12);
                    dirty |= ui::SliderInt("Twig sides", &p.sidesMin, 3, 12);
                    dirty |= ui::SliderInt("Trunk rings", &p.rings, 1, 12);
                    dirty |= ui::SliderInt("Twig rings", &p.ringsMin, 1, 12);
                    helpUnder("Sides x rings per branch segment: fewer is cheaper and blockier");
                    break;
                }
            }
        }
        // Undo for one tab: back to the preset this tree started from.
        ImGui::Spacing();
        static const char* kTabNames[] = {"Trunk", "Branches", "Leaves", "Look", "Detail"};
        char reset[64];
        std::snprintf(reset, sizeof reset, "Reset %s to %s", kTabNames[treeTab_], presets[treePreset_].name);
        if (ui::GhostButton(reset)) {
            switch (treeTab_) {
                case 0:
                    p.height = ref.height, p.thickness = ref.thickness, p.flare = ref.flare;
                    p.taper = ref.taper, p.gnarliness = ref.gnarliness, p.sweep = ref.sweep;
                    break;
                case 1:
                    p.crown = ref.crown, p.whorls = ref.whorls, p.levels = ref.levels;
                    for (int i = 0; i < 3; ++i) p.children[i] = ref.children[i];
                    p.branchAngle = ref.branchAngle, p.angleJitter = ref.angleJitter;
                    p.lengthRatio = ref.lengthRatio, p.lengthTaper = ref.lengthTaper;
                    p.radiusRatio = ref.radiusRatio, p.spawnStart = ref.spawnStart;
                    break;
                case 2:
                    p.leafCount = ref.leafCount, p.leafSize = ref.leafSize;
                    p.leafAspect = ref.leafAspect, p.leafLevels = ref.leafLevels;
                    p.leafStyle = ref.leafStyle;
                    break;
                case 3:
                    p.barkStyle = ref.barkStyle;
                    for (int i = 0; i < 3; ++i) {
                        p.barkColor[i] = ref.barkColor[i], p.barkColor2[i] = ref.barkColor2[i];
                        p.leafColor[i] = ref.leafColor[i], p.leafColor2[i] = ref.leafColor2[i];
                    }
                    break;
                default:
                    p.sides = ref.sides, p.sidesMin = ref.sidesMin;
                    p.rings = ref.rings, p.ringsMin = ref.ringsMin;
                    break;
            }
            dirty = true;
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    // ---- footer: the output ---------------------------------------------------
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x - ImGui::GetStyle().WindowPadding.x, a.y),
                                            ImVec2(a.x + ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x, a.y),
                                            ImGui::GetColorU32(sem.border));
    }
    ImGui::Dummy(ImVec2(0, scaled(8.0f)));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(150.0f));
    if (ImGui::InputText("##treename", treeName_, sizeof(treeName_))) treeNameTouched_ = true;
    ImGui::SameLine();
    std::string base = sanitizeAssetName(treeName_);
    if (base.empty()) base = "tree";
    const std::string outName = treeOutputName(project_.dir, base);
    {
        std::string writes = "\xE2\x86\x92 res/models/trees/" + outName + ".obj + .mtl + " +
                             (treeMesh_.leaves.empty() ? "1 png" : "2 png");
        if (treeGenImpostor_) writes += " + impostor";
        ui::FontScope mono(ui::monoFont());
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", writes.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(outName != base ? "A tree called %s exists - this one becomes %s."
                                              : "Also %s.tree - the parameters, so the tree can be re-opened here.",
                              outName != base ? base.c_str() : outName.c_str(), outName.c_str());
    }
    // Right-aligned group: impostor switch + views, Save, Add.
    const float addW = ImGui::CalcTextSize("Add to scene").x + ImGui::GetStyle().FramePadding.x * 2.6f;
    const float saveW = ImGui::CalcTextSize("Save to project").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    static const char* kViews[] = {"4", "8", "16"};
    float segW = 0.0f;
    for (const char* v : kViews) segW += ImGui::CalcTextSize(v).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    segW += 6.0f;
    const float swW = ImGui::GetFrameHeight() * 1.9f + ImGui::CalcTextSize("Distant impostor").x;
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const float groupW = swW + sp + segW + sp + ImGui::CalcTextSize("views").x + sp * 3.0f + saveW + sp + addW;
    ImGui::SameLine();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (right - groupW > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(right - groupW);
    ui::Switch("Distant impostor", &treeGenImpostor_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Also bake a 2-triangle camera-facing card shown beyond six tree heights\n"
                          "(adjust in the object's Distance and LOD). GPU capture, CPU fallback.");
    if (treeGenImpostor_) {
        ImGui::SameLine();
        int vi = treeImpostorViews_ == 4 ? 0 : treeImpostorViews_ == 16 ? 2 : 1;
        if (ui::Segmented("##treeviews", kViews, 3, &vi)) treeImpostorViews_ = 4 << vi;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Capture views around the tree: more = smoother turning, more VRAM.");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("views");
    }
    ImGui::SameLine(0.0f, sp * 3.0f);
    ImGui::BeginDisabled(treeMesh_.bark.empty());
    if (ImGui::Button("Save to project")) addTreeToScene(false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Write the asset without placing it - drag it in from the Asset Browser later.");
    ImGui::SameLine();
    if (ui::PrimaryButton("Add to scene", addW, !treeMesh_.bark.empty())) addTreeToScene(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bake the files listed on the left and drop a Model object\n"
                          "into the current scene.");

    if (dirty) treePreviewDirty_ = true;
    if (treePreviewDirty_) rebuildTreePreview();

    ImGui::End();
}

// "Add to scene" / "Save to project" from the Tree Generator: bakes the current
// tree's assets (+ the `.tree` parameter sidecar) and, when placing, inserts a
// Model object pointing at them (addModelObject does the naming + commit). A
// name clash makes a numbered variant, so re-saving never clobbers.
void App::addTreeToScene(bool place) {
    std::string base = sanitizeAssetName(treeName_);
    if (base.empty()) base = "tree";
    // Distinct asset file name per generated tree so two trees in a project
    // don't share (and overwrite) each other's .obj/textures.
    const std::string name = treeOutputName(project_.dir, base);

    std::string objRel, err;
    if (!treegen::writeAssets(project_.dir, name, treeParams_, treeMesh_,
                              treeBarkTex_, treeLeafTex_, &objRel, &err)) {
        statusMessage_ = "Tree export failed: " + err;
        return;
    }
    {
        const std::vector<treegen::Preset>& presets = treegen::presets();
        const std::string presetName =
            treePreset_ >= 0 && treePreset_ < (int)presets.size() ? presets[treePreset_].name : "";
        std::ofstream side(std::filesystem::path(project_.dir) / "res" / "models" / "trees" /
                               (name + ".tree"),
                           std::ios::trunc);
        side << treeParamsJson(treeParams_, presetName);
    }
    std::string impostorRel, impostorBackend;
    if (treeGenImpostor_ &&
        !treegen::writeImpostor(project_.dir, name, treeMesh_, treeBarkTex_,
                                treeLeafTex_, &impostorRel, &err, 128, treeImpostorViews_, gpuBakes_, &impostorBackend)) {
        statusMessage_ = "Impostor export failed: " + err;
        return;
    }
    if (!place) {
        statusMessage_ = "Saved tree '" + name + "' to " + objRel + " (" +
                         std::to_string(treeMesh_.triangles()) + " tris)" +
                         (impostorRel.empty() ? "" : " - impostor " + impostorBackend);
        return;
    }
    addModelObject(objRel, nullptr, false);
    if (!impostorRel.empty()) {
        SceneObject& o = project_.objects().back();
        o.impostorPath = impostorRel;
        o.impostorBillboard = true;
        o.impostorViews = treeImpostorViews_;
        o.impostorDistance = treeParams_.height * 6.0f;
    }
    commitChange();
    statusMessage_ = "Added tree '" + name + "' (" +
                     std::to_string(treeMesh_.triangles()) + " tris)" +
                     (impostorRel.empty() ? "" : " - impostor " + impostorBackend);
}

bool App::openTreeInGenerator(const std::string& objRel) {
    std::filesystem::path side = std::filesystem::path(project_.dir) / objRel;
    side.replace_extension(".tree");
    std::ifstream in(side);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    treegen::Params loaded;
    std::string preset;
    if (!treeParamsFromJson(ss.str(), loaded, &preset)) return false;
    treeParams_ = loaded;
    const std::vector<treegen::Preset>& presets = treegen::presets();
    for (int i = 0; i < (int)presets.size(); ++i)
        if (preset == presets[i].name) treePreset_ = i;
    std::snprintf(treeName_, sizeof treeName_, "%s", side.stem().string().c_str());
    treeNameTouched_ = true;
    treePreviewDirty_ = true;
    showTreeGenerator_ = true;
    ImGui::SetWindowFocus("Tree Generator");
    return true;
}

// Picks one of the project's Font Manager entries by name. An empty reference
// means the default entry, so this shows fonts[0] rather than a blank.
bool App::fontCombo(std::string& fontRef) {
    bool changed = false;
    const std::string current =
        fontRef.empty() ? project_.defaultFontName() : fontRef;
    ImGui::SetNextItemWidth(scaled(200.0f));
    // A label-left row inside ui::InspectorRows; the stock combo elsewhere.
    if (ui::BeginCombo("Font", current.c_str())) {
        for (const GameFont& gf : project_.fonts) {
            if (ImGui::Selectable(gf.name.c_str(), gf.name == current)) {
                fontRef = gf.name;
                changed = true;
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Manage fonts...")) showFontManager_ = true;
        ImGui::EndCombo();
    }
    return changed;
}

int App::importHudImageInto(std::vector<HudImage>& target) {
    const std::string src = pickPngFile();
    if (src.empty()) return -1;
    return importHudImageFile(src, target);
}

int App::importHudImageFile(const std::string& src, std::vector<HudImage>& target) {
    const std::filesystem::path srcPath(src);
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir = std::filesystem::path(project_.dir) / "res" / "hud";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(srcPath, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        statusMessage_ = "HUD image import failed: " + ec.message();
        return -1;
    }

    HudImage h;
    h.name = srcPath.stem().string();
    h.imagePath = "res/hud/" + fileName;
    hudTexCache_.erase(h.imagePath);  // reload if replaced
    if (const HudTexture* t = hudTexture(h.imagePath)) {
        h.size[0] = (float)t->w;
        h.size[1] = (float)t->h;
    }
    target.push_back(std::move(h));
    return (int)target.size() - 1;
}

void App::importHudImage() {
    const int i = importHudImageInto(project_.hud);
    if (i < 0) return;
    selectedHud_ = i;
    uiFxSel_ = 0;
    saveAll("Saved");
}

// Texture-bake controls shared by HUD images and the USE prompt: the PS2
// only accepts 8/16/32/64/128/256/512-sized textures; the build resizes the
// imported PNG into .res-baked to that. "Auto" picks the nearest valid size,
// so a mis-sized import just works. Returns true when a setting changed.
bool App::hudBakeControls(HudImage& h) {
    bool changed = false;
    auto nearestValid = [](int v) {
        static const int V[] = {8, 16, 32, 64, 128, 256, 512};
        int best = V[0], bd = 1 << 30;
        for (int d : V) {
            const int dd = v > d ? v - d : d - v;
            if (dd < bd) { bd = dd; best = d; }
        }
        return best;
    };
    auto isValid = [&](int v) { return v > 0 && v == nearestValid(v); };
    auto dimCombo = [&](const char* label, int& dim) {
        static const int vals[] = {0, 8, 16, 32, 64, 128, 256, 512};
        static const char* names[] = {"Auto", "8",   "16",  "32",
                                      "64",   "128", "256", "512"};
        int cur = 0;
        for (int i = 0; i < 8; ++i)
            if (vals[i] == dim) { cur = i; break; }
        if (ui::Combo(label, &cur, names, 8)) {
            dim = vals[cur];
            changed = true;
        }
    };

    ui::Caption("Texture (baked for PS2)");
    int sw = 0, sh = 0;
    if (const HudTexture* t = hudTexture(h.imagePath)) { sw = t->w; sh = t->h; }
    if (sw > 0) {
        const bool bad = !isValid(sw) || !isValid(sh);
        if (bad) {
            char b[64];
            std::snprintf(b, sizeof(b), "Source %dx%d is not a PS2 size", sw, sh);
            ui::StatusLine(1, b);
        } else
            ImGui::TextDisabled("Source: %dx%d px", sw, sh);
    }
    dimCombo("Width##texw", h.texW);
    dimCombo("Height##texh", h.texH);

    // Colors: like the per-asset material quality, "(project default)"
    // follows Preferences > Textures; the others override - e.g. keep an
    // important element full color while the rest of the HUD is quantized.
    int q = h.texQuant == "none" ? 1
            : h.texQuant == "8bit" ? 2
            : h.texQuant == "4bit" ? 3
                                   : 0;
    const char* qn[] = {"Project default", "Full color (32-bit)",
                        "256 colors (8-bit)", "16 colors (4-bit)"};
    if (ui::Combo("Colors##hudq", &q, qn, 4)) {
        h.texQuant = q == 1 ? "none" : q == 2 ? "8bit" : q == 3 ? "4bit" : "";
        changed = true;
    }

    // Resolve "(project default)" for the baked readout.
    auto colorLabel = [](const std::string& qv) {
        return qv == "8bit"   ? "256 colors (8-bit)"
               : qv == "4bit" ? "16 colors (4-bit)"
                              : "Full color (32-bit)";
    };
    const std::string effQ =
        h.texQuant.empty() ? project_.settings.textureQuant : h.texQuant;
    const int bw = h.texW > 0 ? h.texW : (sw > 0 ? nearestValid(sw) : 0);
    const int bh = h.texH > 0 ? h.texH : (sh > 0 ? nearestValid(sh) : 0);
    if (h.texQuant.empty())
        ImGui::TextDisabled("Baked: %dx%d, %s (from project)", bw, bh,
                            colorLabel(effQ));
    else
        ImGui::TextDisabled("Baked: %dx%d, %s", bw, bh, colorLabel(effQ));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("Resized at build (the source in res/hud stays untouched).\n"
                          "The on-screen size is separate - the sprite is stretched.");
    return changed;
}

// UI Editor window (Tools > UI Editor): everything composited over the 3D
// scene, as one reorderable "screen stack" - the HUD images plus the effect
// layers (bloom+grading, film grain, motion blur, custom effects). The stack
// order is the game's draw order: entries above an effect layer stay crisp
// (e.g. the crosshair over the bloom), entries below are composited with it.
// Three columns: the On screen list (true draw order, top = drawn last) | a
// 512x448 preview edited in place (drag, corner handles, arrow nudge) | the
// selected element's inspector.
namespace {
constexpr int kBloomMark = -2;
constexpr int kGrainMark = -3;
constexpr int kMotionBlurMark = -4;
// Custom screen effect placements in the stack encode as kFxMarkBase - index
// (index into Project::screenFx). Any entry <= kFxMarkBase is a custom effect;
// the built-in effect marks (-2..-4) and HUD sprites (>= 0) stay clear of this
// range.
constexpr int kFxMarkBase = -100;
constexpr bool isFxMark(int e) { return e <= kFxMarkBase; }
constexpr int fxMarkIndex(int e) { return kFxMarkBase - e; }
constexpr int fxMark(int i) { return kFxMarkBase - i; }

ImU32 tint(const ImVec4& c, float a) {
    ImVec4 v = c;
    v.w *= a;
    return ImGui::GetColorU32(v);
}

// The kind badge the screen lists lead with: a letter on a tinted square
// (T text, B bar, I image, P prompt, fx effect), coloured by meaning.
void kindBadge(ImDrawList* dl, ImVec2 c, float s, const char* txt, const ImVec4& col) {
    dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f),
                      tint(col, 0.16f), s * 0.28f);
    ImFont* f = ui::boldFont() ? ui::boldFont() : ImGui::GetFont();
    const float fs = IM_TRUNC(s * 0.6f);
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt);
    dl->AddText(f, fs, ImVec2(IM_TRUNC(c.x - ts.x * 0.5f), IM_TRUNC(c.y - ts.y * 0.5f)), tint(col), txt);
}

struct RowResult {
    bool clicked = false, active = false, hovered = false, eye = false;
};

// One row of a screen list: an InvisibleButton named `label` (the --ui-script
// name; text after ## is not drawn), an optional drag grip, the kind badge,
// the name, a dim `meta` on the right and, when `eyeOn` is given, an eye that
// toggles "visible at game start" at the right end.
RowResult screenRow(const char* label, const char* meta, const char* badge, const ImVec4& badgeCol,
                    bool selected, bool dim, bool grip, const bool* eyeOn) {
    const theme::Semantics& sem = theme::semantics();
    RowResult r;
    const float fs = ImGui::GetFontSize();
    const float h = IM_TRUNC(ImGui::GetFrameHeight() + fs * 0.25f);
    const float w = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (eyeOn) ImGui::SetNextItemAllowOverlap();
    r.clicked = ImGui::InvisibleButton(label, ImVec2(w, h));
    r.active = ImGui::IsItemActive();
    r.hovered = ImGui::IsItemHovered();
    uiscript::markLastItemChecked(selected);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rr = ImGui::GetStyle().FrameRounding;
    const ImVec2 q(p.x + w, p.y + h);
    if (selected) {
        dl->AddRectFilled(p, q, tint(sem.accent, 0.16f), rr);
        dl->AddRect(p, q, tint(sem.accent, 0.33f), rr);
    } else if (r.hovered) {
        dl->AddRectFilled(p, q, tint(sem.surface, 0.8f), rr);
    }
    float x = p.x + fs * 0.4f;
    const float cy = p.y + h * 0.5f;
    if (grip) {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 2; ++j)
                dl->AddCircleFilled(ImVec2(x + fs * 0.12f + j * fs * 0.28f, cy + (i - 1) * fs * 0.28f),
                                    ImMax(1.0f, fs * 0.08f), tint(sem.textDim, 0.55f));
        x += fs * 0.8f;
    }
    const float bs = IM_TRUNC(fs * 1.2f);
    kindBadge(dl, ImVec2(x + bs * 0.5f, cy), bs, badge, badgeCol);
    x += bs + fs * 0.5f;
    const float eyeS = IM_TRUNC(fs * 1.25f);
    float right = q.x - fs * 0.4f - (eyeOn ? eyeS + fs * 0.25f : 0.0f);
    if (meta && *meta) {
        const float mfs = IM_TRUNC(fs * 0.86f);
        const float mw = ImGui::GetFont()->CalcTextSizeA(mfs, FLT_MAX, 0.0f, meta).x;
        dl->AddText(ImGui::GetFont(), mfs, ImVec2(right - mw, cy - mfs * 0.5f), tint(sem.textDim, 0.85f), meta);
        right -= mw + fs * 0.5f;
    }
    dl->PushClipRect(ImVec2(x, p.y), ImVec2(ImMax(x, right), q.y), true);
    dl->AddText(ImVec2(x, cy - fs * 0.5f),
                dim ? tint(sem.textDim) : tint(sem.text, selected ? 1.0f : 0.88f), label,
                ImGui::FindRenderedTextEnd(label));
    dl->PopClipRect();
    if (eyeOn) {
        const ImVec2 back = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(q.x - fs * 0.3f - eyeS, cy - eyeS * 0.5f));
        r.eye = ui::EyeToggle("Visible at game start", *eyeOn, eyeS, true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(*eyeOn ? "Visible at game start. Click: start hidden\n"
                                       "(a flow node shows it)."
                                     : "Hidden at game start (a flow node shows it).\n"
                                       "Click: visible at start.");
        ImGui::SetCursorScreenPos(back);
    }
    return r;
}

// A dim sub-heading of a screen list, with an optional note at the right.
void listHead(const char* left, const char* right) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float sfs = IM_TRUNC(fs * 0.86f);
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(0.0f, fs * 0.25f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* f = ImGui::GetFont();
    dl->AddText(f, sfs, ImVec2(p.x + fs * 0.4f, p.y), tint(sem.textDim, 0.85f), left);
    if (right) {
        const float rw = f->CalcTextSizeA(sfs, FLT_MAX, 0.0f, right).x;
        dl->AddText(f, sfs, ImVec2(p.x + w - rw - fs * 0.4f, p.y), tint(sem.textDim, 0.6f), right);
    }
    ImGui::Dummy(ImVec2(w, sfs + fs * 0.2f));
}

void dashedRect(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash) {
    auto line = [&](float x0, float y0, float x1, float y1) {
        const float len = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
        if (len <= 0.0f) return;
        const float ux = (x1 - x0) / len, uy = (y1 - y0) / len;
        for (float t = 0.0f; t < len; t += dash * 2.0f) {
            const float e = ImMin(t + dash, len);
            dl->AddLine(ImVec2(x0 + ux * t, y0 + uy * t), ImVec2(x0 + ux * e, y0 + uy * e), col, 1.0f);
        }
    };
    line(a.x, a.y, b.x, a.y);
    line(b.x, a.y, b.x, b.y);
    line(b.x, b.y, a.x, b.y);
    line(a.x, b.y, a.x, a.y);
}

// Flow nodes that address a HUD element by name: how many, and in which
// objects ("player-1", or "player-1 (level-2)" outside the first scene).
struct HudRefs {
    int nodes = 0;
    std::vector<std::string> where;
};
HudRefs hudRefsOf(const Project& p, const std::string& name, bool isText, bool isBar) {
    HudRefs r;
    if (name.empty()) return r;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            int here = 0;
            for (const FlowNode& fn : o.flowGraph.nodes) {
                if (fn.str != name) continue;
                const FlowNodeType* ft = flowNodeType(fn.type);
                if (!ft) continue;
                if (ft->strKind == FlowParamKind::HudElementName ||
                    (isBar && ft->strKind == FlowParamKind::HudBarName) ||
                    (isText && ft->strKind == FlowParamKind::HudTextName))
                    ++here;
            }
            if (here == 0) continue;
            r.nodes += here;
            r.where.push_back(&sc == &p.scenes.front() ? o.name : o.name + " (" + sc.name + ")");
        }
    return r;
}

std::string refsLine(const HudRefs& r) {
    if (r.nodes == 0) return "no flow node uses it";
    std::string s = "driven by " + std::to_string(r.nodes) + (r.nodes == 1 ? " node in " : " nodes in ");
    s += r.where.size() == 1 ? r.where[0] : std::to_string(r.where.size()) + " objects";
    return s;
}

const char* docsBase() { return "/blob/main/docs/"; }
}  // namespace

void App::drawHudFrame(ImDrawList* dl, ImVec2 fMin, ImVec2 fSize, bool editing, float clock,
                       std::vector<HudHit>* hits) {
    auto screenRect = [&](const float* pos, const float* size, ImVec2& pMin, ImVec2& pMax) {
        const float w = size[0] / 512.0f * fSize.x;
        const float h = size[1] / 448.0f * fSize.y;
        const ImVec2 c(fMin.x + pos[0] * fSize.x, fMin.y + pos[1] * fSize.y);
        pMin = ImVec2(c.x - w * 0.5f, c.y - h * 0.5f);
        pMax = ImVec2(c.x + w * 0.5f, c.y + h * 0.5f);
    };
    // Play show / hide: the selected element runs its transition in, holds,
    // runs it back out (hudanim::transition, the generated hudTransEval's
    // twin), then rests shown again.
    int trSel = -1, trIdx = -1;
    float trP = 1.0f;
    if (editing && uiTransPlayT0_ >= 0.0) {
        const HudTransition* tr = nullptr;
        int idx = -1;
        if (uiFxSel_ == 0 && selectedHud_ >= 0 && selectedHud_ < (int)project_.hud.size()) {
            tr = &project_.hud[selectedHud_].transition;
            idx = selectedHud_;
        } else if (uiFxSel_ == 9 && selectedBar_ >= 0 && selectedBar_ < (int)project_.hudBars.size()) {
            tr = &project_.hudBars[selectedBar_].transition;
            idx = selectedBar_;
        } else if (uiFxSel_ == 4 && selectedText_ >= 0 &&
                   selectedText_ < (int)project_.hudTexts.size()) {
            tr = &project_.hudTexts[selectedText_].transition;
            idx = selectedText_;
        }
        if (!tr || tr->kind == hudanim::Cut) {
            uiTransPlayT0_ = -1.0;
        } else {
            const float d = ImMax(0.05f, tr->duration), hold = 0.7f;
            const float t = (float)(ImGui::GetTime() - uiTransPlayT0_);
            float p = 1.0f;
            if (t < d) p = t / d;
            else if (t < d + hold) p = 1.0f;
            else if (t < 2.0f * d + hold) p = 1.0f - (t - d - hold) / d;
            else if (t < 2.0f * d + hold * 1.5f) p = 0.0f;
            else uiTransPlayT0_ = -1.0;
            trSel = uiFxSel_;
            trIdx = idx;
            trP = p;
        }
    }
    // The looped animation an element carries is drawn through
    // hudanim::evaluate - the same formula the generated game runs. A
    // Blink's off phase draws nothing, like the console. Hit boxes are the
    // element's REST box, so an animated element stays easy to grab.
    auto pose = [&](int sel, int idx, const float* pos, const float* size, const HudAnim& anim,
                    const HudTransition* trans, ImVec2& pMin, ImVec2& pMax, float& alpha) {
        const hudanim::Motion m = hudanim::evaluate(anim.kind, anim.period, anim.amount, clock);
        if (!m.visible) return false;
        float dx = m.dx, dy = m.dy, sc = m.scale;
        alpha = m.alpha;
        if (trans && sel == trSel && idx == trIdx) {
            if (trP <= 0.0f) return false;
            hudanim::transition(trans->kind, trP, dx, dy, sc, alpha);
            if (alpha <= 0.002f) return false;
        }
        const float sz[2] = {size[0] * sc, size[1] * sc};
        const float ps[2] = {pos[0] + dx / 512.0f, pos[1] + dy / 448.0f};
        screenRect(ps, sz, pMin, pMax);
        return true;
    };
    auto push = [&](int sel, int idx, const float* pos, const float* size) {
        if (!hits) return;
        HudHit h;
        h.sel = sel;
        h.index = idx;
        screenRect(pos, size, h.min, h.max);
        hits->push_back(h);
    };
    auto tintOf = [](float alpha) { return IM_COL32(255, 255, 255, (int)(alpha * 255.0f + 0.5f)); };

    for (int i = 0; i < (int)project_.hud.size(); ++i) {
        const HudImage& hi = project_.hud[i];
        // Hidden-at-start images show only while editing, dimmed, so they
        // can still be placed.
        if (!hi.visibleAtStart && !editing) continue;
        push(0, i, hi.pos, hi.size);
        ImVec2 pMin, pMax;
        float alpha = 1.0f;
        if (!pose(0, i, hi.pos, hi.size, hi.anim, &hi.transition, pMin, pMax, alpha)) continue;
        if (!hi.visibleAtStart) alpha *= 0.35f;
        if (const HudTexture* t = hudTexture(hi.imagePath))
            dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1),
                         tintOf(alpha));
        else
            dl->AddRect(pMin, pMax, IM_COL32(255, 100, 100, 200));
    }
    // Live bars, at their start value (or the UI Editor's preview fill for
    // the selected one). Drawn above the stack like the game.
    for (int i = 0; i < (int)project_.hudBars.size(); ++i) {
        const HudBar& hb = project_.hudBars[i];
        const bool isSel = editing && uiFxSel_ == 9 && i == selectedBar_;
        if (!hb.visibleAtStart && !editing) continue;
        push(9, i, hb.pos, hb.size);
        ImVec2 pMin, pMax;
        float alpha = 1.0f;
        if (!pose(9, i, hb.pos, hb.size, hb.anim, &hb.transition, pMin, pMax, alpha)) continue;
        if (!hb.visibleAtStart) alpha *= 0.35f;
        float start = hb.startValue;
        if (!hb.source.empty()) {
            start = hb.minValue;
            for (const SaveValue& sv : project_.saveValues)
                if (sv.name == hb.source) start = sv.value;
        }
        const float frac = (isSel && hudBarPreview_ >= 0.0f)
                               ? hudBarPreview_
                               : hudanim::fraction(start, hb.minValue, hb.maxValue);
        auto col = [&](const float* c) {
            return IM_COL32((int)(c[0] * 255.0f + 0.5f), (int)(c[1] * 255.0f + 0.5f),
                            (int)(c[2] * 255.0f + 0.5f), (int)(alpha * 255.0f + 0.5f));
        };
        const float bw = pMax.x - pMin.x, bh = pMax.y - pMin.y;
        const HudTexture* fillTex =
            hb.fillImage.imagePath.empty() ? nullptr : hudTexture(hb.fillImage.imagePath);
        if (hb.kind == 0) {
            dl->AddRectFilled(pMin, pMax, col(hb.bgColor));
            const float fw = bw * frac;
            const float fx = hb.rightToLeft ? pMax.x - fw : pMin.x;
            if (fw > 0.5f) {
                if (fillTex)
                    dl->AddImage((ImTextureID)(intptr_t)fillTex->tex, ImVec2(fx, pMin.y),
                                 ImVec2(fx + fw, pMax.y), ImVec2(hb.rightToLeft ? 1.0f - frac : 0.0f, 0),
                                 ImVec2(hb.rightToLeft ? 1.0f : frac, 1), col(hb.fillColor));
                else
                    dl->AddRectFilled(ImVec2(fx, pMin.y), ImVec2(fx + fw, pMax.y), col(hb.fillColor));
            }
        } else {
            const int segs = hb.segments < 1 ? 1 : hb.segments;
            const int lit = (int)(frac * segs + 0.001f);
            const float gap = hb.spacing / 512.0f * fSize.x;
            const float segW = (bw - gap * (segs - 1)) / segs;
            for (int k = 0; k < segs; ++k) {
                const int kk = hb.rightToLeft ? segs - 1 - k : k;
                const float sx = pMin.x + kk * (segW + gap);
                const float* c = (k < lit) ? hb.fillColor : hb.bgColor;
                if (fillTex)
                    dl->AddImage((ImTextureID)(intptr_t)fillTex->tex, ImVec2(sx, pMin.y),
                                 ImVec2(sx + segW, pMax.y), ImVec2(0, 0), ImVec2(1, 1), col(c));
                else
                    dl->AddRectFilled(ImVec2(sx, pMin.y), ImVec2(sx + segW, pMax.y), col(c));
            }
        }
        if (!hb.frameImage.imagePath.empty()) {
            if (const HudTexture* t = hudTexture(hb.frameImage.imagePath)) {
                const float k = hb.size[0] > 0.0f ? bw / (hb.size[0] / 512.0f * fSize.x) : 1.0f;
                const float fw = hb.frameImage.size[0] / 512.0f * fSize.x * k;
                const float fh = hb.frameImage.size[1] / 448.0f * fSize.y * k;
                const ImVec2 c(pMin.x + bw * 0.5f, pMin.y + bh * 0.5f);
                dl->AddImage((ImTextureID)(intptr_t)t->tex, ImVec2(c.x - fw * 0.5f, c.y - fh * 0.5f),
                             ImVec2(c.x + fw * 0.5f, c.y + fh * 0.5f), ImVec2(0, 0), ImVec2(1, 1),
                             tintOf(alpha));
            }
        }
    }
    // The USE prompt (custom image or the embedded built-in sprite); in the
    // game it only shows near usable objects - here it is a layout aid.
    {
        const HudImage& up = project_.usePrompt;
        // Text mode wins over the image and is sized by its own bake - the
        // same rule the build follows, so this preview matches.
        const HudTexture* t = nullptr;
        float size[2] = {up.size[0], up.size[1]};
        if (project_.usePromptIsText && !project_.usePromptText.text.empty()) {
            t = hudTextTexture(project_.usePromptText);
            if (t) {
                size[0] = (float)t->w;
                size[1] = (float)t->h;
            }
        } else {
            t = up.imagePath.empty() ? builtinUseTexture() : hudTexture(up.imagePath);
        }
        push(3, 0, up.pos, size);
        ImVec2 pMin, pMax;
        screenRect(up.pos, size, pMin, pMax);
        if (t)
            dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax);
        else
            dl->AddRect(pMin, pMax, IM_COL32(255, 100, 100, 200));
    }
    // On-screen texts: the ones visible at game start (all of them, the
    // hidden ones dimmed, while editing).
    for (int i = 0; i < (int)project_.hudTexts.size(); ++i) {
        const HudText& ht = project_.hudTexts[i];
        if (!ht.visibleAtStart && !editing) continue;
        if (const HudTexture* t = hudTextTexture(ht)) {
            const float size[2] = {(float)t->w, (float)t->h};
            push(4, i, ht.pos, size);
            ImVec2 pMin, pMax;
            float alpha = 1.0f;
            if (!pose(4, i, ht.pos, size, ht.anim, &ht.transition, pMin, pMax, alpha)) continue;
            if (!ht.visibleAtStart) alpha *= 0.35f;
            dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1),
                         tintOf(alpha));
        }
    }
}

void App::drawUiEditorWindow() {
    if (!showUiEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(1120), scaled(700)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UI Editor", &showUiEditor_)) {
        ImGui::End();
        return;
    }
    {
        // An OS drop over this window adds image elements (handleFileDrop).
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        uiEdDropWin_[0] = wp.x, uiEdDropWin_[1] = wp.y;
        uiEdDropWin_[2] = wp.x + ws.x, uiEdDropWin_[3] = wp.y + ws.y;
        uiEdDropFrame_[0] = uiEdDropFrame_[1] = uiEdDropFrame_[2] = uiEdDropFrame_[3] = 0.0f;
        uiEdDropSeen_ = ImGui::GetFrameCount();
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

    bool changed = false;
    // Belt and braces (the Animation/Loading Screen idiom): a hand-set flag is
    // forgettable by the next widget added here, a section comparison is not.
    const std::string beforeSection = project::sectionJson(project_, project::Section::Hud);
    const int n = (int)project_.hud.size();

    // Render-order stack (bottom of the screen list = drawn first): hud indices
    // plus the effect markers. A marker at layer L renders right before hud
    // sprite L; layer -1 (or >= n) renders after every sprite (topmost). Bloom
    // before grain when they share a slot (grain composites over the graded,
    // bloomed image - the fixed internal order).
    const int nFx = (int)project_.screenFx.size();
    auto topmost = [&](int L) { return L < 0 || L >= n; };
    auto emitMarkers = [&](std::vector<int>& s, int layer) {
        // Motion blur first: it smears the SCENE, so this frame's own glow and
        // grain go on top of it (the generated loop composites in this order).
        if (project_.hudMotionBlurLayer == layer) s.push_back(kMotionBlurMark);
        if (project_.hudBloomLayer == layer) s.push_back(kBloomMark);
        if (project_.hudGrainLayer == layer) s.push_back(kGrainMark);
        for (int fi = 0; fi < nFx; ++fi)
            if (project_.screenFx[fi].layer == layer) s.push_back(fxMark(fi));
    };
    auto buildStack = [&]() {
        std::vector<int> s;
        s.reserve(n + 3 + nFx);
        for (int i = 0; i < n; ++i) {
            emitMarkers(s, i);
            s.push_back(i);
        }
        if (topmost(project_.hudMotionBlurLayer)) s.push_back(kMotionBlurMark);
        if (topmost(project_.hudBloomLayer)) s.push_back(kBloomMark);
        if (topmost(project_.hudGrainLayer)) s.push_back(kGrainMark);
        for (int fi = 0; fi < nFx; ++fi)
            if (topmost(project_.screenFx[fi].layer)) s.push_back(fxMark(fi));
        return s;
    };
    // Rebuild the model from a render-order stack: hud array + screenFx list are
    // reordered to match, each layer = number of hud sprites before its marker
    // (n = -1, topmost). Fx markers carry their OLD placement index; screenFx is
    // rebuilt in stack order so composite order among same-slot effects follows
    // the stack.
    auto rebuild = [&](const std::vector<int>& s) {
        std::vector<HudImage> newHud;
        std::vector<ScreenFxPlacement> newFx;
        newHud.reserve(n);
        newFx.reserve(nFx);
        int before = 0, bl = -1, gr = -1, mb = -1;
        for (int e : s) {
            if (e == kBloomMark) bl = before;
            else if (e == kGrainMark) gr = before;
            else if (e == kMotionBlurMark) mb = before;
            else if (isFxMark(e)) {
                ScreenFxPlacement pl = project_.screenFx[fxMarkIndex(e)];
                pl.layer = before;
                newFx.push_back(std::move(pl));
            } else {
                newHud.push_back(project_.hud[e]);
                ++before;
            }
        }
        const int newN = (int)newHud.size();
        project_.hudBloomLayer = bl >= newN ? -1 : bl;
        project_.hudGrainLayer = gr >= newN ? -1 : gr;
        project_.hudMotionBlurLayer = mb >= newN ? -1 : mb;
        for (ScreenFxPlacement& f : newFx)
            if (f.layer >= newN) f.layer = -1;
        project_.hud = std::move(newHud);
        project_.screenFx = std::move(newFx);
    };

    // Display order: top of the screen (drawn last) first = reversed stack.
    std::vector<int> order = buildStack();
    std::reverse(order.begin(), order.end());

    // Valid selection (stale indices after an undo / a peer edit read as none).
    if (selectedHud_ >= n) selectedHud_ = -1;
    if (selectedBar_ >= (int)project_.hudBars.size()) selectedBar_ = -1;
    if (selectedText_ >= (int)project_.hudTexts.size()) selectedText_ = -1;
    if (selectedFx_ >= nFx) selectedFx_ = -1;

    auto uniqueName = [](const std::string& base, auto taken) {
        int suffix = 1;
        while (taken(suffix == 1 ? base : base + "-" + std::to_string(suffix))) ++suffix;
        return suffix == 1 ? base : base + "-" + std::to_string(suffix);
    };
    auto addText = [&]() {
        HudText t;
        // unique name: "text", "text-2", ... (referenced by flow nodes)
        t.name = uniqueName("text", [&](const std::string& s) {
            for (const HudText& e : project_.hudTexts)
                if (e.name == s) return true;
            return false;
        });
        project_.hudTexts.push_back(std::move(t));
        uiFxSel_ = 4;
        selectedText_ = (int)project_.hudTexts.size() - 1;
        changed = true;
    };
    auto addBar = [&]() {
        HudBar b;
        b.name = uniqueName("bar", [&](const std::string& s) {
            for (const HudBar& e : project_.hudBars)
                if (e.name == s) return true;
            return false;
        });
        // Stack new bars down the top-left corner instead of on top of each
        // other, so three new bars are three visible bars.
        b.pos[0] = 0.22f;
        b.pos[1] = 0.08f + 0.05f * (float)project_.hudBars.size();
        project_.hudBars.push_back(std::move(b));
        uiFxSel_ = 9;
        selectedBar_ = (int)project_.hudBars.size() - 1;
        hudBarPreview_ = -1.0f;
        changed = true;
    };
    auto isPlaced = [&](const std::string& key) {
        for (const ScreenFxPlacement& f : project_.screenFx)
            if (f.key == key) return true;
        return false;
    };
    auto addFx = [&](const CustomScreenFx* e) {
        ScreenFxPlacement f;
        f.key = e->key;
        f.layer = -1;  // topmost by default
        f.enabled = true;
        for (int i = 0; i < 4; ++i) f.params[i] = e->paramDefault[i];
        project_.screenFx.push_back(std::move(f));
        uiFxSel_ = 5;
        selectedFx_ = (int)project_.screenFx.size() - 1;
        changed = true;
    };
    auto openDoc = [](const char* page) {
        platform::openUrl(std::string("https://github.com/") + update::kRepo + docsBase() + page + ".md");
    };

    // --- header: preview options ---------------------------------------------
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("HUD drawn over every scene");
        static const char* kBg[] = {"Scene", "Checker", "Black"};
        float segW = 6.0f;
        for (const char* l : kBg) segW += ImGui::CalcTextSize(l).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float sp = ImGui::GetStyle().ItemSpacing.x;
        const float swW = ImGui::CalcTextSize("In viewport").x + IM_TRUNC(fs * 1.05f) * 1.8f +
                          ImGui::GetStyle().ItemInnerSpacing.x;
        const float total = segW + sp + ui::FilterChipWidth("Animations") + sp +
                            ui::FilterChipWidth("Safe area") + sp * 3.0f + swW;
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + sp * 2.0f, right - total));
        ui::Segmented("##uiprevbg", kBg, 3, &uiPrevBg_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("What the preview draws behind the HUD. Scene = the\n"
                              "scene from the viewport camera, as the game draws it.");
        ImGui::SameLine();
        if (ui::FilterChip("Animations", uiPrevAnimate_)) uiPrevAnimate_ = !uiPrevAnimate_;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Run the looped animations (Pulse, Bob...) in the\n"
                              "preview, on the same formula the game uses.");
        ImGui::SameLine();
        if (ui::FilterChip("Safe area", uiPrevSafe_)) uiPrevSafe_ = !uiPrevSafe_;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The inner 90%% of the screen: what a CRT TV is sure to show.");
        ImGui::SameLine(0.0f, sp * 2.0f);
        ui::Switch("In viewport", &showHudInEditor_);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Also draw the HUD over the 3D viewport (as the game\n"
                              "starts: hidden-at-start elements are left out).");
    }
    ImGui::Dummy(ImVec2(0.0f, fs * 0.15f));

    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float totalW = ImGui::GetContentRegionAvail().x;
    const float leftW = ImMin(scaled(252), totalW * 0.28f);
    const float rightW = ImMin(scaled(316), totalW * 0.34f);
    const float midW = ImMax(scaled(120), totalW - leftW - rightW - ImGui::GetStyle().ItemSpacing.x * 2.0f);

    // --- left: On screen ------------------------------------------------------
    ImGui::BeginChild("##ui_stack", ImVec2(leftW, bodyH), ImGuiChildFlags_None);
    {
        const int total = (int)project_.hudTexts.size() + (int)project_.hudBars.size() + n + nFx + 7;
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", total);
        const float addW = ImGui::CalcTextSize("Add").x + ImGui::GetStyle().FramePadding.x * 2.0f + fs;
        ImGui::AlignTextToFramePadding();
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(1.0f, ImGui::GetFrameHeight()));
        {
            // Small caps caption without ui::Caption's hairline (the Add
            // button shares the row).
            ImFont* bf = ui::boldFont() ? ui::boldFont() : ImGui::GetFont();
            const float cfs = IM_TRUNC(fs * 0.78f);
            const float y = cp.y + (ImGui::GetFrameHeight() - cfs) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = cp.x + fs * 0.3f;
            for (const char* c = "ON SCREEN"; *c; ++c) {
                const char s[2] = {*c, 0};
                dl->AddText(bf, cfs, ImVec2(x, y), tint(sem.textDim), s);
                x += bf->CalcTextSizeA(cfs, FLT_MAX, 0.0f, s).x + cfs * 0.08f;
            }
            dl->AddText(ImGui::GetFont(), cfs, ImVec2(x + cfs * 0.6f, y), tint(sem.textDim, 0.7f), cnt);
        }
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - addW);
        if (ImGui::Button("Add", ImVec2(addW, 0))) ImGui::OpenPopup("##ui_add");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Add an image, a text, a bar or a custom screen effect.");
        if (ImGui::BeginPopup("##ui_add")) {
            if (ImGui::MenuItem("Image (PNG)...")) importHudImage();
            if (ImGui::MenuItem("Text")) addText();
            if (ImGui::MenuItem("Bar")) addBar();
            ImGui::Separator();
            int unplaced = 0;
            for (const auto& e : customScreenEffects()) {
                if (isPlaced(e->key)) continue;
                ++unplaced;
                ImGui::PushID(e->key.c_str());
                if (ImGui::MenuItem(("Effect: " + e->title).c_str())) addFx(e.get());
                ImGui::PopID();
            }
            if (unplaced == 0)
                ImGui::TextDisabled(customScreenEffects().empty()
                                        ? "No custom effects in screen-effects/"
                                        : "Every custom effect is placed");
            // Custom screen effects loaded from screen-effects/*.screenfx;
            // management mirrors the Flow Graph "Custom nodes..." menu.
            if (ImGui::BeginMenu("Custom effects")) {
                ImGui::TextDisabled("%d loaded from screen-effects/", (int)customScreenEffects().size());
                ImGui::Separator();
                if (ImGui::MenuItem("Reload from folder")) {
                    const std::string msg = screenfx::loadForProject(project_.dir);
                    // Drop placements whose effect file vanished on reload.
                    for (size_t i = project_.screenFx.size(); i-- > 0;)
                        if (!customScreenFx(project_.screenFx[i].key))
                            project_.screenFx.erase(project_.screenFx.begin() + i);
                    selectedFx_ = -1;
                    statusMessage_ = msg.empty() ? "No custom effects found in screen-effects/" : msg;
                    changed = true;
                }
                if (ImGui::MenuItem("New starter effect (example.screenfx)")) {
                    const std::string path = screenfx::writeExample(project_.dir);
                    if (path.rfind("error:", 0) == 0) {
                        statusMessage_ = path;
                    } else {
                        screenfx::loadForProject(project_.dir);
                        statusMessage_ = "Wrote " + path + " - edit it, then Reload";
                    }
                }
                if (ImGui::BeginMenu("Jump to effect file", !customScreenEffects().empty())) {
                    for (const auto& e : customScreenEffects())
                        if (ImGui::MenuItem(e->title.c_str())) openInVSCode(e->sourceFile);
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("How custom effects work")) openDoc("custom-screen-effects");
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }
    const float footH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f + 1.0f;
    ImGui::BeginChild("##ui_rows", ImVec2(0, ImMax(fs * 4.0f, ImGui::GetContentRegionAvail().y - footH)));
    {
        // Texts draw above the whole HUD stack (under menus).
        listHead("Texts \xC2\xB7 always on top", "draws last");
        for (int i = 0; i < (int)project_.hudTexts.size(); ++i) {
            HudText& t = project_.hudTexts[i];
            ImGui::PushID(1000 + i);
            const char* meta = t.anim.kind != hudanim::None ? hudanim::kindName(t.anim.kind) : "";
            const RowResult r = screenRow(t.name.c_str(), meta, "T", sem.warn,
                                          uiFxSel_ == 4 && selectedText_ == i, !t.visibleAtStart, false,
                                          &t.visibleAtStart);
            if (r.eye) {
                t.visibleAtStart = !t.visibleAtStart;
                changed = true;
            } else if (r.clicked) {
                uiFxSel_ = 4;
                selectedText_ = i;
            }
            ImGui::PopID();
        }
        if (project_.hudTexts.empty()) {
            ImGui::Indent(fs * 0.4f);
            ImGui::TextDisabled("None.");
            ImGui::SameLine();
            if (ImGui::TextLink("Add a text")) addText();
            ImGui::Unindent(fs * 0.4f);
        }

        // Live bars (docs/hud-animation.md): above the stack, under the texts.
        listHead("Bars");
        for (int i = 0; i < (int)project_.hudBars.size(); ++i) {
            HudBar& b = project_.hudBars[i];
            ImGui::PushID(2000 + i);
            std::string meta;
            if (!b.source.empty()) meta = "save \xC2\xB7 " + b.source;
            else if (b.kind == 1) meta = std::to_string(b.segments) + " seg";
            if (b.anim.kind != hudanim::None)
                meta += (meta.empty() ? "" : " \xC2\xB7 ") + std::string(hudanim::kindName(b.anim.kind));
            const RowResult r = screenRow(b.name.c_str(), meta.c_str(), "B", sem.danger,
                                          uiFxSel_ == 9 && selectedBar_ == i, !b.visibleAtStart, false,
                                          &b.visibleAtStart);
            if (r.eye) {
                b.visibleAtStart = !b.visibleAtStart;
                changed = true;
            } else if (r.clicked) {
                if (!(uiFxSel_ == 9 && selectedBar_ == i)) hudBarPreview_ = -1.0f;
                uiFxSel_ = 9;
                selectedBar_ = i;
            }
            ImGui::PopID();
        }
        if (project_.hudBars.empty()) {
            ImGui::Indent(fs * 0.4f);
            ImGui::TextDisabled("None.");
            ImGui::SameLine();
            if (ImGui::TextLink("Add a bar")) addBar();
            ImGui::Unindent(fs * 0.4f);
        }

        listHead("Images & screen effects", "drag to reorder");
        // The USE prompt is part of the screen, but pinned: it always draws
        // above the HUD stack (and under menus), and cannot be deleted.
        if (screenRow("USE prompt", project_.usePromptIsText ? "text" : "image", "P", sem.accent,
                      uiFxSel_ == 3, false, false, nullptr)
                .clicked)
            uiFxSel_ = 3;
        auto pct = [](float v) {
            char b[16];
            std::snprintf(b, sizeof(b), "%.0f%%", v * 100.0f);
            return std::string(b);
        };
        auto lvl = [](float v) {
            char b[16];
            std::snprintf(b, sizeof(b), "%.2f", v);
            return std::string(b);
        };
        for (int r = 0; r < (int)order.size(); ++r) {
            const int id = order[r];
            ImGui::PushID(r);
            bool isSel;
            std::string label, meta;
            const char* badge = "fx";
            ImVec4 bcol = sem.special;
            bool dim = false;
            HudImage* img = nullptr;
            if (id == kBloomMark) {
                isSel = uiFxSel_ == 1;
                label = "Bloom + color grading";
                dim = project_.settings.bloom <= 0.0f;
                meta = dim ? "off" : lvl(project_.settings.bloom);
            } else if (id == kGrainMark) {
                isSel = uiFxSel_ == 2;
                label = "Film grain";
                dim = project_.settings.grain <= 0.0f;
                meta = dim ? "off" : lvl(project_.settings.grain);
            } else if (id == kMotionBlurMark) {
                isSel = uiFxSel_ == 10;
                label = "Motion blur";
                dim = project_.settings.motionBlur <= 0.0f;
                meta = dim ? "off" : pct(project_.settings.motionBlur);
            } else if (isFxMark(id)) {
                const int fi = fxMarkIndex(id);
                const ScreenFxPlacement& pl = project_.screenFx[fi];
                const CustomScreenFx* e = customScreenFx(pl.key);
                isSel = uiFxSel_ == 5 && selectedFx_ == fi;
                label = (e ? e->title : pl.key) + "##fx" + pl.key;
                dim = !pl.enabled;
                meta = !e ? "missing" : dim ? "off" : "custom";
            } else {
                img = &project_.hud[id];
                isSel = uiFxSel_ == 0 && selectedHud_ == id;
                label = img->name.empty() ? "(image)##hud" + std::to_string(id) : img->name;
                badge = "I";
                bcol = sem.ok;
                dim = !img->visibleAtStart;
                if (img->anim.kind != hudanim::None) meta = hudanim::kindName(img->anim.kind);
            }
            const RowResult rr = screenRow(label.c_str(), meta.c_str(), badge, bcol, isSel, dim, true,
                                           img ? &img->visibleAtStart : nullptr);
            if (rr.eye && img) {
                img->visibleAtStart = !img->visibleAtStart;
                changed = true;
            } else if (rr.clicked) {
                if (id == kBloomMark) uiFxSel_ = 1;
                else if (id == kGrainMark) uiFxSel_ = 2;
                else if (id == kMotionBlurMark) uiFxSel_ = 10;
                else if (isFxMark(id)) {
                    uiFxSel_ = 5;
                    selectedFx_ = fxMarkIndex(id);
                } else {
                    uiFxSel_ = 0;
                    selectedHud_ = id;
                }
            }
            // Drag to reorder: swap with the neighbor the cursor moved towards,
            // then rebuild the model from the new order.
            if (rr.active && !rr.hovered) {
                const int dst = r + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
                if (dst >= 0 && dst < (int)order.size()) {
                    // Remember the selected image / effect so its selection
                    // survives the reorder (indices shift; identity does not).
                    const bool hadHud = uiFxSel_ == 0 && selectedHud_ >= 0 && selectedHud_ < n;
                    HudImage selHud;
                    if (hadHud) selHud = project_.hud[selectedHud_];
                    const bool hadFx = uiFxSel_ == 5 && selectedFx_ >= 0 &&
                                       selectedFx_ < (int)project_.screenFx.size();
                    ScreenFxPlacement selFx;
                    if (hadFx) selFx = project_.screenFx[selectedFx_];

                    std::swap(order[r], order[dst]);
                    std::vector<int> s(order.rbegin(), order.rend());
                    rebuild(s);

                    if (hadHud)
                        for (int i = 0; i < (int)project_.hud.size(); ++i)
                            if (project_.hud[i] == selHud) {
                                selectedHud_ = i;
                                break;
                            }
                    if (hadFx)
                        for (int i = 0; i < (int)project_.screenFx.size(); ++i)
                            if (project_.screenFx[i] == selFx) {
                                selectedFx_ = i;
                                break;
                            }
                    ImGui::ResetMouseDragDelta();
                    changed = true;
                    ImGui::PopID();
                    break;  // `order` no longer matches the model
                }
            }
            ImGui::PopID();
        }
        // Depth of field, god rays and the lens flare are pinned under the
        // whole stack: they composite right after the 3D scene (DoF is
        // z-tested per pixel, and sprites stamp z across their full rect).
        auto pinned = [&](const char* label, int sel, float v) {
            if (screenRow(label, v <= 0.0f ? "off" : lvl(v).c_str(), "fx", sem.special, uiFxSel_ == sel,
                          v <= 0.0f, false, nullptr)
                    .clicked)
                uiFxSel_ = sel;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Pinned under the whole stack: it draws right after the\n"
                                  "3D scene, so every HUD entry stays crisp.");
        };
        pinned("Depth of field", 6, project_.settings.dofAmount);
        pinned("God rays", 8, project_.settings.godRays);
        pinned("Lens flare", 7, project_.settings.flare);
        if (project_.hud.empty()) {
            // The empty image slot says what to do, not just that it is empty.
            ImGui::Dummy(ImVec2(0.0f, fs * 0.4f));
            const float w = ImGui::GetContentRegionAvail().x;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float wrap = w - fs * 1.6f;
            const char* msg = "No HUD images yet. Effects above an image draw over it.";
            const ImVec2 ts = ImGui::CalcTextSize(msg, nullptr, false, wrap);
            const float h = ts.y + ImGui::GetTextLineHeightWithSpacing() + fs * 1.4f;
            dashedRect(ImGui::GetWindowDrawList(), ImVec2(p.x + 2, p.y), ImVec2(p.x + w - 2, p.y + h),
                       tint(sem.textDim, 0.45f), fs * 0.3f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.8f, p.y + fs * 0.6f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
            ImGui::TextDisabled("%s", msg);
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.8f, ImGui::GetCursorScreenPos().y));
            if (ImGui::TextLink("Import image (PNG)...")) importHudImage();
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + fs * 0.3f));
            ImGui::Dummy(ImVec2(1, 1));
        }
    }
    ImGui::EndChild();  // ##ui_rows
    // Shared assets, not layers: inline button icons and the font list.
    {
        ImGui::Separator();
        char lbl[64];
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        std::snprintf(lbl, sizeof(lbl), "Button icons \xC2\xB7 %d###uiicons", (int)project_.textIcons.size());
        if (ImGui::Button(lbl, ImVec2(half, 0))) ImGui::OpenPopup("Button icons");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Images any text can splice in with a {{name}} placeholder:\n"
                              "\"Press {{cross}} to jump\". {{action:jump}} draws whatever\n"
                              "that action is bound to right now.");
        ImGui::SameLine();
        std::snprintf(lbl, sizeof(lbl), "Fonts \xC2\xB7 %d###uifonts", (int)project_.fonts.size());
        if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0))) showFontManager_ = true;
        drawTextIconsModal();
    }
    ImGui::EndChild();  // ##ui_stack

    // --- centre: the preview ----------------------------------------------------
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sem.ground);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 2.0f);
    ImGui::BeginChild("##ui_preview", ImVec2(midW, bodyH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 org = ImGui::GetCursorScreenPos();
        const float margin = fs;
        const float readH = ImGui::GetTextLineHeightWithSpacing() * 1.4f;
        float fw = avail.x - margin * 2.0f;
        float fh = fw * 448.0f / 512.0f;
        if (fh > avail.y - margin * 2.0f - readH) {
            fh = ImMax(fs * 4.0f, avail.y - margin * 2.0f - readH);
            fw = fh * 512.0f / 448.0f;
        }
        fw = IM_TRUNC(fw);
        fh = IM_TRUNC(fh);
        const ImVec2 f0(IM_TRUNC(org.x + (avail.x - fw) * 0.5f),
                        IM_TRUNC(org.y + ImMax(margin, (avail.y - fh - readH) * 0.5f)));
        const ImVec2 f1(f0.x + fw, f0.y + fh);
        const ImVec2 fSize(fw, fh);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Background.
        int bg = uiPrevBg_;
        uint32_t sceneTex = 0;
        if (bg == 0) {
            // The scene as the game draws it (no grid, markers or selection)
            // through the editor camera, rendered straight at the frame's
            // 512:448 shape (capped at the GS size) and only when the camera
            // or the project changes - Viewport::renderGameView.
            const float sc = ImGui::GetIO().DisplayFramebufferScale.x;
            const float k = ImMin(1.0f, 512.0f / ImMax(1.0f, fw * sc));
            sceneTex = viewport_.renderGameView(Viewport::GameViewHud, (int)(fw * sc * k),
                                                (int)(fh * sc * k), project_.objects(), modelEditSerial_);
            if (!sceneTex) bg = 1;
        }
        if (bg == 0) {
            // GL textures are bottom-up, hence the swapped v.
            dl->AddImage((ImTextureID)(intptr_t)sceneTex, f0, f1, ImVec2(0, 1), ImVec2(1, 0));
        } else if (bg == 1) {
            dl->AddRectFilled(f0, f1, tint(sem.field));
            const float cell = 16.0f / 512.0f * fw;
            int row = 0;
            for (float y = f0.y; y < f1.y; y += cell, ++row) {
                int col = row & 1;
                for (float x = f0.x; x < f1.x; x += cell, ++col)
                    if (col & 1)
                        dl->AddRectFilled(ImVec2(x, y), ImVec2(ImMin(x + cell, f1.x), ImMin(y + cell, f1.y)),
                                          tint(sem.surface));
            }
        } else {
            dl->AddRectFilled(f0, f1, IM_COL32(0, 0, 0, 255));
        }

        std::vector<HudHit> hits;
        dl->PushClipRect(f0, f1, true);
        drawHudFrame(dl, f0, fSize, true, uiPrevAnimate_ ? (float)ImGui::GetTime() : 0.0f, &hits);
        if (uiPrevSafe_)
            dashedRect(dl, ImVec2(f0.x + fw * 0.05f, f0.y + fh * 0.05f),
                       ImVec2(f1.x - fw * 0.05f, f1.y - fh * 0.05f), IM_COL32(255, 255, 255, 80), fs * 0.3f);
        dl->PopClipRect();
        dl->AddRect(ImVec2(f0.x - 1, f0.y - 1), ImVec2(f1.x + 1, f1.y + 1), tint(sem.border));

        // The selection and its handles. Texts size from their bake (font
        // size), and a text USE prompt too - only images and bars resize here.
        int selKind = -1, selIdx = -1;
        if (uiFxSel_ == 0 && selectedHud_ >= 0) selKind = 0, selIdx = selectedHud_;
        else if (uiFxSel_ == 9 && selectedBar_ >= 0) selKind = 9, selIdx = selectedBar_;
        else if (uiFxSel_ == 3) selKind = 3, selIdx = 0;
        else if (uiFxSel_ == 4 && selectedText_ >= 0) selKind = 4, selIdx = selectedText_;
        auto posOf = [&](int k, int i) -> float* {
            switch (k) {
                case 0: return project_.hud[i].pos;
                case 9: return project_.hudBars[i].pos;
                case 3: return project_.usePrompt.pos;
                case 4: return project_.hudTexts[i].pos;
                default: return nullptr;
            }
        };
        auto sizeOf = [&](int k, int i) -> float* {
            switch (k) {
                case 0: return project_.hud[i].size;
                case 9: return project_.hudBars[i].size;
                case 3: return project_.usePromptIsText ? nullptr : project_.usePrompt.size;
                default: return nullptr;
            }
        };
        const HudHit* selHit = nullptr;
        for (const HudHit& h : hits)
            if (h.sel == selKind && h.index == selIdx) selHit = &h;
        const float hs = IM_TRUNC(scaled(8.0f));
        ImVec2 corner[4];
        if (selHit) {
            const ImVec2 a(selHit->min.x - 3, selHit->min.y - 3), b(selHit->max.x + 3, selHit->max.y + 3);
            corner[0] = a;
            corner[1] = ImVec2(b.x, a.y);
            corner[2] = ImVec2(a.x, b.y);
            corner[3] = b;
        }

        // Direct manipulation: one canvas item over the frame. Press picks the
        // handle under the cursor, else the topmost element (selecting it);
        // dragging moves it (normalized position) or resizes it about its
        // centre (512x448 pixels). The model commits on release.
        ImGui::SetCursorScreenPos(f0);
        ImGui::InvisibleButton("HUD preview canvas", fSize);
        uiEdDropFrame_[0] = f0.x, uiEdDropFrame_[1] = f0.y;
        uiEdDropFrame_[2] = f0.x + fSize.x, uiEdDropFrame_[3] = f0.y + fSize.y;
        const ImVec2 mp = ImGui::GetIO().MousePos;
        auto inRect = [&](ImVec2 a, ImVec2 b, float pad) {
            return mp.x >= a.x - pad && mp.x <= b.x + pad && mp.y >= a.y - pad && mp.y <= b.y + pad;
        };
        if (ImGui::IsItemActivated()) {
            uiDragMode_ = 0;
            if (selHit && sizeOf(selKind, selIdx))
                for (int c = 0; c < 4; ++c)
                    if (inRect(corner[c], corner[c], hs * 0.75f)) uiDragMode_ = 2 + c;
            if (uiDragMode_ == 0) {
                for (int i = (int)hits.size() - 1; i >= 0; --i)
                    if (inRect(hits[i].min, hits[i].max, 2.0f)) {
                        const HudHit& h = hits[i];
                        if (h.sel == 9 && !(uiFxSel_ == 9 && selectedBar_ == h.index)) hudBarPreview_ = -1.0f;
                        uiFxSel_ = h.sel;
                        if (h.sel == 0) selectedHud_ = h.index;
                        else if (h.sel == 9) selectedBar_ = h.index;
                        else if (h.sel == 4) selectedText_ = h.index;
                        selKind = h.sel;
                        selIdx = h.index;
                        uiDragMode_ = 1;
                        break;
                    }
            }
        }
        if (ImGui::IsItemActive() && uiDragMode_ != 0 && selKind >= 0) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                if (uiDragMode_ == 1) {
                    if (float* p = posOf(selKind, selIdx)) {
                        p[0] = ImClamp(p[0] + d.x / fw, 0.0f, 1.0f);
                        p[1] = ImClamp(p[1] + d.y / fh, 0.0f, 1.0f);
                    }
                } else if (float* s = sizeOf(selKind, selIdx)) {
                    const int c = uiDragMode_ - 2;
                    const float sx = (c == 1 || c == 3) ? 1.0f : -1.0f;
                    const float sy = (c >= 2) ? 1.0f : -1.0f;
                    s[0] = ImClamp(std::round(s[0] + sx * 2.0f * d.x * 512.0f / fw), 1.0f, 512.0f);
                    s[1] = ImClamp(std::round(s[1] + sy * 2.0f * d.y * 448.0f / fh), 1.0f, 512.0f);
                }
            }
        }
        if (ImGui::IsItemDeactivated()) {
            if (uiDragMode_ != 0) changed = true;
            uiDragMode_ = 0;
        }
        if (ImGui::IsItemHovered() && uiDragMode_ == 0 && selHit && sizeOf(selKind, selIdx))
            for (int c = 0; c < 4; ++c)
                if (inRect(corner[c], corner[c], hs * 0.75f))
                    ImGui::SetMouseCursor(c == 0 || c == 3 ? ImGuiMouseCursor_ResizeNWSE
                                                           : ImGuiMouseCursor_ResizeNESW);
        // Arrow keys nudge the selection 1 px (Shift: 8 px) while the window
        // has focus and no field is being typed into.
        if (selKind >= 0 && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            const float step = ImGui::GetIO().KeyShift ? 8.0f : 1.0f;
            float dx = 0.0f, dy = 0.0f;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx += step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy += step;
            if (dx != 0.0f || dy != 0.0f)
                if (float* p = posOf(selKind, selIdx)) {
                    p[0] = ImClamp(p[0] + dx / 512.0f, 0.0f, 1.0f);
                    p[1] = ImClamp(p[1] + dy / 448.0f, 0.0f, 1.0f);
                    changed = true;
                }
        }

        if (selHit) {
            dl->PushClipRect(ImVec2(f0.x - hs, f0.y - hs), ImVec2(f1.x + hs, f1.y + hs), true);
            dl->AddRect(corner[0], corner[3], tint(sem.accent), 2.0f, 0, 1.5f);
            if (sizeOf(selKind, selIdx))
                for (int c = 0; c < 4; ++c) {
                    const ImVec2 a(corner[c].x - hs * 0.5f, corner[c].y - hs * 0.5f);
                    const ImVec2 b(corner[c].x + hs * 0.5f, corner[c].y + hs * 0.5f);
                    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
                    dl->AddRect(a, b, tint(sem.accent), 0.0f, 0, 1.5f);
                }
            dl->PopClipRect();
            // Readout under the box: name, size in 512x448 pixels, fill.
            char rd[160];
            const float bw = (selHit->max.x - selHit->min.x) * 512.0f / fw;
            const float bh = (selHit->max.y - selHit->min.y) * 448.0f / fh;
            std::string nm = selKind == 0   ? project_.hud[selIdx].name
                             : selKind == 9 ? project_.hudBars[selIdx].name
                             : selKind == 4 ? project_.hudTexts[selIdx].name
                                            : std::string("USE prompt");
            if (selKind == 9) {
                const HudBar& b = project_.hudBars[selIdx];
                float start = b.startValue;
                if (!b.source.empty()) {
                    start = b.minValue;
                    for (const SaveValue& sv : project_.saveValues)
                        if (sv.name == b.source) start = sv.value;
                }
                const float frac =
                    hudBarPreview_ >= 0.0f ? hudBarPreview_ : hudanim::fraction(start, b.minValue, b.maxValue);
                std::snprintf(rd, sizeof(rd), "%s \xC2\xB7 %.0f\xC3\x97%.0f \xC2\xB7 %.0f%%", nm.c_str(), bw, bh,
                              frac * 100.0f);
            } else {
                std::snprintf(rd, sizeof(rd), "%s \xC2\xB7 %.0f\xC3\x97%.0f", nm.c_str(), bw, bh);
            }
            ImFont* mf = ui::monoFont() ? ui::monoFont() : ImGui::GetFont();
            const float rfs = IM_TRUNC(fs * 0.86f);
            const ImVec2 ts = mf->CalcTextSizeA(rfs, FLT_MAX, 0.0f, rd);
            float rx = ImClamp(corner[0].x, f0.x, ImMax(f0.x, f1.x - ts.x - 10.0f));
            float ry = corner[3].y + 6.0f;
            if (ry + ts.y + 4.0f > f1.y) ry = corner[0].y - ts.y - 10.0f;
            dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + ts.x + 10.0f, ry + ts.y + 4.0f),
                              IM_COL32(14, 16, 21, 220), 4.0f);
            dl->AddText(mf, rfs, ImVec2(rx + 5.0f, ry + 2.0f), IM_COL32(232, 234, 240, 255), rd);
        }

        // Footer readout.
        {
            char sc[64];
            std::snprintf(sc, sizeof(sc), "512\xC3\x97" "448 \xC2\xB7 %.1f\xC3\x97", fw / 512.0f);
            const char* hint = "Drag to move \xC2\xB7 handles resize \xC2\xB7 arrows nudge 1 px";
            ImFont* mf = ui::monoFont() ? ui::monoFont() : ImGui::GetFont();
            const float rfs = IM_TRUNC(fs * 0.9f);
            const float w1 = mf->CalcTextSizeA(rfs, FLT_MAX, 0.0f, sc).x;
            const float w2 = ImGui::GetFont()->CalcTextSizeA(rfs, FLT_MAX, 0.0f, hint).x;
            const float tw = w1 + fs * 1.5f + w2;
            const float y = f1.y + fs * 0.6f;
            const float x = org.x + ImMax(0.0f, (avail.x - tw) * 0.5f);
            dl->AddText(mf, rfs, ImVec2(x, y), tint(sem.textDim), sc);
            dl->AddLine(ImVec2(x + w1 + fs * 0.75f, y + 2), ImVec2(x + w1 + fs * 0.75f, y + rfs - 2),
                        tint(sem.border));
            dl->AddText(ImGui::GetFont(), rfs, ImVec2(x + w1 + fs * 1.5f, y), tint(sem.textDim), hint);
        }
    }
    ImGui::EndChild();  // ##ui_preview

    // --- right: the inspector -----------------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##ui_props", ImVec2(0, bodyH), ImGuiChildFlags_None);
    ImGui::PushItemWidth(-FLT_MIN);
    // Header: kind badge, the name (editable where flow nodes do not need it
    // fixed - renames follow into the graphs), one line of context.
    auto header = [&](const char* badge, const ImVec4& col, const char* title) {
        const float bs = IM_TRUNC(fs * 2.1f);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        kindBadge(ImGui::GetWindowDrawList(), ImVec2(p.x + bs * 0.5f, p.y + bs * 0.5f), bs, badge, col);
        ImGui::SetCursorScreenPos(ImVec2(p.x + bs + fs * 0.6f, p.y));
        ImGui::BeginGroup();
        if (title) {
            ui::FontScope bold(ui::boldFont());
            ImGui::TextUnformatted(title);
        }
        return std::make_pair(p, bs);
    };
    auto headerEnd = [&](std::pair<ImVec2, float> hp, const std::string& sub,
                         const HudRefs* refs = nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(sub.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (refs && refs->nodes > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            std::string tip = "Flow nodes naming it, in:";
            for (const std::string& w : refs->where) tip += "\n  " + w;
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::EndGroup();
        const float y = ImMax(ImGui::GetCursorScreenPos().y, hp.first.y + hp.second + fs * 0.3f);
        ImGui::SetCursorScreenPos(ImVec2(hp.first.x, y));
        ImGui::Dummy(ImVec2(0.0f, fs * 0.2f));
    };
    // A name field drawn as a title: no frame until hovered, the heading cut.
    auto nameField = [&](const char* label, std::string& name, float rightPad) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", name.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
        // The label stays the ID (scripts type into it) but is never drawn:
        // the field is clipped to its own box, like the Properties header.
        const float w = ImMax(fs * 4.0f, ImGui::GetContentRegionAvail().x - rightPad);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(w);
        bool edited;
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::PushClipRect(p, ImVec2(p.x + w, p.y + ImGui::GetFrameHeight() + 2.0f), true);
            edited = ImGui::InputText(label, buf, sizeof(buf));
            ImGui::PopClipRect();
        }
        // What follows on this line (the "..." button) goes after the FIELD,
        // not after the invisible label InputText still lays out.
        ImGui::SameLine();
        ImGui::SetCursorScreenPos(ImVec2(p.x + w + ImGui::GetStyle().ItemSpacing.x, p.y));
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (edited) name = buf;
        return edited;
    };
    auto effectNote = [&](const char* line, const char* doc) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(line);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        if (doc && ImGui::TextLink("Read more")) openDoc(doc);
    };
    auto sceneNote = [&]() {
        ImGui::TextDisabled("Per scene: Scene > Scene Preferences > Post effects.");
    };
    ui::InspectorRows rows(scaled(76), scaled(116));

    if (uiFxSel_ == 1) {
        headerEnd(header("fx", sem.special, "Bloom + color grading"), "Screen effect \xC2\xB7 GS blits, no shaders");
        ui::Caption("Glow");
        // The re-add FIX is a whole byte, so bloom can go to 2x - the extra
        // headroom is what a hot glow needs once the threshold has eaten part
        // of the emitter's energy.
        ui::SliderFloat("Bloom", &project_.settings.bloom, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("How much of the quarter-res blur is added back. Above\n"
                    "1.0 the blur is over-added - blown-out, hot glow.");
        // Bright pass: without it the whole picture glows (soft focus); with
        // it only what is brighter than the cut blooms.
        ui::SliderFloat("Threshold", &project_.settings.bloomThreshold, 0.0f, 1.0f,
                        project_.settings.bloomThreshold <= 0.0f ? "off - whole frame" : "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("Only pixels brighter than this glow. 0 = soft focus over\n"
                    "everything; ~0.6 = only emissive materials, sky, highlights.");
        ui::SliderFloat("Spread", &project_.settings.bloomSpread, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("How far the glow reaches: extra blur rounds, 4 GS\n"
                    "sprites each, no EE time.");
        effectNote("Entries above it draw crisp over the glow. Color grading applies with this "
                   "layer (presets: Tools > Color Grading).",
                   "emissive-materials");
        sceneNote();
    } else if (uiFxSel_ == 2) {
        headerEnd(header("fx", sem.special, "Film grain"), "Screen effect \xC2\xB7 animated noise");
        ui::Caption("Grain");
        ui::SliderFloat("Film grain", &project_.settings.grain, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        effectNote("Subtle values work best. Above the HUD it is a filmic overlay over the whole "
                   "screen; under it, the UI stays clean.",
                   nullptr);
        sceneNote();
    } else if (uiFxSel_ == 10) {
        headerEnd(header("fx", sem.special, "Motion blur"), "Screen effect \xC2\xB7 one GS blend of the last frame");
        ui::Caption("Trail");
        // Stored 0..1, shown as a percentage: it is a fraction of the effect's
        // own range, and "0.20" says nothing about how strong that is.
        float blurPct = project_.settings.motionBlur * 100.0f;
        if (ui::SliderFloat("Motion blur", &blurPct, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
            project_.settings.motionBlur = blurPct * 0.01f;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("How much of the PREVIOUS frame is blended over this one.\n"
                    "The trail compounds: 20-40% is already a long smear.");
        if (ui::Switch("Clear trail when camera settles (Drops the history once the camera stops)",
                       &project_.settings.motionBlurIdleClear))
            changed = true;
        effectNote("Keep it under the HUD entries: from the top of the stack a moving HUD element "
                   "smears too. Runtime: Set Motion Blur node.",
                   "motion-blur");
        sceneNote();
    } else if (uiFxSel_ == 6) {
        headerEnd(header("fx", sem.special, "Depth of field"), "Pinned under the stack \xC2\xB7 z-tested blits");
        ui::Caption("Focus");
        ui::SliderFloat("Amount", &project_.settings.dofAmount, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::DragFloat("Focus", &project_.settings.dofFocus, 0.5f, 0.5f, 500.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("Sharp up to this distance (world units from the camera).");
        ui::DragFloat("Range", &project_.settings.dofRange, 0.5f, 0.1f, 500.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("Full Amount blur is reached at Focus + Range.");
        effectNote("Amount 0 = off. Every HUD entry draws crisp over it. Runtime: Set Depth Of "
                   "Field node.",
                   nullptr);
        sceneNote();
    } else if (uiFxSel_ == 7) {
        headerEnd(header("fx", sem.special, "Lens flare"), "Pinned under the stack \xC2\xB7 additive sprites");
        ui::Caption("Sun");
        ui::SliderFloat("Brightness", &project_.settings.flare, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        effectNote("Along the sun-to-centre axis, tinted by the light colour; fades when "
                   "geometry hides the sun. 0 = off. Runtime: Set Lens Flare node.",
                   nullptr);
        sceneNote();
    } else if (uiFxSel_ == 8) {
        headerEnd(header("fx", sem.special, "God rays"), "Pinned under the stack \xC2\xB7 radial blur");
        ui::Caption("Shafts");
        ui::SliderFloat("Strength", &project_.settings.godRays, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        effectNote("Bright parts streak toward the sun - strongest with the sun on screen. "
                   "0 = off. Runtime: Set God Rays node.",
                   nullptr);
        sceneNote();
    } else if (uiFxSel_ == 3) {
        // --- the pinned USE prompt ------------------------------------------
        HudImage& h = project_.usePrompt;
        headerEnd(header("P", sem.accent, "Interaction prompts"),
                  "USE and PICK UP \xC2\xB7 near a usable object \xC2\xB7 cannot be deleted");
        ui::Caption("Placement");
        ui::DragFloat2("Position##use", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("Shared by both prompts (normalized, centre anchor).");

        // One prompt's content: TEXT or IMAGE as an explicit choice, so flipping
        // to the image to compare does not throw away the text you typed. Text
        // can carry a button glyph that follows the binding, which is why a
        // fresh project starts there (docs/text-icons.md).
        auto promptUi = [&](const char* title, const char* id, bool& isText, HudText& txt,
                            std::string& imagePath, HudImage* bakeOf, const char* builtinNote) {
            ImGui::PushID(id);
            ui::Caption(title);
            int mode = isText ? 0 : 1;
            static const char* kModes[] = {"Text", "Image"};
            if (ui::Segmented("##mode", kModes, 2, &mode, -1.0f)) {
                isText = mode == 0;
                changed = true;
            }
            if (isText) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", txt.text.c_str());
                const float tokW = ImGui::GetFrameHeight() * 1.6f;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - tokW);
                if (ImGui::InputText("##prompttext", buf, sizeof(buf))) txt.text = buf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                changed |= textTokenPicker("prompttok", txt.text);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("{{use}} draws the glyph of whatever the \"use\" action\n"
                                      "is bound to, so the prompt follows a rebind;\n"
                                      "{{cross}} pins a fixed button. See docs/text-icons.md.");
                if (ui::DragInt("Size##prompt", &txt.size, 0.5f, 8, 48, "%d px")) changed = true;
                if (ui::ColorHex("Color##prompt", txt.color)) changed = true;
                if (ui::Switch("Shadow##prompt", &txt.shadow)) changed = true;
                changed |= fontCombo(txt.font);
                if (txt.text.empty())
                    ui::StatusLine(1, "Empty - the image is used instead.");
            } else {
                if (imagePath.empty())
                    ImGui::TextDisabled("%s", builtinNote);
                else
                    ImGui::TextDisabled("%s", imagePath.c_str());
                if (ImGui::Button(imagePath.empty() ? "Custom image (PNG)..." : "Replace image (PNG)...")) {
                    const std::string src = pickPngFile();
                    if (!src.empty()) {
                        const std::filesystem::path srcPath(src);
                        const std::string fileName = sanitizeAssetName(srcPath.filename().string());
                        const std::filesystem::path destDir = std::filesystem::path(project_.dir) / "res" / "hud";
                        std::error_code ec;
                        std::filesystem::create_directories(destDir, ec);
                        std::filesystem::copy_file(srcPath, destDir / fileName,
                                                   std::filesystem::copy_options::overwrite_existing, ec);
                        if (!ec) {
                            imagePath = "res/hud/" + fileName;
                            hudTexCache_.erase(imagePath);  // reload if replaced
                            changed = true;
                        } else {
                            statusMessage_ = "Prompt image import failed: " + ec.message();
                        }
                    }
                }
                if (!imagePath.empty()) {
                    ImGui::SameLine();
                    if (ui::GhostButton("Reset to built-in")) {
                        imagePath.clear();
                        changed = true;
                    }
                }
                // Size + the texture bake only apply to the USE prompt's own
                // HudImage; the PICK UP image rides on the same box.
                if (bakeOf) {
                    ui::DragFloat2("Size (px)##prompt", bakeOf->size, 1.0f, 1.0f, 512.0f, "%.0f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    if (!imagePath.empty()) changed |= hudBakeControls(*bakeOf);
                } else {
                    ImGui::TextDisabled("Drawn in the USE prompt's box.");
                }
            }
            ImGui::PopID();
        };
        promptUi("USE prompt", "useprompt", project_.usePromptIsText, project_.usePromptText, h.imagePath, &h,
                 "Built-in \"USE\" sprite (res/hud/use.png).");
        promptUi("PICK UP prompt", "pickprompt", project_.pickPromptIsText, project_.pickPromptText,
                 project_.pickPromptImage, nullptr, "Built-in \"PICK UP\" sprite (res/hud/pickup.png).");
    } else if (uiFxSel_ == 4 && selectedText_ >= 0 && selectedText_ < (int)project_.hudTexts.size()) {
        // --- a triggerable on-screen text -----------------------------------
        HudText& t = project_.hudTexts[selectedText_];
        const HudRefs refs = hudRefsOf(project_, t.name, true, false);
        auto hp = header("T", sem.warn, nullptr);
        bool del = false;
        {
            const std::string oldName = t.name;
            if (nameField("Name", t.name, ImGui::GetFrameHeight() + fs * 0.3f)) {
                // Renames follow into the flow graphs (Set Text Visible nodes
                // reference texts by name), like layer renames do.
                for (SceneData& sc : project_.scenes)
                    for (SceneObject& o : sc.objects)
                        for (FlowNode& fn : o.flowGraph.nodes) {
                            const FlowNodeType* ft = flowNodeType(fn.type);
                            if (ft && ft->strKind == FlowParamKind::HudTextName && fn.str == oldName)
                                fn.str = t.name;
                        }
                renameHudElementRefs(oldName, t.name, false);
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::MoreButton("Text actions", ImGui::GetFrameHeight())) ImGui::OpenPopup("##textmore");
            if (ImGui::BeginPopup("##textmore")) {
                if (ImGui::MenuItem("Delete text")) del = true;
                ImGui::EndPopup();
            }
        }
        headerEnd(hp, "Text \xC2\xB7 " + refsLine(refs), &refs);
        static const char* kTabs[] = {"Text", "Motion"};
        int& tab = *ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("##insp_text_tab"), 0);
        if (ui::BeginQuietTabBar("##insp_text")) {
            for (int i = 0; i < 2; ++i)
                if (ui::QuietTab(kTabs[i], tab == i)) tab = i;
            ui::EndQuietTabBar();
        }
        if (tab == 0) {
            ui::Caption("Content");
            {
                // multiline: '\n' becomes a new line in the baked sprite
                char textBuf[512];
                std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
                if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf), ImVec2(-1.0f, scaled(70))))
                    t.text = textBuf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                changed |= textTokenPicker("hudtexttok", t.text);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Insert a button icon: {{name}}, or {{action:jump}} to\n"
                                      "follow that action's binding.");
            }
            changed |= fontCombo(t.font);
            if (ui::DragInt("Font size", &t.size, 0.2f, 8, 48, "%d px")) t.size = ImClamp(t.size, 8, 48);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::ColorHex("Color##text", t.color)) changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::Caption("Placement");
            ui::DragFloat2("Position##text", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::Switch("Drop shadow", &t.shadow)) changed = true;
            if (ui::Switch("Visible at game start (Otherwise shown by Set Text Visible)", &t.visibleAtStart))
                changed = true;
            if (const HudTexture* tx = hudTextTexture(t))
                ImGui::TextDisabled("Baked at build to a %dx%d px sprite.", tx->w, tx->h);
            ImGui::TextDisabled("For a string that changes in game: Display Text node.");
        } else {
            changed |= hudMotionControls(t.anim, t.transition, nullptr);
        }
        if (del) {
            project_.hudTexts.erase(project_.hudTexts.begin() + selectedText_);
            selectedText_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (uiFxSel_ == 9 && selectedBar_ >= 0 && selectedBar_ < (int)project_.hudBars.size()) {
        // --- a live bar (docs/hud-animation.md) ------------------------------
        HudBar& b = project_.hudBars[selectedBar_];
        const HudRefs refs = hudRefsOf(project_, b.name, false, true);
        auto hp = header("B", sem.danger, nullptr);
        bool del = false;
        {
            const std::string oldName = b.name;
            if (nameField("Name##bar", b.name, ImGui::GetFrameHeight() + fs * 0.3f))
                renameHudElementRefs(oldName, b.name, true);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::MoreButton("Bar actions", ImGui::GetFrameHeight())) ImGui::OpenPopup("##barmore");
            if (ImGui::BeginPopup("##barmore")) {
                if (ImGui::MenuItem("Delete bar")) del = true;
                ImGui::EndPopup();
            }
        }
        std::string sub = "Bar \xC2\xB7 ";
        if (!b.source.empty() && refs.nodes == 0) sub += "follows save value " + b.source;
        else sub += refsLine(refs);
        headerEnd(hp, sub, &refs);
        static const char* kTabs[] = {"Bar", "Look", "Motion"};
        int& tab = *ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("##insp_bar_tab"), 0);
        if (ui::BeginQuietTabBar("##insp_bar")) {
            for (int i = 0; i < 3; ++i)
                if (ui::QuietTab(kTabs[i], tab == i)) tab = i;
            ui::EndQuietTabBar();
        }
        if (tab == 0) {
            ui::Caption("Placement");
            ui::DragFloat2("Position##bar", b.pos, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::DragFloat2("Size (px)##bar", b.size, 1.0f, 2.0f, 512.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            {
                // A row by hand (Segmented is not one of the row wrappers):
                // the caption column as ui::InspectorRows sizes it.
                static const char* kKinds[] = {"Continuous", "Segments"};
                const float avail = ImGui::GetContentRegionAvail().x;
                const float cw = IM_TRUNC(ImClamp(avail * 0.34f, scaled(76), scaled(116)));
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ImGui::GetStyle().FramePadding.y));
                ImGui::TextDisabled("Type");
                ImGui::SetCursorScreenPos(ImVec2(p.x + cw, p.y));
                if (ui::Segmented("##barkind", kKinds, 2, &b.kind, avail - cw)) changed = true;
            }
            ui::Caption("Value");
            {
                // The save value it follows. Empty = the Set HUD Bar node alone.
                const char* cur = b.source.empty() ? "(Set HUD Bar node only)" : b.source.c_str();
                if (ui::BeginCombo("Follows save value", cur)) {
                    if (ImGui::Selectable("(Set HUD Bar node only)", b.source.empty())) {
                        b.source.clear();
                        changed = true;
                    }
                    for (const SaveValue& sv : project_.saveValues) {
                        ImGui::PushID(sv.name.c_str());
                        if (ImGui::Selectable(sv.name.c_str(), sv.name == b.source)) {
                            b.source = sv.name;
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    if (project_.saveValues.empty())
                        ImGui::TextDisabled("Add save values in the\nProject panel (Save data).");
                    ImGui::EndCombo();
                }
                ui::RowHelp("Read every frame, so Add To Save Value from any graph\n"
                            "moves the bar with no node of its own. Set HUD Bar\n"
                            "writes this value too.");
            }
            ui::DragFloat("Min##bar", &b.minValue, 0.5f, -1e6f, 1e6f, "%.1f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::DragFloat("Max##bar", &b.maxValue, 0.5f, -1e6f, 1e6f, "%.1f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (b.source.empty()) {
                ui::DragFloat("Start##bar", &b.startValue, 0.5f, -1e6f, 1e6f, "%.1f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ui::RowHelp("The value at scene start. A bar following a save\n"
                            "value starts at that value instead.");
            }
            ui::DragFloat("Smoothing (s)", &b.smoothing, 0.01f, 0.0f, 3.0f, "%.2f s");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::RowHelp("Seconds the fill takes to ease to a new value. 0 = jumps.");
            {
                const float start = b.source.empty() ? b.startValue : [&]() {
                    for (const SaveValue& sv : project_.saveValues)
                        if (sv.name == b.source) return sv.value;
                    return b.minValue;
                }();
                float frac = hudBarPreview_ < 0.0f ? hudanim::fraction(start, b.minValue, b.maxValue)
                                                   : hudBarPreview_;
                if (ui::SliderFloat("Preview fill", &frac, 0.0f, 1.0f, "%.2f")) hudBarPreview_ = frac;
                ui::RowHelp("Editor only: how full the preview draws this bar\n"
                            "(the game starts it at its start value).");
                if (hudBarPreview_ >= 0.0f) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Reset##barprev")) hudBarPreview_ = -1.0f;
                }
            }
            ui::Caption("Behaviour");
            if (ui::Switch("Ghost strip (A lagging strip shows how much was just lost)", &b.ghost)) changed = true;
            ui::SliderFloat("Low pulse below", &b.lowFraction, 0.0f, 1.0f,
                            b.lowFraction <= 0.0f ? "never" : "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::RowHelp("Under this fraction the fill breathes - low health\n"
                        "asking for attention. 0 = never.");
            if (ui::Switch("Fill from the right (Drains towards the left edge - a mirrored P2 bar)",
                           &b.rightToLeft))
                changed = true;
            if (ui::Switch("Visible at game start (Otherwise shown by Set HUD Element Visible)##bar",
                           &b.visibleAtStart))
                changed = true;
        } else if (tab == 1) {
            ui::Caption("Colours");
            if (ui::ColorHex("Track color", b.bgColor)) changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::ColorHex("Fill color", b.fillColor)) changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (b.ghost) {
                if (ui::ColorHex("Ghost color", b.ghostColor)) changed = true;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            }
            if (b.kind == 1) {
                ui::Caption("Segments");
                if (ui::DragInt("Segments##bar", &b.segments, 0.1f, 2, 16)) b.segments = ImClamp(b.segments, 2, 16);
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ui::DragFloat("Spacing (px)##bar", &b.spacing, 0.2f, 0.0f, 64.0f, "%.0f px");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            }
            changed |= hudBarImageControls("fillimg", "Fill image (optional)", b.fillImage, false);
            changed |= hudBarImageControls("frameimg", "Frame image (optional)", b.frameImage, true);
        } else {
            changed |= hudMotionControls(b.anim, b.transition, nullptr);
        }
        if (del) {
            project_.hudBars.erase(project_.hudBars.begin() + selectedBar_);
            selectedBar_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (uiFxSel_ == 5 && selectedFx_ >= 0 && selectedFx_ < (int)project_.screenFx.size()) {
        // --- a custom screen effect placement -------------------------------
        ScreenFxPlacement& pl = project_.screenFx[selectedFx_];
        const CustomScreenFx* e = customScreenFx(pl.key);
        headerEnd(header("fx", sem.special, e ? e->title.c_str() : pl.key.c_str()),
                  e ? "Custom effect \xC2\xB7 screen-effects/" +
                          std::filesystem::path(e->sourceFile).filename().string()
                    : std::string("Custom effect \xC2\xB7 file missing"));
        bool remove = false;
        if (!e) {
            ui::StatusLine(1, "The effect file is missing from screen-effects/.");
            ImGui::TextDisabled("Restore it (Add > Custom effects > Reload), or remove it.");
        } else {
            if (ui::Switch("Enabled", &pl.enabled)) changed = true;
            if (e->paramCount > 0) ui::Caption("Parameters");
            for (int i = 0; i < e->paramCount; ++i) {
                if (ui::SliderFloat(e->paramLabel[i].c_str(), &pl.params[i], e->paramMin[i], e->paramMax[i], "%.3f"))
                    pl.params[i] = ImClamp(pl.params[i], e->paramMin[i], e->paramMax[i]);
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            }
            effectNote("Not previewed in the editor (GS blits) - build to see it. Entries above it "
                       "in the stack draw crisp.",
                       "custom-screen-effects");
            if (ImGui::Button("Jump to effect file")) openInVSCode(e->sourceFile);
            ImGui::SameLine();
        }
        if (ui::DangerButton("Remove from stack")) remove = true;
        if (remove) {
            project_.screenFx.erase(project_.screenFx.begin() + selectedFx_);
            selectedFx_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (uiFxSel_ == 0 && selectedHud_ >= 0 && selectedHud_ < n) {
        HudImage& h = project_.hud[selectedHud_];
        const HudRefs refs = hudRefsOf(project_, h.name, false, false);
        auto hp = header("I", sem.ok, nullptr);
        bool del = false;
        {
            const std::string oldName = h.name;
            if (nameField("Name##hud", h.name, ImGui::GetFrameHeight() + fs * 0.3f))
                renameHudElementRefs(oldName, h.name, false);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::MoreButton("Image actions", ImGui::GetFrameHeight())) ImGui::OpenPopup("##hudmore");
            if (ImGui::BeginPopup("##hudmore")) {
                if (ImGui::MenuItem("Delete HUD image")) del = true;
                ImGui::EndPopup();
            }
        }
        headerEnd(hp, "Image \xC2\xB7 " + refsLine(refs), &refs);
        static const char* kTabs[] = {"Image", "Motion"};
        int& tab = *ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("##insp_img_tab"), 0);
        if (ui::BeginQuietTabBar("##insp_img")) {
            for (int i = 0; i < 2; ++i)
                if (ui::QuietTab(kTabs[i], tab == i)) tab = i;
            ui::EndQuietTabBar();
        }
        if (tab == 0) {
            ui::Caption("Placement");
            ui::DragFloat2("Position##hud", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::DragFloat2("Size (px)##hud", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::Switch("Visible at game start (Otherwise shown by Set HUD Element Visible)##hud",
                           &h.visibleAtStart))
                changed = true;
            changed |= hudBakeControls(h);
        } else {
            changed |= hudMotionControls(h.anim, h.transition, nullptr);
        }
        if (del)
            requestAssetDelete(PendingAssetDelete::Hud, h.imagePath, h.name, selectedHud_);
    } else {
        ui::EmptyState(ui::Glyph::Screen, "Nothing selected",
                       "Pick an entry in the list or click an element in the preview.");
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();  // ##ui_props

    // UI edits are not on the undo stack, but they ARE unsaved work: mark the
    // project instead of writing to disk behind the user's back, so the save
    // icon lights up and closing asks. commitChange() rather than a bare
    // setDirty() because that is the ONE verb for a model edit (app.hpp) -
    // for project-wide data it pushes no undo step, so nothing is spammed.
    // A preview drag edits the model every frame; it commits on release.
    if (uiDragMode_ == 0 &&
        (changed || project::sectionJson(project_, project::Section::Hud) != beforeSection))
        commitChange();
    ImGui::End();
}

// The Motion tab every HUD element has (docs/hud-animation.md): a looped
// animation (previewed live in the UI Editor) and the show/hide transition
// (previewed on demand with Play show / hide). `visibleAtStart` is drawn
// when given.
bool App::hudMotionControls(HudAnim& anim, HudTransition& trans, bool* visibleAtStart) {
    bool changed = false;
    ui::Caption("Loop");
    if (visibleAtStart && ui::Switch("Visible at game start##motion", visibleAtStart)) changed = true;
    {
        const char* names[hudanim::KindCount];
        for (int i = 0; i < hudanim::KindCount; ++i) names[i] = hudanim::kindName(i);
        if (ui::Combo("Animation", &anim.kind, names, hudanim::KindCount)) {
            // Each kind reads `amount` in its own unit, so switching kinds
            // re-seeds it with something that looks like that kind.
            switch (anim.kind) {
                case hudanim::Pulse: anim.amount = 0.6f; anim.period = 1.2f; break;
                case hudanim::Breathe: anim.amount = 0.12f; anim.period = 1.6f; break;
                case hudanim::Blink: anim.amount = 0.5f; anim.period = 0.8f; break;
                case hudanim::Shake: anim.amount = 2.0f; anim.period = 0.05f; break;
                case hudanim::None: break;
                default: anim.amount = 4.0f; anim.period = 1.2f; break;
            }
            changed = true;
        }
        ui::RowHelp("A loop on the sprite's position, scale or alpha - no\n"
                    "rebake, a few floats per frame. Runs in the preview.");
    }
    if (anim.kind != hudanim::None) {
        ui::DragFloat(anim.kind == hudanim::Shake ? "Step (s)" : "Period (s)", &anim.period, 0.01f, 0.01f,
                      30.0f, "%.2f s");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (anim.period < 0.01f) anim.period = 0.01f;
        const bool unit01 = anim.kind == hudanim::Pulse || anim.kind == hudanim::Blink ||
                            anim.kind == hudanim::Breathe;
        ui::DragFloat(anim.kind == hudanim::Pulse     ? "Depth"
                      : anim.kind == hudanim::Blink   ? "On share"
                      : anim.kind == hudanim::Breathe ? "Grow"
                                                      : "Amount (px)",
                      &anim.amount, unit01 ? 0.01f : 0.2f, 0.0f, unit01 ? 1.0f : 200.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp(anim.kind == hudanim::Pulse     ? "How far the alpha dips (1 = to invisible)."
                    : anim.kind == hudanim::Blink   ? "Fraction of each period the element is drawn."
                    : anim.kind == hudanim::Breathe ? "How much bigger it grows (0.1 = 10%)."
                    : anim.kind == hudanim::Shake   ? "Jitter radius in pixels; a new offset every Step."
                                                    : "Travel in screen pixels (512x448 space).");
    }
    ui::Caption("Show / hide");
    {
        const char* names[hudanim::TransitionCount];
        for (int i = 0; i < hudanim::TransitionCount; ++i) names[i] = hudanim::transitionName(i);
        if (ui::Combo("Show / hide", &trans.kind, names, hudanim::TransitionCount)) changed = true;
        ui::RowHelp("Played when a flow node shows or hides this element\n"
                    "(in reverse on hide).");
        if (trans.kind != 0) {
            ui::DragFloat("Duration##trans", &trans.duration, 0.01f, 0.0f, 5.0f, "%.2f s");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (trans.duration < 0.0f) trans.duration = 0.0f;
            if (ImGui::Button("Play show / hide")) uiTransPlayT0_ = ImGui::GetTime();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Runs the transition in, holds, and back out in the preview.");
        }
    }
    return changed;
}

// One of a bar's optional images. `withSize` = the image keeps its own
// on-screen size (the frame); a fill is stretched into the bar's box.
bool App::hudBarImageControls(const char* id, const char* title, HudImage& img, bool withSize) {
    bool changed = false;
    ImGui::PushID(id);
    ui::Caption(title);
    if (img.imagePath.empty()) {
        if (ImGui::Button("Set image (PNG)...")) {
            std::vector<HudImage> tmp;
            const int i = importHudImageInto(tmp);
            if (i >= 0) {
                img.imagePath = tmp[i].imagePath;
                if (const HudTexture* t = hudTexture(img.imagePath)) {
                    img.size[0] = (float)t->w;
                    img.size[1] = (float)t->h;
                }
                changed = true;
            }
        }
        if (!withSize && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Cropped to the fill and tinted by the fill color\n(white = as painted).");
    } else {
        ImGui::TextDisabled("%s", img.imagePath.c_str());
        if (ImGui::Button("Replace...")) {
            std::vector<HudImage> tmp;
            const int i = importHudImageInto(tmp);
            if (i >= 0) {
                img.imagePath = tmp[i].imagePath;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ui::GhostButton("Clear image")) {
            img.imagePath.clear();
            changed = true;
        }
        if (withSize) {
            ui::DragFloat2("Size (px)##barimg", img.size, 1.0f, 1.0f, 512.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::RowHelp("Drawn centred on the bar at this size, over the fill.");
        } else {
            ImGui::TextDisabled("Cropped to the fill, tinted by the fill color.");
        }
        if (!img.imagePath.empty()) changed |= hudBakeControls(img);
    }
    ImGui::PopID();
    return changed;
}

// Renames follow into the flow graphs like text renames do: Set HUD Bar names
// a bar, Set HUD Element Visible / Play HUD Effect name any element.
void App::renameHudElementRefs(const std::string& from, const std::string& to,
                               bool isBar) {
    if (from == to) return;
    for (SceneData& sc : project_.scenes)
        for (SceneObject& o : sc.objects)
            for (FlowNode& fn : o.flowGraph.nodes) {
                const FlowNodeType* ft = flowNodeType(fn.type);
                if (!ft || fn.str != from) continue;
                if (ft->strKind == FlowParamKind::HudElementName ||
                    (isBar && ft->strKind == FlowParamKind::HudBarName))
                    fn.str = to;
            }
}

// Property editor for one progress bar (Loading Screens), as label-left rows.
// Returns true when a setting changed (the caller commits).
bool App::loadingBarControls(LoadingBar& b) {
    bool changed = false;
    {
        // A row by hand (Segmented is not one of the row wrappers).
        static const char* kKinds[] = {"Continuous", "Segments"};
        const float avail = ImGui::GetContentRegionAvail().x;
        const float cw = IM_TRUNC(ImClamp(avail * 0.34f, scaled(76), scaled(116)));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ImGui::GetStyle().FramePadding.y));
        ImGui::TextDisabled("Kind");
        ImGui::SetCursorScreenPos(ImVec2(p.x + cw, p.y));
        if (ui::Segmented("##lsbarkind", kKinds, 2, &b.kind, avail - cw)) changed = true;
    }
    if (b.kind == 1) {
        if (ui::DragInt("Segments", &b.segments, 0.1f, 2, 16)) b.segments = ImClamp(b.segments, 2, 16);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::DragFloat("Spacing (px)", &b.spacing, 0.2f, 0.0f, 64.0f, "%.0f px");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ui::Caption("Placement");
    ui::DragFloat2("Position##bar", b.pos, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::RowHelp("Centre of the bar, 0..1 across and down the screen.");
    ui::DragFloat2("Size (px)##bar", b.size, 1.0f, 1.0f, 512.0f, "%.0f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::Caption("Colour");
    if (ui::ColorHex(b.kind == 1 ? "Lit" : "Fill", b.fillColor)) changed = true;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ui::ColorHex(b.kind == 1 ? "Unlit" : "Track", b.bgColor)) changed = true;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (b.kind == 1) {
        ui::Caption("Segment image");
        if (b.segImage.imagePath.empty()) {
            if (ImGui::Button("Plain rect - set a PNG...")) {
                std::vector<HudImage> tmp;
                const int i = importHudImageInto(tmp);
                if (i >= 0) {
                    b.segImage.imagePath = tmp[i].imagePath;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("One texture drawn per segment, tinted Lit / Unlit.");
        } else {
            ImGui::TextDisabled("%s", b.segImage.imagePath.c_str());
            if (ImGui::Button("Replace...")) {
                std::vector<HudImage> tmp;
                const int i = importHudImageInto(tmp);
                if (i >= 0) {
                    b.segImage.imagePath = tmp[i].imagePath;
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ui::GhostButton("Clear image")) {
                b.segImage.imagePath.clear();
                changed = true;
            }
            changed |= hudBakeControls(b.segImage);
        }
    }
    return changed;
}

// The frame an animation shows - the same choice loadingscreen::renderFrame
// makes on the PS2 (docs/loading-screens.md, "Animations"). -1 = hidden.
static int loadingAnimFrame(const LoadingAnim& a, float fraction, bool waiting, float clock) {
    const int n = (int)a.frames.size();
    if (n <= 0) return -1;
    const int tick = (int)(clock * a.fps);
    if (a.mode == 0) {
        if (waiting && a.afterLoad == 0) return -1;
        if (a.playback == 1 && n > 1) {
            const int k = tick % (2 * n - 2);
            return k < n ? k : 2 * n - 2 - k;
        }
        return tick % n;
    }
    if (waiting && a.afterLoad == 1 && n >= 2) return n - 2 + (tick & 1);
    if (a.lastAtFull && n >= 2) {
        if (fraction >= 1.0f) return n - 1;
        return ImMin(n - 2, (int)(fraction * (float)(n - 1)));
    }
    return ImMin(n - 1, (int)(fraction * (float)n));
}

// Bytes a frame takes in GS VRAM once baked (pow2 + its quantization).
static int loadingFrameBytes(int w, int h, const std::string& quant) {
    auto pow2 = [](int v) {
        int p = 8;
        while (p < v && p < 512) p <<= 1;
        return p;
    };
    const int pw = pow2(w), ph = pow2(h);
    if (quant == "4bit") return pw * ph / 2 + 64;
    if (quant == "8bit") return pw * ph + 1024;
    return pw * ph * 4;
}

// Eight frames of a dotted ring, one dot lit: the Spinner template's art, so
// a new screen spins before anyone has drawn anything.
static std::vector<std::string> writeSpinnerFrames(const std::string& projectDir, const std::string& base) {
    std::vector<std::string> out;
    const int S = 64, dots = 8;
    for (int f = 0; f < dots; ++f) {
        std::vector<unsigned char> px((size_t)S * S * 4, 0);
        for (int d = 0; d < dots; ++d) {
            const float ang = (float)d / dots * 6.2831853f;
            const float cx = S * 0.5f + std::cos(ang) * S * 0.34f, cy = S * 0.5f + std::sin(ang) * S * 0.34f;
            const int age = (f - d + dots) % dots;  // 0 = the lit dot, then its fading tail
            const float a = age == 0 ? 1.0f : ImMax(0.15f, 0.75f - age * 0.15f);
            const float r = S * 0.075f;
            for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); ++y)
                for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); ++x) {
                    if (x < 0 || y < 0 || x >= S || y >= S) continue;
                    const float dd = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
                    const float cover = ImClamp(r + 0.5f - dd, 0.0f, 1.0f) * a;
                    unsigned char* p = &px[((size_t)y * S + x) * 4];
                    p[0] = p[1] = p[2] = 255;
                    p[3] = (unsigned char)ImMax((int)p[3], (int)(cover * 255.0f + 0.5f));
                }
        }
        const std::string rel = "res/hud/" + base + "-" + std::to_string(f + 1) + ".png";
        const std::filesystem::path abs = std::filesystem::path(projectDir) / rel;
        std::error_code ec;
        std::filesystem::create_directories(abs.parent_path(), ec);
        if (stbi_write_png(abs.string().c_str(), S, S, 4, px.data(), S * 4)) out.push_back(rel);
    }
    return out;
}

bool App::importLoadingSheet(LoadingAnim& a, const std::string& src, int cols, int rows, int count) {
    int w = 0, h = 0, comp = 0;
    unsigned char* img = stbi_load(src.c_str(), &w, &h, &comp, 4);
    if (!img) {
        statusMessage_ = "Sprite sheet import failed: not a readable image";
        return false;
    }
    cols = ImClamp(cols, 1, 32);
    rows = ImClamp(rows, 1, 32);
    count = ImClamp(count, 1, cols * rows);
    const int cw = w / cols, ch = h / rows;
    if (cw < 1 || ch < 1) {
        stbi_image_free(img);
        statusMessage_ = "Sprite sheet import failed: more cells than pixels";
        return false;
    }
    const std::string base = sanitizeAssetName(std::filesystem::path(src).stem().string());
    std::vector<unsigned char> cell((size_t)cw * ch * 4);
    int written = 0;
    for (int k = 0; k < count; ++k) {
        const int cx = (k % cols) * cw, cy = (k / cols) * ch;
        for (int y = 0; y < ch; ++y)
            std::memcpy(&cell[(size_t)y * cw * 4], &img[((size_t)(cy + y) * w + cx) * 4], (size_t)cw * 4);
        const std::string rel = "res/hud/" + base + "-" + std::to_string(k + 1) + ".png";
        const std::filesystem::path abs = std::filesystem::path(project_.dir) / rel;
        std::error_code ec;
        std::filesystem::create_directories(abs.parent_path(), ec);
        if (!stbi_write_png(abs.string().c_str(), cw, ch, 4, cell.data(), cw * 4)) continue;
        hudTexCache_.erase(rel);
        HudImage f;
        f.name = a.name;
        f.imagePath = rel;
        if (!a.frames.empty()) f.texQuant = a.frames.front().texQuant;
        a.frames.push_back(std::move(f));
        ++written;
    }
    stbi_image_free(img);
    if (written > 0 && a.frames.size() == (size_t)written) {
        a.size[0] = (float)ImMin(cw, 512);
        a.size[1] = (float)ImMin(ch, 448);
    }
    statusMessage_ = "Sliced " + std::to_string(written) + " frames from " +
                     std::filesystem::path(src).filename().string();
    return written > 0;
}

bool App::loadingAnimControls(LoadingAnim& a) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    bool changed = false;
    {
        static const char* kModes[] = {"Spinner", "Progress"};
        if (ui::SegmentedRow("Plays as", kModes, 2, &a.mode)) changed = true;
        ui::RowHelp("SPINNER loops on its own fps while the scene loads.\n"
                    "PROGRESS picks the frame from how far the load is - two cars\n"
                    "closing in, crashing at 100%.");
    }

    // The frames, as a strip: click x to drop one, + to add PNGs, or slice a sheet.
    ui::Caption("Frames", std::to_string(a.frames.size()).c_str());
    {
        const float th = fs * 3.0f;
        int drop = -1;
        ImGui::BeginChild("##lsframes", ImVec2(0, th + fs * 1.6f), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (int k = 0; k < (int)a.frames.size(); ++k) {
            ImGui::PushID(k);
            if (k) ImGui::SameLine();
            ImGui::BeginGroup();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("frame", ImVec2(th, th));
            dl->AddRectFilled(p, ImVec2(p.x + th, p.y + th), ImGui::GetColorU32(sem.field), 4.0f);
            if (const HudTexture* t = hudTexture(a.frames[k].imagePath))
                dl->AddImage((ImTextureID)(intptr_t)t->tex, p, ImVec2(p.x + th, p.y + th));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s - right-click to remove", a.frames[k].imagePath.c_str());
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) drop = k;
            }
            ImGui::TextDisabled("%d", k + 1);
            ImGui::EndGroup();
            ImGui::PopID();
        }
        if (!a.frames.empty()) ImGui::SameLine();
        if (ImGui::Button("+ PNG...", ImVec2(0, th))) {
            const int i = importHudImageInto(a.frames);
            if (i >= 0) {
                a.frames[i].name = a.name;
                if (a.frames.size() == 1) {
                    a.size[0] = a.frames[i].size[0];
                    a.size[1] = a.frames[i].size[1];
                } else {
                    a.frames[i].texQuant = a.frames.front().texQuant;
                }
                changed = true;
            }
        }
        ImGui::EndChild();
        if (drop >= 0) {
            a.frames.erase(a.frames.begin() + drop);
            changed = true;
        }
    }
    if (ImGui::Button("Slice a sprite sheet...")) {
        lsSheetPath_ = pickPngFile();
        lsSheetOpen_ = !lsSheetPath_.empty();
    }
    if (lsSheetOpen_) ImGui::OpenPopup("Slice sprite sheet");
    if (ImGui::BeginPopupModal("Slice sprite sheet", &lsSheetOpen_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(std::filesystem::path(lsSheetPath_).filename().string().c_str());
        ImGui::TextDisabled("Cut into cells, row by row; each becomes a frame PNG in res/hud.");
        ImGui::SetNextItemWidth(fs * 6.0f);
        ImGui::InputInt("Columns", &lsSheetCols_);
        ImGui::SetNextItemWidth(fs * 6.0f);
        ImGui::InputInt("Rows", &lsSheetRows_);
        lsSheetCols_ = ImClamp(lsSheetCols_, 1, 32);
        lsSheetRows_ = ImClamp(lsSheetRows_, 1, 32);
        ImGui::SetNextItemWidth(fs * 6.0f);
        ImGui::InputInt("Frames used", &lsSheetCount_);
        lsSheetCount_ = ImClamp(lsSheetCount_, 1, lsSheetCols_ * lsSheetRows_);
        if (ui::PrimaryButton("Slice")) {
            if (importLoadingSheet(a, lsSheetPath_, lsSheetCols_, lsSheetRows_, lsSheetCount_)) changed = true;
            lsSheetOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::GhostButton("Cancel")) {
            lsSheetOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (a.mode == 0) {
        if (ui::DragFloat("Speed", &a.fps, 0.1f, 1.0f, 60.0f, "%.0f fps")) a.fps = ImClamp(a.fps, 1.0f, 60.0f);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        static const char* kPlay[] = {"Loop", "Ping-pong"};
        if (ui::SegmentedRow("Playback", kPlay, 2, &a.playback)) changed = true;
        static const char* kAfter[] = {"Hide", "Keep spinning"};
        if (ui::SegmentedRow("After loading", kAfter, 2, &a.afterLoad)) changed = true;
        ui::RowHelp("While the screen waits for a button (Press to continue).");
    } else {
        if (ui::Switch("Last frame only when loaded", &a.lastAtFull)) changed = true;
        ui::RowHelp("The final frame (the crash) waits for 100%: the load climbs\n"
                    "through the others, and the last one is the finish.");
        static const char* kAfter[] = {"Hold last", "Loop last two"};
        if (ui::SegmentedRow("After loading", kAfter, 2, &a.afterLoad)) changed = true;
        ui::RowHelp("While the screen waits for a button (Press to continue).");
        if (ui::DragFloat("Loop speed", &a.fps, 0.1f, 1.0f, 60.0f, "%.0f fps")) a.fps = ImClamp(a.fps, 1.0f, 60.0f);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ui::Caption("Placement");
    ui::DragFloat2("Position##lsanim", a.pos, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ui::DragFloat2("Size (px)##lsanim", a.size, 1.0f, 1.0f, 512.0f, "%.0f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    // What the frames cost: each is its own texture in VRAM while loading.
    if (!a.frames.empty()) {
        ui::Caption("Bake (every frame)");
        if (hudBakeControls(a.frames.front())) {
            for (HudImage& f : a.frames) f.texQuant = a.frames.front().texQuant;
            changed = true;
        }
        int bytes = 0;
        for (const HudImage& f : a.frames)
            if (const HudTexture* t = hudTexture(f.imagePath)) bytes += loadingFrameBytes(t->w, t->h, f.texQuant);
        ImGui::TextDisabled("%d frames - %.0f KB of GS VRAM (~1.3 MB free while loading)", (int)a.frames.size(),
                            bytes / 1024.0f);
    }
    return changed;
}

// Loading Screens > Assignments (docs/loading-screens.md, "Assignments"):
// every load in the game and the screen it shows, in one table - Game start,
// the Default, then each scene - with what each row resolves to. A screen's
// own inspector only reports this (Used by); it is edited here.
bool App::drawLoadingAssignments() {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    auto& screens = project_.loadingScreens;
    bool changed = false;
    const bool between = project_.settings.loadingScreen;
    const int def = project_.defaultLoadingScreen >= 0 && project_.defaultLoadingScreen < (int)screens.size()
                        ? project_.defaultLoadingScreen
                        : -1;
    if (project_.bootLoadingScreen >= (int)screens.size()) project_.bootLoadingScreen = -1;
    const int startIdx = project_.scenes.empty()                                      ? -1
                         : project_.startScene >= 0 &&
                                 project_.startScene < (int)project_.scenes.size() ? project_.startScene
                                                                                    : 0;
    // A resolved index (loadingScreenIndexFor codes) as words.
    auto nameOf = [&](int idx) -> std::string {
        return idx >= 0 ? screens[idx].name : idx == -3 ? std::string("None") : std::string("Built-in");
    };
    // The last column: the screen (a link to it in Screens) and how it got there.
    auto resolved = [&](int idx, const std::string& via, bool warn) {
        ImGui::AlignTextToFramePadding();
        if (idx >= 0) {
            if (ImGui::TextLink((screens[idx].name + "##go").c_str())) {
                selectedLoadingScreen_ = idx;
                lsSelKind_ = 3;
                lsSelIdx_ = -1;
                lsTab_ = 0;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Edit this screen");
        } else {
            ImGui::TextUnformatted(idx == -4 ? "No loading screen" : nameOf(idx).c_str());
        }
        if (!via.empty()) {
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::TextColored(warn ? sem.warn : sem.textDim, "\xC2\xB7 %s", via.c_str());
        }
    };
    auto head = [&](const char* name, const char* note) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        if (note && *note) {
            ImGui::SameLine(0.0f, fs * 0.5f);
            ImGui::TextDisabled("%s", note);
        }
    };

    ImGui::Dummy(ImVec2(0.0f, fs * 0.2f));
    ImGui::TextDisabled("Every load in the game and the screen it shows. Scenes set to Default follow the "
                        "Default row.");
    if (!between)
        ui::StatusLine(1, "Show between scenes is off: scenes cut straight in. Game start still shows its "
                          "pick.");
    if (!ImGui::BeginTable("##ls_assign", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp,
                           ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
        return false;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Load", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Shows", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Resolves to", ImGuiTableColumnFlags_WidthStretch, 1.1f);
    ImGui::TableHeadersRow();

    // Game start: the first load at power-on (Project::bootLoadingScreen).
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        head("Game start", "first load at power-on");
        ImGui::TableNextColumn();
        const SceneData* st = startIdx >= 0 ? &project_.scenes[startIdx] : nullptr;
        const int stRes = st ? project::loadingScreenIndexFor(project_, *st) : def;
        const std::string same = "Same as start scene (" + (st ? st->name : std::string("-")) + " \xE2\x86\x92 " +
                                 (between ? nameOf(stRes) : std::string("none")) + ")";
        int& boot = project_.bootLoadingScreen;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##as_boot", boot >= 0 ? screens[boot].name.c_str() : same.c_str())) {
            if (ImGui::Selectable(same.c_str(), boot < 0)) {
                boot = -1;
                changed = true;
            }
            ImGui::Separator();
            for (int i = 0; i < (int)screens.size(); ++i)
                if (ImGui::Selectable(screens[i].name.c_str(), boot == i)) {
                    boot = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        if (boot >= 0) resolved(boot, "picked here", false);
        else if (!between) resolved(-4, "Show between scenes is off", true);
        else resolved(stRes, "via " + (st ? st->name : std::string("the start scene")), false);
    }
    // Default: what every scene left on Default shows.
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        head("Default", "scenes set to Default");
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##as_default", def >= 0 ? screens[def].name.c_str() : "Built-in")) {
            if (ImGui::Selectable("Built-in", def < 0)) {
                project_.defaultLoadingScreen = -1;
                changed = true;
            }
            ImGui::Separator();
            for (int i = 0; i < (int)screens.size(); ++i)
                if (ImGui::Selectable(screens[i].name.c_str(), def == i)) {
                    project_.defaultLoadingScreen = i;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        resolved(def, def < 0 ? "res/hud/loading.png" : "", false);
    }
    // One row per scene; greyed (still editable) while scenes cut straight in.
    const std::string defLabel = "Default (" + nameOf(def) + ")";
    for (int si = 0; si < (int)project_.scenes.size(); ++si) {
        SceneData& sc = project_.scenes[si];
        ImGui::PushID(si);
        if (!between) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        head(sc.name.c_str(), si == startIdx ? "start scene" : "");
        ImGui::TableNextColumn();
        bool dangling = !sc.loadingScreen.empty() && sc.loadingScreen != project::kLoadingBuiltIn &&
                        sc.loadingScreen != project::kLoadingNone;
        for (const LoadingScreenDef& o : screens) dangling &= o.name != sc.loadingScreen;
        const std::string cur = sc.loadingScreen.empty() || dangling ? defLabel
                                : sc.loadingScreen == project::kLoadingBuiltIn ? std::string("Built-in")
                                : sc.loadingScreen == project::kLoadingNone    ? std::string("None")
                                                                      : sc.loadingScreen;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##as_scene", cur.c_str())) {
            auto pick = [&](const char* label, const std::string& v) {
                if (ImGui::Selectable(label, sc.loadingScreen == v)) {
                    sc.loadingScreen = v;
                    changed = true;
                }
            };
            pick(defLabel.c_str(), std::string());
            ImGui::Separator();
            for (const LoadingScreenDef& o : screens) pick(o.name.c_str(), o.name);
            ImGui::Separator();
            pick("Built-in", project::kLoadingBuiltIn);
            pick("None", project::kLoadingNone);
            ImGui::EndCombo();
        }
        ImGui::TableNextColumn();
        const int res = project::loadingScreenIndexFor(project_, sc);
        if (dangling) resolved(res, "\"" + sc.loadingScreen + "\" is gone - via Default", true);
        else if (sc.loadingScreen.empty()) resolved(res, "via Default", false);
        else if (res == -3) resolved(res, "black while it loads", false);
        else resolved(res, "", false);
        if (!between) ImGui::PopStyleVar();
        ImGui::PopID();
    }
    ImGui::EndTable();
    return changed;
}

bool App::loadingWaitControls(LoadingScreenDef& ls) {
    bool changed = false;
    int wait = ls.waitForButton ? 1 : 0;
    static const char* kWait[] = {"Straight away", "Wait for a button"};
    if (ui::SegmentedRow("When loaded", kWait, 2, &wait)) {
        ls.waitForButton = wait == 1;
        changed = true;
    }
    ui::RowHelp("Wait for a button: when the load is done the screen holds on its\n"
                "continue prompt until the player presses the button.");
    if (!ls.waitForButton) return changed;
    static const char* kButtons[] = {"Any", "START", "Cross"};
    if (ui::SegmentedRow("Button", kButtons, 3, &ls.waitButton)) changed = true;
    ui::RowHelp("ANY = Cross, Circle, Square, Triangle or START.\n"
                "Only at power-on? Make this the Game start screen (Assignments)\n"
                "and give the scenes one that does not wait.");
    return changed;
}

// Draws a loading screen into the frame at p0/size (512x448 space), honoring
// `fraction` for the progress bars and animations - matches
// loadingscreen::renderFrame on the PS2. `hits` (may be null) receives each
// element's box (sel 0 image, 1 text, 2 bar, 5 animation, 6 prompt), in draw
// order.
void App::drawLoadingFrame(ImDrawList* dl, const LoadingScreenDef& ls, float fraction, ImVec2 p0,
                           ImVec2 size, std::vector<HudHit>* hits, bool waiting, float clock) {
    const float w = size.x, h = size.y;
    const ImVec2 p1(p0.x + w, p0.y + h);
    auto col = [](const float* c, float a = 1.0f) {
        return IM_COL32((int)(c[0] * 255.0f + 0.5f), (int)(c[1] * 255.0f + 0.5f), (int)(c[2] * 255.0f + 0.5f),
                        (int)(a * 255.0f + 0.5f));
    };
    dl->AddRectFilled(p0, p1, col(ls.bgColor));
    auto sw = [&](float px) { return px / 512.0f * w; };
    auto sh = [&](float px) { return px / 448.0f * h; };
    auto cx = [&](float nx) { return p0.x + nx * w; };
    auto cy = [&](float ny) { return p0.y + ny * h; };
    auto push = [&](int sel, int idx, ImVec2 a, ImVec2 b) {
        if (!hits) return;
        HudHit hh;
        hh.sel = sel;
        hh.index = idx;
        hh.min = a;
        hh.max = b;
        hits->push_back(hh);
    };
    const bool showPrompt = waiting && ls.waitForButton;
    const bool hideOthers = showPrompt && ls.promptHideOthers;
    for (int i = 0; i < (int)ls.images.size(); ++i) {
        const HudImage& im = ls.images[i];
        const float dw = sw(im.size[0]), dh = sh(im.size[1]);
        const ImVec2 a(cx(im.pos[0]) - dw * 0.5f, cy(im.pos[1]) - dh * 0.5f);
        const ImVec2 b(a.x + dw, a.y + dh);
        if (const HudTexture* t = hudTexture(im.imagePath))
            dl->AddImage((ImTextureID)(intptr_t)t->tex, a, b);
        else
            dl->AddRect(a, b, IM_COL32(255, 80, 80, 255));
        push(0, i, a, b);
    }
    for (int i = 0; i < (int)ls.texts.size(); ++i) {
        const HudText& t = ls.texts[i];
        HudText tc = t;  // mangle the cache key so it never collides with HUD texts
        tc.name = "lsprev\x1f" + ls.name + "\x1f" + t.name;
        if (const HudTexture* tex = hudTextTexture(tc)) {
            const float dw = sw((float)tex->w), dh = sh((float)tex->h);
            const ImVec2 a(cx(t.pos[0]) - dw * 0.5f, cy(t.pos[1]) - dh * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex->tex, a, ImVec2(a.x + dw, a.y + dh));
            push(1, i, a, ImVec2(a.x + dw, a.y + dh));
        }
    }
    for (int i = 0; i < (int)ls.anims.size(); ++i) {
        const LoadingAnim& an = ls.anims[i];
        const float dw = sw(an.size[0]), dh = sh(an.size[1]);
        const ImVec2 a(cx(an.pos[0]) - dw * 0.5f, cy(an.pos[1]) - dh * 0.5f);
        const ImVec2 b(a.x + dw, a.y + dh);
        const int f = hideOthers ? -1 : loadingAnimFrame(an, fraction, waiting, clock);
        if (an.frames.empty())
            dashedRect(dl, a, b, IM_COL32(242, 143, 224, 200), 6.0f);
        else if (f >= 0)
            if (const HudTexture* t = hudTexture(an.frames[f].imagePath))
                dl->AddImage((ImTextureID)(intptr_t)t->tex, a, b);
        push(5, i, a, b);
    }
    for (int i = 0; i < (int)ls.bars.size() && !hideOthers; ++i) {
        const LoadingBar& b = ls.bars[i];
        const float bw = sw(b.size[0]), bh = sh(b.size[1]);
        const float bx = cx(b.pos[0]) - bw * 0.5f;
        const float by = cy(b.pos[1]) - bh * 0.5f;
        const HudTexture* segTex =
            b.kind == 1 && !b.segImage.imagePath.empty() ? hudTexture(b.segImage.imagePath) : nullptr;
        if (b.kind == 0) {
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh), col(b.bgColor));
            if (fraction > 0.0f)
                dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * fraction, by + bh), col(b.fillColor));
        } else {
            const int segs = b.segments < 1 ? 1 : b.segments;
            const int lit = (int)(fraction * segs + 0.001f);
            const float segW = (bw - sw(b.spacing) * (segs - 1)) / segs;
            for (int k = 0; k < segs; ++k) {
                const float sx = bx + k * (segW + sw(b.spacing));
                const float* c = (k < lit) ? b.fillColor : b.bgColor;
                if (segTex)
                    dl->AddImage((ImTextureID)(intptr_t)segTex->tex, ImVec2(sx, by), ImVec2(sx + segW, by + bh),
                                 ImVec2(0, 0), ImVec2(1, 1), col(c));
                else
                    dl->AddRectFilled(ImVec2(sx, by), ImVec2(sx + segW, by + bh), col(c));
            }
        }
        push(2, i, ImVec2(bx, by), ImVec2(bx + bw, by + bh));
    }
    // The continue prompt, last - on screen while waiting, and as a selectable
    // ghost the rest of the time so it can still be placed.
    if (ls.waitForButton) {
        HudText tc = ls.prompt;
        tc.name = "lsprev\x1f" + ls.name + "\x1f\x1f" "continue";
        if (const HudTexture* tex = hudTextTexture(tc)) {
            const float dw = sw((float)tex->w), dh = sh((float)tex->h);
            const ImVec2 a(cx(ls.prompt.pos[0]) - dw * 0.5f, cy(ls.prompt.pos[1]) - dh * 0.5f);
            float alpha = 0.25f;
            if (showPrompt) {
                const float period = ls.promptPeriod > 0.05f ? ls.promptPeriod : 1.0f;
                float phase = clock / period;
                phase -= std::floor(phase);
                alpha = ls.promptBlink == 1   ? (phase < 0.5f ? 1.0f : 0.0f)
                        : ls.promptBlink == 2 ? 0.3f + 0.7f * (phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f)
                                              : 1.0f;
            }
            if (alpha > 0.0f)
                dl->AddImage((ImTextureID)(intptr_t)tex->tex, a, ImVec2(a.x + dw, a.y + dh), ImVec2(0, 0),
                             ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(alpha * 255.0f)));
            push(6, 0, a, ImVec2(a.x + dw, a.y + dh));
        }
    }
}

// --- Tools > Animation Editor ----------------------------------------------
// Non-destructive clip editing (docs/animated-models.md). Nothing here writes
// the source .glb/.fbx: every control edits an AnimClipEdit row, which the
// build folds into the .tskl (animedit::applyClipEdits) and the viewport
// preview applies to the placed objects. Project-wide data like the presets
// and sequences: outside undo, but marked with commitChange() like every other
// edit (app.hpp) - history_.push carries no project-wide collection, so the
// commit dirties and bumps the session serial without pushing an undo step.

AnimClipEdit& App::animEditFor(const std::string& model,
                               const std::string& clip) {
    for (AnimClipEdit& e : project_.animClipEdits)
        if (e.model == model && e.clip == clip) return e;
    AnimClipEdit e;
    e.model = model;
    e.clip = clip;
    project_.animClipEdits.push_back(std::move(e));
    return project_.animClipEdits.back();
}

void App::pruneAnimEdits() {
    auto& v = project_.animClipEdits;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const AnimClipEdit& e) { return e.isDefault(); }),
            v.end());
}

void App::renameAnimClipRefs(const std::string& model, const std::string& from,
                             const std::string& to) {
    if (from == to || from.empty()) return;
    auto swap = [&](std::string& s) {
        if (s == from) s = to;
    };
    for (SceneData& sc : project_.scenes)
        for (SceneObject& o : sc.objects) {
            if (o.modelPath != model) continue;
            swap(o.animClip);
            swap(o.playerIdleClip);
            swap(o.playerWalkClip);
            swap(o.playerRunClip);
            swap(o.playerSprintClip);
            swap(o.playerJumpClip);
            // Animation nodes in this object's own graph default to "self",
            // so their Clip param names a clip of THIS model. A node whose
            // target is wired in from elsewhere cannot be resolved from here
            // - the panel says so next to the field.
            for (FlowNode& n : o.flowGraph.nodes)
                if (n.type == "Animation") swap(n.str);
        }
}

// Resolves a stored preview-light selection into the viewport override. An
// unknown preset name (renamed or deleted since editor.ini was written) falls
// back to the scene, which is also the default - the previews are meant to
// show what ships, the override is the opt-out.
Viewport::PreviewLight App::previewLight(const std::string& sel) const {
    Viewport::PreviewLight l;
    if (sel.empty()) return l;  // off: the scene's own ambience
    if (sel == "*") {
        l.on = true;  // AmbiencePreset's own defaults = the neutral studio look
        const AmbiencePreset n;
        for (int i = 0; i < 3; ++i) l.dir[i] = n.lightDir[i], l.color[i] = n.lightColor[i];
        l.ambient = n.ambient;
        l.diffuse = n.diffuse;
        l.brightness = n.brightness;
        return l;
    }
    for (const AmbiencePreset& a : project_.ambiencePresets) {
        if (a.name != sel) continue;
        l.on = true;
        for (int i = 0; i < 3; ++i) l.dir[i] = a.lightDir[i], l.color[i] = a.lightColor[i];
        l.ambient = a.ambient;
        l.diffuse = a.diffuse;
        l.brightness = a.brightness;
        return l;
    }
    return l;
}

bool App::previewLightCombo(const char* label, std::string& sel) {
    const char* current = sel.empty()  ? "Scene ambience"
                          : sel == "*" ? "Neutral studio"
                                       : sel.c_str();
    bool changed = false;
    ImGui::SetNextItemWidth(scaled(170));
    if (ImGui::BeginCombo(label, current)) {
        if (ImGui::Selectable("Scene ambience", sel.empty()) && !sel.empty()) {
            sel.clear();
            changed = true;
        }
        if (ImGui::Selectable("Neutral studio", sel == "*") && sel != "*") {
            sel = "*";
            changed = true;
        }
        if (!project_.ambiencePresets.empty()) ImGui::Separator();
        for (const AmbiencePreset& a : project_.ambiencePresets)
            if (ImGui::Selectable(a.name.c_str(), sel == a.name) && sel != a.name) {
                sel = a.name;
                changed = true;
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Light this preview bakes with.\n\n"
            "Scene ambience shows what the object will really look like\n"
            "in-game - but a dark scene makes the preview dark too.\n"
            "Neutral studio, or any ambience preset, lights the preview\n"
            "instead without touching the scene or the build.");
    return changed;
}

const App::AnimImportProbe& App::animImportProbe(
    const std::string& modelRel, const std::string& sourceRel,
    const std::vector<std::pair<std::string, std::string>>& boneMap) {
    std::string key = modelRel + "|" + sourceRel;
    for (const auto& [a, b] : boneMap) key += "|" + a + ">" + b;
    if (auto it = animProbeCache_.find(key); it != animProbeCache_.end())
        return it->second;

    // ONE pair of parseSkel calls answers everything the panel shows: whether
    // the file is usable, how many clips it holds and how well the rigs match.
    // It deliberately does NOT go through glbInfo, which bakes every clip of
    // the file into morph frames - that is the single most expensive thing the
    // editor can do to a model, and picking a file from a combo must not cost
    // it just to print "3 clips".
    AnimImportProbe probe;
    std::string err;
    const glbparser::Skel* donor =
        skelCache_.get(project_.filePath(sourceRel), err);
    const glbparser::Skel* target =
        donor ? skelCache_.get(project_.filePath(modelRel), err) : nullptr;
    if (!donor || !target) {
        probe.error = err;
    } else {
        probe.ok = true;
        probe.clipCount = (int)donor->clips.size();
        animmerge::MergeOptions opts;
        opts.boneMap = boneMap;
        probe.match = animmerge::compatibility(*target, *donor, opts);
        probe.retarget = animmerge::retargetInfo(*target, *donor, opts);
        // Bones with a home - the number the mapping editor moves.
        std::set<int> boneNodes;
        for (const glbparser::SkelJoint& j : donor->palette)
            boneNodes.insert(j.node);
        for (int n : boneNodes) {
            if (n < 0 || n >= (int)donor->nodes.size()) continue;
            ++probe.bonesTotal;
            if (animmerge::resolveBoneName(*target,
                                           donor->nodes[(size_t)n].name,
                                           opts) >= 0)
                ++probe.bonesMapped;
        }
    }
    return animProbeCache_.emplace(key, std::move(probe)).first->second;
}

void App::invalidateAnimCaches(const std::string& modelRel) {
    // Every one of these holds a clip list derived from a parse of the model,
    // and an import changes which clips exist - so they all go together. The
    // model-info pair is always evicted as a unit (app.cpp precedent).
    // NOT skelCache_: files did not change (size+mtime revalidation covers
    // the case where they did), and keeping it is what makes the re-derive
    // below a merge instead of a parse.
    if (modelRel.empty()) {
        glbInfoCache_.clear();
        modelInfoCache_.clear();
        animProbeCache_.clear();
    } else {
        // One model changed - dropping the whole zoo re-baked EVERY animated
        // model on each mapper Apply (reported as "baking more than needed").
        glbInfoCache_.erase(modelRel);
        modelInfoCache_.erase(modelRel);
        for (auto it = animProbeCache_.begin(); it != animProbeCache_.end();)
            it = it->first.rfind(modelRel + "|", 0) == 0
                     ? animProbeCache_.erase(it)
                     : std::next(it);
    }
    viewport_.invalidateAnimatedModels(modelRel);
}

// Tools > Animation Editor > Imported clips (docs/animation-import.md).
//
// One row per donor file. The compatibility number is the thing to read before
// anything else: it is the fraction of the donor's animated bones that have a
// counterpart on this model, so a rig mismatch shows as a low percentage here
// rather than as a character folded inside out in the preview.
// --- the bone-mapping editor (docs/animation-import.md, "Mapping bones") ---
//
// Two skeletons drawn side by side from their bind poses, donor left, target
// right. Click a red donor joint, then the target joint it should drive - the
// pair lands in the import row's boneMap, which the merge consults before any
// name matching. The fuzzy suggestions (animmerge::suggestBoneMap) are drawn
// amber and applied only through the Accept button: a wrong guess bends the
// wrong limb, so a person confirms them.

void App::loadBoneAliases() {
    if (boneAliasesLoaded_) return;
    boneAliasesLoaded_ = true;
    std::ifstream f(platform::configDir() / "bone-aliases.ini");
    std::string line;
    while (std::getline(f, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        boneAliases_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void App::saveBoneAliases() {
    std::ofstream f(platform::configDir() / "bone-aliases.ini",
                    std::ios::trunc);
    int count = 0;
    for (const auto& [k, v] : boneAliases_) {
        if (++count > 800) break;  // a vocabulary, not a log
        f << k << "=" << v << "\n";
    }
}

// One driver per donor AND per target bone: adding a pair evicts any
// earlier pair claiming either end. Without this, pairs accepted across
// sessions could both claim one target - the resolver gave it to the first,
// the second died silently, and the bone the second SHOULD have freed held
// its bind pose (reported as a spine segment diving under the pelvis).
static void upsertPair(
    std::vector<std::pair<std::string, std::string>>& pairs,
    const std::string& from, const std::string& to) {
    pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                               [&](const auto& pr) {
                                   return pr.first == from || pr.second == to;
                               }),
                pairs.end());
    pairs.emplace_back(from, to);
}

static float rigExtent(const std::vector<float>& pos, const glbparser::Skel& sk, int ax);

void App::openAnimBoneMap(int importRow) {
    if (importRow < 0 || importRow >= (int)project_.animImports.size()) return;
    const AnimImport& a = project_.animImports[(size_t)importRow];
    std::string err;
    // Through the cache: the probe just parsed both files, so opening the
    // window is a copy, not a parse (the "Map bones stalls" report).
    const glbparser::Skel* t = skelCache_.get(project_.filePath(a.model), err);
    const glbparser::Skel* d =
        t ? skelCache_.get(project_.filePath(a.source), err) : nullptr;
    animMapParsed_ = t && d;
    if (animMapParsed_) {
        animMapTarget_ = *t;
        animMapDonor_ = *d;
    }
    animMapSuggValid_ = false;
    animMapHiD_ = animMapHiT_ = -1;
    animMapZoom_ = 1.0f;
    animMapPanX_ = animMapPanY_ = 0.0f;
    animMapPoseClip_ = -1;
    animMapPosePlay_ = false;
    animMapPoseT_ = 0.0f;
    animMapAffixOk_ = false;
    if (animMapAiGen_) animMapAiGen_->cancel();
    animMapAiGen_.reset();
    animMapAiSugg_.clear();
    animMapAiErr_.clear();
    loadBoneAliases();
    if (animMapParsed_) {
        animmerge::bindGlobals(animMapTarget_, animMapTPos_);
        animmerge::bindGlobals(animMapDonor_, animMapDPos_);
        // Side or front for the target: whichever shows the rig wider (a
        // quadruped from the side, a biped from the front).
        animMapTargetView_ = rigExtent(animMapTPos_, animMapTarget_, 0) >=
                                     rigExtent(animMapTPos_, animMapTarget_, 2)
                                 ? 1
                                 : 0;
    }
    // Replay the stored pairs through the hygiene rule, so duplicates from
    // sessions before it existed collapse to the LATEST intent.
    animMapPairs_.clear();
    for (const auto& [from, to] : a.boneMap)
        upsertPair(animMapPairs_, from, to);
    animMapSelDonor_ = -1;
    animMapRow_ = importRow;
    // SetWindowFocus by name does nothing for a window that has not been
    // created yet (the first open), so it opened BEHIND the Animation Editor
    // that asked for it: focus it on its own next Begin instead.
    animMapFocusNext_ = true;
}

// Horizontal extent of a rig's bones along world axis `ax` (0 x, 2 z): which
// plane shows the rig wide rather than end-on.
static float rigExtent(const std::vector<float>& pos, const glbparser::Skel& sk, int ax) {
    float lo = 0.0f, hi = 0.0f;
    bool first = true;
    for (const glbparser::SkelJoint& j : sk.palette) {
        const int n = j.node;
        if (n < 0 || (size_t)(n * 3 + 2) >= pos.size()) continue;
        const float v = pos[(size_t)n * 3 + ax];
        if (first || v < lo) lo = v;
        if (first || v > hi) hi = v;
        first = false;
    }
    return hi - lo;
}

// A dashed line (the pair link that crosses from one rig's card to the other).
static void dashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thick, float dash) {
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (len < 1.0f) return;
    const ImVec2 d((b.x - a.x) / len, (b.y - a.y) / len);
    for (float t = 0.0f; t < len; t += dash * 2.0f) {
        const float e = std::min(len, t + dash);
        dl->AddLine(ImVec2(a.x + d.x * t, a.y + d.y * t), ImVec2(a.x + d.x * e, a.y + d.y * e), col, thick);
    }
}

bool App::drawAnimBoneMapWindow() {
    if (animMapRow_ < 0 || !hasProject_) return false;
    if (animMapRow_ >= (int)project_.animImports.size()) {
        animMapRow_ = -1;
        return false;
    }
    AnimImport& row = project_.animImports[(size_t)animMapRow_];

    bool openFlag = true;
    ImGui::SetNextWindowSize(ImVec2(scaled(1100), scaled(720)), ImGuiCond_FirstUseEver);
    if (animMapFocusNext_) {
        ImGui::SetNextWindowFocus();
        animMapFocusNext_ = false;
    }
    if (!ImGui::Begin("Map bones", &openFlag)) {
        ImGui::End();
        if (!openFlag) animMapRow_ = -1;
        return false;
    }
    const theme::Semantics& sem = theme::semantics();
    if (!animMapParsed_) {
        ui::EmptyState(ui::Glyph::Person, "A file would not parse",
                       "Both the source and this model must load as rigged .glb/.fbx files.");
        ImGui::End();
        if (!openFlag) animMapRow_ = -1;
        return false;
    }

    const glbparser::Skel& tgt = animMapTarget_;
    const glbparser::Skel& don = animMapDonor_;

    // Per-frame state derived from the staged pairs. Cheap (name lookups over
    // <100 bones), so recomputing beats caching + invalidation here. The
    // row's own switches ride along, so the test pose shows what the BAKE
    // will do with them (facing, mirror, lean).
    animmerge::MergeOptions opts;
    opts.boneMap = animMapPairs_;
    opts.facingOverride = row.facing;
    opts.mirror = row.mirror;
    opts.tuneLean = row.lean;
    opts.translation = row.translation == 2   ? animmerge::TranslationMode::CopyAll
                       : row.translation == 1 ? animmerge::TranslationMode::AnimatedOnly
                                              : animmerge::TranslationMode::RootBonesOnly;
    opts.ignoreScale = row.ignoreScale;
    opts.retargetRoot = row.retargetRoot;
    // Which donor nodes are bones, and where each resolves.
    std::set<int> donorBones, targetBones;
    for (const glbparser::SkelJoint& j : don.palette) donorBones.insert(j.node);
    for (const glbparser::SkelJoint& j : tgt.palette) targetBones.insert(j.node);
    std::map<int, int> resolved;  // donor node -> target node (-1 = none)
    std::map<int, bool> explicitPair;
    int mapped = 0;
    for (int n : donorBones) {
        if (n < 0 || n >= (int)don.nodes.size()) continue;
        const std::string& name = don.nodes[(size_t)n].name;
        resolved[n] = animmerge::resolveBoneName(tgt, name, opts);
        bool exp = false;
        for (const auto& [from, to] : animMapPairs_)
            if (from == name) exp = true;
        explicitPair[n] = exp;
        if (resolved[n] >= 0) ++mapped;
    }
    if (!animMapSuggValid_ || animMapSuggFor_ != animMapPairs_) {
        animMapSugg_ = animmerge::suggestBoneMap(tgt, don, opts, &boneAliases_);
        animMapAffixOk_ = animmerge::detectAffixRule(tgt, don, opts, animMapAffix_);
        // AI proposals for donors that got resolved meanwhile drop out.
        animMapAiSugg_.erase(std::remove_if(animMapAiSugg_.begin(), animMapAiSugg_.end(),
                                            [&](const auto& pr) {
                                                return animmerge::resolveBoneName(tgt, pr.first, opts) >= 0;
                                            }),
                             animMapAiSugg_.end());
        animMapSuggFor_ = animMapPairs_;
        animMapSuggValid_ = true;
    }
    std::map<int, int> suggested;  // donor node -> target node
    for (const animmerge::BoneSuggestion& sg : animMapSugg_)
        for (int n : donorBones)
            if (don.nodes[(size_t)n].name == sg.donor)
                for (size_t t = 0; t < tgt.nodes.size(); ++t)
                    if (tgt.nodes[t].name == sg.target) suggested[n] = (int)t;
    int nSugg = 0, nUnmatched = 0;
    for (int n : donorBones) {
        if (!resolved.count(n) || resolved[n] >= 0) continue;
        if (suggested.count(n)) ++nSugg;
        else ++nUnmatched;
    }
    const int nDonor = (int)donorBones.size();
    const ImU32 cGreen = ImGui::GetColorU32(sem.ok);
    const ImU32 cAmber = ImGui::GetColorU32(sem.warn);
    const ImU32 cRed = ImGui::GetColorU32(sem.danger);

    // --- header: what goes onto what, and how ---------------------------------
    const std::string donorFile = std::filesystem::path(row.source).filename().string();
    const std::string targetFile = std::filesystem::path(row.model).filename().string();
    {
        ui::FontScope bold(ui::boldFont());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s \xE2\x86\x92 %s", donorFile.c_str(), targetFile.c_str());
    }
    {
        const animmerge::RetargetInfo ri = animmerge::retargetInfo(tgt, don, opts);
        char mode[96];
        if (ri.full)
            std::snprintf(mode, sizeof mode, "full retarget \xC2\xB7 %.0f deg bind gap", ri.bindGapDeg);
        else
            std::snprintf(mode, sizeof mode, "copy path \xC2\xB7 not a full retarget");
        ImGui::SameLine();
        ui::Badge(mode, ri.full ? 3 : 4);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(ri.full ? "Bind poses differ - clips resample through the\n"
                                        "retargeter; the test pose shows exactly that."
                                      : "Identical binds - channels copy verbatim.");
    }

    // --- progress: matched / suggested / unmatched ------------------------------
    {
        char lead[64];
        std::snprintf(lead, sizeof lead, "%d / %d source joints mapped", mapped, nDonor);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(lead);
        ImGui::SameLine();
        const char* legend[3] = {"matched", "suggested", "unmatched"};
        const int counts[3] = {mapped, nSugg, nUnmatched};
        const ImU32 cols[3] = {cGreen, cAmber, cRed};
        float legW = 0.0f;
        char lbuf[3][32];
        for (int k = 0; k < 3; ++k) {
            std::snprintf(lbuf[k], sizeof lbuf[k], "%s %d", legend[k], counts[k]);
            legW += ImGui::CalcTextSize(lbuf[k]).x + ImGui::GetFontSize() * 1.6f;
        }
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float barW = std::max(scaled(60.0f), ImGui::GetContentRegionAvail().x - legW - scaled(16.0f));
        const float bh = scaled(6.0f);
        const float by = p.y + ImGui::GetFrameHeight() * 0.5f - bh * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, by), ImVec2(p.x + barW, by + bh), ImGui::GetColorU32(sem.field), bh);
        float x = p.x;
        for (int k = 0; k < 3; ++k) {
            const float w = nDonor > 0 ? barW * counts[k] / (float)nDonor : 0.0f;
            if (w > 0.0f) dl->AddRectFilled(ImVec2(x, by), ImVec2(x + w, by + bh), cols[k], k == 0 && x == p.x ? bh : 0.0f);
            x += w;
        }
        ImGui::Dummy(ImVec2(barW, ImGui::GetFrameHeight()));
        for (int k = 0; k < 3; ++k) {
            ImGui::SameLine(0.0f, scaled(14.0f));
            const ImVec2 q = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(q.x + ImGui::GetFontSize() * 0.3f, q.y + ImGui::GetFrameHeight() * 0.5f),
                                ImGui::GetFontSize() * 0.25f, cols[k]);
            ImGui::SetCursorScreenPos(ImVec2(q.x + ImGui::GetFontSize() * 0.75f, q.y));
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", lbuf[k]);
        }
    }
    ImGui::Separator();

    // The list's hover from LAST frame - the canvas draws before the list is
    // laid out, so it highlights with one frame of lag.
    const int hiD = animMapHiD_, hiT = animMapHiT_;
    animMapHiD_ = animMapHiT_ = -1;

    const float footerH = ImGui::GetFrameHeightWithSpacing() + scaled(12);
    const float listW = scaled(330);
    const float bodyH = ImGui::GetContentRegionAvail().y - footerH;

    // The test pose (clip + time) drives both rigs; computed before drawing.
    const bool poseOn = animMapPoseClip_ >= 0 && animMapPoseClip_ < (int)don.clips.size();
    if (poseOn) {
        const float dur = don.clips[(size_t)animMapPoseClip_].duration;
        if (animMapPosePlay_ && dur > 0.01f) {
            animMapPoseT_ += ImGui::GetIO().DeltaTime;
            while (animMapPoseT_ > dur) animMapPoseT_ -= dur;
        }
        animmerge::posedPreview(tgt, don, opts, animMapPoseClip_, animMapPoseT_, animMapDPosed_, animMapTPosed_);
    }

    // --- the canvas: two cards, each framed to its own rig ------------------
    ImGui::BeginChild("##bonecanvas", ImVec2(ImGui::GetContentRegionAvail().x - listW, bodyH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cMin = ImGui::GetCursorScreenPos();
    const ImVec2 cSize = ImGui::GetContentRegionAvail();
    // The child must claim its input region or clicks fall through to
    // whatever is under the canvas.
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("Bone canvas##bonehit", ImVec2(cSize.x > 1 ? cSize.x : 1, cSize.y > 1 ? cSize.y : 1),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHover = ImGui::IsItemHovered();
    // The canvas owns the wheel while hovered - without this the same wheel
    // that zooms the skeletons also scrolled the window around them.
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    const float gap = scaled(8.0f);
    const float srcW = std::clamp(cSize.x * 0.3f, scaled(150.0f), scaled(300.0f));
    const ImVec2 sMin = cMin, sMax(cMin.x + srcW, cMin.y + cSize.y);
    const ImVec2 tMin(cMin.x + srcW + gap, cMin.y), tMax(cMin.x + cSize.x, cMin.y + cSize.y);
    const float rr = ImGui::GetStyle().FrameRounding + 4.0f;
    for (const auto& [a, b] : {std::pair<ImVec2, ImVec2>{sMin, sMax}, std::pair<ImVec2, ImVec2>{tMin, tMax}}) {
        dl->AddRectFilled(a, b, ImGui::GetColorU32(sem.ground), rr);
        dl->AddRect(a, b, ImGui::GetColorU32(sem.border), rr);
    }
    const ImVec2 tCenter((tMin.x + tMax.x) * 0.5f, (tMin.y + tMax.y) * 0.5f);
    const bool overTarget = canvasHover && mouse.x >= tMin.x;
    // View input on the target card: wheel zooms around the cursor,
    // middle-drag pans (the viewport's gestures). The source card always fits.
    if (overTarget) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const float oldZoom = animMapZoom_;
            animMapZoom_ = std::clamp(animMapZoom_ * std::pow(1.15f, wheel), 0.5f, 12.0f);
            const float k = animMapZoom_ / oldZoom;
            animMapPanX_ = mouse.x - tCenter.x - (mouse.x - tCenter.x - animMapPanX_) * k;
            animMapPanY_ = mouse.y - tCenter.y - (mouse.y - tCenter.y - animMapPanY_) * k;
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            animMapPanX_ += d.x;
            animMapPanY_ += d.y;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) {
            animMapZoom_ = 1.0f;
            animMapPanX_ = animMapPanY_ = 0.0f;
        }
    }
    auto tview = [&](ImVec2 pt) {
        return ImVec2(tCenter.x + (pt.x - tCenter.x) * animMapZoom_ + animMapPanX_,
                      tCenter.y + (pt.y - tCenter.y) * animMapZoom_ + animMapPanY_);
    };

    // Each rig in its own card, projected on its own plane: the source on
    // whichever of front/side shows it wider, the target as the toggle says.
    const int srcAx = rigExtent(animMapDPos_, don, 0) >= rigExtent(animMapDPos_, don, 2) ? 0 : 2;
    const int tgtAx = animMapTargetView_ == 1 ? 0 : 2;
    struct Fit {
        float minH = 0, maxH = 0, minY = 0, maxY = 0;
        ImVec2 org;
        float scale = 1.0f;
        int ax = 0;
    };
    const float headH = scaled(46.0f), footH = scaled(40.0f);
    auto fitOf = [&](const std::vector<float>& pos, const std::set<int>& bones, int ax, ImVec2 a, ImVec2 b) {
        Fit f;
        f.ax = ax;
        bool first = true;
        for (int n : bones) {
            if (n < 0 || (size_t)(n * 3 + 2) >= pos.size()) continue;
            const float h = pos[(size_t)n * 3 + ax] * (ax == 2 ? -1.0f : 1.0f), y = pos[(size_t)n * 3 + 1];
            if (first || h < f.minH) f.minH = h;
            if (first || h > f.maxH) f.maxH = h;
            if (first || y < f.minY) f.minY = y;
            if (first || y > f.maxY) f.maxY = y;
            first = false;
        }
        const float w = f.maxH - f.minH, hgt = f.maxY - f.minY;
        const float availW = (b.x - a.x) - scaled(56), availH = (b.y - a.y) - headH - footH;
        const float sx = w > 1e-6f ? availW / w : 1e6f;
        const float sy = hgt > 1e-6f ? availH / hgt : 1e6f;
        f.scale = std::min(sx, sy);
        if (f.scale > 1e5f) f.scale = 1.0f;
        f.org.x = a.x + ((b.x - a.x) - w * f.scale) * 0.5f - f.minH * f.scale;
        f.org.y = a.y + headH + (availH - hgt * f.scale) * 0.5f + f.maxY * f.scale;
        return f;
    };
    // Framing always comes from the BIND pose (stable), the drawn points from
    // the test pose when one is on - so the figures move inside a calm frame.
    const Fit dFit = fitOf(animMapDPos_, donorBones, srcAx, sMin, sMax);
    const Fit tFit = fitOf(animMapTPos_, targetBones, tgtAx, tMin, tMax);
    const std::vector<float>& dCur =
        poseOn && animMapDPosed_.size() == animMapDPos_.size() ? animMapDPosed_ : animMapDPos_;
    const std::vector<float>& tCur =
        poseOn && animMapTPosed_.size() == animMapTPos_.size() ? animMapTPosed_ : animMapTPos_;
    auto rawPlace = [&](const Fit& f, const std::vector<float>& pos, int n) {
        const float h = pos[(size_t)n * 3 + f.ax] * (f.ax == 2 ? -1.0f : 1.0f);
        return ImVec2(f.org.x + h * f.scale, f.org.y - pos[(size_t)n * 3 + 1] * f.scale);
    };
    auto dPlace = [&](int n) { return rawPlace(dFit, dCur, n); };
    auto tPlace = [&](int n) { return tview(rawPlace(tFit, tCur, n)); };

    // card captions
    {
        const float fs = ImGui::GetFontSize();
        auto caption = [&](ImVec2 a, const char* title, const std::string& sub) {
            ui::FontScope bold(ui::boldFont());
            dl->AddText(ImGui::GetFont(), fs * 0.8f, ImVec2(a.x + fs * 0.7f, a.y + fs * 0.6f),
                        ImGui::GetColorU32(sem.textDim), title);
            ui::FontScope mono(ui::monoFont());
            dl->AddText(ImGui::GetFont(), fs * 0.8f, ImVec2(a.x + fs * 0.7f, a.y + fs * 1.6f),
                        ImGui::GetColorU32(sem.textDim), sub.c_str());
        };
        std::string clipNames;
        for (size_t c = 0; c < don.clips.size() && c < 3; ++c)
            clipNames += (c ? ", " : "") + don.clips[c].name;
        caption(sMin, "SOURCE", donorFile + " \xC2\xB7 " + std::to_string(nDonor) + " joints" +
                                     (clipNames.empty() ? "" : " \xC2\xB7 " + clipNames));
        caption(tMin, "THIS MODEL", targetFile + " \xC2\xB7 " + std::to_string(targetBones.size()) + " joints");
    }

    // Bone lines: to the nearest BONE ancestor, so helper nodes between two
    // bones do not break the figure apart.
    auto boneParent = [](const glbparser::Skel& sk, const std::set<int>& bones, int n) {
        int p = sk.nodes[(size_t)n].parent;
        while (p >= 0 && p < (int)sk.nodes.size() && !bones.count(p))
            p = sk.nodes[(size_t)p].parent == p ? -1 : sk.nodes[(size_t)p].parent;
        return p >= 0 && p < (int)sk.nodes.size() ? p : -1;
    };
    const ImU32 boneCol = ImGui::GetColorU32(ImVec4(sem.textDim.x, sem.textDim.y, sem.textDim.z, 0.7f));
    dl->PushClipRect(sMin, sMax, true);
    for (int n : donorBones)
        if (int p = boneParent(don, donorBones, n); p >= 0) dl->AddLine(dPlace(n), dPlace(p), boneCol, 2.0f);
    dl->PopClipRect();
    dl->PushClipRect(tMin, tMax, true);
    for (int n : targetBones)
        if (int p = boneParent(tgt, targetBones, n); p >= 0) dl->AddLine(tPlace(n), tPlace(p), boneCol, 2.0f);
    dl->PopClipRect();

    // Hit test: donor joints first, then target joints.
    const float R = scaled(4.5f);
    int hoverDonor = -1, hoverTarget = -1;
    float bestD = R * 2.5f;
    if (canvasHover && mouse.x < sMax.x)
        for (int n : donorBones) {
            const ImVec2 pt = dPlace(n);
            const float d = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
            if (d < bestD) bestD = d, hoverDonor = n;
        }
    if (overTarget) {
        bestD = R * 2.5f;
        for (int n : targetBones) {
            const ImVec2 pt = tPlace(n);
            const float d = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
            if (d < bestD) bestD = d, hoverTarget = n;
        }
    }

    // The link that crosses the cards: the hovered (or selected, or list-
    // hovered) donor joint to its home - accent for a real mapping, amber
    // for a pending suggestion, dashed.
    auto linkFor = [&](int dn, int forcedT) {
        if (dn < 0 || !donorBones.count(dn)) return;
        int tn = forcedT >= 0 ? forcedT : (resolved.count(dn) ? resolved[dn] : -1);
        // A forced target (a list row) is a pending suggestion unless the
        // donor already resolves.
        bool sugg = forcedT >= 0 && (!resolved.count(dn) || resolved[dn] < 0);
        if (tn < 0 && suggested.count(dn)) tn = suggested[dn], sugg = true;
        const ImVec2 dp = dPlace(dn);
        dl->AddCircle(dp, R + scaled(3), sugg ? cAmber : ImGui::GetColorU32(sem.accent), 0, 2.0f);
        if (tn < 0 || !targetBones.count(tn)) return;
        const ImVec2 tp = tPlace(tn);
        const ImU32 col = sugg ? cAmber : ImGui::GetColorU32(sem.accent);
        dashedLine(dl, dp, tp, col, 1.6f, scaled(5.0f));
        dl->AddCircle(tp, R + scaled(4), col, 0, 2.0f);
    };
    // Target joints: used ones dim green, free ones grey; amber when it is
    // the suggestion for the selected donor joint.
    std::set<int> usedTargets;
    for (auto& [dn, tn] : resolved)
        if (tn >= 0) usedTargets.insert(tn);
    dl->PushClipRect(tMin, tMax, true);
    for (int n : targetBones) {
        const ImVec2 pt = tPlace(n);
        ImU32 col = usedTargets.count(n) ? ImGui::GetColorU32(ImVec4(sem.ok.x, sem.ok.y, sem.ok.z, 0.6f))
                                         : ImGui::GetColorU32(sem.textDim);
        if (animMapSelDonor_ >= 0 && suggested.count(animMapSelDonor_) && suggested[animMapSelDonor_] == n)
            col = cAmber;
        dl->AddCircleFilled(pt, R, col);
        if (n == hoverTarget) dl->AddCircle(pt, R + scaled(2), ImGui::GetColorU32(sem.text));
    }
    dl->PopClipRect();
    // Donor joints: green matched, amber suggested, red unmatched; accent ring
    // = an explicit pair, white ring = selected. Names beside them (few).
    dl->PushClipRect(sMin, sMax, true);
    for (int n : donorBones) {
        const ImVec2 pt = dPlace(n);
        ImU32 col = cRed;
        if (resolved[n] >= 0) col = cGreen;
        else if (suggested.count(n)) col = cAmber;
        dl->AddCircleFilled(pt, R, col);
        if (explicitPair[n]) dl->AddCircle(pt, R + scaled(2), ImGui::GetColorU32(sem.accent), 0, 2.0f);
        if (n == animMapSelDonor_) dl->AddCircle(pt, R + scaled(5), ImGui::GetColorU32(sem.text), 0, 2.0f);
        if (n == hoverDonor) dl->AddCircle(pt, R + scaled(2), ImGui::GetColorU32(sem.text));
        if (nDonor <= 24)
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.78f, ImVec2(pt.x + R * 2.2f, pt.y - R * 1.6f),
                        ImGui::GetColorU32(sem.textDim), don.nodes[(size_t)n].name.c_str());
    }
    dl->PopClipRect();
    if (hoverDonor >= 0) linkFor(hoverDonor, -1);
    else if (hiD >= 0) linkFor(hiD, hiT);
    else if (animMapSelDonor_ >= 0) linkFor(animMapSelDonor_, -1);
    if (hoverTarget >= 0) {
        // Point back at whatever lands on this target joint.
        for (auto& [dn, tn] : resolved)
            if (tn == hoverTarget) linkFor(dn, -1);
        for (auto& [dn, tn] : suggested)
            if (tn == hoverTarget && resolved[dn] < 0) linkFor(dn, -1);
    }

    // Hover name + click actions.
    if (hoverDonor >= 0) {
        ImGui::SetTooltip("%s", don.nodes[(size_t)hoverDonor].name.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) animMapSelDonor_ = hoverDonor;
        // Right-click drops the hand-made pair for that joint.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const std::string& name = don.nodes[(size_t)hoverDonor].name;
            for (size_t k = 0; k < animMapPairs_.size(); ++k)
                if (animMapPairs_[k].first == name) {
                    animMapPairs_.erase(animMapPairs_.begin() + (int)k);
                    break;
                }
        }
    } else if (hoverTarget >= 0) {
        ImGui::SetTooltip("%s", tgt.nodes[(size_t)hoverTarget].name.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && animMapSelDonor_ >= 0) {
            upsertPair(animMapPairs_, don.nodes[(size_t)animMapSelDonor_].name, tgt.nodes[(size_t)hoverTarget].name);
            animMapSelDonor_ = -1;
        }
    }

    // Target view toggle (top-right of its card) and the gesture hint.
    {
        const float m = scaled(10.0f);
        const OverlayPillItem pill[] = {
            {"Side##bm", "Side", animMapTargetView_ == 0},
            {"Front##bm", "Front", animMapTargetView_ == 1},
            {nullptr, nullptr, false},
            {"Frame##bm", "Frame", false},
        };
        float w = ui::OverlayPad() * 2.0f + ui::OverlayTextWidth("Side") + ui::OverlayTextWidth("Front") +
                  ui::OverlayTextWidth("Frame") + ui::OverlayPad() * 2.0f + 1.0f;
        switch (overlayPill(ImVec2(tMax.x - m - w, tMin.y + m), pill, 4)) {
            case 0: animMapTargetView_ = 0; break;
            case 1: animMapTargetView_ = 1; break;
            case 3:
                animMapZoom_ = 1.0f;
                animMapPanX_ = animMapPanY_ = 0.0f;
                break;
            default: break;
        }
        const char* hint = "Click a red or amber joint, then the joint it drives \xC2\xB7 right-click removes \xC2\xB7 wheel zooms";
        const ImVec2 ts = ImGui::CalcTextSize(hint);
        const float pad = scaled(8.0f);
        const ImVec2 a(cMin.x + m, cMin.y + cSize.y - m - ts.y - pad);
        if (ts.x + pad * 2.0f < cSize.x - m * 2.0f) {
            ui::OverlayPanel(dl, a, ImVec2(a.x + ts.x + pad * 2.0f, a.y + ts.y + pad));
            dl->AddText(ImVec2(a.x + pad, a.y + pad * 0.5f), ImGui::GetColorU32(sem.textDim), hint);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- right column: suggestions, pairs, unmatched; the test pose pinned ---
    ImGui::BeginChild("##bonelist", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        auto nodeByName = [](const glbparser::Skel& sk, const std::string& nm) {
            for (size_t k = 0; k < sk.nodes.size(); ++k)
                if (sk.nodes[k].name == nm) return (int)k;
            return -1;
        };
        auto hoverRow = [&](int dNode, int tNode) {
            if (!ImGui::IsItemHovered()) return;
            animMapHiD_ = dNode;
            animMapHiT_ = tNode;
        };
        const float poseH = ImGui::GetFrameHeightWithSpacing() * 2.0f + scaled(30.0f);
        ImGui::BeginChild("##bonelistscroll", ImVec2(0, -poseH));
        const float fs = ImGui::GetFontSize();
        // One pair row: dot, donor > target, score, an action button at the
        // right. Returns 1 when the row was clicked, 2 when its button was.
        auto pairRow = [&](int id, ImU32 dot, const std::string& from, const std::string& to, const char* score,
                           const char* btn, const char* btnTip) {
            ImGui::PushID(id);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
            const float bw = btn ? h : 0.0f;
            int res = 0;
            ImGui::SetNextItemAllowOverlap();
            if (ImGui::InvisibleButton((from + "##row").c_str(), ImVec2(w, h))) res = 1;
            const bool hov = ImGui::IsItemHovered();
            hoverRow(nodeByName(don, from), to.empty() ? -1 : nodeByName(tgt, to));
            ImDrawList* ldl = ImGui::GetWindowDrawList();
            if (hov)
                ldl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                                   ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.9f)),
                                   ImGui::GetStyle().FrameRounding);
            ldl->AddCircleFilled(ImVec2(p.x + fs * 0.5f, p.y + h * 0.5f), fs * 0.24f, dot);
            float x = p.x + fs * 1.1f;
            {
                ui::FontScope mono(ui::monoFont());
                ldl->AddText(ImVec2(x, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(sem.text), from.c_str());
                x += std::max(fs * 5.5f, ImGui::CalcTextSize(from.c_str()).x + fs * 0.6f);
                if (!to.empty()) {
                    ldl->AddText(ImVec2(x, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(sem.textDim), "\xE2\x86\x92");
                    ldl->AddText(ImVec2(x + fs * 0.9f, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(sem.text),
                                 to.c_str());
                } else {
                    ldl->AddText(ImVec2(x, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(sem.textDim),
                                 "click a joint on the right");
                }
                if (score) {
                    const float sw = ImGui::CalcTextSize(score).x;
                    ldl->AddText(ImVec2(p.x + w - bw - sw - fs * 0.6f, p.y + (h - fs) * 0.5f),
                                 ImGui::GetColorU32(sem.textDim), score);
                }
            }
            if (btn) {
                ImGui::SetCursorScreenPos(ImVec2(p.x + w - bw, p.y));
                if (ImGui::Button(btn, ImVec2(bw, h))) res = 2;
                // The button's glyph, drawn: a check to accept, a cross to remove.
                const ImVec2 b0 = ImGui::GetItemRectMin(), c(b0.x + bw * 0.5f, b0.y + h * 0.5f);
                const float s = h * 0.2f;
                const ImU32 gc = ImGui::GetColorU32(sem.text);
                if (std::strstr(btn, "rm")) {
                    ldl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), gc, 1.6f);
                    ldl->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), gc, 1.6f);
                } else {
                    ldl->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x - s * 0.3f, c.y + s * 0.7f), gc, 1.6f);
                    ldl->AddLine(ImVec2(c.x - s * 0.3f, c.y + s * 0.7f), ImVec2(c.x + s, c.y - s * 0.6f), gc, 1.6f);
                }
                if (btnTip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", btnTip);
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y * 0.5f));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
            return res;
        };

        // Suggestions: the amber guesses, each one click to confirm.
        if (!animMapSugg_.empty()) {
            char cnt[16];
            std::snprintf(cnt, sizeof cnt, "%d", (int)animMapSugg_.size());
            ui::Caption("Suggestions", cnt);
            char all[48];
            std::snprintf(all, sizeof all, "Accept all %d", (int)animMapSugg_.size());
            if (ImGui::Button(all, ImVec2(-FLT_MIN, 0)))
                for (const animmerge::BoneSuggestion& sg : animMapSugg_) upsertPair(animMapPairs_, sg.donor, sg.target);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn every amber guess into a real pair.");
            int accept = -1;
            for (size_t k = 0; k < animMapSugg_.size(); ++k) {
                const animmerge::BoneSuggestion& sg = animMapSugg_[k];
                char sc[16];
                std::snprintf(sc, sizeof sc, "%d%%", (int)(sg.score * 100.0f + 0.5f));
                const int r = pairRow(1000 + (int)k, cAmber, sg.donor, sg.target, sc, "##acc", "Accept");
                if (r == 2) accept = (int)k;
                if (r == 1) animMapSelDonor_ = nodeByName(don, sg.donor);
            }
            if (accept >= 0)
                upsertPair(animMapPairs_, animMapSugg_[(size_t)accept].donor, animMapSugg_[(size_t)accept].target);
            ImGui::Spacing();
        }
        if (animMapAffixOk_) {
            char lbl[96];
            snprintf(lbl, sizeof lbl, "Apply rule: %s (%d)", animMapAffix_.describe().c_str(), animMapAffix_.matches);
            if (ImGui::Button(lbl, ImVec2(-FLT_MIN, 0)))
                for (const auto& pr : animmerge::applyAffixRule(tgt, don, opts, animMapAffix_))
                    upsertPair(animMapPairs_, pr.first, pr.second);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("One rename rule covers these bones - the two rigs are\n"
                                  "identical modulo this prefix/suffix.");
            ImGui::Spacing();
        }
        // Unmatched and unsuggested: the ones only a human can place.
        std::vector<int> orphans;
        for (int nd : donorBones)
            if (resolved.count(nd) && resolved[nd] < 0 && !suggested.count(nd)) orphans.push_back(nd);
        if (!orphans.empty()) {
            ui::Caption("Unmatched", std::to_string(orphans.size()).c_str());
            for (int nd : orphans)
                if (pairRow(2000 + nd, cRed, don.nodes[(size_t)nd].name, "", nullptr, nullptr, nullptr) == 1)
                    animMapSelDonor_ = nd;
            ImGui::Spacing();
        }
        // AI: one more suggestion group, never applied on its own.
        if (!animMapAiSugg_.empty()) {
            ui::Caption("AI suggestions", std::to_string(animMapAiSugg_.size()).c_str());
            if (ImGui::Button("Accept all AI", ImVec2(-FLT_MIN, 0))) {
                for (const auto& pr : animMapAiSugg_) upsertPair(animMapPairs_, pr.first, pr.second);
                animMapAiSugg_.clear();
            }
            int acceptAi = -1;
            for (size_t k = 0; k < animMapAiSugg_.size(); ++k)
                if (pairRow(3000 + (int)k, ImGui::GetColorU32(sem.accent), animMapAiSugg_[k].first,
                            animMapAiSugg_[k].second, nullptr, "##aiacc", "Accept") == 2)
                    acceptAi = (int)k;
            if (acceptAi >= 0) {
                upsertPair(animMapPairs_, animMapAiSugg_[(size_t)acceptAi].first,
                           animMapAiSugg_[(size_t)acceptAi].second);
                animMapAiSugg_.erase(animMapAiSugg_.begin() + acceptAi);
            }
            ImGui::Spacing();
        }
        if (animMapAiGen_ && animMapAiGen_->busy()) {
            ui::ProgressInline(-1.0f, "Asking the AI...");
            if (ui::GhostButton("Cancel##ai")) animMapAiGen_->cancel();
        } else {
            if (ui::GhostButton("Ask AI for pairs")) {
                animMapAiErr_.clear();
                animMapAiGen_ = std::make_unique<aigen::Generator>();
                animMapAiGen_->start(globalAi_, animmerge::aiMapPrompt(tgt, don, opts),
                                     "Map the bones now. Reply with the JSON only.");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Send both skeletons (hierarchy + bind positions) to the\n"
                                  "AI backend from Edit > Preferences. Proposals land here\n"
                                  "for you to accept - nothing is applied on its own.");
        }
        if (animMapAiGen_ && !animMapAiGen_->busy()) {
            if (animMapAiGen_->state() == aigen::Generator::State::Success) {
                animMapAiSugg_ = animmerge::parseAiBoneMap(animMapAiGen_->reply(), tgt, don);
                animMapAiSugg_.erase(std::remove_if(animMapAiSugg_.begin(), animMapAiSugg_.end(),
                                                    [&](const auto& pr) {
                                                        return animmerge::resolveBoneName(tgt, pr.first, opts) >= 0;
                                                    }),
                                     animMapAiSugg_.end());
                if (animMapAiSugg_.empty()) animMapAiErr_ = "AI proposed nothing usable.";
            } else if (animMapAiGen_->state() == aigen::Generator::State::Failed) {
                animMapAiErr_ = animMapAiGen_->error();
            }
            animMapAiGen_.reset();
        }
        if (!animMapAiErr_.empty()) ui::StatusLine(1, animMapAiErr_.c_str());

        // Pairs you made (or accepted): what Apply writes.
        if (!animMapPairs_.empty()) {
            ImGui::Spacing();
            ui::Caption("Pairs", std::to_string(animMapPairs_.size()).c_str());
            int removePair = -1;
            for (size_t k = 0; k < animMapPairs_.size(); ++k)
                if (pairRow(4000 + (int)k, ImGui::GetColorU32(sem.accent), animMapPairs_[k].first,
                            animMapPairs_[k].second, nullptr, "##rm", "Remove this pair") == 2)
                    removePair = (int)k;
            if (removePair >= 0) animMapPairs_.erase(animMapPairs_.begin() + removePair);
            if (ui::GhostButton("Clear pairs")) animMapPairs_.clear();
        }
        if (animMapPairs_.empty() && animMapSugg_.empty() && orphans.empty())
            ui::StatusLine(0, "Every source joint matched by name.");
        ImGui::EndChild();

        // The test pose: both rigs posed through the CURRENT mapping.
        ImGui::Separator();
        ui::Caption("Test pose");
        const char* poseLbl = poseOn ? don.clips[(size_t)animMapPoseClip_].name.c_str() : "off";
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##posec", poseLbl)) {
            if (ImGui::Selectable("off", animMapPoseClip_ < 0)) animMapPoseClip_ = -1;
            for (size_t c = 0; c < don.clips.size(); ++c)
                if (ImGui::Selectable(don.clips[c].name.c_str(), (int)c == animMapPoseClip_)) {
                    animMapPoseClip_ = (int)c;
                    animMapPoseT_ = 0.0f;
                    animMapPosePlay_ = true;
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pose both rigs through the current mapping with a source clip.\n"
                              "A wrong pair shows as a folded limb long before any percentage does.");
        ImGui::BeginDisabled(!poseOn);
        if (ImGui::Button(animMapPosePlay_ && poseOn ? "Pause##pose" : "Play##pose", ImVec2(scaled(60), 0)))
            animMapPosePlay_ = !animMapPosePlay_;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const float dur = poseOn ? don.clips[(size_t)animMapPoseClip_].duration : 1.0f;
        if (ImGui::SliderFloat("##poset", &animMapPoseT_, 0.0f, dur > 0.01f ? dur : 0.01f, "%.2f s"))
            animMapPosePlay_ = false;
        ImGui::EndDisabled();
    }
    ImGui::EndChild();

    // --- footer -------------------------------------------------------------
    bool applied = false;
    const bool dirty = animMapPairs_ != row.boneMap;
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Pairs are saved on the import row and always beat name matching.");
    char apply[48];
    std::snprintf(apply, sizeof apply, "Apply %d pair%s##apply", (int)animMapPairs_.size(),
                  animMapPairs_.size() == 1 ? "" : "s");
    const float aw = ImGui::CalcTextSize(apply, ImGui::FindRenderedTextEnd(apply)).x +
                     ImGui::GetStyle().FramePadding.x * 2.6f;
    const float cw = ImGui::CalcTextSize("Close").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SameLine();
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - aw - cw - ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::Button("Close")) animMapRow_ = -1;
    ImGui::SameLine();
    if (ui::PrimaryButton(apply, aw, dirty)) {
        row.boneMap = animMapPairs_;
        // The book learns every accepted pair, so the same rename suggests
        // itself in the next file from the same pack.
        for (const auto& [from, to] : animMapPairs_) boneAliases_[animmerge::canonicalBoneKey(from)] = to;
        saveBoneAliases();
        applied = true;
    }
    if (!dirty && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Nothing to apply: the pairs here are the ones the import row\n"
                          "already has. Accept a suggestion or click two joints.");

    ImGui::End();
    if (!openFlag) animMapRow_ = -1;
    return applied;
}

// The inspector of one "Borrowed from other files" row (animEdImport_): the
// donor file's match, Map bones, and the retarget knobs. Returns true when the
// row changed (the caller re-derives the model's clips).
bool App::drawAnimImportSection(const std::string& modelRel) {
    if (animEdImport_ < 0 || animEdImport_ >= (int)project_.animImports.size() ||
        project_.animImports[(size_t)animEdImport_].model != modelRel) {
        animEdImport_ = -1;
        return false;
    }
    const theme::Semantics& sem = theme::semantics();
    bool changed = false;
    const int i = animEdImport_;
    AnimImport& a = project_.animImports[(size_t)i];
    ImGui::PushID(i);

    ui::Caption("Borrowed clips");
    {
        ui::FontScope bold(ui::boldFont());
        ImGui::TextUnformatted(std::filesystem::path(a.source).filename().string().c_str());
    }
    {
        ui::FontScope mono(ui::monoFont());
        ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
        ImGui::TextUnformatted(a.source.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    const AnimImportProbe& probe = animImportProbe(a.model, a.source, a.boneMap);
    if (!probe.ok) {
        ui::StatusLine(2, ("Unusable: " + probe.error).c_str());
    } else {
        const bool full = probe.bonesMapped == probe.bonesTotal;
        char line[96];
        std::snprintf(line, sizeof line, "%d / %d bones mapped \xC2\xB7 %d clip%s", probe.bonesMapped,
                      probe.bonesTotal, probe.clipCount, probe.clipCount == 1 ? "" : "s");
        ui::StatusLine(full ? 0 : 1, line);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Donor bones with a home on this model - by name, or\n"
                              "by the pairs you set in Map bones.");
        ImGui::SameLine();
        ui::Badge(probe.retarget.full ? "full retarget" : "copy", 4);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(probe.retarget.full
                                  ? "Bind poses differ %.0f deg (facing %.0f) - clips are\n"
                                    "resampled through the full retargeter."
                                  : "Identical binds - channels copy verbatim.",
                              probe.retarget.bindGapDeg, probe.retarget.facingDeg);
        ImGui::Spacing();
        // A partial match is the reason to open the mapper, so it leads.
        if (full ? ImGui::Button("Map bones...") : ui::PrimaryButton("Map bones..."))
            openAnimBoneMap(i);
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    {
        const float capW = std::clamp(ImGui::GetContentRegionAvail().x * 0.38f, scaled(90.0f),
                                      scaled(130.0f));
        ui::InspectorRows rows(capW, capW);
        // Prefix is STAGED and committed when the edit ends - a committed
        // change re-parses, re-merges and re-bakes the model, and doing that
        // per keystroke stalled the editor once per character.
        const bool editingThis = animImpEditRow_ == i;
        char prefix[64];
        snprintf(prefix, sizeof prefix, "%s", editingThis ? animImpPrefix_ : a.prefix.c_str());
        if (ui::InputText("Prefix", prefix, sizeof prefix)) {
            animImpEditRow_ = i;
            snprintf(animImpPrefix_, sizeof animImpPrefix_, "%s", prefix);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && animImpEditRow_ == i) {
            if (a.prefix != animImpPrefix_) {
                a.prefix = animImpPrefix_;
                changed = true;
            }
            animImpEditRow_ = -1;
        }
        ui::RowHelp("Prepended to the imported clip names.");
        const char* modes[] = {"Root bones only", "Only where animated", "Copy everything"};
        const int tSel = a.translation >= 0 && a.translation < 3 ? a.translation : 0;
        if (ui::BeginCombo("Translation", modes[tSel])) {
            for (int m = 0; m < 3; ++m)
                if (ImGui::Selectable(modes[m], m == a.translation) && m != a.translation) {
                    a.translation = m;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        ui::RowHelp("Which bones keep the source's positions.\n"
                    "Root only = keep this model's own proportions (default).");
        {
            const char* faces[] = {"Auto", "0", "90", "180", "270"};
            const int fi = a.facing < 0 ? 0 : a.facing < 45 ? 1 : a.facing < 135 ? 2 : a.facing < 225 ? 3 : 4;
            if (ui::BeginCombo("Facing", faces[fi])) {
                for (int m = 0; m < 5; ++m)
                    if (ImGui::Selectable(faces[m], m == fi) && m != fi) {
                        a.facing = m == 0 ? -1 : (m - 1) * 90;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ui::RowHelp("World yaw of the source rig, degrees. Auto reads both\n"
                        "rigs' facing from their feet (ankles -> toes).");
        }
        ui::DragFloat("Lean", &a.lean, 0.2f, -30.0f, 30.0f, "%.1f deg");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ui::RowHelp("Posture fine-tune: + leans the torso forward, - back.\n"
                    "For a retarget that walks well but stands a bit off.");
        if (ui::Switch("Retarget root motion (Scale hip travel to this rig's hip height)", &a.retargetRoot))
            changed = true;
        if (ui::Switch("Ignore scale (Drop the source's scale tracks - export noise)", &a.ignoreScale))
            changed = true;
        if (ui::Switch("Mirror (Import left<->right - walk_right out of walk_left)", &a.mirror))
            changed = true;
    }
    ImGui::Spacing();
    if (ui::DangerButton("Remove")) {
        project_.animImports.erase(project_.animImports.begin() + i);
        if (animMapRow_ == i) animMapRow_ = -1;
        animEdImport_ = -1;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Stop borrowing this file's clips. Objects playing them\n"
                          "fall back to their model's own clips.");
    ImGui::PopID();
    return changed;
}

namespace {
// Who plays a clip: one entry per (object, slot) that resolves to it.
struct ClipUse {
    int scene = 0, object = 0;
    std::string objName, role;  // role: "idle", "walk", "start clip", "Animation node"...
    bool player = false;
    bool byDefault = false;  // an empty slot falling back to the first clip
};
}  // namespace

void App::drawAnimEditorWindow() {
    if (!showAnimEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(1200), scaled(800)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Editor", &showAnimEditor_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();

    bool changed = false;
    // Belt and braces: `changed` is set by hand at each widget, so a newly
    // added one silently forgets it (that is how the Menu Editor came to leave
    // the save icon dark). Comparing the section's serialized form across the
    // whole body cannot be forgotten. Clip renames also retarget references in
    // the SCENES, which the undo snapshot covers on its own.
    // Two sections are owned by this window, so the guard covers both -
    // concatenated, the documented answer for a panel that spans more than one.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::AnimEdits) +
        project::sectionJson(project_, project::Section::AnimImports);
    // This window has early returns (an unusable model, a model with no clips
    // of its own), and the import inspector can edit - so the commit has to
    // be reachable from every exit, not only the bottom.
    auto commitIfEdited = [&] {
        if (changed ||
            project::sectionJson(project_, project::Section::AnimEdits) +
                    project::sectionJson(project_, project::Section::AnimImports) !=
                beforeSection)
            commitChange();
    };
    // listAnimatedModelFiles returns names relative to res/models; every other
    // API here (glbInfo, the viewport cache, AnimClipEdit::model) speaks
    // project-relative paths, which is also what SceneObject::modelPath holds.
    std::vector<std::string> models;
    for (const std::string& m : listAnimatedModelFiles()) models.push_back("res/models/" + m);
    if (models.empty()) {
        ui::EmptyState(ui::Glyph::Film, "No animated models yet",
                       "Import a .glb or .fbx with Project > Assets > Import model... - "
                       "its clips show up here to trim, rename and retime.");
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (animEdModel_.empty() || std::find(models.begin(), models.end(), animEdModel_) == models.end()) {
        animEdModel_ = models.front();
        animEdClip_.clear();
        animEdImport_ = -1;
    }
    const std::string modelFile = std::filesystem::path(animEdModel_).filename().string();

    // --- header: the model, the project fps, what this tool never does ------
    {
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(modelFile.c_str()).x + ImGui::GetFrameHeight() +
                                ImGui::GetStyle().FramePadding.x * 3.0f);
        if (ImGui::BeginCombo("##aemodel", modelFile.c_str())) {
            for (const std::string& m : models)
                if (ImGui::Selectable(m.c_str(), m == animEdModel_) && m != animEdModel_) {
                    animEdModel_ = m;
                    animEdClip_.clear();
                    animEdImport_ = -1;
                    animEdTime_ = 0.0f;
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", animEdModel_.c_str());
        ImGui::SameLine();
        const float projScaleH = animedit::projectTimeScale(project_.settings);
        char fps[48];
        std::snprintf(fps, sizeof fps, "%.0f \xE2\x86\x92 %.0f fps", project_.settings.animSourceFps,
                      project_.settings.animPlayFps);
        ui::Badge(fps, std::fabs(projScaleH - 1.0f) > 0.001f ? 3 : 4);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Source fps -> play fps (Project > Preferences > Graphics >\n"
                              "Animation fps). Multiplies every clip of every model; a\n"
                              "clip's own Time scale stacks on top (%.3fx now).",
                              projScaleH);
        // Background preview bakes in flight (the async viewport rebake): the
        // panel stays interactive - this is "still working", not a stall.
        if (viewport_.animBakesPending() > 0) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(sem.warn, "%c baking preview", "|/-\\"[(int)(ImGui::GetTime() * 8.0) & 3]);
        }
        const char* note = "Edits are non-destructive - the source file is never rewritten";
        const float nw = ImGui::CalcTextSize(note).x;
        ImGui::SameLine();
        const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        if (right - nw > ImGui::GetCursorPosX()) {
            ImGui::SetCursorPosX(right - nw);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", note);
        } else {
            ImGui::NewLine();
        }
    }
    ImGui::Separator();

    // The bone-mapping window is a satellite of this one and commits through
    // the same section guard. Its rows all belong to the current model, so the
    // targeted invalidation is correct for it too.
    if (drawAnimBoneMapWindow()) {
        invalidateAnimCaches(animEdModel_);
        changed = true;
    }

    const GlbInfo& info = glbInfo(animEdModel_);
    if (!info.ok) {
        ui::StatusLine(2, ("Unusable model: " + info.error).c_str());
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (info.clips.empty()) {
        ui::EmptyState(ui::Glyph::Film, "This model has no animation clips",
                       "Borrow clips from another rigged file below - a Mixamo move fits "
                       "a rigged T-pose.");
    }
    if (!info.clips.empty() &&
        (animEdClip_.empty() || std::find(info.clips.begin(), info.clips.end(), animEdClip_) == info.clips.end())) {
        animEdClip_ = info.clips.front();
        animEdTime_ = 0.0f;
        snprintf(animEdRename_, sizeof(animEdRename_), "%s",
                 animedit::effectiveName(project_, animEdModel_, animEdClip_).c_str());
    }

    // The model's own skeleton (cached parse): which clips are its own, how
    // many joints, and the key times the timeline ticks.
    std::string skErr;
    const glbparser::Skel* ownSkel = skelCache_.get(project_.filePath(animEdModel_), skErr);
    std::set<std::string> ownClips;
    int jointCount = 0;
    const glbparser::SkelClip* ownClip = nullptr;
    if (ownSkel) {
        jointCount = (int)ownSkel->palette.size();
        for (const glbparser::SkelClip& c : ownSkel->clips) {
            ownClips.insert(c.name);
            if (c.name == animEdClip_) ownClip = &c;
        }
    }
    const bool hasClip = !info.clips.empty() && !animEdClip_.empty();

    // --- who plays what (players' slots, start clips, Animation nodes) -------
    // An empty Idle/Walk/start slot means "the model's first clip" - counted as
    // that clip, which is what the game does.
    std::map<std::string, std::vector<ClipUse>> uses;  // keyed by EFFECTIVE name
    const std::string firstEff =
        info.clips.empty() ? std::string() : animedit::effectiveName(project_, animEdModel_, info.clips.front());
    for (size_t si = 0; si < project_.scenes.size(); ++si) {
        const SceneData& sc = project_.scenes[si];
        for (size_t oi = 0; oi < sc.objects.size(); ++oi) {
            const SceneObject& o = sc.objects[oi];
            if (o.modelPath != animEdModel_) continue;
            auto add = [&](const std::string& stored, bool firstIfEmpty, const char* role, bool player) {
                const std::string eff = stored.empty() ? (firstIfEmpty ? firstEff : std::string()) : stored;
                if (eff.empty()) return;
                ClipUse u;
                u.scene = (int)si;
                u.object = (int)oi;
                u.objName = o.name;
                u.role = role;
                u.player = player;
                u.byDefault = stored.empty();
                uses[eff].push_back(u);
            };
            if (o.type == PrimitiveType::Player) {
                add(o.playerIdleClip, true, "idle", true);
                add(o.playerWalkClip, true, "walk", true);
                add(o.playerRunClip, false, "run", true);
                add(o.playerSprintClip, false, "sprint", true);
                add(o.playerJumpClip, false, "jump", true);
                add(o.playerBackClip, false, "back", true);
                add(o.playerStrafeLeftClip, false, "strafe left", true);
                add(o.playerStrafeRightClip, false, "strafe right", true);
            } else {
                add(o.animClip, true, "start clip", false);
            }
            for (const FlowNode& n : o.flowGraph.nodes)
                if (n.type == "Animation" && !n.str.empty()) add(n.str, false, "Animation node", false);
        }
    }
    auto usageBadge = [&](const std::string& eff, std::string& text) -> int {
        auto it = uses.find(eff);
        if (it == uses.end() || it->second.empty()) {
            text = "not used";
            return 1;
        }
        std::set<int> who;
        bool players = true;
        for (const ClipUse& u : it->second) {
            who.insert(u.scene * 100000 + u.object);
            players &= u.player;
        }
        const int n = (int)who.size();
        text = it->second.front().role + " \xC2\xB7 " + std::to_string(n) +
               (players ? (n == 1 ? " player" : " players") : (n == 1 ? " object" : " objects"));
        return 3;
    };

    const AnimClipEdit* cur = hasClip ? animedit::findEdit(project_, animEdModel_, animEdClip_) : nullptr;
    // Snapshot the staged values before any widget can append/prune the vector
    // and invalidate `cur` during this frame.
    const float curTimeScale = cur ? cur->timeScale : 1.0f;
    const bool curInPlace = cur && cur->inPlace;
    const bool curLoop = cur ? cur->loop : true;
    const float projScale = animedit::projectTimeScale(project_.settings);
    const float srcDur = hasClip ? viewport_.animClipDuration(animEdModel_, std::string(), animEdClip_) : 0.0f;
    float trimA = 0.0f, trimB = srcDur;
    animedit::trimWindow(cur, srcDur, trimA, trimB);
    const float clipScale = projScale * ((cur && cur->timeScale > 0.001f) ? cur->timeScale : 1.0f);
    const float outDur = clipScale > 0.001f ? (trimB - trimA) / clipScale : 0.0f;
    const std::string curEff = hasClip ? animedit::effectiveName(project_, animEdModel_, animEdClip_) : "";

    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float listW = scaled(250.0f), inspW = scaled(320.0f);
    auto vline = [&]() {
        const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y), ImGui::GetColorU32(sem.border));
    };

    // --- left: the clips, then what is borrowed ----------------------------
    ImGui::BeginChild("##ae_clips", ImVec2(listW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ae_filter", "Filter clips", animEdFilter_, sizeof animEdFilter_);
    auto passesFilter = [&](const std::string& name) {
        if (!animEdFilter_[0]) return true;
        std::string a = name, b = animEdFilter_;  // case-insensitive substring
        for (char& ch : a) ch = (char)std::tolower((unsigned char)ch);
        for (char& ch : b) ch = (char)std::tolower((unsigned char)ch);
        return a.find(b) != std::string::npos;
    };
    // One clip row: game name, duration, the source name, the usage badge.
    auto clipRow = [&](const std::string& c) {
        const std::string eff = animedit::effectiveName(project_, animEdModel_, c);
        if (!passesFilter(eff) && !passesFilter(c)) return;
        const AnimClipEdit* e = animedit::findEdit(project_, animEdModel_, c);
        const bool sel = animEdImport_ < 0 && c == animEdClip_;
        const float fs = ImGui::GetFontSize();
        const float h = fs * 3.9f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        // The label is the effective name (+ "  *" when edited): the ID and
        // what scripts click.
        const std::string label = eff + (e && !e->isDefault() ? "  *" : "") + "##clip_" + c;
        if (ImGui::InvisibleButton(label.c_str(), ImVec2(w, h)) && (c != animEdClip_ || animEdImport_ >= 0)) {
            animEdClip_ = c;
            animEdImport_ = -1;
            animEdTime_ = 0.0f;
            snprintf(animEdRename_, sizeof(animEdRename_), "%s", eff.c_str());
        }
        uiscript::markLastItemChecked(sel);
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        if (ImGui::IsItemHovered() && e && !e->rename.empty())
            ImGui::SetTooltip("source clip: %s", c.c_str());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 q(p.x + w, p.y + h);
        const float rr = ImGui::GetStyle().FrameRounding + 2.0f;
        if (sel)
            dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.14f)), rr);
        else if (t > 0.0f)
            dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.8f * t)), rr);
        const float x0 = p.x + fs * 0.5f;
        // play mark on the selected clip
        if (sel)
            dl->AddTriangleFilled(ImVec2(x0, p.y + fs * 0.45f), ImVec2(x0, p.y + fs * 1.15f),
                                  ImVec2(x0 + fs * 0.55f, p.y + fs * 0.8f), ImGui::GetColorU32(sem.accent));
        const float tx = x0 + fs * 1.0f;
        char dur[24];
        const float d = viewport_.animClipDuration(animEdModel_, std::string(), c);
        std::snprintf(dur, sizeof dur, "%.2f s", d);
        float durW = 0.0f;
        {
            ui::FontScope mono(ui::monoFont());
            durW = ImGui::CalcTextSize(dur).x;
            dl->AddText(ImVec2(q.x - durW - fs * 0.5f, p.y + fs * 0.3f), ImGui::GetColorU32(sem.textDim), dur);
        }
        dl->PushClipRect(ImVec2(tx, p.y), ImVec2(q.x - durW - fs, q.y), true);
        {
            ui::FontScope bold(ui::boldFont());
            dl->AddText(ImVec2(tx, p.y + fs * 0.3f), ImGui::GetColorU32(sem.text), eff.c_str());
        }
        dl->PopClipRect();
        dl->PushClipRect(ImVec2(tx, p.y), ImVec2(q.x - fs * 0.4f, q.y), true);
        {
            ui::FontScope mono(ui::monoFont());
            dl->AddText(ImGui::GetFont(), fs * 0.85f, ImVec2(tx, p.y + fs * 1.45f), ImGui::GetColorU32(sem.textDim),
                        c.c_str());
        }
        dl->PopClipRect();
        std::string badge;
        const int kind = usageBadge(eff, badge);
        const ImVec2 save = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(tx, p.y + fs * 2.45f));
        ui::Badge(badge.c_str(), kind);
        ImGui::SetCursorScreenPos(save);
    };
    if (!info.clips.empty()) {
        const std::string capTxt = "In " + modelFile;
        int own = 0;
        for (const std::string& c : info.clips) own += ownClips.count(c) || !ownSkel ? 1 : 0;
        ui::Caption(capTxt.c_str(), std::to_string(own).c_str());
        for (const std::string& c : info.clips)
            if (ownClips.count(c) || !ownSkel) clipRow(c);
    }

    // Borrowed: one row per donor file, its clips under it.
    ImGui::Spacing();
    int importRows = 0;
    for (const AnimImport& a : project_.animImports) importRows += a.model == animEdModel_ ? 1 : 0;
    ui::Caption("Borrowed from other files", std::to_string(importRows).c_str());
    std::set<std::string> attributed;
    for (size_t i = 0; i < project_.animImports.size(); ++i) {
        const AnimImport& a = project_.animImports[i];
        if (a.model != animEdModel_) continue;
        ImGui::PushID((int)i);
        const AnimImportProbe& probe = animImportProbe(a.model, a.source, a.boneMap);
        char sub[48] = "";
        if (probe.ok) std::snprintf(sub, sizeof sub, "%d/%d bones", probe.bonesMapped, probe.bonesTotal);
        const std::string label = std::filesystem::path(a.source).filename().string();
        const bool partial = probe.ok && probe.bonesMapped < probe.bonesTotal;
        if (ui::ListRow(label.c_str(), probe.ok ? sub : "!", animEdImport_ == (int)i, ui::Glyph::Folder,
                        ImGui::GetColorU32(!probe.ok ? sem.danger : partial ? sem.warn : sem.textDim)))
            animEdImport_ = (int)i;
        uiscript::markLastItemChecked(animEdImport_ == (int)i);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nClick for its retarget settings and Map bones.", a.source.c_str());
        // Its clips: the donor's names with the row's prefix (the merge may
        // add a numeric suffix on a clash).
        std::string err;
        if (const glbparser::Skel* donor = skelCache_.get(project_.filePath(a.source), err)) {
            ImGui::Indent(ImGui::GetFontSize() * 0.8f);
            for (const glbparser::SkelClip& dc : donor->clips) {
                const std::string want = a.prefix + dc.name;
                for (const std::string& c : info.clips) {
                    if (ownClips.count(c) || attributed.count(c)) continue;
                    if (c == want || c.rfind(want, 0) == 0) {
                        attributed.insert(c);
                        clipRow(c);
                        break;
                    }
                }
            }
            ImGui::Unindent(ImGui::GetFontSize() * 0.8f);
        }
        ImGui::PopID();
    }
    if (ownSkel) {
        bool header = false;
        for (const std::string& c : info.clips)
            if (!ownClips.count(c) && !attributed.count(c)) {
                if (!header) ImGui::TextDisabled("other borrowed clips");
                header = true;
                clipRow(c);
            }
    }
    // Borrow: the project's other rigged files, or a new one from disk.
    std::vector<std::string> candidates;
    for (const std::string& m : models)
        if (m != animEdModel_) candidates.push_back(m);
    auto borrowPopup = [&]() {
        if (!ImGui::BeginPopup("##ae_borrow")) return;
        ImGui::TextDisabled("Rigged files in this project");
        for (const std::string& c : candidates) {
            const AnimImportProbe& probe = animImportProbe(animEdModel_, c, {});
            char sub[64];
            if (probe.ok)
                std::snprintf(sub, sizeof sub, "%d/%d bones \xC2\xB7 %d clip%s", probe.bonesMapped, probe.bonesTotal,
                              probe.clipCount, probe.clipCount == 1 ? "" : "s");
            else
                std::snprintf(sub, sizeof sub, "unusable");
            ImGui::BeginDisabled(!probe.ok);
            if (ImGui::Selectable((c + "##borrow").c_str())) {
                AnimImport a;
                a.model = animEdModel_;
                a.source = c;
                project_.animImports.push_back(std::move(a));
                animEdImport_ = (int)project_.animImports.size() - 1;
                // A poor name match is exactly what the mapper is for - open
                // it on the fresh row instead of leaving a red number.
                if (probe.bonesMapped < probe.bonesTotal) openAnimBoneMap(animEdImport_);
                invalidateAnimCaches(animEdModel_);
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%s", sub);
        }
        if (candidates.empty()) ImGui::TextDisabled("  none yet");
        ImGui::Separator();
        if (ImGui::Selectable("Import a file...")) {
            // The import is a dialog that finishes later (modelImportPlace adds
            // the row then); nothing is in res/ yet when this returns.
            const std::string file = modelImporting_ ? std::string() : pickModelFile();
            if (!file.empty()) {
                modelImportFor_ = ModelImportFor::Borrow;
                modelImportBorrowFor_ = animEdModel_;
                importModelFile(file, {});
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy a .glb/.fbx into res/models/ and borrow its clips.");
        ImGui::EndPopup();
    };
    if (importRows == 0) {
        if (ui::BeginCard("##ae_borrowcard", ImVec2(0, 0))) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            const std::string stem = std::filesystem::path(animEdModel_).stem().string();
            ImGui::TextWrapped("Put a Mixamo move or another rig's clips on the %s - bones match by name, "
                               "the %s keeps its proportions.",
                               stem.c_str(), stem.c_str());
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button("+ Borrow clips from a file", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("##ae_borrow");
            borrowPopup();
        }
        ui::EndCard();
    } else {
        if (ui::GhostButton("+ Borrow clips from a file")) ImGui::OpenPopup("##ae_borrow");
        borrowPopup();
    }
    ImGui::EndChild();
    vline();
    ImGui::SameLine(0.0f, 0.0f);

    // --- centre: the preview (hero) and the transport + timeline -----------
    const float midW = ImGui::GetContentRegionAvail().x - inspW;
    ImGui::BeginChild("##ae_right", ImVec2(midW > 1 ? midW : 1, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (hasClip) {
        const float transportH = ImGui::GetFrameHeight() * 2.0f + scaled(64.0f);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int pw = (int)std::max(scaled(240), avail.x);
        const int ph = (int)std::max(scaled(160), avail.y - transportH - ImGui::GetStyle().ItemSpacing.y);
        ImGuiIO& io = ImGui::GetIO();
        const double now = ImGui::GetTime();
        const float dt = animEdClock_ > 0.0 ? (float)(now - animEdClock_) : 0.0f;
        animEdClock_ = now;
        static const float kSpeeds[] = {0.5f, 1.0f, 2.0f};
        if (animEdPlaying_ && outDur > 0.0001f && animEdTimelineDrag_ == 0) {
            // The playhead runs in OUTPUT seconds - what the game will show -
            // so the timeline length is the retimed length and dragging Time
            // scale visibly speeds the preview up.
            const float next = animEdTime_ + dt * kSpeeds[std::clamp(animEdSpeed_, 0, 2)];
            if (animEdLoopPreview_) {
                animEdTime_ = std::fmod(next, outDur);
            } else if (next >= outDur) {
                animEdTime_ = outDur;
                animEdPlaying_ = false;
            } else {
                animEdTime_ = next;
            }
        }
        animEdTime_ = outDur > 0.0001f ? std::clamp(animEdTime_, 0.0f, outDur) : 0.0f;

        Viewport::AnimPreviewDesc d;
        d.modelRel = animEdModel_;
        d.clip = animEdClip_;
        d.trimStart = trimA;
        d.trimEnd = trimB;
        // Source seconds inside the trimmed window: the preview poses by
        // source time, the playhead counts output time.
        d.time = animEdTime_ * clipScale;
        d.angleDeg = animEdYaw_;
        d.pitchDeg = animEdPitch_;
        d.zoom = animEdZoom_;
        d.panX = animEdPanX_;
        d.panY = animEdPanY_;
        d.wireframe = animEdWireframe_;
        d.inPlace = curInPlace;
        d.light = previewLight(animEdLight_);
        d.skeleton = animEdBones_;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 omax(origin.x + pw, origin.y + ph);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rr = ImGui::GetStyle().FrameRounding + 4.0f;
        if (const uint32_t tex = viewport_.renderAnimPreview(pw, ph, d))
            dl->AddImageRounded((ImTextureID)(intptr_t)tex, origin, omax, ImVec2(0, 1), ImVec2(1, 0),
                                IM_COL32_WHITE, rr);
        dl->AddRect(origin, omax, ImGui::GetColorU32(sem.border), rr);
        // Skeleton overlay: bones as lines child -> parent, joints as dots,
        // drawn over the picture (an x-ray, like a DCC's bone display).
        if (animEdBones_) {
            const std::vector<Viewport::AnimPreviewBone>& bones = viewport_.animPreviewBones();
            const ImU32 lineCol = ImGui::GetColorU32(ImVec4(sem.special.x, sem.special.y, sem.special.z, 0.9f));
            const ImU32 rimCol = ImGui::GetColorU32(ImVec4(sem.ground.x, sem.ground.y, sem.ground.z, 0.8f));
            auto at = [&](const Viewport::AnimPreviewBone& b) {
                return ImVec2(origin.x + b.u * (float)pw, origin.y + b.v * (float)ph);
            };
            dl->PushClipRect(origin, omax, true);
            for (const Viewport::AnimPreviewBone& b : bones)
                if (b.visible && b.parent >= 0 && b.parent < (int)bones.size() &&
                    bones[(size_t)b.parent].visible) {
                    dl->AddLine(at(bones[(size_t)b.parent]), at(b), rimCol, scaled(3.5f));
                    dl->AddLine(at(bones[(size_t)b.parent]), at(b), lineCol, scaled(1.75f));
                }
            for (const Viewport::AnimPreviewBone& b : bones)
                if (b.visible) {
                    dl->AddCircleFilled(at(b), scaled(b.parent < 0 ? 4.0f : 2.75f), rimCol, 12);
                    dl->AddCircleFilled(at(b), scaled(b.parent < 0 ? 3.0f : 2.0f), lineCol, 12);
                }
            dl->PopClipRect();
        }
        // Image() is display-only and does not reliably become active on a
        // drag: a real input item over it gives every mouse button an owner.
        ImGui::SetCursorScreenPos(origin);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("Animation preview##ae_view", ImVec2((float)pw, (float)ph), ui::kPreviewButtons);
        const bool hovered = ImGui::IsItemHovered();
        const ui::PreviewNav nav = ui::PreviewNavigation();
        if (nav.wheel != 0.0f)
            animEdZoom_ = std::clamp(animEdZoom_ * std::pow(1.15f, nav.wheel), 0.1f, 8.0f);
        if (nav.orbiting()) {
            animEdYaw_ += nav.orbit.x * 0.4f;
            animEdPitch_ = std::clamp(animEdPitch_ - nav.orbit.y * 0.3f, -30.0f, 85.0f);
        }
        {
            // Keep the visual travel per pixel stable as zoom changes.
            const float s = 0.0065f / std::max(animEdZoom_, 0.1f);
            animEdPanX_ = std::clamp(animEdPanX_ - nav.pan.x * s, -5.0f, 5.0f);
            animEdPanY_ = std::clamp(animEdPanY_ + nav.pan.y * s, -5.0f, 5.0f);
        }
        if (nav.recentre) animEdPanX_ = animEdPanY_ = 0.0f;
        if (hovered) ImGui::SetItemTooltip("%s", ui::kPreviewNavHint);

        // Overlays: view pill top-left, reset top-right, identity bottom-left.
        const float m = scaled(12.0f);
        const char* lightTxt = animEdLight_.empty()  ? "Scene ambience"
                               : animEdLight_ == "*" ? "Neutral studio"
                                                     : animEdLight_.c_str();
        const OverlayPillItem pill[] = {
            {"Shaded", "Shaded", !animEdWireframe_},
            {"Wireframe", "Wireframe", animEdWireframe_},
            {"##aebonesdiv", nullptr, false},
            {"Bones", "Bones", animEdBones_},
        };
        ImVec2 pillMax;
        switch (overlayPill(ImVec2(origin.x + m, origin.y + m), pill, 4, &pillMax)) {
            case 0: animEdWireframe_ = false; break;
            case 1: animEdWireframe_ = true; break;
            case 3: animEdBones_ = !animEdBones_; break;
            default: break;
        }
        if (animEdBones_ && viewport_.animPreviewBones().empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("This model has no skeleton (rigid animation) - nothing to draw.");
        {
            const float pad = ui::OverlayPad();
            const float w = ui::OverlayMenuButtonWidth(lightTxt) + pad * 2.0f;
            const ImVec2 a(pillMax.x + scaled(6.0f), origin.y + m);
            ui::OverlayPanel(dl, a, ImVec2(a.x + w, pillMax.y));
            ImGui::SetCursorScreenPos(ImVec2(a.x + pad, a.y + pad));
            if (ui::OverlayMenuButton("Preview light##ae", lightTxt, false)) ImGui::OpenPopup("##ae_light");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Light this preview with the scene's ambience (what ships),\n"
                                  "a neutral studio, or any ambience preset.");
            if (ImGui::BeginPopup("##ae_light")) {
                std::string sel = animEdLight_;
                if (ImGui::Selectable("Scene ambience", sel.empty())) sel.clear();
                if (ImGui::Selectable("Neutral studio", sel == "*")) sel = "*";
                if (!project_.ambiencePresets.empty()) ImGui::Separator();
                for (const AmbiencePreset& ap : project_.ambiencePresets)
                    if (ImGui::Selectable(ap.name.c_str(), sel == ap.name)) sel = ap.name;
                if (sel != animEdLight_) {
                    animEdLight_ = sel;
                    saveGlobalConfig();
                }
                ImGui::EndPopup();
            }
        }
        {
            const float pad = ui::OverlayPad();
            const float w = ui::OverlayTextWidth("Reset view") + pad * 2.0f;
            const ImVec2 a(omax.x - m - w, origin.y + m);
            ui::OverlayPanel(dl, a, ImVec2(a.x + w, a.y + ui::OverlayRowHeight() + pad * 2.0f));
            ImGui::SetCursorScreenPos(ImVec2(a.x + pad, a.y + pad));
            if (ui::OverlayTextButton("Reset view", "Reset view", false)) {
                animEdYaw_ = 40.0f;
                animEdPitch_ = 15.0f;
                animEdZoom_ = 1.0f;
                animEdPanX_ = animEdPanY_ = 0.0f;
            }
        }
        {
            char idt[160];
            std::snprintf(idt, sizeof idt, "%s \xC2\xB7 %d joints \xC2\xB7 %d clips", curEff.c_str(), jointCount,
                          (int)info.clips.size());
            ui::FontScope mono(ui::monoFont());
            const ImVec2 ts = ImGui::CalcTextSize(idt);
            const float pad = scaled(8.0f);
            const ImVec2 a(origin.x + m, omax.y - m - ts.y - pad);
            ui::OverlayPanel(dl, a, ImVec2(a.x + ts.x + pad * 2.0f, a.y + ts.y + pad));
            dl->AddText(ImVec2(a.x + pad, a.y + pad * 0.5f), ImGui::GetColorU32(sem.textDim), idt);
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, omax.y + ImGui::GetStyle().ItemSpacing.y));

        // --- transport + timeline card --------------------------------------
        if (ui::BeginCard("##ae_transport", ImVec2(0, 0))) {
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            const float bh = ImGui::GetFrameHeight();
            const ImU32 ink = ImGui::GetColorU32(sem.text);
            auto iconButton = [&](const char* label, float size, bool on, int icon) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const bool pressed = ImGui::InvisibleButton(label, ImVec2(size, size));
                const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
                const ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
                if (icon == 1) {  // the big round play/pause
                    cdl->AddCircleFilled(c, size * 0.5f, ImGui::GetColorU32(sem.text), 32);
                } else if (on || t > 0.0f) {
                    cdl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
                                       ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z,
                                                                 on ? 0.22f : 0.12f * t)),
                                       ImGui::GetStyle().FrameRounding);
                }
                const float s = size * 0.22f;
                const ImU32 gl = icon == 1 ? ImGui::GetColorU32(sem.ground) : (on ? ImGui::GetColorU32(sem.accent) : ink);
                switch (icon) {
                    case 0:  // back to start: bar + triangle
                        cdl->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x - s, c.y + s), gl, 2.0f);
                        cdl->AddTriangleFilled(ImVec2(c.x + s, c.y - s), ImVec2(c.x + s, c.y + s),
                                               ImVec2(c.x - s * 0.6f, c.y), gl);
                        break;
                    case 1:
                        if (animEdPlaying_) {
                            cdl->AddRectFilled(ImVec2(c.x - s * 0.8f, c.y - s), ImVec2(c.x - s * 0.25f, c.y + s), gl);
                            cdl->AddRectFilled(ImVec2(c.x + s * 0.25f, c.y - s), ImVec2(c.x + s * 0.8f, c.y + s), gl);
                        } else {
                            cdl->AddTriangleFilled(ImVec2(c.x - s * 0.6f, c.y - s), ImVec2(c.x - s * 0.6f, c.y + s),
                                                   ImVec2(c.x + s, c.y), gl);
                        }
                        break;
                    default:  // loop: a ring with an arrow head
                        cdl->PathArcTo(c, s * 1.1f, 0.4f, 5.6f, 16);
                        cdl->PathStroke(gl, 0, 1.6f);
                        cdl->AddTriangleFilled(ImVec2(c.x + s * 1.1f, c.y - s * 0.1f),
                                               ImVec2(c.x + s * 1.6f, c.y - s * 0.7f),
                                               ImVec2(c.x + s * 0.5f, c.y - s * 0.7f), gl);
                        break;
                }
                return pressed;
            };
            const float bigS = bh * 1.35f;
            const float yMid = ImGui::GetCursorPosY() + (bigS - bh) * 0.5f;
            ImGui::SetCursorPosY(yMid);
            if (iconButton("Back to start", bh, false, 0)) animEdTime_ = 0.0f;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to start");
            ImGui::SameLine();
            ImGui::SetCursorPosY(yMid - (bigS - bh) * 0.5f);
            if (iconButton(animEdPlaying_ ? "Pause" : "Play", bigS, false, 1)) {
                if (!animEdPlaying_ && !animEdLoopPreview_ && animEdTime_ >= outDur - 1e-4f) animEdTime_ = 0.0f;
                animEdPlaying_ = !animEdPlaying_;
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(yMid);
            if (iconButton("Loop preview", bh, animEdLoopPreview_, 2)) animEdLoopPreview_ = !animEdLoopPreview_;
            uiscript::markLastItemChecked(animEdLoopPreview_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Loop the preview (the clip's own Loop by default is in the inspector).");
            ImGui::SameLine();
            ImGui::SetCursorPosY(yMid);
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::AlignTextToFramePadding();
                {
                    ui::FontScope bold(ui::boldFont());
                    ImGui::Text("%.3f", animEdTime_);
                }
                ImGui::SameLine(0.0f, 0.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled(" / %.3f s", outDur);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Seconds as the game plays it: authored %.3f s, trimmed and\n"
                                  "retimed (%.2fx) to %.3f s.",
                                  srcDur, clipScale, outDur);
            static const char* kSpeedLbl[] = {"0.5\xC3\x97", "1\xC3\x97", "2\xC3\x97"};
            float segW = ImGui::GetStyle().FramePadding.x * 6.0f + 6.0f;
            for (const char* l : kSpeedLbl) segW += ImGui::CalcTextSize(l).x;
            const char* hint = "Drag the green handles to trim";
            ImGui::SameLine();
            const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
            const float hintW = ImGui::CalcTextSize(hint).x + ImGui::GetStyle().ItemSpacing.x;
            if (right - segW - hintW > ImGui::GetCursorPosX()) {
                ImGui::SetCursorPosX(right - segW - hintW);
                ImGui::SetCursorPosY(yMid);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", hint);
                ImGui::SameLine();
            }
            ImGui::SetCursorPosX(right - segW);
            ImGui::SetCursorPosY(yMid);
            ui::Segmented("##ae_speed", kSpeedLbl, 3, &animEdSpeed_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Preview speed only - the clip's Time scale is what ships.");

            // The timeline in SOURCE seconds: trim handles, cut regions,
            // key ticks, the playhead.
            ImGui::Spacing();
            const float fs = ImGui::GetFontSize();
            const ImVec2 t0 = ImGui::GetCursorScreenPos();
            const float tw = ImGui::GetContentRegionAvail().x;
            const float rulerH = fs * 1.1f, barH = fs * 1.5f;
            const float th = rulerH + barH;
            const float pad = fs * 0.6f;  // room for the handles at the ends
            const float x0 = t0.x + pad, x1 = t0.x + tw - pad;
            const float span = srcDur > 1e-4f ? srcDur : 1.0f;
            auto xOf = [&](float s) { return x0 + (x1 - x0) * (s / span); };
            auto sOf = [&](float x) { return std::clamp((x - x0) / (x1 - x0) * span, 0.0f, span); };
            ImGui::InvisibleButton("Timeline##ae", ImVec2(tw, th));
            const bool tlHover = ImGui::IsItemHovered();
            const float by0 = t0.y + rulerH, by1 = by0 + barH;
            // ruler
            {
                float step = 0.25f;
                while ((x1 - x0) / (span / step) < fs * 3.5f) step *= 2.0f;
                ui::FontScope mono(ui::monoFont());
                for (float s = 0.0f; s <= span + 1e-4f; s += step) {
                    char lb[16];
                    std::snprintf(lb, sizeof lb, step < 1.0f ? "%.2f" : "%.1f", s);
                    const float x = xOf(s);
                    cdl->AddText(ImGui::GetFont(), fs * 0.8f, ImVec2(x - fs * 0.6f, t0.y),
                                 ImGui::GetColorU32(sem.textDim), lb);
                    cdl->AddLine(ImVec2(x, by0 - fs * 0.15f), ImVec2(x, by0), ImGui::GetColorU32(sem.border));
                }
            }
            cdl->AddRectFilled(ImVec2(x0, by0), ImVec2(x1, by1), ImGui::GetColorU32(sem.field), 3.0f);
            // the kept window, and hatched cuts outside it
            const float ka = xOf(trimA), kb = xOf(trimB);
            cdl->AddRectFilled(ImVec2(ka, by0), ImVec2(kb, by1),
                               ImGui::GetColorU32(ImVec4(sem.ok.x, sem.ok.y, sem.ok.z, 0.22f)));
            cdl->AddRect(ImVec2(ka, by0), ImVec2(kb, by1), ImGui::GetColorU32(ImVec4(sem.ok.x, sem.ok.y, sem.ok.z, 0.6f)));
            auto hatch = [&](float a, float b) {
                if (b - a < 1.0f) return;
                cdl->PushClipRect(ImVec2(a, by0), ImVec2(b, by1), true);
                for (float x = a - barH; x < b; x += fs * 0.5f)
                    cdl->AddLine(ImVec2(x, by1), ImVec2(x + barH, by0), ImGui::GetColorU32(sem.border), 1.0f);
                cdl->PopClipRect();
            };
            hatch(x0, ka);
            hatch(kb, x1);
            // key ticks (the model's own clips; borrowed ones are merged at bake)
            if (ownClip) {
                std::set<int> ticks;
                for (const glbparser::SkelChannel& ch : ownClip->channels)
                    for (float kt : ch.times) ticks.insert((int)std::lround(xOf(kt)));
                for (int x : ticks)
                    cdl->AddLine(ImVec2((float)x, by1 - barH * 0.3f), ImVec2((float)x, by1),
                                 ImGui::GetColorU32(ImVec4(sem.text.x, sem.text.y, sem.text.z, 0.35f)));
            }
            // handles
            const ImU32 hcol = ImGui::GetColorU32(sem.ok);
            for (float hx : {ka, kb})
                cdl->AddRectFilled(ImVec2(hx - fs * 0.22f, by0 - 2.0f), ImVec2(hx + fs * 0.22f, by1 + 2.0f), hcol, 3.0f);
            // playhead (source time)
            const float phS = trimA + animEdTime_ * clipScale;
            const float phx = xOf(phS);
            cdl->AddLine(ImVec2(phx, by0 - fs * 0.35f), ImVec2(phx, by1), ImGui::GetColorU32(sem.accent), 2.0f);
            cdl->AddNgonFilled(ImVec2(phx, by0 - fs * 0.35f), fs * 0.32f, ImGui::GetColorU32(sem.accent), 4);

            // input: grab the nearest handle, else scrub
            const ImVec2 mp = io.MousePos;
            if (ImGui::IsItemActivated() && srcDur > 1e-4f) {
                const float grab = fs * 0.6f;
                if (std::fabs(mp.x - ka) < grab && std::fabs(mp.x - ka) <= std::fabs(mp.x - kb)) animEdTimelineDrag_ = 1;
                else if (std::fabs(mp.x - kb) < grab) animEdTimelineDrag_ = 2;
                else animEdTimelineDrag_ = 3;
            }
            if (ImGui::IsItemActive() && srcDur > 1e-4f) {
                const float s = sOf(mp.x);
                if (animEdTimelineDrag_ == 1) {
                    animEditFor(animEdModel_, animEdClip_).trimStart = std::clamp(s, 0.0f, std::max(0.0f, trimB - 0.01f));
                } else if (animEdTimelineDrag_ == 2) {
                    const float b = std::clamp(s, trimA + 0.01f, srcDur);
                    // The stored 0 means "to the end", which keeps following a
                    // re-exported longer clip - only store a real cut.
                    animEditFor(animEdModel_, animEdClip_).trimEnd = (b >= srcDur - 1e-4f) ? 0.0f : b;
                } else if (animEdTimelineDrag_ == 3) {
                    animEdPlaying_ = false;
                    animEdTime_ = clipScale > 0.001f ? std::clamp((s - trimA) / clipScale, 0.0f, outDur) : 0.0f;
                }
            }
            if (ImGui::IsItemDeactivated()) {
                if (animEdTimelineDrag_ == 1 || animEdTimelineDrag_ == 2) {
                    pruneAnimEdits();
                    changed = true;
                }
                animEdTimelineDrag_ = 0;
            }
            if (tlHover && !ImGui::IsItemActive()) {
                const float grab = fs * 0.6f;
                if (std::fabs(mp.x - ka) < grab || std::fabs(mp.x - kb) < grab)
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
        }
        ui::EndCard();
    } else {
        ui::EmptyState(ui::Glyph::Film, "No clip to preview", "Borrow clips from a file on the left.");
    }
    ImGui::EndChild();
    vline();
    ImGui::SameLine(0.0f, 0.0f);

    // --- right: the inspector (the clip, or the selected borrowed file) ----
    ImGui::BeginChild("##ae_insp", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (animEdImport_ >= 0) {
        if (drawAnimImportSection(animEdModel_)) {
            invalidateAnimCaches(animEdModel_);
            changed = true;
        }
    } else if (hasClip) {
        const float footH = ImGui::GetFrameHeightWithSpacing() + scaled(14.0f);
        ImGui::BeginChild("##ae_inspbody", ImVec2(0, -footH));
        ui::Caption("Clip");
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("source: %s", animEdClip_.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        const float capW = std::clamp(ImGui::GetContentRegionAvail().x * 0.36f, scaled(90.0f), scaled(120.0f));
        auto helpUnder = [&](const char* text) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + capW);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.9f);
            ImGui::TextWrapped("%s", text);
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
        };
        {
            ui::InspectorRows rows(capW, capW);
            ui::InputText("Name in game", animEdRename_, sizeof(animEdRename_));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string want = animEdRename_;
                // Trim blanks: a name of spaces would be impossible to type
                // into a flow node's Clip field.
                while (!want.empty() && want.front() == ' ') want.erase(want.begin());
                while (!want.empty() && want.back() == ' ') want.pop_back();
                const std::string before = animedit::effectiveName(project_, animEdModel_, animEdClip_);
                // Empty (or back to the source name) means "no rename" - drop
                // the field rather than storing a redundant one.
                const std::string after = want.empty() ? animEdClip_ : want;
                bool clash = false;
                for (const std::string& c : info.clips)
                    if (c != animEdClip_ && animedit::effectiveName(project_, animEdModel_, c) == after) clash = true;
                if (clash) {
                    statusMessage_ = "A clip of this model is already called \"" + after + "\"";
                    snprintf(animEdRename_, sizeof(animEdRename_), "%s", before.c_str());
                } else if (after != before) {
                    AnimClipEdit& e = animEditFor(animEdModel_, animEdClip_);
                    e.rename = (after == animEdClip_) ? std::string() : after;
                    renameAnimClipRefs(animEdModel_, before, after);
                    pruneAnimEdits();
                    changed = true;
                    snprintf(animEdRename_, sizeof(animEdRename_), "%s", after.c_str());
                }
            }
            ui::RowHelp("Empty = the name authored in the file. Renaming retargets the clip\n"
                        "references of objects using this model, including Animation nodes in\n"
                        "their own graphs; a node that drives another object through an\n"
                        "object link keeps its text and may need updating by hand.");
            helpUnder("What flow graphs and scripts call it. Renames follow.");

            float timeScale = curTimeScale;
            if (ui::DragFloat("Time scale", &timeScale, 0.01f, 0.05f, 10.0f, "%.2fx")) {
                timeScale = std::clamp(timeScale, 0.05f, 10.0f);
                animEditFor(animEdModel_, animEdClip_).timeScale = timeScale;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                pruneAnimEdits();
                changed = true;
            }
            ui::RowHelp("Playback speed of THIS clip, on top of the project's animation fps.\n"
                        "2.00x plays it twice as fast (half as long). The object's own Speed\n"
                        "property and a flow node's Speed param multiply on top at runtime.");

            if (srcDur > 0.0001f) {
                // Mirrors of the timeline's green handles, in SOURCE seconds
                // (changing the speed never moves them).
                float a = trimA, b = trimB;
                if (ui::DragFloat("Trim start", &a, 0.005f, 0.0f, srcDur, "%.3f s")) {
                    a = std::clamp(a, 0.0f, std::max(0.0f, b - 0.01f));
                    animEditFor(animEdModel_, animEdClip_).trimStart = a;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    pruneAnimEdits();
                    changed = true;
                }
                if (ui::DragFloat("Trim end", &b, 0.005f, 0.0f, srcDur, "%.3f s")) {
                    b = std::clamp(b, a + 0.01f, srcDur);
                    animEditFor(animEdModel_, animEdClip_).trimEnd = (b >= srcDur - 1e-4f) ? 0.0f : b;
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    pruneAnimEdits();
                    changed = true;
                }
                ui::RowHelp("Cut the clip to a range of the SOURCE animation. The trimmed clip\n"
                            "is rebased to start at 0 with interpolated boundary poses, so it\n"
                            "starts and ends exactly where you cut.");
                char ships[96];
                std::snprintf(ships, sizeof ships, "authored %.3f s \xE2\x86\x92 ships as %.3f s", srcDur, outDur);
                helpUnder(ships);
            } else {
                helpUnder("static clip (no motion to retime)");
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        {
            ui::InspectorRows rows(capW, capW);
            bool inPlace = curInPlace;
            if (ui::Switch("In place (Removes root travel - for walk and run cycles)", &inPlace)) {
                animEditFor(animEdModel_, animEdClip_).inPlace = inPlace;
                pruneAnimEdits();
                changed = true;
            }
            ui::RowHelp("Pins the character to its position at the trimmed start while keeping\n"
                        "vertical bob and all limb animation - for a Player whose object\n"
                        "already moves the avatar. Rebuild before testing in the game.");
            bool loop = curLoop;
            if (ui::Switch("Loop by default (How a placed object starts playing it)", &loop)) {
                animEditFor(animEdModel_, animEdClip_).loop = loop;
                pruneAnimEdits();
                changed = true;
            }
            ui::RowHelp("Seeds the Loop switch of objects that pick this clip as their Start\n"
                        "clip. Objects already placed keep theirs; a flow node's own Loop\n"
                        "param always wins at runtime.");
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ui::Caption("Used by");
        {
            auto it = uses.find(curEff);
            if (it == uses.end() || it->second.empty()) {
                // The consequence, next to its cause: nothing plays this clip.
                std::vector<std::pair<int, int>> where;
                for (size_t si = 0; si < project_.scenes.size(); ++si)
                    for (size_t oi = 0; oi < project_.scenes[si].objects.size(); ++oi) {
                        const SceneObject& o = project_.scenes[si].objects[oi];
                        if (o.type == PrimitiveType::Player && o.modelPath == animEdModel_)
                            where.emplace_back((int)si, (int)oi);
                    }
                if (ui::BeginCard("##ae_unused", ImVec2(0, 0))) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(sem.warn, "No player or object plays %s.", curEff.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                    ImGui::TextWrapped(where.empty() ? "Pick it as an object's Start clip, a Player's locomotion "
                                                       "clip, or in an Animation node."
                                                     : "Unused clips still ship in the model's .tskl.");
                    ImGui::PopStyleColor();
                    ImGui::PopTextWrapPos();
                }
                ui::EndCard();
                if (!where.empty()) {
                    SceneObject& pl = project_.scenes[(size_t)where[0].first].objects[(size_t)where[0].second];
                    const std::string lbl = "Set as Walk on " + pl.name + "##ae_setwalk";
                    if (ImGui::Button(lbl.c_str())) {
                        pl.playerWalkClip = curEff;
                        commitChange();
                        statusMessage_ = pl.name + " now walks with " + curEff;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Writes this clip into %s's Walk slot (Properties > Player).\n"
                                          "Undo puts the old one back.",
                                          pl.name.c_str());
                }
            } else {
                for (size_t k = 0; k < it->second.size(); ++k) {
                    const ClipUse& u = it->second[k];
                    ImGui::PushID((int)k);
                    const bool here = u.scene == project_.activeScene;
                    std::string row = u.objName + "##use";
                    std::string sub = u.role + (u.byDefault ? " (first clip)" : "");
                    if (!here && u.scene >= 0 && u.scene < (int)project_.scenes.size())
                        sub += " \xC2\xB7 " + project_.scenes[(size_t)u.scene].name;
                    if (ui::ListRow(row.c_str(), sub.c_str(), false, u.player ? ui::Glyph::Person : ui::Glyph::Box) &&
                        here)
                        selectOnly(u.object);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(here ? "Select it in the scene." : "In another scene.");
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();

        // Footer: what the clip is made of, and the way back.
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, scaled(4.0f)));
        if (ownClip) {
            int keys = 0;
            for (const glbparser::SkelChannel& ch : ownClip->channels) keys = std::max(keys, (int)ch.times.size());
            ui::FontScope mono(ui::monoFont());
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d keys \xC2\xB7 %d channels", keys, (int)ownClip->channels.size());
            ImGui::SameLine();
        }
        const AnimClipEdit* finalEdit = animedit::findEdit(project_, animEdModel_, animEdClip_);
        const bool edited = finalEdit && !finalEdit->isDefault();
        const float rw = ImGui::CalcTextSize("Reset clip").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - rw);
        if (ui::GhostButton("Reset clip", rw, edited)) {
            const std::string before = animedit::effectiveName(project_, animEdModel_, animEdClip_);
            renameAnimClipRefs(animEdModel_, before, animEdClip_);
            auto& v = project_.animClipEdits;
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [&](const AnimClipEdit& e) {
                                       return e.model == animEdModel_ && e.clip == animEdClip_;
                                   }),
                    v.end());
            snprintf(animEdRename_, sizeof(animEdRename_), "%s", animEdClip_.c_str());
            animEdTime_ = 0.0f;
            changed = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(edited ? "Back to exactly what the file contains." : "Nothing edited on this clip.");
    }
    ImGui::EndChild();

    commitIfEdited();
    ImGui::End();
}

// Loading Screens (Tools > Loading Screens): what the player sees from
// power-on to gameplay. A First power-on strip on top (TyraX logo -> splashes
// -> the Game start screen -> the start scene) beside a Between scenes strip,
// then one of two tabs: Screens (screens + their back-to-front elements | the
// 512x448 preview, edited in place, with a load-progress scrubber that can
// simulate a load | the inspector) or Assignments (which load shows which
// screen, drawLoadingAssignments). Like the other preset collections these live outside undo -
// commitChange() pushes no undo step here, it marks the project dirty and
// advances the session serial (see app.hpp). Per-scene assignment (a
// SceneData field) does push one.
void App::drawLoadingScreenWindow() {
    if (!showLoadingEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(1080), scaled(720)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Loading Screens", &showLoadingEditor_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

    bool changed = false;
    auto& screens = project_.loadingScreens;
    auto& splashes = project_.splashScreens;
    if (selectedLoadingScreen_ >= (int)screens.size()) selectedLoadingScreen_ = -1;
    if (selectedLoadingScreen_ < 0 && !screens.empty())
        selectedLoadingScreen_ = project_.defaultLoadingScreen >= 0 &&
                                         project_.defaultLoadingScreen < (int)screens.size()
                                     ? project_.defaultLoadingScreen
                                     : 0;
    if (selectedSplash_ >= (int)splashes.size()) selectedSplash_ = -1;
    if (lsSelKind_ == 4 && selectedSplash_ < 0) lsSelKind_ = 3;

    // Belt and braces, as in the Animation Editor: a hand-set `changed` flag is
    // forgettable, a section comparison across the whole body is not. This
    // window owns TWO sections - the screens and the boot splashes - plus the
    // master switch (ProjectSettings) and per-scene assignment (scenes).
    const std::string beforeSection = project::sectionJson(project_, project::Section::LoadingScreens) +
                                      project::sectionJson(project_, project::Section::Splash);

    auto startScene = [&]() -> const SceneData* {
        if (project_.scenes.empty()) return nullptr;
        const int s = project_.startScene >= 0 && project_.startScene < (int)project_.scenes.size()
                          ? project_.startScene
                          : 0;
        return &project_.scenes[s];
    };
    if (project_.bootLoadingScreen >= (int)screens.size()) project_.bootLoadingScreen = -1;
    // What the first load at power-on shows (the runtime's LS_BOOT rule): the
    // Game start pick, else - with Show between scenes on - the start scene's
    // own screen. loadingScreenIndexFor codes (-1/-2 built-in, -3 none), plus
    // -4 = no loading screen at all.
    auto bootResolved = [&]() -> int {
        if (project_.bootLoadingScreen >= 0) return project_.bootLoadingScreen;
        if (!project_.settings.loadingScreen) return -4;
        const SceneData* st = startScene();
        if (st) return project::loadingScreenIndexFor(project_, *st);
        return project_.defaultLoadingScreen >= 0 && project_.defaultLoadingScreen < (int)screens.size()
                   ? project_.defaultLoadingScreen
                   : -1;
    };
    // The loads that show screen `i` (Used by): Game start, then the scenes -
    // "(default)" when a scene inherits it.
    auto usersOf = [&](int i) {
        std::vector<std::string> users;
        if (bootResolved() == i) users.push_back("Game start");
        if (!project_.settings.loadingScreen) return users;
        for (const SceneData& sc : project_.scenes) {
            if (project::loadingScreenIndexFor(project_, sc) != i) continue;
            users.push_back(sc.loadingScreen == screens[i].name ? sc.name : sc.name + " (default)");
        }
        return users;
    };
    // A new screen from a template (docs/loading-screens.md, "Templates"):
    // 0 blank, 1 minimal bar, 2 dots, 3 spinner, 4 animated progress, 5 press
    // START. Templates only pre-fill ordinary elements - nothing about the
    // screen remembers which one it came from.
    auto newScreen = [&](int kind = 0) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "loading-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& s : screens) taken |= (s.name == name);
            if (!taken) break;
        }
        LoadingScreenDef s;
        s.name = name;
        auto caption = [&](const char* text, float y) {
            HudText t;
            t.name = "caption";
            t.text = text;
            t.pos[1] = y;
            s.texts.push_back(t);
        };
        if (kind == 1 || kind == 5) {
            caption("LOADING", 0.62f);
            s.bars.push_back(LoadingBar{});
            s.bars.back().name = "bar";
        }
        if (kind == 2) {
            LoadingBar b;
            b.name = "dots";
            b.kind = 1;
            b.segments = 5;
            b.size[0] = 120.0f;
            b.size[1] = 12.0f;
            b.spacing = 12.0f;
            b.pos[1] = 0.8f;
            s.bars.push_back(b);
        }
        if (kind == 3) {
            LoadingAnim a;
            a.name = "spinner";
            a.pos[0] = 0.88f;
            a.pos[1] = 0.86f;
            a.size[0] = a.size[1] = 48.0f;
            for (const std::string& rel : writeSpinnerFrames(project_.dir, name + "-spinner")) {
                HudImage f;
                f.name = a.name;
                f.imagePath = rel;
                f.texQuant = "4bit";
                a.frames.push_back(f);
            }
            s.anims.push_back(a);
        }
        if (kind == 4) {
            LoadingAnim a;
            a.name = "progress";
            a.mode = 1;
            a.size[0] = 256.0f;
            a.size[1] = 96.0f;
            a.pos[1] = 0.7f;
            s.anims.push_back(a);  // frames: Slice a sprite sheet... in its inspector
        }
        if (kind == 5) s.waitForButton = true;
        screens.push_back(std::move(s));
        selectedLoadingScreen_ = (int)screens.size() - 1;
        // A first screen that nothing shows would be a trap (the game keeps
        // the built-in loading.png): with no default yet, it becomes it.
        if (project_.defaultLoadingScreen < 0 || project_.defaultLoadingScreen >= (int)screens.size())
            project_.defaultLoadingScreen = selectedLoadingScreen_;
        lsSelKind_ = 3;
        lsSelIdx_ = -1;
        changed = true;
    };
    auto addSplash = [&]() {
        std::vector<HudImage> tmp;
        const int i = importHudImageInto(tmp);
        if (i < 0) return;
        SplashScreen s;
        s.name = tmp[i].name.empty() ? "splash" : tmp[i].name;
        s.image = tmp[i];
        s.image.pos[0] = 0.5f;
        s.image.pos[1] = 0.5f;
        s.image.size[0] = 512.0f;  // default fullscreen stretch
        s.image.size[1] = 448.0f;
        splashes.push_back(std::move(s));
        selectedSplash_ = (int)splashes.size() - 1;
        lsSelKind_ = 4;
        changed = true;
    };

    // --- header: the master switch ------------------------------------------
    {
        static const char* kTabs[] = {"Screens", "Assignments"};
        ui::Segmented("##ls_tabs", kTabs, 2, &lsTab_, fs * 13.0f);
        const float swW = ImGui::CalcTextSize("Show between scenes").x + IM_TRUNC(fs * 1.05f) * 1.8f +
                          ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SameLine(ImMax(ImGui::GetCursorPosX() + fs, ImGui::GetWindowContentRegionMax().x - swW));
        if (ui::Switch("Show between scenes", &project_.settings.loadingScreen)) changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Project Preferences: \"Loading screen between scenes\".\n"
                              "Off = scenes cut straight in. Splashes play either way.");
    }

    // --- the boot sequence strip --------------------------------------------
    {
        ImGui::Dummy(ImVec2(0.0f, fs * 0.2f));
        ImGui::BeginGroup();
        ui::Caption("First power-on");
        const float cardH = IM_TRUNC(fs * 3.4f);
        const float thumb = cardH - fs * 0.9f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ImGui::GetStyle().FrameRounding + 2.0f;
        auto arrow = [&]() {
            ImGui::SameLine(0.0f, 0.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float aw = fs * 1.4f, y = p.y + cardH * 0.5f;
            dl->AddLine(ImVec2(p.x + fs * 0.35f, y), ImVec2(p.x + aw - fs * 0.35f, y), tint(sem.textDim, 0.6f), 1.5f);
            dl->AddTriangleFilled(ImVec2(p.x + aw - fs * 0.3f, y), ImVec2(p.x + aw - fs * 0.6f, y - fs * 0.22f),
                                  ImVec2(p.x + aw - fs * 0.6f, y + fs * 0.22f), tint(sem.textDim, 0.6f));
            ImGui::Dummy(ImVec2(aw, cardH));
            ImGui::SameLine(0.0f, 0.0f);
        };
        // One card: a thumbnail (texture, or a fill drawn by `paintThumb`),
        // a title and a dim line. Returns the InvisibleButton's state.
        auto card = [&](const char* id, float w, const char* title, const char* sub, ImVec4 subCol, bool selected,
                        bool accent, const std::function<void(ImVec2, ImVec2)>& paintThumb) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, cardH));
            uiscript::markLastItemChecked(selected);
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 q(p.x + w, p.y + cardH);
            dl->AddRectFilled(p, q, accent || selected ? tint(sem.accent, 0.12f) : tint(sem.field), r);
            dl->AddRect(p, q, selected ? tint(sem.accent, 0.7f) : accent ? tint(sem.accent, 0.35f)
                                                                  : hov ? tint(sem.border, 2.0f) : tint(sem.border),
                        r);
            const ImVec2 t0(p.x + fs * 0.45f, p.y + (cardH - thumb) * 0.5f);
            const ImVec2 t1(t0.x + thumb * 1.14f, t0.y + thumb);
            paintThumb(t0, t1);
            const float tx = t1.x + fs * 0.6f;
            dl->PushClipRect(ImVec2(tx, p.y), ImVec2(q.x - fs * 0.3f, q.y), true);
            {
                ImFont* f = (accent && ui::boldFont()) ? ui::boldFont() : ImGui::GetFont();
                dl->AddText(f, fs, ImVec2(tx, p.y + cardH * 0.5f - fs * 1.05f), tint(sem.text), title);
            }
            dl->AddText(ImGui::GetFont(), IM_TRUNC(fs * 0.86f), ImVec2(tx, p.y + cardH * 0.5f + fs * 0.1f),
                        tint(subCol), sub);
            dl->PopClipRect();
            return clicked;
        };
        // The TyraX logo: the engine's banner, before anything the project
        // owns. A click switches it off (ProjectSettings::showEngineLogo) -
        // the card stays, dashed, so the strip still says what was skipped.
        {
            bool& logo = project_.settings.showEngineLogo;
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            if (card("TyraX logo", fs * 9.5f, "TyraX logo", logo ? "2.0 s \xC2\xB7 engine" : "skipped \xC2\xB7 0 s",
                     logo ? sem.textDim : sem.warn, false, false, [&](ImVec2 a, ImVec2 b) {
                         dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), 4.0f);
                         const float tfs = IM_TRUNC(fs * 0.6f);
                         const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(tfs, FLT_MAX, 0.0f, "TYRAX");
                         dl->AddText(ImGui::GetFont(), tfs,
                                     ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f),
                                     tint(sem.textDim, logo ? 1.0f : 0.4f), "TYRAX");
                         if (!logo) dl->AddLine(a, b, tint(sem.warn, 0.8f), 1.5f);
                     })) {
                logo = !logo;
                changed = true;
            }
            uiscript::markLastItemChecked(logo);
            if (!logo)
                dashedRect(dl, cp, ImVec2(cp.x + fs * 9.5f, cp.y + cardH), tint(sem.warn, 0.6f), fs * 0.3f);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(logo ? "The engine's logo, held ~2 s at power-on. Click to skip it."
                                       : "Skipped: the game opens on your splashes. Click to show the logo again.");
        }
        // Splashes, in play order; drag a card sideways to reorder.
        for (int i = 0; i < (int)splashes.size(); ++i) {
            arrow();
            SplashScreen& s = splashes[i];
            ImGui::PushID(5000 + i);
            char sub[48];
            std::snprintf(sub, sizeof(sub), "%.1f s \xC2\xB7 splash", s.duration);
            const HudTexture* t = hudTexture(s.image.imagePath);
            if (card(s.name.c_str(), fs * 9.5f, s.name.c_str(), sub, sem.textDim,
                     lsSelKind_ == 4 && selectedSplash_ == i, false, [&](ImVec2 a, ImVec2 b) {
                         dl->AddRectFilled(a, b, IM_COL32((int)(s.bgColor[0] * 255), (int)(s.bgColor[1] * 255),
                                                          (int)(s.bgColor[2] * 255), 255),
                                           4.0f);
                         if (t) dl->AddImage((ImTextureID)(intptr_t)t->tex, a, b);
                     })) {
                lsSelKind_ = 4;
                selectedSplash_ = i;
            }
            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                const int dst = i + (ImGui::GetMouseDragDelta(0).x < 0.0f ? -1 : 1);
                if (dst >= 0 && dst < (int)splashes.size()) {
                    std::swap(splashes[i], splashes[dst]);
                    if (selectedSplash_ == i) selectedSplash_ = dst;
                    else if (selectedSplash_ == dst) selectedSplash_ = i;
                    ImGui::ResetMouseDragDelta();
                    changed = true;
                    ImGui::PopID();
                    break;
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Drag sideways to reorder - list order is play order.");
            ImGui::PopID();
        }
        arrow();
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = fs * 7.5f;
            if (ImGui::InvisibleButton("+ Splash image", ImVec2(w, cardH))) addSplash();
            const bool hov = ImGui::IsItemHovered();
            dashedRect(dl, p, ImVec2(p.x + w, p.y + cardH), tint(hov ? sem.accent : sem.textDim, 0.6f), fs * 0.3f);
            const char* l = "+ Splash image";
            const ImVec2 ts = ImGui::CalcTextSize(l);
            dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (cardH - ts.y) * 0.5f), tint(sem.accent), l);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("A studio / publisher card shown after the Tyra logo (PNG).");
        }
        arrow();
        const SceneData* start = startScene();
        const int bootScreen = bootResolved();
        {
            std::string title, sub;
            ImVec4 subCol = sem.textDim;
            if (bootScreen == -4) {
                title = "No loading screen";
                sub = "Show between scenes is off";
                subCol = sem.warn;
            } else if (bootScreen == -3) {
                title = "None";
                sub = "black while " + (start ? start->name : std::string("the scene")) + " loads";
            } else if (bootScreen >= 0) {
                title = screens[bootScreen].name;
                sub = (project_.bootLoadingScreen >= 0 ? std::string("Game start") : std::string("via ") + (start ? start->name : std::string("the start scene")));
                if (screens[bootScreen].waitForButton) {
                    static const char* kBtn[] = {"a button", "START", "Cross"};
                    sub += std::string(" \xC2\xB7 waits for ") + kBtn[ImClamp(screens[bootScreen].waitButton, 0, 2)];
                } else {
                    sub += " \xC2\xB7 real progress";
                }
            } else {
                title = "Built-in";
                sub = "res/hud/loading.png on black";
            }
            if (card("Game start screen", fs * 15.0f, title.c_str(), sub.c_str(), subCol, false, bootScreen >= 0,
                     [&](ImVec2 a, ImVec2 b) {
                         if (bootScreen >= 0)
                             drawLoadingFrame(dl, screens[bootScreen], 0.65f, a, ImVec2(b.x - a.x, b.y - a.y),
                                              nullptr);
                         else
                             dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), 4.0f);
                     }))
                ImGui::OpenPopup("##ls_bootpick");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("The Game start screen: the first load at power-on. Click to pick it.");
            if (ImGui::BeginPopup("##ls_bootpick")) {
                ImGui::TextDisabled("Game start shows");
                const int stRes = start ? project::loadingScreenIndexFor(project_, *start) : -1;
                const std::string same =
                    "Same as start scene (" + (start ? start->name : std::string("-")) + " \xE2\x86\x92 " +
                    (!project_.settings.loadingScreen ? std::string("none")
                     : stRes >= 0                     ? screens[stRes].name
                     : stRes == -3                    ? std::string("None")
                                                      : std::string("Built-in")) +
                    ")";
                if (ImGui::Selectable(same.c_str(), project_.bootLoadingScreen < 0)) {
                    project_.bootLoadingScreen = -1;
                    changed = true;
                }
                ImGui::Separator();
                for (int i = 0; i < (int)screens.size(); ++i)
                    if (ImGui::Selectable(screens[i].name.c_str(), project_.bootLoadingScreen == i)) {
                        project_.bootLoadingScreen = i;
                        changed = true;
                    }
                if (bootScreen >= 0) {
                    ImGui::Separator();
                    if (ImGui::Selectable("Edit this screen")) {
                        selectedLoadingScreen_ = bootScreen;
                        lsSelKind_ = 3;
                        lsSelIdx_ = -1;
                        lsTab_ = 0;
                    }
                }
                ImGui::EndPopup();
            }
        }
        arrow();
        card("Start scene", fs * 9.0f, start ? start->name.c_str() : "-", "start scene", sem.ok, false, false,
             [&](ImVec2 a, ImVec2 b) { dl->AddRectFilled(a, b, tint(sem.surface), 4.0f); });
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Pick it in Scene > Preferences > Startup > \"Boot into this scene\".");
        ImGui::EndGroup();

        // Between scenes: every later load - each scene shows its own screen
        // (Assignments). Beside the power-on strip when the window is wide
        // enough, under it otherwise.
        {
            const float need = fs * (10.0f + 11.5f + 9.0f + 2.8f + 1.5f);
            const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
            if (right - ImGui::GetItemRectMax().x > need) ImGui::SameLine(0.0f, fs * 1.5f);
            ImGui::BeginGroup();
            ui::Caption("Between scenes");
            card("Switch Scene", fs * 10.0f, "Switch Scene", "flow node", sem.textDim, false, false,
                 [&](ImVec2 a, ImVec2 b) { dl->AddRectFilled(a, b, tint(sem.surface), 4.0f); });
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("A Switch Scene node (or a portal) starts the next load.");
            arrow();
            const bool on = project_.settings.loadingScreen;
            if (card("The scene's screen", fs * 11.5f, on ? "Its screen" : "No loading screen",
                     on ? "see Assignments" : "Show between scenes is off", on ? sem.accent : sem.warn,
                     lsTab_ == 1, on,
                     [&](ImVec2 a, ImVec2 b) { dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), 4.0f); }))
                lsTab_ = 1;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("The screen the TARGET scene picks. Click for Assignments.");
            arrow();
            card("Next scene", fs * 9.0f, "Next scene", "gameplay", sem.ok, false, false,
                 [&](ImVec2 a, ImVec2 b) { dl->AddRectFilled(a, b, tint(sem.surface), 4.0f); });
            ImGui::EndGroup();
        }
        ImGui::Dummy(ImVec2(0.0f, fs * 0.3f));
    }
    ImGui::Separator();

    if (lsTab_ == 1) {
        changed |= drawLoadingAssignments();
        if (changed || project::sectionJson(project_, project::Section::LoadingScreens) +
                               project::sectionJson(project_, project::Section::Splash) !=
                           beforeSection)
            commitChange();
        ImGui::End();
        return;
    }

    const float bodyH = ImGui::GetContentRegionAvail().y;
    const float totalW = ImGui::GetContentRegionAvail().x;
    const float leftW = ImMin(scaled(232), totalW * 0.27f);
    const float rightW = ImMin(scaled(300), totalW * 0.32f);
    const float midW = ImMax(scaled(120), totalW - leftW - rightW - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    LoadingScreenDef* lsp = selectedLoadingScreen_ >= 0 ? &screens[selectedLoadingScreen_] : nullptr;

    // --- left: screens + elements --------------------------------------------
    ImGui::BeginChild("##ls_list", ImVec2(leftW, bodyH), ImGuiChildFlags_None);
    {
        ImGui::AlignTextToFramePadding();
        listHead("Screens");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - fs * 0.2f);
        if (ImGui::Button("+##newscreen", ImVec2(ImGui::GetFrameHeight(), 0))) ImGui::OpenPopup("##ls_templates");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("New screen");
        if (ImGui::BeginPopup("##ls_templates")) {
            ImGui::TextDisabled("New screen from");
            static const char* kTemplates[] = {"Blank", "Minimal bar", "Dots", "Corner spinner",
                                               "Animated progress", "Press START"};
            static const char* kTips[] = {"Just a background.", "LOADING over a filling bar.",
                                          "Five dots that light up as it loads.",
                                          "A small spinner in the corner (frames included).",
                                          "A frame strip driven by the load - slice your sprite sheet into it.",
                                          "A bar, then PRESS START until the player presses it."};
            for (int k = 0; k < 6; ++k) {
                if (ImGui::MenuItem(kTemplates[k])) newScreen(k);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", kTips[k]);
            }
            if (lsp && ImGui::MenuItem("Duplicate the selected screen")) {
                LoadingScreenDef copy = *lsp;
                const std::string base = copy.name;
                for (int k = 2;; ++k) {
                    copy.name = base + "-" + std::to_string(k);
                    bool taken = false;
                    for (const auto& o : screens) taken |= (o.name == copy.name);
                    if (!taken) break;
                }
                screens.push_back(std::move(copy));
                selectedLoadingScreen_ = (int)screens.size() - 1;
                lsSelKind_ = 3;
                changed = true;
            }
            ImGui::EndPopup();
        }
        for (int i = 0; i < (int)screens.size(); ++i) {
            ImGui::PushID(i);
            const bool isDef = project_.defaultLoadingScreen == i;
            const bool isBoot = project_.bootLoadingScreen == i;
            if (screenRow(screens[i].name.c_str(),
                          isDef && isBoot ? "default \xC2\xB7 start" : isDef ? "default" : isBoot ? "game start" : "",
                          "L", sem.accent,
                          selectedLoadingScreen_ == i && lsSelKind_ != 4, false, false, nullptr)
                    .clicked) {
                selectedLoadingScreen_ = i;
                lsSelKind_ = 3;
                lsSelIdx_ = -1;
            }
            ImGui::PopID();
        }
        if (screens.empty()) {
            ImGui::Indent(fs * 0.4f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("None - every scene shows the built-in loading.png on black.");
            ImGui::PopTextWrapPos();
            ImGui::Unindent(fs * 0.4f);
        }
        if (lsp) {
            // Who shows it: scenes that name it, and - for the default - the
            // ones that inherit. An unused screen says so, with the remedy.
            const std::vector<std::string> users = usersOf(selectedLoadingScreen_);
            ImGui::Indent(fs * 0.5f);
            ImGui::PushTextWrapPos(0.0f);
            if (users.empty()) {
                ui::StatusLine(1, "Not shown anywhere");
                ImGui::TextDisabled("Make it the default, or pick it in Assignments.");
                if (ImGui::TextLink("Make default")) {
                    project_.defaultLoadingScreen = selectedLoadingScreen_;
                    changed = true;
                }
            } else {
                std::string line = "Used by: ";
                for (size_t k = 0; k < users.size(); ++k) line += (k ? ", " : "") + users[k];
                ImGui::TextDisabled("%s", line.c_str());
            }
            if (!project_.settings.loadingScreen) ui::StatusLine(1, "Show between scenes is off");
            ImGui::PopTextWrapPos();
            ImGui::Unindent(fs * 0.5f);

            ImGui::Dummy(ImVec2(0.0f, fs * 0.3f));
            ImGui::Separator();
            listHead("Back to front");
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x -
                            (ImGui::CalcTextSize("+ Add").x + ImGui::GetStyle().FramePadding.x * 2.0f));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - fs * 0.2f);
            if (ImGui::Button("+ Add")) ImGui::OpenPopup("##ls_add");
            if (ImGui::BeginPopup("##ls_add")) {
                if (ImGui::MenuItem("Image (PNG)...")) {
                    const int i = importHudImageInto(lsp->images);
                    if (i >= 0) {
                        lsSelKind_ = 0;
                        lsSelIdx_ = i;
                        changed = true;
                    }
                }
                if (ImGui::MenuItem("Text")) {
                    HudText t;
                    int counter = 0;
                    for (;;) {
                        t.name = "text-" + std::to_string(++counter);
                        bool taken = false;
                        for (const auto& o : lsp->texts) taken |= (o.name == t.name);
                        if (!taken) break;
                    }
                    t.text = "LOADING";
                    t.pos[1] = 0.62f;  // clear of a new bar's 0.75
                    lsp->texts.push_back(std::move(t));
                    lsSelKind_ = 1;
                    lsSelIdx_ = (int)lsp->texts.size() - 1;
                    changed = true;
                }
                if (ImGui::MenuItem("Animation (frames)")) {
                    LoadingAnim an;
                    int counter = 0;
                    for (;;) {
                        an.name = "anim-" + std::to_string(++counter);
                        bool taken = false;
                        for (const auto& o : lsp->anims) taken |= (o.name == an.name);
                        if (!taken) break;
                    }
                    lsp->anims.push_back(std::move(an));
                    lsSelKind_ = 5;
                    lsSelIdx_ = (int)lsp->anims.size() - 1;
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("A spinner or a progress strip from sprite frames\n(PNGs, or a sliced sprite sheet).");
                if (ImGui::MenuItem("Continue prompt", nullptr, lsp->waitForButton)) {
                    lsp->waitForButton = true;
                    lsSelKind_ = 6;
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("PRESS START: the screen waits for a button after loading.");
                if (ImGui::MenuItem("Progress bar")) {
                    LoadingBar b;
                    int counter = 0;
                    for (;;) {
                        b.name = "bar-" + std::to_string(++counter);
                        bool taken = false;
                        for (const auto& o : lsp->bars) taken |= (o.name == b.name);
                        if (!taken) break;
                    }
                    lsp->bars.push_back(std::move(b));
                    lsSelKind_ = 2;
                    lsSelIdx_ = (int)lsp->bars.size() - 1;
                    changed = true;
                }
                ImGui::EndPopup();
            }
            ImGui::BeginChild("##ls_elems", ImVec2(0, 0));
            {
                char hex[16];
                std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", (int)(lsp->bgColor[0] * 255 + 0.5f),
                              (int)(lsp->bgColor[1] * 255 + 0.5f), (int)(lsp->bgColor[2] * 255 + 0.5f));
                if (screenRow("Background", hex, "", ImVec4(lsp->bgColor[0], lsp->bgColor[1], lsp->bgColor[2], 1.0f),
                              lsSelKind_ == 3, false, false, nullptr)
                        .clicked)
                    lsSelKind_ = 3;
                for (int i = 0; i < (int)lsp->images.size(); ++i) {
                    ImGui::PushID(1000 + i);
                    const HudImage& im = lsp->images[i];
                    char meta[32];
                    std::snprintf(meta, sizeof(meta), "%.0f\xC3\x97%.0f", im.size[0], im.size[1]);
                    if (screenRow(im.name.empty() ? "(image)" : im.name.c_str(), meta, "I", sem.ok,
                                  lsSelKind_ == 0 && lsSelIdx_ == i, false, false, nullptr)
                            .clicked)
                        lsSelKind_ = 0, lsSelIdx_ = i;
                    ImGui::PopID();
                }
                for (int i = 0; i < (int)lsp->texts.size(); ++i) {
                    ImGui::PushID(2000 + i);
                    const HudText& t = lsp->texts[i];
                    const std::string meta = std::to_string(t.size) + " px";
                    if (screenRow(t.name.c_str(), meta.c_str(), "T", sem.warn, lsSelKind_ == 1 && lsSelIdx_ == i,
                                  false, false, nullptr)
                            .clicked)
                        lsSelKind_ = 1, lsSelIdx_ = i;
                    ImGui::PopID();
                }
                for (int i = 0; i < (int)lsp->bars.size(); ++i) {
                    ImGui::PushID(3000 + i);
                    const LoadingBar& b = lsp->bars[i];
                    const std::string meta = b.kind == 1 ? std::to_string(b.segments) + " seg" : "fill";
                    if (screenRow(b.name.c_str(), meta.c_str(), "B", sem.danger, lsSelKind_ == 2 && lsSelIdx_ == i,
                                  false, false, nullptr)
                            .clicked)
                        lsSelKind_ = 2, lsSelIdx_ = i;
                    ImGui::PopID();
                }
                for (int i = 0; i < (int)lsp->anims.size(); ++i) {
                    ImGui::PushID(4000 + i);
                    const LoadingAnim& an = lsp->anims[i];
                    char meta[48];
                    std::snprintf(meta, sizeof(meta), "%s \xC2\xB7 %d fr", an.mode == 1 ? "progress" : "spinner",
                                  (int)an.frames.size());
                    if (screenRow(an.name.c_str(), meta, "A", ImVec4(0.95f, 0.56f, 0.88f, 1.0f),
                                  lsSelKind_ == 5 && lsSelIdx_ == i, false, false, nullptr)
                            .clicked)
                        lsSelKind_ = 5, lsSelIdx_ = i;
                    ImGui::PopID();
                }
                // The continue prompt is drawn last, so it is listed last.
                if (lsp->waitForButton) {
                    static const char* kBtn[] = {"any", "START", "Cross"};
                    if (screenRow("Continue prompt", kBtn[ImClamp(lsp->waitButton, 0, 2)], "P", sem.ok,
                                  lsSelKind_ == 6, false, false, nullptr)
                            .clicked)
                        lsSelKind_ = 6;
                }
                if (lsp->images.empty() && lsp->texts.empty() && lsp->bars.empty() && lsp->anims.empty())
                    ImGui::TextDisabled("Only a background. + Add a text, a bar or an animation.");
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();  // ##ls_list

    // --- centre: preview -------------------------------------------------------
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, sem.ground);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 2.0f);
    ImGui::BeginChild("##ls_preview", ImVec2(midW, bodyH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 org = ImGui::GetCursorScreenPos();
        const float margin = fs;
        const float footH = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() + fs;
        float fw = avail.x - margin * 2.0f;
        float fh = fw * 448.0f / 512.0f;
        if (fh > avail.y - margin * 2.0f - footH) {
            fh = ImMax(fs * 4.0f, avail.y - margin * 2.0f - footH);
            fw = fh * 512.0f / 448.0f;
        }
        fw = IM_TRUNC(fw);
        fh = IM_TRUNC(fh);
        const ImVec2 f0(IM_TRUNC(org.x + (avail.x - fw) * 0.5f),
                        IM_TRUNC(org.y + ImMax(margin, (avail.y - fh - footH) * 0.5f)));
        const ImVec2 f1(f0.x + fw, f0.y + fh);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // A simulated load: the game presents a frame every ~1/24 of the work,
        // unevenly paced - so the bar steps, quick then slow then quick.
        if (lsPlayT0_ >= 0.0) {
            const float t = (float)(ImGui::GetTime() - lsPlayT0_), dur = 2.4f;
            if (t >= dur + 0.6f) {
                lsPlayT0_ = -1.0;
                lsPreviewProgress_ = 1.0f;
                // Ends where the game would: on the continue prompt.
                lsWaitPreview_ = lsp && lsp->waitForButton;
            } else {
                const float p = ImMin(1.0f, t / dur);
                const float e = p < 0.5f ? 2.0f * p * p * 1.2f : 1.0f - std::pow(-2.0f * p + 2.0f, 2.0f) * 0.4f;
                lsPreviewProgress_ = ImClamp(std::floor(ImClamp(e, 0.0f, 1.0f) * 24.0f) / 24.0f, 0.0f, 1.0f);
            }
        }

        std::vector<HudHit> hits;
        if (lsSelKind_ == 4 && selectedSplash_ >= 0) {
            // A splash: its background with the image, as it plays.
            const SplashScreen& s = splashes[selectedSplash_];
            dl->AddRectFilled(f0, f1, IM_COL32((int)(s.bgColor[0] * 255), (int)(s.bgColor[1] * 255),
                                               (int)(s.bgColor[2] * 255), 255));
            const float dw = s.image.size[0] / 512.0f * fw, dh = s.image.size[1] / 448.0f * fh;
            const ImVec2 a(f0.x + s.image.pos[0] * fw - dw * 0.5f, f0.y + s.image.pos[1] * fh - dh * 0.5f);
            dl->PushClipRect(f0, f1, true);
            if (const HudTexture* t = hudTexture(s.image.imagePath))
                dl->AddImage((ImTextureID)(intptr_t)t->tex, a, ImVec2(a.x + dw, a.y + dh));
            dl->PopClipRect();
            HudHit h;
            h.sel = 4;
            h.index = selectedSplash_;
            h.min = a;
            h.max = ImVec2(a.x + dw, a.y + dh);
            hits.push_back(h);
        } else if (lsp) {
            dl->PushClipRect(f0, f1, true);
            drawLoadingFrame(dl, *lsp, lsPreviewProgress_, f0, ImVec2(fw, fh), &hits,
                             lsWaitPreview_ && lsp->waitForButton, (float)ImGui::GetTime());
            dl->PopClipRect();
        } else {
            dl->AddRectFilled(f0, f1, IM_COL32(0, 0, 0, 255));
            const ImVec2 c((f0.x + f1.x) * 0.5f, (f0.y + f1.y) * 0.5f);
            if (const HudTexture* t = hudTexture("res/hud/loading.png")) {
                const float dw = t->w / 512.0f * fw, dh = t->h / 448.0f * fh;
                dl->AddImage((ImTextureID)(intptr_t)t->tex, ImVec2(c.x - dw * 0.5f, c.y - dh * 0.5f),
                             ImVec2(c.x + dw * 0.5f, c.y + dh * 0.5f));
            } else {
                // Generated at the first build when missing.
                const char* l = "built-in res/hud/loading.png";
                const ImVec2 ts = ImGui::CalcTextSize(l);
                dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), tint(sem.textDim, 0.7f), l);
            }
        }
        dl->AddRect(ImVec2(f0.x - 1, f0.y - 1), ImVec2(f1.x + 1, f1.y + 1), tint(sem.border));

        // Direct manipulation, as in the UI Editor: press picks a corner
        // handle of the selection or the topmost element; drag moves it, a
        // handle resizes it about its centre (images, bars, splash images).
        auto posOf = [&](int k, int i) -> float* {
            if (k == 4) return splashes[i].image.pos;
            if (!lsp) return nullptr;
            if (k == 5) return lsp->anims[i].pos;
            if (k == 6) return lsp->prompt.pos;
            return k == 0 ? lsp->images[i].pos : k == 1 ? lsp->texts[i].pos : k == 2 ? lsp->bars[i].pos : nullptr;
        };
        auto sizeOf = [&](int k, int i) -> float* {
            if (k == 4) return splashes[i].image.size;
            if (!lsp) return nullptr;
            if (k == 5) return lsp->anims[i].size;
            return k == 0 ? lsp->images[i].size : k == 2 ? lsp->bars[i].size : nullptr;
        };
        const int selKind = lsSelKind_;
        const int selIdx = lsSelKind_ == 4 ? selectedSplash_ : lsSelIdx_;
        const HudHit* selHit = nullptr;
        for (const HudHit& h : hits)
            if (h.sel == selKind && h.index == selIdx) selHit = &h;
        const float hs = IM_TRUNC(scaled(8.0f));
        ImVec2 corner[4];
        if (selHit) {
            const ImVec2 a(selHit->min.x - 3, selHit->min.y - 3), b(selHit->max.x + 3, selHit->max.y + 3);
            corner[0] = a;
            corner[1] = ImVec2(b.x, a.y);
            corner[2] = ImVec2(a.x, b.y);
            corner[3] = b;
        }
        ImGui::SetCursorScreenPos(f0);
        ImGui::InvisibleButton("Loading preview canvas", ImVec2(fw, fh));
        const ImVec2 mp = ImGui::GetIO().MousePos;
        auto inRect = [&](ImVec2 a, ImVec2 b, float pad) {
            return mp.x >= a.x - pad && mp.x <= b.x + pad && mp.y >= a.y - pad && mp.y <= b.y + pad;
        };
        if (ImGui::IsItemActivated()) {
            lsDragMode_ = 0;
            if (selHit && sizeOf(selKind, selIdx))
                for (int c = 0; c < 4; ++c)
                    if (inRect(corner[c], corner[c], hs * 0.75f)) lsDragMode_ = 2 + c;
            if (lsDragMode_ == 0)
                for (int i = (int)hits.size() - 1; i >= 0; --i)
                    if (inRect(hits[i].min, hits[i].max, 2.0f)) {
                        if (hits[i].sel == 4) {
                            selectedSplash_ = hits[i].index;
                        } else {
                            lsSelKind_ = hits[i].sel;
                            lsSelIdx_ = hits[i].index;
                        }
                        lsDragMode_ = 1;
                        break;
                    }
        }
        const int dk = lsSelKind_, di = lsSelKind_ == 4 ? selectedSplash_ : lsSelIdx_;
        if (ImGui::IsItemActive() && lsDragMode_ != 0 && di >= 0) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            if (d.x != 0.0f || d.y != 0.0f) {
                if (lsDragMode_ == 1) {
                    if (float* p = posOf(dk, di)) {
                        p[0] = ImClamp(p[0] + d.x / fw, 0.0f, 1.0f);
                        p[1] = ImClamp(p[1] + d.y / fh, 0.0f, 1.0f);
                    }
                } else if (float* s = sizeOf(dk, di)) {
                    const int c = lsDragMode_ - 2;
                    const float sx = (c == 1 || c == 3) ? 1.0f : -1.0f;
                    const float sy = (c >= 2) ? 1.0f : -1.0f;
                    s[0] = ImClamp(std::round(s[0] + sx * 2.0f * d.x * 512.0f / fw), 1.0f, 512.0f);
                    s[1] = ImClamp(std::round(s[1] + sy * 2.0f * d.y * 448.0f / fh), 1.0f, 512.0f);
                }
            }
        }
        if (ImGui::IsItemDeactivated()) {
            if (lsDragMode_ != 0) changed = true;
            lsDragMode_ = 0;
        }
        if (selHit) {
            dl->AddRect(corner[0], corner[3], tint(sem.accent), 2.0f, 0, 1.5f);
            if (sizeOf(selKind, selIdx))
                for (int c = 0; c < 4; ++c) {
                    const ImVec2 a(corner[c].x - hs * 0.5f, corner[c].y - hs * 0.5f);
                    const ImVec2 b(corner[c].x + hs * 0.5f, corner[c].y + hs * 0.5f);
                    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
                    dl->AddRect(a, b, tint(sem.accent), 0.0f, 0, 1.5f);
                }
        }

        // Load progress: play a simulated load, or scrub it.
        const float rowW = fw;
        ImGui::SetCursorScreenPos(ImVec2(f0.x, f1.y + fs * 0.7f));
        const bool playing = lsPlayT0_ >= 0.0 || lsWaitPreview_;
        if (ImGui::Button(playing ? "Stop##lsplay" : "Preview##lsplay")) {
            lsPlayT0_ = playing ? -1.0 : ImGui::GetTime();
            lsWaitPreview_ = false;
            if (!playing) lsPreviewProgress_ = 0.0f;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Play the screen the way the game shows it: the bar steps with\n"
                              "the load (a frame every ~1/24 of the work, unevenly paced),\n"
                              "spinners spin, and a screen that waits ends on its prompt.");
        if (lsWaitPreview_ && lsp && lsp->waitForButton) {
            static const char* kBtn[] = {"a button", "START", "Cross"};
            ImGui::SameLine();
            const std::string press = std::string("Press ") + (lsp->waitButton == 0 ? "a button" : kBtn[lsp->waitButton]);
            if (ui::PrimaryButton(press.c_str())) lsWaitPreview_ = false;
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(sem.ok, "Loaded \xC2\xB7 waiting for %s", kBtn[ImClamp(lsp->waitButton, 0, 2)]);
            ImGui::SetCursorScreenPos(ImVec2(f0.x, ImGui::GetCursorScreenPos().y));
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Load progress");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImMax(fs * 4.0f, f0.x + rowW - ImGui::GetCursorScreenPos().x));
        float pct = lsPreviewProgress_ * 100.0f;
        if (ImGui::SliderFloat("##lsprogress", &pct, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
            lsPreviewProgress_ = pct * 0.01f;
            lsPlayT0_ = -1.0;
            lsWaitPreview_ = false;
        }
        ImGui::SetCursorScreenPos(ImVec2(f0.x, ImGui::GetCursorScreenPos().y));
        ImGui::TextDisabled("512\xC3\x97" "448 \xC2\xB7 the bar follows real load work in the game");
    }
    ImGui::EndChild();  // ##ls_preview

    // --- right: inspector --------------------------------------------------------
    ImGui::SameLine();
    ImGui::BeginChild("##ls_props", ImVec2(0, bodyH), ImGuiChildFlags_None);
    ImGui::PushItemWidth(-FLT_MIN);
    {
        ui::InspectorRows rows(scaled(76), scaled(116));
        auto title = [&](const char* t, const std::string& sub) {
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted(t);
            }
            ImGui::TextDisabled("%s", sub.c_str());
            ImGui::Dummy(ImVec2(0.0f, fs * 0.2f));
        };
        if (lsSelKind_ == 4 && selectedSplash_ >= 0) {
            SplashScreen& s = splashes[selectedSplash_];
            title(s.name.c_str(), "Boot splash \xC2\xB7 " + std::to_string(selectedSplash_ + 1) + " of " +
                                      std::to_string(splashes.size()));
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
            if (ui::InputText("Name##splash", nameBuf, sizeof(nameBuf))) s.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::DragFloat("Duration (s)", &s.duration, 0.05f, 0.1f, 10.0f, "%.1f s"))
                s.duration = ImClamp(s.duration, 0.1f, 10.0f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::ColorHex("Background##splash", s.bgColor)) changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::Caption("Image");
            ui::DragFloat2("Size (px)##splash", s.image.size, 1.0f, 1.0f, 512.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::DragFloat2("Position##splash", s.image.pos, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Button("Change image...")) {
                std::vector<HudImage> tmp;
                const int i = importHudImageInto(tmp);
                if (i >= 0) {
                    s.image.imagePath = tmp[i].imagePath;
                    changed = true;
                }
            }
            changed |= hudBakeControls(s.image);
            ImGui::Spacing();
            if (ui::DangerButton("Delete splash")) {
                splashes.erase(splashes.begin() + selectedSplash_);
                selectedSplash_ = -1;
                lsSelKind_ = 3;
                changed = true;
            }
        } else if (!lsp) {
            if (ui::EmptyState(ui::Glyph::Screen, "No loading screens",
                               "Scenes show the built-in loading.png on black until a screen exists.",
                               "Create a screen"))
                newScreen(1);
        } else if (lsSelKind_ == 0 && lsSelIdx_ >= 0 && lsSelIdx_ < (int)lsp->images.size()) {
            HudImage& h = lsp->images[lsSelIdx_];
            title(h.name.empty() ? "Image" : h.name.c_str(), "Image \xC2\xB7 " + h.imagePath);
            ui::Caption("Placement");
            ui::DragFloat2("Position##lsimg", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::DragFloat2("Size (px)##lsimg", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= hudBakeControls(h);
            ImGui::Spacing();
            if (ui::DangerButton("Delete image")) {
                lsp->images.erase(lsp->images.begin() + lsSelIdx_);
                lsSelIdx_ = -1;
                lsSelKind_ = 3;
                changed = true;
            }
        } else if (lsSelKind_ == 1 && lsSelIdx_ >= 0 && lsSelIdx_ < (int)lsp->texts.size()) {
            HudText& t = lsp->texts[lsSelIdx_];
            title(t.name.c_str(), "Text \xC2\xB7 baked to a sprite at build");
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", t.name.c_str());
            if (ui::InputText("Name##lstext", nameBuf, sizeof(nameBuf))) t.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            {
                char textBuf[512];
                std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
                if (ImGui::InputTextMultiline("##lstextbody", textBuf, sizeof(textBuf), ImVec2(-1.0f, scaled(64))))
                    t.text = textBuf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            }
            changed |= fontCombo(t.font);
            if (ui::DragInt("Font size##lstext", &t.size, 0.2f, 8, 48, "%d px")) t.size = ImClamp(t.size, 8, 48);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::ColorHex("Color##lstext", t.color)) changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ui::DragFloat2("Position##lstext", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::Switch("Drop shadow##lstext", &t.shadow)) changed = true;
            ImGui::Spacing();
            if (ui::DangerButton("Delete text")) {
                lsp->texts.erase(lsp->texts.begin() + lsSelIdx_);
                lsSelIdx_ = -1;
                lsSelKind_ = 3;
                changed = true;
            }
        } else if (lsSelKind_ == 2 && lsSelIdx_ >= 0 && lsSelIdx_ < (int)lsp->bars.size()) {
            LoadingBar& b = lsp->bars[lsSelIdx_];
            title(b.name.c_str(), b.kind == 1 ? "Progress bar \xC2\xB7 " + std::to_string(b.segments) + " segments"
                                              : std::string("Progress bar \xC2\xB7 continuous"));
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", b.name.c_str());
            if (ui::InputText("Name##lsbar", nameBuf, sizeof(nameBuf))) b.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= loadingBarControls(b);
            ImGui::Spacing();
            if (ui::DangerButton("Delete bar")) {
                lsp->bars.erase(lsp->bars.begin() + lsSelIdx_);
                lsSelIdx_ = -1;
                lsSelKind_ = 3;
                changed = true;
            }
        } else if (lsSelKind_ == 5 && lsSelIdx_ >= 0 && lsSelIdx_ < (int)lsp->anims.size()) {
            LoadingAnim& an = lsp->anims[lsSelIdx_];
            title(an.name.c_str(), std::string("Animation \xC2\xB7 ") +
                                       (an.mode == 1 ? "progress - the load picks the frame" : "spinner"));
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", an.name.c_str());
            if (ui::InputText("Name##lsanim", nameBuf, sizeof(nameBuf))) an.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= loadingAnimControls(an);
            ImGui::Spacing();
            if (ui::DangerButton("Delete animation")) {
                lsp->anims.erase(lsp->anims.begin() + lsSelIdx_);
                lsSelIdx_ = -1;
                lsSelKind_ = 3;
                changed = true;
            }
        } else if (lsSelKind_ == 6 && lsp->waitForButton) {
            HudText& t = lsp->prompt;
            title("Continue prompt", "Shown when the load is done, until the button");
            changed |= loadingWaitControls(*lsp);
            if (lsp->waitForButton) {
                ui::Caption("Prompt");
                char textBuf[128];
                std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
                if (ui::InputText("Text##lsprompt", textBuf, sizeof(textBuf))) t.text = textBuf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                changed |= fontCombo(t.font);
                if (ui::DragInt("Font size##lsprompt", &t.size, 0.2f, 8, 48, "%d px")) t.size = ImClamp(t.size, 8, 48);
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                if (ui::ColorHex("Color##lsprompt", t.color)) changed = true;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ui::DragFloat2("Position##lsprompt", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                static const char* kBlink[] = {"Steady", "Blink", "Pulse"};
                if (ui::SegmentedRow("Effect", kBlink, 3, &lsp->promptBlink)) changed = true;
                if (lsp->promptBlink != 0) {
                    if (ui::DragFloat("Period", &lsp->promptPeriod, 0.02f, 0.2f, 4.0f, "%.1f s"))
                        lsp->promptPeriod = ImClamp(lsp->promptPeriod, 0.2f, 4.0f);
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
                if (ui::Switch("Hide bars and animations##lsprompt", &lsp->promptHideOthers)) changed = true;
                ui::RowHelp("While the prompt shows, the bars and animations step aside\n"
                            "(an animation's After loading still applies when off).");
                ImGui::Spacing();
                if (ui::DangerButton("Remove the prompt")) {
                    lsp->waitForButton = false;
                    lsSelKind_ = 3;
                    changed = true;
                }
            }
        } else {
            // The screen itself: name, background, its wait, and which loads
            // show it - reported here, assigned in the Assignments tab.
            LoadingScreenDef& ls = *lsp;
            title(ls.name.c_str(), project_.defaultLoadingScreen == selectedLoadingScreen_
                                       ? std::string("Loading screen \xC2\xB7 the default")
                                       : std::string("Loading screen"));
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", ls.name.c_str());
            if (ui::InputText("Name##lsname", nameBuf, sizeof(nameBuf))) {
                // '@' starts the no-screen picks (project::kLoadingBuiltIn, project::kLoadingNone).
                std::string nn = nameBuf;
                while (!nn.empty() && nn[0] == '@') nn.erase(0, 1);
                // Keep per-scene references pointing at the renamed screen.
                for (SceneData& sc : project_.scenes)
                    if (sc.loadingScreen == ls.name) sc.loadingScreen = nn;
                ls.name = nn;
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ui::ColorHex("Background", ls.bgColor)) changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= loadingWaitControls(ls);
            // Used by: read-only chips (a click opens Assignments, where the
            // loads are assigned - one place, one answer per load).
            ui::Caption("Used by");
            {
                const std::vector<std::string> users = usersOf(selectedLoadingScreen_);
                if (users.empty()) ImGui::TextDisabled("Nothing shows it yet.");
                for (size_t k = 0; k < users.size(); ++k) {
                    ui::ChipFlow(ui::ChipButtonWidth(users[k].c_str()), k == 0);
                    if (ui::ChipButton(users[k].c_str())) lsTab_ = 1;
                }
                if (ui::ChipButton("Edit in Assignments", true)) lsTab_ = 1;
                if (project_.defaultLoadingScreen != selectedLoadingScreen_) {
                    ImGui::SameLine();
                    if (ui::ChipButton("Make default")) {
                        project_.defaultLoadingScreen = selectedLoadingScreen_;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                        ImGui::SetTooltip("Every scene set to Default shows this screen.");
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Duplicate")) {
                LoadingScreenDef copy = ls;
                const std::string base = copy.name;
                for (int k = 2;; ++k) {
                    copy.name = base + "-" + std::to_string(k);
                    bool taken = false;
                    for (const auto& o : screens) taken |= (o.name == copy.name);
                    if (!taken) break;
                }
                screens.push_back(std::move(copy));
                selectedLoadingScreen_ = (int)screens.size() - 1;
                changed = true;
            } else {
                ImGui::SameLine();
                if (ui::DangerButton("Delete screen")) {
                    const std::string gone = ls.name;
                    for (SceneData& sc : project_.scenes)
                        if (sc.loadingScreen == gone) sc.loadingScreen.clear();
                    if (project_.defaultLoadingScreen == selectedLoadingScreen_)
                        project_.defaultLoadingScreen = -1;
                    else if (project_.defaultLoadingScreen > selectedLoadingScreen_)
                        --project_.defaultLoadingScreen;
                    if (project_.bootLoadingScreen == selectedLoadingScreen_)
                        project_.bootLoadingScreen = -1;
                    else if (project_.bootLoadingScreen > selectedLoadingScreen_)
                        --project_.bootLoadingScreen;
                    screens.erase(screens.begin() + selectedLoadingScreen_);
                    selectedLoadingScreen_ = -1;
                    changed = true;
                }
            }
        }
    }
    ImGui::PopItemWidth();
    ImGui::EndChild();  // ##ls_props

    // project-wide data: marks dirty, pushes no undo step (the per-scene
    // assignment does - commitChange snapshots the scenes). A preview drag
    // commits once, on release.
    if (lsDragMode_ == 0 &&
        (changed || project::sectionJson(project_, project::Section::LoadingScreens) +
                            project::sectionJson(project_, project::Section::Splash) !=
                        beforeSection))
        commitChange();
    ImGui::End();
}
