#pragma once

#include <string>
#include <vector>

#include "roadgen.hpp"

struct SceneObject;

// Bridges and overpasses (docs/roads.md "Bridges"). Host-only - no GL, no
// ImGui - and HOST-BAKED like the junction patches and the kerbs: a bridge
// road is not handed to the console's buildRoads at all. The codegen emits
// its deck as ROAD_JUNCTIONS rows (textured, owner -3, so the road height
// index and every wheel read it) and its structure - parapets, deck edges,
// underside, piers, abutments - as ROAD_BRIDGE rows (vertex-coloured, owner
// -5). The EE tessellates nothing of it, so there is no EE twin to keep in
// step; the viewport, the test drive, the shadow decals and the codegen all
// read the functions below.
//
// THE PROFILE. A bridge road's control points carry a deck height above the
// terrain (SceneObject::roadHeights, 0 when absent). The deck's height at a
// station is the Catmull-Rom of (terrain at control k + height k) along the
// spline - clamped between its two neighbours so it never overshoots - and a
// deck vertex is the HIGHER of that and the ordinary glued road there. So:
//   - every height 0: the deck runs straight from control point to control
//     point and spans whatever dip lies between them (a valley, a river);
//   - a raised point: an overpass, with ramps as long as its neighbours are
//     far away;
//   - wherever the terrain rises above the profile, the road is simply on the
//     ground again.
// The deck is flat ACROSS (one height per station), which is what lets a
// fully elevated station pair collapse to one quad.
namespace roadbridge {

// Above the glued road by more than this, a station gets structure.
inline constexpr float kStructureMin = 0.3f;
// Deck top to underside (the visible edge beam below the parapet).
inline constexpr float kDeckDepth = 0.7f;
// The parapet: a low solid wall outside each edge.
inline constexpr float kParapetHeight = 0.9f;
inline constexpr float kParapetWidth = 0.3f;
// How far a pier and an abutment reach below the ground (hides the seam on a
// slope).
inline constexpr float kSink = 0.3f;
// Piers: one wall pier every this many units of span at most, only where the
// gap under the deck is taller than kPierMinGap, kept off every other road.
inline constexpr float kPierSpacing = 16.0f;
inline constexpr float kPierLength = 1.2f;   // along the road
inline constexpr float kPierMinGap = 1.5f;
// An abutment closes the end of a span where the gap under the deck opens.
inline constexpr float kAbutmentGap = 0.5f;
inline constexpr float kAbutmentLength = 1.5f;
// Structure stations merge into chords within this error, at most this long.
inline constexpr float kMergeTolerance = 0.03f;
inline constexpr float kMaxChord = 8.0f;
// Console chunk grid (the kerbs' cell: one cull box per cell).
inline constexpr float kCell = 32.0f;
inline constexpr int kChunkBudget = 1800;
// Authorable deck height range above the terrain at a control point.
inline constexpr float kMaxHeight = 30.0f;
// Ground queries that must ignore a deck overhead (docs/roads.md "Bridges"):
// a wheel takes the highest surface no more than this above the car's own
// height; a walker keeps collidePlayer's 0.5 step.
inline constexpr float kVehicleStepUp = 1.5f;

// One station of a bridge road: the glued road's own sampling, plus the deck.
struct Station {
    float x = 0, z = 0;    // centre
    float tx = 0, tz = 1;  // unit tangent
    float v = 0;           // texture V (arc / kTexLen)
    float y = 0;           // the PROFILE height (deck top before the max)
};

struct Deck {
    std::vector<Station> st;
    float halfWidth = 3.0f;
    int crossSteps = 1;
    float lift = 0.0f;        // roadgen::kLift + the rank lift
    roadgen::HeightFn ground; // bare terrain
    // The glued road's height at (x, z): ground + lift.
    float glued(float x, float z) const;
    // The drawn deck at (x, z) on station i's row.
    float deckAt(int i, float x, float z) const;
    // How far the deck is above the glued road at a world XZ on the bridge
    // (projected onto the centre line), 0 off it or where it is on the ground.
    float elevationAt(float x, float z) const;
    bool empty() const { return st.size() < 2; }
};

// `heights` per control point (missing = 0). `ground` = bare terrain (empty =
// flat at 0).
Deck buildDeck(const std::vector<float>& pointsXZ, const std::vector<float>& heights,
               float width, float sampleStep, int rank, const roadgen::HeightFn& ground);
Deck buildDeck(const SceneObject& o, const roadgen::HeightFn& ground);

// The drawn deck surface as a triangle list (three Vertex per triangle, the
// winding and UV of roadgen::tessellate). A station pair wholly in the air is
// one full-width quad; one touching the ground keeps every lateral cell.
void tessellateDeck(const Deck& d, std::vector<roadgen::Vertex>& out);

// Any road object's DRAWN surface (triangle list): a bridge's deck, else the
// glued roadgen::tessellate at ground + rank lift (full width, no edge fade).
void drawnRoad(const SceneObject& o, const roadgen::HeightFn& ground,
               std::vector<roadgen::Vertex>& out);

// The structure: parapets (inner face, top, outer face down to the deck
// underside), the underside, end caps, abutments and piers, as a triangle list
// of x, y, z + baked shade (roadgen::KerbVertex: the kerbs' vertex). `roads`
// is the scene's planner view and `self` this road's index in it - piers keep
// off every other road.
struct Structure {
    std::vector<roadgen::KerbVertex> tris;
    int spans = 0, piers = 0, abutments = 0;
};
void buildStructure(const Deck& d, const std::vector<roadgen::CrossingRoad>& roads, int self,
                    Structure& out);

// The planner's elevation for a bridge object (CrossingRoad::elevation).
std::function<float(float, float)> elevationFn(const SceneObject& o,
                                               const roadgen::HeightFn& ground);

// Keep roadHeights in step with an edit of the control points: a bridge's
// heights follow their points (insert interpolates, remove erases); an
// ordinary road's legacy heights are cleared, as they always were.
void onPointInserted(SceneObject& o, int newControl);
void onPointRemoved(SceneObject& o, int removedControl);
void onPointsReshaped(SceneObject& o);  // a move, a loop toggle: resize only
float heightOf(const SceneObject& o, int control);

// --- codegen ---------------------------------------------------------------
// One console chunk of structure (ROAD_BRIDGES row).
struct ChunkRow {
    int first = 0, count = 0;  // into the vertex table, in vertices
};
// Split a structure into cell chunks (each a whole number of triangles, at
// most kChunkBudget vertices). Appends to `verts` (x, y, z, shade).
void chunkStructure(const Structure& s, std::vector<float>& verts, std::vector<ChunkRow>& rows);
// The deck as ROAD_JUNCTIONS-sized rows: at most kChunkBudget vertices each,
// V rebased by a whole repeat per row (the road chunks' rule for the physical
// GS's ST path). `rowSizes` receives one vertex count per row.
void chunkDeck(std::vector<roadgen::Vertex>& tris, std::vector<int>& rowSizes);
// The scene_data tables; `rows` carry their scene.
struct SceneChunk {
    int scene = 0, first = 0, count = 0;
};
std::string tablesSource(const std::vector<SceneChunk>& rows, const std::vector<float>& verts,
                         const std::string& notes);
// Spliced into buildRoads before procFinishChunks.
std::string uploadSource();

// --vehicle-check (vehcheck.cpp calls it): deck profile, overpass without a
// node, the lower road's wheel query, piers to the ground.
void check(void (*verdict)(bool ok, const char* what));

}  // namespace roadbridge
