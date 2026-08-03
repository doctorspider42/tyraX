#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Procedural low-poly WEAPON model generator (Tools > Weapon Editor >
// "Generate model"). Host-only, no GL, no Project dependency - the
// treegen/stochtile/matbake pattern: pure functions over a Params struct, so
// the whole thing is exercisable from a 40-line host harness.
//
// Why it exists: a weapon system needs weapons to point at, and every
// free gun model on the internet arrives with a licence the project would
// then have to carry. These are generated from numbers, so they are the
// project's own content with no strings attached - and, being parametric,
// they are also FASTER to author than hunting for an asset: pick a kind,
// drag Length, get a model sized in the project's own world units.
//
// Output convention (this is load-bearing - the runtime pins the viewmodel
// with it): the weapon points down +Z, +Y is up, +X is right, and the ORIGIN
// sits where the hand grips it. So a viewmodel offset of (0.2, -0.18, 0.55)
// puts the grip at the player's right hip and the barrel pointing where they
// look, with no per-model fiddling.
//
// Deliberately UNTEXTURED: flat Kd materials only. A weapon is small on
// screen, the engine's baked vertex lighting already reads the facets, and a
// texture per gun is GS VRAM the effects want more (docs/gs-vram.md).
namespace weapongen {

struct Params {
    // The silhouette. Each kind is a different construction, not a tuning of
    // one shape - a revolver's drum and a sword's taper have nothing in common.
    enum Kind {
        Pistol = 0,
        Revolver = 1,
        Smg = 2,
        Rifle = 3,
        Shotgun = 4,
        Knife = 5,
        Sword = 6,
        Axe = 7,
        Crowbar = 8,
        KindCount = 9,
    };
    int kind = Pistol;

    // Overall length along +Z in WORLD UNITS - the one size knob. Every other
    // dimension is a fraction of it (the treegen height rule), so this scales
    // the weapon instead of stretching it.
    float length = 0.34f;
    // Chunkiness: multiplies the cross-section without touching the length.
    float bulk = 1.0f;
    // Radial segments on the round parts (barrels, drums, hafts). 6 is the
    // era-correct floor; above ~12 you are paying for a curve nobody sees.
    int sides = 8;
    // Small deterministic per-part jitter (wear, hand-made irregularity).
    // Zero = perfectly machined. Same seed = same bytes, always.
    uint32_t seed = 1u;
    float wear = 0.0f;  // 0..1

    // The three material colors. Every part belongs to exactly one of them,
    // which is what keeps a generated weapon at 3 GS material switches.
    float metal[3] = {0.34f, 0.35f, 0.38f};   // frame, barrel, blade
    float grip[3] = {0.14f, 0.13f, 0.12f};    // grip, stock, haft
    float accent[3] = {0.62f, 0.52f, 0.28f};  // trim, sights, pommel, brass
};

// Flat triangle lists, pos3 + normal3 + uv2 per vertex (24 floats per
// triangle) - the treegen::Mesh layout, so the OBJ writer below is the same
// shape of code and the numbers mean the same thing.
struct Mesh {
    std::vector<float> metal;
    std::vector<float> grip;
    std::vector<float> accent;
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    int triangles() const {
        return (int)(metal.size() + grip.size() + accent.size()) / 24;
    }
};

Mesh generate(const Params& p);

// Human name of a kind, for the UI and the default asset name.
const char* kindName(int kind);

// Tuned starting points (one per kind, index == Params::Kind).
const std::vector<Params>& presets();

// Writes res/models/weapons/<name>.obj + .mtl under projectDir (creating the
// folder). `name` must already be filesystem-safe. On success outObjRel holds
// the project-relative OBJ path (forward slashes).
bool writeAssets(const std::string& projectDir, const std::string& name,
                 const Params& p, const Mesh& mesh, std::string* outObjRel,
                 std::string* outError);

}  // namespace weapongen
