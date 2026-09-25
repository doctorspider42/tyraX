#pragma once

#include <string>
#include <vector>

#include "project.hpp"

// Procedural particle textures (docs/particles.md). Host-only, no GL - the
// treegen/starfield shape: a pure function of a ParticleTexGen recipe, so the
// Particle Editor's preview and the file it writes are the same pixels, and a
// re-bake of an unchanged recipe is byte-identical.
//
// Every kind keeps its SHAPE in the RGB channels as well as in alpha (the
// colour is premultiplied): an additive particle bag ignores texture alpha on
// the GS, so a flame whose edge lived only in alpha would draw as a square.
namespace particletex {

// RGBA8, g.size x g.size, row 0 at the top. Empty for kind 0.
std::vector<unsigned char> generate(const ParticleTexGen& g);

// Folder the baked textures live in (project-relative).
inline constexpr const char* kDir = "res/materials/particles";

// A file-name-safe form of an effect name ("Camp fire!" -> "camp-fire").
std::string fileStem(const std::string& effectName);

// Writes <kDir>/<stem>.png and a one-material <stem>.mtl pointing at it,
// each only when its bytes changed. Returns the .mtl's project-relative path,
// or "" with *err set.
std::string writeAssets(const std::string& projectDir, const std::string& effectName,
                        const ParticleTexGen& g, std::string* err);

}  // namespace particletex
