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
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "facts.hpp"
#include "theme.hpp"
#include "toolkit.hpp"
#include "uiscript.hpp"

#include <imgui.h>
#include <imgui_internal.h>

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
// A segmented control when its options fit the row, else a combo with the
// same ID - a docked, narrow window must not squeeze five words into one pill.
// The combo's own right-hand label is clipped (the row's caption says it).
bool choice(const char* id, const char* const* labels, int n, int* cur, float maxW) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float natural = 6.0f;
    for (int i = 0; i < n; ++i) natural += ImGui::CalcTextSize(labels[i]).x + st.FramePadding.x * 2.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float w = maxW > 0.0f ? std::min(maxW, avail) : avail;
    if (natural <= w) return ui::Segmented(id, labels, n, cur, w);
    bool changed = false;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(p, ImVec2(p.x + w, p.y + ImGui::GetFrameHeight()), true);
    ImGui::SetNextItemWidth(w);
    if (ImGui::BeginCombo(id, labels[*cur])) {
        for (int i = 0; i < n; ++i)
            if (ImGui::Selectable(labels[i], i == *cur) && i != *cur) {
                *cur = i;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ImGui::PopClipRect();
    return changed;
}

void wrappedTip(const char* text) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

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

// One row per node of the tree: the kind first (what the row IS), then a
// comparison's three parts or a query's name; a group's children hang under
// it on a guide line, each with its own remove button. Every width is a share
// of the row, so a docked, narrow window still shows each part.
bool App::drawFactCondition(facts::Condition& c, int depth, const char* id) {
    bool changed = false;
    ImGui::PushID(id);
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float rowX = ImGui::GetCursorPosX();

    const bool group = c.kind == facts::Condition::Kind::All ||
                       c.kind == facts::Condition::Kind::Any ||
                       c.kind == facts::Condition::Kind::Not;

    // The kind combo first: switching it must not silently keep a leaf's fact
    // hanging around in a group (or the other way).
    ImGui::SetNextItemWidth(fs * 6.2f);
    const char* kinds[] = {"ALL of", "ANY of", "NONE of", "compare", "query"};
    int kindIdx = (int)c.kind;
    {
        // A group's kind reads as a keyword: the accent, in the heading cut.
        ui::FontScope bold(group ? ui::boldFont() : nullptr);
        if (group) ImGui::PushStyleColor(ImGuiCol_Text, sem.accent);
        if (ImGui::Combo("##kind", &kindIdx, kinds, 5)) {
            c.kind = (facts::Condition::Kind)kindIdx;
            changed = true;
        }
        if (group) ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
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
        const float vsW = ui::ChipButtonWidth("vs fact");
        const float factX = ImGui::GetCursorPosX();
        const float avail = ImGui::GetContentRegionAvail().x;
        // Narrow (a docked window): the fact gets the rest of the line and
        // the comparison goes under the row - a fact name cut to "charact"
        // says nothing.
        const bool twoLines = avail - vsW - gap * 3.0f < fs * 26.0f;
        const float rest = std::max(fs * 10.0f, (twoLines ? avail : avail - vsW - gap * 3.0f));
        const float factW = twoLines ? avail : rest * 0.42f;
        const float line2 = avail + (factX - rowX);  // the second line starts under the kind
        const float tail = twoLines ? std::max(fs * 4.0f, line2 - vsW - gap * 2.0f) : rest - factW;
        const float opW = tail * (twoLines ? 0.45f : 0.38f), valW = tail - opW;
        ImGui::SetNextItemWidth(factW);
        if (factCombo("##fact", c.fact, false)) changed = true;

        if (twoLines) ImGui::SetCursorPosX(rowX);
        else ImGui::SameLine();
        ImGui::SetNextItemWidth(opW);
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
        ImGui::SetNextItemWidth(valW);
        // Against a constant, or against another fact. One toggle rather than
        // two comparison kinds: "marta.trust at least quest.threshold" is the
        // same sentence with a different right-hand side.
        bool vsFact = !c.rhsFact.empty();
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
        ImGui::SameLine();
        if (ui::ChipButton("vs fact", vsFact)) {
            vsFact = !vsFact;
            if (!vsFact) c.rhsFact.clear();
            else if (c.rhsFact.empty() && !project_.facts.empty())
                c.rhsFact = project_.facts[0].name;
            changed = true;
        }
        uiscript::markLastItemChecked(!c.rhsFact.empty());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Compare against another fact instead of a value.");
    } else if (c.kind == facts::Condition::Kind::Query) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(fs * 8.0f, ImGui::GetContentRegionAvail().x));
        if (factQueryCombo("##query", c.query)) changed = true;
    }

    if (group) {
        ImGui::SameLine();
        // A depth cap that is about the UI, not the language: past this the
        // rows have no width left to be readable in, and a condition that
        // deep wants to be a named query anyway.
        const bool canNest = depth < 5;
        if (ui::GhostButton("+ compare")) {
            facts::Condition ch;
            ch.kind = facts::Condition::Kind::Compare;
            if (!project_.facts.empty()) ch.fact = project_.facts[0].name;
            c.children.push_back(ch);
            changed = true;
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::GhostButton("+ query")) {
            facts::Condition ch;
            ch.kind = facts::Condition::Kind::Query;
            c.children.push_back(ch);
            changed = true;
        }
        if (canNest) {
            ImGui::SameLine(0.0f, 2.0f);
            if (ui::GhostButton("+ group")) {
                facts::Condition ch;
                ch.kind = facts::Condition::Kind::All;
                c.children.push_back(ch);
                changed = true;
            }
        }

        const float indent = fs * 1.3f;
        const float guideX = ImGui::GetCursorScreenPos().x + fs * 0.55f;
        const float guideTop = ImGui::GetCursorScreenPos().y;
        float guideBottom = guideTop;
        ImGui::Indent(indent);
        int remove = -1;
        if (c.children.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(c.kind == facts::Condition::Kind::Any
                                       ? "empty - always false"
                                       : "empty - always true");
            ImGui::PopStyleColor();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (size_t i = 0; i < c.children.size(); ++i) {
            ImGui::PushID((int)i);
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float midY = rp.y + ImGui::GetFrameHeight() * 0.5f;
            dl->AddLine(ImVec2(guideX, midY), ImVec2(rp.x - fs * 0.15f, midY),
                        ImGui::GetColorU32(sem.border), 1.0f);
            guideBottom = midY;
            if (ui::IconButton("Remove", ui::RtIcon::Close, 0.0f, "Remove this row")) remove = (int)i;
            ImGui::SameLine();
            const std::string cid = "c" + std::to_string(i);
            if (drawFactCondition(c.children[i], depth + 1, cid.c_str()))
                changed = true;
            ImGui::PopID();
        }
        if (guideBottom > guideTop)
            dl->AddLine(ImVec2(guideX, guideTop), ImVec2(guideX, guideBottom),
                        ImGui::GetColorU32(sem.border), 1.0f);
        if (remove >= 0) {
            c.children.erase(c.children.begin() + remove);
            changed = true;
        }
        ImGui::Unindent(indent);
    }

    ImGui::PopID();
    return changed;
}

// ------------------------------------------------------------ the window ---

