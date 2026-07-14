#pragma once

#include <map>
#include <string>
#include <vector>

#include "ambience.hpp"
#include "flowgraph.hpp"
#include "grading.hpp"
#include "screenfx.hpp"
#include "sequence.hpp"

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
    // Particle emitter (fire/smoke/fog/sparks/rain): live animated preview in
    // the editor viewport; camera-facing quads (optionally textured via the
    // assigned material) simulated on a fixed pool in the game.
    Emitter = 7,
    // Sound emitter: sphere marker in the editor; in the game it plays an
    // imported sound effect with distance-attenuated (spatial) volume.
    SoundEmitter = 8,
    // Point light: a glowing bulb + radius ring in the editor; at build its
    // color/brightness are baked into nearby terrain and object vertex colors
    // with a linear distance falloff (static lighting - no runtime cost).
    PointLight = 9,
    // Save point: a solid box that is implicitly usable; pressing USE on it
    // in the game opens the memory card save menu (3 slots on mc0:).
    SavePoint = 10,
    // Empty: a pure transform with no geometry in the game (sphere marker in
    // the editor). Anchor for attached scripts, waypoints, flow-graph logic.
    Empty = 11,
    // Plane: a flat unit square in the XZ plane (a floor/wall tile). Rendered
    // double-sided so it is visible from both faces.
    Plane = 12,
    // Decal: a flat unit quad in the XY plane facing +Z, textured through the
    // assigned material's map_Kd with transparency (cutout + alpha blend). Sits
    // slightly in front of its origin along +Z to avoid z-fighting the surface
    // it is stuck on. Never collides. For signs, posters, text on walls.
    Decal = 13,
    // Camera: a shot marker for the Cutscene Director (body + FOV frustum
    // wireframe in the editor, looking down its +Z axis; invisible in the
    // game). A camera-track keyframe bound to it takes eye/look-at/FOV from
    // the entity - animate the entity itself for dolly/crane shots.
    Camera = 14,
};

// Tessellation detail for the geometry primitives, stored per object in
// SceneObject::primDetail. Its meaning depends on the shape: for the curved
// shapes (Sphere, Cylinder, Cone) it is the number of radial segments; for a
// Box it is the number of subdivisions per edge (1 = the plain 12-triangle
// box). Higher = more triangles = smoother/finer baked lighting, at PS2 vertex
// cost. Box grows quadratically, so it has a tighter cap. The same formulas run
// in three places that must stay in sync: the shared host tessellation
// (primmesh.cpp unitBox/... - used by the viewport AND the decal projector),
// the generated PS2 runtime (templates.cpp addBox/addSphere/addCylinder/addCone)
// and primTriangleCount.
constexpr int kDefaultPrimDetail = 16;  // curved shapes (radial segments)
constexpr int kDefaultBoxDetail = 1;    // Box (subdivisions per edge)

// SavePoint tessellates exactly like a Box (the game's geometry builder and
// the viewport both draw it as one), so it shares the Box detail semantics.
// Without this it inherited the curved-shape default of 16 segments, which
// addBox read as 16 subdivisions per edge - every save shrine silently cost
// 3072 triangles (9.2k verts) of near-camera EE clipping in the game.
inline bool primDetailIsBoxLike(PrimitiveType t) {
    return t == PrimitiveType::Box || t == PrimitiveType::SavePoint;
}
inline int primDetailMin(PrimitiveType t) { return primDetailIsBoxLike(t) ? 1 : 3; }
inline int primDetailMax(PrimitiveType t) { return primDetailIsBoxLike(t) ? 16 : 64; }
inline int clampPrimDetail(PrimitiveType t, int d) {
    const int lo = primDetailMin(t), hi = primDetailMax(t);
    return d < lo ? lo : (d > hi ? hi : d);
}
inline int defaultPrimDetail(PrimitiveType t) {
    return primDetailIsBoxLike(t) ? kDefaultBoxDetail : kDefaultPrimDetail;
}
// Sphere vertical rings derived from the radial segment count (~5:7 ratio).
inline int primSphereStacks(int detail) {
    const int s = detail * 5 / 7;
    return s < 2 ? 2 : s;
}
// Triangles a primitive tessellates to at the given detail - for the UI
// readout. Marker/geometry-less types report 0.
inline int primTriangleCount(PrimitiveType type, int detail) {
    const int d = clampPrimDetail(type, detail);
    switch (type) {
        case PrimitiveType::Box:
        case PrimitiveType::SavePoint:
            return 12 * d * d;  // 6 faces * 2 * d^2 subquads
        case PrimitiveType::Sphere: return primSphereStacks(d) * d * 2;
        case PrimitiveType::Cylinder: return d * 4;  // side (2/seg) + 2 caps
        case PrimitiveType::Cone: return d * 2;      // side + base (1/seg each)
        default: return 0;
    }
}

// Unit primitives fit a 1x1x1 cube centered at origin and are transformed by
// scale -> rotation (X, then Y, then Z) -> translation.
struct SceneObject {
    // Stable, opaque identity - generated once (project::ensureObjectIds) and
    // never changed, even across renames. This is the merge/persistence key:
    // the multi-user file layout stores one object per file keyed on it, so
    // two people editing different objects touch different files. Object
    // *references* (flow graphs, sequences, layers) still resolve by name;
    // the id only exists to give the merge machinery something stable to
    // anchor on. Empty on objects authored before ids existed / freshly
    // pasted; filled in on the next load or commit.
    std::string id;
    std::string name;
    PrimitiveType type = PrimitiveType::Box;
    float position[3] = {0.0f, 0.5f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};  // degrees
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float color[3] = {0.6f, 0.6f, 0.6f};  // neutral gray (specialized types override)
    bool physics = false;     // falls with gravity in the game
    bool usable = false;      // shows the USE prompt up close; BTN_USE fires On Used
    bool saveState = false;   // position/color/visibility persisted in save slots
    // Player collision: 0 = box (models use their real mesh AABB), 1 = mesh
    // (models only: per-triangle - ramps/stairs are walkable), 2 = none
    int collisionMode = 0;
    // Streaming layer this object belongs to (SceneData::layers entry name).
    // Empty = no layer: always resident in the game, always shown in the
    // editor. Objects reference layers by name (renames remap references).
    std::string layer;
    // Tessellation detail for the geometry primitives: radial segments for
    // Sphere/Cylinder/Cone, subdivisions per edge for Box. More = smoother +
    // more triangles. Type-dependent range/default - see clampPrimDetail /
    // defaultPrimDetail / primTriangleCount.
    int primDetail = kDefaultPrimDetail;
    // Rendering cut-off: farther than this from the camera the object is not
    // drawn at all (collision, sounds and scripts still run). 0 = unlimited.
    // The cheapest LOD there is - era-correct for dense scenes.
    float drawDistance = 0.0f;
    // Show in reflections: this object is also rendered into the dynamic
    // ("@sky") environment map, so reflective materials mirror it - the GT3
    // trick's second half. Each marked object costs a second (128x128,
    // wide-FOV) render per frame; mark the few props that sell the effect.
    bool reflected = false;
    std::string modelPath;    // for PrimitiveType::Model, e.g. "res/models/tree.obj"
    // Material library (.mtl) assigned to the object, e.g.
    // "res/materials/walls.mtl". Primitives take the file's FIRST material
    // (Kd + map_Kd applied on their UVs); models use it as an override that
    // replaces their own mtl (usemtl names resolve against it). Empty =
    // plain color (primitives) / the model's own materials.
    std::string materialPath;

