// -------------------------------------------------------------------------
// Tools > World Facts (docs/world-facts.md): the game's central memory - the
// declared catalog of facts, the reusable named conditions over them, the
// rules that react to them, the saved test scenarios, and the live World
// Blackboard.
//
// Its own translation unit for the reason every other *_ui.cpp is one. These
// are still App:: members declared in app.hpp.
//
// Two things here are load-bearing and worth not undoing:
//
//  - The window owns a `project::Section`, so it takes the sectionJson guard
//    (see "A hand-set bool changed is not enough on its own" in the
//    tyra-editor-dev skill): a widget added tomorrow that forgets to set
//    `changed` is still caught by comparing the whole section across the body.
//    The Menu Editor's dark save icon is what that guard exists to prevent.
//
//  - A rename goes through project::renameFactRefs, never by hand. A fact is
//    referenced by NAME from flow-graph nodes, query leaves, rule conditions,
//    rule actions and scenario rows, and every one of those is retargeted in
//    one place - the renameObjectRefs contract.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "facts.hpp"

#include <imgui.h>

namespace {

// A unique name in a collection, "base 2", "base 3"... The New-anything idiom
// used all over the editor; here it also stops a duplicate from being created
// at all, which is one class of validation error that never has to be shown.
template <typename Fn>
std::string uniqueName(std::string base, Fn taken) {
    if (base.empty()) base = "fact";
    std::string n = base;
    for (int i = 2; taken(n); ++i) n = base + " " + std::to_string(i);
    return n;
}

// A fact name is hierarchical by convention ("world.power.state"); the
// catalog list nests on the dots and this is the last segment - the part a
// leaf row shows once its groups are already on screen.
std::string factLeaf(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

bool matchesFilter(const facts::Fact& f, const std::string& filter) {
    if (filter.empty()) return true;
    std::string hay = f.name + " " + f.desc;
    std::string needle = filter;
    std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    return hay.find(needle) != std::string::npos;
}

void inputText(const char* id, std::string& value, bool* changed,
               const char* hint = nullptr) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    const bool edited =
        hint ? ImGui::InputTextWithHint(id, hint, buf, sizeof(buf))
             : ImGui::InputText(id, buf, sizeof(buf));
    if (edited) value = buf;
    if (ImGui::IsItemDeactivatedAfterEdit() && changed) *changed = true;
}

// ImGui's own std::string bridge (vendor/imgui/misc/cpp/imgui_stdlib.cpp is
// not in the build): the widget writes into the string's own buffer and asks
// for a bigger one through this callback, so the field has no length limit.
int growString(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
    auto* s = static_cast<std::string*>(d->UserData);
    s->resize((size_t)d->BufTextLen);
    d->Buf = s->data();
    return 0;
}

// A PROSE field: one row high while the text is one row, taller as it wraps or
// gains newlines, up to a cap it starts scrolling at. For the descriptions -
// which are the one thing in this window written in sentences.
//
// Two things the single-line helper above gets wrong for a paragraph. Its
// buffer is a fixed 256 bytes, and a catalog written by hand or by an example's
// build script has entries several times that, so opening the field would have
// silently truncated one - hence the resize callback. And prose WRAPS, which a
// single-line field answers by scrolling sideways, one word visible at a time.
//
// The wrap width is measured a shade narrow on purpose. ImGui wraps against
// the inner width of the child window the widget builds, which depends on
// whether a scrollbar is up; guessing narrow costs at most one spare row, and
// guessing wide would leave the last row clipped below the box.
void inputTextProse(const char* id, std::string& value, bool* changed,
                    int maxRows = 12) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowH = ImGui::GetTextLineHeight();
    const float wrap = std::max(
        ImGui::GetFontSize() * 2.0f,
        ImGui::CalcItemWidth() - st.FramePadding.x * 2.0f - st.ScrollbarSize -
            2.0f);
    float textH = value.empty()
                      ? rowH
                      : ImGui::CalcTextSize(value.c_str(), nullptr, false, wrap).y;
    // A trailing newline draws no glyphs, so the measurement misses the row the
    // caret is sitting on - the one case where the box would otherwise grow
    // only after the NEXT character is typed.
    if (!value.empty() && value.back() == '\n') textH += rowH;
    int rows = (int)(textH / rowH + 0.5f);
    if (rows < 1) rows = 1;
    if (rows > maxRows) rows = maxRows;

    const ImVec2 size(0.0f, rows * rowH + st.FramePadding.y * 2.0f);
    const bool edited = ImGui::InputTextMultiline(
        id, value.data(), value.capacity() + 1, size,
        ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackResize,
        growString, &value);
    (void)edited;
    if (ImGui::IsItemDeactivatedAfterEdit() && changed) *changed = true;
}

// Everything the InputText callback needs. The highlight lives in the App so
// it survives between frames; the callback only nudges it, because Up/Down
// reach the FIELD (which has the keyboard) and never the list.
struct NameEdit {
    const std::vector<facts::Fact>* facts = nullptr;
    int skip = -1;                                // never complete against self
    const std::vector<std::string>* cands = nullptr;
    int* sel = nullptr;
    bool* caretEnd = nullptr;
};

// Longest common prefix of everything that matches - the shell's rule, and the
// fallback when no row is highlighted. Predictable in a way "cycle through the
// candidates" is not.
std::string commonCompletion(const std::vector<facts::Fact>& facts,
                             const std::string& typed, int skip) {
    std::string best;
    bool any = false;
    for (size_t i = 0; i < facts.size(); ++i) {
        if ((int)i == skip) continue;
        const std::string& n = facts[i].name;
        if (n.size() < typed.size() || n.compare(0, typed.size(), typed) != 0)
            continue;
        if (!any) {
            best = n;
            any = true;
            continue;
        }
        size_t k = 0;
        while (k < best.size() && k < n.size() && best[k] == n[k]) ++k;
        best.resize(k);
    }
    return (any && best.size() > typed.size()) ? best : std::string();
}

int factNameCallback(ImGuiInputTextCallbackData* data) {
    NameEdit* e = (NameEdit*)data->UserData;
    switch (data->EventFlag) {
        case ImGuiInputTextFlags_CallbackAlways:
            // Requested after an accept: drop the selection and put the caret
            // past what was just inserted, so typing CONTINUES the name
            // instead of replacing it (which is what a plain refocus does).
            if (e->caretEnd && *e->caretEnd) {
                *e->caretEnd = false;
                data->CursorPos = data->BufTextLen;
                data->SelectionStart = data->SelectionEnd = data->CursorPos;
            }
            break;
        case ImGuiInputTextFlags_CallbackHistory: {
            // Up/Down walk the dropdown. They reach here rather than the list
            // because the FIELD holds the keyboard the whole time - which is
            // the entire reason the list is not a focusable popup.
            if (!e->cands || e->cands->empty() || !e->sel) break;
            const int n = (int)e->cands->size();
            if (data->EventKey == ImGuiKey_UpArrow)
                *e->sel = *e->sel <= 0 ? n - 1 : *e->sel - 1;
            else if (data->EventKey == ImGuiKey_DownArrow)
                *e->sel = (*e->sel + 1) % n;
            break;
        }
        case ImGuiInputTextFlags_CallbackCompletion: {
            std::string to;
            if (e->sel && *e->sel >= 0 && e->cands &&
                *e->sel < (int)e->cands->size())
                to = (*e->cands)[(size_t)*e->sel];
            else
                to = commonCompletion(*e->facts,
                                      std::string(data->Buf,
                                                  (size_t)data->BufTextLen),
                                      e->skip);
            if (to.empty()) break;
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, to.c_str());
            break;
        }
        default: break;
    }
    return 0;
}

