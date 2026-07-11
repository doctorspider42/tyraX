#include "templates.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "glbparser.hpp"
#include "menubake.hpp"
#include "project.hpp"

namespace templates {

static std::vector<std::string> collectMenuEvents(const Project& p);

// Unique (modelPath, material override) pairs referenced by static .obj
// model objects, in first-use order. A material override changes what gets
// loaded, so the PAIR is the model identity: the index in this list ==
// SceneObjectData::model in the generated code. Animated .glb models take
// the separate ANIM_MODEL path (collectAnimModelPaths).
static std::vector<std::pair<std::string, std::string>> collectModelKeys(
    const Project& p) {
    std::vector<std::pair<std::string, std::string>> keys;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            if (o.type != PrimitiveType::Model || o.modelPath.empty() ||
                isAnimatedModelPath(o.modelPath))
                continue;
            const std::pair<std::string, std::string> key{o.modelPath, o.materialPath};
            bool seen = false;
            for (const auto& e : keys) seen |= (e == key);
            if (!seen) keys.push_back(key);
        }
    return keys;
}

static int modelIndexOf(const Project& p, const SceneObject& o) {
    if (o.type != PrimitiveType::Model || o.modelPath.empty() ||
        isAnimatedModelPath(o.modelPath))
        return -1;
    const auto keys = collectModelKeys(p);
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i].first == o.modelPath && keys[i].second == o.materialPath)
            return (int)i;
    return -1;
}

// Unique .glb paths referenced by animated model objects, first-use order.
// The index == SceneObjectData::animModel == the ANIM_MODEL_PATHS slot of
// the baked .tanm. Identity is the path alone (no .mtl overrides for .glb -
// materials come from the file itself).
static std::vector<std::string> collectAnimModelPaths(const Project& p) {
    std::vector<std::string> paths;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            if (o.type != PrimitiveType::Model || !isAnimatedModelPath(o.modelPath))
                continue;
            bool seen = false;
            for (const auto& e : paths) seen |= (e == o.modelPath);
            if (!seen) paths.push_back(o.modelPath);
        }
    return paths;
}

static int animModelIndexOf(const Project& p, const SceneObject& o) {
    if (o.type != PrimitiveType::Model || !isAnimatedModelPath(o.modelPath))
        return -1;
    const auto paths = collectAnimModelPaths(p);
    for (size_t i = 0; i < paths.size(); ++i)
        if (paths[i] == o.modelPath) return (int)i;
    return -1;
}

// C string literal escaping for clip names baked into scene_data.hpp.
static std::string escapeCString(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if ((unsigned char)c >= 0x20) out += c;
    }
    return out;
}

// Unique .mtl paths assigned to non-model solid objects (primitives take the
// file's first material). The index == SceneObjectData::material.
static std::vector<std::string> collectMaterialPaths(const Project& p) {
    std::vector<std::string> paths;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            if (o.type == PrimitiveType::Model || o.materialPath.empty()) continue;
            bool seen = false;
            for (const auto& e : paths) seen |= (e == o.materialPath);
            if (!seen) paths.push_back(o.materialPath);
        }
    return paths;
}

static int materialIndexOf(const Project& p, const SceneObject& o) {
    if (o.type == PrimitiveType::Model || o.materialPath.empty()) return -1;
    const auto paths = collectMaterialPaths(p);
    for (size_t i = 0; i < paths.size(); ++i)
        if (paths[i] == o.materialPath) return (int)i;
    return -1;
}

// Unique texture paths (terrain only - objects are textured via materials),
// first-use order. The index == TERRAIN_TEXTURE in generated code.
static std::vector<std::string> collectTexturePaths(const Project& p) {
    std::vector<std::string> paths;
    auto add = [&](const std::string& t) {
        if (t.empty()) return;
        for (const auto& e : paths)
            if (e == t) return;
        paths.push_back(t);
    };
    for (const SceneData& sc : p.scenes)
        add(project::resolvedSettings(p, sc).terrainTexture);
    return paths;
}

static int textureIndexOf(const Project& p, const std::string& texturePath) {
    if (texturePath.empty()) return -1;
    const auto paths = collectTexturePaths(p);
    for (size_t i = 0; i < paths.size(); ++i)
        if (paths[i] == texturePath) return (int)i;
    return -1;
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// ---------------------------------------------------------------------------
// Tyra game project templates. Placeholders: {{NAME}}, {{WIDTH}}, {{DEPTH}}
// ---------------------------------------------------------------------------

static const char* TPL_MAKEFILE = R"(TARGET      := {{NAME}}.elf
ENGINEDIR	:= /tyra/engine

#The Directories, Source, Includes, Objects, Binary and Resources
SRCDIR      := src
INCDIR      := inc
BUILDDIR    := obj
TARGETDIR   := bin
# .res-baked is the texture-quantized mirror of res/ the editor writes at the
# start of every build (see Project > Preferences > Textures); sources in
# res/ stay full quality.
RESDIR      := .res-baked
SRCEXT      := cpp
VSMEXT		:= vsm
VCLEXT		:= vcl
VCLPPEXT	:= vclpp
DEPEXT      := d
OBJEXT      := o

#Flags, Libraries and Includes
CFLAGS      :=
LIB         := -ltyra
LIBDIRS     := -L$(ENGINEDIR)/bin
INC         := -I$(INCDIR) -I$(ENGINEDIR)/inc
INCDEP      := -I$(INCDIR) -I$(ENGINEDIR)/inc

include /tyra/Makefile.base
)";

static const char* TPL_DOCKERFILE = R"(# syntax=docker/dockerfile:1
FROM h4570/tyra

RUN apt-get update
RUN apt-get install git -y

WORKDIR /src
CMD ["/bin/bash"]
)";

// Per-project container (no fixed container_name - avoids conflicts between
// projects). The Tyra engine sources are maintained inside the editor repo
// (vendor/tyra) and bind-mounted read-only; the shared volume holds the
// compiled engine, synced+rebuilt by the Runner whenever the sources change.
// The volume name carries a hash of the engine source path: projects built
// from the same editor checkout share one compiled engine, while parallel
// checkouts (git worktrees, second clones) get their own - two checkouts
// sharing a volume rsync their diverging engines over each other on every
// build, endlessly rebuilding libtyra and racing mid-compile.
static const char* TPL_COMPOSE = R"(name: {{NAME_LOWER}}
volumes:
  tyra-game-volume:
  tyra-engine:
    name: tyra-engine-{{ENGINE_HASH}}
services:
  compiler:
    environment:
      TERM: xterm-256color
    network_mode: host
    build:
      context: ./
      dockerfile: Dockerfile
    tty: true
    volumes:
      - tyra-game-volume:/src
      - tyra-engine:/tyra
      - ./:/host
      - "{{ENGINE_SRC}}:/engine-src:ro"
)";

static const char* TPL_MAIN_CPP = R"(#include <tyra>
#include <cstdio>
#include <cstring>
#include "terrain_game.hpp"

int main(int argc, char** argv) {
  // "Run on PS2" launches this game over the network (ps2client execee).
  // ps2link stays resident on the IOP serving the host: filesystem, so the
  // Engine must not reset the IOP - that would kill it. Detection is
  // two-fold: the "-ps2link" execee argument (only delivered by toolchains
  // with a current crt0 - ps2link passes args in a non-standard way), plus
  // a "ps2link.run" marker the editor writes next to the ELF on PS2 deploys
  // and deletes on PCSX2 launches. The marker is read over host: BEFORE the
  // Engine boots: on a real PS2 host: only exists while ps2link is alive.
  bool ps2link = false;
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "-ps2link") == 0) ps2link = true;
  if (!ps2link) {
    if (FILE* marker = fopen(Tyra::FileUtils::fromCwd("ps2link.run").c_str(), "rb")) {
      fclose(marker);
      ps2link = true;
    }
  }
  Tyra::IrxLoader::keepIopResident = ps2link;

  // Route TYRA_LOG / TYRA_WARN / TYRA_ERROR and assertion dumps to a host-side
  // "log.txt" (next to the ELF) instead of the EE console, which does not
  // reach PCSX2's emulog. The tyra-editor Debug window tails that file. Must
  // be set before the Engine is constructed (its init logging is the first to
  // hit the file). No cost in a release (NDEBUG) build - the macros compile out.
  // Under ps2link the EE console is BETTER than the file: ps2link forwards
  // printf over the network and the editor shows it live in the Output panel.
  Tyra::Info::writeLogsToFile = !ps2link;

  Tyra::EngineOptions options;
  // The Engine(options) ctor re-applies this flag, so it must be set here
  // too or the static above gets reset to the default (console logging).
  options.writeLogsToFile = !ps2link;
  // Target system (Project > Preferences > Build): Auto follows the console
  // region, NTSC forces 60 Hz, PAL forces 50 Hz.
  options.videoMode = Tyra::VideoMode::{{VIDEO_MODE}};
  Tyra::Engine engine(options);
  {{NAME_UPPER_NS}}::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}
)";

static const char* TPL_TERRAIN_CONFIG_HPP =
    R"(// Generated by tyra-editor. Do not edit - regenerated on every build.
#pragma once

namespace {{NAME_UPPER_NS}} {

// Terrain size and lighting are per scene - see scene_data.hpp arrays and
// the TERRAIN_*/SCENE_* accessor macros defined there.

// Project preferences (Project > Preferences in the editor). These are
// project-wide; sky, clipping, post-FX and the usable-highlight can be
// overridden per scene and live as SCENE_COUNT arrays in scene_data.hpp
// (reached through the accessor macros defined in scene_data.hpp).
constexpr int TERRAIN_MAX_CELLS = {{DETAIL}};
constexpr float EYE_HEIGHT = {{EYE_HEIGHT}};
constexpr float WALK_SPEED = {{WALK_SPEED}};
constexpr float LOOK_SPEED = {{LOOK_SPEED}};    // multiplier
// Stick offsets below this fraction of full deflection read as zero
// (worn pads rest off-center); motion rescales smoothly above it.
// Per stick: left drives movement, right drives the camera.
constexpr float ANALOG_DEADZONE_L = {{DEADZONE_L}};
constexpr float ANALOG_DEADZONE_R = {{DEADZONE_R}};
constexpr float ORBIT_SPEED = {{ORBIT_SPEED}};  // multiplier
constexpr float GRAVITY = {{GRAVITY}};          // units/s^2
constexpr float JUMP_SPEED = {{JUMP_SPEED}};    // units/s

// Scene switches show res/hud/loading.png on black for a moment
constexpr bool LOADING_SCREEN = {{LOADING_SCREEN}};

// Experimental (Preferences > Build > Disable VSync): false skips the vsync
// wait before the flip - continuous frame rate, screen tearing possible.
constexpr bool FRAME_LIMIT = {{FRAME_LIMIT}};

// Debug-profile HUD (Project > Preferences > Build). Both are forced false
// in a release-profile build, which folds the overlay code away entirely.
constexpr bool DEBUG_SHOW_FPS = {{DEBUG_SHOW_FPS}};
constexpr bool DEBUG_SHOW_MEM = {{DEBUG_SHOW_MEM}};

}  // namespace {{NAME_UPPER_NS}}
)";

static const char* TPL_GAME_HPP_ORBIT =
    R"(// Generated by tyra-editor. Delete this line to take ownership of this file.
#pragma once

#include <tyra>
#include <memory>
#include <vector>
#include "save_system.gen.hpp"
#include "scripts/script.hpp"

namespace {{NAME_UPPER_NS}} {

class TerrainGame : public Tyra::Game {
 public:
  explicit TerrainGame(Tyra::Engine* engine);
  ~TerrainGame();

  void init() override;
  void loop() override;

 private:
  void buildScene();
  void generateTerrainGrid();
  void updateCameraOrbit();

  Tyra::Engine* engine;
  Tyra::StaticPipeline stapip;

  Tyra::Vec4 cameraPosition, cameraLookAt;
  float orbitAngle;

  std::vector<Tyra::Vec4> vertices;
  std::vector<Tyra::Color> colors;

  Tyra::M4x4 model;
  std::unique_ptr<Tyra::StaPipBag> bag;
  std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
  std::unique_ptr<Tyra::StaPipColorBag> colorBag;

  // Scene objects at runtime (mutable by scripts/physics); geometry per
  // object, one draw part per model material (primitives use parts[0])
  struct GeoPart {
    std::vector<Tyra::Vec4> vertices;
    std::vector<Tyra::Color> colors;
    std::vector<Tyra::Vec4> sts;  // texture coordinates
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
  };
  struct ObjectGeometry {
    std::vector<GeoPart> parts;
    // Animated models (.glb): this object's skeletal instance (own
    // playback state + skinned output mesh, samples the shared SkelModel).
    std::unique_ptr<Tyra::SkelInstance> animInst;
    // StaPip bags pointing straight into animInst's skinned arrays; the
    // skinned vertices stay in model space, so the object transform rides
    // in animMat (info bag + light matrix), not in the vertex data.
    struct AnimPart {
      std::unique_ptr<Tyra::StaPipBag> bag;
      std::unique_ptr<Tyra::StaPipColorBag> colorBag;
      std::unique_ptr<Tyra::StaPipTextureBag> texBag;
      std::unique_ptr<Tyra::StaPipLightingBag> lightBag;
    };
    std::vector<AnimPart> animParts;
    std::unique_ptr<Tyra::StaPipInfoBag> animInfoBag;
    Tyra::M4x4 animMat;
    // Usable-object highlight: fading shells grown around the object
    // center, drawn after the scene (see renderHighlightHull)
    std::vector<Tyra::Vec4> hullVerts;
    std::vector<Tyra::Color> hullCols;
    std::unique_ptr<Tyra::StaPipBag> hullBag;
    std::unique_ptr<Tyra::StaPipInfoBag> hullInfoBag;
    std::unique_ptr<Tyra::StaPipColorBag> hullColorBag;
  };
  // Custom .obj models, loaded once at startup (paths in model_data.gen.hpp):
  // geometry split per MTL material with optional per-material textures, the
  // real mesh AABB for box collision, a CollisionMesh for mesh collision.
  struct GameModelPart {
    std::vector<float> verts;  // 8 floats per vertex: x,y,z,nx,ny,nz,u,v
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
  };
  struct GameModel {
    std::vector<GameModelPart> parts;  // empty = missing/unparseable model
    float mn[3] = {-0.5F, -0.5F, -0.5F};
    float mx[3] = {0.5F, 0.5F, 0.5F};
    Tyra::CollisionMesh collider;  // built only when a scene needs mesh mode
  };
  std::vector<GameModel> gameModels;
  void loadModels();
  // Animated .glb models: serialized by the editor to .tskl skeletal files
  // (paths in model_data.gen.hpp) - bone keyframe tracks + bind-pose mesh.
  // Poses are evaluated and skinned on the EE/VU0 (SkelInstance) for the
  // in-view instances only; the skinned arrays render through the SAME
  // static pipeline as the rest of the scene (single vertex upload, no VU1
  // program swap, EE clipping). See updateAndRenderAnimObjects for the
  // real-hardware numbers behind this design - PCSX2's fast EE hides them.
  struct GameAnimModel {
    std::unique_ptr<Tyra::SkelModel> src;   // skeleton + mesh + clip tracks
    std::vector<Tyra::Texture*> textures;   // per part, nullptr = untextured
    Tyra::CoreBBox cullBox;  // local AABB over all clips + margin (see load)
  };
  std::vector<GameAnimModel> gameAnimModels;
  void loadAnimModels();
  void setupAnimObject(int index);  // per-object instance + playback state
  void updateAndRenderAnimObjects();
  // Directional light for the animated pass, mirroring the baked static
  // look. The manual dir-lights layout: colors[0..2] + ambient in [3].
  Tyra::Vec4 animLightColors[4];
  Tyra::Vec4 animLightDirs[3];
  Tyra::PipelineDirLightsBag animDirLights{true};

 public:
  // Clip-name lookup for scripts/flow graph (ScriptContext::resolveClip).
  int resolveClipIndex(int objectIndex, const char* clipName) const;

 private:
  // Primitive materials: .mtl assigned to a box/sphere/... - the file's
  // first material supplies the color (kd) and optional texture.
  struct GameMaterial {
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
  };
  std::vector<GameMaterial> gameMaterials;
  void loadMaterials();
  std::vector<Tyra::Texture*> loadedTextures;
  std::vector<Tyra::Vec4> terrainSts;
  Tyra::StaPipTextureBag terrainTexBag;
  std::vector<RuntimeObject> runtimeObjects;
  std::vector<ObjectGeometry> objectGeometry;
  GeoPart skyDome;
  float skyHorizonR = 0, skyHorizonG = 0, skyHorizonB = 0;
  std::vector<Tyra::Sprite> hudSprites;

  void buildSkyDome();
  void rebuildObjectGeometry(int index);
  // Player-vs-objects collision shared by both walkers: box (scale box or
  // model AABB), mesh (CollisionMesh) or none, per SceneObjectData.collision
  void collidePlayer(float prevX, float prevZ, float* nextX, float* nextZ,
                     float feetY, float eyeHeight, float* ground);
  void updateObjectPhysics();
  void renderScene();
  void renderHighlightHull(int index);

  // Player entity (PLAYER_INDEXES in scene_data.hpp); overrides the template
  // camera when present. Returns false when the scene has no player.
  bool updatePlayerEntity();
  float entX = 0, entY = 0, entZ = 0, entVelY = 0, entYaw = 0, entPitch = 0;

  // Multiple scenes: the game starts in scene 0; the flow graph Switch
  // Scene node requests a change applied between frames.
  void loadScene(int sceneIndex);
  int currentScene = 0;
  unsigned int sceneGeneration = 0;

  // Particle emitters (type 7): fixed pools sized at scene load, zero
  // per-frame allocations; camera-facing quads (textured when the emitter
  // has a material with a map_Kd), one bag per emitter.
  struct ParticleSystem {
    int objectIndex = -1;
    unsigned int rng = 1;
    std::vector<Tyra::Vec4> pos, vel;
    std::vector<float> life, maxLife;
    std::vector<Tyra::Vec4> verts;
    std::vector<Tyra::Color> cols;
    std::vector<Tyra::Vec4> sts;  // fixed per-quad UVs (textured emitters)
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
  };
  std::vector<ParticleSystem> particles;
  void buildParticles();
  void updateParticles();

  // Sound emitters (type 8): distance-attenuated one-shots on channels 16-23
  std::vector<audsrv_adpcm_t*> sndSamples;  // scene_data.hpp SND_PATHS order
  std::vector<int> sndTimers;               // per-object retrigger countdown
  void updateSoundEmitters();

  // Scene switches show res/hud/loading.png on black for a moment
  Tyra::Sprite loadingSprite;
  int loadingFrames = 0, loadingTarget = -1;

  // "Use" interaction: nearest usable object the camera looks at (controls.hpp)
  void updateUseTarget();
  int useTargetIndex = -1;
  Tyra::Sprite usePromptSprite;

  // Memory card save menu (save_system.gen.hpp): opened by using a Save
  // point object or the Open Save Menu flow node; gameplay pauses while
  // open. updateSaveMenu() returns true while it owns the pad.
  bool updateSaveMenu();
  void renderSaveMenu();
  void doSave(int slot);
  void doLoad(int slot);
  void applySavedObjects();
  void refreshSlotStates();
  std::vector<float> saveValues;
  std::vector<char> saveTexts;  // SAVE_TEXT_COUNT slots of SAVE_TEXT_LEN bytes
  std::vector<SaveObjectState> pendingObjState;  // applied after a scene load
  int pendingObjScene = -1;
  bool saveMenuOpen = false;
  int saveMenuSlot = 0;
  int saveMenuGrace = 0;  // frames to ignore pad input after opening
  bool slotUsed[SAVE_SLOTS] = {};
  int saveFeedback = 0, saveFeedbackFrames = 0;  // 1 saved, 2 loaded, 3 error
  Tyra::Sprite saveMenuSprite, saveCursorSprite, saveUsedSprite;
  Tyra::Sprite saveFeedbackSprites[3];  // saved / loaded / error

  // Game menus (menu_data.gen.hpp): panels baked by the editor, opened by
  // the Open Menu flow node, a menu entry, or at boot (title screen).
  // Gameplay pauses while one is open; Triangle walks the submenu stack.
  bool updateGameMenu();
  void renderGameMenu();
  std::vector<Tyra::Sprite> menuSprites;
  Tyra::Sprite menuCursorSprite;
  Tyra::Sprite menuDimSprite;  // fullscreen dim under pausing menus
  int gameMenuIndex = -1;
  int gameMenuCursor = 0;
  int gameMenuGrace = 0;
  int gameMenuStack[4] = {};
  int gameMenuStackDepth = 0;

  ScriptContext scriptCtx;
};

}  // namespace {{NAME_UPPER_NS}}
)";

static const char* TPL_GAME_HPP_FPP =
    R"(// Generated by tyra-editor. Delete this line to take ownership of this file.
#pragma once

#include <tyra>
#include <memory>
#include <vector>
#include "save_system.gen.hpp"
#include "scripts/script.hpp"

namespace {{NAME_UPPER_NS}} {

class TerrainGame : public Tyra::Game {
 public:
  explicit TerrainGame(Tyra::Engine* engine);
  ~TerrainGame();

  void init() override;
  void loop() override;

 private:
  void buildScene();
  void generateTerrainGrid();
  void updatePlayer();

  Tyra::Engine* engine;
  Tyra::StaticPipeline stapip;

  Tyra::Vec4 cameraPosition, cameraLookAt;
  float playerX, playerZ, yaw, pitch;
  float playerY, playerVelY;  // feet height + vertical velocity (physics)

  std::vector<Tyra::Vec4> vertices;
  std::vector<Tyra::Color> colors;

  Tyra::M4x4 model;
  std::unique_ptr<Tyra::StaPipBag> bag;
  std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
  std::unique_ptr<Tyra::StaPipColorBag> colorBag;

  // Scene objects at runtime (mutable by scripts/physics); geometry per
  // object, one draw part per model material (primitives use parts[0])
  struct GeoPart {
    std::vector<Tyra::Vec4> vertices;
    std::vector<Tyra::Color> colors;
    std::vector<Tyra::Vec4> sts;  // texture coordinates
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
  };
  struct ObjectGeometry {
    std::vector<GeoPart> parts;
    // Animated models (.glb): this object's skeletal instance (own
    // playback state + skinned output mesh, samples the shared SkelModel).
    std::unique_ptr<Tyra::SkelInstance> animInst;
    // StaPip bags pointing straight into animInst's skinned arrays; the
    // skinned vertices stay in model space, so the object transform rides
    // in animMat (info bag + light matrix), not in the vertex data.
    struct AnimPart {
      std::unique_ptr<Tyra::StaPipBag> bag;
      std::unique_ptr<Tyra::StaPipColorBag> colorBag;
      std::unique_ptr<Tyra::StaPipTextureBag> texBag;
      std::unique_ptr<Tyra::StaPipLightingBag> lightBag;
    };
    std::vector<AnimPart> animParts;
    std::unique_ptr<Tyra::StaPipInfoBag> animInfoBag;
    Tyra::M4x4 animMat;
    // Usable-object highlight: fading shells grown around the object
    // center, drawn after the scene (see renderHighlightHull)
    std::vector<Tyra::Vec4> hullVerts;
    std::vector<Tyra::Color> hullCols;
    std::unique_ptr<Tyra::StaPipBag> hullBag;
    std::unique_ptr<Tyra::StaPipInfoBag> hullInfoBag;
    std::unique_ptr<Tyra::StaPipColorBag> hullColorBag;
  };
  // Custom .obj models, loaded once at startup (paths in model_data.gen.hpp):
  // geometry split per MTL material with optional per-material textures, the
  // real mesh AABB for box collision, a CollisionMesh for mesh collision.
  struct GameModelPart {
    std::vector<float> verts;  // 8 floats per vertex: x,y,z,nx,ny,nz,u,v
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
  };
  struct GameModel {
    std::vector<GameModelPart> parts;  // empty = missing/unparseable model
    float mn[3] = {-0.5F, -0.5F, -0.5F};
    float mx[3] = {0.5F, 0.5F, 0.5F};
    Tyra::CollisionMesh collider;  // built only when a scene needs mesh mode
  };
  std::vector<GameModel> gameModels;
  void loadModels();
  // Animated .glb models: serialized by the editor to .tskl skeletal files
  // (paths in model_data.gen.hpp) - bone keyframe tracks + bind-pose mesh.
  // Poses are evaluated and skinned on the EE/VU0 (SkelInstance) for the
  // in-view instances only; the skinned arrays render through the SAME
  // static pipeline as the rest of the scene (single vertex upload, no VU1
  // program swap, EE clipping). See updateAndRenderAnimObjects for the
  // real-hardware numbers behind this design - PCSX2's fast EE hides them.
  struct GameAnimModel {
    std::unique_ptr<Tyra::SkelModel> src;   // skeleton + mesh + clip tracks
    std::vector<Tyra::Texture*> textures;   // per part, nullptr = untextured
    Tyra::CoreBBox cullBox;  // local AABB over all clips + margin (see load)
  };
  std::vector<GameAnimModel> gameAnimModels;
  void loadAnimModels();
  void setupAnimObject(int index);  // per-object instance + playback state
  void updateAndRenderAnimObjects();
  // Directional light for the animated pass, mirroring the baked static
  // look. The manual dir-lights layout: colors[0..2] + ambient in [3].
  Tyra::Vec4 animLightColors[4];
  Tyra::Vec4 animLightDirs[3];
  Tyra::PipelineDirLightsBag animDirLights{true};

 public:
  // Clip-name lookup for scripts/flow graph (ScriptContext::resolveClip).
  int resolveClipIndex(int objectIndex, const char* clipName) const;

 private:
  // Primitive materials: .mtl assigned to a box/sphere/... - the file's
  // first material supplies the color (kd) and optional texture.
  struct GameMaterial {
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
  };
  std::vector<GameMaterial> gameMaterials;
  void loadMaterials();
  std::vector<Tyra::Texture*> loadedTextures;
  std::vector<Tyra::Vec4> terrainSts;
  Tyra::StaPipTextureBag terrainTexBag;
  std::vector<RuntimeObject> runtimeObjects;
  std::vector<ObjectGeometry> objectGeometry;
  GeoPart skyDome;
  float skyHorizonR = 0, skyHorizonG = 0, skyHorizonB = 0;
  std::vector<Tyra::Sprite> hudSprites;

  void buildSkyDome();
  void rebuildObjectGeometry(int index);
  // Player-vs-objects collision shared by both walkers: box (scale box or
  // model AABB), mesh (CollisionMesh) or none, per SceneObjectData.collision
  void collidePlayer(float prevX, float prevZ, float* nextX, float* nextZ,
                     float feetY, float eyeHeight, float* ground);
  void updateObjectPhysics();
  void renderScene();
  void renderHighlightHull(int index);

  // Player entity (PLAYER_INDEXES in scene_data.hpp); overrides the template
  // camera when present. Returns false when the scene has no player.
  bool updatePlayerEntity();
  float entX = 0, entY = 0, entZ = 0, entVelY = 0, entYaw = 0, entPitch = 0;

  // Multiple scenes: the game starts in scene 0; the flow graph Switch
  // Scene node requests a change applied between frames.
  void loadScene(int sceneIndex);
  int currentScene = 0;
  unsigned int sceneGeneration = 0;

  // Particle emitters (type 7): fixed pools sized at scene load, zero
  // per-frame allocations; camera-facing quads (textured when the emitter
  // has a material with a map_Kd), one bag per emitter.
  struct ParticleSystem {
    int objectIndex = -1;
    unsigned int rng = 1;
    std::vector<Tyra::Vec4> pos, vel;
    std::vector<float> life, maxLife;
    std::vector<Tyra::Vec4> verts;
    std::vector<Tyra::Color> cols;
    std::vector<Tyra::Vec4> sts;  // fixed per-quad UVs (textured emitters)
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
  };
  std::vector<ParticleSystem> particles;
  void buildParticles();
  void updateParticles();

  // Sound emitters (type 8): distance-attenuated one-shots on channels 16-23
  std::vector<audsrv_adpcm_t*> sndSamples;  // scene_data.hpp SND_PATHS order
  std::vector<int> sndTimers;               // per-object retrigger countdown
  void updateSoundEmitters();

  // Scene switches show res/hud/loading.png on black for a moment
  Tyra::Sprite loadingSprite;
  int loadingFrames = 0, loadingTarget = -1;

  // "Use" interaction: nearest usable object the camera looks at (controls.hpp)
  void updateUseTarget();
  int useTargetIndex = -1;
  Tyra::Sprite usePromptSprite;

  // Memory card save menu (save_system.gen.hpp): opened by using a Save
  // point object or the Open Save Menu flow node; gameplay pauses while
  // open. updateSaveMenu() returns true while it owns the pad.
  bool updateSaveMenu();
  void renderSaveMenu();
  void doSave(int slot);
  void doLoad(int slot);
  void applySavedObjects();
  void refreshSlotStates();
  std::vector<float> saveValues;
  std::vector<char> saveTexts;  // SAVE_TEXT_COUNT slots of SAVE_TEXT_LEN bytes
  std::vector<SaveObjectState> pendingObjState;  // applied after a scene load
  int pendingObjScene = -1;
  bool saveMenuOpen = false;
  int saveMenuSlot = 0;
  int saveMenuGrace = 0;  // frames to ignore pad input after opening
  bool slotUsed[SAVE_SLOTS] = {};
  int saveFeedback = 0, saveFeedbackFrames = 0;  // 1 saved, 2 loaded, 3 error
  Tyra::Sprite saveMenuSprite, saveCursorSprite, saveUsedSprite;
  Tyra::Sprite saveFeedbackSprites[3];  // saved / loaded / error

  // Game menus (menu_data.gen.hpp): panels baked by the editor, opened by
  // the Open Menu flow node, a menu entry, or at boot (title screen).
  // Gameplay pauses while one is open; Triangle walks the submenu stack.
  bool updateGameMenu();
  void renderGameMenu();
  std::vector<Tyra::Sprite> menuSprites;
  Tyra::Sprite menuCursorSprite;
  Tyra::Sprite menuDimSprite;  // fullscreen dim under pausing menus
  int gameMenuIndex = -1;
  int gameMenuCursor = 0;
  int gameMenuGrace = 0;
  int gameMenuStack[4] = {};
  int gameMenuStackDepth = 0;

  ScriptContext scriptCtx;
};

}  // namespace {{NAME_UPPER_NS}}
)";

// Game .cpp is composed as: PROLOG + <template>_HEAD + SCENE + <template>_TAIL + FOOTER
static const char* TPL_GAME_CPP_PROLOG =
    R"(// Generated by tyra-editor. Delete this line to take ownership of this file.
#include "terrain_game.hpp"
#include "terrain_config.hpp"
#include "controls.hpp"
#include "scene_data.hpp"
#include "model_data.gen.hpp"
#include "hud_data.gen.hpp"
#include "menu_data.gen.hpp"
#include "terrain_heights.gen.hpp"
#include "texture_data.gen.hpp"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>

// Definition of the active-scene index declared in scene_data.hpp (which also
// defines the SCENE_*/SKY_*/... accessor macros so scripts see them too).
int g_activeScene = 0;

// Wall-clock normalization globals declared in scene_data.hpp; seeded in
// TerrainGame::init() from the resolved video mode (PAL 50 / NTSC 60) and
// refreshed every frame from the measured frame time (updateFrameClock), so
// the game plays at the same real-time speed on both systems AND under
// frame drops (a vsynced PAL game that misses the budget snaps to 25 FPS -
// without compensation that also meant half-speed gameplay).
float g_frameRate = 50.0F;
float g_frameDt = 1.0F / 50.0F;
float g_frameScale = 1.0F;

// Measures the real time since the previous frame (EE COP0 Count register,
// 294.912 MHz, wrap-safe) and folds it into g_frameDt / g_frameScale.
// Within 10% of the nominal vsync step it snaps to exactly nominal, so a
// full-rate game is bit-identical to the fixed-dt behavior; longer (frame
// drop) or shorter (vsync disabled) frames pass through, clamped to
// [1/4, 4x] nominal - a scene load is a hitch, not a gameplay leap.
static u32 g_frameClockLast = 0;
static void updateFrameClock() {
  u32 now;
  asm volatile("mfc0 %0, $9" : "=r"(now));
  const u32 delta = now - g_frameClockLast;
  const bool first = g_frameClockLast == 0;
  g_frameClockLast = now ? now : 1;
  const float nominal = 1.0F / g_frameRate;
  float dt = first ? nominal : (float)delta * (1.0F / 294912000.0F);
  if (dt > nominal * 0.9F && dt < nominal * 1.1F) dt = nominal;
  if (dt < nominal * 0.25F) dt = nominal * 0.25F;
  if (dt > nominal * 4.0F) dt = nominal * 4.0F;
  g_frameDt = dt;
  g_frameScale = dt * 50.0F;
}

namespace {{NAME_UPPER_NS}} {

using namespace Tyra;

namespace {

constexpr float PI = 3.14159265358979F;

/** The texture repository asserts (crashes) on a missing file - probe first
 * so a missing PNG degrades to an untextured draw with a warning. */
bool assetFileExists(const std::string& cwdRel) {
  FILE* f = fopen(FileUtils::fromCwd(cwdRel).c_str(), "rb");
  if (f) fclose(f);
  return f != nullptr;
}

struct V3 {
  float x, y, z;
};

/** Rotation order: X, then Y, then Z (same as the editor viewport). */
V3 rotated(const V3& v, const float* rotDeg) {
  V3 r = v;
  const float rx = rotDeg[0] * PI / 180.0F;
  const float ry = rotDeg[1] * PI / 180.0F;
  const float rz = rotDeg[2] * PI / 180.0F;
  {
    const float c = cosf(rx), s = sinf(rx);
    const float y = r.y * c - r.z * s, z = r.y * s + r.z * c;
    r.y = y, r.z = z;
  }
  {
    const float c = cosf(ry), s = sinf(ry);
    const float x = r.x * c + r.z * s, z = -r.x * s + r.z * c;
    r.x = x, r.z = z;
  }
  {
    const float c = cosf(rz), s = sinf(rz);
    const float x = r.x * c - r.y * s, y = r.x * s + r.y * c;
    r.x = x, r.y = y;
  }
  return r;
}

/** Inverse of rotated(): -Z, then -Y, then -X (world -> object local). */
V3 invRotated(const V3& v, const float* rotDeg) {
  V3 r = v;
  const float rx = rotDeg[0] * PI / 180.0F;
  const float ry = rotDeg[1] * PI / 180.0F;
  const float rz = rotDeg[2] * PI / 180.0F;
  {
    const float c = cosf(rz), s = -sinf(rz);
    const float x = r.x * c - r.y * s, y = r.x * s + r.y * c;
    r.x = x, r.y = y;
  }
  {
    const float c = cosf(ry), s = -sinf(ry);
    const float x = r.x * c + r.z * s, z = -r.x * s + r.z * c;
    r.x = x, r.z = z;
  }
  {
    const float c = cosf(rx), s = -sinf(rx);
    const float y = r.y * c - r.z * s, z = r.y * s + r.z * c;
    r.y = y, r.z = z;
  }
  return r;
}

/** Directional light (Project > Preferences), baked into vertex colors.
 * Returns per-channel multipliers: brightness * (ambient + diffuse*d*lightColor). */
V3 shadeOf(const V3& n) {
  float d = n.x * SCENE_LIGHT_X + n.y * SCENE_LIGHT_Y + n.z * SCENE_LIGHT_Z;
  if (d < 0.0F) d = 0.0F;
  const float base = SCENE_DIFFUSE * d;
  V3 s = {SCENE_BRIGHTNESS * (SCENE_AMBIENT + base * SCENE_LIGHT_COL_R),
          SCENE_BRIGHTNESS * (SCENE_AMBIENT + base * SCENE_LIGHT_COL_G),
          SCENE_BRIGHTNESS * (SCENE_AMBIENT + base * SCENE_LIGHT_COL_B)};
  if (s.x > 1.0F) s.x = 1.0F;
  if (s.y > 1.0F) s.y = 1.0F;
  if (s.z > 1.0F) s.z = 1.0F;
  return s;
}

/** Point lights (SceneObject type 9) in the active scene, baked additively on
 * top of the directional term. Linear distance falloff * N.L, tinted by the
 * light color and scaled by its brightness. wp = world-space vertex position. */
V3 pointLightAt(const V3& wp, const V3& n) {
  V3 add = {0.0F, 0.0F, 0.0F};
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    const SceneObjectData& L = SCENE_OBJECTS[i];
    if (L.type != 9) continue;
    const float radius = L.lightRadius > 0.01F ? L.lightRadius : 0.01F;
    V3 d = {L.position[0] - wp.x, L.position[1] - wp.y, L.position[2] - wp.z};
    const float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist >= radius) continue;
    float atten = 1.0F - dist / radius;
    atten *= atten;  // softer, rounder pool of light
    float ndotl = 1.0F;
    if (dist > 0.0001F) {
      ndotl = (n.x * d.x + n.y * d.y + n.z * d.z) / dist;
      if (ndotl < 0.0F) ndotl = 0.0F;
    }
    const float k = L.lightBright * atten * ndotl;
    add.x += k * L.color[0];
    add.y += k * L.color[1];
    add.z += k * L.color[2];
  }
  return add;
}

// Material context for the primitive builders: addBox & co call pushVert
// without material args, so rebuildObjectGeometry stages the object's
// assigned material here before dispatching. Model parts pass theirs
// explicitly via the kd/textured parameters instead.
const float* g_primKd = nullptr;
bool g_primTextured = false;

// Clip-name resolution for scripts/flow graph: ScriptContext carries a plain
// function pointer (script.hpp must stay engine-agnostic), so the game
// instance is reached through this file-static (set in buildScene).
TerrainGame* g_animGame = nullptr;
int animResolveClipThunk(int objectIndex, const char* clipName) {
  return g_animGame ? g_animGame->resolveClipIndex(objectIndex, clipName) : -1;
}