void App::drawWorldFactsWindow() {
    if (!showWorldFacts_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(1200), scaled(800)), ImGuiCond_FirstUseEver);
    // Tools > World Facts asks for focus over a few frames: closing the menu
    // popup hands focus back to the window under it a frame later, which
    // would otherwise leave this one a background tab.
    if (factFocusFrames_ > 0) {
        --factFocusFrames_;
        ImGui::SetNextWindowFocus();
    }
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
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();

    // --- the title row: tabs with counts, the live state once, the notes ---
    const ImVec2 barPos = ImGui::GetCursorScreenPos();
    if (ui::BeginQuietTabBar("##factstabs")) {
        char lbl[5][48];
        std::snprintf(lbl[0], sizeof(lbl[0]), "Catalog %d###Catalog", (int)project_.facts.size());
        std::snprintf(lbl[1], sizeof(lbl[1]), "Queries %d###Queries", (int)project_.factQueries.size());
        std::snprintf(lbl[2], sizeof(lbl[2]), "Rules %d###Rules", (int)project_.factRules.size());
        std::snprintf(lbl[3], sizeof(lbl[3]), "Scenarios %d###Scenarios",
                      (int)project_.factScenarios.size());
        std::snprintf(lbl[4], sizeof(lbl[4]), "Blackboard###Blackboard");
        for (int i = 0; i < 5; ++i) {
            const ImGuiTabItemFlags fl = factTabJump_ == i ? ImGuiTabItemFlags_SetSelected : 0;
            if (ui::QuietTab(lbl[i], factTab_ == i, fl)) factTab_ = i;
        }
        factTabJump_ = -1;
        ui::EndQuietTabBar();
    }
    const ImVec2 afterBar = ImGui::GetCursorScreenPos();

    // Validation, always one click away: a catalog is a contract several
    // systems depend on, and the moment to hear that a rule writes a computed
    // fact is while you are looking at the rule.
    const std::vector<facts::Issue> issues =
        facts::validate(project_.facts, project_.factQueries, project_.factRules,
                        project_.factScenarios);
    int errors = 0;
    for (const facts::Issue& i : issues) errors += i.error ? 1 : 0;
    {
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        float x = right;
        const float y = barPos.y + (afterBar.y - barPos.y - ImGui::GetFrameHeight()) * 0.5f;
        if (!issues.empty()) {
            char chip[48];
            if (errors)
                std::snprintf(chip, sizeof(chip), "%d problem%s", errors, errors == 1 ? "" : "s");
            else
                std::snprintf(chip, sizeof(chip), "%d note%s", (int)issues.size(),
                              issues.size() == 1 ? "" : "s");
            const float cw = ImGui::CalcTextSize(chip).x * 0.82f + fs;
            x -= cw;
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ui::Badge(chip, errors ? 2 : 1);
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            if (ImGui::InvisibleButton("Problems and notes", ImVec2(cw, ImGui::GetFrameHeight())))
                ImGui::OpenPopup("##factissues");
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            x -= fs;
        }
        const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
        const char* state = live ? "Live - showing the running game's values"
                                 : "No game attached \xc2\xb7 showing start values";
        const float sw = ImGui::CalcTextSize(state).x + fs * 0.9f;
        x -= sw;
        if (x > barPos.x + fs * 28.0f) {  // only when the tabs leave room
            ImGui::SetCursorScreenPos(ImVec2(x, y + ImGui::GetStyle().FramePadding.y));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (live)
                dl->AddCircleFilled(ImVec2(p.x + fs * 0.3f, p.y + fs * 0.55f), fs * 0.22f,
                                    ImGui::GetColorU32(sem.ok));
            else
                dl->AddCircle(ImVec2(p.x + fs * 0.3f, p.y + fs * 0.55f), fs * 0.22f,
                              ImGui::GetColorU32(sem.textDim), 0, 1.2f);
            ImGui::SetCursorScreenPos(ImVec2(p.x + fs * 0.9f, p.y));
            ImGui::PushStyleColor(ImGuiCol_Text, live ? sem.ok : sem.textDim);
            ImGui::TextUnformatted(state);
            ImGui::PopStyleColor();
        }
        if (ImGui::BeginPopup("##factissues")) {
            for (const facts::Issue& i : issues) {
                ImGui::PushStyleColor(ImGuiCol_Text, i.error ? sem.danger : sem.warn);
                ImGui::TextUnformatted(i.error ? "error" : "note");
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fs * 30.0f);
                ImGui::TextWrapped("%s - %s", i.where.c_str(), i.text.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(afterBar);
    }
    ImGui::Spacing();

    switch (factTab_) {
        case 0: drawFactCatalogTab(); break;
        case 1: drawFactQueriesTab(); break;
        case 2: drawFactRulesTab(); break;
        case 3: drawFactScenariosTab(); break;
        default: drawFactBlackboardTab(); break;
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
    // Full width, big and monospace: the name is the fact's identity. The
    // field's own "Name" label falls outside the card and is clipped; it stays
    // the ID scripts type into.
    const ImVec2 nameP = ImGui::GetCursorScreenPos();
    const float nameW = ImGui::GetContentRegionAvail().x;
    ImGui::PushClipRect(nameP, ImVec2(nameP.x + nameW, nameP.y + ImGui::GetFontSize() * 3.0f), true);
    ImGui::SetNextItemWidth(nameW);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", f.name.c_str());
    ImGui::PushFont(ui::monoFont(), ImGui::GetFontSize() * 1.25f);
    NameEdit edit{&project_.facts, factSel_, &factNameSuggest_,
                  &factNameSuggestSel_, &factNameCaretEnd_};
    ImGui::InputText("Name", buf, sizeof(buf),
                     ImGuiInputTextFlags_CallbackCompletion |
                         ImGuiInputTextFlags_CallbackHistory |
                         ImGuiInputTextFlags_CallbackAlways,
                     factNameCallback, &edit);
    ImGui::PopFont();
    ImGui::PopClipRect();
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
    if (ImGui::IsItemHovered() && !active)
        wrappedTip(
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
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    const float paneH = ImGui::GetContentRegionAvail().y;
    const float totalW = ImGui::GetContentRegionAvail().x;
    // Docked beside the Viewport at a 1600 pt window the page is ~700 pt; a
    // 0.46 share with a 420 pt detail minimum left the tree ~280 pt and cut
    // the fact names ("generato", "bootCoun"). Give the list at least 360 pt
    // while the detail keeps half.
    const float minDetail = std::min(scaled(420), totalW * 0.48f);
    if (factListW_ <= 0.0f) factListW_ = std::max(totalW * 0.46f, std::min(scaled(360), totalW * 0.52f));

    // ------------------------------------------------------------ the list
    if (ui::BeginCard("##factlistcard", ImVec2(factListW_, paneH))) {
        const float addW = ImGui::CalcTextSize("+ Fact").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x);
        inputText("##filter", factFilter_, nullptr, "Filter facts - name or meaning");
        ImGui::SameLine();
        if (ui::PrimaryButton("+ Fact", addW)) {
            facts::Fact f;
            f.name = uniqueName("world.newFact", [&](const std::string& n) {
                return facts::indexOf(project_.facts, n) >= 0;
            });
            f.id = project::newObjectId();
            project_.facts.push_back(f);
            factSel_ = (int)project_.facts.size() - 1;
            factKeepFilter_ = -1;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A new fact starts as a yes/no that lives for the session. Give it\n"
                              "a dotted name (world.power.state) and the list groups it.");

        // Persistence chips: "what survives a save?" is the scan a designer does.
        {
            static const char* kChips[6] = {"All", "Save game", "Checkpoint", "Session", "Profile",
                                            "Computed"};
            static const int kVal[6] = {-1, (int)facts::Persist::Save,
                                        (int)facts::Persist::Checkpoint,
                                        (int)facts::Persist::Session,
                                        (int)facts::Persist::Profile, 100};
            for (int i = 0; i < 6; ++i) {
                const float w = ui::FilterChipWidth(kChips[i]);
                ui::ChipFlow(w, i == 0);
                if (ui::FilterChip(kChips[i], factKeepFilter_ == kVal[i])) factKeepFilter_ = kVal[i];
            }
        }
        auto keep = [&](const facts::Fact& f) {
            if (factKeepFilter_ < 0) return true;
            if (factKeepFilter_ == 100) return f.isComputed();
            return !f.isComputed() && (int)f.persist == factKeepFilter_;
        };

        // The dotted name IS the hierarchy - characters.marta.trust nests under
        // characters > marta - so the tree is built from the names every frame
        // and there is no folder field to keep in sync. Filtering builds it
        // from the SURVIVING facts only, so a group whose every leaf was
        // filtered out does not appear at all rather than sitting there empty.
        struct Group {
            std::string seg;          // this level's name segment
            std::vector<int> leaves;  // fact indices that end here
            std::vector<Group> kids;  // first-appearance order, not sorted
            Group* kid(const std::string& s) {
                for (Group& g : kids)
                    if (g.seg == s) return &g;
                kids.push_back(Group{s, {}, {}});
                return &kids.back();
            }
        };
        Group root;
        int shown = 0;
        for (size_t i = 0; i < project_.facts.size(); ++i) {
            if (!matchesFilter(project_.facts[i], factFilter_) || !keep(project_.facts[i])) continue;
            ++shown;
            const std::string& name = project_.facts[i].name;
            Group* at = &root;
            size_t from = 0, dot;
            while ((dot = name.find('.', from)) != std::string::npos) {
                at = at->kid(name.substr(from, dot - from));
                from = dot + 1;
            }
            at->leaves.push_back((int)i);
        }

        if (project_.facts.empty()) {
            if (ui::EmptyState(ui::Glyph::List, "No facts yet",
                               "A fact is named, typed, documented world state that graphs, "
                               "queries and rules read and write.",
                               "Create the first fact")) {
                facts::Fact f;
                f.name = "world.newFact";
                f.id = project::newObjectId();
                project_.facts.push_back(f);
                factSel_ = 0;
            }
        } else if (!shown) {
            ui::EmptyState(ui::Glyph::List, "No fact matches", "Clear the filter or pick All.");
        } else if (ImGui::BeginTable("##factlist", 3,
                                     ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Fact", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Keeps", ImGuiTableColumnFlags_WidthFixed, fs * 5.6f);
            ImGui::TableSetupColumn(live ? "Now" : "Starts", ImGuiTableColumnFlags_WidthFixed,
                                    fs * 4.0f);
            ImGui::TableHeadersRow();

            // Groups first, then this level's own leaves - the file-manager
            // order, and the one that keeps a fact named `world` from hiding
            // above the `world.*` subtree it looks like it owns.
            std::function<void(Group&, const std::string&)> drawGroup =
                [&](Group& g, const std::string& path) {
                    for (Group& k : g.kids) {
                        const std::string kpath = path.empty() ? k.seg : path + "." + k.seg;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        // The full path is the ImGui id: two groups named
                        // "marta" under different parents would otherwise be
                        // one node.
                        const std::string label = k.seg + "##g" + kpath;
                        ui::FontScope bold(ui::boldFont());
                        const bool open = ImGui::TreeNodeEx(
                            label.c_str(), ImGuiTreeNodeFlags_DefaultOpen |
                                               ImGuiTreeNodeFlags_SpanAllColumns);
                        if (open) {
                            drawGroup(k, kpath);
                            ImGui::TreePop();
                        }
                    }
                    for (int i : g.leaves) {
                        const facts::Fact& f = project_.facts[(size_t)i];
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        // The type as a small coloured mark before the name.
                        const char* mark = "?";
                        ImVec4 mc = sem.ok;
                        switch (f.type) {
                            case facts::Type::Bool: mark = "?"; mc = sem.ok; break;
                            case facts::Type::Int: mark = "#"; mc = sem.accent; break;
                            case facts::Type::Float: mark = ".0"; mc = sem.special; break;
                            case facts::Type::Enum: mark = "\xe2\x89\xa1"; mc = sem.special; break;
                            case facts::Type::Position: mark = "xyz"; mc = sem.warn; break;
                        }
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float markW = fs * 1.6f;
                        {
                            ui::FontScope mono(ui::monoFont());
                            ImGui::PushFont(nullptr, fs * 0.8f);
                            const ImVec2 ms = ImGui::CalcTextSize(mark);
                            ImGui::GetWindowDrawList()->AddText(
                                ImVec2(p.x + markW - ms.x - fs * 0.4f, p.y + (fs - ms.y) * 0.5f + 1.0f),
                                ImGui::GetColorU32(mc), mark);
                            ImGui::PopFont();
                        }
                        ImGui::SetCursorScreenPos(ImVec2(p.x + markW, p.y));
                        const std::string label = factLeaf(f.name) + "##fl" + std::to_string(i);
                        if (ImGui::Selectable(label.c_str(), factSel_ == i,
                                              ImGuiSelectableFlags_SpanAllColumns))
                            factSel_ = i;
                        if (ImGui::IsItemHovered() && !f.desc.empty())
                            ImGui::SetTooltip("%s", f.desc.c_str());
                        ImGui::TableNextColumn();
                        if (f.isComputed())
                            ui::Badge("Computed", 0);
                        else
                            ui::Badge(facts::persistLabel(f.persist),
                                      f.persist == facts::Persist::Save         ? 3
                                      : f.persist == facts::Persist::Checkpoint ? 1
                                      : f.persist == facts::Persist::Profile    ? 3
                                                                                : 4);
                        ImGui::TableNextColumn();
                        std::string v;
                        if (f.isComputed() && !live) {
                            v = "=";
                        } else {
                            float v3[3] = {0, 0, 0};
                            if (live) factLiveValue(f.name, v3);
                            else if (f.type == facts::Type::Position)
                                for (int a = 0; a < 3; ++a) v3[a] = f.pos[a];
                            else v3[0] = f.value;
                            v = f.type == facts::Type::Bool ? (v3[0] != 0.0f ? "yes" : "no")
                                                            : facts::formatValue(f, v3);
                        }
                        ui::FontScope mono(ui::monoFont());
                        const float vw = ImGui::CalcTextSize(v.c_str()).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                             std::max(0.0f, ImGui::GetContentRegionAvail().x - vw));
                        ImGui::TextDisabled("%s", v.c_str());
                    }
                };
            drawGroup(root, "");
            ImGui::EndTable();
        }
    }
    ui::EndCard();
    ImGui::SameLine(0.0f, 0.0f);
    ui::VSplitter("##factsplit", &factListW_, scaled(300), minDetail, paneH);

    // ---------------------------------------------------------- the detail
    if (!ui::BeginCard("##factdetail", ImVec2(0.0f, paneH))) {
        ui::EndCard();
        return;
    }
    if (factSel_ < 0 || factSel_ >= (int)project_.facts.size()) {
        ui::EmptyState(ui::Glyph::List, "No fact selected",
                       "Pick a fact on the left to see what it means, how it persists and "
                       "who reads or writes it.");
        ui::EndCard();
        return;
    }
    facts::Fact& f = project_.facts[(size_t)factSel_];

    // Breadcrumb, the name (big, monospace), and what it means - prose first.
    {
        const size_t dot = f.name.rfind('.');
        std::string crumb;
        if (dot != std::string::npos)
            for (char c : f.name.substr(0, dot + 1)) {
                if (c == '.') crumb += " \xc2\xb7 ";
                else crumb += c;
            }
        ui::FontScope mono(ui::monoFont());
        ImGui::PushFont(nullptr, fs * 0.86f);
        ImGui::TextDisabled("%s", crumb.empty() ? "(top level)" : crumb.c_str());
        ImGui::PopFont();
    }
    drawFactNameField(f);
    {
        // Clip the field's right-hand label: the row is the field.
        const ImVec2 dp = ImGui::GetCursorScreenPos();
        const float dw = ImGui::GetContentRegionAvail().x;
        ImGui::PushClipRect(dp, ImVec2(dp.x + dw, dp.y + ImGui::GetFontSize() * 20.0f), true);
        ImGui::SetNextItemWidth(dw);
        inputTextProse("What it means", f.desc, nullptr);
        ImGui::PopClipRect();
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && f.desc.empty())
        ImGui::SetTooltip("Shown wherever this fact is picked. Worth writing: a catalog is\n"
                          "read far more often than it is edited.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Label-left rows. Widgets that draw their own right-hand label get it
    // clipped (the row's caption already says it), so their label stays the ID.
    const float capW = fs * 8.5f;
    auto caption = [&](const char* text) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", text);
        ImGui::SameLine(capW);
    };
    auto clipBegin = [&](float w) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(p, ImVec2(p.x + w, p.y + ImGui::GetFrameHeight()), true);
        ImGui::SetNextItemWidth(w);
    };

    caption("Type");
    {
        const auto& types = facts::typeInfos();
        const char* labels[8];
        int cur = 0;
        const int n = (int)std::min<size_t>(types.size(), 8);
        for (int i = 0; i < n; ++i) {
            labels[i] = types[(size_t)i].label;
            if (types[(size_t)i].type == f.type) cur = i;
        }
        if (choice("Type", labels, n, &cur, -1.0f)) f.type = types[(size_t)cur].type;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", facts::typeInfos()[(size_t)cur].desc);
    }

    if (f.type == facts::Type::Enum) {
        caption("Options");
        ImGui::BeginGroup();
        int removeOpt = -1;
        for (size_t i = 0; i < f.options.size(); ++i) {
            ImGui::PushID((int)i);
            ImGui::TextDisabled("%d", (int)i);
            ImGui::SameLine(0.0f, fs * 0.6f);
            clipBegin(std::max(fs * 4.0f, std::min(fs * 12.0f, ImGui::GetContentRegionAvail().x -
                                                                   ImGui::GetFrameHeight() * 1.6f)));
            inputText("##opt", f.options[i], nullptr);
            ImGui::PopClipRect();
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeOpt = (int)i;
            ImGui::PopID();
        }
        if (removeOpt >= 0) f.options.erase(f.options.begin() + removeOpt);
        if (ui::GhostButton("+ option"))
            f.options.push_back("option " + std::to_string(f.options.size() + 1));
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The value IS the position in this list.");
    }

    bool computed = f.isComputed();
    if (!computed) {
        caption("Starts at");
        if (f.type == facts::Type::Bool) {
            const char* yn[2] = {"no", "yes"};
            int v = f.value != 0.0f ? 1 : 0;
            if (choice("Starts at", yn, 2, &v, fs * 7.0f)) f.value = (float)v;
        } else if (f.type == facts::Type::Position) {
            clipBegin(fs * 16.0f);
            ImGui::DragFloat3("Starts at", f.pos, 0.1f);
            ImGui::PopClipRect();
        } else {
            clipBegin(fs * 8.0f);
            float v3[3] = {f.value, 0, 0};
            if (factValueWidget("Starts at", f, v3)) f.value = v3[0];
            ImGui::PopClipRect();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("What a NEW GAME starts this fact at, and what Clear Fact puts it\n"
                              "back to. An existing save keeps whatever it stored.");

        caption("Keeps its value");
        ImGui::BeginGroup();
        const auto& tiers = facts::persistInfos();
        const char* labels[4];
        int cur = 0;
        for (int i = 0; i < 4 && i < (int)tiers.size(); ++i) {
            labels[i] = tiers[(size_t)i].label;
            if (tiers[(size_t)i].persist == f.persist) cur = i;
        }
        if (choice("Keeps its value", labels, 4, &cur, -1.0f))
            f.persist = tiers[(size_t)cur].persist;
        // The consequence, always visible: the docs' table in one line.
        static const char* kWhat[4] = {
            "Starts fresh every boot; scene switches keep it; never written anywhere.",
            "Load Checkpoint brings it back; lost when the game closes.",
            "Written into the save slot; loading an older slot brings its value back.",
            "One file per card, shared by every slot - unlocks, best times."};
        ImGui::PushFont(nullptr, fs * 0.9f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kWhat[(int)f.persist & 3]);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tiers[(size_t)cur].desc);
    }

    caption("Owned by");
    {
        const auto& scopes = facts::scopeInfos();
        const char* labels[2] = {scopes[0].label, scopes.size() > 1 ? scopes[1].label : "Scene"};
        int cur = f.scope == facts::Scope::World ? 0 : 1;
        if (choice("Owned by", labels, 2, &cur, fs * 12.0f))
            f.scope = cur == 0 ? facts::Scope::World : facts::Scope::Scene;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", scopes[(size_t)cur].desc);
    }

    // Computed: the fact stops being storage and becomes a query - "is this
    // something the game remembers, or something it works out".
    {
        ui::InspectorRows rows(capW, capW);
        if (ui::Switch("Computed from a query (Read-only: the value is whatever the query says, "
                       "nothing can write it)",
                       &computed)) {
            if (!computed) f.computed.clear();
            else if (!project_.factQueries.empty()) f.computed = project_.factQueries[0].name;
        }
    }
    if (computed) {
        caption("From query");
        clipBegin(std::min(fs * 16.0f, ImGui::GetContentRegionAvail().x));
        factQueryCombo("From query", f.computed);
        ImGui::PopClipRect();
    }
    if (live) {
        float v3[3] = {0, 0, 0};
        factLiveValue(f.name, v3);
        caption("Value now");
        ImGui::PushStyleColor(ImGuiCol_Text, sem.ok);
        ImGui::TextUnformatted(facts::formatValue(f, v3).c_str());
        ImGui::PopStyleColor();
    }

    // Used by - always open: the query that reads it and the rule that writes
    // it are the most useful cross-reference a catalog entry has.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const project::FactUsage use = project::factUsage(project_, f.name);
    {
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", use.count());
        ui::Caption("Used by", cnt);
    }
    drawFactUsedBy(f);

    // Footer: the rename hint and the destructive action, apart.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    {
        const float bw = ImGui::CalcTextSize("Delete fact").x + ImGui::GetStyle().FramePadding.x * 2.6f;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Renaming moves it in the tree - the tree is the names.");
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw);
        if (ui::DangerButton("Delete fact", bw)) ImGui::OpenPopup("##delfact");
    }
    // OpenPopup / BeginPopupModal must use the SAME string - the droneui.cpp
    // trap: a mismatch opens a modal that is never drawn, and an open-but-
    // undrawn modal swallows every click in the window.
    if (ImGui::BeginPopupModal("##delfact", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete \"%s\"?", f.name.c_str());
        if (use.any()) {
            ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
            ImGui::Text("%d place(s) still reference it - they will report an unknown fact.",
                        use.count());
            ImGui::PopStyleColor();
        }
        if (ui::DangerButton("Delete")) {
            project_.facts.erase(project_.facts.begin() + factSel_);
            factSel_ = -1;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ui::EndCard();
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ui::EndCard();
}

// ---------------------------------------------------- shared detail parts ---
// The Queries, Rules and Scenarios tabs are the catalog's shape again: a list
// card | a detail card, the detail headed by the thing's name (big, mono),
// what it means in prose directly under it, then its parts under small-caps
// captions, the cross-references always open, and the destructive action
// last and quiet.

namespace {

// A cross-reference row: kind (colour-coded), name, how (dim mono, right).
struct UsedRow {
    const char* kind;
    ImVec4 col;
    std::string name, how;
    int tab = -1, index = -1;  // where a click goes; tab -1 = nowhere
};

// Draws the rows; returns the index of the clicked one (with somewhere to
// go), or -1.
int drawUsedRows(const std::vector<UsedRow>& rows, const char* emptyText) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    if (rows.empty()) {
        ImGui::TextDisabled("%s", emptyText);
        return -1;
    }
    int clicked = -1;
    const float kindW = fs * 5.6f;
    for (size_t i = 0; i < rows.size(); ++i) {
        const UsedRow& r = rows[i];
        ImGui::PushID((int)i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        // How it uses the thing, measured first: when name and "how" do not
        // fit one line (a docked, narrow window), "how" goes under the name.
        const std::string how = r.tab >= 0 ? r.how + "  \xe2\x80\xba" : r.how;
        float hw = 0.0f;
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::PushFont(nullptr, fs * 0.86f);
            hw = ImGui::CalcTextSize(how.c_str()).x;
            ImGui::PopFont();
        }
        const float nw = ImGui::CalcTextSize(r.name.c_str()).x;
        const bool twoLines = kindW + nw + fs * 1.5f + hw > w;
        const float h = twoLines ? fs * 2.6f : ImGui::GetFrameHeight() + fs * 0.3f;
        const std::string id = std::string(r.kind) + " " + r.name;
        if (ImGui::InvisibleButton(id.c_str(), ImVec2(std::max(1.0f, w), h)) && r.tab >= 0)
            clicked = (int)i;
        const bool hov = ImGui::IsItemHovered();
        if (hov && r.tab < 0) ImGui::SetTooltip("Open the object's flow graph to see the node.");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hov)
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(sem.field),
                              ImGui::GetStyle().FrameRounding);
        const float ty = twoLines ? p.y + fs * 0.25f : p.y + (h - fs) * 0.5f;
        {
            ui::FontScope bold(ui::boldFont());
            ImGui::PushFont(nullptr, fs * 0.8f);
            dl->AddText(ImVec2(p.x + fs * 0.5f, ty + fs * 0.12f), ImGui::GetColorU32(r.col), r.kind);
            ImGui::PopFont();
        }
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::PushFont(nullptr, fs * 0.86f);
            const ImVec2 hp = twoLines ? ImVec2(p.x + kindW, ty + fs * 1.2f)
                                       : ImVec2(p.x + w - hw - fs * 0.5f, ty + fs * 0.1f);
            dl->AddText(hp, ImGui::GetColorU32(sem.textDim), how.c_str());
            ImGui::PopFont();
        }
        dl->PushClipRect(p, ImVec2(twoLines ? p.x + w : std::max(p.x + kindW, p.x + w - hw - fs), p.y + h),
                         true);
        dl->AddText(ImVec2(p.x + kindW, ty), ImGui::GetColorU32(sem.text), r.name.c_str());
        dl->PopClipRect();
        ImGui::PopID();
    }
    return clicked;
}

