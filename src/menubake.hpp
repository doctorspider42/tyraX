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

// projectDir resolves image paths; pass "" to ignore images.
PanelLayout panelLayout(const GameMenu& menu, const std::string& projectDir);

// Rasterizes the panel into out (layout.panelW x layout.canvasH RGBA,
// row-major). Returns false when no usable font file is found.
bool bakePanelRGBA(const GameMenu& menu, const std::string& projectDir,
                   std::vector<unsigned char>& out, int& w, int& h);

// Same, PNG-encoded (for res/menus/<name>.png). Empty on failure.
bool bakePanelPNG(const GameMenu& menu, const std::string& projectDir,
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
bool bakeValueStripRGBA(const GameMenu& menu, const std::string& projectDir,
                        std::vector<unsigned char>& out, int& w, int& h);

// Same, PNG-encoded (for res/menus/<name>-values.png). Empty on failure.
bool bakeValueStripPNG(const GameMenu& menu, const std::string& projectDir,
                       std::vector<unsigned char>& png);

// Menu name -> value strip file name ("<sanitized>-values.png").
std::string valueStripFileName(const std::string& menuName);

// Editor preview helper: draws the given option label of every value entry
// onto an already-baked panel RGBA (right-aligned on its row), mirroring
// where the game composites the strip cells. current is index-aligned with
// menu.entries; out-of-range indices clamp.
void overlayValuePreview(const GameMenu& menu, const std::string& projectDir,
                         const std::vector<int>& current,
                         std::vector<unsigned char>& rgba, int w, int h);

// --- HUD texts ---------------------------------------------------------------
// On-screen texts (Tools > UI Editor > Texts) baked to res/hud PNG sprites -
// the engine has no font. Shown/hidden at runtime by the Show Text /
// Hide Text flow nodes.

// Baked texture dimensions for a HUD text (pow2, capped at 512). The text is
// drawn centered in the canvas, so the sprite's center anchor centers the
// content. Returns false when no usable font is found.
bool textLayout(const HudText& text, const std::string& projectDir, int& w,
                int& h);

// Rasterizes the text (multi-line on '\n', optional drop shadow).
bool bakeTextRGBA(const HudText& text, const std::string& projectDir,
                  std::vector<unsigned char>& out, int& w, int& h);

// Same, PNG-encoded (for res/hud/text-<sanitized>.png). Empty on failure.
bool bakeTextPNG(const HudText& text, const std::string& projectDir,
                 std::vector<unsigned char>& png);

// Text name -> res/hud file name ("text-<sanitized>.png").
std::string textFileName(const std::string& textName);

}  // namespace menubake
