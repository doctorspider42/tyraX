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

void App::drawFlowGraphWindow() {
    flowGraphFocused_ = false;  // recomputed below; gates the global Ctrl+C/V
    ImGui::Begin("Flow Graph");
    if (!hasProject_) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }
    if (project_.objects().empty()) {
        ImGui::TextDisabled("Add an object first - every flow graph belongs to an object.");
        ImGui::End();
        return;
    }

    // --- which object's graph is being edited -------------------------------
    if (flowGraphObject_ < 0 || flowGraphObject_ >= (int)project_.objects().size()) {
        flowGraphObject_ =
            (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size())
                ? selectedObject_
                : 0;
        flowPositionsApplied_ = false;
    }

    auto graphLabel = [&](int i) {
        // objects with a non-empty graph are marked with *
        return project_.objects()[i].name +
               (project_.objects()[i].flowGraph.empty() ? "" : " *");
    };
    ImGui::SetNextItemWidth(scaled(220.0f));
    if (ImGui::BeginCombo("Graph of", graphLabel(flowGraphObject_).c_str())) {
        // Most of a scene is props with no logic, so the interesting entries
        // are the few marked with a *. The filter lives INSIDE the list it
        // filters (a Checkbox does not close the popup, only a Selectable
        // does), which is where an author is when the list turns out too long.
        int withNodes = 0;
        for (const SceneObject& o : project_.objects())
            if (!o.flowGraph.empty()) ++withNodes;
        ImGui::Checkbox("Only objects with nodes", &flowOnlyWithNodes_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Hide objects whose graph is still empty - %d of %d objects\n"
                "have nodes (those marked with a *). The object being edited\n"
                "stays listed either way.",
                withNodes, (int)project_.objects().size());
        ImGui::Separator();
        for (int i = 0; i < (int)project_.objects().size(); ++i) {
            // The current object is never filtered out: the combo has to be
            // able to show what it is set to.
            if (flowOnlyWithNodes_ && i != flowGraphObject_ &&
                project_.objects()[i].flowGraph.empty())
                continue;
            const std::string lbl = graphLabel(i) + "##fgobj" + std::to_string(i);
            if (ImGui::Selectable(lbl.c_str(), flowGraphObject_ == i) &&
                flowGraphObject_ != i) {
                flowGraphObject_ = i;
                flowPositionsApplied_ = false;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Selected object") && selectedObject_ >= 0 &&
        selectedObject_ < (int)project_.objects().size() &&
        selectedObject_ != flowGraphObject_) {
        flowGraphObject_ = selectedObject_;
        flowPositionsApplied_ = false;
    }

    // Project-defined custom nodes: reload the flow-nodes/ folder, scaffold a
    // starter file, or open the project in VS Code (jumping to the C++ bodies
    // or a specific node file). See docs/custom-flow-nodes.md.
    ImGui::SameLine();
    if (ImGui::SmallButton("Custom nodes...")) ImGui::OpenPopup("##customnodes");
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

    // AI generation: describe the logic, get a graph (aigen.hpp). The modal
    // pins the target object now - selection changes must not retarget an
    // in-flight request.
    ImGui::SameLine();
    if (ImGui::SmallButton("Generate with AI...")) {
        aiGenTargetObject_ = flowGraphObject_;
        aiGenError_.clear();
        aiGenWarnings_.clear();
        openAiGeneratePopup_ = true;
    }

    SceneObject& owner = project_.objects()[flowGraphObject_];
    FlowGraph& fg = owner.flowGraph;
    bool changed = false;

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

    ImGui::TextDisabled(
        "Right-click: add node. Mouse wheel: zoom (%.0f%%). Round pins: execution, "
        "square pins: object id. Empty object param = self (%s).",
        zoom * 100.0f, owner.name.c_str());

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
        if (!ImGui::BeginCombo(label, preview)) {
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

    for (FlowNode& n : fg.nodes) {
        const FlowNodeType* t = flowNodeType(n.type);
        if (!t) continue;

        ImU32 titleCol = (t->pure && t->boolIn)  // logic gate
                             ? IM_COL32(110, 70, 150, 255)
                             : t->trigger ? IM_COL32(40, 110, 60, 255)
                                          : IM_COL32(60, 80, 140, 255);
        // Live Debugger: the title bar IS the activity light.
        if (const float g = dbgGlow(dbgKeyOfNode(n.id)); g > 0.0f) {
            const ImVec4 base = ImColor(titleCol).Value;
            const ImVec4 hot(0.98f, 0.70f, 0.20f, 1.0f);
            titleCol = ImColor(base.x + (hot.x - base.x) * g,
                               base.y + (hot.y - base.y) * g,
                               base.z + (hot.z - base.z) * g, 1.0f);
        }
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleCol);

        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(t->title);
        ImNodes::EndNodeTitleBar();

        ImGui::PushID(n.id);
        ImGui::PushItemWidth(paramWidth);

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
            // every PadButtons field (pad.hpp) - the codegen uses the name as-is
            const char* buttons[] = {"Cross",    "Circle",   "Square", "Triangle",
                                     "DpadUp",   "DpadDown", "DpadLeft",
                                     "DpadRight", "L1",      "L2",     "L3",
                                     "R1",       "R2",       "R3",     "Start",
                                     "Select"};
            if (beginCombo("Button", n.str.empty() ? "Cross" : n.str.c_str(),
                                     t->strTip)) {
                for (const char* b : buttons) {
                    if (ImGui::Selectable(b, n.str == b)) {
                        n.str = b;
                        changed = true;
                    }
                }
                endCombo();
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
            ImGui::TextDisabled("USB keyboard (Preferences > Build)");
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
                if (ImGui::InputText("Clip", buf, sizeof(buf))) n.str = buf;
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
            if (ImGui::InputText(prefix ? "Prefix" : "Text", buf, sizeof(buf)))
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
                    if (ImGui::InputText("Text", buf, sizeof(buf))) n.str2 = buf;
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
            if (ImGui::InputText("Prefix", pbuf, sizeof(pbuf))) n.str2 = pbuf;
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
            if (ImGui::InputText(event ? "Event" : "Variable", buf, sizeof(buf)))
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
                if (ImGui::Checkbox(t->numLabels[0], &v)) {
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
            if (ImGui::Checkbox(t->numLabels[0], &v)) {
                n.num[0] = v ? 1.0f : 0.0f;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
        } else if (n.type == "SetLight") {
            bool v = n.num[0] != 0.0f;
            if (ImGui::Checkbox("On", &v)) {
                n.num[0] = v ? 1.0f : 0.0f;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            ImGui::DragFloat("Intensity", &n.num[1], 0.02f, 0.0f, 4.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 1));
        } else if (n.type == "SetDof") {
            const char* modes[] = {"Set custom", "Off", "Scene setting"};
            int mode = (int)n.num[3];
            mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
            if (ImGui::Combo("Mode", &mode, modes, 3)) {
                n.num[3] = (float)mode;
                changed = true;
            }
            paramTip(flowNumTip(*t, 3));
            if (mode == 0) {
                if (posLinked) {
                    // the link replaces Focus with the distance to the point
                    ImGui::TextDisabled("Focus: distance to linked point");
                } else {
                    ImGui::DragFloat("Focus", &n.num[0], 0.5f, 0.5f, 500.0f,
                                     "%.1f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    paramTip(flowNumTip(*t, 0));
                }
                ImGui::DragFloat("Range", &n.num[1], 0.5f, 0.1f, 500.0f,
                                 "%.1f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 1));
                ImGui::SliderFloat("Amount", &n.num[2], 0.0f, 1.0f, "%.2f");
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
            if (ImGui::Combo("Mode", &mode, modes, 5)) {
                n.num[0] = (float)mode;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            ImGui::DragFloat("Confirm s", &n.num[1], 0.5f, 0.0f, 60.0f, "%.0f");
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
            if (ImGui::Combo("Stick", &stick, sticks, 3)) {
                n.num[0] = (float)stick;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            const char* curves[] = {"Linear", "Exponential", "S-Curve"};
            int curve = (int)n.num[1];
            curve = curve < 0 ? 0 : curve > 2 ? 2 : curve;
            if (ImGui::Combo("Curve", &curve, curves, 3)) {
                n.num[1] = (float)curve;
                changed = true;
            }
            paramTip(flowNumTip(*t, 1));
            if (curve != 0) {  // exponent only shapes Exponential / S-Curve
                ImGui::DragFloat("Exponent", &n.num[2], 0.05f, 1.0f, 6.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 2));
            }
        } else if (n.type == "VibratePad") {
            ImGui::SliderFloat("Big", &n.num[0], 0.0f, 1.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 0));
            bool small = n.num[1] != 0.0f;
            if (ImGui::Checkbox("Small", &small)) {
                n.num[1] = small ? 1.0f : 0.0f;
                changed = true;
            }
            paramTip(flowNumTip(*t, 1));
            ImGui::DragFloat("Seconds", &n.num[2], 0.05f, 0.0f, 60.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 2));
            ImGui::TextDisabled(
                "Seconds 0 = until the next Vibrate Pad.\n"
                "Big 0 + Small off stops the vibration.");
        } else if (n.type == "Tween") {
            // From/To are whatever the driven parameter is (a color channel, a
            // volume, a world coordinate), so they stay free drags; Ease is the
            // one enumerated field.
            ImGui::DragFloat("From", &n.num[0], 0.1f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 0));
            ImGui::DragFloat("To", &n.num[1], 0.1f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 1));
            ImGui::DragFloat("Seconds", &n.num[2], 0.05f, 0.0f, 600.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 2));
            const char* eases[] = {"Linear", "Ease in", "Ease out",
                                   "Smooth (in-out)"};
            int ease = (int)n.num[3];
            ease = ease < 0 ? 0 : ease > 3 ? 3 : ease;
            if (ImGui::Combo("Ease", &ease, eases, 4)) {
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
                    ImGui::SliderFloat(e->paramLabel[a].c_str(), &n.num[a],
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
            if (ImGui::Combo("Style", &st, styles, 5)) {
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
                ImGui::SliderFloat("Amount", &n.num[1], 0.0f, 1.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                paramTip(flowNumTip(*t, 1));
            }
            ImGui::TextDisabled("Wire a Tween into Amount\nto slide them in.");
        } else if (n.type == "PlayerPos") {
            const char* who[] = {"Player 1", "Player 2"};
            int idx = n.num[0] != 0.0f ? 1 : 0;
            if (ImGui::Combo("Player", &idx, who, 2)) {
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
            if (ImGui::SliderInt("Outputs", &outs, 2, 4)) {
                n.num[0] = (float)outs;
                changed = true;
            }
            paramTip(flowNumTip(*t, 0));
            ImGui::TextDisabled("Only the first %d outputs\nare ever picked.", outs);
        } else if (n.type == "DisplayText") {
            // X/Y are a normalized screen position (center anchor), so they need
            // a much finer step than the generic 0.1 drag.
            ImGui::DragFloat2("Pos##dyntext", n.num, 0.005f, 0.0f, 1.0f, "%.3f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            // One widget for num[0] and num[1], so its tip covers both - X's.
            paramTip(flowNumTip(*t, 0));
            ImGui::DragFloat("Size", &n.num[2], 0.2f, 8.0f, 48.0f, "%.0f px");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 2));
            ImGui::DragFloat("Seconds", &n.num[3], 0.05f, 0.0f, 60.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            paramTip(flowNumTip(*t, 3));
        } else if (t->numKind == FlowParamKind::Color) {
            ImGui::ColorEdit3("Color", n.num, ImGuiColorEditFlags_NoInputs);
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
                    if (ImGui::DragInt(t->numLabels[a], &v, 0.2f, 0, 999)) {
                        n.num[a] = (float)(v < 0 ? 0 : v);
                        changed = true;
                    }
                } else if (isChannel) {
                    // SPU channel 0-23; -1 = rotate through channels
                    ImGui::SliderFloat("Channel", &n.num[a], -1.0f, 23.0f,
                                       n.num[a] < 0.0f ? "auto" : "%.0f");
                    n.num[a] = (float)(int)n.num[a];
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else if (isLoop) {
                    bool loop = n.num[a] != 0.0f;
                    if (ImGui::Checkbox(t->numLabels[a], &loop)) {
                        n.num[a] = loop ? 1.0f : 0.0f;
                        changed = true;
                    }
                } else if (isVolume) {
                    ImGui::SliderFloat("Volume", &n.num[a], 0.0f, 100.0f, "%.0f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else {
                    ImGui::DragFloat(t->numLabels[a], &n.num[a],
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
        ImGui::PopItemWidth();
        ImGui::PopID();

        // pins: exec flow (round) + object id (square, amber) + position
        // (triangle, green) + bool value (circle, violet) + text (circle,
        // cyan). Pure data nodes have no exec pins; bool-in and text-in pins
        // accept several links (folded / concatenated).
        const unsigned idPinCol = IM_COL32(222, 170, 60, 255);
        const unsigned posPinCol = IM_COL32(110, 200, 120, 255);
        const unsigned boolPinCol = IM_COL32(180, 120, 220, 255);
        const unsigned textPinCol = IM_COL32(90, 190, 210, 255);
        const unsigned numPinCol = IM_COL32(230, 120, 155, 255);
        if (t->idIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginInputAttribute(flowIdInPin(n.id), ImNodesPinShape_QuadFilled);
            ImGui::TextDisabled("object");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginInputAttribute(flowPosInPin(n.id),
                                         ImNodesPinShape_TriangleFilled);
            ImGui::TextDisabled("position");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginInputAttribute(flowBoolInPin(n.id), ImNodesPinShape_CircleFilled);
            ImGui::TextDisabled("bool");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->textIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, textPinCol);
            ImNodes::BeginInputAttribute(flowTextInPin(n.id), ImNodesPinShape_CircleFilled);
            ImGui::TextDisabled("text");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->numIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, numPinCol);
            ImNodes::BeginInputAttribute(flowNumInPin(n.id),
                                         ImNodesPinShape_CircleFilled);
            // A Math node folds every wired input, so say so on the pin: it is
            // the difference between "a + b + c" and "the first link wins".
            // Everything else REPLACES num[0], so the pin takes that param's
            // own name - "slot", "value", "radius" - which says what the link
            // will do far better than a repeated "number" ever did.
            std::string numPinLabel = "number";
            if (flowNumFolds(*t)) {
                numPinLabel = "numbers";
            } else if (t->numCount > 0 && t->numLabels[0] &&
                       t->numLabels[0][0]) {
                numPinLabel = t->numLabels[0];
                for (char& ch : numPinLabel)
                    ch = (char)tolower((unsigned char)ch);
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
                    ImNodes::BeginInputAttribute(flowExecInPin(n.id, e));
                    ImGui::Text("> %s", flowExecInLabel(*t, e));
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
                ImNodes::BeginOutputAttribute(flowExecOutPin(n.id, e));
                rightLabel(flowExecOutLabel(*t, e), false);
                ImNodes::EndOutputAttribute();
            }
        }
        if (t->idOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginOutputAttribute(flowIdOutPin(n.id), ImNodesPinShape_QuadFilled);
            rightLabel("object", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginOutputAttribute(flowPosOutPin(n.id),
                                          ImNodesPinShape_TriangleFilled);
            rightLabel("position", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginOutputAttribute(flowBoolOutPin(n.id),
                                          ImNodesPinShape_CircleFilled);
            rightLabel("bool", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->textOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, textPinCol);
            ImNodes::BeginOutputAttribute(flowTextOutPin(n.id),
                                          ImNodesPinShape_CircleFilled);
            rightLabel("text", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->numOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, numPinCol);
            ImNodes::BeginOutputAttribute(flowNumOutPin(n.id),
                                          ImNodesPinShape_CircleFilled);
            rightLabel("number", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }

        ImNodes::EndNode();
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

    // Mouse wheel over the canvas: zoom, keeping the point under the cursor
    // fixed (the panning is adjusted for the new scale). flowZoom_ keeps the
    // CONTINUOUS request so small wheel notches accumulate; the pan correction
    // and the re-push are driven by the snapped, effective scale, or a notch
    // that does not reach the next whole font pixel would pan without zooming.
    if (editorHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float next =
            ImClamp(flowZoom_ * ImPow(1.1f, ImGui::GetIO().MouseWheel), 0.4f, 1.8f);
        const float nextEff = ImMax(1.0f, IM_ROUND(uiFontPx * next)) / uiFontPx;
        flowZoom_ = next;
        if (nextEff != zoom) {
            const float ratio = nextEff / zoom;
            ImVec2 pan = ImNodes::EditorContextGetPanning();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float relX = mouse.x - canvasOrigin.x;
            const float relY = mouse.y - canvasOrigin.y;
            pan.x = relX - ratio * (relX - pan.x);
            pan.y = relY - ratio * (relY - pan.y);
            ImNodes::EditorContextResetPanning(pan);
            flowPositionsApplied_ = false;  // re-push positions at the new scale
        }
    }

    // New link dragged between pins. Pin kinds by id (flowPinKind): 0 = object
    // in, 1 = exec out, 2 = exec in, 3 = object out, 4 = position in,
    // 5 = position out, 6 = bool in, 7 = bool out, 8 = text in, 9 = text out,
    // 10..15 = the node's 2nd..7th exec in, 16/17 = number in/out,
    // 18..24 = the node's 2nd..8th exec out.
    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
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
            }
        }
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

    // Delete selection
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
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
        const int numNodes = ImNodes::NumSelectedNodes();
        if (numNodes > 0) {
            std::vector<int> ids(numNodes);
            ImNodes::GetSelectedNodes(ids.data());
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
        }
    }

    // Right-click on a node: the debugger's per-node actions (breakpoint,
    // force-fire). Right-click on empty canvas keeps opening the add-node menu.
    int dbgCtxHovered = -1;
    if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        ImNodes::IsNodeHovered(&dbgCtxHovered)) {
        flowCtxNode_ = dbgCtxHovered;
        ImGui::OpenPopup("##flow_node_ctx");
    } else if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##flow_add_node");
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
    if (ImGui::BeginPopup("##flow_add_node")) {
        const ImVec2 clickPos = ImGui::GetMousePosOnOpeningCurrentPopup();
        for (const char* cat : flowNodeCategories()) {
            if (!ImGui::BeginMenu(cat)) continue;
            for (const FlowNodeType* tp : flowAllNodeTypes()) {
                const FlowNodeType& t = *tp;
                if (std::strcmp(t.category, cat) != 0) continue;
                if (ImGui::MenuItem(t.title)) {
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
                    ImNodes::SetNodeScreenSpacePos(n.id, clickPos);
                    changed = true;
                }
                // The add-menu tooltip is the node's documentation, drawn by the
                // same flowNodeDoc the node hover uses (custom nodes fill their
                // half from the .flownode `desc =` / `tipN =` header keys).
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) &&
                    t.desc && *t.desc) {
                    ImGui::BeginTooltip();
                    flowNodeDoc(t, scaled(360.0f));
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (changed) commitChange();

    ImGui::End();
}