// kd: material diffuse (MTL) multiplied into the object color, null = white.
// textured: this batch draws with a texture (a model part's map_Kd or a
// primitive material's) - switches the color to modulation scale (128 = 1.0).
void pushVert(std::vector<Vec4>& verts, std::vector<Color>& cols,
              std::vector<Vec4>& sts, const SceneObjectData& o, V3 p, V3 n,
              float u, float v, const float* kdArg = nullptr,
              bool texturedArg = false) {
  const float* kd = kdArg ? kdArg : g_primKd;
  const bool textured = texturedArg || g_primTextured;
  p.x *= o.scale[0], p.y *= o.scale[1], p.z *= o.scale[2];
  p = rotated(p, o.rotation);
  n = rotated(n, o.rotation);
  const V3 wp = {p.x + o.position[0], p.y + o.position[1], p.z + o.position[2]};
  V3 shade = shadeOf(n);
  const V3 pl = pointLightAt(wp, n);
  shade.x += pl.x, shade.y += pl.y, shade.z += pl.z;
  if (shade.x > 1.0F) shade.x = 1.0F;
  if (shade.y > 1.0F) shade.y = 1.0F;
  if (shade.z > 1.0F) shade.z = 1.0F;
  if (kd) shade.x *= kd[0], shade.y *= kd[1], shade.z *= kd[2];
  verts.push_back(Vec4(wp.x, wp.y, wp.z, 1.0F));
  // In textured mode the color modulates the texture (128 = 1.0). Kd may
  // exceed 1 (material brightness) - cap at the GS's 255 so untextured
  // colors cannot wrap.
  const float scale = textured ? 128.0F : 255.0F;
  auto c255 = [](float v) { return v > 255.0F ? 255.0F : v; };
  cols.push_back(Color(c255(o.color[0] * scale * shade.x),
                       c255(o.color[1] * scale * shade.y),
                       c255(o.color[2] * scale * shade.z), 128.0F));
  sts.push_back(Vec4(u, v, 1.0F, 0.0F));
}

void pushQuad(std::vector<Vec4>& verts, std::vector<Color>& cols,
              std::vector<Vec4>& sts, const SceneObjectData& o, V3 a, V3 b, V3 c,
              V3 d, V3 n) {
  pushVert(verts, cols, sts, o, a, n, 0, 0);
  pushVert(verts, cols, sts, o, b, n, 1, 0);
  pushVert(verts, cols, sts, o, c, n, 1, 1);
  pushVert(verts, cols, sts, o, a, n, 0, 0);
  pushVert(verts, cols, sts, o, c, n, 1, 1);
  pushVert(verts, cols, sts, o, d, n, 0, 1);
}

void addBox(std::vector<Vec4>& verts, std::vector<Color>& cols,
            std::vector<Vec4>& sts, const SceneObjectData& o) {
  const float h = 0.5F;
  pushQuad(verts, cols, sts, o, {h, -h, -h}, {h, h, -h}, {h, h, h}, {h, -h, h}, {1, 0, 0});
  pushQuad(verts, cols, sts, o, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-h, -h, -h},
           {-1, 0, 0});
  pushQuad(verts, cols, sts, o, {-h, h, -h}, {-h, h, h}, {h, h, h}, {h, h, -h}, {0, 1, 0});
  pushQuad(verts, cols, sts, o, {-h, -h, h}, {-h, -h, -h}, {h, -h, -h}, {h, -h, h},
           {0, -1, 0});
  pushQuad(verts, cols, sts, o, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
  pushQuad(verts, cols, sts, o, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h},
           {0, 0, -1});
}

void addSphere(std::vector<Vec4>& verts, std::vector<Color>& cols,
               std::vector<Vec4>& sts, const SceneObjectData& o) {
  // Detail = radial segments; stacks ~5:7 of that (mirror of the editor's
  // primSphereStacks in project.hpp - keep the two in sync).
  int slices = o.primDetail < 3 ? 3 : (o.primDetail > 64 ? 64 : o.primDetail);
  int stacks = slices * 5 / 7;
  if (stacks < 2) stacks = 2;
  const float r = 0.5F;
  for (int st = 0; st < stacks; ++st) {
    const float t0 = PI * st / stacks, t1 = PI * (st + 1) / stacks;
    const float tv0 = (float)st / stacks, tv1 = (float)(st + 1) / stacks;
    for (int sl = 0; sl < slices; ++sl) {
      const float p0 = 2.0F * PI * sl / slices, p1 = 2.0F * PI * (sl + 1) / slices;
      const float tu0 = (float)sl / slices, tu1 = (float)(sl + 1) / slices;
      const V3 v00 = {r * sinf(t0) * cosf(p0), r * cosf(t0), r * sinf(t0) * sinf(p0)};
      const V3 v01 = {r * sinf(t0) * cosf(p1), r * cosf(t0), r * sinf(t0) * sinf(p1)};
      const V3 v10 = {r * sinf(t1) * cosf(p0), r * cosf(t1), r * sinf(t1) * sinf(p0)};
      const V3 v11 = {r * sinf(t1) * cosf(p1), r * cosf(t1), r * sinf(t1) * sinf(p1)};
      // Smooth shading: normal of a unit-sphere vertex is the vertex itself.
      const V3 n00 = {v00.x * 2, v00.y * 2, v00.z * 2};
      const V3 n01 = {v01.x * 2, v01.y * 2, v01.z * 2};
      const V3 n10 = {v10.x * 2, v10.y * 2, v10.z * 2};
      const V3 n11 = {v11.x * 2, v11.y * 2, v11.z * 2};
      pushVert(verts, cols, sts, o, v00, n00, tu0, tv0);
      pushVert(verts, cols, sts, o, v10, n10, tu0, tv1);
      pushVert(verts, cols, sts, o, v11, n11, tu1, tv1);
      pushVert(verts, cols, sts, o, v00, n00, tu0, tv0);
      pushVert(verts, cols, sts, o, v11, n11, tu1, tv1);
      pushVert(verts, cols, sts, o, v01, n01, tu1, tv0);
    }
  }
}

void addCylinder(std::vector<Vec4>& verts, std::vector<Color>& cols,
                 std::vector<Vec4>& sts, const SceneObjectData& o) {
  const int seg = o.primDetail < 3 ? 3 : (o.primDetail > 64 ? 64 : o.primDetail);
  const float r = 0.5F, h = 0.5F;
  for (int i = 0; i < seg; ++i) {
    const float a0 = 2.0F * PI * i / seg, a1 = 2.0F * PI * (i + 1) / seg;
    const float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
    const float x0 = r * cosf(a0), z0 = r * sinf(a0);
    const float x1 = r * cosf(a1), z1 = r * sinf(a1);
    const V3 n0 = {cosf(a0), 0, sinf(a0)}, n1 = {cosf(a1), 0, sinf(a1)};
    // side (smooth)
    pushVert(verts, cols, sts, o, {x0, -h, z0}, n0, u0, 1);
    pushVert(verts, cols, sts, o, {x0, h, z0}, n0, u0, 0);
    pushVert(verts, cols, sts, o, {x1, h, z1}, n1, u1, 0);
    pushVert(verts, cols, sts, o, {x0, -h, z0}, n0, u0, 1);
    pushVert(verts, cols, sts, o, {x1, h, z1}, n1, u1, 0);
    pushVert(verts, cols, sts, o, {x1, -h, z1}, n1, u1, 1);
    // caps (planar mapping)
    pushVert(verts, cols, sts, o, {0, h, 0}, {0, 1, 0}, 0.5F, 0.5F);
    pushVert(verts, cols, sts, o, {x1, h, z1}, {0, 1, 0}, x1 + 0.5F, z1 + 0.5F);
    pushVert(verts, cols, sts, o, {x0, h, z0}, {0, 1, 0}, x0 + 0.5F, z0 + 0.5F);
    pushVert(verts, cols, sts, o, {0, -h, 0}, {0, -1, 0}, 0.5F, 0.5F);
    pushVert(verts, cols, sts, o, {x0, -h, z0}, {0, -1, 0}, x0 + 0.5F, z0 + 0.5F);
    pushVert(verts, cols, sts, o, {x1, -h, z1}, {0, -1, 0}, x1 + 0.5F, z1 + 0.5F);
  }
}

void addCone(std::vector<Vec4>& verts, std::vector<Color>& cols,
             std::vector<Vec4>& sts, const SceneObjectData& o) {
  const int seg = o.primDetail < 3 ? 3 : (o.primDetail > 64 ? 64 : o.primDetail);
  const float r = 0.5F, h = 0.5F;
  const float nl = 0.894F, ny = 0.447F;  // side normal for r=0.5, h=1
  for (int i = 0; i < seg; ++i) {
    const float a0 = 2.0F * PI * i / seg, a1 = 2.0F * PI * (i + 1) / seg;
    const float am = (a0 + a1) * 0.5F;
    const float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
    const float x0 = r * cosf(a0), z0 = r * sinf(a0);
    const float x1 = r * cosf(a1), z1 = r * sinf(a1);
    // side
    pushVert(verts, cols, sts, o, {0, h, 0}, {nl * cosf(am), ny, nl * sinf(am)},
             (u0 + u1) * 0.5F, 0);
    pushVert(verts, cols, sts, o, {x1, -h, z1}, {nl * cosf(a1), ny, nl * sinf(a1)}, u1, 1);
    pushVert(verts, cols, sts, o, {x0, -h, z0}, {nl * cosf(a0), ny, nl * sinf(a0)}, u0, 1);
    // base (planar mapping)
    pushVert(verts, cols, sts, o, {0, -h, 0}, {0, -1, 0}, 0.5F, 0.5F);
    pushVert(verts, cols, sts, o, {x0, -h, z0}, {0, -1, 0}, x0 + 0.5F, z0 + 0.5F);
    pushVert(verts, cols, sts, o, {x1, -h, z1}, {0, -1, 0}, x1 + 0.5F, z1 + 0.5F);
  }
}

/** Debug-profile HUD (Project > Preferences > Build): FPS and RAM
 * (used/total EE MB) readouts in the top-left corner, drawn from the 8x8
 * glyph strip res/hud/debugfont.png. Compiles to nothing in a release build
 * (the DEBUG_SHOW_* constants in terrain_config.hpp fold the calls away). */
void drawDebugHud(Engine* engine) {
  if (!DEBUG_SHOW_FPS && !DEBUG_SHOW_MEM) return;
  static Sprite glyph;
  static bool glyphReady = false;
  static int memRefresh = 0;
  static float memFreeMB = 0.0F;
  if (!glyphReady) {
    glyph.mode = SpriteMode::MODE_REPEAT;
    glyph.size = Vec2(8.0F, 8.0F);  // one atlas cell
    glyph.scale = 2.0F;
    auto* texture = engine->renderer.getTextureRepository().add(
        FileUtils::fromCwd("hud/debugfont.png"));
    texture->addLink(glyph.id);
    glyphReady = true;
  }
  // Glyph order in the atlas - must match the editor's debugFontPng().
  // Cells are 16px apart: the glyph sits in the left 8px, the right 8px are
  // transparent padding so bilinear sampling never bleeds the next glyph.
  static const char* atlas = "0123456789.FPSMBE/";
  auto drawText = [&](const char* s, float x, float y) {
    for (; *s; ++s, x += 14.0F) {
      if (*s == ' ') continue;
      const char* hit = strchr(atlas, *s);
      if (!hit) continue;
      glyph.offset = Vec2((float)(hit - atlas) * 16.0F, 0.0F);
      glyph.position = Vec2(x, y);
      engine->renderer.renderer2D.render(glyph);
    }
  };
  char line[32];
  float y = 16.0F;
  if (DEBUG_SHOW_FPS) {
    snprintf(line, sizeof(line), "FPS %d", (int)engine->info.getFps());
    drawText(line, 16.0F, y);
    y += 20.0F;
  }
  if (DEBUG_SHOW_MEM) {
    // getAvailableRAM() probes the heap with mallocs - too expensive to run
    // every frame, so the readout refreshes every ~2 seconds. Shown as
    // used/total: the EE has 32 MB, so MEM 6.1/32 MB = 6.1 MB in use.
    if (memRefresh-- <= 0) {
      memFreeMB = engine->info.getAvailableRAM();
      memRefresh = everyFrames(2.0F);
    }
    snprintf(line, sizeof(line), "MEM %.1f/32 MB", 32.0F - memFreeMB);
    drawText(line, 16.0F, y);
  }
}

}  // namespace
)";

// Orbit template: the camera slowly circles the terrain center.
static const char* TPL_GAME_CPP_ORBIT_HEAD = R"(
TerrainGame::TerrainGame(Engine* t_engine)
    : engine(t_engine), orbitAngle(0.0F), model(M4x4::Identity) {}

TerrainGame::~TerrainGame() {}

void TerrainGame::init() {
  // Engine clipper fix: the default clipMargin (-10.0F) moves the near
  // clipping plane ~10 units away from the camera, cutting away nearby
  // geometry. Clip right in front of the real near plane instead.
  // (read by the clipper during setRenderer below)
  PlanesClipAlgorithm::clipMargin =
      -(engine->renderer.core.getSettings().getNear() + 0.5F);

  // Wall-clock normalization: per-frame steps below are tuned for 50 Hz;
  // g_frameScale stretches them so NTSC's 60 Hz plays at the same speed.
  // These are the seeds - updateFrameClock() refreshes both every frame
  // from the measured frame time (frame drops slow the picture, not the
  // game; also what makes the vsync-off build play at the right speed).
  g_frameRate = engine->renderer.core.getSettings().getRefreshRate();
  g_frameDt = 1.0F / g_frameRate;
  g_frameScale = 50.0F / g_frameRate;
  // Experimental (Project > Preferences > Build): skip the vsync wait -
  // continuous frame rate instead of the 50/25 vsync snap, with tearing.
  if (!FRAME_LIMIT) engine->renderer.core.setFrameLimit(false);

  stapip.setRenderer(&engine->renderer.core);
  engine->renderer.core.postFx.setBloom(POSTFX_BLOOM);
  engine->renderer.core.postFx.setGrain(POSTFX_GRAIN);
  // Default color grading look (Tools > Color Grading); no-op when -1.
  // Grading is global - scene switches keep whatever preset is active.
  applySceneGrading(engine, GRADING_DEFAULT);

  engine->renderer.setClearScreenColor(Color(SKY_R, SKY_G, SKY_B));

  cameraLookAt = Vec4(0.0F, 0.0F, 0.0F);
  updateCameraOrbit();

  buildScene();

  scriptCtx.engine = engine;
  scriptCtx.objects = runtimeObjects.data();
  scriptCtx.objectCount = (int)runtimeObjects.size();
  scriptCtx.skyColor = Color(SKY_R, SKY_G, SKY_B);
  scriptCtx.playerPosition = cameraPosition;
  for (Script* script : getScripts()) script->init(scriptCtx);

  // HUD sprites (see hud_data.gen.hpp)
  const auto& screen = engine->renderer.core.getSettings();
  hudSprites.reserve(HUD_COUNT);
  for (int i = 0; i < HUD_COUNT; ++i) {
    const HudImageData& h = HUD_IMAGES[i];
    Sprite sprite;
    sprite.mode = SpriteMode::MODE_STRETCH;
    sprite.size = Vec2(h.w, h.h);
    sprite.position = Vec2(h.x * screen.getWidth() - h.w * 0.5F,
                           h.y * screen.getHeight() - h.h * 0.5F);
    hudSprites.push_back(sprite);
    auto* texture =
        engine->renderer.getTextureRepository().add(FileUtils::fromCwd(h.path));
    texture->addLink(hudSprites.back().id);
  }

  // "USE" prompt (res/hud/use.png), shown while looking at a usable object
  usePromptSprite.mode = SpriteMode::MODE_STRETCH;
  usePromptSprite.size = Vec2(128.0F, 32.0F);
  usePromptSprite.position = Vec2((screen.getWidth() - 128.0F) * 0.5F,
                                  screen.getHeight() * 0.72F);
  auto* useTexture =
      engine->renderer.getTextureRepository().add(FileUtils::fromCwd("hud/use.png"));
  useTexture->addLink(usePromptSprite.id);

  // Loading screen sprite (res/hud/loading.png), shown on scene switches
  loadingSprite.mode = SpriteMode::MODE_STRETCH;
  loadingSprite.size = Vec2(256.0F, 64.0F);
  loadingSprite.position = Vec2((screen.getWidth() - 256.0F) * 0.5F,
                                (screen.getHeight() - 64.0F) * 0.5F);
  auto* loadingTexture = engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd("hud/loading.png"));
  loadingTexture->addLink(loadingSprite.id);

  // Sound emitter samples (adpenc output next to the ELF)
  for (int i = 0; i < SND_COUNT; ++i)
    sndSamples.push_back(
        engine->audio.adpcm.load(FileUtils::fromCwd(SND_PATHS[i])));
}

void TerrainGame::loop() {
  updateFrameClock();  // real dt: frame drops slow the picture, not the game
  const bool saveMenuActive = updateSaveMenu();
  const bool gameMenuPausing = updateGameMenu();  // false for overlay menus
  const bool menuActive = saveMenuActive || gameMenuPausing;
  if (!menuActive) {
    if (!updatePlayerEntity()) updateCameraOrbit();
    updateUseTarget();
  }

  scriptCtx.playerPosition = cameraPosition;
  if (menuActive) scriptCtx.usedObject = -1;
  // Menus pause scripts - except the frame a menu entry fires a flow event,
  // which must reach the On Menu Event triggers.
  if (!menuActive || scriptCtx.menuEvent >= 0)
    for (Script* script : getScripts()) script->update(scriptCtx);
  engine->renderer.setClearScreenColor(scriptCtx.skyColor);

  // Scene switch requested by the flow graph / scripts. With the loading
  // screen enabled the switch hides behind a short black "LOADING..." hold
  // (the load itself is synchronous - the hold is presentation).
  if (scriptCtx.requestScene >= 0) {
    const int target = scriptCtx.requestScene;
    scriptCtx.requestScene = -1;
    if (LOADING_SCREEN) {
      loadingTarget = target;
      loadingFrames = everyFrames(0.7F);  // ~0.7s hold
    } else {
      loadScene(target);
    }
  }
  if (loadingFrames > 0) {
    engine->renderer.setClearScreenColor(Color(0.0F, 0.0F, 0.0F));
    engine->renderer.beginFrame();
    engine->renderer.renderer2D.render(loadingSprite);
    engine->renderer.endFrame();
    --loadingFrames;
    if (loadingFrames == everyFrames(0.7F) - 5)
      loadScene(loadingTarget);  // 5 frames shown first
    return;
  }

  // Flow graph / script teleport request (needs a Player entity - the orbit
  // camera itself is not teleportable)
  if (scriptCtx.teleport) {
    scriptCtx.teleport = false;
    if (PLAYER_INDEX >= 0) {
      entX = scriptCtx.teleportPos.x;
      entY = scriptCtx.teleportPos.y;
      entZ = scriptCtx.teleportPos.z;
      entVelY = 0.0F;
      entYaw = scriptCtx.teleportYaw * PI / 180.0F;
    }
  }

  if (!menuActive) updateObjectPhysics();
  updateParticles();
  updateSoundEmitters();

  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt));
  {
    engine->renderer.renderer3D.usePipeline(stapip);
    renderScene();
    if (scriptCtx.hudVisible)
      for (auto& sprite : hudSprites) engine->renderer.renderer2D.render(sprite);
    if (useTargetIndex >= 0) engine->renderer.renderer2D.render(usePromptSprite);
    renderGameMenu();
    renderSaveMenu();
    drawDebugHud(engine);
  }
  engine->renderer.endFrame();
}
)";

// Shared scene/terrain mesh building and runtime object management.
static const char* TPL_GAME_CPP_SCENE = R"(
void TerrainGame::buildScene() {
  // Load all scene textures once (paths in texture_data.gen.hpp)
  loadedTextures.assign(TEXTURE_COUNT, nullptr);
  for (int i = 0; i < TEXTURE_COUNT; ++i) {
    if (!assetFileExists(TEXTURE_PATHS[i])) {
      TYRA_WARN("Scene texture missing: ", TEXTURE_PATHS[i]);
      continue;  // objects fall back to their plain color
    }
    loadedTextures[i] =
        engine->renderer.getTextureRepository().add(FileUtils::fromCwd(TEXTURE_PATHS[i]));
  }

  loadModels();
  loadMaterials();
  loadAnimModels();
  g_animGame = this;
  scriptCtx.resolveClip = &animResolveClipThunk;

  vertices.clear();
  colors.clear();
  terrainSts.clear();

  generateTerrainGrid();

  infoBag = std::make_unique<StaPipInfoBag>();
  infoBag->model = &model;
  infoBag->shadingType = TyraShadingFlat;
  // Always classify per package against the frustum: packages fully outside
  // are skipped, packages touching a plane get per-triangle clipping
  // (CLIP_PRECISE, Project > Preferences) or per-triangle culling (fast -
  // cheaper, may drop triangles at the screen edge). Raw submission (None)
  // is never safe: geometry behind or far off-screen wraps the GS raster
  // window and smears giant polygons (faithful on real HW / SW renderer).
  infoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
  infoBag->fullClipChecks = CLIP_PRECISE;

  colorBag = std::make_unique<StaPipColorBag>();
  colorBag->many = colors.data();

  bag = std::make_unique<StaPipBag>();
  bag->info = infoBag.get();
  bag->color = colorBag.get();
  bag->vertices = vertices.data();
  bag->count = static_cast<u32>(vertices.size());
  bag->texture = nullptr;
  bag->lighting = nullptr;

  if (TERRAIN_TEXTURE >= 0 && loadedTextures[TERRAIN_TEXTURE]) {
    terrainTexBag.texture = loadedTextures[TERRAIN_TEXTURE];
    terrainTexBag.coordinates = terrainSts.data();
    bag->texture = &terrainTexBag;
  }

  // Runtime copies of the scene objects - scripts and physics mutate these.
  skyHorizonR = SKY_R, skyHorizonG = SKY_G, skyHorizonB = SKY_B;
  buildSkyDome();

  // Save system: BIOS mc modules, custom values, menu sprites (hud/save-*.png)
  saveValues.assign(SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1, 0.0F);
  for (int i = 0; i < SAVE_VALUE_COUNT; ++i) saveValues[i] = SAVE_VALUE_DEFAULTS[i];
  scriptCtx.saveValues = saveValues.data();
  scriptCtx.saveValueCount = SAVE_VALUE_COUNT;
  saveTexts.assign((SAVE_TEXT_COUNT > 0 ? SAVE_TEXT_COUNT : 1) * SAVE_TEXT_LEN, '\0');
  for (int i = 0; i < SAVE_TEXT_COUNT; ++i)
    snprintf(&saveTexts[i * SAVE_TEXT_LEN], SAVE_TEXT_LEN, "%s",
             SAVE_TEXT_DEFAULTS[i]);
  scriptCtx.saveTexts = saveTexts.data();
  scriptCtx.saveTextCount = SAVE_TEXT_COUNT;
  saveInit();
  {
    const auto& scr = engine->renderer.core.getSettings();
    auto setupSprite = [&](Sprite& s, const char* path, float w, float h,
                           float x, float y) {
      s.mode = SpriteMode::MODE_STRETCH;
      s.size = Vec2(w, h);
      s.position = Vec2(x, y);
      auto* t =
          engine->renderer.getTextureRepository().add(FileUtils::fromCwd(path));
      t->addLink(s.id);
    };
    const float panelX = (scr.getWidth() - 256.0F) * 0.5F;
    const float panelY = (scr.getHeight() - 128.0F) * 0.5F - 24.0F;
    setupSprite(saveMenuSprite, "hud/save-menu.png", 256, 128, panelX, panelY);
    // slot rows are baked into save-menu.png at y = 40 + slot * 24
    setupSprite(saveCursorSprite, "hud/save-cursor.png", 16, 16, panelX + 32.0F,
                panelY + 41.0F);
    setupSprite(saveUsedSprite, "hud/save-used.png", 64, 16, panelX + 152.0F,
                panelY + 42.0F);
    const float fbX = (scr.getWidth() - 128.0F) * 0.5F;
    const float fbY = panelY + 136.0F;
    setupSprite(saveFeedbackSprites[0], "hud/save-saved.png", 128, 32, fbX, fbY);
    setupSprite(saveFeedbackSprites[1], "hud/save-loaded.png", 128, 32, fbX, fbY);
    setupSprite(saveFeedbackSprites[2], "hud/save-error.png", 128, 32, fbX, fbY);

    // Game menus: one baked panel sprite each (menu_data.gen.hpp) + a
    // shared cursor. The panel center sits at the menu's normalized screen
    // position (only the drawn contentH counts - the canvas slack is air).
    menuSprites.clear();
    menuSprites.reserve(MENU_COUNT);
    for (int i = 0; i < MENU_COUNT; ++i) {
      const MenuData& m = MENUS[i];
      Sprite s;
      s.mode = SpriteMode::MODE_STRETCH;
      s.size = Vec2((float)m.panelW, (float)m.panelH);
      s.position = Vec2(m.screenX * scr.getWidth() - m.panelW * 0.5F,
                        m.screenY * scr.getHeight() - m.contentH * 0.5F);
      menuSprites.push_back(s);
      auto* t = engine->renderer.getTextureRepository().add(
          FileUtils::fromCwd(m.panel));
      t->addLink(menuSprites.back().id);
    }
    setupSprite(menuCursorSprite, "hud/save-cursor.png", 16, 16, 0.0F, 0.0F);
    setupSprite(menuDimSprite, "hud/menu-dim.png", scr.getWidth(),
                scr.getHeight(), 0.0F, 0.0F);
    if (TITLE_MENU >= 0) {
      gameMenuIndex = TITLE_MENU;
      gameMenuCursor = 0;
      // The pad reconfigures for ~3.5s after boot and reports garbage
      // clicks - one of those must not press a title entry.
      gameMenuGrace = 200;
    }
  }

  loadScene(0);
}

// Loads every custom .obj once at startup through the engine's LeanObjLoader:
// geometry split per MTL material, map_Kd textures through the texture
// repository (de-duplicated by path), the real mesh AABB for box collision
// and a CollisionMesh where some scene object collides in mesh mode.
void TerrainGame::loadModels() {
  gameModels.assign(MODEL_COUNT > 0 ? MODEL_COUNT : 0, GameModel());
  std::map<std::string, Texture*> textureByPath;
  for (int i = 0; i < MODEL_COUNT; ++i) {
    const std::string overrideMtl = MODEL_MTLS[i];
    auto mesh = LeanObjLoader::load(MODEL_PATHS[i], overrideMtl);
    if (!mesh) continue;  // stays empty - objects using it render nothing
    GameModel& gm = gameModels[i];
    for (int k = 0; k < 3; ++k) {
      gm.mn[k] = mesh->min[k];
      gm.mx[k] = mesh->max[k];
    }
    // map_Kd texture names resolve relative to the file that defined them:
    // the override .mtl when one is assigned, the model otherwise
    std::string dir = overrideMtl.empty() ? MODEL_PATHS[i] : overrideMtl;
    const size_t slash = dir.find_last_of('/');
    dir = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
    for (auto& mat : mesh->materials) {
      GameModelPart part;
      part.verts.swap(mat.vertices);
      part.kd[0] = mat.kd[0];
      part.kd[1] = mat.kd[1];
      part.kd[2] = mat.kd[2];
      if (!mat.textureName.empty()) {
        const std::string path = dir + mat.textureName;
        auto it = textureByPath.find(path);
        if (it == textureByPath.end()) {
          Texture* t = nullptr;
          // a texture the .mtl wants but the project lacks degrades the part
          // to its Kd color instead of an assert at boot
          if (assetFileExists(path))
            t = engine->renderer.getTextureRepository().add(
                FileUtils::fromCwd(path));
          else
            TYRA_WARN("Model texture missing: ", path.c_str());
          it = textureByPath.emplace(path, t).first;
        }
        part.texture = it->second;
      }
      gm.parts.push_back(std::move(part));
    }
    if (MODEL_NEEDS_COLLIDER[i]) {
      std::vector<float> all;  // the collider spans every material part
      for (const auto& part : gm.parts)
        all.insert(all.end(), part.verts.begin(), part.verts.end());
      gm.collider.build(all.data(), (u32)(all.size() / 8), 8);
    }
  }
}

// Primitive materials: each MATERIAL_PATHS entry is a .mtl whose FIRST
// material becomes the surface of the primitives it is assigned to
// (Kd color + optional map_Kd texture, resolved relative to the .mtl).
void TerrainGame::loadMaterials() {
  gameMaterials.assign(MATERIAL_COUNT > 0 ? MATERIAL_COUNT : 0, GameMaterial());
  std::map<std::string, Texture*> textureByPath;
  for (int i = 0; i < MATERIAL_COUNT; ++i) {
    const auto materials = LeanObjLoader::loadMtl(MATERIAL_PATHS[i]);
    if (materials.empty()) continue;  // stays white - plain object color
    const auto& mat = materials.front();
    GameMaterial& gmat = gameMaterials[i];
    gmat.kd[0] = mat.kd[0];
    gmat.kd[1] = mat.kd[1];
    gmat.kd[2] = mat.kd[2];
    if (mat.textureName.empty()) continue;
    std::string dir = MATERIAL_PATHS[i];
    const size_t slash = dir.find_last_of('/');
    dir = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
    const std::string path = dir + mat.textureName;
    auto it = textureByPath.find(path);
    if (it == textureByPath.end()) {
      Texture* t = nullptr;
      if (assetFileExists(path))
        t = engine->renderer.getTextureRepository().add(FileUtils::fromCwd(path));
      else
        TYRA_WARN("Material texture missing: ", path.c_str());
      it = textureByPath.emplace(path, t).first;
    }
    gmat.texture = it->second;
  }
}

// Animated models: .glb files serialized by the editor into .tskl skeletal
// files (TsklLoader). The model data is shared; every scene object gets a
// SkelInstance (own playback state + skinned output mesh). Textures ship as
// PNGs next to the .tskl and link to each instance's materials by id.
void TerrainGame::loadAnimModels() {
  gameAnimModels.clear();
  gameAnimModels.resize(ANIM_MODEL_COUNT > 0 ? ANIM_MODEL_COUNT : 0);
  std::map<std::string, Texture*> textureByPath;
  for (int i = 0; i < ANIM_MODEL_COUNT; ++i) {
    auto model = TsklLoader::load(ANIM_MODEL_PATHS[i]);
    if (!model) continue;  // stays empty - objects using it render nothing
    GameAnimModel& gam = gameAnimModels[i];
    gam.textures.assign(model->parts.size(), nullptr);
    for (size_t m = 0; m < model->parts.size(); ++m) {
      const std::string& path = model->parts[m].texturePath;
      if (path.empty()) continue;
      auto it = textureByPath.find(path);
      if (it == textureByPath.end()) {
        Texture* t = nullptr;
        if (assetFileExists(path))
          t = engine->renderer.getTextureRepository().add(FileUtils::fromCwd(path));
        else
          TYRA_WARN("Model texture missing: ", path.c_str());
        it = textureByPath.emplace(path, t).first;
      }
      if (it->second)
        gam.textures[m] = it->second;
      else  // missing texture degrades the part to its plain color
        model->parts[m].texturePath.clear();
    }
    // Local-space cull box: the .tskl AABB is a union over every clip
    // (sampled by the baker), padded 10% per axis for pose positions
    // between the bake samples. Culling skips pose+skin+submit entirely,
    // so the box must stay conservative.
    {
      float lo[3], hi[3];
      for (int c = 0; c < 3; ++c) {
        const float pad = 0.1F * (model->max[c] - model->min[c]);
        lo[c] = model->min[c] - pad;
        hi[c] = model->max[c] + pad;
      }
      Vec4 corners[2];
      corners[0].set(lo[0], lo[1], lo[2], 1.0F);
      corners[1].set(hi[0], hi[1], hi[2], 1.0F);
      gam.cullBox = CoreBBox(corners, 2);
    }
    gam.src = std::move(model);
  }
  // One shared directional-light set for every animated instance; the
  // arrays are member storage, values refresh each frame in the anim pass.
  animDirLights.setLightsManually(animLightColors, animLightDirs);
}

int TerrainGame::resolveClipIndex(int objectIndex, const char* clipName) const {
  if (objectIndex < 0 || objectIndex >= (int)runtimeObjects.size()) return -1;
  const RuntimeObject& o = runtimeObjects[objectIndex];
  if (o.data.animModel < 0 || o.data.animModel >= (int)gameAnimModels.size())
    return -1;
  const GameAnimModel& gam = gameAnimModels[o.data.animModel];
  if (!gam.src) return -1;
  if (!clipName || !clipName[0]) return 0;  // "" = the file's first clip
  for (size_t c = 0; c < gam.src->clips.size(); ++c)
    if (gam.src->clips[c].name == clipName) return (int)c;
  return -1;
}

// Creates this object's skeletal instance and resets its playback state
// to the object's authored defaults. Called for every object on scene load.
void TerrainGame::setupAnimObject(int index) {
  RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];
  g.animInst.reset();
  g.animParts.clear();  // bags point into the instance - drop them together
  g.animInfoBag.reset();
  if (o.data.type != 5 || o.data.animModel < 0 ||
      o.data.animModel >= (int)gameAnimModels.size())
    return;
  GameAnimModel& gam = gameAnimModels[o.data.animModel];
  if (!gam.src) return;

  g.animInst = std::make_unique<SkelInstance>(gam.src.get());
  DynamicMesh* mesh = g.animInst->mesh.get();
  for (size_t m = 0; m < mesh->materials.size(); ++m) {
    MeshMaterial* mat = mesh->materials[m];
    // texture links are per material id and every instance has fresh ids
    if (m < gam.textures.size() && gam.textures[m])
      gam.textures[m]->addLink(mat->id);
    // per-instance tint: object color multiplies the material base color
    const float* base = gam.src->parts[m].color;
    mat->ambient.set(base[0] * 128.0F * o.data.color[0],
                     base[1] * 128.0F * o.data.color[1],
                     base[2] * 128.0F * o.data.color[2], 128.0F);
  }

  // Static-pipeline bags around the skinned arrays (rebuilt in place every
  // skin; bboxVersion bumps keep the frustum boxes honest). One info bag
  // per object carries the model matrix; parts share it.
  g.animMat.identity();
  g.animInfoBag = std::make_unique<StaPipInfoBag>();
  g.animInfoBag->model = &g.animMat;
  g.animInfoBag->shadingType = TyraShadingGouraud;  // per-vertex lighting
  g.animInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
  g.animInfoBag->fullClipChecks = true;  // near-camera geometry, like objects
  g.animParts.clear();
  g.animParts.resize(mesh->materials.size());
  for (size_t m = 0; m < mesh->materials.size(); ++m) {
    ObjectGeometry::AnimPart& ap = g.animParts[m];
    MeshMaterialFrame* frame = mesh->materials[m]->frames[0];
    ap.colorBag = std::make_unique<StaPipColorBag>();
    ap.colorBag->single = &mesh->materials[m]->ambient;
    ap.lightBag = std::make_unique<StaPipLightingBag>();
    ap.lightBag->lightMatrix = &g.animMat;
    ap.lightBag->normals = frame->normals;
    ap.lightBag->dirLights = &animDirLights;
    ap.bag = std::make_unique<StaPipBag>();
    ap.bag->info = g.animInfoBag.get();
    ap.bag->color = ap.colorBag.get();
    ap.bag->vertices = frame->vertices;
    ap.bag->count = frame->count;
    ap.bag->lighting = ap.lightBag.get();
    if (m < gam.textures.size() && gam.textures[m] && frame->textureCoords) {
      ap.texBag = std::make_unique<StaPipTextureBag>();
      ap.texBag->texture = gam.textures[m];
      ap.texBag->coordinates = frame->textureCoords;
      ap.bag->texture = ap.texBag.get();
    }
  }

  o.animClip = resolveClipIndex(index, o.data.animClip);
  if (o.animClip < 0) {
    if (o.data.animClip && o.data.animClip[0])
      TYRA_WARN("Unknown animation clip: ", o.data.animClip);
    o.animClip = 0;
  }
  o.animLoop = o.data.animLoop != 0;
  o.animSpeed = o.data.animSpeed;
  o.animPlaying = o.data.animAutoplay != 0;
  o.animRestart = true;  // clip applied on the first anim update
  o.animFinished = false;
  o.animFade = 0.0F;
}

