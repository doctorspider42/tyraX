#pragma once

#include <string>
#include <vector>

// Procedural road surface textures (docs/road-textures.md). Host-only, no GL,
// no ImGui and no project model - the particletex/treegen shape: a pure
// function of a RoadTexParams recipe, so the generator window's preview, the
// files it writes and the --road-texture CLI are the same pixels, and a
// re-bake of an unchanged recipe is byte-identical (seeded hashes only, never
// a running RNG).
//
// Layout contract (the road tessellator's, docs/roads.md): U runs 0..1 ACROSS
// the full road width, V runs along the road with one repeat per
// roadgen::kTexLen (4) world units. So markings are laid out across U, and a
// dash period is quantized to divide the 4-unit repeat - the image tiles in
// V. The intersection variant is mapped in world space one repeat per 32
// units in both axes, so it carries no markings and tiles in U and V. The
// pavement variant is the same idea on a 2 x 2-unit tile (the pavement strip's
// u = across / 2, v = along / 2).
//
// Alpha is 255 everywhere except, optionally, a ragged outer band of a dirt
// track (U near 0 and 1): StaPip discards alpha-0 texels, so alpha 0 is only
// ever written there, never inside the road.
namespace roadtex {

// Stored by NUMBER in .roadtex recipes: append, never renumber.
enum Surface {
    kAsphalt = 0,
    kCobble = 1,
    kGravel = 2,
    kDirt = 3,
    kSlabs = 4,   // square concrete paving slabs
    kPavers = 5,  // small brick pavers in a running bond
    // kBallast (docs/roads.md "Rails and tram tracks"): a railway's track bed -
    // crushed stone with sleepers painted across it at kSleeperPitch, one row
    // of sleepers per track (`tracks`), centred the way roadrail lays the rails.
    kBallast = 6,
    kSurfaceCount = 7
};
enum Sleepers { kSleepersTimber = 0, kSleepersConcrete = 1 };
// Sleeper layout of the ballast surface, world units at the standard gauge.
// Seven sleepers per 4-unit V repeat (0.571 apart; real track is ~0.6). The
// sleeper length and the track spacing match roadrail's kTrackSpacingRail.
inline constexpr int kSleepersPerRepeat = 7;
inline constexpr float kSleeperLength = 2.6f;
inline constexpr float kBallastTrackSpacing = 4.0f;
inline constexpr float kBallastGauge = 1.435f;

// A painted line's pattern. The two mixed kinds are a solid and a dashed line
// side by side ("may cross from the dashed side"): kSolidDashed has the solid
// line on the LEFT (lower U), kDashedSolid on the right. On the right-hand
// edge line the sides mirror, so "solid" always means the same side relative
// to the road's own edge.
enum LineStyle {
    kLineNone = 0,
    kLineDashed = 1,
    kLineSolid = 2,
    kLineDouble = 3,
    kLineSolidDashed = 4,
    kLineDashedSolid = 5,
    kLineStyleCount = 6
};

struct LinePaint {
    int style = kLineNone;
    float colour[3] = {0.88f, 0.88f, 0.84f};
    float width = 0.12f;  // one line's width, world units (a double = two of these)
    float dash = 2.0f;    // dash length along the road, units
    float gap = 2.0f;     // gap between dashes, units
    bool operator==(const LinePaint& o) const;
};

inline constexpr float kWhite[3] = {0.88f, 0.88f, 0.84f};
inline constexpr float kYellow[3] = {0.93f, 0.70f, 0.13f};

// The pattern actually drawn: dash + gap is snapped so a whole number of
// periods fits the 4-unit texture repeat (the ratio is kept). Returns the
// number of dashes per repeat; *dashOut / *gapOut get the snapped lengths.
int quantizeDash(const LinePaint& l, float* dashOut, float* gapOut);

struct RoadTexParams {
    int surface = kAsphalt;
    int lanes = 2;          // 0 = no lane lines (edge lines still apply), 1..6
    LinePaint centre;       // between the two directions (lanes >= 2)
    LinePaint divider;      // between lanes of one direction (lanes >= 3)
    LinePaint edge;         // near both road edges
    // Weathering, each 0..1; all three at 0 is the clean texture. wear: stains,
    // tone blotches and repairs, wheel tracks, chipped / faded / broken paint
    // with soft edges. grime: the dark rubber strip down each lane, dust toward
    // the edges, gutter darkening at the outer edges. cracks: crack lines and
    // tar-sealed seams.
    float wear = 0.35f;
    float grime = 0.3f;
    float cracks = 0.3f;
    float tint[3] = {1.0f, 1.0f, 1.0f};  // multiplies the surface's base colour
    int seed = 1;
    int size = 128;         // 64 / 128 / 256, square
    float width = 0.0f;     // design road width in units (line placement); 0 = from lanes
    bool raggedEdges = false;   // dirt only: alpha-0 notches along U 0 and 1
    // A dirt-and-gravel VERGE this many units wide along both edges of a
    // directional (not isotropic) surface, with a ragged inner border - a
    // country road's soft shoulders (docs/road-textures.md "Verges"). Painted
    // over everything, edge lines included. 0 = none (and not written).
    float shoulder = 0.0f;
    bool intersection = false;  // isotropic junction patch: no markings, tiles in U too
    bool pavement = false;      // 2 x 2-unit pavement tile: no markings, tiles both ways
    float slabSize = 0.5f;      // slab edge, units (pavers: brick = half x quarter of it)
    float jointWidth = 0.02f;   // joint between slabs / pavers, units

