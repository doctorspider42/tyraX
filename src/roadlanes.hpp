#pragma once

#include <math.h>
#include <array>
#include <string>
#include <vector>

#include "roadfurniture.hpp"
#include "roadgen.hpp"

struct Project;

// Road traffic (docs/traffic.md): the LANE GRAPH, baked on the host from the
// road network, and everything that turns it into living traffic.
//
// Host-only - no GL, no ImGui - the roadgen/roadfurn shape. One build()
// serves every consumer: the codegen (compact console tables), the editor's
// View > Lanes overlay, the --road-lanes CLI and --vehicle-check "road
// traffic". The runtime half - where a car is on its path, the signal cycle,
// junction admission, the pedals - is src/traffic_core.inl, compiled here for
// the host simulation and pasted verbatim into the generated game.
namespace roadlanes {

// The core, compiled for the host (the generated game pastes the same text
// into its TerrainGame). See src/traffic_core.inl.
#include "traffic_core.inl"

struct Options {
    bool leftHand = false;   // left-hand traffic: lanes on the left of travel
    float speed = 11.0f;     // the lanes' speed, units/s
};

// One lane: a road's direction of travel between two nodes (or a node and an
// open end), offset from the centre line onto its side of the road.
struct Lane {
    int road = -1;          // CrossingRoad index
    int dir = 1;            // +1 along the road's points, -1 against them
    int index = 0;          // 0 = the kerb-side lane, upward toward the centre
    // The lanes beside it in the same direction of the same stretch: toward
    // the centre (index + 1) and toward the kerb (index - 1), -1 for none.
    int inner = -1, outer = -1;
    int perDir = 1;         // lanes this direction has
    float offset = 0.0f;    // distance right (or left) of the centre line
    int fromNode = -1, fromArm = -1;  // crossing index + arm it leaves, -1 = an open end
    int toNode = -1, toArm = -1;      // crossing index + arm it reaches, -1 = an open end
    std::vector<float> pts;           // x, y, z
};

enum Turn : int { kStraight = 0, kNearSide = 1, kFarSide = 2, kBack = 3 };

// A connection: from the end of one lane to the start of another, through a
// node (a Bezier inside its patch), across an end-to-end join of two roads, or
// back round at a dead end.
struct Connection {
    int node = -1;          // crossing index; -1 = a join or a turn back
    int from = -1, to = -1; // lane indices
    int arm = -1;           // the node arm `from` arrives on
    int turn = kStraight;
    bool givesWay = false;  // `from` arrives on an arm that gives way (a stop line)
    int rank = 0;           // priority: 2 major / 0 giving way, +1 not a far-side turn
    int group = -1;         // signalled node: the approach's phase (roadfurn::signalPhase)
    std::vector<float> pts; // x, y, z
    std::vector<int> conflicts;  // connections whose paths this one crosses
};

struct NodeInfo {
    int crossing = -1;
    float x = 0.0f, z = 0.0f;
    bool signalled = false;
    int phases = 0;         // signalled: phases in its cycle (roadfurn::signalPhases)
    int arms = 0;
};

struct Graph {
    std::vector<Lane> lanes;
    std::vector<Connection> conns;
    std::vector<NodeInfo> nodes;          // one per plan crossing (index = crossing)
    std::vector<std::vector<int>> laneOut;  // per lane: the connections leaving its end
    int deadEnds = 0;
    std::vector<std::string> warnings;    // dead ends, nodes with no legal exit, ...
};

// The lanes every road (not a railway) carries per direction: the recipe's
// lane count halved when the road's generated texture says, else one lane per
// direction for every 7 units of width.
int lanesPerDirection(const roadgen::CrossingRoad& r);

// The graph of one scene's roads. `signalled` is per plan crossing
// (roadfurn::nodeSignalled); `ground` is the bare terrain height (lanes follow
// the drawn surface over it: rank lift, road lift, a bridge's elevation).
Graph build(const std::vector<roadgen::CrossingRoad>& roads, const roadgen::CrossingPlan& plan,
            const std::vector<bool>& signalled, const roadgen::HeightFn& ground,
            const Options& opt);

// The project's own call: one scene's roads, plan and signals, as the codegen
// and the editor see them.
Graph buildScene(const Project& p, int scene);

// The runtime form (the core's TfGraph): lanes first, then connections, the
// signal timing from the project. `seed` scatters the nodes' cycle offsets.
void toSim(const Graph& g, const Options& opt, float green, float amber, float allRed,
           TfGraph& out);

// --road-lanes: lanes and connections per node, then the warnings.
std::string describe(const Graph& g, const std::vector<roadgen::CrossingRoad>& roads);

// --- codegen ------------------------------------------------------------------
// Traffic is on for a project (ProjectSettings::traffic.cars > 0, roads and a
// vehicle definition with a model). Off = not one byte of this reaches the
// generated sources.
bool projectHasTraffic(const Project& p);
// The project with its ambient cars appended to every scene that has lanes:
// N Vehicle objects per scene, hidden until the runtime spawns them. The
// codegen generates FROM this copy, so the cars get every table a placed
// vehicle gets. Returns false (and leaves `out` alone) when traffic is off.
bool withTrafficCars(const Project& p, Project& out);
// The object name prefix the appended cars carry (live link skips them).
inline constexpr const char* kCarPrefix = "~traffic-";
// The console tables (TRAFFIC_*), accumulated scene by scene by the codegen's
// road pass - it already holds each scene's roads, plan and furniture.
struct Tables {
    std::vector<std::array<int, 6>> scenes;  // segFirst, segCount, nodeFirst, nodeCount, lampFirst, lampCount
    std::vector<int> segs;                   // 13 ints per segment (TrafficSegData)
    std::vector<float> pts;
    std::vector<int> next, conf, nodeSignal;
    std::vector<float> lamps;                // node, group, x, y, z, fx, fz, scale
    std::string notes;
    int lanes = 0, conns = 0, warnings = 0;
    void addScene(int scene, const Graph& g, const Options& opt,
                  const std::vector<roadfurn::Instance>& furniture);
    std::string source(const Project& p, int sceneCount) const;
};
// The core's text, indented for the class body.
std::string coreSource();
// The class members and the TerrainGame functions. `streamed`: the project
// streams its roads, so a car is never placed where they are not built.
std::string membersSource();
std::string implSource(bool streamed);
// Splices the hooks into the vehicle runtime template text. Silent where an
// anchor is absent (every other template); the check proves all six land.
std::string patchTemplate(std::string s);
extern const char* const kHookMarks[6];

// --vehicle-check "road traffic".
void check(void (*verdict)(bool, const char*));

}  // namespace roadlanes
