#pragma once

#include <functional>
#include <string>
#include <vector>

#include "roadgen.hpp"

struct SceneObject;

namespace roadpresets {
struct Preset;
}

// The Draw road tool's decisions (docs/roads.md "Drawing roads"): where a
// click lands once it is snapped, what a finished drawing does to the scene
// (a new road, or an existing road extended), and the bridge-height handle's
// arithmetic. Host-only and pure - no GL, no ImGui - so the viewport tool, the
// --draw-road CLI, the AI Assistant's draw_road tool and --vehicle-check all
// run the SAME code; the viewport only turns the mouse into a world XZ and
// draws what these functions answer.
//
// The snapping is built so the junction the author means is the junction
// roadgen::findNodes forms: an end placed ON another road's centre line is a
// contact (a T), a point placed on a centre line with the road carried on past
// it is a centre-line crossing (an X), and an end placed exactly on another
// road's end is a corner - or, in line with it, the same road extended.
namespace roaddraw {

struct Options {
    bool roadSnap = true;     // ends and centre lines of existing roads
    bool angleSnap = true;    // Shift in the viewport turns it off
    float angleStep = 15.0f;  // degrees
    bool gridSnap = false;
    float grid = 4.0f;        // world units
    // How far outside a road's half width the cursor still snaps to it,
    // world units (the viewport sets ~14 pixels' worth).
    float tolerance = 1.0f;
    // A drawing that starts (or ends) on a road END, carries on within 45
    // degrees of it and uses the same look extends that road instead of
    // making a second one.
    bool extendEnds = true;
};

enum Kind : int {
    kFree = 0,
    kGrid = 1,    // on the grid
    kAngle = 2,   // on an angle step from the reference direction
    kEnd = 3,     // exactly on an existing road's open end
    kCentre = 4,  // on an existing road's centre line
    kClose = 5,   // back on the drawing's own first point (a loop)
};
const char* kindName(int kind);

struct Snap {
    int kind = kFree;
    float x = 0.0f, z = 0.0f;
    int road = -1;       // index into the snapper's roads (kEnd, kCentre)
    int end = -1;        // kEnd: 0 = the road's first point, 1 = its last
    float tx = 1.0f, tz = 0.0f;  // the road's direction there (kEnd: OUTWARD)
    bool angled = false;         // the point also sits on an angle step
    float angle = 0.0f;          // that step, degrees from the reference
    float height = 0.0f;         // a bridge deck height for this point (CLI)
};

// The scene's roads as the snapper sees them: centre lines sampled exactly as
// roadgen samples them (Catmull-Rom, kSampleStep pieces), so a point projected
// onto one lies on the polyline findNodes tests.
class Snapper {
public:
    explicit Snapper(std::vector<roadgen::CrossingRoad> roads);
    // The snapped position for a cursor at (cx, cz) given what has been
    // placed so far (the drawing's own points, each with its own snap).
    Snap resolve(const std::vector<Snap>& placed, float cx, float cz, const Options& o) const;
    int roads() const { return (int)roads_.size(); }
    const roadgen::CrossingRoad& road(int i) const { return roads_[(size_t)i]; }
    // The sampled centre line, x0,z0,x1,z1,... (the viewport highlights it).
    const std::vector<float>& line(int i) const { return lines_[(size_t)i].xz; }

private:
    struct Line {
        std::vector<float> xz;
        bool closed = false;
        float halfW = 1.0f;
    };
    std::vector<roadgen::CrossingRoad> roads_;
    std::vector<Line> lines_;
    // Nearest point of road r's centre line, its distance and the direction.
    float nearest(int r, float x, float z, float* px, float* pz, float* tx, float* tz) const;
};

// What a finished drawing does.
struct Plan {
    bool ok = false;
    std::string error;
    int extend = -1;          // road index to extend, or -1 = a new road
    std::vector<float> points;   // the new road's (or the extended road's whole) XZ list
    std::vector<float> heights;  // per control point, the deck height (all 0 = no bridge)
    bool closed = false;
    std::string summary;         // one line: what was snapped where
};
// `sameLook(road)` says whether that road already looks like what is being
// drawn (the extension rule); empty = never extend.
Plan finish(const Snapper& s, const std::vector<Snap>& placed, const Options& o,
            const std::function<bool(int road)>& sameLook);

// Applies a plan to a scene's objects: a new Road named `<preset key>-N` with
// the preset applied and its materials written on demand into projectDir, or
// the extended road's new points (its own look kept). `roadObject` maps the
// snapper's road indices to object indices. Returns the object index, -1 when
// the plan is not ok.
int commit(std::vector<SceneObject>& objects, const std::vector<int>& roadObject, const Plan& plan,
           const roadpresets::Preset& preset, const std::string& projectDir, float unitsPerMeter);

// Bridge height handles (docs/roads.md "Bridges"): the world Y on the
// vertical line through (px, pz) nearest the camera ray o + t d - what a drag
// of the handle reads. False when the ray is (nearly) vertical, so a top view
// keeps the height it had, or the point lies behind the camera.
bool verticalHandleY(const float o[3], const float d[3], float px, float pz, float* y);
// The roadHeights value for a deck top at `y` over ground at `groundY`
// (clamped to 0..roadbridge::kMaxHeight, snapped to 0.1 when asked).
float bridgeHeightFor(float y, float groundY, bool snap);

// --vehicle-check "road drawing".
void check(void (*verdict)(bool ok, const char* what));

}  // namespace roaddraw