    // Decal projection (used when type == Decal). false = the flat quad; true =
    // project the texture onto the receiver geometry (terrain + every solid
    // object whose bounding box overlaps this decal's oriented unit-cube volume)
    // so it conforms to walls/models/floors instead of floating as a flat plane.
    // The transform is the projector: scale = footprint (X/Y) + depth (Z),
    // rotation aims +Z at the surface, position places it. Computed at build
    // time on the host (decalproj) and baked to static geometry - zero PS2 cost.
    // For graffiti/wall text on angled or curved surfaces and fake blob shadows.
    bool decalProject = false;

    // Player entity parameters (used when type == Player)
    int playerMode = 0;            // 0 = walk (FPP), 1 = noclip (fly)
    float playerWalkSpeed = 0.4f;  // units per frame at full stick
    float playerLookSpeed = 1.0f;  // multiplier
    float playerEyeHeight = 1.8f;
    float playerJumpSpeed = 4.5f;  // units/s (walk mode, X button)
    bool playerCanJump = true;     // walk mode: X jumps

    // Camera-attached spot light carried by the player (used when type ==
    // Player). Additive cone + distance falloff computed per vertex on VU1, on
    // top of the baked shading (no N.L - the PS2 color pipelines carry no
    // normals). `flashlightEnabled` is the master switch: it is the initial
    // state and the flow graph can flip it at runtime (Set Flashlight node).
    // `flashlightToggleButton` (empty = none) is an optional pad button the
    // player presses to turn the beam on/off; that on/off state only shows
    // while the master is enabled (it respects flashlightEnabled).
    bool flashlightEnabled = false;
    float flashlightColor[3] = {0.75f, 0.75f, 0.62f};
    float flashlightRange = 30.0f;  // world units
    float flashlightAngle = 20.0f;  // cone half-angle, degrees
    std::string flashlightToggleButton;  // pad button name, e.g. "Circle"; "" = none

    // Particle emitter parameters (used when type == Emitter). The particle
    // texture comes from the shared materialPath (first material's map_Kd);
    // scale X/Z = spawn area, color = tint.
    int emitterKind = 0;      // 0 fire, 1 smoke, 2 fog, 3 sparks, 4 rain, 5 custom
    int emitterCount = 24;    // particle pool size = density (compiled in)
    float emitterSize = 0.5f; // base particle size in world units
    bool emitterEnabled = true;       // off = starts disabled; Show/Hide Object
                                      // flow nodes switch it at runtime
    bool emitterFollowPlayer = false; // position becomes an offset from the
                                      // player (rain that tracks the camera)
    // Custom kind (5) physics. Particles shoot along the object's +Y axis
    // rotated by the object rotation (tilt the emitter 90 deg = a horizontal
    // pipe leak), inside a cone of emitterSpread degrees.
    float emitterSpeed = 3.0f;    // emission speed, units/s (+-20% jitter)
    float emitterSpread = 20.0f;  // cone half-angle, degrees (0 = jet)
    float emitterGravity = 9.8f;  // units/s^2; negative = buoyant (steam)
    float emitterWeight = 1.0f;   // air drag ~ 1/weight: light particles brake
                                  // and drift, heavy ones keep their velocity
    float emitterLife = 1.5f;     // particle lifetime, seconds (+-25% jitter)
    float emitterGrow = 1.0f;     // size multiplier reached at end of life
    float emitterOpacity = 0.6f;  // base alpha 0..1 (fades out near death)
    bool emitterDieOnGround = false;  // particle dies when it hits the terrain
                                      // (water soaking in instead of clipping)

    // Sound emitter parameters (used when type == SoundEmitter)
    std::string soundPath;      // one of Project::sounds ("res/sfx/x.wav")
    bool soundAuto = true;      // play automatically while the player is in range
    float soundRange = 15.0f;   // world units; volume fades linearly to 0
    float soundInterval = 0.0f; // seconds between retriggers; 0 = loop seamlessly
    bool soundOnPlayer = false; // plays centered on the player (plain stereo,
                                // full volume, no distance/pan) - e.g. dialogs

    // Point light parameters (used when type == PointLight). The light color
    // is the shared `color` field above.
    float lightBright = 1.0f;   // intensity added on top of the scene ambient
    float lightRadius = 8.0f;   // world units; contribution fades linearly to 0

    // Camera entity parameter (used when type == Camera): vertical field of
    // view in degrees. A Cutscene Director shot bound to this camera applies
    // it to the real PS2 projection for the duration of the shot.
    float cameraFov = 60.0f;

    // Animated model parameters (Model objects whose modelPath ends in .glb;
    // the editor bakes the file's clips to morph frames - see glbparser.hpp).
    std::string animClip;       // starting clip name ("" = the file's first)
    bool animAutoplay = true;   // play the starting clip at scene start
    bool animLoop = true;       // starting clip loops
    float animSpeed = 1.0f;     // playback speed multiplier

    // Per-object logic. Object-referencing nodes default to this object
    // ("self"), so a copied object brings a working copy of its behavior.
    FlowGraph flowGraph;

    // Attached object scripts: class names registered in src/scripts/*.cpp
    // with TYRA_OBJECT_SCRIPT(Name). Each attachment becomes its own script
    // instance in the game (Unity-style components); the same class can be
    // attached to any number of objects across scenes.
    std::vector<std::string> scripts;
};

const char* primitiveTypeName(PrimitiveType t);

// Animated models are .glb files (baked to morph frames at build); static
// models are .obj. Decides which import/render/codegen path an object takes.
inline bool isAnimatedModelPath(const std::string& path) {
    return path.size() > 4 && path.compare(path.size() - 4, 4, ".glb") == 0;
}

