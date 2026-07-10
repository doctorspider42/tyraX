#pragma once

#include <functional>
#include <string>

#include "project.hpp"

// Build-time texture bake: mirrors res/ into .res-baked/ (which the generated
// Makefile copies next to the ELF instead of res/), quantizing PNGs to
// palettized 8/4-bit per the project policy. Sources in res/ stay untouched,
// so quality settings can change freely between builds.
//
// Policy resolution per PNG:
//  - every .obj under res/models and every .mtl under res/materials +
//    res/models is an "asset"; the PNGs its materials reference belong to it
//  - an asset with an entry in Project::textureQuality pins its textures to
//    that quality; when several assets share a texture, the HIGHEST quality
//    wins (full > 8-bit > 4-bit) - important textures stay sharp
//  - unreferenced PNGs under res/models|materials|textures follow the
//    project-wide ProjectSettings::textureQuant
//  - res/hud and res/fonts are never quantized (UI legibility)
namespace texbake {

// Returns "" on success or an error message. log receives progress lines.
std::string bake(const Project& p,
                 const std::function<void(const std::string&)>& log);

}  // namespace texbake
