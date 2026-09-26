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
    int bodyTriBudget = 2400;
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

    // The FAST wheel (docs/vehicles.md, "A fast wheel"): "" = none, "@auto" =
    // the ordinary wheel again decimated to fastWheelTriBudget, anything else
    // = the name of a mesh node in the model (an artist's blurred wheel), which
    // is then left out of the body and of the wheel detection.
    std::string fastWheel;
    int fastWheelTriBudget = 120;

    // Translucent glass (VehicleDef::glassOpacity < 1): untextured glass-named
    // materials (glassMaterial) leave the merge into ONE part named "glass",
    // placed after "lamps", never decimated, tiered or mirrored - the runtime
    // draws it last with a vertex alpha. Off = byte-identical bake.
    bool glassSplit = false;

    // An AUTHORED far tier (VehicleDef::farModel, docs/vehicles.md "An authored
    // far model"): an absolute path to a second .glb/.fbx in the same space as
    // the model, "" = the decimated tiers. See collectFarModel for its rules.
    std::string farModel;
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
    // The fast wheel, same frame and same palette as `wheel`; no parts when the
    // definition has none (or the named node did not exist - see notes).
    tmdl::Model fastWheel;

    // The generated colour palette, or empty when nothing needed merging.
    // Written next to the baked models as an ordinary PNG so texbake, the
    // atlas planner and the VRAM accounting treat it like any other texture.
    std::vector<unsigned char> palettePng;
    int paletteSize = 0;  // square side in pixels

    // Top-down body silhouette used by the cheap moving blob-shadow quad.
    // It is derived from the exact canonical body bake, so the runtime gets a
    // car-shaped shadow without re-rendering the vehicle from a light camera.
    std::vector<unsigned char> shadowPng;

    // Source images retained by textured body/wheel parts. Names are the
    // bin-relative paths stored in the TMDL; both build and preview write them.
    struct Texture {
        std::string path;
        std::vector<unsigned char> png;
    };
    std::vector<Texture> textures;

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
    int fastWheelTris = 0;

    // Lamp clusters measured off lamp-named materials, canonical frame:
    // {|x| offset, y, z, half-size}; size 0 = none found (fallback).
    float lampRear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lampFront[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    // The emissive lamp PART: every lamp-named material's geometry, split out
    // of the merge into ONE body part named "lamps" - the rear lamps' corners
    // first, the front lamps' after - so the runtime can brighten the two
    // vertex RANGES per instance with one submit for the lot. -1 = the model
    // marked no lamps. lampRearVerts is the rear range's corner count (0 =
    // the model marked front lamps only). One part rather than two because a
    // submit is ~1 ms of fixed EE time: two lamp parts cost a driven frame a
    // fifth of its budget for a few dozen triangles.
    int lampPart = -1;
    int lampRearVerts = 0;
    // The "glass" part (Options::glassSplit), -1 = none split out.
    int glassPart = -1;

    // Triangles of the paint part's far tiers (body + four wheels each),
    // coarsest last - what a distant car costs, for the Cost tab. Empty when
    // the body was too small to tier.
    std::vector<int> farTris;
    // The body part carrying the far tier (-1 = none): the part whose shown
    // tier tells the runtime to stop the wheel bag. NOT parts[0] - that is
    // usually the lamps, which never tier.
    int farPart = -1;
    // Body parts hidden while the far tier shows (bit per part; the authored
    // far model's unreached glass - never the lamps), and what a distant car
    // submits.
    int farHideMask = 0;
    int farSubmits = 0;
    bool farAuthored = false;  // the tier came from Options::farModel

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
    std::string body, wheel, palette, shadow;  // bin-relative
    // The fast wheel's .tmdl, "" when the definition has no fast wheel.
    std::string fastWheel;
};

// Bin-relative paths for one definition. Pure string arithmetic, so codegen can
// name the files without running the bake.
BakedPaths pathsFor(const VehicleDef& v);

// The built-in tyre-effect textures (docs/vehicles.md, "Skid marks and
// smoke"): bin-relative paths the game loads when a definition names no
// material, and the generated PNGs the build writes there.
extern const char* kSkidTexturePath;
extern const char* kSmokeTexturePath;
std::vector<unsigned char> builtinSkidPng();
std::vector<unsigned char> builtinSmokePng();

// The built-in puff is the PARTICLE LIBRARY's smoke texture (particletex kind
// 1) from this fixed recipe - one procedural smoke generator in the editor,
// not two. builtinSmokePng() scales its alpha by kBuiltinSmokeDensity so the
// puff keeps the coverage of the pre-1.133 hand-written one (mean alpha 0.18
// against the recipe's 0.195).
ParticleTexGen builtinSmokeRecipe();
inline constexpr float kBuiltinSmokeDensity = 0.91f;

// A particle-library effect as a vehicle's tyre smoke (docs/vehicles.md,
// "Skid marks and smoke"): what VEHICLE_SMOKE_LOOKS carries per definition.
// Only the look travels - colour (on the vertex-colour scale: 128 = 1x over a
// texture, 255 = white untextured), peak alpha (128 = 1), start/end size,
// life and rise multipliers on the slip-driven puff, and the flipbook; WHEN
// and WHERE puffs spawn and how they drift stay the tyre's.
struct SmokeLook {
    float rgb[3];
    float alpha, size0, size1, life, rise;
    int frames;
    float fps;
};
SmokeLook smokeLookOf(const ParticleEffect& fx);

// "Tyre smoke": a library effect whose look reproduces the built-in puff
// (the recipe above, the built-in sizes, life and opacity) - the starting
// point the Vehicle Editor's "New library smoke" creates, so an author edits
// the car's smoke in the Particle Editor instead of starting from Smoke.
ParticleEffect tyreSmokeEffect();

// What the bake hands BACK to a definition: the lamp measurements (the
// glow clusters and the emissive part indices). Pure measurement with no
// authored value to respect, so it is adopted UNCONDITIONALLY - by the
// editor's per-frame tick and by bakeProject alike. It used to sit inside the
// editor's "drive spec still at its defaults" guard, and a car whose wheelbase
// had been adopted long before its model grew lamp materials therefore never
// received a lamp part index: the console drew the fallback quads over a body
// that carried real lamp parts, and a headless build (no GUI tick at all)
// could not have carried them either. The WHEEL RADIUS follows the drawn wheel
// the same way (rounded to 1 mm), with the ride height moved by the same
// amount so the author's clearance above the tyres survives - a radius that
// disagreed with the baked wheel sank or floated every tyre by the difference.
// Returns true when anything moved.
bool adoptMeasured(VehicleDef& v, const Result& r);

// Returns "" on success, else the first error. Definitions with no model are
// skipped silently - an author part-way through setting one up is not an error.
// Mutates the project: every baked definition adopts its lamp measurements
// (adoptMeasured) so the codegen that follows the bake reads what the bake
// produced, whether or not the .tyra had ever seen an editor.
std::string bakeProject(Project& p,
                        const std::function<void(const std::string&)>& log);

}  // namespace vehbake