inline bool operator==(const SceneObject& a, const SceneObject& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.id == b.id && a.name == b.name && a.type == b.type && eq3(a.position, b.position) &&
           eq3(a.rotation, b.rotation) && eq3(a.scale, b.scale) && eq3(a.color, b.color) &&
           a.physics == b.physics && a.usable == b.usable &&
           a.saveState == b.saveState && a.collisionMode == b.collisionMode &&
           a.layer == b.layer &&
           a.primDetail == b.primDetail && a.drawDistance == b.drawDistance &&
           a.reflected == b.reflected &&
           a.modelPath == b.modelPath &&
           a.materialPath == b.materialPath && a.decalProject == b.decalProject &&
           a.playerMode == b.playerMode &&
           a.playerWalkSpeed == b.playerWalkSpeed &&
           a.playerLookSpeed == b.playerLookSpeed &&
           a.playerEyeHeight == b.playerEyeHeight &&
           a.playerJumpSpeed == b.playerJumpSpeed &&
           a.playerCanJump == b.playerCanJump &&
           a.flashlightEnabled == b.flashlightEnabled &&
           eq3(a.flashlightColor, b.flashlightColor) &&
           a.flashlightRange == b.flashlightRange &&
           a.flashlightAngle == b.flashlightAngle &&
           a.flashlightToggleButton == b.flashlightToggleButton &&
           a.emitterKind == b.emitterKind &&
           a.emitterCount == b.emitterCount && a.emitterSize == b.emitterSize &&
           a.emitterEnabled == b.emitterEnabled &&
           a.emitterFollowPlayer == b.emitterFollowPlayer &&
           a.emitterSpeed == b.emitterSpeed && a.emitterSpread == b.emitterSpread &&
           a.emitterGravity == b.emitterGravity &&
           a.emitterWeight == b.emitterWeight && a.emitterLife == b.emitterLife &&
           a.emitterGrow == b.emitterGrow && a.emitterOpacity == b.emitterOpacity &&
           a.emitterDieOnGround == b.emitterDieOnGround &&
           a.soundPath == b.soundPath && a.soundAuto == b.soundAuto &&
           a.soundRange == b.soundRange && a.soundInterval == b.soundInterval &&
           a.soundOnPlayer == b.soundOnPlayer &&
           a.lightBright == b.lightBright && a.lightRadius == b.lightRadius &&
           a.cameraFov == b.cameraFov &&
           a.animClip == b.animClip && a.animAutoplay == b.animAutoplay &&
           a.animLoop == b.animLoop && a.animSpeed == b.animSpeed &&
           a.flowGraph == b.flowGraph && a.scripts == b.scripts;
}

// General project preferences (Project > Preferences in the editor).
// Baked into the generated terrain_config.hpp on every build.
struct ProjectSettings {
    // Build target. videoSystem: "auto" follows the console region, "ntsc"
    // forces 60 Hz, "pal" forces 50 Hz (gameplay speed is wall-clock
    // normalized via g_frameScale in the generated game, so both play the
    // same). The "debug" profile unlocks the on-screen FPS / free-RAM
    // overlays; "release" strips them from the build.
    std::string videoSystem = "auto";      // "auto" | "ntsc" | "pal"
    std::string buildProfile = "release";  // "release" | "debug"

    // Output scan mode. "interlaced" is the stock 480i/576i signal (follows
    // videoSystem). "progressive" outputs flicker-free 480p, "1080i" a
    // pillarboxed HD signal - both need component cables on a real console
    // (PCSX2 shows every mode) and always run at 60 Hz.
    std::string displayMode = "interlaced";  // "interlaced" | "progressive" | "1080i"

    // 16:9 anamorphic output: widens the projection so proportions are
    // correct on a widescreen TV (the framebuffer stays the same; in 1080i
    // the GS display window widens instead). Also switchable at runtime
    // via the Set Widescreen flow node.
    bool widescreen = false;

    // Texture quantization at build (the PS2-native "compression": palettized
    // PSMT8/PSMT4 textures). Applied to res/models|materials|textures PNGs
    // when baking res/ -> .res-baked/; sources stay untouched. Per-asset
    // overrides live in Project::textureQuality. "none" = full color,
    // "8bit" = 256 colors, "4bit" = 16 colors (default - era-correct).
    std::string textureQuant = "4bit";
    bool showFps = false;     // debug profile only: on-screen FPS counter
    bool showMemory = false;  // debug profile only: on-screen free-RAM readout
    bool showProfiler = false;  // debug profile only: per-phase EE-time HUD
                                // (scene / highlight / particles / whole frame)

    // Experimental: skip the vsync wait before the buffer flip. Frame rate
    // becomes continuous instead of quantized to 50/25 (PAL), at the cost
    // of screen tearing. Gameplay speed is unaffected either way - the
    // generated game measures real frame time (see updateFrameClock).
    bool disableVsync = false;

    // "precise": real per-triangle clipping - no holes at screen edges, but
    // costs EE time. "fast": VU1 cull only - fastest, may drop triangles
    // that extend far beyond the screen.
    // Triangle handling: "vu1" (default - precise clipping in the VU1 clip
    // programs, no EE cost), "precise" (the legacy EE clipper) or "fast"
    // (cull-only). Projects saved before the vu1 default keep their value.
    std::string clipping = "vu1";

    // Animation LOD: animated-model instances farther than this from the
    // camera refresh their pose/skinning every 2nd frame (every 4th beyond
    // twice the distance), staggered across objects so the cost spreads.
    // Playback time is unaffected - the pose catches up on the next
    // refresh. 0 = off (every instance skins every frame).
    float animLodDistance = 0.0f;

    // Mesh LOD: the build bakes decimated variants (~50% and ~25% vertices,
    // quadric-error collapse) of every animated model into the .tskl;
    // instances farther than this render the 50% mesh, beyond twice the
    // distance the 25% one. 0 = off (no LODs baked or kept in RAM).
    float meshLodDistance = 0.0f;

    int terrainDetail = 32;  // max terrain grid cells per axis (quality vs perf)

    // Terrain streaming: the generated game builds the terrain in 16x16-cell
    // chunks; with a view distance > 0 only the chunks within that range of
    // the camera stay in memory (the ring streams in as the player moves,
    // like the layer streaming). 0 = whole map resident. Large maps at high
    // detail NEED this - the full mesh would not fit in the PS2's 32 MB.
    float terrainViewDistance = 0.0f;  // world units, 0 = off
    float skyColor[3] = {0.25f, 0.55f, 0.78f};   // horizon / clear color
    float skyTopColor[3] = {0.08f, 0.3f, 0.65f};  // zenith (gradient dome)
    bool skyDome = true;  // render a gradient sky dome (vs flat clear color)
    // How much of the dome the zenith color fills. 0.5 = linear (color scales
    // linearly with elevation); higher = zenith reaches lower toward the
    // horizon (bigger zenith cap); lower = zenith stays near the top. Both the
    // viewport preview and the generated dome remap the gradient by
    // pow(t, (1-size)/size), t = 0 horizon .. 1 zenith.
    float zenithSize = 0.5f;  // 0.05 .. 0.95

    // FPP template
    float eyeHeight = 1.8f;
    float walkSpeed = 0.4f;
    float lookSpeed = 1.0f;  // multiplier

    // Analog sticks: offsets below this fraction of full deflection read as
    // zero (real DualShock sticks rest off-center); motion rescales smoothly
    // from the deadzone edge. Per stick - worn pads rarely drift equally.
    float stickDeadzoneL = 0.2f;  // 0..0.9, left stick (movement)
    float stickDeadzoneR = 0.2f;  // 0..0.9, right stick (camera)

