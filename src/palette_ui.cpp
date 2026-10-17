// The command palette (Ctrl+K / Cmd+K, or the search button in the top bar):
// one box that finds commands, scene objects, scenes, windows, workspaces and
// documentation pages, so the editor's ~40 windows and ten menus are reachable
// by name instead of by remembering where they live (docs/editor-theme.md).
//
// Every entry is a label, a dim hint, a category and an action. The list is
// rebuilt each frame the palette is open - it is a few hundred entries at most,
// and building it fresh means it can never disagree with the project.
//
// Commands are the main menus themselves (menu_actions.hpp): one harvest pass
// over drawMainMenus lists every enabled item with its menu path and shortcut,
// and choosing one runs a second pass that executes exactly that item's code.
// Only what has no menu item - the top bar's Run / Stop / profile, New scene,
// centring on the selection - is written out here.
#include "aichat.hpp"
#include "app.hpp"
#include "app_internal.hpp"
#include "menu_actions.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "update.hpp"
#include "widgets.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

namespace {

struct Entry {
    std::string label;
    std::string hint;
    const char* category;  // "Commands", "Objects", ...
    std::string key;       // shortcut shown on the right, may be ""
    ui::Glyph glyph;
    std::function<void()> run;
};

// A glyph per top-level menu, so a row says where it comes from at a glance.
ui::Glyph menuGlyph(const std::string& path) {
    const std::string top = path.substr(0, path.find(" > "));
    if (top == "File") return ui::Glyph::Folder;
    if (top == "Edit") return ui::Glyph::Sliders;
    if (top == "View") return ui::Glyph::Eye;
    if (top == "Layout") return ui::Glyph::Layers;
    if (top == "Scene") return ui::Glyph::Plus;
    if (top == "Project") return ui::Glyph::Gear;
    if (top == "Build") return ui::Glyph::Box;
    if (top == "Session") return ui::Glyph::Person;
    if (top == "Tools") return ui::Glyph::List;
    return ui::Glyph::Comment;  // Help
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Match score: 0 = no match. Prefix beats word-start beats substring; every
// space-separated term must match somewhere in the label or the hint.
int score(const std::string& label, const std::string& hint, const std::string& q) {
    if (q.empty()) return 1;
    const std::string l = lower(label), h = lower(hint);
    int total = 0;
    size_t i = 0;
    while (i < q.size()) {
        while (i < q.size() && q[i] == ' ') ++i;
        size_t j = i;
        while (j < q.size() && q[j] != ' ') ++j;
        if (j == i) break;
        const std::string term = q.substr(i, j - i);
        i = j;
        const size_t at = l.find(term);
        if (at == 0) total += 100;
        else if (at != std::string::npos) total += (l[at - 1] == ' ' || l[at - 1] == '-') ? 60 : 30;
        else if (h.find(term) != std::string::npos) total += 10;
        else return 0;
    }
    return total;
}

}  // namespace

// The optional windows by the names their menus and title bars use. Keys are
// kLayoutWindowKeys' (app.cpp) - a key missing here simply has no palette row.
static const char* windowTitle(const std::string& key) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"cutscene", "Cutscene Director"}, {"material", "Material Editor"},
        {"terrain", "Terrain Editor"},     {"ui", "UI Editor"},
        {"fonts", "Font Manager"},         {"menus", "Menu Editor"},
        {"menupreview", "Menu Preview"},   {"grading", "Color Grading"},
        {"ambience", "Ambience Editor"},   {"loading", "Loading Screens"},
        {"disc", "Disc Layout"},           {"anim", "Animation Editor"},
        {"tree", "Tree Generator"},        {"debugger", "Debugger"},
        {"phonecam", "Phone Camera"},      {"assets", "Asset Browser"},
        {"gibake", "Global Illumination"}, {"input", "Input Map"},
        {"drone", "Drone Generator"},      {"pad", "Remote Pad"},
        {"proc", "Procedural"},            {"prefabs", "Prefabs"},
        {"save", "Save Editor"},           {"facts", "World Facts"},
        {"credits", "Credits"},            {"vu", "VU Programs"},
        {"chat", "AI Assistant"},          {"blss", "Neural Upscaler (BLSS)"},
        {"atlas", "Texture Atlas"},        {"batches", "Static Batches"},
        {"projectprefs", "Project Preferences"},
        {"vehicles", "Vehicle Editor"},    {"particles", "Particle Editor"}};
    for (const auto& [k, n] : kNames)
        if (key == k) return n;
    return nullptr;
}

