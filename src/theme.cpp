#include "theme.hpp"

#include <imnodes.h>

namespace theme {
namespace {

// A theme is eleven colours. Every one of the ~60 ImGuiCol_ entries - and the
// ground and field tones - is DERIVED from them (below), which is what stops
// three themes from being three sixty-line tables that drift apart the day a
// new ImGui colour appears.
struct Palette {
    ImVec4 bg;       // window background - the darkest surface
    ImVec4 surface;  // frames, buttons, inputs: one step up from the page
    ImVec4 raised;   // menu bar, title bars, table headers
    ImVec4 border;
    ImVec4 text;
    ImVec4 textDim;
    ImVec4 accent;  // selection, focus, the one bright colour
    ImVec4 ok;
    ImVec4 warn;
    ImVec4 danger;
    ImVec4 special;  // the fourth semantic colour - "live", special state
};

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 fade(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

// Graphite: a neutral graphite base and four semantic colours - blue as the
// accent, green for "running", red for "stopped", pink for "live" - instead
// of a tint over every panel, which is what keeps a full-screen editor calm.
// (The id stays FaceButtons / key "faceButtons": editor.ini stores the key.)
constexpr Palette kFaceButtons = {
    /*bg*/ rgb(18, 21, 27),      /*surface*/ rgb(30, 35, 44),
    /*raised*/ rgb(25, 29, 37),  /*border*/ rgb(36, 40, 49),
    /*text*/ rgb(232, 234, 240), /*textDim*/ rgb(140, 148, 164),
    /*accent*/ rgb(122, 162, 255),
    /*ok*/ rgb(61, 214, 140),
    /*warn*/ rgb(245, 184, 74),
    /*danger*/ rgb(255, 107, 107),
    /*special*/ rgb(242, 143, 224),
};

// The boot screen: the deep navy the console fades up from, with the logo's
// electric blue. Every surface carries the tint, so it reads as one piece of
// hardware at the cost of being less neutral than the graphite.
constexpr Palette kBootScreen = {
    /*bg*/ rgb(10, 14, 23),      /*surface*/ rgb(22, 32, 47),
    /*raised*/ rgb(13, 18, 32),  /*border*/ rgb(30, 41, 60),
    /*text*/ rgb(227, 235, 245), /*textDim*/ rgb(122, 140, 166),
    /*accent*/ rgb(58, 167, 255), /*ok*/ rgb(90, 214, 125),
    /*warn*/ rgb(240, 176, 64),   /*danger*/ rgb(239, 95, 95),
    /*special*/ rgb(242, 143, 224),
};

// The memory-card browser: the violet of the OSD, the most "retro" of the
// three and the only one whose accent is not a blue.
constexpr Palette kMemoryCard = {
    /*bg*/ rgb(10, 9, 18),       /*surface*/ rgb(28, 25, 48),
    /*raised*/ rgb(16, 14, 28),  /*border*/ rgb(38, 34, 56),
    /*text*/ rgb(232, 230, 245), /*textDim*/ rgb(134, 126, 166),
    /*accent*/ rgb(139, 124, 255), /*ok*/ rgb(79, 214, 174),
    /*warn*/ rgb(232, 178, 60),    /*danger*/ rgb(239, 95, 138),
    /*special*/ rgb(255, 160, 200),
};

const Palette& paletteOf(Id id) {
    switch (id) {
        case Id::BootScreen: return kBootScreen;
        case Id::MemoryCard: return kMemoryCard;
        default: return kFaceButtons;
    }
}

// The gizmo's axis colours: the same in every theme (a red X is a convention,
// not a palette choice), and kept as one triple so every X/Y/Z readout and the
// handle it edits agree.
constexpr ImVec4 kAxisX = rgb(255, 107, 107);
constexpr ImVec4 kAxisY = rgb(61, 214, 140);
constexpr ImVec4 kAxisZ = rgb(122, 162, 255);

// The two derived tones. Ground is what the panels float on (the dockspace
// host, the gaps between docked panels, the main menu bar); field is the inset
// fill of value inputs. Both are the page pushed towards black, so a theme
// never has to pick them - and never picks them out of step with its page.
ImVec4 groundOf(const Palette& p) { return mix(p.bg, ImVec4(0, 0, 0, 1), 0.55f); }
ImVec4 fieldOf(const Palette& p) { return mix(p.bg, ImVec4(0, 0, 0, 1), 0.32f); }

Semantics semanticsOf(const Palette& p) {
    return {p.accent,  fade(p.accent, 0.35f), p.ok,      p.warn,
            p.danger,  p.text,                p.textDim, p.surface,
            p.border,  groundOf(p),           fieldOf(p), p.special,
            kAxisX,    kAxisY,                kAxisZ};
}

Semantics gSemantics = semanticsOf(kFaceButtons);

// Stock ImGui's own dark colours, read back after StyleColorsDark() - so the
// escape-hatch theme's chips are still driven by semantics() rather than
// falling back to whatever the last theme left behind.
Semantics stockSemantics(const ImGuiStyle& s) {
    const ImVec4 bg = s.Colors[ImGuiCol_WindowBg];
    return {s.Colors[ImGuiCol_HeaderActive],
            fade(s.Colors[ImGuiCol_HeaderActive], 0.35f),
            rgb(95, 200, 115),
            rgb(240, 175, 70),
            rgb(225, 95, 85),
            s.Colors[ImGuiCol_Text],
            s.Colors[ImGuiCol_TextDisabled],
            s.Colors[ImGuiCol_FrameBg],
            s.Colors[ImGuiCol_Border],
            mix(bg, ImVec4(0, 0, 0, 1), 0.55f),
            s.Colors[ImGuiCol_FrameBg],
            rgb(242, 143, 224),
            kAxisX,
            kAxisY,
            kAxisZ};
}

void applyColors(const Palette& p, ImGuiStyle& s) {
    ImVec4* c = s.Colors;
    const ImVec4 ground = groundOf(p);
    const ImVec4 field = fieldOf(p);
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    // Popups sit ON a window, so they are a step LIGHTER than the page - a
    // menu the same colour as what it covers reads as a hole.
    c[ImGuiCol_PopupBg] = mix(p.raised, p.surface, 0.45f);  // opaque: menus over the viewport must not show it through
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    // Value fields are INSET: darker than the panel they sit in, with the
    // hairline frame border giving the edge. Hover lifts the fill towards the
    // panel rather than tinting it, so a row of fields stays calm under the
    // cursor; only an active edit takes the accent.
    c[ImGuiCol_FrameBg] = field;
    c[ImGuiCol_FrameBgHovered] = mix(field, p.surface, 0.55f);
    c[ImGuiCol_FrameBgActive] = mix(field, p.accent, 0.16f);

    // A docked panel's title/tab strip is part of the card, not a band over
    // it: the same fill as the page, focused or not.
    c[ImGuiCol_TitleBg] = p.bg;
    c[ImGuiCol_TitleBgActive] = p.bg;  // the strip of pills stays the card
    c[ImGuiCol_TitleBgCollapsed] = p.bg;
    // The menu bar sits on the ground with the panels, not on a band of its
    // own - the top of the window is the app's frame, the panels its content.
    c[ImGuiCol_MenuBarBg] = ground;

    // A scrollbar is chrome, not content: no track, a grab that only asserts
    // itself once the cursor is on it.
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = fade(p.textDim, 0.30f);
    c[ImGuiCol_ScrollbarGrabHovered] = fade(p.textDim, 0.55f);
    c[ImGuiCol_ScrollbarGrabActive] = fade(p.accent, 0.80f);

    // Filled checkbox with a knocked-out tick: the tick is the PAGE colour on
    // an accent fill, which is why CheckMark is a background colour here.
    c[ImGuiCol_CheckMark] = p.bg;
    c[ImGuiCol_CheckboxSelectedBg] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = mix(p.accent, p.text, 0.25f);

    // Buttons hover NEUTRAL (lighter, not bluer): a toolbar swept with the
    // cursor should not flash the accent at every button it passes. The press
    // is where the accent belongs.
    c[ImGuiCol_Button] = p.surface;
    c[ImGuiCol_ButtonHovered] = mix(p.surface, p.text, 0.10f);
    c[ImGuiCol_ButtonActive] = mix(p.surface, p.accent, 0.35f);

    // Header* is every list row: Selectable, TreeNode, MenuItem. A translucent
    // accent wash keeps a selected row readable at any background.
    c[ImGuiCol_Header] = fade(p.accent, 0.20f);
    c[ImGuiCol_HeaderHovered] = fade(p.accent, 0.30f);
    c[ImGuiCol_HeaderActive] = fade(p.accent, 0.44f);

    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_SeparatorHovered] = fade(p.accent, 0.60f);
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = fade(p.textDim, 0.22f);
    c[ImGuiCol_ResizeGripHovered] = fade(p.accent, 0.55f);
    c[ImGuiCol_ResizeGripActive] = p.accent;
    c[ImGuiCol_InputTextCursor] = p.accent;

    // Tabs are quiet pills (style.TabPillInset, an in-tree ImGui patch): the
    // selected one a surface-filled pill, the others just their label in the
    // dim text colour, no overline. A panel alone in its dock node shows no
    // tab at all (ui::PanelChromeNewFrame) - tabs only ever appear where there
    // is a choice to make, so they should not shout.
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = fade(p.surface, 0.70f);
    c[ImGuiCol_TabSelected] = p.surface;
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = mix(p.bg, p.surface, 0.70f);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_DockingPreview] = fade(p.accent, 0.35f);
    // The void behind an empty dock node is the ground the panels float on,
    // so an unfilled layout looks deliberate rather than broken.
    c[ImGuiCol_DockingEmptyBg] = ground;