    // Analog stick response curve, applied AFTER the deadzone rescales the
    // magnitude to 0..1: 0 = Linear (raw), 1 = Exponential (pow(mag, exp) -
    // finer control near center, snappier at the edge), 2 = S-Curve (eases in
    // and out - a soft center plus a firm cap). stickExp* tunes the shape of
    // curves 1/2 (>=1; higher = more pronounced). Per stick, and live-settable
    // from a flow graph via the Set Stick Curve node.
    int stickCurveL = 0;     // left stick (movement)
    int stickCurveR = 0;     // right stick (camera)
    float stickExpL = 2.0f;  // exponent for the L curve (>=1)
    float stickExpR = 2.0f;  // exponent for the R curve (>=1)

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

    // Terrain material (.mtl asset; empty = checker greens). The first
    // material's Kd tints the terrain; its map_Kd (when present) textures it,
    // tiled by the map's "-s" scale (repeats per world unit), otherwise the
    // terrain is a flat Kd-colored surface.
    std::string terrainMaterial;

    // Post effects (GS framebuffer blits at the end of every frame; no
    // pixel shaders on the PS2). 0 = off, 1 = maximum.
    float bloom = 0.0f;  // downsample + blur + additive re-add (glow)
    float grain = 0.0f;  // animated film grain noise overlay

    // GS hardware distance fog (atmospheric fade-out). Geometry blends
    // toward fogColor between fogStart and fogEnd view distances; free on the
    // GS (per-vertex coefficient computed on VU1). Match fogColor with the
    // sky/clear color and keep fogEnd at (or before) the far plane.
    bool fogEnabled = false;
    float fogColor[3] = {0.5f, 0.5f, 0.55f};
    float fogStart = 15.0f;   // world units from the camera
    float fogEnd = 120.0f;    // full fog at/after this distance

    // Scene switches show res/hud/loading.png centered on black for a
    // moment (a generated placeholder is written when the file is missing).
    bool loadingScreen = true;

    // In-game outline around usable objects while the player is within
    // highlightDistance (fading silhouette shells drawn after the scene).
    bool highlightUsable = false;
    float highlightDistance = 6.0f;                  // world units
    float highlightColor[3] = {1.0f, 0.85f, 0.15f};  // outline color
    float highlightWidth = 0.35f;  // total rim width incl. blur, world units
    int highlightSteps = 4;        // blur shells; 1 = sharp outline
    // Opacity of the strongest (innermost) shell, 0..1; the outer shells fade
    // from it. 1 = the strongest shell is fully opaque.
    float highlightOpacity = 0.56f;
    // Experimental: draw the glow ON the object surface (a colored overlay
    // that fades outward into a rim) instead of only a rim BEHIND it. Off =
    // the classic silhouette outline (shells pushed behind object depth).
    bool highlightOverlay = false;
};

inline bool operator==(const ProjectSettings& a, const ProjectSettings& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.videoSystem == b.videoSystem && a.buildProfile == b.buildProfile &&
           a.displayMode == b.displayMode && a.widescreen == b.widescreen &&
           a.showFps == b.showFps && a.showMemory == b.showMemory &&
           a.showProfiler == b.showProfiler &&
           a.disableVsync == b.disableVsync &&
           a.clipping == b.clipping && a.animLodDistance == b.animLodDistance &&
           a.meshLodDistance == b.meshLodDistance &&
           a.terrainDetail == b.terrainDetail &&
           a.terrainViewDistance == b.terrainViewDistance &&
           eq3(a.skyColor, b.skyColor) && eq3(a.skyTopColor, b.skyTopColor) &&
           a.skyDome == b.skyDome && a.zenithSize == b.zenithSize &&
           a.eyeHeight == b.eyeHeight &&
           a.walkSpeed == b.walkSpeed && a.lookSpeed == b.lookSpeed &&
           a.stickDeadzoneL == b.stickDeadzoneL &&
           a.stickDeadzoneR == b.stickDeadzoneR &&
           a.stickCurveL == b.stickCurveL && a.stickCurveR == b.stickCurveR &&
           a.stickExpL == b.stickExpL && a.stickExpR == b.stickExpR &&
           a.orbitSpeed == b.orbitSpeed && a.gravity == b.gravity &&
           a.jumpSpeed == b.jumpSpeed && eq3(a.lightDir, b.lightDir) &&
           a.ambient == b.ambient && a.diffuse == b.diffuse &&
           eq3(a.lightColor, b.lightColor) && a.brightness == b.brightness &&
           a.terrainMaterial == b.terrainMaterial && a.bloom == b.bloom &&
           a.grain == b.grain && a.fogEnabled == b.fogEnabled &&
           eq3(a.fogColor, b.fogColor) && a.fogStart == b.fogStart &&
           a.fogEnd == b.fogEnd &&
           a.loadingScreen == b.loadingScreen &&
           a.highlightUsable == b.highlightUsable &&
           a.highlightDistance == b.highlightDistance &&
           eq3(a.highlightColor, b.highlightColor) &&
           a.highlightWidth == b.highlightWidth &&
           a.highlightSteps == b.highlightSteps &&
           a.highlightOpacity == b.highlightOpacity &&
           a.highlightOverlay == b.highlightOverlay;
}

// Per-scene override switches (Scene > Preferences). Each "scene-visual"
// category can override the project defaults; when a flag is off, the scene
// inherits Project::settings for that category (see project::resolvedSettings).
// Camera, physics, terrain detail and the game template stay project-wide.
struct SceneOverrides {
    bool lighting = false;    // lightDir, ambient, diffuse, lightColor, brightness
    bool sky = false;         // skyColor, skyTopColor, skyDome
    bool clipping = false;    // clipping mode
    bool terrainMat = false;  // terrainMaterial
    bool postFx = false;      // bloom, grain
    bool fog = false;         // fogEnabled, fogColor, fogStart, fogEnd
    bool highlight = false;   // highlightUsable + distance/color/width/steps
};

inline bool operator==(const SceneOverrides& a, const SceneOverrides& b) {
    return a.lighting == b.lighting && a.sky == b.sky && a.clipping == b.clipping &&
           a.terrainMat == b.terrainMat && a.postFx == b.postFx &&
           a.fog == b.fog && a.highlight == b.highlight;
}

class History;

// A HUD image (PNG sprite) drawn on top of the 3D scene.
struct HudImage {
    std::string name;
    std::string imagePath;      // e.g. "res/hud/crosshair.png"
    float pos[2] = {0.5f, 0.5f};   // normalized screen position (center anchor)
    float size[2] = {64.0f, 64.0f};  // on-screen draw size in pixels (PS2 screen
                                     // is 512x448); independent of the texture
                                     // resolution below (the sprite is stretched)

    // Build-time bake: the PS2 rejects textures that are not 8/16/32/64/128/
    // 256/512 in each dimension (a runtime assert), so the editor resizes the
    // imported PNG into .res-baked before it reaches the game. 0 = auto (the
    // nearest valid power-of-two of the source); otherwise a chosen valid size.
    int texW = 0;
    int texH = 0;
    // Palette quantization, like the per-asset material texture quality:
    // "" = follow the project default (ProjectSettings::textureQuant),
    // "none" = full color (32-bit) override, "8bit" = 256-color, "4bit" =
    // 16-color. Lets an important HUD element keep full color while the rest
    // of the project runs quantized (or vice versa).
    std::string texQuant;
};

