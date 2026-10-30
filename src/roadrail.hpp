#pragma once

#include <vector>

#include "roadgen.hpp"

// Rails and tram tracks (docs/roads.md "Rails and tram tracks"). Host-only, no
// GL, no project model - the roadgen shape. A Road whose kind is kRail is a
// railway: its own strip is the ballast bed (a roadtex kBallast texture with
// the sleepers painted on), and the rails are real geometry standing on it. A
// kTram road is an ordinary street with the rails set flush into it.
//
// The rails are baked on the host exactly like the kerbs and travel in the
// SAME runtime tables (ROAD_KERBS / ROAD_KERB_VERTS, owner -4 procChunks), so
// the console needs no new table, no new upload loop and no rail code at all:
// a rail vertex is a kerb vertex whose shade names a palette colour.
namespace roadrail {

enum Kind : int { kRoad = 0, kRail = 1, kTram = 2 };

// Standard gauge (between the rail heads' INNER faces), world units at one
// unit per metre. Every rail dimension below is at this gauge and scales with
// a road's own gauge / kStandardGauge, so a narrow-gauge line gets lighter
// rails and a project at another scale only has to set the gauge.
inline constexpr float kStandardGauge = 1.435f;
inline constexpr float kRailHeadWidth = 0.07f;
inline constexpr float kRailHeight = 0.12f;   // a raised rail's top above its bed
inline constexpr float kRailSink = 0.03f;     // its side faces start this far below
// Flush rails (a tram street, a railway across a road) float above the road
// surface like the node paint does (roadgen::kSpillLift = 0.02), just above it,
// so a rail crossing a zebra's stripe stays on top of the paint.
inline constexpr float kFlushLift = 0.035f;
inline constexpr float kGrooveWidth = 0.045f;  // the flangeway beside a flush rail
inline constexpr float kPanelLift = 0.025f;    // level-crossing panel above the road
inline constexpr float kPanelOverhang = 0.6f;  // the panel's reach past each rail
// Track centre to track centre when a road carries two tracks.
inline constexpr float kTrackSpacingRail = 4.0f;  // == roadtex::kBallastTrackSpacing
inline constexpr float kTrackSpacingTram = 3.0f;
// Lateral and vertical error a merged rail chord may have (rails must look
// straight, so tighter than the kerbs' 0.02), and the longest merged chord.
inline constexpr float kRailTolerance = 0.01f;
inline constexpr float kRailMaxRun = 8.0f;

// The vertex colour. roadgen::KerbVertex::shade below 1.5 is the kerb's own
// concrete grey (shade x a warm tint); 2 and up names an entry of kPalette.
// TWIN NOTICE: the generated upload (templates.cpp roadKerbsUpload) decodes the
// same rule from ROAD_KERB_PALETTE, which the codegen prints from kPalette.
enum Shade : int { kShadeRailTop = 2, kShadeRailSide = 3, kShadeGroove = 4, kShadePanel = 5 };
inline constexpr int kPaletteCount = 4;
inline constexpr float kPalette[kPaletteCount][3] = {
    {0.80f, 0.80f, 0.82f},  // the polished running surface
    {0.38f, 0.28f, 0.22f},  // rusty web and foot
    {0.11f, 0.10f, 0.10f},  // the groove
    {0.20f, 0.20f, 0.21f},  // a level crossing's rubber panel
};
// 0..1 RGB of a shade - the viewport's copy of the runtime rule.
void shadeRgb(float shade, float rgb[3]);

enum Profile : int {
    kProfileRaised = 0,  // a rail standing on its bed: two side faces + the head
    kProfileFlush = 1,   // a rail set into a road: the head + its groove
    kProfilePanel = 2,   // a level crossing's panel between and beside the rails
};

// One swept line. pts: x, y (the drawn surface under the line), z, and the
// unit RIGHT vector rx, rz of the road there - per point.
struct RailPiece {
    int road = -1;    // CrossingRoad index
    int profile = kProfileRaised;
    float scale = 1.0f;  // gauge / kStandardGauge
    float inner = 0.0f;  // +1 / -1: the track centre lies along +right / -right
    std::vector<float> pts;
    int points() const { return (int)(pts.size() / 5); }
};

// True when some road is a railway or a tram street.
bool anyRails(const std::vector<roadgen::CrossingRoad>& roads);

// Lateral offsets of a road's rails from its centre line (right = +), two per
// track: each track centre +- (gauge + head) / 2.
std::vector<float> railOffsets(const roadgen::CrossingRoad& r);

// Every rail (and level-crossing panel) of every rail / tram road. The rails
// run the road's WHOLE length and are never cut at a node: across another
// road (any non-railway road or a node patch with one) a railway's rails turn
// flush and get a panel, the level crossing; through a fork or diamond of two
// railways each line's rails simply run on through. `surface` answers the
// DRAWN height (roads + patches, the rank lift included) or Surface::kNone;
// `ground` is the bare terrain. Deterministic, road order.
std::vector<RailPiece> planRails(const std::vector<roadgen::CrossingRoad>& roads,
                                 const roadgen::CrossingPlan& plan,
                                 const roadgen::HeightFn& surface,
                                 const roadgen::HeightFn& ground);
// The pieces as triangle STRIP runs (the kerbs' run contract, kStripRun) in
// roadgen::kKerbCell chunks of at most kChunkBudget vertices; `chunkSizes`
// gets one vertex count per chunk. The codegen appends them to the kerbs'.
void railStrips(const std::vector<RailPiece>& pieces, std::vector<roadgen::KerbVertex>& out,
                std::vector<int>& chunkSizes);
// One piece as a triangle LIST - the same triangles the strips make.
void railTriangles(const RailPiece& piece, std::vector<roadgen::KerbVertex>& out);
// The rail heads, grooves and panels as drawn surface (the runtime's road
// height index reads the owner -4 chunks they ride in). Plans on `s` as it is
// (call it after the roads and patches are in), builds it, adds, and leaves
// the caller to build again.
void addRailsToSurface(roadgen::Surface& s, const std::vector<roadgen::CrossingRoad>& roads,
                       const roadgen::CrossingPlan& plan, const roadgen::HeightFn& ground);

}  // namespace roadrail