// The type as a small coloured mark - the catalog's column, reused wherever a
// fact is listed.
const char* typeMark(facts::Type t, ImVec4* col) {
    const theme::Semantics& sem = theme::semantics();
    switch (t) {
        case facts::Type::Bool: *col = sem.ok; return "?";
        case facts::Type::Int: *col = sem.accent; return "#";
        case facts::Type::Float: *col = sem.special; return ".0";
        case facts::Type::Enum: *col = sem.special; return "\xe2\x89\xa1";
        case facts::Type::Position: *col = sem.warn; return "xyz";
    }
    *col = sem.textDim;
    return "?";
}

// What kind of thing the detail shows ("QUERY"), then its name: big,
// monospace, across the card. The field's own label falls outside the card
// and is clipped - it stays the ID scripts type into.
void detailName(const char* kind, const char* id, std::string& value) {
    const float fs = ImGui::GetFontSize();
    {
        ui::FontScope mono(ui::monoFont());
        ImGui::PushFont(nullptr, fs * 0.86f);
        ImGui::TextDisabled("%s", kind);
        ImGui::PopFont();
    }
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushClipRect(p, ImVec2(p.x + w, p.y + fs * 3.0f), true);
    ImGui::SetNextItemWidth(w);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", value.c_str());
    ImGui::PushFont(ui::monoFont(), fs * 1.25f);
    if (ImGui::InputText(id, buf, sizeof(buf))) value = buf;
    ImGui::PopFont();
    ImGui::PopClipRect();
}

