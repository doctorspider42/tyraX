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
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gl_loader.h"
#include "menubake.hpp"
#include "platform.hpp"

#include <imgui.h>

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

// A single-line text field over a std::string, sized to the panel.
bool creditsInput(const char* label, std::string& value, float width,
                  bool multiline = false, float height = 0.0f) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    ImGui::SetNextItemWidth(width);
    const bool edited =
        multiline
            ? ImGui::InputTextMultiline(label, buf, sizeof(buf),
                                        ImVec2(width, height))
            : ImGui::InputText(label, buf, sizeof(buf));
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
// The window
// ---------------------------------------------------------------------------

void App::drawCreditsWindow() {
    if (!showCreditsEditor_ || !hasProject_) {
        if (!showCreditsEditor_ && crPreviewValid_) creditsPreviewDrop();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(scaled(900), scaled(760)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Credits", &showCreditsEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    auto& rolls = project_.credits;
    if (selectedCredits_ >= (int)rolls.size()) selectedCredits_ = -1;

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

    // The window is two stacked halves with a draggable splitter: settings on
    // top, the preview below. Which one deserves the room depends on what you
    // are doing - writing a roll wants the block list tall, judging its timing
    // wants the preview big - so the split is the user's, and persists per
    // machine (creditsSplit_ in editor.ini) rather than per project.
    const float totalH = ImGui::GetContentRegionAvail().y;
    const float splitH = scaled(9.0f);
    const float transportH = ImGui::GetFrameHeightWithSpacing();
    const float minPreview = scaled(90.0f);
    const float minTop = scaled(180.0f);
    if (creditsSplit_ < 0.15f) creditsSplit_ = 0.15f;
    if (creditsSplit_ > 0.75f) creditsSplit_ = 0.75f;
    float previewH = (totalH - splitH - transportH) * creditsSplit_;
    const float roomForPreview = totalH - splitH - transportH - minTop;
    if (previewH > roomForPreview) previewH = roomForPreview;
    if (previewH < minPreview) previewH = minPreview;

    ImGui::BeginChild("##cr_top",
                      ImVec2(0, -(previewH + splitH + transportH)));

    // --- left: the roll list -----------------------------------------------
    ImGui::BeginChild("##cr_list", ImVec2(scaled(160), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New roll", ImVec2(-1, 0))) {
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
        crPreviewTime_ = 0.0f;
        creditsPreviewDrop();
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)rolls.size(); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(rolls[i].name.c_str(), selectedCredits_ == i)) {
            selectedCredits_ = i;
            crSelBlock_ = -1;
            crPreviewTime_ = 0.0f;
            creditsPreviewDrop();
        }
        ImGui::PopID();
    }
    if (rolls.empty())
        ImGui::TextDisabled("No rolls yet.\n\nStart one, then play\nit with a menu row\n"
                            "or the Play Credits\nflow node.");
    ImGui::EndChild();
    ImGui::SameLine();

    if (selectedCredits_ < 0 || selectedCredits_ >= (int)rolls.size()) {
        ImGui::BeginChild("##cr_none", ImVec2(0, 0));
        ImGui::TextDisabled("Select a roll on the left (or create one).");
        ImGui::TextDisabled("\nA roll plays from:");
        ImGui::BulletText("a menu row (Menu Editor > action \"Play credits\")");
        ImGui::BulletText("the Play Credits flow node");
        ImGui::TextDisabled(
            "\nIt owns the screen and the pad while it rolls, and\n"
            "then runs its own finish action - resume, switch\n"
            "scene, open a menu, or fire a flow event.");
        ImGui::EndChild();
        ImGui::EndChild();  // cr_top
        commitIfEdited();
        ImGui::End();
        return;
    }
    CreditsRoll& r = rolls[selectedCredits_];
    if (crSelBlock_ >= (int)r.blocks.size()) crSelBlock_ = -1;

    // --- middle: the block stack -------------------------------------------
    ImGui::BeginChild("##cr_blocks", ImVec2(scaled(250), 0), ImGuiChildFlags_Borders);
    {
        std::string name = r.name;
        if (creditsInput("##cr_name", name, -1)) {
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
                        if (t && t->strKind == FlowParamKind::CreditsName &&
                            n.str == r.name)
                            n.str = name;
                    }
            r.name = name;
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (ImGui::SmallButton("Duplicate")) {
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
        creditsPreviewDrop();
        changed = true;
        ImGui::EndChild();
        ImGui::EndChild();  // cr_top
        commitIfEdited();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete roll")) {
        rolls.erase(rolls.begin() + selectedCredits_);
        selectedCredits_ = -1;
        crSelBlock_ = -1;
        creditsPreviewDrop();
        ImGui::EndChild();
        ImGui::EndChild();  // cr_top
        commitIfEdited();
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Blocks");
    if (ImGui::SmallButton("Import text...")) {
        const std::string file = platform::pickFile(
            "Import credits text",
            {{"Text file (*.txt, *.md, *.csv)", {"*.txt", "*.md", "*.csv"}},
             {"All files (*.*)", {"*.*"}}});
        if (!file.empty()) {
            const std::string err = creditsImportFile(r, file);
            if (err.empty()) {
                statusMessage_ = "Imported " + std::to_string(r.blocks.size()) +
                                 " credit blocks";
                changed = true;
            } else {
                statusMessage_ = err;
            }
        }
    }
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
    if (!r.source.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Re-import")) {
            const std::string abs = fs::path(r.source).is_absolute()
                                        ? r.source
                                        : project_.filePath(r.source);
            const std::string err = creditsImportFile(r, abs);
            statusMessage_ = err.empty() ? "Re-imported " + r.source : err;
            changed |= err.empty();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Re-read %s", r.source.c_str());
    }

    ImGui::BeginChild("##cr_stack", ImVec2(0, -scaled(96)), ImGuiChildFlags_Borders);
    for (int i = 0; i < (int)r.blocks.size(); ++i) {
        ImGui::PushID(i);
        const std::string label = creditsBlockLabel(r.blocks[i]);
        if (ImGui::Selectable(label.c_str(), crSelBlock_ == i)) crSelBlock_ = i;
        ImGui::PopID();
    }
    if (r.blocks.empty()) ImGui::TextDisabled("Empty roll.");
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
        changed = true;
    };
    if (ImGui::SmallButton("+ Heading")) addBlock(CreditsBlock::Heading);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Role/name")) addBlock(CreditsBlock::Pair);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Line")) addBlock(CreditsBlock::Line);
    if (ImGui::SmallButton("+ Image")) addBlock(CreditsBlock::Image);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Gap")) addBlock(CreditsBlock::Gap);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Break")) addBlock(CreditsBlock::Break);
    if (crSelBlock_ >= 0) {
        if (ImGui::SmallButton("Move up") && crSelBlock_ > 0) {
            std::swap(r.blocks[crSelBlock_], r.blocks[crSelBlock_ - 1]);
            --crSelBlock_;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Move down") && crSelBlock_ + 1 < (int)r.blocks.size()) {
            std::swap(r.blocks[crSelBlock_], r.blocks[crSelBlock_ + 1]);
            ++crSelBlock_;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            r.blocks.erase(r.blocks.begin() + crSelBlock_);
            if (crSelBlock_ >= (int)r.blocks.size()) crSelBlock_ = (int)r.blocks.size() - 1;
            changed = true;
        }
    }
    ImGui::EndChild();  // cr_blocks
    ImGui::SameLine();

    // --- right: the inspector ----------------------------------------------
    ImGui::BeginChild("##cr_props", ImVec2(0, 0), ImGuiChildFlags_Borders);
    const float w = scaled(200);

    // A collapsing header rather than a fixed section: with a block selected
    // this sits between you and the roll settings, and in a short column that
    // matters more than the two clicks it saves.
    if (crSelBlock_ >= 0 &&
        ImGui::CollapsingHeader("Selected block", ImGuiTreeNodeFlags_DefaultOpen)) {
        CreditsBlock& b = r.blocks[crSelBlock_];
        ImGui::SetNextItemWidth(w);
        if (ImGui::Combo("Kind", &b.kind, kCreditsBlockKinds,
                         IM_ARRAYSIZE(kCreditsBlockKinds)))
            changed = true;
        if (b.kind == CreditsBlock::Heading || b.kind == CreditsBlock::Line) {
            creditsInput("Text", b.text, scaled(260), true, scaled(48));
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("Wraps to the page width. {{cross}} style tokens draw the\n"
                     "button glyph (docs/text-icons.md).");
        } else if (b.kind == CreditsBlock::Pair) {
            creditsInput("Role", b.text, w);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            creditsInput("Name(s)", b.text2, scaled(260), true, scaled(48));
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("One name per line for a shared role.");
        } else if (b.kind == CreditsBlock::Image) {
            ImGui::TextDisabled("%s", b.imagePath.empty() ? "(no image)"
                                                          : b.imagePath.c_str());
            if (ImGui::SmallButton("Import PNG...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const fs::path srcPath(src);
                    const std::string file =
                        sanitizeAssetName(srcPath.filename().string());
                    const fs::path destDir = fs::path(project_.dir) / "res" / "credits";
                    std::error_code ec;
                    fs::create_directories(destDir, ec);
                    fs::copy_file(srcPath, destDir / file,
                                  fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        statusMessage_ = "Image import failed: " + ec.message();
                    } else {
                        b.imagePath = "res/credits/" + file;
                        hudTexCache_.erase(b.imagePath);
                        changed = true;
                    }
                }
            }
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderFloat("Width", &b.scale, 0.05f, 1.0f, "%.2f of page"))
                changed = true;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (b.kind != CreditsBlock::Break) {
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat("Space below", &b.space, 1.0f, 0.0f, 512.0f, "%.0f px"))
                changed = true;
            if (b.kind == CreditsBlock::Gap)
                prefHelp("The gap itself. 0 = one line of the roll's body size.");
        }
        if (b.kind == CreditsBlock::Heading || b.kind == CreditsBlock::Line ||
            b.kind == CreditsBlock::Image) {
            ImGui::SetNextItemWidth(w);
            const char* aligns[] = {"Left", "Center", "Right"};
            if (ImGui::Combo("Align", &b.align, aligns, 3)) changed = true;
        }
        if (b.kind == CreditsBlock::Heading || b.kind == CreditsBlock::Line ||
            b.kind == CreditsBlock::Pair) {
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragInt("Size", &b.size, 0.5f, 0, 64,
                               b.size == 0 ? "roll default" : "%d px"))
                changed = true;
            prefHelp("0 = the roll's heading / body size.");
            if (ImGui::Checkbox("Own color", &b.ownColor)) changed = true;
            if (b.ownColor) {
                ImGui::SameLine();
                if (ImGui::ColorEdit3("##cr_bcol", b.color,
                                      ImGuiColorEditFlags_NoInputs))
                    changed = true;
            }
            if (fontCombo(b.font)) changed = true;
            prefHelp("Empty = the roll's typeface.");
        }
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::ColorEdit3("Background", r.bgColor, ImGuiColorEditFlags_NoInputs))
            changed = true;
        if (ImGui::ColorEdit3("Text", r.color, ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::SameLine();
        if (ImGui::ColorEdit3("Headings", r.headingColor, ImGuiColorEditFlags_NoInputs))
            changed = true;
        if (fontCombo(r.font)) changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragInt("Heading size", &r.headingSize, 0.5f, 8, 64, "%d px"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragInt("Body size", &r.lineSize, 0.5f, 8, 64, "%d px"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("Line spacing", &r.lineSpacing, 0.01f, 0.8f, 3.0f, "%.2f"))
            changed = true;
        if (ImGui::Checkbox("Text shadow", &r.shadow)) changed = true;
        ImGui::SetNextItemWidth(w);
        int pageIdx = r.pageW == 256 ? 0 : 1;
        const char* pageWidths[] = {"256 px (half screen)", "512 px (full screen)"};
        if (ImGui::Combo("Page width", &pageIdx, pageWidths, 2)) {
            r.pageW = pageIdx == 0 ? 256 : 512;
            changed = true;
        }
        prefHelp("The width of the baked page textures. 256 halves their VRAM\n"
                 "cost and is plenty for centered credits; 512 gives full-width\n"
                 "role/name columns.");
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("Side margin", &r.margin, 0.5f, 0.0f, 200.0f, "%.0f px"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("Column gap", &r.columnGap, 0.5f, 0.0f, 200.0f, "%.0f px"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        const char* quants[] = {"4-bit (16 colors)", "8-bit (256 colors)",
                                "Full color"};
        int qi = r.quant == "none" ? 2 : (r.quant == "8bit" ? 1 : 0);
        if (ImGui::Combo("Page depth", &qi, quants, 3)) {
            r.quant = qi == 2 ? "none" : (qi == 1 ? "8bit" : "4bit");
            changed = true;
        }
        prefHelp("Palette depth of the baked pages. Text on a flat background\n"
                 "needs 16 colors; a backdrop (below) makes the pages\n"
                 "transparent and usually wants 8-bit. Full color costs 8x the\n"
                 "VRAM of 4-bit - see the budget line under the preview.");

        ImGui::TextDisabled("Backdrop (still image behind the roll)");
        ImGui::TextDisabled("%s", r.bgImage.imagePath.empty()
                                      ? "(none - flat background color)"
                                      : r.bgImage.imagePath.c_str());
        if (ImGui::SmallButton("Import backdrop...")) {
            const std::string src = pickPngFile();
            if (!src.empty()) {
                const fs::path srcPath(src);
                const std::string file = sanitizeAssetName(srcPath.filename().string());
                const fs::path destDir = fs::path(project_.dir) / "res" / "hud";
                std::error_code ec;
                fs::create_directories(destDir, ec);
                fs::copy_file(srcPath, destDir / file,
                              fs::copy_options::overwrite_existing, ec);
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
            if (ImGui::SmallButton("Remove backdrop")) {
                r.bgImage.imagePath.clear();
                changed = true;
            }
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat2("Backdrop size", r.bgImage.size, 1.0f, 8.0f, 512.0f,
                                  "%.0f px"))
                changed = true;
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat2("Backdrop pos", r.bgImage.pos, 0.005f, 0.0f, 1.0f,
                                  "%.3f"))
                changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(w);
        const char* modes[] = {"Scroll up", "Cards (one page at a time)"};
        if (ImGui::Combo("Mode", &r.mode, modes, 2)) changed = true;
        prefHelp("Cards split the roll at every page break and show each for\n"
                 "its own time, cross-fading - for title cards and dedications.");
        if (r.mode == 0) {
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat("Speed", &r.speed, 0.2f, 1.0f, 200.0f, "%.0f px/s"))
                changed = true;
        } else {
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat("Card time", &r.cardSeconds, 0.05f, 0.5f, 30.0f,
                                 "%.2f s"))
                changed = true;
        }
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("Start delay", &r.startDelay, 0.02f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("End hold", &r.endHold, 0.02f, 0.0f, 20.0f, "%.2f s"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("Fade in", &r.fadeIn, 0.02f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloat("Fade out", &r.fadeOut, 0.02f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
    }

    if (ImGui::CollapsingHeader("Music")) {
        ImGui::SetNextItemWidth(scaled(260));
        if (ImGui::BeginCombo("Track", r.music.empty() ? "<keep playing>"
                                                       : r.music.c_str())) {
            if (ImGui::Selectable("<keep playing>", r.music.empty())) {
                r.music.clear();
                changed = true;
            }
            for (const std::string& track : project_.music)
                if (ImGui::Selectable(track.c_str(), track == r.music)) {
                    r.music = track;
                    changed = true;
                }
            if (project_.music.empty())
                ImGui::TextDisabled("Import WAVs in the\nProject panel (Music).");
            ImGui::EndCombo();
        }
        prefHelp("Started when the roll begins. <keep playing> leaves whatever\n"
                 "music the game had running alone.");
        if (!r.music.empty()) {
            if (ImGui::Checkbox("Loop", &r.musicLoop)) changed = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Stop at the end", &r.musicStopAtEnd)) changed = true;
            ImGui::SetNextItemWidth(w);
            if (ImGui::SliderInt("Volume", &r.musicVolume, 0, 100)) changed = true;
        }
    }

    if (ImGui::CollapsingHeader("Skipping")) {
        if (ImGui::Checkbox("Player can skip", &r.skippable)) changed = true;
        if (r.skippable) {
            ImGui::SetNextItemWidth(scaled(260));
            if (ImGui::BeginCombo("Button", r.skipAction.empty()
                                                ? "<confirm or start>"
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
            prefHelp("An action from Tools > Input Map, so the skip follows a\n"
                     "rebind. The default accepts the menu confirm action and\n"
                     "Start.");
            ImGui::SetNextItemWidth(w);
            if (ImGui::DragFloat("Ignore for", &r.skipAfter, 0.02f, 0.0f, 30.0f,
                                 "%.2f s"))
                changed = true;
            prefHelp("A held button from the moment before the roll started\n"
                     "should not skip it instantly.");
            if (ImGui::Checkbox("Show hint", &r.showSkipHint)) changed = true;
            if (r.showSkipHint) {
                creditsInput("Hint", r.skipHint, scaled(260));
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                prefHelp("Baked text, so {{confirm}} draws the button bound at\n"
                         "BUILD time (docs/text-icons.md).");
                ImGui::SetNextItemWidth(w);
                if (ImGui::DragFloat2("Hint pos", r.hintPos, 0.005f, 0.0f, 1.0f, "%.3f"))
                    changed = true;
                ImGui::SetNextItemWidth(w);
                if (ImGui::DragInt("Hint size", &r.hintSize, 0.5f, 8, 40, "%d px"))
                    changed = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("When it ends", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(scaled(260));
        const char* finishes[] = {"Resume the game", "Switch scene", "Open menu",
                                  "Fire flow event", "Hold the last frame"};
        if (ImGui::Combo("Then", &r.finish, finishes, 5)) {
            r.finishParam.clear();
            changed = true;
        }
        prefHelp("A skip runs this too - so a player who skips lands where a\n"
                 "player who watched lands.");
        if (r.finish == CreditsRoll::SwitchScene) {
            ImGui::SetNextItemWidth(scaled(260));
            if (ImGui::BeginCombo("Scene", r.finishParam.empty()
                                               ? "<none>"
                                               : r.finishParam.c_str())) {
                for (const SceneData& sc : project_.scenes)
                    if (ImGui::Selectable(sc.name.c_str(), sc.name == r.finishParam)) {
                        r.finishParam = sc.name;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
        } else if (r.finish == CreditsRoll::OpenMenu) {
            ImGui::SetNextItemWidth(scaled(260));
            if (ImGui::BeginCombo("Menu", r.finishParam.empty() ? "<none>"
                                                                : r.finishParam.c_str())) {
                for (const GameMenu& m : project_.menus)
                    if (ImGui::Selectable(m.name.c_str(), m.name == r.finishParam)) {
                        r.finishParam = m.name;
                        changed = true;
                    }
                if (project_.menus.empty())
                    ImGui::TextDisabled("Add menus in\nTools > Menu Editor.");
                ImGui::EndCombo();
            }
        } else if (r.finish == CreditsRoll::FlowEvent) {
            creditsInput("Event", r.finishParam, scaled(260));
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            prefHelp("Caught by an On Menu Event trigger with the same name.");
        } else if (r.finish == CreditsRoll::Hold) {
            ImGui::TextDisabled("Nothing follows: the last frame stays on screen.");
        }
    }
    ImGui::EndChild();  // cr_props
    ImGui::EndChild();  // cr_top

    // --- splitter: drag to trade settings height for preview height ---------
    ImGui::InvisibleButton("##cr_split", ImVec2(-1.0f, splitH));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && roomForPreview > minPreview) {
        previewH -= ImGui::GetIO().MouseDelta.y;
        if (previewH < minPreview) previewH = minPreview;
        if (previewH > roomForPreview) previewH = roomForPreview;
        const float span = totalH - splitH - transportH;
        if (span > 1.0f) creditsSplit_ = previewH / span;
        if (creditsSplit_ < 0.15f) creditsSplit_ = 0.15f;
        if (creditsSplit_ > 0.75f) creditsSplit_ = 0.75f;
    }
    if (ImGui::IsItemDeactivated()) saveGlobalConfig();  // persist the split
    {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const float cy = (mn.y + mx.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(mn.x + scaled(4.0f), cy), ImVec2(mx.x - scaled(4.0f), cy),
            ImGui::GetColorU32(ImGui::IsItemActive()    ? ImGuiCol_SeparatorActive
                               : ImGui::IsItemHovered() ? ImGuiCol_SeparatorHovered
                                                        : ImGuiCol_Separator),
            scaled(2.0f));
    }

    // --- the preview -------------------------------------------------------
    // The pages here ARE the baked pages, placed by the runtime's own
    // arithmetic - the strip's top enters from the bottom of the screen and
    // walks up, so what plays here is what plays on the console.
    const bool baked = creditsPreviewRefresh();
    const float total = creditsDuration(r);
    if (crPreviewPlaying_) {
        crPreviewTime_ += ImGui::GetIO().DeltaTime;
        if (crPreviewTime_ > total) crPreviewTime_ = 0.0f;
    }
    if (ImGui::Button(crPreviewPlaying_ ? "Pause" : "Play"))
        crPreviewPlaying_ = !crPreviewPlaying_;
    ImGui::SameLine();
    if (ImGui::Button("Rewind")) crPreviewTime_ = 0.0f;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(260));
    ImGui::SliderFloat("##cr_time", &crPreviewTime_, 0.0f, total > 0 ? total : 1.0f,
                       "%.1f s");
    ImGui::SameLine();
    {
        // The honest cost report: pages, running time, and what the pages will
        // take of the GS texture budget (docs/gs-vram.md). A roll that cannot
        // fit says so here rather than flushing textures mid-scroll in-game.
        const int bpp = r.quant == "none" ? 32 : (r.quant == "8bit" ? 8 : 4);
        const float kb = (float)crLayout_.pageCount *
                         ((float)crLayout_.pageW * crLayout_.pageH * bpp / 8.0f / 1024.0f +
                          8.0f);
        ImGui::Text("%d/%d pages | %.0f s | ~%.0f KB VRAM", crLayout_.pageCount,
                    menubake::kCreditsMaxPages, total, kb);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Pages are the roll baked into textures. The console pins each "
                "one it draws\nin a ~1.33 MB budget, so a very long roll wants a "
                "slower speed, card\nmode, 256 px pages or a second roll - see "
                "docs/credits.md.");
    }
    if (crLayout_.clipped) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "content clipped");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The roll is longer than %d pages (or a card is taller than one\n"
                "screen). Everything past that is NOT baked and never shows.",
                menubake::kCreditsMaxPages);
    }

    // The preview keeps the PS2 frame's 512x448 aspect and is fitted into
    // whatever the splitter left it; the space beside it (a 4:3.5 image in a
    // wide window leaves a lot) goes to the jump list rather than staying empty.
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float listW = avail.x > scaled(520.0f) ? scaled(230.0f) : 0.0f;
    float ph = avail.y < scaled(70) ? scaled(70) : avail.y;
    float pw = ph * 512.0f / 448.0f;  // the PS2 frame's aspect
    const float maxPw = avail.x - (listW > 0.0f ? listW + spacing : 0.0f);
    if (pw > maxPw && maxPw > scaled(40.0f)) {
        pw = maxPw;
        ph = pw * 448.0f / 512.0f;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float sx = pw / 512.0f, sy = ph / 448.0f;
    auto col = [](const float* c, float a) {
        return IM_COL32((int)(c[0] * 255.0f), (int)(c[1] * 255.0f),
                        (int)(c[2] * 255.0f), (int)(a * 255.0f));
    };
    dl->AddRectFilled(origin, ImVec2(origin.x + pw, origin.y + ph),
                      col(r.bgColor, 1.0f));
    if (!baked) {
        dl->AddText(ImVec2(origin.x + scaled(8), origin.y + scaled(8)),
                    IM_COL32(255, 120, 120, 255), "No usable TTF font found");
    } else {
        dl->PushClipRect(origin, ImVec2(origin.x + pw, origin.y + ph), true);
        if (!r.bgImage.imagePath.empty()) {
            if (const HudTexture* t = hudTexture(r.bgImage.imagePath)) {
                const float bw = r.bgImage.size[0] * sx, bh = r.bgImage.size[1] * sy;
                const ImVec2 tl(origin.x + r.bgImage.pos[0] * pw - bw * 0.5f,
                                origin.y + r.bgImage.pos[1] * ph - bh * 0.5f);
                dl->AddImage((ImTextureID)(intptr_t)t->tex, tl,
                             ImVec2(tl.x + bw, tl.y + bh));
            }
        }
        const float pageW = (float)crPreviewPageW_ * sx;
        const float pageH = (float)crPreviewPageH_ * sy;
        const float px = origin.x + (pw - pageW) * 0.5f;
        const float elapsed = crPreviewTime_ - r.startDelay;
        if (elapsed >= 0.0f) {
            if (r.mode == 1) {
                int card = r.cardSeconds > 0.0f ? (int)(elapsed / r.cardSeconds) : 0;
                if (card >= (int)crPreviewTex_.size())
                    card = (int)crPreviewTex_.size() - 1;
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
                    dl->AddImage((ImTextureID)(intptr_t)crPreviewTex_[k],
                                 ImVec2(px, y), ImVec2(px + pageW, y + pageH));
                }
            }
            if (r.skippable && r.showSkipHint && !r.skipHint.empty()) {
                const HudText hint = menubake::creditsHintText(r);
                if (const HudTexture* t = hudTextTexture(hint)) {
                    const float hw = (float)t->w * sx, hh = (float)t->h * sy;
                    const ImVec2 tl(origin.x + r.hintPos[0] * pw - hw * 0.5f,
                                    origin.y + r.hintPos[1] * ph - hh * 0.5f);
                    dl->AddImage((ImTextureID)(intptr_t)t->tex, tl,
                                 ImVec2(tl.x + hw, tl.y + hh));
                }
            }
        }
        // The fades, drawn the way the runtime composites them: one tinted quad
        // of the background color over everything.
        float fade = 0.0f;
        if (r.fadeIn > 0.0f && crPreviewTime_ < r.fadeIn)
            fade = 1.0f - crPreviewTime_ / r.fadeIn;
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
    dl->AddRect(origin, ImVec2(origin.x + pw, origin.y + ph),
                IM_COL32(90, 90, 100, 255));
    ImGui::Dummy(ImVec2(pw, ph));

    // --- jump list: every block with the moment it is readable --------------
    // The roll's own arithmetic run backwards - "when is this block centred on
    // screen" - so a click scrubs the preview to the frame that answers "does
    // the Music row look right", without hunting on the slider.
    if (listW > 0.0f) {
        auto blockTime = [&](int i) -> float {
            if (i < 0 || i >= (int)crLayout_.boxes.size()) return 0.0f;
            const menubake::CreditsBlockBox& b = crLayout_.boxes[i];
            if (r.mode == 1) {
                const int card = crLayout_.pageH > 0 ? b.y / crLayout_.pageH : 0;
                return r.startDelay + ((float)card + 0.5f) * r.cardSeconds;
            }
            const float centre = (float)b.y + (float)b.h * 0.5f;
            const float speed = r.speed > 1.0f ? r.speed : 1.0f;
            return r.startDelay + (centre + 448.0f * 0.5f) / speed;
        };
        ImGui::SameLine();
        ImGui::BeginChild("##cr_jump", ImVec2(listW, ph), ImGuiChildFlags_Borders);
        ImGui::TextDisabled("Jump to");
        ImGui::Separator();
        for (int i = 0; i < (int)r.blocks.size(); ++i) {
            const CreditsBlock& b = r.blocks[i];
            if (b.kind == CreditsBlock::Gap) continue;
            ImGui::PushID(i);
            const float t = blockTime(i);
            if (b.kind == CreditsBlock::Break) {
                ImGui::TextDisabled(r.mode == 1 ? "-- next card --" : "-- next page --");
                ImGui::PopID();
                continue;
            }
            char row[160];
            std::snprintf(row, sizeof(row), "%d:%02d  %s", (int)t / 60,
                          (int)t % 60, creditsBlockLabel(b).c_str());
            const bool clipped = i < (int)crLayout_.boxes.size() &&
                                 crLayout_.boxes[i].clipped;
            if (clipped) ImGui::PushStyleColor(ImGuiCol_Text,
                                               ImVec4(1.0f, 0.55f, 0.25f, 1.0f));
            if (ImGui::Selectable(row, crSelBlock_ == i)) {
                crSelBlock_ = i;
                crPreviewTime_ = t;
            }
            if (clipped) ImGui::PopStyleColor();
            if (clipped && ImGui::IsItemHovered())
                ImGui::SetTooltip("Past the page budget - not baked, never shows.");
            ImGui::PopID();
        }
        if (r.blocks.empty()) ImGui::TextDisabled("No blocks yet.");
        ImGui::EndChild();
    }

    commitIfEdited();
    ImGui::End();
}