// What could follow what has been typed: every existing name that starts with
// it, cut at the next dot. Typing "world." offers world.generator. /
// world.power. / world.alarm. - one step at a time, the way a namespace
// browser walks, rather than a wall of full names.
std::vector<std::string> completionsFor(const std::vector<facts::Fact>& facts,
                                        const std::string& typed, int skip) {
    std::vector<std::string> out;
    if (typed.empty()) return out;
    for (size_t i = 0; i < facts.size(); ++i) {
        if ((int)i == skip) continue;
        const std::string& n = facts[i].name;
        if (n.size() <= typed.size() ||
            n.compare(0, typed.size(), typed) != 0)
            continue;
        const size_t dot = n.find('.', typed.size());
        const std::string cand =
            dot == std::string::npos ? n : n.substr(0, dot + 1);
        bool seen = false;
        for (const std::string& e : out) seen |= (e == cand);
        if (!seen) out.push_back(cand);
    }
    return out;
}

}  // namespace

// --------------------------------------------------------------- pickers ---

bool App::factCombo(const char* id, std::string& value, bool positionsToo) {
    bool changed = false;
    const char* preview = value.empty() ? "<none>" : value.c_str();
    if (!ImGui::BeginCombo(id, preview)) return false;
    if (ImGui::Selectable("<none>", value.empty())) {
        value.clear();
        changed = true;
    }
    for (size_t i = 0; i < project_.facts.size(); ++i) {
        const facts::Fact& f = project_.facts[i];
        if (!positionsToo && f.type == facts::Type::Position) continue;
        // An explicit ##index on every entry: a Selectable's LABEL is its
        // ImGui id, and two facts can legitimately share a leaf name across
        // groups (the droneui.cpp knob-label trap, same cause).
        const std::string label = f.name + "##fc" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), f.name == value)) {
            value = f.name;
            changed = true;
        }
        if (ImGui::IsItemHovered() && !f.desc.empty())
            ImGui::SetTooltip("%s", f.desc.c_str());
    }
    ImGui::EndCombo();
    return changed;
}

bool App::factQueryCombo(const char* id, std::string& value) {
    bool changed = false;
    const char* preview = value.empty() ? "<none>" : value.c_str();
    if (!ImGui::BeginCombo(id, preview)) return false;
    if (ImGui::Selectable("<none>", value.empty())) {
        value.clear();
        changed = true;
    }
    for (size_t i = 0; i < project_.factQueries.size(); ++i) {
        const facts::Query& q = project_.factQueries[i];
        const std::string label = q.name + "##qc" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), q.name == value)) {
            value = q.name;
            changed = true;
        }
        if (ImGui::IsItemHovered() && !q.desc.empty())
            ImGui::SetTooltip("%s", q.desc.c_str());
    }
    ImGui::EndCombo();
    return changed;
}

bool App::factValueWidget(const char* id, const facts::Fact& f, float* v3) {
    // The widget is chosen from the DECLARED type and nowhere else, so the
    // catalog, the rule actions, the scenarios and the blackboard all present
    // a fact the same way. A one-of-several fact is the reason this exists:
    // everywhere else it would be an int nobody can read.
    switch (f.type) {
        case facts::Type::Bool: {
            bool b = v3[0] != 0.0f;
            if (ImGui::Checkbox(id, &b)) {
                v3[0] = b ? 1.0f : 0.0f;
                return true;
            }
            return false;
        }
        case facts::Type::Int: {
            int v = (int)std::lround(v3[0]);
            if (ImGui::DragInt(id, &v, 1.0f)) {
                v3[0] = (float)v;
                return true;
            }
            return false;
        }
        case facts::Type::Float:
            return ImGui::DragFloat(id, &v3[0], 0.01f);
        case facts::Type::Enum: {
            if (f.options.empty()) {
                ImGui::TextDisabled("no options");
                return false;
            }
            int idx = (int)std::lround(v3[0]);
            if (idx < 0 || idx >= (int)f.options.size()) idx = 0;
            if (!ImGui::BeginCombo(id, f.options[(size_t)idx].c_str()))
                return false;
            bool changed = false;
            for (size_t i = 0; i < f.options.size(); ++i) {
                const std::string label =
                    f.options[i] + "##ev" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), (int)i == idx)) {
                    v3[0] = (float)i;
                    changed = true;
                }
            }
            ImGui::EndCombo();
            return changed;
        }
        case facts::Type::Position:
            return ImGui::DragFloat3(id, v3, 0.1f);
    }
    return false;
}

// ------------------------------------------------------------ live values ---

bool App::factLiveValue(const std::string& name, float* out3) const {
    out3[0] = out3[1] = out3[2] = 0.0f;
    const int fi = facts::indexOf(project_.facts, name);
    if (fi < 0) return false;
    const facts::Fact& f = project_.facts[(size_t)fi];

    // A running game is the truth when there is one. The blackboard reads
    // facts out of the SAME watch table the flow variables ride, so this is a
    // lookup in dbgSyms_ rather than a second channel: the sym file names
    // every watch slot, and a fact's entry carries kind 'f' (scalar) or 'F'
    // (position).
    if (dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted) {
        for (size_t i = 0; i < dbgSyms_.vars.size(); ++i) {
            const livedbg::VarSym& v = dbgSyms_.vars[i];
            if ((v.kind != 'f' && v.kind != 'F') || v.name != name) continue;
            if (i * 3 + 2 < dbgSnap_.vars.size()) {
                out3[0] = dbgSnap_.vars[i * 3 + 0];
                out3[1] = dbgSnap_.vars[i * 3 + 1];
                out3[2] = dbgSnap_.vars[i * 3 + 2];
                return true;
            }
        }
    }

    // Nothing attached: the catalog's own defaults, which is what a fresh game
    // would start at. That is what makes "Why?" answerable with the game off.
    if (f.isComputed()) {
        const int qi = facts::queryIndexOf(project_.factQueries, f.computed);
        if (qi < 0) return false;
        const bool v = facts::evaluate(
            project_.factQueries[(size_t)qi].root, project_.facts,
            project_.factQueries,
            [this](const std::string& n, float* o) {
                return factLiveValue(n, o);
            });
        out3[0] = v ? 1.0f : 0.0f;
        return true;
    }
    if (f.type == facts::Type::Position) {
        for (int a = 0; a < 3; ++a) out3[a] = f.pos[a];
        return true;
    }
    out3[0] = facts::defaultNum(f);
    return true;
}

