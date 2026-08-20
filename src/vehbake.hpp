#pragma once

#include <functional>
#include <string>
#include <vector>

#include "glbparser.hpp"
#include "project.hpp"
#include "tmdl.hpp"
#include "vehiclesim.hpp"

// The vehicle import bake (docs/vehicles.md): one authored .glb/.fbx in, a
// body .tmdl plus one wheel .tmdl out.
//
// Host-only, no GL, no ImGui - the aobake/texbake shape, so the whole thing
// runs from a harness against a real car before any of it is wired to a
// window. Everything above the build-path section at the bottom is free of
// project.hpp as well, which is what makes that harness a 40-line main().
//
// This is deliberately a VEHICLE importer and not a general "static .glb"
// importer. A vehicle has to be cut up (body vs wheels), re-framed (into the
// canonical forward/up the sim works in) and re-materialised (see the merge
// below) no matter what, and none of those steps mean anything for an ordinary
// prop. Making static .glb work everywhere is a separate, larger job that
// would touch model classification, texbake and codegen; this touches none of
// them, because what it emits is an ordinary .tmdl.
//
// Two things it exists to solve, both measured on the CC96 test car:
//
//   * 36 parts. A .tmdl part is one bag and a bag is ~1 ms of fixed EE time,
//     so the model as authored is 36 ms of submit overhead - nearly two PAL
//     frames for one parked car. `mergeUntextured` below is the fix.
//
//   * 8780 triangles. PS2-era cars are 1-3k, and the budget knobs below are
//     what bring an asset authored for a modern renderer into that range.
namespace vehbake {

struct Options {
    // Triangle budgets. Decimation is meshlod's quadric-error collapse, the
    // same one both model bakes already use.  0 = leave the mesh alone.
    int bodyTriBudget = 1500;
    // 700 rather than the ~150 a PS2 wheel would suggest, because a wheel is
    // several MATERIALS (tyre, rim, disc, trim) and meshlod locks material
    // seams - so the collapse cannot thin the rim without destroying its
    // roundness. Measured on the test car: at 200 the silhouette lost 21% of
    // its radius, at 700 it loses 2%.
    int wheelTriBudget = 700;

    // Merge every UNTEXTURED material into one part, baking each one's colour
    // into a generated palette texture and pointing its vertices at the right
    // texel (see palettePng). Textured materials always keep their own part -
    // they have real UVs that cannot be rewritten.
    //
    // This is what takes the test car from 36 submits to 2. It is on by
    // default because a vehicle that does not do it cannot be shipped, and the
    // switch exists only so the panel can show what it costs.
    bool mergeUntextured = true;

    // Bin-relative path the GAME will load the generated palette from, e.g.
    // "models/cars/car1-palette.png". Baked into the merged part's texture
    // field, so it has to be the path the importer actually writes the PNG to -
    // a .tmdl naming a texture that is not there loads as untextured white.
    std::string paletteTexture;

    // Paint shine, 0..1: the BODY's paint gets a reflection pass at this
    // strength, riding fields tmdl already carries (docs/reflective-materials.md).
    // The untextured merge is split into paint and MATTE (rubber, near-black
    // trim - see shinyMaterial), so tyres and bumpers stay dull the way real
    // ones do; the split costs one extra submit and only happens when shine
    // is on. The wheels never shine. 0 writes nothing, so an existing bake is
    // byte-identical.
    float bodyShine = 0.0f;

    // What the paint MIRRORS: a bin-relative sphere map ("textures/x.png"),
    // or empty for the engine's dynamic "@sky" env map. A static map is the
    // era's own trick - Underground's wet lacquer is vertical light streaks
    // in exactly such a texture - and it reads far stronger than a smooth
    // sky gradient, whose reflection is nearly invisible by construction:
    // a gradient has no features to see MOVE.
    std::string bodyReflMap;
};

// A project-relative reflection-map path ("res/textures/x.png") as the
// bin-relative path the tmdl must carry ("textures/x.png") - the Makefile's
// resources step copies res/* to bin/, so stripping the prefix IS the mapping.
// One function, because the editor's per-frame bake and the build's
// bakeProject must not each spell it.
std::string binReflPath(const std::string& resRel);

struct Result {
    tmdl::Model body;
    tmdl::Model wheel;  // ONE wheel, hub at the origin, ready to be placed

    // The generated colour palette, or empty when nothing needed merging.
    // Written next to the baked models as an ordinary PNG so texbake, the
    // atlas planner and the VRAM accounting treat it like any other texture.
    std::vector<unsigned char> palettePng;
    int paletteSize = 0;  // square side in pixels

    vehiclesim::Detection detection;

    // Seeded from the model's own measurements, in the units parseSkel reports
    // (metres - both importers normalise, so this is NOT the raw file scale).
    vehiclesim::DriveSpec spec;

    // Stats for the Vehicle Editor's cost readout. Before/after so the panel
    // can state what the merge and the decimation actually bought rather than
    // asserting that they help.
    int srcParts = 0, srcTris = 0;
    int bodyParts = 0, bodyTris = 0;
    int wheelParts = 0, wheelTris = 0;

    // Lamp clusters measured off lamp-named materials, canonical frame:
    // {|x| offset, y, z, half-size}; size 0 = none found (fallback).
    float lampRear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lampFront[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    std::vector<std::string> notes;
};

// Model-space AABBs of every mesh-bearing node in a parsed skeleton - the
// wheel detector's input.
//
// The load-bearing detail: SkelPart::positions are LOCAL to the node that
// owns them (a rigid mesh node gets a palette slot with an identity inverse
// bind matrix, so the skin evaluates to nodeGlobal * p), which measured as
// four wheels all reporting the same unit-cube AABB at the origin until the
// node transforms were composed in. Anything reading positions straight out
// of a Skel and expecting model space is wrong in exactly that way.
std::vector<vehiclesim::MeshNode> meshNodes(const glbparser::Skel& skel);

// Parses the model and detects its wheels without baking anything - what the
// import dialog shows before the author commits.
bool inspect(const std::string& modelPath, vehiclesim::Detection& out,
             std::vector<vehiclesim::MeshNode>& nodes, std::string& error);

// The full bake.
bool build(const std::string& modelPath, const Options& opt, Result& out,
           std::string& error);

// --- the build-path bake ----------------------------------------------------
//
// Bakes EVERY definition in a project into `.res-baked/vehicles/`, which the
// generated Makefile's `RESDIR` copies next to the ELF. The texbake::bake
// shape, and called from the same two places: the Runner's build and the
// headless CLI.
//
// This exists because the bake used to run only from the editor's per-frame
// tick, which meant a headless `--build` shipped a game with no vehicle
// geometry at all - measured, the directory came back empty. One function,
// called by the build AND by the editor, is what stops the console and the
// preview from being able to disagree about what a car is.
//
// Files are content-compared before writing, so a build that changed nothing
// hands the compiler no fresh mtimes.
struct BakedPaths {
    std::string body, wheel, palette;  // bin-relative, "" if not produced
};

// Bin-relative paths for one definition. Pure string arithmetic, so codegen can
// name the files without running the bake.
BakedPaths pathsFor(const VehicleDef& v);

// Returns "" on success, else the first error. Definitions with no model are
// skipped silently - an author part-way through setting one up is not an error.
std::string bakeProject(const Project& p,
                        const std::function<void(const std::string&)>& log);

}  // namespace vehbake