// Prose directly under the name, the full width of the card.
void detailProse(const char* id, std::string& value) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushClipRect(p, ImVec2(p.x + w, p.y + ImGui::GetFontSize() * 20.0f), true);
    ImGui::SetNextItemWidth(w);
    inputTextProse(id, value, nullptr);
    ImGui::PopClipRect();
}

void detailRule() {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

// A label-left row's caption (the catalog's rows: same column).
void rowCaption(const char* text) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", text);
    ImGui::SameLine(ImGui::GetFontSize() * 8.5f);
}

// A list row: a status dot (0 = none), the name, a dim note on the right.
// `label` is the ID; its visible part is drawn. Returns clicked.
bool listRow(const char* label, const char* note, bool selected, ImU32 dot, bool dim) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight() + fs * 0.2f;
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(std::max(1.0f, w), h));
    const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = ImGui::GetStyle().FrameRounding;
    const ImVec2 q(p.x + w, p.y + h);
    if (selected) {
        ImVec4 a = sem.accent;
        a.w = 0.16f;
        dl->AddRectFilled(p, q, ImGui::GetColorU32(a), r);
        a.w = 0.30f;
        dl->AddRect(p, q, ImGui::GetColorU32(a), r);
    } else if (t > 0.0f) {
        ImVec4 s = sem.field;
        s.w *= t;
        dl->AddRectFilled(p, q, ImGui::GetColorU32(s), r);
    }
    float x = p.x + fs * 0.6f;
    if (dot) {
        dl->AddCircleFilled(ImVec2(x + fs * 0.2f, p.y + h * 0.5f), fs * 0.2f, dot);
        x += fs * 0.9f;
    }
    float nw = 0.0f;
    if (note && *note) {
        ui::FontScope mono(ui::monoFont());
        ImGui::PushFont(nullptr, fs * 0.82f);
        const ImVec2 ns = ImGui::CalcTextSize(note);
        nw = ns.x;
        dl->AddText(ImVec2(q.x - nw - fs * 0.6f, p.y + (h - ns.y) * 0.5f),
                    ImGui::GetColorU32(sem.textDim), note);
        ImGui::PopFont();
    }
    const char* end = ImGui::FindRenderedTextEnd(label);
    dl->PushClipRect(p, ImVec2(q.x - nw - fs, q.y), true);
    dl->AddText(ImVec2(x, p.y + (h - fs) * 0.5f),
                ImGui::GetColorU32(dim ? sem.textDim : sem.text), label, end);
    dl->PopClipRect();
    return pressed;
}

