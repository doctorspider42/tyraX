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
#include <map>
#include <set>
#include <sstream>

#include "aisupport.hpp"
#include "animedit.hpp"
#include "decalproj.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "gl_loader.h"
#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "theme.hpp"
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
    if (ImGui::BeginCombo("Source", current.c_str())) {
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
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fonts imported into the project (res/fonts) travel\n"
                          "with it; Windows fonts depend on this machine.\n"
                          "Only the rasterized pixels ever reach the PS2 -\n"
                          "the TTF itself never ships.");
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

// Tools > Font Manager: the project's typefaces. Every text (HUD texts, menus,
// loading screens, Display Text nodes) names one of these, so restyling a
// project is an edit here rather than a hunt through every text.
//
// fonts[0] is the fallback for unset references, which is why the last entry
// can never be deleted.
void App::drawFontManagerWindow() {
    if (!showFontManager_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(600.0f), scaled(440.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Font Manager", &showFontManager_)) {
        ImGui::End();
        return;
    }
    if (project_.fonts.empty()) project_.fonts.push_back(GameFont{});
    if (fontSel_ < 0 || fontSel_ >= (int)project_.fonts.size()) fontSel_ = 0;

    bool changed = false;
    // Belt and braces: fonts ride in the Hud section (writeHudSection), and a
    // rename retargets references in the SCENES, which undo covers on its own.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Hud);

    ImGui::BeginChild("fontlist", ImVec2(scaled(170.0f), 0), true);
    for (size_t i = 0; i < project_.fonts.size(); ++i) {
        ImGui::PushID((int)i);
        std::string label = project_.fonts[i].name;
        if (i == 0) label += "  [default]";
        if (ImGui::Selectable(label.c_str(), fontSel_ == (int)i))
            fontSel_ = (int)i;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    GameFont& f = project_.fonts[fontSel_];
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", f.name.c_str());
        ImGui::SetNextItemWidth(scaled(200.0f));
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
            std::string want = buf;
            // Names are the reference key, so they must stay unique and
            // non-empty; a clash would silently redirect other texts here.
            bool clash = want.empty();
            for (size_t i = 0; i < project_.fonts.size(); ++i)
                if ((int)i != fontSel_ && project_.fonts[i].name == want)
                    clash = true;
            if (!clash) renameFont(fontSel_, want);
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    changed |= fontSourceCombo(f.fontPath);
    if (ImGui::ColorEdit3("Color##font", f.color, ImGuiColorEditFlags_NoInputs))
        changed = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(Display Text only)");
    if (ImGui::Checkbox("Drop shadow##font", &f.shadow)) changed = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(Display Text only)");

    // Everything below only matters to a font a Display Text node draws with -
    // static text never touches the atlas.
    const std::vector<int> atlasFonts = project_.atlasFontIndices();
    const bool usedDynamically =
        std::find(atlasFonts.begin(), atlasFonts.end(), fontSel_) !=
        atlasFonts.end();

    ImGui::SeparatorText("Glyph atlas");
    if (!usedDynamically) {
        ImGui::TextDisabled(
            "No Display Text node uses this font, so no atlas is\n"
            "baked and nothing ships to the PS2. Static texts\n"
            "rasterize straight from the TTF at build.");
    }
    ImGui::SetNextItemWidth(scaled(120.0f));
    if (ImGui::DragInt("Atlas size", &f.atlasSize, 0.2f, 8, 48, "%d px"))
        f.atlasSize = f.atlasSize < 8 ? 8 : f.atlasSize > 48 ? 48 : f.atlasSize;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Height the glyphs are rasterized at. Display Text\n"
                          "scales from this, so it trades sharpness against\n"
                          "VRAM - not the on-screen size.");
    {
        const char* kQuant[] = {"4bit", "8bit", "none"};
        int qi = f.quant == "8bit" ? 1 : f.quant == "none" ? 2 : 0;
        ImGui::SetNextItemWidth(scaled(120.0f));
        if (ImGui::Combo("Atlas colors", &qi, kQuant, 3)) {
            f.quant = kQuant[qi];
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Palette depth of the baked atlas. Glyphs bake white and\n"
                "are tinted at runtime, so 16 colors is normally plenty -\n"
                "and roughly 8x cheaper in VRAM than full color.");
    }

    // Atlas footprint: the PS2 has ~1.33 MB of texture VRAM once the frame and
    // z buffers are placed, and a resident atlas never leaves it, so show the
    // real cost rather than let it surprise someone on hardware.
    //
    // Cached on the bake-affecting fields: atlasLayout measures all 95 glyphs
    // (stbtt box per codepoint), which is not something to redo every frame
    // just to print one line.
    {
        const std::string key = f.fontPath + "\x1f" + std::to_string(f.atlasSize) +
                                "\x1f" + f.quant;
        if (key != fontAtlasKey_) {
            fontAtlasKey_ = key;
            fontAtlasInfo_.clear();
            menubake::AtlasLayout lay;
            if (menubake::atlasLayout(f, project_, lay)) {
                const int bpp = f.quant == "none" ? 32 : f.quant == "8bit" ? 8 : 4;
                // +8 KB per allocation (the engine's alignment tax), +CLUT for
                // the palettized modes.
                const int kb = (lay.texW * lay.texH * bpp / 8 + 8192 +
                                (bpp == 32 ? 0 : 8192)) /
                               1024;
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "Atlas: %dx%d, ~%d KB VRAM while shown", lay.texW,
                              lay.texH, kb);
                fontAtlasInfo_ = buf;
                fontAtlasClipped_ = lay.clipped;
            } else {
                fontAtlasClipped_ = false;
            }
        }
        if (fontAtlasInfo_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "No usable TTF found for this font.");
        } else {
            ImGui::TextUnformatted(fontAtlasInfo_.c_str());
            if (fontAtlasClipped_)
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f),
                                   "Glyphs past the 512px cap were dropped -\n"
                                   "lower the atlas size.");
        }
    }
    // Live preview: the exact rasterizer the build uses, so what is on screen
    // here is what the game gets.
    {
        HudText sample;
        sample.name = "##fontpreview";
        sample.text = "Sample 0123 gjpq";
        sample.font = f.name;
        sample.size = f.atlasSize;
        sample.shadow = f.shadow;
        for (int i = 0; i < 3; ++i) sample.color[i] = f.color[i];
        if (const HudTexture* tex = hudTextTexture(sample))
            ImGui::Image((ImTextureID)(intptr_t)tex->tex,
                         ImVec2(scaled((float)tex->w), scaled((float)tex->h)));
    }

    ImGui::Separator();
    if (ImGui::Button("+ Add font")) {
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
    ImGui::SameLine();
    ImGui::BeginDisabled(project_.fonts.size() <= 1);
    if (ImGui::Button("Delete font")) {
        // Texts naming it fall back to the default entry (Project::findFont),
        // so deleting never breaks a bake.
        project_.fonts.erase(project_.fonts.begin() + fontSel_);
        if (fontSel_ >= (int)project_.fonts.size())
            fontSel_ = (int)project_.fonts.size() - 1;
        changed = true;
    }
    ImGui::EndDisabled();
    if (project_.fonts.size() <= 1 && ImGui::IsItemHovered())
        ImGui::SetTooltip("The default font cannot be deleted - it is what\n"
                          "every unset font reference resolves to.");
    ImGui::EndGroup();

    if (changed || project::sectionJson(project_, project::Section::Hud) != beforeSection)
        commitChange();
    ImGui::End();
}

// One action's bindings inside one preset: a pad-button, a keyboard-key and a
// mouse-button picker, each with a "(none)" entry - an action may be bound to
// any combination, and all of them fire it.
bool App::inputBindingRow(InputPreset& preset, const InputAction& action) {
    bool changed = false;
    InputBinding& b = preset.at(action.name);

    ImGui::SetNextItemWidth(scaled(130.0f));
    if (ImGui::BeginCombo("Pad button",
                          b.pad.empty() ? "(none)" : b.pad.c_str())) {
        if (ImGui::Selectable("(none)", b.pad.empty())) {
            b.pad.clear();
            changed = true;
        }
        for (int i = 0; i < 16; ++i)
            if (ImGui::Selectable(kPadButtonNames[i], b.pad == kPadButtonNames[i])) {
                b.pad = kPadButtonNames[i];
                changed = true;
            }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(scaled(130.0f));
    const char* keyLabel = b.key ? inputKeyLabel(b.key) : "(none)";
    if (ImGui::BeginCombo("Keyboard key", *keyLabel ? keyLabel : "(none)")) {
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
    ImGui::SameLine();
    ImGui::TextDisabled("(USB keyboard)");

    ImGui::SetNextItemWidth(scaled(130.0f));
    if (ImGui::Combo("Mouse button", &b.mouse,
                     "(none)\0Left\0Right\0Middle\0"))
        changed = true;

    ImGui::TextDisabled("Resolves to: %s", inputBindingLabel(b).c_str());
    return changed;
}

// Tools > Input Map (docs/input-bindings.md). Left: the project's named
// actions. Right: the selected action's identity plus its binding in each
// preset. The presets are what "bindings per project" means; a player's own
// rebinds happen in-game through a menu Rebind key row and never touch this.
void App::drawInputMapWindow() {
    if (!showInputMap_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(680.0f), scaled(460.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Input Map", &showInputMap_)) {
        ImGui::End();
        return;
    }

    InputMap& im = project_.input;
    if (im.presets.empty()) im.presets.push_back(InputPreset{});
    if (inputPresetSel_ < 0 || inputPresetSel_ >= (int)im.presets.size())
        inputPresetSel_ = 0;
    if (im.activePreset < 0 || im.activePreset >= (int)im.presets.size())
        im.activePreset = 0;

    bool changed = false;
    // Belt and braces, as elsewhere: the window edits the Input section AND
    // one Settings field (sprint speed), so both go into the comparison.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Input) +
        project::sectionJson(project_, project::Section::Settings);

    // --- left: the action list -------------------------------------------
    ImGui::BeginChild("##inputactions", ImVec2(scaled(200.0f), 0),
                      ImGuiChildFlags_Borders);
    for (size_t i = 0; i < im.actions.size(); ++i) {
        ImGui::PushID((int)i);
        const InputAction& a = im.actions[i];
        std::string label = a.label.empty() ? a.name : a.label;
        if (a.role == InputAction::RoleNone) label += "  *";  // custom action
        if (ImGui::Selectable(label.c_str(), inputActionSel_ == (int)i))
            inputActionSel_ = (int)i;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "%s\nBound to: %s\n%s", a.name.c_str(),
                inputBindingLabel(im.presets[inputPresetSel_].at(a.name)).c_str(),
                a.role == InputAction::RoleNone
                    ? "Custom action - only the On Action flow trigger reads it."
                    : "Built-in role - the generated game reads it directly.");
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (inputActionSel_ < 0 || inputActionSel_ >= (int)im.actions.size())
        inputActionSel_ = 0;

    if (im.actions.empty()) {
        ImGui::TextWrapped(
            "This project has no input actions. \"Restore built-ins\" brings "
            "back the standard set (jump / use / sprint / menu navigation).");
    } else {
        InputAction& a = im.actions[inputActionSel_];
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", a.label.c_str());
            ImGui::SetNextItemWidth(scaled(200.0f));
            if (ImGui::InputText("Label", buf, sizeof(buf))) a.label = buf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        {
            // The name is the reference key (flow nodes, menu rows), so a
            // rename has to follow into them - and must stay unique.
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", a.name.c_str());
            ImGui::SetNextItemWidth(scaled(200.0f));
            if (ImGui::InputText("Name", buf, sizeof(buf))) {
                std::string want = buf;
                bool clash = want.empty();
                for (size_t i = 0; i < im.actions.size(); ++i)
                    if ((int)i != inputActionSel_ && im.actions[i].name == want)
                        clash = true;
                if (!clash) renameInputAction(inputActionSel_, want);
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(referenced by name)");

        // Role: which built-in behavior the action drives. Reassigning is
        // allowed (that is how you move "sprint" onto a custom action).
        {
            static const char* kRoleLabels =
                "(custom - flow graph only)\0Jump\0Use\0Throw\0Sprint\0"
                "Fly up (noclip)\0Fly down (noclip)\0Menu confirm\0Menu back\0"
                "Pause menu\0Alternate (load slot)\0Menu up\0Menu down\0"
                "Menu left\0Menu right\0Move forward\0Move back\0Move left\0"
                "Move right\0";
            ImGui::SetNextItemWidth(scaled(220.0f));
            if (ImGui::Combo("Role", &a.role, kRoleLabels)) changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "What the generated game uses this action for. Only one\n"
                    "action per role reaches the game (the first one wins);\n"
                    "\"custom\" actions exist purely for the On Action trigger.");
            // A duplicated role is silently dropped at codegen - say so here.
            const int owner = im.roleIndex(a.role);
            if (a.role != InputAction::RoleNone && owner != inputActionSel_)
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                                   "\"%s\" already owns this role - this "
                                   "action is inert",
                                   im.actions[owner].label.c_str());
        }
        if (ImGui::Checkbox("Player may rebind it in-game", &a.rebindable))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Off = a menu \"Rebind key\" row shows the binding but refuses\n"
                "to change it. Keep the menu navigation actions off, or a bad\n"
                "rebind can lock the player out of the menu that would fix it.\n"
                "In-game rebinding covers the PAD button only; the keyboard and\n"
                "mouse bindings below stay exactly as you author them here.");

        // --- presets ------------------------------------------------------
        ImGui::SeparatorText("Bindings per preset");
        if (ImGui::BeginTabBar("##presets")) {
            for (size_t i = 0; i < im.presets.size(); ++i) {
                ImGui::PushID((int)i);
                std::string title = im.presets[i].name;
                if ((int)i == im.activePreset) title += " *";
                if (ImGui::BeginTabItem(title.c_str())) {
                    inputPresetSel_ = (int)i;
                    changed |= inputBindingRow(im.presets[i], a);
                    ImGui::EndTabItem();
                }
                ImGui::PopID();
            }
            ImGui::EndTabBar();
        }
    }

    // --- preset management ------------------------------------------------
    ImGui::SeparatorText("Presets");
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s",
                      im.presets[inputPresetSel_].name.c_str());
        ImGui::SetNextItemWidth(scaled(160.0f));
        if (ImGui::InputText("Preset name", buf, sizeof(buf))) {
            // Flow-graph Set Input Preset nodes reference presets by name.
            renameInputPreset(inputPresetSel_, buf);
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::SetNextItemWidth(scaled(160.0f));
    {
        std::string items;
        for (const InputPreset& pr : im.presets) {
            items += pr.name;
            items.push_back('\0');
        }
        items.push_back('\0');
        if (ImGui::Combo("Preset at game start", &im.activePreset, items.c_str()))
            changed = true;
    }
    if (ImGui::Button("+ Add preset")) {
        // A new preset starts as a copy of the edited one - the point of a
        // preset is a few deliberate differences, not a blank slate.
        InputPreset np = im.presets[inputPresetSel_];
        std::string base = "preset";
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
    ImGui::SameLine();
    ImGui::BeginDisabled(im.presets.size() <= 1);
    if (ImGui::Button("Delete preset")) {
        im.presets.erase(im.presets.begin() + inputPresetSel_);
        if (inputPresetSel_ >= (int)im.presets.size())
            inputPresetSel_ = (int)im.presets.size() - 1;
        if (im.activePreset >= (int)im.presets.size()) im.activePreset = 0;
        changed = true;
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Actions");
    if (ImGui::Button("+ Add action")) {
        InputAction na;
        std::string base = "action";
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
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(im.actions.empty());
    if (ImGui::Button("Delete action")) {
        const std::string gone = im.actions[inputActionSel_].name;
        im.actions.erase(im.actions.begin() + inputActionSel_);
        for (InputPreset& pr : im.presets)
            for (size_t i = 0; i < pr.bindings.size();)
                if (pr.bindings[i].action == gone)
                    pr.bindings.erase(pr.bindings.begin() + i);
                else
                    ++i;
        if (inputActionSel_ >= (int)im.actions.size())
            inputActionSel_ = (int)im.actions.size() - 1;
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Restore built-ins")) {
        // Only adds what is missing (project::ensureInputActions) - existing
        // bindings are never overwritten.
        project::ensureInputActions(project_);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-adds any missing built-in action (jump, use,\n"
                          "sprint, menu navigation...) with its default\n"
                          "binding. Never changes an action you already have.");

    ImGui::Separator();
    if (ImGui::Checkbox("Allow in-game rebinding", &im.allowRebind))
        changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Master switch for the menu \"Rebind key\" rows.\n"
                          "Off = the rows display bindings read-only.");
    ImGui::SetNextItemWidth(scaled(160.0f));
    if (ImGui::DragFloat("Sprint speed", &project_.settings.sprintMultiplier,
                         0.02f, 1.0f, 4.0f, "%.2fx"))
        changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Walk-speed multiplier while the sprint action is\n"
                          "held. 1.00x switches sprinting off without\n"
                          "unbinding the button.");
    ImGui::EndGroup();

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

// Tools > Bake Global Illumination (docs/global-illumination.md).
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

// The "Global illumination" tab of the Ambience Editor (drawAmbienceWindow).
// It lives there because that window is already where a scene's light is
// authored - the AO settings, the sky, the sun - and the bake is the last step
// of the same job. Not inside the PRESET editor beside it: these are
// project-wide settings plus a per-scene cache, not part of a mood bundle.
void App::drawGiBakeSection() {
    ProjectSettings& st = project_.settings;
    bool changed = false;

    if (ImGui::Checkbox("Enable baked global illumination", &st.giEnabled))
        changed = true;
    prefHelp(
        "Static geometry gets a baked multi-bounce lightmap; everything that "
        "moves gets its light from a probe grid. Off = the classic ambient + "
        "directional lighting, unchanged.");
    ImGui::TextDisabled(
        "Sky, sun, emissive materials and baked point lights become one\n"
        "integrator with bounces. Costs the PS2 nothing at runtime.");
    ImGui::Separator();

    ImGui::BeginDisabled(!st.giEnabled || giBaker_.running());
    ImGui::SeparatorText("Quality");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderInt("Rays per texel", &st.giRays, 16, 512)) changed = true;
    prefHelp("Hemisphere rays per lightmap texel and per probe. More = less "
             "noise in the bounce, linearly more bake time.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderInt("Bounces", &st.giBounces, 0, 6)) changed = true;
    prefHelp("0 = direct light + sky only. 1 already gives colour bleeding; "
             "2 is the useful default.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Sky light", &st.giSkyLight, 0.0f, 3.0f, "%.2f"))
        changed = true;
    prefHelp("How strongly the sky dome lights the scene. At 1.0 an open "
             "surface receives about what the flat Ambient used to give it.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Sun light", &st.giSunLight, 0.0f, 3.0f, "%.2f"))
        changed = true;
    prefHelp("Multiplier on the scene's directional light - which GI shadows "
             "for the first time.");
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Ambient floor", &st.giAmbientFloor, 0.0f, 0.3f,
                           "%.3f"))
        changed = true;
    prefHelp("A constant added everywhere. Real GI makes a sealed room with no "
             "light source pitch black; this keeps it readable.");

    ImGui::SeparatorText("Light probes");
    if (ImGui::Checkbox("Bake a probe grid", &st.giProbes)) changed = true;
    prefHelp("What lights the player, NPCs, animated models, physics bodies, "
             "imported models and textured surfaces. Without it those keep the "
             "flat ambient while the static half gets GI - which shows.");
    ImGui::BeginDisabled(!st.giProbes);
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Spacing", &st.giProbeSpacing, 1.0f, 12.0f, "%.1f u"))
        changed = true;
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderFloat("Level height", &st.giProbeHeight, 0.5f, 8.0f,
                           "%.1f u"))
        changed = true;
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::SliderInt("Levels", &st.giProbeLevels, 1, 12)) changed = true;
    prefHelp("Vertical layers above the lowest ground. 12 bytes per probe in "
             "EE RAM.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (changed) commitChange();

    ImGui::SeparatorText("Scenes");
    const gibake::Settings gst = gibake::settingsOf(st);
    int staleCount = 0;
    if (ImGui::BeginTable("giscenes", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Scene");
        ImGui::TableSetupColumn("Bake");
        ImGui::TableSetupColumn("Contents");
        ImGui::TableHeadersRow();
        for (int si = 0; si < (int)project_.scenes.size(); ++si) {
            gibake::Bake b;
            const bool present =
                gibake::read(gibake::cachePath(project_, si), b);
            const bool fresh =
                present && st.giEnabled &&
                b.signature == gibake::signature(project_, project_.scenes[si],
                                                 gst);
            if (!fresh) ++staleCount;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(project_.scenes[si].name.c_str());
            ImGui::TableNextColumn();
            if (fresh)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.45f, 1.0f), "baked");
            else if (present)
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f), "stale");
            else
                ImGui::TextDisabled("not baked");
            ImGui::TableNextColumn();
            if (present)
                ImGui::Text("lightmap %d, ground %d, probes %dx%dx%d",
                            b.atlas.size, b.terrain.size, b.probes.dim[0],
                            b.probes.dim[1], b.probes.dim[2]);
            else
                ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (giBaker_.running()) {
        ImGui::ProgressBar(giBaker_.progress(),
                           ImVec2(-FLT_MIN, 0.0f));
        ImGui::TextUnformatted(giBaker_.status().c_str());
        if (ImGui::Button("Cancel", ImVec2(scaled(140.0f), 0)))
            giBaker_.cancel();
    } else {
        ImGui::BeginDisabled(!st.giEnabled);
        if (ImGui::Button("Bake this scene", ImVec2(scaled(140.0f), 0))) {
            saveProject();  // the bake reads the project from disk-backed state
            giBaker_.start(project_, {project_.activeScene});
        }
        ImGui::SameLine();
        if (ImGui::Button("Bake all scenes", ImVec2(scaled(140.0f), 0))) {
            saveProject();
            giBaker_.start(project_, {});
        }
        ImGui::EndDisabled();
        if (st.giEnabled && staleCount > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.30f, 1.0f),
                               "%d scene(s) need a bake", staleCount);
        }
    }
    // The same switch Project Preferences > Build carries, offered where the
    // staleness it answers is on screen. Only stale scenes, so a build with
    // everything fresh costs one signature pass.
    ImGui::BeginDisabled(!st.giEnabled);
    if (ImGui::Checkbox("Re-bake stale scenes before every build",
                        &st.giAutoBake))
        commitChange();
    ImGui::EndDisabled();
    prefHelp("Only the scenes whose cache no longer matches them; a changed big "
             "scene can cost minutes. Also in Project > Preferences > Build.");

    ImGui::Spacing();
    ImGui::SeparatorText("What this does not do");
    // Said out loud in the UI rather than in a changelog - every one of these
    // is a question someone would otherwise file as a bug.
    ImGui::TextWrapped(
        "Nothing here is real time. Moving a crate does not move its light "
        "until the next bake.\n"
        "GI does not buy more dynamic lights: the flashlight and live point "
        "lights are unchanged.\n"
        "Textured surfaces and imported models take their light from the "
        "probe grid, not the lightmap - a flat additive term over a texture "
        "would blow out its dark texels. For a per-PIXEL answer on one of "
        "those, bake the light into its texture instead (Properties > Bake "
        "lighting into texture, docs/prelit-models.md).\n"
        "The editor preview evaluates the probe grid per pixel, so the "
        "console's per-texel contact shadows are sharper than what you see "
        "here.");
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

