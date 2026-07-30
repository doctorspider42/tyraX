#pragma once

#include <string>
#include <vector>

#include "charanim.hpp"
#include "glbparser.hpp"

// Procedural character generator (Tools > Character Generator,
// docs/character-generator.md). Host-only, no GL, no Project dependency - the
// treegen/matbake pattern, so the whole thing is exercisable from a small host
// harness instead of by clicking the GUI.
//
// What it is: MakeHuman's *data* driven by our own code. A macro blend over
// the CC0 base mesh (gender / age / muscle / weight / ethnicity) deforms the
// 19158-vertex reference body; the CC0 proxy mesh rides that deformation down
// to a **741-vertex PS2-budget body**; the rig is re-derived from the morphed
// mesh (MakeHuman defines each joint as a cube of base-mesh vertices, so the
// skeleton follows the morph instead of being fitted to it) and collapsed to
// ~23 Mixamo-named bones; the CC0 skin texture is box-filtered down to 256.
// The result is written as a plain .glb, which is where the editor's existing
// animated-model chain already begins.
//
// Deliberately NOT here: MakeHuman's height and body-proportion target sets
// (288 files, ~120 MB) - `heightMeters` scales the mesh instead, which is what
// a game actually wants. See the docs for what that costs.
namespace chargen {

// Every slider is 0..1 like MakeHuman's own macro modifiers, so the values
// mean the same thing they do in that program's UI.
struct Params {
    float gender = 0.5f;  // 0 female .. 1 male
    // Maps onto MakeHuman's four age levels: 0 = baby (1 year),
    // 0.1875 = child (10), 0.5 = young (25), 1 = old (90).
    float age = 0.5f;
    float muscle = 0.5f;  // 0 min .. 0.5 average .. 1 max
    float weight = 0.5f;  // 0 min .. 0.5 average .. 1 max

    // Ethnicity is a 3-way mix; the generator normalizes it to sum to 1, so
    // the caller can just raise one and let the others give way.
    float african = 1.0f / 3.0f;
    float asian = 1.0f / 3.0f;
    float caucasian = 1.0f / 3.0f;

    // The generated body is scaled so it stands this tall, feet on y = 0.
    float heightMeters = 1.75f;

    int skin = 0;        // index into skins(), -1 = untextured
    int textureSize = 256;  // power of two, 32..256 (PS2 VRAM budget)

    // Wearables: indices into clothesList() / shoesList() / hairList(), -1 =
    // none. Each becomes its own mesh part with its own texture, and the body
    // it covers is dropped rather than left to poke through.
    int clothes = -1;
    int shoes = -1;
    int hair = -1;
    // Triangle budget for the wearables: 0 low, 1 medium, 2 high. The source
    // garments are 3.5k-16k triangles - offline-render meshes - so this is the
    // difference between a character that fits a PS2 and one that does not.
    int clothingDetail = 1;

    // Procedural idle/walk/run/jump (charanim). On by default: a character
    // with no clips is a statue, and these are the clip names the generated
    // game's third-person locomotion already looks for.
    bool animations = true;
    charanim::Params anim;
    // A .glb/.fbx whose clips are retargeted onto the generated rig instead of
    // the procedural ones ("" = procedural). Any Mixamo-named rig works - that
    // is what the bone naming is for. The file is read at build time, so a
    // character is deterministic in (Params + that file).
    std::string animSource;
    charanim::RetargetOptions retarget;

    std::string name = "character";

    bool operator==(const Params& o) const;
    bool operator!=(const Params& o) const { return !(*this == o); }
};

// Skin textures found in the data directory (file stems, sorted). Empty when
// the data is missing.
const std::vector<std::string>& skins();

// The CC0 wardrobe found in the data directory, by slot (asset stems, sorted).
// Clothes and shoes both live in `clothes/` upstream and are split by name.
const std::vector<std::string>& clothesList();
const std::vector<std::string>& shoesList();
const std::vector<std::string>& hairList();

// Absolute path to the MakeHuman CC0 data (vendor/mh-assets next to the
// editor's own directory), or "" when it is not there. setup.ps1 fetches it.
std::string dataDir();
bool dataAvailable();

// Bone names of the generated rig, in palette order: Mixamo's naming
// ("mixamorig:Hips", ...), because that is what every free animation library
// and every retarget tool keys off.
const std::vector<std::string>& boneNames();

// Builds a character. Returns false with `error` set when the data directory
// is missing or unreadable; `warnings` collects non-fatal notes (a skin that
// would not decode, a bone whose joint the rig could not find).
//
// Deterministic: the same Params always produce the same bytes, which is what
// makes a rebuild-on-slider-drag preview cheap to reason about.
bool build(const Params& p, glbparser::Skel& out, std::vector<std::string>& warnings,
           std::string& error);

// Writes a built character into <projectDir>/res/models/characters/<name>.glb.
// On success `outRelPath` holds the project-relative path with forward
// slashes, ready for App::addModelObject.
bool writeAsset(const std::string& projectDir, const std::string& name,
                const glbparser::Skel& skel, std::string* outRelPath, std::string* outError);

// Tuned starting points for the UI (index 0 = the default).
struct Preset {
    const char* name;
    Params params;
};
const std::vector<Preset>& presets();

}  // namespace chargen