    // No lines are painted (a junction patch or a pavement tile).
    bool isotropic() const { return intersection || pavement; }
    // Ballast only: the sleepers' material and how many tracks the bed carries
    // (1 or 2, kBallastTrackSpacing apart, centred across the width).
    int sleepers = kSleepersTimber;
    int tracks = 1;

    RoadTexParams();
    bool operator==(const RoadTexParams& o) const;
    bool operator!=(const RoadTexParams& o) const { return !(*this == o); }
};

// The road width the texture is laid out for: `width` when set, else
// lanes x 3 + 1.5 (6 for a road without lanes).
float designWidth(const RoadTexParams& p);

// The world units one texture repeat spans: across (U) and along (V).
void tileExtent(const RoadTexParams& p, float* across, float* along);

// The slab / paver grid actually drawn: the size is snapped so a whole number
// of slabs fits each axis of the tile. Columns across U, rows along V.
void slabGrid(const RoadTexParams& p, int* cols, int* rows);

// RGBA8, size x size, row 0 at the top (V = 0).
std::vector<unsigned char> generate(const RoadTexParams& p);

// Where the LEFT edge line is painted, as a fraction of the width (U), the
// whole pattern (a double counts as one): what a road node's painted edge line
// (roadgen::bakeMarkings) lines up with. False when there is none.
bool edgeLineSpan(const RoadTexParams& p, float* u0, float* u1);

// Folder the generated materials live in (project-relative).
inline constexpr const char* kDir = "res/materials/roads";

// A file-name-safe form of a name ("Main Street!" -> "main-street").
std::string fileStem(const std::string& name);

// key=value text (one per line) - the .roadtex sidecar and the CLI's args.
// Line keys are prefixed: centre=double, centre.colour=yellow (or r,g,b),
// centre.width, centre.dash, centre.gap; the same for divider. and edge.
std::string toText(const RoadTexParams& p);
// Applies one key=value; false (and *err) for an unknown key or bad value.
bool applyKey(RoadTexParams& p, const std::string& key, const std::string& value,
              std::string* err);
// Parses a whole toText() body; unknown keys are ignored (forward compat).
RoadTexParams fromText(const std::string& text);

// Writes <kDir>/<stem>.png, a one-material <stem>.mtl pointing at it and the
// editor-only <stem>.roadtex recipe, each only when its bytes changed. Returns
// the .mtl's project-relative path, or "" with *err set.
std::string writeAssets(const std::string& projectDir, const std::string& name,
                        const RoadTexParams& p, std::string* err);

// Reads <projectDir>/<kDir>/<stem>.roadtex; false when there is none.
bool readRecipe(const std::string& projectDir, const std::string& name, RoadTexParams* out);

// Worn road paint (docs/roads.md "Markings"): the texture a node's painted
// lines are drawn with - near-white with grime in it, and an alpha that the
// wear eats away in patches and chips, so the paint blends into the asphalt
// under it instead of lying on it as a flat, clean colour. Tiles every
// kPaintExtent units in world X/Z (the paint's UVs are its world position).
inline constexpr const char* kPaintStem = "road-paint";
inline constexpr float kPaintExtent = 4.0f;
std::vector<unsigned char> paintWear(int size = 64, unsigned seed = 1);
// Writes res/materials/roads/road-paint.png + .mtl when missing or different
// (generated, deterministic - no recipe), and returns the .mtl's project
// path; "" when it cannot be written.
std::string ensurePaintTexture(const std::string& projectDir);

// The ready materials a new project is seeded with (road-2lane, road-4lane,
// road-dirt, road-cobble, road-junction, pavement-slabs,
// rail-ballast, rail-ballast-double, rail-junction).
struct Preset {
    const char* name;
    RoadTexParams params;
};
std::vector<Preset> presets();
inline constexpr const char* kDefaultSurface = "res/materials/roads/road-2lane.mtl";
inline constexpr const char* kDefaultJunction = "res/materials/roads/road-junction.mtl";
// What a road switched to Kind = Rail picks (props_ui), by track count.
inline constexpr const char* kDefaultBallast = "res/materials/roads/rail-ballast.mtl";
inline constexpr const char* kDefaultBallastDouble =
    "res/materials/roads/rail-ballast-double.mtl";
inline constexpr const char* kDefaultRailJunction = "res/materials/roads/rail-junction.mtl";

// Writes every preset into a project. "" on success.
std::string seedProject(const std::string& projectDir);

}  // namespace roadtex
