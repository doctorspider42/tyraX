#pragma once

#include <string>
#include <vector>

#include "glbparser.hpp"
#include "tmdl.hpp"
#include "vehiclesim.hpp"

// The vehicle import bake (docs/vehicles.md): one authored .glb/.fbx in, a
// body .tmdl plus one wheel .tmdl out.
//
// Host-only, no GL, no ImGui, no project.hpp - the aobake/matbake shape, so
// the whole thing runs from a harness against a real car before any of it is
// wired to a window.
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
};

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

}  // namespace vehbake
