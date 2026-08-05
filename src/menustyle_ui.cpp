// The Menu Editor's Style tab, its stylesheet text tab and the shared preview
// (docs/menu-styles.md). App:: methods declared in app.hpp, in their own TU -
// the credits_ui.cpp / mateditor_ui.cpp precedent.
//
// The arrangement to keep: the file on disk is the truth (menu-styles/*.menustyle),
// these widgets are an editor over its AST, and the preview draws the BAKE. So
// there are exactly three things here - a widget per property kind, the sheet's
// own undo stack (style edits are not project edits, the Material Editor made
// the same call), and a readout of what the look costs on the console.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <imgui.h>

#include "app.hpp"
#include "app_internal.hpp"
#include "gl_loader.h"
#include "menubake.hpp"
#include "menulayout.hpp"
#include "menustyle.hpp"

namespace {

// The element groups the Style tab offers, in the order a menu is read on
// screen. `Image` is left out on purpose: image placement is per-image and
// already lives in the Content tab.
struct ElemGroup {
    menustyle::Elem elem;
    const char* label;
    const char* help;
};

const ElemGroup kGroups[] = {
    {menustyle::Elem::Panel, "Panel",
     "The box everything sits in: background, frame, corners, shadow, and the\n"
     "colour depth it ships at."},
    {menustyle::Elem::Title, "Title",
     "The menu's heading and the separator rule under it."},
    {menustyle::Elem::Row, "Rows",
     "Every entry row. The states below it are what a selected or unusable row\n"
     "adds on top."},
    {menustyle::Elem::List, "List",
     "The rows as a group - how many are on screen, and what says there are\n"
     "more."},
    {menustyle::Elem::Value, "Values",
     "The right-hand side of a Toggle / Choice row: its option label, or a\n"
     "slider bar instead."},
    {menustyle::Elem::Description, "Description",
     "A pane that shows the selected row's description. It only exists once you\n"
     "give it a height."},
    {menustyle::Elem::Hint, "Hints",
     "The button hint line at the bottom of the panel."},
    {menustyle::Elem::Marker, "Caret",
     "The selection cursor sprite."},
};

// Which properties each element offers, and in which order. A property missing
// from every list here is unreachable from the GUI (the text tab still takes
// it), so a new one belongs in the right row.
const std::vector<menustyle::Prop>& propsFor(menustyle::Elem e) {
    using P = menustyle::Prop;
    static const std::vector<P> panel = {
        P::Width,      P::Quant,   P::Background, P::BackgroundImage,
        P::BorderWidth, P::Radius, P::Shadow,     P::Padding,
        P::Gap,        P::Opacity};
    static const std::vector<P> title = {
        P::Font,     P::FontSize,      P::TextColor, P::Align,
        P::LetterSpacing, P::TextTransform, P::TextShadow, P::TextOutline,
        P::RuleBelow, P::Margin,       P::Padding,   P::Height};
    static const std::vector<P> row = {
        P::Height,   P::Font,          P::FontSize,  P::TextColor,
        P::Align,    P::LetterSpacing, P::TextTransform, P::Padding,
        P::Background, P::BorderWidth, P::Radius,    P::TranslateX,
        P::IconSize, P::TextShadow,    P::TextOutline, P::Selectable};
    static const std::vector<P> list = {P::RowsVisible, P::ScrollMarker};
    static const std::vector<P> value = {
        P::Font,    P::FontSize, P::TextColor, P::Align,   P::MarginRight,
        P::Display, P::BarSize,  P::BarFill,   P::BarTrack};
    static const std::vector<P> desc = {
        P::Height, P::Area,   P::Font,       P::FontSize,   P::TextColor,
        P::Align,  P::Wrap,   P::Padding,    P::Background, P::BorderWidth,
        P::Radius};
    static const std::vector<P> hint = {
        P::Content, P::Font,  P::FontSize, P::TextColor, P::Align,
        P::Height,  P::Margin, P::Padding, P::LetterSpacing};
    static const std::vector<P> marker = {P::Marker, P::MarkerSide, P::TranslateX};
    static const std::vector<P> none = {};
    switch (e) {
        case menustyle::Elem::Panel: return panel;
        case menustyle::Elem::Title: return title;
        case menustyle::Elem::Row: return row;
        case menustyle::Elem::List: return list;
        case menustyle::Elem::Value: return value;
        case menustyle::Elem::Description: return desc;
        case menustyle::Elem::Hint: return hint;
        case menustyle::Elem::Marker: return marker;
        default: return none;
    }
}

// The states a row offers as sub-groups.
const char* kStateLabels[] = {"", "Selected", "Disabled"};

ImVec4 toImVec4(menustyle::Color c) {
    return ImVec4(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

menustyle::Color fromImVec4(const ImVec4& v) {
    auto b = [](float f) {
        const int k = (int)(f * 255.0f + 0.5f);
        return (unsigned char)(k < 0 ? 0 : k > 255 ? 255 : k);
    };
    return menustyle::Color{b(v.x), b(v.y), b(v.z), b(v.w)};
}

}  // namespace

// Whether the PROJECT already has a sheet of that name. Deliberately not "is
// there a sheet called this in the registry": the built-ins are in there too, so
// installing a copy of `neon` into a project with no neon file must be allowed
// to call it `neon` (it collided with itself and came out `neon-2` first).
bool App::menuStyleFileExists(const std::string& key) const {
    if (project_.dir.empty() || key.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(project_.dir) /
                                       "menu-styles" / (key + ".menustyle"),
                                   ec);
}

// A PNG a stylesheet points at is an ordinary project asset, imported the same
// way the Content tab's images are: copied into res/hud and stored relative.
std::string App::importMenuImage(const std::string& srcPath) {
    if (srcPath.empty() || project_.dir.empty()) return {};
    const std::filesystem::path src(srcPath);
    const std::string fileName = sanitizeAssetName(src.filename().string());
    const std::filesystem::path destDir =
        std::filesystem::path(project_.dir) / "res" / "hud";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(src, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing,
                               ec);
    if (ec) return {};
    return "res/hud/" + fileName;
}

// --- the staged sheet ---------------------------------------------------------

// Loads the menu's sheet into the staging copy when the selection changed. The
// staged copy is what the widgets edit; Save writes it back through
// menustyle::save (which re-parses, so the registry and the file never disagree).
void App::menuStyleSync(const GameMenu& m) {
    if (menuStyleKey_ == m.style && menuStyleLoaded_) return;
    menuStyleKey_ = m.style;
    menuStyleStaged_ = menustyle::find(m.style);
    menuStyleLoaded_ = true;
    menuStyleDirty_ = false;
    menuStyleUndo_.clear();
    menuStyleUndoAt_ = 0;
    menuStyleText_ = menustyle::write(menuStyleStaged_);
    menuPreviewKey_.clear();
}

// One undo step per logical edit, exactly like the Material Editor's own stack:
// a stylesheet is not project data, so commitChange() must not see it.
void App::menuStylePush() {
    if (menuStyleUndoAt_ < menuStyleUndo_.size())
        menuStyleUndo_.resize(menuStyleUndoAt_);
    menuStyleUndo_.push_back(menuStyleStaged_);
    if (menuStyleUndo_.size() > 64) menuStyleUndo_.erase(menuStyleUndo_.begin());
    menuStyleUndoAt_ = menuStyleUndo_.size();
}

void App::menuStyleEdited() {
    menuStyleDirty_ = true;
    menuStyleText_ = menustyle::write(menuStyleStaged_);
    menuPreviewKey_.clear();  // the preview re-bakes from the staged sheet
}

// The rule a widget writes into, created on demand. A rule with no declarations
// left is dropped by the writer, so removing the last override removes the rule.
menustyle::Rule& App::menuStyleRule(const std::string& menuScope,
                                    menustyle::Elem elem, const std::string& cls,
                                    int state) {
    for (menustyle::Rule& r : menuStyleStaged_.rules)
        if (r.menu == menuScope && r.elem == elem && r.cls == cls &&
            r.state == state)
            return r;
    menustyle::Rule r;
    r.menu = menuScope;
    r.elem = elem;
    r.cls = cls;
    r.state = state;
    menuStyleStaged_.rules.push_back(std::move(r));
    return menuStyleStaged_.rules.back();
}

// --- one property row ---------------------------------------------------------

// Draws the widget for `prop` inside the rule (scope, elem, cls, state). The
// row shows what the value RESOLVES to today (the default, or the menu's own
// field) until it is overridden, so nothing ever presents a made-up zero as if
// the author had chosen it.
bool App::menuStyleProp(const GameMenu& m, menustyle::Elem elem,
                        const std::string& cls, int state, menustyle::Prop prop) {
    const menustyle::PropSpec* spec = menustyle::propSpec(prop);
    if (!spec) return false;
    const std::string scope = menuStyleScoped_ ? m.name : std::string();

    // Is it overridden here, and by which declaration?
    menustyle::Decl* have = nullptr;
    for (menustyle::Rule& r : menuStyleStaged_.rules) {
        if (r.menu != scope || r.elem != elem || r.cls != cls || r.state != state)
            continue;
        for (menustyle::Decl& d : r.decls)
            if (d.prop == prop) have = &d;
    }

    // What it currently resolves to, for the placeholder values.
    menustyle::Computed cur = menulayout::baseFor(m, elem);
    cur = menustyle::compute(menuStyleStaged_, m.name, elem, cls,
                             menustyle::StateNormal, cur);
    if (state != menustyle::StateNormal)
        cur = menustyle::compute(menuStyleStaged_, m.name, elem, cls, state, cur);

    ImGui::PushID((int)prop + (int)elem * 100 + state * 10000);
    bool changed = false;

    // The override checkbox is the row's identity: off = "whatever it already
    // was", on = "this sheet decides".
    bool on = have != nullptr;
    if (ImGui::Checkbox("##on", &on)) {
        menuStylePush();
        if (on) {
            menustyle::Decl d;
            d.prop = prop;
            // Seed from what it resolves to now, so switching an override on
            // never MOVES anything - it just takes ownership of the value.
            switch (spec->kind) {
                case menustyle::Kind::Length:
                    d.value.n[0] =
                        prop == menustyle::Prop::Width ? cur.width
                        : prop == menustyle::Prop::Height ? cur.height
                        : prop == menustyle::Prop::FontSize ? cur.fontSize
                        : prop == menustyle::Prop::Radius ? cur.radius
                        : prop == menustyle::Prop::LetterSpacing ? cur.letterSpacing
                        : prop == menustyle::Prop::TranslateX ? cur.translateX
                        : prop == menustyle::Prop::IconSize ? cur.iconSize
                        : prop == menustyle::Prop::RowsVisible ? (float)cur.rowsVisible
                        : prop == menustyle::Prop::Opacity ? cur.opacity
                        : prop == menustyle::Prop::Gap ? cur.gap
                        : prop == menustyle::Prop::MarginRight ? cur.margin.r
                        : prop == menustyle::Prop::Slice ? cur.slice
                                                          : 0.0f;
                    break;
                case menustyle::Kind::Color:
                    d.value.c = prop == menustyle::Prop::BarFill ? cur.barFill
                                : prop == menustyle::Prop::BarTrack ? cur.barTrack
                                : prop == menustyle::Prop::BorderColor
                                    ? cur.borderColor
                                    : cur.color;
                    break;
                case menustyle::Kind::Fill: d.value.fill = cur.background; break;
                case menustyle::Kind::Edge:
                    if (prop == menustyle::Prop::BarSize) {
                        d.value.n[0] = cur.barW;
                        d.value.n[1] = cur.barH;
                    } else {
                        const menustyle::Edge& e =
                            prop == menustyle::Prop::Margin ? cur.margin : cur.padding;
                        d.value.n[0] = e.t;
                        d.value.n[1] = e.r;
                        d.value.n[2] = e.b;
                        d.value.n[3] = e.l;
                    }
                    break;
                case menustyle::Kind::Str:
                    d.value.s = prop == menustyle::Prop::Content ? cur.content
                                                                 : cur.font;
                    break;
                case menustyle::Kind::Enum:
                    d.value.i = prop == menustyle::Prop::Align ? cur.align
                                : prop == menustyle::Prop::Display ? cur.display
                                : prop == menustyle::Prop::Area ? cur.area
                                : prop == menustyle::Prop::Quant ? cur.quant
                                : prop == menustyle::Prop::MarkerSide
                                    ? cur.markerSide
                                    : (cur.upper ? 1 : 0);
                    break;
                case menustyle::Kind::Border:
                    if (prop == menustyle::Prop::RuleBelow) {
                        d.value.n[0] = cur.ruleBelow;
                        d.value.c = cur.ruleColor;
                    } else if (prop == menustyle::Prop::TextOutline) {
                        d.value.n[0] = cur.outlineW;
                        d.value.c = cur.outlineColor;
                    } else {
                        d.value.n[0] = cur.borderW;
                        d.value.c = cur.borderColor;
                    }
                    break;
                case menustyle::Kind::Shadow:
                    if (prop == menustyle::Prop::TextShadow) {
                        d.value.n[0] = cur.textShadowX;
                        d.value.n[1] = cur.textShadowY;
                        d.value.c = cur.textShadowColor;
                    } else {
                        d.value.n[0] = cur.shadowX;
                        d.value.n[1] = cur.shadowY;
                        d.value.n[2] = cur.shadowBlur;
                        d.value.c = cur.shadowColor;
                    }
                    break;
                case menustyle::Kind::Bool:
                    d.value.i = prop == menustyle::Prop::Wrap ? (cur.wrap ? 1 : 0)
                                                              : (cur.selectable ? 1 : 0);
                    break;
                case menustyle::Kind::Url: break;
            }
            menuStyleRule(scope, elem, cls, state).decls.push_back(d);
        } else {
            for (menustyle::Rule& r : menuStyleStaged_.rules) {
                if (r.menu != scope || r.elem != elem || r.cls != cls ||
                    r.state != state)
                    continue;
                r.decls.erase(std::remove_if(r.decls.begin(), r.decls.end(),
                                             [&](const menustyle::Decl& d) {
                                                 return d.prop == prop;
                                             }),
                              r.decls.end());
            }
        }
        menuStyleEdited();
        changed = true;
        ImGui::PopID();
        return changed;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Off: this sheet says nothing and the value below is\n"
                          "what the menu or the built-in default supplies.\n"
                          "On: the sheet owns it.");
    ImGui::SameLine();

    ImGui::BeginDisabled(!have);
    menustyle::Value v = have ? have->value : menustyle::Value{};
    bool edited = false;
    const float wide = scaled(150.0f);
    switch (spec->kind) {
        case menustyle::Kind::Length: {
            ImGui::SetNextItemWidth(wide);
            float f = v.n[0];
            const bool screenOk = prop == menustyle::Prop::Width;
            if (screenOk && f < 0) {
                if (ImGui::Button("screen (512 px)")) {
                    f = 256;
                    edited = true;
                }
            } else if (ImGui::DragFloat(spec->name, &f, 0.5f, -1.0f, 512.0f, "%.0f px")) {
            }
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            if (screenOk) {
                ImGui::SameLine();
                if (ImGui::SmallButton("screen")) {
                    f = -1;
                    edited = true;
                }
            }
            v.n[0] = f;
            break;
        }
        case menustyle::Kind::Color: {
            ImVec4 col = toImVec4(v.c);
            if (ImGui::ColorEdit4(spec->name, &col.x,
                                  ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_AlphaBar))
                v.c = fromImVec4(col);
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case menustyle::Kind::Fill: {
            int kind = v.fill.kind;
            ImGui::SetNextItemWidth(wide);
            if (ImGui::Combo("##fillkind", &kind, "None\0Colour\0Gradient\0")) {
                v.fill.kind = kind;
                edited = true;
            }
            if (v.fill.kind != menustyle::Fill::None) {
                ImVec4 a = toImVec4(v.fill.a);
                ImGui::SameLine();
                if (ImGui::ColorEdit4("##fa", &a.x,
                                      ImGuiColorEditFlags_NoInputs |
                                          ImGuiColorEditFlags_AlphaBar))
                    v.fill.a = fromImVec4(a);
                edited |= ImGui::IsItemDeactivatedAfterEdit();
            }
            if (v.fill.kind == menustyle::Fill::Gradient) {
                ImVec4 b = toImVec4(v.fill.b);
                ImGui::SameLine();
                if (ImGui::ColorEdit4("##fb", &b.x,
                                      ImGuiColorEditFlags_NoInputs |
                                          ImGuiColorEditFlags_AlphaBar))
                    v.fill.b = fromImVec4(b);
                edited |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(90.0f));
                ImGui::DragFloat("##angle", &v.fill.angle, 1.0f, 0.0f, 360.0f,
                                 "%.0f deg");
                edited |= ImGui::IsItemDeactivatedAfterEdit();
            }
            break;
        }
        case menustyle::Kind::Edge: {
            ImGui::SetNextItemWidth(wide * 1.4f);
            if (prop == menustyle::Prop::BarSize) {
                ImGui::DragFloat2(spec->name, v.n, 0.5f, 0.0f, 512.0f, "%.0f");
            } else {
                ImGui::DragFloat4(spec->name, v.n, 0.5f, 0.0f, 256.0f, "%.0f");
            }
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case menustyle::Kind::Str: {
            if (prop == menustyle::Prop::Font) {
                std::string f = v.s;
                if (fontCombo(f)) {
                    v.s = f;
                    edited = true;
                }
            } else {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", v.s.c_str());
                ImGui::SetNextItemWidth(wide * 1.6f);
                if (ImGui::InputText(spec->name, buf, sizeof(buf))) v.s = buf;
                edited |= ImGui::IsItemDeactivatedAfterEdit();
                if (prop == menustyle::Prop::Content) {
                    ImGui::SameLine();
                    if (textTokenPicker("hinttok", v.s)) edited = true;
                }
            }
            break;
        }
        case menustyle::Kind::Enum: {
            // The keyword list IS the combo, built from the property table so a
            // new keyword never needs a second list here.
            std::string items;
            for (const char* q = spec->keywords; *q; ++q)
                items += *q == '|' ? '\0' : *q;
            items += '\0';
            items += '\0';
            ImGui::SetNextItemWidth(wide);
            if (ImGui::Combo(spec->name, &v.i, items.c_str())) edited = true;
            break;
        }
        case menustyle::Kind::Border: {
            ImGui::SetNextItemWidth(scaled(80.0f));
            ImGui::DragFloat("##bw", &v.n[0], 0.25f, 0.0f, 32.0f, "%.0f px");
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            ImVec4 col = toImVec4(v.c);
            ImGui::SameLine();
            if (ImGui::ColorEdit4(spec->name, &col.x,
                                  ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_AlphaBar))
                v.c = fromImVec4(col);
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case menustyle::Kind::Shadow: {
            ImGui::SetNextItemWidth(scaled(130.0f));
            ImGui::DragFloat3("##sh", v.n, 0.25f, -32.0f, 32.0f, "%.0f");
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            ImVec4 col = toImVec4(v.c);
            ImGui::SameLine();
            if (ImGui::ColorEdit4(spec->name, &col.x,
                                  ImGuiColorEditFlags_NoInputs |
                                      ImGuiColorEditFlags_AlphaBar))
                v.c = fromImVec4(col);
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        case menustyle::Kind::Bool: {
            bool b = v.i != 0;
            if (ImGui::Checkbox(spec->name, &b)) {
                v.i = b ? 1 : 0;
                edited = true;
            }
            break;
        }
        case menustyle::Kind::Url: {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", v.s.c_str());
            ImGui::SetNextItemWidth(wide * 1.4f);
            if (ImGui::InputText(spec->name, buf, sizeof(buf))) v.s = buf;
            edited |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            if (ImGui::SmallButton("PNG...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    v.s = importMenuImage(src);
                    edited = !v.s.empty();
                }
            }
            if (prop == menustyle::Prop::BackgroundImage) {
                ImGui::SetNextItemWidth(scaled(90.0f));
                ImGui::DragFloat("9-slice", &v.n[0], 0.5f, 0.0f, 64.0f, "%.0f px");
                edited |= ImGui::IsItemDeactivatedAfterEdit();
            }
            break;
        }
    }
    ImGui::EndDisabled();
    prefHelp(spec->doc);

    if (edited && have) {
        menuStylePush();
        have->value = v;
        menuStyleEdited();
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

// --- the Style tab ------------------------------------------------------------

void App::drawMenuStyleTab(GameMenu& m, bool& projectChanged) {
    menuStyleSync(m);

    // --- which sheet ---------------------------------------------------------
    const std::vector<menustyle::Sheet>& all = menustyle::sheets();
    std::string label = m.style.empty() ? "Classic (built-in)" : m.style;
    for (const menustyle::Sheet& s : all)
        if (s.key == m.style && !m.style.empty()) label = s.name + " (" + s.key + ")";
    ImGui::SetNextItemWidth(scaled(240.0f));
    if (ImGui::BeginCombo("Stylesheet", label.c_str())) {
        if (ImGui::Selectable("Classic (built-in)##styleclassic", m.style.empty())) {
            m.style.clear();
            projectChanged = true;
            menuStyleLoaded_ = false;
            menuPreviewKey_.clear();
        }
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].key == "classic") continue;  // that IS the entry above
            char id[128];
            std::snprintf(id, sizeof(id), "%s (%s)##sheet%zu", all[i].name.c_str(),
                          all[i].key.c_str(), i);
            if (ImGui::Selectable(id, m.style == all[i].key)) {
                m.style = all[i].key;
                projectChanged = true;
                menuStyleLoaded_ = false;
                menuPreviewKey_.clear();
            }
        }
        ImGui::EndCombo();
    }
    prefHelp("Which menu-styles/<name>.menustyle file bakes this menu. The\n"
             "built-in sheets are starting points - installing one copies it\n"
             "into the project, where it is yours to edit.\n\n"
             "Classic is the look menus had before stylesheets, and it is what\n"
             "every existing project keeps.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Install a copy...")) ImGui::OpenPopup("##installsheet");
    if (ImGui::BeginPopup("##installsheet")) {
        ImGui::TextDisabled("Copy a built-in sheet into the project:");
        for (size_t i = 0; i < menustyle::builtinSources().size(); ++i) {
            const auto& b = menustyle::builtinSources()[i];
            char id[96];
            std::snprintf(id, sizeof(id), "%s##inst%zu", b.first.c_str(), i);
            if (ImGui::Selectable(id)) {
                std::string key = b.first;
                for (int n = 2; menuStyleFileExists(key); ++n)
                    key = b.first + "-" + std::to_string(n);
                menustyle::Sheet sheet = menustyle::parse(b.second, key, "");
                if (menustyle::save(project_.dir, sheet)) {
                    m.style = key;
                    projectChanged = true;
                    menuStyleLoaded_ = false;
                    menuPreviewKey_.clear();
                }
            }
        }
        ImGui::EndPopup();
    }

    if (menuStyleStaged_.builtin && !m.style.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f),
                           "Built-in sheet - install a copy to edit it.");
    const bool editable = !menuStyleStaged_.builtin || m.style.empty();

    // --- diagnostics ---------------------------------------------------------
    if (!menuStyleStaged_.diags.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%d problem(s):",
                           (int)menuStyleStaged_.diags.size());
        for (const menustyle::Diag& d : menuStyleStaged_.diags)
            ImGui::BulletText("line %d: %s", d.line, d.message.c_str());
    }

    // --- save / undo ---------------------------------------------------------
    ImGui::BeginDisabled(!menuStyleDirty_ || m.style.empty());
    if (ImGui::Button("Save stylesheet")) {
        if (menustyle::save(project_.dir, menuStyleStaged_)) {
            menuStyleDirty_ = false;
            menuStyleLoaded_ = false;  // re-read what the file now says
            menuPreviewKey_.clear();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // The sheet is an ordinary text file, so the fastest way to make a big change
    // is the editor people already have open. It installs the TyraX extension on
    // first use, which is what colours and snippets `.menustyle`.
    ImGui::BeginDisabled(m.style.empty() || menuStyleStaged_.path.empty());
    if (ImGui::Button("Open in VS Code")) {
        if (menuStyleDirty_) menustyle::save(project_.dir, menuStyleStaged_);
        menuStyleDirty_ = false;
        menuStyleLoaded_ = false;  // re-read whatever comes back from the editor
        openInVSCode(menuStyleStaged_.path);
    }
    ImGui::EndDisabled();
    prefHelp("Opens menu-styles/<name>.menustyle in VS Code. Unsaved widget\n"
             "edits are written out first, so the two never disagree - and the\n"
             "Stylesheet tab's Reload picks up what you change there.");
    ImGui::SameLine();
    ImGui::BeginDisabled(menuStyleUndoAt_ == 0);
    if (ImGui::Button("Undo##style")) {
        menuStyleStaged_ = menuStyleUndo_[--menuStyleUndoAt_];
        menuStyleEdited();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(menuStyleUndoAt_ + 1 >= menuStyleUndo_.size());
    if (ImGui::Button("Redo##style")) {
        menuStyleStaged_ = menuStyleUndo_[++menuStyleUndoAt_];
        menuStyleEdited();
    }
    ImGui::EndDisabled();
    if (menuStyleDirty_) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "unsaved");
    }
    if (m.style.empty()) {
        ImGui::TextDisabled(
            "Classic has no file to edit. Install a copy of a built-in sheet\n"
            "above (or pick one) and every control below becomes live.");
    }

    ImGui::Checkbox("Only this menu", &menuStyleScoped_);
    prefHelp("Off: the rule applies to every menu using this sheet.\n"
             "On: it is written into a `menu#<name>` block, so it styles this\n"
             "menu alone. That is the same cascade the docs describe - menu\n"
             "scope beats a general rule.");

    ImGui::BeginDisabled(!editable);
    // --- the element groups --------------------------------------------------
    for (const ElemGroup& g : kGroups) {
        if (!ImGui::CollapsingHeader(g.label)) continue;
        ImGui::PushID(g.label);
        ImGui::TextDisabled("%s", g.help);
        for (menustyle::Prop prop : propsFor(g.elem))
            menuStyleProp(m, g.elem, menuStyleClass_, menustyle::StateNormal, prop);

        // Rows also carry their states and a class picker: the two things that
        // make a professional-looking selection possible at all.
        if (g.elem == menustyle::Elem::Row) {
            ImGui::Separator();
            char clsBuf[64];
            std::snprintf(clsBuf, sizeof(clsBuf), "%s", menuStyleClass_.c_str());
            ImGui::SetNextItemWidth(scaled(150.0f));
            if (ImGui::InputText("Class", clsBuf, sizeof(clsBuf)))
                menuStyleClass_ = clsBuf;
            prefHelp("Empty = every row. A name here edits `row.<name>` instead,\n"
                     "which styles only the rows whose Class field (Content tab)\n"
                     "matches - a header row, a danger row, a locked row.");
            for (int st = 1; st < menustyle::StateCount; ++st) {
                char hdr[64];
                std::snprintf(hdr, sizeof(hdr), "%s rows##st%d", kStateLabels[st], st);
                if (!ImGui::TreeNode(hdr)) continue;
                ImGui::TextDisabled(
                    st == menustyle::StateSelected
                        ? "What the highlighted row adds. Anything here costs one\n"
                          "baked cell per row - the readout below counts them."
                        : "What an unusable row looks like (a row whose\n"
                          "'Enabled when' save value is 0).");
                for (menustyle::Prop prop : propsFor(g.elem))
                    menuStyleProp(m, g.elem, menuStyleClass_, st, prop);
                ImGui::TreePop();
            }
        }
        ImGui::PopID();
    }

    // --- motion --------------------------------------------------------------
    if (ImGui::CollapsingHeader("Motion")) {
        ImGui::TextDisabled(
            "Sprite properties, not baked pixels: the panel slides and fades in,\n"
            "the caret eases between rows. Free on the console.");
        static const char* kWhich[] = {"Open", "Close", "Caret"};
        for (int wi = 0; wi < menustyle::Transition::WhichCount; ++wi) {
            ImGui::PushID(wi);
            menustyle::Transition* t = nullptr;
            for (menustyle::Transition& ex : menuStyleStaged_.transitions)
                if (ex.which == wi) t = &ex;
            bool on = t != nullptr;
            if (ImGui::Checkbox(kWhich[wi], &on)) {
                menuStylePush();
                if (on) {
                    menustyle::Transition nt;
                    nt.which = wi;
                    nt.seconds = 0.18f;
                    nt.ease = 1;
                    menuStyleStaged_.transitions.push_back(nt);
                } else {
                    menuStyleStaged_.transitions.erase(
                        std::remove_if(menuStyleStaged_.transitions.begin(),
                                       menuStyleStaged_.transitions.end(),
                                       [&](const menustyle::Transition& x) {
                                           return x.which == wi;
                                       }),
                        menuStyleStaged_.transitions.end());
                }
                menuStyleEdited();
                ImGui::PopID();
                continue;
            }
            if (t) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(90.0f));
                ImGui::DragFloat("s", &t->seconds, 0.005f, 0.0f, 2.0f, "%.2f s");
                const bool a = ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(110.0f));
                bool b = ImGui::Combo("##ease", &t->ease,
                                      "linear\0ease-out\0ease-in-out\0");
                bool c = false, d = false;
                if (wi != menustyle::Transition::Cursor) {
                    c = ImGui::Checkbox("fade", &t->fade);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(120.0f));
                    float dxy[2] = {t->translateX, t->translateY};
                    ImGui::DragFloat2("slide", dxy, 0.5f, -128.0f, 128.0f, "%.0f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        t->translateX = dxy[0];
                        t->translateY = dxy[1];
                        d = true;
                    }
                }
                if (a || b || c || d) {
                    menuStylePush();
                    menuStyleEdited();
                }
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();

    // --- what it costs -------------------------------------------------------
    drawMenuCost(m);
}

// --- the raw stylesheet tab ---------------------------------------------------

void App::drawMenuStyleText(GameMenu& m, bool& projectChanged) {
    (void)projectChanged;
    menuStyleSync(m);
    if (m.style.empty()) {
        ImGui::TextDisabled(
            "Classic is the built-in default and has no file. Install a copy of\n"
            "a sheet on the Style tab to get one you can type into.");
        return;
    }
    ImGui::TextDisabled("%s", menuStyleStaged_.path.empty()
                                  ? "built-in (install a copy to edit)"
                                  : menuStyleStaged_.path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload from disk")) {
        menuStyleLoaded_ = false;
        menuPreviewKey_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open in VS Code##text")) {
        openInVSCode(menuStyleStaged_.path);
        menuStyleLoaded_ = false;
    }

    // The buffer is the sheet's canonical text; parsing on every keystroke is
    // cheap (a sheet is a few KB) and it is what makes the diagnostics and the
    // preview follow the cursor.
    menuStyleTextBuf_.resize(std::max<size_t>(menuStyleText_.size() + 4096, 8192));
    std::snprintf(menuStyleTextBuf_.data(), menuStyleTextBuf_.size(), "%s",
                  menuStyleText_.c_str());
    if (ImGui::InputTextMultiline("##sheettext", menuStyleTextBuf_.data(),
                                  menuStyleTextBuf_.size(),
                                  ImVec2(-FLT_MIN, scaled(260.0f)))) {
        menuStyleText_ = menuStyleTextBuf_.data();
        menustyle::Sheet parsed = menustyle::parse(menuStyleText_,
                                                  menuStyleStaged_.key,
                                                  menuStyleStaged_.path);
        // Keep the text as typed (the writer would reformat it under the
        // cursor); only the AST follows, which is what the preview reads.
        const std::string keepText = menuStyleText_;
        menuStyleStaged_ = std::move(parsed);
        menuStyleDirty_ = true;
        menuStyleText_ = keepText;
        menuPreviewKey_.clear();
    }
    for (const menustyle::Diag& d : menuStyleStaged_.diags)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "line %d: %s", d.line,
                           d.message.c_str());
    ImGui::BeginDisabled(!menuStyleDirty_ || menuStyleStaged_.builtin);
    if (ImGui::Button("Save stylesheet##text")) {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(project_.dir) / "menu-styles", ec);
        std::ofstream f(std::filesystem::path(project_.dir) / "menu-styles" /
                        (menuStyleStaged_.key + ".menustyle"),
                        std::ios::binary);
        if (f) {
            f << menuStyleText_;
            f.close();
            menustyle::loadForProject(project_.dir);
            menuStyleDirty_ = false;
            menuStyleLoaded_ = false;
            menuPreviewKey_.clear();
        }
    }
    ImGui::EndDisabled();
    prefHelp("Saves the text exactly as typed. The Style tab's Save instead\n"
             "writes the canonical form of the same rules.");
}