inline bool operator==(const HudImage& a, const HudImage& b) {
    return a.name == b.name && a.imagePath == b.imagePath &&
           a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] &&
           a.size[0] == b.size[0] && a.size[1] == b.size[1] &&
           a.texW == b.texW && a.texH == b.texH && a.texQuant == b.texQuant;
}

// The built-in "USE" prompt as a customizable HUD element (Tools > UI
// Editor, a non-deletable entry). imagePath "" = the embedded built-in
// use.png sprite; a custom PNG replaces it (baked like any HUD image). The
// defaults reproduce the classic hardcoded placement: centered, top edge at
// 72% of the 448px screen, 128x32 px.
inline HudImage defaultUsePrompt() {
    HudImage h;
    h.name = "USE prompt";
    h.pos[0] = 0.5f;
    h.pos[1] = 0.72f + 16.0f / 448.0f;  // center anchor of the old top-at-72%
    h.size[0] = 128.0f;
    h.size[1] = 32.0f;
    return h;
}

// An on-screen text (Tools > UI Editor > Texts): baked to a PNG sprite at
// build (res/hud/text-<name>.png - the engine has no font), shown/hidden at
// runtime by the Show Text / Hide Text flow nodes. Multi-line on '\n'.
struct HudText {
    std::string name = "text";
    std::string text = "New text";
    float pos[2] = {0.5f, 0.8f};   // normalized screen position (center anchor)
    int size = 16;                 // font pixel height
    float color[3] = {1.0f, 1.0f, 1.0f};
    // Baked text font, GameMenu::fontPath semantics: "" = default (Consolas
    // Bold chain), "res/fonts/x.ttf" = project font, bare name = Windows font.
    std::string fontPath;
    bool shadow = true;           // 1px dark offset behind the glyphs
    bool visibleAtStart = false;  // shown when the scene starts
};

inline bool operator==(const HudText& a, const HudText& b) {
    return a.name == b.name && a.text == b.text && a.pos[0] == b.pos[0] &&
           a.pos[1] == b.pos[1] && a.size == b.size &&
           a.color[0] == b.color[0] && a.color[1] == b.color[1] &&
           a.color[2] == b.color[2] && a.fontPath == b.fontPath &&
           a.shadow == b.shadow && a.visibleAtStart == b.visibleAtStart;
}

// A progress bar on a loading screen (Tools > Loading Screens). Continuous =
// a track quad with a fill quad growing left-to-right with load progress.
// Quantized = `segments` cells lighting up one per completed 1/segments step;
// cells are colored rects, or copies of segImage when it names a PNG (lit
// cells tinted fillColor, unlit bgColor - color modulation, one texture).
struct LoadingBar {
    std::string name = "bar";
    int kind = 0;                    // 0 = continuous, 1 = quantized
    float pos[2] = {0.5f, 0.75f};    // normalized screen position (center anchor)
    float size[2] = {256.0f, 16.0f}; // total on-screen size in px (512x448 screen)
    float bgColor[3] = {0.15f, 0.15f, 0.15f};  // track / unlit segment tint
    float fillColor[3] = {0.9f, 0.9f, 0.9f};   // fill / lit segment tint
    int segments = 5;                // quantized only (2..16)
    float spacing = 6.0f;            // quantized: gap between segments, px
    // Optional segment sprite (quantized): imagePath "" = colored rects.
    // Reuses HudImage so the pow2/quant bake controls and texbake apply; its
    // pos/size are ignored (segments are laid out from the bar's pos/size).
    HudImage segImage;
};

inline bool operator==(const LoadingBar& a, const LoadingBar& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.kind == b.kind && a.pos[0] == b.pos[0] &&
           a.pos[1] == b.pos[1] && a.size[0] == b.size[0] &&
           a.size[1] == b.size[1] && eq3(a.bgColor, b.bgColor) &&
           eq3(a.fillColor, b.fillColor) && a.segments == b.segments &&
           a.spacing == b.spacing && a.segImage == b.segImage;
}

// A named loading screen (Tools > Loading Screens): what the game shows while
// a scene loads. Scenes pick one by name (SceneData::loadingScreen); unnamed
// scenes use Project::defaultLoadingScreen; with neither, the game falls back
// to the classic built-in screen (hud/loading.png centered on black).
struct LoadingScreenDef {
    std::string name = "loading";
    float bgColor[3] = {0.0f, 0.0f, 0.0f};  // clear color behind everything
    std::vector<HudImage> images;  // PNG sprites, drawn in order
    std::vector<HudText> texts;    // baked text sprites (visibleAtStart unused)
    std::vector<LoadingBar> bars;  // progress bars, drawn on top
};

inline bool operator==(const LoadingScreenDef& a, const LoadingScreenDef& b) {
    return a.name == b.name && a.bgColor[0] == b.bgColor[0] &&
           a.bgColor[1] == b.bgColor[1] && a.bgColor[2] == b.bgColor[2] &&
           a.images == b.images && a.texts == b.texts && a.bars == b.bars;
}

// A boot splash screen (Tools > Loading Screens > Boot splash): a single image
// shown for `duration` seconds during startup, after the engine's Tyra logo
// and before the loading screen. Splashes play in list order. Images only for
// now. Independent of the loading-screen master toggle - splashes always show
// when defined. Reuses HudImage so import + pow2/quant bake apply.
struct SplashScreen {
    std::string name = "splash";
    HudImage image;                         // the graphic (fullscreen by default)
    float bgColor[3] = {0.0f, 0.0f, 0.0f};  // behind the image (letterbox)
    float duration = 2.0f;                  // seconds shown (0.1 .. 10)
};

inline bool operator==(const SplashScreen& a, const SplashScreen& b) {
    return a.name == b.name && a.image == b.image &&
           a.bgColor[0] == b.bgColor[0] && a.bgColor[1] == b.bgColor[1] &&
           a.bgColor[2] == b.bgColor[2] && a.duration == b.duration;
}

// A named editor window layout (docking arrangement), stored per project and
// switchable from the Layout menu. `ini` is an ImGui docking dump
// (SaveIniSettingsToMemory); when it is empty and `recipe` >= 0 the layout is
// (re)built programmatically from a built-in DockBuilder recipe the first time
// it is shown - this is how the seeded built-ins (Default/Director/Material)
// start life without needing an ImGui context at project-create time.
// `openWindows` lists the optional editor windows (Cutscene Director, Material
// Editor, ...) that must be open for the layout to make sense; switching to the
// layout opens them, and saving the layout captures whichever are currently
// open. Layouts are editor state, not game data or undo history.
struct WindowLayout {
    std::string name;
    std::string ini;                       // ImGui docking dump; empty = use recipe
    int recipe = -1;                       // -1 none, 0 default, 1 director, 2 material
    std::vector<std::string> openWindows;  // optional-window keys (see App::layoutWindowKeys)
};