void App::factPushOverrides() {
    // Overrides ride the Live Debugger's command channel: it is already the
    // "editor tells the running game to do something" direction, already
    // throttled, already guarded against torn writes. A second channel for
    // four floats would be a second thing to keep working.
    dbgCmd_.factSets.clear();
    const facts::Layout lay = facts::layoutOf(project_.facts);
    for (const auto& [name, v] : factOverrides_) {
        const int fi = facts::indexOf(project_.facts, name);
        if (fi < 0) continue;
        livedbg::FactSet fs;
        if (lay.posOf[(size_t)fi] >= 0) {
            fs.slot = lay.posOf[(size_t)fi];
            fs.isPosition = true;
        } else if (lay.numOf[(size_t)fi] >= 0) {
            fs.slot = lay.numOf[(size_t)fi];
        } else {
            continue;  // computed: nothing to hold
        }
        for (int a = 0; a < 3; ++a) fs.v[a] = v[(size_t)a];
        if ((int)dbgCmd_.factSets.size() >= livedbg::kMaxFactSets) break;
        dbgCmd_.factSets.push_back(fs);
    }
    dbgCmdWritten_ = false;  // livedbgTick writes it on its next pass
}

// ----------------------------------------------------- condition editor ---

bool App::drawFactCondition(facts::Condition& c, int depth, const char* id) {
    bool changed = false;
    ImGui::PushID(id);

    const bool group = c.kind == facts::Condition::Kind::All ||
                       c.kind == facts::Condition::Kind::Any ||
                       c.kind == facts::Condition::Kind::Not;

    // The kind combo first: it is what the row IS, and switching it must not
    // silently keep a leaf's fact hanging around in a group (or the other way).
    ImGui::SetNextItemWidth(scaled(110));
    const char* kinds[] = {"ALL of", "ANY of", "NONE of", "compare", "query"};
    int kindIdx = (int)c.kind;
    if (ImGui::Combo("##kind", &kindIdx, kinds, 5)) {
        c.kind = (facts::Condition::Kind)kindIdx;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "ALL - every row below must hold.\n"
            "ANY - at least one must.\n"
            "NONE - none of them may.\n"
            "compare - one fact against a value or another fact.\n"
            "query - a named condition from the Queries tab.\n\n"
            "An empty ALL is true and an empty ANY is false, here and on the\n"
            "console - the same arithmetic, so the preview cannot disagree\n"
            "with the game.");

    if (c.kind == facts::Condition::Kind::Compare) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(190));
        if (factCombo("##fact", c.fact, false)) changed = true;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(100));
        const auto& ops = facts::cmpInfos();
        std::string opPreview = facts::cmpLabel(c.cmp);
        if (ImGui::BeginCombo("##cmp", opPreview.c_str())) {
            for (size_t i = 0; i < ops.size(); ++i) {
                const std::string label =
                    std::string(ops[i].label) + "##op" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), ops[i].op == c.cmp)) {
                    c.cmp = ops[i].op;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        // Against a constant, or against another fact. One toggle rather than
        // two comparison kinds: "marta.trust at least quest.threshold" is the
        // same sentence with a different right-hand side.
        bool vsFact = !c.rhsFact.empty();
        if (ImGui::Checkbox("vs fact", &vsFact)) {
            if (!vsFact) c.rhsFact.clear();
            else if (c.rhsFact.empty() && !project_.facts.empty())
                c.rhsFact = project_.facts[0].name;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(170));
        if (vsFact) {
            if (factCombo("##rhs", c.rhsFact, false)) changed = true;
        } else {
            const int fi = facts::indexOf(project_.facts, c.fact);
            if (fi >= 0) {
                float v3[3] = {c.value, 0, 0};
                if (factValueWidget("##val", project_.facts[(size_t)fi], v3)) {
                    c.value = v3[0];
                    changed = true;
                }
            } else if (ImGui::DragFloat("##val", &c.value, 0.1f)) {
                changed = true;
            }
        }
    } else if (c.kind == facts::Condition::Kind::Query) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(220));
        if (factQueryCombo("##query", c.query)) changed = true;
    }

    if (group) {
        ImGui::SameLine();
        // A depth cap that is about the UI, not the language: past this the
        // rows have no width left to be readable in, and a condition that
        // deep wants to be a named query anyway.
        const bool canNest = depth < 5;
        if (ImGui::SmallButton("+ compare")) {
            facts::Condition ch;
            ch.kind = facts::Condition::Kind::Compare;
            if (!project_.facts.empty()) ch.fact = project_.facts[0].name;
            c.children.push_back(ch);
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ query")) {
            facts::Condition ch;
            ch.kind = facts::Condition::Kind::Query;
            c.children.push_back(ch);
            changed = true;
        }
        if (canNest) {
            ImGui::SameLine();
            if (ImGui::SmallButton("+ group")) {
                facts::Condition ch;
                ch.kind = facts::Condition::Kind::All;
                c.children.push_back(ch);
                changed = true;
            }
        }

        ImGui::Indent(scaled(16));
        int remove = -1;
        for (size_t i = 0; i < c.children.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x")) remove = (int)i;
            ImGui::SameLine();
            const std::string cid = "c" + std::to_string(i);
            if (drawFactCondition(c.children[i], depth + 1, cid.c_str()))
                changed = true;
            ImGui::PopID();
        }
        if (remove >= 0) {
            c.children.erase(c.children.begin() + remove);
            changed = true;
        }
        ImGui::Unindent(scaled(16));
    }

    ImGui::PopID();
    return changed;
}

// ------------------------------------------------------------ the window ---