// --- the cost readout ---------------------------------------------------------

void App::drawMenuCost(const GameMenu& m) {
    const menulayout::Layout L = menulayout::compute(m, project_);
    ImGui::SeparatorText("What it costs");
    const float pct = L.vramFraction() * 100.0f;
    ImGui::Text("%d texture(s), %d KB, %.1f%% of GS texture VRAM",
                (int)L.textures.size(), L.words() * 4 / 1024, pct);
    if (pct > 35.0f)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "That is a lot for one menu - drop `quant` to 4bit.");
    for (const menulayout::Texture& t : L.textures)
        ImGui::BulletText("%s  %dx%d  %d-bit", t.file.c_str(), t.w, t.h, t.bits);
    ImGui::Text("%d sprite(s) per frame, %d row(s) of %d visible",
                L.spritesPerFrame, L.rowsVisible, (int)L.rows.size());
    if (L.stateCells > 0)
        ImGui::Text("%d state cell(s) baked", L.stateCells);
    // Honest failure states rather than a silent cut (docs/menu-styles.md).
    if (L.clipped)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "Panel taller than 512 px - the bottom is clipped.");
    if (L.stateClipped)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "Too many state cells for one 512 px atlas - the rows\n"
                           "that missed out draw in their normal style.");
    if (L.listClipped)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "More rows than a 512 px strip holds - the tail is\n"
                           "clipped. Fewer rows, or a smaller row height.");
}