// The animated-models pass of the scene render. Real-hardware numbers drove
// this shape (PCSX2's fast EE hides all of it): each visible 1092-vert
// instance costs ~0.9 ms pose+skin plus ~1 ms submit on the EE, and the old
// code paid it for every instance every frame - 7 spiders saturated the
// 20 ms PAL budget on their own and halved the frame rate to 25.
// Three measures keep the pass inside the budget:
//  - instances whose conservative all-clips AABB is outside the frustum
//    skip pose/skin/submit entirely (playback still advances, so
//    animFinished and re-entry poses stay honest);
//  - instances striking the identical pose (same clip advanced in lockstep,
//    the ambient-prop / enemy-pack case) share one skinned mesh - the first
//    skins, the rest re-point their bags at its arrays;
//  - the skinned arrays render through the SAME static pipeline as the rest
//    of the scene: one submission per vertex (DynPip uploads every vertex
//    twice for its from/to lerp), no VU1 program swap mid-frame, and the
//    EE clipper handles screen-edge crossers like all other geometry.
// One directional light matches the baked static lighting (point lights are
// baked into static vertex colors and cannot follow animated meshes).
void TerrainGame::updateAndRenderAnimObjects() {
  if (gameAnimModels.empty()) return;
  bool any = false;
  for (int i = 0; i < (int)runtimeObjects.size() && !any; ++i)
    any = runtimeObjects[i].visible && objectGeometry[i].animInst != nullptr;
  if (!any) return;

  const float amb = 128.0F * SCENE_BRIGHTNESS * SCENE_AMBIENT;
  const float dif = 128.0F * SCENE_BRIGHTNESS * SCENE_DIFFUSE;
  // manual dir-lights layout: [0..2] directional colors, [3] ambient
  animLightColors[0].set(dif * SCENE_LIGHT_COL_R, dif * SCENE_LIGHT_COL_G,
                         dif * SCENE_LIGHT_COL_B, 1.0F);
  animLightColors[1].set(0.0F, 0.0F, 0.0F, 1.0F);
  animLightColors[2].set(0.0F, 0.0F, 0.0F, 1.0F);
  animLightColors[3].set(amb, amb, amb, 128.0F);
  animLightDirs[0].set(SCENE_LIGHT_X, SCENE_LIGHT_Y, SCENE_LIGHT_Z, 1.0F);
  animLightDirs[1].set(0.0F, 0.0F, 0.0F, 1.0F);
  animLightDirs[2].set(0.0F, 0.0F, 0.0F, 1.0F);

  // in-view instances rendered so far this frame: object index + the object
  // whose instance owns the skinned arrays it drew with (itself, or the
  // group leader it followed)
  struct RenderedAnim {
    int obj;
    int meshOwner;
  };
  static std::vector<RenderedAnim> rendered;
  rendered.clear();

  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    RuntimeObject& o = runtimeObjects[i];
    ObjectGeometry& g = objectGeometry[i];
    SkelInstance* inst = g.animInst.get();
    if (!inst || !o.visible) continue;
    const GameAnimModel& gam = gameAnimModels[o.data.animModel];

    if (o.animClip < 0 || o.animClip >= (int)gam.src->clips.size())
      o.animClip = 0;
    if (o.animRestart) {
      o.animRestart = false;
      o.animFinished = false;
      // animFade > 0 crossfades from the pose currently showing
      inst->play((u32)o.animClip, o.animLoop, o.animFade);
      o.animFade = 0.0F;  // consumed by this restart
    }
    inst->setLoop(o.animLoop);
    // time always advances by wall-clock seconds (speed scales the step),
    // visible or not - animFinished stays honest for offscreen instances
    const float step = o.animPlaying ? g_frameDt * o.animSpeed : 0.0F;
    o.animFinished = inst->advance(step);

    // model matrix straight from the object data: T * R(X,Y,Z) * S, the
    // same transform the static path bakes through pushVert()/rotated()
    const V3 bx = rotated({o.data.scale[0], 0.0F, 0.0F}, o.data.rotation);
    const V3 by = rotated({0.0F, o.data.scale[1], 0.0F}, o.data.rotation);
    const V3 bz = rotated({0.0F, 0.0F, o.data.scale[2]}, o.data.rotation);
    M4x4& m = g.animMat;  // the info bag and light matrix point here
    m.identity();
    m.data[0] = bx.x, m.data[1] = bx.y, m.data[2] = bx.z;
    m.data[4] = by.x, m.data[5] = by.y, m.data[6] = by.z;
    m.data[8] = bz.x, m.data[9] = bz.y, m.data[10] = bz.z;
    m.data[12] = o.data.position[0];
    m.data[13] = o.data.position[1];
    m.data[14] = o.data.position[2];

    // pose + skin + submit only when the conservative box touches the view
    if (gam.cullBox.frustumCheck(
            engine->renderer.core.renderer3D.frustumPlanes.getAll(), m) ==
        CoreBBoxFrustum::OUTSIDE_FRUSTUM)
      continue;

    // pose sharing: follow an already-rendered instance in the same pose
    int meshOwner = i;
    for (const RenderedAnim& r : rendered) {
      if (runtimeObjects[r.obj].data.animModel != o.data.animModel) continue;
      if (!inst->poseEquals(*objectGeometry[r.obj].animInst)) continue;
      meshOwner = r.meshOwner;
      break;
    }

    ObjectGeometry& owner = objectGeometry[meshOwner];
    const bool reskinned =
        meshOwner == i ? inst->ensurePose() : false;
    DynamicMesh* srcMesh = owner.animInst->mesh.get();
    for (size_t p = 0; p < g.animParts.size(); ++p) {
      ObjectGeometry::AnimPart& ap = g.animParts[p];
      if (!ap.bag) continue;
      // bags may still point at another frame's group leader - re-aim them
      MeshMaterialFrame* frame = srcMesh->materials[p]->frames[0];
      ap.bag->vertices = frame->vertices;
      ap.lightBag->normals = frame->normals;
      if (ap.texBag) ap.texBag->coordinates = frame->textureCoords;
      if (meshOwner == i) {
        if (reskinned) ap.bag->bboxVersion++;  // skinned in place
      } else {
        // identical pointer + version = followers reuse the owner's cached
        // frustum boxes instead of recomputing them per instance
        ap.bag->bboxVersion = owner.animParts[p].bag->bboxVersion;
      }
      stapip.core.render(ap.bag.get());
    }
    rendered.push_back({i, meshOwner});
  }
}

// Shared player-vs-scene collision (both walkers). Box mode reproduces the
// classic behavior (XZ box + stand-on-top + step up 0.5), with models sized
// by their real mesh AABB instead of the unit scale box. Mesh mode collides
// with the model's triangles in object-local space: a downward ray finds the
// walkable ground (ramps/stairs work) and steep faces push the player out
// like walls. Rotation is honored in mesh mode and ignored in box mode.
void TerrainGame::collidePlayer(float prevX, float prevZ, float* nextX,
                                float* nextZ, float feetY, float eyeHeight,
                                float* ground) {
  const float playerRadius = 0.35F;
  for (const RuntimeObject& o : runtimeObjects) {
    if (!o.visible || o.data.type == 4 || o.data.type == 6 ||
        o.data.type == 7 || o.data.type == 8 || o.data.type == 9 ||
        o.data.type == 11)
      continue;
    if (o.data.collision == 2) continue;  // none

    const GameModel* gm = nullptr;
    if (o.data.type == 5 && o.data.model >= 0 &&
        o.data.model < (int)gameModels.size())
      gm = &gameModels[o.data.model];

    if (o.data.collision == 1 && gm && !gm->collider.empty()) {
      // --- mesh mode ---
      const float sx = o.data.scale[0] > 0.0001F ? o.data.scale[0] : 0.0001F;
      const float sy = o.data.scale[1] > 0.0001F ? o.data.scale[1] : 0.0001F;
      const float sz = o.data.scale[2] > 0.0001F ? o.data.scale[2] : 0.0001F;
      auto toLocal = [&](float wx, float wy, float wz) {
        V3 p = {wx - o.data.position[0], wy - o.data.position[1],
                wz - o.data.position[2]};
        p = invRotated(p, o.data.rotation);
        return V3{p.x / sx, p.y / sy, p.z / sz};
      };
      auto toWorld = [&](const V3& l) {
        V3 p = {l.x * sx, l.y * sy, l.z * sz};
        p = rotated(p, o.data.rotation);
        return V3{p.x + o.data.position[0], p.y + o.data.position[1],
                  p.z + o.data.position[2]};
      };

      // walls: push a chest-height sphere out of steep triangles (the radius
      // is approximated by the average XZ scale - exact for uniform scales)
      const V3 c = toLocal(*nextX, feetY + eyeHeight * 0.5F, *nextZ);
      Vec4 center(c.x, c.y, c.z, 1.0F);
      const float sAvg = (sx + sz) * 0.5F;
      if (gm->collider.resolveSphere(&center, playerRadius / sAvg, 0.7F)) {
        const V3 w = toWorld({center.x, center.y, center.z});
        *nextX = w.x;
        *nextZ = w.z;
      }

      // ground: a ray from step height (0.5 above the feet) straight down,
      // transformed into local space so rotated/scaled models stay exact
      const V3 ro = toLocal(*nextX, feetY + 0.5F, *nextZ);
      const V3 rq = toLocal(*nextX, feetY - 100.0F, *nextZ);
      V3 rd = {rq.x - ro.x, rq.y - ro.y, rq.z - ro.z};
      const float rl = sqrtf(rd.x * rd.x + rd.y * rd.y + rd.z * rd.z);
      if (rl > 0.0001F) {
        rd.x /= rl, rd.y /= rl, rd.z /= rl;
        float t;
        if (gm->collider.raycast(Vec4(ro.x, ro.y, ro.z, 1.0F),
                                 Vec4(rd.x, rd.y, rd.z, 0.0F), rl, &t)) {
          const V3 hit =
              toWorld({ro.x + rd.x * t, ro.y + rd.y * t, ro.z + rd.z * t});
          if (hit.y > *ground) *ground = hit.y;
        }
      }
      continue;
    }

    // --- box mode --- (models: real mesh AABB; primitives: unit scale box;
    // animated models: the baked AABB, a union over every clip's poses -
    // mesh mode is a static-model feature, so .glb objects collide as boxes)
    const SkelModel* anim = nullptr;
    if (o.data.type == 5 && o.data.animModel >= 0 &&
        o.data.animModel < (int)gameAnimModels.size())
      anim = gameAnimModels[o.data.animModel].src.get();
    float cx = o.data.position[0], cy = o.data.position[1],
          cz = o.data.position[2];
    float ex = 0.5F * o.data.scale[0], ey = 0.5F * o.data.scale[1],
          ez = 0.5F * o.data.scale[2];
    const float* mn = gm ? gm->mn : (anim ? anim->min : nullptr);
    const float* mx = gm ? gm->mx : (anim ? anim->max : nullptr);
    if (mn && mx) {
      cx += 0.5F * (mn[0] + mx[0]) * o.data.scale[0];
      cy += 0.5F * (mn[1] + mx[1]) * o.data.scale[1];
      cz += 0.5F * (mn[2] + mx[2]) * o.data.scale[2];
      ex = 0.5F * (mx[0] - mn[0]) * o.data.scale[0];
      ey = 0.5F * (mx[1] - mn[1]) * o.data.scale[1];
      ez = 0.5F * (mx[2] - mn[2]) * o.data.scale[2];
    }
    const float hx = ex + playerRadius;
    const float hz = ez + playerRadius;
    const float top = cy + ey;
    const float bottom = cy - ey;

    const bool nextInside = *nextX > cx - hx && *nextX < cx + hx &&
                            *nextZ > cz - hz && *nextZ < cz + hz;
    if (!nextInside) continue;

    if (feetY + 0.5F >= top) {
      // low enough to walk onto - candidate floor
      if (top > *ground) *ground = top;
    } else if (feetY < top && feetY + eyeHeight > bottom) {
      // blocked - cancel the axes that entered the box this frame
      const bool wasInsideX = prevX > cx - hx && prevX < cx + hx;
      const bool wasInsideZ = prevZ > cz - hz && prevZ < cz + hz;
      if (!wasInsideX) *nextX = prevX;
      if (!wasInsideZ) *nextZ = prevZ;
      if (wasInsideX && wasInsideZ) {
        *nextX = prevX;
        *nextZ = prevZ;
      }
    }
  }
}

// Last volume/pan sent to each emitter channel (16-23). audsrv RPCs are
// synchronous and share one client lock with the music stream, so
// updateSoundEmitters only issues an RPC when the quantized value changes.
static int sndChVol[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int sndChPan[8] = {-999, -999, -999, -999, -999, -999, -999, -999};

// Switches the runtime state to a scene from scene_data.hpp. Assets
// (textures, models) are loaded once for all scenes at startup, so this
// only rebuilds the runtime objects: vectors and per-object bags are
// reused/freed here - nothing leaks and the switch takes a frame or two.
void TerrainGame::loadScene(int sceneIndex) {
  if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
  currentScene = sceneIndex;
  g_activeScene = sceneIndex;
  sceneGeneration++;  // scene scripts see this and reset their state

  // Terrain, lighting, sky, clipping and post-FX are per scene (Scene >
  // Preferences overrides) - rebuild the terrain mesh + sky dome and re-apply
  // the scene's render settings. Vectors and bags are reused.
  if (bag) {
    vertices.clear();
    colors.clear();
    terrainSts.clear();
    generateTerrainGrid();
    colorBag->many = colors.data();
    bag->vertices = vertices.data();
    bag->count = static_cast<u32>(vertices.size());
    bag->bboxVersion++;
    if (TERRAIN_TEXTURE >= 0 && loadedTextures[TERRAIN_TEXTURE]) {
      terrainTexBag.texture = loadedTextures[TERRAIN_TEXTURE];
      terrainTexBag.coordinates = terrainSts.data();
      bag->texture = &terrainTexBag;
    } else {
      bag->texture = nullptr;
    }
    infoBag->fullClipChecks = CLIP_PRECISE;
    skyHorizonR = SKY_R, skyHorizonG = SKY_G, skyHorizonB = SKY_B;
    buildSkyDome();
  }
  // Per-scene sky color (the loop paints the clear screen from ctx.skyColor)
  // and post effects.
  scriptCtx.skyColor = Color(SKY_R, SKY_G, SKY_B);
  engine->renderer.setClearScreenColor(scriptCtx.skyColor);
  engine->renderer.core.postFx.setBloom(POSTFX_BLOOM);
  engine->renderer.core.postFx.setGrain(POSTFX_GRAIN);

  runtimeObjects.assign(SCENE_OBJECT_COUNT, RuntimeObject());
  objectGeometry.clear();
  objectGeometry.resize(SCENE_OBJECT_COUNT);
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    runtimeObjects[i].data = SCENE_OBJECTS[i];
    // spawn points and the player are editor markers, not geometry; emitters
    // honor their Enabled flag (Show/Hide Object flips visible at runtime)
    runtimeObjects[i].visible =
        SCENE_OBJECTS[i].type != 4 && SCENE_OBJECTS[i].type != 6 &&
        !(SCENE_OBJECTS[i].type == 7 && !SCENE_OBJECTS[i].emitEnabled);
    runtimeObjects[i].dirty = true;
  }
  // Animated models: fresh per-object mesh instances + playback defaults
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) setupAnimObject(i);

  scriptCtx.objects = runtimeObjects.data();
  scriptCtx.objectCount = (int)runtimeObjects.size();
  scriptCtx.scene = currentScene;
  scriptCtx.sceneGeneration = sceneGeneration;
  scriptCtx.usedObject = -1;
  useTargetIndex = -1;

  // Player entity start state for this scene
  if (PLAYER_INDEX >= 0 && PLAYER_INDEX < SCENE_OBJECT_COUNT) {
    entX = SCENE_OBJECTS[PLAYER_INDEX].position[0];
    entZ = SCENE_OBJECTS[PLAYER_INDEX].position[2];
    entY = PLAYER_MODE == 1 ? SCENE_OBJECTS[PLAYER_INDEX].position[1]
                            : terrainHeightAt(entX, entZ);
    entYaw = SCENE_OBJECTS[PLAYER_INDEX].rotation[1] * PI / 180.0F;
    entVelY = 0.0F;
    entPitch = 0.0F;
  }

  buildParticles();

  // Sound emitters: fresh retrigger state; mute the emitter channels (an
  // ADPCM sample can't be stopped - it plays out, but silently).
  sndTimers.assign(runtimeObjects.size(), 0);
  for (int ch = 16; ch < 24; ++ch) {
    engine->audio.adpcm.setVolume(0, (s8)ch);
    sndChVol[ch - 16] = 0;  // keep the RPC cache in sync with the mute
  }

  // A loaded save targeting this scene: apply the stored object state now
  if (pendingObjScene == sceneIndex && !pendingObjState.empty())
    applySavedObjects();
}

// --- Sound emitters ----------------------------------------------------
// Volume falls off linearly with the distance to the player; the sound is
// panned left/right by the emitter's position relative to where the camera
// faces (positional stereo). Interval 0 retriggers every frame: tryPlay() is
// skipped while the channel is still busy, so the sample loops seamlessly.
// sndOnPlayer emitters skip all of that: full volume, centered - they play
// "on the player" wherever they are (dialogs, narration). Hide Object mutes.
void TerrainGame::updateSoundEmitters() {
  if (sndSamples.empty()) return;
  if (sndTimers.size() != runtimeObjects.size())
    sndTimers.assign(runtimeObjects.size(), 0);
  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    const RuntimeObject& o = runtimeObjects[i];
    if (o.data.type != 8 || !o.data.sndAuto) continue;
    if (o.data.snd < 0 || o.data.snd >= (int)sndSamples.size()) continue;
    if (!sndSamples[o.data.snd]) continue;  // sample failed to load (too big for SPU2?)
    const s8 ch = (s8)(16 + (i & 7));  // emitters own channels 16-23
    const int chIdx = i & 7;
    if (!o.visible) {
      if (sndChVol[chIdx] != 0) {
        engine->audio.adpcm.setVolume(0, ch);
        sndChVol[chIdx] = 0;
      }
      continue;
    }
    int vol = 100;
    int pan = 0;
    if (!o.data.sndOnPlayer) {
      const float dx = o.data.position[0] - scriptCtx.playerPosition.x;
      const float dy = o.data.position[1] - scriptCtx.playerPosition.y;
      const float dz = o.data.position[2] - scriptCtx.playerPosition.z;
      const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
      const float range = o.data.sndRange > 0.5F ? o.data.sndRange : 0.5F;
      vol = dist >= range ? 0 : (int)(100.0F * (1.0F - dist / range));
      // Pan: project the horizontal direction to the emitter onto the camera's
      // screen-right axis; -100 = full left, +100 = full right. Screen-right is
      // fwd x up = (-fwd.z, fwd.x) in this right-handed Y-up world - NOT the
      // particle billboards' (fwd.z, -fwd.x), which is mirrored (billboard quads
      // are symmetric so the sign never mattered there; ears notice).
      Vec4 fwd = cameraLookAt - cameraPosition;
      float rx = -fwd.z, rz = fwd.x;
      const float rl = sqrtf(rx * rx + rz * rz);
      if (rl > 0.0001F) {
        rx /= rl; rz /= rl;
        const float ex = o.data.position[0] - cameraPosition.x;
        const float ez = o.data.position[2] - cameraPosition.z;
        const float el = sqrtf(ex * ex + ez * ez);
        if (el > 0.0001F) {
          pan = (int)(((ex * rx + ez * rz) / el) * 100.0F);
          if (pan > 100) pan = 100; else if (pan < -100) pan = -100;
        }
      }
    }
    // audsrv RPCs are synchronous and share one client lock with the music
    // stream - an RPC per emitter per frame stalls the main thread whenever
    // the song thread holds the lock (measured 50 -> 42 FPS in PCSX2 with
    // one emitter + music). Quantize and only send real changes; a static
    // player near a static emitter then costs zero RPCs per frame.
    vol = ((vol + 2) / 5) * 5;
    if (vol > 100) vol = 100;
    pan = pan >= 0 ? ((pan + 5) / 10) * 10 : -(((-pan + 5) / 10) * 10);
    if (vol != sndChVol[chIdx] || pan != sndChPan[chIdx]) {
      engine->audio.adpcm.setVolumeAndPan((u8)vol, (s8)pan, ch);
      sndChVol[chIdx] = vol;
      sndChPan[chIdx] = pan;
    }
    if (vol <= 0) continue;
    if (sndTimers[i] > 0) {
      --sndTimers[i];
      continue;
    }
    engine->audio.adpcm.tryPlay(sndSamples[o.data.snd], ch);
    sndTimers[i] = everyFrames(o.data.sndInterval);
  }
}

// --- Particle emitters ------------------------------------------------
// 2002-style particles: fixed pools sized at scene load, one cheap LCG,
// no trig and no allocations in the per-frame path. Camera-facing quads
// colored per vertex; an emitter with a material carrying a map_Kd draws
// its quads with that texture (the color then modulates it).
// The editor viewport mirrors this simulation (viewport.cpp,
// simulateEmitter) - keep the per-kind formulas in sync.
static float prand(unsigned int& s) {  // 0..1
  s = s * 1664525u + 1013904223u;
  return (float)(s >> 8) * (1.0F / 16777216.0F);
}

void TerrainGame::buildParticles() {
  particles.clear();
  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    if (runtimeObjects[i].data.type != 7) continue;
    ParticleSystem ps;
    ps.objectIndex = i;
    ps.rng = 12345u + (unsigned int)i * 7919u;
    int n = SCENE_OBJECTS[i].emitCount;
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    ps.pos.assign(n, Vec4(0.0F, 0.0F, 0.0F, 1.0F));
    ps.vel.assign(n, Vec4(0.0F, 0.0F, 0.0F, 0.0F));
    ps.life.assign(n, 0.0F);  // dead -> staggered respawn over the first frames
    ps.maxLife.assign(n, 1.0F);
    ps.verts.assign((size_t)n * 6, Vec4(0.0F, 0.0F, 0.0F, 1.0F));
    ps.cols.assign((size_t)n * 6, Color(0.0F, 0.0F, 0.0F, 0.0F));
    ps.infoBag = std::make_unique<StaPipInfoBag>();
    ps.infoBag->model = &model;
    ps.infoBag->shadingType = TyraShadingGouraud;
    ps.infoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
    ps.infoBag->fullClipChecks = true;
    ps.colorBag = std::make_unique<StaPipColorBag>();
    ps.bag = std::make_unique<StaPipBag>();
    ps.bag->info = ps.infoBag.get();
    ps.bag->color = ps.colorBag.get();
    ps.bag->texture = nullptr;
    ps.bag->lighting = nullptr;
    ps.bag->count = 0;
    // Textured particles: the emitter's material supplies the map (its Kd is
    // ignored - the emitter color is the tint). UVs are fixed per quad in the
    // v0,v1,v2 / v0,v2,v3 vertex order used by updateParticles().
    const int mi = SCENE_OBJECTS[i].material;
    if (mi >= 0 && mi < (int)gameMaterials.size() && gameMaterials[mi].texture) {
      ps.sts.reserve((size_t)n * 6);
      for (int q = 0; q < n; ++q) {
        ps.sts.push_back(Vec4(0.0F, 1.0F, 1.0F, 0.0F));
        ps.sts.push_back(Vec4(1.0F, 1.0F, 1.0F, 0.0F));
        ps.sts.push_back(Vec4(1.0F, 0.0F, 1.0F, 0.0F));
        ps.sts.push_back(Vec4(0.0F, 1.0F, 1.0F, 0.0F));
        ps.sts.push_back(Vec4(1.0F, 0.0F, 1.0F, 0.0F));
        ps.sts.push_back(Vec4(0.0F, 0.0F, 1.0F, 0.0F));
      }
      ps.texBag = std::make_unique<StaPipTextureBag>();
      ps.texBag->texture = gameMaterials[mi].texture;
      ps.texBag->coordinates = ps.sts.data();
      ps.bag->texture = ps.texBag.get();
    }
    particles.push_back(std::move(ps));
  }
}

void TerrainGame::updateParticles() {
  if (particles.empty()) return;
  const float dt = g_frameDt;

  // camera right/up shared by every billboard this frame
  Vec4 fwd = cameraLookAt - cameraPosition;
  const float fl = sqrtf(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
  if (fl > 0.0001F) fwd.x /= fl, fwd.y /= fl, fwd.z /= fl;
  float rx = fwd.z, rz = -fwd.x;
  const float rl = sqrtf(rx * rx + rz * rz);
  if (rl > 0.0001F) rx /= rl, rz /= rl;
  else rx = 1.0F, rz = 0.0F;
  const float ux = -rz * fwd.y;
  const float uy = rz * fwd.x - rx * fwd.z;
  const float uz = rx * fwd.y;

  for (ParticleSystem& ps : particles) {
    const RuntimeObject& o = runtimeObjects[ps.objectIndex];
    if (!o.visible) {
      ps.bag->count = 0;  // Hide Object turns the emitter off
      continue;
    }
    const SceneObjectData& d = o.data;
    const int kind = d.emitKind;
    const int n = (int)ps.life.size();

    // Follow player: the emitter position becomes an offset from the camera
    // (place it at X=0/Z=0 and the height above the player's head) - rain
    // that tracks the player instead of soaking the whole map.
    float bx = d.position[0], by = d.position[1], bz = d.position[2];
    if (d.emitFollow) {
      bx += cameraPosition.x;
      by += cameraPosition.y;
      bz += cameraPosition.z;
    }

    // Custom kind: emission direction = the object's +Y axis rotated by the
    // object rotation (tilt the emitter for a horizontal pipe leak), plus an
    // orthonormal tangent basis for the spread cone.
    V3 edir = {0.0F, 1.0F, 0.0F}, et1 = {1.0F, 0.0F, 0.0F},
       et2 = {0.0F, 0.0F, 1.0F};
    if (kind == 5) {
      edir = rotated({0.0F, 1.0F, 0.0F}, d.rotation);
      const V3 seed =
          fabsf(edir.y) < 0.9F ? V3{0.0F, 1.0F, 0.0F} : V3{1.0F, 0.0F, 0.0F};
      et1 = {seed.y * edir.z - seed.z * edir.y, seed.z * edir.x - seed.x * edir.z,
             seed.x * edir.y - seed.y * edir.x};
      const float tl =
          sqrtf(et1.x * et1.x + et1.y * et1.y + et1.z * et1.z);
      if (tl > 0.0001F) et1.x /= tl, et1.y /= tl, et1.z /= tl;
      et2 = {edir.y * et1.z - edir.z * et1.y, edir.z * et1.x - edir.x * et1.z,
             edir.x * et1.y - edir.y * et1.x};
    }

    for (int i = 0; i < n; ++i) {
      ps.life[i] -= dt;
      if (ps.life[i] <= 0.0F) {
        const float r1 = prand(ps.rng), r2 = prand(ps.rng), r3 = prand(ps.rng);
        const float sx = bx + (r1 - 0.5F) * d.scale[0];
        const float sz = bz + (r3 - 0.5F) * d.scale[2];
        ps.pos[i] = Vec4(sx, by, sz, 1.0F);
        if (kind == 0) {  // fire: rises and flickers
          ps.vel[i] = Vec4((r1 - 0.5F) * 0.8F, 1.2F + r2 * 1.2F, (r3 - 0.5F) * 0.8F, 0.0F);
          ps.maxLife[i] = 0.5F + r2 * 0.6F;
        } else if (kind == 1) {  // smoke: slow rise with drift
          ps.vel[i] = Vec4((r1 - 0.5F) * 0.5F, 0.5F + r2 * 0.5F, (r3 - 0.5F) * 0.5F, 0.0F);
          ps.maxLife[i] = 2.0F + r2 * 1.5F;
        } else if (kind == 2) {  // fog: big lazy puffs hugging the ground
          ps.vel[i] = Vec4((r1 - 0.5F) * 0.25F, 0.02F, (r3 - 0.5F) * 0.25F, 0.0F);
          ps.maxLife[i] = 3.0F + r2 * 3.0F;
        } else if (kind == 4) {  // rain: fast streaks, die on the terrain
          const float fall = 14.0F + r2 * 6.0F;
          ps.vel[i] = Vec4((r1 - 0.5F) * 0.6F, -fall, (r3 - 0.5F) * 0.6F, 0.0F);
          float drop = by - terrainHeightAt(sx, sz);
          if (drop < 0.5F) drop = 0.5F;
          ps.maxLife[i] = drop / fall;
        } else if (kind == 5) {  // custom: cone jet, physics from the knobs
          const float th = d.emitSpread * (PI / 180.0F) * prand(ps.rng);
          const float ph = 2.0F * PI * prand(ps.rng);
          const float ct = cosf(th), st = sinf(th);
          const float cp = cosf(ph), sp = sinf(ph);
          const float spd = d.emitSpeed * (0.8F + 0.4F * r2);
          ps.vel[i] = Vec4((edir.x * ct + (et1.x * cp + et2.x * sp) * st) * spd,
                           (edir.y * ct + (et1.y * cp + et2.y * sp) * st) * spd,
                           (edir.z * ct + (et1.z * cp + et2.z * sp) * st) * spd,
                           0.0F);
          ps.maxLife[i] = d.emitLife * (0.75F + 0.5F * r1);
        } else {  // sparks: radial burst pulled down by gravity
          ps.vel[i] = Vec4((r1 - 0.5F) * 5.0F, 1.5F + r2 * 2.5F, (r3 - 0.5F) * 5.0F, 0.0F);
          ps.maxLife[i] = 0.35F + r2 * 0.5F;
        }
        ps.life[i] = ps.maxLife[i] * (0.05F + 0.95F * prand(ps.rng));  // stagger
      }
      if (kind == 3) ps.vel[i].y -= 6.0F * dt;
      if (kind == 5) {
        // gravity + air drag ~ 1/weight: applied after the pull, so heavy
        // particles keep falling while light ones reach a slow terminal
        // drift (steam) - a natural terminal velocity
        ps.vel[i].y -= d.emitGravity * dt;
        const float w = d.emitWeight < 0.05F ? 0.05F : d.emitWeight;
        float damp = 1.0F - (0.6F / w) * dt;
        if (damp < 0.0F) damp = 0.0F;
        ps.vel[i].x *= damp;
        ps.vel[i].y *= damp;
        ps.vel[i].z *= damp;
      }
      ps.pos[i].x += ps.vel[i].x * dt;
      ps.pos[i].y += ps.vel[i].y * dt;
      ps.pos[i].z += ps.vel[i].z * dt;
      // custom + Die on terrain: the particle vanishes this frame (alpha 0)
      // and respawns next - water soaks into the ground instead of clipping
      if (kind == 5 && d.emitDieGround &&
          ps.pos[i].y <= terrainHeightAt(ps.pos[i].x, ps.pos[i].z))
        ps.life[i] = 0.0F;

      // life fraction drives size, alpha (0-128) and the color ramp
      const float t = ps.life[i] / ps.maxLife[i];
      float size = d.emitSize;
      float sizeUp = 0.0F;  // rain: extra world-up half-height (streaks)
      float alpha;
      float cr = d.color[0] * 128.0F, cg = d.color[1] * 128.0F, cb = d.color[2] * 128.0F;
      if (kind == 0) {
        size *= 0.5F + 0.8F * t;
        alpha = 90.0F * t;
        cg *= 0.35F + 0.65F * t;  // orange cools to red as it dies
        cb *= 0.25F * t;
      } else if (kind == 1) {
        size *= 1.6F - t;  // smoke grows while fading
        alpha = 40.0F * t;
      } else if (kind == 2) {
        size *= 3.0F;
        alpha = 18.0F * (t < 0.5F ? t * 2.0F : (1.0F - t) * 2.0F);  // fade in+out
      } else if (kind == 4) {
        sizeUp = size * 0.5F;  // size = streak length
        size *= 0.06F;         // thin
        alpha = 70.0F * (t < 0.15F ? t * (1.0F / 0.15F) : 1.0F);  // fade at impact
      } else if (kind == 5) {
        size *= 1.0F + (d.emitGrow - 1.0F) * (1.0F - t);  // 1 -> Grow over life
        alpha = d.emitOpacity * 128.0F * (t < 0.25F ? t * 4.0F : 1.0F);
      } else {
        size *= 0.35F;
        alpha = 110.0F * t;
      }

      // rain streaks stay vertical (world-up quads); everything else is a
      // full camera-facing billboard
      const float Rx = rx * size, Rz = rz * size;
      const float Ux = sizeUp > 0.0F ? 0.0F : ux * size;
      const float Uy = sizeUp > 0.0F ? sizeUp : uy * size;
      const float Uz = sizeUp > 0.0F ? 0.0F : uz * size;
      const Vec4& P = ps.pos[i];
      const Vec4 v0(P.x - Rx - Ux, P.y - Uy, P.z - Rz - Uz, 1.0F);
      const Vec4 v1(P.x + Rx - Ux, P.y - Uy, P.z + Rz - Uz, 1.0F);
      const Vec4 v2(P.x + Rx + Ux, P.y + Uy, P.z + Rz + Uz, 1.0F);
      const Vec4 v3(P.x - Rx + Ux, P.y + Uy, P.z - Rz + Uz, 1.0F);
      const int b = i * 6;
      ps.verts[b] = v0;
      ps.verts[b + 1] = v1;
      ps.verts[b + 2] = v2;
      ps.verts[b + 3] = v0;
      ps.verts[b + 4] = v2;
      ps.verts[b + 5] = v3;
      const Color c(cr, cg, cb, alpha);
      for (int k = 0; k < 6; ++k) ps.cols[b + k] = c;
    }
    ps.colorBag->many = ps.cols.data();
    ps.bag->vertices = ps.verts.data();
    ps.bag->count = (u32)ps.verts.size();
    ps.bag->bboxVersion++;  // moving cloud - refresh the frustum bbox
  }
}
// Picks the nearest usable object the camera is close to and looking at
// (thresholds in controls.hpp). BTN_USE on it -> scriptCtx.usedObject for
// one frame, which fires the flow graph "On Used" trigger.
void TerrainGame::updateUseTarget() {
  useTargetIndex = -1;
  scriptCtx.usedObject = -1;

  Vec4 dir = cameraLookAt - cameraPosition;
  const float dirLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (dirLen < 0.0001F) return;
  dir.x /= dirLen, dir.y /= dirLen, dir.z /= dirLen;

  float bestDist = 0.0F;
  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    const RuntimeObject& o = runtimeObjects[i];
    if (!o.data.usable || !o.visible) continue;
    if (o.data.type == 4 || o.data.type == 6 || o.data.type == 7 ||
        o.data.type == 8 || o.data.type == 9 || o.data.type == 11)
      continue;

    const float dx = o.data.position[0] - cameraPosition.x;
    const float dy = o.data.position[1] - cameraPosition.y;
    const float dz = o.data.position[2] - cameraPosition.z;
    const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float half = o.data.scale[0];
    if (o.data.scale[1] > half) half = o.data.scale[1];
    if (o.data.scale[2] > half) half = o.data.scale[2];
    half *= 0.5F;
    if (dist > USE_DISTANCE + half || dist < 0.001F) continue;

    const float dot = (dx * dir.x + dy * dir.y + dz * dir.z) / dist;
    if (dot < USE_LOOK_DOT && dist > half) continue;  // inside = always "looking"

    if (useTargetIndex < 0 || dist < bestDist) {
      useTargetIndex = i;
      bestDist = dist;
    }
  }

  if (useTargetIndex >= 0 && engine->pad.getClicked().BTN_USE)
    scriptCtx.usedObject = useTargetIndex;

  // Using a save point (type 10) opens the save menu next frame; the
  // "On Used" trigger still fires for its flow graph this frame.
  if (scriptCtx.usedObject >= 0 &&
      SCENE_OBJECTS[scriptCtx.usedObject].type == 10)
    scriptCtx.openSaveMenu = true;
}

// --- Memory card save menu ----------------------------------------------
// Dpad picks a slot, Cross saves, Circle loads, Triangle closes. Returns
// true while the menu owns the pad - loop() then skips player movement,
// the use target, scripts and object physics (a straight pause).
bool TerrainGame::updateSaveMenu() {
  if (saveFeedbackFrames > 0) --saveFeedbackFrames;

  if (scriptCtx.openSaveMenu) {
    scriptCtx.openSaveMenu = false;
    if (!saveMenuOpen) {
      saveMenuOpen = true;
      saveMenuSlot = 0;
      // The pad reports garbage transitions while it (re)configures -
      // swallow clicks briefly so opening the menu can't instantly save.
      saveMenuGrace = 15;
      useTargetIndex = -1;  // drop the USE prompt while the menu is up
      refreshSlotStates();
      return true;
    }
  }
  if (!saveMenuOpen) return false;
  if (saveMenuGrace > 0) {
    --saveMenuGrace;
    return true;
  }

  const auto& clicked = engine->pad.getClicked();
  if (clicked.DpadUp)
    saveMenuSlot = (saveMenuSlot + SAVE_SLOTS - 1) % SAVE_SLOTS;
  if (clicked.DpadDown) saveMenuSlot = (saveMenuSlot + 1) % SAVE_SLOTS;
  if (clicked.Triangle) {
    saveMenuOpen = false;
    return true;
  }
  if (clicked.Cross) doSave(saveMenuSlot);
  if (clicked.Circle && slotUsed[saveMenuSlot]) doLoad(saveMenuSlot);
  return true;
}

void TerrainGame::refreshSlotStates() {
  for (int i = 0; i < SAVE_SLOTS; ++i) slotUsed[i] = saveSlotUsed(i);
}

void TerrainGame::doSave(int slot) {
  static SaveGameData d;  // the payload can be a few KB - keep it off the stack
  d = SaveGameData();
  d.magic = SAVE_MAGIC;
  d.version = SAVE_VERSION;
  d.scene = currentScene;
  // Feet position + facing: the Player entity when the scene has one,
  // otherwise derived from the camera (the FPP template player; the orbit
  // camera simply ignores the restore).
  if (PLAYER_INDEX >= 0) {
    d.playerPos[0] = entX;
    d.playerPos[1] = entY;
    d.playerPos[2] = entZ;
    d.playerYaw = entYaw * 180.0F / PI;
  } else {
    d.playerPos[0] = cameraPosition.x;
    d.playerPos[1] = cameraPosition.y - EYE_HEIGHT;
    d.playerPos[2] = cameraPosition.z;
    const Vec4 dir = cameraLookAt - cameraPosition;
    d.playerYaw = atan2f(dir.x, dir.z) * 180.0F / PI;
  }
  d.valueCount = SAVE_VALUE_COUNT;
  for (int i = 0; i < SAVE_VALUE_COUNT; ++i) d.values[i] = saveValues[i];
  d.textCount = SAVE_TEXT_COUNT;
  for (int i = 0; i < SAVE_TEXT_COUNT; ++i)
    snprintf(d.texts[i], SAVE_TEXT_LEN, "%s", &saveTexts[i * SAVE_TEXT_LEN]);
  d.objectCount = 0;
  for (int i = 0;
       i < (int)runtimeObjects.size() && d.objectCount < SAVE_OBJECT_MAX; ++i) {
    if (!SCENE_OBJECTS[i].saveState) continue;
    SaveObjectState& st = d.objects[d.objectCount++];
    st.index = i;
    for (int a = 0; a < 3; ++a) st.position[a] = runtimeObjects[i].data.position[a];
    for (int a = 0; a < 3; ++a) st.color[a] = runtimeObjects[i].data.color[a];
    st.visible = runtimeObjects[i].visible ? 1 : 0;
  }
  const bool ok = saveWrite(slot, d);
  if (ok) slotUsed[slot] = true;
  saveFeedback = ok ? 1 : 3;
  saveFeedbackFrames = everyFrames(1.8F);  // ~1.8 s
}

void TerrainGame::doLoad(int slot) {
  static SaveGameData d;
  if (!saveRead(slot, d)) {
    saveFeedback = 3;
    saveFeedbackFrames = everyFrames(1.8F);
    return;
  }
  for (int i = 0; i < d.valueCount && i < SAVE_VALUE_COUNT; ++i)
    saveValues[i] = d.values[i];
  for (int i = 0; i < d.textCount && i < SAVE_TEXT_COUNT; ++i) {
    d.texts[i][SAVE_TEXT_LEN - 1] = '\0';  // corrupted cards happen
    snprintf(&saveTexts[i * SAVE_TEXT_LEN], SAVE_TEXT_LEN, "%s", d.texts[i]);
  }
  pendingObjState.assign(d.objects, d.objects + d.objectCount);
  pendingObjScene = d.scene;
  // Restore the player through the teleport request - the flag survives a
  // scene switch and covers both player kinds.
  scriptCtx.teleport = true;
  scriptCtx.teleportPos = Vec4(d.playerPos[0], d.playerPos[1], d.playerPos[2]);
  scriptCtx.teleportYaw = d.playerYaw;
  if (d.scene != currentScene)
    scriptCtx.requestScene = d.scene;  // object state applies after the load
  else
    applySavedObjects();
  saveMenuOpen = false;
  saveFeedback = 2;
  saveFeedbackFrames = 90;
}

