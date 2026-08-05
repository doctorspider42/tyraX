#pragma once

#include <string>
#include <vector>

#include "menustyle.hpp"
#include "project.hpp"

// The menu layout engine (docs/menu-styles.md): (GameMenu + stylesheet) ->
// positioned boxes, row geometry, baked-cell tables and a VRAM estimate.
//
// This is the ONE place a menu's geometry is decided. Three consumers read it
// and none of them may re-derive it:
//   * menubake.cpp rasterizes the boxes into the panel / state / value /
//     description textures,
//   * the Menu Editor previews those same pixels and prints the cost,
//   * templates.cpp bakes the numbers the runtime compositor needs into
//     menu_data.gen.hpp.
//
// Host-only: no GL, no ImGui, no font handling (text placement inside a box is
// the baker's job, which is what keeps this exercisable from a harness).
namespace menulayout {

// Rows per menu. Was 8 (a panel had to fit its rows and 8 x 24px plus chrome is
// all a 512px texture holds); a scrolling list (`list { rows-visible: N }`)
// lifts it, because the rows then live in their own windowed texture.
constexpr int kMaxRows = 32;

// Rect on the panel canvas, pixels, origin top-left.
struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

// A MenuImage, fitted and placed. Index-aligned with nothing - it carries the
// image's own index so the baker can find its pixels.
struct ImagePlace {
    int image = 0;  // index into GameMenu::images
    int slot = 0;   // MenuImage::Slot - the baker draws Overlay last
    Box box;
    bool stretchBackground = false;  // the Background slot covers the content
};

struct Row {
    int entry = 0;  // index into GameMenu::entries
    Box box;        // the whole row strip (panel-wide)
    Box icon;       // icon box, w = 0 when the style asks for none
    std::string cls;
    bool selectable = true;
    // Resolved style per state. [StateNormal] is what the panel bakes; the
    // others are only used when paints[state] is true.
    menustyle::Computed style[menustyle::StateCount];
    bool paints[menustyle::StateCount] = {false, false, false};
    // Cell index in the state atlas per state, -1 = not baked (either the
    // state paints nothing or the atlas ran out - see Layout::stateClipped).
    int stateCell[menustyle::StateCount] = {-1, -1, -1};
    int descCell = -1;  // cell in the description atlas, -1 = no description
};

// One baked texture the menu ships. `bytesPerPixel4` is the depth the VRAM
// estimate assumed (the panel follows its style's `quant`).
struct Texture {
    std::string file;  // res/menus/<name>.png, relative to res/
    int w = 0, h = 0;
    int bits = 32;
    int words() const;  // GS words incl. the per-allocation padding
};

struct Layout {
    // --- the legacy contract (unchanged meaning) -----------------------------
    int panelW = 256;
    int canvasH = 64;   // panel texture height (pow2)
    int contentH = 64;  // drawn part, used for vertical centering
    int row0Y = 44;     // first row's top
    int rowH = 24;      // uniform row pitch (max row height when classes differ)
    bool clipped = false;  // content exceeded 512px and was cut

    // --- resolved styles ----------------------------------------------------
    menustyle::Computed panel, title, list, hint, desc, value, marker;

    // --- boxes --------------------------------------------------------------
    Box titleBox, titleRule, listBox, hintBox, descBox;
    std::vector<ImagePlace> images;
    std::vector<Row> rows;

    // --- scrolling ----------------------------------------------------------
    // rowsVisible < rows.size() = the list scrolls: the row strip is a WINDOW
    // into the panel texture, moved by an offset (no extra texture).
    int rowsVisible = 0;
    bool scrolls = false;
    // The scrolling strip's own texture (<menu>-list.png), only when scrolls.
    int listCanvasH = 0;
    bool listClipped = false;  // more rows than 512px of strip holds

    // --- the state atlas (<menu>-rows.png) ----------------------------------
    int stateCellW = 0, stateCellH = 0, statePitch = 0;
    int stateCells = 0, stateCanvasH = 0;
    bool stateClipped = false;  // cells did not fit 512px - reported, not hidden

    // --- the animated background layer (<menu>-bganim.png) ------------------
    // Its own sprite under the panel, because baked pixels cannot move. Scroll
    // bakes the source tiled TWICE along each scrolled axis and walks a window
    // through the first copy - the window then never leaves the texture, so this
    // needs no wrap mode and behaves exactly like the value strip. Frames stacks
    // the strip's frames and jumps the window between them.
    int bgAnimW = 0, bgAnimH = 0;        // texture size (pow2)
    int bgAnimTileW = 0, bgAnimTileH = 0;  // Scroll: how far the window walks
    int bgAnimFrameH = 0;                // Frames: one frame's height
    int bgAnimFrames = 0;

    // --- the description atlas (<menu>-desc.png) ----------------------------
    int descCellW = 0, descCellH = 0, descPitch = 0;
    int descCells = 0, descCanvasH = 0;

    // --- cost ---------------------------------------------------------------
    std::vector<Texture> textures;  // every texture this menu ships
    int spritesPerFrame = 0;        // worst case, excluding the dim overlay

    bool hasBgAnim() const { return bgAnimW > 0 && bgAnimH > 0; }
    bool hasStateAtlas() const { return stateCells > 0; }
    bool hasDescAtlas() const { return descCells > 0; }
    int words() const;         // total GS words
    float vramFraction() const;  // of the ~282 000-word texture heap
};

// The sheet a menu is styled by (GameMenu::style through the registry).
const menustyle::Sheet& sheetFor(const GameMenu& menu);

// The layout of a menu. `p` supplies the project directory (image sizes), the
// font registry and the save-menu row count.
Layout compute(const GameMenu& menu, const Project& p);

// The menu's own fields as the base of the cascade: panel width and border from
// `accent`, title/row font sizes, the menu font. A sheet overrides any of them;
// with no sheet these ARE the look, which is what makes an existing project
// bake identically. Public because the Style tab shows them as the base value.
menustyle::Computed baseFor(const GameMenu& menu, menustyle::Elem elem);

// The row list of a menu as the bake sees it. Only the save menu differs from
// what the author typed: its rows ARE its save slots, so it bakes that many
// BLANK rows and the game draws "SLOT n" into the geometry at runtime.
GameMenu asBaked(const GameMenu& menu, const Project& p);

// res/menus file names. All four sanitize identically because they share
// sanitizeName - menubake.cpp delegates here rather than keeping a second copy.
std::string sanitizeName(const std::string& name, const char* fallback);
std::string panelFileName(const std::string& menuName);
std::string valueStripFileName(const std::string& menuName);
std::string listFileName(const std::string& menuName);
std::string bgAnimFileName(const std::string& menuName);
std::string stateAtlasFileName(const std::string& menuName);
std::string descAtlasFileName(const std::string& menuName);

}  // namespace menulayout