// The "Baked lighting" tab of the Ambience Editor (drawAmbienceWindow): the
// light that is computed on the HOST and ships as pixels inside textures the
// project already has. It lives beside the GI tab because that window is where
// a scene's light is authored - and separately from it because none of this is
// per scene: model AO is a property of an ASSET.
//
// A list of sections on purpose. Add the next one by appending a call here.
void App::drawBakedLightingSection() {
    drawModelAoSection();
    ImGui::Spacing();
    drawPrelitSection();
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
        modelAoBaker_.start(project_, modelao::paramsOf(st));
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
        auto startBatch = [&](bool onlyStale) {
            std::vector<int> objs;
            for (const PrelitStatus& r : rows) {
                if (!sc.objects[r.index].prelitWanted) continue;
                if (onlyStale && r.prelit && r.fresh) continue;
                objs.push_back(r.index);
            }
            if (objs.empty()) return;
            saveProject();  // the bake reads the models off disk
            litBaker_.start(project_, project_.activeScene, objs,
                            litBakeParams_);
        };
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
    // The same switch Project Preferences > Build carries, offered where the
    // person is looking at the staleness it answers. Project-wide, not per
    // scene - a build compiles every scene.
    if (ImGui::Checkbox("Re-bake stale objects before every build",
                        &project_.settings.prelitAutoBake))
        commitChange();
    prefHelp("Only the stale ones, all scenes; a build with everything fresh "
             "costs nothing. Also in Project > Preferences > Build.");
    ImGui::TextDisabled("Headless: --bake-prelit <projectDir> [scene]");
}

// Tools > Tree Generator: author a low-poly tree procedurally (treegen) with a
// live turntable preview, then bake it into res/models/trees as an ordinary
// Model object. Deterministic in the seed, so the same knobs always give the
// same tree - "5 oak variants" is 5 seeds, not a mesh library.
void App::drawTreeGeneratorWindow() {
    if (!showTreeGenerator_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(880.0f), scaled(560.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Tree Generator", &showTreeGenerator_)) {
        ImGui::End();
        return;
    }
    ImGuiIO& io = ImGui::GetIO();
    treegen::Params& p = treeParams_;
    bool dirty = false;  // a parameter changed this frame

    // ---- left: parameter panel ------------------------------------------
    ImGui::BeginChild("treeparams", ImVec2(scaled(320.0f), 0), true);

    const std::vector<treegen::Preset>& presets = treegen::presets();
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::BeginCombo("Preset", presets[treePreset_].name)) {
        for (int i = 0; i < (int)presets.size(); ++i)
            if (ImGui::Selectable(presets[i].name, treePreset_ == i)) {
                treePreset_ = i;
                const uint32_t keepSeed = p.seed;
                p = presets[i].params;
                p.seed = keepSeed;  // a preset is a shape, not a seed
                dirty = true;
            }
        ImGui::EndCombo();
    }

    // Seed row: editable value + a dice that rolls a fresh one.
    ImGui::SetNextItemWidth(scaled(120.0f));
    int seed = (int)p.seed;
    if (ImGui::InputInt("Seed", &seed)) {
        p.seed = (uint32_t)(seed < 0 ? 0 : seed);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Roll")) {
        // xorshift the current seed for a new-but-reproducible value
        uint32_t s = p.seed ? p.seed : 0x1234567u;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        p.seed = s;
        dirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Roll a new random seed (shape stays, layout varies)");

    if (ImGui::CollapsingHeader("Trunk", ImGuiTreeNodeFlags_DefaultOpen)) {
        dirty |= ImGui::SliderFloat("Height", &p.height, 0.5f, 20.0f, "%.1f");
        // Thickness and leaf size are FRACTIONS of height (shown as percent),
        // so Height scales the whole tree instead of stretching it thinner.
        float thickPct = p.thickness * 100.0f;
        if (ImGui::SliderFloat("Thickness", &thickPct, 0.5f, 15.0f, "%.1f%% of h")) {
            p.thickness = thickPct * 0.01f;
            dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Trunk base radius as a share of height (= %.2f units)",
                              p.thickness * p.height);
        dirty |= ImGui::SliderFloat("Root flare", &p.flare, 1.0f, 3.0f, "%.2f");
        dirty |= ImGui::SliderFloat("Taper", &p.taper, 0.1f, 0.95f, "%.2f");
        dirty |= ImGui::SliderFloat("Gnarliness", &p.gnarliness, 0.0f, 0.5f, "%.2f");
        dirty |= ImGui::SliderFloat("Upward pull", &p.sweep, -0.3f, 0.5f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Branches", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* kCrown[] = {"Spread (broadleaf)", "Conical (conifer)"};
        dirty |= ImGui::Combo("Crown", &p.crown, kCrown, 2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Spread: branches spiral up every parent, the shape emerges.\n"
                "Conical: the trunk runs to the apex and carries whorls of\n"
                "boughs that shorten toward the top - a spruce/fir habit.");
        dirty |= ImGui::SliderInt("Levels", &p.levels, 1, 4);
        if (p.crown == 1) {
            dirty |= ImGui::SliderInt("Whorls", &p.whorls, 2, 20);
            dirty |= ImGui::SliderInt("Per whorl", &p.children[0], 1, 8);
            for (int i = 1; i + 1 < p.levels && i < 3; ++i) {
                ImGui::PushID(i);
                char lbl[32];
                std::snprintf(lbl, sizeof(lbl), "Children L%d", i);
                dirty |= ImGui::SliderInt(lbl, &p.children[i], 0, 12);
                ImGui::PopID();
            }
        } else {
            for (int i = 0; i + 1 < p.levels && i < 3; ++i) {
                ImGui::PushID(i);
                char lbl[32];
                std::snprintf(lbl, sizeof(lbl), "Children L%d", i);
                dirty |= ImGui::SliderInt(lbl, &p.children[i], 0, 12);
                ImGui::PopID();
            }
        }
        dirty |= ImGui::SliderFloat("Branch angle", &p.branchAngle, 5.0f, 90.0f, "%.0f");
        if (p.crown == 1 && ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Conical: the tilt at mid-trunk. Lower whorls droop past it,\n"
                "the ones near the apex sweep up.");
        dirty |= ImGui::SliderFloat("Angle jitter", &p.angleJitter, 0.0f, 30.0f, "%.0f");
        dirty |= ImGui::SliderFloat("Length ratio", &p.lengthRatio, 0.2f, 0.95f, "%.2f");
        if (p.crown != 1)
            dirty |= ImGui::SliderFloat("Length taper", &p.lengthTaper, 0.0f, 0.9f, "%.2f");
        dirty |= ImGui::SliderFloat("Radius ratio", &p.radiusRatio, 0.2f, 0.9f, "%.2f");
        dirty |= ImGui::SliderFloat("Spawn start", &p.spawnStart, 0.0f, 0.8f, "%.2f");
    }

    if (ImGui::CollapsingHeader("Leaves", ImGuiTreeNodeFlags_DefaultOpen)) {
        dirty |= ImGui::SliderInt("Count", &p.leafCount, 0, 600);
        float leafPct = p.leafSize * 100.0f;
        if (ImGui::SliderFloat("Size", &leafPct, 1.0f, 30.0f, "%.1f%% of h")) {
            p.leafSize = leafPct * 0.01f;
            dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Leaf card width as a share of height (= %.2f units)",
                              p.leafSize * p.height);
        dirty |= ImGui::SliderFloat("Aspect", &p.leafAspect, 0.5f, 2.5f, "%.2f");
        dirty |= ImGui::SliderInt("On outer levels", &p.leafLevels, 1, 4);
        const char* kLeaf[] = {"Broadleaf", "Needles", "Single leaf"};
        dirty |= ImGui::Combo("Foliage", &p.leafStyle, kLeaf, 3);
    }

    if (ImGui::CollapsingHeader("Appearance")) {
        const char* kBark[] = {"Rough", "Birch", "Plates"};
        dirty |= ImGui::Combo("Bark", &p.barkStyle, kBark, 3);
        dirty |= ImGui::ColorEdit3("Bark light", p.barkColor,
                                   ImGuiColorEditFlags_NoInputs);
        dirty |= ImGui::ColorEdit3("Bark dark", p.barkColor2,
                                   ImGuiColorEditFlags_NoInputs);
        dirty |= ImGui::ColorEdit3("Leaf light", p.leafColor,
                                   ImGuiColorEditFlags_NoInputs);
        dirty |= ImGui::ColorEdit3("Leaf dark", p.leafColor2,
                                   ImGuiColorEditFlags_NoInputs);
    }

    if (ImGui::CollapsingHeader("Detail (poly budget)")) {
        dirty |= ImGui::SliderInt("Trunk sides", &p.sides, 3, 12);
        dirty |= ImGui::SliderInt("Twig sides", &p.sidesMin, 3, 12);
        dirty |= ImGui::SliderInt("Trunk rings", &p.rings, 1, 12);
        dirty |= ImGui::SliderInt("Twig rings", &p.ringsMin, 1, 12);
    }

    ImGui::EndChild();
    ImGui::SameLine();

    // ---- right: preview + actions ---------------------------------------
    ImGui::BeginGroup();

    // Triangle budget readout - the PS2 stays happy well under a couple
    // thousand tris per tree; warn (not block) past a soft ceiling.
    const int tris = treeMesh_.triangles();
    const int barkT = treeMesh_.barkTriangles();
    const int leafT = treeMesh_.leafTriangles();
    ImVec4 col = tris > 3000   ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f)
                 : tris > 1800 ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                               : ImVec4(0.55f, 0.85f, 0.55f, 1.0f);
    ImGui::TextColored(col, "%d triangles", tris);
    ImGui::SameLine();
    ImGui::TextDisabled("(%d bark + %d leaves)", barkT, leafT);
    if (tris > 3000) {
        ImGui::SameLine();
        ImGui::TextColored(col, "- heavy for PS2, trim detail/leaves");
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float footer = scaled(96.0f);
    const int pw = (int)avail.x < 1 ? 1 : (int)avail.x;
    const int ph = (int)(avail.y - footer) < 1 ? 1 : (int)(avail.y - footer);

    if (treeGenSpin_) {
        treeGenAngle_ += io.DeltaTime * 22.0f;
        if (treeGenAngle_ > 360.0f) treeGenAngle_ -= 360.0f;
    }

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
    desc.radius = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    if (desc.radius < 0.01f) desc.radius = 0.01f;
    desc.angleDeg = treeGenAngle_;
    desc.pitchDeg = treeGenPitch_;
    desc.zoom = treeGenZoom_;
    desc.displayMode = treeGenDisplayMode_;

    const uint32_t tex = viewport_.renderTreePreview(pw, ph, desc);
    if (tex) {
        const ImVec2 imgPos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)pw, (float)ph),
                     ImVec2(0, 1), ImVec2(1, 0));
        ImGui::SetCursorScreenPos(imgPos);
        ImGui::InvisibleButton("##tree_prev_in", ImVec2((float)pw, (float)ph),
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered && io.MouseWheel != 0.0f) {
            treeGenZoom_ *= std::pow(1.15f, io.MouseWheel);
            treeGenZoom_ = treeGenZoom_ < 0.2f ? 0.2f
                           : treeGenZoom_ > 12.0f ? 12.0f
                                                  : treeGenZoom_;
        }
        if (active && (ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1))) {
            treeGenAngle_ += io.MouseDelta.x * 0.5f;
            treeGenPitch_ += io.MouseDelta.y * 0.4f;
            treeGenPitch_ = treeGenPitch_ < -30.0f ? -30.0f
                            : treeGenPitch_ > 85.0f ? 85.0f
                                                    : treeGenPitch_;
            treeGenSpin_ = false;  // grabbing the camera stops the turntable
        }
    }

    // footer: view toggles + name + add-to-scene
    ImGui::Checkbox("Spin", &treeGenSpin_);
    ImGui::SameLine();
    bool wire = treeGenDisplayMode_ == 1;
    if (ImGui::Checkbox("Wireframe", &wire)) treeGenDisplayMode_ = wire ? 1 : 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset view")) {
        treeGenAngle_ = 40.0f;
        treeGenPitch_ = 18.0f;
        treeGenZoom_ = 1.0f;
    }

    ImGui::SetNextItemWidth(scaled(180.0f));
    ImGui::InputText("Name", treeName_, sizeof(treeName_));
    ImGui::SameLine();
    ImGui::BeginDisabled(treeMesh_.bark.empty());
    if (ImGui::Button("Add to scene")) addTreeToScene();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bake .obj + .mtl + textures into res/models/trees\n"
                          "and drop a Model object into the current scene.");
    ImGui::EndGroup();

    if (dirty) treePreviewDirty_ = true;
    if (treePreviewDirty_) rebuildTreePreview();

    ImGui::End();
}