void TerrainGame::applySavedObjects() {
  for (const SaveObjectState& st : pendingObjState) {
    if (st.index < 0 || st.index >= (int)runtimeObjects.size()) continue;
    RuntimeObject& o = runtimeObjects[st.index];
    for (int a = 0; a < 3; ++a) o.data.position[a] = st.position[a];
    for (int a = 0; a < 3; ++a) o.data.color[a] = st.color[a];
    o.visible = st.visible != 0;
    o.velocityY = 0.0F;
    o.dirty = true;
  }
  pendingObjState.clear();
  pendingObjScene = -1;
}

void TerrainGame::renderSaveMenu() {
  if (saveMenuOpen) {
    engine->renderer.renderer2D.render(menuDimSprite);
    engine->renderer.renderer2D.render(saveMenuSprite);
    // slot rows sit at y = 40 + slot * 24 inside the panel sprite
    const float baseY = saveMenuSprite.position.y;
    saveCursorSprite.position.y = baseY + 41.0F + saveMenuSlot * 24.0F;
    engine->renderer.renderer2D.render(saveCursorSprite);
    for (int i = 0; i < SAVE_SLOTS; ++i) {
      if (!slotUsed[i]) continue;
      saveUsedSprite.position.y = baseY + 42.0F + i * 24.0F;
      engine->renderer.renderer2D.render(saveUsedSprite);
    }
  }
  if (saveFeedbackFrames > 0 && saveFeedback >= 1 && saveFeedback <= 3)
    engine->renderer.renderer2D.render(saveFeedbackSprites[saveFeedback - 1]);
}

// --- Game menus (menu_data.gen.hpp) ---------------------------------------
// Panels are baked by the editor; the runtime only moves a cursor and runs
// entry actions. Dpad picks a row, Cross selects, Triangle pops the submenu
// stack (or closes; a title screen's root cannot be dismissed with Back).
// The Start button opens/closes the designated pause menu (PAUSE_MENU).
// Returns true while an open menu PAUSES gameplay - menus with the pause
// flag off float over the running game (pad presses reach both).
bool TerrainGame::updateGameMenu() {
  scriptCtx.menuEvent = -1;
  auto pausing = [&] {
    return gameMenuIndex >= 0 && MENUS[gameMenuIndex].pause != 0;
  };

  if (scriptCtx.openMenu >= 0) {
    const int target = scriptCtx.openMenu;
    scriptCtx.openMenu = -1;
    if (target < MENU_COUNT && !saveMenuOpen && gameMenuIndex < 0) {
      gameMenuIndex = target;
      gameMenuCursor = 0;
      gameMenuStackDepth = 0;
      gameMenuGrace = 15;  // pad-garbage grace (see updateSaveMenu)
      useTargetIndex = -1;
      return pausing();
    }
  }

  // Start toggles the pause menu: opens it during gameplay, closes it again
  // while its root is showing (submenus first go back with Triangle).
  if (PAUSE_MENU >= 0 && !saveMenuOpen && engine->pad.getClicked().Start) {
    if (gameMenuIndex < 0) {
      gameMenuIndex = PAUSE_MENU;
      gameMenuCursor = 0;
      gameMenuStackDepth = 0;
      gameMenuGrace = 15;
      useTargetIndex = -1;
      return pausing();
    }
    if (gameMenuIndex == PAUSE_MENU && gameMenuStackDepth == 0 &&
        gameMenuGrace == 0) {
      gameMenuIndex = -1;
      return false;
    }
  }

  if (gameMenuIndex < 0) return false;
  if (saveMenuOpen) return pausing();  // save menu on top - hold, no pad
  if (gameMenuGrace > 0) {
    --gameMenuGrace;
    return pausing();
  }

  const MenuData& m = MENUS[gameMenuIndex];
  const auto& clicked = engine->pad.getClicked();
  if (clicked.DpadUp && m.entryCount > 0)
    gameMenuCursor = (gameMenuCursor + m.entryCount - 1) % m.entryCount;
  if (clicked.DpadDown && m.entryCount > 0)
    gameMenuCursor = (gameMenuCursor + 1) % m.entryCount;

  if (clicked.Triangle) {
    if (gameMenuStackDepth > 0) {
      gameMenuIndex = gameMenuStack[--gameMenuStackDepth];
      gameMenuCursor = 0;
    } else if (!m.titleScreen) {
      gameMenuIndex = -1;
    }
    return pausing();
  }

  if (clicked.Cross && gameMenuCursor >= 0 && gameMenuCursor < m.entryCount) {
    const MenuEntryData& e = m.entries[gameMenuCursor];
    switch (e.action) {
      case 0:  // close
        gameMenuIndex = -1;
        gameMenuStackDepth = 0;
        break;
      case 1:  // switch scene
        if (e.param >= 0) {
          scriptCtx.requestScene = e.param;
          gameMenuIndex = -1;
          gameMenuStackDepth = 0;
        }
        break;
      case 2:  // open save menu (replaces this menu next frame)
        gameMenuIndex = -1;
        gameMenuStackDepth = 0;
        scriptCtx.openSaveMenu = true;
        break;
      case 3:  // open submenu
        if (e.param >= 0 && e.param < MENU_COUNT &&
            gameMenuStackDepth < (int)(sizeof(gameMenuStack) / sizeof(int))) {
          gameMenuStack[gameMenuStackDepth++] = gameMenuIndex;
          gameMenuIndex = e.param;
          gameMenuCursor = 0;
        }
        break;
      case 4:  // set save value (menu stays open)
        if (e.param >= 0 && e.param < SAVE_VALUE_COUNT)
          saveValues[e.param] = e.amount;
        break;
      case 5:  // add to save value
        if (e.param >= 0 && e.param < SAVE_VALUE_COUNT)
          saveValues[e.param] += e.amount;
        break;
      case 6:  // flow event: scripts run this frame to catch it
        scriptCtx.menuEvent = e.param;
        break;
    }
  }
  return pausing();
}

void TerrainGame::renderGameMenu() {
  if (gameMenuIndex < 0 || gameMenuIndex >= (int)menuSprites.size()) return;
  if (saveMenuOpen) return;  // the save menu draws on top instead
  const MenuData& m = MENUS[gameMenuIndex];
  Sprite& panel = menuSprites[gameMenuIndex];
  if (m.pause) engine->renderer.renderer2D.render(menuDimSprite);
  engine->renderer.renderer2D.render(panel);
  if (m.entryCount > 0) {
    menuCursorSprite.position =
        Vec2(panel.position.x + 32.0F,
             panel.position.y + m.row0Y + gameMenuCursor * m.rowH + 1.0F);
    engine->renderer.renderer2D.render(menuCursorSprite);
  }
}

bool TerrainGame::updatePlayerEntity() {
  if (PLAYER_INDEX < 0) return false;

  const auto& leftJoy = engine->pad.getLeftJoyPad();
  const auto& rightJoy = engine->pad.getRightJoyPad();
  // ANALOG_DEADZONE_L/_R (Preferences > Input) zero resting drift per stick;
  // above the deadzone the value rescales from 0 so the edge does not step.
  auto axis = [](const u8& raw, const float dz) {
    const float v = (raw - 128.0F) / 128.0F;
    const float mag = v < 0.0F ? -v : v;
    if (mag <= dz) return 0.0F;
    const float scaled = (mag - dz) / (1.0F - dz);
    return v < 0.0F ? -scaled : scaled;
  };

  // Right stick: look around (stick right = turn right)
  entYaw -= axis(rightJoy.h, ANALOG_DEADZONE_R) * 0.05F * PLAYER_LOOK_SPEED * g_frameScale;
  entPitch -=
      axis(rightJoy.v, ANALOG_DEADZONE_R) * 0.035F * PLAYER_LOOK_SPEED * g_frameScale;
  if (entPitch > 1.35F) entPitch = 1.35F;
  if (entPitch < -1.35F) entPitch = -1.35F;

  const float fx = sinf(entYaw);
  const float fz = cosf(entYaw);
  const float forward = -axis(leftJoy.v, ANALOG_DEADZONE_L);
  const float strafe = axis(leftJoy.h, ANALOG_DEADZONE_L);

  if (PLAYER_MODE == 1) {
    // Noclip: fly where the camera looks; X up, Square down.
    const float cp = cosf(entPitch);
    const float step = PLAYER_WALK_SPEED * g_frameScale;
    entX += (fx * cp * forward - fz * strafe) * step;
    entZ += (fz * cp * forward + fx * strafe) * step;
    entY += sinf(entPitch) * forward * step;
    if (engine->pad.getPressed().BTN_FLY_UP) entY += step;
    if (engine->pad.getPressed().BTN_FLY_DOWN) entY -= step;

    cameraPosition = Vec4(entX, entY, entZ);
    cameraLookAt = Vec4(entX + fx * cp, entY + sinf(entPitch), entZ + fz * cp);
    return true;
  }

  // Walk mode: terrain bounds, object collision, gravity + jump.
  float nextX = entX + (fx * forward - fz * strafe) * PLAYER_WALK_SPEED * g_frameScale;
  float nextZ = entZ + (fz * forward + fx * strafe) * PLAYER_WALK_SPEED * g_frameScale;

  const float limX = TERRAIN_WIDTH * 0.5F - 1.0F;
  const float limZ = TERRAIN_DEPTH * 0.5F - 1.0F;
  if (nextX > limX) nextX = limX;
  if (nextX < -limX) nextX = -limX;
  if (nextZ > limZ) nextZ = limZ;
  if (nextZ < -limZ) nextZ = -limZ;

  float ground = terrainHeightAt(nextX, nextZ);
  collidePlayer(entX, entZ, &nextX, &nextZ, entY, PLAYER_EYE_HEIGHT, &ground);
  entX = nextX;
  entZ = nextZ;

  entVelY -= GRAVITY * g_frameDt * g_frameDt;  // GRAVITY is units/s^2
  entY += entVelY;
  if (entY <= ground) {
    entY = ground;
    entVelY = 0.0F;
    if (PLAYER_CAN_JUMP && engine->pad.getClicked().BTN_JUMP)
      entVelY = PLAYER_JUMP_SPEED * g_frameDt;  // units/s
  }

  const float eyeY = entY + PLAYER_EYE_HEIGHT;
  cameraPosition = Vec4(entX, eyeY, entZ);
  cameraLookAt = Vec4(entX + fx * cosf(entPitch), eyeY + sinf(entPitch),
                      entZ + fz * cosf(entPitch));
  return true;
}

void TerrainGame::buildSkyDome() {
  if (!SKY_DOME) return;

  const float diag = TERRAIN_WIDTH > TERRAIN_DEPTH ? TERRAIN_WIDTH : TERRAIN_DEPTH;
  float radius = diag * 1.5F;
  if (radius < 60.0F) radius = 60.0F;
  if (radius > 450.0F) radius = 450.0F;

  const int stacks = 6, slices = 14;
  auto skyAt = [&](float t) {  // t: 0 = horizon, 1 = zenith
    return Color(skyHorizonR + (SKY_TOP_R - skyHorizonR) * t,
                 skyHorizonG + (SKY_TOP_G - skyHorizonG) * t,
                 skyHorizonB + (SKY_TOP_B - skyHorizonB) * t, 128.0F);
  };
  auto domeVert = [&](int stack, int slice) {
    // Start slightly below the horizon so the seam is never visible
    const float lat = -0.06F + (PI * 0.5F + 0.06F) * stack / stacks;
    const float lon = 2.0F * PI * slice / slices;
    return Vec4(radius * cosf(lat) * cosf(lon), radius * sinf(lat),
                radius * cosf(lat) * sinf(lon), 1.0F);
  };

  skyDome.vertices.clear();
  skyDome.colors.clear();
  for (int st = 0; st < stacks; ++st) {
    const float t0 = (float)st / stacks, t1 = (float)(st + 1) / stacks;
    for (int sl = 0; sl < slices; ++sl) {
      const Vec4 v00 = domeVert(st, sl), v01 = domeVert(st, sl + 1);
      const Vec4 v10 = domeVert(st + 1, sl), v11 = domeVert(st + 1, sl + 1);
      skyDome.vertices.push_back(v00);
      skyDome.vertices.push_back(v10);
      skyDome.vertices.push_back(v11);
      skyDome.vertices.push_back(v00);
      skyDome.vertices.push_back(v11);
      skyDome.vertices.push_back(v01);
      skyDome.colors.push_back(skyAt(t0));
      skyDome.colors.push_back(skyAt(t1));
      skyDome.colors.push_back(skyAt(t1));
      skyDome.colors.push_back(skyAt(t0));
      skyDome.colors.push_back(skyAt(t1));
      skyDome.colors.push_back(skyAt(t0));
    }
  }

  skyDome.infoBag = std::make_unique<StaPipInfoBag>();
  skyDome.infoBag->model = &model;
  skyDome.infoBag->shadingType = TyraShadingGouraud;
  // Static geometry crossing the screen edges all the time - needs clipping
  skyDome.infoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
  skyDome.infoBag->fullClipChecks = true;
  skyDome.colorBag = std::make_unique<StaPipColorBag>();
  skyDome.colorBag->many = skyDome.colors.data();
  skyDome.bag = std::make_unique<StaPipBag>();
  skyDome.bag->info = skyDome.infoBag.get();
  skyDome.bag->color = skyDome.colorBag.get();
  skyDome.bag->vertices = skyDome.vertices.data();
  skyDome.bag->count = static_cast<u32>(skyDome.vertices.size());
  skyDome.bag->texture = nullptr;
  skyDome.bag->lighting = nullptr;
  static u32 domeVersion = 0;  // dome rebuilds on retint - skip stale bboxes
  skyDome.bag->bboxVersion = ++domeVersion;
}

void TerrainGame::rebuildObjectGeometry(int index) {
  RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];
  o.dirty = false;

  // models: one draw part per MTL material; everything else fills parts[0]
  const GameModel* gm = nullptr;
  if (o.data.type == 5 && o.data.model >= 0 &&
      o.data.model < (int)gameModels.size())
    gm = &gameModels[o.data.model];
  const int partCount = o.data.type == 5 ? (gm ? (int)gm->parts.size() : 0) : 1;
  if ((int)g.parts.size() != partCount) g.parts.resize(partCount);

  for (int pi = 0; pi < partCount; ++pi) {
    GeoPart& part = g.parts[pi];
    part.vertices.clear();
    part.colors.clear();
    part.sts.clear();
  }

  // primitives: the assigned material (first entry of its .mtl) supplies
  // the surface color/texture - staged for the builders via g_prim*
  const GameMaterial* gmat = nullptr;
  if (o.data.type != 5 && o.data.material >= 0 &&
      o.data.material < (int)gameMaterials.size())
    gmat = &gameMaterials[o.data.material];

  if (o.data.type == 5) {
    for (int pi = 0; pi < partCount; ++pi) {
      const GameModelPart& src = gm->parts[pi];
      GeoPart& part = g.parts[pi];
      const bool textured = src.texture != nullptr;
      for (size_t i = 0; i + 7 < src.verts.size(); i += 8) {
        const float* v = &src.verts[i];
        pushVert(part.vertices, part.colors, part.sts, o.data,
                 {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6], v[7], src.kd,
                 textured);
      }
    }
  } else {
    g_primKd = gmat ? gmat->kd : nullptr;
    g_primTextured = gmat && gmat->texture;
    GeoPart& p0 = g.parts[0];
    switch (o.data.type) {
      case 1: addSphere(p0.vertices, p0.colors, p0.sts, o.data); break;
      case 2: addCylinder(p0.vertices, p0.colors, p0.sts, o.data); break;
      case 3: addCone(p0.vertices, p0.colors, p0.sts, o.data); break;
      case 4: break;   // spawn point - marker only
      case 6: break;   // player - marker only
      case 7: break;   // emitter - particles are built by updateParticles()
      case 8: break;   // sound emitter - marker only, no geometry
      case 9: break;   // point light - invisible source, no geometry
      case 11: break;  // empty - pure transform, no geometry
      default: addBox(p0.vertices, p0.colors, p0.sts, o.data); break;
    }
    g_primKd = nullptr;
    g_primTextured = false;
  }

  for (int pi = 0; pi < partCount; ++pi) {
    GeoPart& part = g.parts[pi];
    if (part.vertices.empty()) {
      part.bag.reset();
      continue;
    }
    if (!part.bag) {
      part.infoBag = std::make_unique<StaPipInfoBag>();
      part.infoBag->model = &model;
      part.infoBag->shadingType = TyraShadingFlat;
      // Objects go through frustum classification too - raw submission (None)
      // wraps the GS raster window for anything behind/off-screen. The bbox
      // cache is keyed by pointer + bboxVersion, bumped on every rebuild, so
      // moving objects never reuse a stale box.
      part.infoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
      part.infoBag->fullClipChecks = true;
      part.colorBag = std::make_unique<StaPipColorBag>();
      part.bag = std::make_unique<StaPipBag>();
      part.bag->info = part.infoBag.get();
      part.bag->color = part.colorBag.get();
      part.bag->texture = nullptr;
      part.bag->lighting = nullptr;
    }
    part.colorBag->many = part.colors.data();
    part.bag->vertices = part.vertices.data();
    part.bag->count = static_cast<u32>(part.vertices.size());
    part.bag->bboxVersion++;  // geometry changed - refresh the bbox cache

    // models: the part's own map_Kd; primitives: the assigned material's
    Texture* tex =
        o.data.type == 5 ? gm->parts[pi].texture : (gmat ? gmat->texture : nullptr);
    if (tex) {
      if (!part.texBag) part.texBag = std::make_unique<StaPipTextureBag>();
      part.texBag->texture = tex;
      part.texBag->coordinates = part.sts.data();
      part.bag->texture = part.texBag.get();
    } else {
      part.bag->texture = nullptr;
    }
  }
}

void TerrainGame::updateObjectPhysics() {
  // GRAVITY is units/s^2
  const float gravityPerFrame = GRAVITY * g_frameDt * g_frameDt;
  for (RuntimeObject& o : runtimeObjects) {
    if (!o.data.physics) continue;
    const float half = 0.5F * o.data.scale[1];
    const float floorY =
        terrainHeightAt(o.data.position[0], o.data.position[2]) + half;
    if (o.velocityY == 0.0F && o.data.position[1] <= floorY) continue;  // resting

    o.velocityY -= gravityPerFrame;
    o.data.position[1] += o.velocityY;
    if (o.data.position[1] <= floorY) {
      o.data.position[1] = floorY;
      o.velocityY = 0.0F;
    }
    o.dirty = true;
  }
}

void TerrainGame::renderScene() {
  // Scripts changing ctx.skyColor retint the dome horizon
  if (skyDome.bag && (scriptCtx.skyColor.r != skyHorizonR ||
                      scriptCtx.skyColor.g != skyHorizonG ||
                      scriptCtx.skyColor.b != skyHorizonB)) {
    skyHorizonR = scriptCtx.skyColor.r;
    skyHorizonG = scriptCtx.skyColor.g;
    skyHorizonB = scriptCtx.skyColor.b;
    buildSkyDome();
  }
  if (skyDome.bag) stapip.core.render(skyDome.bag.get());
  stapip.core.render(bag.get());
  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    if (runtimeObjects[i].dirty) rebuildObjectGeometry(i);
    if (!runtimeObjects[i].visible) continue;
    for (GeoPart& part : objectGeometry[i].parts)
      if (part.bag) stapip.core.render(part.bag.get());
  }
  // Animated models: advance playback, then skin + draw the in-view ones
  // through the same static pipeline (see updateAndRenderAnimObjects)
  updateAndRenderAnimObjects();
  // Highlight rims after every object so their depth is in the z-buffer:
  // the rim is depth-tested against the finished scene and can no longer be
  // punched through by an object drawn later in the loop.
  if (HIGHLIGHT_USABLE)
    for (int i = 0; i < (int)runtimeObjects.size(); ++i)
      if (runtimeObjects[i].visible && runtimeObjects[i].data.usable &&
          !objectGeometry[i].parts.empty())
        renderHighlightHull(i);  // proximity-checked inside; no-op when far
  // particles last - alpha blended over the scene
  for (ParticleSystem& ps : particles)
    if (ps.bag && ps.bag->count > 0) stapip.core.render(ps.bag.get());
}

// Usable-object highlight (Project > Preferences): a soft colored rim around
// the object silhouette. Three concentric copies of the object, grown around
// its center with fading alpha, drawn after the scene with z-test but no
// z-write (PipelineZTest_TestOnly). Each shell is additionally pushed away
// from the camera by a uniform scale around the eye point - that keeps its
// screen silhouette identical but places it behind the object's own depth,
// so the z-buffer rejects the interior and only the rim survives, correctly
// occluded by anything nearer. Proximity uses the same reference point as
// the USE interaction (the camera / player eye).
void TerrainGame::renderHighlightHull(int index) {
  const RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];
  size_t n = 0;  // hull spans every draw part of the object
  for (const GeoPart& part : g.parts) n += part.vertices.size();
  if (n == 0) return;

  float half = o.data.scale[0];
  if (o.data.scale[1] > half) half = o.data.scale[1];
  if (o.data.scale[2] > half) half = o.data.scale[2];
  half *= 0.5F;
  if (half < 0.01F) half = 0.01F;

  const float dx = o.data.position[0] - cameraPosition.x;
  const float dy = o.data.position[1] - cameraPosition.y;
  const float dz = o.data.position[2] - cameraPosition.z;
  const float dist2 = dx * dx + dy * dy + dz * dz;
  const float reach = HIGHLIGHT_DISTANCE + half;
  if (dist2 > reach * reach) return;
  const float dist = sqrtf(dist2);

  // HIGHLIGHT_STEPS concentric shells up to HIGHLIGHT_WIDTH wide (both from
  // Project > Preferences), alpha roughly halving outward. Near the
  // silhouette all shells overlap - solid color fading outward.
  const float cx = o.data.position[0], cy = o.data.position[1],
              cz = o.data.position[2];
  // How far in front of the object the camera is - the pushback must move
  // the shell past the object's front surface without reaching things
  // right behind it.
  float behind = dist - half;
  if (behind < 0.5F) behind = 0.5F;

  g.hullVerts.resize(n * HIGHLIGHT_STEPS);
  g.hullCols.resize(n * HIGHLIGHT_STEPS);
  // One pushback for all shells (sized for the widest) - per-shell depths
  // would make the terrain/scene cut each shell on a different line and the
  // rim edge turns into visible steps.
  float growMax = HIGHLIGHT_WIDTH / half;
  if (growMax > 0.6F) growMax = 0.6F;
  const float k = 1.0F + (growMax * half + 0.15F) / behind;
  float alpha = HIGHLIGHT_STEPS > 1 ? 72.0F : 100.0F;  // single step = solid
  for (int s = 0; s < HIGHLIGHT_STEPS; ++s) {
    // uniform growth - rotated objects stay unskewed
    float grow = (HIGHLIGHT_WIDTH * (s + 1)) / (HIGHLIGHT_STEPS * half);
    if (grow > 0.6F) grow = 0.6F;
    const float f = 1.0F + grow;
    const Color c(HIGHLIGHT_R, HIGHLIGHT_G, HIGHLIGHT_B, alpha);
    alpha *= 0.55F;
    size_t v = 0;
    for (const GeoPart& part : g.parts)
      for (const Vec4& p : part.vertices) {
        float hx = cx + (p.x - cx) * f;
        float hy = cy + (p.y - cy) * f;
        float hz = cz + (p.z - cz) * f;
        hx = cameraPosition.x + (hx - cameraPosition.x) * k;
        hy = cameraPosition.y + (hy - cameraPosition.y) * k;
        hz = cameraPosition.z + (hz - cameraPosition.z) * k;
        // Shell parts of grounded objects dip below the terrain and the
        // ground in front z-rejects them (no bottom rim from a low camera).
        // Lift them just above the surface - the bottom rim becomes a glow
        // apron hugging the ground around the base.
        const float ground = terrainHeightAt(hx, hz) + 0.02F;
        if (hy < ground) hy = ground;
        g.hullVerts[s * n + v] = Vec4(hx, hy, hz, 1.0F);
        g.hullCols[s * n + v] = c;
        ++v;
      }
  }

  if (!g.hullBag) {
    g.hullInfoBag = std::make_unique<StaPipInfoBag>();
    g.hullInfoBag->model = &model;
    g.hullInfoBag->shadingType = TyraShadingFlat;
    g.hullInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
    g.hullInfoBag->fullClipChecks = true;
    g.hullInfoBag->zTestType = PipelineZTest_TestOnly;
    g.hullColorBag = std::make_unique<StaPipColorBag>();
    g.hullBag = std::make_unique<StaPipBag>();
    g.hullBag->info = g.hullInfoBag.get();
    g.hullBag->color = g.hullColorBag.get();
    g.hullBag->texture = nullptr;
    g.hullBag->lighting = nullptr;
  }
  g.hullColorBag->many = g.hullCols.data();
  g.hullBag->vertices = g.hullVerts.data();
  g.hullBag->count = static_cast<u32>(g.hullVerts.size());
  g.hullBag->bboxVersion++;  // rebuilt every frame while highlighted
  stapip.core.render(g.hullBag.get());
  // The pushback clears the object's front face but not its receding side
  // faces - shells still blend over those at glancing angles. Repainting
  // the object wins the equal-depth test (GEQUAL) and erases the wash
  // without touching the rim outside the silhouette.
  for (GeoPart& part : g.parts)
    if (part.bag) stapip.core.render(part.bag.get());
}

void TerrainGame::generateTerrainGrid() {
  // The grid follows the heightmap (terrain_heights.gen.hpp): one cell per
  // heightmap quad, vertex heights sampled directly, per-vertex shading
  // from the height gradient.
  const u32 cellsX = HM_W - 1;
  const u32 cellsZ = HM_D - 1;
  const float stepX = TERRAIN_WIDTH / cellsX;
  const float stepZ = TERRAIN_DEPTH / cellsZ;
  const float startX = -TERRAIN_WIDTH * 0.5F;
  const float startZ = -TERRAIN_DEPTH * 0.5F;

  auto hAt = [&](int ix, int iz) {
    if (ix < 0) ix = 0;
    if (iz < 0) iz = 0;
    if (ix > HM_W - 1) ix = HM_W - 1;
    if (iz > HM_D - 1) iz = HM_D - 1;
    return TERRAIN_HEIGHTS[iz * HM_W + ix];
  };
  auto shadeAt = [&](int ix, int iz) -> V3 {
    V3 n = {hAt(ix - 1, iz) - hAt(ix + 1, iz), 2.0F * (stepX < stepZ ? stepX : stepZ),
            hAt(ix, iz - 1) - hAt(ix, iz + 1)};
    const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 0.00001F) n.x /= len, n.y /= len, n.z /= len;
    V3 s = shadeOf(n);
    const V3 wp = {startX + ix * stepX, hAt(ix, iz), startZ + iz * stepZ};
    const V3 pl = pointLightAt(wp, n);
    s.x += pl.x, s.y += pl.y, s.z += pl.z;
    if (s.x > 1.0F) s.x = 1.0F;
    if (s.y > 1.0F) s.y = 1.0F;
    if (s.z > 1.0F) s.z = 1.0F;
    return s;
  };

  // Untextured: two greens in a checker pattern. Textured: a neutral gray
  // that modulates the texture 1:1 (PS2 modulation: 128 = 1.0).
  const bool textured = TERRAIN_TEXTURE >= 0;
  const float baseA[3] = {textured ? 128.0F : 96.0F, textured ? 128.0F : 160.0F,
                          textured ? 128.0F : 72.0F};
  const float baseB[3] = {textured ? 128.0F : 74.0F, textured ? 128.0F : 128.0F,
                          textured ? 128.0F : 56.0F};

  for (u32 z = 0; z < cellsZ; ++z) {
    for (u32 x = 0; x < cellsX; ++x) {
      const float x0 = startX + x * stepX;
      const float x1 = x0 + stepX;
      const float z0 = startZ + z * stepZ;
      const float z1 = z0 + stepZ;
      const float* base = ((x + z) % 2 == 0) ? baseA : baseB;

      const float h00 = hAt(x, z), h10 = hAt(x + 1, z);
      const float h01 = hAt(x, z + 1), h11 = hAt(x + 1, z + 1);
      const V3 s00 = shadeAt(x, z), s10 = shadeAt(x + 1, z);
      const V3 s01 = shadeAt(x, z + 1), s11 = shadeAt(x + 1, z + 1);
      auto shaded = [&](const V3& s) {
        return Color(base[0] * s.x, base[1] * s.y, base[2] * s.z, 128.0F);
      };
      auto st = [&](float wx, float wz) {
        terrainSts.push_back(
            Vec4(wx / TERRAIN_TEX_SCALE, wz / TERRAIN_TEX_SCALE, 1.0F, 0.0F));
      };

      vertices.push_back(Vec4(x0, h00, z0, 1.0F));
      vertices.push_back(Vec4(x1, h10, z0, 1.0F));
      vertices.push_back(Vec4(x0, h01, z1, 1.0F));
      vertices.push_back(Vec4(x1, h10, z0, 1.0F));
      vertices.push_back(Vec4(x1, h11, z1, 1.0F));
      vertices.push_back(Vec4(x0, h01, z1, 1.0F));

      st(x0, z0);
      st(x1, z0);
      st(x0, z1);
      st(x1, z0);
      st(x1, z1);
      st(x0, z1);

      colors.push_back(shaded(s00));
      colors.push_back(shaded(s10));
      colors.push_back(shaded(s01));
      colors.push_back(shaded(s10));
      colors.push_back(shaded(s11));
      colors.push_back(shaded(s01));
    }
  }
}
)";

static const char* TPL_GAME_CPP_ORBIT_TAIL = R"(
void TerrainGame::updateCameraOrbit() {
  orbitAngle += 0.005F * ORBIT_SPEED * g_frameScale;
  const float diag = TERRAIN_WIDTH > TERRAIN_DEPTH ? TERRAIN_WIDTH : TERRAIN_DEPTH;
  const float orbitRadius = diag * 0.9F;
  const float orbitHeight = diag * 0.55F;

  cameraPosition.x = orbitRadius * cosf(orbitAngle);
  cameraPosition.y = orbitHeight;
  cameraPosition.z = orbitRadius * sinf(orbitAngle);
}
)";

// FPP template: walk with the left stick, look around with the right stick.
// Axis conventions follow the official Tyra demo (v<128 = stick up).
static const char* TPL_GAME_CPP_FPP_HEAD = R"(
TerrainGame::TerrainGame(Engine* t_engine)
    : engine(t_engine),
      playerX(0.0F),
      playerZ(0.0F),
      yaw(0.0F),
      pitch(0.0F),
      playerY(0.0F),
      playerVelY(0.0F),
      model(M4x4::Identity) {}

TerrainGame::~TerrainGame() {}

void TerrainGame::init() {
  // Engine clipper fix: the default clipMargin (-10.0F) moves the near
  // clipping plane ~10 units away from the camera, cutting away nearby
  // geometry. Clip right in front of the real near plane instead.
  // (read by the clipper during setRenderer below)
  PlanesClipAlgorithm::clipMargin =
      -(engine->renderer.core.getSettings().getNear() + 0.5F);

  // Wall-clock normalization: per-frame steps below are tuned for 50 Hz;
  // g_frameScale stretches them so NTSC's 60 Hz plays at the same speed.
  // These are the seeds - updateFrameClock() refreshes both every frame
  // from the measured frame time (frame drops slow the picture, not the
  // game; also what makes the vsync-off build play at the right speed).
  g_frameRate = engine->renderer.core.getSettings().getRefreshRate();
  g_frameDt = 1.0F / g_frameRate;
  g_frameScale = 50.0F / g_frameRate;
  // Experimental (Project > Preferences > Build): skip the vsync wait -
  // continuous frame rate instead of the 50/25 vsync snap, with tearing.
  if (!FRAME_LIMIT) engine->renderer.core.setFrameLimit(false);

  stapip.setRenderer(&engine->renderer.core);
  engine->renderer.core.postFx.setBloom(POSTFX_BLOOM);
  engine->renderer.core.postFx.setGrain(POSTFX_GRAIN);
  // Default color grading look (Tools > Color Grading); no-op when -1.
  // Grading is global - scene switches keep whatever preset is active.
  applySceneGrading(engine, GRADING_DEFAULT);

  engine->renderer.setClearScreenColor(Color(SKY_R, SKY_G, SKY_B));

  // Player start: the first spawn point in the scene (if any)
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    if (SCENE_OBJECTS[i].type == 4) {
      playerX = SCENE_OBJECTS[i].position[0];
      playerZ = SCENE_OBJECTS[i].position[2];
      yaw = SCENE_OBJECTS[i].rotation[1] * PI / 180.0F;
      break;
    }
  }
  playerY = terrainHeightAt(playerX, playerZ);

  updatePlayer();
  buildScene();

  scriptCtx.engine = engine;
  scriptCtx.objects = runtimeObjects.data();
  scriptCtx.objectCount = (int)runtimeObjects.size();
  scriptCtx.skyColor = Color(SKY_R, SKY_G, SKY_B);
  scriptCtx.playerPosition = cameraPosition;
  for (Script* script : getScripts()) script->init(scriptCtx);

  // HUD sprites (see hud_data.gen.hpp)
  const auto& screen = engine->renderer.core.getSettings();
  hudSprites.reserve(HUD_COUNT);
  for (int i = 0; i < HUD_COUNT; ++i) {
    const HudImageData& h = HUD_IMAGES[i];
    Sprite sprite;
    sprite.mode = SpriteMode::MODE_STRETCH;
    sprite.size = Vec2(h.w, h.h);
    sprite.position = Vec2(h.x * screen.getWidth() - h.w * 0.5F,
                           h.y * screen.getHeight() - h.h * 0.5F);
    hudSprites.push_back(sprite);
    auto* texture =
        engine->renderer.getTextureRepository().add(FileUtils::fromCwd(h.path));
    texture->addLink(hudSprites.back().id);
  }

  // "USE" prompt (res/hud/use.png), shown while looking at a usable object
  usePromptSprite.mode = SpriteMode::MODE_STRETCH;
  usePromptSprite.size = Vec2(128.0F, 32.0F);
  usePromptSprite.position = Vec2((screen.getWidth() - 128.0F) * 0.5F,
                                  screen.getHeight() * 0.72F);
  auto* useTexture =
      engine->renderer.getTextureRepository().add(FileUtils::fromCwd("hud/use.png"));
  useTexture->addLink(usePromptSprite.id);

  // Loading screen sprite (res/hud/loading.png), shown on scene switches
  loadingSprite.mode = SpriteMode::MODE_STRETCH;
  loadingSprite.size = Vec2(256.0F, 64.0F);
  loadingSprite.position = Vec2((screen.getWidth() - 256.0F) * 0.5F,
                                (screen.getHeight() - 64.0F) * 0.5F);
  auto* loadingTexture = engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd("hud/loading.png"));
  loadingTexture->addLink(loadingSprite.id);

  // Sound emitter samples (adpenc output next to the ELF)
  for (int i = 0; i < SND_COUNT; ++i)
    sndSamples.push_back(
        engine->audio.adpcm.load(FileUtils::fromCwd(SND_PATHS[i])));
}

void TerrainGame::loop() {
  updateFrameClock();  // real dt: frame drops slow the picture, not the game
  const bool saveMenuActive = updateSaveMenu();
  const bool gameMenuPausing = updateGameMenu();  // false for overlay menus
  const bool menuActive = saveMenuActive || gameMenuPausing;
  if (!menuActive) {
    if (!updatePlayerEntity()) updatePlayer();
    updateUseTarget();
  }

  scriptCtx.playerPosition = cameraPosition;
  if (menuActive) scriptCtx.usedObject = -1;
  // Menus pause scripts - except the frame a menu entry fires a flow event,
  // which must reach the On Menu Event triggers.
  if (!menuActive || scriptCtx.menuEvent >= 0)
    for (Script* script : getScripts()) script->update(scriptCtx);
  engine->renderer.setClearScreenColor(scriptCtx.skyColor);

  // Scene switch requested by the flow graph / scripts. The built-in FPP
  // player respawns at the new scene's spawn point.
  static bool fppSpawnPending = false;
  if (scriptCtx.requestScene >= 0) {
    const int target = scriptCtx.requestScene;
    scriptCtx.requestScene = -1;
    if (LOADING_SCREEN) {
      loadingTarget = target;
      loadingFrames = everyFrames(0.7F);  // ~0.7s hold
    } else {
      loadScene(target);
      fppSpawnPending = true;
    }
  }
  if (loadingFrames > 0) {
    engine->renderer.setClearScreenColor(Color(0.0F, 0.0F, 0.0F));
    engine->renderer.beginFrame();
    engine->renderer.renderer2D.render(loadingSprite);
    engine->renderer.endFrame();
    --loadingFrames;
    if (loadingFrames == everyFrames(0.7F) - 5) {  // a few frames shown first
      loadScene(loadingTarget);
      fppSpawnPending = true;
    }
    return;
  }
  if (fppSpawnPending) {
    // The built-in FPP player respawns at the new scene's spawn point.
    fppSpawnPending = false;
    playerX = 0.0F;
    playerZ = 0.0F;
    yaw = 0.0F;
    pitch = 0.0F;
    for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
      if (SCENE_OBJECTS[i].type == 4) {
        playerX = SCENE_OBJECTS[i].position[0];
        playerZ = SCENE_OBJECTS[i].position[2];
        yaw = SCENE_OBJECTS[i].rotation[1] * PI / 180.0F;
        break;
      }
    }
    playerY = terrainHeightAt(playerX, playerZ);
    playerVelY = 0.0F;
  }

  // Flow graph / script teleport request: move the Player entity when the
  // scene has one, the built-in FPP player otherwise.
  if (scriptCtx.teleport) {
    scriptCtx.teleport = false;
    if (PLAYER_INDEX >= 0) {
      entX = scriptCtx.teleportPos.x;
      entY = scriptCtx.teleportPos.y;
      entZ = scriptCtx.teleportPos.z;
      entVelY = 0.0F;
      entYaw = scriptCtx.teleportYaw * PI / 180.0F;
    } else {
      playerX = scriptCtx.teleportPos.x;
      playerY = scriptCtx.teleportPos.y;
      playerZ = scriptCtx.teleportPos.z;
      playerVelY = 0.0F;
      yaw = scriptCtx.teleportYaw * PI / 180.0F;
    }
  }

  if (!menuActive) updateObjectPhysics();
  updateParticles();
  updateSoundEmitters();

  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt));
  {
    engine->renderer.renderer3D.usePipeline(stapip);
    renderScene();
    if (scriptCtx.hudVisible)
      for (auto& sprite : hudSprites) engine->renderer.renderer2D.render(sprite);
    if (useTargetIndex >= 0) engine->renderer.renderer2D.render(usePromptSprite);
    renderGameMenu();
    renderSaveMenu();
    drawDebugHud(engine);
  }
  engine->renderer.endFrame();
}
)";

