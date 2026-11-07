#pragma once

#include <string>

#include "glbparser.hpp"

// glTF 2.0 binary (.glb) WRITER - the exact inverse of glbparser::parseSkel.
// Host-only, no GL, no Project dependency.
//
// The Character Generator produces a `glbparser::Skel` in memory and drops it
// into the project as an ordinary .glb asset, because that is where the whole
// existing animated-model chain already starts: import validation, the
// viewport preview, the Animation Editor, the .tskl bake with its LODs, player
// avatars, NPCs. Writing a real .glb instead of a private format means none of
// that had to learn about generated characters - and the file also opens in
// Blender, which is how a generated body gets hand-finished.
//
// The output deliberately stays inside the subset glbparser reads: one buffer
// (the GLB BIN chunk), no sparse accessors, embedded PNG images, flat triangle
// lists without index buffers, LINEAR/STEP animation samplers.
namespace gltfwrite {

// Serializes a Skel into .glb bytes. `generator` goes into asset.generator.
// Bind-pose geometry, the matrix palette and every clip are written; LODs
// (SkelPart::lods) are NOT - they are a bake-time artefact regenerated from
// the base mesh by meshlod, and glTF has no place for them.
std::string writeGlb(const glbparser::Skel& skel, const std::string& generator);

// Same, straight to a file. Returns false with `error` set on an I/O failure.
bool writeGlbFile(const std::string& path, const glbparser::Skel& skel,
                  const std::string& generator, std::string& error);

}  // namespace gltfwrite