// --- the preview --------------------------------------------------------------

// --- the preview ---------------------------------------------------------------
// Three parts, because the preview is drawn in TWO places (the Menu Editor's own
// tab and the standalone Menu Preview window) and they must not become two
// previews: one refresh that owns the bake, one control row, one draw. The
// texture is shared - a display mode only changes how it is PRESENTED, never
// what is baked - so both windows can sit on different modes at once.

void App::menuPreviewRefresh(const GameMenu& m) {
    const menulayout::Layout L = menulayout::compute(m, project_);
    const int rows = (int)m.entries.size();
    if (menuPreviewRow_ >= rows) menuPreviewRow_ = rows > 0 ? rows - 1 : 0;
    if (L.rowsVisible > 0 && L.rowsVisible < rows) {
        if (menuPreviewRow_ < menuPreviewScroll_) menuPreviewScroll_ = menuPreviewRow_;
        if (menuPreviewRow_ >= menuPreviewScroll_ + L.rowsVisible)
            menuPreviewScroll_ = menuPreviewRow_ - L.rowsVisible + 1;
    } else {
        menuPreviewScroll_ = 0;
    }

    // The cache key is everything the bake reads: the menu, the sheet (the
    // staged one, so a widget drag is visible immediately) and the simulated
    // state.
    std::string key = m.name + "\x1f" + m.title + "\x1f" +
                      std::to_string(m.panelW) + "\x1f" +
                      std::to_string(m.showTitle) + "\x1f" + m.font + "\x1f" +
                      std::to_string(m.titleSize) + "|" +
                      std::to_string(m.entrySize) + "\x1f" +
                      std::to_string(m.accent[0]) + "," +
                      std::to_string(m.accent[1]) + "," +
                      std::to_string(m.accent[2]) + "\x1f" + m.style;
    for (const MenuImage& img : m.images)
        key += "\x1f" + img.path + "|" + std::to_string(img.slot) + "|" +
               std::to_string(img.scale) + "|" + std::to_string(img.offset[0]) + "," +
               std::to_string(img.offset[1]);
    for (const MenuEntry& en : m.entries) {
        key += "\x1f" + en.label + "|" + std::to_string(en.action) + "|" + en.param +
               "|" + en.styleClass + "|" + en.description + "|" + en.icon + "|" +
               en.enabledWhen;
        for (const std::string& opt : en.options) key += "," + opt;
    }
    for (const SaveValue& sv : project_.saveValues)
        key += "\x1f" + sv.name + "=" + std::to_string((int)sv.value);
    key += "\x1fsel" + std::to_string(menuPreviewRow_) + "/" +
           std::to_string(menuPreviewScroll_);
    key += "\x1f" + menuStyleText_;  // the staged sheet, verbatim
    if (key == menuPreviewKey_) return;

    // The staged sheet has to be what the bake sees, or the preview would show
    // the file on disk while the widgets say otherwise. The registry is the
    // single lookup every consumer uses, so the staged copy is pushed into it
    // (the same thing Save does, minus the file).
    const bool stage = !m.style.empty() && menuStyleLoaded_;
    if (stage) menustyle::stage(menuStyleStaged_);
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    std::vector<int> current(m.entries.size(), 0);
    std::vector<char> disabled(m.entries.size(), 0);
    for (size_t e = 0; e < m.entries.size(); ++e) {
        for (const SaveValue& sv : project_.saveValues) {
            if (sv.name == m.entries[e].param) current[e] = (int)sv.value;
            if (!m.entries[e].enabledWhen.empty() &&
                sv.name == m.entries[e].enabledWhen && sv.value == 0.0f)
                disabled[e] = 1;
        }
    }
    if (menubake::bakeMenuPreviewRGBA(m, project_, menuPreviewRow_, disabled,
                                     menuPreviewScroll_, current, rgba, w, h)) {
        if (!menuPreviewTex_) glGenTextures(1, &menuPreviewTex_);
        glBindTexture(GL_TEXTURE_2D, menuPreviewTex_);
        glUploadTexRgba(w, h, rgba.data());
        menuPreviewW_ = w;
        menuPreviewH_ = h;
        menuPreviewContentH_ = L.contentH;
        menuPreviewClipped_ = L.clipped;
    }
    if (stage) menustyle::unstage();
    menuPreviewKey_ = key;
}

