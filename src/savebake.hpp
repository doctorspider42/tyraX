#pragma once

#include <string>
#include <vector>

#include "project.hpp"

// Bakes the memory card save appearance (Tools > Save Editor) into the two
// files the PS2 browser reads from a save directory: icon.sys (964-byte
// metadata block: title, background/light colors, icon filenames) and
// list.icn (a PS2 3D icon - here a static two-sided quad carrying a 128x128
// texture). Both are written to res/save/ on every build (refreshGenerated)
// and copied onto the card by the generated save system the first time a
// slot is written. Format per Martin Akesson's "PS2 Icon Format v0.5".
namespace savebake {

// Title shown in the PS2 browser: Project::saveTitle, falling back to the
// project name. '|' marks the second-line break (stripped from the output).
std::string displayTitle(const Project& p);

// The 964-byte icon.sys block. Title is 7-bit ASCII passed through as
// single-byte Shift-JIS (other bytes become '?'), clamped to 67 chars.
std::vector<unsigned char> iconSys(const Project& p);

// The 128x128 RGBA texture the icon carries: Project::saveIcon (any
// stb-readable image, bilinearly resampled, alpha composited over a dark
// backdrop) or the built-in placeholder when the path is empty/unreadable.
// Also what the Save Editor previews - identical to what ships (models with
// their own texture override it inside iconIcn).
std::vector<unsigned char> iconTextureRGBA(const Project& p);

// What the baked icon actually contains - the Save Editor's stats line and
// the size source for templates::saveSizeInfo / the generated copy buffer.
struct IconInfo {
    int triangles = 0;
    int shapes = 1;        // animation shapes (morph frames)
    int bytes = 0;         // exact list.icn size
    std::string source;    // "flat image", "house.obj", "robot.glb (walk)"
    std::string warning;   // non-empty = fell back to the flat icon and why
};

// Geometry sources, picked by Project::saveIconModel:
//  - ""                : the flat two-sided quad carrying iconTextureRGBA,
//                        gently swaying (kQuadShapes morph shapes).
//  - res/models/*.obj  : the model's triangles + its map_Kd texture, with
//                        the same sway baked into saveIconFrames shapes.
//  - res/models/*.glb  : Project::saveIconClip sampled into saveIconFrames
//                        morph shapes (glbparser::bake) - a real animated
//                        icon; its embedded texture ships.
// Models over kMaxIconTris fall back to the quad (IconInfo::warning says so;
// the build never fails over an icon). Everything is fitted into icon space
// (y down, standing on y = 0, ~3.6 units tall).
IconInfo iconInfo(const Project& p);
std::vector<unsigned char> iconIcn(const Project& p, IconInfo* info = nullptr);

// What the Save Editor shows: `size` x `size` RGBA renders of the baked icon,
// ONE PER ANIMATION SHAPE, so cycling them previews the motion that ships.
//
// This rasterizes the GEOMETRY and multiplies texture x vertex colour the way
// the PS2 browser does. That product is the whole point: a model with no
// map_Kd carries its colours in the vertices and gets a near-white texture
// (modelFallbackTexture), so a preview that drew the texture segment alone -
// which is what this replaced - showed a blank square for every such icon.
// Empty when there is no geometry to draw.
std::vector<std::vector<unsigned char>> iconPreviewFrames(const Project& p,
                                                          int size);

// Idle motions for a source with no animation of its own (Project::
// saveIconMotion). The browser plays the shapes as morph targets, so every
// one of these is baked as displaced COPIES of shape 0 - there is no runtime
// transform on a memory card icon, which is also why they must stay small
// enough to morph cleanly (a big rotation between two shapes lerps THROUGH
// the model rather than around it).
struct IconMotion {
    const char* key;    // what the .tyra stores - stable, never translated
    const char* label;  // what the picker shows
    const char* desc;   // one line of help
};
const std::vector<IconMotion>& iconMotions();

// Index into iconMotions() for a stored key. An empty or unknown key gives 0
// (sway), which is what every icon did before the setting existed - so an old
// project and one written by a newer editor both degrade to the old look.
int iconMotionIndex(const std::string& key);

constexpr int kMaxIconTris = 800;  // beyond this the browser gets sluggish
constexpr int kMaxIconShapes = 8;
constexpr int kQuadShapes = 6;  // shapes the flat icon animates over

constexpr int kIconSysBytes = 964;

// The "Checking memory card..." busy overlay, baked to res/hud/save-busy.png
// when missing (replaceable, like the save-menu sprites). Both the bake
// (refreshGenerated) and codegen (the sprite's size constants) derive from
// this one HudText so they can never disagree.
HudText busyText();

// The async write's activity indicator: kSpinnerFrames cells of
// kSpinnerCell x kSpinnerCell laid out in ONE row, baked to
// res/hud/save-spinner.png when missing (user-replaceable, like the save-menu
// sprites). A sheet rather than a rotated quad because the 2D renderer has no
// rotation - the game walks Sprite::offset across the cells, exactly the way
// the font atlas is drawn. False only if the PNG encoder failed.
//
// BOTH SHEET DIMENSIONS MUST BE POWERS OF TWO. The GS takes 8/16/32/.../512
// and the engine asserts on anything else, so an 8x24 layout (a 192x24 strip)
// halts the game at boot with "Texture width/height should be
// 8/16/32/64/128/256/512" - which presents as the TyraX splash never handing
// over, not as a bad sprite. 8 cells x 32 = 256 wide by 32 tall: both legal.
// A replacement PNG has to respect the same rule.
constexpr int kSpinnerFrames = 8;
constexpr int kSpinnerCell = 32;
bool spinnerPNG(std::vector<unsigned char>& png);

// The sheet a project will actually ship, after Project::saveSpinnerImage has
// been checked. This is the one place that decides, so the Save Editor's
// preview, its warning and the generated constants can never disagree.
//
// A rejected image FALLS BACK to the built-in rather than shipping: the
// failure mode it prevents is a game that asserts at boot and never leaves the
// TyraX splash, which is a miserable thing to debug from a screenshot.
struct SpinnerInfo {
    std::string resPath;  // project-relative, e.g. "res/hud/save-spinner.png"
    int frames = kSpinnerFrames;
    int sheetW = 0, sheetH = 0;
    int cellW = kSpinnerCell, cellH = kSpinnerCell;
    bool custom = false;    // a picked image is in use (not the built-in)
    std::string warning;    // why a picked image was rejected ("" = fine)
};
SpinnerInfo spinnerInfo(const Project& p);

}  // namespace savebake