// The answer, then the tree that gave it: every node with the value it read,
// a green/red dot, and the child that decided its group in full ink (the
// others dim) - the "Why?" the tree shape exists for.
void drawExplain(const facts::Explain& e, int depth) {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float x = p.x + (float)depth * fs * 1.2f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(x + fs * 0.3f, p.y + h * 0.5f), fs * 0.2f,
                        ImGui::GetColorU32(e.value ? sem.ok : sem.danger));
    {
        ui::FontScope mono(ui::monoFont());
        ImGui::PushFont(nullptr, fs * 0.88f);
        const float th = ImGui::GetTextLineHeight();
        const bool strong = depth == 0 || e.decisive;
        dl->AddText(ImVec2(x + fs * 0.9f, p.y + (h - th) * 0.5f),
                    ImGui::GetColorU32(strong ? sem.text : sem.textDim), e.text.c_str());
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), h));
    // A long leaf runs past a narrow card: the whole line on hover.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", e.text.c_str());
    for (const facts::Explain& k : e.children) drawExplain(k, depth + 1);
}

// "true right now" / "false right now" and the tree under it.
void drawWhy(bool answer, const facts::Explain& ex) {
    ui::StatePill(answer ? "true right now" : "false right now", answer ? 0 : 2);
    ImGui::Spacing();
    drawExplain(ex, 0);
}

// The list card's top row: a filter field and the tab's primary "+ X".
// Returns true when the button was clicked.
bool listTop(const char* filterId, std::string& filter, const char* hint, const char* add) {
    const float addW = ImGui::CalcTextSize(add).x + ImGui::GetStyle().FramePadding.x * 2.6f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - addW - ImGui::GetStyle().ItemSpacing.x);
    inputText(filterId, filter, nullptr, hint);
    ImGui::SameLine();
    return ui::PrimaryButton(add, addW);
}

bool nameMatches(const std::string& name, const std::string& desc, const std::string& filter) {
    if (filter.empty()) return true;
    std::string hay = name + " " + desc, needle = filter;
    std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    return hay.find(needle) != std::string::npos;
}

// The delete footer: a hint on the left, the quiet danger button on the
// right. Returns true when the button was clicked.
bool detailFooter(const char* hint, const char* del) {
    detailRule();
    const float bw = ImGui::CalcTextSize(del).x + ImGui::GetStyle().FramePadding.x * 2.6f;
    ImGui::AlignTextToFramePadding();
    if (hint) ImGui::TextDisabled("%s", hint);
    else ImGui::Dummy(ImVec2(1.0f, 1.0f));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw));
    return ui::DangerButton(del, bw);
}

}  // namespace

// The Used by rows of a catalog entry: kind (colour-coded), name, HOW it uses
// the fact ("reads - trust >= 5", "writes - +5", "sets 8"), click to jump there.
void App::drawFactUsedBy(const facts::Fact& f) {
    const theme::Semantics& sem = theme::semantics();
    std::vector<UsedRow> rows;
    auto num = [](float v) {
        char b[32];
        std::snprintf(b, sizeof(b), "%g", v);
        return std::string(b);
    };
    // The first comparison in a condition that names this fact, as text.
    std::function<bool(const facts::Condition&, std::string&)> leaf =
        [&](const facts::Condition& c, std::string& out) {
            if (c.kind == facts::Condition::Kind::Compare &&
                (c.fact == f.name || c.rhsFact == f.name)) {
                out = factLeaf(c.fact) + " " + facts::cmpCpp(c.cmp) + " " +
                      (c.rhsFact.empty() ? num(c.value) : factLeaf(c.rhsFact));
                return true;
            }
            for (const facts::Condition& k : c.children)
                if (leaf(k, out)) return true;
            return false;
        };
    for (size_t i = 0; i < project_.factQueries.size(); ++i) {
        std::string l;
        if (leaf(project_.factQueries[i].root, l))
            rows.push_back({"QUERY", sem.accent, project_.factQueries[i].name, "reads \xc2\xb7 " + l, 1,
                            (int)i});
    }
    for (size_t i = 0; i < project_.factRules.size(); ++i) {
        const facts::Rule& r = project_.factRules[i];
        std::string l, how;
        if (leaf(r.when, l)) how = "reads \xc2\xb7 " + l;
        for (const facts::RuleAction& a : r.then) {
            if (a.kind == facts::RuleAction::Kind::SendEvent || a.target != f.name) continue;
            std::string w = a.kind == facts::RuleAction::Kind::AddFact
                                ? (a.value >= 0 ? "+" : "") + num(a.value)
                            : a.kind == facts::RuleAction::Kind::ToggleFact ? std::string("toggles")
                                                                            : "sets " + num(a.value);
            how += (how.empty() ? "writes \xc2\xb7 " : ", writes \xc2\xb7 ") + w;
            break;
        }
        if (!how.empty()) rows.push_back({"RULE", sem.special, r.name, how, 2, (int)i});
    }
    for (size_t i = 0; i < project_.factScenarios.size(); ++i)
        for (const facts::ScenarioValue& v : project_.factScenarios[i].values)
            if (v.fact == f.name) {
                const float v3[3] = {v.value, v.pos[1], v.pos[2]};
                const float* src = f.type == facts::Type::Position ? v.pos : v3;
                rows.push_back({"SCENARIO", sem.warn, project_.factScenarios[i].name,
                                "sets " + facts::formatValue(f, src), 3, (int)i});
                break;
            }
    const project::FactUsage use = project::factUsage(project_, f.name);
    for (const std::string& c : use.computed)
        rows.push_back({"COMPUTED", sem.ok, c, "computed from it", 0,
                        facts::indexOf(project_.facts, c)});
    for (const std::string& g : use.graphs)
        rows.push_back({"GRAPH", sem.ok, g, "flow node", -1, -1});

    const int hit = drawUsedRows(rows, "Nothing reads or writes this fact yet.");
    if (hit >= 0) factJumpTo(rows[(size_t)hit].tab, rows[(size_t)hit].index);
}