// The mode picker + the simulated cursor. `mode` is per-window: 0 = the baked
// pixels, 1.. = the project's supported resolutions.
void App::menuPreviewControls(const GameMenu& m, int& mode) {
    // Mode 0 is the panel at its baked pixels; the rest are the project's
    // SUPPORTED resolutions (Preferences > Display), each showing the panel the
    // size the game will actually draw it - the framebuffer is 512x448
    // interlaced but 448x540 in 1080i, so "does my menu still fit" is a
    // per-resolution question (docs/menu-styles.md "Resolutions").
    const std::vector<std::string> modes =
        project::supportedDisplayModes(project_.settings);
    // ImGui's combo takes a NUL-separated list, and a std::string built from a
    // "\0" literal is EMPTY (the char* constructor stops at the NUL) - so the
    // separators go in one character at a time.
    std::string modeItems = "Panel (baked pixels)";
    for (const std::string& k : modes) {
        modeItems.push_back('\0');
        modeItems += project::displayModeInfo(k).label;
    }
    modeItems.push_back('\0');
    if (mode > (int)modes.size()) mode = 0;
    ImGui::SetNextItemWidth(scaled(210.0f));
    // A visible label, not "##previewmode": a bare ## id cannot be targeted by
    // --ui-script (it has no name), which is exactly how a combo like this goes
    // unchecked (see docs/ui-scripting.md).
    ImGui::Combo("Preview in", &mode, modeItems.c_str());
    ImGui::SameLine();
    // The simulated cursor: a styled menu's whole point is what the SELECTED
    // row looks like, so the preview has to be able to move it.
    const int rows = (int)m.entries.size();
    ImGui::BeginDisabled(rows <= 0);
    if (ImGui::ArrowButton("##prevup", ImGuiDir_Up) && rows > 0)
        menuPreviewRow_ = (menuPreviewRow_ + rows - 1) % rows;
    ImGui::SameLine(0.0f, 2.0f);
    if (ImGui::ArrowButton("##prevdown", ImGuiDir_Down) && rows > 0)
        menuPreviewRow_ = (menuPreviewRow_ + 1) % rows;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("row %d", rows > 0 ? menuPreviewRow_ + 1 : 0);
    prefHelp("Moves the simulated cursor. The preview then draws the same cells\n"
             "the game picks - selected row, disabled rows, the description.");
}

