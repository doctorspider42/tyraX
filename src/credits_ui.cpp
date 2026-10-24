// -------------------------------------------------------------------------
// The Credits Editor (Tools > Credits Editor, docs/credits.md): end-credits
// rolls as project-wide data - a flow of headings, role/name pairs, lines and
// images, scrolled (or paged as cards) over music, skippable, with somewhere to
// go afterwards.
//
// Two things are worth knowing before editing this file:
//  - The preview draws the SAME baked page textures the console gets
//    (menubake::bakeCreditsStripRGBA), positioned by the same arithmetic the
//    generated player uses. It is not a re-implementation of the roll's look:
//    if the two ever disagree, one of them is wrong, and it is not the bake.
//  - Rolls are project-wide data like Ambience presets or Loading Screens, so
//    they are NOT part of undo/redo - but that is a statement about the undo
//    stack, not about saving. Edits go through commitChange() like every other
//    edit in the editor (app.hpp): for a project-wide collection history_.push
//    carries nothing and returns false, so no undo step appears, while the
//    project is still marked dirty and the session serial advances. This file
//    used to call saveAll() per widget instead, which wrote the whole project
//    AND the history file on every slider frame and cleared the dirty flag -
//    so the save icon never lit for a roll and the edit was not visible to the
//    collaboration / Live Link diff.
//
// App:: methods declared in app.hpp, in their own TU (the assetbrowser.cpp
// precedent) so the editor keeps building in parallel.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gl_loader.h"
#include "menubake.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "toolkit.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace fs = std::filesystem;

namespace {

const char* kCreditsBlockKinds[] = {"Heading", "Line",  "Role / name",
                                    "Image",   "Gap",   "Page break"};

// The list row for a block: its kind and enough of its content to find the one
// you meant in a hundred-block roll.
std::string creditsBlockLabel(const CreditsBlock& b) {
    switch (b.kind) {
        case CreditsBlock::Heading:
            return "# " + (b.text.empty() ? std::string("(heading)") : b.text);
        case CreditsBlock::Pair:
            return (b.text.empty() ? std::string("(role)") : b.text) + ": " +
                   (b.text2.empty() ? std::string("(name)") : b.text2);
        case CreditsBlock::Image:
            return "[img] " + (b.imagePath.empty()
                                   ? std::string("(none)")
                                   : fs::path(b.imagePath).filename().string());
        case CreditsBlock::Gap:
            return "--- gap ---";
        case CreditsBlock::Break:
            return "=== page break ===";
        default:
            return b.text.empty() ? std::string("(empty line)") : b.text;
    }
}

// A text field over a std::string, sized to the panel. Inside the inspector's
// rows a single line is a row (caption left); a multi-line field takes the full
// width under its caption - the label stays its ID (scripts type into it), the
// stock label just lands past the right edge, clipped.
bool creditsInput(const char* label, std::string& value, float width,
                  bool multiline = false, float height = 0.0f) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    bool edited = false;
    if (multiline) {
        ImGui::TextDisabled("%.*s", (int)(ImGui::FindRenderedTextEnd(label) - label), label);
        edited = ImGui::InputTextMultiline(label, buf, sizeof(buf),
                                           ImVec2(width < 0 ? -FLT_MIN : width, height));
    } else {
        ImGui::SetNextItemWidth(width < 0 ? -FLT_MIN : width);
        edited = ui::InputText(label, buf, sizeof(buf));
    }
    if (edited) value = buf;
    return edited;
}

}  // namespace

// ---------------------------------------------------------------------------
// Preview page textures
// ---------------------------------------------------------------------------

void App::creditsPreviewDrop() {
    for (unsigned int tex : crPreviewTex_)
        if (tex) glDeleteTextures(1, &tex);
    crPreviewTex_.clear();
    crPreviewValid_ = false;
    crLayout_ = menubake::CreditsLayout{};
}

bool App::creditsPreviewRefresh() {
    if (selectedCredits_ < 0 || selectedCredits_ >= (int)project_.credits.size()) {
        creditsPreviewDrop();
        return false;
    }
    const CreditsRoll& r = project_.credits[selectedCredits_];
    // Every TTF the roll could draw with: the roll names fonts by NAME, so a
    // Font Manager entry re-pointed at another file must re-bake even though
    // nothing in the roll changed.
    std::string fonts;
    for (const GameFont& f : project_.fonts) fonts += f.name + "=" + f.fontPath + ";";
    if (crPreviewValid_ && crPreviewRoll_ == r && crPreviewFonts_ == fonts)
        return true;

    creditsPreviewDrop();
    std::vector<unsigned char> strip;
    menubake::CreditsLayout layout;
    if (!menubake::bakeCreditsStripRGBA(r, project_, strip, layout)) return false;
    crLayout_ = layout;
    crPreviewPageW_ = layout.pageW;
    crPreviewPageH_ = layout.pageH;
    const size_t pageBytes = (size_t)layout.pageW * layout.pageH * 4;
    for (int k = 0; k < layout.pageCount; ++k) {
        unsigned int tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUploadTexRgba(layout.pageW, layout.pageH, strip.data() + (size_t)k * pageBytes);
        crPreviewTex_.push_back(tex);
    }
    crPreviewRoll_ = r;
    crPreviewFonts_ = fonts;
    crPreviewValid_ = true;
    return true;
}

float App::creditsDuration(const CreditsRoll& r) const {
    const menubake::CreditsLayout l = menubake::creditsLayout(r, project_);
    float rolling = 0.0f;
    if (r.mode == 1) {
        rolling = (float)l.pageCount * r.cardSeconds;
    } else {
        // The runtime is done when the last strip pixel has left the top of the
        // 448-line screen, which is the distance the roll actually travels.
        rolling = r.speed > 0.0f ? ((float)l.contentH + 448.0f) / r.speed : 0.0f;
    }
    return r.startDelay + rolling + r.endHold;
}

// ---------------------------------------------------------------------------
// Text import
// ---------------------------------------------------------------------------

std::string App::creditsImportFile(CreditsRoll& r, const std::string& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return "Cannot read " + file;
    std::stringstream text;
    text << in.rdbuf();
    std::vector<CreditsBlock> blocks = menubake::parseCreditsMarkup(text.str());
    if (blocks.empty()) return "That file produced no credit lines.";
    r.blocks = std::move(blocks);
    // Remember the source as a project-relative path when the file lives inside
    // the project (so it travels with it), the absolute one otherwise - either
    // way "Re-import" can pick up an edited file.
    std::error_code ec;
    const fs::path rel = fs::relative(fs::path(file), fs::path(project_.dir), ec);
    r.source = (!ec && !rel.empty() && rel.native()[0] != '.')
                   ? rel.generic_string()
                   : file;
    crSelBlock_ = -1;
    return {};
}

// ---------------------------------------------------------------------------
// Cross-references
// ---------------------------------------------------------------------------

void App::openGraphOf(int scene, int object) {
    if (scene < 0 || scene >= (int)project_.scenes.size()) return;
    if (scene != project_.activeScene) {
        // Switching scenes from a tool window would be a surprise; say where.
        statusMessage_ = "That graph is in scene \"" + project_.scenes[scene].name +
                         "\" - switch to it to open the graph.";
        return;
    }
    if (object < 0 || object >= (int)project_.scenes[scene].objects.size()) return;
    flowGraphObject_ = object;
    flowPositionsApplied_ = false;
    pendingFocusWindow_ = "Flow Graph";
}