// A Used-by click: select the target and bring its tab forward.
void App::factJumpTo(int tab, int index) {
    if (tab == 0 && index >= 0) factSel_ = index;
    if (tab == 1) factQuerySel_ = index;
    if (tab == 2) factRuleSel_ = index;
    if (tab == 3) factScenarioSel_ = index;
    if (tab != factTab_) {
        factTab_ = tab;
        factTabJump_ = tab;
    }
}

// ---------------------------------------------------------------- queries ---

void App::drawFactQueriesTab() {
    const theme::Semantics& sem = theme::semantics();
    const float paneH = ImGui::GetContentRegionAvail().y;
    if (factSideW_ <= 0.0f) factSideW_ = std::max(scaled(260), ImGui::GetContentRegionAvail().x * 0.32f);
    auto read = [this](const std::string& n, float* o) { return factLiveValue(n, o); };

    // ------------------------------------------------------------ the list
    if (ui::BeginCard("##qlistcard", ImVec2(factSideW_, paneH))) {
        if (listTop("##qfilter", factSideFilter_, "Filter queries", "+ Query")) {
            facts::Query q;
            q.name = uniqueName("CanDoThing", [&](const std::string& n) {
                return facts::queryIndexOf(project_.factQueries, n) >= 0;
            });
            project_.factQueries.push_back(q);
            factQuerySel_ = (int)project_.factQueries.size() - 1;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A named condition over facts - CanEnterBasement, MartaWillTalk.\n"
                              "The same query gates a door, a dialogue line and a graph, so\n"
                              "the design changes in one place.");
        ImGui::Spacing();
        if (project_.factQueries.empty()) {
            if (ui::EmptyState(ui::Glyph::List, "No queries yet",
                               "A query is a named condition over facts that doors, dialogue "
                               "and graphs share instead of copying.",
                               "Create the first query")) {
                facts::Query q;
                q.name = "CanDoThing";
                project_.factQueries.push_back(q);
                factQuerySel_ = 0;
            }
        } else {
            ImGui::BeginChild("##qlist");
            int shown = 0;
            for (size_t i = 0; i < project_.factQueries.size(); ++i) {
                const facts::Query& q = project_.factQueries[i];
                if (!nameMatches(q.name, q.desc, factSideFilter_)) continue;
                ++shown;
                const bool v = facts::evaluate(q.root, project_.facts, project_.factQueries, read);
                const std::string label = q.name + "##ql" + std::to_string(i);
                if (listRow(label.c_str(), v ? "true" : "false", factQuerySel_ == (int)i,
                            ImGui::GetColorU32(v ? sem.ok : sem.danger), false))
                    factQuerySel_ = (int)i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !q.desc.empty())
                    ImGui::SetTooltip("%s", q.desc.c_str());
            }
            if (!shown) ui::EmptyState(ui::Glyph::List, "No query matches", "Clear the filter.");
            ImGui::EndChild();
        }
    }
    ui::EndCard();
    ImGui::SameLine(0.0f, 0.0f);
    ui::VSplitter("##qsplit", &factSideW_, scaled(220), scaled(420), paneH);

    // ---------------------------------------------------------- the detail
    if (!ui::BeginCard("##qdetail", ImVec2(0.0f, paneH))) {
        ui::EndCard();
        return;
    }
    if (factQuerySel_ < 0 || factQuerySel_ >= (int)project_.factQueries.size()) {
        ui::EmptyState(ui::Glyph::List, "No query selected",
                       "Pick a query on the left to see its condition, why it answers what it "
                       "does right now, and what uses it.");
        ui::EndCard();
        return;
    }
    facts::Query& q = project_.factQueries[(size_t)factQuerySel_];

    // The rename dance: retarget every reference the moment the edit commits.
    std::string typed = q.name;
    detailName("query", "Name", typed);
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !ImGui::IsItemActive())
        ImGui::SetTooltip("Renaming retargets every graph node, rule, query and computed fact\n"
                          "that names this query.");
    detailProse("What it means", q.desc);
    detailRule();

    ui::Caption("Condition");
    drawFactCondition(q.root, 0, "qroot");
    detailRule();

    // "Why?" - the host evaluates the SAME structure codegen compiles, against
    // the same values the blackboard shows.
    ui::Caption("Why?");
    facts::Explain ex;
    const bool answer = facts::evaluate(q.root, project_.facts, project_.factQueries, read, &ex);
    drawWhy(answer, ex);
    detailRule();

    const project::FactUsage use = project::queryUsage(project_, q.name);
    {
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", use.count());
        ui::Caption("Used by", cnt);
    }
    {
        std::vector<UsedRow> rows;
        for (const std::string& n : use.queries)
            rows.push_back({"QUERY", sem.accent, n, "names it", 1,
                            facts::queryIndexOf(project_.factQueries, n)});
        for (const std::string& n : use.rules) {
            int ri = -1;
            for (size_t i = 0; i < project_.factRules.size(); ++i)
                if (project_.factRules[i].name == n) ri = (int)i;
            rows.push_back({"RULE", sem.special, n, "when", 2, ri});
        }
        for (const std::string& n : use.computed)
            rows.push_back({"COMPUTED", sem.ok, n, "computed from it", 0, facts::indexOf(project_.facts, n)});
        for (const std::string& n : use.graphs) rows.push_back({"GRAPH", sem.ok, n, "flow node", -1, -1});
        const int hit = drawUsedRows(rows, "Nothing references this query yet.");
        if (hit >= 0) factJumpTo(rows[(size_t)hit].tab, rows[(size_t)hit].index);
    }

    if (detailFooter(nullptr, "Delete query")) {
        project_.factQueries.erase(project_.factQueries.begin() + factQuerySel_);
        factQuerySel_ = -1;
    }
    ui::EndCard();
}

// ------------------------------------------------------------------ rules ---

