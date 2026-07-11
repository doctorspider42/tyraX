#pragma once

#include <map>
#include <string>
#include <vector>

#include "flowgraph.hpp"
#include "grading.hpp"

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
};

// Unit primitives fit a 1x1x1 cube centered at origin and are transformed by
// scale -> rotation (X, then Y, then Z) -> translation.
struct SceneObject {
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
    // Rendering cut-off: farther than this from the camera the object is not
    // drawn at all (collision, sounds and scripts still run). 0 = unlimited.
    // The cheapest LOD there is - era-correct for dense scenes.
    float drawDistance = 0.0f;
    std::string modelPath;    // for PrimitiveType::Model, e.g. "res/models/tree.obj"
    // Material library (.mtl) assigned to the object, e.g.
    // "res/materials/walls.mtl". Primitives take the file's FIRST material
    // (Kd + map_Kd applied on their UVs); models use it as an override that
    // replaces their own mtl (usemtl names resolve against it). Empty =
    // plain color (primitives) / the model's own materials.
    std::string materialPath;

    // Player entity parameters (used when type == Player)
    int playerMode = 0;            // 0 = walk (FPP), 1 = noclip (fly)
    float playerWalkSpeed = 0.4f;  // units per frame at full stick
    float playerLookSpeed = 1.0f;  // multiplier
    float playerEyeHeight = 1.8f;
    float playerJumpSpeed = 4.5f;  // units/s (walk mode, X button)
    bool playerCanJump = true;     // walk mode: X jumps

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
    return a.name == b.name && a.type == b.type && eq3(a.position, b.position) &&
           eq3(a.rotation, b.rotation) && eq3(a.scale, b.scale) && eq3(a.color, b.color) &&
           a.physics == b.physics && a.usable == b.usable &&
           a.saveState == b.saveState && a.collisionMode == b.collisionMode &&
           a.drawDistance == b.drawDistance && a.modelPath == b.modelPath &&
           a.materialPath == b.materialPath && a.playerMode == b.playerMode &&
           a.playerWalkSpeed == b.playerWalkSpeed &&
           a.playerLookSpeed == b.playerLookSpeed &&
           a.playerEyeHeight == b.playerEyeHeight &&
           a.playerJumpSpeed == b.playerJumpSpeed &&
           a.playerCanJump == b.playerCanJump && a.emitterKind == b.emitterKind &&
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

    // Texture quantization at build (the PS2-native "compression": palettized
    // PSMT8/PSMT4 textures). Applied to res/models|materials|textures PNGs
    // when baking res/ -> .res-baked/; sources stay untouched. Per-asset
    // overrides live in Project::textureQuality. "none" = full color,
    // "8bit" = 256 colors, "4bit" = 16 colors (default - era-correct).
    std::string textureQuant = "4bit";
    bool showFps = false;     // debug profile only: on-screen FPS counter
    bool showMemory = false;  // debug profile only: on-screen free-RAM readout

    // Experimental: skip the vsync wait before the buffer flip. Frame rate
    // becomes continuous instead of quantized to 50/25 (PAL), at the cost
    // of screen tearing. Gameplay speed is unaffected either way - the
    // generated game measures real frame time (see updateFrameClock).
    bool disableVsync = false;

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

    // Analog sticks: offsets below this fraction of full deflection read as
    // zero (real DualShock sticks rest off-center); motion rescales smoothly
    // from the deadzone edge. Per stick - worn pads rarely drift equally.
    float stickDeadzoneL = 0.2f;  // 0..0.9, left stick (movement)
    float stickDeadzoneR = 0.2f;  // 0..0.9, right stick (camera)

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

    // Post effects (GS framebuffer blits at the end of every frame; no
    // pixel shaders on the PS2). 0 = off, 1 = maximum.
    float bloom = 0.0f;  // downsample + blur + additive re-add (glow)
    float grain = 0.0f;  // animated film grain noise overlay

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
};

inline bool operator==(const ProjectSettings& a, const ProjectSettings& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.videoSystem == b.videoSystem && a.buildProfile == b.buildProfile &&
           a.showFps == b.showFps && a.showMemory == b.showMemory &&
           a.disableVsync == b.disableVsync &&
           a.clipping == b.clipping && a.terrainDetail == b.terrainDetail &&
           eq3(a.skyColor, b.skyColor) && eq3(a.skyTopColor, b.skyTopColor) &&
           a.skyDome == b.skyDome && a.eyeHeight == b.eyeHeight &&
           a.walkSpeed == b.walkSpeed && a.lookSpeed == b.lookSpeed &&
           a.stickDeadzoneL == b.stickDeadzoneL &&
           a.stickDeadzoneR == b.stickDeadzoneR &&
           a.orbitSpeed == b.orbitSpeed && a.gravity == b.gravity &&
           a.jumpSpeed == b.jumpSpeed && eq3(a.lightDir, b.lightDir) &&
           a.ambient == b.ambient && a.diffuse == b.diffuse &&
           eq3(a.lightColor, b.lightColor) && a.brightness == b.brightness &&
           a.terrainTexture == b.terrainTexture &&
           a.terrainTexScale == b.terrainTexScale && a.bloom == b.bloom &&
           a.grain == b.grain && a.loadingScreen == b.loadingScreen &&
           a.highlightUsable == b.highlightUsable &&
           a.highlightDistance == b.highlightDistance &&
           eq3(a.highlightColor, b.highlightColor) &&
           a.highlightWidth == b.highlightWidth &&
           a.highlightSteps == b.highlightSteps;
}