static const char* TPL_GAME_CPP_FPP_TAIL = R"(
namespace {

// ANALOG_DEADZONE_L/_R (Preferences > Input) zero resting drift per stick;
// above the deadzone the value rescales from 0 so the edge does not step.
float axisValue(const u8& raw, const float dz) {
  const float v = (raw - 128.0F) / 128.0F;
  const float mag = v < 0.0F ? -v : v;
  if (mag <= dz) return 0.0F;
  const float scaled = (mag - dz) / (1.0F - dz);
  return v < 0.0F ? -scaled : scaled;
}

}  // namespace

void TerrainGame::updatePlayer() {
  const auto& leftJoy = engine->pad.getLeftJoyPad();
  const auto& rightJoy = engine->pad.getRightJoyPad();

  // Right stick: look around (stick right = turn right)
  yaw -= axisValue(rightJoy.h, ANALOG_DEADZONE_R) * 0.05F * LOOK_SPEED * g_frameScale;
  pitch -= axisValue(rightJoy.v, ANALOG_DEADZONE_R) * 0.035F * LOOK_SPEED * g_frameScale;
  if (pitch > 1.2F) pitch = 1.2F;
  if (pitch < -1.2F) pitch = -1.2F;

  // Left stick: walk. Forward is where the camera looks (flat).
  const float fx = sinf(yaw);
  const float fz = cosf(yaw);
  const float forward = -axisValue(leftJoy.v, ANALOG_DEADZONE_L);
  const float strafe = axisValue(leftJoy.h, ANALOG_DEADZONE_L);
  float nextX = playerX + (fx * forward - fz * strafe) * WALK_SPEED * g_frameScale;
  float nextZ = playerZ + (fz * forward + fx * strafe) * WALK_SPEED * g_frameScale;

  // Keep the player on the terrain
  const float limX = TERRAIN_WIDTH * 0.5F - 1.0F;
  const float limZ = TERRAIN_DEPTH * 0.5F - 1.0F;
  if (nextX > limX) nextX = limX;
  if (nextX < -limX) nextX = -limX;
  if (nextZ > limZ) nextZ = limZ;
  if (nextZ < -limZ) nextZ = -limZ;

  // Collision with scene objects (collidePlayer: box/mesh/none per object)
  // + standing on top of them. Player can step ~0.5 units up.
  // The floor is the sculpted terrain.
  float ground = terrainHeightAt(nextX, nextZ);
  collidePlayer(playerX, playerZ, &nextX, &nextZ, playerY, EYE_HEIGHT, &ground);
  playerX = nextX;
  playerZ = nextZ;

  // Gravity & jumping (X). GRAVITY: units/s^2, JUMP_SPEED: units/s.
  playerVelY -= GRAVITY * g_frameDt * g_frameDt;
  playerY += playerVelY;
  if (playerY <= ground) {
    playerY = ground;
    playerVelY = 0.0F;
    if (engine->pad.getClicked().BTN_JUMP) playerVelY = JUMP_SPEED * g_frameDt;
  }

  const float eyeY = playerY + EYE_HEIGHT;
  cameraPosition = Vec4(playerX, eyeY, playerZ);
  cameraLookAt = Vec4(playerX + fx * cosf(pitch), eyeY + sinf(pitch),
                      playerZ + fz * cosf(pitch));
}
)";

static const char* TPL_GAME_CPP_FOOTER = R"(
}  // namespace {{NAME_UPPER_NS}}
)";

// ---------------------------------------------------------------------------
// Engine patch v2: fast EE clipper. Replaces two engine .cpp files (headers
// untouched, ABI identical). Copied into /tyra by the Runner and the engine
// is rebuilt once per shared volume.
//
// What it fixes:
// - Cohen-Sutherland outcodes: fully-visible triangles skip the 6-plane
//   Sutherland-Hodgman entirely (1 byte per vertex instead of ~6 passes of
//   64-byte struct copies), fully-outside triangles are rejected instantly.
// - clipAgainstPlane edge loop used BY-VALUE copies of two 64-byte structs
//   per edge per plane - now const references.
// - StaPipClipper::clip allocated a std::vector on the heap per call, per
//   subpackage, per frame - now a static pool (render is single-threaded).
// ---------------------------------------------------------------------------

// Engine patch v3: clipping leaves the EE. The VU1 cull programs get a 3x
// wider XY accept window (the GS scissor trims pixels in hardware, so
// edge-crossing triangles need no geometric clipping at all), and the bbox
// "second chance" reclassifies side-crossing packages as cullable. The EE
// clipper stays only for geometry near the camera plane, where perspective
// division would explode - the one case a scissor cannot fix.

// Shell script applied inside the container: swaps the PerformClipCheck VU1
// macro for a guard-band version (XY widened 3x, Z/W test untouched).
// ---------------------------------------------------------------------------
// Built-in assets for the "FPP showcase" template
// ---------------------------------------------------------------------------

const char* houseObjText() {
    return R"(# simple house: cube walls + pyramid roof (tyra-editor built-in)
v -1 0 -1
v  1 0 -1
v  1 0  1
v -1 0  1
v -1 1.2 -1
v  1 1.2 -1
v  1 1.2  1
v -1 1.2  1
v  0 2.2  0
f 1 2 6
f 1 6 5
f 2 3 7
f 2 7 6
f 3 4 8
f 3 8 7
f 4 1 5
f 4 5 8
f 5 6 9
f 6 7 9
f 7 8 9
f 8 5 9
f 4 3 2
f 4 2 1
)";
}

// Global pad mapping + "use" interaction tuning. Marker-owned: delete the
// first line to take ownership (e.g. to remap buttons per project).
static const char* TPL_CONTROLS_HPP =
    R"(// Generated by tyra-editor. Delete this line to take ownership of this file.
#pragma once

// Global pad mapping - the single place gameplay buttons are defined.
// Values are Tyra pad button member names (Cross, Circle, Square, Triangle,
// L1, R1, DpadUp, ...), used as engine->pad.getClicked().BTN_USE etc.
#define BTN_USE Square
#define BTN_JUMP Cross
#define BTN_FLY_UP Cross     // noclip: ascend
#define BTN_FLY_DOWN Square  // noclip: descend

// "Use" interaction (objects marked usable in the editor)
constexpr float USE_DISTANCE = 4.0F;   // max distance to the object surface
constexpr float USE_LOOK_DOT = 0.92F;  // how directly you must look (cos angle)
)";

// 64x64 white crosshair, PNG (372 bytes)
const unsigned char* crosshairPng(size_t& size) {
    static const unsigned char data[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,64,
        0,0,0,64,8,6,0,0,0,170,105,113,222,0,0,0,1,115,82,71,
        66,0,174,206,28,233,0,0,0,4,103,65,77,65,0,0,177,143,11,252,
        97,5,0,0,0,9,112,72,89,115,0,0,14,195,0,0,14,195,1,199,
        111,168,100,0,0,1,9,73,68,65,84,120,94,237,211,209,106,194,64,24,
        132,209,188,255,75,183,101,57,12,181,88,73,83,81,216,249,207,157,159,201,
        154,17,61,198,24,99,140,49,94,237,227,7,185,135,221,33,247,176,59,228,
        30,118,135,220,195,238,144,123,216,29,114,15,187,67,238,97,119,200,61,236,
        14,185,135,221,33,247,176,59,228,30,118,135,220,195,238,144,123,216,29,114,
        15,187,67,238,97,119,200,99,140,2,254,246,119,185,100,95,118,62,228,210,
        253,216,119,138,91,222,207,243,92,230,152,69,58,197,45,79,253,252,75,156,
        115,153,99,22,41,228,69,10,121,159,47,192,203,27,222,90,164,27,191,245,
        191,88,135,255,135,115,46,115,204,34,133,188,72,33,191,255,11,120,38,207,
        116,138,91,246,98,219,41,110,217,143,125,15,185,116,95,118,222,229,146,30,
        149,163,199,151,245,123,255,70,238,97,119,200,61,236,14,185,135,221,33,247,
        176,59,228,30,118,135,220,195,238,144,123,216,29,114,15,187,67,238,97,119,
        200,61,236,14,185,135,221,33,247,176,59,228,30,118,135,220,195,238,144,123,
        216,29,242,24,99,140,49,198,139,28,199,39,208,62,150,176,24,28,75,254,
        0,0,0,0,73,69,78,68,174,66,96,130,
    };
    size = sizeof(data);
    return data;
}


// 128x32 "USE" prompt, PNG (781 bytes) - written into res/hud/use.png of
// every project when missing; drawn by the game while the camera looks at a
// usable object up close. PS2 textures need power-of-two dimensions.
const unsigned char* usePromptPng(size_t& size) {
    static const unsigned char data[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,128,0,0,
        0,32,8,6,0,0,0,218,34,112,37,0,0,0,1,115,82,71,66,0,174,206,
        28,233,0,0,0,4,103,65,77,65,0,0,177,143,11,252,97,5,0,0,0,9,
        112,72,89,115,0,0,14,195,0,0,14,195,1,199,111,168,100,0,0,2,
        162,73,68,65,84,120,94,237,154,61,170,219,64,20,133,189,0,247,
        193,27,112,29,112,151,52,94,129,33,101,136,33,11,48,105,82,
        186,15,36,120,11,174,178,0,119,41,2,233,12,15,178,128,52,230,
        173,224,145,54,164,84,238,25,207,125,156,140,198,146,162,153,
        160,87,156,15,14,146,231,71,8,223,51,191,154,153,16,66,8,33,
        132,16,79,149,149,169,33,117,193,229,80,47,101,110,218,154,
        78,38,46,123,49,29,76,185,58,32,125,135,33,218,153,68,33,183,
        254,248,28,185,114,28,208,231,166,92,153,84,159,77,204,152,
        224,187,100,130,2,194,31,191,90,173,26,6,105,81,76,72,99,80,
        47,166,227,57,47,112,63,159,207,155,237,118,219,156,78,167,
        88,234,202,229,114,105,14,135,3,215,249,110,2,217,119,24,194,
        110,183,243,103,201,4,35,169,101,128,215,184,46,151,203,152,
        211,13,140,16,235,125,51,201,0,19,82,203,0,15,104,249,104,229,
        67,161,224,125,194,85,6,152,134,90,6,104,54,155,77,76,189,130,
        224,44,22,139,199,252,245,122,221,156,207,231,152,123,197,243,
        160,142,119,232,147,130,95,64,53,3,32,224,14,2,237,233,44,152,
        128,193,144,225,121,35,13,160,224,23,242,223,122,0,140,243,
        28,224,14,189,194,117,164,1,68,33,213,12,128,57,64,14,204,11,
        142,199,99,88,25,240,144,16,181,52,245,189,67,159,68,1,181,
        12,240,14,215,180,139,207,129,225,1,229,98,189,175,166,236,
        59,12,33,62,3,18,35,169,101,0,60,231,13,238,209,202,209,253,
        247,65,75,193,47,184,202,0,211,80,211,0,126,31,132,33,1,45,
        125,191,223,183,102,255,14,213,151,1,38,162,150,1,122,229,59,
        132,12,230,6,158,223,241,14,125,18,5,96,18,214,218,193,163,
        217,251,75,20,50,158,153,90,19,61,54,0,67,99,252,7,211,222,
        116,23,127,135,30,193,193,118,177,167,143,52,128,168,64,248,
        51,25,218,97,251,75,233,82,143,103,245,220,205,115,96,83,161,
        213,59,21,12,128,47,142,162,144,179,169,53,78,243,78,30,90,
        126,26,124,44,239,144,103,250,137,43,111,4,1,4,154,247,1,112,
        207,173,31,176,209,10,134,0,153,160,144,181,105,208,18,142,
        161,224,189,55,221,195,36,183,38,123,183,64,157,248,140,150,
        1,134,64,61,136,76,80,8,198,233,86,43,191,69,242,53,15,224,
        28,192,111,180,242,161,38,160,222,33,124,73,148,1,166,231,163,
        233,113,29,159,6,210,119,244,16,40,148,51,249,247,124,7,38,
        248,101,10,70,66,112,82,144,134,149,0,181,124,156,33,200,174,
        68,134,32,3,212,231,173,201,255,212,46,165,39,122,28,172,42,
        126,152,114,117,82,193,48,78,107,31,225,31,164,224,87,102,97,
        194,217,189,48,57,36,225,76,223,209,132,96,245,177,49,33,48,
        92,31,66,26,206,11,226,220,96,202,24,19,40,248,66,8,33,132,
        16,66,136,39,197,108,246,7,253,174,253,75,36,86,2,246,0,0,0,
        0,73,69,78,68,174,66,96,130,
    };
    size = sizeof(data);
    return data;
}

// 512x8 glyph strip for the debug-profile HUD, rendered from an embedded
// 8x8 pixel font and encoded on first use. 18 glyphs ("0123456789.FPSMBE/"),
// one per 16px cell; the right half of each cell stays transparent so the
// GS's bilinear filter never samples the neighboring glyph.
const std::vector<unsigned char>& debugFontPng() {
    static std::vector<unsigned char> png = [] {
        // Classic CP437-style 8x8 glyphs, one byte per row, MSB = left pixel.
        static const unsigned char rows[18][8] = {
            {0x7C, 0xC6, 0xCE, 0xDE, 0xF6, 0xE6, 0x7C, 0x00},  // 0
            {0x30, 0x70, 0x30, 0x30, 0x30, 0x30, 0xFC, 0x00},  // 1
            {0x78, 0xCC, 0x0C, 0x38, 0x60, 0xCC, 0xFC, 0x00},  // 2
            {0x78, 0xCC, 0x0C, 0x38, 0x0C, 0xCC, 0x78, 0x00},  // 3
            {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00},  // 4
            {0xFC, 0xC0, 0xF8, 0x0C, 0x0C, 0xCC, 0x78, 0x00},  // 5
            {0x38, 0x60, 0xC0, 0xF8, 0xCC, 0xCC, 0x78, 0x00},  // 6
            {0xFC, 0xCC, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},  // 7
            {0x78, 0xCC, 0xCC, 0x78, 0xCC, 0xCC, 0x78, 0x00},  // 8
            {0x78, 0xCC, 0xCC, 0x7C, 0x0C, 0x18, 0x70, 0x00},  // 9
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00},  // .
            {0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00},  // F
            {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00},  // P
            {0x78, 0xCC, 0xE0, 0x70, 0x1C, 0xCC, 0x78, 0x00},  // S
            {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},  // M
            {0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00},  // B
            {0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00},  // E
            {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},  // /
        };
        const int w = 512, h = 8;  // PS2 textures need power-of-two sizes
        std::vector<unsigned char> rgba(w * h * 4, 0);
        for (int g = 0; g < 18; ++g)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    if (!(rows[g][y] & (0x80 >> x))) continue;
                    unsigned char* px = &rgba[(y * w + g * 16 + x) * 4];
                    px[0] = px[1] = px[2] = px[3] = 255;
                }
        std::vector<unsigned char> out;
        stbi_write_png_to_func(
            [](void* ctx, void* data, int size) {
                auto* v = static_cast<std::vector<unsigned char>*>(ctx);
                v->insert(v->end(), static_cast<unsigned char*>(data),
                          static_cast<unsigned char*>(data) + size);
            },
            &out, w, h, 4, rgba.data(), w * 4);
        return out;
    }();
    return png;
}

// 256x64 "LOADING..." sprite, PNG - written into res/hud/loading.png of
// every project when missing; shown centered on black during scene switches
// when the loading screen is enabled. Replace the file to customize.
const unsigned char* loadingPng(size_t& size) {
    static const unsigned char data[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,1,0,0,0,
        0,64,8,6,0,0,0,245,93,169,190,0,0,0,1,115,82,71,66,0,174,206,
        28,233,0,0,0,4,103,65,77,65,0,0,177,143,11,252,97,5,0,0,0,9,
        112,72,89,115,0,0,14,195,0,0,14,195,1,199,111,168,100,0,0,9,186,73,
        68,65,84,120,94,237,156,65,168,22,215,21,199,93,102,225,34,8,66,221,189,141,
        18,220,201,91,104,179,18,178,209,69,178,10,40,65,12,4,186,144,71,22,113,101,
        35,116,83,16,219,82,226,38,8,33,139,79,8,106,11,6,74,65,130,80,144,6,
        163,80,186,40,40,72,119,5,81,10,110,4,87,34,218,255,127,190,115,111,238,157,
        57,119,230,206,124,227,243,125,244,255,131,131,126,247,158,115,230,206,185,119,206,189,
        115,103,230,237,18,66,8,33,132,16,66,8,33,132,16,66,8,33,132,16,66,8,
        33,132,16,66,8,33,132,16,66,8,33,132,16,66,8,33,132,16,66,8,33,132,
        16,66,8,33,132,16,66,8,33,132,72,121,245,234,213,1,200,247,142,28,48,149,
        149,120,253,250,245,38,124,109,65,190,77,124,83,190,132,156,130,236,51,213,201,36,
        62,219,114,202,84,6,129,110,41,14,109,225,121,240,124,62,130,84,181,29,122,181,
        190,163,152,105,4,101,69,31,136,241,81,83,235,128,122,215,206,170,171,128,254,62,
        8,207,151,231,221,246,245,7,200,22,251,217,212,197,58,193,142,131,120,172,212,161,
        24,20,28,48,255,54,95,189,112,32,225,159,73,199,131,221,209,198,137,3,143,111,
        106,131,64,189,20,135,94,106,218,206,250,70,121,4,102,26,65,81,209,135,197,121,
        183,169,102,160,220,181,179,234,94,160,198,228,205,243,171,6,250,76,8,179,76,30,
        98,27,64,159,185,3,228,244,233,211,191,71,245,231,144,95,52,138,149,192,116,55,
        6,192,143,75,47,227,128,221,151,230,166,26,14,56,51,119,57,126,252,248,111,161,
        246,254,82,187,12,84,39,37,128,0,219,97,174,58,160,122,74,2,96,236,41,13,
        40,234,245,177,88,44,254,12,181,247,150,218,63,131,170,82,2,200,252,183,97,95,
        152,234,84,180,34,88,7,216,81,203,254,202,57,118,236,216,95,81,189,128,108,52,
        138,21,192,108,242,197,31,120,252,248,241,55,230,110,16,168,239,94,90,149,185,124,
        249,242,63,161,250,187,165,69,25,168,174,148,0,200,243,231,207,255,97,238,50,80,
        53,37,1,48,246,148,6,20,13,250,56,120,240,224,215,80,125,103,105,177,4,197,
        165,4,144,249,79,25,74,170,53,108,109,109,253,26,174,70,77,30,226,45,128,190,
        154,45,1,96,224,20,151,139,215,174,93,187,127,242,228,201,31,232,247,204,153,51,
        127,187,115,231,206,127,172,170,195,213,171,87,255,104,46,123,193,241,62,50,147,34,
        72,40,207,160,202,243,232,29,140,80,117,227,112,238,220,185,191,179,205,65,46,94,
        188,120,143,62,173,186,195,173,91,183,174,153,203,8,138,153,168,54,175,95,191,254,
        33,125,60,120,240,224,191,141,114,2,203,210,227,192,44,187,64,161,50,152,0,110,
        222,188,201,91,158,15,150,22,75,80,60,42,1,12,205,252,76,168,161,31,41,140,
        15,143,107,213,145,228,28,170,199,143,120,11,160,175,102,73,0,48,41,222,139,31,
        62,124,152,155,78,97,192,69,97,34,48,149,14,27,27,27,159,66,167,23,12,214,
        111,77,189,23,14,88,168,127,184,180,242,129,218,80,28,50,225,192,55,149,14,39,
        78,156,224,76,236,193,88,46,188,228,199,50,214,57,210,0,149,193,4,64,172,189,
        239,46,173,198,37,0,20,23,143,193,4,190,103,207,158,239,160,150,182,45,202,254,
        253,251,255,148,38,2,37,128,53,1,125,53,75,2,40,205,254,206,5,116,2,194,
        123,85,250,61,196,123,87,83,205,224,5,134,250,226,189,59,84,220,229,191,55,27,
        113,240,194,164,247,54,0,106,67,113,96,91,216,230,32,31,156,61,123,246,134,169,
        101,216,197,236,37,28,46,207,55,158,61,123,246,131,169,70,146,4,192,125,144,244,
        56,13,80,169,74,0,182,226,249,108,105,85,182,67,85,232,143,72,41,161,90,252,
        130,126,16,158,95,232,71,10,227,243,89,72,234,74,0,107,2,250,106,229,4,0,
        117,247,98,116,102,181,206,50,28,106,67,182,217,61,109,192,91,254,243,226,231,108,
        111,63,51,96,66,95,157,77,178,0,84,38,197,225,201,147,39,183,77,53,131,51,
        34,170,93,27,47,89,38,231,235,110,204,65,165,211,62,218,120,183,19,150,60,155,
        99,227,103,85,2,64,145,219,15,201,45,84,16,38,112,183,79,140,119,143,28,57,
        242,69,178,90,80,2,216,201,160,143,231,72,0,238,242,223,6,98,24,56,69,63,
        165,153,7,85,180,115,87,1,176,233,108,54,114,246,65,213,194,126,102,216,109,0,
        7,175,11,84,38,197,193,75,68,196,218,226,94,204,115,38,0,182,207,126,102,236,
        221,187,247,171,146,29,65,85,232,151,6,20,185,122,182,137,26,116,139,241,115,56,
        4,161,141,18,192,78,6,125,188,114,2,192,128,230,139,32,29,18,31,197,199,77,
        164,100,111,123,7,29,91,232,239,51,149,140,48,235,220,190,125,187,179,10,176,13,
        50,182,197,5,42,83,19,128,219,22,110,22,162,154,118,241,126,60,48,103,2,64,
        213,226,222,189,123,63,89,81,196,150,237,239,227,191,85,9,160,162,15,7,159,164,
        56,240,220,251,86,11,226,109,131,62,222,142,4,208,251,28,190,194,62,3,250,167,
        76,37,146,92,224,139,43,87,174,252,202,138,51,44,65,184,183,1,168,158,28,7,
        83,205,72,18,0,103,194,140,185,19,192,230,230,230,121,43,202,96,219,31,61,122,
        244,75,251,153,65,59,147,134,138,62,104,246,52,160,199,23,188,218,111,3,122,162,
        151,129,214,1,244,241,118,36,128,94,31,21,246,217,222,1,244,139,203,127,147,119,
        172,56,195,116,226,6,89,10,170,223,84,2,200,30,203,17,94,32,166,22,89,37,
        1,64,62,247,98,200,122,62,143,183,159,25,102,71,105,168,237,195,146,158,131,94,
        4,90,7,216,81,203,254,202,217,97,9,32,218,67,247,128,85,103,36,155,78,205,
        5,14,189,206,203,44,201,42,161,179,44,69,245,90,39,0,252,116,55,241,120,43,
        96,255,205,48,59,74,67,109,31,148,244,218,76,125,147,84,108,51,232,171,57,18,
        64,103,73,78,108,227,109,208,7,236,221,151,79,188,157,100,79,55,93,254,67,154,
        37,55,138,221,141,73,219,157,239,44,203,81,53,41,14,80,113,237,146,13,208,109,
        73,0,212,129,223,193,23,163,2,102,71,105,40,245,97,178,178,26,149,0,198,140,
        31,241,22,65,95,205,145,0,220,89,57,217,65,238,245,1,251,206,146,190,245,248,
        41,218,67,183,243,156,159,186,188,24,40,47,94,188,248,11,116,154,251,80,171,206,
        176,11,179,115,27,128,170,73,113,192,113,220,228,149,188,252,180,109,9,128,188,124,
        249,242,142,85,247,2,85,218,81,26,208,38,183,15,147,99,52,231,15,61,110,122,
        110,158,63,127,254,12,99,195,120,46,53,115,148,0,214,4,244,213,202,9,128,152,
        89,135,190,141,55,2,21,247,248,173,151,79,154,37,123,105,144,142,129,207,205,205,
        103,182,59,143,170,209,113,64,117,205,179,243,206,6,232,155,76,0,40,118,207,163,
        13,84,67,251,34,86,213,193,98,208,94,53,49,177,45,88,103,106,25,74,0,107,
        2,250,106,150,4,128,65,237,46,13,237,66,118,95,195,69,117,241,227,161,228,248,
        241,241,19,116,87,254,72,133,216,109,64,118,97,162,120,84,28,80,85,108,123,107,
        67,178,115,15,252,38,19,0,129,255,193,87,164,161,22,218,23,129,93,113,121,111,
        95,85,166,48,129,110,240,251,6,83,201,240,226,6,255,238,19,4,171,142,160,108,
        86,61,209,3,250,106,150,4,0,19,119,54,36,188,71,191,123,247,110,251,2,226,
        183,230,238,5,148,12,108,74,124,249,196,170,87,198,54,232,178,79,143,81,92,29,
        7,20,31,69,219,59,183,34,36,89,97,80,220,103,231,28,164,166,30,153,57,1,
        184,239,38,164,64,45,180,49,130,226,98,31,146,135,15,31,254,134,190,77,189,1,
        197,213,113,131,173,155,96,172,58,50,183,158,232,1,241,114,59,144,3,153,3,140,
        239,173,35,208,157,44,27,196,220,52,192,172,248,65,16,129,254,143,102,231,94,60,
        1,155,161,195,0,109,6,16,138,93,223,233,151,105,158,240,28,76,53,146,44,209,
        7,63,154,105,199,193,138,139,180,62,124,106,150,205,176,203,254,42,143,169,118,224,
        113,238,223,191,255,47,234,208,46,5,213,213,9,128,192,71,113,54,39,80,9,109,
        204,64,85,111,31,6,146,243,25,90,193,213,36,0,182,63,158,195,220,122,162,7,
        196,203,29,248,181,152,155,200,211,167,79,63,182,170,73,180,46,160,180,179,59,203,
        255,214,108,235,10,151,227,166,158,97,199,137,27,116,40,90,41,14,164,213,246,184,
        194,64,213,104,223,102,26,65,209,168,4,128,234,222,217,28,42,161,157,29,86,237,
        67,50,50,1,100,109,153,91,79,244,128,120,173,154,0,58,217,246,198,141,27,167,
        57,203,154,74,21,28,204,173,153,159,210,204,208,168,118,7,115,235,91,131,244,203,
        180,40,24,136,7,77,61,163,253,135,66,80,52,57,14,133,182,199,123,127,168,76,
        73,0,89,92,81,52,42,1,16,92,32,197,199,130,168,14,237,116,185,112,225,194,
        49,250,55,245,81,176,239,147,100,168,4,176,147,65,188,86,77,0,165,96,31,226,
        236,59,52,136,184,73,152,204,22,169,196,11,168,52,144,147,139,174,247,79,137,193,
        190,239,99,163,230,56,248,57,42,14,28,228,53,109,39,80,159,146,0,130,175,6,
        20,141,78,0,164,244,88,16,85,153,255,2,135,120,126,165,151,137,82,216,22,238,
        173,180,86,65,20,37,128,157,12,226,197,217,53,254,181,154,177,2,23,125,193,230,
        172,204,89,118,193,129,145,218,37,47,249,180,133,131,57,123,68,135,142,110,158,61,
        243,189,246,212,7,170,130,205,208,183,6,251,218,182,20,107,67,243,132,162,54,14,
        206,0,79,165,211,118,50,37,198,48,11,62,27,60,31,73,91,138,9,128,231,126,
        233,210,165,79,82,223,20,84,101,254,123,96,50,163,255,5,227,213,246,211,211,143,
        20,246,125,154,200,179,119,8,130,160,42,232,55,204,173,39,234,96,166,14,129,155,
        34,125,112,51,172,25,68,61,194,221,254,56,91,20,160,31,207,182,115,209,21,224,
        11,64,158,125,202,216,56,112,144,215,180,157,76,137,113,27,207,71,49,1,36,112,
        149,212,182,163,212,194,227,242,60,61,31,169,176,45,220,91,201,86,65,45,154,119,
        8,28,105,51,183,158,232,129,47,219,176,147,167,74,45,28,24,169,221,152,207,69,
        121,161,167,182,65,106,41,217,167,109,168,141,67,223,0,47,49,37,198,109,60,31,
        53,109,89,53,118,41,94,27,210,24,14,81,219,150,185,245,132,16,66,8,33,132,
        16,66,8,33,132,16,66,8,33,132,16,66,8,33,132,16,66,8,33,132,16,66,
        8,33,132,16,66,8,33,132,16,66,8,33,132,16,66,8,33,132,16,255,143,236,
        218,245,63,190,43,70,212,162,35,194,196,0,0,0,0,73,69,78,68,174,66,96,
        130,
    };
    size = sizeof(data);
    return data;
}
// ---------------------------------------------------------------------------
// Legacy templates (v1, before scene objects). Kept verbatim so that projects
// generated by older editor versions can be recognized as unedited and safely
// regenerated. Do not modify.
// ---------------------------------------------------------------------------

static const char* TPL_GAME_HPP_V1 = R"(#pragma once

#include <tyra>
#include <memory>
#include <vector>

namespace {{NAME_UPPER_NS}} {

class TerrainGame : public Tyra::Game {
 public:
  explicit TerrainGame(Tyra::Engine* engine);
  ~TerrainGame();

  void init() override;
  void loop() override;

 private:
  void generateTerrainGrid();
  void updateCameraOrbit();

  Tyra::Engine* engine;
  Tyra::StaticPipeline stapip;

  Tyra::Vec4 cameraPosition, cameraLookAt;
  float orbitAngle;

  std::vector<Tyra::Vec4> vertices;
  std::vector<Tyra::Color> colors;

  Tyra::M4x4 model;
  std::unique_ptr<Tyra::StaPipBag> bag;
  std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
  std::unique_ptr<Tyra::StaPipColorBag> colorBag;
};

}  // namespace {{NAME_UPPER_NS}}
)";

static const char* TPL_GAME_CPP_V1 = R"(#include "terrain_game.hpp"
#include "terrain_config.hpp"
#include <math.h>

namespace {{NAME_UPPER_NS}} {

using namespace Tyra;

TerrainGame::TerrainGame(Engine* t_engine)
    : engine(t_engine), orbitAngle(0.0F), model(M4x4::Identity) {}

TerrainGame::~TerrainGame() {}

void TerrainGame::init() {
  stapip.setRenderer(&engine->renderer.core);

  // Sky blue background
  engine->renderer.setClearScreenColor(Color(64.0F, 140.0F, 200.0F));

  cameraLookAt = Vec4(0.0F, 0.0F, 0.0F);
  updateCameraOrbit();

  generateTerrainGrid();
}

void TerrainGame::loop() {
  updateCameraOrbit();

  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt));
  {
    engine->renderer.renderer3D.usePipeline(stapip);
    stapip.core.render(bag.get());
  }
  engine->renderer.endFrame();
}

void TerrainGame::generateTerrainGrid() {
  // Cap the amount of quads so big terrains stay PS2-friendly.
  // Cell size grows with terrain size instead.
  const u32 maxCells = 32;
  const u32 cellsX = TERRAIN_WIDTH > maxCells ? maxCells : (u32)TERRAIN_WIDTH;
  const u32 cellsZ = TERRAIN_DEPTH > maxCells ? maxCells : (u32)TERRAIN_DEPTH;
  const float stepX = TERRAIN_WIDTH / cellsX;
  const float stepZ = TERRAIN_DEPTH / cellsZ;
  const float startX = -TERRAIN_WIDTH * 0.5F;
  const float startZ = -TERRAIN_DEPTH * 0.5F;

  // Two greens in a checker pattern, so the grid is visible.
  // PS2 colors: RGB 0-255, alpha 0-128.
  const Color colorA(96.0F, 160.0F, 72.0F, 128.0F);
  const Color colorB(74.0F, 128.0F, 56.0F, 128.0F);

  vertices.clear();
  colors.clear();
  vertices.reserve(cellsX * cellsZ * 6);
  colors.reserve(cellsX * cellsZ * 6);

  for (u32 z = 0; z < cellsZ; ++z) {
    for (u32 x = 0; x < cellsX; ++x) {
      const float x0 = startX + x * stepX;
      const float x1 = x0 + stepX;
      const float z0 = startZ + z * stepZ;
      const float z1 = z0 + stepZ;
      const Color& c = ((x + z) % 2 == 0) ? colorA : colorB;

      const Vec4 v00(x0, 0.0F, z0, 1.0F);
      const Vec4 v10(x1, 0.0F, z0, 1.0F);
      const Vec4 v01(x0, 0.0F, z1, 1.0F);
      const Vec4 v11(x1, 0.0F, z1, 1.0F);

      vertices.push_back(v00);
      vertices.push_back(v10);
      vertices.push_back(v01);
      vertices.push_back(v10);
      vertices.push_back(v11);
      vertices.push_back(v01);

      for (int i = 0; i < 6; ++i) colors.push_back(c);
    }
  }

  infoBag = std::make_unique<StaPipInfoBag>();
  infoBag->model = &model;
  infoBag->shadingType = TyraShadingFlat;
  infoBag->fullClipChecks = false;

  colorBag = std::make_unique<StaPipColorBag>();
  colorBag->many = colors.data();

  bag = std::make_unique<StaPipBag>();
  bag->info = infoBag.get();
  bag->color = colorBag.get();
  bag->vertices = vertices.data();
  bag->count = static_cast<u32>(vertices.size());
  bag->texture = nullptr;
  bag->lighting = nullptr;
}

void TerrainGame::updateCameraOrbit() {
  orbitAngle += 0.005F;
  const float diag = TERRAIN_WIDTH > TERRAIN_DEPTH ? TERRAIN_WIDTH : TERRAIN_DEPTH;
  const float orbitRadius = diag * 0.9F;
  const float orbitHeight = diag * 0.55F;

  cameraPosition.x = orbitRadius * cosf(orbitAngle);
  cameraPosition.y = orbitHeight;
  cameraPosition.z = orbitRadius * sinf(orbitAngle);
}

}  // namespace {{NAME_UPPER_NS}}
)";

// Script API header (inc/scripts/script.hpp). Marker-owned: regenerated on
// build while the marker is present, so the API can evolve with the editor.
static const char* TPL_SCRIPT_HPP =
    R"(// Generated by tyra-editor. Delete this line to take ownership of this file.
#pragma once

#include <tyra>
#include <vector>
#include "scene_data.hpp"

namespace {{NAME_UPPER_NS}} {

/** A scene object at runtime. Mutate `data` (position/rotation/scale/color),
 * `visible` or `velocityY`, then set `dirty = true` so the geometry gets
 * rebuilt on the next frame. */
struct RuntimeObject {
  SceneObjectData data;
  bool visible = true;
  float velocityY = 0.0F;  // vertical velocity (object physics)
  bool dirty = true;

  // Animated models (.glb) playback state; ignored on everything else.
  // To switch clips set animClip (resolve names with ctx.resolveClip or the
  // playAnimation() helper below) and animRestart = true; the game applies
  // it during the render pass.
  int animClip = 0;          // active clip index (model's clip table order)
  bool animPlaying = false;  // false = frozen on the current pose
  bool animLoop = true;
  float animSpeed = 1.0F;    // multiplier on the authored playback speed
  bool animRestart = false;  // (re)start animClip on the next frame
  float animFade = 0.0F;     // crossfade seconds for that restart (0 = pop)
  bool animFinished = false; // one frame: the clip reached its last frame
                             // (one-shots: once; looping: every wrap)
};

/** Everything a script can see and touch each frame. */
struct ScriptContext {
  Tyra::Engine* engine = nullptr;  // pad, renderer, audio, ...
  Tyra::Vec4 playerPosition;       // camera/player position this frame
  RuntimeObject* objects = nullptr;  // mutable scene objects
  int objectCount = 0;
  Tyra::Color skyColor;  // write to change the clear color

  // Set teleport = true and teleportPos to move the player (Player entity or
  // the FPP template player) there; the game applies and clears it.
  // teleportYaw: facing direction in degrees (Y rotation, 0 = +Z).
  bool teleport = false;
  Tyra::Vec4 teleportPos;
  float teleportYaw = 0.0F;

  // Index of the usable object the player pressed BTN_USE on this frame
  // (-1 = none). Drives the flow graph "On Used" trigger.
  int usedObject = -1;

  // Write to show/hide all HUD images (the USE prompt is unaffected).
  bool hudVisible = true;

  // Save data: named values persisted in memory card slots (SAVE_VALUE_NAMES
  // order, scene_data.hpp). Set openSaveMenu = true to open the in-game
  // save/load menu (also opened by using a Save point object); the game
  // applies and clears it.
  float* saveValues = nullptr;
  int saveValueCount = 0;
  // Text values: saveTextCount slots of SAVE_TEXT_LEN bytes each
  // (SAVE_TEXT_NAMES order, scene_data.hpp), always NUL-terminated.
  char* saveTexts = nullptr;
  int saveTextCount = 0;
  bool openSaveMenu = false;

  // Game menus (menu_data.gen.hpp order). Write a menu index into openMenu
  // to open it (the game applies and clears it). menuEvent holds the index
  // of the "Flow event" a menu entry fired this frame (-1 = none) - it
  // drives the "On Menu Event" trigger.
  int openMenu = -1;
  int menuEvent = -1;