void App::drawWorldFactsWindow() {
    if (!showWorldFacts_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(820), scaled(560)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("World Facts", &showWorldFacts_)) {
        ImGui::End();
        return;
    }

    // The section guard (see the file header). Taken across the WHOLE body,
    // so a widget added later that forgets to set its own flag is still
    // caught. Nothing in this window repairs state on entry, so there is no
    // "after the repair" subtlety to get wrong here.
    const std::string before =
        project::sectionJson(project_, project::Section::Facts);

    if (ImGui::BeginTabBar("##factstabs")) {
        if (ImGui::BeginTabItem("Catalog")) {
            drawFactCatalogTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Queries")) {
            drawFactQueriesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Rules")) {
            drawFactRulesTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scenarios")) {
            drawFactScenariosTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Blackboard")) {
            drawFactBlackboardTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Validation, always visible: a catalog is a contract several systems
    // depend on, and the moment to hear that a rule writes a computed fact is
    // while you are looking at the rule - not from a compiler forty minutes
    // later inside Docker.
    const std::vector<facts::Issue> issues =
        facts::validate(project_.facts, project_.factQueries, project_.factRules,
                        project_.factScenarios);
    if (!issues.empty()) {
        int errors = 0;
        for (const facts::Issue& i : issues) errors += i.error ? 1 : 0;
        ImGui::Separator();
        const std::string label =
            errors ? std::to_string(errors) + " problem(s), " +
                         std::to_string((int)issues.size() - errors) + " note(s)"
                   : std::to_string((int)issues.size()) + " note(s)";
        if (ImGui::CollapsingHeader(label.c_str(),
                                    errors ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
            for (const facts::Issue& i : issues) {
                const ImVec4 col = i.error ? ImVec4(0.95f, 0.45f, 0.40f, 1.0f)
                                           : ImVec4(0.90f, 0.78f, 0.35f, 1.0f);
                ImGui::TextColored(col, "%s", i.error ? "error" : "note");
                ImGui::SameLine();
                ImGui::TextWrapped("%s - %s", i.where.c_str(), i.text.c_str());
            }
        }
    }

    if (project::sectionJson(project_, project::Section::Facts) != before)
        commitChange();

    ImGui::End();
}

// ------------------------------------------------------- the name field ---

void App::drawFactNameField(facts::Fact& f) {
    // --- keys first, from LAST frame's state. The InputText consumes Enter
    // itself, so an accept has to be decided before the field is submitted.
    bool accept = false;
    if (factNameSuggestOpen_ && factNameActive_) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            factNameSuggestOpen_ = false;
            factNameSuggestSel_ = -1;
        } else if (factNameSuggestSel_ >= 0 &&
                   (ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            accept = true;
        }
    }
    if (accept && factNameSuggestSel_ < (int)factNameSuggest_.size()) {
        if (factRenameFrom_.empty()) factRenameFrom_ = f.name;
        f.name = factNameSuggest_[(size_t)factNameSuggestSel_];
        factNameSuggestSel_ = -1;
        factNameCaretEnd_ = true;
        // Enter also deactivated the field, so hand the keyboard back - the
        // callback above then drops the select-all a refocus would otherwise
        // leave behind.
        factNameRefocus_ = true;
    }

    if (factNameRefocus_) {
        factNameRefocus_ = false;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(scaled(280));
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", f.name.c_str());
    NameEdit edit{&project_.facts, factSel_, &factNameSuggest_,
                  &factNameSuggestSel_, &factNameCaretEnd_};
    ImGui::InputText("Name", buf, sizeof(buf),
                     ImGuiInputTextFlags_CallbackCompletion |
                         ImGuiInputTextFlags_CallbackHistory |
                         ImGuiInputTextFlags_CallbackAlways,
                     factNameCallback, &edit);
    const std::string typed = buf;
    const bool active = ImGui::IsItemActive();
    const ImVec2 fieldMin = ImGui::GetItemRectMin();
    const ImVec2 fieldMax = ImGui::GetItemRectMax();

    if (typed != f.name) {
        if (factRenameFrom_.empty()) factRenameFrom_ = f.name;
        f.name = typed;
        // A fresh keystroke re-opens a list Escape had dismissed, and drops a
        // highlight that no longer points at what it did.
        factNameSuggestOpen_ = true;
        factNameSuggestSel_ = -1;
    }
    // The rename dance: retarget every reference the moment the edit commits.
    if (ImGui::IsItemDeactivatedAfterEdit() && !factRenameFrom_.empty() &&
        factRenameFrom_ != f.name) {
        const std::string to = f.name;
        f.name = factRenameFrom_;  // rename FROM the old name, project-wide
        project::renameFactRefs(project_, factRenameFrom_, to);
        f.name = to;
        factRenameFrom_.clear();
    }
    prefHelp(
        "Dots make the hierarchy: characters.marta.trust groups under "
        "characters.marta, and the list nests on them. Type a prefix and the "
        "dropdown offers what already exists at that level - arrows to pick, "
        "Tab or Enter to take it, Escape to dismiss. Renaming retargets every "
        "graph node, query, rule and scenario that names this fact, and a "
        "player's existing save survives it because the save is keyed by the "
        "fact's id and not by its name.");

    factNameSuggest_ = completionsFor(project_.facts, f.name, factSel_);
    // What opens it: typing (handled above) and taking focus. What closes it:
    // Escape (handled above) and losing focus. Anything else would make an
    // Escape last only as long as the key is held.
    if (active && !factNameActive_) factNameSuggestOpen_ = true;
    if (!active && !factNameSuggestHover_) factNameSuggestOpen_ = false;
    factNameActive_ = active;

    const bool show = factNameSuggestOpen_ && !factNameSuggest_.empty() &&
                      (active || factNameSuggestHover_);
    if (!show) {
        factNameSuggestHover_ = false;
        if (!active) factNameSuggestSel_ = -1;
        return;
    }
    if (factNameSuggestSel_ >= (int)factNameSuggest_.size())
        factNameSuggestSel_ = -1;

    // A floating window rather than a popup: a popup takes the keyboard, and
    // the whole arrangement depends on the FIELD keeping it (that is what
    // makes Up/Down/Tab arrive in the callback above). NoFocusOnAppearing
    // keeps it from stealing focus when it opens, and it is kept alive for the
    // frame of a CLICK by factNameSuggestHover_ - the InputText deactivates on
    // mouse-down, so without that the row would vanish before its click
    // registered.
    const float rowH = ImGui::GetTextLineHeightWithSpacing();
    const int rows = (int)factNameSuggest_.size();
    const float h = std::min(rowH * (float)rows, scaled(200)) +
                    ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGui::SetNextWindowPos(ImVec2(fieldMin.x, fieldMax.y + scaled(2)));
    ImGui::SetNextWindowSize(ImVec2(fieldMax.x - fieldMin.x, h));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (ImGui::Begin("##factnamesuggest", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoNavInputs)) {
        for (int i = 0; i < rows; ++i) {
            const std::string& c = factNameSuggest_[(size_t)i];
            // Show the whole candidate but dim the part already typed, so the
            // eye lands on what is being ADDED.
            const std::string label = "##sg" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), i == factNameSuggestSel_)) {
                if (factRenameFrom_.empty()) factRenameFrom_ = f.name;
                f.name = c;
                factNameSuggestSel_ = -1;
                factNameCaretEnd_ = true;
                // SetKeyboardFocusHere is scoped to the CURRENT window, so it
                // cannot reach the field from in here - ask for the focus on
                // the next frame instead, where the field is submitted.
                factNameRefocus_ = true;
            }
            ImGui::SameLine(0.0f, 0.0f);
            const size_t typedLen = std::min(typed.size(), c.size());
            ImGui::TextDisabled("%s", c.substr(0, typedLen).c_str());
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextUnformatted(c.c_str() + typedLen);
            // A keyboard walk has to drag the view with it.
            if (i == factNameSuggestSel_ && !ImGui::IsItemVisible())
                ImGui::SetScrollHereY(0.5f);
        }
        factNameSuggestHover_ =
            ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    }
    ImGui::End();
}

