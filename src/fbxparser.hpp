#pragma once

#include <string>

#include "glbparser.hpp"

// FBX importer for animated models, built on the vendored ufbx reader
// (vendor/ufbx, MIT - cloned by setup.ps1 like the other deps). It fills the
// SAME structures glbparser produces, so everything downstream - the
// viewport preview, the import-status validation, .tskl serialization, LOD
// generation and codegen - is format-agnostic and untouched.
//
// Support matrix (what ufbx hands us after normalization):
//  - binary and ASCII FBX, any exporter version ufbx reads (Blender, Maya,
//    3ds Max...); axes/units normalized to the glTF convention (right-handed
//    Y-up, meters) so a Maya centimeter rig imports at the same size as its
//    .glb twin
//  - skinned meshes (weights capped to the 4 strongest, renormalized) and
//    rigid node animation; geometry transforms (Maya pivots) baked into the
//    vertices
//  - every anim stack ("take") becomes a named clip. FBX animation curves
//    are RESAMPLED at a fixed rate and then keyframe-reduced per channel
//    (RDP), instead of translating the curve soup (compound rotation
//    orders, pre/post rotations, pivots) - identical poses, simple code
//  - materials: PBR base color when present, classic FBX diffuse otherwise;
//    textures either embedded or from files next to the .fbx (non-PNG
//    transcoded, same rules as .glb)
// Not supported (warning, piece skipped): blend shapes/morph targets,
// non-file procedural textures. Unreadable/empty files fail with `error`.
namespace fbxparser {

// Morph-frame bake at `fps` samples per second - the editor-side preview /
// validation path, mirroring glbparser::bake.
bool bake(const std::string& path, float fps, glbparser::Baked& out,
          std::string& error);

// Skeletal representation for the PS2 runtime - mirrors glbparser::parseSkel.
bool parseSkel(const std::string& path, glbparser::Skel& out,
               std::string& error);

// FBX files, unlike .glb, often reference textures as separate files. Import
// copies the referenced files next to the copied .fbx so a project stays
// self-contained; returns the number copied (missing files are skipped - the
// parse itself warns about them).
int copyExternalTextures(const std::string& fbxPath,
                         const std::string& destDir);

}  // namespace fbxparser

// Format dispatch by extension: .glb -> glbparser, .fbx -> fbxparser. Import
// sites call these so adding another animated-model format stays a one-line
// change here.
namespace animimport {

// bake()/parseSkel() also apply the "<model>.uvs" replacement-UV sidecar
// (the Material Editor's UV unwrap for animated models - .glb/.fbx sources
// can't be rewritten, docs/material-painting.md). Because the sidecar is
// folded in HERE, every consumer sees the same mapping: the editor
// previews and matbake (Baked path), and the .tskl the game ships plus
// its LODs, which are generated afterwards (Skel path). A part is matched
// by material name + vertex count; a stale sidecar part (count mismatch
// after a re-export) is skipped. Format v1, little-endian:
//   "TXUV", u32 version = 1, u32 partCount,
//   per part: char material[32] (NUL-padded), u32 vertexCount,
//             vertexCount * 2 f32 (u, v per flat corner)
bool bake(const std::string& path, float fps, glbparser::Baked& out,
          std::string& error);
bool parseSkel(const std::string& path, glbparser::Skel& out,
               std::string& error);

// Writes the sidecar next to the model (partUvs parallel to baked.parts,
// each vertexCount * 2 floats; an empty inner vector skips that part).
bool writeUvSidecar(const std::string& modelPath,
                    const glbparser::Baked& baked,
                    const std::vector<std::vector<float>>& partUvs,
                    std::string& error);

}  // namespace animimport
