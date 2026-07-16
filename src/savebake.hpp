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

constexpr int kMaxIconTris = 800;  // beyond this the browser gets sluggish
constexpr int kMaxIconShapes = 8;
constexpr int kQuadShapes = 6;  // the flat icon's sway animation

constexpr int kIconSysBytes = 964;

// The "Checking memory card..." busy overlay, baked to res/hud/save-busy.png
// when missing (replaceable, like the save-menu sprites). Both the bake
// (refreshGenerated) and codegen (the sprite's size constants) derive from
// this one HudText so they can never disagree.
HudText busyText();

}  // namespace savebake