    c[ImGuiCol_PlotLines] = p.accent;
    c[ImGuiCol_PlotLinesHovered] = p.warn;
    c[ImGuiCol_PlotHistogram] = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = p.warn;

    c[ImGuiCol_TableHeaderBg] = p.raised;
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableBorderLight] = fade(p.border, 0.55f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = fade(p.text, 0.03f);

    c[ImGuiCol_TextLink] = p.accent;
    c[ImGuiCol_TextSelectedBg] = fade(p.accent, 0.35f);
    c[ImGuiCol_TreeLines] = fade(p.textDim, 0.40f);
    c[ImGuiCol_DragDropTarget] = p.accent;
    c[ImGuiCol_DragDropTargetBg] = fade(p.accent, 0.15f);
    c[ImGuiCol_UnsavedMarker] = p.warn;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_NavWindowingHighlight] = fade(p.text, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.02f, 0.03f, 0.65f);
}

// The metrics are SHARED by every theme, including the stock-ImGui one: they
// are what makes the editor read as one product, and a theme is a palette, not
// a second layout. Everything here is authored at 100% - App::applyUiScale
// runs ScaleAllSizes() over it afterwards.
void applyMetrics(ImGuiStyle& s) {
    s.WindowPadding = ImVec2(12, 10);
    s.WindowRounding = 10.0f;
    s.WindowBorderSize = 1.0f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.ChildRounding = 8.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupRounding = 8.0f;
    s.PopupBorderSize = 1.0f;
    // 8x6 around the 15 px face = a ~27 px field: tall enough to hit and to
    // read a number in, the size every modern inspector settled on.
    s.FramePadding = ImVec2(8, 6);
    s.FrameRounding = 6.0f;
    // A hairline around every frame is most of what separates "themed ImGui"
    // from "an application": it gives inputs an edge the fill alone does not.
    s.FrameBorderSize = 1.0f;
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 3);
    s.ScrollbarSize = 10.0f;
    s.ScrollbarRounding = 5.0f;
    s.GrabMinSize = 12.0f;
    s.GrabRounding = 4.0f;
    s.ImageRounding = 4.0f;
    s.TabRounding = 6.0f;
    s.TabBorderSize = 0.0f;
    // Pills: no line under the strip, no accent overline (the filled pill says
    // which tab is in front), inset 3 px top and bottom.
    s.TabBarBorderSize = 0.0f;
    s.TabBarOverlineSize = 0.0f;
    s.TabPillInset = 3.0f;
    s.MenuItemRounding = 5.0f;
    // The gap between docked panels. Wide enough to read as a gap (the panels
    // become separate cards on the ground colour - pushDockspaceColors) and to
    // grab for resizing; ImGui draws it with the dockspace host's WindowBg.
    s.DockingSeparatorSize = 6.0f;
    // ...and each docked panel a rounded card of its own (an in-tree ImGui
    // patch: vendor/imgui-tyrax/TYRAX-PATCHES.md).
    s.DockNodeRounding = 8.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(16, 4);
    s.DisabledAlpha = 0.50f;
    // Hierarchy lines in the outliner and every other tree: cheap structure,
    // and the one ImGui feature that makes a deep object list scannable.
    s.TreeLinesFlags = ImGuiTreeNodeFlags_DrawLinesToNodes;
    s.TreeLinesSize = 1.0f;
    s.TreeLinesRounding = 3.0f;
}

}  // namespace