void App::menuPreviewDraw(const GameMenu& m, int mode, float zoom) {
    if (!menuPreviewTex_) return;
    if (mode == 0) {
        // Baked at native PS2 pixels; upscale the on-screen copy so it isn't a
        // postage stamp next to the DPI-scaled controls.
        ImGui::Image((ImTextureID)(intptr_t)menuPreviewTex_,
                     ImVec2(scaled((float)menuPreviewW_ * zoom),
                            scaled((float)menuPreviewContentH_ * zoom)),
                     ImVec2(0.0f, 0.0f),
                     ImVec2(1.0f, (float)menuPreviewContentH_ /
                                      (float)menuPreviewH_));
    } else {
        // A mock TV for one display mode: its framebuffer stretched to the
        // physical shape of its display window, with the panel scaled exactly
        // the way renderGameMenu scales it (the same two factors).
        const std::vector<std::string> modes =
            project::supportedDisplayModes(project_.settings);
        if (mode - 1 >= (int)modes.size()) return;
        const std::string modeKey = modes[(size_t)mode - 1];
        const DisplayModeInfo& dm = project::displayModeInfo(modeKey);
        const float bufW = (float)dm.bufW;
        const float bufH = (float)dm.logicalH;
        float windowAspect =
            project_.settings.widescreen ? (16.0f / 9.0f) : (4.0f / 3.0f);
        if (modeKey == "1080i" && project_.settings.widescreen)
            windowAspect = (1792.0f / 1920.0f) * (16.0f / 9.0f);
        const float sw = scaled(320.0f * zoom);
        const float sh = sw / windowAspect;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + sw, p0.y + sh);
        dl->AddRectFilled(p0, p1, IM_COL32(18, 20, 26, 255));
        if (m.pauseGame) dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 115));
        // The runtime's own scale, then buffer -> screen.
        const float uiSX = bufW / 512.0f, uiSY = bufH / 448.0f;
        const float toX = sw / bufW, toY = sh / bufH;
        const float panelW = menuPreviewW_ * uiSX;
        const float panelH = menuPreviewContentH_ * uiSY;
        const float panelX = m.screenPos[0] * bufW - panelW * 0.5f;
        const float panelY = m.screenPos[1] * bufH - panelH * 0.5f;
        const ImVec2 m0(p0.x + panelX * toX, p0.y + panelY * toY);
        const ImVec2 m1(m0.x + panelW * toX, m0.y + panelH * toY);
        dl->AddImage((ImTextureID)(intptr_t)menuPreviewTex_, m0, m1, ImVec2(0, 0),
                     ImVec2(1.0f, (float)menuPreviewContentH_ /
                                      (float)menuPreviewH_));
        dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 120));
        char tag[96];
        std::snprintf(tag, sizeof(tag), "%s  %.0fx%.0f  %s", dm.label, bufW, bufH,
                      project_.settings.widescreen ? "16:9" : "4:3");
        dl->AddText(ImVec2(p0.x + 4, p1.y - 18), IM_COL32(255, 255, 255, 160), tag);
        ImGui::Dummy(ImVec2(sw, sh + 4.0f));
        // Does it still fit? A panel authored for 512x448 is 87.5% as wide in
        // 480p/1080i, so overflow is rare - but a full-screen panel placed near
        // an edge does run off, and that is worth saying before the build.
        if (panelX < 0.0f || panelY < 0.0f || panelX + panelW > bufW ||
            panelY + panelH > bufH)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.35f, 1.0f),
                               "Runs off the screen in this mode - move it or "
                               "make it narrower.");
    }
    if (menuPreviewClipped_)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "Panel taller than 512 px - the bottom gets clipped.");
}