  // Scenes: `scene` is the active scene index (scene_data.hpp order),
  // `sceneGeneration` bumps on every (re)load - scripts use it to reset
  // their state. Write a scene index into `requestScene` to switch after
  // the current frame's scripts.
  int scene = 0;
  unsigned int sceneGeneration = 0;
  int requestScene = -1;

  // Animated models: clip-name -> clip-index lookup for an object (-1 =
  // unknown clip / not an animated model). Set by the game at startup.
  int (*resolveClip)(int objectIndex, const char* clipName) = nullptr;
};

/** Plays a named clip on an animated model object ("" = its first clip).
 * fade > 0 crossfades from the current pose over that many seconds instead
 * of snapping. No-op on objects that are not animated models. */
inline void playAnimation(ScriptContext& ctx, int objectIndex,
                          const char* clip = "", bool loop = true,
                          float speed = 1.0F, float fade = 0.0F) {
  if (objectIndex < 0 || objectIndex >= ctx.objectCount) return;
  const int index = ctx.resolveClip ? ctx.resolveClip(objectIndex, clip) : -1;
  if (index < 0) return;
  RuntimeObject& o = ctx.objects[objectIndex];
  o.animClip = index;
  o.animLoop = loop;
  o.animSpeed = speed;
  o.animFade = fade > 0.0F ? fade : 0.0F;
  o.animPlaying = true;
  o.animRestart = true;
}

/** Freezes an animated model object on its current pose. */
inline void stopAnimation(ScriptContext& ctx, int objectIndex) {
  if (objectIndex < 0 || objectIndex >= ctx.objectCount) return;
  ctx.objects[objectIndex].animPlaying = false;
}

/** True the frame an object's clip reached its last frame (one-shots: once;
 * looping clips: every wrap). */
inline bool animationFinished(const ScriptContext& ctx, int objectIndex) {
  if (objectIndex < 0 || objectIndex >= ctx.objectCount) return false;
  return ctx.objects[objectIndex].animFinished;
}

/** Base class for GLOBAL game scripts: registered with TYRA_SCRIPT, one
 * instance for the whole game, update() runs every frame in every scene.
 * For per-object behavior prefer ObjectScript below. */
class Script {
 public:
  virtual ~Script() {}
  virtual void init(ScriptContext&) {}
  virtual void update(ScriptContext&) {}
};

inline std::vector<Script*>& getScripts() {
  static std::vector<Script*> scripts;
  return scripts;
}

/** Base class for OBJECT scripts (Unity-style components). Write a class in
 * src/scripts/, register it with TYRA_OBJECT_SCRIPT(MyScript); inside your
 * namespace, then attach it to objects in the editor (Properties > Scripts).
 *
 * The game creates one instance per attachment when a scene (re)loads and
 * deletes it when the scene is left - the same class attached to five
 * objects runs as five independent instances, each with its own members and
 * its own `self`. Only attached scripts run; an unattached class costs
 * nothing. */
class ObjectScript {
 public:
  virtual ~ObjectScript() {}

  /** The object this instance is attached to - mutate self->data (position/
   * rotation/scale/color), self->visible or self->velocityY, then set
   * self->dirty = true. Equals &ctx.objects[selfIndex]; refreshed by the
   * game every frame before onUpdate. */
  RuntimeObject* self = nullptr;
  int selfIndex = -1;

  /** Scene (re)loaded: self is valid, runs before the first onUpdate. */
  virtual void onStart(ScriptContext&) {}
  /** Every frame while the owning scene is active. */
  virtual void onUpdate(ScriptContext&) {}
  /** The player pressed USE on self this frame (usable objects only). */
  virtual void onUsed(ScriptContext&) {}
};

/** Object-script classes register here by name (via TYRA_OBJECT_SCRIPT);
 * the generated object_scripts.gen.cpp instantiates attachments by name at
 * scene load. */
struct ObjectScriptFactory {
  const char* name;
  ObjectScript* (*create)();
};

inline std::vector<ObjectScriptFactory>& getObjectScriptFactories() {
  static std::vector<ObjectScriptFactory> factories;
  return factories;
}

}  // namespace {{NAME_UPPER_NS}}

/** Registers a script class. Put TYRA_SCRIPT(MyScript); at file scope. */
#define TYRA_SCRIPT_CONCAT_INNER(a, b) a##b
#define TYRA_SCRIPT_CONCAT(a, b) TYRA_SCRIPT_CONCAT_INNER(a, b)
#define TYRA_SCRIPT(ClassName)                                             \
  static const bool TYRA_SCRIPT_CONCAT(_tyraScript_, __COUNTER__) = []() { \
    {{NAME_UPPER_NS}}::getScripts().push_back(new ClassName());            \
    return true;                                                           \
  }()

/** Registers an object script class under its (stringized) name - the name
 * the editor's Properties > Scripts attach list shows. Put
 * TYRA_OBJECT_SCRIPT(MyScript); at file scope INSIDE your namespace. */
#define TYRA_OBJECT_SCRIPT(ClassName)                                         \
  static const bool TYRA_SCRIPT_CONCAT(_tyraObjScript_, __COUNTER__) = []() { \
    {{NAME_UPPER_NS}}::getObjectScriptFactories().push_back(                  \
        {#ClassName, []() -> {{NAME_UPPER_NS}}::ObjectScript* {               \
          return new ClassName();                                             \
        }});                                                                  \
    return true;                                                              \
  }()
)";

// Example script for FPP projects - user-owned, written only at creation.
static const char* TPL_EXAMPLE_SCRIPT_FPP =
    R"(// Example tyra-editor script. This file is yours - it is never regenerated.
// Walk close to the box and press X: the sky changes color (and a message
// lands in the PCSX2 log via TYRA_LOG).
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace {{NAME_UPPER_NS}} {

class ExampleInteraction : public Script {
 public:
  void update(ScriptContext& ctx) override {
    // Find the first box in the scene
    RuntimeObject* box = nullptr;
    for (int i = 0; i < ctx.objectCount; ++i) {
      if (ctx.objects[i].data.type == 0) {  // 0 = box
        box = &ctx.objects[i];
        break;
      }
    }
    if (!box) return;

    const float dx = ctx.playerPosition.x - box->data.position[0];
    const float dz = ctx.playerPosition.z - box->data.position[2];
    const bool nearBox = (dx * dx + dz * dz) < 8.0F * 8.0F;

    if (nearBox && ctx.engine->pad.getClicked().Cross) {
      toggled = !toggled;
      TYRA_LOG("Box says hello! Sky toggled: ", (int)toggled);
      ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
                             : Tyra::Color(SKY_R, SKY_G, SKY_B);
    }
  }

 private:
  bool toggled = false;
};

}  // namespace {{NAME_UPPER_NS}}

TYRA_SCRIPT({{NAME_UPPER_NS}}::ExampleInteraction);
)";

// Example script for orbit projects (no player to walk around with).
static const char* TPL_EXAMPLE_SCRIPT_ORBIT =
    R"(// Example tyra-editor script. This file is yours - it is never regenerated.
// Press X on the pad: the sky changes color (and a message lands in the
// PCSX2 log via TYRA_LOG).
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace {{NAME_UPPER_NS}} {

class ExampleInteraction : public Script {
 public:
  void update(ScriptContext& ctx) override {
    if (ctx.engine->pad.getClicked().Cross) {
      toggled = !toggled;
      TYRA_LOG("X pressed! Sky toggled: ", (int)toggled);
      ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
                             : Tyra::Color(SKY_R, SKY_G, SKY_B);
    }
  }

 private:
  bool toggled = false;
};

}  // namespace {{NAME_UPPER_NS}}

TYRA_SCRIPT({{NAME_UPPER_NS}}::ExampleInteraction);
)";

// Stub for "New script..." in the editor: an attachable object script.
static const char* TPL_SCRIPT_STUB =
    R"(// {{SCRIPT_FILE}} - created by tyra-editor. This file is yours.
#include "scripts/script.hpp"

namespace {{NAME_UPPER_NS}} {

// Attach me to objects in the editor: Properties > Scripts. Every attached
// object runs its own instance of this class - `self` is that object.
class {{SCRIPT_CLASS}} : public ObjectScript {
 public:
  void onStart(ScriptContext& ctx) override {
    // Scene (re)loaded - self is valid, one call before the first onUpdate.
  }

  void onUpdate(ScriptContext& ctx) override {
    // Called every frame. Examples:
    //   self->data.rotation[1] += 60.0F * g_frameDt;  // spin self
    //   self->dirty = true;                           // geometry changed
    //   ctx.engine->pad.getClicked().Cross  - X pressed this frame
    //   ctx.playerPosition                  - camera/player position
  }

  void onUsed(ScriptContext& ctx) override {
    // Player pressed USE on self (objects with "Usable" checked).
  }
};

TYRA_OBJECT_SCRIPT({{SCRIPT_CLASS}});

}  // namespace {{NAME_UPPER_NS}}
)";

static const char* TPL_RUN_PS1 = R"($ConfigFile = Join-Path $PSScriptRoot './windows-pcsx2.ps1'
. $ConfigFile

RunPCSX2
)";

static const char* TPL_PCSX2_PS1 = R"PS1(# =======================
$CUSTOM_PCSX2_PATH = "" # "D:/My/Path/To/PCSX2"
# =======================

function GetTargetELFName {
    return (Select-String -Path './Makefile' -Pattern "[^ ]*.elf").Matches.Value
}

function FindPCSX2Directory {
    if (-not [string]::IsNullOrEmpty($CUSTOM_PCSX2_PATH)) {
        return $CUSTOM_PCSX2_PATH
    }
    else {
        $pcsx2Path = "${Env:ProgramFiles}/PCSX2"
        $pcsx2Pathx86 = "${Env:ProgramFiles(x86)}/PCSX2"

        if (Test-Path -Path $pcsx2Path) {
            return $pcsx2Path
        }
        elseif (Test-Path -Path $pcsx2Pathx86) {
            return $pcsx2Pathx86
        }
        else {
            throw "PCSX2 directory not found!"
        }
    }
}

function FindPCSX2Executable {
    param ([string]$directory)

    foreach ($name in 'pcsx2.exe', 'pcsx2-qt.exe') {
        if (Test-Path -Path (Join-Path $directory $name)) { return $name }
    }
    throw "PCSX2 executable not found in: $directory!"
}

function RunPCSX2 {
    $dirPath = FindPCSX2Directory
    $isNewVersion = Test-Path -Path "$dirPath/qt.conf"
    $executableName = FindPCSX2Executable -directory $dirPath
    $executableNameWithoutExt = (Split-Path $executableName -Leaf).Split('.')[0]
    $targetFileName = "$PWD/bin/$(GetTargetELFName)"

    Stop-Process -Name $executableNameWithoutExt -ErrorAction 'SilentlyContinue'

    if ($isNewVersion) {
        Start-Process -FilePath "$dirPath/$executableName" -ArgumentList "-elf", $targetFileName
    }
    else {
        Start-Process -FilePath "$dirPath/$executableName" -ArgumentList "--elf=$targetFileName"
    }
}
)PS1";

static const char* TPL_GITIGNORE = R"(obj/
bin/*.elf
*.history
.vscode/
.res-baked/
)";

static const char* TPL_DIR_KEEP = "*\n!.gitignore\n";

static std::string floatLit(float v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.6g", (double)v);
    std::string s = buf;
    // "8" -> "8.0": the F suffix is only valid on floating-point literals
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
        s += ".0";
    return s + "F";
}

// Absolute path to the in-tree Tyra engine (editor repo, vendor/tyra), with
// forward slashes - bind-mounted into the build container by docker-compose.
static std::string engineSourceDir() {
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        std::filesystem::path candidate = std::filesystem::path(exePath).parent_path() /
                                          ".." / "vendor" / "tyra";
        std::error_code ec;
        if (std::filesystem::exists(candidate / "Makefile.base", ec)) {
            std::string s = std::filesystem::weakly_canonical(candidate, ec).string();
            for (auto& c : s)
                if (c == '\\') c = '/';
            return s;
        }
    }
    return ".";  // wrong on purpose - the engine sync fails with a clear error
}

// Short stable hash of the engine source path for the compiled-engine volume
// name (see TPL_COMPOSE). FNV-1a over the forward-slash path, hex-encoded.
static std::string engineSourceHash() {
    const std::string src = engineSourceDir();
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : src) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%08x", (unsigned)(h ^ (h >> 32)));
    return buf;
}

static std::string vec3Init(const float* v) {
    return "{" + floatLit(v[0]) + ", " + floatLit(v[1]) + ", " + floatLit(v[2]) + "}";
}

// inc/scene_data.hpp - pure data mirror of the .tyra project, regenerated on build
static std::string sceneDataContent(const Project& p, const std::string& ns) {
    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n"
           "\n"
           "namespace "
        << ns
        << " {\n"
           "\n"
           "struct SceneObjectData {\n"
           "  int type;  // 0=box 1=sphere 2=cylinder 3=cone 4=spawn-point 5=model\n"
           "             // 6=player 7=emitter 8=sound 9=point-light 10=save-point\n"
           "             // 11=empty (pure transform, no geometry/collision)\n"
           "  float position[3];\n"
           "  float rotation[3];  // degrees\n"
           "  float scale[3];\n"
           "  float color[3];  // 0..1\n"
           "  int physics;  // 1 = falls with gravity\n"
           "  int model;    // index into MODEL_PATHS / gameModels, -1 = none\n"
           "  int material; // primitives: index into MATERIAL_PATHS, -1 = plain color\n"
           "  int usable;   // 1 = shows the USE prompt up close (see controls.hpp)\n"
           "  int emitKind;   // emitters: 0 fire, 1 smoke, 2 fog, 3 sparks, 4 rain,\n"
           "                  // 5 custom (physics below)\n"
           "  int emitCount;  // emitters: particle pool size (density)\n"
           "  float emitSize; // emitters: base particle size\n"
           "  int emitEnabled; // emitters: 0 = starts disabled (Show Object enables)\n"
           "  int emitFollow;  // emitters: 1 = position is an offset from the player\n"
           "  float emitSpeed;   // custom: emission speed along rotated +Y, units/s\n"
           "  float emitSpread;  // custom: cone half-angle, degrees\n"
           "  float emitGravity; // custom: units/s^2, negative = rises\n"
           "  float emitWeight;  // custom: air drag ~ 1/weight\n"
           "  float emitLife;    // custom: particle lifetime, seconds\n"
           "  float emitGrow;    // custom: size multiplier at end of life\n"
           "  float emitOpacity; // custom: base alpha 0..1\n"
           "  int emitDieGround; // custom: 1 = particle dies on the terrain\n"
           "  int snd;        // sound emitters: index into SND_PATHS, -1 = none\n"
           "  int sndAuto;    // sound emitters: 1 = plays while in range\n"
           "  float sndRange;    // sound emitters: audible distance\n"
           "  float sndInterval; // sound emitters: retrigger period (s), 0 = loop\n"
           "  int sndOnPlayer;   // sound emitters: 1 = centered on the player\n"
           "                     // (plain stereo, full volume, no distance/pan)\n"
           "  float lightBright; // point lights (type 9): baked intensity\n"
           "  float lightRadius; // point lights (type 9): falloff radius\n"
           "  int saveState;  // 1 = position/color/visibility persisted in saves\n"
           "  int collision;  // 0 = box (models: mesh AABB), 1 = mesh, 2 = none\n"
           "  int animModel;  // animated models: index into ANIM_MODEL_PATHS, -1 = none\n"
           "  const char* animClip;  // animated models: starting clip (\"\" = first)\n"
           "  int animAutoplay;      // animated models: 1 = play at scene start\n"
           "  int animLoop;          // animated models: 1 = starting clip loops\n"
           "  float animSpeed;       // animated models: playback speed multiplier\n"
           "  int primDetail;        // curved primitives: radial segment count\n"
           "};\n"
           "\n";

    // One object table per scene; the game indexes everything through
    // SCENE_OBJECT_TABLES[g_activeScene] (see the accessor macros in the
    // generated game cpp).
    const int sceneCount = (int)p.scenes.size();
    out << "constexpr int SCENE_COUNT = " << sceneCount << ";\n\n";

    for (int si = 0; si < sceneCount; ++si) {
        const auto& objs = p.scenes[si].objects;
        out << "// scene \"" << p.scenes[si].name << "\"\n"
            << "constexpr SceneObjectData SCENE_" << si << "_OBJECTS["
            << (objs.empty() ? (size_t)1 : objs.size()) << "] = {\n";
        if (objs.empty()) {
            out << "    {0, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, 0, -1, -1, 0, "
                   "0, 0, 0.0F, 1, 0, 3.0F, 20.0F, 9.8F, 1.0F, 1.5F, 1.0F, 0.6F, 0, "
                   "-1, 0, 15.0F, 0.0F, 0, 1.0F, 8.0F, 0, 0, -1, \"\", 1, 1, 1.0F, "
                   "16},\n";
        } else {
            auto soundIndexOf = [&](const std::string& path) {
                for (size_t i = 0; i < p.sounds.size(); ++i)
                    if (p.sounds[i] == path) return (int)i;
                return -1;
            };
            for (const SceneObject& o : objs) {
                out << "    {" << (int)o.type << ", " << vec3Init(o.position) << ", "
                    << vec3Init(o.rotation) << ", " << vec3Init(o.scale) << ", "
                    << vec3Init(o.color) << ", " << (o.physics ? 1 : 0) << ", "
                    << modelIndexOf(p, o) << ", " << materialIndexOf(p, o)
                    << ", "
                    // save points are always usable - USE is how they open
                    << ((o.usable || o.type == PrimitiveType::SavePoint) ? 1 : 0)
                    << ", " << o.emitterKind << ", "
                    << o.emitterCount << ", " << floatLit(o.emitterSize) << ", "
                    << (o.emitterEnabled ? 1 : 0) << ", "
                    << (o.emitterFollowPlayer ? 1 : 0) << ", "
                    << floatLit(o.emitterSpeed) << ", "
                    << floatLit(o.emitterSpread) << ", "
                    << floatLit(o.emitterGravity) << ", "
                    << floatLit(o.emitterWeight) << ", "
                    << floatLit(o.emitterLife) << ", " << floatLit(o.emitterGrow)
                    << ", " << floatLit(o.emitterOpacity) << ", "
                    << (o.emitterDieOnGround ? 1 : 0) << ", "
                    << soundIndexOf(o.soundPath) << ", " << (o.soundAuto ? 1 : 0)
                    << ", " << floatLit(o.soundRange) << ", "
                    << floatLit(o.soundInterval) << ", " << (o.soundOnPlayer ? 1 : 0)
                    << ", " << floatLit(o.lightBright)
                    << ", " << floatLit(o.lightRadius) << ", " << (o.saveState ? 1 : 0)
                    << ", " << o.collisionMode << ", " << animModelIndexOf(p, o)
                    << ", \"" << escapeCString(o.animClip) << "\", "
                    << (o.animAutoplay ? 1 : 0) << ", " << (o.animLoop ? 1 : 0)
                    << ", " << floatLit(o.animSpeed) << ", "
                    << clampPrimDetail(o.primDetail) << "},  // " << o.name << "\n";
            }
        }
        out << "};\n";
    }

    out << "\nconstexpr int SCENE_OBJECT_COUNTS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "") << p.scenes[si].objects.size();
    out << "};\n"
           "inline const SceneObjectData* SCENE_OBJECT_TABLES[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "") << "SCENE_" << si << "_OBJECTS";
    out << "};\n\n";

    // Sound effect samples referenced by sound emitters (SceneObjectData.snd
    // indexes this list; res/sfx/x.wav -> sfx/x.adpcm next to the ELF)
    out << "constexpr int SND_COUNT = " << p.sounds.size() << ";\n"
        << "inline const char* SND_PATHS[" << (p.sounds.empty() ? (size_t)1 : p.sounds.size())
        << "] = {";
    if (p.sounds.empty()) {
        out << "\"\"";
    } else {
        for (size_t i = 0; i < p.sounds.size(); ++i) {
            std::string bin = p.sounds[i];
            if (bin.rfind("res/", 0) == 0) bin = bin.substr(4);
            if (const size_t dot = bin.rfind('.'); dot != std::string::npos)
                bin = bin.substr(0, dot);
            out << (i ? ", " : "") << "\"" << bin << ".adpcm\"";
        }
    }
    out << "};\n\n";

    // Player entity per scene: the first Player object drives the camera
    std::vector<const SceneObject*> players(sceneCount, nullptr);
    std::vector<int> playerIdx(sceneCount, -1);
    for (int si = 0; si < sceneCount; ++si)
        for (size_t i = 0; i < p.scenes[si].objects.size(); ++i)
            if (p.scenes[si].objects[i].type == PrimitiveType::Player) {
                players[si] = &p.scenes[si].objects[i];
                playerIdx[si] = (int)i;
                break;
            }

    out << "constexpr int PLAYER_INDEXES[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si) out << (si ? ", " : "") << playerIdx[si];
    out << "};\n"
        << "constexpr int PLAYER_MODES[SCENE_COUNT] = {";  // 0 = walk, 1 = noclip
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "") << (players[si] ? players[si]->playerMode : 0);
    out << "};\n"
        << "constexpr float PLAYER_WALK_SPEEDS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << floatLit(players[si] ? players[si]->playerWalkSpeed : 0.4f);
    out << "};\n"
        << "constexpr float PLAYER_LOOK_SPEEDS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << floatLit(players[si] ? players[si]->playerLookSpeed : 1.0f);
    out << "};\n"
        << "constexpr float PLAYER_EYE_HEIGHTS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << floatLit(players[si] ? players[si]->playerEyeHeight : 1.8f);
    out << "};\n"
        << "constexpr float PLAYER_JUMP_SPEEDS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << floatLit(players[si] ? players[si]->playerJumpSpeed : 4.5f);
    out << "};\n"
        << "constexpr bool PLAYER_CAN_JUMPS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << (!players[si] || players[si]->playerCanJump ? "true" : "false");
    out << "};\n\n";

    // Per-scene settings: the project defaults with each scene's active
    // override categories applied (see project::resolvedSettings). Lighting,
    // sky, clipping, post-FX and the usable-highlight are per scene; the game
    // reads them through the SCENE_*/SKY_*/... accessor macros in the game cpp.
    std::vector<ProjectSettings> rs;
    rs.reserve(sceneCount);
    for (int si = 0; si < sceneCount; ++si)
        rs.push_back(project::resolvedSettings(p, p.scenes[si]));

    auto sceneFloats = [&](const char* name, auto get) {
        out << "constexpr float " << name << "[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si) out << (si ? ", " : "") << get(si);
        out << "};\n";
    };
    auto sceneInts = [&](const char* name, auto get) {
        out << "constexpr int " << name << "[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si) out << (si ? ", " : "") << get(si);
        out << "};\n";
    };
    auto sceneBools = [&](const char* name, auto get) {
        out << "constexpr bool " << name << "[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si)
            out << (si ? ", " : "") << (get(si) ? "true" : "false");
        out << "};\n";
    };
    auto fx128 = [](float v) {
        int n = (int)(v * 128.0f + 0.5f);
        return n < 0 ? 0 : (n > 128 ? 128 : n);
    };

    sceneFloats("TERRAIN_WIDTHS",
                [&](int si) { return floatLit((float)p.scenes[si].terrain.width); });
    sceneFloats("TERRAIN_DEPTHS",
                [&](int si) { return floatLit((float)p.scenes[si].terrain.depth); });
    auto lightOf = [&](int si, int axis) {
        float lx = rs[si].lightDir[0], ly = rs[si].lightDir[1], lz = rs[si].lightDir[2];
        const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
        if (len > 1e-5f) lx /= len, ly /= len, lz /= len;
        else lx = 0, ly = 1, lz = 0;
        return floatLit(axis == 0 ? lx : axis == 1 ? ly : lz);
    };
    sceneFloats("SCENE_LIGHT_XS", [&](int si) { return lightOf(si, 0); });
    sceneFloats("SCENE_LIGHT_YS", [&](int si) { return lightOf(si, 1); });
    sceneFloats("SCENE_LIGHT_ZS", [&](int si) { return lightOf(si, 2); });
    sceneFloats("SCENE_AMBIENTS", [&](int si) { return floatLit(rs[si].ambient); });
    sceneFloats("SCENE_DIFFUSES", [&](int si) { return floatLit(rs[si].diffuse); });
    sceneFloats("SCENE_LIGHT_COL_RS", [&](int si) { return floatLit(rs[si].lightColor[0]); });
    sceneFloats("SCENE_LIGHT_COL_GS", [&](int si) { return floatLit(rs[si].lightColor[1]); });
    sceneFloats("SCENE_LIGHT_COL_BS", [&](int si) { return floatLit(rs[si].lightColor[2]); });
    sceneFloats("SCENE_BRIGHTNESSES", [&](int si) { return floatLit(rs[si].brightness); });

    // Per-scene sky, clipping, post-FX and usable-highlight (Scene > Preferences
    // overrides of Project > Preferences). Accessor macros drop the trailing S.
    sceneBools("CLIP_PRECISES", [&](int si) { return rs[si].clipping != "fast"; });
    sceneFloats("SKY_RS", [&](int si) { return floatLit(rs[si].skyColor[0] * 255.0f); });
    sceneFloats("SKY_GS", [&](int si) { return floatLit(rs[si].skyColor[1] * 255.0f); });
    sceneFloats("SKY_BS", [&](int si) { return floatLit(rs[si].skyColor[2] * 255.0f); });
    sceneBools("SKY_DOMES", [&](int si) { return rs[si].skyDome; });
    sceneFloats("SKY_TOP_RS", [&](int si) { return floatLit(rs[si].skyTopColor[0] * 255.0f); });
    sceneFloats("SKY_TOP_GS", [&](int si) { return floatLit(rs[si].skyTopColor[1] * 255.0f); });
    sceneFloats("SKY_TOP_BS", [&](int si) { return floatLit(rs[si].skyTopColor[2] * 255.0f); });
    sceneInts("POSTFX_BLOOMS", [&](int si) { return fx128(rs[si].bloom); });
    sceneInts("POSTFX_GRAINS", [&](int si) { return fx128(rs[si].grain); });
    sceneBools("HIGHLIGHT_USABLES", [&](int si) { return rs[si].highlightUsable; });
    sceneFloats("HIGHLIGHT_DISTANCES", [&](int si) { return floatLit(rs[si].highlightDistance); });
    sceneFloats("HIGHLIGHT_RS", [&](int si) { return floatLit(rs[si].highlightColor[0] * 255.0f); });
    sceneFloats("HIGHLIGHT_GS", [&](int si) { return floatLit(rs[si].highlightColor[1] * 255.0f); });
    sceneFloats("HIGHLIGHT_BS", [&](int si) { return floatLit(rs[si].highlightColor[2] * 255.0f); });
    sceneFloats("HIGHLIGHT_WIDTHS", [&](int si) { return floatLit(rs[si].highlightWidth); });
    sceneInts("HIGHLIGHT_STEPS_S", [&](int si) { return rs[si].highlightSteps; });

    // Color grading presets (Tools > Color Grading), compiled to the raw
    // GS-level parameters RendererCorePostFx::setGrading takes (the editor
    // viewport previews the same quantized numbers). GRADING_DEFAULT is
    // applied at boot; the Set Color Grading flow node switches at runtime.
    const size_t gradingCount = p.gradings.size();
    std::vector<CompiledGrading> compiled;
    for (const auto& g : p.gradings) compiled.push_back(compileGrading(g));
    out << "\nconstexpr int GRADING_COUNT = " << gradingCount << ";\n"
        << "inline const char* GRADING_NAMES[GRADING_COUNT > 0 ? "
           "GRADING_COUNT : 1] = {";
    if (gradingCount == 0) {
        out << "\"\"";
    } else {
        for (size_t i = 0; i < gradingCount; ++i)
            out << (i ? ", " : "") << "\"" << p.gradings[i].name << "\"";
    }
    out << "};\n";
    auto gradingTriples = [&](const char* type, const char* name, auto get) {
        out << "constexpr " << type << " " << name
            << "[GRADING_COUNT > 0 ? GRADING_COUNT : 1][3] = {";
        if (gradingCount == 0) out << "{0, 0, 0}";
        for (size_t i = 0; i < gradingCount; ++i) {
            const int* v = get(compiled[i]);
            out << (i ? ", " : "") << "{" << v[0] << ", " << v[1] << ", " << v[2]
                << "}";
        }
        out << "};\n";
    };
    gradingTriples("unsigned char", "GRADING_GAINS",
                   [](const CompiledGrading& c) { return c.gain; });
    gradingTriples("short", "GRADING_LIFTS",
                   [](const CompiledGrading& c) { return c.lift; });
    gradingTriples("unsigned char", "GRADING_MIX_COLORS",
                   [](const CompiledGrading& c) { return c.mixColor; });
    out << "constexpr unsigned char GRADING_MIX_AMTS[GRADING_COUNT > 0 ? "
           "GRADING_COUNT : 1] = {";
    if (gradingCount == 0) out << "0";
    for (size_t i = 0; i < gradingCount; ++i)
        out << (i ? ", " : "") << compiled[i].mixAmt;
    out << "};\n";
    const int defGrading =
        (p.defaultGrading >= 0 && p.defaultGrading < (int)gradingCount)
            ? p.defaultGrading
            : -1;
    out << "constexpr int GRADING_DEFAULT = " << defGrading << ";\n"
        << "\n"
           "// Template so this header stays engine-include-free; instantiated\n"
           "// where Tyra::Engine is complete. index -1 (or any out of range)\n"
           "// is a no-op.\n"
           "template <typename TEngine>\n"
           "inline void applySceneGrading(TEngine* engine, int index) {\n"
           "  if (index < 0 || index >= GRADING_COUNT) return;\n"
           "  engine->renderer.core.postFx.setGrading(\n"
           "      GRADING_GAINS[index], GRADING_LIFTS[index],\n"
           "      GRADING_MIX_COLORS[index], GRADING_MIX_AMTS[index]);\n"
           "}\n";

    // Save system: custom values (Project panel, Save data) and the largest
    // scene object count - sizes the fixed save-slot payload at compile time.
    const size_t valueCount = p.saveValues.size();
    out << "\nconstexpr int SAVE_VALUE_COUNT = " << valueCount << ";\n"
        << "inline const char* SAVE_VALUE_NAMES[SAVE_VALUE_COUNT > 0 ? "
           "SAVE_VALUE_COUNT : 1] = {";
    if (valueCount == 0) {
        out << "\"\"";
    } else {
        for (size_t i = 0; i < valueCount; ++i)
            out << (i ? ", " : "") << "\"" << p.saveValues[i].name << "\"";
    }
    out << "};\n"
        << "constexpr float SAVE_VALUE_DEFAULTS[SAVE_VALUE_COUNT > 0 ? "
           "SAVE_VALUE_COUNT : 1] = {";
    if (valueCount == 0) {
        out << "0.0F";
    } else {
        for (size_t i = 0; i < valueCount; ++i)
            out << (i ? ", " : "") << floatLit(p.saveValues[i].value);
    }
    out << "};\n";
    // Text values: fixed SAVE_TEXT_LEN-byte slots in the save payload
    const size_t textCount = p.saveTexts.size();
    out << "constexpr int SAVE_TEXT_COUNT = " << textCount << ";\n"
        << "constexpr int SAVE_TEXT_LEN = 32;  // incl. the terminating NUL\n"
        << "inline const char* SAVE_TEXT_NAMES[SAVE_TEXT_COUNT > 0 ? "
           "SAVE_TEXT_COUNT : 1] = {";
    if (textCount == 0) {
        out << "\"\"";
    } else {
        for (size_t i = 0; i < textCount; ++i)
            out << (i ? ", " : "") << "\"" << escapeCString(p.saveTexts[i].name)
                << "\"";
    }
    out << "};\n"
        << "inline const char* SAVE_TEXT_DEFAULTS[SAVE_TEXT_COUNT > 0 ? "
           "SAVE_TEXT_COUNT : 1] = {";
    if (textCount == 0) {
        out << "\"\"";
    } else {
        for (size_t i = 0; i < textCount; ++i)
            out << (i ? ", " : "") << "\"" << escapeCString(p.saveTexts[i].value)
                << "\"";
    }
    out << "};\n";
    size_t maxObjects = 1;
    for (const SceneData& sc : p.scenes)
        if (sc.objects.size() > maxObjects) maxObjects = sc.objects.size();
    out << "constexpr int SAVE_OBJECT_MAX = " << maxObjects << ";\n";

    out << "\n}  // namespace " << ns << "\n";

    // Active-scene accessors. g_activeScene is defined in the generated game
    // cpp; every TU that includes scene_data.hpp (the game cpp AND user scripts
    // via script.hpp) reads the per-scene tables through these macros. They
    // expand at the use site, which is inside `namespace " << ns << "` where
    // the tables above are visible.
    out << R"(
// Index of the scene the game is currently in (defined in the game cpp).
extern int g_activeScene;

// Wall-clock normalization (defined in the game cpp, set at init from the
// video mode): the game logic is tuned per-frame at PAL's 50 Hz, so on a
// 60 Hz NTSC signal every per-frame step is multiplied by g_frameScale
// (50/60) to cover the same distance per real second. g_frameDt is the real
// seconds-per-frame for code that works in units/s.
extern float g_frameRate;   // vsync rate: 50 (PAL) or 60 (NTSC)
extern float g_frameDt;     // 1 / g_frameRate
extern float g_frameScale;  // 50 / g_frameRate
// Frames per `seconds` of wall-clock time (>= 1), for frame-counter timers.
inline int everyFrames(float seconds) {
  const int f = (int)(seconds * g_frameRate);
  return f < 1 ? 1 : f;
}
#define SCENE_OBJECT_COUNT SCENE_OBJECT_COUNTS[g_activeScene]
#define SCENE_OBJECTS SCENE_OBJECT_TABLES[g_activeScene]
#define PLAYER_INDEX PLAYER_INDEXES[g_activeScene]
#define PLAYER_MODE PLAYER_MODES[g_activeScene]
#define PLAYER_WALK_SPEED PLAYER_WALK_SPEEDS[g_activeScene]
#define PLAYER_LOOK_SPEED PLAYER_LOOK_SPEEDS[g_activeScene]
#define PLAYER_EYE_HEIGHT PLAYER_EYE_HEIGHTS[g_activeScene]
#define PLAYER_JUMP_SPEED PLAYER_JUMP_SPEEDS[g_activeScene]
#define PLAYER_CAN_JUMP PLAYER_CAN_JUMPS[g_activeScene]
#define TERRAIN_WIDTH TERRAIN_WIDTHS[g_activeScene]
#define TERRAIN_DEPTH TERRAIN_DEPTHS[g_activeScene]
#define SCENE_LIGHT_X SCENE_LIGHT_XS[g_activeScene]
#define SCENE_LIGHT_Y SCENE_LIGHT_YS[g_activeScene]
#define SCENE_LIGHT_Z SCENE_LIGHT_ZS[g_activeScene]
#define SCENE_AMBIENT SCENE_AMBIENTS[g_activeScene]
#define SCENE_DIFFUSE SCENE_DIFFUSES[g_activeScene]
#define SCENE_LIGHT_COL_R SCENE_LIGHT_COL_RS[g_activeScene]
#define SCENE_LIGHT_COL_G SCENE_LIGHT_COL_GS[g_activeScene]
#define SCENE_LIGHT_COL_B SCENE_LIGHT_COL_BS[g_activeScene]
#define SCENE_BRIGHTNESS SCENE_BRIGHTNESSES[g_activeScene]
#define HM_W HM_WS[g_activeScene]
#define HM_D HM_DS[g_activeScene]
#define TERRAIN_HEIGHTS TERRAIN_HEIGHTS_TABLES[g_activeScene]
#define TERRAIN_TEXTURE TERRAIN_TEXTURES[g_activeScene]
#define TERRAIN_TEX_SCALE TERRAIN_TEX_SCALES[g_activeScene]
// Per-scene sky / clipping / post-FX / usable-highlight (Scene > Preferences)
#define CLIP_PRECISE CLIP_PRECISES[g_activeScene]
#define SKY_R SKY_RS[g_activeScene]
#define SKY_G SKY_GS[g_activeScene]
#define SKY_B SKY_BS[g_activeScene]
#define SKY_DOME SKY_DOMES[g_activeScene]
#define SKY_TOP_R SKY_TOP_RS[g_activeScene]
#define SKY_TOP_G SKY_TOP_GS[g_activeScene]
#define SKY_TOP_B SKY_TOP_BS[g_activeScene]
#define POSTFX_BLOOM POSTFX_BLOOMS[g_activeScene]
#define POSTFX_GRAIN POSTFX_GRAINS[g_activeScene]
#define HIGHLIGHT_USABLE HIGHLIGHT_USABLES[g_activeScene]
#define HIGHLIGHT_DISTANCE HIGHLIGHT_DISTANCES[g_activeScene]
#define HIGHLIGHT_R HIGHLIGHT_RS[g_activeScene]
#define HIGHLIGHT_G HIGHLIGHT_GS[g_activeScene]
#define HIGHLIGHT_B HIGHLIGHT_BS[g_activeScene]
#define HIGHLIGHT_WIDTH HIGHLIGHT_WIDTHS[g_activeScene]
#define HIGHLIGHT_STEPS HIGHLIGHT_STEPS_S[g_activeScene]
#define terrainHeightAt(x, z) terrainHeightAtScene(g_activeScene, (x), (z))
)";
    return out.str();
}

static std::string sanitizeNamespace(const std::string& name) {
    // Project name -> valid C++ namespace: letters/digits/underscore, no leading digit
    std::string ns;
    for (char c : name) {
        if (isalnum((unsigned char)c) || c == '_')
            ns += c;
        else
            ns += '_';
    }
    if (ns.empty() || isdigit((unsigned char)ns[0])) ns = "Game" + ns;
    if (!ns.empty() && islower((unsigned char)ns[0])) ns[0] = (char)toupper((unsigned char)ns[0]);
    return ns;
}