const Info& info(Id id) {
    static const Info kInfos[(int)Id::Count] = {
        {"faceButtons", "Graphite",
         "Neutral graphite with four semantic colours: blue accent, green for "
         "running, red for stopped."},
        {"bootScreen", "Boot screen",
         "The navy the console fades up from, with an electric blue on every "
         "surface."},
        {"memoryCard", "Memory card",
         "The violet of the console's own browser - the most retro of the "
         "three."},
        {"imguiDark", "ImGui dark",
         "Dear ImGui's stock colours, kept for comparison and for anyone who "
         "prefers them."},
    };
    const int i = (int)id;
    return kInfos[(i >= 0 && i < (int)Id::Count) ? i : (int)kDefault];
}

Id fromKey(const std::string& key) {
    for (int i = 0; i < (int)Id::Count; ++i)
        if (key == info((Id)i).key) return (Id)i;
    return kDefault;
}

const Semantics& semantics() { return gSemantics; }

void apply(Id id, ImGuiStyle& style) {
    if (id == Id::ImGuiDark) {
        ImGui::StyleColorsDark();
        // StyleColorsDark writes into the CONTEXT style, which may not be the
        // one being built - copy it across.
        const ImGuiStyle& ctx = ImGui::GetStyle();
        for (int i = 0; i < ImGuiCol_COUNT; ++i) style.Colors[i] = ctx.Colors[i];
        applyMetrics(style);
        gSemantics = stockSemantics(style);
        return;
    }
    const Palette& p = paletteOf(id);
    applyColors(p, style);
    applyMetrics(style);
    gSemantics = semanticsOf(p);
}

