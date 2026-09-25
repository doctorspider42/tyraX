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

// RGBA8, g.size x g.size, row 0 at the top. Empty for kind 0. `frame` picks
// one of g.frames flipbook frames: the noise travels through a SEAMLESS loop
// (frame g.frames would equal frame 0), so the console can cycle them forever.
std::vector<unsigned char> generate(const ParticleTexGen& g, int frame = 0);

// Frame k's material next to frame 0's: "a/fire.mtl" -> "a/fire-f2.mtl"
// (k = 0 returns the path unchanged). The ONE naming rule - the bake, codegen
// and the viewport all derive frame paths through it.
std::string framePath(const std::string& mtlPath, int k);

// Folder the baked textures live in (project-relative).
inline constexpr const char* kDir = "res/materials/particles";

// A file-name-safe form of an effect name ("Camp fire!" -> "camp-fire").
std::string fileStem(const std::string& effectName);

// Writes <kDir>/<stem>.png and a one-material <stem>.mtl pointing at it (plus
// <stem>-f<k>.png/.mtl for every further flipbook frame), each only when its
// bytes changed. Returns frame 0's .mtl project-relative path, or "" with
// *err set.
std::string writeAssets(const std::string& projectDir, const std::string& effectName,
                        const ParticleTexGen& g, std::string* err);

// Bakes every layer of `fx` that has a recipe (layer 0 = the effect's own
// fields, then fx.layers) under project::particleLayerStem, points each
// layer's materialPath at its frame 0 and pins every frame to full colour in
// p.textureQuality. Returns the number of layers baked, -1 with *err set.
int bakeEffect(Project& p, ParticleEffect& fx, std::string* err);

}  // namespace particletex
