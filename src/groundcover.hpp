#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "project.hpp"

// Ground cover (docs/ground-cover.md): grass-like cards that grow by
// themselves on a painted terrain material. Host side: the built-in tuft
// texture, the per-scene list of covers, and THE card generator the editor
// viewport draws with. The game runs the same generator in generated C++
// (groundCoverFillTile in game_templates.inc) - change one, change both.
namespace groundcover {

// The built-in tuft card (used when a cover names no material). Written by
// the build (and by the editor when a cover is switched on) only when bytes
// change.
inline constexpr const char* kBuiltinMtl = "res/materials/vegetation/grass-tuft.mtl";
inline constexpr const char* kBuiltinPng = "res/materials/vegetation/grass-tuft.png";

// World tile edge, metres. Cards are seeded per tile from its integer world
// coordinates, so the same tile always grows the same cards.
inline constexpr float kTileMetres = 2.0f;
// Hard cap of cards per tile and cover (density x tile area is clamped).
inline constexpr int kMaxCardsPerTile = 64;
// The outer fraction of the draw distance in which cards thin out.
inline constexpr float kFadeBand = 0.25f;

// The .mtl a cover draws with ("" = the built-in tuft).
std::string materialOf(const GroundCover& c);

// One enabled cover of a scene: layer -1 = the base terrain material.
struct Entry {
    int layer = -1;
    GroundCover cover;
};
// Enabled covers in draw order (base first, then the layers bottom-up).
// Empty when the scene has no terrain.
std::vector<Entry> covers(const SceneData& sc);
bool projectUsesBuiltin(const Project& p);

// Cards a tile of `tileUnits` seeds for a cover (density x area, clamped).
int cardsPerTile(const GroundCover& c, float tileUnits, float unitsPerMeter);

// RGBA8 size x size, row 0 at the top: a tuft of tapered, leaning blades with
// a binary alpha cutout (the GS draws it with the alpha test). The two outer
// rows/columns stay transparent so bilinear filtering never wraps a root
// into the top edge.
std::vector<unsigned char> builtinTuft(int size = 64);

// Writes kBuiltinPng + kBuiltinMtl under projectDir (each only when its bytes
// changed). False with *err on a write failure.
bool writeBuiltin(const std::string& projectDir, std::string* err);

// ---- The card generator (twin: groundCoverFillTile in the game) ----------

// Tile seed and the game's LCG (the particles' prand).
uint32_t tileSeed(int tx, int tz, int coverIndex);
inline float prand(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return (float)(s >> 8) * (1.0f / 16777216.0f);
}

struct Card {
    float x = 0, y = 0, z = 0;  // card CENTRE (the base is y - halfH)
    float halfW = 0, halfH = 0;
    float thin = 0;   // 0..1: drawn while thin < the tile's fade
    float shade = 1;  // brightness multiplier 0.8..1
    bool mirror = false;  // odd card slots draw the texture mirrored
};

// Weight 0..1 of cover `layer` at (x, z) - the painted layer stack composited
// the way the terrain draws it (base = what no layer covers).
struct Ground {
    virtual ~Ground() = default;
    virtual float height(float x, float z) const = 0;
    virtual float weight(int layer, float x, float z) const = 0;
};

// The cards of one tile for one cover; rejected candidates are left out.
// `sizeUnits` = metres -> world units (ProjectSettings::unitsPerMeter).
void tileCards(int tx, int tz, int coverIndex, const GroundCover& c, int layer,
               float tileUnits, float unitsPerMeter, const Ground& g,
               std::vector<Card>& out);

// Composited weight of `layer` from per-layer raw weights w[0..n-1] (0..1):
// layer = w_l * prod_{k>l}(1 - w_k), base (-1) = prod_k (1 - w_k).
float compositeWeight(int layer, const float* w, int n);

}  // namespace groundcover
