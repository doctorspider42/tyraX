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

// --- the controller's colours (PadColors) ---------------------------------
// Graphite and the light page have hand-picked tables (the approved pad
// sheet); the other themes derive theirs from what they already are, so a new
// theme never has to know the pad exists.
constexpr ImVec4 hexa(unsigned rgbHex, float a = 1.0f) {
    return ImVec4(((rgbHex >> 16) & 0xFF) / 255.0f, ((rgbHex >> 8) & 0xFF) / 255.0f,
                  (rgbHex & 0xFF) / 255.0f, a);
}

constexpr PadColors kPadGraphite = {
    /*body*/ hexa(0x232831), /*bodyLow*/ hexa(0x15181E), /*bodyEdge*/ hexa(0x323947),
    /*bodyHi*/ ImVec4(1, 1, 1, 0.07f), /*well*/ hexa(0x14171D), /*wellLow*/ hexa(0x0A0C10),
    /*plate*/ ImVec4(0, 0, 0, 0.10f), /*base*/ hexa(0x262B34), /*baseEdge*/ hexa(0x3A414D),
    /*shadow*/ ImVec4(0, 0, 0, 0.45f), /*key*/ hexa(0x2A2F38), /*keyHover*/ hexa(0x323947),
    /*keyEdge*/ hexa(0x3A4250), /*glyph*/ hexa(0xAEB5C2), /*label*/ hexa(0x6F7787),
    /*cap*/ hexa(0x2A303A), /*capLow*/ hexa(0x1A1E25), /*capRing*/ hexa(0x3B4350),
    /*triangle*/ hexa(0x3DD68C), /*circle*/ hexa(0xFF6B6B), /*cross*/ hexa(0x7FB0FF),
    /*square*/ hexa(0xF28AD2), /*led*/ hexa(0xFF5A5A), /*ledOff*/ hexa(0x3A2427),
    /*port1*/ hexa(0x7AA2FF), /*port2*/ hexa(0x4FD1C5), /*callBg*/ hexa(0x14171E),
    /*callEdge*/ ImVec4(1, 1, 1, 0.12f), /*lead*/ hexa(0xE8EAF0, 0.32f),
};

constexpr PadColors kPadLight = {
    /*body*/ hexa(0xE3E7EE), /*bodyLow*/ hexa(0xD3D9E2), /*bodyEdge*/ hexa(0xAEB7C4),
    /*bodyHi*/ ImVec4(1, 1, 1, 0.70f), /*well*/ hexa(0xD2D8E1), /*wellLow*/ hexa(0xB5BECA),
    /*plate*/ ImVec4(1, 1, 1, 0.35f), /*base*/ hexa(0xEEF1F5), /*baseEdge*/ hexa(0xA9B2BF),
    /*shadow*/ hexa(0x141E32, 0.16f), /*key*/ hexa(0xF7F8FA), /*keyHover*/ hexa(0xFFFFFF),
    /*keyEdge*/ hexa(0xA9B2BF), /*glyph*/ hexa(0x3F4756), /*label*/ hexa(0x6B7382),
    /*cap*/ hexa(0xEEF1F5), /*capLow*/ hexa(0xD3D9E1), /*capRing*/ hexa(0xB6BFCB),
    /*triangle*/ hexa(0x139660), /*circle*/ hexa(0xD63B3B), /*cross*/ hexa(0x2F6FE0),
    /*square*/ hexa(0xC0429D), /*led*/ hexa(0xE03A3A), /*ledOff*/ hexa(0xD9C4C6),
    /*port1*/ hexa(0x3366E0), /*port2*/ hexa(0x138F86), /*callBg*/ hexa(0xFFFFFF),
    /*callEdge*/ ImVec4(0, 0, 0, 0.14f), /*lead*/ hexa(0x161A22, 0.35f),
};

ImVec4 scaleRgb(const ImVec4& c, float k) { return ImVec4(c.x * k, c.y * k, c.z * k, c.w); }
// A possibly translucent colour as it shows on `bg` (stock ImGui's frame and
// border colours are half transparent; a pad body must not be).
ImVec4 over(const ImVec4& c, const ImVec4& bg) {
    return ImVec4(bg.x + (c.x - bg.x) * c.w, bg.y + (c.y - bg.y) * c.w, bg.z + (c.z - bg.z) * c.w, 1.0f);
}

PadColors padOf(const ImVec4& bgIn, const Semantics& s) {
    const ImVec4 bg = ImVec4(bgIn.x, bgIn.y, bgIn.z, 1.0f);
    const float lum = 0.2126f * bg.x + 0.7152f * bg.y + 0.0722f * bg.z;
    if (lum > 0.5f) {  // a light page: the light table, the theme's accent
        PadColors p = kPadLight;
        p.port1 = s.accent;
        return p;
    }
    const ImVec4 surface = over(s.surface, bg), border = over(s.border, bg), field = over(s.field, bg);
    const ImVec4 text = over(s.text, bg), dim = over(s.textDim, bg);
    PadColors p = kPadGraphite;  // symbols, LED, shadows and highlights stay
    p.body = surface;
    p.bodyLow = scaleRgb(surface, 0.8f);
    p.bodyEdge = mix(border, text, 0.10f);
    p.well = field;
    p.wellLow = scaleRgb(field, 0.7f);
    p.base = mix(surface, text, 0.05f);
    p.baseEdge = mix(border, text, 0.14f);
    p.key = mix(surface, text, 0.05f);
    p.keyHover = mix(surface, text, 0.10f);
    p.keyEdge = mix(border, text, 0.12f);
    p.glyph = mix(dim, text, 0.45f);
    p.label = mix(dim, bg, 0.25f);
    p.cap = mix(surface, text, 0.04f);
    p.capLow = scaleRgb(surface, 0.7f);
    p.capRing = mix(border, text, 0.12f);
    p.port1 = over(s.accent, bg);
    p.callBg = mix(field, surface, 0.3f);
    p.lead = fade(text, 0.32f);
    return p;
}

PadColors gPad = kPadGraphite;

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
    // Sliders are a 4 px track filled in the accent up to a round white knob,
    // the value as mono text at the row's right end (style.SliderThinTrack,
    // an in-tree ImGui patch) - the look of the tool designs' sliders.
    // DragFloat fields keep the framed look: they are numbers, not ranges.
    s.SliderThinTrack = 4.0f;
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

const PadColors& padColors() { return gPad; }

void apply(Id id, ImGuiStyle& style) {
    if (id == Id::ImGuiDark) {
        ImGui::StyleColorsDark();
        // StyleColorsDark writes into the CONTEXT style, which may not be the
        // one being built - copy it across.
        const ImGuiStyle& ctx = ImGui::GetStyle();
        for (int i = 0; i < ImGuiCol_COUNT; ++i) style.Colors[i] = ctx.Colors[i];
        applyMetrics(style);
        gSemantics = stockSemantics(style);
        gPad = padOf(style.Colors[ImGuiCol_WindowBg], gSemantics);
        return;
    }
    const Palette& p = paletteOf(id);
    applyColors(p, style);
    applyMetrics(style);
    gSemantics = semanticsOf(p);
    gPad = id == Id::FaceButtons ? kPadGraphite : padOf(p.bg, gSemantics);
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
