// -------------------------------------------------------------------------
// The Flow Graph editor window (imnodes): node add-menu, pins, links, params
// and the debugger overlay drawn on top of it.
//
// Split out of app.cpp so the editor builds in parallel: it was one 26k-line
// translation unit and therefore the whole build's critical path. These are
// still App:: members declared in app.hpp - the assetbrowser.cpp precedent.
// -------------------------------------------------------------------------
#include "app.hpp"
#include "app_internal.hpp"
#include "widgets.hpp"
#include "toolkit.hpp"
#include "theme.hpp"
#include "livelogic.hpp"

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

// The node's documentation, drawn identically by the add-menu tooltip and by
// the node hover: what the NODE does, then ONE LINE PER PARAMETER. The failure
// this replaces is a paragraph that carries every knob's meaning inline - the
// reader is hovering a drag labelled "Seed" and gets three sentences about
// runtime volumes before the one clause they wanted. Same shape as
// procNodeDoc() in procui.cpp; the two editors must not grow two answers to
// "what does hovering a node tell me".
static void flowNodeDoc(const FlowNodeType& t, float wrap) {
    ImGui::PushTextWrapPos(wrap);
    ImGui::TextUnformatted(t.title);
    if (t.desc && *t.desc) ImGui::TextDisabled("%s", t.desc);
    auto line = [](const char* label, const char* tip) {
        ImGui::TextUnformatted(label);
        if (tip && *tip) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextDisabled(" - %s", tip);
        }
    };
    const char* strLabel = flowStrLabel(t);
    const char* str2Label = flowStr2Label(t);
    const bool anyParam =
        (strLabel && *strLabel) || t.numCount > 0 ||
        (!t.pure && !t.trigger && t.execInCount > 1);
    if (anyParam && flowHasParamTips(t)) {
        ImGui::Separator();
        if (strLabel && *strLabel) line(strLabel, t.strTip);
        if (str2Label && *str2Label) line(str2Label, t.str2Tip);
        for (int i = 0; i < t.numCount && i < 4; ++i)
            line(t.numLabels[i] && *t.numLabels[i] ? t.numLabels[i] : "Value",
                 flowNumTip(t, i));
        // Exec inputs are parameters of a kind - which of the node's branches
        // you fire is as much a choice as what you type into it.
        if (!t.pure && !t.trigger && t.execInCount > 1) {
            for (int e = 0; e < t.execInCount && e < kFlowMaxExecIn; ++e) {
                const std::string pin = std::string("> ") + flowExecInLabel(t, e);
                line(pin.c_str(), flowExecInTip(t, e));
            }
        }
    }
    ImGui::PopTextWrapPos();
}


// --- Flow Graph window helpers ---------------------------------------------

static ImVec4 fgMix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// The colour a node's kind is drawn in (title dot, palette rows): triggers,
// logic gates, other data nodes, actions. A meaning, so it follows the theme.
static ImVec4 flowKindColor(const FlowNodeType& t) {
    const theme::Semantics& s = theme::semantics();
    if (t.trigger) return s.ok;
    if (t.pure && t.boolIn) return s.special;
    if (t.pure) return s.warn;
    return s.accent;
}

// Can Live Logic run this node type at all (livelogic.hpp's one list)?
static bool flowLiveSupported(const std::string& type) {
    for (const std::string& k : livelogic::supportedNodeTypes())
        if (k == type) return true;
    return false;
}

static std::string flowShort(std::string s, size_t max) {
    if (s.size() > max) {
        s.resize(max > 3 ? max - 3 : max);
        s += "...";
    }
    return s;
}

// The k-th label of a '|'-separated FlowNodeType::numChoices list.
static std::string flowChoice(const char* list, int k) {
    std::string out;
    int at = 0;
    for (const char* p = list;; ++p) {
        if (*p == '|' || !*p) {
            if (at == k) return out;
            out.clear();
            ++at;
            if (!*p) break;
        } else {
            out.push_back(*p);
        }
    }
    return out;
}

// What is wrong with a node right now, in a few words, or nullptr. These are
// the compile-out conditions the node bodies used to print inline as prose;
// collected here so the header can count them and a list can find every one
// in the scene.
static const char* flowNodeProblem(const Project& p, const FlowNode& n) {
    const FlowNodeType* t = flowNodeType(n.type);
    if (!t) return "Unknown node type - its definition is missing";
    auto hasObj = [&](const std::string& name) {
        for (const SceneObject& o : p.objects())
            if (o.name == name) return true;
        return false;
    };
    switch (t->strKind) {
        case FlowParamKind::AreaName:
            if (n.str.empty()) return "No area picked - the node compiles out";
            if (!hasObj(n.str)) return "The area is not in this scene";
            break;
        case FlowParamKind::EventName:
            if (n.str.empty()) return "Unnamed event - the node compiles out";
            break;
        case FlowParamKind::InputActionName:
            if (!p.input.findAction(n.str)) return "Unknown input action - it never fires";
            break;
        case FlowParamKind::FactName:
            if (n.str.empty()) return "No fact picked";
            if (facts::indexOf(p.facts, n.str) < 0) return "The fact was renamed or deleted";
            break;
        case FlowParamKind::FactQueryName: {
            if (n.str.empty()) return "No query picked";
            bool found = false;
            for (const facts::Query& q : p.factQueries) found |= q.name == n.str;
            if (!found) return "The query was renamed or deleted";
            break;
        }
        case FlowParamKind::SaveSlotMode:
            if (n.str == "autosave" && p.saveAutosaveSlot < 0)
                return "No autosave slot set (Tools > Save Editor)";
            break;
        case FlowParamKind::SaveValue: {
            bool found = n.str.empty();
            for (const SaveValue& v : p.saveValues) found |= v.name == n.str;
            if (!found) return "The save value no longer exists";
            break;
        }
        case FlowParamKind::SequenceName: {
            bool found = n.str.empty();
            for (const Sequence& s : p.sequences) found |= s.name == n.str;
            if (!found) return "The sequence no longer exists";
            break;
        }
        case FlowParamKind::MenuName: {
            bool found = n.str.empty();
            for (const GameMenu& m : p.menus) found |= m.name == n.str;
            if (!found) return "The menu no longer exists";
            break;
        }
        case FlowParamKind::ObjectName:
            if (!n.str.empty() && !hasObj(n.str)) return "The object is not in this scene";
            break;
        default: break;
    }
    return nullptr;
}

// The add-node palette's ranking of one node type against the typed query:
// every word must match somewhere; a title prefix beats a word start beats a
// substring beats the category beats the description. 0 = no match.
static int flowPaletteScore(const FlowNodeType& t, const std::string& query) {
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    const std::string title = lower(t.title), key = lower(t.key), cat = lower(t.category);
    const std::string desc = lower(t.desc ? t.desc : "");
    int total = 0;
    std::istringstream words(lower(query));
    std::string w;
    while (words >> w) {
        int s = 0;
        if (title.rfind(w, 0) == 0) s = 100;
        else if (title.find(" " + w) != std::string::npos) s = 80;
        else if (title.find(w) != std::string::npos) s = 60;
        else if (key.find(w) != std::string::npos) s = 50;
        else if (cat.find(w) != std::string::npos) s = 30;
        else if (desc.find(w) != std::string::npos) s = 10;
        if (s == 0) return 0;
        total += s;
    }
    return total > 0 ? total : 1;
}

// Can a wire dragged from pin slot `fromKind` (flowPinKind) end on a node of
// type t? Returns the pin id on node `nodeId` it would connect to, or -1.
static int flowCompatiblePin(const FlowNodeType& t, int nodeId, int fromKind) {
    if (flowExecOutIndex(fromKind) >= 0)
        return (!t.trigger && !t.pure) ? flowExecInPin(nodeId, 0) : -1;
    if (flowExecInIndex(fromKind) >= 0)
        return (!t.pure && flowExecOutCount(t) > 0) ? flowExecOutPin(nodeId, 0) : -1;
    switch (fromKind) {
        case 3: return t.idIn ? flowIdInPin(nodeId) : -1;
        case 0: return t.idOut ? flowIdOutPin(nodeId) : -1;
        case 5: return t.posIn ? flowPosInPin(nodeId) : -1;
        case 4: return t.posOut ? flowPosOutPin(nodeId) : -1;
        case 7: return t.boolIn ? flowBoolInPin(nodeId) : -1;
        case 6: return t.boolOut ? flowBoolOutPin(nodeId) : -1;
        case 9: return t.textIn ? flowTextInPin(nodeId) : -1;
        case 8: return t.textOut ? flowTextOutPin(nodeId) : -1;
        case 17: return t.numIn ? flowNumInPin(nodeId) : -1;
        case 16: return t.numOut ? flowNumOutPin(nodeId) : -1;
        default: return -1;
    }
}

// The palette's search field: Up/Down move the highlighted row (the field
// keeps the keyboard, so ImGui's own navigation never steals the arrows).
static int flowPaletteKeys(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
        int* sel = (int*)d->UserData;
        if (d->EventKey == ImGuiKey_UpArrow) --*sel;
        if (d->EventKey == ImGuiKey_DownArrow) ++*sel;
    }
    return 0;
}