static std::string fillTemplate(const Project& p, const char* tpl) {
    // docker compose project name: lowercase, must start with letter/digit
    std::string nameLower;
    for (char c : p.name) nameLower += (char)tolower((unsigned char)c);
    if (nameLower.empty() || !isalnum((unsigned char)nameLower[0]))
        nameLower = "tyra-" + nameLower;

    std::string s = tpl;
    s = replaceAll(s, "{{NAME_LOWER}}", nameLower);
    s = replaceAll(s, "{{NAME}}", p.name);
    s = replaceAll(s, "{{NAME_UPPER_NS}}", sanitizeNamespace(p.name));
    s = replaceAll(s, "{{WIDTH}}", std::to_string(p.scenes[0].terrain.width));
    s = replaceAll(s, "{{DEPTH}}", std::to_string(p.scenes[0].terrain.depth));

    // Project-wide constants baked into terrain_config.hpp. Sky, clipping,
    // post-FX, usable-highlight and lighting are per scene now (scene_data.hpp
    // arrays via project::resolvedSettings), so no scalar tokens for them here.
    const ProjectSettings& st = p.settings;
    s = replaceAll(s, "{{DETAIL}}", std::to_string(st.terrainDetail));
    s = replaceAll(s, "{{EYE_HEIGHT}}", floatLit(st.eyeHeight));
    s = replaceAll(s, "{{WALK_SPEED}}", floatLit(st.walkSpeed));
    s = replaceAll(s, "{{LOOK_SPEED}}", floatLit(st.lookSpeed));
    s = replaceAll(s, "{{DEADZONE_L}}", floatLit(st.stickDeadzoneL));
    s = replaceAll(s, "{{DEADZONE_R}}", floatLit(st.stickDeadzoneR));
    s = replaceAll(s, "{{ORBIT_SPEED}}", floatLit(st.orbitSpeed));
    s = replaceAll(s, "{{GRAVITY}}", floatLit(st.gravity));
    s = replaceAll(s, "{{JUMP_SPEED}}", floatLit(st.jumpSpeed));
    s = replaceAll(s, "{{LOADING_SCREEN}}", st.loadingScreen ? "true" : "false");
    s = replaceAll(s, "{{FRAME_LIMIT}}", st.disableVsync ? "false" : "true");
    s = replaceAll(s, "{{VIDEO_MODE}}", st.videoSystem == "pal"    ? "PAL"
                                        : st.videoSystem == "ntsc" ? "NTSC"
                                                                   : "Auto");
    const bool debugProfile = st.buildProfile == "debug";
    s = replaceAll(s, "{{DEBUG_SHOW_FPS}}",
                   debugProfile && st.showFps ? "true" : "false");
    s = replaceAll(s, "{{DEBUG_SHOW_MEM}}",
                   debugProfile && st.showMemory ? "true" : "false");
    s = replaceAll(s, "{{ENGINE_SRC}}", engineSourceDir());
    s = replaceAll(s, "{{ENGINE_HASH}}", engineSourceHash());
    return s;
}

bool matchesLegacy(const Project& p, const std::string& relativePath,
                   const std::string& content) {
    const char* tpl = nullptr;
    if (relativePath == "src\\terrain_game.cpp")
        tpl = TPL_GAME_CPP_V1;
    else if (relativePath == "inc\\terrain_game.hpp")
        tpl = TPL_GAME_HPP_V1;
    if (!tpl) return false;
    return content == fillTemplate(p, tpl);
}

// ---------------------------------------------------------------------------
// Flow graph -> C++ script. Object names are resolved to indices at codegen
// time; unknown names produce a comment instead of code.
// ---------------------------------------------------------------------------
std::string flowGraphScript(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);

    auto sceneIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.scenes.size(); ++i)
            if (p.scenes[i].name == name) return (int)i;
        return -1;
    };
    auto gradingIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.gradings.size(); ++i)
            if (p.gradings[i].name == name) return (int)i;
        return -1;
    };
    auto saveValueIndex = [&](const std::string& name) {
        for (size_t i = 0; i < p.saveValues.size(); ++i)
            if (p.saveValues[i].name == name) return (int)i;
        return -1;
    };
    auto saveTextIndex = [&](const std::string& name) {
        for (size_t i = 0; i < p.saveTexts.size(); ++i)
            if (p.saveTexts[i].name == name) return (int)i;
        return -1;
    };
    auto menuIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.menus.size(); ++i)
            if (p.menus[i].name == name) return (int)i;
        return -1;
    };
    // Same list as menu_data.gen.hpp MENU_EVENTS - indices must agree.
    const std::vector<std::string> menuEvents = collectMenuEvents(p);
    auto menuEventIndex = [&](const std::string& name) {
        for (size_t i = 0; i < menuEvents.size(); ++i)
            if (menuEvents[i] == name) return (int)i;
        return -1;
    };

    // Flow variables ("Variables" nodes): one shared namespace per type
    // across every scene's graphs. Names resolve to indices into the
    // flowInt/flowBool/flowPos arrays emitted below - a variable exists by
    // being named on any Set/Get node.
    std::vector<std::string> intVars, boolVars, posVars;
    {
        auto collect = [](std::vector<std::string>& v, const std::string& name) {
            if (name.empty()) return;
            for (const std::string& e : v)
                if (e == name) return;
            v.push_back(name);
        };
        for (const SceneData& sc : p.scenes)
            for (const SceneObject& o : sc.objects)
                for (const FlowNode& n : o.flowGraph.nodes) {
                    if (n.type == "SetVarInt" || n.type == "VarAtLeast" ||
                        n.type == "GetVarIntText")
                        collect(intVars, n.str);
                    else if (n.type == "SetVarBool" || n.type == "GetVarBool")
                        collect(boolVars, n.str);
                    else if (n.type == "SetVarPos" || n.type == "GetVarPos")
                        collect(posVars, n.str);
                }
    }
    auto varIndex = [](const std::vector<std::string>& v, const std::string& name) {
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == name) return (int)i;
        return -1;
    };
    auto intLit = [](float f) { return std::to_string((long)std::lround(f)); };

    // Text plane (Log inputs, Convert nodes, save texts) only when used -
    // keeps graphs without text nodes free of the string helpers.
    bool anyTextNode = false;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects)
            for (const FlowNode& n : o.flowGraph.nodes)
                if (const FlowNodeType* t = flowNodeType(n.type))
                    anyTextNode |= (t->textIn || t->textOut);

    std::ostringstream out;
    out << "// Generated by tyra-editor from the per-object Flow Graphs. Do not\n"
           "// edit - regenerated on every build. Edit the graphs in the editor.\n"
           "#include \"scripts/script.hpp\"\n\n"
           "#include <math.h>\n"
           "#include <stdio.h>\n\n"
           "#include <string>\n\n"
           "namespace "
        << ns << " {\n";

    if (anyTextNode) {
        out << "\n// Text-plane helpers (Convert nodes / Get Save Value)\n"
               "static inline std::string flowNumText(float v) {\n"
               "  char b[32];\n"
               "  snprintf(b, sizeof(b), \"%g\", (double)v);\n"
               "  return std::string(b);\n"
               "}\n"
               "static inline std::string flowPosText(float x, float y, float z) {\n"
               "  char b[64];\n"
               "  snprintf(b, sizeof(b), \"(%g, %g, %g)\", (double)x, (double)y, "
               "(double)z);\n"
               "  return std::string(b);\n"
               "}\n";
    }

    if (!intVars.empty() || !boolVars.empty() || !posVars.empty()) {
        auto names = [](const std::vector<std::string>& v) {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
            return s;
        };
        out << "\n// Flow variables (\"Variables\" nodes): shared by every graph,\n"
               "// zeroed at boot, kept across scene switches, not saved to the\n"
               "// memory card (use Save values for persistence).\n";
        if (!intVars.empty())
            out << "static int flowInt[" << intVars.size() << "] = {};  // "
                << names(intVars) << "\n";
        if (!boolVars.empty())
            out << "static bool flowBool[" << boolVars.size() << "] = {};  // "
                << names(boolVars) << "\n";
        if (!posVars.empty())
            out << "static float flowPos[" << posVars.size() << "][3] = {};  // "
                << names(posVars) << "\n";
    }

    std::ostringstream registrations;
    bool anyGraph = false;

    for (size_t si = 0; si < p.scenes.size(); ++si) {
    const auto& sceneObjs = p.scenes[si].objects;
    auto objectIndex = [&](const std::string& name) {
        for (size_t i = 0; i < sceneObjs.size(); ++i)
            if (sceneObjs[i].name == name) return (int)i;
        return -1;
    };
    for (size_t ownerIdx = 0; ownerIdx < sceneObjs.size(); ++ownerIdx) {
        const FlowGraph& fg = sceneObjs[ownerIdx].flowGraph;
        if (fg.empty()) continue;
        anyGraph = true;

        auto nodeById = [&](int id) -> const FlowNode* {
            for (const FlowNode& n : fg.nodes)
                if (n.id == id) return &n;
            return nullptr;
        };

        // Which object a node refers to: incoming data link (follow the
        // chain) > explicit name > the owning object ("self"). Resolved
        // fully at codegen time - ids in links are wiring, not runtime data.
        auto resolveTarget = [&](const FlowNode& n) -> int {
            const FlowNode* cur = &n;
            std::vector<int> visited;
            for (;;) {
                bool seen = false;
                for (int id : visited) seen |= (id == cur->id);
                if (seen) break;  // cycle guard
                visited.push_back(cur->id);
                const FlowNodeType* t = flowNodeType(cur->type);
                if (!t || !t->idIn) break;
                const FlowLink* dataLink = nullptr;
                for (const FlowLink& l : fg.links)
                    if (l.kind == FlowLinkObject && l.toNode == cur->id) {
                        dataLink = &l;
                        break;
                    }
                if (!dataLink) break;
                const FlowNode* src = nodeById(dataLink->fromNode);
                if (!src) break;
                cur = src;
            }
            const FlowNodeType* ct = flowNodeType(cur->type);
            if (ct && ct->strKind == FlowParamKind::ObjectName && !cur->str.empty())
                return objectIndex(cur->str);
            return (int)ownerIdx;  // self
        };

        // XYZ expressions a node's position resolves to: an incoming position
        // link (Get Position reads the source object live; Set Object
        // Position forwards its own resolution) beats the node's own
        // X/Y/Z params (SetPosition) or its target object's position
        // (TeleportPlayer / GetPosition).
        std::function<std::array<std::string, 3>(const FlowNode&, std::vector<int>&)>
            posExprImpl = [&](const FlowNode& n,
                              std::vector<int>& visited) -> std::array<std::string, 3> {
            auto objectPos = [&](int idx) -> std::array<std::string, 3> {
                const std::string base =
                    "ctx.objects[" + std::to_string(idx) + "].data.position[";
                return {base + "0]", base + "1]", base + "2]"};
            };
            bool seen = false;
            for (int id : visited) seen |= (id == n.id);
            if (!seen) {
                visited.push_back(n.id);
                const FlowNodeType* t = flowNodeType(n.type);
                if (t && t->posIn) {
                    for (const FlowLink& l : fg.links) {
                        if (l.kind != FlowLinkPos || l.toNode != n.id) continue;
                        if (const FlowNode* src = nodeById(l.fromNode)) {
                            const FlowNodeType* st = flowNodeType(src->type);
                            if (st && st->posOut) return posExprImpl(*src, visited);
                        }
                    }
                }
            }
            if (n.type == "GetVarPos") {
                const int vi = varIndex(posVars, n.str);
                if (vi < 0) return {"0.0F", "0.0F", "0.0F"};
                const std::string base = "flowPos[" + std::to_string(vi) + "][";
                return {base + "0]", base + "1]", base + "2]"};
            }
            if (n.type == "SetPosition" || n.type == "SetVarPos" ||
                n.type == "MoveObjectTo")
                return {floatLit(n.num[0]), floatLit(n.num[1]), floatLit(n.num[2])};
            const int idx = resolveTarget(n);
            if (idx < 0) return {"0.0F", "0.0F", "0.0F"};
            return objectPos(idx);
        };
        auto posExpr = [&](const FlowNode& n) {
            std::vector<int> visited;
            return posExprImpl(n, visited);
        };

        // Boolean-value plane: each trigger exposes a per-frame condition,
        // logic gates fold those conditions, and "On Condition" turns a bool
        // back into an exec pulse. Every bool is a self-contained C++
        // expression evaluated fresh from ctx, so it inlines anywhere.
        auto sourceCondition = [&](const FlowNode& n) -> std::string {
            if (n.type == "IsVisible") {
                const int idx = resolveTarget(n);
                if (idx < 0) return "false";
                return "(ctx.objects[" + std::to_string(idx) + "].visible)";
            }
            if (n.type == "ValueAtLeast") {
                const int vi = saveValueIndex(n.str);
                if (vi < 0) return "false";
                return "(ctx.saveValues[" + std::to_string(vi) +
                       "] >= " + floatLit(n.num[0]) + ")";
            }
            if (n.type == "GetVarBool") {
                const int vi = varIndex(boolVars, n.str);
                if (vi < 0) return "false";
                return "flowBool[" + std::to_string(vi) + "]";
            }
            if (n.type == "VarAtLeast") {
                const int vi = varIndex(intVars, n.str);
                if (vi < 0) return "false";
                return "(flowInt[" + std::to_string(vi) + "] >= " + intLit(n.num[0]) +
                       ")";
            }
            if (n.type == "OnMenuEvent") {
                const int ei = menuEventIndex(n.str);
                if (ei < 0) return "false";  // no menu entry fires this event
                return "(ctx.menuEvent == " + std::to_string(ei) + ")";
            }
            return "false";
        };

        // A node's bool-output expression. Sources return their condition;
        // gates fold their (possibly several) bool inputs. visited is passed
        // by value so a value reused on two branches (a diamond) still emits,
        // while a genuine cycle short-circuits to "false".
        std::function<std::string(const FlowNode&, std::vector<int>)> boolExprImpl =
            [&](const FlowNode& n, std::vector<int> visited) -> std::string {
            for (int id : visited)
                if (id == n.id) return "false";  // cycle guard
            visited.push_back(n.id);
            const FlowNodeType* t = flowNodeType(n.type);
            if (!t) return "false";
            if (!(t->pure && t->boolIn)) return sourceCondition(n);  // bool source

            std::vector<std::string> es;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkBool || l.toNode != n.id) continue;
                const FlowNode* src = nodeById(l.fromNode);
                if (!src) continue;
                const FlowNodeType* st = flowNodeType(src->type);
                if (!st || !st->boolOut) continue;
                es.push_back(boolExprImpl(*src, visited));
            }
            if (es.empty()) return "false";

            auto fold = [&](const char* op) {
                std::string s = "(";
                for (size_t i = 0; i < es.size(); ++i) {
                    if (i) s += op;
                    s += es[i];
                }
                return s + ")";
            };
            auto parityOdd = [&]() {
                std::string s = "(";
                for (size_t i = 0; i < es.size(); ++i) {
                    if (i) s += " + ";
                    s += "(" + es[i] + " ? 1 : 0)";
                }
                s += ")";
                return "((" + s + " & 1) != 0)";
            };

            if (n.type == "And") return fold(" && ");
            if (n.type == "Nand") return "(!" + fold(" && ") + ")";
            if (n.type == "Or") return fold(" || ");
            if (n.type == "Not") return "(!" + fold(" || ") + ")";  // NOT of the fold
            if (n.type == "Xor") return parityOdd();
            if (n.type == "Xnor") return "(!" + parityOdd() + ")";
            return "false";
        };

        // OR of the bool inputs feeding a node (empty = no bool input wired)
        auto boolInputsOr = [&](const FlowNode& n) -> std::string {
            std::vector<std::string> es;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkBool || l.toNode != n.id) continue;
                const FlowNode* src = nodeById(l.fromNode);
                if (!src) continue;
                const FlowNodeType* st = flowNodeType(src->type);
                if (!st || !st->boolOut) continue;
                es.push_back(boolExprImpl(*src, std::vector<int>{}));
            }
            if (es.empty()) return "";
            std::string s = "(";
            for (size_t i = 0; i < es.size(); ++i) {
                if (i) s += " || ";
                s += es[i];
            }
            return s + ")";
        };

        // Text plane: a text-out node's std::string expression. Chains are
        // one level deep (only Log / Set Save Text consume text), so no
        // recursion is needed.
        auto textExpr = [&](const FlowNode& n) -> std::string {
            if (n.type == "GetSaveValue") {
                const int vi = saveValueIndex(n.str);
                if (vi < 0) return "std::string(\"?\")";
                return "flowNumText(ctx.saveValues[" + std::to_string(vi) + "])";
            }
            if (n.type == "GetSaveText") {
                const int ti = saveTextIndex(n.str);
                if (ti < 0) return "std::string(\"?\")";
                return "std::string(&ctx.saveTexts[" + std::to_string(ti) +
                       " * SAVE_TEXT_LEN])";
            }
            if (n.type == "GetVarIntText") {
                const int vi = varIndex(intVars, n.str);
                if (vi < 0) return "std::string(\"?\")";
                return "std::to_string(flowInt[" + std::to_string(vi) + "])";
            }
            if (n.type == "PosToText") {
                const auto e = posExpr(n);
                return "flowPosText(" + e[0] + ", " + e[1] + ", " + e[2] + ")";
            }
            if (n.type == "BoolToText") {
                std::string expr = boolInputsOr(n);
                if (expr.empty()) expr = "false";
                return "std::string(" + expr + " ? \"true\" : \"false\")";
            }
            return "std::string()";
        };

        // Text expressions wired into a node's text-in pin (link order)
        auto textInputs = [&](const FlowNode& n) -> std::vector<std::string> {
            std::vector<std::string> es;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkText || l.toNode != n.id) continue;
                const FlowNode* src = nodeById(l.fromNode);
                if (!src) continue;
                const FlowNodeType* st = flowNodeType(src->type);
                if (!st || !st->textOut) continue;
                es.push_back(textExpr(*src));
            }
            return es;
        };

        // Sounds referenced by this graph's Play Sound nodes become sfx<i>
        // members loaded once in init().
        std::vector<std::string> usedSounds;
        auto soundIndex = [&](const std::string& path) {
            bool known = false;
            for (const std::string& s : p.sounds) known |= (s == path);
            if (!known) return -1;
            for (size_t i = 0; i < usedSounds.size(); ++i)
                if (usedSounds[i] == path) return (int)i;
            usedSounds.push_back(path);
            return (int)usedSounds.size() - 1;
        };

        // action node -> inline statements
        auto actionCode = [&](const FlowNode& n, const std::string& pad) -> std::string {
            std::ostringstream c;
            const int idx = resolveTarget(n);
            const bool needsObject =
                flowNodeType(n.type)->strKind == FlowParamKind::ObjectName;
            if (needsObject && idx < 0) {
                c << pad << "// node " << n.id << " (" << n.type << "): unknown object '"
                  << n.str << "'\n";
                return c.str();
            }
            std::string obj = "ctx.objects[" + std::to_string(idx) + "]";
            if (n.type == "SetSky") {
                c << pad << "ctx.skyColor = Tyra::Color(" << floatLit(n.num[0] * 255.0f)
                  << ", " << floatLit(n.num[1] * 255.0f) << ", "
                  << floatLit(n.num[2] * 255.0f) << ");\n";
            } else if (n.type == "SwitchScene") {
                const int target = sceneIndexOf(n.str);
                if (target < 0) {
                    c << pad << "// node " << n.id << " (SwitchScene): unknown scene '"
                      << n.str << "'\n";
                } else {
                    c << pad << "ctx.requestScene = " << target << ";  // \"" << n.str
                      << "\"\n";
                }
            } else if (n.type == "SetGrading") {
                if (n.str.empty()) {
                    c << pad << "ctx.engine->renderer.core.postFx.clearGrading();\n";
                } else {
                    const int gi = gradingIndexOf(n.str);
                    if (gi < 0) {
                        c << pad << "// node " << n.id
                          << " (SetGrading): unknown preset '" << n.str << "'\n";
                    } else {
                        c << pad << "applySceneGrading(ctx.engine, " << gi
                          << ");  // \"" << n.str << "\"\n";
                    }
                }
            } else if (n.type == "ShowObject") {
                c << pad << obj << ".visible = true;\n";
            } else if (n.type == "HideObject") {
                c << pad << obj << ".visible = false;\n";
            } else if (n.type == "ToggleObject") {
                c << pad << obj << ".visible = !" << obj << ".visible;\n";
            } else if (n.type == "MoveObjectBy") {
                for (int a = 0; a < 3; ++a)
                    if (n.num[a] != 0.0f)
                        c << pad << obj << ".data.position[" << a
                          << "] += " << floatLit(n.num[a]) << ";\n";
                c << pad << obj << ".dirty = true;\n";
            } else if (n.type == "SetObjectColor") {
                for (int a = 0; a < 3; ++a)
                    c << pad << obj << ".data.color[" << a
                      << "] = " << floatLit(n.num[a]) << ";\n";
                c << pad << obj << ".dirty = true;\n";
            } else if (n.type == "SetPosition") {
                const auto e = posExpr(n);
                for (int a = 0; a < 3; ++a)
                    c << pad << obj << ".data.position[" << a << "] = " << e[a] << ";\n";
                c << pad << obj << ".dirty = true;\n";
            } else if (n.type == "TeleportPlayer") {
                const auto e = posExpr(n);
                c << pad << "ctx.teleport = true;\n"
                  << pad << "ctx.teleportPos = Tyra::Vec4(" << e[0] << ", " << e[1] << ", "
                  << e[2] << ");\n"
                  << pad << "ctx.teleportYaw = " << obj << ".data.rotation[1];\n";
            } else if (n.type == "Log") {
                // static text first, then every wired text input (link
                // order), space-separated. TYRA_LOG streams its args.
                const auto es = textInputs(n);
                c << pad << "TYRA_LOG(";
                bool first = true;
                if (!n.str.empty() || es.empty()) {
                    c << "\"" << escapeCString(n.str) << "\"";
                    first = false;
                }
                for (const std::string& e : es) {
                    c << (first ? "" : ", \" \", ") << e;
                    first = false;
                }
                c << ");\n";
            } else if (n.type == "Delay") {
                // arm (or restart) the countdown; the per-frame block at the
                // top of update() fires the linked actions when it hits 0
                c << pad << "delay" << n.id << " = everyFrames("
                  << floatLit(n.num[0]) << ");\n";
            } else if (n.type == "MoveObjectTo") {
                c << pad << "move" << n.id << " = true;\n";
            } else if (n.type == "SetSaveText") {
                const int ti = saveTextIndex(n.str);
                if (ti < 0) {
                    c << pad << "// node " << n.id
                      << " (SetSaveText): unknown save text '" << n.str << "'\n";
                } else {
                    const auto es = textInputs(n);
                    std::string val;
                    if (es.empty()) {
                        val = "std::string(\"" + escapeCString(n.str2) + "\")";
                    } else {
                        for (size_t i = 0; i < es.size(); ++i)
                            val += (i ? " + \" \" + " : "") + es[i];
                        if (es.size() > 1) val = "(" + val + ")";
                    }
                    c << pad << "snprintf(&ctx.saveTexts[" << ti
                      << " * SAVE_TEXT_LEN], SAVE_TEXT_LEN, \"%s\", (" << val
                      << ").c_str());  // \"" << n.str << "\"\n";
                }
            } else if (n.type == "PlayMusic") {
                bool knownTrack = false;
                for (const std::string& m : p.music) knownTrack |= (m == n.str);
                if (n.str.empty() || !knownTrack) {
                    // Tracks removed from the project stop playing even if a
                    // node still references them (the file may linger in bin/)
                    c << pad << "// node " << n.id << " (PlayMusic): unknown track '"
                      << n.str << "'\n";
                } else {
                    // res/audio/x.wav lands as audio/x.wav next to the ELF
                    std::string binPath = n.str;
                    if (binPath.rfind("res/", 0) == 0) binPath = binPath.substr(4);
                    int vol = (int)n.num[0];
                    if (vol < 0) vol = 0;
                    if (vol > 100) vol = 100;
                    c << pad << "{\n"
                      << pad << "  auto& song = ctx.engine->audio.song;\n"
                      << pad << "  song.stop();\n"
                      << pad << "  song.load(Tyra::FileUtils::fromCwd(\"" << binPath
                      << "\"));\n"
                      << pad << "  song.inLoop = " << (n.num[1] != 0.0f ? "true" : "false")
                      << ";\n"
                      << pad << "  song.setVolume(" << vol << ");\n"
                      << pad << "  song.play();\n"
                      << pad << "}\n";
                }
            } else if (n.type == "ShowHud") {
                c << pad << "ctx.hudVisible = true;\n";
            } else if (n.type == "HideHud") {
                c << pad << "ctx.hudVisible = false;\n";
            } else if (n.type == "ToggleHud") {
                c << pad << "ctx.hudVisible = !ctx.hudVisible;\n";
            } else if (n.type == "StopMusic") {
                c << pad << "ctx.engine->audio.song.stop();\n";
            } else if (n.type == "PlaySound") {
                const int si = soundIndex(n.str);
                if (si < 0) {
                    c << pad << "// node " << n.id << " (PlaySound): unknown sound '"
                      << n.str << "'\n";
                } else {
                    int vol = (int)n.num[0];
                    if (vol < 0) vol = 0;
                    if (vol > 100) vol = 100;
                    int ch = (int)n.num[1];
                    if (ch > 23) ch = 23;
                    c << pad << "{\n";
                    if (ch >= 0) {
                        c << pad << "  const s8 ch = " << ch << ";\n";
                    } else {
                        c << pad << "  const s8 ch = (s8)sfxNextCh;\n"
                          << pad << "  sfxNextCh = (sfxNextCh + 1) % 24;\n";
                    }
                    c << pad << "  ctx.engine->audio.adpcm.setVolume(" << vol << ", ch);\n"
                      << pad << "  ctx.engine->audio.adpcm.tryPlay(sfx" << si << ", ch);\n"
                      << pad << "}\n";
                }
            } else if (n.type == "SetMusicVolume") {
                int vol = (int)n.num[0];
                if (vol < 0) vol = 0;
                if (vol > 100) vol = 100;
                c << pad << "ctx.engine->audio.song.setVolume(" << vol << ");\n";
            } else if (n.type == "SetValue" || n.type == "AddValue") {
                const int vi = saveValueIndex(n.str);
                if (vi < 0) {
                    c << pad << "// node " << n.id << " (" << n.type
                      << "): unknown save value '" << n.str << "'\n";
                } else {
                    c << pad << "ctx.saveValues[" << vi << "] "
                      << (n.type == "SetValue" ? "=" : "+=") << " "
                      << floatLit(n.num[0]) << ";  // \"" << n.str << "\"\n";
                }
            } else if (n.type == "OpenSaveMenu") {
                c << pad << "ctx.openSaveMenu = true;\n";
            } else if (n.type == "SetVarInt") {
                const int vi = varIndex(intVars, n.str);
                if (vi < 0) {
                    c << pad << "// node " << n.id << " (SetVarInt): unnamed variable\n";
                } else {
                    c << pad << "flowInt[" << vi << "] = " << intLit(n.num[0])
                      << ";  // \"" << n.str << "\"\n";
                }
            } else if (n.type == "SetVarBool") {
                const int vi = varIndex(boolVars, n.str);
                if (vi < 0) {
                    c << pad << "// node " << n.id << " (SetVarBool): unnamed variable\n";
                } else {
                    c << pad << "flowBool[" << vi << "] = "
                      << (n.num[0] != 0.0f ? "true" : "false") << ";  // \"" << n.str
                      << "\"\n";
                }
            } else if (n.type == "SetVarPos") {
                const int vi = varIndex(posVars, n.str);
                if (vi < 0) {
                    c << pad << "// node " << n.id << " (SetVarPos): unnamed variable\n";
                } else {
                    const auto e = posExpr(n);  // position link beats X/Y/Z params
                    for (int a = 0; a < 3; ++a)
                        c << pad << "flowPos[" << vi << "][" << a << "] = " << e[a]
                          << ";" << (a == 0 ? "  // \"" + n.str + "\"" : "") << "\n";
                }
            } else if (n.type == "OpenMenu") {
                const int mi = menuIndexOf(n.str);
                if (mi < 0) {
                    c << pad << "// node " << n.id << " (OpenMenu): unknown menu '"
                      << n.str << "'\n";
                } else {
                    c << pad << "ctx.openMenu = " << mi << ";  // \"" << n.str
                      << "\"\n";
                }
            } else if (n.type == "PlayAnimation") {
                // str = clip name ("" = the model's first clip); Loop 1 = loop,
                // Speed <= 0 = the authored default (1.0); Fade = crossfade
                // seconds (0 = instant switch)
                const float speed = n.num[1] > 0.001f ? n.num[1] : 1.0f;
                const float fade = n.num[2] > 0.0f ? n.num[2] : 0.0f;
                c << pad << "playAnimation(ctx, " << idx << ", \""
                  << escapeCString(n.str) << "\", "
                  << (n.num[0] != 0.0f ? "true" : "false") << ", "
                  << floatLit(speed) << ", " << floatLit(fade) << ");\n";
            } else if (n.type == "StopAnimation") {
                c << pad << "stopAnimation(ctx, " << idx << ");\n";
            }
            return c.str();
        };

        // all actions exec-linked to a trigger (pure data nodes never "run")
        auto linkedActions = [&](int triggerId, const std::string& pad) {
            std::ostringstream c;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkExec || l.fromNode != triggerId) continue;
                for (const FlowNode& n : fg.nodes) {
                    if (n.id != l.toNode) continue;
                    const FlowNodeType* t = flowNodeType(n.type);
                    if (t && !t->trigger && !t->pure) c << actionCode(n, pad);
                }
            }
            return c.str();
        };

        const std::string cls =
            "FlowGraphScript_" + std::to_string(si) + "_" + std::to_string(ownerIdx);
        std::ostringstream clsOut;
        clsOut << "\n// Scene \"" << p.scenes[si].name << "\": graph of \""
            << sceneObjs[ownerIdx].name << "\" (object " << ownerIdx << ")\nclass " << cls
            << " : public Script {\n"
               " public:\n"
               "  void update(ScriptContext& ctx) override {\n"
               "    if (ctx.scene != "
            << si
            << ") return;\n"
               "    if (ctx.sceneGeneration != generation) {\n"
               "      // scene was (re)loaded - back to the initial state\n"
               "      generation = ctx.sceneGeneration;\n"
               "      frame = 0;\n"
               "      started = false;\n"
               "{{FLAG_RESETS}}"
               "    }\n"
               "    frame++;\n";

        std::ostringstream members;
        std::ostringstream flagResets;

        // Per-frame node state, ticked before the trigger scans so anything
        // armed this frame starts counting/moving on the NEXT frame:
        //  - Delay: countdown armed by its exec input; fires its "after"
        //    actions the frame it reaches 0.
        //  - Move Object To: glides the target toward the (live) goal at
        //    Speed units/s until it arrives.
        for (const FlowNode& n : fg.nodes) {
            if (n.type == "Delay") {
                const std::string var = "delay" + std::to_string(n.id);
                members << "  int " << var << " = 0;\n";
                flagResets << "      " << var << " = 0;\n";
                clsOut << "    if (" << var << " > 0 && --" << var << " == 0) {\n"
                       << linkedActions(n.id, "      ") << "    }\n";
            } else if (n.type == "MoveObjectTo") {
                const int idx = resolveTarget(n);
                if (idx < 0) {
                    clsOut << "    // node " << n.id
                           << " (MoveObjectTo): unknown object '" << n.str << "'\n";
                    continue;
                }
                const std::string var = "move" + std::to_string(n.id);
                members << "  bool " << var << " = false;\n";
                flagResets << "      " << var << " = false;\n";
                const auto e = posExpr(n);
                const float speed = n.num[3] > 0.001f ? n.num[3] : 2.0f;
                const std::string obj = "ctx.objects[" + std::to_string(idx) + "]";
                clsOut << "    if (" << var << ") {\n"
                       << "      float* pos = " << obj << ".data.position;\n"
                       << "      const float tx = " << e[0] << ";\n"
                       << "      const float ty = " << e[1] << ";\n"
                       << "      const float tz = " << e[2] << ";\n"
                       << "      const float dx = tx - pos[0];\n"
                       << "      const float dy = ty - pos[1];\n"
                       << "      const float dz = tz - pos[2];\n"
                       << "      const float dist = sqrtf(dx * dx + dy * dy + dz * dz);\n"
                       << "      const float step = " << floatLit(speed)
                       << " * g_frameDt;\n"
                       << "      if (dist <= step) {\n"
                       << "        pos[0] = tx; pos[1] = ty; pos[2] = tz;\n"
                       << "        " << var << " = false;\n"
                       << "      } else {\n"
                       << "        pos[0] += dx / dist * step;\n"
                       << "        pos[1] += dy / dist * step;\n"
                       << "        pos[2] += dz / dist * step;\n"
                       << "      }\n"
                       << "      " << obj << ".dirty = true;\n"
                       << "    }\n";
            }
        }

        for (const FlowNode& n : fg.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (!t || !t->trigger) continue;
            const std::string body = linkedActions(n.id, "      ");
            if (body.empty()) continue;

            if (n.type == "OnStart") {
                clsOut << "    if (!started) {\n      started = true;\n" << body << "    }\n";
            } else if (n.type == "OnUsed") {
                const int idx = resolveTarget(n);
                if (idx < 0) {
                    clsOut << "    // node " << n.id << " (OnUsed): unknown object '" << n.str
                        << "'\n";
                    continue;
                }
                clsOut << "    if (ctx.usedObject == " << idx << ") {\n" << body << "    }\n";
            } else if (n.type == "OnButton") {
                std::string btn = n.str.empty() ? "Cross" : n.str;
                clsOut << "    if (ctx.engine->pad.getClicked()." << btn << ") {\n" << body
                    << "    }\n";
            } else if (n.type == "NearObject") {
                const int idx = resolveTarget(n);
                if (idx < 0) {
                    clsOut << "    // node " << n.id << " (NearObject): unknown object '"
                        << n.str << "'\n";
                    continue;
                }
                const std::string flag = "near" + std::to_string(n.id);
                members << "  bool " << flag << " = false;\n";
                flagResets << "      " << flag << " = false;\n";
                const float r = n.num[0] > 0.01f ? n.num[0] : 3.0f;
                clsOut << "    {\n      const float dx = ctx.playerPosition.x - ctx.objects["
                    << idx
                    << "].data.position[0];\n      const float dz = ctx.playerPosition.z - "
                       "ctx.objects["
                    << idx
                    << "].data.position[2];\n      const bool isNear = dx * dx + dz * dz < "
                    << floatLit(r * r) << ";\n      if (isNear && !" << flag << ") {\n"
                    << body << "      }\n      " << flag << " = isNear;\n    }\n";
            } else if (n.type == "EverySeconds") {
                clsOut << "    if (frame % everyFrames(" << floatLit(n.num[0])
                       << ") == 0) {\n" << body << "    }\n";
            } else if (n.type == "OnAnimFinished") {
                const int idx = resolveTarget(n);
                if (idx < 0) {
                    clsOut << "    // node " << n.id
                           << " (OnAnimFinished): unknown object '" << n.str << "'\n";
                    continue;
                }
                clsOut << "    if (ctx.objects[" << idx << "].animFinished) {\n"
                       << body << "    }\n";
            } else if (n.type == "OnMenuEvent") {
                const int ei = menuEventIndex(n.str);
                if (ei < 0) {
                    clsOut << "    // node " << n.id
                        << " (OnMenuEvent): no menu entry fires event '" << n.str
                        << "'\n";
                    continue;
                }
                clsOut << "    if (ctx.menuEvent == " << ei << ") {  // \"" << n.str
                    << "\"\n" << body << "    }\n";
            } else if (n.type == "OnCondition") {
                // bridge bool -> exec: fire on the rising edge of the input
                const std::string expr = boolInputsOr(n);
                if (expr.empty()) {
                    clsOut << "    // node " << n.id
                        << " (OnCondition): no bool input\n";
                    continue;
                }
                const std::string flag = "cond" + std::to_string(n.id);
                members << "  bool " << flag << " = false;\n";
                flagResets << "      " << flag << " = false;\n";
                clsOut << "    {\n      const bool c = " << expr
                    << ";\n      if (c && !" << flag << ") {\n"
                    << body << "      }\n      " << flag << " = c;\n    }\n";
            }
        }

        clsOut << "  }\n";

        if (!usedSounds.empty()) {
            clsOut << "\n  void init(ScriptContext& ctx) override {\n";
            for (size_t i = 0; i < usedSounds.size(); ++i) {
                // res/sfx/x.wav lands as sfx/x.adpcm next to the ELF (adpenc)
                std::string binPath = usedSounds[i];
                if (binPath.rfind("res/", 0) == 0) binPath = binPath.substr(4);
                const size_t dot = binPath.rfind('.');
                if (dot != std::string::npos) binPath = binPath.substr(0, dot);
                clsOut << "    sfx" << i << " = ctx.engine->audio.adpcm.load(\n"
                    << "        Tyra::FileUtils::fromCwd(\"" << binPath << ".adpcm\"));\n";
            }
            clsOut << "  }\n";
        }

        clsOut << "\n private:\n"
               "  unsigned int generation = 0;\n"
               "  int frame = 0;\n"
               "  bool started = false;\n";
        if (!usedSounds.empty()) {
            clsOut << "  int sfxNextCh = 0;\n";
            for (size_t i = 0; i < usedSounds.size(); ++i)
                clsOut << "  audsrv_adpcm_t* sfx" << i << " = nullptr;\n";
        }
        clsOut << members.str() << "};\n";

        out << replaceAll(clsOut.str(), "{{FLAG_RESETS}}", flagResets.str());

        registrations << "TYRA_SCRIPT(" << ns << "::" << cls << ");\n";
    }
    }

    if (!anyGraph) out << "\n// No object has a flow graph yet.\n";

    out << "\n}  // namespace " << ns << "\n\n" << registrations.str();
    return out.str();
}