void applyImNodes() {
    const Semantics& sm = gSemantics;
    ImU32* c = ImNodes::GetStyle().Colors;
    const ImVec4 nodeBg = mix(sm.surface, ImVec4(0, 0, 0, 1), 0.15f);
    c[ImNodesCol_NodeBackground] = ImGui::ColorConvertFloat4ToU32(nodeBg);
    c[ImNodesCol_NodeBackgroundHovered] =
        ImGui::ColorConvertFloat4ToU32(mix(nodeBg, sm.accent, 0.12f));
    c[ImNodesCol_NodeBackgroundSelected] =
        ImGui::ColorConvertFloat4ToU32(mix(nodeBg, sm.accent, 0.22f));
    c[ImNodesCol_NodeOutline] = ImGui::ColorConvertFloat4ToU32(sm.border);
    // The canvas is DARKER than a window: the graph is a surface you look
    // into, and the nodes have to sit on top of something.
    const ImVec4 grid = mix(sm.surface, ImVec4(0, 0, 0, 1), 0.62f);
    c[ImNodesCol_GridBackground] = ImGui::ColorConvertFloat4ToU32(grid);
    c[ImNodesCol_GridLine] =
        ImGui::ColorConvertFloat4ToU32(mix(grid, sm.border, 0.55f));
    c[ImNodesCol_GridLinePrimary] =
        ImGui::ColorConvertFloat4ToU32(mix(grid, sm.border, 1.0f));
    c[ImNodesCol_BoxSelector] = ImGui::ColorConvertFloat4ToU32(fade(sm.accent, 0.20f));
    c[ImNodesCol_BoxSelectorOutline] =
        ImGui::ColorConvertFloat4ToU32(fade(sm.accent, 0.80f));
    // Link and pin colours are pushed per link/pin by the two editors (they
    // encode pin TYPE); these are the fallbacks for anything unpushed.
    c[ImNodesCol_Link] = ImGui::ColorConvertFloat4ToU32(fade(sm.textDim, 0.75f));
    c[ImNodesCol_LinkHovered] = ImGui::ColorConvertFloat4ToU32(sm.accent);
    c[ImNodesCol_LinkSelected] = ImGui::ColorConvertFloat4ToU32(sm.accent);
    c[ImNodesCol_Pin] = ImGui::ColorConvertFloat4ToU32(sm.textDim);
    c[ImNodesCol_PinHovered] = ImGui::ColorConvertFloat4ToU32(sm.accent);
    c[ImNodesCol_MiniMapBackground] =
        ImGui::ColorConvertFloat4ToU32(fade(grid, 0.55f));
    c[ImNodesCol_MiniMapBackgroundHovered] =
        ImGui::ColorConvertFloat4ToU32(fade(grid, 0.80f));
    c[ImNodesCol_MiniMapOutline] = ImGui::ColorConvertFloat4ToU32(sm.border);
    c[ImNodesCol_MiniMapOutlineHovered] =
        ImGui::ColorConvertFloat4ToU32(fade(sm.accent, 0.70f));
    c[ImNodesCol_MiniMapNodeBackground] =
        ImGui::ColorConvertFloat4ToU32(fade(sm.textDim, 0.55f));
    c[ImNodesCol_MiniMapNodeBackgroundHovered] =
        ImGui::ColorConvertFloat4ToU32(fade(sm.text, 0.75f));
    c[ImNodesCol_MiniMapNodeBackgroundSelected] =
        ImGui::ColorConvertFloat4ToU32(sm.accent);
    c[ImNodesCol_MiniMapNodeOutline] =
        ImGui::ColorConvertFloat4ToU32(fade(sm.border, 0.60f));
    c[ImNodesCol_MiniMapLink] = ImGui::ColorConvertFloat4ToU32(fade(sm.textDim, 0.50f));
    c[ImNodesCol_MiniMapLinkSelected] = ImGui::ColorConvertFloat4ToU32(sm.accent);
    c[ImNodesCol_MiniMapCanvas] = ImGui::ColorConvertFloat4ToU32(fade(sm.accent, 0.08f));
    c[ImNodesCol_MiniMapCanvasOutline] =
        ImGui::ColorConvertFloat4ToU32(fade(sm.accent, 0.25f));
}

float hoverAnim(ImGuiID id, bool hovered, float rate) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    float t = store->GetFloat(id, 0.0f);
    const float step = ImGui::GetIO().DeltaTime * rate;
    t = hovered ? t + step : t - step;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    store->SetFloat(id, t);
    return t;
}

void pushDockspaceColors() {
    // Only the host window and its splitters: DockSpaceOverViewport Begins and
    // Ends the host inside this push, and the panels docked into it are
    // submitted afterwards, with the normal page colour.
    const ImVec4 ground = gSemantics.ground;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ground);
    ImGui::PushStyleColor(ImGuiCol_Border, ground);
}

void popDockspaceColors() { ImGui::PopStyleColor(2); }

}  // namespace theme