void App::drawFactRulesTab() {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float paneH = ImGui::GetContentRegionAvail().y;
    if (factSideW_ <= 0.0f) factSideW_ = std::max(scaled(260), ImGui::GetContentRegionAvail().x * 0.32f);
    auto read = [this](const std::string& n, float* o) { return factLiveValue(n, o); };
    const auto& pol = facts::policyInfos();
    auto policyShort = [](facts::RulePolicy p) {
        switch (p) {
            case facts::RulePolicy::OnBecomeTrue: return "on true";
            case facts::RulePolicy::WhileTrue: return "every frame";
            case facts::RulePolicy::Once: return "once";
        }
        return "";
    };

    // ------------------------------------------------------------ the list
    if (ui::BeginCard("##rlistcard", ImVec2(factSideW_, paneH))) {
        if (listTop("##rfilter", factSideFilter_, "Filter rules", "+ Rule")) {
            facts::Rule r;
            r.name = uniqueName("WhenSomething", [&](const std::string& n) {
                for (const facts::Rule& e : project_.factRules)
                    if (e.name == n) return true;
                return false;
            });
            project_.factRules.push_back(r);
            factRuleSel_ = (int)project_.factRules.size() - 1;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A reaction: when this condition holds, change these facts or send\n"
                              "this event. Rules run once per frame before every graph.");
        ImGui::Spacing();
        if (project_.factRules.empty()) {
            if (ui::EmptyState(ui::Glyph::List, "No rules yet",
                               "A rule reacts: when a condition holds it sets, adds to or toggles "
                               "a fact, or sends an event into a graph.",
                               "Create the first rule")) {
                facts::Rule r;
                r.name = "WhenSomething";
                project_.factRules.push_back(r);
                factRuleSel_ = 0;
            }
        } else {
            // The order IS part of the design (rules run top to bottom within
            // a pass), so the list says it and rows drag to reorder.
            ImGui::TextDisabled("Runs top to bottom - drag to reorder");
            ImGui::BeginChild("##rlist");
            int shown = 0;
            for (size_t i = 0; i < project_.factRules.size(); ++i) {
                const facts::Rule& r = project_.factRules[i];
                if (!nameMatches(r.name, r.desc, factSideFilter_)) continue;
                ++shown;
                const bool v = r.enabled &&
                               facts::evaluate(r.when, project_.facts, project_.factQueries, read);
                char note[48];
                std::snprintf(note, sizeof(note), "%s", r.enabled ? policyShort(r.policy) : "off");
                const std::string label = r.name + "##rl" + std::to_string(i);
                if (listRow(label.c_str(), note, factRuleSel_ == (int)i,
                            r.enabled ? ImGui::GetColorU32(v ? sem.ok : sem.textDim) : 0, !r.enabled))
                    factRuleSel_ = (int)i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !r.desc.empty())
                    ImGui::SetTooltip("%s", r.desc.c_str());
                // Drag to reorder: past half a row up or down swaps with the
                // neighbour (only while unfiltered - a filtered list hides the
                // neighbour the swap would cross).
                if (factSideFilter_.empty() && ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                    const float dy = ImGui::GetIO().MousePos.y - ImGui::GetItemRectMin().y;
                    const int to = dy < 0.0f ? (int)i - 1
                                   : dy > ImGui::GetItemRectSize().y ? (int)i + 1
                                                                     : (int)i;
                    if (to != (int)i && to >= 0 && to < (int)project_.factRules.size()) {
                        std::swap(project_.factRules[i], project_.factRules[(size_t)to]);
                        if (factRuleSel_ == (int)i) factRuleSel_ = to;
                        else if (factRuleSel_ == to) factRuleSel_ = (int)i;
                    }
                }
            }
            if (!shown) ui::EmptyState(ui::Glyph::List, "No rule matches", "Clear the filter.");
            ImGui::EndChild();
        }
    }
    ui::EndCard();
    ImGui::SameLine(0.0f, 0.0f);
    ui::VSplitter("##rsplit", &factSideW_, scaled(220), scaled(420), paneH);

    // ---------------------------------------------------------- the detail
    if (!ui::BeginCard("##rdetail", ImVec2(0.0f, paneH))) {
        ui::EndCard();
        return;
    }
    if (factRuleSel_ < 0 || factRuleSel_ >= (int)project_.factRules.size()) {
        ui::EmptyState(ui::Glyph::List, "No rule selected",
                       "Pick a rule on the left to see when it runs, what it does, and whether "
                       "it would fire right now.");
        ui::EndCard();
        return;
    }
    facts::Rule& r = project_.factRules[(size_t)factRuleSel_];

    detailName("rule", "Name", r.name);
    detailProse("What it does", r.desc);
    detailRule();

    {
        ui::InspectorRows rows(fs * 8.5f, fs * 8.5f);
        ui::Switch("Enabled (Off: the rule is kept but never runs)", &r.enabled);
    }
    rowCaption("Runs");
    {
        const char* labels[3];
        int cur = 0;
        const int n = (int)std::min<size_t>(pol.size(), 3);
        for (int i = 0; i < n; ++i) {
            labels[i] = pol[(size_t)i].label;
            if (pol[(size_t)i].policy == r.policy) cur = i;
        }
        ImGui::BeginGroup();
        if (choice("Runs", labels, n, &cur, -1.0f)) r.policy = pol[(size_t)cur].policy;
        // The consequence, always visible.
        static const char* kWhat[3] = {
            "Once each time the world enters the state - the default.",
            "Every frame it holds: for re-asserting, never for counting or sending.",
            "The first time, then never again until the game restarts."};
        ImGui::PushFont(nullptr, fs * 0.9f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", kWhat[cur < 3 ? cur : 0]);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            wrappedTip(pol[(size_t)cur].desc);
    }
    detailRule();

    ui::Caption("When");
    drawFactCondition(r.when, 0, "rwhen");
    detailRule();

    {
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", (int)r.then.size());
        ui::Caption("Then", cnt);
    }
    int removeAct = -1;
    const auto& kinds = facts::actionKindInfos();
    for (size_t i = 0; i < r.then.size(); ++i) {
        facts::RuleAction& a = r.then[i];
        ImGui::PushID((int)i);
        if (ui::IconButton("Remove", ui::RtIcon::Close, 0.0f, "Remove this action")) removeAct = (int)i;
        ImGui::SameLine();
        const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f;
        const bool hasValue = a.kind != facts::RuleAction::Kind::ToggleFact;
        const float kindW = std::max(fs * 6.0f, avail * 0.26f);
        const float valW = hasValue ? std::max(fs * 5.0f, avail * 0.26f) : 0.0f;
        const float tgtW = std::max(fs * 6.0f, avail - kindW - valW);
        ImGui::SetNextItemWidth(kindW);
        {
            const char* cur = "Set fact";
            for (const facts::ActionKindInfo& k : kinds)
                if (k.kind == a.kind) cur = k.label;
            ui::FontScope bold(ui::boldFont());
            ImGui::PushStyleColor(ImGuiCol_Text, sem.special);
            const bool open = ImGui::BeginCombo("##akind", cur);
            ImGui::PopStyleColor();
            if (open) {
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
        ImGui::SetNextItemWidth(tgtW);
        if (a.kind == facts::RuleAction::Kind::SendEvent) {
            inputText("##atarget", a.target, nullptr, "event name");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip(
                    "An On Event node with this name receives it on the next\n"
                    "frame, like any other graph event.");
        } else {
            factCombo("##atarget", a.target, false);
        }
        if (hasValue) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(valW);
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
    if (r.then.empty()) ImGui::TextDisabled("No actions - the rule fires and changes nothing.");
    if (ui::GhostButton("+ action")) r.then.push_back(facts::RuleAction{});
    detailRule();

    ui::Caption("Why?");
    facts::Explain ex;
    const bool answer = facts::evaluate(r.when, project_.facts, project_.factQueries, read, &ex);
    drawWhy(answer, ex);

    if (detailFooter("Rules run in the list's order.", "Delete rule")) {
        project_.factRules.erase(project_.factRules.begin() + factRuleSel_);
        factRuleSel_ = -1;
    }
    ui::EndCard();
}

// -------------------------------------------------------------- scenarios ---

void App::drawFactScenariosTab() {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const float paneH = ImGui::GetContentRegionAvail().y;
    if (factSideW_ <= 0.0f) factSideW_ = std::max(scaled(260), ImGui::GetContentRegionAvail().x * 0.32f);
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    auto taken = [&](const std::string& n) {
        for (const facts::Scenario& e : project_.factScenarios)
            if (e.name == n) return true;
        return false;
    };

    // ------------------------------------------------------------ the list
    if (ui::BeginCard("##slistcard", ImVec2(factSideW_, paneH))) {
        if (listTop("##sfilter", factSideFilter_, "Filter scenarios", "+ Scenario")) {
            facts::Scenario s;
            s.name = uniqueName("Situation", taken);
            project_.factScenarios.push_back(s);
            factScenarioSel_ = (int)project_.factScenarios.size() - 1;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("A saved set of fact values - \"generator repaired, Marta saved,\n"
                              "no key\" - pushed into the running game to look at a situation\n"
                              "twenty minutes deep without playing to it.");
        // Capturing beats typing: play into the situation once and press this.
        if (ui::SecondaryButton("Capture from the running game", -1.0f, live)) {
            facts::Scenario s;
            s.name = uniqueName("Captured", taken);
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
        if (!live && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Needs a running debug build with the Live Debugger on.");
        ImGui::Spacing();
        if (project_.factScenarios.empty()) {
            ui::EmptyState(ui::Glyph::Flag, "No scenarios yet",
                           "A scenario is a saved set of fact values to push into the running "
                           "game - make one with + Scenario, or capture one while it runs.");
        } else {
            ImGui::BeginChild("##slist");
            int shown = 0;
            for (size_t i = 0; i < project_.factScenarios.size(); ++i) {
                const facts::Scenario& s = project_.factScenarios[i];
                if (!nameMatches(s.name, s.desc, factSideFilter_)) continue;
                ++shown;
                char note[32];
                std::snprintf(note, sizeof(note), "%d value%s", (int)s.values.size(),
                              s.values.size() == 1 ? "" : "s");
                const std::string label = s.name + "##sl" + std::to_string(i);
                if (listRow(label.c_str(), note, factScenarioSel_ == (int)i, 0, false))
                    factScenarioSel_ = (int)i;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !s.desc.empty())
                    ImGui::SetTooltip("%s", s.desc.c_str());
            }
            if (!shown) ui::EmptyState(ui::Glyph::Flag, "No scenario matches", "Clear the filter.");
            ImGui::EndChild();
        }
    }
    ui::EndCard();
    ImGui::SameLine(0.0f, 0.0f);
    ui::VSplitter("##ssplit", &factSideW_, scaled(220), scaled(420), paneH);

    // ---------------------------------------------------------- the detail
    if (!ui::BeginCard("##sdetail", ImVec2(0.0f, paneH))) {
        ui::EndCard();
        return;
    }
    if (factScenarioSel_ < 0 || factScenarioSel_ >= (int)project_.factScenarios.size()) {
        ui::EmptyState(ui::Glyph::Flag, "No scenario selected",
                       "Pick a scenario on the left to see the values it sets and push them "
                       "into the running game.");
        ui::EndCard();
        return;
    }
    facts::Scenario& s = project_.factScenarios[(size_t)factScenarioSel_];

    detailName("scenario", "Name", s.name);
    detailProse("What it sets up", s.desc);
    ImGui::Spacing();

    // THE action of the tab, with why it is unavailable right under it.
    if (ui::PrimaryButton("Apply to the running game", 0.0f, live)) {
        for (const facts::ScenarioValue& v : s.values) {
            const int fi = facts::indexOf(project_.facts, v.fact);
            if (fi < 0 || project_.facts[(size_t)fi].isComputed()) continue;
            factOverrides_[v.fact] = {v.value, v.pos[1], v.pos[2]};
            if (project_.facts[(size_t)fi].type == facts::Type::Position)
                factOverrides_[v.fact] = {v.pos[0], v.pos[1], v.pos[2]};
        }
        factPushOverrides();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled))
        wrappedTip("Applied as blackboard overrides, which the game re-asserts every frame "
                   "until they are cleared - so a rule that fights the value cannot quietly "
                   "undo the scenario before you have seen it. Clear them on the Blackboard tab.");
    ImGui::PushFont(nullptr, fs * 0.9f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", live ? "Held until cleared on the Blackboard."
                                   : "Needs a running debug build with the Live Debugger on.");
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    detailRule();

    {
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", (int)s.values.size());
        ui::Caption("Values", cnt);
    }
    int removeRow = -1;
    if (!s.values.empty() &&
        ImGui::BeginTable("##svals", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Fact", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("##rm", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        for (size_t i = 0; i < s.values.size(); ++i) {
            facts::ScenarioValue& v = s.values[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const int fi = facts::indexOf(project_.facts, v.fact);
            {
                ImVec4 mc = sem.textDim;
                const char* mark = fi >= 0 ? typeMark(project_.facts[(size_t)fi].type, &mc) : "!";
                if (fi < 0) mc = sem.danger;
                ui::FontScope mono(ui::monoFont());
                ImGui::PushFont(nullptr, fs * 0.8f);
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, mc);
                const float mw = ImGui::CalcTextSize(mark).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, fs * 1.4f - mw - fs * 0.3f));
                ImGui::TextUnformatted(mark);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::SetNextItemWidth(-FLT_MIN);
            factCombo("##sfact", v.fact, true);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (fi >= 0) {
                const facts::Fact& f = project_.facts[(size_t)fi];
                if (f.isComputed()) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("computed - not applied");
                } else if (f.type == facts::Type::Position) {
                    ImGui::DragFloat3("##sval", v.pos, 0.1f);
                } else {
                    float v3[3] = {v.value, 0, 0};
                    if (factValueWidget("##sval", f, v3)) v.value = v3[0];
                }
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                ImGui::TextUnformatted(v.fact.empty() ? "pick a fact" : "unknown fact");
                ImGui::PopStyleColor();
            }
            ImGui::TableNextColumn();
            if (ui::IconButton("Remove", ui::RtIcon::Close, 0.0f, "Remove this value")) removeRow = (int)i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeRow >= 0) s.values.erase(s.values.begin() + removeRow);
    if (s.values.empty()) ImGui::TextDisabled("No values - applying it changes nothing.");
    if (ui::GhostButton("+ value")) s.values.push_back(facts::ScenarioValue{});

    if (detailFooter(nullptr, "Delete scenario")) {
        project_.factScenarios.erase(project_.factScenarios.begin() + factScenarioSel_);
        factScenarioSel_ = -1;
    }
    ui::EndCard();
}

// ------------------------------------------------------------- blackboard ---

void App::drawFactBlackboardTab() {
    const theme::Semantics& sem = theme::semantics();
    const float fs = ImGui::GetFontSize();
    const bool live = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;

    // The state line: what this table is reading, and the held overrides.
    if (!live)
        ui::Note(3, "No game reporting - these are the start values. The Blackboard reads a running "
                    "debug build through the Live Debugger: debug profile, Live Debugger on, Run.");
    {
        ImGui::SetNextItemWidth(std::min(fs * 18.0f, ImGui::GetContentRegionAvail().x));
        inputText("##bbfilter", factBoardFilter_, nullptr, "Filter facts - name or meaning");
        if (!factOverrides_.empty()) {
            char held[64];
            std::snprintf(held, sizeof(held), "%d held by hand", (int)factOverrides_.size());
            ImGui::SameLine();
            ui::StatePill(held, 1);
            ImGui::SameLine();
            if (ui::GhostButton("Clear all overrides")) {
                factOverrides_.clear();
                factPushOverrides();
            }
        }
    }
    ImGui::Spacing();

    if (project_.facts.empty()) {
        ui::EmptyState(ui::Glyph::List, "No facts to watch",
                       "Declare facts on the Catalog tab; the Blackboard shows each one's value "
                       "in the running game.");
        return;
    }

    if (!ui::BeginCard("##bbcard", ImVec2(0.0f, ImGui::GetContentRegionAvail().y))) {
        ui::EndCard();
        return;
    }
    if (ImGui::BeginTable("##bb", 5,
                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Fact", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn(live ? "Now" : "Starts", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Keeps", ImGuiTableColumnFlags_WidthFixed, fs * 5.6f);
        ImGui::TableSetupColumn("Override", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Last changed", ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        const facts::Layout lay = facts::layoutOf(project_.facts);
        for (size_t i = 0; i < project_.facts.size(); ++i) {
            const facts::Fact& f = project_.facts[i];
            if (!matchesFilter(f, factBoardFilter_)) continue;
            ImGui::TableNextRow(0, ImGui::GetFrameHeight());
            ImGui::PushID((int)i);

            ImGui::TableNextColumn();
            {
                ImVec4 mc;
                const char* mark = typeMark(f.type, &mc);
                ui::FontScope mono(ui::monoFont());
                ImGui::PushFont(nullptr, fs * 0.8f);
                ImGui::AlignTextToFramePadding();
                const float mw = ImGui::CalcTextSize(mark).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, fs * 1.4f - mw - fs * 0.3f));
                ImGui::PushStyleColor(ImGuiCol_Text, mc);
                ImGui::TextUnformatted(mark);
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            ImGui::SameLine(0.0f, fs * 0.3f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(f.name.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !f.desc.empty())
                ImGui::SetTooltip("%s", f.desc.c_str());

            ImGui::TableNextColumn();
            float v3[3] = {0, 0, 0};
            factLiveValue(f.name, v3);
            {
                ui::FontScope mono(ui::monoFont());
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, live ? sem.ok : sem.textDim);
                const std::string v = f.type == facts::Type::Bool ? (v3[0] != 0.0f ? "yes" : "no")
                                                                  : facts::formatValue(f, v3);
                ImGui::TextUnformatted(v.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::TableNextColumn();
            // The badge is shorter than the row's frame: centre it on the line.
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y * 0.7f);
            if (f.isComputed())
                ui::Badge("Computed", 0);
            else
                ui::Badge(facts::persistLabel(f.persist),
                          f.persist == facts::Persist::Save         ? 3
                          : f.persist == facts::Persist::Checkpoint ? 1
                          : f.persist == facts::Persist::Profile    ? 3
                                                                    : 4);

            ImGui::TableNextColumn();
            if (f.isComputed()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("-");
            } else {
                auto it = factOverrides_.find(f.name);
                bool held = it != factOverrides_.end();
                if (ui::Switch("##hold", &held)) {
                    if (held) factOverrides_[f.name] = {v3[0], v3[1], v3[2]};
                    else factOverrides_.erase(f.name);
                    factPushOverrides();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Hold this fact at a value: re-asserted every frame until\n"
                                      "cleared, so a rule that rewrites it cannot undo it.");
                if (held) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(-FLT_MIN);
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
            ImGui::AlignTextToFramePadding();
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
                ImGui::TextUnformatted(who.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%u frame%s ago", ago, ago == 1 ? "" : "s");
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::EndCard();
}