void App::harvestMainMenus(std::vector<menuact::Item>* out, int invoke) {
    // An item-less child: every plain ImGui call in the menu code (headings,
    // separators, tooltips) is skipped, and the menuact calls record instead
    // of drawing. NoInputs keeps IsItemHovered false in there.
    ImGui::BeginChild("##menu_harvest", ImVec2(1.0f, 1.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                          ImGuiWindowFlags_NoScrollbar);
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const bool skip = w->SkipItems;
    w->SkipItems = true;
    menuact::beginHarvest(out, invoke);
    drawMainMenus();
    menuact::endHarvest();
    w->SkipItems = skip;
    ImGui::EndChild();
}

void App::drawCommandPalette() {
    if (hasProject_ && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) {
        paletteOpen_ = !paletteOpen_;
        paletteFocus_ = paletteOpen_;
        paletteQuery_[0] = '\0';
        paletteSel_ = 0;
    }
    if (!paletteOpen_ || !hasProject_) {
        paletteOpen_ = false;
        return;
    }

    // --- the entries ---------------------------------------------------------
    std::vector<Entry> all;
    const bool busy = runner_.busy();
    auto cmd = [&](std::string label, std::string hint, const char* key, ui::Glyph g,
                   std::function<void()> run, bool enabled = true) {
        if (enabled) all.push_back({std::move(label), std::move(hint), "Commands", key, g, std::move(run)});
    };
    // What has no menu item. The top bar's Run follows the PCSX2 | PS2 switch
    // (the Build menu has both targets), and so does its Stop.
    cmd(runOnPs2_ ? "Build && Run on PS2" : "Build && Run in PCSX2", "The top bar's Run",
        runOnPs2_ ? "F6" : "F5", ui::Glyph::Move, [this] { runSelectedTarget(true); }, !busy);
    cmd(busy ? "Cancel build" : (runOnPs2_ ? "Stop the game on the PS2" : "Stop PCSX2"),
        "The top bar's Stop", "", ui::Glyph::Area, [this, busy] {
            if (busy) runner_.cancel();
            else if (runOnPs2_) runner_.stopPs2(project_);
            else runner_.stopEmulator(project_);
        });
    cmd(project_.settings.buildProfile == "debug" ? "Switch to the Release profile"
                                                  : "Switch to the Debug profile",
        "Build profile (Live Link, the debugger and Live Logic need Debug)", "", ui::Glyph::Code, [this] {
            project_.settings.buildProfile =
                project_.settings.buildProfile == "debug" ? "release" : "debug";
            commitChange();
        });
    cmd("New scene", "Add a scene to the project", "", ui::Glyph::Layers, [this] {
        openNewScenePopup_ = true;
        newSceneError_.clear();
    });
    if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size())
        cmd("Center the camera on the selection", project_.objects()[selectedObject_].name, "",
            ui::Glyph::Camera, [this] {
                viewport_.setTarget(project_.objects()[selectedObject_].position);
                navFocusedIndex_ = selectedObject_;
            });

    // Every enabled menu item, under its menu path. Running one re-runs the
    // menus with its ordinal (same frame, same state, so the same ordinal).
    // A Tools item that opens a window also brings that window forward - a
    // docked one may be open already behind another tab.
    std::vector<menuact::Item> menuItems;
    harvestMainMenus(&menuItems);
    for (const menuact::Item& it : menuItems) {
        if (!it.enabled || it.label.empty()) continue;
        std::string hint = it.path;
        if (it.selected) hint += hint.empty() ? "on" : " - on";
        std::string bare = it.label;
        if (bare.size() > 3 && bare.compare(bare.size() - 3, 3, "...") == 0) bare.resize(bare.size() - 3);
        const char* focus = nullptr;
        for (const std::string& key : chatWindowKeys())
            if (const char* t = windowTitle(key); t && bare == t) focus = t;
        const int ord = it.ordinal;
        all.push_back({it.label, std::move(hint), "Commands", it.shortcut, menuGlyph(it.path),
                       [this, ord, focus] {
                           std::vector<menuact::Item> again;
                           harvestMainMenus(&again, ord);
                           if (focus) pendingFocusWindow_ = focus;
                       }});
    }

    // Scenes.
    for (int i = 0; i < (int)project_.scenes.size(); ++i)
        all.push_back({project_.scenes[i].name,
                       std::string(i == project_.activeScene ? "the active scene" : "switch to this scene") +
                           (i == project_.startScene ? " (start)" : ""),
                       "Scenes", "", ui::Glyph::Flag, [this, i] { selectScene(i); }});

    // Objects of the active scene: select it and centre the camera on it.
    const auto& objs = project_.objects();
    for (int i = 0; i < (int)objs.size(); ++i)
        all.push_back({objs[i].name, primitiveTypeName(objs[i].type), "Objects", "", ui::Glyph::Box,
                       [this, i] {
                           if (i >= (int)project_.objects().size()) return;
                           selectOnly(i);
                           viewport_.setTarget(project_.objects()[i].position);
                           navFocusedIndex_ = i;
                       }});

    // Documentation: the pages baked into the editor, opened on GitHub.
    for (const aichat::DocInfo& d : aichat::docIndex()) {
        const std::string url =
            std::string("https://github.com/") + update::kRepo + "/blob/main/docs/" + d.name + ".md";
        all.push_back({d.title, d.summary, "Docs", "", ui::Glyph::Comment,
                       [url] { platform::openUrl(url); }});
    }

    // --- filter + rank -------------------------------------------------------
    const std::string q = lower(paletteQuery_);
    struct Hit {
        int score;
        int order;
        const Entry* e;
    };
    std::vector<Hit> hits;
    for (int i = 0; i < (int)all.size(); ++i) {
        // An empty query shows the commands and scenes - the hundreds of
        // objects and pages only once you type.
        if (q.empty() && std::string(all[i].category) != "Commands" &&
            std::string(all[i].category) != "Scenes")
            continue;
        const int sc = score(all[i].label, all[i].hint, q);
        if (sc > 0) hits.push_back({sc, i, &all[i]});
    }
    // Group by category in a fixed order, best match first inside each.
    auto catRank = [](const char* c) {
        static const char* const kOrder[] = {"Commands", "Objects", "Scenes", "Docs"};
        for (int k = 0; k < 4; ++k)
            if (std::string(c) == kOrder[k]) return k;
        return 4;
    };
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
        const int ca = catRank(a.e->category), cb = catRank(b.e->category);
        if (ca != cb) return ca < cb;
        if (a.score != b.score) return a.score > b.score;
        return a.order < b.order;
    });
    // Keep each category short: a palette is for the next action, not a list.
    {
        std::vector<Hit> kept;
        const char* cat = nullptr;
        int n = 0;
        for (const Hit& h : hits) {
            if (!cat || std::string(cat) != h.e->category) {
                cat = h.e->category;
                n = 0;
            }
            if (n++ < (q.empty() ? 12 : 8)) kept.push_back(h);
        }
        hits.swap(kept);
    }
    if (paletteSel_ >= (int)hits.size()) paletteSel_ = (int)hits.size() - 1;
    if (paletteSel_ < 0) paletteSel_ = 0;

    // --- the window ----------------------------------------------------------
    const theme::Semantics& sem = theme::semantics();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = ImMin(scaled(640.0f), vp->WorkSize.x - scaled(40.0f));
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + (vp->WorkSize.x - w) * 0.5f, vp->WorkPos.y + scaled(70.0f)));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f));
    // Always over the main window, even when the focused panel is in an OS
    // window of its own (every other OS window is dimmed below).
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, sem.surface);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(scaled(8.0f), scaled(8.0f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBackground |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove;
    ImGui::Begin("Command palette", nullptr, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    // Dim everything behind the palette, then its own card. Both on the
    // palette's draw list with the clip opened to the whole viewport: that list
    // renders above every other window and below the palette's own content
    // (the foreground list would dim the palette too).
    {
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->PushClipRect(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), false);
        wdl->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(4, 5, 8, 150));
        // Panels in OS windows of their own are dimmed too, on their own
        // foreground lists (the palette never draws on them).
        const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
        for (ImGuiViewport* other : pio.Viewports)
            if (other != vp)
                ImGui::GetForegroundDrawList(other)->AddRectFilled(
                    other->Pos, ImVec2(other->Pos.x + other->Size.x, other->Pos.y + other->Size.y),
                    IM_COL32(4, 5, 8, 150));
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        wdl->AddRectFilled(ImVec2(wp.x, wp.y + scaled(10.0f)), ImVec2(wp.x + ws.x, wp.y + ws.y + scaled(18.0f)),
                           IM_COL32(0, 0, 0, 90), scaled(14.0f));
        wdl->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ImGui::GetColorU32(sem.surface), scaled(12.0f));
        wdl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(255, 255, 255, 26), scaled(12.0f));
        wdl->PopClipRect();
    }

    // Close on Esc or on a click anywhere outside.
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        close = true;

    // The query field: large, borderless, with the magnifier.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight() * 1.3f;
        // magnifier
        const ImVec2 c(p.x + h * 0.45f, p.y + h * 0.48f);
        dl->AddCircle(c, h * 0.17f, ImGui::GetColorU32(sem.textDim), 16, scaled(1.6f));
        dl->AddLine(ImVec2(c.x + h * 0.12f, c.y + h * 0.12f), ImVec2(c.x + h * 0.25f, c.y + h * 0.25f),
                    ImGui::GetColorU32(sem.textDim), scaled(1.6f));
        ImGui::SetCursorScreenPos(ImVec2(p.x + h * 0.85f, p.y));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(scaled(4.0f), (h - ImGui::GetFontSize() * 1.15f) * 0.5f));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.15f);
        if (paletteFocus_) {
            ImGui::SetKeyboardFocusHere();
            paletteFocus_ = false;
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##palette_query", "Search commands, objects, scenes, windows, docs...",
                                     paletteQuery_, sizeof(paletteQuery_)))
            paletteSel_ = 0;
        ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        const float y = ImGui::GetCursorScreenPos().y;
        dl->AddLine(ImVec2(ImGui::GetWindowPos().x, y), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), y),
                    ImGui::GetColorU32(sem.border));
        ImGui::Dummy(ImVec2(0.0f, scaled(4.0f)));
    }

    // Keyboard: arrows move (and scroll the row into view), Enter runs.
    bool scrollToSel = false;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !hits.empty()) {
        paletteSel_ = (paletteSel_ + 1) % (int)hits.size();
        scrollToSel = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !hits.empty()) {
        paletteSel_ = (paletteSel_ + (int)hits.size() - 1) % (int)hits.size();
        scrollToSel = true;
    }
    const Entry* chosen = nullptr;
    const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    // Ctrl+Enter (Cmd+Return on a Mac - ImGui swaps the two there): hand the
    // query to the AI Assistant as its draft instead of running a hit.
    bool askAi = enter && ImGui::GetIO().KeyCtrl;
    if (enter && !askAi && !hits.empty()) chosen = hits[paletteSel_].e;

    // Results.
    // As tall as what it lists, up to 60% of the window.
    const float rowH = ImGui::GetFrameHeight() * 1.45f;
    float wantH = 0.0f;
    {
        const char* c = nullptr;
        for (const Hit& h : hits) {
            if (!c || std::string(c) != h.e->category) {
                c = h.e->category;
                wantH += scaled(2.0f) + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
            }
            wantH += rowH + ImGui::GetStyle().ItemSpacing.y;
        }
    }
    const float maxH = ImMin(scaled(460.0f), vp->WorkSize.y * 0.6f);
    ImGui::BeginChild("##palette_results",
                      ImVec2(0.0f, hits.empty() ? ImGui::GetFrameHeight() * 1.5f : ImMin(maxH, wantH)),
                      ImGuiChildFlags_None);
    if (hits.empty()) ImGui::TextDisabled("  Nothing matches \"%s\".", paletteQuery_);
    const char* lastCat = nullptr;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < (int)hits.size(); ++i) {
        const Entry& e = *hits[i].e;
        if (!lastCat || std::string(lastCat) != e.category) {
            lastCat = e.category;
            ImGui::Dummy(ImVec2(0.0f, scaled(2.0f)));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + scaled(8.0f));
            ImGui::TextDisabled("%s", e.category);
        }
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float rw = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton(e.label.c_str(), ImVec2(rw, rowH))) chosen = &e;
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x * ImGui::GetIO().MouseDelta.x +
                                              ImGui::GetIO().MouseDelta.y * ImGui::GetIO().MouseDelta.y > 0.0f)
            paletteSel_ = i;
        const bool sel = i == paletteSel_;
        if (sel) {
            dl->AddRectFilled(p, ImVec2(p.x + rw, p.y + rowH),
                              ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.16f)),
                              ImGui::GetStyle().FrameRounding);
            if (scrollToSel) ImGui::SetScrollHereY(0.5f);
        }
        const float ic = rowH * 0.7f;
        const ImVec2 t0(p.x + scaled(6.0f), p.y + (rowH - ic) * 0.5f);
        dl->AddRectFilled(t0, ImVec2(t0.x + ic, t0.y + ic), ImGui::GetColorU32(sem.ground), scaled(6.0f));
        ui::DrawGlyph(dl, e.glyph, ImVec2(t0.x + ic * 0.5f, t0.y + ic * 0.5f), ic * 0.5f,
                      ImGui::GetColorU32(sel ? sem.accent : sem.textDim));
        const float tx = t0.x + ic + scaled(10.0f);
        const float lh = ImGui::GetTextLineHeight();
        float keyW = 0.0f;
        if (!e.key.empty()) {
            ui::FontScope mono(ui::monoFont());
            keyW = ImGui::CalcTextSize(e.key.c_str()).x;
            dl->AddText(ImVec2(p.x + rw - keyW - scaled(10.0f), p.y + (rowH - lh) * 0.5f),
                        ImGui::GetColorU32(sem.textDim), e.key.c_str());
        }
        dl->PushClipRect(p, ImVec2(p.x + rw - keyW - scaled(18.0f), p.y + rowH), true);
        // ImGui would render "&&" as two ampersands; the run labels mean one.
        std::string shown = e.label;
        for (size_t k = shown.find("&&"); k != std::string::npos; k = shown.find("&&", k + 1))
            shown.erase(k, 1);
        dl->AddText(ImVec2(tx, p.y + (rowH - lh) * 0.5f), ImGui::GetColorU32(sem.text), shown.c_str());
        const float lw = ImGui::CalcTextSize(shown.c_str()).x;
        dl->AddText(ImVec2(tx + lw + scaled(10.0f), p.y + (rowH - lh) * 0.5f),
                    ImGui::GetColorU32(ImVec4(sem.textDim.x, sem.textDim.y, sem.textDim.z, 0.8f)), e.hint.c_str());
        dl->PopClipRect();
        ImGui::PopID();
    }
    ImGui::EndChild();

    // Footer: the keys on the left, the way out to the AI Assistant on the
    // right (a button too - "Ask the AI assistant").
    ImGui::TextDisabled("Up/Down to move   Enter to run   Esc to close");
    {
        const bool hasReturn = ImGui::GetFont()->IsGlyphInFont((ImWchar)0x21B5) ||
                               (ui::monoFont() && ui::monoFont()->IsGlyphInFont((ImWchar)0x21B5));
        const std::string chord =
            ui::ShortcutChord("Enter", hasReturn ? "\xE2\x86\xB5" : "Return", "+", true);
        const char* askLabel = "Ask the AI assistant";
        const float lh = ImGui::GetTextLineHeight();
        const float capW = ui::Keycap(nullptr, ImVec2(), lh, chord.c_str());
        const float askW = ImGui::CalcTextSize(askLabel).x + scaled(8.0f) + capW;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - askW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton(askLabel, ImVec2(askW, lh))) askAi = true;
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* fdl = ImGui::GetWindowDrawList();
        fdl->AddText(p, ImGui::GetColorU32(hov ? sem.text : sem.textDim), askLabel);
        ui::Keycap(fdl, ImVec2(p.x + askW - capW, p.y), lh, chord.c_str());
        if (hov)
            ImGui::SetTooltip("Open the AI Assistant with what you typed here as the draft message.");
    }
    // Run it while the palette is still the current window: a menu command
    // re-runs the menus in a child of it (harvestMainMenus).
    if (chosen && !askAi) {
        chosen->run();
        close = true;
        chosen = nullptr;
    }
    ImGui::End();

    if (askAi) {
        // The query becomes the Assistant's draft - appended on a new line if
        // something is already drafted there, never sent by itself.
        std::string draft = chatInputBuf_;
        if (paletteQuery_[0]) draft += (draft.empty() ? "" : "\n") + std::string(paletteQuery_);
        std::snprintf(chatInputBuf_, sizeof(chatInputBuf_), "%s", draft.c_str());
        showAiChat_ = true;
        chatFocusInput_ = true;
        pendingFocusWindow_ = "AI Assistant";
        chosen = nullptr;
        close = true;
    }
    if (chosen) {
        chosen->run();
        close = true;
    }
    if (close) paletteOpen_ = false;
}