// Built-in DockBuilder recipe ids (WindowLayout::recipe). The arrangement lives
// in App::buildLayoutRecipe; only the id travels in the .tyra file.
enum class LayoutRecipe { None = -1, Default = 0, Director = 1, Material = 2 };

// One custom screen effect placed in the screen stack. The effect body lives
// in a <project>/screen-effects/<stem>.screenfx file (loaded into
// customScreenEffects()); this is the project-side record: which effect, where
// in the stack, and its param values. See CustomScreenFx in screenfx.hpp.
struct ScreenFxPlacement {
    std::string key;             // "custom:<file-stem>"
    int layer = -1;              // stack slot, like Project::hudBloomLayer
    bool enabled = true;         // unchecked = kept but not composited/generated
    float params[4] = {0, 0, 0, 0};
};

inline bool operator==(const ScreenFxPlacement& a, const ScreenFxPlacement& b) {
    return a.key == b.key && a.layer == b.layer && a.enabled == b.enabled &&
           a.params[0] == b.params[0] && a.params[1] == b.params[1] &&
           a.params[2] == b.params[2] && a.params[3] == b.params[3];
}

// A streaming layer: a named group of scene objects that the game can load
// into / evict from memory at runtime (Load Layer / Unload Layer flow nodes)
// - GTA3-style interior streaming. Also doubles as an editor visibility
// group (the eye toggle hides the layer's objects while editing).
struct SceneLayer {
    std::string name = "Layer";
    bool startLoaded = true;    // game: resident when the scene starts
    bool editorVisible = true;  // editor-only: draw/pick the objects

    // Auto-streaming (opt-in): the game loads this layer while the player is
    // within streamRadius of (streamX, streamZ) and unloads it once they
    // leave radius + hysteresis - GTA-style zone streaming without wiring
    // the flow graph. Requests are edge-triggered (issued only when the
    // player crosses the boundary), so Load/Unload Layer nodes can still
    // override until the next crossing. When enabled, the initial residency
    // comes from the spawn distance, not startLoaded.
    bool autoStream = false;
    float streamX = 0.0f, streamZ = 0.0f;  // zone center, world units
    float streamRadius = 60.0f;            // load within this range
};

inline bool operator==(const SceneLayer& a, const SceneLayer& b) {
    return a.name == b.name && a.startLoaded == b.startLoaded &&
           a.editorVisible == b.editorVisible && a.autoStream == b.autoStream &&
           a.streamX == b.streamX && a.streamZ == b.streamZ &&
           a.streamRadius == b.streamRadius;
}

// A scene: its own objects (each with its flow graph), its own terrain
// (size, heightmap, texture) and its own lighting. Sky, physics prefs, HUD
// and audio assets are shared; the game starts in the first scene and
// switches via the Switch Scene flow node.
struct SceneData {
    std::string name = "main";
    std::vector<SceneObject> objects;
    std::vector<SceneLayer> layers;

    TerrainConfig terrain;
    // Heightmap: vertex heights on the render grid (row-major, hmW x hmD).
    // Empty = flat. Persisted in <project>/terrain-<scene>.heights.
    std::vector<float> heights;
    int hmW = 0, hmD = 0;

    // Per-scene overrides of the project's scene-visual settings. `settings`
    // holds this scene's values; `overrides` says which categories are active.
    // Inactive categories inherit Project::settings - resolve with
    // project::resolvedSettings(); never read these fields directly for the
    // final value (an inactive category's stored values are stale).
    ProjectSettings settings;
    SceneOverrides overrides;

    // Ambience preset this scene uses (name into Project::ambiencePresets).
    // Empty = the project default preset (Project::defaultAmbience). When a
    // preset resolves, project::resolvedSettings overlays its sky/lighting/fog
    // over this scene's settings.
    std::string ambiencePreset;

    // Loading screen shown while this scene loads (name into
    // Project::loadingScreens). Empty = the project default
    // (Project::defaultLoadingScreen); a dangling name also falls back there.
    std::string loadingScreen;
};

inline bool operator==(const SceneData& a, const SceneData& b) {
    return a.name == b.name && a.objects == b.objects && a.layers == b.layers &&
           a.terrain.width == b.terrain.width && a.terrain.depth == b.terrain.depth &&
           a.heights == b.heights && a.hmW == b.hmW && a.hmD == b.hmD &&
           a.overrides == b.overrides && a.settings == b.settings &&
           a.ambiencePreset == b.ambiencePreset &&
           a.loadingScreen == b.loadingScreen;
}

// One selectable row of a generated in-game menu.
struct MenuEntry {
    std::string label = "New entry";
    // What Cross does on this row. Close/scene/save-menu also dismiss the
    // menu; set/add value, events and toggles/choices keep it open (add a
    // Close entry).
    enum Action {
        Close = 0,       // dismiss the menu (a title screen's "Start")
        SwitchScene = 1, // param = scene name
        OpenSaveMenu = 2,
        OpenMenu = 3,    // param = menu name (submenu; Triangle goes back)
        SetValue = 4,    // param = save value name, amount = new value
        AddValue = 5,    // param = save value name, amount = delta
        FlowEvent = 6,   // param = event name (fires On Menu Event triggers)
        // Stateful rows. The state lives in a save value (param = its name):
        // the value holds the option index, the save value's default is the
        // initial state, and flow graphs react through the pure bool sources
        // (Value At Least -> On Condition). Cross / dpad right cycle forward,
        // dpad left backward. The current option label renders right-aligned
        // on the row from the baked value strip (menubake).
        Toggle = 7,      // two options, "Off"/"On" unless customized
        Choice = 8,      // one of `options`, cycled in order
    };
    int action = Close;
    std::string param;
    float amount = 0.0f;
    // Toggle/Choice option labels (value = index into this list). Toggle
    // treats an empty list as {"Off", "On"}.
    std::vector<std::string> options;
};

inline bool operator==(const MenuEntry& a, const MenuEntry& b) {
    return a.label == b.label && a.action == b.action && a.param == b.param &&
           a.amount == b.amount && a.options == b.options;
}

// One image composited into a menu's baked panel (see GameMenu::images).
struct MenuImage {
    std::string path;  // PNG, e.g. "res/hud/logo.png"
    enum Slot {
        AboveTitle = 0,   // block in the vertical flow, above the title
        AboveEntries = 1, // between the title and the entry rows
        BelowEntries = 2, // under the entry rows, above the button hints
        Background = 3,   // stretched under everything (dark wash on top)
        Overlay = 4,      // in front of the text, freeform offset position
    };
    int slot = AboveTitle;
    float scale = 1.0f;              // on top of the fit-to-panel size
    float offset[2] = {0.0f, 0.0f};  // px nudge; Overlay: top-left position
};

inline bool operator==(const MenuImage& a, const MenuImage& b) {
    return a.path == b.path && a.slot == b.slot && a.scale == b.scale &&
           a.offset[0] == b.offset[0] && a.offset[1] == b.offset[1];
}