// "Add to scene" from the Tree Generator: bakes the current tree's assets and
// inserts a Model object pointing at them (addModelObject does the naming +
// commit). A name clash reuses a numbered variant so re-adding never clobbers.
void App::addTreeToScene() {
    std::string base = sanitizeAssetName(treeName_);
    if (base.empty()) base = "tree";
    // Distinct asset file name per generated tree so two trees in a project
    // don't share (and overwrite) each other's .obj/textures.
    namespace fs = std::filesystem;
    std::string name = base;
    for (int n = 2;
         fs::exists(fs::path(project_.dir) / "res" / "models" / "trees" /
                    (name + ".obj"));
         ++n)
        name = base + "-" + std::to_string(n);

    std::string objRel, err;
    if (!treegen::writeAssets(project_.dir, name, treeParams_, treeMesh_,
                              treeBarkTex_, treeLeafTex_, &objRel, &err)) {
        statusMessage_ = "Tree export failed: " + err;
        return;
    }
    addModelObject(objRel);        // creates the Model object + commitChange()
    statusMessage_ = "Added tree '" + name + "' (" +
                     std::to_string(treeMesh_.triangles()) + " tris)";
}

// Picks one of the project's Font Manager entries by name. An empty reference
// means the default entry, so this shows fonts[0] rather than a blank.
bool App::fontCombo(std::string& fontRef) {
    bool changed = false;
    const std::string current =
        fontRef.empty() ? project_.defaultFontName() : fontRef;
    ImGui::SetNextItemWidth(scaled(200.0f));
    if (ImGui::BeginCombo("Font", current.c_str())) {
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
        if (ImGui::Combo(label, &cur, names, 8)) {
            dim = vals[cur];
            changed = true;
        }
    };

    ImGui::SeparatorText("Texture (baked for PS2)");
    int sw = 0, sh = 0;
    if (const HudTexture* t = hudTexture(h.imagePath)) { sw = t->w; sh = t->h; }
    if (sw > 0) {
        const bool bad = !isValid(sw) || !isValid(sh);
        if (bad)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                               "Source %dx%d is not a PS2 size", sw, sh);
        else
            ImGui::TextDisabled("Source: %dx%d px", sw, sh);
    }
    ImGui::PushItemWidth(90.0f * uiScaleApplied_);
    dimCombo("Width##texw", h.texW);
    ImGui::SameLine();
    dimCombo("Height##texh", h.texH);
    ImGui::PopItemWidth();

    // Colors: like the per-asset material quality, "(project default)"
    // follows Preferences > Textures; the others override - e.g. keep an
    // important element full color while the rest of the HUD is quantized.
    int q = h.texQuant == "none" ? 1
            : h.texQuant == "8bit" ? 2
            : h.texQuant == "4bit" ? 3
                                   : 0;
    const char* qn[] = {"Project default", "Full color (32-bit)",
                        "256 colors (8-bit)", "16 colors (4-bit)"};
    if (ImGui::Combo("Colors##hudq", &q, qn, 4)) {
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
    ImGui::TextDisabled(
        "Resized at build (source in res/hud stays untouched). The\n"
        "on-screen size above is separate - the sprite is stretched.");
    return changed;
}

// UI Editor window (Tools > UI Editor): everything composited over the 3D
// scene, as one reorderable "screen stack" - the HUD images plus two effect
// layers (bloom+grading, and film grain). The stack order is the game's draw
// order: entries above an effect layer stay crisp (e.g. the crosshair over the
// bloom), entries below are composited with it. Bloom and grain are separate
// entries so, say, bloom can sit under the HUD while grain overlays the whole
// screen.
namespace {
constexpr int kBloomMark = -2;
constexpr int kGrainMark = -3;
// Custom screen effect placements in the stack encode as kFxMarkBase - index
// (index into Project::screenFx). Any entry <= kFxMarkBase is a custom effect;
// bloom/grain (-2/-3) and HUD sprites (>= 0) stay clear of this range.
constexpr int kFxMarkBase = -100;
constexpr bool isFxMark(int e) { return e <= kFxMarkBase; }
constexpr int fxMarkIndex(int e) { return kFxMarkBase - e; }
constexpr int fxMark(int i) { return kFxMarkBase - i; }
}  // namespace