// Per-scene override switches (Scene > Preferences). Each "scene-visual"
// category can override the project defaults; when a flag is off, the scene
// inherits Project::settings for that category (see project::resolvedSettings).
// Camera, physics, terrain detail and the game template stay project-wide.
struct SceneOverrides {
    bool lighting = false;    // lightDir, ambient, diffuse, lightColor, brightness
    bool sky = false;         // skyColor, skyTopColor, skyDome
    bool clipping = false;    // clipping mode
    bool terrainTex = false;  // terrainTexture, terrainTexScale
    bool postFx = false;      // bloom, grain
    bool highlight = false;   // highlightUsable + distance/color/width/steps
};

inline bool operator==(const SceneOverrides& a, const SceneOverrides& b) {
    return a.lighting == b.lighting && a.sky == b.sky && a.clipping == b.clipping &&
           a.terrainTex == b.terrainTex && a.postFx == b.postFx &&
           a.highlight == b.highlight;
}

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

    // Per-scene overrides of the project's scene-visual settings. `settings`
    // holds this scene's values; `overrides` says which categories are active.
    // Inactive categories inherit Project::settings - resolve with
    // project::resolvedSettings(); never read these fields directly for the
    // final value (an inactive category's stored values are stale).
    ProjectSettings settings;
    SceneOverrides overrides;
};

inline bool operator==(const SceneData& a, const SceneData& b) {
    return a.name == b.name && a.objects == b.objects &&
           a.terrain.width == b.terrain.width && a.terrain.depth == b.terrain.depth &&
           a.heights == b.heights && a.hmW == b.hmW && a.hmD == b.hmD &&
           a.overrides == b.overrides && a.settings == b.settings;
}

// One selectable row of a generated in-game menu.
struct MenuEntry {
    std::string label = "New entry";
    // What Cross does on this row. Close/scene/save-menu also dismiss the
    // menu; set/add value and events keep it open (add a Close entry).
    enum Action {
        Close = 0,       // dismiss the menu (a title screen's "Start")
        SwitchScene = 1, // param = scene name
        OpenSaveMenu = 2,
        OpenMenu = 3,    // param = menu name (submenu; Triangle goes back)
        SetValue = 4,    // param = save value name, amount = new value
        AddValue = 5,    // param = save value name, amount = delta
        FlowEvent = 6,   // param = event name (fires On Menu Event triggers)
    };
    int action = Close;
    std::string param;
    float amount = 0.0f;
};

inline bool operator==(const MenuEntry& a, const MenuEntry& b) {
    return a.label == b.label && a.action == b.action && a.param == b.param &&
           a.amount == b.amount;
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

    // --- Editor-side state, persisted in the .tyra project file ------------
    // Not game data and not part of undo/redo (undo lives in the history
    // file). Restores the editing session on reopen.
    int selectedObject = -1;   // selected object in the active scene (-1 none)
    int gizmoOp = 0;           // transform gizmo: 0 move, 1 rotate, 2 scale
    int gizmoSpace = 0;        // gizmo axes: 0 absolute (world), 1 camera-relative
    int viewMode = 0;          // viewport shading: 0 solid, 1 wire, 2 wire+solid
    std::string windowLayout;  // ImGui docking layout (SaveIniSettingsToMemory)
    // Absolute path to the PCSX2 executable to launch (Project > Preferences).
    // Empty = auto-detect under Program Files. Editor-side, not game data.
    std::string emulatorPath;
    // IP of a PS2 running ps2link, for "Run on PS2" network deploys
    // (Project > Preferences). Empty = the menu entries stay disabled.
    std::string ps2LinkIp;

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

// The effective settings for a scene: the project defaults with each scene
// category (lighting, sky, clipping, terrain texture, post-FX, highlight)
// replaced by the scene's own values where its override flag is set. All
// codegen and viewport code reads scene-visual settings through this.
ProjectSettings resolvedSettings(const Project& p, const SceneData& s);

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
// are rewritten only while they still carry the "Generated by tyra-editor"
// marker in the first line (delete it to take ownership of a file).
// Called before every build.
std::string refreshGenerated(const Project& p);

}  // namespace project
