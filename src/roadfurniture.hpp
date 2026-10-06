#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "roadgen.hpp"

namespace json {
struct Value;
}

// Street furniture (docs/roads.md "Street furniture", format v101): street
// lamps, trees and bollards in lines along a road's pavement (or its edge
// when it has none), plus a give-way / stop sign at each stop line and
// optional traffic lights at four-way nodes - all GENERATED from the road at
// build, never saved as scene objects.
//
// Host-only (no GL, no ImGui, no project.hpp), the roadgen/roaddetail shape
// and for the same reason: the codegen bakes the instances into merged
// vertex-colour chunks (ROAD_FURN_VERTS) for the console, the viewport draws
// the same triangles, and --vehicle-check proves their properties - three
// consumers of ONE function. The PS2 does no furniture work beyond uploading
// the chunks and their collision boxes at scene load.
//
// Every instance is a pure function of its road's points, width, settings
// and stable id (counter-based hashes, never a running RNG), so an unchanged
// project bakes the same furniture byte for byte and editing one road never
// reshuffles another's.
namespace roadfurn {

enum Kind : int {
    kLamp = 0,
    kTree = 1,
    kBollard = 2,
    kSign = 3,     // give-way / stop sign at a stop line
    kSignal = 4,   // traffic light at a signalled node (nodeSignalled)
    kKindCount = 5
};
const char* kindName(int kind);

enum Side : int {
    kBoth = 0,
    kLeft = 1,       // left of the direction the road's points run
    kRight = 2,
    kAlternate = 3,  // left, right, left, ... station by station
};

enum SignKind : int {
    kSignNone = 0,
    kSignGiveWay = 1,
    kSignStop = 2,
};

// One line of furniture along a road.
struct Line {
    // A project .obj (res/models/...; a .tmdl names its sibling .obj).
    // "" = the built-in model for the line's kind (box-and-cylinder geometry,
    // a few dozen triangles). A model's +Z faces the road (lamps) - `yaw`
    // turns it when the asset was authored another way.
    std::string model;
    float spacing = 0.0f;  // units between stations along the road, 0 = off
    int side = kBoth;
    float offset = 0.6f;   // from the road edge (the kerb face) outward
    float phase = 0.0f;    // first station this far along (+ half a spacing)
    float scale = 1.0f;    // model scale (built-in models are 1:1 metres)
    float yaw = 0.0f;      // degrees added to the facing
    bool operator==(const Line&) const = default;
};

// Breakable furniture (docs/roads.md "Breakable furniture", format v109): a
// car that hits one of this kind at `speed` units/s or faster knocks it over
// - the prop snaps off as debris, its collision box goes, and the car keeps
// (1 - loss) of its speed instead of stopping dead. Slower, it is a wall, as
// it always was.
struct Break {
    bool on = true;      // this kind breaks (when the road's Breakable is on)
    float speed = 9.0f;  // units/s - the threshold
    float loss = 0.15f;  // share of the car's speed the hit takes
    bool operator==(const Break&) const = default;
};
// The per-kind defaults: lamps, signs, signals and bollards break, trees do
// not (a tree is what stops a car in every racer of the era). Thresholds in
// units/s (9 = 32 km/h at 1 unit = 1 m).
inline Break defaultBreak(int kind) {
    switch (kind) {
    case kTree: return {false, 14.0f, 0.45f};
    case kBollard: return {true, 7.0f, 0.10f};
    case kSign: return {true, 6.0f, 0.08f};
    case kSignal: return {true, 9.0f, 0.18f};
    default: return {true, 9.0f, 0.15f};  // lamp
    }
}

// A road's furniture settings (SceneObject::roadFurniture).
struct Settings {
    Line lamps{"", 0.0f, kBoth, 0.6f, 0.0f, 1.0f, 0.0f};
    Line trees{"", 0.0f, kBoth, 1.6f, 0.0f, 1.0f, 0.0f};
    Line bollards{"", 0.0f, kBoth, 0.4f, 0.0f, 1.0f, 0.0f};
    int seed = 0;               // tree yaw / size jitter, another arrangement
    int signs = kSignNone;      // a sign at each stop line this road gives way at
    bool signals = false;       // traffic lights at this road's three- and four-way nodes
    std::string signModel;      // "" = built-in (the sign kind picks it)
    std::string signalModel;    // "" = built-in
    // Breakable furniture (format v109): the road's switch, one Break per
    // kind (lamps, trees, bollards, signs, signals) and the hit's sound - a
    // project .wav, "" = the project sound whose name says break / crash /
    // impact / hit / smash / clank / thud / knock, or none.
    bool breakable = false;
    Break brk[kKindCount] = {defaultBreak(kLamp), defaultBreak(kTree), defaultBreak(kBollard),
                             defaultBreak(kSign), defaultBreak(kSignal)};
    std::string breakSound;
    bool operator==(const Settings&) const = default;
    const Line& line(int kind) const { return kind == kTree ? trees : kind == kBollard ? bollards : lamps; }
    Line& line(int kind) { return kind == kTree ? trees : kind == kBollard ? bollards : lamps; }
};
// The Break that applies to an instance of `kind` placed by road `s`, or a
// Break with on = false when the road is not breakable.
Break breakOf(const Settings& s, int kind);
// Any road in the list breakable with at least one kind on (the codegen gate,
// together with "the project has vehicles").
bool anyBreakable(const std::vector<Settings>& settings);

bool isDefault(const Settings& s);
// The "roadFurniture" object value (only its non-default keys), "" when the
// settings are all default - the caller writes `, "roadFurniture": <this>`.
std::string toJson(const Settings& s);
void fromJson(const json::Value& v, Settings& out);
// FNV over every field (0 for the defaults): the viewport signature and the
// Live Link recipe hash mix it.
uint64_t signature(const Settings& s);
// Every model path the settings name (the asset browser's usage notes and
// rename swaps go through these).
std::vector<std::string*> modelPaths(Settings& s);
std::vector<const std::string*> modelPaths(const Settings& s);

// --- placement ---------------------------------------------------------------

// The console's draw distance (from the chunk centre) - sooner than the
// district's buildings (145), later than the kerbs (60): a lamp post is tall.
inline constexpr float kDrawDistance = 80.0f;
// Chunk grid (cull granularity) and budget.
inline constexpr float kCell = 48.0f;
inline constexpr int kChunkBudget = 1800;
// Nothing that is not this instance's own pavement - a road, a junction patch,
// node paint, another instance - may come nearer than this to its footprint.
inline constexpr float kClearance = 0.3f;
// Trees and bollards also keep this far from a node patch (sight lines).
// Lamps may stand at a corner: only kClearance applies to them.
inline constexpr float kNodeGap = 2.0f;

struct Instance {
    int road = -1;      // CrossingRoad index
    int kind = kLamp;
    int node = -1;      // crossing index (signs, signals), else -1
    int arm = -1;       // ... and the arm of it whose approach it faces
    int variant = 0;    // a sign's SignKind
    float x = 0, y = 0, z = 0;  // the base, on the pavement or the ground
    float fx = 0, fz = 1;       // unit facing (the model's +Z)
    float scale = 1.0f;
    float radius = 0.2f;        // footprint (placement and collision)
    float height = 1.0f;        // collision height above the base
    int firstVertex = 0, vertexCount = 0;  // its run of Result::tris
    // Breakable furniture: the threshold (units/s, 0 = never breaks) and the
    // share of the car's speed it keeps (1 - Break::loss).
    float breakSpeed = 0.0f;
    float breakKeep = 1.0f;
};

struct Vertex {
    float x, y, z;
    float r, g, b;  // baked shade x colour, 0..1 (1 = the full colour)
};

// A solid box per instance (the pole or the trunk), world AABB.
struct Box {
    float mn[3], mx[3];
};

// One scene's roads as the codegen sees them. `settings` is parallel to
// `roads`. `patches` and `paint` are the drawn junction patches and node
// markings (triangle lists), `pavements` the planPavements output (furniture
// stands on its top), `ground` the bare terrain.
struct SceneInput {
    const std::vector<roadgen::CrossingRoad>* roads = nullptr;
    const std::vector<Settings>* settings = nullptr;
    const roadgen::CrossingPlan* plan = nullptr;
    roadgen::HeightFn ground;
    std::vector<roadgen::Vertex> patches;
    std::vector<roadgen::Vertex> paint;
    std::vector<roadgen::PavementMesh> pavements;
    std::string projectDir;  // resolves model paths ("" = built-in models only)
    // The project runs traffic (docs/traffic.md): the built-in signal's three
    // lenses are baked UNLIT, because the console lights the current one.
    bool liveSignals = false;
};

struct Result {
    std::vector<Instance> instances;
    // Triangle LIST of every instance, grouped into kCell chunks of whole
    // instances; chunkSizes sums to tris.size(). Untextured: an .obj model's
    // texture is SAMPLED into the vertex colours, so every chunk is one
    // vertex-colour bag and the furniture costs no VRAM.
    std::vector<Vertex> tris;
    std::vector<int> chunkSizes;
    std::vector<Box> boxes;
    int perKind[kKindCount] = {};
    int candidates = 0;           // stations tried
    int rejectedRoad = 0;         // on (or too near) a road's carriageway
    int rejectedNode = 0;         // on / too near a junction patch or its paint
    int rejectedOverlap = 0;      // on top of an earlier instance
    std::vector<std::string> warnings;  // unreadable models, ...
};

// True when any road asks for any furniture (the zero-cost gate).
bool any(const std::vector<Settings>& settings);
Result build(const SceneInput& in);
// The codegen's own inputs rebuilt from the roads alone (the drawn roads,
// the patches fitted to `grid`, their paint, the kerbs and pavements) - for
// a consumer that has nothing baked yet (--road-crossings, the check).
SceneInput prepare(const std::vector<roadgen::CrossingRoad>& roads,
                   const std::vector<Settings>& settings, const roadgen::CrossingPlan& plan,
                   const roadgen::HeightFn& ground, const roadgen::TerrainGrid& grid,
                   const std::string& projectDir);

// The built-in model of a kind (local space, +Y up, +Z facing, base at 0),
// a triangle list with one colour per vertex - exposed for the check.
struct ModelTri {
    float p[3][3];
    float c[3][3];
    bool lit = true;  // false = emissive (a lamp lens): no shading
};
std::vector<ModelTri> builtinModel(int kind, int signKind = kSignGiveWay,
                                   bool liveSignals = false);

// The built-in signal head's three lenses, top to bottom red / amber / green,
// in its model space at scale 1: lens centre heights, the depth of the lens
// face (+Z faces the approaching driver) and the half size. The traffic
// runtime draws the lit lens there (docs/traffic.md "Traffic lights").
inline constexpr float kSignalLensY[3] = {3.70f, 3.40f, 3.10f};
inline constexpr float kSignalLensZ = 0.115f;
inline constexpr float kSignalLensHalf = 0.10f;

// Does node `c` carry traffic lights: a patch node (not a transition, not one
// a railway crosses) of three or four arms where any of its roads asks for
// signals - or of three arms or more whose junction override says Signals
// (roadgen::kControlSignals; only in a scene that builds street furniture, so
// the heads always stand where the lights work). An override of None or Stop
// signs turns them off. The furniture's rule, shared with the lane graph's
// phase cycle.
bool nodeSignalled(const roadgen::Crossing& c, const std::vector<roadgen::CrossingRoad>& roads,
                   const std::vector<Settings>& sets);
// A signalled node's phases: a four-way node runs two (opposite arms share
// one), every other node one per arm (a T runs three). `arm` is an index into
// the node's armList - the signal head on that arm shows that phase.
inline int signalPhases(int arms) { return arms == 4 ? 2 : arms; }
inline int signalPhase(int arms, int arm) { return arms == 4 ? arm % 2 : arm; }

// --- the console tables -----------------------------------------------------------
//
// Accumulates every scene's Result into the generated tables: ROAD_FURN (one
// {scene, first, count} row per chunk), ROAD_FURN_VERTS (x y z), ROAD_FURN_RGB
// (0xRRGGBB per vertex) and ROAD_FURN_BOXES (scene + world AABB per instance).
struct Tables {
    struct Row {
        int scene, first, count;
        int light = 0;  // 1 = a street lamp POOL row (docs/weather.md)
    };
    std::vector<Row> rows;
    std::vector<float> verts;        // x, y, z
    std::vector<uint32_t> rgb;       // per vertex (a pool row: its packed UV)
    std::vector<float> boxes;        // scene, min xyz, max xyz
    std::string notes;               // "// scene N: ..." lines
    // Lit street lamps (docs/weather.md): the rows carry a `light` column, and
    // a light row's colour word is its texture coordinate (roadlight::packUv).
    // False = the tables print exactly as they did before lamps lit.
    bool lit = false;
    // Breakable furniture (docs/roads.md "Breakable furniture"): one
    // ROAD_FURN_PIECES row per instance (= per box, the same index), saying
    // where its vertices are in which ROAD_FURN row, where its lamp's pool
    // is, its ROAD_LAMPS row, its sound and its threshold. False = none of it
    // is printed (every other project keeps its exact tables).
    bool breakable = false;
    struct Piece {
        int row = -1, first = 0, count = 0;  // ROAD_FURN row, first vertex in it, count
        int kind = kLamp;
        int lamp = -1;                       // ROAD_LAMPS row, -1 = none
        int poolRow = -1, poolFirst = 0, poolCount = 0;
        int snd = -1;                        // SND_PATHS index, -1 = silent
        float speed = 0.0f, keep = 1.0f;     // Instance::breakSpeed / breakKeep
    };
    std::vector<Piece> pieces;
    // `breakSnd`: the sound index per road of the scene (CrossingRoad order),
    // empty = none.
    void add(int scene, const Result& r, const std::vector<int>& breakSnd = {});
    // One scene's lamp pools (roadlight::bakePools): x y z per vertex, the
    // packed UV in the colour word, whole chunks. With `breakable`, also
    // each pool's vertex range (`poolFirst`/`poolCount` per lamp, indices into
    // xyz / 3; `lampInstance` the furniture instance of the scene each lamp
    // is, from the LAST add()) and the ROAD_LAMPS row of the first lamp.
    void addLight(int scene, const std::vector<float>& xyz, const std::vector<uint32_t>& uv,
                  const std::vector<int>& chunkSizes, const std::string& note,
                  const std::vector<int>& lampInstance = {}, const std::vector<int>& poolFirst = {},
                  const std::vector<int>& poolCount = {}, int lampBase = 0);
    // Which row holds global vertex `v` (-1 = none), for the piece ranges.
    int rowOf(int v) const;
    int lastBoxBase = 0;  // the first box (= piece) of the last add()
    // embedVerts false (docs/roads.md "Tables on disk"): the rows, boxes and
    // counts only - the vertices and colours are in bin/roadfile/roads.bin.
    std::string source(bool embedVerts = true) const;
};
// The block spliced into the generated buildRoads before procFinishChunks:
// owner -7 chunks (renderProcChunks draws them: frustum reject, the chunk
// draw distance, occlusion) and owner -7 procColliders.
// `lit`: the pool rows' variant (Tables::lit) - the same text plus the light
// column's lines, so a project without lamps keeps its exact source.
std::string uploadSource(bool lit = false);

// --vehicle-check "road furniture".
void check(void (*verdict)(bool, const char*));

}  // namespace roadfurn
