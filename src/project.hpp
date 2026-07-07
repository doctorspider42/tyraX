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
    // Player entity: marker in the editor; in the game the camera becomes
    // this player (walk FPP or noclip), regardless of the project template.
    Player = 6,
    // Particle emitter (fire/smoke/fog/sparks): cone marker in the editor,
    // camera-facing color quads simulated on a fixed pool in the game.
    Emitter = 7,
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
    bool usable = false;      // shows the USE prompt up close; BTN_USE fires On Used
    std::string modelPath;    // for PrimitiveType::Model, e.g. "res/models/tree.obj"
    std::string texturePath;  // PNG, e.g. "res/textures/bricks.png" (empty = color only)

    // Player entity parameters (used when type == Player)
    int playerMode = 0;            // 0 = walk (FPP), 1 = noclip (fly)
    float playerWalkSpeed = 0.4f;  // units per frame at full stick
    float playerLookSpeed = 1.0f;  // multiplier
    float playerEyeHeight = 1.8f;
    float playerJumpSpeed = 4.5f;  // units/s (walk mode, X button)
    bool playerCanJump = true;     // walk mode: X jumps

    // Particle emitter parameters (used when type == Emitter)
    int emitterKind = 0;      // 0 fire, 1 smoke, 2 fog, 3 sparks
    int emitterCount = 24;    // particle pool size (compiled in, no runtime alloc)
    float emitterSize = 0.5f; // base particle size in world units

    // Per-object logic. Object-referencing nodes default to this object
    // ("self"), so a copied object brings a working copy of its behavior.
    FlowGraph flowGraph;
};

const char* primitiveTypeName(PrimitiveType t);

inline bool operator==(const SceneObject& a, const SceneObject& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.type == b.type && eq3(a.position, b.position) &&
           eq3(a.rotation, b.rotation) && eq3(a.scale, b.scale) && eq3(a.color, b.color) &&
           a.physics == b.physics && a.usable == b.usable && a.modelPath == b.modelPath &&
           a.texturePath == b.texturePath && a.playerMode == b.playerMode &&
           a.playerWalkSpeed == b.playerWalkSpeed &&
           a.playerLookSpeed == b.playerLookSpeed &&
           a.playerEyeHeight == b.playerEyeHeight &&
           a.playerJumpSpeed == b.playerJumpSpeed &&
           a.playerCanJump == b.playerCanJump && a.emitterKind == b.emitterKind &&
           a.emitterCount == b.emitterCount && a.emitterSize == b.emitterSize &&
           a.flowGraph == b.flowGraph;
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

// A scene: its own objects (each with its flow graph), its own terrain
// (size, heightmap, texture) and its own lighting. Sky, physics prefs, HUD
// and audio assets are shared; the game starts in the first scene and
// switches via the Switch Scene flow node.
struct SceneData {
    std::string name = "main";
    std::vector<SceneObject> objects;

    TerrainConfig terrain;
    // Heightmap: vertex heights on the render grid (row-major, hmW x hmD).
    // Empty = flat. Persisted in <project>/terrain-<scene>.heights.
    std::vector<float> heights;
    int hmW = 0, hmD = 0;
    std::string terrainTexture;    // PNG, tiled; empty = checker colors
    float terrainTexScale = 4.0f;  // world units per texture tile

    // Lighting (baked into vertex colors)
    float lightDir[3] = {0.37f, 0.82f, 0.44f};  // direction TO the light
    float ambient = 0.55f;                      // 0..1
    float diffuse = 0.45f;                      // 0..1
    float lightColor[3] = {1.0f, 1.0f, 1.0f};   // tints the diffuse term
    float brightness = 1.0f;                    // global multiplier (0..2)
};

inline bool operator==(const SceneData& a, const SceneData& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.objects == b.objects &&
           a.terrain.width == b.terrain.width && a.terrain.depth == b.terrain.depth &&
           a.heights == b.heights && a.hmW == b.hmW && a.hmD == b.hmD &&
           a.terrainTexture == b.terrainTexture &&
           a.terrainTexScale == b.terrainTexScale && eq3(a.lightDir, b.lightDir) &&
           a.ambient == b.ambient && a.diffuse == b.diffuse &&
           eq3(a.lightColor, b.lightColor) && a.brightness == b.brightness;
}

struct Project {
    std::string name;
    std::string dir;  // absolute path to project root
    std::string gameTemplate = "orbit";  // "orbit" | "fpp"
    ProjectSettings settings;
    std::vector<SceneData> scenes{SceneData{}};
    int activeScene = 0;  // scene edited in the editor (not persisted in json)

    // The active scene - what the editor UI operates on.
    SceneData& active() {
        if (activeScene < 0 || activeScene >= (int)scenes.size()) activeScene = 0;
        return scenes[activeScene];
    }
    const SceneData& active() const {
        const int i =
            (activeScene < 0 || activeScene >= (int)scenes.size()) ? 0 : activeScene;
        return scenes[i];
    }
    std::vector<SceneObject>& objects() { return active().objects; }
    const std::vector<SceneObject>& objects() const { return active().objects; }

    std::vector<HudImage> hud;
    // Music tracks (16-bit 22kHz stereo WAV in res/audio/), played via the
    // flow graph (Play Music / Stop Music / Set Music Volume actions).
    std::vector<std::string> music;
    // Sound effects (16-bit 22kHz WAV in res/sfx/, converted to ADPCM by the
    // toolchain at build). One-shots via the flow graph Play Sound action.
    std::vector<std::string> sounds;

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

// Vertex grid dimensions for the ACTIVE scene terrain size + detail cap.
void terrainGridDims(const Project& p, int& vertsW, int& vertsD);

// Makes every scene heightmap match its grid (zero-fill or resample).
void ensureHeightmap(Project& p);

// Bilinear height at world coordinates in the ACTIVE scene (0 outside).
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
