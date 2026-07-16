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
// Also what the Save Editor previews - identical to what ships.
std::vector<unsigned char> iconTextureRGBA(const Project& p);

// The complete list.icn: static quad geometry + iconTextureRGBA converted
// to the icon texture format. Fixed size: kIcnBytes.
std::vector<unsigned char> iconIcn(const Project& p);

// list.icn is fixed-size (12 vertices, uncompressed 128x128 BGR555 texture):
// 20 header + 12*24 vertex + 36 animation + 32768 texture bytes.
constexpr int kIcnBytes = 20 + 12 * 24 + 36 + 128 * 128 * 2;
constexpr int kIconSysBytes = 964;

// The "Checking memory card..." busy overlay, baked to res/hud/save-busy.png
// when missing (replaceable, like the save-menu sprites). Both the bake
// (refreshGenerated) and codegen (the sprite's size constants) derive from
// this one HudText so they can never disagree.
HudText busyText();

}  // namespace savebake
