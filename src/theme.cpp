#include "theme.hpp"

#include <imnodes.h>

namespace theme {
namespace {

// A theme is nine colours. Every one of the ~60 ImGuiCol_ entries is DERIVED
// from them (below), which is what stops three themes from being three
// sixty-line tables that drift apart the day a new ImGui colour appears.
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
};

constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 fade(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }

// The face-button palette: a neutral graphite base, and the PlayStation
// controller's own four colours doing semantic work - cross blue as the
// accent, triangle green for "running", circle red for "stopped". The PS2
// reference is in the colours a reader recognizes rather than in a blue tint
// over every panel, which is what keeps a full-screen editor calm.
constexpr Palette kFaceButtons = {
    /*bg*/ rgb(17, 17, 20),      /*surface*/ rgb(30, 30, 36),
    /*raised*/ rgb(23, 23, 27),  /*border*/ rgb(45, 45, 53),
    /*text*/ rgb(236, 236, 237), /*textDim*/ rgb(129, 129, 140),
    /*accent*/ rgb(79, 195, 247),  // cross
    /*ok*/ rgb(90, 214, 125),      // triangle
    /*warn*/ rgb(240, 175, 70),
    /*danger*/ rgb(242, 85, 122),  // circle
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
};

// The memory-card browser: the violet of the OSD, the most "retro" of the
// three and the only one whose accent is not a blue.
constexpr Palette kMemoryCard = {
    /*bg*/ rgb(10, 9, 18),       /*surface*/ rgb(28, 25, 48),
    /*raised*/ rgb(16, 14, 28),  /*border*/ rgb(38, 34, 56),
    /*text*/ rgb(232, 230, 245), /*textDim*/ rgb(134, 126, 166),
    /*accent*/ rgb(139, 124, 255), /*ok*/ rgb(79, 214, 174),
    /*warn*/ rgb(232, 178, 60),    /*danger*/ rgb(239, 95, 138),
};

const Palette& paletteOf(Id id) {
    switch (id) {
        case Id::BootScreen: return kBootScreen;
        case Id::MemoryCard: return kMemoryCard;
        default: return kFaceButtons;
    }
}

Semantics gSemantics = {
    kFaceButtons.accent, fade(kFaceButtons.accent, 0.35f),
    kFaceButtons.ok,     kFaceButtons.warn,
    kFaceButtons.danger, kFaceButtons.text,
    kFaceButtons.textDim, kFaceButtons.surface,
    kFaceButtons.border,
};

// Stock ImGui's own dark colours, read back after StyleColorsDark() - so the
// escape-hatch theme's chips are still driven by semantics() rather than
// falling back to whatever the last theme left behind.
Semantics stockSemantics(const ImGuiStyle& s) {
    return {s.Colors[ImGuiCol_HeaderActive],
            fade(s.Colors[ImGuiCol_HeaderActive], 0.35f),
            rgb(95, 200, 115),
            rgb(240, 175, 70),
            rgb(225, 95, 85),
            s.Colors[ImGuiCol_Text],
            s.Colors[ImGuiCol_TextDisabled],
            s.Colors[ImGuiCol_FrameBg],
            s.Colors[ImGuiCol_Border]};
}

void applyColors(const Palette& p, ImGuiStyle& s) {
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = p.bg;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    // Popups sit ON a window, so they are a step LIGHTER than the page - a
    // menu the same colour as what it covers reads as a hole.
    c[ImGuiCol_PopupBg] = fade(mix(p.raised, p.surface, 0.45f), 0.98f);
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg] = p.surface;
    c[ImGuiCol_FrameBgHovered] = mix(p.surface, p.accent, 0.18f);
    c[ImGuiCol_FrameBgActive] = mix(p.surface, p.accent, 0.28f);

    c[ImGuiCol_TitleBg] = p.raised;
    c[ImGuiCol_TitleBgActive] = mix(p.raised, p.accent, 0.12f);
    c[ImGuiCol_TitleBgCollapsed] = p.raised;
    c[ImGuiCol_MenuBarBg] = p.raised;

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

    c[ImGuiCol_Button] = p.surface;
    c[ImGuiCol_ButtonHovered] = mix(p.surface, p.accent, 0.22f);
    c[ImGuiCol_ButtonActive] = mix(p.surface, p.accent, 0.40f);

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

    // Docking tabs: the SELECTED tab is the page colour, so it reads as the
    // front edge of the panel below it, and the accent overline is what says
    // which one is active. Unselected tabs sit between page and raised.
    const ImVec4 tabIdle = mix(p.bg, p.raised, 0.65f);
    c[ImGuiCol_Tab] = tabIdle;
    c[ImGuiCol_TabHovered] = mix(tabIdle, p.accent, 0.25f);
    c[ImGuiCol_TabSelected] = p.bg;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = mix(p.bg, p.raised, 0.35f);
    c[ImGuiCol_TabDimmedSelected] = p.bg;
    c[ImGuiCol_TabDimmedSelectedOverline] = fade(p.accent, 0.40f);
    c[ImGuiCol_DockingPreview] = fade(p.accent, 0.35f);
    // The void behind an empty dock node: below the page, so an unfilled
    // layout looks deliberate rather than broken.
    c[ImGuiCol_DockingEmptyBg] = mix(p.bg, ImVec4(0, 0, 0, 1), 0.45f);

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
    s.WindowPadding = ImVec2(10, 8);
    s.WindowRounding = 6.0f;
    s.WindowBorderSize = 1.0f;
    s.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    s.ChildRounding = 6.0f;
    s.ChildBorderSize = 1.0f;
    s.PopupRounding = 6.0f;
    s.PopupBorderSize = 1.0f;
    s.FramePadding = ImVec2(7, 4);
    s.FrameRounding = 5.0f;
    // A hairline around every frame is most of what separates "themed ImGui"
    // from "an application": it gives inputs an edge the fill alone does not.
    s.FrameBorderSize = 1.0f;
    s.ItemSpacing = ImVec2(8, 5);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(6, 3);
    s.ScrollbarSize = 12.0f;
    s.ScrollbarRounding = 6.0f;
    s.GrabMinSize = 10.0f;
    s.GrabRounding = 4.0f;
    s.ImageRounding = 4.0f;
    s.TabRounding = 6.0f;
    s.TabBorderSize = 0.0f;
    s.TabBarBorderSize = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.MenuItemRounding = 4.0f;
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
        {"faceButtons", "Face buttons",
         "Neutral graphite with the DualShock button colours: blue accent, "
         "green for running, red for stopped."},
        {"bootScreen", "Boot screen",
         "The navy the console fades up from, with the PS2 logo blue on every "
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
    gSemantics = {p.accent, fade(p.accent, 0.35f), p.ok,      p.warn,
                  p.danger, p.text,                p.textDim, p.surface,
                  p.border};
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

}  // namespace theme
