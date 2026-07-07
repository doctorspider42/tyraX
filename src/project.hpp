#pragma once

#include <string>
#include <vector>

#include "flowgraph.hpp"

struct TerrainConfig {
    int width = 64;   // world units, X axis
    int depth = 64;   // world units, Z axis
};

enum class PrimitiveType {
    Box = 0,
    Sphere = 1,
    Cylinder = 2,
    Cone = 3,
    // Marker, not geometry: where the player appears at game start
    // (used by the FPP template; the first one in the scene wins).
    SpawnPoint = 4,
    // Custom .obj mesh (modelPath, relative to the project dir)
    Model = 5,
};

// Unit primitives fit a 1x1x1 cube centered at origin and are transformed by
// scale -> rotation (X, then Y, then Z) -> translation.
struct SceneObject {
    std::string name;
    PrimitiveType type = PrimitiveType::Box;
    float position[3] = {0.0f, 0.5f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};  // degrees
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float color[3] = {0.8f, 0.35f, 0.25f};
    bool physics = false;     // falls with gravity in the game
    std::string modelPath;    // for PrimitiveType::Model, e.g. "res/models/tree.obj"
    std::string texturePath;  // PNG, e.g. "res/textures/bricks.png" (empty = color only)
};

const char* primitiveTypeName(PrimitiveType t);

inline bool operator==(const SceneObject& a, const SceneObject& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.type == b.type && eq3(a.position, b.position) &&
           eq3(a.rotation, b.rotation) && eq3(a.scale, b.scale) && eq3(a.color, b.color) &&
           a.physics == b.physics && a.modelPath == b.modelPath &&
           a.texturePath == b.texturePath;
}

// General project preferences (Project > Preferences in the editor).
// Baked into the generated terrain_config.hpp on every build.
struct ProjectSettings {
    // "precise": real per-triangle clipping - no holes at screen edges, but
    // costs EE time. "fast": VU1 cull only - fastest, may drop triangles
    // that extend far beyond the screen.
    std::string clipping = "precise";

    int terrainDetail = 32;  // max terrain grid cells per axis (quality vs perf)
    float skyColor[3] = {0.25f, 0.55f, 0.78f};   // horizon / clear color
    float skyTopColor[3] = {0.08f, 0.3f, 0.65f};  // zenith (gradient dome)
    bool skyDome = true;  // render a gradient sky dome (vs flat clear color)

    // FPP template
    float eyeHeight = 1.8f;
    float walkSpeed = 0.4f;
    float lookSpeed = 1.0f;  // multiplier

    // Orbit template
    float orbitSpeed = 1.0f;  // multiplier

    // Physics
    float gravity = 9.8f;    // units/s^2
    float jumpSpeed = 4.5f;  // units/s (FPP jump)

    // Lighting (baked into vertex colors at build)
    float lightDir[3] = {0.37f, 0.82f, 0.44f};   // direction TO the light
    float ambient = 0.55f;                       // 0..1
    float diffuse = 0.45f;                       // 0..1
    float lightColor[3] = {1.0f, 1.0f, 1.0f};    // tints the diffuse term
    float brightness = 1.0f;                     // global multiplier (0..2)

    // Terrain texture (PNG, tiled; empty = checker colors)
    std::string terrainTexture;
    float terrainTexScale = 4.0f;  // world units per texture tile
};

class History;

// A HUD image (PNG sprite) drawn on top of the 3D scene.
struct HudImage {
    std::string name;
    std::string imagePath;      // e.g. "res/hud/crosshair.png"
    float pos[2] = {0.5f, 0.5f};   // normalized screen position (center anchor)
    float size[2] = {64.0f, 64.0f};  // pixels (PS2 screen is 512x448)
};

struct Project {
    std::string name;
    std::string dir;  // absolute path to project root
    TerrainConfig terrain;
    std::string gameTemplate = "orbit";  // "orbit" | "fpp"
    ProjectSettings settings;
    std::vector<std::string> scenes{"main"};
    std::vector<SceneObject> objects;
    std::vector<HudImage> hud;
    FlowGraph flowGraph;

    // Terrain heightmap: vertex heights on the render grid (row-major,
    // hmW x hmD where hmW = min(detail, width) + 1 etc.). Empty = flat.
    // Persisted in <project>/terrain.heights, sculpted with the viewport brush.
    std::vector<float> heights;
    int hmW = 0, hmD = 0;

    bool valid() const { return !name.empty() && !dir.empty(); }
    std::string elfName() const { return name + ".elf"; }
    std::string elfPath() const { return dir + "\\bin\\" + elfName(); }
};

namespace project {

// Creates the project directory, generates all Tyra game sources / build files
// and project.json. gameTemplate: "orbit" (camera circles the terrain) or
// "fpp" (walk with the left stick, look with the right; seeds a spawn point).
// Returns empty string on success, error message otherwise.
std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& gameTemplate = "orbit");

// Loads project.json from an existing project directory.
std::string load(Project& out, const std::string& projectDir);

std::string save(const Project& p);

// --- Terrain heightmap -------------------------------------------------------

// Vertex grid dimensions for the current terrain size + detail cap.
void terrainGridDims(const Project& p, int& vertsW, int& vertsD);

// Makes p.heights match the current grid (zero-fill or nearest resample).
void ensureHeightmap(Project& p);

// Bilinear height at world coordinates (0 outside the terrain).
float heightAtWorld(const Project& p, float x, float z);

// Raise/lower with a smooth (cosine) falloff brush.
void sculptHeightmap(Project& p, float worldX, float worldZ, float radius, float delta);

std::string saveHeights(const Project& p);
void loadHeights(Project& p);  // silent no-op when the file is absent

// --- Solution file (<name>.tyra) --------------------------------------------
// Editor-side state next to project.json: selection, active gizmo tool and
// the undo history (up to History::kMaxEntries snapshots).

std::string saveSolution(const Project& p, const History& h, int selectedObject, int gizmoOp,
                         int viewMode);

// Restores history + editor state. Returns an error string when the file is
// missing/malformed/stale - the caller should then start a fresh history.
std::string loadSolution(const Project& p, History& h, int& selectedObject, int& gizmoOp,
                         int& viewMode);

// Rewrites editor-owned files from the current templates and project data:
// docker infra (Dockerfile, docker-compose.yml) and generated headers
// (terrain_config.hpp, scene_data.hpp) are always rewritten; game sources
// are rewritten only while they still carry the "Generated by tyra-editor"
// marker in the first line (delete it to take ownership of a file).
// Called before every build.
std::string refreshGenerated(const Project& p);

}  // namespace project