// A generated in-game menu. The editor bakes the whole panel (title, entry
// labels, button hints - the engine has no font) into res/menus/<name>.png
// on every build; the game pauses while one is open. Navigation: dpad,
// Cross = select, Triangle = back/close.
struct GameMenu {
    std::string name = "menu";
    std::string title = "MENU";
    bool titleScreen = false;  // opens automatically at game start
    // Gameplay freezes while the menu is open (plus a dim overlay). false =
    // the menu floats over the running game; pad presses reach both.
    bool pauseGame = true;
    // The Start button opens this menu in-game and closes it again (the
    // classic pause menu; one per project).
    bool pauseMenu = false;
    float accent[3] = {0.47f, 0.82f, 1.0f};  // border/title tint
    // Images composited into the baked panel. Flow slots (AboveTitle /
    // AboveEntries / BelowEntries) are blocks in the panel's vertical flow -
    // list order = stacking order within a slot; Background stretches under
    // everything (with a dark wash); Overlay draws in front of the text at
    // a freeform offset.
    std::vector<MenuImage> images;
    // Panel placement: texture width (pow2 - PS2 requirement) and the
    // normalized screen position of the panel center (like HUD images).
    int panelW = 256;  // 128 / 256 / 512
    float screenPos[2] = {0.5f, 0.45f};
    bool showTitle = true;  // off = logo-only menus (skips title + separator)
    // Baked text: "" = default (Consolas Bold chain), "res/fonts/x.ttf" = a
    // font imported into the project, bare "impact.ttf" = a Windows font.
    std::string fontPath;
    int titleSize = 18;  // px; entrySize also drives the row pitch (and the
    int entrySize = 15;  // cursor geometry) through menubake::panelLayout.
    std::vector<MenuEntry> entries;
};

inline bool operator==(const GameMenu& a, const GameMenu& b) {
    return a.name == b.name && a.title == b.title &&
           a.titleScreen == b.titleScreen && a.pauseGame == b.pauseGame &&
           a.pauseMenu == b.pauseMenu && a.accent[0] == b.accent[0] &&
           a.accent[1] == b.accent[1] && a.accent[2] == b.accent[2] &&
           a.images == b.images && a.panelW == b.panelW &&
           a.screenPos[0] == b.screenPos[0] && a.screenPos[1] == b.screenPos[1] &&
           a.showTitle == b.showTitle && a.fontPath == b.fontPath &&
           a.titleSize == b.titleSize && a.entrySize == b.entrySize &&
           a.entries == b.entries;
}

// A named value persisted on the memory card (project-wide, not per scene).
// Flow graph Save nodes read/write these; every save slot stores a snapshot.
struct SaveValue {
    std::string name;
    float value = 0.0f;  // starting value on a fresh game
};

inline bool operator==(const SaveValue& a, const SaveValue& b) {
    return a.name == b.name && a.value == b.value;
}

// A named text persisted on the memory card (same lifecycle as SaveValue;
// stored in fixed 32-byte slots in the save payload, so keep them short).
struct SaveTextValue {
    std::string name;
    std::string value;  // starting text on a fresh game
};

inline bool operator==(const SaveTextValue& a, const SaveTextValue& b) {
    return a.name == b.name && a.value == b.value;
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
    // The USE prompt as an overridable HUD element (see defaultUsePrompt).
    // Always present - the UI Editor edits it but cannot delete it.
    HudImage usePrompt = defaultUsePrompt();
    // On-screen texts baked to sprites at build, triggered by the Show Text /
    // Hide Text flow nodes (Tools > UI Editor > Texts).
    std::vector<HudText> hudTexts;
    // Where the full-screen post effects sit in the screen stack (Tools > UI
    // Editor). Bloom (with color grading) and film grain are placed
    // independently: the effect applies right before the HUD sprite at that
    // index, so sprites with a lower index get the effect and higher ones draw
    // crisp on top. -1 = apply at the very end of the frame, over everything
    // including menus (the classic behavior, and the default). Typical split:
    // bloom under the HUD so it does not blur the crosshair, grain at -1 as a
    // filmic overlay over the whole screen. Grading rides with bloom.
    int hudBloomLayer = -1;
    int hudGrainLayer = -1;
    // Custom screen effects placed in the screen stack (Tools > UI Editor).
    // Each placement references a <project>/screen-effects/*.screenfx file by
    // its key ("custom:<stem>") and carries the effect's per-placement param
    // values; `layer` has the same meaning as hudBloomLayer (index into `hud`,
    // -1 = topmost). Project-wide like bloom/grain (not per-scene), and not on
    // the undo stack (edited via saveAll, like the rest of the UI Editor).
    // Placements whose .screenfx file is missing on load are dropped.
    std::vector<ScreenFxPlacement> screenFx;
    // Music tracks (16-bit 22kHz stereo WAV in res/audio/), played via the
    // flow graph (Play Music / Stop Music / Set Music Volume actions).
    std::vector<std::string> music;
    // Per-track build-time conversion (Music panel, "PS2 build" controls):
    // the res/audio source stays untouched; after every build the Runner
    // re-converts the bin/audio copy the game actually streams. Lower rates
    // and mono are the knobs when music snags on a real console over the
    // network deploy (each halves the byte rate). Keyed by the music relPath;
    // absent entry = ship as-is. rate 0 = keep the source rate.
    struct MusicBuildOpt {
        int rate = 0;
        bool mono = false;
    };
    std::map<std::string, MusicBuildOpt> musicBuild;
    // Sound effects (16-bit 22kHz WAV in res/sfx/, converted to ADPCM by the
    // toolchain at build). One-shots via the flow graph Play Sound action.
    std::vector<std::string> sounds;
    // Custom values persisted in memory card saves (Project panel, Save data).
    std::vector<SaveValue> saveValues;
    // Custom text values persisted in memory card saves (same panel).
    std::vector<SaveTextValue> saveTexts;
    // Per-asset texture-quality overrides of ProjectSettings::textureQuant,
    // keyed by asset path (a res/models .obj or a .mtl library): "none" /
    // "8bit" / "4bit". Textures referenced by several assets take the
    // HIGHEST requested quality - e.g. everything 4-bit, but the hero model
    // pinned to "none" keeps its textures full color.
    std::map<std::string, std::string> textureQuality;
    // In-game menus (Project panel, Menus): panels baked at build, opened by
    // the Open Menu flow node, menu entries, or at boot (titleScreen).
    std::vector<GameMenu> menus;
    // Color grading presets (Tools > Color Grading): project-wide looks
    // applied as GS full-screen passes. defaultGrading is the index applied
    // at game boot (-1 = none); the Set Color Grading flow node switches
    // presets at runtime (the switch persists across scene changes).
    std::vector<ColorGradingPreset> gradings;
    int defaultGrading = -1;
    // Ambience presets (Tools > Ambience Editor): project-wide sky/lighting/fog
    // "mood" bundles. defaultAmbience is the preset a scene uses when it names
    // none (-1 = fall back to the raw project/scene settings). A scene picks
    // its preset by name (SceneData::ambiencePreset); the Set Ambience flow
    // node repaints the sky at runtime.
    std::vector<AmbiencePreset> ambiencePresets;
    int defaultAmbience = -1;
    // Loading screens (Tools > Loading Screens): project-wide definitions of
    // what shows while a scene loads. defaultLoadingScreen is the one used by
    // scenes that name none (-1 = the classic built-in hud/loading.png on
    // black). A scene picks its screen by name (SceneData::loadingScreen).
    // The ProjectSettings::loadingScreen bool stays the master enable.
    std::vector<LoadingScreenDef> loadingScreens;
    int defaultLoadingScreen = -1;
    // Boot splash screens (Tools > Loading Screens > Boot splash): images shown
    // in order at startup, after the Tyra logo, before the loading screen.
    std::vector<SplashScreen> splashScreens;
    // Cutscene Director sequences (Tools > Cutscene Director): project-wide
    // keyframe timelines that pose scene objects + the camera over time. Like
    // the preset collections above they persist through save() but are not part
    // of undo/redo. The Play/Stop Sequence flow nodes drive them at runtime.
    std::vector<Sequence> sequences;

    // --- Editor-side state, persisted in the .tyra project file ------------
    // Not game data and not part of undo/redo (undo lives in the history
    // file). Restores the editing session on reopen.
    int selectedObject = -1;   // selected object in the active scene (-1 none)
    int gizmoOp = 0;           // transform gizmo: 0 move, 1 rotate, 2 scale
    int gizmoSpace = 0;        // gizmo axes: 0 absolute (world), 1 camera-relative
    int viewMode = 0;          // viewport shading: 0 solid, 1 wire, 2 wire+solid
    // Named window layouts (docking arrangements), switchable from the Layout
    // menu and edited by simply rearranging windows. Every project keeps at
    // least one; seedBuiltinLayouts() fills a fresh/legacy project with the
    // Default/Director/Material built-ins. activeLayout indexes into this list.
    std::vector<WindowLayout> windowLayouts;
    int activeLayout = 0;
    // Emulator path + dev-PS2 IP. These are machine-global editor settings
    // (stored in editor.ini, edited in Edit > Preferences), NOT persisted in the
    // .tyra file. They live on Project only as the runtime transport the Runner
    // reads: App::attachProject copies the global values in on every open. The
    // headless --build path (main.cpp) also sets ps2LinkIp here directly.
    std::string emulatorPath;  // PCSX2 exe; empty = auto-detect under Program Files
    std::string ps2LinkIp;     // ps2link IP for "Run on PS2"; empty = disabled

    bool valid() const { return !name.empty() && !dir.empty(); }
    std::string elfName() const { return name + ".elf"; }
    std::string elfPath() const { return dir + "\\bin\\" + elfName(); }
};

