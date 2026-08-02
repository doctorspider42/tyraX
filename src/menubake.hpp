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

// --- Lens flare sprites ------------------------------------------------------
// Procedural 64x64 RGBA sprites for the sun lens flare and the light-beam
// coronas (no font involved): kind 0 = soft radial glow (shape in alpha, for
// 2D additive sprites), kind 1 = thin ring (ditto), kind 2 = corona (shape
// in RGB - additive 3D bags blend Cs*FIX + Cd and ignore texture alpha).
// Written to res/hud/flare-{glow,ring,corona}.png by refreshGenerated when
// the project uses the flare / beams.
bool bakeFlarePNG(int kind, std::vector<unsigned char>& png);
std::string flareFileName(int kind);

// --- Sun and moon discs (docs/day-night-cycle.md) ----------------------------
// The two sky bodies a day/night cycle draws, baked to res/hud/ by
// refreshGenerated exactly like the flare sprites above and gated the same way
// (DAYCYCLE_USED in scene_data.hpp).
//
// The RGBA bakes are the single source: refreshGenerated PNG-encodes them for
// the console, and the editor viewport uploads the SAME pixels straight to GL
// while the phase slider moves. A second, "preview-quality" moon would be a
// second answer to what the moon looks like.
constexpr int kSunDiscSize = 64;
constexpr int kMoonDiscSize = 128;

// The sun is 64x64 with its shape in RGB: it is drawn through an additive bag,
// which blends Cs*FIX + Cd and never reads texture alpha (same rule as the
// corona, kind 2 above).
void bakeSunRGBA(std::vector<unsigned char>& rgba);
bool bakeSunPNG(std::vector<unsigned char>& png);

// The moon is 128x128 RGBA - an ordinary alpha-blended quad, so the disc mask
// lives in alpha. The near side is projected orthographically out of an
// equirectangular albedo map; `phase` (0 new .. 0.5 full .. 1 new) is applied
// as a terminator, with the lit limb toward +X so the renderer only has to
// rotate the quad (ambience::Resolved::moonUpAngle).
//
// `sourcePath` empty = NASA's embedded LRO colour map. Otherwise a project
// asset: 2:1 images are treated as equirectangular and projected, anything else
// is used as the disc face directly.
bool bakeMoonRGBA(float phase, const std::string& sourcePath,
                  std::vector<unsigned char>& rgba);
bool bakeMoonPNG(float phase, const std::string& sourcePath,
                 std::vector<unsigned char>& png);
// --- Interaction prompts -----------------------------------------------------
// The USE / PICK UP prompts are baked like a HUD text, with one difference: the
// action tokens in them are NOT composited in. A prompt has to keep telling the
// truth after the player rebinds the action at runtime, and a baked glyph
// cannot - so the text is baked with a HOLE where each glyph goes and the game
// blits the current binding's glyph from the icon sheet into it every frame.
// (Other baked text stays a build-time snapshot; use a Display Text node when it
// must follow a rebind.)

// Where one live glyph goes, in pixels inside the baked sprite.
struct PromptIconSlot {
    std::string action;
    int x = 0, y = 0, size = 0;
};

// Canvas size + one glyph slot per action token, in reading order and wherever
// they sit ("Press {{use}} to open" as much as a leading token, on any line).
// Icons that did NOT come from an action are baked in and get no slot: they
// cannot be rebound, so there is nothing for the game to keep up with. An empty
// slot list means the sprite is complete on its own. False when no usable font
// is found.
bool promptLayout(const HudText& text, const Project& p, int& w, int& h,
                  std::vector<PromptIconSlot>& slots);

// Rasterizes the prompt's TEXT (the glyph slots are left transparent) and
// reports them.
bool bakePromptRGBA(const HudText& text, const Project& p,
                    std::vector<unsigned char>& out, int& w, int& h,
                    std::vector<PromptIconSlot>& slots);

// Same, PNG-encoded. Empty on failure.
bool bakePromptPNG(const HudText& text, const Project& p,
                   std::vector<unsigned char>& png,
                   std::vector<PromptIconSlot>& slots);

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