void App::drawUiEditorWindow() {
    if (!showUiEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(560), scaled(420)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UI Editor", &showUiEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    // Belt and braces (the Animation/Loading Screen idiom): a hand-set flag is
    // forgettable by the next widget added here, a section comparison is not.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Hud);
    const int n = (int)project_.hud.size();

    // Render-order stack (bottom of the screen list = drawn first): hud indices
    // plus the two effect markers. A marker at layer L renders right before hud
    // sprite L; layer -1 (or >= n) renders after every sprite (topmost). Bloom
    // before grain when they share a slot (grain composites over the graded,
    // bloomed image - the fixed internal order).
    const int nFx = (int)project_.screenFx.size();
    auto topmost = [&](int L) { return L < 0 || L >= n; };
    auto emitMarkers = [&](std::vector<int>& s, int layer) {
        if (project_.hudBloomLayer == layer) s.push_back(kBloomMark);
        if (project_.hudGrainLayer == layer) s.push_back(kGrainMark);
        for (int fi = 0; fi < nFx; ++fi)
            if (project_.screenFx[fi].layer == layer) s.push_back(fxMark(fi));
    };
    auto buildStack = [&]() {
        std::vector<int> s;
        s.reserve(n + 2 + nFx);
        for (int i = 0; i < n; ++i) {
            emitMarkers(s, i);
            s.push_back(i);
        }
        // Topmost markers (layer -1 or >= n): bloom, grain, then effects in
        // placement order.
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
        int before = 0, bl = -1, gr = -1;
        for (int e : s) {
            if (e == kBloomMark) bl = before;
            else if (e == kGrainMark) gr = before;
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
        for (ScreenFxPlacement& f : newFx)
            if (f.layer >= newN) f.layer = -1;
        project_.hud = std::move(newHud);
        project_.screenFx = std::move(newFx);
    };

    // Display order: top of the screen (drawn last) first = reversed stack.
    std::vector<int> order = buildStack();
    std::reverse(order.begin(), order.end());

    // --- left: the screen stack ---------------------------------------------
    ImGui::BeginChild("##ui_stack", ImVec2(230 * uiScaleApplied_, 0),
                      ImGuiChildFlags_Borders);
    if (ImGui::Button("Import image (PNG)...", ImVec2(-1, 0))) importHudImage();
    ImGui::Checkbox("Show in viewport", &showHudInEditor_);

    // Triggerable on-screen texts. They draw above the whole HUD stack
    // (under menus), so they sit above the reorderable list.
    ImGui::SeparatorText("Texts");
    for (int i = 0; i < (int)project_.hudTexts.size(); ++i) {
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(project_.hudTexts[i].name.c_str(),
                              uiFxSel_ == 4 && selectedText_ == i)) {
            uiFxSel_ = 4;
            selectedText_ = i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add text")) {
        HudText t;
        // unique name: "text", "text-2", ... (referenced by flow nodes)
        int suffix = 1;
        auto taken = [&](const std::string& n) {
            for (const HudText& e : project_.hudTexts)
                if (e.name == n) return true;
            return false;
        };
        while (taken(suffix == 1 ? "text" : "text-" + std::to_string(suffix)))
            ++suffix;
        t.name = suffix == 1 ? "text" : "text-" + std::to_string(suffix);
        project_.hudTexts.push_back(std::move(t));
        uiFxSel_ = 4;
        selectedText_ = (int)project_.hudTexts.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Baked to PNG sprites at build (the PS2 engine has no font).\n"
            "Show/hide them from the flow graph: Show Text / Hide Text.");

    // Inline text icons. They belong to no single element - ANY text in the
    // project can splice one in - so they get their own modal rather than a
    // slot in the screen stack.
    ImGui::SeparatorText("Button icons");
    if (ImGui::SmallButton("Manage icons..."))
        ImGui::OpenPopup("Button icons");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Images any text can splice in with a {{name}} placeholder:\n"
            "\"Press {{cross}} to jump\". {{action:jump}} draws whatever\n"
            "that action is bound to right now. Seeded with the pad buttons.");
    drawTextIconsModal();

    // Custom screen effects loaded from screen-effects/*.screenfx. Effects not
    // yet placed in the stack are offered here with a "+ Add"; management
    // (reload / scaffold / jump) mirrors the Flow Graph "Custom nodes..." menu.
    ImGui::SeparatorText("Screen effects");
    {
        auto isPlaced = [&](const std::string& key) {
            for (const ScreenFxPlacement& f : project_.screenFx)
                if (f.key == key) return true;
            return false;
        };
        auto addToStack = [&](const CustomScreenFx* e) {
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
        int unplaced = 0;
        for (const auto& e : customScreenEffects()) {
            if (isPlaced(e->key)) continue;
            ++unplaced;
            ImGui::PushID(("addfx" + e->key).c_str());
            if (ImGui::SmallButton("+ Add")) addToStack(e.get());
            ImGui::SameLine();
            ImGui::TextUnformatted(e->title.c_str());
            ImGui::PopID();
        }
        if (customScreenEffects().empty())
            ImGui::TextDisabled("None. New starter effect below,\nor drop a "
                                ".screenfx in screen-effects/.");
        else if (unplaced == 0)
            ImGui::TextDisabled("All %d effect(s) placed in the stack.",
                                (int)customScreenEffects().size());

        if (ImGui::SmallButton("Custom effects...")) ImGui::OpenPopup("##customfx");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Low-level full-screen effects (GS blits, like bloom/grain),\n"
                "written in screen-effects/*.screenfx. See\n"
                "docs/custom-screen-effects.md.");
        if (ImGui::BeginPopup("##customfx")) {
            ImGui::TextDisabled("%d loaded from screen-effects/",
                                (int)customScreenEffects().size());
            ImGui::Separator();
            if (ImGui::MenuItem("Reload from folder")) {
                const std::string msg = screenfx::loadForProject(project_.dir);
                // Drop placements whose effect file vanished on reload.
                for (size_t i = project_.screenFx.size(); i-- > 0;)
                    if (!customScreenFx(project_.screenFx[i].key))
                        project_.screenFx.erase(project_.screenFx.begin() + i);
                statusMessage_ = msg.empty()
                                     ? "No custom effects found in screen-effects/"
                                     : msg;
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
            if (ImGui::BeginMenu("Jump to effect file",
                                 !customScreenEffects().empty())) {
                for (const auto& e : customScreenEffects())
                    if (ImGui::MenuItem(e->title.c_str()))
                        openInVSCode(e->sourceFile);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SeparatorText("Screen stack");
    ImGui::TextDisabled("Top entry draws last (on top).\nDrag to reorder.");
    // The USE prompt is part of the screen, but pinned: it always draws
    // above the HUD stack (and under menus), and cannot be deleted.
    if (ImGui::Selectable("[ USE prompt ]", uiFxSel_ == 3)) uiFxSel_ = 3;
    for (int r = 0; r < (int)order.size(); ++r) {
        const int id = order[r];
        ImGui::PushID(r);
        bool isSel;
        std::string label;
        bool dim = false;  // disabled custom effect
        if (id == kBloomMark) {
            isSel = uiFxSel_ == 1;
            label = "[ Bloom + color grading ]";
        } else if (id == kGrainMark) {
            isSel = uiFxSel_ == 2;
            label = "[ Film grain ]";
        } else if (isFxMark(id)) {
            const int fi = fxMarkIndex(id);
            const ScreenFxPlacement& pl = project_.screenFx[fi];
            const CustomScreenFx* e = customScreenFx(pl.key);
            isSel = uiFxSel_ == 5 && selectedFx_ == fi;
            label = "[ FX: " + (e ? e->title : pl.key) + " ]";
            dim = !pl.enabled;
            if (dim) label += "  (off)";
        } else {
            isSel = uiFxSel_ == 0 && selectedHud_ == id;
            label = project_.hud[id].name;
        }
        if (dim) ImGui::PushStyleColor(ImGuiCol_Text,
                                       ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(label.c_str(), isSel)) {
            if (id == kBloomMark) uiFxSel_ = 1;
            else if (id == kGrainMark) uiFxSel_ = 2;
            else if (isFxMark(id)) { uiFxSel_ = 5; selectedFx_ = fxMarkIndex(id); }
            else { uiFxSel_ = 0; selectedHud_ = id; }
        }
        if (dim) ImGui::PopStyleColor();
        // Drag to reorder: swap with the neighbor the cursor moved towards,
        // then rebuild the model from the new order.
        if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
            const int dst = r + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
            if (dst >= 0 && dst < (int)order.size()) {
                // Remember the selected image / effect so its selection
                // survives the reorder (indices shift; identity does not).
                const bool hadHud =
                    uiFxSel_ == 0 && selectedHud_ >= 0 && selectedHud_ < n;
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
                        if (project_.hud[i] == selHud) { selectedHud_ = i; break; }
                if (hadFx)
                    for (int i = 0; i < (int)project_.screenFx.size(); ++i)
                        if (project_.screenFx[i] == selFx) { selectedFx_ = i; break; }
                ImGui::ResetMouseDragDelta();
                changed = true;
            }
        }
        ImGui::PopID();
    }
    // Depth of field is pinned at the very bottom: it composites right after
    // the 3D scene (per-pixel z-tested against scene depth), so it can never
    // sit above a sprite - sprites stamp z = max across their whole rect and
    // would punch sharp rectangles into the blur.
    if (ImGui::Selectable("[ Depth of field ]", uiFxSel_ == 6)) uiFxSel_ = 6;
    // God rays + lens flare are pinned with DoF: both draw right after the
    // 3D scene (rays blur the scene itself, flare sprites sit under the HUD).
    if (ImGui::Selectable("[ God rays ]", uiFxSel_ == 8)) uiFxSel_ = 8;
    if (ImGui::Selectable("[ Lens flare ]", uiFxSel_ == 7)) uiFxSel_ = 7;
    if (project_.hud.empty())
        ImGui::TextDisabled("No HUD images yet.\nImport a PNG above.");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- right: selected entry ------------------------------------------------
    ImGui::BeginChild("##ui_props", ImVec2(0, 0));
    if (uiFxSel_ == 1) {
        ImGui::SeparatorText("Bloom + color grading");
        // The re-add FIX is a whole byte, so bloom can go to 2x - the extra
        // headroom is what a hot glow needs once the threshold has eaten part
        // of the emitter's energy.
        ImGui::SliderFloat("Bloom", &project_.settings.bloom, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How much of the blur is added back. Above 1.0\n"
                              "the blur is over-added - blown-out, hot glow.");
        ImGui::TextDisabled(
            "GS framebuffer trick - no pixel shaders on the PS2. Quarter-res\n"
            "blur re-added over the frame (soft glow).");
        // Bright pass: without it the whole picture glows (soft focus); with
        // it only what is brighter than the cut blooms, which is what makes an
        // emissive material read as a glowing object.
        ImGui::SliderFloat("Threshold", &project_.settings.bloomThreshold, 0.0f,
                           1.0f,
                           project_.settings.bloomThreshold <= 0.0f
                               ? "off - whole frame"
                               : "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Only pixels BRIGHTER than this contribute to the glow (the\n"
                "GS subtracts it from the downsampled frame and clamps at 0).\n"
                "0 = the classic soft-focus bloom over everything. Raise it to\n"
                "~0.6 and the halo collapses onto emissive materials (Material\n"
                "Editor > Glow), the sky and specular hits.");
        ImGui::SliderFloat("Spread", &project_.settings.bloomSpread, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "How far the glow reaches. Extra blur rounds over the\n"
                "quarter-res buffer, each with doubled tap offsets, so the\n"
                "halo grows from a tight fringe to a wide corona. Costs 4\n"
                "extra GS sprites per round and no EE time - but a very wide\n"
                "glow over a busy frame reads as haze, so tune it with the\n"
                "threshold.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Stack entries above this layer draw crisp on top of the bloom - "
            "put the crosshair or text there so the glow does not blur them. "
            "At the very top the bloom applies at the end of the frame, over "
            "everything including menus.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Color grading applies with this layer. Author presets in\n"
            "Tools > Color Grading. Per-scene bloom strength: Scene > Scene\n"
            "Preferences > Post effects.");
    } else if (uiFxSel_ == 2) {
        ImGui::SeparatorText("Film grain");
        ImGui::SliderFloat("Film grain", &project_.settings.grain, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "Animated noise overlay (GS blits). Subtle values work best.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "As a separate layer the grain can sit above the bloom and the "
            "HUD - a filmic overlay over the whole screen - while the bloom "
            "stays underneath so it does not smear the UI.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Per-scene grain strength: Scene > Scene Preferences > Post "
            "effects.");
    } else if (uiFxSel_ == 6) {
        ImGui::SeparatorText("Depth of field");
        ImGui::SliderFloat("Amount", &project_.settings.dofAmount, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Focus", &project_.settings.dofFocus, 0.5f, 0.5f,
                         500.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Range", &project_.settings.dofRange, 0.5f, 0.1f,
                         500.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "The image stays sharp up to Focus (world units from the\n"
            "camera) and blurs progressively, reaching the full Amount\n"
            "blur at Focus + Range. Amount 0 = off.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Pinned under the whole stack: the blur follows real scene depth "
            "per pixel (z-tested GS blits), and sprites stamp z across their "
            "full rect - compositing DoF above them would punch sharp "
            "rectangles into the blur. Every HUD entry always draws crisp.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Per-scene values: Scene > Scene Preferences > Post effects.\n"
            "Runtime: the Set Depth Of Field flow node overrides these\n"
            "(and can restore them with its Scene setting mode).");
    } else if (uiFxSel_ == 7) {
        ImGui::SeparatorText("Sun lens flare");
        ImGui::SliderFloat("Brightness", &project_.settings.flare, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "Additive sprites along the sun-to-center axis, tinted by the\n"
            "scene light color. The sun sits infinitely far along the\n"
            "Lighting direction (Preferences > Lighting). 0 = off.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Occlusion is a single ray cast against objects and terrain per "
            "frame - the flare fades out when the sun hides behind geometry "
            "and eases back in the open. Draws under the whole HUD stack.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Per-scene brightness: Scene > Scene Preferences > Post effects.\n"
            "Runtime: the Set Lens Flare flow node.");
    } else if (uiFxSel_ == 8) {
        ImGui::SeparatorText("God rays (light shafts)");
        ImGui::SliderFloat("Strength", &project_.settings.godRays, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "Bright parts of the frame streak toward the sun's screen\n"
            "position (radial-blur GS blits, reusing the bloom chain) and\n"
            "composite back additively. 0 = off.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Strongest when the sun is on screen or just off the edge - sky "
            "seen through trees, doorways or fog lights up in shafts. Costs "
            "a few quarter-res blits per frame, only when enabled.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Per-scene strength: Scene > Scene Preferences > Post effects.\n"
            "Runtime: the Set God Rays flow node.");
    } else if (uiFxSel_ == 3) {
        // --- the pinned USE prompt ------------------------------------------
        HudImage& h = project_.usePrompt;
        ImGui::SeparatorText("Interaction prompts");
        ImGui::TextWrapped(
            "Shown while the player looks at a usable object up close - the "
            "\"PICK UP\" variant instead when the object is pickable. Both draw "
            "above the HUD stack (and under menus) at the same screen position, "
            "and neither can be deleted.");
        ImGui::Spacing();
        ImGui::DragFloat2("Position##use", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shared by both prompts (normalized, center "
                              "anchor).");

        // One prompt's content: TEXT or IMAGE as an explicit choice, so flipping
        // to the image to compare does not throw away the text you typed. Text
        // can carry a button glyph that follows the binding, which is why a
        // fresh project starts there (docs/text-icons.md).
        auto promptUi = [&](const char* title, const char* id, bool& isText,
                            HudText& txt, std::string& imagePath,
                            HudImage* bakeOf, const char* builtinNote) {
            ImGui::PushID(id);
            ImGui::SeparatorText(title);
            int mode = isText ? 0 : 1;
            if (ImGui::RadioButton("Text", &mode, 0)) {
                isText = true;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Image", &mode, 1)) {
                isText = false;
                changed = true;
            }

            if (isText) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", txt.text.c_str());
                ImGui::SetNextItemWidth(scaled(240.0f));
                if (ImGui::InputText("Text##prompt", buf, sizeof(buf)))
                    txt.text = buf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                changed |= textTokenPicker("prompttok", txt.text);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Baked to a sprite at build (the PS2 has no font).\n"
                        "{{use}} draws the glyph of whatever the \"use\" action\n"
                        "is bound to, so the prompt follows a rebind;\n"
                        "{{cross}} pins a fixed button. See docs/text-icons.md.");
                ImGui::SetNextItemWidth(scaled(110.0f));
                if (ImGui::DragInt("Size##prompt", &txt.size, 0.5f, 8, 48,
                                   "%d px"))
                    changed = true;
                ImGui::SameLine();
                if (ImGui::ColorEdit3("Color##prompt", txt.color,
                                      ImGuiColorEditFlags_NoInputs))
                    changed = true;
                ImGui::SameLine();
                if (ImGui::Checkbox("Shadow##prompt", &txt.shadow))
                    changed = true;
                changed |= fontCombo(txt.font);
                if (txt.text.empty())
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                                       "Empty - the image is used instead.");
                else if (const HudTexture* t = hudTextTexture(txt))
                    ImGui::Image((ImTextureID)(intptr_t)t->tex,
                                 ImVec2(scaled((float)t->w),
                                        scaled((float)t->h)));
                ImGui::TextDisabled("The baked text sizes the sprite.");
            } else {
                if (imagePath.empty()) {
                    ImGui::TextDisabled("%s", builtinNote);
                } else {
                    ImGui::TextDisabled("Custom: %s", imagePath.c_str());
                    if (const HudTexture* t = hudTexture(imagePath))
                        ImGui::Image((ImTextureID)(intptr_t)t->tex,
                                     ImVec2(scaled((float)t->w),
                                            scaled((float)t->h)));
                }
                if (ImGui::Button(imagePath.empty() ? "Custom image (PNG)..."
                                                    : "Replace image (PNG)...")) {
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
                            std::filesystem::copy_options::overwrite_existing,
                            ec);
                        if (!ec) {
                            imagePath = "res/hud/" + fileName;
                            hudTexCache_.erase(imagePath);  // reload if replaced
                            changed = true;
                        } else {
                            statusMessage_ =
                                "Prompt image import failed: " + ec.message();
                        }
                    }
                }
                if (!imagePath.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Reset to built-in")) {
                        imagePath.clear();
                        changed = true;
                    }
                }
                // Size + the texture bake only apply to the USE prompt's own
                // HudImage; the PICK UP image rides on the same box.
                if (bakeOf) {
                    ImGui::DragFloat2("Size (px)##prompt", bakeOf->size, 1.0f,
                                      1.0f, 512.0f, "%.0f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    if (!imagePath.empty()) changed |= hudBakeControls(*bakeOf);
                } else {
                    ImGui::TextDisabled("Drawn in the USE prompt's box.");
                }
            }
            ImGui::PopID();
        };

        promptUi("USE prompt", "useprompt", project_.usePromptIsText,
                 project_.usePromptText, h.imagePath, &h,
                 "Built-in \"USE\" sprite (res/hud/use.png).");
        promptUi("PICK UP prompt", "pickprompt", project_.pickPromptIsText,
                 project_.pickPromptText, project_.pickPromptImage, nullptr,
                 "Built-in \"PICK UP\" sprite (res/hud/pickup.png).");
    } else if (uiFxSel_ == 4 && selectedText_ >= 0 &&
               selectedText_ < (int)project_.hudTexts.size()) {
        // --- a triggerable on-screen text -----------------------------------
        HudText& t = project_.hudTexts[selectedText_];
        ImGui::SeparatorText(t.name.c_str());
        {
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", t.name.c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                // Renames follow into the flow graphs (Show/Hide Text nodes
                // reference texts by name), like layer renames do.
                const std::string oldName = t.name;
                t.name = nameBuf;
                for (SceneData& sc : project_.scenes)
                    for (SceneObject& o : sc.objects)
                        for (FlowNode& fn : o.flowGraph.nodes) {
                            const FlowNodeType* ft = flowNodeType(fn.type);
                            if (ft && ft->strKind == FlowParamKind::HudTextName &&
                                fn.str == oldName)
                                fn.str = t.name;
                        }
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        {
            // multiline: '\n' becomes a new line in the baked sprite
            char textBuf[512];
            std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
            if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf),
                                          ImVec2(-1.0f, 80.0f * uiScaleApplied_)))
                t.text = textBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            changed |= textTokenPicker("hudtexttok", t.text);
        }
        changed |= fontCombo(t.font);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Font size", &t.size, 0.2f, 8, 48, "%d px"))
            t.size = t.size < 8 ? 8 : t.size > 48 ? 48 : t.size;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::ColorEdit3("Color##text", t.color,
                              ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::DragFloat2("Position##text", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Drop shadow", &t.shadow)) changed = true;
        if (ImGui::Checkbox("Visible at game start", &t.visibleAtStart))
            changed = true;
        ImGui::TextDisabled(
            "Show/hide from the flow graph: HUD > Set Text Visible\n"
            "(the show pin takes an optional auto-hide after N\n"
            "seconds). This string is baked at build - for one that\n"
            "changes while the game runs, use a Display Text node.");

        // Live preview: the exact sprite the build will bake.
        {
            std::string key = t.name + "\x1f" + t.text + "\x1f" + t.font +
                              "\x1f" + std::to_string(t.size) + "\x1f" +
                              std::to_string(t.shadow) + "\x1f" +
                              std::to_string(t.color[0]) + "," +
                              std::to_string(t.color[1]) + "," +
                              std::to_string(t.color[2]);
            if (key != textPreviewKey_) {
                std::vector<unsigned char> rgba;
                int w = 0, h = 0;
                if (menubake::bakeTextRGBA(t, project_, rgba, w, h)) {
                    if (!textPreviewTex_) glGenTextures(1, &textPreviewTex_);
                    glBindTexture(GL_TEXTURE_2D, textPreviewTex_);
                    glUploadTexRgba(w, h, rgba.data());
                    textPreviewW_ = w;
                    textPreviewH_ = h;
                }
                textPreviewKey_ = key;
            }
            if (textPreviewTex_) {
                ImGui::SeparatorText("Preview");
                ImGui::Image((ImTextureID)(intptr_t)textPreviewTex_,
                             ImVec2((float)textPreviewW_, (float)textPreviewH_));
                ImGui::TextDisabled("Texture: %dx%d px", textPreviewW_,
                                    textPreviewH_);
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Delete text")) {
            project_.hudTexts.erase(project_.hudTexts.begin() + selectedText_);
            selectedText_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (uiFxSel_ == 5 && selectedFx_ >= 0 &&
               selectedFx_ < (int)project_.screenFx.size()) {
        // --- a custom screen effect placement -------------------------------
        ScreenFxPlacement& pl = project_.screenFx[selectedFx_];
        const CustomScreenFx* e = customScreenFx(pl.key);
        ImGui::SeparatorText(e ? e->title.c_str() : pl.key.c_str());
        if (!e) {
            ImGui::TextWrapped(
                "The effect file for '%s' is missing from screen-effects/. "
                "Restore it (then Reload), or remove this placement.",
                pl.key.c_str());
        } else {
            if (ImGui::Checkbox("Enabled", &pl.enabled)) changed = true;
            ImGui::TextDisabled(
                "Low-level GS effect (screen-effects/%s). No pixel shaders on\n"
                "the PS2 - not previewed in the editor viewport; build to see it.",
                (std::filesystem::path(e->sourceFile).filename().string()).c_str());
            ImGui::Spacing();
            if (e->paramCount == 0) {
                ImGui::TextDisabled("This effect has no parameters.");
            } else {
                for (int i = 0; i < e->paramCount; ++i) {
                    ImGui::SetNextItemWidth(scaled(200));
                    if (ImGui::SliderFloat(e->paramLabel[i].c_str(), &pl.params[i],
                                           e->paramMin[i], e->paramMax[i], "%.3f"))
                        pl.params[i] = pl.params[i] < e->paramMin[i]
                                           ? e->paramMin[i]
                                           : pl.params[i] > e->paramMax[i]
                                                 ? e->paramMax[i]
                                                 : pl.params[i];
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Stack entries above this layer draw crisp on top of the "
                "effect; entries below are composited with it. Drag it in the "
                "stack to move it (e.g. under the HUD, or over everything).");
            ImGui::Spacing();
            if (ImGui::Button("Jump to effect file"))
                openInVSCode(e->sourceFile);
        }
        ImGui::Spacing();
        if (ImGui::Button("Remove from stack")) {
            project_.screenFx.erase(project_.screenFx.begin() + selectedFx_);
            selectedFx_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (selectedHud_ >= 0 && selectedHud_ < n) {
        HudImage& h = project_.hud[selectedHud_];
        ImGui::SeparatorText(h.name.c_str());
        ImGui::DragFloat2("Position##hud", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Size (px)##hud", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        changed |= hudBakeControls(h);

        ImGui::Spacing();
        if (ImGui::Button("Delete HUD image"))
            requestAssetDelete(PendingAssetDelete::Hud, h.imagePath, h.name,
                               selectedHud_);
    } else {
        ImGui::TextDisabled("Select an entry on the left.");
    }
    ImGui::EndChild();

    // UI edits are not on the undo stack, but they ARE unsaved work: mark the
    // project instead of writing to disk behind the user's back, so the save
    // icon lights up and closing asks. commitChange() rather than a bare
    // setDirty() because that is the ONE verb for a model edit (app.hpp) -
    // for project-wide data it pushes no undo step, so nothing is spammed.
    if (changed || project::sectionJson(project_, project::Section::Hud) != beforeSection)
        commitChange();
    ImGui::End();
}

// Property editor for one progress bar (Loading Screens). Returns true when a
// setting changed (the caller saves).
bool App::loadingBarControls(LoadingBar& b) {
    bool changed = false;
    const char* kinds[] = {"Continuous (fill)", "Quantized (segments)"};
    ImGui::SetNextItemWidth(scaled(200));
    if (ImGui::Combo("Type", &b.kind, kinds, 2)) changed = true;
    ImGui::DragFloat2("Position##bar", b.pos, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat2("Size (px)##bar", b.size, 1.0f, 1.0f, 512.0f, "%.0f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::ColorEdit3("Track / off color", b.bgColor)) changed = true;
    if (ImGui::ColorEdit3("Fill / on color", b.fillColor)) changed = true;
    if (b.kind == 1) {
        ImGui::SetNextItemWidth(scaled(120));
        if (ImGui::DragInt("Segments", &b.segments, 0.1f, 2, 16))
            b.segments = b.segments < 2 ? 2 : b.segments > 16 ? 16 : b.segments;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(scaled(120));
        ImGui::DragFloat("Spacing (px)", &b.spacing, 0.2f, 0.0f, 64.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SeparatorText("Segment image (optional)");
        if (b.segImage.imagePath.empty()) {
            ImGui::TextDisabled("Colored rectangles (no texture).");
            if (ImGui::Button("Set segment image (PNG)...")) {
                std::vector<HudImage> tmp;
                const int i = importHudImageInto(tmp);
                if (i >= 0) {
                    b.segImage.imagePath = tmp[i].imagePath;
                    changed = true;
                }
            }
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
            if (ImGui::Button("Clear image")) {
                b.segImage.imagePath.clear();
                changed = true;
            }
            changed |= hudBakeControls(b.segImage);
        }
        ImGui::TextDisabled(
            "Lit segments are tinted with the on color, unlit with the\n"
            "off color (color modulation of one texture).");
    }
    return changed;
}

// Draws the loading screen into the current window at 512x448 aspect, honoring
// `fraction` for the progress bars (matches loadingscreen::renderFrame on PS2).
void App::drawLoadingPreview(const LoadingScreenDef& ls, float fraction) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 20.0f || avail.y < 20.0f) return;
    const float aspect = 512.0f / 448.0f;
    float w = avail.x, h = w / aspect;
    if (h > avail.y) { h = avail.y; w = h * aspect; }
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += (avail.x - w) * 0.5f;
    const ImVec2 p1(p0.x + w, p0.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto col = [](const float* c, float a = 1.0f) {
        return IM_COL32((int)(c[0] * 255.0f + 0.5f), (int)(c[1] * 255.0f + 0.5f),
                        (int)(c[2] * 255.0f + 0.5f), (int)(a * 255.0f + 0.5f));
    };
    dl->AddRectFilled(p0, p1, col(ls.bgColor));
    // Screen-space size of a value given in 512x448 pixels.
    auto sw = [&](float px) { return px / 512.0f * w; };
    auto sh = [&](float px) { return px / 448.0f * h; };
    auto cx = [&](float nx) { return p0.x + nx * w; };
    auto cy = [&](float ny) { return p0.y + ny * h; };

    for (int i = 0; i < (int)ls.images.size(); ++i) {
        const HudImage& im = ls.images[i];
        const float dw = sw(im.size[0]), dh = sh(im.size[1]);
        const ImVec2 a(cx(im.pos[0]) - dw * 0.5f, cy(im.pos[1]) - dh * 0.5f);
        const ImVec2 b(a.x + dw, a.y + dh);
        if (const HudTexture* t = hudTexture(im.imagePath))
            dl->AddImage((ImTextureID)(intptr_t)t->tex, a, b);
        else
            dl->AddRect(a, b, IM_COL32(255, 80, 80, 255));
        if (showLoadingEditor_ && lsSelKind_ == 0 && lsSelIdx_ == i)
            dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1),
                        IM_COL32(80, 200, 255, 255));
    }
    for (int i = 0; i < (int)ls.texts.size(); ++i) {
        const HudText& t = ls.texts[i];
        HudText tc = t;  // mangle the cache key so it never collides with HUD texts
        tc.name = "lsprev\x1f" + ls.name + "\x1f" + t.name;
        if (const HudTexture* tex = hudTextTexture(tc)) {
            const float dw = sw((float)tex->w), dh = sh((float)tex->h);
            const ImVec2 a(cx(t.pos[0]) - dw * 0.5f, cy(t.pos[1]) - dh * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex->tex, a,
                         ImVec2(a.x + dw, a.y + dh));
            if (showLoadingEditor_ && lsSelKind_ == 1 && lsSelIdx_ == i)
                dl->AddRect(ImVec2(a.x - 1, a.y - 1),
                            ImVec2(a.x + dw + 1, a.y + dh + 1),
                            IM_COL32(80, 200, 255, 255));
        }
    }
    for (int i = 0; i < (int)ls.bars.size(); ++i) {
        const LoadingBar& b = ls.bars[i];
        const float bw = sw(b.size[0]), bh = sh(b.size[1]);
        const float bx = cx(b.pos[0]) - bw * 0.5f;
        const float by = cy(b.pos[1]) - bh * 0.5f;
        if (b.kind == 0) {
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                              col(b.bgColor));
            if (fraction > 0.0f)
                dl->AddRectFilled(ImVec2(bx, by),
                                  ImVec2(bx + bw * fraction, by + bh),
                                  col(b.fillColor));
        } else {
            const int segs = b.segments < 1 ? 1 : b.segments;
            const int lit = (int)(fraction * segs + 0.001f);
            const float segW = (bw - sw(b.spacing) * (segs - 1)) / segs;
            for (int k = 0; k < segs; ++k) {
                const float sx = bx + k * (segW + sw(b.spacing));
                const float* c = (k < lit) ? b.fillColor : b.bgColor;
                dl->AddRectFilled(ImVec2(sx, by), ImVec2(sx + segW, by + bh),
                                  col(c));
            }
        }
        if (showLoadingEditor_ && lsSelKind_ == 2 && lsSelIdx_ == i)
            dl->AddRect(ImVec2(bx - 1, by - 1), ImVec2(bx + bw + 1, by + bh + 1),
                        IM_COL32(80, 200, 255, 255));
    }
    dl->AddRect(p0, p1, IM_COL32(120, 120, 120, 255));
    ImGui::Dummy(ImVec2(avail.x, h));
}

// Loading Screens (Tools > Loading Screens): named loading screens shown while
// a scene loads. Each has a background color, image + text elements (baked like
// the HUD) and progress bars (continuous or quantized). Scenes pick one in
// Scene > Preferences; one can be the project default. Like the other preset
// collections these live outside undo - commitChange() therefore pushes no
// undo step here, it marks the project dirty and advances the session serial,
// and the bytes reach disk on the next ordinary save (see app.hpp).
// Boot splash screens: a collapsing section at the top of the Loading Screens
// window. Images shown in order at startup (after the Tyra logo, before the
// loading screen), each for its own duration. Self-contained (balanced
// Begin/EndChild) so the caller's later early-returns stay valid.
bool App::drawSplashSection() {
    if (!ImGui::CollapsingHeader("Boot splash screens")) return false;
    bool changed = false;
    auto& splashes = project_.splashScreens;
    if (selectedSplash_ >= (int)splashes.size()) selectedSplash_ = -1;

    ImGui::TextDisabled("Images shown at startup, in order, before the loading screen.");

    ImGui::BeginChild("##splash_body", ImVec2(0, scaled(190)),
                      ImGuiChildFlags_Borders);

    // --- left: splash list -------------------------------------------------
    ImGui::BeginChild("##splash_list", ImVec2(scaled(210), 0),
                      ImGuiChildFlags_Borders);
    if (ImGui::Button("+ Add splash (PNG)...", ImVec2(-1, 0))) {
        std::vector<HudImage> tmp;
        const int i = importHudImageInto(tmp);
        if (i >= 0) {
            SplashScreen s;
            s.name = tmp[i].name.empty() ? "splash" : tmp[i].name;
            s.image = tmp[i];
            s.image.pos[0] = 0.5f;
            s.image.pos[1] = 0.5f;
            s.image.size[0] = 512.0f;  // default fullscreen stretch
            s.image.size[1] = 448.0f;
            splashes.push_back(std::move(s));
            selectedSplash_ = (int)splashes.size() - 1;
            changed = true;
        }
    }
    ImGui::Separator();
    for (int i = 0; i < (int)splashes.size(); ++i) {
        ImGui::PushID(i);
        char label[96];
        std::snprintf(label, sizeof(label), "%d. %s  (%.1fs)", i + 1,
                      splashes[i].name.c_str(), splashes[i].duration);
        if (ImGui::Selectable(label, selectedSplash_ == i)) selectedSplash_ = i;
        ImGui::PopID();
    }
    if (splashes.empty())
        ImGui::TextDisabled("No splash screens.\nAdd one above.");
    ImGui::EndChild();
    ImGui::SameLine();

    // --- right: selected splash properties ---------------------------------
    ImGui::BeginChild("##splash_props", ImVec2(0, 0));
    if (selectedSplash_ >= 0 && selectedSplash_ < (int)splashes.size()) {
        SplashScreen& s = splashes[selectedSplash_];
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::InputText("Name##splash", nameBuf, sizeof(nameBuf)))
            s.name = nameBuf;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::DragFloat("Duration (s)", &s.duration, 0.05f, 0.1f, 10.0f, "%.1f"))
            s.duration =
                s.duration < 0.1f ? 0.1f : (s.duration > 10.0f ? 10.0f : s.duration);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::ColorEdit3("Background##splash", s.bgColor,
                              ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::DragFloat2("Size (px)##splash", s.image.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Position##splash", s.image.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        changed |= hudBakeControls(s.image);

        // Structural actions last (they invalidate `s`), each returns cleanly.
        ImGui::Spacing();
        ImGui::BeginDisabled(selectedSplash_ == 0);
        if (ImGui::SmallButton("Move up")) {
            std::swap(splashes[selectedSplash_], splashes[selectedSplash_ - 1]);
            --selectedSplash_;
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::EndChild();
            return true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedSplash_ >= (int)splashes.size() - 1);
        if (ImGui::SmallButton("Move down")) {
            std::swap(splashes[selectedSplash_], splashes[selectedSplash_ + 1]);
            ++selectedSplash_;
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::EndChild();
            return true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Change image...")) {
            std::vector<HudImage> tmp;
            const int i = importHudImageInto(tmp);
            if (i >= 0) {
                s.image.imagePath = tmp[i].imagePath;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            splashes.erase(splashes.begin() + selectedSplash_);
            selectedSplash_ = -1;
            ImGui::EndChild();
            ImGui::EndChild();
            return true;
        }
    } else {
        ImGui::TextDisabled("Select a splash on the left, or add one.");
    }
    ImGui::EndChild();  // props
    ImGui::EndChild();  // body
    return changed;
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
    }
    // Replay the stored pairs through the hygiene rule, so duplicates from
    // sessions before it existed collapse to the LATEST intent.
    animMapPairs_.clear();
    for (const auto& [from, to] : a.boneMap)
        upsertPair(animMapPairs_, from, to);
    animMapSelDonor_ = -1;
    animMapRow_ = importRow;
    ImGui::SetWindowFocus("Map bones");
}

bool App::drawAnimBoneMapWindow() {
    if (animMapRow_ < 0 || !hasProject_) return false;
    if (animMapRow_ >= (int)project_.animImports.size()) {
        animMapRow_ = -1;
        return false;
    }
    AnimImport& row = project_.animImports[(size_t)animMapRow_];

    bool openFlag = true;
    ImGui::SetNextWindowSize(ImVec2(scaled(760), scaled(560)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map bones", &openFlag)) {
        ImGui::End();
        if (!openFlag) animMapRow_ = -1;
        return false;
    }
    if (!animMapParsed_) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                           "A file would not parse.");
        ImGui::End();
        if (!openFlag) animMapRow_ = -1;
        return false;
    }

    const glbparser::Skel& tgt = animMapTarget_;
    const glbparser::Skel& don = animMapDonor_;

    // Per-frame state derived from the staged pairs. Cheap (name lookups over
    // <100 bones), so recomputing beats caching + invalidation here. The
    // row's own switches ride along, so the test pose shows what the BAKE
    // will do with them (facing, mirror, lean - it used to ignore them).
    animmerge::MergeOptions opts;
    opts.boneMap = animMapPairs_;
    opts.facingOverride = row.facing;
    opts.mirror = row.mirror;
    opts.tuneLean = row.lean;
    opts.translation = row.translation == 2
                           ? animmerge::TranslationMode::CopyAll
                       : row.translation == 1
                           ? animmerge::TranslationMode::AnimatedOnly
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
        animMapAffixOk_ = animmerge::detectAffixRule(tgt, don, opts,
                                                     animMapAffix_);
        // AI proposals for donors that got resolved meanwhile drop out.
        animMapAiSugg_.erase(
            std::remove_if(animMapAiSugg_.begin(), animMapAiSugg_.end(),
                           [&](const auto& pr) {
                               return animmerge::resolveBoneName(
                                          tgt, pr.first, opts) >= 0;
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

    // --- header: the numbers and the verbs, one line ------------------------
    ImGui::Text("%d/%d bones", mapped, (int)donorBones.size());
    ImGui::SameLine();
    if (!animMapSugg_.empty()) {
        char lbl[48];
        snprintf(lbl, sizeof lbl, "Accept %d suggestion%s",
                 (int)animMapSugg_.size(),
                 animMapSugg_.size() == 1 ? "" : "s");
        if (ImGui::SmallButton(lbl))
            for (const animmerge::BoneSuggestion& sg : animMapSugg_)
                upsertPair(animMapPairs_, sg.donor, sg.target);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turn the amber guesses into real pairs.");
        ImGui::SameLine();
    }
    if (!animMapPairs_.empty()) {
        if (ImGui::SmallButton("Clear pairs")) animMapPairs_.clear();
        ImGui::SameLine();
    }
    if (animMapAffixOk_) {
        char lbl[96];
        snprintf(lbl, sizeof lbl, "Apply rule: %s (%d)",
                 animMapAffix_.describe().c_str(), animMapAffix_.matches);
        if (ImGui::SmallButton(lbl))
            for (const auto& pr :
                 animmerge::applyAffixRule(tgt, don, opts, animMapAffix_))
                upsertPair(animMapPairs_, pr.first, pr.second);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "One rename rule covers these bones - the two rigs are\n"
                "identical modulo this prefix/suffix.");
        ImGui::SameLine();
    }
    {
        const animmerge::RetargetInfo ri =
            animmerge::retargetInfo(tgt, don, opts);
        ImGui::TextDisabled(ri.full ? "full retarget (%.0f deg)" : "copy",
                            ri.bindGapDeg);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                ri.full ? "Bind poses differ - clips resample through the\n"
                          "retargeter; the test pose shows exactly that."
                        : "Identical binds - channels copy verbatim.");
        ImGui::SameLine();
    }
    // Legend - the canvas colors, named once, tersely.
    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "matched");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "suggested");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "unmatched");
    ImGui::SameLine();
    ImGui::TextDisabled("- click a joint, then its match on the right");

    // The list pane's hover from LAST frame - the canvas draws before the
    // list is laid out, so it highlights with one frame of lag.
    const int hiD = animMapHiD_, hiT = animMapHiT_;
    animMapHiD_ = animMapHiT_ = -1;

    // --- row 2: the test pose and the AI assist ----------------------------
    ImGui::SetNextItemWidth(scaled(170));
    const char* poseLbl = animMapPoseClip_ >= 0 &&
                                  animMapPoseClip_ < (int)don.clips.size()
                              ? don.clips[(size_t)animMapPoseClip_].name.c_str()
                              : "Test pose: off";
    if (ImGui::BeginCombo("##posec", poseLbl)) {
        if (ImGui::Selectable("off", animMapPoseClip_ < 0))
            animMapPoseClip_ = -1;
        for (size_t c = 0; c < don.clips.size(); ++c)
            if (ImGui::Selectable(don.clips[c].name.c_str(),
                                  (int)c == animMapPoseClip_)) {
                animMapPoseClip_ = (int)c;
                animMapPoseT_ = 0.0f;
            }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Pose both rigs through the current mapping. A wrong pair\n"
            "shows as a folded limb long before any percentage does.");
    const bool poseOn =
        animMapPoseClip_ >= 0 && animMapPoseClip_ < (int)don.clips.size();
    if (poseOn) {
        const float dur =
            don.clips[(size_t)animMapPoseClip_].duration;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(140));
        ImGui::SliderFloat("##poset", &animMapPoseT_, 0.0f,
                           dur > 0.01f ? dur : 0.01f, "%.2fs");
        ImGui::SameLine();
        ImGui::Checkbox("Play", &animMapPosePlay_);
        if (animMapPosePlay_ && dur > 0.01f) {
            animMapPoseT_ += ImGui::GetIO().DeltaTime;
            while (animMapPoseT_ > dur) animMapPoseT_ -= dur;
        }
        animmerge::posedPreview(tgt, don, opts, animMapPoseClip_,
                                animMapPoseT_, animMapDPosed_, animMapTPosed_);
    }
    ImGui::SameLine();
    if (animMapAiGen_ && animMapAiGen_->busy()) {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "%c AI...",
                           "|/-\\"[(int)(ImGui::GetTime() * 8.0) & 3]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) animMapAiGen_->cancel();
    } else {
        if (ImGui::SmallButton("Ask AI")) {
            animMapAiErr_.clear();
            animMapAiGen_ = std::make_unique<aigen::Generator>();
            animMapAiGen_->start(
                globalAi_, animmerge::aiMapPrompt(tgt, don, opts),
                "Map the bones now. Reply with the JSON only.");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Send both skeletons (hierarchy + bind positions) to the\n"
                "AI backend from Edit > Preferences. Proposals land below\n"
                "for you to accept - nothing is applied on its own.");
    }
    if (animMapAiGen_ && !animMapAiGen_->busy()) {
        if (animMapAiGen_->state() == aigen::Generator::State::Success) {
            animMapAiSugg_ =
                animmerge::parseAiBoneMap(animMapAiGen_->reply(), tgt, don);
            animMapAiSugg_.erase(
                std::remove_if(animMapAiSugg_.begin(), animMapAiSugg_.end(),
                               [&](const auto& pr) {
                                   return animmerge::resolveBoneName(
                                              tgt, pr.first, opts) >= 0;
                               }),
                animMapAiSugg_.end());
            if (animMapAiSugg_.empty())
                animMapAiErr_ = "AI proposed nothing usable.";
        } else if (animMapAiGen_->state() == aigen::Generator::State::Failed) {
            animMapAiErr_ = animMapAiGen_->error();
        }
        animMapAiGen_.reset();
    }
    if (!animMapAiErr_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", animMapAiErr_.c_str());
    }

    // --- the canvas ---------------------------------------------------------
    const float footerH = ImGui::GetFrameHeightWithSpacing() + scaled(8);
    const float listW = scaled(230);
    ImGui::BeginChild("##bonecanvas", ImVec2(-listW, -footerH),
                      ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cMin = ImGui::GetCursorScreenPos();
    const ImVec2 cSize = ImGui::GetContentRegionAvail();
    // The child must claim its input region or clicks fall through to
    // whatever is under the canvas.
    ImGui::InvisibleButton("##bonehit", ImVec2(cSize.x > 1 ? cSize.x : 1,
                                               cSize.y > 1 ? cSize.y : 1));
    const bool canvasHover = ImGui::IsItemHovered();
    // The canvas owns the wheel while hovered - without this the same wheel
    // that zooms the skeletons also scrolled the window around them.
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    // View input first, so this frame already draws the moved view: wheel
    // zooms around the cursor, middle-drag pans (the viewport's gestures).
    const ImVec2 cCenter(cMin.x + cSize.x * 0.5f, cMin.y + cSize.y * 0.5f);
    if (canvasHover) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const float oldZoom = animMapZoom_;
            animMapZoom_ = std::clamp(animMapZoom_ * std::pow(1.15f, wheel),
                                      0.5f, 12.0f);
            const float k = animMapZoom_ / oldZoom;
            animMapPanX_ = mouse.x - cCenter.x - (mouse.x - cCenter.x - animMapPanX_) * k;
            animMapPanY_ = mouse.y - cCenter.y - (mouse.y - cCenter.y - animMapPanY_) * k;
        }
    }
    if (canvasHover && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        const ImVec2 d = ImGui::GetIO().MouseDelta;
        animMapPanX_ += d.x;
        animMapPanY_ += d.y;
    }
    if (canvasHover && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle)) {
        animMapZoom_ = 1.0f;
        animMapPanX_ = animMapPanY_ = 0.0f;
    }
    auto view = [&](ImVec2 pt) {
        return ImVec2(cCenter.x + (pt.x - cCenter.x) * animMapZoom_ + animMapPanX_,
                      cCenter.y + (pt.y - cCenter.y) * animMapZoom_ + animMapPanY_);
    };

    // Fit each skeleton into its half: front view (x right, y up).
    struct Fit {
        float minX = 0, maxX = 0, minY = 0, maxY = 0;
        ImVec2 org;  // screen-space origin
        float scale = 1.0f;
    };
    auto fitOf = [&](const std::vector<float>& pos, const std::set<int>& bones,
                     float x0, float x1) {
        Fit f;
        bool first = true;
        for (int n : bones) {
            if (n < 0 || (size_t)(n * 3 + 2) >= pos.size()) continue;
            const float x = pos[(size_t)n * 3], y = pos[(size_t)n * 3 + 1];
            if (first || x < f.minX) f.minX = x;
            if (first || x > f.maxX) f.maxX = x;
            if (first || y < f.minY) f.minY = y;
            if (first || y > f.maxY) f.maxY = y;
            first = false;
        }
        const float w = f.maxX - f.minX, h = f.maxY - f.minY;
        const float availW = (x1 - x0) - scaled(40);
        const float availH = cSize.y - scaled(56);
        const float sx = w > 1e-6f ? availW / w : 1.0f;
        const float sy = h > 1e-6f ? availH / h : 1.0f;
        f.scale = sx < sy ? sx : sy;
        // center the box in its half; y flipped (screen y grows down)
        f.org.x = cMin.x + x0 + ((x1 - x0) - w * f.scale) * 0.5f -
                  f.minX * f.scale;
        f.org.y = cMin.y + scaled(36) + (availH - h * f.scale) * 0.5f +
                  f.maxY * f.scale;
        return f;
    };
    const float half = cSize.x * 0.5f;
    // Framing always comes from the BIND pose (stable), the drawn points from
    // the test pose when one is on - so the figures move inside a calm frame.
    const Fit dFit = fitOf(animMapDPos_, donorBones, scaled(8), half - scaled(8));
    const Fit tFit = fitOf(animMapTPos_, targetBones, half + scaled(8),
                           cSize.x - scaled(8));
    const std::vector<float>& dCur =
        poseOn && animMapDPosed_.size() == animMapDPos_.size() ? animMapDPosed_
                                                               : animMapDPos_;
    const std::vector<float>& tCur =
        poseOn && animMapTPosed_.size() == animMapTPos_.size() ? animMapTPosed_
                                                               : animMapTPos_;
    auto place = [&](const Fit& f, const std::vector<float>& pos, int n) {
        return view(ImVec2(f.org.x + pos[(size_t)n * 3] * f.scale,
                           f.org.y - pos[(size_t)n * 3 + 1] * f.scale));
    };

    const theme::Semantics& sem = theme::semantics();
    dl->AddText(ImVec2(cMin.x + scaled(8), cMin.y + scaled(8)),
                ImGui::GetColorU32(sem.textDim), "source");
    dl->AddText(ImVec2(cMin.x + half + scaled(8), cMin.y + scaled(8)),
                ImGui::GetColorU32(sem.textDim), "this model");
    dl->AddLine(ImVec2(cMin.x + half, cMin.y),
                ImVec2(cMin.x + half, cMin.y + cSize.y),
                ImGui::GetColorU32(sem.border));

    // Bone lines: to the nearest BONE ancestor, so helper nodes between two
    // bones do not break the figure apart.
    auto boneParent = [](const glbparser::Skel& sk, const std::set<int>& bones,
                         int n) {
        int p = sk.nodes[(size_t)n].parent;
        while (p >= 0 && p < (int)sk.nodes.size() && !bones.count(p))
            p = sk.nodes[(size_t)p].parent == p ? -1 : sk.nodes[(size_t)p].parent;
        return p >= 0 && p < (int)sk.nodes.size() ? p : -1;
    };
    for (int n : donorBones)
        if (int p = boneParent(don, donorBones, n); p >= 0)
            dl->AddLine(place(dFit, dCur, n), place(dFit, dCur, p),
                        ImGui::GetColorU32(sem.border), 1.5f);
    for (int n : targetBones)
        if (int p = boneParent(tgt, targetBones, n); p >= 0)
            dl->AddLine(place(tFit, tCur, n), place(tFit, tCur, p),
                        ImGui::GetColorU32(sem.border), 1.5f);

    // Selection line: the selected donor joint to its current home.
    if (animMapSelDonor_ >= 0 && resolved.count(animMapSelDonor_) &&
        resolved[animMapSelDonor_] >= 0)
        dl->AddLine(place(dFit, dCur, animMapSelDonor_),
                    place(tFit, tCur, resolved[animMapSelDonor_]),
                    ImGui::GetColorU32(sem.accentMuted), 1.0f);

    // Joints + hit test. Donor colors: green = matched by name, cyan ring =
    // explicit pair, amber = suggested, red = unmatched.
    const float R = scaled(4.0f);
    int hoverDonor = -1, hoverTarget = -1;
    float bestD = R * 2.5f;
    for (int n : donorBones) {
        const ImVec2 pt = place(dFit, dCur, n);
        const float d = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
        if (canvasHover && d < bestD) bestD = d, hoverDonor = n;
    }
    if (hoverDonor < 0) {
        bestD = R * 2.5f;
        for (int n : targetBones) {
            const ImVec2 pt = place(tFit, tCur, n);
            const float d = std::hypot(mouse.x - pt.x, mouse.y - pt.y);
            if (canvasHover && d < bestD) bestD = d, hoverTarget = n;
        }
    }
    const ImU32 cGreen = IM_COL32(90, 210, 110, 255);
    const ImU32 cAmber = IM_COL32(235, 190, 80, 255);
    const ImU32 cRed = IM_COL32(240, 90, 70, 255);
    for (int n : donorBones) {
        const ImVec2 pt = place(dFit, dCur, n);
        ImU32 col = cRed;
        if (resolved[n] >= 0) col = cGreen;
        else if (suggested.count(n)) col = cAmber;
        dl->AddCircleFilled(pt, R, col);
        if (explicitPair[n])
            dl->AddCircle(pt, R + scaled(2), ImGui::GetColorU32(sem.accent), 0, 2.0f);
        if (n == animMapSelDonor_)
            dl->AddCircle(pt, R + scaled(4), ImGui::GetColorU32(sem.text), 0, 2.0f);
        if (n == hoverDonor)
            dl->AddCircle(pt, R + scaled(2), ImGui::GetColorU32(sem.text));
    }
    // Target joints: used ones dim green, free ones gray; amber when it is
    // the suggestion for the selected donor joint.
    std::set<int> usedTargets;
    for (auto& [dn, tn] : resolved)
        if (tn >= 0) usedTargets.insert(tn);
    for (int n : targetBones) {
        const ImVec2 pt = place(tFit, tCur, n);
        ImU32 col = usedTargets.count(n) ? IM_COL32(80, 140, 90, 255)
                                         : ImGui::GetColorU32(sem.textDim);
        if (animMapSelDonor_ >= 0 && suggested.count(animMapSelDonor_) &&
            suggested[animMapSelDonor_] == n)
            col = cAmber;
        dl->AddCircleFilled(pt, R, col);
        if (n == hoverTarget)
            dl->AddCircle(pt, R + scaled(2), ImGui::GetColorU32(sem.text));
    }

    // Hovering a JOINT shows its link at once: ring the other end and tie
    // them with a line - accent for a real mapping (by name or by hand),
    // amber for a pending suggestion. Same for hovering a target joint,
    // which points back at whatever lands on it.
    if (hoverDonor >= 0) {
        const ImVec2 dp = place(dFit, dCur, hoverDonor);
        int tn = resolved.count(hoverDonor) ? resolved[hoverDonor] : -1;
        bool isSugg = false;
        if (tn < 0 && suggested.count(hoverDonor)) {
            tn = suggested[hoverDonor];
            isSugg = true;
        }
        if (tn >= 0 && targetBones.count(tn)) {
            const ImVec2 tp = place(tFit, tCur, tn);
            const ImU32 col =
                isSugg ? cAmber : ImGui::GetColorU32(sem.accent);
            dl->AddCircle(tp, R + scaled(3), col, 0, 2.0f);
            dl->AddLine(dp, tp, col, 1.5f);
        }
    } else if (hoverTarget >= 0) {
        const ImVec2 tp = place(tFit, tCur, hoverTarget);
        for (auto& [dn, tn] : resolved) {
            if (tn != hoverTarget) continue;
            const ImVec2 dp = place(dFit, dCur, dn);
            dl->AddCircle(dp, R + scaled(3), ImGui::GetColorU32(sem.accent), 0,
                          2.0f);
            dl->AddLine(dp, tp, ImGui::GetColorU32(sem.accent), 1.5f);
        }
        for (auto& [dn, tn] : suggested) {
            if (tn != hoverTarget) continue;
            const ImVec2 dp = place(dFit, dCur, dn);
            dl->AddCircle(dp, R + scaled(3), cAmber, 0, 2.0f);
            dl->AddLine(dp, tp, cAmber, 1.5f);
        }
    }

    // The list pane's hover: ring both ends and tie them with a line, so
    // "which joint is this row" needs no reading of coordinates.
    if (hiD >= 0 && donorBones.count(hiD)) {
        const ImVec2 pt = place(dFit, dCur, hiD);
        dl->AddCircle(pt, R + scaled(3), ImGui::GetColorU32(sem.accent), 0, 2.0f);
        if (hiT >= 0 && targetBones.count(hiT)) {
            const ImVec2 tp = place(tFit, tCur, hiT);
            dl->AddCircle(tp, R + scaled(3), ImGui::GetColorU32(sem.accent), 0,
                          2.0f);
            dl->AddLine(pt, tp, ImGui::GetColorU32(sem.accent), 1.5f);
        }
    }

    // Hover name + click actions.
    if (hoverDonor >= 0) {
        ImGui::SetTooltip("%s", don.nodes[(size_t)hoverDonor].name.c_str());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            animMapSelDonor_ = hoverDonor;
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
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            animMapSelDonor_ >= 0) {
            const std::string& from = don.nodes[(size_t)animMapSelDonor_].name;
            const std::string& to = tgt.nodes[(size_t)hoverTarget].name;
            upsertPair(animMapPairs_, from, to);
            animMapSelDonor_ = -1;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    // --- the pair list: what is mapped onto what, hover = show me ----------
    ImGui::BeginChild("##bonelist", ImVec2(0, -footerH),
                      ImGuiChildFlags_Borders);
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
        int removePair = -1;
        if (!animMapPairs_.empty()) {
            ImGui::SeparatorText("Pairs");
            for (size_t k = 0; k < animMapPairs_.size(); ++k) {
                ImGui::PushID((int)k);
                if (ImGui::SmallButton("x")) removePair = (int)k;
                ImGui::SameLine();
                ImGui::TextUnformatted(animMapPairs_[k].first.c_str());
                hoverRow(nodeByName(don, animMapPairs_[k].first),
                         nodeByName(tgt, animMapPairs_[k].second));
                ImGui::Indent(scaled(18));
                ImGui::TextDisabled("> %s", animMapPairs_[k].second.c_str());
                hoverRow(nodeByName(don, animMapPairs_[k].first),
                         nodeByName(tgt, animMapPairs_[k].second));
                ImGui::Unindent(scaled(18));
                ImGui::PopID();
            }
        }
        if (removePair >= 0)
            animMapPairs_.erase(animMapPairs_.begin() + removePair);
        if (!animMapSugg_.empty()) {
            ImGui::SeparatorText("Suggestions");
            int accept = -1;
            for (size_t k = 0; k < animMapSugg_.size(); ++k) {
                const animmerge::BoneSuggestion& sg = animMapSugg_[k];
                ImGui::PushID(1000 + (int)k);
                if (ImGui::SmallButton("+")) accept = (int)k;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Accept.");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "%s",
                                   sg.donor.c_str());
                hoverRow(nodeByName(don, sg.donor), nodeByName(tgt, sg.target));
                ImGui::Indent(scaled(18));
                ImGui::TextDisabled("~ %s  %d%%", sg.target.c_str(),
                                    (int)(sg.score * 100.0f + 0.5f));
                hoverRow(nodeByName(don, sg.donor), nodeByName(tgt, sg.target));
                ImGui::Unindent(scaled(18));
                ImGui::PopID();
            }
            if (accept >= 0)
                upsertPair(animMapPairs_, animMapSugg_[(size_t)accept].donor,
                           animMapSugg_[(size_t)accept].target);
        }
        if (!animMapAiSugg_.empty()) {
            ImGui::SeparatorText("AI");
            if (ImGui::SmallButton("Accept all AI")) {
                for (const auto& pr : animMapAiSugg_)
                    upsertPair(animMapPairs_, pr.first, pr.second);
                animMapAiSugg_.clear();
            }
            int acceptAi = -1;
            for (size_t k = 0; k < animMapAiSugg_.size(); ++k) {
                const auto& pr = animMapAiSugg_[k];
                ImGui::PushID(3000 + (int)k);
                if (ImGui::SmallButton("+")) acceptAi = (int)k;
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s",
                                   pr.first.c_str());
                hoverRow(nodeByName(don, pr.first), nodeByName(tgt, pr.second));
                ImGui::Indent(scaled(18));
                ImGui::TextDisabled("~ %s", pr.second.c_str());
                hoverRow(nodeByName(don, pr.first), nodeByName(tgt, pr.second));
                ImGui::Unindent(scaled(18));
                ImGui::PopID();
            }
            if (acceptAi >= 0) {
                upsertPair(animMapPairs_, animMapAiSugg_[(size_t)acceptAi].first,
                           animMapAiSugg_[(size_t)acceptAi].second);
                animMapAiSugg_.erase(animMapAiSugg_.begin() + acceptAi);
            }
        }
        // Unmatched and unsuggested: the ones only a human can place.
        std::vector<int> orphans;
        for (int nd : donorBones)
            if (resolved.count(nd) && resolved[nd] < 0 && !suggested.count(nd))
                orphans.push_back(nd);
        if (!orphans.empty()) {
            ImGui::SeparatorText("Unmatched");
            for (int nd : orphans) {
                ImGui::PushID(2000 + nd);
                if (ImGui::Selectable(don.nodes[(size_t)nd].name.c_str(),
                                      animMapSelDonor_ == nd))
                    animMapSelDonor_ = nd;
                hoverRow(nd, -1);
                ImGui::PopID();
            }
        }
        if (animMapPairs_.empty() && animMapSugg_.empty() && orphans.empty())
            ImGui::TextDisabled("all bones matched by name");
    }
    ImGui::EndChild();

    // --- footer -------------------------------------------------------------
    bool applied = false;
    const bool dirty = animMapPairs_ != row.boneMap;
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Apply")) {
        row.boneMap = animMapPairs_;
        // The book learns every accepted pair, so the same rename suggests
        // itself in the next file from the same pack.
        for (const auto& [from, to] : animMapPairs_)
            boneAliases_[animmerge::canonicalBoneKey(from)] = to;
        saveBoneAliases();
        applied = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) animMapRow_ = -1;
    ImGui::SameLine();
    ImGui::TextDisabled(
        "wheel zoom - middle-drag pan - right-click removes a pair");

    ImGui::End();
    if (!openFlag) animMapRow_ = -1;
    return applied;
}