// ---------------------------------------------------------------- catalog ---

void App::drawFactCatalogTab() {
    if (ImGui::Button("+ Fact")) {
        facts::Fact f;
        f.name = uniqueName("world.newFact", [&](const std::string& n) {
            return facts::indexOf(project_.facts, n) >= 0;
        });
        f.id = project::newObjectId();
        project_.facts.push_back(f);
        factSel_ = (int)project_.facts.size() - 1;
    }
    prefHelp(
        "A new fact starts as a yes/no that lives for the session. Give it "
        "a dotted name (world.power.state) and the list groups it for "
        "you.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(200));
    inputText("##filter", factFilter_, nullptr, "Filter...");
    ImGui::SameLine();
    ImGui::TextDisabled("%d fact(s)", (int)project_.facts.size());

    ImGui::Separator();
    ImGui::BeginChild("##factlist", ImVec2(scaled(280), 0), true);
    // The dotted name IS the hierarchy - characters.marta.trust nests under
    // characters > marta - so the tree is built from the names every frame and
    // there is no folder field to keep in sync. A project that never uses dots
    // simply sees one flat list. Filtering builds the tree from the SURVIVING
    // facts only, so a group whose every leaf was filtered out does not appear
    // at all rather than sitting there empty.
    struct Group {
        std::string seg;                 // this level's name segment
        std::vector<int> leaves;         // fact indices that end here
        std::vector<Group> kids;         // first-appearance order, not sorted
        Group* kid(const std::string& s) {
            for (Group& g : kids)
                if (g.seg == s) return &g;
            kids.push_back(Group{s, {}, {}});
            return &kids.back();
        }
    };
    Group root;
    for (size_t i = 0; i < project_.facts.size(); ++i) {
        if (!matchesFilter(project_.facts[i], factFilter_)) continue;
        const std::string& name = project_.facts[i].name;
        Group* at = &root;
        size_t from = 0, dot;
        while ((dot = name.find('.', from)) != std::string::npos) {
            at = at->kid(name.substr(from, dot - from));
            from = dot + 1;
        }
        at->leaves.push_back((int)i);
    }

    // Groups first, then this level's own leaves - the file-manager order, and
    // the one that keeps a fact named `world` from hiding above the `world.*`
    // subtree it looks like it owns.
    std::function<void(Group&, const std::string&)> drawGroup =
        [&](Group& g, const std::string& path) {
            for (Group& k : g.kids) {
                const std::string kpath =
                    path.empty() ? k.seg : path + "." + k.seg;
                // The full path is the ImGui id: two groups named "marta"
                // under different parents would otherwise be one node.
                const std::string label = k.seg + "##g" + kpath;
                if (ImGui::TreeNodeEx(label.c_str(),
                                      ImGuiTreeNodeFlags_DefaultOpen |
                                          ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    drawGroup(k, kpath);
                    ImGui::TreePop();
                }
            }
            for (int i : g.leaves) {
                const facts::Fact& f = project_.facts[(size_t)i];
                const std::string label =
                    factLeaf(f.name) + "##fl" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), factSel_ == i))
                    factSel_ = i;
                if (f.isComputed()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("=");
                }
            }
        };
    drawGroup(root, "");
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##factdetail", ImVec2(0, 0), false);
    if (factSel_ < 0 || factSel_ >= (int)project_.facts.size()) {
        ImGui::TextDisabled("Select a fact.");
        ImGui::EndChild();
        return;
    }
    facts::Fact& f = project_.facts[(size_t)factSel_];

    drawFactNameField(f);

    ImGui::SetNextItemWidth(scaled(280));
    {
        const auto& types = facts::typeInfos();
        if (ImGui::BeginCombo("Type", facts::typeLabel(f.type))) {
            for (size_t i = 0; i < types.size(); ++i) {
                const std::string label =
                    std::string(types[i].label) + "##ty" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), types[i].type == f.type))
                    f.type = types[i].type;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", types[i].desc);
            }
            ImGui::EndCombo();
        }
    }

    if (f.type == facts::Type::Enum) {
        ImGui::Indent(scaled(12));
        ImGui::TextDisabled("Options - the value IS the position in this list");
        int removeOpt = -1;
        for (size_t i = 0; i < f.options.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x")) removeOpt = (int)i;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(200));
            inputText("##opt", f.options[i], nullptr);
            ImGui::SameLine();
            ImGui::TextDisabled("%d", (int)i);
            ImGui::PopID();
        }
        if (removeOpt >= 0) f.options.erase(f.options.begin() + removeOpt);
        if (ImGui::SmallButton("+ option"))
            f.options.push_back("option " +
                                std::to_string(f.options.size() + 1));
        ImGui::Unindent(scaled(12));
    }

    ImGui::SetNextItemWidth(scaled(280));
    {
        const auto& tiers = facts::persistInfos();
        if (ImGui::BeginCombo("Keeps its value", facts::persistLabel(f.persist))) {
            for (size_t i = 0; i < tiers.size(); ++i) {
                const std::string label =
                    std::string(tiers[i].label) + "##pe" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), tiers[i].persist == f.persist))
                    f.persist = tiers[i].persist;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tiers[i].desc);
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SetNextItemWidth(scaled(280));
    {
        const auto& scopes = facts::scopeInfos();
        const char* cur = f.scope == facts::Scope::World ? "World" : "Scene";
        if (ImGui::BeginCombo("Owned by", cur)) {
            for (size_t i = 0; i < scopes.size(); ++i) {
                const std::string label =
                    std::string(scopes[i].label) + "##sc" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), scopes[i].scope == f.scope))
                    f.scope = scopes[i].scope;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", scopes[i].desc);
            }
            ImGui::EndCombo();
        }
    }

    // Computed: the fact stops being storage and becomes a query. Presented as
    // a checkbox because that is the decision - "is this something the game
    // remembers, or something it works out".
    bool computed = f.isComputed();
    if (ImGui::Checkbox("Computed from a query", &computed)) {
        if (!computed) f.computed.clear();
        else if (!project_.factQueries.empty())
            f.computed = project_.factQueries[0].name;
    }
    prefHelp(
        "A computed fact has no storage at all - it IS the query, worked "
        "out wherever it is read. Nothing can write to one, which is the "
        "point: marta.isAlly should never be settable behind the back of "
        "the things that decide it.");
    if (computed) {
        ImGui::SetNextItemWidth(scaled(280));
        factQueryCombo("From query", f.computed);
    } else if (f.type == facts::Type::Position) {
        ImGui::SetNextItemWidth(scaled(280));
        ImGui::DragFloat3("Starts at", f.pos, 0.1f);
    } else {
        ImGui::SetNextItemWidth(scaled(280));
        float v3[3] = {f.value, 0, 0};
        if (factValueWidget("Starts at", f, v3)) f.value = v3[0];
        prefHelp(
            "What a NEW GAME starts this fact at, and what Clear Fact puts "
            "it back to. An existing save keeps whatever it stored.");
    }

    ImGui::SetNextItemWidth(scaled(280));
    inputTextProse("What it means", f.desc, nullptr);
    prefHelp(
        "Shown when picking this fact anywhere in the editor. Worth "
        "writing: a catalog is read far more often than it is edited.");

    // Where the value came from and where it goes - the live half of the
    // catalog, so a fact can be read without switching tabs.
    ImGui::Separator();
    {
        float v3[3] = {0, 0, 0};
        factLiveValue(f.name, v3);
        const bool live = dbgState_ == DbgState::Running ||
                          dbgState_ == DbgState::Halted;
        ImGui::Text("Value now: %s", facts::formatValue(f, v3).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled(live ? "(from the running game)"
                                 : "(no game attached - the default)");
    }

    // Find Usages. One flat walk over the model, the rebuildAssetUsage shape -
    // and the answer to "what breaks if I change this", which for a catalog
    // entry is the question that matters most.
    const project::FactUsage use = project::factUsage(project_, f.name);
    const std::string header =
        "Used by " + std::to_string(use.count()) + " place(s)";
    if (ImGui::CollapsingHeader(header.c_str())) {
        auto list = [](const char* what, const std::vector<std::string>& v) {
            if (v.empty()) return;
            ImGui::TextDisabled("%s", what);
            for (const std::string& s : v) ImGui::BulletText("%s", s.c_str());
        };
        list("Flow graphs", use.graphs);
        list("Queries", use.queries);
        list("Rules", use.rules);
        list("Computed facts", use.computed);
        list("Scenarios", use.scenarios);
        if (!use.any()) ImGui::TextDisabled("Nothing references this fact yet.");
    }

    ImGui::Separator();
    if (ImGui::Button("Delete fact")) ImGui::OpenPopup("##delfact");
    // OpenPopup / BeginPopupModal must use the SAME string - the droneui.cpp
    // trap: a mismatch opens a modal that is never drawn, and an open-but-
    // undrawn modal swallows every click in the window.
    if (ImGui::BeginPopupModal("##delfact", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"?", f.name.c_str());
        if (use.any())
            ImGui::TextColored(ImVec4(0.90f, 0.78f, 0.35f, 1.0f),
                               "%d place(s) still reference it - they will "
                               "report an unknown fact.",
                               use.count());
        if (ImGui::Button("Delete")) {
            project_.facts.erase(project_.facts.begin() + factSel_);
            factSel_ = -1;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::EndChild();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------- queries ---

void App::drawFactQueriesTab() {
    if (ImGui::Button("+ Query")) {
        facts::Query q;
        q.name = uniqueName("CanDoThing", [&](const std::string& n) {
            return facts::queryIndexOf(project_.factQueries, n) >= 0;
        });
        project_.factQueries.push_back(q);
        factQuerySel_ = (int)project_.factQueries.size() - 1;
    }
    prefHelp(
        "A named condition over facts - CanEnterBasement, MartaWillTalk. "
        "The same query gates a door, a dialogue line and an NPC's "
        "behaviour, so the design changes in one place instead of in every "
        "graph that copied it.");
    ImGui::SameLine();
    ImGui::TextDisabled("%d quer(ies)", (int)project_.factQueries.size());
    ImGui::Separator();

    ImGui::BeginChild("##qlist", ImVec2(scaled(220), 0), true);
    for (size_t i = 0; i < project_.factQueries.size(); ++i) {
        const std::string label =
            project_.factQueries[i].name + "##ql" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), factQuerySel_ == (int)i))
            factQuerySel_ = (int)i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##qdetail", ImVec2(0, 0), false);

    if (factQuerySel_ < 0 || factQuerySel_ >= (int)project_.factQueries.size()) {
        ImGui::TextDisabled("Select a query.");
        ImGui::EndChild();
        return;
    }
    facts::Query& q = project_.factQueries[(size_t)factQuerySel_];

    ImGui::SetNextItemWidth(scaled(280));
    std::string typed = q.name;
    inputText("Name", typed, nullptr);
    if (typed != q.name) {
        if (factQueryRenameFrom_.empty()) factQueryRenameFrom_ = q.name;
        q.name = typed;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !factQueryRenameFrom_.empty() &&
        factQueryRenameFrom_ != q.name) {
        const std::string to = q.name;
        q.name = factQueryRenameFrom_;
        project::renameFactQueryRefs(project_, factQueryRenameFrom_, to);
        q.name = to;
        factQueryRenameFrom_.clear();
    }
    ImGui::SetNextItemWidth(scaled(280));
    inputTextProse("What it means", q.desc, nullptr);

    ImGui::SeparatorText("Condition");
    drawFactCondition(q.root, 0, "qroot");

    // "Why?" - the diagnostic the whole tree shape exists for. The host
    // evaluates the SAME structure codegen compiles, against the same values
    // the blackboard shows, and marks the child that decided each group.
    ImGui::SeparatorText("Why?");
    facts::Explain ex;
    const bool answer = facts::evaluate(
        q.root, project_.facts, project_.factQueries,
        [this](const std::string& n, float* o) { return factLiveValue(n, o); },
        &ex);
    ImGui::TextColored(answer ? ImVec4(0.55f, 0.85f, 0.55f, 1.0f)
                              : ImVec4(0.85f, 0.55f, 0.55f, 1.0f),
                       "%s", answer ? "true right now" : "false right now");
    ImGui::TextUnformatted(facts::explainText(ex).c_str());

    const project::FactUsage use = project::queryUsage(project_, q.name);
    const std::string header =
        "Used by " + std::to_string(use.count()) + " place(s)";
    if (ImGui::CollapsingHeader(header.c_str())) {
        auto list = [](const char* what, const std::vector<std::string>& v) {
            if (v.empty()) return;
            ImGui::TextDisabled("%s", what);
            for (const std::string& s : v) ImGui::BulletText("%s", s.c_str());
        };
        list("Flow graphs", use.graphs);
        list("Queries", use.queries);
        list("Rules", use.rules);
        list("Computed facts", use.computed);
        if (!use.any()) ImGui::TextDisabled("Nothing references this query yet.");
    }

    if (ImGui::Button("Delete query")) {
        project_.factQueries.erase(project_.factQueries.begin() + factQuerySel_);
        factQuerySel_ = -1;
    }
    ImGui::EndChild();
}

// ------------------------------------------------------------------ rules ---

void App::drawFactRulesTab() {
    if (ImGui::Button("+ Rule")) {
        facts::Rule r;
        r.name = uniqueName("WhenSomething", [&](const std::string& n) {
            for (const facts::Rule& e : project_.factRules)
                if (e.name == n) return true;
            return false;
        });
        project_.factRules.push_back(r);
        factRuleSel_ = (int)project_.factRules.size() - 1;
    }
    prefHelp(
        "A reaction: when this condition holds, change these facts or send "
        "this event. Rules run once per frame before every graph, so a "
        "fact a rule writes is already there by the time a graph looks.");
    ImGui::SameLine();
    ImGui::TextDisabled("%d rule(s)", (int)project_.factRules.size());
    ImGui::Separator();

    ImGui::BeginChild("##rlist", ImVec2(scaled(220), 0), true);
    for (size_t i = 0; i < project_.factRules.size(); ++i) {
        const facts::Rule& r = project_.factRules[i];
        const std::string label = r.name + "##rl" + std::to_string(i);
        if (!r.enabled) ImGui::BeginDisabled();
        if (ImGui::Selectable(label.c_str(), factRuleSel_ == (int)i))
            factRuleSel_ = (int)i;
        if (!r.enabled) ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##rdetail", ImVec2(0, 0), false);

    if (factRuleSel_ < 0 || factRuleSel_ >= (int)project_.factRules.size()) {
        ImGui::TextDisabled("Select a rule.");
        ImGui::EndChild();
        return;
    }
    facts::Rule& r = project_.factRules[(size_t)factRuleSel_];

    ImGui::SetNextItemWidth(scaled(280));
    inputText("Name", r.name, nullptr);
    ImGui::SameLine();
    ImGui::Checkbox("Enabled", &r.enabled);
    ImGui::SetNextItemWidth(scaled(280));
    inputTextProse("What it does", r.desc, nullptr);

    ImGui::SetNextItemWidth(scaled(280));
    {
        const auto& pol = facts::policyInfos();
        const char* cur = "When it becomes true";
        for (const facts::PolicyInfo& i : pol)
            if (i.policy == r.policy) cur = i.label;
        if (ImGui::BeginCombo("Runs", cur)) {
            for (size_t i = 0; i < pol.size(); ++i) {
                const std::string label =
                    std::string(pol[i].label) + "##po" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), pol[i].policy == r.policy))
                    r.policy = pol[i].policy;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", pol[i].desc);
            }
            ImGui::EndCombo();
        }
    }

    ImGui::SeparatorText("When");
    drawFactCondition(r.when, 0, "rwhen");

    ImGui::SeparatorText("Then");
    int removeAct = -1;
    for (size_t i = 0; i < r.then.size(); ++i) {
        facts::RuleAction& a = r.then[i];
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("x")) removeAct = (int)i;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(120));
        {
            const auto& kinds = facts::actionKindInfos();
            const char* cur = "Set fact";
            for (const facts::ActionKindInfo& k : kinds)
                if (k.kind == a.kind) cur = k.label;
            if (ImGui::BeginCombo("##akind", cur)) {
                for (size_t k = 0; k < kinds.size(); ++k) {
                    const std::string label =
                        std::string(kinds[k].label) + "##ak" + std::to_string(k);
                    if (ImGui::Selectable(label.c_str(), kinds[k].kind == a.kind))
                        a.kind = kinds[k].kind;
                }
                ImGui::EndCombo();
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(190));
        if (a.kind == facts::RuleAction::Kind::SendEvent) {
            inputText("##atarget", a.target, nullptr, "event name");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "An On Event node with this name receives it on the next "
                    "frame, like any other graph event. The door from a rule "
                    "into something with steps and timing.");
        } else {
            factCombo("##atarget", a.target, false);
        }
        if (a.kind != facts::RuleAction::Kind::ToggleFact) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(140));
            const int fi = facts::indexOf(project_.facts, a.target);
            if (fi >= 0 && a.kind == facts::RuleAction::Kind::SetFact) {
                float v3[3] = {a.value, 0, 0};
                if (factValueWidget("##aval", project_.facts[(size_t)fi], v3))
                    a.value = v3[0];
            } else {
                ImGui::DragFloat("##aval", &a.value, 0.1f);
            }
        }
        ImGui::PopID();
    }
    if (removeAct >= 0) r.then.erase(r.then.begin() + removeAct);
    if (ImGui::SmallButton("+ action")) r.then.push_back(facts::RuleAction{});

    ImGui::SeparatorText("Why?");
    facts::Explain ex;
    const bool answer = facts::evaluate(
        r.when, project_.facts, project_.factQueries,
        [this](const std::string& n, float* o) { return factLiveValue(n, o); },
        &ex);
    ImGui::TextColored(answer ? ImVec4(0.55f, 0.85f, 0.55f, 1.0f)
                              : ImVec4(0.85f, 0.55f, 0.55f, 1.0f),
                       "%s", answer ? "true right now" : "false right now");
    ImGui::TextUnformatted(facts::explainText(ex).c_str());

    if (ImGui::Button("Delete rule")) {
        project_.factRules.erase(project_.factRules.begin() + factRuleSel_);
        factRuleSel_ = -1;
    }
    ImGui::EndChild();
}

