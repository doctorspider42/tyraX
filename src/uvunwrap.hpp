#pragma once

#include <string>

// uvunwrap: automatic UV unwrapping for static .obj models (the Material
// Editor's "Unwrap UVs..." button, docs/material-painting.md). Host-only.
//
// The classic hard-surface recipe ("smart project"): faces cluster into
// CHARTS by normal similarity (BFS across shared edges while a face's
// normal stays within the angle threshold of its chart seed), every chart
// is projected onto its plane, rotated to the tightest bounding box, and
// all charts pack into the 0..1 square at ONE global scale (uniform texel
// density) with a margin against bilinear bleed. Deterministic: seeds are
// picked by area then face order, packing order is fixed, no randomness.
//
// The .obj is rewritten IN PLACE: every line is preserved verbatim except
// the old `vt` pool (dropped), the face `vt` references (renumbered; `v`
// and `vn` parts stay untouched) and the new `vt` block inserted before
// the first face. Positions, normals, materials, groups and comments
// survive byte-for-byte, so the file keeps working everywhere - viewport,
// bake, game. The PREVIOUS UVs are the only thing lost; projects are git
// repositories, revert from there.
namespace uvunwrap {

struct Params {
    float angleDeg = 55.0f;  // chart growing: max angle to the chart seed
    int marginPx = 4;        // spacing between charts, in texels of...
    int marginRefSize = 256; // ...a texture of this size
};

struct Stats {
    int faces = 0;
    int charts = 0;
    float coverage = 0.0f;  // packed chart area / the full 0..1 square
};

// Unwraps the model and rewrites the file. Returns false and fills `error`
// on failure (unreadable file, no faces); the file is only written after
// the whole unwrap succeeded.
bool unwrapObjFile(const std::string& path, const Params& p,
                   std::string& error, Stats* stats = nullptr);

}  // namespace uvunwrap
