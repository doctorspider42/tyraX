#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Procedural content graph (Houdini/PCG-style dataflow) - the AUTHORING half
// of docs/procedural-generation.md. A graph lives on a Scatter scene object
// (SceneObject::procGraph): the object's transform is the volume the graph
// works in, so moving/scaling the region is the ordinary gizmo.
//
// This header is the DATA MODEL and the node-type registry only - no
// evaluation (procgen.cpp), no baking (procbake.cpp), no UI. The split is the
// same one flowgraph.hpp / templates.cpp use, and it keeps the model free of
// GL/Project dependencies so the evaluator can be exercised from a host
// harness.
//
// Unlike a flow graph there is no execution: every link carries DATA
// (a point cloud, a 2D mask, a curve) from an output pin to an input pin, and
// the graph is pulled from its Output node. Nothing about it reaches the PS2 -
// the graph is evaluated in the editor and BAKED to ordinary static geometry
// (procbake), so the console only ever loads a finished mesh.

// What flows along a link. A pin only accepts a link of its own type; the
// editor refuses the rest with a message (see procgraph::linkError).
enum class ProcType {
    Points = 0,  // point cloud: transforms + named per-point attributes
    Mask = 1,    // 2D float field over the volume's XZ footprint (0..1)
    Curve = 2,   // polyline / Catmull-Rom control points in world space
};

// Per-point attribute names the built-in nodes produce and consume. Attributes
// are the extension mechanism of the whole system (GRAF-02): a node that needs
// to hand information downstream writes an attribute instead of growing a new
// data type, and a node that does not know an attribute simply passes it
// through. These are the well-known ones; Set Attribute can write any name.
namespace procattr {
inline constexpr const char* kSlope = "slope";    // degrees off horizontal
inline constexpr const char* kHeight = "height";  // world Y of the point
inline constexpr const char* kNormalX = "nx";
inline constexpr const char* kNormalY = "ny";
inline constexpr const char* kNormalZ = "nz";
inline constexpr const char* kMask = "mask";      // last sampled mask value
inline constexpr const char* kSize = "size";      // uniform scale factor
inline constexpr const char* kRandom = "random";  // per-point 0..1, stable
inline constexpr const char* kCurveT = "t";       // 0..1 along a curve
inline constexpr const char* kDist = "dist";      // distance to something
// Blocks Fill writes these two. `faces` is a 6-bit visibility mask (+X 1, -X 2,
// +Y 4, -Y 8, +Z 16, -Z 32): the merger drops a triangle whose outward normal
// points at a face this block does not have, which is what stops a block world
// from spending its entire triangle budget on surfaces nobody can see. It is
// deliberately a plain attribute rather than a property of a "block" data type:
// any asset merged at any point in the graph honours it, and a node that does
// not know about it passes it through like every other attribute.
inline constexpr const char* kFaces = "faces";
inline constexpr const char* kDepth = "depth";  // blocks below the column top
}  // namespace procattr

// One row of a node's variable-length table: the asset pool of Pick Asset
// (s = asset, v = weight/scale range) and the control points of Curve
// (v[0..2] = world XYZ). One generic list instead of a per-node container -
// the same trade the SceneObject type-specific fields make.
struct ProcRow {
    std::string s;
    float v[4] = {0, 0, 0, 0};
};

inline bool operator==(const ProcRow& a, const ProcRow& b) {
    return a.s == b.s && a.v[0] == b.v[0] && a.v[1] == b.v[1] &&
           a.v[2] == b.v[2] && a.v[3] == b.v[3];
}

struct ProcNode {
    int id = 0;             // unique within the graph, never reused
    std::string type;       // key into procNodeTypes()
    float pos[2] = {0, 0};  // node-editor position
    // Parameters by key (defaults come from the type registry, so a node only
    // stores what was actually touched - and a new parameter on an existing
    // node type reads as its default in old projects).
    std::map<std::string, float> nums;
    std::map<std::string, std::string> strs;
    std::vector<ProcRow> rows;
    bool bypass = false;  // pass the first input through untouched (A/B a step)
};

struct ProcLink {
    int id = 0;
    int fromNode = 0;
    int fromPin = 0;  // index into the source type's outs
    int toNode = 0;
    int toPin = 0;  // index into the target type's ins
};

// A manual edit of ONE generated instance that survives re-evaluation
// (FILT-05 - the task procedural tools usually lose). Bound to the point's
// stable key, never to its index: the key is derived from the generator node
// and the point's ordinal in a fixed sample sequence, so changing density
// upstream keeps every surviving point's identity.
struct ProcOverride {
    uint64_t key = 0;
    bool removed = false;      // deleted instance
    float offset[3] = {0, 0, 0};  // added to the generated position
    float rotate[3] = {0, 0, 0};  // added to the generated rotation (degrees)
    float scale = 1.0f;           // multiplies the generated scale
    int asset = -1;                // >= 0 replaces the picked asset index
};