// The Menu Editor's own compact preview, above its tabs.
void App::drawMenuPreview(const GameMenu& m) {
    ImGui::SeparatorText("Preview");
    menuPreviewControls(m, menuPreviewMode_);
    menuPreviewRefresh(m);
    menuPreviewDraw(m, menuPreviewMode_, 1.0f);
}

// The standalone Menu Preview window (Tools > Menu Preview, and part of the
// Menu Designer layout). It follows the Menu Editor's selection and shares its
// baked texture - what it adds is ROOM: a zoom, its own display mode, and the
// cost readout next to a panel big enough to judge.
void App::drawMenuPreviewWindow() {
    if (!showMenuPreview_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(520), scaled(560)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Menu Preview", &showMenuPreview_)) {
        ImGui::End();
        return;
    }
    if (project_.menus.empty()) {
        ImGui::TextDisabled("No menus in this project yet.");
        ImGui::End();
        return;
    }
    // Its own menu picker, so the window is useful with the Menu Editor closed;
    // it defaults to whatever the editor has selected.
    if (selectedMenu_ < 0 || selectedMenu_ >= (int)project_.menus.size())
        selectedMenu_ = 0;
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::BeginCombo("Menu", project_.menus[selectedMenu_].name.c_str())) {
        for (size_t i = 0; i < project_.menus.size(); ++i) {
            char id[96];
            std::snprintf(id, sizeof(id), "%s##pvm%zu",
                          project_.menus[i].name.c_str(), i);
            if (ImGui::Selectable(id, (int)i == selectedMenu_)) {
                selectedMenu_ = (int)i;
                menuStyleLoaded_ = false;  // the sheet follows the selection
                menuPreviewKey_.clear();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(120.0f));
    ImGui::SliderFloat("Zoom", &menuPreviewZoom_, 0.5f, 4.0f, "%.1fx");

    const GameMenu& m = project_.menus[selectedMenu_];
    menuStyleSync(m);
    menuPreviewControls(m, menuPreviewWinMode_);
    menuPreviewRefresh(m);
    ImGui::BeginChild("##menu_preview_img", ImVec2(0, -scaled(160.0f)), 0,
                      ImGuiWindowFlags_HorizontalScrollbar);
    menuPreviewDraw(m, menuPreviewWinMode_, menuPreviewZoom_);
    ImGui::EndChild();
    drawMenuCost(m);
    ImGui::End();
}
