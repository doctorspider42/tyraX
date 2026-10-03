#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "roadfurniture.hpp"
#include "roadgen.hpp"

// Lit street lamps and wet roads (docs/weather.md, docs/roads.md "Street
// furniture" > "Lamps that light").
//
// Host-only (no GL, no ImGui, no project.hpp) - the roadfurn/roaddetail shape:
// the codegen bakes, the viewport previews and --vehicle-check proves, three
// consumers of ONE set of functions.
//
// What a lit lamp is on the console, and why each piece is that cheap:
// - its POOL of light on the street is a host-baked additive decal under the
//   lamp head, laid on the drawn surface (road, patch, pavement, terrain) by a
//   quadtree that splits only where the surface bends, so a lamp over a flat
//   street is ONE quad. The pools ride in the furniture's own ROAD_FURN rows
//   (a `light` column, the colour word carrying the texture coordinate), so
//   they chunk by 96-unit cells, stream as furniture items and
//   go to bin/roadfile/roads.bin with them. One global colour - the night
//   level - is all the EE touches per frame;
// - its HALO is a camera-facing corona, and on a wet road its REFLECTION is
//   a stretched streak lying on the road toward the viewer - both rebuilt
//   every frame in ONE additive bag from a small per-lamp table (ROAD_LAMPS),
//   only for the lamps near the camera.
namespace roadlight {

// How high above the drawn surface a pool lies: over the node paint (0.02)
// and the road details (0.03).
inline constexpr float kPoolLift = 0.05f;
// A pool quad is split until it is never more than kPoolSink under the lifted
// surface (so at least kPoolLift - kPoolSink over the drawn one) nor more
// than kPoolFloat over it.
inline constexpr float kPoolSink = 0.03f;
inline constexpr float kPoolFloat = 0.12f;
// ... at most this many times (2^2 = 4 x 4 cells over the pool); a cell at
// the cap that still dips under a step is raised over it instead.
inline constexpr int kPoolDepth = 2;
// The pool is laid out to this fraction of its radius: the corona sprite is
// all but black beyond it.
inline constexpr float kPoolCrop = 0.85f;
// A vertex takes the highest surface within 0.6 (or one finest cell):
// a kerb's top, not the road beside it, so no part of a pool hides under a
// step.
// Pools are chunked by this cell (twice the furniture's: a pool chunk is a
// handful of quads, and every chunk drawn is one more submit).
inline constexpr float kPoolCell = 96.0f;
// A pool chunk is drawn up to this far (from its centre).
inline constexpr float kPoolDrawDistance = 90.0f;
// The pool's radius is this fraction of the lamp head's height above the
// ground, clamped to [kPoolMinRadius, kPoolMaxRadius].
inline constexpr float kPoolRadiusPerHeight = 1.1f;
inline constexpr float kPoolMinRadius = 2.0f;
inline constexpr float kPoolMaxRadius = 9.0f;

// One lit lamp: its head (the light), the point of the drawn surface under
// it, that surface's slope there (for the wet streak) and its pool radius.
struct Lamp {
    int road = -1;
    float hx = 0, hy = 0, hz = 0;  // the head / lens centre
    float gx = 0, gy = 0, gz = 0;  // the surface under it
    float sx = 0, sz = 0;          // dy/dx, dy/dz of the surface around it
    float radius = 5.0f;           // pool radius
};

// The lamps of a furniture bake: every kLamp instance, its head found from
// its own triangles (the built-in lamp's unshaded lens; an .obj lamp's
// highest, furthest-forward part).
std::vector<Lamp> lampsOf(const roadfurn::Result& furniture);

struct Pools {
    std::vector<roadgen::Vertex> tris;  // triangle list, x y z + u v (0..1 over the pool)
    std::vector<int> chunkSizes;        // whole pools per kPoolCell cell
    int lamps = 0;
};
// `top` = the highest drawn surface at (x, z) (roads, patches, pavements and
// the terrain under them). Fills each lamp's gy/sx/sz/radius and bakes the
// pools. Pure: the same lamps and surface bake the same bytes.
Pools bakePools(std::vector<Lamp>& lamps, const roadgen::HeightFn& top);

// The pool vertices' texture coordinate in a ROAD_FURN_RGB word: 12 bits of
// U (high) and 12 of V, 0..4095 over 0..1.
uint32_t packUv(float u, float v);
void unpackUv(uint32_t w, float* u, float* v);

// The ROAD_LAMPS table: scene + 9 floats per lamp (head, ground, slope,
// radius), every scene's lamps together and in order.
std::string lampTableSource(const std::vector<std::pair<int, Lamp>>& lamps);
// The same table with no lamp (a project with weather and no lit lamps).
std::string emptyLampTableSource();

// --- the generated runtime ---------------------------------------------------

struct Gates {
    bool lamps = false;    // some road has furniture lamps (pool rows + ROAD_LAMPS)
    bool weather = false;  // some scene rains or a graph has Set Weather
    bool roads = false;    // the project has road chunks (the wet tint)
    bool vehicles = false; // the project has vehicles (wet car-light streaks, with weather)
    bool puddles = false;  // weather + road details (puddle chunks)
};
// Applies every hook to one filled template (templates.cpp fillTemplate,
// last). A project with neither gate keeps its exact source.
std::string patchTemplate(std::string s, const Gates& g);

// The weather/lamp block appended to inc/daynight.gen.hpp: the per-scene
// tables, the pasted core (src/weather_core.inl) and its state.
struct SceneWeather {
    int weather = 0;          // 0 dry, 1 rain
    float intensity = 1.0f;
    int lamps = 0;            // 0 auto, 1 always on, 2 off
    float staticLevel = 0.0f; // auto + a cycle that does not run: the level at its baked hour
};
std::string weatherHeaderSource(const std::vector<SceneWeather>& scenes);

// The weather core compiled for the host (the viewport preview and the check).
struct WeatherSim {
    float rain = 0, wet = 0;
    float from = 0, to = 0, time = 0, span = 0;  // the transition in flight
    void reset(float intensity);
    void request(int kind, float intensity, float seconds);
    void tick(float dt);
};
float lampLevelFromSun(float sunY);
// Puddles' one colour (0..255, alpha on the GS 0..128 scale) from the wetness
// and the sky colour - the core's weatherPuddleColor (docs/weather.md).
float puddleLevel(float wet);
void puddleColor(float wet, float skyR, float skyG, float skyB, float lamps, float out[4]);
// One car's wet-road lamp streaks - the core's weatherCarStreaks, the very
// function the console's renderRoadLamps calls (quads: 4 x 12 floats).
int carStreaks(const float pos[3], float yawDeg, float scale, const float lampFront[4],
               const float lampRear[4], float track, float wheelBase, float overhang,
               float groundFront, float groundRear, int lightsOn, int brakeOn, int broken,
               float ex, float ey, float ez, float wet, float quads[48], float ks[4],
               int rear[4]);

// --vehicle-check "wet roads and lamps".
void check(void (*verdict)(bool, const char*));

}  // namespace roadlight