inline bool operator==(const ProcOverride& a, const ProcOverride& b) {
    return a.key == b.key && a.removed == b.removed &&
           a.offset[0] == b.offset[0] && a.offset[1] == b.offset[1] &&
           a.offset[2] == b.offset[2] && a.rotate[0] == b.rotate[0] &&
           a.rotate[1] == b.rotate[1] && a.rotate[2] == b.rotate[2] &&
           a.scale == b.scale && a.asset == b.asset;
}

struct ProcGraph {
    std::vector<ProcNode> nodes;
    std::vector<ProcLink> links;
    int nextId = 1;
    // Global seed: the one knob that reshuffles everything (GRAF-04). Every
    // per-point random stream is derived from (seed, node id, point key,
    // channel), so nothing depends on evaluation order or on how many
    // unrelated nodes sit next to it.
    uint32_t seed = 1;
    // WHERE the graph runs (docs/procedural-runtime.md). false = the classic
    // bake: the editor evaluates the graph and writes finished chunk meshes, the
    // console never learns a graph existed. true = the graph is COMPILED into
    // the game and evaluated on the EE, so the world can differ every boot -
    // which costs load time, RAM and a much smaller node vocabulary
    // (procrt::capability is the honest list). A graph is one or the other:
    // baking a runtime volume would defeat the point, and running a baked one
    // would need nodes the console has no data for.
    bool runtime = false;
    // Generate during the scene load, inside the loading screen's progress
    // pump. Off = nothing appears until a Generate Volume flow node fires,
    // which is how you regenerate on a button or stage a world in pieces.
    bool runAtStart = true;
    // 0 = use `seed` (the same world every boot - reproducible, and what you
    // want while authoring); 1 = roll a fresh one at generation time.
    int seedMode = 0;
    std::vector<ProcOverride> overrides;
    // Hash of the last bake (procbake::graphHash) - what the generated chunk
    // objects in the scene were built from. Differs from the live hash = the
    // bake is stale; the build re-bakes before generating anything.
    uint64_t bakedHash = 0;

    bool empty() const { return nodes.empty(); }
};

inline bool operator==(const ProcNode& a, const ProcNode& b) {
    return a.id == b.id && a.type == b.type && a.pos[0] == b.pos[0] &&
           a.pos[1] == b.pos[1] && a.nums == b.nums && a.strs == b.strs &&
           a.rows == b.rows && a.bypass == b.bypass;
}

inline bool operator==(const ProcLink& a, const ProcLink& b) {
    return a.id == b.id && a.fromNode == b.fromNode && a.fromPin == b.fromPin &&
           a.toNode == b.toNode && a.toPin == b.toPin;
}

inline bool operator==(const ProcGraph& a, const ProcGraph& b) {
    return a.nextId == b.nextId && a.seed == b.seed && a.runtime == b.runtime &&
           a.runAtStart == b.runAtStart && a.seedMode == b.seedMode &&
           a.nodes == b.nodes && a.links == b.links &&
           a.overrides == b.overrides && a.bakedHash == b.bakedHash;
}

// ---------------------------------------------------------------------------
// Node type registry
// ---------------------------------------------------------------------------

enum class ProcParamKind {
    Float = 0,
    Int,
    Bool,
    Enum,        // choices, "|"-separated in ProcParamDef::choices
    ObjectName,  // scene object name (empty = the terrain / the volume)
    Attr,        // per-point attribute name (see procattr)
    Text,
    // A terrain material: -1 = the terrain's base material, 0..N-1 = a painted
    // layer (docs/terrain-painting.md). Stored as that index, picked by NAME -
    // an index typed into a box is unreadable the moment a scene has more than
    // one layer, and reordering the stack would silently retarget it anyway.
    TerrainLayer,
};

struct ProcParamDef {
    const char* key = "";
    const char* label = "";
    ProcParamKind kind = ProcParamKind::Float;
    float def = 0.0f;
    float lo = 0.0f;
    float hi = 1.0f;
    const char* choices = "";  // Enum only
    // ObjectName only: what "no object chosen" means for THIS parameter. It
    // differs per node (the terrain, the volume's own centre, nothing at all),
    // and a combo that says "(terrain)" on a node where empty means something
    // else is a small lie in the most-read place there is.
    const char* emptyLabel = "(terrain)";
    const char* tip = "";
};

struct ProcPinDef {
    const char* label = "";
    ProcType type = ProcType::Points;
    bool optional = false;  // an unconnected required input is a graph error
};