void App::drawFlowGraphWindow() {
    flowGraphFocused_ = false;  // recomputed below; gates the global Ctrl+C/V
    ImGui::Begin("Flow Graph");
    if (!hasProject_) {
        ui::EmptyState(ui::Glyph::Nodes, "No project open",
                       "Open or create a project to give its objects behaviour.");
        ImGui::End();
        return;
    }
    if (project_.objects().empty()) {
        ui::EmptyState(ui::Glyph::Nodes, "No objects yet",
                       "Every flow graph belongs to an object - add one to the scene first.");
        ImGui::End();
        return;
    }
    if (flowEditorCtx_) ImNodes::EditorContextSet((ImNodesEditorContext*)flowEditorCtx_);
    const theme::Semantics& sem = theme::semantics();
    const int objCount = (int)project_.objects().size();
    // Switching graphs: positions are re-pushed and the new graph framed on
    // the next draw. Clicks made after the canvas picked its graph for this
    // frame are staged in switchTo and applied at the end.
    auto openGraph = [&](int i) {
        if (i < 0 || i >= objCount || i == flowGraphObject_) return;
        flowGraphObject_ = i;
        flowPositionsApplied_ = false;
        flowFrameAllReq_ = 1;
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
    };
    int switchTo = -1;

    // --- which object's graph is being edited -------------------------------
    if (flowGraphObject_ < 0 || flowGraphObject_ >= objCount) {
        flowGraphObject_ =
            (selectedObject_ >= 0 && selectedObject_ < objCount) ? selectedObject_ : 0;
        flowPositionsApplied_ = false;
        flowFrameAllReq_ = 1;
    }
    // Follows selection: react to the selection CHANGING, not to its value, so
    // a graph picked from the list stays until something else is selected.
    if (flowFollowSel_ && selectedObject_ != flowFollowedSel_) {
        flowFollowedSel_ = selectedObject_;
        openGraph(selectedObject_);
    }
    const bool dbgLive = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    const bool dbgHalted = dbgState_ == DbgState::Halted;
    // ...and while the game is halted, the graph that stopped it.
    if (dbgHalted && flowFollowSel_ && dbgSnap_.breakKey >= 0 &&
        dbgSnap_.breakKey != flowFollowedBreak_) {
        flowFollowedBreak_ = dbgSnap_.breakKey;
        if (const livedbg::NodeSym* ns = dbgSyms_.find(dbgSnap_.breakKey))
            if (ns->scene == project_.activeScene)
                for (int i = 0; i < objCount; ++i)
                    if (project_.objects()[i].id == ns->objectId) {
                        openGraph(i);
                        flowSelectReq_ = ns->nodeId;
                        break;
                    }
    }
    if (!dbgHalted) flowFollowedBreak_ = -1;

    // --- the scene's graphs, once per frame ---------------------------------
    // Which objects own logic, their first trigger and size, and - with a game
    // reporting - how recently and how often their nodes ran.
    struct GraphRow {
        int index = 0;
        int nodes = 0;
        std::string trigger;
        float heat = FLT_MAX;
        uint32_t hits = 0;
        bool bp = false;
    };
    std::vector<GraphRow> graphRows;
    for (int i = 0; i < objCount; ++i) {
        const SceneObject& o = project_.objects()[i];
        if (o.flowGraph.nodes.empty()) continue;
        GraphRow r;
        r.index = i;
        r.nodes = (int)o.flowGraph.nodes.size();
        int trig = 0;
        for (const FlowNode& n : o.flowGraph.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (t && t->trigger && trig++ == 0) r.trigger = t->title;
            if (dbgHasBreakpoint(o.id, n.id)) r.bp = true;
        }
        if (trig > 1) r.trigger += " +" + std::to_string(trig - 1);
        graphRows.push_back(std::move(r));
    }
    if (dbgLive) {
        std::map<std::string, size_t> rowOf;
        for (size_t r = 0; r < graphRows.size(); ++r)
            rowOf[project_.objects()[graphRows[r].index].id] = r;
        for (const livedbg::NodeSym& ns : dbgSyms_.nodes) {
            if (ns.scene != project_.activeScene) continue;
            auto it = rowOf.find(ns.objectId);
            if (it == rowOf.end()) continue;
            GraphRow& r = graphRows[it->second];
            r.heat = std::min(r.heat, dbgNodeHeat(ns.key));
            if (ns.key >= 0 && ns.key < (int)dbgSnap_.hits.size()) r.hits += dbgSnap_.hits[ns.key];
        }
        // Live: the graph that just did something is the one you are after.
        std::stable_sort(graphRows.begin(), graphRows.end(),
                         [](const GraphRow& a, const GraphRow& b) {
                             if (a.heat != b.heat) return a.heat < b.heat;
                             return a.hits > b.hits;
                         });
    }
    struct Problem {
        int obj;
        int node;
        const char* what;
    };
    std::vector<Problem> problems;
    for (int i = 0; i < objCount; ++i)
        for (const FlowNode& n : project_.objects()[i].flowGraph.nodes)
            if (const char* w = flowNodeProblem(project_, n)) problems.push_back({i, n.id, w});

    SceneObject& owner = project_.objects()[flowGraphObject_];
    FlowGraph& fg = owner.flowGraph;
    bool changed = false;

    // Live Logic for THIS graph (livelogic::capability), cached per edit.
    {
        const int capKey = flowGraphObject_ + project_.activeScene * 1000003;
        if (flowCapObj_ != capKey || flowCapSerial_ != modelEditSerial_) {
            const livelogic::Capability cap =
                livelogic::capability(project_, project_.active(), fg);
            flowCapPatchable_ = cap.patchable;
            flowCapReasons_ = cap.reasons;
            flowCapObj_ = capKey;
            flowCapSerial_ = modelEditSerial_;
        }
    }
    int unsupportedNodes = 0;
    for (const FlowNode& n : fg.nodes)
        if (!flowLiveSupported(n.type)) ++unsupportedNodes;

    // --- header --------------------------------------------------------------
    ui::PanelInlineTitle("Flow Graph");  // only while alone in its dock node (no tab)
    // Graph switcher: the owner's name, big, opening a filterable list. Named
    // "Graph of" - the --ui-script name the old combo had.
    {
        const float fs = ImGui::GetFontSize();
        const float h = ImGui::GetFrameHeight();
        ImVec2 ts;
        {
            ui::FontScope bold(ui::boldFont());
            ts = ImGui::CalcTextSize(owner.name.c_str());
        }
        const float w = ts.x + fs * 3.4f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("Graph of", ImVec2(w, h))) {
            flowSwitchFilter_.clear();
            ImGui::OpenPopup("##graph_switch");
        }
        const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("The object whose graph this is - click to switch.");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 q(p.x + w, p.y + h);
        const float r = ImGui::GetStyle().FrameRounding;
        dl->AddRectFilled(p, q, ImGui::GetColorU32(fgMix(sem.field, sem.surface, 0.5f + 0.5f * t)), r);
        dl->AddRect(p, q, ImGui::GetColorU32(sem.border), r);
        ui::DrawGlyph(dl, ui::Glyph::Nodes, ImVec2(p.x + fs * 0.9f, p.y + h * 0.5f), fs * 0.8f,
                      ImGui::GetColorU32(sem.textDim));
        {
            ui::FontScope bold(ui::boldFont());
            dl->AddText(ImVec2(p.x + fs * 1.7f, p.y + (h - ts.y) * 0.5f),
                        ImGui::GetColorU32(sem.text), owner.name.c_str());
        }
        const float cx = q.x - fs * 0.85f, cy = p.y + h * 0.5f;
        dl->AddTriangleFilled(ImVec2(cx - fs * 0.25f, cy - fs * 0.1f),
                              ImVec2(cx + fs * 0.25f, cy - fs * 0.1f),
                              ImVec2(cx, cy + fs * 0.18f), ImGui::GetColorU32(sem.textDim));
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(260.0f), 0.0f),
                                        ImVec2(scaled(420.0f), scaled(440.0f)));
    if (ImGui::BeginPopup("##graph_switch")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        char fbuf[96];
        std::snprintf(fbuf, sizeof(fbuf), "%s", flowSwitchFilter_.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##gsfilter", "Filter objects...", fbuf, sizeof(fbuf)))
            flowSwitchFilter_ = fbuf;
        std::string needle = flowSwitchFilter_;
        for (char& c : needle) c = (char)std::tolower((unsigned char)c);
        auto matches = [&](const std::string& name) {
            if (needle.empty()) return true;
            std::string l = name;
            for (char& c : l) c = (char)std::tolower((unsigned char)c);
            return l.find(needle) != std::string::npos;
        };
        char cnt[16];
        std::snprintf(cnt, sizeof(cnt), "%d", (int)graphRows.size());
        ui::Caption("With logic", cnt);
        std::vector<char> hasLogic(objCount, 0);
        for (const GraphRow& r : graphRows) hasLogic[r.index] = 1;
        for (int i = 0; i < objCount; ++i) {
            if (!hasLogic[i] || !matches(project_.objects()[i].name)) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(project_.objects()[i].name.c_str(), i == flowGraphObject_)) {
                switchTo = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ui::Caption("Without logic");
        int shown = 0;
        for (int i = 0; i < objCount; ++i) {
            if (hasLogic[i] || !matches(project_.objects()[i].name)) continue;
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            if (ImGui::Selectable(project_.objects()[i].name.c_str(), i == flowGraphObject_)) {
                switchTo = i;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
            ImGui::PopID();
            ++shown;
        }
        if (shown == 0) ImGui::TextDisabled("None");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    {
        // While halted the same toggle follows the graph that stopped.
        const char* lbl = dbgHalted ? "Follows the halt###Follows selection"
                                    : "Follows selection###Follows selection";
        if (ui::FilterChip(lbl, flowFollowSel_)) {
            flowFollowSel_ = !flowFollowSel_;
            flowFollowedSel_ = flowFollowSel_ ? -2 : selectedObject_;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(dbgHalted
                                  ? "On: the graph that stopped the game opens by itself."
                                  : "On: selecting an object in the viewport or the Outliner\n"
                                    "opens its graph. Off: the graph stays where you put it.");
    }
    ImGui::SameLine();
    {
        ui::FontScope mono(ui::monoFont());
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d node%s \xc2\xb7 %d link%s", (int)fg.nodes.size(),
                            fg.nodes.size() == 1 ? "" : "s", (int)fg.links.size(),
                            fg.links.size() == 1 ? "" : "s");
    }
    // Right end: Live Logic and problems as status pills, then the AI and the
    // custom-node actions as quiet buttons.
    {
        char liveLbl[96] = "";
        int liveKind = 4;
        if (liveLogicState_ == LogicState::Off) {
            std::snprintf(liveLbl, sizeof(liveLbl), "Live Logic off###fg_livelogic");
        } else if (!fg.nodes.empty()) {
            if (flowCapPatchable_) {
                std::snprintf(liveLbl, sizeof(liveLbl), "Edits go live###fg_livelogic");
                liveKind = 0;
            } else {
                if (unsupportedNodes > 0)
                    std::snprintf(liveLbl, sizeof(liveLbl),
                                  "Edits need a build \xc2\xb7 %d node%s###fg_livelogic",
                                  unsupportedNodes, unsupportedNodes == 1 ? "" : "s");
                else
                    std::snprintf(liveLbl, sizeof(liveLbl), "Edits need a build###fg_livelogic");
                liveKind = 1;
            }
        }
        char probLbl[64];
        std::snprintf(probLbl, sizeof(probLbl), "%d problem%s###fg_problems", (int)problems.size(),
                      problems.size() == 1 ? "" : "s");
        const ImGuiStyle& st = ImGui::GetStyle();
        const float genW = ImGui::CalcTextSize("Generate").x + st.FramePadding.x * 2.6f;
        const float total = (liveLbl[0] ? ui::StatusPillWidth(liveLbl) + st.ItemSpacing.x : 0.0f) +
                            ui::StatusPillWidth(probLbl) + st.ItemSpacing.x + genW +
                            st.ItemSpacing.x + ImGui::GetFrameHeight();
        ImGui::SameLine();
        const float right = ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetCursorPosX() < right - total) ImGui::SetCursorPosX(right - total);
        if (liveLbl[0]) {
            ui::StatusPill(liveLbl, liveKind);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(scaled(360.0f));
                if (liveLogicState_ == LogicState::Off)
                    ImGui::TextUnformatted(
                        "Live Logic is off (Project Preferences > Build & debug): every graph edit "
                        "needs a build to reach the game.");
                else if (flowCapPatchable_)
                    ImGui::TextUnformatted(
                        "Live Logic can hot-patch this graph: edits reach a running debug "
                        "build within a few frames.");
                else {
                    ImGui::TextUnformatted(
                        "Live Logic cannot hot-patch this graph - edits apply on the next "
                        "Run (F5). Because of:");
                    for (const std::string& r : flowCapReasons_) ImGui::BulletText("%s", r.c_str());
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
        }
        if (ui::StatusPill(probLbl, problems.empty() ? 0 : 2))
            ImGui::OpenPopup("##fg_problems_list");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Nodes in this scene's graphs that compile out or point at\n"
                              "something that no longer exists. Click for the list.");
        ImGui::SameLine();
        // AI generation: describe the logic, get a graph (aigen.hpp). The modal
        // pins the target object now - selection changes must not retarget an
        // in-flight request.
        if (ui::GhostButton("Generate")) {
            aiGenTargetObject_ = flowGraphObject_;
            aiGenError_.clear();
            aiGenWarnings_.clear();
            openAiGeneratePopup_ = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Generate with AI: describe the behaviour, get a graph.");
        ImGui::SameLine();
        // Project-defined custom nodes: reload the flow-nodes/ folder, scaffold
        // a starter file, or open the project in VS Code (jumping to the C++
        // bodies or a specific node file). See docs/custom-flow-nodes.md.
        if (ui::GlyphButton("Custom nodes", ui::Glyph::Code, 0.0f,
                            "Custom nodes: reload flow-nodes/, scaffold one, open in VS Code"))
            ImGui::OpenPopup("##customnodes");
    }
    if (ImGui::BeginPopup("##customnodes")) {
        ImGui::TextDisabled("%d loaded from flow-nodes/", (int)customFlowNodes().size());
        ImGui::Separator();
        if (ImGui::MenuItem("Reload from folder")) {
            const std::string msg = flownode::loadForProject(project_.dir);
            statusMessage_ =
                msg.empty() ? "No custom nodes found in flow-nodes/" : msg;
        }
        if (ImGui::MenuItem("New starter node (example.flownode)")) {
            const std::string path = flownode::writeExample(project_.dir);
            if (path.rfind("error:", 0) == 0) {
                statusMessage_ = path;
            } else {
                flownode::loadForProject(project_.dir);
                statusMessage_ = "Wrote " + path + " - edit it, then Reload";
            }
        }
        ImGui::Separator();
        // Open in VS Code, in the whole-project context. The C++ bodies file is
        // the natural landing spot for `call = fn` nodes; the submenu jumps to
        // an individual .flownode definition.
        if (ImGui::MenuItem("Open in VS Code (flow_nodes.hpp)"))
            openInVSCode("inc\\scripts\\flow_nodes.hpp");
        if (ImGui::BeginMenu("Jump to node file", !customFlowNodes().empty())) {
            for (const auto& c : customFlowNodes())
                if (ImGui::MenuItem(c->title.c_str())) openInVSCode(c->sourceFile);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Syntax highlighting + validation for .flownode/.screenfx files. Opening
        // a project already installs it; this is the manual/refresh entry point.
        if (ImGui::MenuItem("Install VS Code extension"))
            statusMessage_ = installVsCodeExtension();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Install the Tyra extension (.flownode/.screenfx highlighting)\n"
                "into VS Code. Reload the VS Code window afterwards.");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##fg_problems_list")) {
        if (problems.empty()) ImGui::TextDisabled("No problems in this scene's graphs.");
        for (size_t k = 0; k < problems.size(); ++k) {
            const Problem& pr = problems[k];
            const SceneObject& o = project_.objects()[pr.obj];
            const char* title = "?";
            for (const FlowNode& n : o.flowGraph.nodes)
                if (n.id == pr.node)
                    if (const FlowNodeType* t = flowNodeType(n.type)) title = t->title;
            ImGui::PushID((int)k);
            const std::string lbl = o.name + " \xc2\xb7 " + title;
            if (ImGui::Selectable(lbl.c_str())) {
                switchTo = pr.obj;
                flowSelectReq_ = pr.node;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
            ImGui::TextUnformatted(pr.what);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }


    // Drop links whose pins no longer exist (nodes deleted from the registry
    // or outputs removed in newer editor versions) - imnodes must never be
    // handed a link to a pin that was not submitted.
    {
        auto typeOf = [&](int nodeId) -> const FlowNodeType* {
            for (const FlowNode& n : fg.nodes)
                if (n.id == nodeId) return flowNodeType(n.type);
            return nullptr;
        };
        for (size_t i = fg.links.size(); i-- > 0;) {
            const FlowLink& l = fg.links[i];
            const FlowNodeType* from = typeOf(l.fromNode);
            const FlowNodeType* to = typeOf(l.toNode);
            bool ok = from && to;
            if (ok) {
                switch (l.kind) {
                    case FlowLinkExec:
                        // Both ends must still HAVE the pin the link names: a
                        // node type that lost an output (or a graph saved with a
                        // wider one) would otherwise hand imnodes a pin that was
                        // never submitted.
                        ok = l.fromPin >= 0 &&
                             l.fromPin < flowExecOutCount(*from) &&
                             l.toPin >= 0 && !to->trigger && !to->pure &&
                             l.toPin < (to->execInCount < 1 ? 1 : to->execInCount);
                        break;
                    case FlowLinkObject: ok = from->idOut && to->idIn; break;
                    case FlowLinkPos: ok = from->posOut && to->posIn; break;
                    case FlowLinkBool: ok = from->boolOut && to->boolIn; break;
                    case FlowLinkText: ok = from->textOut && to->textIn; break;
                    case FlowLinkNum: ok = from->numOut && to->numIn; break;
                    default: ok = false; break;
                }
            }
            if (!ok) {
                fg.links.erase(fg.links.begin() + i);
                changed = true;
            }
        }
    }

    // Which object a node's target resolves to in the editor, mirroring the
    // codegen order: incoming object link chain > explicit name > the graph
    // owner ("self"). Used by the Animation node's clip picker.
    auto uiResolveTarget = [&](const FlowNode& start) -> int {
        const FlowNode* cur = &start;
        std::vector<int> visited;
        for (;;) {
            bool seen = false;
            for (int id : visited) seen |= (id == cur->id);
            if (seen) break;  // cycle guard
            visited.push_back(cur->id);
            const FlowNodeType* ct = flowNodeType(cur->type);
            if (!ct || !ct->idIn) break;
            const FlowNode* src = nullptr;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkObject || l.toNode != cur->id) continue;
                for (const FlowNode& m : fg.nodes)
                    if (m.id == l.fromNode) src = &m;
                break;
            }
            if (!src) break;
            cur = src;
        }
        const FlowNodeType* ct = flowNodeType(cur->type);
        if (ct && ct->strKind == FlowParamKind::ObjectName && !cur->str.empty()) {
            for (int i = 0; i < (int)project_.objects().size(); ++i)
                if (project_.objects()[i].name == cur->str) return i;
            return -1;
        }
        return flowGraphObject_;  // self
    };

    // --- body: graphs | canvas | inspector ------------------------------------
    const float thick = ImMax(6.0f, ImGui::GetFontSize() * 0.45f);  // the toolkit splitter's
    const float bodyH = ImMax(1.0f, ImGui::GetContentRegionAvail().y);
    const float totalW = ImGui::GetContentRegionAvail().x;
    if (flowListW_ <= 0.0f) flowListW_ = scaled(220.0f);
    if (flowInspW_ <= 0.0f) flowInspW_ = scaled(270.0f);
    // Clamp what this frame draws, not the stored widths: a docked window's
    // first frames are only a few px wide, and writing the clamp back crushed
    // both columns to their minimum for good (names cut to "instrument").
    float inspW = ImClamp(flowInspW_, scaled(220.0f), ImMax(scaled(220.0f), totalW * 0.4f));
    float listW = ImClamp(flowListW_, scaled(150.0f), ImMax(scaled(150.0f), totalW * 0.3f));
    // The canvas comes first: when it would get under half the window (or
    // 300 px) the list and then the inspector give way down to their minimums,
    // and only under 300 px does the graph list fold away.
    {
        float short_ = ImMax(scaled(300.0f), totalW * 0.5f) - (totalW - listW - inspW - 2.0f * thick);
        if (short_ > 0.0f) {
            const float l = ImMin(short_, listW - scaled(150.0f));
            listW -= l;
            short_ -= l;
        }
        if (short_ > 0.0f) inspW -= ImMin(short_, inspW - scaled(220.0f));
    }
    const bool showList = totalW - listW - inspW - 2.0f * thick >= scaled(300.0f);

    // --- left: the scene's graphs ----------------------------------------------
    if (showList) {
        ImGui::BeginChild("##fg_graphs", ImVec2(listW, bodyH));
        {
            char cnt[32];
            std::snprintf(cnt, sizeof(cnt), "%d of %d", (int)graphRows.size(), objCount);
            const std::string cap = "Graphs in " + project_.active().name;
            ui::Caption(cap.c_str(), dbgLive ? "by activity" : cnt);
            char fbuf[96];
            std::snprintf(fbuf, sizeof(fbuf), "%s", flowListFilter_.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##fg_graph_filter", "Filter graphs...", fbuf, sizeof(fbuf)))
                flowListFilter_ = fbuf;
            std::string needle = flowListFilter_;
            for (char& c : needle) c = (char)std::tolower((unsigned char)c);
            auto matches = [&](const std::string& name) {
                if (needle.empty()) return true;
                std::string l = name;
                for (char& c : l) c = (char)std::tolower((unsigned char)c);
                return l.find(needle) != std::string::npos;
            };
            const float fs = ImGui::GetFontSize();
            const float footerH =
                ImGui::CalcTextSize("69 objects have no logic. Select one in the viewport or "
                                    "Outliner to start its graph.",
                                    nullptr, false, ImGui::GetContentRegionAvail().x)
                    .y +
                ImGui::GetFrameHeightWithSpacing() * 2.0f + fs * 0.5f;
            ImGui::BeginChild("##fg_graph_rows", ImVec2(0, -footerH));
            // One row: activity dot, name, trigger, count; selection fill.
            auto graphRow = [&](int i, const std::string& sub, const std::string& right,
                                ImU32 dot, bool bp, bool dim) {
                const SceneObject& o = project_.objects()[i];
                const float h = ImGui::GetFrameHeight();
                const float w = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::PushID(i);
                if (ImGui::InvisibleButton(o.name.c_str(), ImVec2(w, h))) switchTo = i;
                const bool sel = i == flowGraphObject_;
                uiscript::markLastItemChecked(sel);
                const float t = theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
                ImGui::PopID();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 q(p.x + w, p.y + h);
                const float r = ImGui::GetStyle().FrameRounding;
                if (sel) {
                    dl->AddRectFilled(p, q, ImGui::GetColorU32(fgMix(sem.surface, sem.accent, 0.16f)), r);
                    dl->AddRect(p, q, ImGui::GetColorU32(ImVec4(sem.accent.x, sem.accent.y, sem.accent.z, 0.35f)), r);
                } else if (t > 0.0f) {
                    dl->AddRectFilled(p, q, ImGui::GetColorU32(ImVec4(sem.surface.x, sem.surface.y, sem.surface.z, 0.8f * t)), r);
                }
                const float cy = p.y + h * 0.5f;
                dl->AddCircleFilled(ImVec2(p.x + fs * 0.55f, cy), fs * 0.16f,
                                    dot ? dot : ImGui::GetColorU32(ImVec4(sem.textDim.x, sem.textDim.y, sem.textDim.z, 0.45f)));
                float rx = q.x - fs * 0.4f;
                if (!right.empty()) {
                    ui::FontScope mono(ui::monoFont());
                    const float rw = ImGui::CalcTextSize(right.c_str()).x;
                    rx -= rw;
                    dl->AddText(ImVec2(rx, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                                ImGui::GetColorU32(sem.textDim), right.c_str());
                    rx -= fs * 0.6f;
                }
                if (bp) {
                    dl->AddCircleFilled(ImVec2(rx - fs * 0.2f, cy), fs * 0.2f, ImGui::GetColorU32(sem.danger));
                    rx -= fs * 0.7f;
                }
                if (!sub.empty()) {
                    const float sw = ImGui::CalcTextSize(sub.c_str()).x;
                    // The trigger yields to the name: "instrument-console" over
                    // "instrument  On Used".
                    const float nameEnd = p.x + fs * 1.1f + ImGui::CalcTextSize(o.name.c_str()).x;
                    if (rx - sw - fs * 0.5f >= nameEnd) {
                        rx -= sw;
                        dl->AddText(ImVec2(rx, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                                    ImGui::GetColorU32(sem.textDim), sub.c_str());
                        rx -= fs * 0.5f;
                    }
                }
                {
                    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
                    ImGui::PushStyleColor(ImGuiCol_Text, dim ? sem.textDim : sem.text);
                    ImGui::RenderTextEllipsis(dl, ImVec2(p.x + fs * 1.1f, ty),
                                              ImVec2(rx, ty + ImGui::GetTextLineHeight()), rx,
                                              o.name.c_str(), nullptr, nullptr);
                    ImGui::PopStyleColor();
                }
            };
            for (const GraphRow& r : graphRows) {
                if (!matches(project_.objects()[r.index].name)) continue;
                ImU32 dot = 0;
                std::string right = std::to_string(r.nodes);
                if (dbgLive) {
                    if (r.heat < 1.0f) dot = ImGui::GetColorU32(sem.warn);
                    right = "\xc3\x97" + std::to_string(r.hits);
                }
                graphRow(r.index, dbgLive ? std::string() : r.trigger, right, dot, r.bp, false);
            }
            if (graphRows.empty() && needle.empty())
                ImGui::TextDisabled("No object has logic yet.");
            if (flowListAll_) {
                ui::Caption("Without logic");
                std::vector<char> hasLogic(objCount, 0);
                for (const GraphRow& r : graphRows) hasLogic[r.index] = 1;
                for (int i = 0; i < objCount; ++i)
                    if (!hasLogic[i] && matches(project_.objects()[i].name))
                        graphRow(i, std::string(), std::string(), 0, false, true);
            }
            ImGui::EndChild();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            if (dbgLive)
                ImGui::TextWrapped("Dot = ran in the last second. Number = node fires since boot.");
            else
                ImGui::TextWrapped("%d object%s no logic. Select one in the viewport or Outliner "
                                   "to start its graph.",
                                   objCount - (int)graphRows.size(),
                                   objCount - (int)graphRows.size() == 1 ? " has" : "s have");
            ImGui::PopStyleColor();
            ImGui::PopTextWrapPos();
            // Short enough for the list's 150 px minimum (the long wording
            // clipped there); the tooltip keeps the full meaning.
            ui::Switch("Show all objects", &flowListAll_);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Also list the objects without logic, under \"Without logic\".");
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 0.0f);
        if (ui::VSplitter("##fg_split_list", &listW, scaled(150.0f), scaled(420.0f), bodyH))
            flowListW_ = listW;
    }
    const float canvasW = ImMax(scaled(200.0f), totalW - (showList ? listW + thick : 0.0f) -
                                                     inspW - thick);
    std::vector<int> selNodes;  // filled after the canvas draws; read by the inspector

    // --- centre: the canvas ------------------------------------------------------
    ImGui::BeginChild("##fg_canvas", ImVec2(canvasW, bodyH), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // imnodes has no native zoom: emulate it by scaling the font, the style
    // metrics and the grid-space node positions by flowZoom_. EVERY length that
    // contributes to a node's size has to carry the same factor, or the canvas
    // stops being self-similar: node positions scale while their contents do
    // not, so the nodes visibly drift apart / pile up as you zoom.
    //
    // The font MUST go through PushFont, not SetWindowFontScale. imnodes runs
    // its canvas in a child window (BeginChild("scrolling_region")) and since
    // ImGui 1.92 a per-window font scale is NOT inherited by children -
    // UpdateCurrentFontSize() reads window->FontWindowScale only, and the
    // FontWindowScaleParents it computes for children is dead weight. So the
    // node text silently stayed at 100% at every zoom level while the metrics
    // around it shrank. PushFont sets the context-level FontSizeBase, which
    // child windows do inherit.
    //
    // uiScaleApplied_ rides along wherever ImGui's own ScaleAllSizes() does not
    // reach - the pixel literals here and the whole of ImNodesStyle, which the
    // editor's UI scale never touched (at 250% the node padding, pin radii and
    // grid were pixel-for-pixel their 100% values next to a 2.5x font).
    //
    // Finally, the zoom is SNAPPED so the node font lands on a whole pixel.
    // ImGui rounds every font size (GetRoundedFontSize), so text width is a
    // staircase in the zoom while everything else is a straight line - and a
    // node whose width steps while its position slides is exactly the "the
    // nodes move relative to each other" symptom. Snapping makes the text the
    // thing every other length is derived FROM, so the canvas is self-similar
    // at every zoom level instead of only approximately.
    const float uiFontSize = ImMax(1.0f, ImGui::GetStyle().FontSizeBase);
    // Everything ImGui multiplies a pushed font size by before rounding it.
    const float fontMul = ImMax(0.01f, ImGui::GetStyle().FontScaleMain *
                                           ImGui::GetStyle().FontScaleDpi);
    const float uiFontPx = ImMax(1.0f, IM_ROUND(uiFontSize * fontMul));  // chrome
    const float nodeFontPx = ImMax(1.0f, IM_ROUND(uiFontPx * flowZoom_));
    const float zoom = nodeFontPx / uiFontPx;  // the zoom the text actually got
    const float nodeScale = nodeFontPx / uiFontSize;  // vs the authored 1.0 design


    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::PushFont(nullptr, nodeFontPx / fontMul);
    const ImGuiStyle& gstyle = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(gstyle.ItemSpacing.x * zoom, gstyle.ItemSpacing.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                        ImVec2(gstyle.ItemInnerSpacing.x * zoom,
                               gstyle.ItemInnerSpacing.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(gstyle.FramePadding.x * zoom, gstyle.FramePadding.y * zoom));
    ImNodesStyle& nstyle = ImNodes::GetStyle();
    const ImNodesStyle savedStyle = nstyle;
    nstyle.GridSpacing *= nodeScale;
    nstyle.NodeCornerRounding *= nodeScale;
    nstyle.NodePadding = ImVec2(savedStyle.NodePadding.x * nodeScale,
                                savedStyle.NodePadding.y * nodeScale);
    nstyle.NodeBorderThickness *= nodeScale;
    nstyle.LinkThickness *= nodeScale;
    nstyle.LinkHoverDistance *= nodeScale;
    nstyle.PinCircleRadius *= nodeScale;
    nstyle.PinQuadSideLength *= nodeScale;
    nstyle.PinTriangleSideLength *= nodeScale;
    nstyle.PinLineThickness *= nodeScale;
    nstyle.PinHoverRadius *= nodeScale;
    nstyle.PinOffset *= nodeScale;
    // The mini-map is a fixed overlay, not part of the zoomed canvas: it takes
    // the UI scale but never the zoom.
    nstyle.MiniMapPadding = ImVec2(savedStyle.MiniMapPadding.x * uiScaleApplied_,
                                   savedStyle.MiniMapPadding.y * uiScaleApplied_);
    nstyle.MiniMapOffset = ImVec2(savedStyle.MiniMapOffset.x * uiScaleApplied_,
                                  savedStyle.MiniMapOffset.y * uiScaleApplied_);

    // The param-widget column: the one width every node is built around, so it
    // carries both scales like the font does.
    const float paramWidth = 130.0f * nodeScale;
    // A dropdown list is a popup window sitting OUTSIDE the canvas, so it keeps
    // the UI font - a node at 40% zoom is meant to be unreadable, its menus are
    // not. Mirrors of ImGui::BeginCombo/EndCombo used by every node param.
    // Hover help for the parameter widget just submitted: FlowNodeType's
    // strTip / str2Tip / numTips / execInTips. Hovering a NODE describes the node,
    // hovering a KNOB describes that knob - which is the whole point, since the
    // reader looking at "Seed" is not asking what a procedural volume is. Like
    // a dropdown list, a tooltip is a window outside the canvas, so it keeps
    // the UI font: a node at 40% zoom is meant to be unreadable, its help is
    // not. ImGuiHoveredFlags_ForTooltip carries ImGui's own hover delay, so
    // these match the rest of the editor and cannot fire mid-drag.
    // Set when a parameter tip is actually drawn this frame, so the node-level
    // doc tooltip below stands down. Both conditions are true at once whenever
    // the cursor rests on a documented widget (the node IS hovered), and two
    // ImGui tooltips in one frame are drawn on top of each other - the specific
    // answer has to win over the general one.
    bool paramTipShown = false;
    auto paramTip = [&](const char* tip) {
        if (!tip || !*tip) return;
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) return;
        paramTipShown = true;
        ImGui::BeginTooltip();
        ImGui::PushFont(nullptr, uiFontSize);
        ImGui::PushTextWrapPos(scaled(320.0f));
        ImGui::TextUnformatted(tip);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndTooltip();
        // NOTE for new call sites: a tooltip is a WINDOW, and ImGui's
        // "last item" is context-global - drawing one clobbers it. So this must
        // come AFTER every IsItem* query on the widget it documents
        // (IsItemDeactivatedAfterEdit is how most of these commit), or the edit
        // silently stops being saved the moment the cursor rests on it.
    };
    // The tip is checked only while the list is CLOSED: an open dropdown means
    // the cursor is already choosing, and the "last item" inside a popup window
    // is whatever was drawn in it, not the combo.
    auto beginCombo = [&](const char* label, const char* preview,
                          const char* tip = nullptr) {
        if (!ui::BeginCombo(label, preview)) {
            paramTip(tip);
            return false;
        }
        ImGui::PushFont(nullptr, uiFontSize);
        return true;
    };
    auto endCombo = [&]() {
        ImGui::PopFont();
        ImGui::EndCombo();
    };

    // Node content width (params + right-aligned pin labels share it)
    const float nodeWidth = paramWidth + ImGui::GetStyle().ItemInnerSpacing.x +
                            ImGui::CalcTextSize("Object").x;
    // Right-aligns a pin label to the node edge. The ">" arrow is drawn as
    // its own item in a FIXED column (same x for every row) instead of being
    // part of a width-dependent indented string: text rendering truncates
    // the pen start to whole pixels, so a per-label fractional indent made
    // the arrows land on different pixels (visibly ragged at some DPI/zoom
    // combinations). A constant arrow column cannot drift by construction.
    auto rightLabel = [&](const char* txt, bool disabled) {
        const float left = ImGui::GetCursorPosX();
        const float arrowX = left + nodeWidth - ImGui::CalcTextSize(">").x;
        const float textX =
            arrowX - ImGui::CalcTextSize(" ").x - ImGui::CalcTextSize(txt).x;
        if (textX > left) ImGui::SetCursorPosX(textX);
        if (disabled)
            ImGui::TextDisabled("%s", txt);
        else
            ImGui::TextUnformatted(txt);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(arrowX);
        if (disabled)
            ImGui::TextDisabled(">");
        else
            ImGui::TextUnformatted(">");
    };

    // --- Live Debugger overlay state (docs/live-debugger.md) ---------------
    // With a game reporting, this graph is drawn as a live instrument: node
    // titles glow as they run, exec links light up behind them, hit counters
    // sit in the corners and breakpoints show as red dots. Rewinding in the
    // Debugger's timeline replaces "recently" with "in the frame you are
    // looking at", so the same drawing code replays history.
    const bool dbgOverlay =
        (dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted) &&
        flowGraphObject_ >= 0 && flowGraphObject_ < (int)project_.objects().size();
    const std::string dbgOwnerId =
        dbgOverlay ? project_.objects()[flowGraphObject_].id : std::string();
    // Rewound? Then only the nodes of that one frame count as "just fired".
    std::vector<int> dbgScrubKeys;
    if (dbgOverlay && dbgScrub_ >= 0) {
        const auto& frames = dbgTimeline_.frames();
        if (dbgScrub_ < (int)frames.size()) dbgScrubKeys = frames[dbgScrub_].keys;
    }
    const bool dbgRewound = dbgOverlay && dbgScrub_ >= 0;
    auto dbgKeyOfNode = [&](int nodeId) {
        return dbgOverlay ? dbgKeyFor(project_.activeScene, dbgOwnerId, nodeId) : -1;
    };
    // 0 = firing right now, 1 = cold. Fades over kDbgFade seconds so a node
    // that fires every few frames stays lit instead of strobing.
    const float kDbgFade = 0.6f;
    auto dbgGlow = [&](int key) -> float {
        if (key < 0) return 0.0f;
        if (dbgRewound) {
            for (int k : dbgScrubKeys)
                if (k == key) return 1.0f;
            return 0.0f;
        }
        const float age = dbgNodeHeat(key);
        return age >= kDbgFade ? 0.0f : 1.0f - age / kDbgFade;
    };
    const ImVec2 dbgCanvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 dbgCanvasSize = ImGui::GetContentRegionAvail();

    // Every parameter of one node, as editable widgets. Drawn by the node
    // INSPECTOR (right column) - the nodes on the canvas show compact value
    // chips instead (chipsFor below). The ui:: wrappers lay each field out as
    // an inspector row; outside a row scope they are the stock widgets.
    auto drawNodeParams = [&](FlowNode& n, const FlowNodeType* t) {
        bool posLinked = false;
        for (const FlowLink& l : fg.links)
            posLinked |= (l.kind == FlowLinkPos && l.toNode == n.id);

        // string param
        if (t->strKind == FlowParamKind::ObjectName) {
            bool idLinked = false;
            for (const FlowLink& l : fg.links)
                idLinked |= (l.kind == FlowLinkObject && l.toNode == n.id);
            if (idLinked) {
                ImGui::TextDisabled("Object: from id link");
            } else {
                const char* current = n.str.empty() ? "(self)" : n.str.c_str();
                if (beginCombo("Object", current, t->strTip)) {
                    if (ImGui::Selectable("(self)", n.str.empty())) {
                        n.str.clear();
                        changed = true;
                    }
                    for (const SceneObject& o : project_.objects()) {
                        if (ImGui::Selectable(o.name.c_str(), o.name == n.str)) {
                            n.str = o.name;
                            changed = true;
                        }
                    }
                    endCombo();
                }
                if (ImGui::SmallButton("From selected") && selectedObject_ >= 0 &&
                    selectedObject_ < (int)project_.objects().size()) {
                    n.str = project_.objects()[selectedObject_].name;
                    changed = true;
                }
            }
        } else if (t->strKind == FlowParamKind::Button) {
            // every PadButtons field (pad.hpp, kPadButtonNames) - the codegen
            // uses the name as-is. The picker is the shared controller one.
            std::string v = n.str.empty() ? "Cross" : n.str;
            if (ui::PadButtonCombo("Button", &v, false, t->strTip, nullptr, uiFontSize)) {
                n.str = v;
                changed = true;
            }
        } else if (t->strKind == FlowParamKind::InputActionName) {
            // Configurable input (Tools > Input Map): the action, not a button.
            const char* current = n.str.empty() ? "(pick an action)" : n.str.c_str();
            if (beginCombo("Action", current, t->strTip)) {
                for (const InputAction& a : project_.input.actions) {
                    const std::string label =
                        a.label.empty() ? a.name : a.label + "  (" + a.name + ")";
                    if (ImGui::Selectable(label.c_str(), a.name == n.str)) {
                        n.str = a.name;
                        changed = true;
                    }
                }
                endCombo();
            }
            if (!n.str.empty() && !project_.input.findAction(n.str)) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                                   "unknown action - the trigger never fires");
            } else if (const InputAction* a = project_.input.findAction(n.str)) {
                ImGui::TextDisabled("bound to %s",
                                    inputBindingLabel(project_.input.resolve(a->name))
                                        .c_str());
            }
        } else if (t->strKind == FlowParamKind::SaveSlotMode) {
            const std::vector<SaveSlotModeInfo>& modes = saveSlotModes();
            size_t cur = 0;  // "" reads as fixed, so an old graph is unchanged
            for (size_t i = 0; i < modes.size(); ++i)
                if (n.str == modes[i].key) cur = i;
            if (beginCombo("Writes to", modes[cur].label, t->strTip)) {
                for (size_t i = 0; i < modes.size(); ++i) {
                    if (ImGui::Selectable(modes[i].label, i == cur)) {
                        n.str = modes[i].key;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", modes[i].desc);
                }
                endCombo();
            }
            // A node body is not the place for a paragraph - the combo's own
            // items carry these descriptions on hover. What DOES stay in the
            // body is the one thing that is wrong right now: an autosave mode
            // with no autosave slot set is a silent no-op in game.
            if (n.str == "autosave" && project_.saveAutosaveSlot < 0)
                ImGui::TextColored(theme::semantics().warn,
                                   "No autosave slot set (Tools > Save Editor)");
        } else if (t->strKind == FlowParamKind::ReverbPreset) {
            const std::vector<ReverbPresetInfo>& presets = reverbPresets();
            const int cur = reverbPresetIndex(n.str);
            if (beginCombo("Preset", presets[cur].label, t->strTip)) {
                for (int i = 0; i < (int)presets.size(); ++i) {
                    if (ImGui::Selectable(presets[i].label, i == cur)) {
                        n.str = presets[i].key;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", presets[i].desc);
                }
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::KeyName) {
            if (beginCombo("Key",
                           n.str.empty() ? "(pick a key)" : n.str.c_str(),
                           t->strTip)) {
                for (const InputKeyName& k : inputKeyNames())
                    if (ImGui::Selectable(k.label, n.str == k.label)) {
                        n.str = k.label;
                        changed = true;
                    }
                endCombo();
            }
            ImGui::TextDisabled("USB keyboard (Preferences > Build & debug)");
        } else if (n.type == "SetInputPreset") {
            const char* current = n.str.empty() ? "(pick a preset)" : n.str.c_str();
            if (beginCombo("Preset", current, t->strTip)) {
                for (const InputPreset& pr : project_.input.presets)
                    if (ImGui::Selectable(pr.name.c_str(), pr.name == n.str)) {
                        n.str = pr.name;
                        changed = true;
                    }
                endCombo();
            }
        } else if (n.type == "Animation") {
            // Clip picker when the resolved target is an animated .glb model
            // (explicit object wired/named, or self); free text otherwise.
            const int target = uiResolveTarget(n);
            bool picker = false;
            if (target >= 0 && target < (int)project_.objects().size() &&
                isAnimatedModelPath(project_.objects()[target].modelPath)) {
                const std::string& mp = project_.objects()[target].modelPath;
                const GlbInfo& info = glbInfo(mp);
                const std::vector<std::string> clips = effectiveClips(mp);
                if (info.ok && !clips.empty()) {
                    picker = true;
                    const std::string label =
                        n.str.empty() ? clips.front() + " (first)" : n.str;
                    if (beginCombo("Clip", label.c_str(), t->strTip)) {
                        for (const std::string& c : clips) {
                            const bool selected =
                                c == n.str || (n.str.empty() && c == clips.front());
                            if (ImGui::Selectable(c.c_str(), selected) &&
                                n.str != c) {
                                n.str = c;
                                changed = true;
                            }
                        }
                        endCombo();
                    }
                }
            }
            if (!picker) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
                if (ui::InputText("Clip", buf, sizeof(buf))) n.str = buf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(t->strTip);
                ImGui::TextDisabled("Target is not an animated\n.glb - type the clip name.");
            }
        } else if (t->strKind == FlowParamKind::Text) {
            // Patrol Waypoints repurposes the text param as the waypoint
            // name prefix (the target NPC comes from the object link / self).
            const bool patrol = n.type == "PatrolWaypoints";
            const bool prefix = patrol || n.type == "FindNearest";
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
            if (ui::InputText(prefix ? "Prefix" : "Text", buf, sizeof(buf)))
                n.str = buf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(t->strTip);
            if (n.type == "FindNearest") {
                int count = 0;
                if (!n.str.empty())
                    for (const SceneObject& o : project_.objects())
                        if (o.name.rfind(n.str, 0) == 0) ++count;
                ImGui::TextDisabled("%d candidate%s in this scene", count,
                                    count == 1 ? "" : "s");
            }
            if (patrol) {
                int count = 0;
                if (!n.str.empty())
                    for (const SceneObject& o : project_.objects())
                        if (o.name.rfind(n.str, 0) == 0) ++count;
                ImGui::TextDisabled("Route: %s1, %s2, ...\n%d found in this scene",
                                    n.str.c_str(), n.str.c_str(), count);
            }
        } else if (t->strKind == FlowParamKind::MusicTrack) {
            const std::string current =
                n.str.empty() ? "<none>"
                              : std::filesystem::path(n.str).filename().string();
            if (beginCombo("Track", current.c_str(), t->strTip)) {
                for (const std::string& m : project_.music) {
                    const std::string name = std::filesystem::path(m).filename().string();
                    if (ImGui::Selectable(name.c_str(), m == n.str)) {
                        n.str = m;
                        changed = true;
                    }
                }
                if (project_.music.empty())
                    ImGui::TextDisabled("Import tracks in the\nProject panel (Music).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::SoundTrack) {
            const std::string current =
                n.str.empty() ? "<none>"
                              : std::filesystem::path(n.str).filename().string();
            if (beginCombo("Sound", current.c_str(), t->strTip)) {
                for (const std::string& s : project_.sounds) {
                    const std::string name = std::filesystem::path(s).filename().string();
                    if (ImGui::Selectable(name.c_str(), s == n.str)) {
                        n.str = s;
                        changed = true;
                    }
                }
                if (project_.sounds.empty())
                    ImGui::TextDisabled("Import sounds in the\nProject panel (Sounds).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::SceneName) {
            if (beginCombo("Scene", n.str.empty() ? "<none>" : n.str.c_str(),
                                    t->strTip)) {
                for (const SceneData& s : project_.scenes) {
                    if (ImGui::Selectable(s.name.c_str(), s.name == n.str)) {
                        n.str = s.name;
                        changed = true;
                    }
                }
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::LayerName) {
            if (beginCombo("Layer", n.str.empty() ? "<none>" : n.str.c_str(),
                                    t->strTip)) {
                for (const SceneLayer& l : project_.active().layers) {
                    if (ImGui::Selectable(l.name.c_str(), l.name == n.str)) {
                        n.str = l.name;
                        changed = true;
                    }
                }
                if (project_.active().layers.empty())
                    ImGui::TextDisabled("Add layers in the\nProject panel (Layers).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::SoundBankName) {
            if (beginCombo("Bank", n.str.empty() ? "<none>" : n.str.c_str(), t->strTip)) {
                for (const Project::SoundBank& b : project_.soundBanks) {
                    if (ImGui::Selectable(b.name.c_str(), b.name == n.str)) {
                        n.str = b.name;
                        changed = true;
                    }
                }
                if (project_.soundBanks.empty())
                    ImGui::TextDisabled("Make banks in the\nProject panel (Audio).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::AreaName) {
            // In Area: pick one of the scene's Area objects (docs/areas.md).
            if (areaCombo("Area", n.str)) changed = true;
            paramTip(t->strTip);
            if (n.str.empty())
                ImGui::TextDisabled("Pick an area - the node\ncompiles out without one.");
        } else if (t->strKind == FlowParamKind::SaveValue) {
            if (beginCombo("Value", n.str.empty() ? "<none>" : n.str.c_str(),
                                    t->strTip)) {
                for (const SaveValue& v : project_.saveValues) {
                    if (ImGui::Selectable(v.name.c_str(), v.name == n.str)) {
                        n.str = v.name;
                        changed = true;
                    }
                }
                if (project_.saveValues.empty())
                    ImGui::TextDisabled("Add values in\nTools > Save Editor.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::SaveText) {
            if (beginCombo("Value", n.str.empty() ? "<none>" : n.str.c_str(),
                                    t->strTip)) {
                for (const SaveTextValue& v : project_.saveTexts) {
                    if (ImGui::Selectable(v.name.c_str(), v.name == n.str)) {
                        n.str = v.name;
                        changed = true;
                    }
                }
                if (project_.saveTexts.empty())
                    ImGui::TextDisabled("Add text values in\nTools > Save Editor.");
                endCombo();
            }
            if (n.type == "SetSaveText") {
                bool textLinked = false;
                for (const FlowLink& l : fg.links)
                    textLinked |= (l.kind == FlowLinkText && l.toNode == n.id);
                if (textLinked) {
                    ImGui::TextDisabled("Text: from link");
                } else {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s", n.str2.c_str());
                    if (ui::InputText("Text", buf, sizeof(buf))) n.str2 = buf;
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    paramTip(t->str2Tip);
                }
            }
        } else if (t->strKind == FlowParamKind::GradingName) {
            if (beginCombo("Preset", n.str.empty() ? "<none>" : n.str.c_str(),
                                     t->strTip)) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const ColorGradingPreset& g : project_.gradings) {
                    if (ImGui::Selectable(g.name.c_str(), g.name == n.str)) {
                        n.str = g.name;
                        changed = true;
                    }
                }
                if (project_.gradings.empty())
                    ImGui::TextDisabled("Add presets in\nTools > Color Grading.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::AmbienceName) {
            if (beginCombo("Preset", n.str.empty() ? "<none>" : n.str.c_str(),
                                     t->strTip)) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const AmbiencePreset& a : project_.ambiencePresets) {
                    if (ImGui::Selectable(a.name.c_str(), a.name == n.str)) {
                        n.str = a.name;
                        changed = true;
                    }
                }
                if (project_.ambiencePresets.empty())
                    ImGui::TextDisabled("Add presets in\nTools > Ambience Editor.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::SequenceName) {
            if (beginCombo("Sequence", n.str.empty() ? "<none>" : n.str.c_str(),
                                       t->strTip)) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const Sequence& s : project_.sequences) {
                    if (ImGui::Selectable(s.name.c_str(), s.name == n.str)) {
                        n.str = s.name;
                        changed = true;
                    }
                }
                if (project_.sequences.empty())
                    ImGui::TextDisabled("Add sequences in\nTools > Cutscene Director.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::CreditsName) {
            if (beginCombo("Credits", n.str.empty() ? "<none>" : n.str.c_str(),
                                      t->strTip)) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const CreditsRoll& r : project_.credits) {
                    if (ImGui::Selectable(r.name.c_str(), r.name == n.str)) {
                        n.str = r.name;
                        changed = true;
                    }
                }
                if (project_.credits.empty())
                    ImGui::TextDisabled("Add rolls in\nTools > Credits Editor.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::MenuName) {
            if (beginCombo("Menu", n.str.empty() ? "<none>" : n.str.c_str(),
                                   t->strTip)) {
                for (const GameMenu& gm : project_.menus) {
                    if (ImGui::Selectable(gm.name.c_str(), gm.name == n.str)) {
                        n.str = gm.name;
                        changed = true;
                    }
                }
                if (project_.menus.empty())
                    ImGui::TextDisabled("Add menus in the\nProject panel (Menus).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::HudTextName) {
            if (beginCombo("Text", n.str.empty() ? "<none>" : n.str.c_str(),
                                   t->strTip)) {
                for (const HudText& ht : project_.hudTexts) {
                    if (ImGui::Selectable(ht.name.c_str(), ht.name == n.str)) {
                        n.str = ht.name;
                        changed = true;
                    }
                }
                if (project_.hudTexts.empty())
                    ImGui::TextDisabled("Add texts in\nTools > UI Editor (Texts).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::HudBarName) {
            if (beginCombo("Bar", n.str.empty() ? "<none>" : n.str.c_str(),
                                   t->strTip)) {
                for (const HudBar& hb : project_.hudBars) {
                    if (ImGui::Selectable(hb.name.c_str(), hb.name == n.str)) {
                        n.str = hb.name;
                        changed = true;
                    }
                }
                if (project_.hudBars.empty())
                    ImGui::TextDisabled("Add bars in\nTools > UI Editor (Bars).");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::HudElementName) {
            // Every kind of element in one list, labelled by kind so a name
            // shared between an image and a bar is still two rows. Each row
            // needs an explicit id: two Selectables with one label collide.
            if (beginCombo("Element", n.str.empty() ? "<none>" : n.str.c_str(),
                                   t->strTip)) {
                int row = 0;
                auto pick = [&](const std::string& name, const char* kind) {
                    ImGui::PushID(row++);
                    const std::string label = name + "  (" + kind + ")";
                    if (ImGui::Selectable(label.c_str(), name == n.str)) {
                        n.str = name;
                        changed = true;
                    }
                    ImGui::PopID();
                };
                for (const HudImage& hi : project_.hud) pick(hi.name, "image");
                for (const HudBar& hb : project_.hudBars) pick(hb.name, "bar");
                for (const HudText& ht : project_.hudTexts) pick(ht.name, "text");
                if (row == 0)
                    ImGui::TextDisabled("Add images, bars or texts in\nTools > UI Editor.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::FontName) {
            // Empty = the project's first font (project::defaultFontName), so a
            // fresh Display Text node draws without picking anything.
            const std::string cur =
                n.str.empty() ? project_.defaultFontName() : n.str;
            if (beginCombo("Font", cur.c_str(), t->strTip)) {
                for (const GameFont& gf : project_.fonts) {
                    if (ImGui::Selectable(gf.name.c_str(), gf.name == cur)) {
                        n.str = gf.name;
                        changed = true;
                    }
                }
                endCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Fonts are managed in Tools > Font Manager.\nColor and drop "
                    "shadow come from the font entry.");
            // str2 = a static prefix in front of the wired text ("Score: " +
            // a Get Save Value), the way Log Message reads.
            char pbuf[64];
            std::snprintf(pbuf, sizeof(pbuf), "%s", n.str2.c_str());
            if (ui::InputText("Prefix", pbuf, sizeof(pbuf))) n.str2 = pbuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(t->str2Tip);
        } else if (t->strKind == FlowParamKind::ScreenFxName) {
            // Only PLACED effects can be driven - the generated symbol exists
            // per placement, not per .screenfx file.
            std::string cur = n.str.empty() ? "<none>" : n.str;
            if (const CustomScreenFx* e = customScreenFx(n.str)) cur = e->title;
            if (beginCombo("Effect", cur.c_str(), t->strTip)) {
                int placed = 0;
                for (const ScreenFxPlacement& pl : project_.screenFx) {
                    const CustomScreenFx* e = customScreenFx(pl.key);
                    if (!e) continue;
                    ++placed;
                    const std::string lbl =
                        e->title + (pl.enabled ? "" : "  (disabled)");
                    if (ImGui::Selectable(lbl.c_str(), pl.key == n.str)) {
                        n.str = pl.key;
                        changed = true;
                    }
                }
                if (placed == 0)
                    ImGui::TextDisabled(
                        "Place a custom effect in\nTools > UI Editor first.");
                endCombo();
            }
        } else if (t->strKind == FlowParamKind::FactName ||
                   t->strKind == FlowParamKind::FactQueryName) {
            // A fact is PICKED, never typed - that is the whole difference
            // between the catalog and the Variables nodes above it, and the
            // reason a fact node can say what its value means. The list is the
            // same one the World Facts window offers, so a fact renamed there
            // is renamed here (project::renameFactRefs).
            const bool query = t->strKind == FlowParamKind::FactQueryName;
            std::string cur = n.str.empty() ? "<none>" : n.str;
            if (beginCombo(query ? "Query" : "Fact", cur.c_str(), t->strTip)) {
                int offered = 0;
                if (query) {
                    for (size_t i = 0; i < project_.factQueries.size(); ++i) {
                        const facts::Query& q = project_.factQueries[i];
                        ++offered;
                        const std::string lbl =
                            q.name + "##fq" + std::to_string(i);
                        if (ImGui::Selectable(lbl.c_str(), q.name == n.str)) {
                            n.str = q.name;
                            changed = true;
                        }
                        if (ImGui::IsItemHovered() && !q.desc.empty())
                            ImGui::SetTooltip("%s", q.desc.c_str());
                    }
                } else {
                    // Which facts this node can even mean: the position nodes
                    // want position facts and nothing else, the rest want
                    // everything else. Offering the wrong ones would compile
                    // to a commented-out node with no hint why.
                    const bool wantPos = n.type == "SetFactPos" ||
                                         n.type == "GetFactPos";
                    for (size_t i = 0; i < project_.facts.size(); ++i) {
                        const facts::Fact& f = project_.facts[i];
                        const bool isPos = f.type == facts::Type::Position;
                        if (isPos != wantPos) continue;
                        // Nothing can write a computed fact, so a writer must
                        // not offer one.
                        if (f.isComputed() &&
                            (n.type == "SetFact" || n.type == "SetFactPos" ||
                             n.type == "ClearFact"))
                            continue;
                        ++offered;
                        const std::string lbl =
                            f.name + "##ff" + std::to_string(i);
                        if (ImGui::Selectable(lbl.c_str(), f.name == n.str)) {
                            n.str = f.name;
                            changed = true;
                        }
                        if (ImGui::IsItemHovered() && !f.desc.empty())
                            ImGui::SetTooltip("%s", f.desc.c_str());
                    }
                }
                if (offered == 0)
                    ImGui::TextDisabled(
                        "Nothing to pick yet - declare\none in Tools > World "
                        "Facts.");
                endCombo();
            }
            if (!n.str.empty() && !query) {
                // A one-of-several fact turns the node's Value param into a
                // list of names, which is the whole reason to declare one.
                const int fi = facts::indexOf(project_.facts, n.str);
                if (fi < 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                                       "No such fact - it was\nrenamed or "
                                       "deleted.");
            }
        } else if (t->strKind == FlowParamKind::VarName ||
                   t->strKind == FlowParamKind::EventName) {
            // Both are free text that EXISTS by being named, so both get the
            // same field plus a picker over the names already in the project.
            const bool event = t->strKind == FlowParamKind::EventName;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
            if (ui::InputText(event ? "Event" : "Variable", buf, sizeof(buf)))
                n.str = buf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(t->strTip);
            if (event && n.str.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                                   "Name it - an unnamed event\ncompiles out.");
            // Same-namespace names already used anywhere in the project
            // (variables and events are game-global; picking beats
            // retyping/typos).
            const std::vector<std::string> known = flowVarNames(n.type);
            if (!known.empty()) {
                if (ImGui::SmallButton("Pick...")) ImGui::OpenPopup("##pickvar");
                if (ImGui::BeginPopup("##pickvar")) {
                    ImGui::PushFont(nullptr, uiFontSize);  // popup, not canvas
                    for (const std::string& name : known) {
                        if (ImGui::Selectable(name.c_str(), name == n.str)) {
                            n.str = name;
                            changed = true;
                        }
                    }
                    ImGui::PopFont();
                    ImGui::EndPopup();
                }
            }
        }

        if (n.type == "Self") ImGui::TextDisabled("(%s)", owner.name.c_str());

        // numeric params (own ID scope - a num label may repeat the string
        // param's label, e.g. Set Save Value's combo and drag are both "Value")
        ImGui::PushID("params");
        // X/Y/Z come from the position link; params past them (Speed) stay.
        // SetDof draws its own params (mode combo) below.
        int firstNum = 0;
        if (posLinked && t->posIn && t->numCount >= 3 && n.type != "SetDof") {
            ImGui::TextDisabled("Position: from link");
            firstNum = 3;
        }
        // A fact node whose Value names a one-of-several fact draws the
        // OPTION NAMES rather than an index - the same reason the catalog
        // does, and the difference between "power.state is 2" and
        // "power.state is Overloaded" in a graph someone reads next year.
        if ((n.type == "SetFact" || n.type == "FactIs" ||
             n.type == "FactAtLeast" || n.type == "FactAtMost") &&
            !n.str.empty()) {
            const int fi = facts::indexOf(project_.facts, n.str);
            if (fi >= 0 &&
                project_.facts[(size_t)fi].type == facts::Type::Enum &&
                !project_.facts[(size_t)fi].options.empty()) {
                const facts::Fact& f = project_.facts[(size_t)fi];
                int idx = (int)std::lround(n.num[0]);
                if (idx < 0 || idx >= (int)f.options.size()) idx = 0;
                if (beginCombo(t->numLabels[0], f.options[(size_t)idx].c_str(),
                               t->numTips[0])) {
                    for (size_t o = 0; o < f.options.size(); ++o) {
                        const std::string lbl =
                            f.options[o] + "##fo" + std::to_string(o);
                        if (ImGui::Selectable(lbl.c_str(), (int)o == idx)) {
                            n.num[0] = (float)o;
                            changed = true;
                        }
                    }
                    endCombo();
                }
                firstNum = 1;  // the drag below must not draw it a second time
            }
        }
        // A yes/no fact's Value is a checkbox for the same reason.
        if (n.type == "SetFact" && !n.str.empty() && firstNum == 0) {
            const int fi = facts::indexOf(project_.facts, n.str);
            if (fi >= 0 &&
                project_.facts[(size_t)fi].type == facts::Type::Bool) {
                bool v = n.num[0] != 0.0f;
                if (ui::Switch(t->numLabels[0], &v)) {
                    n.num[0] = v ? 1.0f : 0.0f;
                    changed = true;
                }
                paramTip(t->numTips[0]);
                firstNum = 1;
            }
        }
        if (n.type == "SetVarBool" || n.type == "SetFlashlight" ||
            n.type == "SetFog" || n.type == "SetParticles" ||
            n.type == "SetWidescreen" || n.type == "Gate") {
            bool v = n.num[0] != 0.0f;
            if (ui::Switch(t->numLabels[0], &v)) {
                n.num[0] = v ? 1.0f : 0.0f;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
        } else if (n.type == "SetLight") {
            bool v = n.num[0] != 0.0f;
            if (ui::Switch("On", &v)) {
                n.num[0] = v ? 1.0f : 0.0f;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            ui::DragFloat("Intensity", &n.num[1], 0.02f, 0.0f, 4.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 1));
        } else if (n.type == "SetDof") {
            const char* modes[] = {"Set custom", "Off", "Scene setting"};
            int mode = (int)n.num[3];
            mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
            if (ui::Combo("Mode", &mode, modes, 3)) {
                n.num[3] = (float)mode;
                changed = true;
            }
            paramTip(flowNumTip(*t, 3));
            if (mode == 0) {
                if (posLinked) {
                    // the link replaces Focus with the distance to the point
                    ImGui::TextDisabled("Focus: distance to linked point");
                } else {
                    ui::DragFloat("Focus", &n.num[0], 0.5f, 0.5f, 500.0f,
                                     "%.1f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    paramTip(flowNumTip(*t, 0));
                }
                ui::DragFloat("Range", &n.num[1], 0.5f, 0.1f, 500.0f,
                                 "%.1f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 1));
                ui::SliderFloat("Amount", &n.num[2], 0.0f, 1.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 2));
            } else if (mode == 1) {
                ImGui::TextDisabled("Turns depth of field off.");
            } else {
                ImGui::TextDisabled(
                    "Restores the scene's authored values\n"
                    "(Tools > UI Editor > Depth of field).");
            }
        } else if (n.type == "SetDisplayMode") {
            // Order = Tyra::DisplayMode enum values (serialized in num[0]).
            const char* modes[] = {"Interlaced (480i/576i)",
                                   "Progressive (480p)", "1080i",
                                   "Field rendering (480i/576i)",
                                   "PAL 576i (full-height)"};
            int mode = (int)n.num[0];
            mode = mode < 0 ? 0 : mode > 4 ? 4 : mode;
            if (ui::Combo("Mode", &mode, modes, 5)) {
                n.num[0] = (float)mode;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            ui::DragFloat("Confirm s", &n.num[1], 0.5f, 0.0f, 60.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 1));
            ImGui::TextDisabled(
                "Confirm > 0: the game asks to keep the\n"
                "mode (X = yes) and reverts automatically\n"
                "when the timer runs out. 0 = switch blind.");
        } else if (n.type == "SetStickCurve") {
            const char* sticks[] = {"Left (move)", "Right (camera)", "Both"};
            int stick = (int)n.num[0];
            stick = stick < 0 ? 0 : stick > 2 ? 2 : stick;
            if (ui::Combo("Stick", &stick, sticks, 3)) {
                n.num[0] = (float)stick;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            const char* curves[] = {"Linear", "Exponential", "S-Curve"};
            int curve = (int)n.num[1];
            curve = curve < 0 ? 0 : curve > 2 ? 2 : curve;
            if (ui::Combo("Curve", &curve, curves, 3)) {
                n.num[1] = (float)curve;
                changed = true;
            }
            paramTip(flowNumTip(*t, 1));
            if (curve != 0) {  // exponent only shapes Exponential / S-Curve
                ui::DragFloat("Exponent", &n.num[2], 0.05f, 1.0f, 6.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 2));
            }
        } else if (n.type == "VibratePad") {
            ui::SliderFloat("Big", &n.num[0], 0.0f, 1.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 0));
            bool small = n.num[1] != 0.0f;
            if (ui::Switch("Small", &small)) {
                n.num[1] = small ? 1.0f : 0.0f;
                changed = true;
            }
            paramTip(flowNumTip(*t, 1));
            ui::DragFloat("Seconds", &n.num[2], 0.05f, 0.0f, 60.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 2));
            ImGui::TextDisabled(
                "Seconds 0 = until the next Vibrate Pad.\n"
                "Big 0 + Small off stops the vibration.");
        } else if (n.type == "Tween") {
            // From/To are whatever the driven parameter is (a color channel, a
            // volume, a world coordinate), so they stay free drags; Ease is the
            // one enumerated field.
            ui::DragFloat("From", &n.num[0], 0.1f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 0));
            ui::DragFloat("To", &n.num[1], 0.1f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 1));
            ui::DragFloat("Seconds", &n.num[2], 0.05f, 0.0f, 600.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 2));
            const char* eases[] = {"Linear", "Ease in", "Ease out",
                                   "Smooth (in-out)"};
            int ease = (int)n.num[3];
            ease = ease < 0 ? 0 : ease > 3 ? 3 : ease;
            if (ui::Combo("Ease", &ease, eases, 4)) {
                n.num[3] = (float)ease;
                changed = true;
            }
            paramTip(flowNumTip(*t, 3));
            ImGui::TextDisabled("Wire the number output into\nwhatever should animate.");
        } else if (n.type == "SetScreenFx") {
            // The params belong to the EFFECT, so label and bound them from its
            // own .screenfx manifest rather than the registry's generic P1..P4.
            const CustomScreenFx* e = customScreenFx(n.str);
            if (!e) {
                ImGui::TextDisabled("Pick an effect to see\nits parameters.");
            } else if (e->paramCount == 0) {
                ImGui::TextDisabled("%s takes no parameters -\nuse the on/off pins.",
                                    e->title.c_str());
            } else {
                bool p0Linked = false;
                for (const FlowLink& l : fg.links)
                    p0Linked |= (l.kind == FlowLinkNum && l.toNode == n.id);
                for (int a = 0; a < e->paramCount && a < 4; ++a) {
                    if (a == 0 && p0Linked) {
                        ImGui::TextDisabled("%s: from link",
                                            e->paramLabel[0].c_str());
                        continue;
                    }
                    ui::SliderFloat(e->paramLabel[a].c_str(), &n.num[a],
                                       e->paramMin[a], e->paramMax[a], "%.3f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    // The LABEL is the effect's; what the slot means on this
                    // node is the registry's, so the tip still comes from there.
                    paramTip(flowNumTip(*t, a));
                }
            }
        } else if (n.type == "SetBars") {
            const char* styles[] = {"None", "Cinema 2.39:1", "Wide 16:9",
                                    "Pillarbox", "Frame"};
            int st = (int)n.num[0];
            st = st < 0 ? 0 : st > 4 ? 4 : st;
            if (ui::Combo("Style", &st, styles, 5)) {
                n.num[0] = (float)st;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            bool amtLinked = false;
            for (const FlowLink& l : fg.links)
                amtLinked |= (l.kind == FlowLinkNum && l.toNode == n.id);
            if (amtLinked) {
                ImGui::TextDisabled("Amount: from link");
            } else {
                ui::SliderFloat("Amount", &n.num[1], 0.0f, 1.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 1));
            }
            ImGui::TextDisabled("Wire a Tween into Amount\nto slide them in.");
        } else if (n.type == "PlayerPos") {
            const char* who[] = {"Player 1", "Player 2"};
            int idx = n.num[0] != 0.0f ? 1 : 0;
            if (ui::Combo("Player", &idx, who, 2)) {
                n.num[0] = (float)idx;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            if (idx == 1)
                ImGui::TextDisabled(
                    "Reads player 1 while\nplayer 2 is not in the game.");
        } else if (n.type == "RandomBranch") {
            int outs = (int)n.num[0];
            outs = outs < 2 ? 2 : outs > 4 ? 4 : outs;
            if (ui::SliderInt("Outputs", &outs, 2, 4)) {
                n.num[0] = (float)outs;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            ImGui::TextDisabled("Only the first %d outputs\nare ever picked.", outs);
        } else if (n.type == "DisplayText") {
            // X/Y are a normalized screen position (center anchor), so they need
            // a much finer step than the generic 0.1 drag.
            ui::DragFloat2("Pos##dyntext", n.num, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            // One widget for num[0] and num[1], so its tip covers both - X's.
            paramTip(flowNumTip(*t, 0));
            ui::DragFloat("Size", &n.num[2], 0.2f, 8.0f, 48.0f, "%.0f px");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 2));
            ui::DragFloat("Seconds", &n.num[3], 0.05f, 0.0f, 60.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 3));
        } else if (t->numKind == FlowParamKind::Color) {
            ui::ColorHex("Color", n.num);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            // The picker is num[0..2] in one widget, so num[0]'s tip is the
            // colour's - the registry writes it as "RGB", not as "red".
            paramTip(flowNumTip(*t, 0));
        } else {
            // A wired number replaces num[0] (flowgraph.hpp's one convention),
            // so show that instead of a param the game will ignore.
            bool numLinked = false;
            if (t->numIn)
                for (const FlowLink& l : fg.links)
                    numLinked |= (l.kind == FlowLinkNum && l.toNode == n.id);
            // Angles live in the tens-to-hundreds range, so the generic 0.1
            // drag step would take a very long mouse journey to reach 90.
            const bool isAngle = n.type == "RotateObjectBy" ||
                                 n.type == "SetRotation" ||
                                 n.type == "SpinObject" ||
                                 n.type == "PosRotateY";
            for (int a = firstNum; a < t->numCount; ++a) {
                // A wired number REPLACES num[0] - unless the node says the
                // wire is an operand of its own (Clamp's value between its Min
                // and Max), in which case every param stays editable.
                if (a == 0 && numLinked && !flowNumFolds(*t) && !t->numInExtra) {
                    ImGui::TextDisabled("%s: from link", t->numLabels[0]);
                    continue;
                }
                // A declared choice wins over every label heuristic below:
                // the node said what this parameter IS, so there is nothing to
                // guess from its name.
                if (t->numChoices[a] && *t->numChoices[a]) {
                    const char* list = t->numChoices[a];
                    int count = 1;
                    for (const char* p = list; *p; ++p)
                        if (*p == '|') ++count;
                    int idx = (int)(n.num[a] + 0.5f);
                    if (idx < 0) idx = 0;
                    if (idx >= count) idx = count - 1;
                    // Slice the k-th '|'-separated label out of the list.
                    auto option = [&](int k) {
                        std::string out;
                        int at = 0;
                        for (const char* p = list;; ++p) {
                            if (*p == '|' || !*p) {
                                if (at == k) return out;
                                out.clear();
                                ++at;
                                if (!*p) break;
                            } else {
                                out.push_back(*p);
                            }
                        }
                        return out;
                    };
                    const std::string cur = option(idx);
                    if (beginCombo(t->numLabels[a], cur.c_str(), t->numTips[a])) {
                        for (int k = 0; k < count; ++k) {
                            const std::string lab = option(k);
                            if (ImGui::Selectable(lab.c_str(), k == idx)) {
                                n.num[a] = (float)k;
                                changed = true;
                            }
                        }
                        endCombo();
                    }
                    continue;
                }
                // A declared fraction, like a declared choice, wins over the
                // label heuristics: it is drawn as a percentage and BOUNDED,
                // where the generic drag at the end of this loop takes any
                // value at all - including the negatives and the hundreds that
                // codegen then silently clamps away.
                if (t->numPercent[a]) {
                    float pct = n.num[a] * 100.0f;
                    if (ui::SliderFloat(t->numLabels[a], &pct, 0.0f, 100.0f,
                                           "%.0f%%",
                                           ImGuiSliderFlags_AlwaysClamp)) {
                        n.num[a] = pct * 0.01f;
                        changed = true;
                    }
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    paramTip(flowNumTip(*t, a));
                    continue;
                }
                const bool isLoop = std::strcmp(t->numLabels[a], "Loop") == 0 ||
                                    std::strcmp(t->numLabels[a], "Once") == 0 ||
                                    std::strcmp(t->numLabels[a], "Whole") == 0 ||
                                    std::strcmp(t->numLabels[a], "Tilt too") == 0 ||
                                    std::strcmp(t->numLabels[a], "LOS") == 0;
                const bool isVolume = std::strcmp(t->numLabels[a], "Volume") == 0;
                const bool isChannel = std::strcmp(t->numLabels[a], "Channel") == 0;
                // Whole-number params (Do N Times, Counter's Every, For Loop's
                // Times): a 0.1 drag on a repeat count offers fractions that
                // codegen rounds away.
                const bool isCount = std::strcmp(t->numLabels[a], "Times") == 0 ||
                                     std::strcmp(t->numLabels[a], "Every") == 0;
                if (isCount) {
                    int v = (int)n.num[a];
                    if (ui::DragInt(t->numLabels[a], &v, 0.2f, 0, 999)) {
                        n.num[a] = (float)(v < 0 ? 0 : v);
                        changed = true;
                    }
                } else if (isChannel) {
                    // SPU channel 0-23; -1 = rotate through channels
                    ui::SliderFloat("Channel", &n.num[a], -1.0f, 23.0f,
                                       n.num[a] < 0.0f ? "auto" : "%.0f");
                    n.num[a] = (float)(int)n.num[a];
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else if (isLoop) {
                    bool loop = n.num[a] != 0.0f;
                    if (ui::Switch(t->numLabels[a], &loop)) {
                        n.num[a] = loop ? 1.0f : 0.0f;
                        changed = true;
                    }
                } else if (isVolume) {
                    ui::SliderFloat("Volume", &n.num[a], 0.0f, 100.0f, "%.0f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else {
                    ui::DragFloat(t->numLabels[a], &n.num[a],
                                     isAngle ? 1.0f : 0.1f);
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
                // The one call that documents most of the registry: this loop
                // draws every param of every node that has no bespoke branch.
                paramTip(flowNumTip(*t, a));
            }
        }
        ImGui::PopID();  // "params"
        if (flowNumFolds(*t)) {
            // B is the second operand only while there is nothing wired to be
            // one - with two inputs it drops out, and a silent param is worse
            // than a labeled one.
            int wired = 0;
            for (const FlowLink& l : fg.links)
                if (l.kind == FlowLinkNum && l.toNode == n.id) ++wired;
            const char* opLabel = n.type == "NumAdd"   ? "+ B"
                                  : n.type == "NumSub" ? "- B"
                                  : n.type == "NumMul" ? "x B"
                                  : n.type == "NumDiv" ? "/ B"
                                  : n.type == "NumMod" ? "% B"
                                  : n.type == "NumPow" ? "^ Exponent"
                                  : n.type == "NumMin" ? "vs B (smaller wins)"
                                  : n.type == "NumMax" ? "vs B (larger wins)"
                                                       : "with B";
            const char* bName =
                std::strcmp(t->numLabels[0], "B") == 0 ? "B" : t->numLabels[0];
            if (wired >= 2)
                ImGui::TextDisabled("%s unused: %d inputs wired.", bName, wired);
            else if (wired == 1)
                ImGui::TextDisabled("input %s", opLabel);
            else
                ImGui::TextDisabled("Nothing wired: result is %s.", bName);
        }
        if (n.type == "ValueAtLeast" || n.type == "VarAtLeast")
            ImGui::TextDisabled("Checked every frame - wire the\nbool into On Condition or a gate.");
        if (n.type == "Raycast")
            ImGui::TextDisabled("Casts from the player's eye along\nthe view direction on exec.");
        if (n.type == "Branch")
            ImGui::TextDisabled("Nothing wired = always 'false'.\nEvaluated at the moment it runs.");
        if (n.type == "Sequence")
            ImGui::TextDisabled("Each output's whole chain finishes\nbefore the next one starts.");
        if (n.type == "Cooldown")
            ImGui::TextDisabled("Swallowed execs are LOST,\nnot queued (that is Delay).");
        if (n.type == "ForLoop")
            ImGui::TextDisabled("Runs inside one frame.\nCapped at 64 iterations.");
        if (n.type == "ChasePlayer" || n.type == "FleePlayer" ||
            n.type == "PatrolWaypoints")
            ImGui::TextDisabled(
                "Walks the baked nav grid.\nOne AI state per object -\na new "
                "command replaces it.");
        if (n.type == "OnPlayerSeen")
            ImGui::TextDisabled(
                "Vision cone from the NPC's\nfacing; LOS: hills block\nsight. "
                "Bool = seen now.");
    };

    // Is input pin kind `kind` of node `nodeId` fed by a link?
    auto linkedTo = [&](int nodeId, int kind) {
        for (const FlowLink& l : fg.links)
            if (l.kind == kind && l.toNode == nodeId) return true;
        return false;
    };
    // The compact node body: the values that make this node THIS node, as
    // short chips ("self", "lenses", "Seconds 2"). Every parameter, its tip
    // and its wiring are in the inspector.
    auto chipsFor = [&](const FlowNode& n, const FlowNodeType& t) {
        std::vector<std::string> out;
        auto fileOf = [](const std::string& s) {
            return std::filesystem::path(s).filename().string();
        };
        switch (t.strKind) {
            case FlowParamKind::None: break;
            case FlowParamKind::ObjectName:
                out.push_back(linkedTo(n.id, FlowLinkObject) ? "from link"
                              : n.str.empty()                 ? "self"
                                                              : flowShort(n.str, 20));
                break;
            case FlowParamKind::Text:
                if (!n.str.empty()) out.push_back("\"" + flowShort(n.str, 22) + "\"");
                break;
            case FlowParamKind::MusicTrack:
            case FlowParamKind::SoundTrack:
                out.push_back(n.str.empty() ? "none" : flowShort(fileOf(n.str), 20));
                break;
            case FlowParamKind::FontName:
                if (!n.str2.empty()) out.push_back("\"" + flowShort(n.str2, 22) + "\"");
                break;
            case FlowParamKind::SaveSlotMode: {
                const std::vector<SaveSlotModeInfo>& modes = saveSlotModes();
                std::string lbl = modes.empty() ? std::string() : modes[0].label;
                for (const SaveSlotModeInfo& m : modes)
                    if (n.str == m.key) lbl = m.label;
                if (!lbl.empty()) out.push_back(lbl);
                break;
            }
            case FlowParamKind::ReverbPreset:
                out.push_back(reverbPresets()[reverbPresetIndex(n.str)].label);
                break;
            case FlowParamKind::ScreenFxName:
                if (const CustomScreenFx* e = customScreenFx(n.str)) out.push_back(e->title);
                else out.push_back("none");
                break;
            default: out.push_back(n.str.empty() ? "none" : flowShort(n.str, 20)); break;
        }
        if (n.type == "SetSaveText" && !n.str2.empty() && !linkedTo(n.id, FlowLinkText))
            out.push_back("\"" + flowShort(n.str2, 16) + "\"");
        int first = 0;
        if (t.posIn && t.numCount >= 3 && linkedTo(n.id, FlowLinkPos) && n.type != "SetDof") {
            out.push_back("position: link");
            first = 3;
        }
        if (t.numKind == FlowParamKind::Color) {
            char b[16];
            std::snprintf(b, sizeof(b), "#%02X%02X%02X", (int)(ImSaturate(n.num[0]) * 255.0f + 0.5f),
                          (int)(ImSaturate(n.num[1]) * 255.0f + 0.5f),
                          (int)(ImSaturate(n.num[2]) * 255.0f + 0.5f));
            out.push_back(b);
            first = t.numCount;
        }
        const bool numL = t.numIn && linkedTo(n.id, FlowLinkNum);
        for (int a = first; a < t.numCount && a < 4; ++a) {
            const char* lab = t.numLabels[a] && *t.numLabels[a] ? t.numLabels[a] : "";
            std::string v;
            if (a == 0 && numL && !flowNumFolds(t) && !t.numInExtra) {
                v = "link";
            } else if (t.numChoices[a] && *t.numChoices[a]) {
                v = flowChoice(t.numChoices[a], (int)(n.num[a] + 0.5f));
                lab = "";
            } else if (t.numPercent[a]) {
                char b[16];
                std::snprintf(b, sizeof(b), "%.0f%%", n.num[a] * 100.0f);
                v = b;
            } else if (std::strcmp(lab, "Loop") == 0 || std::strcmp(lab, "Once") == 0 ||
                       std::strcmp(lab, "Whole") == 0 || std::strcmp(lab, "Tilt too") == 0 ||
                       std::strcmp(lab, "LOS") == 0) {
                // an on/off flag (drawn as a switch in the inspector): named when on
                if (n.num[a] != 0.0f) out.push_back(lab);
                continue;
            } else if (std::strcmp(lab, "Channel") == 0 && n.num[a] < 0.0f) {
                v = "auto";  // SPU channel -1 rotates through them
            } else {
                char b[24];
                std::snprintf(b, sizeof(b), "%.3g", n.num[a]);
                v = b;
            }
            out.push_back(*lab ? std::string(lab) + " " + v : v);
        }
        if (out.size() > 4) {
            const size_t extra = out.size() - 3;
            out.resize(3);
            out.push_back("+" + std::to_string(extra));
        }
        return out;
    };

    // Which pins carry a link - an unwired pin is drawn hollow.
    std::set<int> wiredPins;
    for (const FlowLink& l : fg.links) {
        switch (l.kind) {
            case FlowLinkObject:
                wiredPins.insert(flowIdOutPin(l.fromNode));
                wiredPins.insert(flowIdInPin(l.toNode));
                break;
            case FlowLinkPos:
                wiredPins.insert(flowPosOutPin(l.fromNode));
                wiredPins.insert(flowPosInPin(l.toNode));
                break;
            case FlowLinkBool:
                wiredPins.insert(flowBoolOutPin(l.fromNode));
                wiredPins.insert(flowBoolInPin(l.toNode));
                break;
            case FlowLinkText:
                wiredPins.insert(flowTextOutPin(l.fromNode));
                wiredPins.insert(flowTextInPin(l.toNode));
                break;
            case FlowLinkNum:
                wiredPins.insert(flowNumOutPin(l.fromNode));
                wiredPins.insert(flowNumInPin(l.toNode));
                break;
            default:
                wiredPins.insert(flowExecOutPin(l.fromNode, l.fromPin));
                wiredPins.insert(flowExecInPin(l.toNode, l.toPin));
                break;
        }
    }
    auto pinShape = [&](int pin, ImNodesPinShape filled) -> ImNodesPinShape {
        if (wiredPins.count(pin)) return filled;
        switch (filled) {
            case ImNodesPinShape_QuadFilled: return ImNodesPinShape_Quad;
            case ImNodesPinShape_TriangleFilled: return ImNodesPinShape_Triangle;
            default: return ImNodesPinShape_Circle;
        }
    };

    // A new link between two pins (any order). Pin kinds by id (flowPinKind):
    // 0 = object in, 1 = exec out, 2 = exec in, 3 = object out, 4 = position
    // in, 5 = position out, 6 = bool in, 7 = bool out, 8 = text in, 9 = text
    // out, 10..15 = the node's 2nd..7th exec in, 16/17 = number in/out,
    // 18..24 = the node's 2nd..8th exec out. Returns true when a link was made.
    auto connectPins = [&](int startPin, int endPin) -> bool {
        const int a = flowPinKind(startPin), b = flowPinKind(endPin);
        int outPin = -1, inPin = -1;
        int kind = FlowLinkExec;
        int toPin = 0;    // which exec input of the target the link fires
        int fromPin = 0;  // which exec output of the source it leaves
        const int aExecIn = flowExecInIndex(a), bExecIn = flowExecInIndex(b);
        const int aExecOut = flowExecOutIndex(a), bExecOut = flowExecOutIndex(b);
        if ((aExecOut >= 0 && bExecIn >= 0) || (aExecIn >= 0 && bExecOut >= 0)) {
            const bool aIsOut = aExecOut >= 0 && bExecIn >= 0;
            outPin = aIsOut ? startPin : endPin;
            inPin = aIsOut ? endPin : startPin;
            fromPin = aIsOut ? aExecOut : bExecOut;
            toPin = aIsOut ? bExecIn : aExecIn;
        } else if ((a == 3 && b == 0) || (a == 0 && b == 3)) {
            outPin = a == 3 ? startPin : endPin;
            inPin = a == 0 ? startPin : endPin;
            kind = FlowLinkObject;
        } else if ((a == 5 && b == 4) || (a == 4 && b == 5)) {
            outPin = a == 5 ? startPin : endPin;
            inPin = a == 4 ? startPin : endPin;
            kind = FlowLinkPos;
        } else if ((a == 7 && b == 6) || (a == 6 && b == 7)) {
            outPin = a == 7 ? startPin : endPin;
            inPin = a == 6 ? startPin : endPin;
            kind = FlowLinkBool;
        } else if ((a == 9 && b == 8) || (a == 8 && b == 9)) {
            outPin = a == 9 ? startPin : endPin;
            inPin = a == 8 ? startPin : endPin;
            kind = FlowLinkText;
        } else if ((a == 17 && b == 16) || (a == 16 && b == 17)) {
            outPin = a == 17 ? startPin : endPin;
            inPin = a == 16 ? startPin : endPin;
            kind = FlowLinkNum;
        }
        if (outPin >= 0 && flowPinNode(outPin) != flowPinNode(inPin)) {
            FlowLink l;
            l.fromNode = flowPinNode(outPin);
            l.toNode = flowPinNode(inPin);
            l.kind = kind;
            l.toPin = toPin;
            l.fromPin = fromPin;
            // A single-value input takes at most one link, so a second drag
            // REPLACES it - dragging onto an occupied "Value" pin should just
            // rewire, not silently stack a link codegen would ignore. Bool-in,
            // text-in and the folding Math number inputs keep every link.
            bool single = kind == FlowLinkObject || kind == FlowLinkPos;
            if (kind == FlowLinkNum) {
                const FlowNodeType* tt = nullptr;
                for (const FlowNode& n : fg.nodes)
                    if (n.id == l.toNode) tt = flowNodeType(n.type);
                single = !tt || !flowNumFolds(*tt);
            }
            if (single) {
                for (size_t i = fg.links.size(); i-- > 0;)
                    if (fg.links[i].kind == kind && fg.links[i].toNode == l.toNode)
                        fg.links.erase(fg.links.begin() + i);
            }
            // Both pins are part of the identity: the same trigger may drive two
            // different branches of one merged node, and a Branch's true and
            // false outputs may legitimately meet at the same action.
            bool duplicate = false;
            for (const FlowLink& e : fg.links)
                duplicate |= (e.kind == l.kind && e.fromNode == l.fromNode &&
                              e.toNode == l.toNode && e.toPin == l.toPin &&
                              e.fromPin == l.fromPin);
            if (!duplicate) {
                l.id = fg.nextId++;
                fg.links.push_back(l);
                changed = true;
                return true;
            }
        }
        return false;
    };

    // A fresh node of type t whose title lands at `screenPos`, with the
    // defaults that make it do something sensible before it is touched.
    auto makeNode = [&](const FlowNodeType& t, ImVec2 screenPos) -> int {
        FlowNode n;
        n.id = fg.nextId++;
        n.type = t.key;
        if (t.strKind == FlowParamKind::Button) n.str = "Cross";
        // A fresh On Action / Set Input Preset starts on something
        // that exists, so it compiles before the user touches it.
        if (t.strKind == FlowParamKind::InputActionName &&
            !project_.input.actions.empty())
            n.str = project_.input.actions.front().name;
        if (t.strKind == FlowParamKind::KeyName) n.str = "Space";
        if (std::string(t.key) == "SetInputPreset" &&
            !project_.input.presets.empty())
            n.str = project_.input.presets.front().name;
        if (t.numKind == FlowParamKind::Color)
            n.num[0] = n.num[1] = n.num[2] = 1.0f;
        if (std::string(t.key) == "NearObject") n.num[0] = 4.0f;
        if (std::string(t.key) == "EverySeconds") n.num[0] = 1.0f;
        if (std::string(t.key) == "Delay") n.num[0] = 1.0f;  // seconds
        if (std::string(t.key) == "MoveObjectTo") n.num[3] = 2.0f;  // speed
        if (std::string(t.key) == "Animation") n.num[1] = 1.0f;  // speed
        if (std::string(t.key) == "DisplayText") {
            n.num[0] = 0.5f;   // centered
            n.num[1] = 0.85f;  // near the bottom, like a subtitle
            n.num[2] = 16.0f;  // size in px
        }
        if (std::string(t.key) == "PlayMusic") {
            n.num[0] = 80.0f;  // volume
            n.num[1] = 1.0f;   // loop
            if (!project_.music.empty()) n.str = project_.music.front();
        }
        if (std::string(t.key) == "SetMusicVolume") n.num[0] = 80.0f;
        if (t.strKind == FlowParamKind::LayerName &&
            !project_.active().layers.empty())
            n.str = project_.active().layers.front().name;
        if (t.strKind == FlowParamKind::SoundBankName && !project_.soundBanks.empty())
            n.str = project_.soundBanks.front().name;
        // In Area starts on the scene's first area, if any.
        if (t.strKind == FlowParamKind::AreaName)
            for (const SceneObject& a : project_.objects())
                if (a.type == PrimitiveType::Area) {
                    n.str = a.name;
                    break;
                }
        if (std::string(t.key) == "SetVarBool") n.num[0] = 1.0f;
        if (std::string(t.key) == "SetFlashlight") n.num[0] = 1.0f;
        if (std::string(t.key) == "SetLight") {
            n.num[0] = 1.0f;  // on
            n.num[1] = 1.0f;  // authored intensity
        }
        if (std::string(t.key) == "SetFlare") n.num[0] = 0.5f;
        if (std::string(t.key) == "SetGodRays") n.num[0] = 0.5f;
        if (std::string(t.key) == "Raycast") n.num[0] = 50.0f;  // max dist
        if (std::string(t.key) == "SetDof") {
            n.num[0] = 20.0f;  // focus distance
            n.num[1] = 15.0f;  // range (full blur at focus+range)
            n.num[2] = 1.0f;   // amount (num[3] mode: 0 = set)
        }
        if (std::string(t.key) == "SetStickCurve") n.num[2] = 2.0f;  // exponent
        // Flow control: a node whose count/duration is 0 does
        // nothing at all, which reads as broken rather than as
        // "not configured yet".
        if (std::string(t.key) == "DoN") n.num[0] = 3.0f;
        if (std::string(t.key) == "Gate") n.num[0] = 1.0f;  // starts open
        if (std::string(t.key) == "RandomBranch") n.num[0] = 2.0f;
        if (std::string(t.key) == "Cooldown") n.num[0] = 1.0f;
        if (std::string(t.key) == "Counter") n.num[0] = 1.0f;  // every time
        if (std::string(t.key) == "Timer") n.num[0] = 0.0f;  // runs forever
        if (std::string(t.key) == "ForLoop") n.num[0] = 4.0f;
        if (std::string(t.key) == "Tween") {
            n.num[1] = 1.0f;  // 0 -> 1, the useful default ramp
            n.num[2] = 1.0f;  // over a second
        }
        // Math / Vector: an identity default, so dropping the node
        // in and wiring it changes nothing until a param is touched.
        if (std::string(t.key) == "NumPow") n.num[0] = 2.0f;
        if (std::string(t.key) == "NumMod") n.num[0] = 1.0f;
        if (std::string(t.key) == "NumClamp") n.num[1] = 1.0f;
        if (std::string(t.key) == "NumLerp") n.num[1] = 1.0f;
        if (std::string(t.key) == "NumRemap") {
            n.num[1] = 1.0f;  // in  0..1
            n.num[3] = 1.0f;  // out 0..1
        }
        if (std::string(t.key) == "NumEquals") n.num[1] = 0.01f;
        if (std::string(t.key) == "NumInRange") n.num[1] = 1.0f;
        if (std::string(t.key) == "Oscillate") {
            n.num[0] = 1.0f;  // amplitude
            n.num[1] = 1.0f;  // one cycle a second
        }
        if (std::string(t.key) == "RollRandom") n.num[1] = 1.0f;
        if (std::string(t.key) == "PosScale") n.num[0] = 1.0f;
        if (std::string(t.key) == "PosRotateY") n.num[0] = 90.0f;
        // Object / Player: an identity default again - a scale of
        // (0,0,0) or a factor of 0 would flatten the target on the
        // first fire, which reads as a broken node.
        if (std::string(t.key) == "SetScale")
            n.num[0] = n.num[1] = n.num[2] = 1.0f;
        if (std::string(t.key) == "ScaleObjectBy") n.num[0] = 1.0f;
        if (std::string(t.key) == "SetSfxVolume") n.num[0] = 100.0f;
        if (std::string(t.key) == "SetFade") n.num[0] = 1.0f;
        if (std::string(t.key) == "SetBars") {
            n.num[0] = 1.0f;  // cinema
            n.num[1] = 1.0f;  // fully in
        }
        if (std::string(t.key) == "CameraShake") {
            n.num[0] = 0.15f;  // a noticeable but not silly knock
            n.num[1] = 0.4f;
        }
        // A fresh rotate/spin node starts on the yaw: it is what
        // almost every turning prop wants, and a node whose every
        // param is 0 looks broken.
        if (std::string(t.key) == "RotateObjectBy") n.num[1] = 45.0f;
        if (std::string(t.key) == "SpinObject") n.num[1] = 90.0f;
        if (std::string(t.key) == "VibratePad") {
            n.num[0] = 1.0f;  // big motor at full
            n.num[2] = 0.5f;  // a short kick by default
        }
        if (std::string(t.key) == "PlaySound") {
            n.num[0] = 100.0f;  // volume
            n.num[1] = -1.0f;   // channel: auto
            if (!project_.sounds.empty()) n.str = project_.sounds.front();
        }
        fg.nodes.push_back(n);
        ImNodes::SetNodeScreenSpacePos(n.id, screenPos);
        changed = true;
        auto it = std::find(flowRecent_.begin(), flowRecent_.end(), std::string(t.key));
        if (it != flowRecent_.end()) flowRecent_.erase(it);
        flowRecent_.insert(flowRecent_.begin(), t.key);
        if (flowRecent_.size() > 6) flowRecent_.resize(6);
        return n.id;
    };
    auto openPalette = [&](ImVec2 at, int fromPin) {
        flowPalOpen_ = true;
        flowPalPos_ = at;
        flowPalFromPin_ = fromPin;
    };
    auto deleteNodes = [&](const std::vector<int>& ids) {
        for (int id : ids) {
            for (size_t i = 0; i < fg.nodes.size(); ++i)
                if (fg.nodes[i].id == id) {
                    fg.nodes.erase(fg.nodes.begin() + i);
                    changed = true;
                    break;
                }
            for (size_t i = fg.links.size(); i-- > 0;)
                if (fg.links[i].fromNode == id || fg.links[i].toNode == id)
                    fg.links.erase(fg.links.begin() + i);
        }
        ImNodes::ClearNodeSelection();
    };
    // Zoom keeping `pivot` (screen) fixed. flowZoom_ keeps the CONTINUOUS
    // request so small wheel notches accumulate; the pan correction and the
    // re-push are driven by the snapped, effective scale, or a notch that does
    // not reach the next whole font pixel would pan without zooming.
    auto applyZoom = [&](float next, ImVec2 pivot) {
        next = ImClamp(next, 0.4f, 1.8f);
        const float nextEff = ImMax(1.0f, IM_ROUND(uiFontPx * next)) / uiFontPx;
        flowZoom_ = next;
        if (nextEff != zoom) {
            const float ratio = nextEff / zoom;
            ImVec2 pan = ImNodes::EditorContextGetPanning();
            const float relX = pivot.x - canvasOrigin.x;
            const float relY = pivot.y - canvasOrigin.y;
            pan.x = relX - ratio * (relX - pan.x);
            pan.y = relY - ratio * (relY - pan.y);
            ImNodes::EditorContextResetPanning(pan);
            flowPositionsApplied_ = false;  // re-push positions at the new scale
        }
    };
    // Frame all: the zoom (never past 100%) and pan that fit every node, in
    // model units - node sizes come from this frame's draw.
    auto frameAll = [&]() {
        if (fg.nodes.empty()) {
            ImNodes::EditorContextResetPanning(ImVec2(0, 0));
            return;
        }
        float x0 = FLT_MAX, y0 = FLT_MAX, x1 = -FLT_MAX, y1 = -FLT_MAX;
        for (const FlowNode& n : fg.nodes) {
            const ImVec2 d = ImNodes::GetNodeDimensions(n.id);
            x0 = ImMin(x0, n.pos[0]);
            y0 = ImMin(y0, n.pos[1]);
            x1 = ImMax(x1, n.pos[0] + d.x / nodeScale);
            y1 = ImMax(y1, n.pos[1] + d.y / nodeScale);
        }
        const float margin = 48.0f;
        const float fit = ImMin(dbgCanvasSize.x / (x1 - x0 + 2.0f * margin),
                                dbgCanvasSize.y / (y1 - y0 + 2.0f * margin));
        const float z = ImClamp(fit / fontMul, 0.4f, 1.0f);
        flowZoom_ = z;
        const float ns = ImMax(1.0f, IM_ROUND(uiFontPx * z)) / uiFontSize;
        ImNodes::EditorContextResetPanning(ImVec2(dbgCanvasSize.x * 0.5f - (x0 + x1) * 0.5f * ns,
                                                  dbgCanvasSize.y * 0.5f - (y0 + y1) * 0.5f * ns));
        flowPositionsApplied_ = false;
    };
    // Tidy: columns by execution depth (longest exec path from a trigger);
    // data nodes sit one column left of the earliest node they feed; rows keep
    // their current top-to-bottom order.
    auto tidy = [&]() {
        const int N = (int)fg.nodes.size();
        if (N == 0) return;
        std::map<int, int> idx;
        for (int i = 0; i < N; ++i) idx[fg.nodes[i].id] = i;
        std::vector<int> depth(N, 0);
        std::vector<char> pureNode(N, 0);
        for (int i = 0; i < N; ++i) {
            const FlowNodeType* t = flowNodeType(fg.nodes[i].type);
            pureNode[i] = t && t->pure && !t->trigger;
        }
        for (int it = 0; it < N; ++it) {
            bool moved = false;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkExec) continue;
                auto a = idx.find(l.fromNode), b = idx.find(l.toNode);
                if (a == idx.end() || b == idx.end()) continue;
                if (depth[b->second] < depth[a->second] + 1 && depth[a->second] + 1 < N) {
                    depth[b->second] = depth[a->second] + 1;
                    moved = true;
                }
            }
            if (!moved) break;
        }
        std::vector<char> placed(N, 0);
        for (int it = 0; it < N; ++it) {
            bool moved = false;
            for (const FlowLink& l : fg.links) {
                if (l.kind == FlowLinkExec) continue;
                auto a = idx.find(l.fromNode), b = idx.find(l.toNode);
                if (a == idx.end() || b == idx.end() || !pureNode[a->second]) continue;
                const int want = depth[b->second] - 1;
                if (!placed[a->second] || want < depth[a->second]) {
                    if (depth[a->second] != want || !placed[a->second]) moved = true;
                    depth[a->second] = want;
                    placed[a->second] = 1;
                }
            }
            if (!moved) break;
        }
        const int minD = *std::min_element(depth.begin(), depth.end());
        int maxD = 0;
        for (int& d : depth) maxD = std::max(maxD, d -= minD);
        std::vector<float> colW(maxD + 1, 0.0f);
        std::vector<ImVec2> dims(N);
        for (int i = 0; i < N; ++i) {
            const ImVec2 d = ImNodes::GetNodeDimensions(fg.nodes[i].id);
            dims[i] = ImVec2(d.x / nodeScale, d.y / nodeScale);
            colW[depth[i]] = ImMax(colW[depth[i]], dims[i].x);
        }
        float x = 40.0f;
        for (int c = 0; c <= maxD; ++c) {
            std::vector<int> col;
            for (int i = 0; i < N; ++i)
                if (depth[i] == c) col.push_back(i);
            std::stable_sort(col.begin(), col.end(), [&](int a, int b) {
                return fg.nodes[a].pos[1] < fg.nodes[b].pos[1];
            });
            float y = 40.0f;
            for (int i : col) {
                fg.nodes[i].pos[0] = x;
                fg.nodes[i].pos[1] = y;
                y += dims[i].y + 28.0f;
            }
            x += colW[c] + 72.0f;
        }
        flowPositionsApplied_ = false;
        changed = true;
    };

    if (flowEditorCtx_) ImNodes::EditorContextSet((ImNodesEditorContext*)flowEditorCtx_);
    ImNodes::BeginNodeEditor();

    // Push stored node positions into imnodes whenever the edited graph, the
    // zoom or the UI scale changes (node ids repeat across graphs; positions
    // are stored unscaled and multiplied on the way in / divided on the way
    // out). The factor is nodeScale, the SAME one the node contents use: a
    // stored position is a distance between nodes, so if it kept only the zoom
    // while the nodes themselves also grew with the UI scale, a 250% editor
    // would draw a graph whose nodes overlap each other.
    if (!flowPositionsApplied_) {
        flowPositionsApplied_ = true;
        for (const FlowNode& n : fg.nodes)
            ImNodes::SetNodeGridSpacePos(
                n.id, ImVec2(n.pos[0] * nodeScale, n.pos[1] * nodeScale));
    }

    const float fireW = ImGui::CalcTextSize("Fire").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    for (FlowNode& n : fg.nodes) {
        const FlowNodeType* t = flowNodeType(n.type);
        if (!t) continue;

        // The title is quiet - the kind is a small square in its colour - and
        // the Live Debugger turns the whole bar into the activity light.
        const ImVec4 kindC = flowKindColor(*t);
        ImVec4 titleV = fgMix(sem.surface, kindC, 0.18f);
        titleV.w = 1.0f;
        if (const float g = dbgGlow(dbgKeyOfNode(n.id)); g > 0.0f)
            titleV = fgMix(titleV, sem.warn, 0.8f * g);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, ImGui::GetColorU32(titleV));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered,
                                ImGui::GetColorU32(fgMix(titleV, sem.text, 0.06f)));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected,
                                ImGui::GetColorU32(fgMix(titleV, sem.accent, 0.25f)));

        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        {
            const float startX = ImGui::GetCursorPosX();
            const float fsN = ImGui::GetFontSize();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, p.y + fsN * 0.32f), ImVec2(p.x + fsN * 0.36f, p.y + fsN * 0.68f),
                ImGui::GetColorU32(kindC), fsN * 0.08f);
            ImGui::Dummy(ImVec2(fsN * 0.36f, fsN));
            ImGui::SameLine(0.0f, fsN * 0.45f);
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted(t->title);
            }
            const int key = dbgKeyOfNode(n.id);
            if (dbgOverlay && t->trigger && key >= 0) {
                // Fire the branch under this trigger in the running game.
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImMax(ImGui::GetCursorPosX(), startX + nodeWidth - fireW));
                ImGui::PushID(n.id);
                if (ImGui::SmallButton("Fire")) dbgFireNode(key, false);
                ImGui::PopID();
            } else if (liveLogicState_ != LogicState::Off && !flowLiveSupported(n.type)) {
                // Live Logic cannot patch this node: the inspector says why.
                ImGui::SameLine(0.0f, 0.0f);
                const float x = ImMax(ImGui::GetCursorPosX() + fsN * 0.5f,
                                      startX + nodeWidth - fsN * 0.4f);
                ImGui::SetCursorPosX(x);
                const ImVec2 dp = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(dp.x + fsN * 0.2f, dp.y + fsN * 0.5f), fsN * 0.2f,
                    ImGui::GetColorU32(sem.warn));
                ImGui::Dummy(ImVec2(fsN * 0.4f, fsN));
            }
        }
        ImNodes::EndNodeTitleBar();

        // Compact body: value chips, wrapped to the node width, and a problem
        // in one line if there is one.
        {
            const std::vector<std::string> chips = chipsFor(n, *t);
            const float sp = ImGui::GetStyle().ItemInnerSpacing.x;
            float used = 0.0f;
            for (size_t c = 0; c < chips.size(); ++c) {
                float cw;
                {
                    ui::FontScope mono(ui::monoFont());
                    cw = ImGui::CalcTextSize(chips[c].c_str()).x + ImGui::GetFontSize() * 0.8f;
                }
                if (c > 0 && used + sp + cw <= nodeWidth) {
                    ImGui::SameLine(0.0f, sp);
                    used += sp + cw;
                } else {
                    used = cw;
                }
                ui::ValueChip(chips[c].c_str(), chips[c] == "none" || chips[c] == "self");
            }
            if (const char* pr = flowNodeProblem(project_, n)) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + nodeWidth);
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                ImGui::TextWrapped("%s", pr);
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
        }

        // pins: exec flow (round) + object id (square, amber) + position
        // (triangle, green) + bool value (circle, violet) + text (circle,
        // cyan). Pure data nodes have no exec pins; bool-in and text-in pins
        // accept several links (folded / concatenated). A pin with nothing
        // wired to it is drawn hollow.
        const unsigned idPinCol = IM_COL32(222, 170, 60, 255);
        const unsigned posPinCol = IM_COL32(110, 200, 120, 255);
        const unsigned boolPinCol = IM_COL32(180, 120, 220, 255);
        const unsigned textPinCol = IM_COL32(90, 190, 210, 255);
        const unsigned numPinCol = IM_COL32(230, 120, 155, 255);
        if (t->idIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginInputAttribute(flowIdInPin(n.id),
                                         pinShape(flowIdInPin(n.id), ImNodesPinShape_QuadFilled));
            ImGui::TextDisabled("object");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginInputAttribute(flowPosInPin(n.id),
                                         pinShape(flowPosInPin(n.id), ImNodesPinShape_TriangleFilled));
            ImGui::TextDisabled("position");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginInputAttribute(flowBoolInPin(n.id),
                                         pinShape(flowBoolInPin(n.id), ImNodesPinShape_CircleFilled));
            ImGui::TextDisabled("bool");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->textIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, textPinCol);
            ImNodes::BeginInputAttribute(flowTextInPin(n.id),
                                         pinShape(flowTextInPin(n.id), ImNodesPinShape_CircleFilled));
            ImGui::TextDisabled("text");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->numIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, numPinCol);
            ImNodes::BeginInputAttribute(flowNumInPin(n.id),
                                         pinShape(flowNumInPin(n.id), ImNodesPinShape_CircleFilled));
            // A Math node folds every wired input, so say so on the pin: it is
            // the difference between "a + b + c" and "the first link wins".
            // Everything else REPLACES num[0], so the pin takes that param's
            // own name - "slot", "value", "radius".
            std::string numPinLabel = "number";
            if (flowNumFolds(*t)) {
                numPinLabel = "numbers";
            } else if (t->numCount > 0 && t->numLabels[0] && t->numLabels[0][0]) {
                numPinLabel = t->numLabels[0];
                for (char& ch : numPinLabel) ch = (char)tolower((unsigned char)ch);
            }
            ImGui::TextDisabled("%s", numPinLabel.c_str());
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (!t->pure) {
            if (!t->trigger) {
                // One pin per exec input: a plain action draws the lone
                // "> do", a merged one a labeled pin per branch (show/hide).
                const int execIns = t->execInCount < 1 ? 1 : t->execInCount;
                for (int e = 0; e < execIns; ++e) {
                    const int pin = flowExecInPin(n.id, e);
                    const bool w = wiredPins.count(pin) > 0;
                    ImNodes::BeginInputAttribute(pin, pinShape(pin, ImNodesPinShape_CircleFilled));
                    if (w) ImGui::Text("> %s", flowExecInLabel(*t, e));
                    else ImGui::TextDisabled("> %s", flowExecInLabel(*t, e));
                    // A merged node's pins ARE its parameters (which branch you
                    // fire is a choice), so they get the same hover treatment.
                    paramTip(flowExecInTip(*t, e));
                    ImNodes::EndInputAttribute();
                }
            }
            // One pin per exec output: a trigger's "then", an execThrough
            // action's "after", or a flow-control node's labeled branches.
            const int execOuts = flowExecOutCount(*t);
            for (int e = 0; e < execOuts; ++e) {
                const int pin = flowExecOutPin(n.id, e);
                ImNodes::BeginOutputAttribute(pin, pinShape(pin, ImNodesPinShape_CircleFilled));
                rightLabel(flowExecOutLabel(*t, e), wiredPins.count(pin) == 0);
                ImNodes::EndOutputAttribute();
            }
        }
        if (t->idOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginOutputAttribute(flowIdOutPin(n.id),
                                          pinShape(flowIdOutPin(n.id), ImNodesPinShape_QuadFilled));
            rightLabel("object", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginOutputAttribute(flowPosOutPin(n.id),
                                          pinShape(flowPosOutPin(n.id), ImNodesPinShape_TriangleFilled));
            rightLabel("position", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginOutputAttribute(flowBoolOutPin(n.id),
                                          pinShape(flowBoolOutPin(n.id), ImNodesPinShape_CircleFilled));
            rightLabel("bool", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->textOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, textPinCol);
            ImNodes::BeginOutputAttribute(flowTextOutPin(n.id),
                                          pinShape(flowTextOutPin(n.id), ImNodesPinShape_CircleFilled));
            rightLabel("text", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->numOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, numPinCol);
            ImNodes::BeginOutputAttribute(flowNumOutPin(n.id),
                                          pinShape(flowNumOutPin(n.id), ImNodesPinShape_CircleFilled));
            rightLabel("number", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }

        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
    }

    for (const FlowLink& l : fg.links) {
        if (l.kind == FlowLinkObject) {
            // object links amber, position links green, exec the default
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(222, 170, 60, 255));
            ImNodes::Link(l.id, flowIdOutPin(l.fromNode), flowIdInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkPos) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(110, 200, 120, 255));
            ImNodes::Link(l.id, flowPosOutPin(l.fromNode), flowPosInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkBool) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(180, 120, 220, 255));
            ImNodes::Link(l.id, flowBoolOutPin(l.fromNode), flowBoolInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkText) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(90, 190, 210, 255));
            ImNodes::Link(l.id, flowTextOutPin(l.fromNode), flowTextInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkNum) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(230, 120, 155, 255));
            ImNodes::Link(l.id, flowNumOutPin(l.fromNode), flowNumInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (const float g = dbgGlow(dbgKeyOfNode(l.fromNode)); g > 0.0f) {
            // Live Debugger: the exec chain that just ran, lit and thickened -
            // this is what makes the flow of a frame readable at a glance.
            ImNodes::PushColorStyle(
                ImNodesCol_Link,
                (ImU32)ImColor(1.0f, 0.55f + 0.35f * g, 0.20f,
                               0.55f + 0.45f * g));
            ImNodes::PushStyleVar(ImNodesStyleVar_LinkThickness,
                                  (3.0f + 2.0f * g) * nodeScale);
            ImNodes::Link(l.id, flowExecOutPin(l.fromNode, l.fromPin),
                          flowExecInPin(l.toNode, l.toPin));
            ImNodes::PopStyleVar();
            ImNodes::PopColorStyle();
        } else {
            ImNodes::Link(l.id, flowExecOutPin(l.fromNode, l.fromPin),
                          flowExecInPin(l.toNode, l.toPin));
        }
    }

    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    const bool editorHovered = ImNodes::IsEditorHovered();
    ImNodes::EndNodeEditor();

    // The selection the inspector edits, and selection requests (problem
    // list, follow-the-halt, the inspector's pin links) applied now that the
    // nodes exist in the editor.
    if (flowSelectReq_ >= 0 && flowPositionsApplied_) {
        for (const FlowNode& n : fg.nodes)
            if (n.id == flowSelectReq_) {
                ImNodes::ClearNodeSelection();
                ImNodes::SelectNode(n.id);
                // and bring it into view
                const ImVec2 d = ImNodes::GetNodeDimensions(n.id);
                ImNodes::EditorContextResetPanning(
                    ImVec2(dbgCanvasSize.x * 0.5f - n.pos[0] * nodeScale - d.x * 0.5f,
                           dbgCanvasSize.y * 0.4f - n.pos[1] * nodeScale - d.y * 0.5f));
            }
        flowSelectReq_ = -1;
    }
    if (const int ns = ImNodes::NumSelectedNodes(); ns > 0) {
        selNodes.resize(ns);
        ImNodes::GetSelectedNodes(selNodes.data());
    }


    // Live: a click in the gutter left of a node's title toggles its breakpoint
    // (the dot the badges above draw there).
    if (dbgOverlay && editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        for (const FlowNode& n : fg.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (!t || (t->pure && !t->trigger)) continue;
            const ImVec2 p = ImNodes::GetNodeScreenSpacePos(n.id);
            const float g = 22.0f * nodeScale;
            if (m.x >= p.x - g && m.x < p.x - 1.0f && m.y >= p.y && m.y < p.y + g) {
                dbgToggleBreakpoint(owner.id, n.id);
                break;
            }
        }
    }

    // Live Debugger badges: a breakpoint marker in a gutter LEFT of the node
    // plus a bar down its edge, the cumulative hit count under the bottom-right
    // corner, and a fading ring around whatever just ran.
    //
    // These go into their own borderless, input-less child window laid over the
    // canvas rect - NOT into the Flow Graph window's draw list. imnodes runs its
    // editor in a child window, and a child renders ON TOP of its parent's
    // content, so anything drawn into the parent list after EndNodeEditor ends
    // up UNDERNEATH the nodes (that is exactly why the first breakpoint dot was
    // barely visible). A later sibling child draws on top of an earlier one.
    if (dbgOverlay) {
        ImGui::SetCursorScreenPos(dbgCanvasMin);
        ImGui::BeginChild("##dbg_overlay", dbgCanvasSize, false,
                          ImGuiWindowFlags_NoInputs |
                              ImGuiWindowFlags_NoBackground |
                              ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        // Badge sizes follow the node they annotate: at 40% zoom an unscaled
        // gutter dot is as wide as the node it sits next to.
        auto zs = [&](float px) { return px * nodeScale; };
        for (const FlowNode& n : fg.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (!t || (t->pure && !t->trigger)) continue;  // pure data node
            const int key = dbgKeyOfNode(n.id);
            const bool bp = dbgHasBreakpoint(dbgOwnerId, n.id);
            const float g = dbgGlow(key);
            if (key < 0 && !bp) continue;
            const ImVec2 p = ImNodes::GetNodeScreenSpacePos(n.id);
            const ImVec2 d = ImNodes::GetNodeDimensions(n.id);
            if (g > 0.0f) {
                const float pad = zs(3.0f) + zs(3.0f) * g;
                dl->AddRect(ImVec2(p.x - pad, p.y - pad),
                            ImVec2(p.x + d.x + pad, p.y + d.y + pad),
                            ImColor(1.0f, 0.72f, 0.22f, 0.25f + 0.65f * g),
                            zs(6.0f), 0, zs(2.0f) + zs(1.5f) * g);
            }
            if (bp) {
                const bool stopped = dbgState_ == DbgState::Halted &&
                                     dbgSnap_.breakKey == key;
                const ImU32 col = stopped ? IM_COL32(255, 205, 70, 255)
                                          : IM_COL32(230, 60, 55, 255);
                // Edge bar: readable even zoomed out, when a dot is one pixel.
                dl->AddRectFilled(ImVec2(p.x - zs(2.0f), p.y),
                                  ImVec2(p.x + zs(2.0f), p.y + d.y), col,
                                  zs(2.0f));
                // Gutter marker, like a breakpoint in an IDE: outside the node
                // so it never fights the title text, with a dark ring for
                // contrast against any background.
                const float r = zs(7.0f);
                const ImVec2 c(p.x - r - zs(4.0f), p.y + zs(9.0f));
                dl->AddCircleFilled(c, r + zs(1.5f), IM_COL32(15, 15, 18, 220));
                dl->AddCircleFilled(c, r, col);
                dl->AddCircle(c, r, IM_COL32(255, 255, 255, 90), 0, zs(1.0f));
                if (stopped) {
                    // Halted here: a pulsing halo, so the eye finds it at once.
                    const float t2 = (float)ImGui::GetTime();
                    const float pulse = 0.5f + 0.5f * sinf(t2 * 6.0f);
                    dl->AddCircle(c, r + zs(3.0f) + zs(2.0f) * pulse,
                                  ImColor(1.0f, 0.85f, 0.35f, 0.35f + 0.4f * pulse),
                                  0, zs(2.0f));
                }
            }
            if (key >= 0 && key < (int)dbgSnap_.hits.size() &&
                dbgSnap_.hits[key] > 0) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%u", dbgSnap_.hits[key]);
                const ImVec2 ts = ImGui::CalcTextSize(buf);
                const ImVec2 at(p.x + d.x - ts.x - zs(4.0f),
                                p.y + d.y - ts.y - zs(2.0f));
                dl->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f),
                            IM_COL32(0, 0, 0, 160), buf);
                dl->AddText(at,
                            g > 0.0f ? IM_COL32(255, 225, 150, 255)
                                     : IM_COL32(170, 180, 195, 220),
                            buf);
            }
        }
        ImGui::EndChild();
    }

    // Canvas scaling ends here, BEFORE the tooltip below: a tooltip is a window
    // of its own and reads like the rest of the editor's chrome, not like a node.
    nstyle = savedStyle;
    ImGui::PopStyleVar(3);
    ImGui::PopFont();

    // --- floating canvas cards -------------------------------------------------
    // Children laid over the canvas: a later sibling child draws (and takes
    // the mouse) above the imnodes region, which a draw into this window's own
    // list would not (see the Live Debugger badges above).
    const float cfs = ImGui::GetFontSize();
    const float cpad = cfs * 0.6f;
    auto cardBegin = [&](const char* id, ImVec2 pos, ImVec2 size, bool inputs) {
        ImGui::SetCursorScreenPos(pos);
        ImVec4 bg = sem.surface;
        bg.w = 0.94f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border, sem.border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::GetStyle().FrameRounding + 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(cfs * 0.45f, cfs * 0.3f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(cfs * 0.3f, cfs * 0.3f));
        ImGuiChildFlags cf = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
        if (size.x == 0.0f) cf |= ImGuiChildFlags_AutoResizeX;
        if (size.y == 0.0f) cf |= ImGuiChildFlags_AutoResizeY;
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (!inputs) wf |= ImGuiWindowFlags_NoInputs;
        ImGui::BeginChild(id, size, cf, wf);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    };
    const ImVec2 cMin = dbgCanvasMin;
    const ImVec2 cMax(dbgCanvasMin.x + dbgCanvasSize.x, dbgCanvasMin.y + dbgCanvasSize.y);

    // Pin legend (top-left): what each pin shape and colour carries.
    {
        struct Leg { const char* name; ImU32 col; int shape; };  // 0 circle 1 quad 2 tri
        const Leg legs[] = {{"exec", ImGui::GetColorU32(sem.text), 0},
                            {"object", IM_COL32(222, 170, 60, 255), 1},
                            {"position", IM_COL32(110, 200, 120, 255), 2},
                            {"bool", IM_COL32(180, 120, 220, 255), 0},
                            {"text", IM_COL32(90, 190, 210, 255), 0},
                            {"number", IM_COL32(230, 120, 155, 255), 0}};
        float w = cpad;
        for (const Leg& l : legs) w += cfs * 0.9f + ImGui::CalcTextSize(l.name).x + cfs * 0.8f;
        const float h = ImGui::GetFrameHeight();
        // Shown only when it fits: a legend is a reminder, not worth a clip.
        if (w <= dbgCanvasSize.x - 2.0f * cpad) {
            cardBegin("##fg_legend", ImVec2(cMin.x + cpad, cMin.y + cpad), ImVec2(w, h), false);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 wp = ImGui::GetWindowPos();
            float x = wp.x + cpad;
            const float cy = wp.y + h * 0.5f;
            for (const Leg& l : legs) {
                const float r = cfs * 0.24f;
                if (l.shape == 1) dl->AddRectFilled(ImVec2(x, cy - r), ImVec2(x + 2 * r, cy + r), l.col);
                else if (l.shape == 2)
                    dl->AddTriangleFilled(ImVec2(x, cy - r), ImVec2(x + 2 * r, cy), ImVec2(x, cy + r), l.col);
                else dl->AddCircleFilled(ImVec2(x + r, cy), r, l.col);
                x += cfs * 0.9f;
                dl->AddText(ImVec2(x, cy - ImGui::GetTextLineHeight() * 0.5f),
                            ImGui::GetColorU32(sem.textDim), l.name);
                x += ImGui::CalcTextSize(l.name).x + cfs * 0.8f;
            }
            ImGui::EndChild();
        }
    }

    // Zoom card (bottom-left): zoom out / % / zoom in, Frame all (F), Tidy (L).
    bool wantFrame = false, wantTidy = false;
    {
        const float h = ImGui::GetFrameHeight() + cfs * 0.6f + 2.0f;
        cardBegin("##fg_zoom", ImVec2(cMin.x + cpad, cMax.y - cpad - h), ImVec2(0, 0), true);
        const ImVec2 mid(cMin.x + dbgCanvasSize.x * 0.5f, cMin.y + dbgCanvasSize.y * 0.5f);
        if (ui::GhostButton("-##fg_zoom_out")) applyZoom(flowZoom_ / 1.25f, mid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Zoom out (mouse wheel)");
        ImGui::SameLine();
        {
            ui::FontScope mono(ui::monoFont());
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%3.0f%%", zoom * 100.0f);
        }
        ImGui::SameLine();
        if (ui::GhostButton("+##fg_zoom_in")) applyZoom(flowZoom_ * 1.25f, mid);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Zoom in (mouse wheel)");
        ImGui::SameLine();
        auto keyed = [&](const char* label, const char* key) {
            const bool hit = ui::GhostButton(label);
            ImGui::SameLine(0.0f, cfs * 0.2f);
            const ImVec2 kp = ImGui::GetCursorScreenPos();
            const float kh = ImGui::GetFrameHeight() * 0.72f;
            const float kw = ui::Keycap(ImGui::GetWindowDrawList(),
                                        ImVec2(kp.x, kp.y + (ImGui::GetFrameHeight() - kh) * 0.5f), kh, key);
            ImGui::Dummy(ImVec2(kw, ImGui::GetFrameHeight()));
            return hit;
        };
        wantFrame |= keyed("Frame all", "F");
        ImGui::SameLine();
        wantTidy |= keyed("Tidy", "L");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Lay the graph out in columns, triggers first.");
        ImGui::EndChild();
    }

    // Empty graph: say so, and offer the first trigger.
    if (fg.nodes.empty()) {
        const float w = ImMin(scaled(360.0f), dbgCanvasSize.x - cpad * 2.0f);
        const float h = cfs * 9.5f;
        const ImVec2 at(cMin.x + (dbgCanvasSize.x - w) * 0.5f, cMin.y + (dbgCanvasSize.y - h) * 0.45f);
        cardBegin("##fg_empty", at, ImVec2(w, 0), true);
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float cw = ImGui::GetContentRegionAvail().x;
            dl->AddCircleFilled(ImVec2(p.x + cw * 0.5f, p.y + cfs * 1.3f), cfs * 1.2f,
                                ImGui::GetColorU32(sem.field), 32);
            ui::DrawGlyph(dl, ui::Glyph::Nodes, ImVec2(p.x + cw * 0.5f, p.y + cfs * 1.3f), cfs * 1.3f,
                          ImGui::GetColorU32(sem.textDim));
            ImGui::Dummy(ImVec2(cw, cfs * 2.8f));
            const std::string title = owner.name + " has no logic yet";
            {
                ui::FontScope bold(ui::boldFont());
                const float tw = ImGui::CalcTextSize(title.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, (cw - tw) * 0.5f));
                ImGui::TextUnformatted(title.c_str());
            }
            ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("Start with a trigger. Space or a right-click on the canvas adds "
                               "any node; drag from a pin to wire it.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::Spacing();
            const ImVec2 mid(cMin.x + dbgCanvasSize.x * 0.35f, cMin.y + dbgCanvasSize.y * 0.4f);
            if (const FlowNodeType* st = flowNodeType("OnStart"); st && ui::PrimaryButton("On Start"))
                makeNode(*st, mid);
            ImGui::SameLine();
            if (const FlowNodeType* ut = flowNodeType("OnUsed"); ut && ui::GhostButton("On Used"))
                makeNode(*ut, mid);
            ImGui::SameLine();
            if (ui::GhostButton("Add node..."))
                openPalette(ImVec2(cMin.x + dbgCanvasSize.x * 0.5f - scaled(170.0f), cMin.y + cfs * 3.0f), -1);
        }
        ImGui::EndChild();
    }

    // Halted: the transport lives where you are looking - why it stopped,
    // Continue (F10), Step node, Step frame (F11).
    auto dbgSend = [&](bool halt, int frames, bool untilFire) {
        dbgCmd_.halt = halt;
        dbgCmd_.stepFrames = frames;
        dbgCmd_.stepUntilFire = untilFire;
        dbgCmdWritten_ = false;
        dbgScrub_ = -1;
    };
    std::string haltWhat, haltWhere;
    bool haltOnBp = false;
    if (dbgHalted) {
        if (const livedbg::NodeSym* ns = dbgSyms_.find(dbgSnap_.breakKey)) {
            const FlowNodeType* bt = flowNodeType(ns->type);
            haltWhat = bt ? bt->title : ns->type;
            haltOnBp = dbgHasBreakpoint(ns->objectId, ns->nodeId);
            for (const SceneObject& o : project_.objects())
                if (o.id == ns->objectId) haltWhere = o.name;
        }
        const std::string lead = haltWhat.empty() ? std::string("Halted")
                                                  : "Stopped at " + haltWhat;
        const std::string why = haltWhat.empty() ? std::string("paused")
                                : haltOnBp       ? std::string("breakpoint")
                                                 : std::string("step");
        const ImGuiStyle& st = ImGui::GetStyle();
        const float bw = [&](const char* s) {
            return ImGui::CalcTextSize(s).x + st.FramePadding.x * 2.6f;
        }("Continue") + [&](const char* s) {
            return ImGui::CalcTextSize(s).x + st.FramePadding.x * 2.6f;
        }("Step node") + [&](const char* s) {
            return ImGui::CalcTextSize(s).x + st.FramePadding.x * 2.6f;
        }("Step frame");
        const float w = cfs * 1.2f + ImGui::CalcTextSize(lead.c_str()).x +
                        ImGui::CalcTextSize(why.c_str()).x + cfs * 1.6f + bw + cfs * 6.0f;
        const float x = ImMax(cMin.x + cpad, cMin.x + (dbgCanvasSize.x - w) * 0.5f);
        cardBegin("##fg_transport", ImVec2(x, cMin.y + cpad + ImGui::GetFrameHeight() + cfs * 0.5f),
                  ImVec2(0, 0), true);
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(p.x + cfs * 0.35f, p.y + ImGui::GetFrameHeight() * 0.5f), cfs * 0.22f,
                ImGui::GetColorU32(sem.warn));
            ImGui::Dummy(ImVec2(cfs * 0.7f, ImGui::GetFrameHeight()));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(lead.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("\xc2\xb7 %s", why.c_str());
            if (!haltWhere.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("In %s's graph, frame %u.", haltWhere.c_str(), dbgSnap_.frame);
            ImGui::SameLine(0.0f, cfs);
            if (ui::PrimaryButton("Continue")) dbgSend(false, 0, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("F10");
            ImGui::SameLine();
            if (ui::GhostButton("Step node")) dbgSend(false, 0, true);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Run until any instrumented node fires, then stop on it.");
            ImGui::SameLine();
            if (ui::GhostButton("Step frame")) dbgSend(false, 1, false);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("F11. Run exactly one frame, then stop again.");
        }
        ImGui::EndChild();
    }

    // Live: breakpoints are a click in the gutter left of a node's title.
    if (dbgOverlay) {
        const char* hint = "Click left of a node's title to set a breakpoint";
        const ImVec2 hs = ImGui::CalcTextSize(hint);
        const float zoomCardW = scaled(300.0f);
        if (dbgCanvasSize.x > zoomCardW + hs.x + cfs * 4.0f) {
            ImGui::SetCursorScreenPos(ImVec2(cMin.x + zoomCardW, cMax.y - cpad - hs.y - cfs * 0.4f));
            ImGui::BeginChild("##fg_livehint", ImVec2(hs.x + cfs, hs.y + 4.0f), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground);
            ImGui::TextDisabled("%s", hint);
            ImGui::EndChild();
        }
    }

    // Rest the mouse on a node to get its documentation (flowNodeDoc - the same
    // renderer the add-menu tooltips use). Delayed so it never flickers while
    // wiring/dragging; any mouse button suppresses it. A PARAMETER tip that
    // already fired this frame suppresses it too: the cursor is inside the node
    // either way, so without that the specific answer and the general one would
    // be drawn one on top of the other.
    {
        int hovered = -1;
        if (ImNodes::IsNodeHovered(&hovered) && !ImGui::IsAnyMouseDown() &&
            !paramTipShown) {
            if (hovered != flowDescNode_) {
                flowDescNode_ = hovered;
                flowDescSince_ = ImGui::GetTime();
            } else if (ImGui::GetTime() - flowDescSince_ > 0.6) {
                const FlowNodeType* ht = nullptr;
                for (const FlowNode& n : fg.nodes)
                    if (n.id == hovered) ht = flowNodeType(n.type);
                if (ht && ht->desc && *ht->desc) {
                    ImGui::BeginTooltip();
                    flowNodeDoc(*ht, scaled(360.0f));
                    ImGui::EndTooltip();
                }
            }
        } else {
            flowDescNode_ = -1;
        }
    }

    // Read node positions back in unscaled model coordinates (imnodes owns the
    // scaled ones while dragging)
    for (FlowNode& n : fg.nodes) {
        const ImVec2 pos = ImNodes::GetNodeGridSpacePos(n.id);
        n.pos[0] = pos.x / nodeScale;
        n.pos[1] = pos.y / nodeScale;
    }

    // Mouse wheel over the canvas: zoom around the cursor.
    if (editorHovered && ImGui::GetIO().MouseWheel != 0.0f)
        applyZoom(flowZoom_ * ImPow(1.1f, ImGui::GetIO().MouseWheel), ImGui::GetIO().MousePos);

    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) connectPins(startPin, endPin);
    // A wire dropped on empty canvas: the palette, filtered to nodes that can
    // take it, and the chosen node gets wired.
    {
        int droppedFrom = -1;
        if (ImNodes::IsLinkDropped(&droppedFrom, false) && droppedFrom >= 0)
            openPalette(ImGui::GetIO().MousePos, droppedFrom);
    }

    // Copy/paste nodes. When this window has focus, Ctrl+C/V operate on the
    // graph instead of the scene objects (the global handler stands down while
    // flowGraphFocused_ is set). Skip while typing in a node param so Ctrl+C
    // still copies text there.
    const bool fgFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    flowGraphFocused_ = fgFocused;
    if (fgFocused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            const int numSel = ImNodes::NumSelectedNodes();
            if (numSel > 0) {
                std::vector<int> ids(numSel);
                ImNodes::GetSelectedNodes(ids.data());
                flowClipboard_ = FlowGraph{};
                for (int id : ids)
                    for (const FlowNode& n : fg.nodes)
                        if (n.id == id) flowClipboard_.nodes.push_back(n);
                auto copied = [&](int id) {
                    for (const FlowNode& n : flowClipboard_.nodes)
                        if (n.id == id) return true;
                    return false;
                };
                for (const FlowLink& l : fg.links)
                    if (copied(l.fromNode) && copied(l.toNode))
                        flowClipboard_.links.push_back(l);
                statusMessage_ =
                    "Copied " + std::to_string(flowClipboard_.nodes.size()) +
                    (flowClipboard_.nodes.size() == 1 ? " node" : " nodes");
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) &&
            !flowClipboard_.nodes.empty()) {
            // Fresh ids from the target graph so a paste into the same or a
            // different graph never collides; links are remapped to them.
            std::vector<std::pair<int, int>> idMap;  // old id -> new id
            for (const FlowNode& src : flowClipboard_.nodes) {
                FlowNode n = src;
                n.id = fg.nextId++;
                n.pos[0] += 20.0f;  // offset so the paste sits beside the source
                n.pos[1] += 20.0f;
                idMap.push_back({src.id, n.id});
                fg.nodes.push_back(n);
            }
            auto mapId = [&](int old) {
                for (const auto& p : idMap)
                    if (p.first == old) return p.second;
                return -1;
            };
            for (const FlowLink& src : flowClipboard_.links) {
                FlowLink l = src;
                l.id = fg.nextId++;
                l.fromNode = mapId(src.fromNode);
                l.toNode = mapId(src.toNode);
                fg.links.push_back(l);
            }
            flowPositionsApplied_ = false;  // push the pasted node positions
            changed = true;
            statusMessage_ =
                "Pasted " + std::to_string(flowClipboard_.nodes.size()) +
                (flowClipboard_.nodes.size() == 1 ? " node" : " nodes");
        }
    }

    // Delete selection (not while typing in a field - Delete edits text there)
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        const int numLinks = ImNodes::NumSelectedLinks();
        if (numLinks > 0) {
            std::vector<int> ids(numLinks);
            ImNodes::GetSelectedLinks(ids.data());
            for (int id : ids)
                for (size_t i = 0; i < fg.links.size(); ++i)
                    if (fg.links[i].id == id) {
                        fg.links.erase(fg.links.begin() + i);
                        changed = true;
                        break;
                    }
        }
        if (!selNodes.empty()) {
            deleteNodes(selNodes);
            selNodes.clear();
        }
    }
    // Canvas keys: Space = add node at the cursor, F = frame all, L = tidy.
    if (fgFocused && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            const bool inside = m.x >= cMin.x && m.y >= cMin.y && m.x < cMax.x && m.y < cMax.y;
            openPalette(inside ? m : ImVec2(cMin.x + dbgCanvasSize.x * 0.3f, cMin.y + dbgCanvasSize.y * 0.3f), -1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false) && !ImGui::GetIO().KeyCtrl) wantFrame = true;
        if (ImGui::IsKeyPressed(ImGuiKey_L, false) && !ImGui::GetIO().KeyCtrl) wantTidy = true;
    }

    // Right-click on a node: the debugger's per-node actions (breakpoint,
    // force-fire). Right-click on empty canvas opens the add-node palette.
    int dbgCtxHovered = -1;
    if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        ImNodes::IsNodeHovered(&dbgCtxHovered)) {
        flowCtxNode_ = dbgCtxHovered;
        ImGui::OpenPopup("##flow_node_ctx");
    } else if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        openPalette(ImGui::GetIO().MousePos, -1);
    if (ImGui::BeginPopup("##flow_node_ctx")) {
        const FlowNode* cn = nullptr;
        for (const FlowNode& n : fg.nodes)
            if (n.id == flowCtxNode_) cn = &n;
        const FlowNodeType* ct = cn ? flowNodeType(cn->type) : nullptr;
        if (!cn || !ct) {
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextDisabled("%s", ct->title);
            ImGui::Separator();
            const std::string ownerId =
                flowGraphObject_ >= 0 &&
                        flowGraphObject_ < (int)project_.objects().size()
                    ? project_.objects()[flowGraphObject_].id
                    : std::string();
            const bool dataNode = ct->pure && !ct->trigger;
            const int key = dbgKeyOfNode(cn->id);
            if (dataNode) {
                ImGui::TextDisabled(
                    "A data node - it has no moment of its own to break on.");
            } else {
                const bool bp = dbgHasBreakpoint(ownerId, cn->id);
                if (ImGui::MenuItem(bp ? "Remove breakpoint" : "Set breakpoint",
                                    nullptr, bp))
                    dbgToggleBreakpoint(ownerId, cn->id);
                const bool live = dbgState_ == DbgState::Running ||
                                  dbgState_ == DbgState::Halted;
                if (ct->trigger) {
                    if (ImGui::MenuItem("Fire now (one frame)", nullptr, false,
                                        live && key >= 0))
                        dbgFireNode(key, false);
                    if (ImGui::MenuItem("Fire and continue", nullptr, false,
                                        live && key >= 0))
                        dbgFireNode(key, true);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip(
                            "Use this when the branch only ARMS something - a "
                            "Delay, a Move Object To glide.\n"
                            "One frame is enough "
                            "to run the branch, but a countdown needs the game "
                            "to keep\n"
                            "running to reach zero.");
                }
                if (!live)
                    ImGui::TextDisabled(
                        "(no game reporting - breakpoints arm on the next run)");
                if (key >= 0 && key < (int)dbgSnap_.hits.size())
                    ImGui::TextDisabled("%u hits so far", dbgSnap_.hits[key]);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Open the Debugger panel")) showDebugger_ = true;
        }
        ImGui::EndPopup();
    }

    // --- the add-node palette -----------------------------------------------------
    // Search first, categories as chips, one line of what each node does, the
    // keyboard to pick. Opened by Space, a right-click on empty canvas, or a
    // wire dropped on empty canvas (then only nodes that can take that wire).
    if (flowPalOpen_) {
        ImGui::OpenPopup("##flow_add_node");
        flowPalOpen_ = false;
        flowPalFocus_ = true;
        flowPalQuery_.clear();
        flowPalSel_ = 0;
        flowPalCat_ = -1;
        flowPalMoreCats_ = false;
    }
    ImGui::SetNextWindowPos(flowPalPos_, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(scaled(360.0f), 0.0f));
    if (ImGui::BeginPopup("##flow_add_node")) {
        const std::vector<const FlowNodeType*> all = flowAllNodeTypes();
        const std::vector<const char*> cats = flowNodeCategories();
        const int fromKind = flowPalFromPin_ >= 0 ? flowPinKind(flowPalFromPin_) : -1;
        // the matching list
        std::vector<std::pair<int, const FlowNodeType*>> hits;
        for (size_t k = 0; k < all.size(); ++k) {
            const FlowNodeType& t = *all[k];
            if (flowPalCat_ >= 0 && flowPalCat_ < (int)cats.size() &&
                std::strcmp(t.category, cats[flowPalCat_]) != 0)
                continue;
            if (fromKind >= 0 && flowCompatiblePin(t, 0, fromKind) < 0) continue;
            const int s = flowPaletteScore(t, flowPalQuery_);
            if (s <= 0) continue;
            // ties: the shorter title (the more exact match), then registry order
            int rank = s * 100000 - (int)std::strlen(t.title) * 300 - (int)k;
            if (flowPalQuery_.empty()) {
                // Nothing typed: the recent ones first, then the registry.
                rank = -(int)k;
                for (size_t r = 0; r < flowRecent_.size(); ++r)
                    if (flowRecent_[r] == t.key) rank = 1000000 - (int)r;
            }
            hits.push_back({rank, &t});
        }
        std::stable_sort(hits.begin(), hits.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        if (flowPalSel_ < 0) flowPalSel_ = 0;
        if (flowPalSel_ >= (int)hits.size()) flowPalSel_ = hits.empty() ? 0 : (int)hits.size() - 1;

        // search field + count
        if (flowPalFocus_) {
            ImGui::SetKeyboardFocusHere();
            flowPalFocus_ = false;
        }
        char qbuf[96];
        std::snprintf(qbuf, sizeof(qbuf), "%s", flowPalQuery_.c_str());
        char cnt[24];
        std::snprintf(cnt, sizeof(cnt), "%d nodes", (int)hits.size());
        const float cntW = ImGui::CalcTextSize(cnt).x;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - cntW - ImGui::GetStyle().ItemSpacing.x);
        const int prevSel = flowPalSel_;
        const bool enter = ImGui::InputTextWithHint(
            "##nodesearch", fromKind >= 0 ? "Search nodes that take this wire..." : "Search nodes...",
            qbuf, sizeof(qbuf),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
            flowPaletteKeys, &flowPalSel_);
        if (std::string(qbuf) != flowPalQuery_) {
            flowPalQuery_ = qbuf;
            flowPalSel_ = 0;
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", cnt);
        // category chips: All, the common ones, then +N for the rest
        {
            static const char* kCommon[] = {"Triggers", "Object", "Flow", "Audio"};
            std::vector<int> shown;
            for (const char* c : kCommon)
                for (int i = 0; i < (int)cats.size(); ++i)
                    if (std::strcmp(cats[i], c) == 0) shown.push_back(i);
            if (flowPalMoreCats_) {
                shown.clear();
                for (int i = 0; i < (int)cats.size(); ++i) shown.push_back(i);
            } else if (flowPalCat_ >= 0 &&
                       std::find(shown.begin(), shown.end(), flowPalCat_) == shown.end()) {
                shown.push_back(flowPalCat_);
            }
            ui::ChipFlow(ui::FilterChipWidth("All"), true);
            if (ui::FilterChip("All", flowPalCat_ < 0)) {
                flowPalCat_ = -1;
                flowPalSel_ = 0;
                flowPalFocus_ = true;
            }
            for (int i : shown) {
                ui::ChipFlow(ui::FilterChipWidth(cats[i]), false);
                if (ui::FilterChip(cats[i], flowPalCat_ == i)) {
                    flowPalCat_ = flowPalCat_ == i ? -1 : i;
                    flowPalSel_ = 0;
                    flowPalFocus_ = true;
                }
            }
            if (!flowPalMoreCats_ && (int)cats.size() > (int)shown.size()) {
                char more[16];
                std::snprintf(more, sizeof(more), "+%d", (int)cats.size() - (int)shown.size());
                ui::ChipFlow(ui::FilterChipWidth(more), false);
                if (ui::FilterChip(more, false)) {
                    flowPalMoreCats_ = true;
                    flowPalFocus_ = true;
                }
            }
        }
        // results
        const float fs = ImGui::GetFontSize();
        const float rowH = ImGui::GetFrameHeight();
        const float descH = ImGui::GetTextLineHeight();
        ImGui::BeginChild("##palres", ImVec2(0, ImMin(scaled(280.0f), (float)hits.size() * rowH + descH + fs)),
                          ImGuiChildFlags_None);
        const FlowNodeType* place = nullptr;
        if (enter && !hits.empty()) place = hits[flowPalSel_].second;
        for (int i = 0; i < (int)hits.size(); ++i) {
            const FlowNodeType& t = *hits[i].second;
            const bool sel = i == flowPalSel_;
            const bool showDesc = sel && t.desc && *t.desc;
            const float h = rowH + (showDesc ? descH : 0.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(i);
            if (ImGui::InvisibleButton(t.title, ImVec2(ImMax(1.0f, w), h))) place = &t;
            const bool hov = ImGui::IsItemHovered();
            if (hov && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f))
                flowPalSel_ = i;
            if (hov && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && t.desc && *t.desc) {
                ImGui::BeginTooltip();
                flowNodeDoc(t, scaled(360.0f));
                ImGui::EndTooltip();
            }
            ImGui::PopID();
            if (sel && flowPalSel_ != prevSel) ImGui::SetScrollHereY(0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (sel)
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                                  ImGui::GetColorU32(fgMix(sem.surface, sem.accent, 0.18f)),
                                  ImGui::GetStyle().FrameRounding);
            const float ty = p.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddCircleFilled(ImVec2(p.x + fs * 0.55f, p.y + rowH * 0.5f), fs * 0.2f,
                                ImGui::GetColorU32(flowKindColor(t)));
            const float cw = ImGui::CalcTextSize(t.category).x;
            dl->AddText(ImVec2(p.x + w - cw - fs * 0.5f, ty), ImGui::GetColorU32(sem.textDim), t.category);
            {
                ui::FontScope bold(sel ? ui::boldFont() : nullptr);
                dl->AddText(ImVec2(p.x + fs * 1.2f, ty), ImGui::GetColorU32(sem.text), t.title);
            }
            if (showDesc) {
                dl->PushClipRect(p, ImVec2(p.x + w - fs * 0.3f, p.y + h), true);
                dl->AddText(ImVec2(p.x + fs * 1.2f, ty + rowH - fs * 0.25f), ImGui::GetColorU32(sem.textDim),
                            t.desc);
                dl->PopClipRect();
            }
        }
        if (hits.empty())
            ImGui::TextDisabled(fromKind >= 0 ? "No node takes this wire with that name."
                                              : "No node matches.");
        ImGui::EndChild();
        // footer
        ImGui::Separator();
        ImGui::TextDisabled("Up/Down pick \xc2\xb7 Enter places at the cursor");
        if (!flowRecent_.empty())
            if (const FlowNodeType* rt = flowNodeType(flowRecent_.front())) {
                const std::string rec = std::string("Recent: ") + rt->title;
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(rec.c_str()).x);
                ImGui::TextDisabled("%s", rec.c_str());
            }
        if (place) {
            // From an INPUT pin the new node goes to the left, so its output
            // faces the wire it is joining.
            ImVec2 at = flowPalPos_;
            if (fromKind >= 0 && flowExecOutIndex(fromKind) < 0 &&
                (flowExecInIndex(fromKind) >= 0 || fromKind == 0 || fromKind == 4 || fromKind == 6 ||
                 fromKind == 8 || fromKind == 16))
                at.x -= nodeWidth + 60.0f * nodeScale;
            const int id = makeNode(*place, at);
            if (fromKind >= 0) {
                const int pin = flowCompatiblePin(*place, id, fromKind);
                if (pin >= 0) connectPins(flowPalFromPin_, pin);
            }
            flowPalFromPin_ = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Frame all / tidy: after the readback, so the model positions are this
    // frame's and every node has a measured size.
    if (wantTidy) {
        tidy();
        frameAll();
        flowFrameAllReq_ = 0;
    } else if (wantFrame) {
        frameAll();
        flowFrameAllReq_ = 0;
    } else {
        // A switch frames the new graph - but only once it is really on
        // screen and settled: a window behind another tab measures every node
        // as 0x0, and a layout being applied hands the canvas a different size
        // each frame for a moment.
        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID kw = ImGui::GetID("##fg_last_w"), kh = ImGui::GetID("##fg_last_h");
        const bool settled = st->GetFloat(kw) == dbgCanvasSize.x &&
                             st->GetFloat(kh) == dbgCanvasSize.y && dbgCanvasSize.x > scaled(120.0f) &&
                             dbgCanvasSize.y > scaled(120.0f);
        st->SetFloat(kw, dbgCanvasSize.x);
        st->SetFloat(kh, dbgCanvasSize.y);
        if (flowFrameAllReq_ > 0 && settled && !ImGui::GetCurrentWindowRead()->SkipItems &&
            flowPositionsApplied_) {
            frameAll();
            flowFrameAllReq_ = 0;
        }
    }

    ImGui::EndChild();  // ##fg_canvas

    // --- right: the inspector ------------------------------------------------------
    ImGui::SameLine(0.0f, 0.0f);
    {
        float cw = canvasW;
        ui::VSplitter("##fg_split_insp", &cw, scaled(200.0f), scaled(220.0f), bodyH);
        if (cw != canvasW) flowInspW_ = inspW + (canvasW - cw);  // dragging right narrows the inspector
    }
    ImGui::BeginChild("##fg_inspector", ImVec2(0, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        const float fs = ImGui::GetFontSize();
        // The Live tab comes forward when a game starts reporting.
        const bool liveFlip = dbgLive != flowInspWasLive_;
        if (liveFlip) {
            flowInspTab_ = dbgLive ? 1 : 0;
            flowInspWasLive_ = dbgLive;
        }
        int tab = dbgLive ? flowInspTab_ : 0;
        if (dbgLive && ui::BeginQuietTabBar("##fg_insp_tabs")) {
            const ImGuiTabItemFlags force = liveFlip ? ImGuiTabItemFlags_SetSelected : 0;
            const int want = tab;  // while forcing, ImGui still reports the old tab this frame
            if (ui::QuietTab("Node", want == 0, want == 0 ? force : 0) && !force) flowInspTab_ = tab = 0;
            if (ui::QuietTab("Live", want == 1, want == 1 ? force : 0) && !force) flowInspTab_ = tab = 1;
            ui::EndQuietTabBar();
        }
        auto nodeById = [&](int id) -> FlowNode* {
            for (FlowNode& n : fg.nodes)
                if (n.id == id) return &n;
            return nullptr;
        };
        auto titleOf = [&](int id) -> const char* {
            if (const FlowNode* n = nodeById(id))
                if (const FlowNodeType* t = flowNodeType(n->type)) return t->title;
            return "?";
        };

        if (tab == 1) {
            // --- live: why it stopped, the values this graph touches, every
            // breakpoint in the scene, and what Live Logic can patch.
            if (dbgHalted) {
                ui::Caption("Why it stopped");
                ImGui::PushTextWrapPos(0.0f);
                if (haltWhat.empty())
                    ImGui::TextWrapped("Paused by hand at frame %u.", dbgSnap_.frame);
                else
                    ImGui::TextWrapped("%s on %s stopped the game at frame %u (%s). Nothing after it "
                                       "ran in that frame.",
                                       haltWhat.c_str(), haltWhere.empty() ? "?" : haltWhere.c_str(),
                                       dbgSnap_.frame, haltOnBp ? "a breakpoint" : "a step");
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }
            ui::Caption("Values this graph touches", "live");
            {
                // Variables, save values and facts the graph names, with the
                // value now and - when it changed - the one before.
                std::vector<std::string> names;
                for (const FlowNode& n : fg.nodes) {
                    const FlowNodeType* t = flowNodeType(n.type);
                    if (!t || n.str.empty()) continue;
                    if (t->strKind == FlowParamKind::SaveValue || t->strKind == FlowParamKind::VarName ||
                        t->strKind == FlowParamKind::FactName || t->strKind == FlowParamKind::SaveText)
                        if (std::find(names.begin(), names.end(), n.str) == names.end())
                            names.push_back(n.str);
                }
                const auto& frames = dbgTimeline_.frames();
                const std::vector<float>* prev = nullptr;
                for (size_t f = frames.size(); f-- > 0;)
                    if (!frames[f].vars.empty() && frames[f].vars != dbgSnap_.vars) {
                        prev = &frames[f].vars;
                        break;
                    }
                int shown = 0;
                for (const std::string& nm : names) {
                    for (size_t i = 0; i < dbgSyms_.vars.size(); ++i) {
                        const livedbg::VarSym& v = dbgSyms_.vars[i];
                        if (v.name != nm) continue;
                        const size_t o = i * 3;
                        if (o >= dbgSnap_.vars.size()) continue;
                        const float now = dbgSnap_.vars[o];
                        ImGui::TextUnformatted(nm.c_str());
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", v.kind == 's' ? "save" : v.kind == 'b' ? "bool" : "var");
                        char buf[64];
                        if (prev && o < prev->size() && (*prev)[o] != now)
                            std::snprintf(buf, sizeof(buf), "%g -> %g", (*prev)[o], now);
                        else
                            std::snprintf(buf, sizeof(buf), "%g", now);
                        ui::FontScope mono(ui::monoFont());
                        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(buf).x);
                        ImGui::TextUnformatted(buf);
                        ++shown;
                    }
                }
                if (shown == 0) ImGui::TextDisabled("This graph reads and writes no named values.");
            }
            ImGui::Spacing();
            ui::Caption("Breakpoints");
            {
                int shown = 0;
                for (const std::string& b : project_.debugBreakpoints) {
                    const size_t colon = b.rfind(':');
                    if (colon == std::string::npos) continue;
                    const std::string oid = b.substr(0, colon);
                    const int nid = std::atoi(b.c_str() + colon + 1);
                    for (int i = 0; i < objCount; ++i) {
                        const SceneObject& o = project_.objects()[i];
                        if (o.id != oid) continue;
                        const char* title = "?";
                        for (const FlowNode& n : o.flowGraph.nodes)
                            if (n.id == nid)
                                if (const FlowNodeType* t = flowNodeType(n.type)) title = t->title;
                        const int key = dbgKeyFor(project_.activeScene, oid, nid);
                        const bool stopped = dbgHalted && key >= 0 && key == dbgSnap_.breakKey;
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddCircleFilled(
                            ImVec2(p.x + fs * 0.35f, p.y + ImGui::GetTextLineHeight() * 0.5f), fs * 0.22f,
                            ImGui::GetColorU32(stopped ? sem.warn : sem.danger));
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fs);
                        ImGui::PushID(b.c_str());
                        const std::string lbl = o.name + " \xc2\xb7 " + title;
                        if (ImGui::Selectable(lbl.c_str(), false)) {
                            switchTo = i;
                            flowSelectReq_ = nid;
                        }
                        ImGui::PopID();
                        char hits[24] = "";
                        if (stopped) std::snprintf(hits, sizeof(hits), "hit");
                        else if (key >= 0 && key < (int)dbgSnap_.hits.size())
                            std::snprintf(hits, sizeof(hits), "%u", dbgSnap_.hits[key]);
                        if (hits[0]) {
                            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(hits).x);
                            ImGui::TextDisabled("%s", hits);
                        }
                        ++shown;
                    }
                }
                if (shown == 0) ImGui::TextDisabled("None in this scene. Click left of a node's title.");
            }
            ImGui::Spacing();
            ui::Caption("Live Logic");
            ImGui::PushTextWrapPos(0.0f);
            if (liveLogicState_ == LogicState::Off) {
                ImGui::TextDisabled("Off - every edit needs Run (F5).");
            } else if (fg.nodes.empty()) {
                ImGui::TextDisabled("Nothing to patch.");
            } else if (flowCapPatchable_) {
                ImGui::TextWrapped("Edits to this graph reach the running game within a few frames.");
            } else {
                std::string why;
                for (const std::string& r : flowCapReasons_) why += (why.empty() ? "" : ", ") + r;
                ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
                ImGui::TextWrapped("%s need Run (F5) - the rest of the graph waits with them.",
                                   why.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
        } else if (selNodes.size() == 1 && nodeById(selNodes[0]) &&
                   flowNodeType(nodeById(selNodes[0])->type)) {
            // --- one node: everything about it.
            FlowNode& n = *nodeById(selNodes[0]);
            const FlowNodeType* t = flowNodeType(n.type);
            {
                ui::FontScope bold(ui::boldFont());
                ImGui::TextUnformatted(t->title);
            }
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("(?)").x);
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::BeginTooltip();
                flowNodeDoc(*t, scaled(360.0f));
                ImGui::EndTooltip();
            }
            ImGui::TextDisabled("%s \xc2\xb7 node %d", t->category, n.id);
            if (t->desc && *t->desc) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                ImGui::TextWrapped("%s", t->desc);
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
            if (const char* pr = flowNodeProblem(project_, n)) {
                ImGui::PushStyleColor(ImGuiCol_Text, sem.danger);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("%s", pr);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            ui::Caption("Settings");
            {
                const float y0 = ImGui::GetCursorPosY();
                ui::InspectorRows rows(scaled(70.0f), scaled(130.0f));
                ImGui::PushID(n.id);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.62f);
                drawNodeParams(n, t);
                ImGui::PopItemWidth();
                ImGui::PopID();
                if (ImGui::GetCursorPosY() == y0)
                    ImGui::TextDisabled("Nothing to set - this node is all wiring.");
            }

            // Display Text: where on the 512x448 screen it lands. Drag to move.
            if (n.type == "DisplayText") {
                ImGui::Spacing();
                ui::Caption("Placement", "512\xc3\x97" "448");
                const float w = ImGui::GetContentRegionAvail().x;
                const float h = w * 448.0f / 512.0f;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("Placement preview", ImVec2(ImMax(1.0f, w), ImMax(1.0f, h)));
                if (ImGui::IsItemActive()) {
                    const ImVec2 m = ImGui::GetIO().MousePos;
                    n.num[0] = ImSaturate((m.x - p.x) / w);
                    n.num[1] = ImSaturate((m.y - p.y) / h);
                }
                if (ImGui::IsItemDeactivated()) changed = true;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("Drag to place the text on the 512x448 screen.");
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(sem.field),
                                  ImGui::GetStyle().FrameRounding);
                dl->AddRect(ImVec2(p.x + w * 0.05f, p.y + h * 0.05f), ImVec2(p.x + w * 0.95f, p.y + h * 0.95f),
                            ImGui::GetColorU32(ImVec4(sem.border.x, sem.border.y, sem.border.z, 0.8f)),
                            0.0f, 0, 1.0f);
                const std::string txt = n.str2.empty() ? std::string("Text") : n.str2;
                const float px = ImMax(6.0f, n.num[2] * (w / 512.0f));
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(px, FLT_MAX, 0.0f, txt.c_str());
                const ImVec2 c(p.x + n.num[0] * w, p.y + n.num[1] * h);
                dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
                dl->AddText(ImGui::GetFont(), px, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f),
                            ImGui::GetColorU32(sem.text), txt.c_str());
                dl->AddCircleFilled(c, fs * 0.22f, ImGui::GetColorU32(sem.accent));
                dl->PopClipRect();
                // Wrapped: at the inspector's default width the line ran off its edge.
                ImGui::PushStyleColor(ImGuiCol_Text, sem.textDim);
                if (n.num[3] > 0.0f)
                    ImGui::TextWrapped("Centred on %.2f, %.2f \xc2\xb7 %.0f px \xc2\xb7 hides after %.1f s",
                                       n.num[0], n.num[1], n.num[2], n.num[3]);
                else
                    ImGui::TextWrapped("Centred on %.2f, %.2f \xc2\xb7 %.0f px \xc2\xb7 stays", n.num[0],
                                       n.num[1], n.num[2]);
                ImGui::PopStyleColor();
            }

            // Wiring, both ways; each end is a link to that node.
            ImGui::Spacing();
            ui::Caption("Pins");
            {
                const float col = ImMin(scaled(90.0f), ImGui::GetContentRegionAvail().x * 0.35f);
                int row = 0;
                auto pinRow = [&](const std::string& label, const std::vector<std::pair<int, std::string>>& ends,
                                  bool in) {
                    ImGui::TextDisabled("%s", label.c_str());
                    ImGui::SameLine(col);
                    if (ends.empty()) {
                        ImGui::TextDisabled("not wired");
                        return;
                    }
                    for (size_t e = 0; e < ends.size(); ++e) {
                        if (e > 0) ImGui::SetCursorPosX(col);
                        ImGui::PushID(row++);
                        const std::string txt = (in ? "\xe2\x86\x90 " : "\xe2\x86\x92 ") + ends[e].second;
                        if (ImGui::TextLink(txt.c_str())) flowSelectReq_ = ends[e].first;
                        ImGui::PopID();
                    }
                };
                auto ins = [&](int kind, int toPin) {
                    std::vector<std::pair<int, std::string>> v;
                    for (const FlowLink& l : fg.links) {
                        if (l.kind != kind || l.toNode != n.id || (kind == FlowLinkExec && l.toPin != toPin))
                            continue;
                        std::string s = titleOf(l.fromNode);
                        if (kind == FlowLinkExec)
                            if (const FlowNode* src = nodeById(l.fromNode))
                                if (const FlowNodeType* st = flowNodeType(src->type))
                                    if (flowExecOutCount(*st) > 1)
                                        s += std::string(" \xc2\xb7 ") + flowExecOutLabel(*st, l.fromPin);
                        v.push_back({l.fromNode, s});
                    }
                    return v;
                };
                auto outs = [&](int kind, int fromPin) {
                    std::vector<std::pair<int, std::string>> v;
                    for (const FlowLink& l : fg.links) {
                        if (l.kind != kind || l.fromNode != n.id || (kind == FlowLinkExec && l.fromPin != fromPin))
                            continue;
                        std::string s = titleOf(l.toNode);
                        if (kind == FlowLinkExec)
                            if (const FlowNode* dst = nodeById(l.toNode))
                                if (const FlowNodeType* dt = flowNodeType(dst->type))
                                    if (dt->execInCount > 1)
                                        s += std::string(" \xc2\xb7 ") + flowExecInLabel(*dt, l.toPin);
                        v.push_back({l.toNode, s});
                    }
                    return v;
                };
                if (!t->pure && !t->trigger) {
                    const int execIns = t->execInCount < 1 ? 1 : t->execInCount;
                    for (int e = 0; e < execIns; ++e) pinRow(flowExecInLabel(*t, e), ins(FlowLinkExec, e), true);
                }
                if (t->idIn) pinRow("object", ins(FlowLinkObject, 0), true);
                if (t->posIn) pinRow("position", ins(FlowLinkPos, 0), true);
                if (t->boolIn) pinRow("bool", ins(FlowLinkBool, 0), true);
                if (t->textIn) pinRow("text", ins(FlowLinkText, 0), true);
                if (t->numIn) pinRow("number", ins(FlowLinkNum, 0), true);
                if (!t->pure)
                    for (int e = 0; e < flowExecOutCount(*t); ++e)
                        pinRow(flowExecOutLabel(*t, e), outs(FlowLinkExec, e), false);
                if (t->idOut) pinRow("object out", outs(FlowLinkObject, 0), false);
                if (t->posOut) pinRow("position out", outs(FlowLinkPos, 0), false);
                if (t->boolOut) pinRow("bool out", outs(FlowLinkBool, 0), false);
                if (t->textOut) pinRow("text out", outs(FlowLinkText, 0), false);
                if (t->numOut) pinRow("number out", outs(FlowLinkNum, 0), false);
            }

            if (liveLogicState_ != LogicState::Off && !flowLiveSupported(n.type)) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, sem.warn);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextWrapped("Live Logic cannot patch %s into a running game. Changes here apply "
                                   "on the next Run (F5).",
                                   t->title);
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::Spacing();
            if (ui::DangerButton("Delete node")) deleteNodes({n.id});
        } else if (selNodes.size() > 1) {
            ImGui::Text("%d nodes selected", (int)selNodes.size());
            ImGui::TextDisabled("%s copies them, Delete removes them.",
                                ui::ShortcutChord("C", nullptr, "+").c_str());
            ImGui::Spacing();
            if (ui::DangerButton("Delete nodes")) deleteNodes(selNodes);
        } else {
            ui::EmptyState(ui::Glyph::Nodes, "No node selected",
                           fg.nodes.empty() ? "Add a trigger on the canvas to begin."
                                            : "Click a node to edit its settings here.");
        }
    }
    ImGui::EndChild();

    if (switchTo >= 0) openGraph(switchTo);
    flowGraphFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (changed) commitChange();

    ImGui::End();
}