// -------------------------------------------------------------- scenarios ---

void App::drawFactScenariosTab() {
    ImGui::TextWrapped(
        "A scenario is a saved set of fact values - \"generator repaired, "
        "Marta saved, no key\". Push one into the running game to look at a "
        "situation twenty minutes deep without playing to it.");
    ImGui::Separator();

    if (ImGui::Button("+ Scenario")) {
        facts::Scenario s;
        s.name = uniqueName("Situation", [&](const std::string& n) {
            for (const facts::Scenario& e : project_.factScenarios)
                if (e.name == n) return true;
            return false;
        });
        project_.factScenarios.push_back(s);
        factScenarioSel_ = (int)project_.factScenarios.size() - 1;
    }
    ImGui::SameLine();
    // Capturing beats typing: the fastest way to a scenario is to play into
    // the situation once and press this.
    const bool live =
        dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    ImGui::BeginDisabled(!live);
    if (ImGui::Button("Capture from the running game")) {
        facts::Scenario s;
        s.name = uniqueName("Captured", [&](const std::string& n) {
            for (const facts::Scenario& e : project_.factScenarios)
                if (e.name == n) return true;
            return false;
        });
        s.desc = "Captured from a running game";
        for (const facts::Fact& f : project_.facts) {
            if (f.isComputed()) continue;  // derived - nothing to store
            facts::ScenarioValue v;
            v.fact = f.name;
            float v3[3] = {0, 0, 0};
            factLiveValue(f.name, v3);
            v.value = v3[0];
            for (int a = 0; a < 3; ++a) v.pos[a] = v3[a];
            s.values.push_back(v);
        }
        project_.factScenarios.push_back(s);
        factScenarioSel_ = (int)project_.factScenarios.size() - 1;
    }
    ImGui::EndDisabled();
    if (!live && ImGui::IsItemHovered())
        ImGui::SetTooltip("Needs a running debug build with the Live Debugger on.");

    ImGui::Separator();
    ImGui::BeginChild("##slist", ImVec2(scaled(220), 0), true);
    for (size_t i = 0; i < project_.factScenarios.size(); ++i) {
        const std::string label =
            project_.factScenarios[i].name + "##sl" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), factScenarioSel_ == (int)i))
            factScenarioSel_ = (int)i;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##sdetail", ImVec2(0, 0), false);

    if (factScenarioSel_ < 0 ||
        factScenarioSel_ >= (int)project_.factScenarios.size()) {
        ImGui::TextDisabled("Select a scenario.");
        ImGui::EndChild();
        return;
    }
    facts::Scenario& s = project_.factScenarios[(size_t)factScenarioSel_];

    ImGui::SetNextItemWidth(scaled(280));
    inputText("Name", s.name, nullptr);
    ImGui::SetNextItemWidth(scaled(280));
    inputTextProse("What it sets up", s.desc, nullptr);

    ImGui::BeginDisabled(!live);
    if (ImGui::Button("Apply to the running game")) {
        for (const facts::ScenarioValue& v : s.values) {
            const int fi = facts::indexOf(project_.facts, v.fact);
            if (fi < 0 || project_.facts[(size_t)fi].isComputed()) continue;
            factOverrides_[v.fact] = {v.value, v.pos[1], v.pos[2]};
            if (project_.facts[(size_t)fi].type == facts::Type::Position)
                factOverrides_[v.fact] = {v.pos[0], v.pos[1], v.pos[2]};
        }
        factPushOverrides();
    }
    ImGui::EndDisabled();
    prefHelp(
        "Applied as blackboard overrides, which the game re-asserts every "
        "frame until they are cleared - so a rule that fights the value "
        "cannot quietly undo the scenario before you have seen it. Clear "
        "them from the Blackboard tab.");

    ImGui::SeparatorText("Values");
    int removeRow = -1;
    for (size_t i = 0; i < s.values.size(); ++i) {
        facts::ScenarioValue& v = s.values[i];
        ImGui::PushID((int)i);
        if (ImGui::SmallButton("x")) removeRow = (int)i;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(220));
        factCombo("##sfact", v.fact, true);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(180));
        const int fi = facts::indexOf(project_.facts, v.fact);
        if (fi >= 0) {
            const facts::Fact& f = project_.facts[(size_t)fi];
            if (f.type == facts::Type::Position) {
                ImGui::DragFloat3("##sval", v.pos, 0.1f);
            } else {
                float v3[3] = {v.value, 0, 0};
                if (factValueWidget("##sval", f, v3)) v.value = v3[0];
            }
        } else {
            ImGui::TextDisabled("unknown fact");
        }
        ImGui::PopID();
    }
    if (removeRow >= 0) s.values.erase(s.values.begin() + removeRow);
    if (ImGui::SmallButton("+ value")) s.values.push_back(facts::ScenarioValue{});

    if (ImGui::Button("Delete scenario")) {
        project_.factScenarios.erase(project_.factScenarios.begin() +
                                     factScenarioSel_);
        factScenarioSel_ = -1;
    }
    ImGui::EndChild();
}