// What a node's `rows` table means (the UI draws the matching editor).
enum class ProcRowKind {
    None = 0,
    Assets,    // s = asset key, v[0] = weight, v[1..2] = scale min/max
    Points,    // v[0..2] = world XYZ control point
    Settings,  // s = procObjectProps() key, v[0] = the value
    Prefabs,   // s = prefab name, v[0] = weight, v[1..2] = scale min/max
};

// ---------------------------------------------------------------------------
// Object settings: the "apply this to everything the graph generates" list
// ---------------------------------------------------------------------------
// A bake rebuilds its chunk objects on every run, so editing one in the
// Properties panel is not a workflow - the next density change makes new
// chunks that know nothing about it. The Object Settings node is where a
// property is stated ONCE for the whole volume, and procbake applies it to
// every generated chunk.
//
// This list is the single source of truth for that node (UI rows, the bake and
// the docs all read it). It is APPEND-ONLY in spirit: a row stores its property
// by KEY, so removing an entry silently drops what a project asked for.
// Deliberately absent are the four properties the Output node already owns
// (draw distance, cast shadow, collision, layer) - two places to set one field
// is how they end up disagreeing.
enum class ProcObjPropKind { Float = 0, Bool };

struct ProcObjProp {
    const char* key = "";  // stored in ProcRow::s
    const char* label = "";
    ProcObjPropKind kind = ProcObjPropKind::Float;
    float def = 0.0f;
    float lo = 0.0f;
    float hi = 1.0f;
    const char* tip = "";
};

const std::vector<ProcObjProp>& procObjectProps();
const ProcObjProp* procObjectProp(const std::string& key);

struct ProcNodeType {
    const char* key = "";
    const char* title = "";
    const char* category = "";  // add-menu submenu
    std::vector<ProcPinDef> ins;
    std::vector<ProcPinDef> outs;
    std::vector<ProcParamDef> params;
    ProcRowKind rows = ProcRowKind::None;
    // One-paragraph behavior description - THE documentation of the node, the
    // same convention flow nodes follow: the add-menu tooltip, the node hover
    // and the generated reference all read it.
    const char* desc = "";
};

// The registry. One entry per node type; omitted fields keep their defaults.
const std::vector<ProcNodeType>& procNodeTypes();

// Type by key, or nullptr for an unknown/retired type.
const ProcNodeType* procNodeType(const std::string& key);

namespace procgraph {

// Default parameter values of a type, applied to a freshly added node.
void applyDefaults(ProcNode& n);

// Parameter access with the type registry's default as the fallback, so a
// node never has to store an untouched parameter.
float num(const ProcNode& n, const char* key);
int inum(const ProcNode& n, const char* key);
bool flag(const ProcNode& n, const char* key);
const std::string& str(const ProcNode& n, const char* key);

// The link feeding an input pin (nullptr = unconnected). One link per input;
// connecting a second one replaces the first (the editor does that).
const ProcLink* linkTo(const ProcGraph& g, int nodeId, int pin);
const ProcNode* node(const ProcGraph& g, int id);
ProcNode* node(ProcGraph& g, int id);

// Why a proposed link is invalid ("" = it is valid): unknown pins, type
// mismatch, or a cycle. Checked before the link is created, so a graph in the
// file is always acyclic and type-correct (GRAF-01).
std::string linkError(const ProcGraph& g, int fromNode, int fromPin, int toNode,
                      int toPin);

// Everything wrong with the graph as it stands, in reading order: unknown node
// types, missing required inputs, a missing/duplicated Output. Shown in the
// editor's problem list; an empty vector means the graph evaluates.
struct ProcIssue {
    int nodeId = 0;  // 0 = graph-wide
    std::string text;
};
std::vector<ProcIssue> validate(const ProcGraph& g);

// The graph's terminal node (the first Output), or nullptr.
const ProcNode* outputNode(const ProcGraph& g);

// The graph's Object Settings node (the first one), or nullptr. Found by type
// rather than by following links on purpose: the node states a fact about the
// whole bake, so it carries no pins and sits beside the chain, not in it.
const ProcNode* settingsNode(const ProcGraph& g);

// Adds a node of `type` at the given editor position and returns its id
// (0 for an unknown type). Parameters are seeded from the registry.
int addNode(ProcGraph& g, const std::string& type, float x, float y);

// Removes a node and every link touching it.
void removeNode(ProcGraph& g, int nodeId);

// Connects two pins, replacing whatever fed the target input. Returns false
// (and changes nothing) when linkError rejects the pair.
bool addLink(ProcGraph& g, int fromNode, int fromPin, int toNode, int toPin);

// A ready-made starter graph: scatter on the terrain, thin by slope, pick from
// an asset pool, vary the transform, output. What a new Scatter object gets,
// so the tool is never a blank canvas.
ProcGraph starterGraph();

}  // namespace procgraph