// --- Credits rolls -----------------------------------------------------------
// A credits roll (Tools > Credits Editor, docs/credits.md) is a vertical flow
// of blocks - headings, lines, role/name pairs, images - baked into a STRIP of
// pow2 PAGE textures rather than one sprite per line: a roll is dozens of
// strings, and the PS2 pins every texture it draws in a ~1.33 MB VRAM budget
// (docs/gs-vram.md), so per-line sprites would flush mid-scroll while pages
// keep the runtime at two sprite draws a frame.
//
// creditsLayout() is the contract between this baker, the editor's preview and
// the generated runtime (which scrolls by strip pixels), so all three agree on
// where every block sits - the panelLayout() arrangement.

constexpr int kCreditsPageH = 256;  // page texture height (pow2)
// Page budget. 512x256 at 4 bits is ~64 KB + the per-allocation overhead, so
// sixteen pages is about all the GS has room for; beyond that the engine would
// flush its whole texture cache mid-roll. 16 pages = 4096 px of scroll (over
// three minutes at 20 px/s) - a longer roll wants a slower speed, card mode, or
// a second roll, and the editor says so instead of silently cutting.
constexpr int kCreditsMaxPages = 16;

// Where one block landed in the strip. Index-aligned with CreditsRoll::blocks,
// so the editor can list block positions and flag the ones that fell off.
struct CreditsBlockBox {
    int y = 0;             // top edge, in strip pixels
    int h = 0;             // laid-out height
    bool clipped = false;  // past the page budget: not baked, never shown
};

struct CreditsLayout {
    int pageW = 512;
    int pageH = kCreditsPageH;
    int pageCount = 0;  // pages that will be baked (>= 1 when anything fits)
    int contentH = 0;   // laid-out height in strip pixels
    bool clipped = false;  // some block did not fit the page budget
    std::vector<CreditsBlockBox> boxes;
    // Card mode (CreditsRoll::mode == 1): one entry per card, the page it
    // occupies. Cards are page-aligned by construction, which is what lets the
    // runtime show a card by drawing a single page.
    std::vector<int> cardPages;
};

CreditsLayout creditsLayout(const CreditsRoll& r, const Project& p);

// Rasterizes the whole strip (layout.pageW x layout.pageH*pageCount RGBA,
// row-major) - what the editor previews. False when no usable font is found.
bool bakeCreditsStripRGBA(const CreditsRoll& r, const Project& p,
                          std::vector<unsigned char>& out, CreditsLayout& layout);

// The strip sliced into one PNG per page (for res/credits/<name>-<k>.png).
// Bakes once and slices, so cost is independent of the page count.
bool bakeCreditsPagesPNG(const CreditsRoll& r, const Project& p,
                         std::vector<std::vector<unsigned char>>& pages,
                         CreditsLayout& layout);

// Roll name -> file name inside the BAKE folder. The pages and the hint live in
// `res/credits/pages/` and nothing else does: that folder is swept of whatever
// no roll claims on every build (a shortened or deleted roll must stop
// shipping), so the images a roll's Image blocks point at have to sit one level
// up, in `res/credits/`, where the build never touches them.
constexpr const char* kCreditsBakeDir = "res/credits/pages";
std::string creditsPageFileName(const std::string& rollName, int page);
std::string creditsHintFileName(const std::string& rollName);

// The skip hint as an ordinary HudText, so it bakes (and previews) through the
// existing text path - icon tokens included, which is how "PRESS {{confirm}} TO
// SKIP" shows the button. Baked at build like every static text: it is a
// snapshot of the binding, not a live readout.
HudText creditsHintText(const CreditsRoll& r);

// --- Text import -------------------------------------------------------------
// The authoring format for a credits roll as a plain text file
// (docs/credits.md), so a long roll can live in a text editor or come out of a
// spreadsheet:
//
//   # SECTION          -> a Heading block
//   Role: Name         -> a Pair block (several names: "Role: A, B" or repeated)
//   > centered line    -> a Line block, centered ("< " left, "| " right)
//   [image res/credits/logo.png 0.5]  -> an Image block (scale optional)
//   ---                -> a Break (next page / next card)
//   (blank line)       -> a Gap
//   anything else      -> a Line block
//
// Pure text -> blocks: no file system, no fonts, no project - which is what
// makes the format testable on its own.
std::vector<CreditsBlock> parseCreditsMarkup(const std::string& text);

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