// inc/model_data.gen.hpp - .obj model paths (+ optional per-object .mtl
// overrides) and the primitive material libraries. Nothing is baked into the
// ELF: the game loads everything at startup through the engine's
// LeanObjLoader, from bin/ (the Makefile copies res/ next to the ELF).
static std::string modelDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const auto keys = collectModelKeys(p);
    const auto materials = collectMaterialPaths(p);

    auto binPathOf = [](std::string path) {
        // res/models/x.obj on the host lands as models/x.obj in bin/
        if (path.rfind("res/", 0) == 0) path = path.substr(4);
        return path;
    };

    // A CollisionMesh is only built for models some object collides with in
    // mesh mode - box-only models skip the memory and build time.
    std::vector<bool> needsCollider(keys.size(), false);
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            if (o.type != PrimitiveType::Model || o.collisionMode != 1) continue;
            for (size_t m = 0; m < keys.size(); ++m)
                if (keys[m].first == o.modelPath && keys[m].second == o.materialPath)
                    needsCollider[m] = true;
        }

    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns << " {\n\n"
        << "constexpr int MODEL_COUNT = " << keys.size() << ";\n"
        << "inline const char* MODEL_PATHS[MODEL_COUNT > 0 ? MODEL_COUNT : 1] = {\n";
    if (keys.empty()) {
        out << "    \"\",\n";
    } else {
        for (const auto& key : keys) out << "    \"" << binPathOf(key.first) << "\",\n";
    }
    out << "};\n"
           "// per-model .mtl override (\"\" = the model's own material libraries)\n"
           "inline const char* MODEL_MTLS[MODEL_COUNT > 0 ? MODEL_COUNT : 1] = {\n";
    if (keys.empty()) {
        out << "    \"\",\n";
    } else {
        for (const auto& key : keys)
            out << "    \"" << (key.second.empty() ? "" : binPathOf(key.second))
                << "\",\n";
    }
    out << "};\n"
           "constexpr bool MODEL_NEEDS_COLLIDER[MODEL_COUNT > 0 ? MODEL_COUNT : 1] = {";
    if (keys.empty()) {
        out << "false";
    } else {
        for (size_t m = 0; m < keys.size(); ++m)
            out << (m ? ", " : "") << (needsCollider[m] ? "true" : "false");
    }
    out << "};\n\n";

    // Animated models: .glb sources serialized to .tskl (skeleton, bind
    // mesh, keyframe tracks) at build time; loaded by the engine's
    // TsklLoader and skinned on the EE at runtime.
    const auto animPaths = collectAnimModelPaths(p);
    out << "constexpr int ANIM_MODEL_COUNT = " << animPaths.size() << ";\n"
        << "inline const char* ANIM_MODEL_PATHS[ANIM_MODEL_COUNT > 0 ? "
           "ANIM_MODEL_COUNT : 1] = {\n";
    if (animPaths.empty()) {
        out << "    \"\",\n";
    } else {
        for (std::string path : animPaths) {
            if (const size_t dot = path.rfind('.'); dot != std::string::npos)
                path = path.substr(0, dot) + ".tskl";
            out << "    \"" << binPathOf(path) << "\",\n";
        }
    }
    out << "};\n\n"
        << "// .mtl libraries assigned to primitives (first material = surface)\n"
        << "constexpr int MATERIAL_COUNT = " << materials.size() << ";\n"
        << "inline const char* MATERIAL_PATHS[MATERIAL_COUNT > 0 ? MATERIAL_COUNT : 1] = {\n";
    if (materials.empty()) {
        out << "    \"\",\n";
    } else {
        for (const auto& path : materials) out << "    \"" << binPathOf(path) << "\",\n";
    }
    out << "};\n\n}  // namespace " << ns << "\n";
    return out.str();
}

// inc/terrain_heights.gen.hpp - per-scene sculpted heightmaps + sampler
static std::string terrainHeightsHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const int sceneCount = (int)p.scenes.size();

    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n\n#include \"scene_data.hpp\"\n\nnamespace "
        << ns << " {\n\n";

    std::vector<int> vws(sceneCount, 2), vds(sceneCount, 2);
    for (int si = 0; si < sceneCount; ++si) {
        const SceneData& sc = p.scenes[si];
        const bool hasData =
            sc.hmW >= 2 && sc.hmD >= 2 && (int)sc.heights.size() == sc.hmW * sc.hmD;
        if (hasData) vws[si] = sc.hmW, vds[si] = sc.hmD;
        out << "// scene \"" << sc.name << "\"\n"
            << "constexpr float HM_" << si << "_HEIGHTS[" << vws[si] * vds[si] << "] = {";
        if (!hasData) {
            out << "0, 0, 0, 0";
        } else {
            for (int i = 0; i < vws[si] * vds[si]; ++i) {
                if (i % 12 == 0) out << "\n    ";
                out << floatLit(sc.heights[i]) << ",";
            }
        }
        out << "\n};\n";
    }

    auto intArr = [&](const char* name, auto get) {
        out << "constexpr int " << name << "[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si) out << (si ? ", " : "") << get(si);
        out << "};\n";
    };
    auto floatArr = [&](const char* name, auto get) {
        out << "constexpr float " << name << "[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si) out << (si ? ", " : "") << get(si);
        out << "};\n";
    };
    out << "\n";
    intArr("HM_WS", [&](int si) { return vws[si]; });
    intArr("HM_DS", [&](int si) { return vds[si]; });
    floatArr("HM_ORIGIN_XS",
             [&](int si) { return floatLit(-(float)p.scenes[si].terrain.width * 0.5f); });
    floatArr("HM_ORIGIN_ZS",
             [&](int si) { return floatLit(-(float)p.scenes[si].terrain.depth * 0.5f); });
    floatArr("HM_STEP_XS", [&](int si) {
        return floatLit((float)p.scenes[si].terrain.width / (vws[si] - 1));
    });
    floatArr("HM_STEP_ZS", [&](int si) {
        return floatLit((float)p.scenes[si].terrain.depth / (vds[si] - 1));
    });
    out << "inline const float* TERRAIN_HEIGHTS_TABLES[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "") << "HM_" << si << "_HEIGHTS";
    out << "};\n\n"
           "/** Bilinear terrain height at world coordinates in a scene. The\n"
           " * game maps terrainHeightAt(x, z) to the active scene. */\n"
           "inline float terrainHeightAtScene(int scene, float x, float z) {\n"
           "  const float* hm = TERRAIN_HEIGHTS_TABLES[scene];\n"
           "  const int hw = HM_WS[scene];\n"
           "  const int hd = HM_DS[scene];\n"
           "  float gx = (x - HM_ORIGIN_XS[scene]) / HM_STEP_XS[scene];\n"
           "  float gz = (z - HM_ORIGIN_ZS[scene]) / HM_STEP_ZS[scene];\n"
           "  if (gx < 0.0F) gx = 0.0F;\n"
           "  if (gz < 0.0F) gz = 0.0F;\n"
           "  if (gx > hw - 1.001F) gx = hw - 1.001F;\n"
           "  if (gz > hd - 1.001F) gz = hd - 1.001F;\n"
           "  const int ix = (int)gx;\n"
           "  const int iz = (int)gz;\n"
           "  const float fx = gx - ix;\n"
           "  const float fz = gz - iz;\n"
           "  const float t = hm[iz * hw + ix] * (1.0F - fx) +\n"
           "                  hm[iz * hw + ix + 1] * fx;\n"
           "  const float b = hm[(iz + 1) * hw + ix] * (1.0F - fx) +\n"
           "                  hm[(iz + 1) * hw + ix + 1] * fx;\n"
           "  return t * (1.0F - fz) + b * fz;\n"
           "}\n\n}  // namespace "
        << ns << "\n";
    return out.str();
}
// inc/texture_data.gen.hpp - PNG textures used by the scene
static std::string textureDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const auto paths = collectTexturePaths(p);

    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns << " {\n\n"
        << "constexpr int TEXTURE_COUNT = " << paths.size() << ";\n"
        << "inline const char* TEXTURE_PATHS[TEXTURE_COUNT > 0 ? TEXTURE_COUNT : 1] = {\n";
    if (paths.empty()) {
        out << "    \"\",\n";
    } else {
        for (const auto& path : paths) {
            // res/textures/x.png on the host lands as textures/x.png in bin/
            std::string binPath = path;
            if (binPath.rfind("res/", 0) == 0) binPath = binPath.substr(4);
            out << "    \"" << binPath << "\",\n";
        }
    }
    out << "};\n\n"
        << "constexpr int TERRAIN_TEXTURES[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "")
            << textureIndexOf(p, project::resolvedSettings(p, p.scenes[si]).terrainTexture);
    out << "};\n"
        << "constexpr float TERRAIN_TEX_SCALES[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "")
            << floatLit(project::resolvedSettings(p, p.scenes[si]).terrainTexScale);
    out << "};\n\n}  // namespace " << ns << "\n";
    return out.str();
}

// inc/hud_data.gen.hpp - HUD image sprites (PNG paths relative to bin/)
static std::string hudDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns
        << " {\n\n"
           "struct HudImageData {\n"
           "  const char* path;  // relative to the game binary (res/ is copied there)\n"
           "  float x, y;        // normalized screen position, center anchor\n"
           "  float w, h;        // size in pixels\n"
           "};\n\n"
        << "constexpr int HUD_COUNT = " << p.hud.size() << ";\n"
        << "inline const HudImageData HUD_IMAGES[HUD_COUNT > 0 ? HUD_COUNT : 1] = {\n";
    if (p.hud.empty()) {
        out << "    {\"\", 0, 0, 0, 0},\n";
    } else {
        for (const HudImage& h : p.hud) {
            // res/hud/x.png on the host lands as hud/x.png next to the ELF
            std::string binPath = h.imagePath;
            if (binPath.rfind("res/", 0) == 0) binPath = binPath.substr(4);
            out << "    {\"" << binPath << "\", " << floatLit(h.pos[0]) << ", "
                << floatLit(h.pos[1]) << ", " << floatLit(h.size[0]) << ", "
                << floatLit(h.size[1]) << "},  // " << h.name << "\n";
        }
    }
    out << "};\n\n}  // namespace " << ns << "\n";
    return out.str();
}

// Distinct "Flow event" names across all menu entries, first-seen order -
// the contract between menu_data.gen.hpp and the flow-graph codegen (the
// On Menu Event trigger resolves its name to an index in this list).
static std::vector<std::string> collectMenuEvents(const Project& p) {
    std::vector<std::string> events;
    for (const GameMenu& m : p.menus)
        for (const MenuEntry& e : m.entries) {
            if (e.action != MenuEntry::FlowEvent || e.param.empty()) continue;
            bool seen = false;
            for (const auto& s : events) seen |= (s == e.param);
            if (!seen) events.push_back(e.param);
        }
    return events;
}

// inc/menu_data.gen.hpp - the game-side mirror of Project::menus. Panels are
// baked to res/menus/*.png by the editor (menubake.cpp); entry params are
// resolved to indices here at codegen time.
static std::string menuDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const auto events = collectMenuEvents(p);
    auto sceneIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.scenes.size(); ++i)
            if (p.scenes[i].name == name) return (int)i;
        return -1;
    };
    auto menuIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.menus.size(); ++i)
            if (p.menus[i].name == name) return (int)i;
        return -1;
    };
    auto valueIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.saveValues.size(); ++i)
            if (p.saveValues[i].name == name) return (int)i;
        return -1;
    };
    auto eventIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i] == name) return (int)i;
        return -1;
    };

    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns
        << " {\n\n"
           "// Menu entry actions: 0 close, 1 switch scene, 2 open save menu,\n"
           "// 3 open menu (submenu), 4 set save value, 5 add to save value,\n"
           "// 6 fire flow event. param = resolved index, -1 = unknown target.\n"
           "struct MenuEntryData {\n"
           "  int action;\n"
           "  int param;\n"
           "  float amount;\n"
           "};\n\n"
           "struct MenuData {\n"
           "  const char* panel;  // baked panel sprite, relative to the ELF\n"
           "  int panelW, panelH; // sprite/texture size (pow2 canvas)\n"
           "  int contentH;       // drawn panel part (vertical centering)\n"
           "  int row0Y, rowH;    // cursor row geometry (baked into the panel)\n"
           "  int entryCount;\n"
           "  const MenuEntryData* entries;\n"
           "  int titleScreen;    // 1 = opens at game start\n"
           "  int pause;          // 1 = gameplay freezes + dim overlay\n"
           "  float screenX, screenY;  // normalized panel-center position\n"
           "};\n\n"
        << "constexpr int MENU_COUNT = " << p.menus.size() << ";\n\n";

    for (size_t mi = 0; mi < p.menus.size(); ++mi) {
        const GameMenu& m = p.menus[mi];
        const int entries = (int)m.entries.size() > menubake::kMaxEntries
                                ? menubake::kMaxEntries
                                : (int)m.entries.size();
        out << "// menu \"" << m.name << "\"\n"
            << "constexpr MenuEntryData MENU_" << mi << "_ENTRIES["
            << (entries > 0 ? entries : 1) << "] = {\n";
        if (entries == 0) {
            out << "    {0, -1, 0.0F},\n";
        } else {
            for (int e = 0; e < entries; ++e) {
                const MenuEntry& en = m.entries[e];
                int param = -1;
                switch (en.action) {
                    case MenuEntry::SwitchScene: param = sceneIndexOf(en.param); break;
                    case MenuEntry::OpenMenu: param = menuIndexOf(en.param); break;
                    case MenuEntry::SetValue:
                    case MenuEntry::AddValue: param = valueIndexOf(en.param); break;
                    case MenuEntry::FlowEvent: param = eventIndexOf(en.param); break;
                    default: break;
                }
                out << "    {" << en.action << ", " << param << ", "
                    << floatLit(en.amount) << "},  // " << en.label << "\n";
            }
        }
        out << "};\n";
    }
    if (p.menus.empty())
        out << "constexpr MenuEntryData MENU_0_ENTRIES[1] = {{0, -1, 0.0F}};\n";

    int titleMenu = -1;
    for (size_t mi = 0; mi < p.menus.size(); ++mi)
        if (p.menus[mi].titleScreen && titleMenu < 0) titleMenu = (int)mi;

    int pauseMenu = -1;
    for (size_t mi = 0; mi < p.menus.size(); ++mi)
        if (p.menus[mi].pauseMenu && pauseMenu < 0) pauseMenu = (int)mi;

    out << "\ninline const MenuData MENUS[MENU_COUNT > 0 ? MENU_COUNT : 1] = {\n";
    if (p.menus.empty()) {
        out << "    {\"\", 0, 0, 0, 0, 0, 0, MENU_0_ENTRIES, 0, 0, 0.5F, 0.45F},\n";
        // (unreachable - MENU_COUNT is 0; the dummy keeps the array valid)
    } else {
        for (size_t mi = 0; mi < p.menus.size(); ++mi) {
            const GameMenu& m = p.menus[mi];
            const int entries = (int)m.entries.size() > menubake::kMaxEntries
                                    ? menubake::kMaxEntries
                                    : (int)m.entries.size();
            // Layout depends on the custom images (flow blocks push the
            // cursor rows down) - the baker is the single source of truth.
            const menubake::PanelLayout l = menubake::panelLayout(m, p.dir);
            out << "    {\"menus/" << menubake::panelFileName(m.name) << "\", "
                << l.panelW << ", " << l.canvasH << ", " << l.contentH << ", "
                << l.row0Y << ", " << l.rowH << ", " << entries
                << ", MENU_" << mi << "_ENTRIES, " << (m.titleScreen ? 1 : 0)
                << ", " << (m.pauseGame ? 1 : 0) << ", " << floatLit(m.screenPos[0])
                << ", " << floatLit(m.screenPos[1]) << "},  // " << m.name << "\n";
        }
    }
    out << "};\n\n"
        << "constexpr int TITLE_MENU = " << titleMenu << ";\n"
        << "// The Start button opens/closes this menu in-game (-1 = none)\n"
        << "constexpr int PAUSE_MENU = " << pauseMenu << ";\n\n"
        << "constexpr int MENU_EVENT_COUNT = " << events.size() << ";\n"
        << "// Names of the \"Flow event\" entry actions (menuEvent indexes this)\n"
        << "inline const char* MENU_EVENTS[MENU_EVENT_COUNT > 0 ? "
           "MENU_EVENT_COUNT : 1] = {";
    if (events.empty()) {
        out << "\"\"";
    } else {
        for (size_t i = 0; i < events.size(); ++i)
            out << (i ? ", " : "") << "\"" << events[i] << "\"";
    }
    out << "};\n\n}  // namespace " << ns << "\n";
    return out.str();
}

// Memory card directory for this game's saves ("TYRA-" + sanitized project
// name; libmc paths are card-root-relative, so no mc0: prefix. Card names
// allow up to 31 chars, keep well under).
static std::string saveGameDir(const Project& p) {
    std::string id;
    for (char c : p.name) {
        if (isalnum((unsigned char)c)) id += (char)toupper((unsigned char)c);
        if (id.size() >= 16) break;
    }
    if (id.empty()) id = "GAME";
    return "/TYRA-" + id;
}

// inc/save_system.gen.hpp - memory card save slots (fixed-size payload)
static std::string saveSystemHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#pragma once\n\n#include \"scene_data.hpp\"\n\nnamespace "
        << ns
        << " {\n\n"
           "// Memory card save system: fixed slots under SAVE_MC_DIR on card 1\n"
           "// (libmc, card-root-relative path). When the BIOS mc modules cannot\n"
           "// be loaded or no formatted PS2 card responds, the slots fall back\n"
           "// to save<n>.sav next to the ELF (host: under PCSX2).\n"
           "constexpr int SAVE_SLOTS = 3;\n"
        << "constexpr const char* SAVE_MC_DIR = \"" << saveGameDir(p)
        << "\";\n"
           "constexpr unsigned int SAVE_MAGIC = 0x56535954u;  // \"TYSV\"\n"
           "// v2: SaveGameData gained the text-value block (SAVE_TEXT_*)\n"
           "constexpr int SAVE_VERSION = 2;\n"
           "\n"
           "// Runtime state of one save-flagged object (SceneObjectData.saveState).\n"
           "struct SaveObjectState {\n"
           "  int index;  // object index in the saved scene\n"
           "  float position[3];\n"
           "  float color[3];\n"
           "  int visible;\n"
           "};\n"
           "\n"
           "// One slot's payload, written/read as a single fixed-size block\n"
           "// (64-byte aligned - the libmc RPC transfers it by DMA).\n"
           "struct alignas(64) SaveGameData {\n"
           "  unsigned int magic;\n"
           "  int version;\n"
           "  int scene;\n"
           "  float playerPos[3];  // feet position\n"
           "  float playerYaw;     // degrees\n"
           "  int valueCount;\n"
           "  float values[SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1];\n"
           "  int textCount;\n"
           "  char texts[SAVE_TEXT_COUNT > 0 ? SAVE_TEXT_COUNT : 1][SAVE_TEXT_LEN];\n"
           "  int objectCount;\n"
           "  SaveObjectState objects[SAVE_OBJECT_MAX];\n"
           "};\n"
           "\n"
           "// Loads the BIOS memory card modules once (sio2man is already\n"
           "// resident - the engine loads it for the pads).\n"
           "bool saveInit();\n"
           "bool saveMcReady();\n"
           "// Why the card is (un)available: [xmcman, mcman, xmcserv, mcserv]\n"
           "// SifLoadModule codes, then [mcInit, getInfo, probeOpen, formatResult,\n"
           "// probeOpen-after-format] (-999 = not attempted).\n"
           "const int* saveInitCodes();\n"
           "bool saveSlotUsed(int slot);\n"
           "bool saveWrite(int slot, const SaveGameData& data);\n"
           "bool saveRead(int slot, SaveGameData& out);\n"
           "\n}  // namespace "
        << ns << "\n";
    return out.str();
}

// src/save_system.gen.cpp - plain POSIX/stdio IO on both paths: the ps2sdk
// newlib port routes device-prefixed paths (mc0:, host:) through fio itself
// and forbids calling fio directly (#error in <fileio.h>). Host fallback
// resolves through FileUtils::fromCwd (lands next to the ELF).
static std::string saveSystemSource(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by tyra-editor. Do not edit - regenerated on every build.\n"
           "#include \"save_system.gen.hpp\"\n"
           "\n"
           "#include <tyra>\n"
           "#include <libmc.h>\n"
           "#include <loadfile.h>\n"
           "#include <stdio.h>\n"
           "#include <string>\n"
           "\n"
           "namespace "
        << ns
        << R"( {

static bool mcTried = false;
static bool mcReady = false;
// [xmcman, mcman, xmcserv, mcserv] SifLoadModule codes + [mcInit, getInfo,
// probeOpen, formatResult, probeOpen-after-format]
static int initCodes[9] = {-999, -999, -999, -999, -999, -999, -999, -999, -999};

// ioman-style open flags understood by mcserv
static const int kMcRdonly = 0x0001, kMcWronly = 0x0002, kMcCreat = 0x0200;

// Judges card health by doing exactly what saves do: ensure the save dir
// and open/close/delete a probe file. Returns the mcOpen result; -2 is
// sceMcResNoFormat (a virgin card).
static int probeCard() {
  int r = -100;
  mcMkDir(0, 0, SAVE_MC_DIR);
  mcSync(MC_WAIT, nullptr, &r);  // "already exists" is fine - the open decides
  std::string probe = std::string(SAVE_MC_DIR) + "/probe.tmp";
  int fd = -100;
  mcOpen(0, 0, probe.c_str(), kMcWronly | kMcCreat);
  mcSync(MC_WAIT, nullptr, &fd);
  if (fd >= 0) {
    mcClose(fd);
    mcSync(MC_WAIT, nullptr, &r);
    mcDelete(0, 0, probe.c_str());
    mcSync(MC_WAIT, nullptr, &r);
  }
  return fd;
}

bool saveInit() {
  if (mcTried) return mcReady;
  mcTried = true;
  // BIOS ROM modules - nothing to embed. The X variants pair with the
  // ps2sdk sio2man the engine already loaded; plain MCMAN/MCSERV are the
  // fallback for early BIOSes. Never load a variant twice - a second
  // instance over a resident one hangs the IOP.
  initCodes[0] = SifLoadModule("rom0:XMCMAN", 0, nullptr);
  if (initCodes[0] < 0) initCodes[1] = SifLoadModule("rom0:MCMAN", 0, nullptr);
  const bool useX = initCodes[0] >= 0;
  const bool mcman = useX || initCodes[1] >= 0;
  bool mcserv = false;
  if (mcman) {
    if (useX) {
      initCodes[2] = SifLoadModule("rom0:XMCSERV", 0, nullptr);
      mcserv = initCodes[2] >= 0;
    } else {
      initCodes[3] = SifLoadModule("rom0:MCSERV", 0, nullptr);
      mcserv = initCodes[3] >= 0;
    }
  }
  if (mcman && mcserv) {
    initCodes[4] = mcInit(useX ? MC_TYPE_XMC : MC_TYPE_MC);
    if (initCodes[4] >= 0) {
      // Clear the "card changed" latch (the first query after boot always
      // reports a change). The out params are unreliable across module
      // variants, so card health is judged by a real probe write instead.
      int type = 0, freeSpace = 0, format = 0, ret = -100;
      mcGetInfo(0, 0, &type, &freeSpace, &format);
      mcSync(MC_WAIT, nullptr, &ret);
      initCodes[5] = ret;
      int fd = probeCard();
      if (fd == -1) fd = probeCard();  // -1 = changed-card latch; retry once
      initCodes[6] = fd;
      if (fd == -2) {
        // sceMcResNoFormat: a virgin card (PCSX2 images start unformatted)
        // holds no data, so formatting it destroys nothing. Formatted
        // cards never return this code.
        int fr = -100;
        mcFormat(0, 0);
        mcSync(MC_WAIT, nullptr, &fr);
        initCodes[7] = fr;
        fd = probeCard();
        if (fd == -1) fd = probeCard();
        initCodes[8] = fd;
      }
      mcReady = fd >= 0;
    }
  }
  TYRA_LOG("Save system: ", mcReady ? "memory card ready"
                                    : "no memory card - using host files");
  return mcReady;
}

bool saveMcReady() { return mcReady; }

const int* saveInitCodes() { return initCodes; }

static std::string mcSlotName(int slot) {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s/save%d.sav", SAVE_MC_DIR, slot);
  return std::string(buf);
}

static std::string hostSlotPath(int slot) {
  char buf[32];
  snprintf(buf, sizeof(buf), "save%d.sav", slot);  // next to the ELF
  return Tyra::FileUtils::fromCwd(buf);
}

bool saveWrite(int slot, const SaveGameData& data) {
  if (slot < 0 || slot >= SAVE_SLOTS) return false;
  if (mcReady) {
    int fd = -1;
    mcOpen(0, 0, mcSlotName(slot).c_str(), kMcWronly | kMcCreat);
    mcSync(MC_WAIT, nullptr, &fd);
    if (fd < 0) return false;
    int wrote = -1, ret = 0;
    mcWrite(fd, &data, sizeof(data));
    mcSync(MC_WAIT, nullptr, &wrote);
    mcClose(fd);
    mcSync(MC_WAIT, nullptr, &ret);
    return wrote == (int)sizeof(data);
  }
  FILE* f = fopen(hostSlotPath(slot).c_str(), "wb");
  if (!f) return false;
  const size_t written = fwrite(&data, 1, sizeof(data), f);
  fclose(f);
  return written == sizeof(data);
}

bool saveRead(int slot, SaveGameData& out) {
  if (slot < 0 || slot >= SAVE_SLOTS) return false;
  int got = -1;
  if (mcReady) {
    int fd = -1;
    mcOpen(0, 0, mcSlotName(slot).c_str(), kMcRdonly);
    mcSync(MC_WAIT, nullptr, &fd);
    if (fd < 0) return false;
    int ret = 0;
    mcRead(fd, &out, sizeof(out));
    mcSync(MC_WAIT, nullptr, &got);
    mcClose(fd);
    mcSync(MC_WAIT, nullptr, &ret);
  } else {
    FILE* f = fopen(hostSlotPath(slot).c_str(), "rb");
    if (!f) return false;
    got = (int)fread(&out, 1, sizeof(out), f);
    fclose(f);
  }
  if (got != (int)sizeof(out)) return false;
  if (out.magic != SAVE_MAGIC || out.version != SAVE_VERSION) return false;
  if (out.scene < 0 || out.scene >= SCENE_COUNT) return false;
  if (out.valueCount < 0 || out.valueCount > SAVE_VALUE_COUNT) return false;
  if (out.objectCount < 0 || out.objectCount > SAVE_OBJECT_MAX) return false;
  return true;
}

bool saveSlotUsed(int slot) {
  static SaveGameData probe;  // static - the payload can be a few KB
  return saveRead(slot, probe);
}

}  // namespace )"
        << ns << "\n";
    return out.str();
}

std::string scriptStub(const Project& p, const std::string& className,
                       const std::string& fileName) {
    std::string s = fillTemplate(p, TPL_SCRIPT_STUB);
    s = replaceAll(s, "{{SCRIPT_CLASS}}", className);
    s = replaceAll(s, "{{SCRIPT_FILE}}", fileName);
    return s;
}

// .vscode/c_cpp_properties.json - IntelliSense for scripts in VS Code.
// Uses the editor's bundled Tyra engine headers and the PS2SDK headers
// exported from the docker toolchain (see Runner). Machine-specific paths,
// hence regenerated on every build.
static std::string vscodeCppProperties() {
    auto slashes = [](std::string s) {
        for (auto& c : s)
            if (c == '\\') c = '/';
        return s;
    };

    std::string engineInc;
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        std::filesystem::path candidate = std::filesystem::path(exePath).parent_path() /
                                          ".." / "vendor" / "tyra" / "engine" / "inc";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec))
            engineInc = slashes(std::filesystem::weakly_canonical(candidate, ec).string());
    }

    std::string sdk;
    if (const char* lad = getenv("LOCALAPPDATA"))
        sdk = slashes(std::string(lad) + "\\tyra-editor\\ps2sdk");

    std::ostringstream out;
    out << "{\n"
           "  \"configurations\": [\n"
           "    {\n"
           "      \"name\": \"PS2 (Tyra)\",\n"
           "      \"includePath\": [\n"
           "        \"${workspaceFolder}/inc\",\n"
           "        \"${workspaceFolder}/src\"";
    if (!engineInc.empty()) out << ",\n        \"" << engineInc << "\"";
    if (!sdk.empty())
        out << ",\n        \"" << sdk << "/ee/include\",\n        \"" << sdk
            << "/common/include\"";
    out << "\n      ],\n"
           "      \"defines\": [\"_EE\"],\n"
           "      \"cStandard\": \"c11\",\n"
           "      \"cppStandard\": \"c++20\",\n"
           "      \"intelliSenseMode\": \"linux-gcc-x86\"\n"
           "    }\n"
           "  ],\n"
           "  \"version\": 4\n"
           "}\n";
    return out.str();
}

std::vector<File> bakeAnimAssets(const Project& p,
                                 std::vector<std::string>* warnings) {
    std::vector<File> files;
    auto warn = [&](const std::string& msg) {
        if (warnings) warnings->push_back(msg);
    };
    for (const std::string& relPath : collectAnimModelPaths(p)) {
        const std::string full = p.dir + "\\" + replaceAll(relPath, "/", "\\");
        // "res/models/sub/x.glb" -> dir "res/models/sub/", stem "x"
        std::string dir = relPath, stem = relPath;
        if (const size_t slash = relPath.find_last_of('/');
            slash != std::string::npos) {
            dir = relPath.substr(0, slash + 1);
            stem = relPath.substr(slash + 1);
        } else {
            dir = "";
        }
        if (const size_t dot = stem.rfind('.'); dot != std::string::npos)
            stem = stem.substr(0, dot);
        // the game's cwd-relative directory ("res/" is copied next to the ELF)
        std::string binDir = dir;
        if (binDir.rfind("res/", 0) == 0) binDir = binDir.substr(4);

        glbparser::Skel skel;
        std::string error;
        if (!glbparser::parseSkel(full, skel, error)) {
            warn(relPath + ": " + error);
            continue;
        }
        for (const std::string& w : skel.warnings) warn(relPath + ": " + w);

        // Extracted textures land next to the .tskl, prefixed with the model
        // stem so two models' equally-named images cannot collide. The game
        // loads them by these bin/-relative paths (stored in the .tskl).
        std::vector<std::string> textureNames;
        for (size_t i = 0; i < skel.images.size(); ++i) {
            const std::string png = stem + "_" + skel.images[i].name;
            textureNames.push_back(binDir + png);
            files.push_back(
                {replaceAll(dir, "/", "\\") + png,
                 std::string(
                     reinterpret_cast<const char*>(skel.images[i].png.data()),
                     skel.images[i].png.size())});
        }
        files.push_back({replaceAll(dir + stem, "/", "\\") + ".tskl",
                         glbparser::writeTskl(skel, textureNames)});
    }
    return files;
}

// src/scripts/object_scripts.gen.cpp - the object-script runtime: attachment
// table (scene index, object index, class name - straight from the editor's
// Properties > Scripts) plus a driver that owns the instances. The driver is
// a regular global Script, so any generated game runs it without changes to
// the (user-ownable) terrain_game.cpp: on a scene (re)load it deletes the old
// instances and creates one per attachment via the TYRA_OBJECT_SCRIPT factory
// registry; every frame it refreshes `self` and forwards onUpdate/onUsed.
static std::string objectScriptsSource(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);

    struct Attach {
        int scene, object;
        std::string script, comment;
    };
    std::vector<Attach> attaches;
    for (size_t si = 0; si < p.scenes.size(); ++si)
        for (size_t oi = 0; oi < p.scenes[si].objects.size(); ++oi)
            for (const std::string& s : p.scenes[si].objects[oi].scripts)
                attaches.push_back({(int)si, (int)oi, s,
                                    p.scenes[si].name + " / " +
                                        p.scenes[si].objects[oi].name});

    std::ostringstream out;
    out << "// Generated by tyra-editor from the per-object script attachments. Do\n"
           "// not edit - regenerated on every build. Attach/detach scripts on\n"
           "// objects in the editor: Properties > Scripts.\n"
           "#include <string.h>\n"
           "\n"
           "#include <vector>\n"
           "\n"
           "#include \"scripts/script.hpp\"\n"
           "\n"
           "namespace "
        << ns
        << " {\n"
           "\n"
           "struct ObjectScriptAttach {\n"
           "  int scene;            // scene_data.hpp scene index\n"
           "  int object;           // object index within that scene\n"
           "  const char* script;   // TYRA_OBJECT_SCRIPT registration name\n"
           "};\n"
           "\n"
        << "constexpr int OBJECT_SCRIPT_ATTACH_COUNT = " << attaches.size() << ";\n"
        << "constexpr ObjectScriptAttach OBJECT_SCRIPT_ATTACHES["
        << (attaches.empty() ? (size_t)1 : attaches.size()) << "] = {\n";
    if (attaches.empty()) out << "    {0, 0, \"\"},\n";
    for (const Attach& a : attaches)
        out << "    {" << a.scene << ", " << a.object << ", \""
            << escapeCString(a.script) << "\"},  // " << a.comment << "\n";
    out << "};\n"
           "\n"
           "/** Owns the live ObjectScript instances (one per attachment of the\n"
           " * active scene). Rebuilds them when the scene generation changes;\n"
           " * unattached classes and other scenes' attachments cost nothing. */\n"
           "class ObjectScriptDriver : public Script {\n"
           " public:\n"
           "  void update(ScriptContext& ctx) override {\n"
           "    if (first || ctx.sceneGeneration != generation) {\n"
           "      first = false;\n"
           "      generation = ctx.sceneGeneration;\n"
           "      rebuild(ctx);\n"
           "    }\n"
           "    for (ObjectScript* s : instances) {\n"
           "      // runtimeObjects is rebuilt per scene - re-resolve every frame\n"
           "      s->self = &ctx.objects[s->selfIndex];\n"
           "      s->onUpdate(ctx);\n"
           "    }\n"
           "    if (ctx.usedObject >= 0)\n"
           "      for (ObjectScript* s : instances)\n"
           "        if (s->selfIndex == ctx.usedObject) s->onUsed(ctx);\n"
           "  }\n"
           "\n"
           " private:\n"
           "  void rebuild(ScriptContext& ctx) {\n"
           "    for (ObjectScript* s : instances) delete s;\n"
           "    instances.clear();\n"
           "    for (int i = 0; i < OBJECT_SCRIPT_ATTACH_COUNT; ++i) {\n"
           "      const ObjectScriptAttach& a = OBJECT_SCRIPT_ATTACHES[i];\n"
           "      if (a.scene != ctx.scene) continue;\n"
           "      if (a.object < 0 || a.object >= ctx.objectCount) continue;\n"
           "      ObjectScript* s = nullptr;\n"
           "      for (const ObjectScriptFactory& f : getObjectScriptFactories())\n"
           "        if (strcmp(f.name, a.script) == 0) {\n"
           "          s = f.create();\n"
           "          break;\n"
           "        }\n"
           "      if (!s) {\n"
           "        TYRA_LOG(\"Object script not registered: \", a.script);\n"
           "        continue;\n"
           "      }\n"
           "      s->selfIndex = a.object;\n"
           "      s->self = &ctx.objects[a.object];\n"
           "      instances.push_back(s);\n"
           "      s->onStart(ctx);\n"
           "    }\n"
           "  }\n"
           "\n"
           "  std::vector<ObjectScript*> instances;\n"
           "  unsigned int generation = 0;\n"
           "  bool first = true;\n"
           "};\n"
           "\n"
           "}  // namespace "
        << ns
        << "\n"
           "\n"
           "TYRA_SCRIPT("
        << ns << "::ObjectScriptDriver);\n";
    return out.str();
}

std::vector<File> generate(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    auto fill = [&](const char* tpl) { return fillTemplate(p, tpl); };

    const bool fpp = p.gameTemplate == "fpp";
    const std::string gameCpp =
        fill(TPL_GAME_CPP_PROLOG) +
        fill(fpp ? TPL_GAME_CPP_FPP_HEAD : TPL_GAME_CPP_ORBIT_HEAD) +
        fill(TPL_GAME_CPP_SCENE) +
        fill(fpp ? TPL_GAME_CPP_FPP_TAIL : TPL_GAME_CPP_ORBIT_TAIL) +
        fill(TPL_GAME_CPP_FOOTER);

    return {
        {"Makefile", fill(TPL_MAKEFILE)},
        {"Dockerfile", fill(TPL_DOCKERFILE)},
        {"docker-compose.yml", fill(TPL_COMPOSE)},
        {"src\\main.cpp", fill(TPL_MAIN_CPP)},
        {"src\\terrain_game.cpp", gameCpp},
        {"inc\\terrain_game.hpp", fill(fpp ? TPL_GAME_HPP_FPP : TPL_GAME_HPP_ORBIT)},
        {"inc\\terrain_config.hpp", fill(TPL_TERRAIN_CONFIG_HPP)},
        {"inc\\controls.hpp", fill(TPL_CONTROLS_HPP)},
        {"inc\\scene_data.hpp", sceneDataContent(p, ns)},
        {"inc\\model_data.gen.hpp", modelDataHeader(p)},
        {"inc\\hud_data.gen.hpp", hudDataHeader(p)},
        {"inc\\terrain_heights.gen.hpp", terrainHeightsHeader(p)},
        {"inc\\texture_data.gen.hpp", textureDataHeader(p)},
        {"inc\\save_system.gen.hpp", saveSystemHeader(p)},
        {"src\\save_system.gen.cpp", saveSystemSource(p)},
        {"inc\\menu_data.gen.hpp", menuDataHeader(p)},
        {"inc\\scripts\\script.hpp", fill(TPL_SCRIPT_HPP)},
        {"src\\scripts\\flow_graph.gen.cpp", flowGraphScript(p)},
        {"src\\scripts\\object_scripts.gen.cpp", objectScriptsSource(p)},
        {"src\\scripts\\example_interaction.cpp",
         fill(fpp ? TPL_EXAMPLE_SCRIPT_FPP : TPL_EXAMPLE_SCRIPT_ORBIT)},
        {".vscode\\c_cpp_properties.json", vscodeCppProperties()},
        {"run.ps1", fill(TPL_RUN_PS1)},
        {"windows-pcsx2.ps1", fill(TPL_PCSX2_PS1)},
        {".gitignore", fill(TPL_GITIGNORE)},
        {"res\\.gitignore", TPL_DIR_KEEP},
        {"bin\\.gitignore", TPL_DIR_KEEP},
        {"obj\\.gitignore", TPL_DIR_KEEP},
    };
}

}  // namespace templates