// ------------------------------------------------------------- blackboard ---

void App::drawFactBlackboardTab() {
    const bool live =
        dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    if (!live) {
        ImGui::TextWrapped(
            "No game reporting. The World Blackboard reads the running game "
            "through the Live Debugger channel - build with the debug profile "
            "and the Live Debugger preference on, then run. The values below "
            "are the catalog's own defaults until then.");
        ImGui::Separator();
    }

    if (!factOverrides_.empty()) {
        ImGui::TextColored(ImVec4(0.90f, 0.78f, 0.35f, 1.0f),
                           "%d fact(s) held at a value by hand",
                           (int)factOverrides_.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear all overrides")) {
            factOverrides_.clear();
            factPushOverrides();
        }
    }

    if (ImGui::BeginTable("##bb", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Fact");
        ImGui::TableSetupColumn("Value");
        ImGui::TableSetupColumn("Keeps");
        ImGui::TableSetupColumn("Override");
        ImGui::TableSetupColumn("Last changed");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        const facts::Layout lay = facts::layoutOf(project_.facts);
        for (size_t i = 0; i < project_.facts.size(); ++i) {
            const facts::Fact& f = project_.facts[i];
            ImGui::TableNextRow();
            ImGui::PushID((int)i);

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(f.name.c_str());
            if (ImGui::IsItemHovered() && !f.desc.empty())
                ImGui::SetTooltip("%s", f.desc.c_str());

            ImGui::TableNextColumn();
            float v3[3] = {0, 0, 0};
            factLiveValue(f.name, v3);
            ImGui::TextUnformatted(facts::formatValue(f, v3).c_str());

            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", f.isComputed()
                                          ? "computed"
                                          : facts::persistLabel(f.persist));

            ImGui::TableNextColumn();
            if (f.isComputed()) {
                ImGui::TextDisabled("-");
            } else {
                auto it = factOverrides_.find(f.name);
                bool held = it != factOverrides_.end();
                if (ImGui::Checkbox("##hold", &held)) {
                    if (held) {
                        factOverrides_[f.name] = {v3[0], v3[1], v3[2]};
                    } else {
                        factOverrides_.erase(f.name);
                    }
                    factPushOverrides();
                }
                if (held) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(140));
                    std::array<float, 3>& ov = factOverrides_[f.name];
                    float tmp[3] = {ov[0], ov[1], ov[2]};
                    if (factValueWidget("##ovval", f, tmp)) {
                        ov = {tmp[0], tmp[1], tmp[2]};
                        factPushOverrides();
                    }
                }
            }

            ImGui::TableNextColumn();
            // The change history. The game rings every fact write with WHO did
            // it, and the sym file turns that number back into a place in the
            // editor - so this column is the answer to "which graph set this,
            // and when", which is the question a blackboard exists for.
            int slot = -1;
            if (lay.posOf[i] >= 0)
                slot = lay.numCount + lay.posOf[i];
            else if (lay.numOf[i] >= 0)
                slot = lay.numOf[i];
            const livedbg::FactEvent* last = nullptr;
            if (slot >= 0)
                for (const livedbg::FactEvent& e : dbgSnap_.factEvents)
                    if (e.slot == slot) last = &e;
            if (!last) {
                ImGui::TextDisabled("-");
            } else {
                std::string who = "?";
                if (last->src >= 0) {
                    if (const livedbg::NodeSym* n = dbgSyms_.find(last->src)) {
                        who = n->type;
                        for (const SceneData& sc : project_.scenes)
                            for (const SceneObject& o : sc.objects)
                                if (o.id == n->objectId)
                                    who = std::string(n->type) + " in " +
                                          sc.name + " / " + o.name;
                    }
                } else if (last->src < -1) {
                    // -(rule + 2) - see the emitter in templates.cpp; -1 is
                    // "unattributed", so rules start at -2.
                    const int ri = -last->src - 2;
                    if (ri >= 0 && ri < (int)project_.factRules.size())
                        who = "rule " + project_.factRules[(size_t)ri].name;
                    else
                        who = "a rule";
                }
                const uint32_t ago =
                    dbgSnap_.frame >= last->frame ? dbgSnap_.frame - last->frame
                                                  : 0;
                ImGui::Text("%s, %u frame(s) ago", who.c_str(), ago);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