namespace project {

// Creates the project directory, generates all Tyra game sources / build files
// and the <name>.tyra project file. `preset` picks the starting content:
//   "empty" - orbit camera, no objects.
//   "fpp"   - FPP game template with a single Player entity in the center.
// Returns empty string on success, error message otherwise.
std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& preset = "empty");

// Fills p.windowLayouts with the three built-in layouts (Default, Director,
// Material Designer) as recipe-backed entries with empty ini, and resets
// activeLayout to 0. Used for fresh projects and to migrate older projects that
// predate named layouts. A pre-existing single "layout" dump can be preserved
// by the caller by assigning it into windowLayouts[0].ini after seeding.
void seedBuiltinLayouts(Project& p);

// A fresh opaque object id (16 hex chars from a 64-bit random value). Unique
// within a project with negligible collision odds; the merge/file-split layout
// keys on it. See SceneObject::id.
std::string newObjectId();

// Assigns a stable id to every scene object that lacks one (empty id, e.g. an
// object from a pre-id project or a fresh paste), and repairs any accidental
// duplicate so ids stay unique project-wide. Idempotent - a no-op once every
// object already has a distinct id. Called on load, on create, and from the
// editor's commitChange() so no object is ever persisted without an id.
void ensureObjectIds(Project& p);

// The effective settings for a scene: the project defaults with each scene
// category (lighting, sky, clipping, terrain material, post-FX, highlight)
// replaced by the scene's own values where its override flag is set. All
// codegen and viewport code reads scene-visual settings through this.
ProjectSettings resolvedSettings(const Project& p, const SceneData& s);

// Index into Project::ambiencePresets of the preset a scene resolves to:
// its named preset if it exists, otherwise the project default. -1 = none
// (no presets, or a dangling/empty name with no default).
int ambienceIndexFor(const Project& p, const SceneData& s);

// Index into Project::loadingScreens of the screen a scene resolves to: its
// named screen if it exists, otherwise the project default. -1 = the built-in
// fallback screen (hud/loading.png centered on black).
int loadingScreenIndexFor(const Project& p, const SceneData& s);

// A terrain material resolved to what the terrain actually needs.
struct TerrainMaterial {
    bool present = false;            // false = no material -> checker greens
    std::string texture;             // res-relative map_Kd ("" = flat color)
    float kd[3] = {1.0f, 1.0f, 1.0f};  // tint (defaults to white)
    float tile[2] = {1.0f, 1.0f};    // texture repeats per world unit (u, v)
};

// Resolves a terrain material (.mtl asset, res-relative, e.g. from
// resolvedSettings(...).terrainMaterial) to its first material's map_Kd
// texture, Kd tint and "-s" tiling. `present` is false when unassigned or the
// .mtl is unreadable. Codegen, the editor viewport and the ISO planner resolve
// through this so they agree on the terrain's texture, color and tiling.
TerrainMaterial resolveTerrainMaterial(const Project& p, const std::string& matRel);

// Loads the single <name>.tyra project file from an existing project
// directory (game data + editor-side state + window layout).
std::string load(Project& out, const std::string& projectDir);

// Writes the single <name>.tyra project file. Editor-side state (selection,
// gizmo, view mode) and the window layout are taken from the Project fields.
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

// Level the terrain toward targetH under the brush (same falloff; strength
// is the per-stroke lerp rate, 0..1).
void flattenHeightmap(Project& p, float worldX, float worldZ, float radius, float targetH,
                      float strength);

std::string saveHeights(const Project& p);
void loadHeights(Project& p);  // silent no-op when the file is absent

// --- History file (<name>.history) ------------------------------------------
// The undo history (up to History::kMaxEntries scene snapshots), kept next to
// the project file. Churny disposable editor state - gitignored in generated
// projects; the .tyra project file is the tracked source of truth.

std::string saveHistory(const Project& p, const History& h);

// Restores the undo history. Returns an error string when the file is
// missing/malformed/stale - the caller should then start a fresh history.
std::string loadHistory(const Project& p, History& h);

// Rewrites editor-owned files from the current templates and project data:
// docker infra (Dockerfile, docker-compose.yml) and generated headers
// (terrain_config.hpp, scene_data.hpp) are always rewritten; game sources
// are rewritten only while they still carry the "Generated by TyraX"
// marker in the first line (delete it to take ownership of a file).
// Called before every build.
std::string refreshGenerated(const Project& p);

}  // namespace project
