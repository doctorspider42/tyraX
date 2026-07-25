#pragma once

#include <string>
#include <vector>

#include "project.hpp"

// Bakes a GameMenu's whole panel (title, entry labels, button hints, border,
// custom images) into an RGBA bitmap / PNG using a Windows TTF font
// (Consolas Bold, Arial Bold fallback). The PS2 engine has no text
// rendering, so every menu ships as one pre-rendered sprite; the game only
// draws the panel and a cursor.
//
// Panel composition is a vertical flow: [AboveTitle images] title
// [AboveEntries images] entry rows [BelowEntries images] hints - plus a
// Background layer under everything and Overlay images in front. The layout
// values are the contract between the baker, the editor preview and the
// generated game runtime (cursor row positions).
namespace menubake {

constexpr int kMaxEntries = 8;
constexpr int kMaxOptions = 8;  // options per Toggle/Choice entry

// Geometry of a menu's panel. Flow images push the title and rows down, the
// row pitch follows the entry font size; canvas height rounds up to a power
// of two (64..512, the PS2 texture cap) with the slack rows transparent.
struct PanelLayout {
    int panelW = 256;    // texture width (from GameMenu::panelW)
    int canvasH = 64;    // texture height (pow2)
    int contentH = 64;   // drawn part (border to border)
    int row0Y = 44;      // first entry row
    int rowH = 24;       // entry row pitch (entrySize + 9)
    bool clipped = false;  // content exceeded the 512px cap and was cut
};

// The project supplies both the font registry (GameMenu::font names a
// Project::fonts entry) and, through Project::dir, the image paths.
PanelLayout panelLayout(const GameMenu& menu, const Project& p);

// Rasterizes the panel into out (layout.panelW x layout.canvasH RGBA,
// row-major). Returns false when no usable font file is found.
bool bakePanelRGBA(const GameMenu& menu, const Project& p,
                   std::vector<unsigned char>& out, int& w, int& h);

// Same, PNG-encoded (for res/menus/<name>.png). Empty on failure.
bool bakePanelPNG(const GameMenu& menu, const Project& p,
                  std::vector<unsigned char>& png);

// Menu name -> res/menus file name ("<sanitized>.png").
std::string panelFileName(const std::string& menuName);

// --- Toggle / Choice value labels ------------------------------------------
// The panel is a single pre-rendered sprite, so the CURRENT state of a
// Toggle / Choice entry cannot live in it. Instead every possible value
// label is baked into a second per-menu texture (res/menus/<name>-values.png)
// as a vertical strip of fixed-size cells; the game draws the active cell as
// a sub-rectangle sprite (MODE_REPEAT + offset, like the debug glyph atlas)
// right-aligned on the entry row.

// The labels a Toggle / Choice entry cycles through. Toggle defaults to
// Off/On when no custom pair is stored; Choice falls back to a lone "-".
// Empty for every other action. Capped at kMaxOptions.
std::vector<std::string> entryOptionLabels(const MenuEntry& entry);

// True when any entry needs a value strip (a Toggle/Choice entry exists).
bool menuHasValueEntries(const GameMenu& menu);

// Geometry of a menu's value strip. Cells stack vertically at a fixed pitch
// (cellH + bleed padding); canvas height rounds up to a power of two, capped
// at the 512px PS2 texture limit (cells past the cap are clipped).
struct ValueStripLayout {
    int cellW = 128;   // texture width (pow2; 64 on 128px panels)
    int cellH = 24;    // drawn cell height = entry row pitch (entrySize + 9)
    int pitch = 32;    // vertical cell distance (cellH + padding)
    int canvasH = 64;  // texture height (pow2, <= 512)
    int cells = 0;     // total baked cells across all value entries
    bool clipped = false;  // cells exceeded the 512px cap and were cut
    // First cell index per menu entry (index-aligned with menu.entries,
    // capped at kMaxEntries); -1 = not a value entry.
    std::vector<int> firstCell;
};

ValueStripLayout valueStripLayout(const GameMenu& menu);

// Rasterizes the value strip (layout.cellW x layout.canvasH RGBA). Returns
// false when the menu has no value entries or no usable font is found.
bool bakeValueStripRGBA(const GameMenu& menu, const Project& p,
                        std::vector<unsigned char>& out, int& w, int& h);

// Same, PNG-encoded (for res/menus/<name>-values.png). Empty on failure.
bool bakeValueStripPNG(const GameMenu& menu, const Project& p,
                       std::vector<unsigned char>& png);

// Menu name -> value strip file name ("<sanitized>-values.png").
std::string valueStripFileName(const std::string& menuName);

// Editor preview helper: draws the given option label of every value entry
// onto an already-baked panel RGBA (right-aligned on its row), mirroring
// where the game composites the strip cells. current is index-aligned with
// menu.entries; out-of-range indices clamp.
void overlayValuePreview(const GameMenu& menu, const Project& p,
                         const std::vector<int>& current,
                         std::vector<unsigned char>& rgba, int w, int h);

// --- HUD texts ---------------------------------------------------------------
// On-screen texts (Tools > UI Editor > Texts) baked to res/hud PNG sprites -
// the engine has no font. Shown/hidden at runtime by the Set Text Visible
// flow node.

// Baked texture dimensions for a HUD text (pow2, capped at 512). The text is
// drawn centered in the canvas, so the sprite's center anchor centers the
// content. Returns false when no usable font is found.
bool textLayout(const HudText& text, const Project& p, int& w, int& h);

// Rasterizes the text (multi-line on '\n', optional drop shadow).
bool bakeTextRGBA(const HudText& text, const Project& p,
                  std::vector<unsigned char>& out, int& w, int& h);

// Same, PNG-encoded (for res/hud/text-<sanitized>.png). Empty on failure.
bool bakeTextPNG(const HudText& text, const Project& p,
                 std::vector<unsigned char>& png);

// Text name -> res/hud file name ("text-<sanitized>.png").
std::string textFileName(const std::string& textName);

// --- Font atlases ------------------------------------------------------------
// A Display Text node draws a string only known at runtime, so it cannot use a
// pre-baked sprite like the texts above. Instead its font ships as a glyph
// atlas: every glyph rasterized once into a grid, which the game blits cell by
// cell (the trick the engine's own debug font uses). Glyphs bake WHITE so a
// single atlas serves any color - the runtime tints the sprite.
//
// Only fonts an actual Display Text node references are baked; a font used
// solely by static text costs the game nothing.

// Codepoint range baked into an atlas: printable ASCII. Anything outside it is
// dropped at draw time rather than rendered as a blank.
constexpr int kAtlasFirstChar = 32;   // space
constexpr int kAtlasLastChar = 126;   // '~'
constexpr int kAtlasCharCount = kAtlasLastChar - kAtlasFirstChar + 1;

// One glyph's placement in the atlas and how to lay it down. The runtime draws
// exactly the glyph's rect (not the whole cell), so blanks and side bearings
// cost no fill rate. Mirrored by the generated FontGlyph struct in
// font_data.gen.hpp - keep the two in sync.
struct AtlasGlyph {
    int u = 0, v = 0;        // top-left texel of the glyph in the atlas
    int w = 0, h = 0;        // glyph size (0 = nothing to draw, e.g. space)
    int xoff = 0, yoff = 0;  // pen/line-top -> glyph top-left, at baseSize
    int advance = 0;         // pen step to the next glyph, at baseSize
};

// Geometry of a font's atlas. Cells sit on a fixed grid with a 1px gutter, so
// bilinear sampling never bleeds a neighbor in (the same reason the engine's
// debug strip pads its cells).
struct AtlasLayout {
    int texW = 64, texH = 64;  // pow2, <= 512 (the PS2 texture cap)
    int cellW = 8, cellH = 8;
    int cols = 8;
    int lineH = 16;  // baseline pitch for multi-line text
    int baseSize = 16;  // GameFont::atlasSize the metrics were baked at
    bool clipped = false;  // glyphs did not fit under the 512px cap
    std::vector<AtlasGlyph> glyphs;  // kAtlasCharCount entries
};

// Computes a font's atlas geometry. Returns false when no usable font file is
// found. Codegen and the baker both call this, so the emitted metrics and the
// baked pixels always agree.
bool atlasLayout(const GameFont& font, const Project& p, AtlasLayout& out);

// Rasterizes the atlas (layout.texW x layout.texH RGBA, white glyphs).
bool bakeAtlasRGBA(const GameFont& font, const Project& p,
                   std::vector<unsigned char>& out, AtlasLayout& layout);

// Same, PNG-encoded (for res/fonts/atlas-<sanitized>.png). Empty on failure.
bool bakeAtlasPNG(const GameFont& font, const Project& p,
                  std::vector<unsigned char>& png);

// Font name -> res/fonts file name ("atlas-<sanitized>.png").
std::string atlasFileName(const std::string& fontName);

// --- Text icons (button glyphs) ----------------------------------------------
// Inline images any text can splice in with a `{{name}}` placeholder
// (docs/text-icons.md). The seeded set is named after the pad buttons, and its
// images are DRAWN here rather than shipped as blobs: the face buttons are
// geometry, the shoulder/Start/Select ones a rounded plate plus a label, so the
// set stays small in the exe and scales to any size.
//
// They bake WHITE on transparent - every consumer tints them with the color of
// the text they sit in, exactly like the font atlas glyphs.

// Built-in icon names, in the order the UI Editor lists them. These are the
// lowercased pad-button names (see textIconNameForPad), which is what lets
// `{{action:jump}}` resolve to a button's icon.
const std::vector<std::string>& builtinIconNames();

// Draws the built-in icon `name` at px x px into RGBA `out`. False when the
// name is not a built-in one (a user icon has a PNG instead) or px is unusable.
bool bakeBuiltinIconRGBA(const std::string& name, int px,
                         std::vector<unsigned char>& out);

// Same, PNG-encoded (for res/hud/icon-<name>.png). Empty on failure.
bool bakeBuiltinIconPNG(const std::string& name, int px,
                        std::vector<unsigned char>& png);

// Side length the built-in icons are generated at. Generous enough that a
// 512px menu panel can scale one up without visible softness.
constexpr int kIconBakeSize = 48;

// res/hud file name of a built-in icon ("icon-<sanitized>.png").
std::string iconFileName(const std::string& iconName);

// Drops the decoded-icon cache. Icon images are cached by name for the process
// (bakes and previews ask for the same few over and over), so call this after
// repointing an icon or regenerating its PNG or the old pixels keep showing.
void clearIconImageCache();

// --- Icon atlas (runtime text) -----------------------------------------------
// Runtime text (a Display Text node, a menu rebind row) cannot use a baked
// sprite, so the icons it may splice in ship as one sheet next to the font
// atlases - the same trick, one texture for every icon.

struct IconAtlasEntry {
    std::string name;
    int u = 0, v = 0, w = 0, h = 0;  // rect in the atlas
};

struct IconAtlasLayout {
    int texW = 64, texH = 64;  // pow2, <= 512
    int cell = kIconBakeSize;  // every icon is square and the same size
    int cols = 1;
    bool clipped = false;  // icons did not fit under the 512px cap
    std::vector<IconAtlasEntry> icons;  // one per Project::textIcons entry kept
};

// Geometry of the project's icon sheet. Codegen and the baker both call this,
// so the emitted rects and the baked pixels always agree.
IconAtlasLayout iconAtlasLayout(const Project& p);

// Rasterizes the sheet (layout.texW x layout.texH RGBA, white icons).
bool bakeIconAtlasRGBA(const Project& p, std::vector<unsigned char>& out,
                       IconAtlasLayout& layout);

// Same, PNG-encoded (for res/hud/icons.png). Empty when the project has no
// icons.
bool bakeIconAtlasPNG(const Project& p, std::vector<unsigned char>& png);

}  // namespace menubake