std::vector<App::ToolRef> App::creditsStartedBy(const std::string& roll) const {
    std::vector<ToolRef> out;
    if (roll.empty()) return out;
    for (int mi = 0; mi < (int)project_.menus.size(); ++mi)
        for (const MenuEntry& e : project_.menus[mi].entries)
            if (e.action == MenuEntry::PlayCredits && e.param == roll) {
                ToolRef ref;
                ref.text = "the " + (e.label.empty() ? std::string("(unnamed)") : e.label) +
                           " row of the " + project_.menus[mi].name + " menu";
                ref.menu = mi;
                out.push_back(std::move(ref));
            }
    for (int si = 0; si < (int)project_.scenes.size(); ++si) {
        const SceneData& sc = project_.scenes[si];
        for (int oi = 0; oi < (int)sc.objects.size(); ++oi)
            for (const FlowNode& n : sc.objects[oi].flowGraph.nodes) {
                const FlowNodeType* t = flowNodeType(n.type);
                if (t && t->strKind == FlowParamKind::CreditsName && n.str == roll) {
                    ToolRef ref;
                    ref.text = "Play Credits in " + sc.objects[oi].name;
                    ref.scene = si;
                    ref.object = oi;
                    out.push_back(std::move(ref));
                    break;
                }
            }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

namespace {

std::string clockText(float s, bool tenths) {
    if (s < 0.0f) s = 0.0f;
    char buf[32];
    const int m = (int)(s / 60.0f);
    const float rest = s - (float)m * 60.0f;
    if (tenths)
        std::snprintf(buf, sizeof(buf), "%d:%04.1f", m, rest);
    else
        std::snprintf(buf, sizeof(buf), "%d:%02d", m, (int)rest);
    return buf;
}

// Readable on the editor's dark panel: the roll's own heading colour, pulled
// toward the text colour when it is too dark to read there.
ImU32 legibleCol(const float* c) {
    const theme::Semantics& sem = theme::semantics();
    const float lum = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    ImVec4 v(c[0], c[1], c[2], 1.0f);
    if (lum < 0.35f) {
        const float t = 0.6f;
        v = ImVec4(v.x + (sem.text.x - v.x) * t, v.y + (sem.text.y - v.y) * t,
                   v.z + (sem.text.z - v.z) * t, 1.0f);
    }
    return ImGui::GetColorU32(v);
}

}  // namespace

// "When is this block centred on screen": the roll's own arithmetic run
// backwards, so a click scrubs the preview to the frame that answers "does the
// Music row look right" without hunting on the timeline.
float App::creditsBlockTime(const CreditsRoll& r, int i) const {
    if (i < 0 || i >= (int)crLayout_.boxes.size()) return 0.0f;
    const menubake::CreditsBlockBox& b = crLayout_.boxes[i];
    if (r.mode == 1) {
        const int card = crLayout_.pageH > 0 ? b.y / crLayout_.pageH : 0;
        return r.startDelay + ((float)card + 0.5f) * r.cardSeconds;
    }
    const float centre = (float)b.y + (float)b.h * 0.5f;
    const float speed = r.speed > 1.0f ? r.speed : 1.0f;
    return r.startDelay + (centre + 448.0f * 0.5f) / speed;
}

void App::drawCreditsWindow() {
    if (!showCreditsEditor_ || !hasProject_) {
        if (!showCreditsEditor_ && crPreviewValid_) creditsPreviewDrop();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(scaled(1180), scaled(760)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Credits", &showCreditsEditor_)) {
        ImGui::End();
        return;
    }
    const theme::Semantics& sem = theme::semantics();

    bool changed = false;
    auto& rolls = project_.credits;
    if (selectedCredits_ >= (int)rolls.size()) selectedCredits_ = -1;
    // A window that opens on "pick something" when there is exactly one thing
    // to pick is a click wasted - open on the first roll.
    if (selectedCredits_ < 0 && !rolls.empty()) {
        selectedCredits_ = 0;
        crSelBlock_ = -1;
        crPreviewSeek_ = true;
    }

    // Belt and braces: `changed` is set by hand at each widget, so the next
    // one added here can forget it. Comparing the section's serialized form
    // across the whole body cannot be forgotten. The Credits section alone is
    // enough - a roll rename also retargets menu rows and flow nodes, but it
    // renames the roll itself in the same breath, so this catches it too and
    // the window need not re-serialize Menus every frame it is open.
    const std::string beforeSection =
        project::sectionJson(project_, project::Section::Credits);
    auto commitIfEdited = [&] {
        if (changed ||
            project::sectionJson(project_, project::Section::Credits) != beforeSection)
            commitChange();
    };

    auto newRoll = [&] {
        CreditsRoll r;
        int counter = 0;
        for (;;) {
            r.name = counter == 0 ? "credits" : "credits-" + std::to_string(counter);
            bool taken = false;
            for (const auto& o : rolls) taken |= (o.name == r.name);
            if (!taken) break;
            ++counter;
        }
        // A brand-new roll that bakes to nothing is a dead end to look at, so it
        // starts as the skeleton of a real one.
        CreditsBlock h;
        h.kind = CreditsBlock::Heading;
        h.text = "CREDITS";
        CreditsBlock gap;
        gap.kind = CreditsBlock::Gap;
        CreditsBlock pair;
        pair.kind = CreditsBlock::Pair;
        pair.text = "Game design";
        pair.text2 = "Your name";
        r.blocks = {h, gap, pair};
        rolls.push_back(std::move(r));
        selectedCredits_ = (int)rolls.size() - 1;
        crSelBlock_ = -1;
        crPreviewSeek_ = true;
        creditsPreviewDrop();
        changed = true;
    };

    if (rolls.empty()) {
        if (ui::EmptyState(ui::Glyph::Film, "No credits rolls yet",
                           "A roll is started by a menu row or the Play Credits node; it owns "
                           "the screen while it plays, then runs its finish action.",
                           "+ New roll"))
            newRoll();
        commitIfEdited();
        ImGui::End();
        return;
    }

    // The bake first: the block list's timestamps and the timeline come from
    // its layout, so they must describe the roll on screen this frame.
    const bool baked = creditsPreviewRefresh();

    // --- header: the rolls as pills, then who starts the selected one -------
    const float fs = ImGui::GetFontSize();
    for (int i = 0; i < (int)rolls.size(); ++i) {
        const CreditsRoll& ri = rolls[i];
        std::string detail;
        if (ri.mode == 1)
            detail = "cards";
        else
            detail = clockText(creditsDuration(ri), false);
        ImGui::PushID(i);
        if (i > 0) ImGui::SameLine(0.0f, fs * 0.35f);
        if (ui::PillTab(ri.name.c_str(), detail.c_str(), selectedCredits_ == i) &&
            selectedCredits_ != i) {
            selectedCredits_ = i;
            crSelBlock_ = -1;
            crPreviewSeek_ = true;
            creditsPreviewDrop();
        }
        ImGui::PopID();
    }
    ImGui::SameLine(0.0f, fs * 0.35f);
    if (ui::ToolIconButton("+ New roll", ui::ToolIcon::Plus)) newRoll();
    if (selectedCredits_ < 0) {  // the + above can only add; nothing else unselects
        commitIfEdited();
        ImGui::End();
        return;
    }
    CreditsRoll& r = rolls[selectedCredits_];
    if (crSelBlock_ >= (int)r.blocks.size()) crSelBlock_ = -1;

    // Who plays this roll. An orphan roll is a real mistake (it can never be
    // seen), so it says so in the warning colour.
    {
        const std::vector<ToolRef> refs = creditsStartedBy(r.name);
        std::string line;
        if (refs.empty())
            line = "Nothing starts this roll";
        else if (refs.size() == 1)
            line = "Started by " + refs[0].text;
        else
            line = "Started by " + refs[0].text + " +" + std::to_string(refs.size() - 1);
        const float moreW = ImGui::GetFrameHeight();
        const float lineW = ImGui::CalcTextSize(line.c_str()).x;
        const float right = ImGui::GetWindowContentRegionMax().x;
        const float x = right - moreW - fs * 0.6f - lineW;
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() < x) ImGui::SetCursorPosX(x);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, refs.empty() ? sem.warn : sem.textDim);
        if (refs.empty()) {
            ImGui::TextUnformatted(line.c_str());
        } else if (ImGui::TextLink(line.c_str())) {
            const ToolRef& f = refs[0];
            if (f.menu >= 0) {
                showMenusEditor_ = true;
                selectedMenu_ = f.menu;
                pendingFocusWindow_ = "Menu Editor";
            } else {
                openGraphOf(f.scene, f.object);
            }
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            std::string tip;
            if (refs.empty())
                tip = "No menu row (action Play credits) and no Play Credits node\n"
                      "names this roll, so the game never shows it.";
            for (const ToolRef& f : refs) tip += f.text + "\n";
            ImGui::SetTooltip("%s", tip.c_str());
        }
        ImGui::SameLine(right - moreW + ImGui::GetStyle().WindowPadding.x * 0.0f);
        if (ui::MoreButton("Roll actions", moreW)) ImGui::OpenPopup("##cr_more");
    }
    bool rollGone = false;
    if (ImGui::BeginPopup("##cr_more")) {
        std::string name = r.name;
        if (creditsInput("Name", name, scaled(200))) {
            // Keep every reference pointing at the renamed roll: menu rows, the
            // Play Credits nodes, and the finish targets of other rolls.
            for (GameMenu& m : project_.menus)
                for (MenuEntry& e : m.entries)
                    if (e.action == MenuEntry::PlayCredits && e.param == r.name)
                        e.param = name;
            for (SceneData& sc : project_.scenes)
                for (SceneObject& o : sc.objects)
                    for (FlowNode& n : o.flowGraph.nodes) {
                        const FlowNodeType* t = flowNodeType(n.type);
                        if (t && t->strKind == FlowParamKind::CreditsName && n.str == r.name)
                            n.str = name;
                    }
            r.name = name;
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::Separator();
        if (ImGui::Selectable("Import text...")) crImportRequest_ = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Replaces every block from a text file:\n"
                "  # SECTION           a heading\n"
                "  Role: Name          a role/name row\n"
                "  > centered line     (\"< \" left, \"| \" right)\n"
                "  [image res/credits/logo.png 0.5]\n"
                "  ---                 next page / next card\n"
                "  (blank line)        a gap\n"
                "See docs/credits.md.");
        if (ImGui::Selectable("Duplicate")) {
            CreditsRoll copy = r;
            const std::string base = copy.name;
            for (int n = 2;; ++n) {
                copy.name = base + "-" + std::to_string(n);
                bool taken = false;
                for (const auto& o : rolls) taken |= (o.name == copy.name);
                if (!taken) break;
            }
            rolls.push_back(std::move(copy));
            selectedCredits_ = (int)rolls.size() - 1;
            crPreviewSeek_ = true;
            creditsPreviewDrop();
            changed = true;
            rollGone = true;  // `r` may dangle after the push_back
        }
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
        if (!rollGone && ImGui::Selectable("Delete roll")) {
            rolls.erase(rolls.begin() + selectedCredits_);
            selectedCredits_ = rolls.empty() ? -1 : 0;
            crSelBlock_ = -1;
            crPreviewSeek_ = true;
            creditsPreviewDrop();
            changed = true;
            rollGone = true;
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    if (rollGone) {
        commitIfEdited();
        ImGui::End();
        return;
    }
    if (crImportRequest_) {
        crImportRequest_ = false;
        const std::string file = platform::pickFile(
            "Import credits text",
            {{"Text file (*.txt, *.md, *.csv)", {"*.txt", "*.md", "*.csv"}},
             {"All files (*.*)", {"*.*"}}});
        if (!file.empty()) {
            const std::string err = creditsImportFile(r, file);
            if (err.empty()) {
                statusMessage_ = "Imported " + std::to_string(r.blocks.size()) + " credit blocks";
                crPreviewSeek_ = true;
                changed = true;
            } else {
                statusMessage_ = err;
            }
        }
    }

    // The preview opens on something to look at: t = 0 is inside the start
    // delay and the fade-in, so it used to open on an empty box.
    const float total = creditsDuration(r);
    if (crPreviewSeek_ && baked && crPreviewValid_ && crPreviewRoll_ == r) {
        crPreviewSeek_ = false;
        float t = r.startDelay + r.fadeIn;
        for (int i = 0; i < (int)r.blocks.size(); ++i) {
            const int k = r.blocks[i].kind;
            if (k == CreditsBlock::Gap || k == CreditsBlock::Break) continue;
            t = std::max(t, creditsBlockTime(r, i));
            break;
        }
        crPreviewTime_ = std::min(t, total);
    }

    // --- body: block list | preview | settings ------------------------------
    ImGui::Spacing();
    const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
    const float bodyH = bodyAvail.y;
    const float rightW = std::min(scaled(320.0f), bodyAvail.x * 0.34f);
    creditsSplit_ = std::clamp(creditsSplit_, 0.18f, 0.36f);
    float leftW = std::max(scaled(220.0f), bodyAvail.x * creditsSplit_);

    // --- left: the blocks, written the way the roll reads -------------------
    ImGui::BeginChild("##cr_blocks", ImVec2(leftW, bodyH));
    {
        // Where the blocks came from, and the honest warning that importing
        // again throws hand edits away.
        if (!r.source.empty()) {
            const std::string file = fs::path(r.source).filename().string();
            ui::Badge(file.c_str(), 4);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Imported from %s", r.source.c_str());
            ImGui::SameLine();
            if (ui::GhostButton("Re-import")) {
                const std::string abs = fs::path(r.source).is_absolute()
                                            ? r.source
                                            : project_.filePath(r.source);
                const std::string err = creditsImportFile(r, abs);
                statusMessage_ = err.empty() ? "Re-imported " + r.source : err;
                if (err.empty()) {
                    crPreviewSeek_ = true;
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Re-read %s. Replaces every block - edits made here are lost.",
                                  r.source.c_str());
        } else {
            if (ui::GhostButton("Import text...")) crImportRequest_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Replaces every block with the lines of a text file\n"
                                  "(docs/credits.md has the markup).");
        }
        ImGui::SameLine();
        {
            const char* warn = "replaces all blocks";
            const float ww = ImGui::CalcTextSize(warn).x;
            const float x = ImGui::GetContentRegionMax().x - ww;
            if (ImGui::GetCursorPosX() < x) ImGui::SetCursorPosX(x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", warn);
        }

        const float addRowsH = ImGui::GetFrameHeightWithSpacing() * 2.0f + fs * 0.6f;
        ImGui::BeginChild("##cr_stack", ImVec2(0, -addRowsH));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float stampW = 0.0f;
        {
            ui::FontScope mono(ui::monoFont());
            stampW = ImGui::CalcTextSize("0:00").x;
        }
        // The role column: as wide as the widest role, capped so names keep room.
        const float rowW = ImGui::GetContentRegionAvail().x;
        float roleW = 0.0f;
        for (const CreditsBlock& b : r.blocks)
            if (b.kind == CreditsBlock::Pair)
                roleW = std::max(roleW, ImGui::CalcTextSize(b.text.c_str()).x);
        roleW = std::min(roleW, (rowW - stampW) * 0.42f);
        const ImU32 headCol = legibleCol(r.headingColor);
        for (int i = 0; i < (int)r.blocks.size(); ++i) {
            const CreditsBlock& b = r.blocks[i];
            const bool sel = crSelBlock_ == i;
            const bool clipped =
                i < (int)crLayout_.boxes.size() && crLayout_.boxes[i].clipped;
            float h = ImGui::GetFrameHeight();
            if (b.kind == CreditsBlock::Gap) h = fs * 0.75f;
            if (b.kind == CreditsBlock::Break) h = fs * 1.6f;
            ImGui::PushID(i);
            const std::string id = creditsBlockLabel(b);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const bool click = ImGui::InvisibleButton(id.c_str(), ImVec2(std::max(1.0f, rowW), h));
            const bool hov = ImGui::IsItemHovered();
            if (click) {
                crSelBlock_ = i;
                crTab_ = 2;
                crTabForce_ = true;
                if (b.kind != CreditsBlock::Gap && b.kind != CreditsBlock::Break)
                    crPreviewTime_ = creditsBlockTime(r, i);
            }
            if (clipped && hov)
                ImGui::SetTooltip("Past the page budget - not baked, never shows.");
            const ImVec2 q(p.x + rowW, p.y + h);
            const float rr = ImGui::GetStyle().FrameRounding;
            if (sel) {
                dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.16f)), rr);
                dl->AddRect(p, q, ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.4f)), rr);
            } else if (hov) {
                dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.8f)), rr);
            }
            const float pad = fs * 0.45f;
            const float cx = p.x + pad + stampW + fs * 0.7f;
            const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
            const ImU32 dim = ImGui::GetColorU32(sem.textDim);
            const ImU32 ink = clipped ? ImGui::GetColorU32(sem.warn) : ImGui::GetColorU32(sem.text);
            dl->PushClipRect(p, q, true);
            if (b.kind == CreditsBlock::Gap) {
                // A dotted spacer, as tall as nothing.
                const float y = p.y + h * 0.5f;
                for (float x = cx; x < q.x - pad; x += fs * 0.5f)
                    dl->AddLine(ImVec2(x, y), ImVec2(x + fs * 0.25f, y),
                                ImGui::GetColorU32(ImVec4(sem.textDim.x, sem.textDim.y, sem.textDim.z, 0.35f)),
                                1.0f);
            } else if (b.kind == CreditsBlock::Break) {
                const char* t = r.mode == 1 ? "NEXT CARD" : "PAGE BREAK";
                // A dashed rule with the break named in its middle.
                const float tw = ImGui::CalcTextSize(t).x;
                const float tx = cx + (q.x - pad - cx - tw) * 0.5f;
                const float y = p.y + h * 0.5f;
                for (float x = cx; x < q.x - pad; x += fs * 0.6f)
                    if (x + fs * 0.3f < tx - fs * 0.5f || x > tx + tw + fs * 0.5f)
                        dl->AddLine(ImVec2(x, y), ImVec2(x + fs * 0.3f, y), dim, 1.0f);
                dl->AddText(ImVec2(tx, ty), dim, t);
            } else {
                {
                    ui::FontScope mono(ui::monoFont());
                    dl->AddText(ImVec2(p.x + pad, ty), dim,
                                clockText(creditsBlockTime(r, i), false).c_str());
                }
                if (b.kind == CreditsBlock::Heading) {
                    ui::FontScope bold(ui::boldFont());
                    dl->AddText(ImVec2(cx, ty), clipped ? ink : headCol,
                                b.text.empty() ? "(heading)" : b.text.c_str());
                } else if (b.kind == CreditsBlock::Pair) {
                    const char* role = b.text.empty() ? "(role)" : b.text.c_str();
                    const float rw = ImGui::CalcTextSize(role).x;
                    dl->AddText(ImVec2(cx + std::max(0.0f, roleW - rw), ty), dim, role);
                    std::string names = b.text2.empty() ? "(name)" : b.text2;
                    for (size_t k = 0; (k = names.find('\n', k)) != std::string::npos;)
                        names.replace(k, 1, ", ");
                    dl->AddText(ImVec2(cx + roleW + fs * 0.6f, ty), ink, names.c_str());
                } else if (b.kind == CreditsBlock::Image) {
                    const std::string f = b.imagePath.empty()
                                              ? std::string("(no image)")
                                              : fs::path(b.imagePath).filename().string();
                    dl->AddText(ImVec2(cx, ty), ImGui::GetColorU32(sem.special), f.c_str());
                    char pct[32];
                    std::snprintf(pct, sizeof(pct), "%d%% of width", (int)(b.scale * 100.0f + 0.5f));
                    dl->AddText(ImVec2(cx + ImGui::CalcTextSize(f.c_str()).x + fs * 0.6f, ty), dim, pct);
                } else {
                    dl->AddText(ImVec2(cx, ty), ink, b.text.empty() ? "(empty line)" : b.text.c_str());
                }
            }
            dl->PopClipRect();
            ImGui::PopID();
        }
        if (r.blocks.empty())
            ImGui::TextDisabled("No blocks - add one below or import a text file.");
        ImGui::EndChild();

        // Add: the new block lands after the selection, which is what makes
        // building a roll top-to-bottom a matter of clicking one button.
        auto addBlock = [&](int kind) {
            CreditsBlock b;
            b.kind = kind;
            if (kind == CreditsBlock::Heading) b.text = "SECTION";
            if (kind == CreditsBlock::Line) b.text = "Line of text";
            if (kind == CreditsBlock::Pair) {
                b.text = "Role";
                b.text2 = "Name";
            }
            const int at = crSelBlock_ >= 0 ? crSelBlock_ + 1 : (int)r.blocks.size();
            r.blocks.insert(r.blocks.begin() + at, b);
            crSelBlock_ = at;
            crTab_ = 2;
            crTabForce_ = true;
            changed = true;
        };
        ImGui::Spacing();
        static const char* const kAdd[] = {"+ Heading", "+ Role / name", "+ Line",
                                           "+ Image",   "+ Gap",         "+ Page break"};
        static const int kAddKind[] = {CreditsBlock::Heading, CreditsBlock::Pair,
                                       CreditsBlock::Line,    CreditsBlock::Image,
                                       CreditsBlock::Gap,     CreditsBlock::Break};
        for (int k = 0; k < 6; ++k) {
            ui::ChipFlow(ui::ChipButtonWidth(kAdd[k]), k == 0);
            if (ui::ChipButton(kAdd[k])) addBlock(kAddKind[k]);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    {
        float lw = leftW;
        ui::VSplitter("##cr_split", &lw, scaled(200.0f), rightW + scaled(260.0f), bodyH);
        if (lw != leftW && bodyAvail.x > 1.0f) creditsSplit_ = lw / bodyAvail.x;
        if (ImGui::IsItemDeactivated()) saveGlobalConfig();  // persist the split
        leftW = lw;
    }

    // --- centre: the preview, the transport and the timeline ----------------
    const float centreW = std::max(scaled(200.0f), ImGui::GetContentRegionAvail().x - rightW);
    ImGui::BeginChild("##cr_preview", ImVec2(centreW, bodyH));
    {
        if (crPreviewPlaying_) {
            crPreviewTime_ += ImGui::GetIO().DeltaTime;
            if (crPreviewTime_ > total) crPreviewTime_ = 0.0f;
        }
        const float transportH = ImGui::GetFrameHeightWithSpacing() + fs * 1.9f +
                                 ImGui::GetTextLineHeightWithSpacing() + fs * 1.2f;
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float margin = fs * 1.0f;
        float pw = avail.x - margin * 2.0f;
        float ph = pw * 448.0f / 512.0f;  // the PS2 frame's aspect
        const float maxPh = avail.y - transportH - margin * 2.0f;
        if (ph > maxPh && maxPh > scaled(60.0f)) {
            ph = maxPh;
            pw = ph * 512.0f / 448.0f;
        }
        const ImVec2 c0 = ImGui::GetCursorScreenPos();
        const ImVec2 origin(c0.x + (avail.x - pw) * 0.5f,
                            c0.y + std::max(margin, (avail.y - transportH - ph) * 0.5f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float sx = pw / 512.0f, sy = ph / 448.0f;
        auto col = [](const float* c, float a) {
            return IM_COL32((int)(c[0] * 255.0f), (int)(c[1] * 255.0f), (int)(c[2] * 255.0f),
                            (int)(a * 255.0f));
        };
        const float rr = ImGui::GetStyle().FrameRounding;
        dl->AddRectFilled(origin, ImVec2(origin.x + pw, origin.y + ph), col(r.bgColor, 1.0f), rr);
        if (!baked) {
            dl->AddText(ImVec2(origin.x + scaled(8), origin.y + scaled(8)),
                        ImGui::GetColorU32(sem.danger), "No usable TTF font found");
        } else {
            // The pages here ARE the baked pages, placed by the runtime's own
            // arithmetic - the strip's top enters from the bottom of the screen
            // and walks up, so what plays here is what plays on the console.
            dl->PushClipRect(origin, ImVec2(origin.x + pw, origin.y + ph), true);
            if (!r.bgImage.imagePath.empty()) {
                if (const HudTexture* t = hudTexture(r.bgImage.imagePath)) {
                    const float bw = r.bgImage.size[0] * sx, bh = r.bgImage.size[1] * sy;
                    const ImVec2 tl(origin.x + r.bgImage.pos[0] * pw - bw * 0.5f,
                                    origin.y + r.bgImage.pos[1] * ph - bh * 0.5f);
                    dl->AddImage((ImTextureID)(intptr_t)t->tex, tl, ImVec2(tl.x + bw, tl.y + bh));
                }
            }
            const float pageW = (float)crPreviewPageW_ * sx;
            const float pageH = (float)crPreviewPageH_ * sy;
            const float px = origin.x + (pw - pageW) * 0.5f;
            const float elapsed = crPreviewTime_ - r.startDelay;
            if (elapsed >= 0.0f) {
                if (r.mode == 1) {
                    int card = r.cardSeconds > 0.0f ? (int)(elapsed / r.cardSeconds) : 0;
                    if (card >= (int)crPreviewTex_.size()) card = (int)crPreviewTex_.size() - 1;
                    if (card >= 0 && !crPreviewTex_.empty()) {
                        const ImVec2 tl(px, origin.y + (ph - pageH) * 0.5f);
                        dl->AddImage((ImTextureID)(intptr_t)crPreviewTex_[card], tl,
                                     ImVec2(tl.x + pageW, tl.y + pageH));
                    }
                } else {
                    const float scroll = elapsed * r.speed * sy;
                    for (int k = 0; k < (int)crPreviewTex_.size(); ++k) {
                        const float y = origin.y + ph - scroll + (float)k * pageH;
                        if (y >= origin.y + ph || y + pageH <= origin.y) continue;
                        dl->AddImage((ImTextureID)(intptr_t)crPreviewTex_[k], ImVec2(px, y),
                                     ImVec2(px + pageW, y + pageH));
                    }
                }
                if (r.skippable && r.showSkipHint && !r.skipHint.empty()) {
                    const HudText hint = menubake::creditsHintText(r);
                    if (const HudTexture* t = hudTextTexture(hint)) {
                        const float hw = (float)t->w * sx, hh = (float)t->h * sy;
                        const ImVec2 tl(origin.x + r.hintPos[0] * pw - hw * 0.5f,
                                        origin.y + r.hintPos[1] * ph - hh * 0.5f);
                        dl->AddImage((ImTextureID)(intptr_t)t->tex, tl, ImVec2(tl.x + hw, tl.y + hh));
                    }
                }
            }
            // The fades, drawn the way the runtime composites them: one tinted
            // quad of the background color over everything.
            float fade = 0.0f;
            if (r.fadeIn > 0.0f && crPreviewTime_ < r.fadeIn) fade = 1.0f - crPreviewTime_ / r.fadeIn;
            const float endsAt = total - r.endHold;
            if (r.fadeOut > 0.0f && crPreviewTime_ > endsAt) {
                const float o = (crPreviewTime_ - endsAt) / r.fadeOut;
                if (o > fade) fade = o;
            }
            if (fade > 0.0f)
                dl->AddRectFilled(origin, ImVec2(origin.x + pw, origin.y + ph),
                                  col(r.bgColor, fade > 1.0f ? 1.0f : fade));
            dl->PopClipRect();
        }
        dl->AddRect(origin, ImVec2(origin.x + pw, origin.y + ph), ImGui::GetColorU32(sem.border), rr);
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(pw, ph));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The baked pages, at %.0f%% of PS2 size", sx * 100.0f);

        // Transport: play, restart, the clock, and what the roll costs.
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + ph + fs * 0.8f));
        if (ui::ToolIconButton(crPreviewPlaying_ ? "Pause" : "Play",
                               crPreviewPlaying_ ? ui::ToolIcon::Pause : ui::ToolIcon::Play))
            crPreviewPlaying_ = !crPreviewPlaying_;
        ImGui::SameLine();
        if (ui::ToolIconButton("Rewind", ui::ToolIcon::Restart)) crPreviewTime_ = 0.0f;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::TextUnformatted(clockText(crPreviewTime_, true).c_str());
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::TextDisabled("/ %s", clockText(total, false).c_str());
        }
        {
            // The honest cost report: pages and what they take of the GS
            // texture budget (docs/gs-vram.md). A roll that cannot fit says so
            // here rather than flushing textures mid-scroll in-game.
            const int bpp = r.quant == "none" ? 32 : (r.quant == "8bit" ? 8 : 4);
            const float kb = (float)crLayout_.pageCount *
                             ((float)crLayout_.pageW * crLayout_.pageH * bpp / 8.0f / 1024.0f + 8.0f);
            char cost[96];
            std::snprintf(cost, sizeof(cost), "%d/%d pages · %.0f KB VRAM", crLayout_.pageCount,
                          menubake::kCreditsMaxPages, kb);
            const char* clip = crLayout_.clipped ? "content clipped" : nullptr;
            float w = ImGui::CalcTextSize(cost).x;
            if (clip) w += ImGui::CalcTextSize(clip).x + fs;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), origin.x - ImGui::GetWindowPos().x + pw - w));
            ImGui::TextDisabled("%s", cost);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Pages are the roll baked into textures. The console pins each one it\n"
                    "draws in a ~1.33 MB budget, so a very long roll wants a slower speed,\n"
                    "card mode, 256 px pages or a second roll - see docs/credits.md.");
            if (clip) {
                ImGui::SameLine();
                ImGui::TextColored(sem.warn, "%s", clip);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "The roll is longer than %d pages (or a card is taller than one\n"
                        "screen). Everything past that is NOT baked and never shows.",
                        menubake::kCreditsMaxPages);
            }
        }

        // The timeline: one tick per block at the moment it is centred, the
        // start delay and end hold shaded, the playhead; click or drag scrubs.
        ImGui::SetCursorScreenPos(ImVec2(origin.x, ImGui::GetCursorScreenPos().y + fs * 0.2f));
        {
            const float tw = pw, th = fs * 1.9f;
            const ImVec2 t0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("Timeline", ImVec2(tw, th));
            const float span = total > 0.01f ? total : 1.0f;
            if (ImGui::IsItemActive()) {
                const float f = (ImGui::GetIO().MousePos.x - t0.x) / tw;
                crPreviewTime_ = std::clamp(f, 0.0f, 1.0f) * span;
            }
            const ImVec2 t1(t0.x + tw, t0.y + th);
            dl->AddRectFilled(t0, t1, ImGui::GetColorU32(sem.field), rr);
            const ImU32 shade = ImGui::GetColorU32(ImVec4(sem.text.x, sem.text.y, sem.text.z, 0.05f));
            if (r.startDelay > 0.0f)
                dl->AddRectFilled(t0, ImVec2(t0.x + tw * std::min(1.0f, r.startDelay / span), t1.y), shade,
                                  rr, ImDrawFlags_RoundCornersLeft);
            if (r.endHold > 0.0f)
                dl->AddRectFilled(ImVec2(t0.x + tw * std::max(0.0f, 1.0f - r.endHold / span), t0.y), t1,
                                  shade, rr, ImDrawFlags_RoundCornersRight);
            const ImU32 headCol = legibleCol(r.headingColor);
            for (int i = 0; i < (int)r.blocks.size(); ++i) {
                const int k = r.blocks[i].kind;
                if (k == CreditsBlock::Gap || k == CreditsBlock::Break) continue;
                const float x = t0.x + tw * std::clamp(creditsBlockTime(r, i) / span, 0.0f, 1.0f);
                const bool head = k == CreditsBlock::Heading;
                const float inset = head ? th * 0.22f : th * 0.34f;
                const ImU32 c = head ? headCol
                                     : k == CreditsBlock::Image ? ImGui::GetColorU32(sem.special)
                                                                : ImGui::GetColorU32(sem.textDim);
                dl->AddRectFilled(ImVec2(x - 1.2f, t0.y + inset), ImVec2(x + 1.2f, t1.y - inset), c, 1.0f);
                if (crSelBlock_ == i)
                    dl->AddRect(ImVec2(x - 3.0f, t0.y + inset - 2.0f), ImVec2(x + 3.0f, t1.y - inset + 2.0f),
                                ImGui::GetColorU32(sem.accent), 2.0f);
            }
            const float hx = t0.x + tw * std::clamp(crPreviewTime_ / span, 0.0f, 1.0f);
            dl->AddLine(ImVec2(hx, t0.y - 2.0f), ImVec2(hx, t1.y + 2.0f), ImGui::GetColorU32(sem.accent), 2.0f);
            if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
                ImGui::SetTooltip("Start delay %.1f s · end hold %.1f s - click or drag to scrub",
                                  r.startDelay, r.endHold);

            // Legend.
            ImGui::SetCursorScreenPos(ImVec2(t0.x, t1.y + fs * 0.4f));
            auto key = [&](ImU32 c, const char* t) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float d = fs * 0.5f;
                const float y = p.y + (ImGui::GetTextLineHeight() - d) * 0.5f;
                dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + d, y + d), c, 2.0f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + d + fs * 0.35f, p.y));
                ImGui::TextDisabled("%s", t);
                ImGui::SameLine(0.0f, fs * 0.9f);
            };
            key(headCol, "heading");
            key(ImGui::GetColorU32(sem.textDim), "credit");
            key(ImGui::GetColorU32(sem.special), "image");
            const char* hint = "Click a block to jump the preview to it";
            const float hw = ImGui::CalcTextSize(hint).x;
            const float hx0 = t0.x - ImGui::GetWindowPos().x + tw - hw;
            if (hx0 > ImGui::GetCursorPosX()) {  // only when it fits beside the legend
                ImGui::SameLine(hx0);
                ImGui::TextDisabled("%s", hint);
            } else {
                ImGui::NewLine();
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- right: Look / Playback / Block -------------------------------------
    ImGui::BeginChild("##cr_props", ImVec2(0, bodyH));
    if (ui::BeginQuietTabBar("##cr_tabs")) {
        static const char* const kTabs[] = {"Look", "Playback", "Block"};
        const bool force = crTabForce_;
        for (int i = 0; i < 3; ++i) {
            const ImGuiTabItemFlags f = force && i == crTab_ ? ImGuiTabItemFlags_SetSelected : 0;
            if (ui::QuietTab(kTabs[i], i == crTab_, f) && !force) crTab_ = i;
        }
        crTabForce_ = false;
        ui::EndQuietTabBar();
    }
    // The tab body is its own child: a fresh ID seed, so every field below is
    // nameable by --ui-script, and it scrolls without taking the tabs along.
    ImGui::BeginChild("##cr_tabbody", ImVec2(0, 0));
    // fontCombo (shared with other windows) is a stock combo with its label on
    // the right; as a row it gets its caption on the left and the stock label
    // clipped away, like the ui:: row wrappers do.
    auto rowFontCombo = [&](const char* caption, std::string& font) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float labelW = std::clamp(avail / 3.0f, scaled(84.0f), scaled(118.0f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", caption);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + labelW, p.y));
        ImGui::PushClipRect(ImVec2(p.x + labelW, p.y),
                            ImVec2(p.x + labelW + std::min(avail - labelW, scaled(200.0f)),
                                   p.y + ImGui::GetFrameHeight()),
                            true);
        ImGui::SetNextItemWidth(avail - labelW);
        const bool c = fontCombo(font);
        ImGui::PopClipRect();
        return c;
    };
    {
        ui::InspectorRows rows(scaled(84.0f), scaled(118.0f));
        if (crTab_ == 0) {
            // --- Look -------------------------------------------------------
            ui::Caption("Colours");
            if (ui::ColorHex("Background", r.bgColor)) changed = true;
            if (ui::ColorHex("Text", r.color)) changed = true;
            if (ui::ColorHex("Headings", r.headingColor)) changed = true;
            ImGui::Spacing();
            ui::Caption("Type");
            if (rowFontCombo("Font", r.font)) changed = true;
            if (ui::DragInt("Heading size", &r.headingSize, 0.5f, 8, 64, "%d px")) changed = true;
            if (ui::DragInt("Body size", &r.lineSize, 0.5f, 8, 64, "%d px")) changed = true;
            if (ui::DragFloat("Line spacing", &r.lineSpacing, 0.01f, 0.8f, 3.0f, "%.2f")) changed = true;
            if (ui::Switch("Text shadow", &r.shadow)) changed = true;
            ImGui::Spacing();
            ui::Caption("Page");
            int pageIdx = r.pageW == 256 ? 0 : 1;
            const char* pageWidths[] = {"256 px (half screen)", "512 px (full screen)"};
            if (ui::Combo("Page width", &pageIdx, pageWidths, 2)) {
                r.pageW = pageIdx == 0 ? 256 : 512;
                changed = true;
            }
            ui::RowHelp("The width of the baked page textures. 256 halves their VRAM\n"
                        "cost and is plenty for centered credits; 512 gives full-width\n"
                        "role/name columns.");
            if (ui::DragFloat("Side margin", &r.margin, 0.5f, 0.0f, 200.0f, "%.0f px")) changed = true;
            if (ui::DragFloat("Column gap", &r.columnGap, 0.5f, 0.0f, 200.0f, "%.0f px")) changed = true;
            const char* quants[] = {"4-bit (16 colors)", "8-bit (256 colors)", "Full color"};
            int qi = r.quant == "none" ? 2 : (r.quant == "8bit" ? 1 : 0);
            if (ui::Combo("Page depth", &qi, quants, 3)) {
                r.quant = qi == 2 ? "none" : (qi == 1 ? "8bit" : "4bit");
                changed = true;
            }
            ui::RowHelp("Palette depth of the baked pages. Text on a flat background\n"
                        "needs 16 colors; a backdrop makes the pages transparent and\n"
                        "usually wants 8-bit. Full color costs 8x the VRAM of 4-bit.");
            ImGui::Spacing();
            ui::Caption("Backdrop");
            ImGui::TextDisabled("%s", r.bgImage.imagePath.empty() ? "None - the flat background colour"
                                                                   : r.bgImage.imagePath.c_str());
            if (ui::ChipButton("Import backdrop...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const fs::path srcPath(src);
                    const std::string file = sanitizeAssetName(srcPath.filename().string());
                    const fs::path destDir = fs::path(project_.dir) / "res" / "hud";
                    std::error_code ec;
                    fs::create_directories(destDir, ec);
                    fs::copy_file(srcPath, destDir / file, fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        statusMessage_ = "Backdrop import failed: " + ec.message();
                    } else {
                        r.bgImage.imagePath = "res/hud/" + file;
                        r.bgImage.name = srcPath.stem().string();
                        r.bgImage.size[0] = 512.0f;
                        r.bgImage.size[1] = 448.0f;
                        hudTexCache_.erase(r.bgImage.imagePath);
                        changed = true;
                    }
                }
            }
            if (!r.bgImage.imagePath.empty()) {
                ImGui::SameLine();
                if (ui::DangerButton("Remove backdrop")) {
                    r.bgImage.imagePath.clear();
                    changed = true;
                }
                if (ui::DragFloat2("Backdrop size", r.bgImage.size, 1.0f, 8.0f, 512.0f, "%.0f px"))
                    changed = true;
                if (ui::DragFloat2("Backdrop pos", r.bgImage.pos, 0.005f, 0.0f, 1.0f, "%.3f"))
                    changed = true;
            }
        } else if (crTab_ == 1) {
            // --- Playback ---------------------------------------------------
            ui::Caption("Motion");
            static const char* const kModes[] = {"Scroll up", "Cards"};
            if (ui::Segmented("##cr_mode", kModes, 2, &r.mode, -1.0f)) changed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cards split the roll at every page break and show each for\n"
                                  "its own time, cross-fading - for title cards and dedications.");
            if (r.mode == 0) {
                if (ui::DragFloat("Speed", &r.speed, 0.2f, 1.0f, 200.0f, "%.0f px/s")) changed = true;
            } else {
                if (ui::DragFloat("Card time", &r.cardSeconds, 0.05f, 0.5f, 30.0f, "%.2f s"))
                    changed = true;
            }
            if (ui::DragFloat("Start delay", &r.startDelay, 0.02f, 0.0f, 10.0f, "%.2f s")) changed = true;
            if (ui::DragFloat("End hold", &r.endHold, 0.02f, 0.0f, 20.0f, "%.2f s")) changed = true;
            if (ui::DragFloat("Fade in", &r.fadeIn, 0.02f, 0.0f, 10.0f, "%.2f s")) changed = true;
            if (ui::DragFloat("Fade out", &r.fadeOut, 0.02f, 0.0f, 10.0f, "%.2f s")) changed = true;

            ImGui::Spacing();
            ui::Caption("Music");
            if (ui::BeginCombo("Track", r.music.empty() ? "Keep what is playing" : r.music.c_str())) {
                if (ImGui::Selectable("Keep what is playing", r.music.empty())) {
                    r.music.clear();
                    changed = true;
                }
                for (const std::string& track : project_.music)
                    if (ImGui::Selectable(track.c_str(), track == r.music)) {
                        r.music = track;
                        changed = true;
                    }
                if (project_.music.empty())
                    ImGui::TextDisabled("Import WAVs in the Project panel (Audio).");
                ImGui::EndCombo();
            }
            ui::RowHelp("Started when the roll begins. Keep what is playing leaves the\n"
                        "game's music alone.");
            if (!r.music.empty()) {
                if (ui::Switch("Loop", &r.musicLoop)) changed = true;
                if (ui::Switch("Stop at the end", &r.musicStopAtEnd)) changed = true;
                if (ui::SliderInt("Volume", &r.musicVolume, 0, 100)) changed = true;
            }

            ImGui::Spacing();
            ui::Caption("Skipping");
            if (ui::Switch("Player can skip", &r.skippable)) changed = true;
            if (r.skippable) {
                {
                    char sum[160];
                    std::snprintf(sum, sizeof(sum), "after %.1f s · %s · %s", r.skipAfter,
                                  r.skipAction.empty() ? "confirm or Start" : r.skipAction.c_str(),
                                  r.showSkipHint ? "hint shown" : "no hint");
                    ImGui::TextDisabled("%s", sum);
                }
                if (ui::BeginCombo("Button", r.skipAction.empty() ? "<confirm or start>"
                                                                   : r.skipAction.c_str())) {
                    if (ImGui::Selectable("<confirm or start>", r.skipAction.empty())) {
                        r.skipAction.clear();
                        changed = true;
                    }
                    for (const InputAction& a : project_.input.actions)
                        if (ImGui::Selectable(a.name.c_str(), a.name == r.skipAction)) {
                            r.skipAction = a.name;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
                ui::RowHelp("An action from Tools > Input Map, so the skip follows a\n"
                            "rebind. The default accepts the menu confirm action and Start.");
                if (ui::DragFloat("Ignore for", &r.skipAfter, 0.02f, 0.0f, 30.0f, "%.2f s")) changed = true;
                ui::RowHelp("A held button from the moment before the roll started\n"
                            "should not skip it instantly.");
                if (ui::Switch("Show hint", &r.showSkipHint)) changed = true;
                if (r.showSkipHint) {
                    creditsInput("Hint", r.skipHint, -1);
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    ui::RowHelp("Baked text, so {{confirm}} draws the button bound at\n"
                                "BUILD time (docs/text-icons.md).");
                    if (ui::DragFloat2("Hint pos", r.hintPos, 0.005f, 0.0f, 1.0f, "%.3f")) changed = true;
                    if (ui::DragInt("Hint size", &r.hintSize, 0.5f, 8, 40, "%d px")) changed = true;
                }
            }

            ImGui::Spacing();
            ui::Caption("When it ends");
            const char* finishes[] = {"Resume the game", "Switch scene", "Open menu", "Fire flow event",
                                      "Hold the last frame"};
            if (ui::Combo("Then", &r.finish, finishes, 5)) {
                r.finishParam.clear();
                changed = true;
            }
            if (r.finish == CreditsRoll::SwitchScene) {
                if (ui::BeginCombo("Scene", r.finishParam.empty() ? "<none>" : r.finishParam.c_str())) {
                    for (const SceneData& sc : project_.scenes)
                        if (ImGui::Selectable(sc.name.c_str(), sc.name == r.finishParam)) {
                            r.finishParam = sc.name;
                            changed = true;
                        }
                    ImGui::EndCombo();
                }
            } else if (r.finish == CreditsRoll::OpenMenu) {
                if (ui::BeginCombo("Menu", r.finishParam.empty() ? "<none>" : r.finishParam.c_str())) {
                    for (const GameMenu& m : project_.menus)
                        if (ImGui::Selectable(m.name.c_str(), m.name == r.finishParam)) {
                            r.finishParam = m.name;
                            changed = true;
                        }
                    if (project_.menus.empty()) ImGui::TextDisabled("Add menus in Tools > Menu Editor.");
                    ImGui::EndCombo();
                }
            } else if (r.finish == CreditsRoll::FlowEvent) {
                creditsInput("Event", r.finishParam, -1);
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ui::RowHelp("Caught by an On Menu Event trigger with the same name.");
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", r.finish == CreditsRoll::Hold
                                          ? "Nothing follows: the last frame stays on screen. "
                                            "On Credits Finished still fires."
                                          : "Skipping runs this too. On Credits Finished fires "
                                            "either way.");
            ImGui::PopTextWrapPos();
        } else {
            // --- Block -------------------------------------------------------
            if (crSelBlock_ < 0) {
                ui::EmptyState(ui::Glyph::List, "No block selected",
                               "Pick a block in the list - the preview jumps to it.");
            } else {
                CreditsBlock& b = r.blocks[crSelBlock_];
                if (ui::Combo("Kind", &b.kind, kCreditsBlockKinds, IM_ARRAYSIZE(kCreditsBlockKinds)))
                    changed = true;
                if (b.kind == CreditsBlock::Heading || b.kind == CreditsBlock::Line) {
                    creditsInput("Text", b.text, -1, true, scaled(48));
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    ui::RowHelp("Wraps to the page width. {{cross}} style tokens draw the\n"
                                "button glyph (docs/text-icons.md).");
                } else if (b.kind == CreditsBlock::Pair) {
                    creditsInput("Role", b.text, -1);
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    creditsInput("Name(s)", b.text2, -1, true, scaled(48));
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    ui::RowHelp("One name per line for a shared role.");
                } else if (b.kind == CreditsBlock::Image) {
                    ImGui::TextDisabled("%s", b.imagePath.empty() ? "(no image)" : b.imagePath.c_str());
                    if (ui::ChipButton("Import PNG...")) {
                        const std::string src = pickPngFile();
                        if (!src.empty()) {
                            const fs::path srcPath(src);
                            const std::string file = sanitizeAssetName(srcPath.filename().string());
                            const fs::path destDir = fs::path(project_.dir) / "res" / "credits";
                            std::error_code ec;
                            fs::create_directories(destDir, ec);
                            fs::copy_file(srcPath, destDir / file, fs::copy_options::overwrite_existing, ec);
                            if (ec) {
                                statusMessage_ = "Image import failed: " + ec.message();
                            } else {
                                b.imagePath = "res/credits/" + file;
                                hudTexCache_.erase(b.imagePath);
                                changed = true;
                            }
                        }
                    }
                    if (ui::SliderFloat("Width", &b.scale, 0.05f, 1.0f, "%.2f of page")) changed = true;
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
                if (b.kind != CreditsBlock::Break) {
                    if (ui::DragFloat("Space below", &b.space, 1.0f, 0.0f, 512.0f, "%.0f px")) changed = true;
                    if (b.kind == CreditsBlock::Gap)
                        ui::RowHelp("The gap itself. 0 = one line of the roll's body size.");
                }
                if (b.kind == CreditsBlock::Heading || b.kind == CreditsBlock::Line ||
                    b.kind == CreditsBlock::Image) {
                    const char* aligns[] = {"Left", "Center", "Right"};
                    if (ui::Combo("Align", &b.align, aligns, 3)) changed = true;
                }
                if (b.kind == CreditsBlock::Heading || b.kind == CreditsBlock::Line ||
                    b.kind == CreditsBlock::Pair) {
                    if (ui::DragInt("Size", &b.size, 0.5f, 0, 64, b.size == 0 ? "roll default" : "%d px"))
                        changed = true;
                    ui::RowHelp("0 = the roll's heading / body size.");
                    if (ui::Switch("Own color", &b.ownColor)) changed = true;
                    if (b.ownColor && ui::ColorHex("Color", b.color)) changed = true;
                    if (rowFontCombo("Font", b.font)) changed = true;
                }
                ImGui::Spacing();
                const bool canUp = crSelBlock_ > 0;
                const bool canDown = crSelBlock_ + 1 < (int)r.blocks.size();
                if (ui::ChipButton("Move up", false, 0.0f, canUp)) {
                    std::swap(r.blocks[crSelBlock_], r.blocks[crSelBlock_ - 1]);
                    --crSelBlock_;
                    changed = true;
                }
                ImGui::SameLine();
                if (ui::ChipButton("Move down", false, 0.0f, canDown)) {
                    std::swap(r.blocks[crSelBlock_], r.blocks[crSelBlock_ + 1]);
                    ++crSelBlock_;
                    changed = true;
                }
                ImGui::SameLine();
                if (ui::DangerButton("Delete")) {
                    r.blocks.erase(r.blocks.begin() + crSelBlock_);
                    if (crSelBlock_ >= (int)r.blocks.size()) crSelBlock_ = (int)r.blocks.size() - 1;
                    changed = true;
                }
            }
        }
    }
    ImGui::EndChild();  // cr_tabbody
    ImGui::EndChild();  // cr_props

    commitIfEdited();
    ImGui::End();
}