bool App::drawAnimImportSection(const std::string& modelRel) {
    bool changed = false;
    int rows = 0;
    for (const AnimImport& a : project_.animImports)
        if (a.model == modelRel) ++rows;

    const std::string header =
        rows ? "Imported clips (" + std::to_string(rows) + ")###animimp"
             : std::string("Imported clips###animimp");
    // Interface text stays terse (the skill rule): facts in the panel, the
    // explanation - short - in tooltips. The old version opened with a
    // three-line paragraph nobody came here to read.
    const bool open = ImGui::CollapsingHeader(header.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Borrow clips from another rigged file.");
    if (!open) return false;

    std::vector<std::string> candidates;
    for (const std::string& m : listAnimatedModelFiles()) {
        const std::string rel = "res/models/" + m;
        if (rel != modelRel) candidates.push_back(rel);
    }
    ImGui::SetNextItemWidth(scaled(280));
    const char* preview =
        animImpSource_.empty() ? "Pick a file..." : animImpSource_.c_str();
    if (ImGui::BeginCombo("##impsrc", preview)) {
        for (const std::string& c : candidates)
            if (ImGui::Selectable(c.c_str(), c == animImpSource_))
                animImpSource_ = c;
        if (candidates.empty())
            ImGui::TextDisabled("No other animated model in the project.");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import file...")) {
        const size_t before = listAnimatedModelFiles().size();
        importModelAsset();
        const std::vector<std::string> after = listAnimatedModelFiles();
        if (after.size() > before) {
            for (const std::string& m : after) {
                const std::string rel = "res/models/" + m;
                if (rel == modelRel) continue;
                bool known = false;
                for (const std::string& c : candidates)
                    if (c == rel) known = true;
                if (!known) animImpSource_ = rel;
            }
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy a .glb/.fbx into res/models/ and select it.");

    if (!animImpSource_.empty()) {
        const AnimImportProbe& probe =
            animImportProbe(modelRel, animImpSource_, {});
        if (!probe.ok) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Unusable: %s",
                               probe.error.c_str());
        } else {
            ImGui::SameLine();
            if (ImGui::Button("Add clips")) {
                AnimImport a;
                a.model = modelRel;
                a.source = animImpSource_;
                project_.animImports.push_back(std::move(a));
                // A poor name match is exactly what the mapper is for - open
                // it on the fresh row instead of leaving a red number.
                if (probe.bonesMapped < probe.bonesTotal)
                    openAnimBoneMap((int)project_.animImports.size() - 1);
                animImpSource_.clear();
                changed = true;
            }
            ImGui::SameLine();
            const float match = probe.match;
            const ImVec4 col = match > 0.85f ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                               : match > 0.4f ? ImVec4(0.95f, 0.8f, 0.3f, 1.0f)
                                              : ImVec4(1.0f, 0.4f, 0.3f, 1.0f);
            ImGui::TextColored(col, "%d/%d bones", probe.bonesMapped,
                               probe.bonesTotal);
            ImGui::SameLine();
            ImGui::TextDisabled("- %d clip%s", probe.clipCount,
                                probe.clipCount == 1 ? "" : "s");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Bones matched by name. Fix the rest in Map bones.");
        }
    }

    if (rows) ImGui::Separator();
    int removeAt = -1;
    for (size_t i = 0; i < project_.animImports.size(); ++i) {
        AnimImport& a = project_.animImports[i];
        if (a.model != modelRel) continue;
        ImGui::PushID((int)i);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(a.source.c_str());
        ImGui::SameLine();
        {
            const AnimImportProbe& probe =
                animImportProbe(a.model, a.source, a.boneMap);
            if (probe.ok) {
                const bool full = probe.bonesMapped == probe.bonesTotal;
                ImGui::TextColored(full ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                        : ImVec4(0.95f, 0.8f, 0.3f, 1.0f),
                                   "%d/%d", probe.bonesMapped, probe.bonesTotal);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bones mapped.");
                ImGui::SameLine();
                ImGui::TextDisabled(probe.retarget.full ? "retarget" : "copy");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        probe.retarget.full
                            ? "Bind poses differ %.0f deg (facing %.0f) - "
                              "clips are resampled through the full "
                              "retargeter."
                            : "Identical binds - channels copy verbatim.",
                        probe.retarget.bindGapDeg, probe.retarget.facingDeg);
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "!");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", probe.error.c_str());
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Map bones...")) openAnimBoneMap((int)i);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) removeAt = (int)i;

        ImGui::Indent();
        // Prefix is STAGED and committed when the edit ends - a committed
        // change re-parses, re-merges and re-bakes the model, and doing that
        // per keystroke stalled the editor once per character.
        const bool editingThis = animImpEditRow_ == (int)i;
        char prefix[64];
        snprintf(prefix, sizeof prefix, "%s",
                 editingThis ? animImpPrefix_ : a.prefix.c_str());
        ImGui::SetNextItemWidth(scaled(110));
        if (ImGui::InputText("Prefix", prefix, sizeof prefix)) {
            animImpEditRow_ = (int)i;
            snprintf(animImpPrefix_, sizeof animImpPrefix_, "%s", prefix);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && animImpEditRow_ == (int)i) {
            if (a.prefix != animImpPrefix_) {
                a.prefix = animImpPrefix_;
                changed = true;
            }
            animImpEditRow_ = -1;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Prepended to the imported clip names.");
        ImGui::SameLine();
        const char* modes[] = {"Root bones only", "Only where animated",
                               "Copy everything"};
        ImGui::SetNextItemWidth(scaled(150));
        const int tSel =
            a.translation >= 0 && a.translation < 3 ? a.translation : 0;
        if (ImGui::BeginCombo("Translation", modes[tSel])) {
            for (int m = 0; m < 3; ++m)
                if (ImGui::Selectable(modes[m], m == a.translation) &&
                    m != a.translation) {
                    a.translation = m;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Which bones keep the source's positions.\n"
                "Root only = keep this model's own proportions (default).");
        if (ImGui::Checkbox("Retarget root motion", &a.retargetRoot))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scale hip travel to this rig's hip height.");
        ImGui::SameLine();
        if (ImGui::Checkbox("Ignore scale", &a.ignoreScale)) changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drop the source's scale tracks (export noise).");
        ImGui::SameLine();
        {
            const char* faces[] = {"Auto", "0", "90", "180", "270"};
            const int fi = a.facing < 0 ? 0
                           : a.facing < 45 ? 1
                           : a.facing < 135 ? 2
                           : a.facing < 225 ? 3 : 4;
            ImGui::SetNextItemWidth(scaled(70));
            if (ImGui::BeginCombo("Facing", faces[fi])) {
                for (int m = 0; m < 5; ++m)
                    if (ImGui::Selectable(faces[m], m == fi) && m != fi) {
                        a.facing = m == 0 ? -1 : (m - 1) * 90;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "World yaw of the source rig, degrees. Auto reads both\n"
                    "rigs' facing from their feet (ankles -> toes).");
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Mirror", &a.mirror)) changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Import the clips mirrored left<->right - walk_right out\n"
                "of walk_left.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(80));
        ImGui::DragFloat("Lean", &a.lean, 0.2f, -30.0f, 30.0f, "%.1f deg");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Posture fine-tune: + leans the torso forward, - back.\n"
                "For a retarget that walks well but stands a bit off.");
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        project_.animImports.erase(project_.animImports.begin() + removeAt);
        if (animMapRow_ == removeAt) animMapRow_ = -1;
        changed = true;
    }
    return changed;
}

void App::drawAnimEditorWindow() {
    if (!showAnimEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(920), scaled(620)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Editor", &showAnimEditor_)) {
        ImGui::End();
        return;
    }

    // Dimmed "(?)" on the current line (the Preferences idiom - prefHelp is
    // defined further down the file, next to the dialog that introduced it).
    auto helpMarker = [](const char* tip) {
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    };

    bool changed = false;
    // Belt and braces: `changed` is set by hand at each widget, so a newly
    // added one silently forgets it (that is how the Menu Editor came to leave
    // the save icon dark). Comparing the section's serialized form across the
    // whole body cannot be forgotten. Clip renames also retarget references in
    // the SCENES, which the undo snapshot covers on its own.
    // Two sections are owned by this window now, so the guard covers both -
    // concatenated, which is the documented answer for a panel that spans more
    // than one (app.hpp rule 1).
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::AnimEdits) +
        project::sectionJson(project_, project::Section::AnimImports);
    // This window has early returns (an unusable model, a model with no clips
    // of its own), and the import block above them can edit - so the commit
    // has to be reachable from every exit, not only the bottom. The
    // drawCreditsWindow / drawGradingWindow idiom.
    auto commitIfEdited = [&] {
        if (changed ||
            project::sectionJson(project_, project::Section::AnimEdits) +
                    project::sectionJson(project_,
                                         project::Section::AnimImports) !=
                beforeSection)
            commitChange();
    };
    // listAnimatedModelFiles returns names relative to res/models; every other
    // API here (glbInfo, the viewport cache, AnimClipEdit::model) speaks
    // project-relative paths, which is also what SceneObject::modelPath holds.
    std::vector<std::string> models;
    for (const std::string& m : listAnimatedModelFiles())
        models.push_back("res/models/" + m);
    if (models.empty()) {
        ImGui::TextDisabled(
            "No animated models in this project.\n\n"
            "Project > Assets > Import model... and pick a .glb or .fbx.");
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (animEdModel_.empty() ||
        std::find(models.begin(), models.end(), animEdModel_) == models.end()) {
        animEdModel_ = models.front();
        animEdClip_.clear();
    }

    // --- header: model picker + the project-wide fps ratio ------------------
    ImGui::SetNextItemWidth(scaled(280));
    if (ImGui::BeginCombo("Model", animEdModel_.c_str())) {
        for (const std::string& m : models)
            if (ImGui::Selectable(m.c_str(), m == animEdModel_) &&
                m != animEdModel_) {
                animEdModel_ = m;
                animEdClip_.clear();
                animEdTime_ = 0.0f;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    // Background preview bakes in flight (the async viewport rebake). The
    // work is off the UI thread now, so the panel STAYS interactive - this
    // is the "it is still working" signal, not a stall.
    if (viewport_.animBakesPending() > 0) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "%c baking preview",
            "|/-\\"[(int)(ImGui::GetTime() * 8.0) & 3]);
        ImGui::SameLine();
    }
    const float projScale = animedit::projectTimeScale(project_.settings);
    if (std::fabs(projScale - 1.0f) > 0.001f)
        ImGui::TextDisabled("project fps: %.0f -> %.0f (%.3fx)",
                            project_.settings.animSourceFps,
                            project_.settings.animPlayFps, projScale);
    else
        ImGui::TextDisabled("project fps: 1:1");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Project > Preferences > Rendering > Animation fps.\n"
            "Multiplies every clip of every model; the per-clip time\n"
            "scale below stacks on top of it.");

    // Imported clips, BEFORE the parse below and before its early returns: an
    // import changes what clips this model has, and a character with none of
    // its own - a bare rigged T-pose plus a folder of downloaded moves - is
    // precisely the case this feature exists for. Drawn first, so that model
    // can still reach the picker instead of hitting "no animation clips".
    if (drawAnimImportSection(animEdModel_)) {
        invalidateAnimCaches(animEdModel_);
        changed = true;
    }
    // The bone-mapping window is a satellite of this one and commits through
    // the same section guard. Its rows all belong to the current model, so
    // the targeted invalidation is correct for it too.
    if (drawAnimBoneMapWindow()) {
        invalidateAnimCaches(animEdModel_);
        changed = true;
    }
    ImGui::Separator();

    const GlbInfo& info = glbInfo(animEdModel_);
    if (!info.ok) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Unusable model: %s",
                           info.error.c_str());
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (info.clips.empty()) {
        ImGui::TextDisabled("This model has no animation clips.");
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (animEdClip_.empty() ||
        std::find(info.clips.begin(), info.clips.end(), animEdClip_) ==
            info.clips.end()) {
        animEdClip_ = info.clips.front();
        animEdTime_ = 0.0f;
        snprintf(animEdRename_, sizeof(animEdRename_), "%s",
                 animedit::effectiveName(project_, animEdModel_, animEdClip_)
                     .c_str());
    }

    const AnimClipEdit* cur =
        animedit::findEdit(project_, animEdModel_, animEdClip_);
    // Snapshot the staged values before any widget can append/prune the vector
    // and invalidate `cur` during this frame.
    const float curTimeScale = cur ? cur->timeScale : 1.0f;
    const bool curInPlace = cur && cur->inPlace;
    const bool curLoop = cur ? cur->loop : true;
    const float srcDur =
        viewport_.animClipDuration(animEdModel_, std::string(), animEdClip_);
    float trimA = 0.0f, trimB = srcDur;
    animedit::trimWindow(cur, srcDur, trimA, trimB);
    const float clipScale =
        projScale * ((cur && cur->timeScale > 0.001f) ? cur->timeScale : 1.0f);
    const float outDur = clipScale > 0.001f ? (trimB - trimA) / clipScale : 0.0f;

    ImGui::Separator();

    // --- left: the model's clips -------------------------------------------
    ImGui::BeginChild("##ae_clips", ImVec2(scaled(210), -scaled(4)),
                      ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##ae_filter", "Filter", animEdFilter_,
                             sizeof animEdFilter_);
    auto passesFilter = [&](const std::string& name) {
        if (!animEdFilter_[0]) return true;
        // case-insensitive substring
        std::string a = name, b = animEdFilter_;
        for (char& ch : a) ch = (char)std::tolower((unsigned char)ch);
        for (char& ch : b) ch = (char)std::tolower((unsigned char)ch);
        return a.find(b) != std::string::npos;
    };
    for (const std::string& c : info.clips) {
        if (!passesFilter(animedit::effectiveName(project_, animEdModel_, c)))
            continue;
        const AnimClipEdit* e = animedit::findEdit(project_, animEdModel_, c);
        std::string label = animedit::effectiveName(project_, animEdModel_, c);
        if (e && !e->isDefault()) label += "  *";
        if (ImGui::Selectable(label.c_str(), c == animEdClip_) &&
            c != animEdClip_) {
            animEdClip_ = c;
            animEdTime_ = 0.0f;
            snprintf(animEdRename_, sizeof(animEdRename_), "%s",
                     animedit::effectiveName(project_, animEdModel_, c).c_str());
        }
        if (ImGui::IsItemHovered() && e && !e->rename.empty())
            ImGui::SetTooltip("source clip: %s", c.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##ae_right", ImVec2(0, -scaled(4)));

    // --- live preview -------------------------------------------------------
    {
        const double now = ImGui::GetTime();
        const float dt =
            animEdClock_ > 0.0 ? (float)(now - animEdClock_) : 0.0f;
        animEdClock_ = now;
        if (animEdPlaying_ && outDur > 0.0001f) {
            // The playhead runs in OUTPUT seconds - what the game will show -
            // so the timeline length is the retimed length and dragging Time
            // scale visibly speeds the preview up.
            animEdTime_ = std::fmod(animEdTime_ + dt, outDur);
        }
        if (outDur > 0.0001f)
            animEdTime_ = std::clamp(animEdTime_, 0.0f, outDur);
        else
            animEdTime_ = 0.0f;

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
        const int pw = (int)std::max(scaled(240), ImGui::GetContentRegionAvail().x);
        const int ph = (int)scaled(300);
        const uint32_t tex = viewport_.renderAnimPreview(pw, ph, d);
        if (tex) {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)(intptr_t)tex,
                         ImVec2((float)pw, (float)ph), ImVec2(0, 1),
                         ImVec2(1, 0));
            // Image() is display-only and does not reliably become active on
            // a drag. Put a real input item over it, as the Material Editor
            // does, so every mouse button has a deterministic owner.
            ImGui::SetCursorScreenPos(origin);
            ImGui::InvisibleButton(
                "Animation preview##ae_view", ImVec2((float)pw, (float)ph),
                ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            ImGuiIO& io = ImGui::GetIO();

            if (hovered && io.MouseWheel != 0.0f) {
                animEdZoom_ *= std::pow(1.15f, io.MouseWheel);
                animEdZoom_ = std::clamp(animEdZoom_, 0.1f, 8.0f);
            }

            if (active) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    animEdYaw_ += io.MouseDelta.x * 0.4f;
                    animEdPitch_ = std::clamp(
                        animEdPitch_ - io.MouseDelta.y * 0.3f, -30.0f, 85.0f);
                } else if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    // Keep the visual travel per pixel stable as zoom changes.
                    const float s = 0.0065f / std::max(animEdZoom_, 0.1f);
                    animEdPanX_ = std::clamp(animEdPanX_ - io.MouseDelta.x * s,
                                             -5.0f, 5.0f);
                    animEdPanY_ = std::clamp(animEdPanY_ + io.MouseDelta.y * s,
                                             -5.0f, 5.0f);
                }
            }

            ImGui::GetWindowDrawList()->AddText(
                ImVec2(origin.x + scaled(8), origin.y + scaled(8)),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                "RMB rotate  |  MMB pan  |  wheel zoom");
        }
    }

    // --- transport + timeline ----------------------------------------------
    if (ImGui::Button(animEdPlaying_ ? "Pause" : "Play", ImVec2(scaled(70), 0)))
        animEdPlaying_ = !animEdPlaying_;
    ImGui::SameLine();
    if (ImGui::Button("|<", ImVec2(scaled(34), 0))) animEdTime_ = 0.0f;
    ImGui::SameLine();
    // Room on the same line for the Wireframe box + the preview-light combo.
    ImGui::SetNextItemWidth(-scaled(340));
    if (ImGui::SliderFloat("##ae_time", &animEdTime_, 0.0f,
                           outDur > 0.0001f ? outDur : 1.0f, "%.3f s"))
        animEdPlaying_ = false;
    ImGui::SameLine();
    ImGui::Checkbox("Wireframe", &animEdWireframe_);
    ImGui::SameLine();
    if (previewLightCombo("##ae_light", animEdLight_)) saveGlobalConfig();

    if (ImGui::SmallButton("Reset view")) {
        animEdYaw_ = 40.0f;
        animEdPitch_ = 15.0f;
        animEdZoom_ = 1.0f;
        animEdPanX_ = animEdPanY_ = 0.0f;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("RMB rotate; MMB pan; wheel zoom");

    // --- the edit itself ----------------------------------------------------
    ImGui::SeparatorText("Clip");
    ImGui::TextDisabled("source: %s", animEdClip_.c_str());

    ImGui::SetNextItemWidth(scaled(240));
    ImGui::InputText("Name in game", animEdRename_, sizeof(animEdRename_));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string want = animEdRename_;
        // Trim blanks: a name of spaces would be impossible to type into a
        // flow node's Clip field.
        while (!want.empty() && want.front() == ' ') want.erase(want.begin());
        while (!want.empty() && want.back() == ' ') want.pop_back();
        const std::string before =
            animedit::effectiveName(project_, animEdModel_, animEdClip_);
        // Empty (or back to the source name) means "no rename" - drop the
        // field rather than storing a redundant one.
        const std::string after = want.empty() ? animEdClip_ : want;
        bool clash = false;
        for (const std::string& c : info.clips)
            if (c != animEdClip_ &&
                animedit::effectiveName(project_, animEdModel_, c) == after)
                clash = true;
        if (clash) {
            statusMessage_ =
                "A clip of this model is already called \"" + after + "\"";
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
    ImGui::SameLine();
    helpMarker(
        "The name the game (and every script / flow node) uses for this clip.\n"
        "Empty = the name authored in the file. Renaming retargets the clip\n"
        "references of objects using this model, including Animation nodes in\n"
        "their own graphs; a node that drives another object through an\n"
        "object link keeps its text and may need updating by hand.");

    float timeScale = curTimeScale;
    ImGui::SetNextItemWidth(scaled(240));
    if (ImGui::DragFloat("Time scale", &timeScale, 0.01f, 0.05f, 10.0f,
                         "%.2fx")) {
        timeScale = std::clamp(timeScale, 0.05f, 10.0f);
        animEditFor(animEdModel_, animEdClip_).timeScale = timeScale;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        pruneAnimEdits();
        changed = true;
    }
    ImGui::SameLine();
    helpMarker(
        "Playback speed of THIS clip, on top of the project's animation fps.\n"
        "2.00x plays it twice as fast (half as long). The object's own Speed\n"
        "property and a flow node's Speed param multiply on top at runtime.");

    if (srcDur > 0.0001f) {
        float a = trimA, b = trimB;
        ImGui::SetNextItemWidth(scaled(240));
        if (ImGui::SliderFloat("Trim start", &a, 0.0f, srcDur, "%.3f s")) {
            a = std::clamp(a, 0.0f, std::max(0.0f, b - 0.01f));
            animEditFor(animEdModel_, animEdClip_).trimStart = a;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            pruneAnimEdits();
            changed = true;
        }
        ImGui::SetNextItemWidth(scaled(240));
        if (ImGui::SliderFloat("Trim end", &b, 0.0f, srcDur, "%.3f s")) {
            b = std::clamp(b, a + 0.01f, srcDur);
            // The stored 0 means "to the end", which keeps following a
            // re-exported longer clip - only store a real cut.
            animEditFor(animEdModel_, animEdClip_).trimEnd =
                (b >= srcDur - 1e-4f) ? 0.0f : b;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            pruneAnimEdits();
            changed = true;
        }
        ImGui::SameLine();
        helpMarker(
            "Cut the clip down to a range of the SOURCE animation (seconds as\n"
            "authored, so changing the speed never moves these handles). The\n"
            "trimmed clip is rebased to start at 0 and gets interpolated\n"
            "boundary poses, so it starts and ends exactly where you cut.");
    }

    bool inPlace = curInPlace;
    if (ImGui::Checkbox("In place", &inPlace)) {
        animEditFor(animEdModel_, animEdClip_).inPlace = inPlace;
        pruneAnimEdits();
        changed = true;
    }
    ImGui::SameLine();
    helpMarker(
        "Removes horizontal root motion from this clip, pinning the character\n"
        "to its position at the trimmed start while keeping vertical bob and\n"
        "all limb animation. This is per clip: an object playing a different\n"
        "Start clip is unaffected. Use it when the Player object already moves\n"
        "the avatar through the world; rebuild before testing in the game.");

    bool loop = curLoop;
    if (ImGui::Checkbox("Loop by default", &loop)) {
        animEditFor(animEdModel_, animEdClip_).loop = loop;
        pruneAnimEdits();
        changed = true;
    }
    ImGui::SameLine();
    helpMarker(
        "Seeds the Loop checkbox of objects that pick this clip as their\n"
        "Start clip. Objects already placed keep whatever they were set to,\n"
        "and a flow node's own Loop param always wins at runtime.");

    ImGui::Spacing();
    if (srcDur > 0.0001f)
        ImGui::TextDisabled("authored %.3f s  ->  ships as %.3f s  (%.2fx)",
                            srcDur, outDur, clipScale);
    else
        ImGui::TextDisabled("static clip (no motion to retime)");

    const AnimClipEdit* finalEdit =
        animedit::findEdit(project_, animEdModel_, animEdClip_);
    if (finalEdit && !finalEdit->isDefault()) {
        if (ImGui::Button("Reset this clip")) {
            const std::string before =
                animedit::effectiveName(project_, animEdModel_, animEdClip_);
            renameAnimClipRefs(animEdModel_, before, animEdClip_);
            auto& v = project_.animClipEdits;
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [&](const AnimClipEdit& e) {
                                       return e.model == animEdModel_ &&
                                              e.clip == animEdClip_;
                                   }),
                    v.end());
            snprintf(animEdRename_, sizeof(animEdRename_), "%s",
                     animEdClip_.c_str());
            animEdTime_ = 0.0f;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(back to exactly what the file contains)");
    }

    ImGui::EndChild();  // ae_right

    commitIfEdited();
    ImGui::End();
}

void App::drawLoadingScreenWindow() {
    if (!showLoadingEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(780), scaled(760)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Loading Screens", &showLoadingEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    auto& screens = project_.loadingScreens;
    if (selectedLoadingScreen_ >= (int)screens.size()) selectedLoadingScreen_ = -1;

    // Belt and braces, as in the Animation Editor: a hand-set `changed` flag is
    // forgettable, a section comparison across the whole body is not. This
    // window owns TWO sections - the screens and the boot splashes it hosts -
    // and the delete path also clears scene references, which undo covers.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::LoadingScreens) +
        project::sectionJson(project_, project::Section::Splash);
    auto commitIfEdited = [&] {
        if (changed ||
            project::sectionJson(project_, project::Section::LoadingScreens) +
                    project::sectionJson(project_, project::Section::Splash) !=
                beforeSection)
            commitChange();
    };

    changed |= drawSplashSection();

    const float previewH = scaled(360);
    ImGui::BeginChild("##ls_top", ImVec2(0, -(previewH + scaled(34))));

    // --- left: screen list -------------------------------------------------
    ImGui::BeginChild("##ls_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New screen", ImVec2(-1, 0))) {
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
        screens.push_back(std::move(s));
        selectedLoadingScreen_ = (int)screens.size() - 1;
        lsSelKind_ = 0;
        lsSelIdx_ = -1;
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)screens.size(); ++i) {
        ImGui::PushID(i);
        std::string tag = screens[i].name;
        if (project_.defaultLoadingScreen == i) tag += "  [default]";
        if (ImGui::Selectable(tag.c_str(), selectedLoadingScreen_ == i)) {
            selectedLoadingScreen_ = i;
            lsSelKind_ = 0;
            lsSelIdx_ = -1;
        }
        ImGui::PopID();
    }
    if (screens.empty())
        ImGui::TextDisabled("No screens yet.\n\nScenes with none use\n"
                            "the built-in loading.png\non black.");
    ImGui::EndChild();
    ImGui::SameLine();

    if (selectedLoadingScreen_ < 0 || selectedLoadingScreen_ >= (int)screens.size()) {
        ImGui::BeginChild("##ls_none", ImVec2(0, 0));
        ImGui::TextDisabled("Select a loading screen on the left (or create one).");
        ImGui::TextDisabled("\nUse loading screens by:");
        ImGui::BulletText("marking one \"Default at game start\"");
        ImGui::BulletText("picking one per scene in Scene > Preferences");
        ImGui::TextDisabled(
            "\nThe master toggle is Project > Preferences >\n"
            "\"Loading screen between scenes\".");
        ImGui::EndChild();
        ImGui::EndChild();  // ls_top
        commitIfEdited();
        ImGui::End();
        return;
    }
    LoadingScreenDef& ls = screens[selectedLoadingScreen_];

    // --- middle: screen header + element stack -----------------------------
    ImGui::BeginChild("##ls_stack", ImVec2(scaled(210), 0), ImGuiChildFlags_Borders);
    {
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", ls.name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##lsname", nameBuf, sizeof(nameBuf))) {
            // Keep per-scene references pointing at the renamed screen.
            for (SceneData& sc : project_.scenes)
                if (sc.loadingScreen == ls.name) sc.loadingScreen = nameBuf;
            ls.name = nameBuf;
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (ImGui::SmallButton("Duplicate")) {
        LoadingScreenDef copy = ls;
        std::string base = copy.name;
        for (int n = 2;; ++n) {
            copy.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& o : screens) taken |= (o.name == copy.name);
            if (!taken) break;
        }
        screens.push_back(std::move(copy));
        selectedLoadingScreen_ = (int)screens.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete screen")) {
        const std::string gone = ls.name;
        for (SceneData& sc : project_.scenes)
            if (sc.loadingScreen == gone) sc.loadingScreen.clear();
        if (project_.defaultLoadingScreen == selectedLoadingScreen_)
            project_.defaultLoadingScreen = -1;
        else if (project_.defaultLoadingScreen > selectedLoadingScreen_)
            --project_.defaultLoadingScreen;
        screens.erase(screens.begin() + selectedLoadingScreen_);
        selectedLoadingScreen_ = -1;
        ImGui::EndChild();       // ls_stack
        ImGui::EndChild();       // ls_top
        commitIfEdited();
        ImGui::End();
        return;
    }
    bool isDefault = project_.defaultLoadingScreen == selectedLoadingScreen_;
    if (ImGui::Checkbox("Default at game start", &isDefault)) {
        project_.defaultLoadingScreen = isDefault ? selectedLoadingScreen_ : -1;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scenes that don't pick a screen use this one.");
    if (ImGui::ColorEdit3("Background", ls.bgColor,
                          ImGuiColorEditFlags_NoInputs))
        changed = true;

    ImGui::SeparatorText("Images");
    for (int i = 0; i < (int)ls.images.size(); ++i) {
        ImGui::PushID(1000 + i);
        const bool sel = lsSelKind_ == 0 && lsSelIdx_ == i;
        std::string label = ls.images[i].name.empty() ? "(image)" : ls.images[i].name;
        if (ImGui::Selectable(label.c_str(), sel)) { lsSelKind_ = 0; lsSelIdx_ = i; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Import image (PNG)...")) {
        const int i = importHudImageInto(ls.images);
        if (i >= 0) { lsSelKind_ = 0; lsSelIdx_ = i; changed = true; }
    }

    ImGui::SeparatorText("Texts");
    for (int i = 0; i < (int)ls.texts.size(); ++i) {
        ImGui::PushID(2000 + i);
        const bool sel = lsSelKind_ == 1 && lsSelIdx_ == i;
        if (ImGui::Selectable(ls.texts[i].name.c_str(), sel)) {
            lsSelKind_ = 1;
            lsSelIdx_ = i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add text")) {
        HudText t;
        int counter = 0;
        for (;;) {
            t.name = "text-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& o : ls.texts) taken |= (o.name == t.name);
            if (!taken) break;
        }
        ls.texts.push_back(std::move(t));
        lsSelKind_ = 1;
        lsSelIdx_ = (int)ls.texts.size() - 1;
        changed = true;
    }

    ImGui::SeparatorText("Progress bars");
    for (int i = 0; i < (int)ls.bars.size(); ++i) {
        ImGui::PushID(3000 + i);
        const bool sel = lsSelKind_ == 2 && lsSelIdx_ == i;
        if (ImGui::Selectable(ls.bars[i].name.c_str(), sel)) {
            lsSelKind_ = 2;
            lsSelIdx_ = i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add bar")) {
        LoadingBar b;
        int counter = 0;
        for (;;) {
            b.name = "bar-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& o : ls.bars) taken |= (o.name == b.name);
            if (!taken) break;
        }
        ls.bars.push_back(std::move(b));
        lsSelKind_ = 2;
        lsSelIdx_ = (int)ls.bars.size() - 1;
        changed = true;
    }
    ImGui::EndChild();  // ls_stack
    ImGui::SameLine();

    // --- right: selected element properties --------------------------------
    ImGui::BeginChild("##ls_props", ImVec2(0, 0));
    if (lsSelKind_ == 0 && lsSelIdx_ >= 0 && lsSelIdx_ < (int)ls.images.size()) {
        HudImage& h = ls.images[lsSelIdx_];
        ImGui::SeparatorText(h.name.empty() ? "Image" : h.name.c_str());
        ImGui::DragFloat2("Position##lsimg", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Size (px)##lsimg", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        changed |= hudBakeControls(h);
        ImGui::Spacing();
        if (ImGui::Button("Delete image")) {
            ls.images.erase(ls.images.begin() + lsSelIdx_);
            lsSelIdx_ = -1;
            changed = true;
        }
    } else if (lsSelKind_ == 1 && lsSelIdx_ >= 0 &&
               lsSelIdx_ < (int)ls.texts.size()) {
        HudText& t = ls.texts[lsSelIdx_];
        ImGui::SeparatorText(t.name.c_str());
        {
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", t.name.c_str());
            ImGui::SetNextItemWidth(scaled(160));
            if (ImGui::InputText("Name##lstext", nameBuf, sizeof(nameBuf)))
                t.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        {
            char textBuf[512];
            std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
            if (ImGui::InputTextMultiline("Text##lstext", textBuf, sizeof(textBuf),
                                          ImVec2(-1.0f, scaled(70))))
                t.text = textBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        changed |= fontCombo(t.font);
        ImGui::SetNextItemWidth(scaled(120));
        if (ImGui::DragInt("Font size##lstext", &t.size, 0.2f, 8, 48, "%d px"))
            t.size = t.size < 8 ? 8 : t.size > 48 ? 48 : t.size;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::ColorEdit3("Color##lstext", t.color, ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::DragFloat2("Position##lstext", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Drop shadow##lstext", &t.shadow)) changed = true;
        ImGui::Spacing();
        if (ImGui::Button("Delete text")) {
            ls.texts.erase(ls.texts.begin() + lsSelIdx_);
            lsSelIdx_ = -1;
            changed = true;
        }
    } else if (lsSelKind_ == 2 && lsSelIdx_ >= 0 &&
               lsSelIdx_ < (int)ls.bars.size()) {
        LoadingBar& b = ls.bars[lsSelIdx_];
        ImGui::SeparatorText(b.name.c_str());
        {
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", b.name.c_str());
            ImGui::SetNextItemWidth(scaled(160));
            if (ImGui::InputText("Name##lsbar", nameBuf, sizeof(nameBuf)))
                b.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        changed |= loadingBarControls(b);
        ImGui::Spacing();
        if (ImGui::Button("Delete bar")) {
            ls.bars.erase(ls.bars.begin() + lsSelIdx_);
            lsSelIdx_ = -1;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("Select an element on the left,\nor add one.");
    }
    ImGui::EndChild();  // ls_props

    ImGui::EndChild();  // ls_top

    // --- preview + progress slider -----------------------------------------
    ImGui::SetNextItemWidth(scaled(240));
    ImGui::SliderFloat("Preview progress", &lsPreviewProgress_, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(simulated load fraction)");
    drawLoadingPreview(ls, lsPreviewProgress_);

    commitIfEdited();  // project-wide data: marks dirty, pushes no undo step
    ImGui::End();
}
