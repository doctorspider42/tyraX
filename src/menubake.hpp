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

constexpr int kRowH = 24;  // entry row pitch
constexpr int kMaxEntries = 8;

// Geometry of a menu's panel. Flow images push the title and rows down;
// canvas height rounds up to a power of two (64..512, the PS2 texture cap)
// with the slack rows fully transparent.
struct PanelLayout {
    int panelW = 256;    // texture width (from GameMenu::panelW)
    int canvasH = 64;    // texture height (pow2)
    int contentH = 64;   // drawn part (border to border)
    int row0Y = 44;      // first entry row
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

}  // namespace menubake
