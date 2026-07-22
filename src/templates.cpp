#include "templates.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <set>
#include <sstream>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include "decalproj.hpp"
#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "menubake.hpp"
#include "navmesh.hpp"
#include "project.hpp"
#include "stochtile.hpp"

namespace templates {

// The texture-table key for a terrain texture: the baked supertile path when
// stochastic tiling is on (docs/terrain-painting.md), else the source as-is.
// Used identically by collectTexturePaths and the per-scene texture indices so
// they resolve to the same slot.
static std::string terrainTexKey(const std::string& srcRel, bool stochastic) {
    if (srcRel.empty()) return srcRel;
    return stochastic ? stochtile::bakedBinPath(srcRel) : srcRel;
}

// Stochastic tiling multiplies the supertile's world span by `factor`, so the
// runtime repeats-per-unit is divided by it to keep the source at its size.
static float terrainStochFactor(const Project& p, const std::string& srcRel,
                                 bool stochastic) {
    if (srcRel.empty() || !stochastic) return 1.0f;
    return (float)stochtile::factorFor(
        (std::filesystem::path(p.dir) / srcRel).string());
}

static std::vector<std::string> collectMenuEvents(const Project& p);
static bool anyNavAiNode(const Project& p);
static std::vector<int> waypointIndices(const std::vector<SceneObject>& objs,
                                        const std::string& prefix);

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

// An object that renders through the skeletal (.glb) pipeline: an animated
// Model, or a third-person Player whose avatar is its own animated model.
// The Player reuses the exact same anim path (baking, rendering, LOD,
// pose-sharing, Play Animation flow node) - the game just drives its transform
// from input and picks its clip from locomotion each frame.
static bool hasAnimBody(const SceneObject& o) {
    if (!isAnimatedModelPath(o.modelPath)) return false;
    if (o.type == PrimitiveType::Model) return true;
    if (o.type == PrimitiveType::Player) return o.playerMode == 2;
    return false;
}

// Unique .glb paths referenced by animated model objects, first-use order.
// The index == SceneObjectData::animModel == the ANIM_MODEL_PATHS slot of
// the baked .tanm. Identity is the path alone (no .mtl overrides for .glb -
// materials come from the file itself).
static std::vector<std::string> collectAnimModelPaths(const Project& p) {
    std::vector<std::string> paths;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            if (!hasAnimBody(o)) continue;
            bool seen = false;
            for (const auto& e : paths) seen |= (e == o.modelPath);
            if (!seen) paths.push_back(o.modelPath);
        }
    return paths;
}

static int animModelIndexOf(const Project& p, const SceneObject& o) {
    if (!hasAnimBody(o)) return -1;
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
// first-use order. The index == TERRAIN_TEXTURE in generated code. Each
// terrain material contributes its first material's map_Kd (if any).
static std::vector<std::string> collectTexturePaths(const Project& p) {
    std::vector<std::string> paths;
    auto add = [&](const std::string& t) {
        if (t.empty()) return;
        for (const auto& e : paths)
            if (e == t) return;
        paths.push_back(t);
    };
    for (const SceneData& sc : p.scenes) {
        const std::string baseTex =
            project::resolveTerrainMaterial(p, project::resolvedSettings(p, sc).terrainMaterial)
                .texture;
        add(terrainTexKey(baseTex, sc.terrainBaseStochastic));
        // Painted terrain layers (docs/terrain-painting.md): each layer's
        // material texture ships too - the game blends them over the base at
        // runtime (two-pass vertex-alpha splatting), full tiled resolution.
        // A stochastic layer/base ships its baked supertile instead.
        for (const TerrainLayer& l : sc.terrainLayers)
            add(terrainTexKey(project::resolveTerrainMaterial(p, l.material).texture,
                              l.stochastic));
    }
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
#include <graph.h>  // graph_get_region - the PAL-picture promotion below
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
  // reach PCSX2's emulog. The TyraX Debug window tails that file. Must
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
  // Scan mode (Project > Preferences > Build > Display mode): interlaced
  // 480i/576i (whole frames or true field rendering), progressive 480p,
  // 1080i, or the full-height PAL 576i frame (always 50 Hz). The DTV modes
  // need component cables on a real console and always run at 60 Hz.
  options.displayMode = Tyra::DisplayMode::{{DISPLAY_MODE}};
  // PAL picture (Preferences > Build > PAL picture): with the
  // region-following interlaced mode, a PAL console (or a forced-PAL
  // target system) boots the full-height 512-line 576i frame instead of
  // the letterboxed NTSC-size picture. Resolved here, before engine init,
  // so the whole boot (logo, loading screen) already runs in it; the menu
  // "DEFAULT" display option maps back to whatever this resolves to.
  if ({{PAL_FULL_HEIGHT}} &&
      options.displayMode == Tyra::DisplayMode::Interlaced &&
      (options.videoMode == Tyra::VideoMode::PAL ||
       (options.videoMode == Tyra::VideoMode::Auto &&
        graph_get_region() == GRAPH_MODE_PAL)))
    options.displayMode = Tyra::DisplayMode::Pal576i;
  // 16:9 anamorphic output (Preferences > Build > Widescreen).
  options.widescreen = {{WIDESCREEN}};
  Tyra::Engine engine(options);
  {{NAME_UPPER_NS}}::TerrainGame game(&engine);
  engine.run(&game);
  SleepThread();
  return 0;
}
)";

static const char* TPL_TERRAIN_CONFIG_HPP =
    R"(// Generated by TyraX. Do not edit - regenerated on every build.
#pragma once

namespace {{NAME_UPPER_NS}} {

// Terrain size and lighting are per scene - see scene_data.hpp arrays and
// the TERRAIN_*/SCENE_* accessor macros defined there.

// Project preferences (Project > Preferences in the editor). These are
// project-wide; sky, clipping, post-FX and the usable-highlight can be
// overridden per scene and live as SCENE_COUNT arrays in scene_data.hpp
// (reached through the accessor macros defined in scene_data.hpp).
constexpr int TERRAIN_MAX_CELLS = {{DETAIL}};

// Terrain streaming (Preferences > Terrain). The terrain mesh is built in
// TERRAIN_CHUNK_CELLS x TERRAIN_CHUNK_CELLS tiles; with a view distance > 0
// only the tiles within that range of the view focus are kept in memory
// (the rest streams in as the player moves - pair with fog to hide pop-in).
// 0 keeps the whole map resident, like before chunking existed.
constexpr int TERRAIN_CHUNK_CELLS = 16;
constexpr float TERRAIN_VIEW_DISTANCE = {{TERRAIN_VIEW_DISTANCE}};

constexpr float EYE_HEIGHT = {{EYE_HEIGHT}};
constexpr float WALK_SPEED = {{WALK_SPEED}};
constexpr float LOOK_SPEED = {{LOOK_SPEED}};    // multiplier
// Stick offsets below this fraction of full deflection read as zero
// (worn pads rest off-center); motion rescales smoothly above it.
// Per stick: left drives movement, right drives the camera.
constexpr float ANALOG_DEADZONE_L = {{DEADZONE_L}};
constexpr float ANALOG_DEADZONE_R = {{DEADZONE_R}};
// Stick response curve applied after the deadzone (Preferences > Input):
// 0 = Linear, 1 = Exponential (pow, finer near center), 2 = S-Curve.
// STICK_EXP_* tunes curves 1/2 (>=1). These seed the runtime g_stickCurve*/
// g_stickExp* globals, which the Set Stick Curve flow node (and a menu "Aim
// curve" option block) can change live.
constexpr int STICK_CURVE_L = {{STICK_CURVE_L}};
constexpr int STICK_CURVE_R = {{STICK_CURVE_R}};
constexpr float STICK_EXP_L = {{STICK_EXP_L}};
constexpr float STICK_EXP_R = {{STICK_EXP_R}};
constexpr float ORBIT_SPEED = {{ORBIT_SPEED}};  // multiplier
constexpr float GRAVITY = {{GRAVITY}};          // units/s^2
constexpr float JUMP_SPEED = {{JUMP_SPEED}};    // units/s

// Two players (Preferences > Multiplayer, docs/multiplayer.md). The mode
// while player 2 is active: 0 = off (single player only), 1 = shared screen
// (one camera frames both avatars), 2 = split screen (P1 top / P2 bottom).
// Player 2 exists in scenes with a second Player object (PLAYER2_INDEXES in
// scene_data.hpp); joins with Start on pad 2 (when enabled) or a menu
// "Player count" option block, both mid-game.
constexpr int MULTIPLAYER_MODE = {{MULTIPLAYER_MODE}};
constexpr bool P2_JOIN_ON_START = {{P2_JOIN_ON_START}};

// Scene switches show res/hud/loading.png on black for a moment
constexpr bool LOADING_SCREEN = {{LOADING_SCREEN}};

// Experimental (Preferences > Build > Disable VSync): false skips the vsync
// wait before the flip - continuous frame rate, screen tearing possible.
constexpr bool FRAME_LIMIT = {{FRAME_LIMIT}};

// Animation LOD (Preferences > Rendering): animated instances farther than
// this refresh pose/skinning every 2nd frame, every 4th beyond twice the
// distance (staggered per object). 0 = off. Playback time is unaffected.
constexpr float ANIM_LOD_DISTANCE = {{ANIM_LOD_DISTANCE}};

// Mesh LOD (Preferences > Rendering): instances farther than this render
// the ~50%-vertex variant baked into the .tskl, beyond twice the distance
// the ~25% one. 0 = off (the build then bakes no LOD chains at all).
constexpr float MESH_LOD_DISTANCE = {{MESH_LOD_DISTANCE}};

// Static batching (Preferences > Rendering): merge non-moving primitive
// objects sharing a material into combined world-space bags at scene load -
// each StaPip submit costs ~0.7-1.5 ms of fixed EE overhead on real
// hardware regardless of size, so many small separate objects dominate the
// frame (twice over in split screen). Eligibility is decided at build time
// (SceneObjectData::batchStatic); runtime edits to a batched member rebuild
// its batch. false = every object submits its own bag.
constexpr bool STATIC_BATCHING = {{STATIC_BATCHING}};

// Debug-profile HUD (Project > Preferences > Build). All forced false in a
// release-profile build, which folds the overlay + instrumentation away.
constexpr bool DEBUG_SHOW_FPS = {{DEBUG_SHOW_FPS}};
constexpr bool DEBUG_SHOW_MEM = {{DEBUG_SHOW_MEM}};
// Per-phase EE-time breakdown (scene / usable-highlight / particles / whole
// frame), averaged over ~1s. The COP0-timer reads that feed it are guarded
// by this constexpr, so a build with it false pays nothing (see drawDebugHud
// / renderScene). This is the profiling harness used to diagnose the
// usable-highlight cost, wired in as a shippable debug option.
constexpr bool DEBUG_SHOW_PROFILER = {{DEBUG_SHOW_PROFILER}};

}  // namespace {{NAME_UPPER_NS}}
)";

static const char* TPL_GAME_HPP_ORBIT =
    R"(// Generated by TyraX. Delete this line to take ownership of this file.
#pragma once

#include <tyra>
#include <map>
#include <memory>
#include <string>
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
  void resetTerrainChunks();
  void buildTerrainChunk(int slot, int cx, int cz);
  // Streams the chunk ring around one or two view foci (two-player modes:
  // P2's avatar is the second focus) - a chunk near EITHER focus stays
  // resident, so the split halves stop evicting each other's terrain.
  void updateTerrainChunks(float focusX, float focusZ, float focus2X,
                           float focus2Z, bool twoFoci, int budget);
  int countPendingChunks(float focusX, float focusZ);
  void renderTerrain();
  void updateCameraOrbit();

  Tyra::Engine* engine;
  Tyra::StaticPipeline stapip;

  Tyra::Vec4 cameraPosition, cameraLookAt;
  float orbitAngle;

  // Terrain chunks: the heightmap grid is cut into TERRAIN_CHUNK_CELLS-sized
  // square tiles, one StaPip bag each (see the chunk functions in the .cpp).
  // Slots live in a pool sized once per scene load (resetTerrainChunks) and
  // never move afterwards - each bag points into its own slot's vectors.
  struct TerrainChunk {
    std::vector<Tyra::Vec4> vertices;
    std::vector<Tyra::Color> colors;
    std::vector<Tyra::Vec4> sts;  // texture coordinates (textured terrain)
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    Tyra::StaPipTextureBag texBag;
    // Painted terrain layers: one extra alpha-blended pass over the base per
    // layer with any weight in this chunk. Shares the chunk's vertices; own
    // tiled STs + shade colors whose alpha carries the painted weight.
    struct LayerPass {
      std::vector<Tyra::Color> colors;
      std::vector<Tyra::Vec4> sts;
      std::unique_ptr<Tyra::StaPipBag> bag;
      std::unique_ptr<Tyra::StaPipColorBag> colorBag;
      Tyra::StaPipTextureBag texBag;
      int layer = -1;
    };
    std::vector<LayerPass> layerPasses;
    int cx = -1, cz = -1;  // chunk coords; -1 = free pool slot
    float aabbMin[3] = {0, 0, 0}, aabbMax[3] = {0, 0, 0};  // band culling
    // Height extent of this chunk's cells, filled at build - the portal
    // through-view's exact AABB-vs-exit-plane dead-zone test reads it.
    float minY = 0.0F, maxY = 0.0F;
  };
  std::vector<TerrainChunk> terrainChunks;  // slot pool
  std::vector<short> terrainChunkSlot;      // chunk index -> slot, -1 = unbuilt
  int terrainChunksX = 0, terrainChunksZ = 0;

  Tyra::M4x4 model;
  std::unique_ptr<Tyra::StaPipInfoBag> infoBag;  // shared by all chunk bags
  // Same render settings as infoBag but with GS blending on - shared by every
  // chunk's layer passes (the base pass must stay opaque).
  std::unique_ptr<Tyra::StaPipInfoBag> layerInfoBag;

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
    // Reflective material (refl in the .mtl): the additive sphere-map second
    // pass. World-space normals are captured at rebuild and ride in the ST
    // slot; the TCE VU1 programs compute the matcap ST from the per-mesh
    // camera basis (refreshed every frame in renderScene). The env bag shares
    // this part's vertex array and bboxVersion and mirrors the base bag's
    // shape (texture + many colors), so both passes share one frustum-bbox
    // cache entry.
    std::vector<Tyra::Vec4> envNormals;
    std::vector<Tyra::Color> envColors;  // all-white 128 = unmodulated texel
    std::unique_ptr<Tyra::StaPipBag> envBag;
    std::unique_ptr<Tyra::StaPipInfoBag> envInfoBag;
    std::unique_ptr<Tyra::StaPipColorBag> envColorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> envTexBag;
  };
  struct ObjectGeometry {
    std::vector<GeoPart> parts;
    // Physics fast path (awake bodies): parts hold LOCAL-space vertices
    // (scale baked in, shading frozen at the wake pose) and every
    // part.infoBag->model points at objMat, rebuilt from position/rotation
    // each frame - VU1 applies the motion, the EE stops re-tessellating and
    // re-shading mid-flight. Any full rebuild (rebuildObjectGeometry default
    // = world-space bake) turns it off; going to sleep forces one, so a
    // settled body gets correct rest-pose shading back.
    Tyra::M4x4 objMat;
    bool matrixMode = false;
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
      // The lit StaPip VU1 programs (_d/_td) derive the vertex color purely
      // from the directional lights - they never read a base/vertex color -
      // so an untextured mesh would render in the plain scene light color
      // (i.e. gray). This part's material albedo is folded into its own light
      // and ambient colors instead (outputColor = albedo * sceneLighting),
      // matching how the editor viewport tints the .glb. Directions stay
      // shared (animLightDirs); only the colors carry the per-part tint.
      std::unique_ptr<Tyra::PipelineDirLightsBag> animLights;
      Tyra::Vec4 litColors[4];
    };
    std::vector<AnimPart> animParts;
    std::unique_ptr<Tyra::StaPipInfoBag> animInfoBag;
    Tyra::M4x4 animMat;
    u32 animLastTick = 0;  // animLodTick of the last in-view frame; 0 = never
    // Usable-object highlight: terrain-hugging glow ring around the base,
    // built when first highlighted, cleared whenever the object rebuilds
    // (see buildHighlightApron)
    std::vector<Tyra::Vec4> apronVerts;
    std::vector<Tyra::Color> apronCols;
    u32 apronStamp = 0;
    // Low-detail stand-in the highlight shells are drawn from (positions
    // only - shells are single-color flat). Subdividing a primitive never
    // changes its silhouette, so a detail-1 box / low-segment curve gives a
    // pixel-near-identical rim for a fraction of the clip/transform cost
    // (see buildHighlightProxy). Built when first highlighted, cleared
    // whenever the object rebuilds.
    std::vector<Tyra::Vec4> hullProxyVerts;
    u32 hullProxyStamp = 0;
  };
  // Custom .obj models (paths in model_data.gen.hpp): geometry split per MTL
  // material with optional per-material textures, the real mesh AABB for box
  // collision, a CollisionMesh for mesh collision. Loaded on demand by the
  // layer streaming - only models some resident layer uses stay in memory.
  struct GameModelPart {
    std::vector<float> verts;  // 8 floats per vertex: x,y,z,nx,ny,nz,u,v
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
    // refl: spherical environment map (nullptr = not reflective).
    // reflDynamic = the "@sky" dynamic env map (engine-owned VRAM texture);
    // reflRounded = "-rounded": env normals radiate from the part centroid.
    Tyra::Texture* reflTexture = nullptr;
    float reflStrength = 0.0F;
    bool reflDynamic = false;
    bool reflRounded = false;
  };
  struct GameModel {
    std::vector<GameModelPart> parts;  // empty = missing/unparseable model
    float mn[3] = {-0.5F, -0.5F, -0.5F};
    float mx[3] = {0.5F, 0.5F, 0.5F};
    Tyra::CollisionMesh collider;  // built only when a scene needs mesh mode
    std::vector<std::string> texPaths;  // texture-cache refs this model holds
  };
  std::vector<GameModel> gameModels;
  void loadModelAsset(int index);
  void freeModelAsset(int index);
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
    std::vector<std::string> texPaths;      // texture-cache refs held
    Tyra::CoreBBox cullBox;  // local AABB over all clips + margin (see load)
  };
  std::vector<GameAnimModel> gameAnimModels;
  void loadAnimModelAsset(int index);
  void freeAnimModelAsset(int index);
  void setupAnimObject(int index);  // per-object instance + playback state
  void updateAndRenderAnimObjects();
  // Directional light for the animated pass, mirroring the baked static
  // look. The manual dir-lights layout: colors[0..2] + ambient in [3].
  Tyra::Vec4 animLightColors[4];
  Tyra::Vec4 animLightDirs[3];
  Tyra::PipelineDirLightsBag animDirLights{true};
  u32 animLodTick = 0;  // frame counter for the ANIM_LOD_DISTANCE stagger

 public:
  // Clip-name lookup for scripts/flow graph (ScriptContext::resolveClip).
  int resolveClipIndex(int objectIndex, const char* clipName) const;
  // Dynamic spawning for scripts/flow graph (ScriptContext::spawnObject /
  // despawnObject): clone an authored object into the spawn pool / free it.
  int spawnObjectAt(int templateIndex, float x, float y, float z, float yaw);
  void despawnObjectAt(int index);

 private:
  // Primitive materials: .mtl assigned to a box/sphere/... - the file's
  // first material supplies the color (kd) and optional texture.
  struct GameMaterial {
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
    std::string texPath;  // texture-cache ref held ("" = untextured)
    // refl: spherical environment map (nullptr = not reflective).
    // reflDynamic = the "@sky" dynamic env map (engine-owned VRAM texture);
    // reflRounded = "-rounded": env normals radiate from the part centroid.
    Tyra::Texture* reflTexture = nullptr;
    float reflStrength = 0.0F;
    bool reflDynamic = false;
    bool reflRounded = false;
    std::string reflTexPath;  // texture-cache ref held ("" = none)
  };
  std::vector<GameMaterial> gameMaterials;
  void loadMaterialAsset(int index);
  void freeMaterialAsset(int index);
  std::vector<Tyra::Texture*> loadedTextures;

  // Streaming layers (SCENE_LAYER_* tables): an asset is resident only while
  // some active-scene object in a resident layer (or with no layer) uses it.
  // Load Layer queues the layer's missing assets and the queue drains one
  // asset per frame (loads spread out - no long stall); Unload Layer drops
  // the layer's objects immediately and frees whatever nothing else needs.
  // Textures shared between layers/models are reference-counted by path.
  struct TexEntry {
    Tyra::Texture* tex = nullptr;
    int refs = 0;
  };
  std::map<std::string, TexEntry> texCache;
  Tyra::Texture* acquireTexture(const std::string& path);
  void releaseTexture(const std::string& path);
  void loadSceneTexture(int index);
  void freeSceneTexture(int index);
  std::vector<unsigned char> modelLoaded, materialLoaded, animModelLoaded,
      sceneTexLoaded;
  bool layerOn(int layer) const;  // desired residency of an object's layer
  void applyLayerResidency();     // free unneeded assets, queue missing ones
  void processOneStreamJob();
  void activateObject(int index);
  void deactivateObject(int index);
  void updateLayerStreaming();
  std::vector<unsigned char> layerState;   // 0 unloaded, 1 loading, 2 loaded
  std::vector<unsigned char> layerTarget;  // desired residency per layer
  std::vector<signed char> layerRequest;   // script requests (-1 = none)
  std::vector<unsigned char> layerAutoInside;  // auto zones: focus inside?
  std::vector<int> streamQueue;            // (kind << 16) | asset index
  std::vector<RuntimeObject> runtimeObjects;
  std::vector<ObjectGeometry> objectGeometry;
  // Static batching (STATIC_BATCHING, Preferences > Rendering): authored
  // objects flagged batchStatic at build time merge into combined
  // world-space bags at scene load, grouped by material + a coarse world
  // cell - one StaPip submit per batch instead of per object (the fixed
  // ~1 ms per-bag EE cost on real hardware dominates scenes made of many
  // small primitives). Members keep their runtimeObjects entry (collision,
  // raycasts and scripts read data as always) but skip the per-object draw
  // path. Runtime mutation of a member (Live Link edits, Raycast-driven
  // actions, global scripts - all set dirty) DEMOTES it to the solo path
  // and rebuilds the batch once without it; a visibility/residency flip
  // (caught by the shown snapshot - hide/show can skip the dirty flag)
  // only rebuilds the batch in place.
  struct StaticBatch {
    int material = -1;                 // group key (-1 = plain color)
    std::vector<int> members;          // authored object indices
    std::vector<unsigned char> shown;  // per member: baked as visible?
    std::vector<Tyra::Vec4> vertices;  // world-space baked, like terrain
    std::vector<Tyra::Color> colors;
    std::vector<Tyra::Vec4> sts;
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
    float aabbMin[3] = {0, 0, 0}, aabbMax[3] = {0, 0, 0};  // band culling
    bool dirty = true;
  };
  std::vector<StaticBatch> staticBatches;
  std::vector<short> objectBatchOf;  // authored index -> batch, -1 = solo
  std::unique_ptr<Tyra::StaPipInfoBag> batchInfoBag;  // shared by all batches
  void buildStaticBatchList();
  void rebuildStaticBatch(StaticBatch& b);
  void renderStaticBatches();
  GeoPart skyDome;
  // Re-centered on the camera every frame (renderScene) so a large map can
  // never let the player walk (or climb) out from under the sky. The dome
  // geometry stays static; only this translation matrix moves - one matrix
  // set per frame, so following the camera costs nothing measurable.
  Tyra::M4x4 skyMat = Tyra::M4x4::Identity;
  float skyHorizonR = 0, skyHorizonG = 0, skyHorizonB = 0;
  std::vector<Tyra::Sprite> hudSprites;

  void buildSkyDome();
  // localSpace = bake for the physics fast path (ObjectGeometry::objMat).
  void rebuildObjectGeometry(int index, bool localSpace = false);
  // A moving body takes the matrix fast path unless another consumer assumes
  // world-space vertex arrays (usable highlight hull, reflective matcap
  // normals) or it is an animated model (animMat already drives those).
  bool physFastPathEligible(int index) const;
  void updateObjMat(int index);
  // Player-vs-objects collision shared by both walkers: box (scale box or
  // model AABB), mesh (CollisionMesh) or none, per SceneObjectData.collision.
  // ceiling receives the lowest overhead surface so the walkers can keep the
  // camera from poking into geometry from below (jump clamp).
  void collidePlayer(float prevX, float prevZ, float* nextX, float* nextZ,
                     float feetY, float eyeHeight, float* ground,
                     float* ceiling);
  void updateObjectPhysics();
  // Physics bodies in a walking player's path get shoved along the attempted
  // move (impulse scaled by 1/mass) and woken; called before collidePlayer so
  // a blocked step still transfers its push into the crate.
  void pushPhysicsBodies(float prevX, float prevZ, float nextX, float nextZ,
                         float feetY, float eyeHeight);
  // Physics helpers: world-space AABB half-extents + center offset (models
  // use their mesh AABB, like collidePlayer) and the "solid enough to
  // block/bump a body" filter.
  static void physExtents(const SceneObjectData& d, const GameModel* gm,
                          const Tyra::SkelModel* anim, float* cOff, float* ext);
  static bool physObstacle(const SceneObjectData& d);
  void renderScene();
  // Mirror objects (type 15): re-submit each listed target's live bags
  // under a reflection matrix about the glass plane, then blend the quad
  // over the copies. mirrorMat holds the reflection for the mirror being
  // drawn; mirrorAnimMat composes it with an animated target's animMat.
  void renderMirrors();
  void renderMirroredObject(int index);
  Tyra::M4x4 mirrorMat;
  Tyra::M4x4 mirrorObjMat;  // reflection * objMat for fast-path bodies
  Tyra::M4x4 mirrorAnimMat;
  // Portal objects (type 16): a linked pair of surfaces. renderPortalView
  // renders the through-view of the best on-screen portal into the engine's
  // portal render target (the player camera mapped through the pair, so the
  // second camera stays in lockstep with the player's); renderPortals blends
  // every portal's tinted quad after the scene and projects the live view
  // onto the winner's surface; updatePortals teleports the player / physics
  // objects that cross a linked surface, carrying position, view angle and
  // vertical velocity through the same mapping - the view and the arrival
  // line up exactly, so stepping through is seamless. Up to four portal
  // views render per frame (nearest qualify, carved farthest-first);
  // portalLiveFlags marks the PORTALS entries whose opening is live.
  void renderPortalView();
  bool renderOnePortalView(int pi);
  void renderPortals();
  bool portalCamera(int pi, Tyra::Vec4* outEye, Tyra::Vec4* outAt);
  bool updatePortals(float prevX, float prevY, float prevZ, float* px,
                     float* py, float* pz, float* pyaw, float* ppitch,
                     float* pvelY, float eyeH);
  // True when the walker's body column at (x, z) sits inside a linked
  // FLOOR portal's rectangle near its plane - the walkers suppress the
  // terrain ground clamp there, so a portal lying on the ground swallows
  // them (the clamp would otherwise rest the feet on the terrain before
  // the crossing plane is ever reached).
  bool portalSwallowsPlayer(float x, float feetY, float z);
  bool portalSwallowZone(const RuntimeObject& m, float hx, float hy, float x,
                         float y, float z);
  // Swept variant for physics bodies: a faller at PHYS_MAX_SPEED (3 u/frame)
  // can step clean over the 2-unit approach zone in one frame - the endpoint
  // test misses, the terrain clamp kills the fall and the infinite-fall loop
  // visibly hangs on the ground before it re-swallows. Tests both frame
  // endpoints plus the segment's plane-crossing point.
  bool portalSwallowSwept(const RuntimeObject& m, float hx, float hy,
                          const Tyra::Vec4& a, const Tyra::Vec4& b);
  // Portal pass-through for the walkers: when the body column sits inside
  // a linked portal's opening near its plane, updatePortalPass publishes
  // that portal's plane and collidePlayer stops colliding with objects
  // fully BEHIND it (exact OBB extent) - the wall a portal is mounted on
  // opens up like a doorway while everything else keeps blocking.
  void updatePortalPass(float x, float feetY, float z);
  float portalPassPlane[4] = {0, 0, 0, 0};
  bool portalPassOn = false;
  // Thrown objects fly through portals too. portalCarryAim finds the
  // linked portal whose opening the motion segment a->b pierces (front
  // face, authored rectangle + slack); -1 = none. forObj gates it through
  // portalCanCross for that object; forObj == -1 is unconditional (the
  // player-released flight). portalCanCross is the owner's rule: whatever
  // a portal SHOWS can also go through it - teleportObjects, viewAll, a
  // view-list member, or the player-released body all qualify.
  // portalCarryCrossing maps position + full velocity through that pair
  // (the same isometry updatePortals applies). While a flight segment
  // aims into an opening, sweepPass* excludes obstacles fully behind that
  // portal's plane from sweepSphere, and the physics pass applies the
  // same exclusion to its static-solid resolution - the mounting wall
  // must not stop a body's center r short of the crossing plane (callers
  // pad the segment end by the body's extent for exactly that reason).
  bool portalCanCross(const PortalData& p, int oi);
  // True when portal pi's through-view actually DRAWS object oi (viewAll, or
  // oi is on its explicit view list) - a carried object may only be mapped
  // through a portal that will render it on the far side.
  bool portalShowsObject(int pi, int oi);
  // Map a world point through portal pi's pair (source local -> flip about
  // local Y -> target world), the same isometry as the teleport/camera.
  void portalMapPoint(int pi, float& x, float& y, float& z);
  int portalCarryAim(const float* a, const float* b, int forObj);
  bool portalCarryCrossing(const float* a, float* pos, float* vel);
  // Arms sweepPass* when segment a->b pierces a linked opening (pad the
  // end by the swept body's extent). Pair with sweepPassOn = false after
  // the sweep.
  bool armSweepPass(const float* a, const float* b);
  float sweepPassPlane[4] = {0, 0, 0, 0};
  bool sweepPassOn = false;
  // The last player-released rigid body (throw OR drop): portal-free -
  // crosses any linked portal, flag or not - until it settles to sleep.
  // -1 = none. The non-physics thrown arc (thrownIndex) is always free.
  int thrownFreeIndex = -1;
  std::vector<unsigned char> portalLiveFlags;  // per PORTALS entry: view drawn
  std::vector<float> portalPrevPos;  // 3 floats per runtime object (crossings)
  // Per object: frames until it may hop again (set on every hop). Damps
  // frame-scale re-hop jitter (rect-edge bounces, resolution kicks) without
  // touching legit loops - the example's fall re-crosses every ~13 frames.
  std::vector<unsigned char> portalHopCool;
  // Exit-plane of the through-view being rendered (nx, ny, nz, d) - set
  // around the destination render so renderTerrain can drop chunks in the
  // dead zone between the virtual camera and the target portal's plane.
  float portalExitPlane[4] = {0, 0, 0, 0};
  bool portalExitPlaneOn = false;
  void renderHighlightHull(int index);
  void buildHighlightApron(int index, float half);
  void buildHighlightProxy(int index);
  bool highlightInReach(int index) const;
  // Usable-object highlight: one shared bag re-submitted for every part of
  // every shell. The grow about the object center and the pushback about
  // the eye compose into a single scale+translation model matrix (hullMat),
  // so VU1 does all per-vertex work on the object's own vertex arrays.
  // Shell colors need persistent storage - the single-color pointer is
  // DMA-referenced at submit time, not copied.
  Tyra::M4x4 hullMat;
  std::vector<Tyra::Color> hullShellCols;
  std::unique_ptr<Tyra::StaPipBag> hullBag;
  std::unique_ptr<Tyra::StaPipInfoBag> hullInfoBag;
  std::unique_ptr<Tyra::StaPipColorBag> hullColorBag;
  std::unique_ptr<Tyra::StaPipBag> apronBag;
  std::unique_ptr<Tyra::StaPipInfoBag> apronInfoBag;
  std::unique_ptr<Tyra::StaPipColorBag> apronColorBag;

  // Player entities (PLAYER_INDEXES / PLAYER2_INDEXES in scene_data.hpp);
  // override the template camera when present. players[0] is the scene's
  // first Player object (P1), players[1] the second (P2 - only meaningful
  // with MULTIPLAYER_MODE != 0, see docs/multiplayer.md).
  struct PlayerCtl {
    int objIndex = -1;  // scene object index, -1 = this player doesn't exist
    float x = 0, y = 0, z = 0, velY = 0, yaw = 0, pitch = 0;
    // Third-person only: yaw/pitch orbit the camera, faceYaw is the avatar's
    // own facing (turns toward the walk direction). Clip indices are resolved
    // from the model's clip table at scene load; -1 = unmapped.
    float faceYaw = 0;
    int idleClip = -1, walkClip = -1, runClip = -1, jumpClip = -1;
    // Smoothed spring-arm boom length - snaps in on a hit, eases back out.
    float boom = 0;
    // This player's own view; the dispatcher (or the split-screen render
    // pass) picks which of these drives the frame camera.
    Tyra::Vec4 camPos, camLook;
  };
  PlayerCtl players[2];
  // Dispatcher: walks P1 (and P2 while active), then composes the frame
  // camera. Returns false when the scene has no player.
  bool updatePlayerEntity();
  // Walks one player from its pad and writes its camera into camPos/camLook.
  void updatePlayerWalker(PlayerCtl& P, int pi, Tyra::Pad& pad);
  // Shared-screen camera: orbits (P1's right stick) around the midpoint of
  // both players, boom stretched by their separation. Writes cameraPosition.
  void updateSharedCamera();
  float sharedBoom = 0;
  // Player 2 join/leave, both mid-game: Start on pad 2 (P2_JOIN_ON_START) or
  // a menu Toggle bound to "Player count". Shows/hides the P2 avatar.
  void setPlayerTwoActive(bool active);
  void syncPlayerCountMenuValue();
  bool playerTwoActive = false;
  Tyra::Pad pad2;              // connector 2; optional, hot-join friendly
  int menuPlayerCountPrev = -2;  // edge detect for the menu bind
  // True while renderScene runs inside a split-screen half: the dynamic
  // env-map bracket restores a FULL-screen raster on end(), which would
  // break the active half, so the env pass pauses during split rendering
  // (the VRAM target keeps its last content).
  bool splitPassActive = false;
  // True during the SECOND split half's renderScene: animation playback and
  // skinning already ran this frame (first half), so the animated pass
  // must not advance time again (2x playback speed) nor re-skin the same
  // pose (PC-grade avatars cost real EE ms) - it re-submits the
  // frame's skinned buffers under the second camera instead.
  bool splitSecondPass = false;
  // Split-band culling: the split raster shows only the CENTRAL half of the
  // full-height projection, but the frustum planes the engine classifies
  // against stay full-height - so each half would transform ~2x the geometry
  // it can show. Two extra planes bound the visible vertical band; chunks and
  // static objects entirely outside skip submission before the engine ever
  // sees them. Recomputed per half from the live camera + projection FOV.
  void computeSplitBand();
  bool outsideSplitBand(const float mn[3], const float mx[3]) const;
  bool objectOutsideSplitBand(int i) const;
  bool splitBandActive = false;
  float splitBandN[2][3];  // inward top/bottom plane normals (apex = camera)
  float splitBandP[3];     // the apex
  // Picks the third-person avatar's locomotion clip from its planar speed
  // (fraction of full walk speed) and grounded state, cross-fading on change.
  void drivePlayerAnim(PlayerCtl& P, RuntimeObject& body, float speedFrac,
                       bool grounded);
  // Spring arm: the distance down the boom (from the head, along d) at which
  // the camera would enter geometry or the terrain. camBoom is the smoothed
  // boom length actually used - whisker casts ease it in ahead of a hit, a
  // hard clamp keeps it out of geometry, and it eases back out.
  float springArm(float px, float py, float pz, float dx, float dy, float dz,
                  float maxDist) const;
  // The general sweep behind springArm: first blocked distance along d for a
  // sphere of `radius`, vs object AABBs + the terrain; skipIndex is excluded
  // (the swept object itself). Also carries/throws pickable objects.
  float sweepSphere(float px, float py, float pz, float dx, float dy, float dz,
                    float maxDist, float radius, int skipIndex) const;

  // Multiple scenes: the game starts in scene 0; the flow graph Switch
  // Scene node requests a change applied between frames.
  void loadScene(int sceneIndex);
  // Loads scene 0 + runs scripts' init(); called once from the loop's boot
  // sequence (see bootPhase) so the initial load is vsync-paced.
  void bootFirstScene();
  // Boot state: 0 = boot splash images, 1 = loading-screen hold for the first
  // scene, 2 = gameplay running (the Tyra logo hold lives in the engine's
  // banner). splashIndex/splashFrames step through the splash sequence.
  int bootPhase = 0;
  int splashIndex = 0;
  int splashFrames = 0;
  int currentScene = 0;
  unsigned int sceneGeneration = 0;

  // Particle emitters (type 7): fixed pools sized at scene load, zero
  // per-frame allocations. The EE only SIMULATES; each particle is
  // submitted as a single center vertex plus one qword of 2x2 basis
  // weights and one color, and the VU1 billboard program expands it into
  // a camera-facing quad (textured when the emitter has a material with a
  // map_Kd). One bag per emitter; the camera basis rides on the billboard
  // bag, so another view (a portal pass) can re-render the same centers
  // after swapping billboardBag->right/up.
  struct ParticleSystem {
    int objectIndex = -1;
    unsigned int rng = 1;
    std::vector<Tyra::Vec4> pos, vel;
    std::vector<float> life, maxLife;
    std::vector<Tyra::Vec4> params;  // per-particle (m00, m01, m10, m11)
    std::vector<Tyra::Color> cols;   // one RGBA per particle
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
    std::unique_ptr<Tyra::StaPipBillboardBag> billboardBag;
  };
  std::vector<ParticleSystem> particles;
  void buildParticles();
  void updateParticles();

  // Sound emitters (type 8): distance-attenuated one-shots on channels 16-23
  std::vector<audsrv_adpcm_t*> sndSamples;  // scene_data.hpp SND_PATHS order
  std::vector<int> sndTimers;               // per-object retrigger countdown
  void updateSoundEmitters();

  // Scene switch target held across the loading-screen frames (the screen
  // itself is drawn by loadingscreen::renderFrame from loading_data.gen.hpp).
  int loadingFrames = 0, loadingTarget = -1;
  // Snapshot of the armed hold length: everyFrames() tracks the measured
  // frame time now, so re-evaluating it in a == comparison against the
  // counter could miss its frame at uncapped FPS.
  int loadingTotal = 0;

  // "Use" interaction: nearest usable object the camera looks at (controls.hpp)
  void updateUseTarget();
  int useTargetIndex = -1;
  // Pickable objects: at most one carried + one in flight. The carried object
  // rides a fixed reach in front of the camera, swept against the world
  // (sweepSphere) so it cannot be pushed through or parked behind geometry.
  void updateCarriedObject();
  // Hands a dropped/thrown object back to the physics sim (waking it), or
  // returns false when it has no rigid body to hand off to.
  bool releaseCarried(RuntimeObject& o, float vx, float vy, float vz);
  float objectHalfExtent(const RuntimeObject& o) const;
  // Blocks the walker from pressing against geometry the carried object no
  // longer fits in front of (the spring arm's sweep, pushing the walker back
  // instead of pulling the camera in).
  void applyCarryWhisker(float* nextX, float* nextZ, float probeY, float yaw,
                         float feetY, float eyeHeight);
  int carryIndex = -1;        // runtimeObjects index being carried, -1 = none
  // The portal the carried object is currently passing THROUGH (its carry ray
  // pierces the opening and that portal renders the object in its
  // through-view), or -1. The object is drawn NORMALLY in the main pass at
  // its real position - the portal's z-cap clips the part inside the opening
  // that is past the surface, and a wall around the opening occludes the
  // rest - while that portal's through-view draws a copy mapped to the far
  // side, so the portion "through" the opening appears coming out the other
  // end. Two-sided, like a real portal; recomputed each carry frame.
  int carryPortalPi = -1;
  bool carryGrabbed = false;  // eats the BTN_USE press that picked it up
  float carryDist = 0;        // smoothed carry reach: snaps in when the sweep
                              // blocks, eases back out (the boom's policy)
  int thrownIndex = -1;       // launched object still in flight, -1 = none
  float thrownVel[3] = {0, 0, 0};  // units/frame
  Tyra::Sprite usePromptSprite;
  Tyra::Sprite pickPromptSprite;  // "PICK UP" variant, same placement

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
  // Ready-made option-block rows (Menu Editor): map each bound Toggle/Choice
  // row's option index onto its engine setting (volume/deadzone/curve/display).
  void applyMenuBindings();
  std::vector<Tyra::Sprite> menuSprites;
  // Toggle/Choice entry values: one sub-rect sprite per menu into its baked
  // value strip (menu_data.gen.hpp; only menus with such entries have one).
  std::vector<Tyra::Sprite> menuValueSprites;

  // On-screen texts (hud_data.gen.hpp): baked text sprites the Set Text
  // Visible flow node flips via ScriptContext; a positive timer auto-hides.
  void updateAndRenderHudTexts();
  std::vector<Tyra::Sprite> hudTextSprites;
  std::vector<signed char> hudTextReq;   // ScriptContext::textRequest
  std::vector<float> hudTextDur;         // ScriptContext::textDuration
  std::vector<unsigned char> hudTextOn;  // visible this frame
  std::vector<float> hudTextTimer;       // seconds left (0 = until hidden)

  // Runtime texts (font_data.gen.hpp): one slot per Display Text node, drawn
  // glyph by glyph from a font atlas because the string is only known now.
  void updateAndRenderDynTexts();
  std::vector<signed char> dynTextReq;   // ScriptContext::dynTextRequest
  std::vector<float> dynTextDur;         // ScriptContext::dynTextDuration
  std::vector<char> dynTextBuf;          // DYN_TEXT_COUNT * DYN_TEXT_LEN
  std::vector<unsigned char> dynTextOn;  // visible this frame
  std::vector<float> dynTextTimer;
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
    R"(// Generated by TyraX. Delete this line to take ownership of this file.
#pragma once

#include <tyra>
#include <map>
#include <memory>
#include <string>
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
  void resetTerrainChunks();
  void buildTerrainChunk(int slot, int cx, int cz);
  // Streams the chunk ring around one or two view foci (two-player modes:
  // P2's avatar is the second focus) - a chunk near EITHER focus stays
  // resident, so the split halves stop evicting each other's terrain.
  void updateTerrainChunks(float focusX, float focusZ, float focus2X,
                           float focus2Z, bool twoFoci, int budget);
  int countPendingChunks(float focusX, float focusZ);
  void renderTerrain();
  void updatePlayer();

  Tyra::Engine* engine;
  Tyra::StaticPipeline stapip;

  Tyra::Vec4 cameraPosition, cameraLookAt;
  float playerX, playerZ, yaw, pitch;
  float playerY, playerVelY;  // feet height + vertical velocity (physics)

  // Terrain chunks: the heightmap grid is cut into TERRAIN_CHUNK_CELLS-sized
  // square tiles, one StaPip bag each (see the chunk functions in the .cpp).
  // Slots live in a pool sized once per scene load (resetTerrainChunks) and
  // never move afterwards - each bag points into its own slot's vectors.
  struct TerrainChunk {
    std::vector<Tyra::Vec4> vertices;
    std::vector<Tyra::Color> colors;
    std::vector<Tyra::Vec4> sts;  // texture coordinates (textured terrain)
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    Tyra::StaPipTextureBag texBag;
    // Painted terrain layers: one extra alpha-blended pass over the base per
    // layer with any weight in this chunk. Shares the chunk's vertices; own
    // tiled STs + shade colors whose alpha carries the painted weight.
    struct LayerPass {
      std::vector<Tyra::Color> colors;
      std::vector<Tyra::Vec4> sts;
      std::unique_ptr<Tyra::StaPipBag> bag;
      std::unique_ptr<Tyra::StaPipColorBag> colorBag;
      Tyra::StaPipTextureBag texBag;
      int layer = -1;
    };
    std::vector<LayerPass> layerPasses;
    int cx = -1, cz = -1;  // chunk coords; -1 = free pool slot
    float aabbMin[3] = {0, 0, 0}, aabbMax[3] = {0, 0, 0};  // band culling
    // Height extent of this chunk's cells, filled at build - the portal
    // through-view's exact AABB-vs-exit-plane dead-zone test reads it.
    float minY = 0.0F, maxY = 0.0F;
  };
  std::vector<TerrainChunk> terrainChunks;  // slot pool
  std::vector<short> terrainChunkSlot;      // chunk index -> slot, -1 = unbuilt
  int terrainChunksX = 0, terrainChunksZ = 0;

  Tyra::M4x4 model;
  std::unique_ptr<Tyra::StaPipInfoBag> infoBag;  // shared by all chunk bags
  // Same render settings as infoBag but with GS blending on - shared by every
  // chunk's layer passes (the base pass must stay opaque).
  std::unique_ptr<Tyra::StaPipInfoBag> layerInfoBag;

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
    // Reflective material (refl in the .mtl): the additive sphere-map second
    // pass. World-space normals are captured at rebuild and ride in the ST
    // slot; the TCE VU1 programs compute the matcap ST from the per-mesh
    // camera basis (refreshed every frame in renderScene). The env bag shares
    // this part's vertex array and bboxVersion and mirrors the base bag's
    // shape (texture + many colors), so both passes share one frustum-bbox
    // cache entry.
    std::vector<Tyra::Vec4> envNormals;
    std::vector<Tyra::Color> envColors;  // all-white 128 = unmodulated texel
    std::unique_ptr<Tyra::StaPipBag> envBag;
    std::unique_ptr<Tyra::StaPipInfoBag> envInfoBag;
    std::unique_ptr<Tyra::StaPipColorBag> envColorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> envTexBag;
  };
  struct ObjectGeometry {
    std::vector<GeoPart> parts;
    // Physics fast path (awake bodies): parts hold LOCAL-space vertices
    // (scale baked in, shading frozen at the wake pose) and every
    // part.infoBag->model points at objMat, rebuilt from position/rotation
    // each frame - VU1 applies the motion, the EE stops re-tessellating and
    // re-shading mid-flight. Any full rebuild (rebuildObjectGeometry default
    // = world-space bake) turns it off; going to sleep forces one, so a
    // settled body gets correct rest-pose shading back.
    Tyra::M4x4 objMat;
    bool matrixMode = false;
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
      // The lit StaPip VU1 programs (_d/_td) derive the vertex color purely
      // from the directional lights - they never read a base/vertex color -
      // so an untextured mesh would render in the plain scene light color
      // (i.e. gray). This part's material albedo is folded into its own light
      // and ambient colors instead (outputColor = albedo * sceneLighting),
      // matching how the editor viewport tints the .glb. Directions stay
      // shared (animLightDirs); only the colors carry the per-part tint.
      std::unique_ptr<Tyra::PipelineDirLightsBag> animLights;
      Tyra::Vec4 litColors[4];
    };
    std::vector<AnimPart> animParts;
    std::unique_ptr<Tyra::StaPipInfoBag> animInfoBag;
    Tyra::M4x4 animMat;
    u32 animLastTick = 0;  // animLodTick of the last in-view frame; 0 = never
    // Usable-object highlight: terrain-hugging glow ring around the base,
    // built when first highlighted, cleared whenever the object rebuilds
    // (see buildHighlightApron)
    std::vector<Tyra::Vec4> apronVerts;
    std::vector<Tyra::Color> apronCols;
    u32 apronStamp = 0;
    // Low-detail stand-in the highlight shells are drawn from (positions
    // only - shells are single-color flat). Subdividing a primitive never
    // changes its silhouette, so a detail-1 box / low-segment curve gives a
    // pixel-near-identical rim for a fraction of the clip/transform cost
    // (see buildHighlightProxy). Built when first highlighted, cleared
    // whenever the object rebuilds.
    std::vector<Tyra::Vec4> hullProxyVerts;
    u32 hullProxyStamp = 0;
  };
  // Custom .obj models (paths in model_data.gen.hpp): geometry split per MTL
  // material with optional per-material textures, the real mesh AABB for box
  // collision, a CollisionMesh for mesh collision. Loaded on demand by the
  // layer streaming - only models some resident layer uses stay in memory.
  struct GameModelPart {
    std::vector<float> verts;  // 8 floats per vertex: x,y,z,nx,ny,nz,u,v
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
    // refl: spherical environment map (nullptr = not reflective).
    // reflDynamic = the "@sky" dynamic env map (engine-owned VRAM texture);
    // reflRounded = "-rounded": env normals radiate from the part centroid.
    Tyra::Texture* reflTexture = nullptr;
    float reflStrength = 0.0F;
    bool reflDynamic = false;
    bool reflRounded = false;
  };
  struct GameModel {
    std::vector<GameModelPart> parts;  // empty = missing/unparseable model
    float mn[3] = {-0.5F, -0.5F, -0.5F};
    float mx[3] = {0.5F, 0.5F, 0.5F};
    Tyra::CollisionMesh collider;  // built only when a scene needs mesh mode
    std::vector<std::string> texPaths;  // texture-cache refs this model holds
  };
  std::vector<GameModel> gameModels;
  void loadModelAsset(int index);
  void freeModelAsset(int index);
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
    std::vector<std::string> texPaths;      // texture-cache refs held
    Tyra::CoreBBox cullBox;  // local AABB over all clips + margin (see load)
  };
  std::vector<GameAnimModel> gameAnimModels;
  void loadAnimModelAsset(int index);
  void freeAnimModelAsset(int index);
  void setupAnimObject(int index);  // per-object instance + playback state
  void updateAndRenderAnimObjects();
  // Directional light for the animated pass, mirroring the baked static
  // look. The manual dir-lights layout: colors[0..2] + ambient in [3].
  Tyra::Vec4 animLightColors[4];
  Tyra::Vec4 animLightDirs[3];
  Tyra::PipelineDirLightsBag animDirLights{true};
  u32 animLodTick = 0;  // frame counter for the ANIM_LOD_DISTANCE stagger

 public:
  // Clip-name lookup for scripts/flow graph (ScriptContext::resolveClip).
  int resolveClipIndex(int objectIndex, const char* clipName) const;
  // Dynamic spawning for scripts/flow graph (ScriptContext::spawnObject /
  // despawnObject): clone an authored object into the spawn pool / free it.
  int spawnObjectAt(int templateIndex, float x, float y, float z, float yaw);
  void despawnObjectAt(int index);

 private:
  // Primitive materials: .mtl assigned to a box/sphere/... - the file's
  // first material supplies the color (kd) and optional texture.
  struct GameMaterial {
    Tyra::Texture* texture = nullptr;
    float kd[3] = {1.0F, 1.0F, 1.0F};
    std::string texPath;  // texture-cache ref held ("" = untextured)
    // refl: spherical environment map (nullptr = not reflective).
    // reflDynamic = the "@sky" dynamic env map (engine-owned VRAM texture);
    // reflRounded = "-rounded": env normals radiate from the part centroid.
    Tyra::Texture* reflTexture = nullptr;
    float reflStrength = 0.0F;
    bool reflDynamic = false;
    bool reflRounded = false;
    std::string reflTexPath;  // texture-cache ref held ("" = none)
  };
  std::vector<GameMaterial> gameMaterials;
  void loadMaterialAsset(int index);
  void freeMaterialAsset(int index);
  std::vector<Tyra::Texture*> loadedTextures;

  // Streaming layers (SCENE_LAYER_* tables): an asset is resident only while
  // some active-scene object in a resident layer (or with no layer) uses it.
  // Load Layer queues the layer's missing assets and the queue drains one
  // asset per frame (loads spread out - no long stall); Unload Layer drops
  // the layer's objects immediately and frees whatever nothing else needs.
  // Textures shared between layers/models are reference-counted by path.
  struct TexEntry {
    Tyra::Texture* tex = nullptr;
    int refs = 0;
  };
  std::map<std::string, TexEntry> texCache;
  Tyra::Texture* acquireTexture(const std::string& path);
  void releaseTexture(const std::string& path);
  void loadSceneTexture(int index);
  void freeSceneTexture(int index);
  std::vector<unsigned char> modelLoaded, materialLoaded, animModelLoaded,
      sceneTexLoaded;
  bool layerOn(int layer) const;  // desired residency of an object's layer
  void applyLayerResidency();     // free unneeded assets, queue missing ones
  void processOneStreamJob();
  void activateObject(int index);
  void deactivateObject(int index);
  void updateLayerStreaming();
  std::vector<unsigned char> layerState;   // 0 unloaded, 1 loading, 2 loaded
  std::vector<unsigned char> layerTarget;  // desired residency per layer
  std::vector<signed char> layerRequest;   // script requests (-1 = none)
  std::vector<unsigned char> layerAutoInside;  // auto zones: focus inside?
  std::vector<int> streamQueue;            // (kind << 16) | asset index
  std::vector<RuntimeObject> runtimeObjects;
  std::vector<ObjectGeometry> objectGeometry;
  // Static batching (STATIC_BATCHING, Preferences > Rendering): authored
  // objects flagged batchStatic at build time merge into combined
  // world-space bags at scene load, grouped by material + a coarse world
  // cell - one StaPip submit per batch instead of per object (the fixed
  // ~1 ms per-bag EE cost on real hardware dominates scenes made of many
  // small primitives). Members keep their runtimeObjects entry (collision,
  // raycasts and scripts read data as always) but skip the per-object draw
  // path. Runtime mutation of a member (Live Link edits, Raycast-driven
  // actions, global scripts - all set dirty) DEMOTES it to the solo path
  // and rebuilds the batch once without it; a visibility/residency flip
  // (caught by the shown snapshot - hide/show can skip the dirty flag)
  // only rebuilds the batch in place.
  struct StaticBatch {
    int material = -1;                 // group key (-1 = plain color)
    std::vector<int> members;          // authored object indices
    std::vector<unsigned char> shown;  // per member: baked as visible?
    std::vector<Tyra::Vec4> vertices;  // world-space baked, like terrain
    std::vector<Tyra::Color> colors;
    std::vector<Tyra::Vec4> sts;
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
    float aabbMin[3] = {0, 0, 0}, aabbMax[3] = {0, 0, 0};  // band culling
    bool dirty = true;
  };
  std::vector<StaticBatch> staticBatches;
  std::vector<short> objectBatchOf;  // authored index -> batch, -1 = solo
  std::unique_ptr<Tyra::StaPipInfoBag> batchInfoBag;  // shared by all batches
  void buildStaticBatchList();
  void rebuildStaticBatch(StaticBatch& b);
  void renderStaticBatches();
  GeoPart skyDome;
  // Re-centered on the camera every frame (renderScene) so a large map can
  // never let the player walk (or climb) out from under the sky. The dome
  // geometry stays static; only this translation matrix moves - one matrix
  // set per frame, so following the camera costs nothing measurable.
  Tyra::M4x4 skyMat = Tyra::M4x4::Identity;
  float skyHorizonR = 0, skyHorizonG = 0, skyHorizonB = 0;
  std::vector<Tyra::Sprite> hudSprites;

  void buildSkyDome();
  // localSpace = bake for the physics fast path (ObjectGeometry::objMat).
  void rebuildObjectGeometry(int index, bool localSpace = false);
  // A moving body takes the matrix fast path unless another consumer assumes
  // world-space vertex arrays (usable highlight hull, reflective matcap
  // normals) or it is an animated model (animMat already drives those).
  bool physFastPathEligible(int index) const;
  void updateObjMat(int index);
  // Player-vs-objects collision shared by both walkers: box (scale box or
  // model AABB), mesh (CollisionMesh) or none, per SceneObjectData.collision.
  // ceiling receives the lowest overhead surface so the walkers can keep the
  // camera from poking into geometry from below (jump clamp).
  void collidePlayer(float prevX, float prevZ, float* nextX, float* nextZ,
                     float feetY, float eyeHeight, float* ground,
                     float* ceiling);
  void updateObjectPhysics();
  // Physics bodies in a walking player's path get shoved along the attempted
  // move (impulse scaled by 1/mass) and woken; called before collidePlayer so
  // a blocked step still transfers its push into the crate.
  void pushPhysicsBodies(float prevX, float prevZ, float nextX, float nextZ,
                         float feetY, float eyeHeight);
  // Physics helpers: world-space AABB half-extents + center offset (models
  // use their mesh AABB, like collidePlayer) and the "solid enough to
  // block/bump a body" filter.
  static void physExtents(const SceneObjectData& d, const GameModel* gm,
                          const Tyra::SkelModel* anim, float* cOff, float* ext);
  static bool physObstacle(const SceneObjectData& d);
  void renderScene();
  // Mirror objects (type 15): re-submit each listed target's live bags
  // under a reflection matrix about the glass plane, then blend the quad
  // over the copies. mirrorMat holds the reflection for the mirror being
  // drawn; mirrorAnimMat composes it with an animated target's animMat.
  void renderMirrors();
  void renderMirroredObject(int index);
  Tyra::M4x4 mirrorMat;
  Tyra::M4x4 mirrorObjMat;  // reflection * objMat for fast-path bodies
  Tyra::M4x4 mirrorAnimMat;
  // Portal objects (type 16): a linked pair of surfaces. renderPortalView
  // renders the through-view of the best on-screen portal into the engine's
  // portal render target (the player camera mapped through the pair, so the
  // second camera stays in lockstep with the player's); renderPortals blends
  // every portal's tinted quad after the scene and projects the live view
  // onto the winner's surface; updatePortals teleports the player / physics
  // objects that cross a linked surface, carrying position, view angle and
  // vertical velocity through the same mapping - the view and the arrival
  // line up exactly, so stepping through is seamless. Up to four portal
  // views render per frame (nearest qualify, carved farthest-first);
  // portalLiveFlags marks the PORTALS entries whose opening is live.
  void renderPortalView();
  bool renderOnePortalView(int pi);
  void renderPortals();
  bool portalCamera(int pi, Tyra::Vec4* outEye, Tyra::Vec4* outAt);
  bool updatePortals(float prevX, float prevY, float prevZ, float* px,
                     float* py, float* pz, float* pyaw, float* ppitch,
                     float* pvelY, float eyeH);
  // True when the walker's body column at (x, z) sits inside a linked
  // FLOOR portal's rectangle near its plane - the walkers suppress the
  // terrain ground clamp there, so a portal lying on the ground swallows
  // them (the clamp would otherwise rest the feet on the terrain before
  // the crossing plane is ever reached).
  bool portalSwallowsPlayer(float x, float feetY, float z);
  bool portalSwallowZone(const RuntimeObject& m, float hx, float hy, float x,
                         float y, float z);
  // Swept variant for physics bodies: a faller at PHYS_MAX_SPEED (3 u/frame)
  // can step clean over the 2-unit approach zone in one frame - the endpoint
  // test misses, the terrain clamp kills the fall and the infinite-fall loop
  // visibly hangs on the ground before it re-swallows. Tests both frame
  // endpoints plus the segment's plane-crossing point.
  bool portalSwallowSwept(const RuntimeObject& m, float hx, float hy,
                          const Tyra::Vec4& a, const Tyra::Vec4& b);
  // Portal pass-through for the walkers: when the body column sits inside
  // a linked portal's opening near its plane, updatePortalPass publishes
  // that portal's plane and collidePlayer stops colliding with objects
  // fully BEHIND it (exact OBB extent) - the wall a portal is mounted on
  // opens up like a doorway while everything else keeps blocking.
  void updatePortalPass(float x, float feetY, float z);
  float portalPassPlane[4] = {0, 0, 0, 0};
  bool portalPassOn = false;
  // Thrown objects fly through portals too. portalCarryAim finds the
  // linked portal whose opening the motion segment a->b pierces (front
  // face, authored rectangle + slack); -1 = none. forObj gates it through
  // portalCanCross for that object; forObj == -1 is unconditional (the
  // player-released flight). portalCanCross is the owner's rule: whatever
  // a portal SHOWS can also go through it - teleportObjects, viewAll, a
  // view-list member, or the player-released body all qualify.
  // portalCarryCrossing maps position + full velocity through that pair
  // (the same isometry updatePortals applies). While a flight segment
  // aims into an opening, sweepPass* excludes obstacles fully behind that
  // portal's plane from sweepSphere, and the physics pass applies the
  // same exclusion to its static-solid resolution - the mounting wall
  // must not stop a body's center r short of the crossing plane (callers
  // pad the segment end by the body's extent for exactly that reason).
  bool portalCanCross(const PortalData& p, int oi);
  // True when portal pi's through-view actually DRAWS object oi (viewAll, or
  // oi is on its explicit view list) - a carried object may only be mapped
  // through a portal that will render it on the far side.
  bool portalShowsObject(int pi, int oi);
  // Map a world point through portal pi's pair (source local -> flip about
  // local Y -> target world), the same isometry as the teleport/camera.
  void portalMapPoint(int pi, float& x, float& y, float& z);
  int portalCarryAim(const float* a, const float* b, int forObj);
  bool portalCarryCrossing(const float* a, float* pos, float* vel);
  // Arms sweepPass* when segment a->b pierces a linked opening (pad the
  // end by the swept body's extent). Pair with sweepPassOn = false after
  // the sweep.
  bool armSweepPass(const float* a, const float* b);
  float sweepPassPlane[4] = {0, 0, 0, 0};
  bool sweepPassOn = false;
  // The last player-released rigid body (throw OR drop): portal-free -
  // crosses any linked portal, flag or not - until it settles to sleep.
  // -1 = none. The non-physics thrown arc (thrownIndex) is always free.
  int thrownFreeIndex = -1;
  std::vector<unsigned char> portalLiveFlags;  // per PORTALS entry: view drawn
  std::vector<float> portalPrevPos;  // 3 floats per runtime object (crossings)
  // Per object: frames until it may hop again (set on every hop). Damps
  // frame-scale re-hop jitter (rect-edge bounces, resolution kicks) without
  // touching legit loops - the example's fall re-crosses every ~13 frames.
  std::vector<unsigned char> portalHopCool;
  // Exit-plane of the through-view being rendered (nx, ny, nz, d) - set
  // around the destination render so renderTerrain can drop chunks in the
  // dead zone between the virtual camera and the target portal's plane.
  float portalExitPlane[4] = {0, 0, 0, 0};
  bool portalExitPlaneOn = false;
  void renderHighlightHull(int index);
  void buildHighlightApron(int index, float half);
  void buildHighlightProxy(int index);
  bool highlightInReach(int index) const;
  // Usable-object highlight: one shared bag re-submitted for every part of
  // every shell. The grow about the object center and the pushback about
  // the eye compose into a single scale+translation model matrix (hullMat),
  // so VU1 does all per-vertex work on the object's own vertex arrays.
  // Shell colors need persistent storage - the single-color pointer is
  // DMA-referenced at submit time, not copied.
  Tyra::M4x4 hullMat;
  std::vector<Tyra::Color> hullShellCols;
  std::unique_ptr<Tyra::StaPipBag> hullBag;
  std::unique_ptr<Tyra::StaPipInfoBag> hullInfoBag;
  std::unique_ptr<Tyra::StaPipColorBag> hullColorBag;
  std::unique_ptr<Tyra::StaPipBag> apronBag;
  std::unique_ptr<Tyra::StaPipInfoBag> apronInfoBag;
  std::unique_ptr<Tyra::StaPipColorBag> apronColorBag;

  // Player entities (PLAYER_INDEXES / PLAYER2_INDEXES in scene_data.hpp);
  // override the template camera when present. players[0] is the scene's
  // first Player object (P1), players[1] the second (P2 - only meaningful
  // with MULTIPLAYER_MODE != 0, see docs/multiplayer.md).
  struct PlayerCtl {
    int objIndex = -1;  // scene object index, -1 = this player doesn't exist
    float x = 0, y = 0, z = 0, velY = 0, yaw = 0, pitch = 0;
    // Third-person only: yaw/pitch orbit the camera, faceYaw is the avatar's
    // own facing (turns toward the walk direction). Clip indices are resolved
    // from the model's clip table at scene load; -1 = unmapped.
    float faceYaw = 0;
    int idleClip = -1, walkClip = -1, runClip = -1, jumpClip = -1;
    // Smoothed spring-arm boom length - snaps in on a hit, eases back out.
    float boom = 0;
    // This player's own view; the dispatcher (or the split-screen render
    // pass) picks which of these drives the frame camera.
    Tyra::Vec4 camPos, camLook;
  };
  PlayerCtl players[2];
  // Dispatcher: walks P1 (and P2 while active), then composes the frame
  // camera. Returns false when the scene has no player.
  bool updatePlayerEntity();
  // Walks one player from its pad and writes its camera into camPos/camLook.
  void updatePlayerWalker(PlayerCtl& P, int pi, Tyra::Pad& pad);
  // Shared-screen camera: orbits (P1's right stick) around the midpoint of
  // both players, boom stretched by their separation. Writes cameraPosition.
  void updateSharedCamera();
  float sharedBoom = 0;
  // Player 2 join/leave, both mid-game: Start on pad 2 (P2_JOIN_ON_START) or
  // a menu Toggle bound to "Player count". Shows/hides the P2 avatar.
  void setPlayerTwoActive(bool active);
  void syncPlayerCountMenuValue();
  bool playerTwoActive = false;
  Tyra::Pad pad2;              // connector 2; optional, hot-join friendly
  int menuPlayerCountPrev = -2;  // edge detect for the menu bind
  // True while renderScene runs inside a split-screen half: the dynamic
  // env-map bracket restores a FULL-screen raster on end(), which would
  // break the active half, so the env pass pauses during split rendering
  // (the VRAM target keeps its last content).
  bool splitPassActive = false;
  // True during the SECOND split half's renderScene: animation playback and
  // skinning already ran this frame (first half), so the animated pass
  // must not advance time again (2x playback speed) nor re-skin the same
  // pose (PC-grade avatars cost real EE ms) - it re-submits the
  // frame's skinned buffers under the second camera instead.
  bool splitSecondPass = false;
  // Split-band culling: the split raster shows only the CENTRAL half of the
  // full-height projection, but the frustum planes the engine classifies
  // against stay full-height - so each half would transform ~2x the geometry
  // it can show. Two extra planes bound the visible vertical band; chunks and
  // static objects entirely outside skip submission before the engine ever
  // sees them. Recomputed per half from the live camera + projection FOV.
  void computeSplitBand();
  bool outsideSplitBand(const float mn[3], const float mx[3]) const;
  bool objectOutsideSplitBand(int i) const;
  bool splitBandActive = false;
  float splitBandN[2][3];  // inward top/bottom plane normals (apex = camera)
  float splitBandP[3];     // the apex
  // Picks the third-person avatar's locomotion clip from its planar speed
  // (fraction of full walk speed) and grounded state, cross-fading on change.
  void drivePlayerAnim(PlayerCtl& P, RuntimeObject& body, float speedFrac,
                       bool grounded);
  // Spring arm: the distance down the boom (from the head, along d) at which
  // the camera would enter geometry or the terrain. camBoom is the smoothed
  // boom length actually used - whisker casts ease it in ahead of a hit, a
  // hard clamp keeps it out of geometry, and it eases back out.
  float springArm(float px, float py, float pz, float dx, float dy, float dz,
                  float maxDist) const;
  // The general sweep behind springArm: first blocked distance along d for a
  // sphere of `radius`, vs object AABBs + the terrain; skipIndex is excluded
  // (the swept object itself). Also carries/throws pickable objects.
  float sweepSphere(float px, float py, float pz, float dx, float dy, float dz,
                    float maxDist, float radius, int skipIndex) const;

  // Multiple scenes: the game starts in scene 0; the flow graph Switch
  // Scene node requests a change applied between frames.
  void loadScene(int sceneIndex);
  // Loads scene 0 + runs scripts' init(); called once from the loop's boot
  // sequence (see bootPhase) so the initial load is vsync-paced.
  void bootFirstScene();
  // Boot state: 0 = boot splash images, 1 = loading-screen hold for the first
  // scene, 2 = gameplay running (the Tyra logo hold lives in the engine's
  // banner). splashIndex/splashFrames step through the splash sequence.
  int bootPhase = 0;
  int splashIndex = 0;
  int splashFrames = 0;
  int currentScene = 0;
  unsigned int sceneGeneration = 0;

  // Particle emitters (type 7): fixed pools sized at scene load, zero
  // per-frame allocations. The EE only SIMULATES; each particle is
  // submitted as a single center vertex plus one qword of 2x2 basis
  // weights and one color, and the VU1 billboard program expands it into
  // a camera-facing quad (textured when the emitter has a material with a
  // map_Kd). One bag per emitter; the camera basis rides on the billboard
  // bag, so another view (a portal pass) can re-render the same centers
  // after swapping billboardBag->right/up.
  struct ParticleSystem {
    int objectIndex = -1;
    unsigned int rng = 1;
    std::vector<Tyra::Vec4> pos, vel;
    std::vector<float> life, maxLife;
    std::vector<Tyra::Vec4> params;  // per-particle (m00, m01, m10, m11)
    std::vector<Tyra::Color> cols;   // one RGBA per particle
    std::unique_ptr<Tyra::StaPipBag> bag;
    std::unique_ptr<Tyra::StaPipInfoBag> infoBag;
    std::unique_ptr<Tyra::StaPipColorBag> colorBag;
    std::unique_ptr<Tyra::StaPipTextureBag> texBag;
    std::unique_ptr<Tyra::StaPipBillboardBag> billboardBag;
  };
  std::vector<ParticleSystem> particles;
  void buildParticles();
  void updateParticles();

  // Sound emitters (type 8): distance-attenuated one-shots on channels 16-23
  std::vector<audsrv_adpcm_t*> sndSamples;  // scene_data.hpp SND_PATHS order
  std::vector<int> sndTimers;               // per-object retrigger countdown
  void updateSoundEmitters();

  // Scene switch target held across the loading-screen frames (the screen
  // itself is drawn by loadingscreen::renderFrame from loading_data.gen.hpp).
  int loadingFrames = 0, loadingTarget = -1;
  // Snapshot of the armed hold length: everyFrames() tracks the measured
  // frame time now, so re-evaluating it in a == comparison against the
  // counter could miss its frame at uncapped FPS.
  int loadingTotal = 0;

  // "Use" interaction: nearest usable object the camera looks at (controls.hpp)
  void updateUseTarget();
  int useTargetIndex = -1;
  // Pickable objects: at most one carried + one in flight. The carried object
  // rides a fixed reach in front of the camera, swept against the world
  // (sweepSphere) so it cannot be pushed through or parked behind geometry.
  void updateCarriedObject();
  // Hands a dropped/thrown object back to the physics sim (waking it), or
  // returns false when it has no rigid body to hand off to.
  bool releaseCarried(RuntimeObject& o, float vx, float vy, float vz);
  float objectHalfExtent(const RuntimeObject& o) const;
  // Blocks the walker from pressing against geometry the carried object no
  // longer fits in front of (the spring arm's sweep, pushing the walker back
  // instead of pulling the camera in).
  void applyCarryWhisker(float* nextX, float* nextZ, float probeY, float yaw,
                         float feetY, float eyeHeight);
  int carryIndex = -1;        // runtimeObjects index being carried, -1 = none
  // The portal the carried object is currently passing THROUGH (its carry ray
  // pierces the opening and that portal renders the object in its
  // through-view), or -1. The object is drawn NORMALLY in the main pass at
  // its real position - the portal's z-cap clips the part inside the opening
  // that is past the surface, and a wall around the opening occludes the
  // rest - while that portal's through-view draws a copy mapped to the far
  // side, so the portion "through" the opening appears coming out the other
  // end. Two-sided, like a real portal; recomputed each carry frame.
  int carryPortalPi = -1;
  bool carryGrabbed = false;  // eats the BTN_USE press that picked it up
  float carryDist = 0;        // smoothed carry reach: snaps in when the sweep
                              // blocks, eases back out (the boom's policy)
  int thrownIndex = -1;       // launched object still in flight, -1 = none
  float thrownVel[3] = {0, 0, 0};  // units/frame
  Tyra::Sprite usePromptSprite;
  Tyra::Sprite pickPromptSprite;  // "PICK UP" variant, same placement

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
  // Ready-made option-block rows (Menu Editor): map each bound Toggle/Choice
  // row's option index onto its engine setting (volume/deadzone/curve/display).
  void applyMenuBindings();
  std::vector<Tyra::Sprite> menuSprites;
  // Toggle/Choice entry values: one sub-rect sprite per menu into its baked
  // value strip (menu_data.gen.hpp; only menus with such entries have one).
  std::vector<Tyra::Sprite> menuValueSprites;

  // On-screen texts (hud_data.gen.hpp): baked text sprites the Set Text
  // Visible flow node flips via ScriptContext; a positive timer auto-hides.
  void updateAndRenderHudTexts();
  std::vector<Tyra::Sprite> hudTextSprites;
  std::vector<signed char> hudTextReq;   // ScriptContext::textRequest
  std::vector<float> hudTextDur;         // ScriptContext::textDuration
  std::vector<unsigned char> hudTextOn;  // visible this frame
  std::vector<float> hudTextTimer;       // seconds left (0 = until hidden)

  // Runtime texts (font_data.gen.hpp): one slot per Display Text node, drawn
  // glyph by glyph from a font atlas because the string is only known now.
  void updateAndRenderDynTexts();
  std::vector<signed char> dynTextReq;   // ScriptContext::dynTextRequest
  std::vector<float> dynTextDur;         // ScriptContext::dynTextDuration
  std::vector<char> dynTextBuf;          // DYN_TEXT_COUNT * DYN_TEXT_LEN
  std::vector<unsigned char> dynTextOn;  // visible this frame
  std::vector<float> dynTextTimer;
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
    R"(// Generated by TyraX. Delete this line to take ownership of this file.
#include "terrain_game.hpp"
#include "terrain_config.hpp"
#include "controls.hpp"
// Fallbacks for user-owned controls.hpp files written before these knobs
// existed - the game must still build when that header never regenerates.
#ifndef BTN_THROW
#define BTN_THROW Circle
#endif
#ifndef PICK_CARRY_DIST
#define PICK_CARRY_DIST 2.2F
#endif
#ifndef PICK_THROW_SPEED
#define PICK_THROW_SPEED 14.0F
#endif
#ifndef PICK_MIN_DIST
#define PICK_MIN_DIST 0.3F
#endif
#include "scene_data.hpp"
#include "model_data.gen.hpp"
#include "hud_data.gen.hpp"
#include "font_data.gen.hpp"
#include "loading_data.gen.hpp"
#include "menu_data.gen.hpp"
#include "terrain_heights.gen.hpp"
#include "texture_data.gen.hpp"
#include "decal_data.gen.hpp"  // baked projected-decal meshes (host-computed)
#include "scripts/sequences.gen.hpp"  // cutscene bars/fade overlay
#include "scripts/screen_fx.gen.hpp"  // custom full-screen effects
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
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

// True while a pausing menu owns the frame (set at the top of loop()). Read by
// the effect systems that would otherwise keep running under a pause -
// particle simulation and skeletal-animation playback freeze on their last
// frame instead of advancing behind the menu.
bool g_gameplayPaused = false;

// Camera flashlight runtime state (a Player object property; declared in
// scene_data.hpp). g_flashEnabled is the master switch - seeded per scene from
// the player's Enabled flag in loadScene, flipped by the Set Flashlight flow
// node. g_flashOn is the on/off state the optional toggle button drives; the
// beam only shows while BOTH are set, so the toggle respects Enabled.
bool g_flashEnabled = false;
bool g_flashOn = true;
// Global emitter draw switch (Set Particles flow node). false = updateParticles
// skips all simulation + drawing, so every emitter's fill cost disappears.
bool g_particlesOn = true;

// Runtime analog stick deadzone (Preferences > Input; a menu "Deadzone" option
// block changes it live via applyMenuBindings). Seeded from the baked
// ANALOG_DEADZONE_* constants in buildScene (they are namespaced, this scope is
// not), then read every frame by stickAxis() - so with no option block the
// sticks behave exactly as the Preferences deadzone.
float g_deadzoneL = 0.2F;
float g_deadzoneR = 0.2F;

// Analog stick response curves (Preferences > Input; the Set Stick Curve flow
// node and a menu "Aim curve" option block change them live). g_stickCurve*:
// 0 = Linear, 1 = Exponential, 2 = S-Curve; g_stickExp* tunes curves 1/2.
// Seeded from the baked STICK_* constants in init() (the constants are
// namespaced, this scope is not), then read every frame by stickAxis();
// runtime changes persist across scenes.
int g_stickCurveL = 0;
int g_stickCurveR = 0;
float g_stickExpL = 2.0F;
float g_stickExpR = 2.0F;

// Pad vibration auto-stop: > 0 counts down (real seconds, g_frameDt) to a
// setActuators(false, 0), armed by a Vibrate Pad request with Seconds > 0.
// Global runtime state - a running countdown survives a scene switch.
float g_rumbleTimer = 0.0F;

// Last option index a "Display mode" / "Widescreen" menu block applied. Video
// switches rebuild VRAM + arm the confirm prompt, so they fire only on change
// (seeded from the saved option in buildScene so a persisted choice does not
// re-trigger a switch at boot).
int g_menuDispOpt = -1;
int g_menuWideOpt = -1;

// The project-default display mode: whatever main.cpp resolved the boot
// mode to (fixed preference, or region + the PAL-picture choice). Captured
// in buildScene before any runtime switch can happen; a display row's
// "DEFAULT" option (optModes -1) maps to it.
int g_defaultDispMode = 0;

// Maps a pad axis byte (128 = center) to a signed -1..1 value: it reads 0
// below the deadzone, rescales from the deadzone edge so there is no step,
// then shapes the magnitude by the per-stick response curve. Both the FPP
// player and the Player-entity update call this (keep the two in sync).
float stickAxis(const u8& raw, float dz, int curve, float e) {
  const float v = (raw - 128.0F) / 128.0F;
  const float mag = v < 0.0F ? -v : v;
  if (mag <= dz) return 0.0F;
  float s = (mag - dz) / (1.0F - dz);  // 0 at the edge .. 1 at full deflection
  if (curve == 1) {
    s = powf(s, e);  // Exponential: gentle near center, snappy at the edge
  } else if (curve == 2) {
    // S-Curve: smoothstep (soft center + firm cap), sharpened by the exponent.
    const float smooth = s * s * (3.0F - 2.0F * s);
    s = e == 1.0F ? smooth : powf(smooth, e);
  }
  return v < 0.0F ? -s : s;
}

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

// Display-mode option rows (bind 5): the engine mode an option drives, and
// the option a mode shows as. Rows without an explicit optModes table keep
// the positional mapping (option index == Tyra::DisplayMode); a table entry
// of -1 is the "DEFAULT" option - the project-default boot mode.
int displayOptionMode(const MenuEntryData& en, int idx) {
  if (en.optModes && idx >= 0 && idx < en.optionCount) {
    const int m = en.optModes[idx];
    return m < 0 ? g_defaultDispMode : m;
  }
  return idx;
}
int displayOptionIndexOf(const MenuEntryData& en, int mode) {
  for (int i = 0; i < en.optionCount; ++i)
    if (displayOptionMode(en, i) == mode) return i;
  return -1;
}

/** The texture repository asserts (crashes) on a missing file - probe first
 * so a missing PNG degrades to an untextured draw with a warning. */
bool assetFileExists(const std::string& cwdRel) {
  FILE* f = fopen(FileUtils::fromCwd(cwdRel).c_str(), "rb");
  if (f) fclose(f);
  return f != nullptr;
}

/** Per-object rendering cut-off (Properties > Draw distance) measured from
 * the object center - the cheapest LOD. Draw-time only: collision, sounds
 * and scripts elsewhere never consult this. 0 = unlimited. */
bool beyondDrawDistance(const SceneObjectData& d, const Vec4& cam) {
  if (d.drawDistance <= 0.0F) return false;
  const float dx = d.position[0] - cam.x;
  const float dy = d.position[1] - cam.y;
  const float dz = d.position[2] - cam.z;
  return dx * dx + dy * dy + dz * dz > d.drawDistance * d.drawDistance;
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

/** Point lights (SceneObject type 9) of the active scene, collected once per
 * scene load. pointLightAt runs PER VERTEX while baking terrain chunks and
 * object meshes; scanning the whole SCENE_OBJECTS table there thrashes the
 * EE's 16 KB dcache on big scenes - an 1100-object scene cost ~170 ms per
 * 16x16 terrain chunk (a visible hitch on every chunk-border crossing).
 * Lights are static authored data, so the tiny list is exact. */
struct BakedPointLight {
  V3 pos;
  float color[3];
  float radius, bright;
};
std::vector<BakedPointLight> g_scenePointLights;
void collectScenePointLights() {
  g_scenePointLights.clear();
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    const SceneObjectData& L = SCENE_OBJECTS[i];
    if (L.type != 9) continue;
    BakedPointLight b;
    b.pos = {L.position[0], L.position[1], L.position[2]};
    b.color[0] = L.color[0];
    b.color[1] = L.color[1];
    b.color[2] = L.color[2];
    b.radius = L.lightRadius > 0.01F ? L.lightRadius : 0.01F;
    b.bright = L.lightBright;
    g_scenePointLights.push_back(b);
  }
}

/** Point lights baked additively on top of the directional term. Linear
 * distance falloff * N.L, tinted by the light color and scaled by its
 * brightness. wp = world-space vertex position. */
V3 pointLightAt(const V3& wp, const V3& n) {
  V3 add = {0.0F, 0.0F, 0.0F};
  for (const BakedPointLight& L : g_scenePointLights) {
    V3 d = {L.pos.x - wp.x, L.pos.y - wp.y, L.pos.z - wp.z};
    const float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
    if (dist >= L.radius) continue;
    float atten = 1.0F - dist / L.radius;
    atten *= atten;  // softer, rounder pool of light
    float ndotl = 1.0F;
    if (dist > 0.0001F) {
      ndotl = (n.x * d.x + n.y * d.y + n.z * d.z) / dist;
      if (ndotl < 0.0F) ndotl = 0.0F;
    }
    const float k = L.bright * atten * ndotl;
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
// Physics fast path: push LOCAL-space positions (scale baked, rotation and
// translation left to ObjectGeometry::objMat). Shading still bakes from the
// full wake pose, so the switch itself never pops a color.
bool g_bakeLocal = false;

// Reflective materials: while non-null, pushVert also captures the rotated
// (world-space) normal of every emitted vertex - the per-frame sphere-map ST
// computation needs them (see renderScene). Staged per part like g_primKd.
std::vector<Vec4>* g_envNormals = nullptr;

// Loaded materials/model parts using the "@sky" dynamic env map. While > 0,
// renderScene renders the sky dome into the engine's env-map target each
// frame (the GT3 trick - reflections follow the live sky).
int g_dynamicEnvUsers = 0;

// Every rebuilt vertex buffer gets a process-unique bbox version. The
// engine's frustum-bbox cache is keyed by (vertex pointer, version); layer
// streaming frees and reallocates buffers, so with per-bag counters a
// recycled heap address could arrive with the dead buffer's exact version
// and inherit its cached boxes - packages misclassify (objects smear or
// vanish) or the package index runs past the cached part count (the
// stapip_bag_packages_bbox assert). One monotonic stamp shared by every
// bag makes each (pointer, version) pair unique for the whole run.
u32 g_bboxStamp = 0;

// Clip-name resolution for scripts/flow graph: ScriptContext carries a plain
// function pointer (script.hpp must stay engine-agnostic), so the game
// instance is reached through this file-static (set in buildScene).
TerrainGame* g_animGame = nullptr;
int animResolveClipThunk(int objectIndex, const char* clipName) {
  return g_animGame ? g_animGame->resolveClipIndex(objectIndex, clipName) : -1;
}

// Dynamic spawn pool size: at most this many live clones (Spawn Object flow
// node) on top of the authored scene objects. Slots are recycled on despawn.
constexpr int MAX_SPAWNED_OBJECTS = 32;
int spawnObjectThunk(int templateIndex, float x, float y, float z, float yaw) {
  return g_animGame ? g_animGame->spawnObjectAt(templateIndex, x, y, z, yaw)
                    : -1;
}
void despawnObjectThunk(int objectIndex) {
  if (g_animGame) g_animGame->despawnObjectAt(objectIndex);
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
  const V3 lp = p;  // local (scaled) position - what g_bakeLocal pushes
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
  verts.push_back(g_bakeLocal ? Vec4(lp.x, lp.y, lp.z, 1.0F)
                              : Vec4(wp.x, wp.y, wp.z, 1.0F));
  // In textured mode the color modulates the texture (128 = 1.0). Kd may
  // exceed 1 (material brightness) - cap at the GS's 255 so untextured
  // colors cannot wrap.
  const float scale = textured ? 128.0F : 255.0F;
  auto c255 = [](float v) { return v > 255.0F ? 255.0F : v; };
  cols.push_back(Color(c255(o.color[0] * scale * shade.x),
                       c255(o.color[1] * scale * shade.y),
                       c255(o.color[2] * scale * shade.z), 128.0F));
  sts.push_back(Vec4(u, v, 1.0F, 0.0F));
  if (g_envNormals) g_envNormals->push_back(Vec4(n.x, n.y, n.z, 0.0F));
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
  // Detail = subdivisions per edge (1 = plain 6-quad box); each face is an
  // n x n grid, UVs span 0..1 per face. Mirror of the editor's unitBox.
  const int n = o.primDetail < 1 ? 1 : (o.primDetail > 16 ? 16 : o.primDetail);
  const float h = 0.5F, H = 1.0F;
  auto face = [&](V3 c0, V3 du, V3 dv, V3 nrm) {
    for (int i = 0; i < n; ++i)
      for (int j = 0; j < n; ++j) {
        const float s0 = (float)i / n, s1 = (float)(i + 1) / n;
        const float t0 = (float)j / n, t1 = (float)(j + 1) / n;
        auto P = [&](float s, float t) -> V3 {
          return {c0.x + du.x * s + dv.x * t, c0.y + du.y * s + dv.y * t,
                  c0.z + du.z * s + dv.z * t};
        };
        pushVert(verts, cols, sts, o, P(s0, t0), nrm, s0, t0);
        pushVert(verts, cols, sts, o, P(s1, t0), nrm, s1, t0);
        pushVert(verts, cols, sts, o, P(s1, t1), nrm, s1, t1);
        pushVert(verts, cols, sts, o, P(s0, t0), nrm, s0, t0);
        pushVert(verts, cols, sts, o, P(s1, t1), nrm, s1, t1);
        pushVert(verts, cols, sts, o, P(s0, t1), nrm, s0, t1);
      }
  };
  face({h, -h, -h}, {0, H, 0}, {0, 0, H}, {1, 0, 0});    // +X
  face({-h, -h, h}, {0, H, 0}, {0, 0, -H}, {-1, 0, 0});  // -X
  face({-h, h, -h}, {0, 0, H}, {H, 0, 0}, {0, 1, 0});    // +Y
  face({-h, -h, h}, {0, 0, -H}, {H, 0, 0}, {0, -1, 0});  // -Y
  face({-h, -h, h}, {H, 0, 0}, {0, H, 0}, {0, 0, 1});    // +Z
  face({h, -h, -h}, {-H, 0, 0}, {0, H, 0}, {0, 0, -1});  // -Z
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

// Flat unit square in the XZ plane, double-sided (visible from both faces).
void addPlane(std::vector<Vec4>& verts, std::vector<Color>& cols,
              std::vector<Vec4>& sts, const SceneObjectData& o) {
  const float h = 0.5F;
  pushQuad(verts, cols, sts, o, {-h, 0, -h}, {-h, 0, h}, {h, 0, h}, {h, 0, -h}, {0, 1, 0});
  pushQuad(verts, cols, sts, o, {-h, 0, h}, {-h, 0, -h}, {h, 0, -h}, {h, 0, h}, {0, -1, 0});
}

// Decal: a flat unit quad in the XY plane facing +Z, textured via the assigned
// material (transparency comes from the texture's alpha - the static pipeline
// runs an alpha test that drops fully-transparent texels and alpha-blends the
// rest). Nudged +Z by DECAL_OFFSET so it sits just in front of the surface it
// is placed on instead of z-fighting it. Single-sided (front only). U runs with
// local -X (slide-projector convention) so the texture reads correctly - not
// mirrored - viewed from the +Z front; matches unitDecal + the projected decal.
void addDecal(std::vector<Vec4>& verts, std::vector<Color>& cols,
              std::vector<Vec4>& sts, const SceneObjectData& o) {
  const float h = 0.5F, z = 0.02F;  // DECAL_OFFSET, local units (scaled by Z)
  const V3 n = {0, 0, 1};
  pushVert(verts, cols, sts, o, {-h, -h, z}, n, 1, 0);
  pushVert(verts, cols, sts, o, {h, -h, z}, n, 0, 0);
  pushVert(verts, cols, sts, o, {h, h, z}, n, 0, 1);
  pushVert(verts, cols, sts, o, {-h, -h, z}, n, 1, 0);
  pushVert(verts, cols, sts, o, {h, h, z}, n, 0, 1);
  pushVert(verts, cols, sts, o, {-h, h, z}, n, 1, 1);
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

/** 8x8 glyph text from the res/hud/debugfont.png strip (digits, letters and
 * a few symbols - the atlas below must match the editor's debugFontPng()).
 * Shared by the debug-profile overlays and the video-mode confirm prompt,
 * so the strip ships with every build. */
void drawHudText(Engine* engine, const char* s, float x, float y) {
  static Sprite glyph;
  static bool glyphReady = false;
  if (!glyphReady) {
    glyph.mode = SpriteMode::MODE_REPEAT;
    glyph.size = Vec2(8.0F, 8.0F);  // one atlas cell
    glyph.scale = 2.0F;
    auto* texture = engine->renderer.getTextureRepository().add(
        FileUtils::fromCwd("hud/debugfont.png"));
    texture->addLink(glyph.id);
    glyphReady = true;
  }
  // 32 cells of 16px per atlas row (the glyph sits in the left 8px, the
  // right 8px stay transparent so bilinear sampling never bleeds the next
  // glyph), rows 8px apart.
  static const char* atlas = "0123456789.FPSMBE/ACDGHIJKLNOQRTUVWXYZ?=-:";
  for (; *s; ++s, x += 14.0F) {
    if (*s == ' ') continue;
    const char* hit = strchr(atlas, *s);
    if (!hit) continue;
    const int idx = (int)(hit - atlas);
    glyph.offset = Vec2((float)(idx % 32) * 16.0F, (float)(idx / 32) * 8.0F);
    glyph.position = Vec2(x, y);
    engine->renderer.renderer2D.render(glyph);
  }
}

float hudTextWidth(const char* s) { return (float)strlen(s) * 14.0F; }

/** Runtime text drawn from a Font Manager glyph atlas (Display Text nodes).
 *
 * VRAM: the atlas Texture is only handed to the repository the first time this
 * font actually draws, and the engine only DMAs a texture to GS VRAM on its
 * first render - so a font nobody displays costs zero VRAM, and one that is
 * hidden again keeps costing only its EE-side copy. We deliberately never call
 * useTexture() eagerly here (unlike the streamed model textures), because that
 * would pin the sheet before anything asked for it. */
float fontTextWidth(int fontIdx, const char* s, float size) {
  const FontData& f = FONTS[fontIdx];
  if (!f.glyphs) return 0.0F;
  const float k = size / (float)f.baseSize;
  float w = 0.0F;
  for (; *s; ++s) {
    const int gi = (int)(unsigned char)*s - FONT_FIRST_CHAR;
    if (gi < 0 || gi >= FONT_CHAR_COUNT) continue;
    w += (float)f.glyphs[gi].adv * k;
  }
  return w;
}

void drawFontText(Engine* engine, int fontIdx, const char* s, float cx,
                  float cy, float size) {
  const FontData& f = FONTS[fontIdx];
  if (!f.glyphs || !f.atlas[0]) return;

  // One sprite per font, kept across frames: the texture link is by sprite id,
  // so every glyph of a font reuses the same id and the same atlas binding.
  static Sprite glyph[FONT_COUNT > 0 ? FONT_COUNT : 1];
  static bool ready[FONT_COUNT > 0 ? FONT_COUNT : 1] = {};
  if (!ready[fontIdx]) {
    glyph[fontIdx].mode = SpriteMode::MODE_REPEAT;  // sample a sub-rect
    auto* texture = engine->renderer.getTextureRepository().add(
        FileUtils::fromCwd(f.atlas));
    texture->addLink(glyph[fontIdx].id);
    ready[fontIdx] = true;
  }

  Sprite& sp = glyph[fontIdx];
  const float k = size / (float)f.baseSize;
  sp.scale = k;

  // Center anchor, like the baked HUD text sprites.
  const float startX = cx - fontTextWidth(fontIdx, s, size) * 0.5F;
  const float top = cy - (float)f.lineH * k * 0.5F;

  // Shadow first, then the glyphs: two passes over the same cells, the dark
  // one offset by a pixel (the baked texts get theirs at bake time instead).
  for (int pass = f.shadow ? 0 : 1; pass < 2; ++pass) {
    const float ox = pass == 0 ? 1.0F : 0.0F;
    if (pass == 0)
      sp.color = Color(10.0F, 12.0F, 16.0F, 100.0F);
    else
      sp.color = Color((float)f.r, (float)f.g, (float)f.b, 128.0F);

    float pen = startX;
    for (const char* c = s; *c; ++c) {
      const int gi = (int)(unsigned char)*c - FONT_FIRST_CHAR;
      if (gi < 0 || gi >= FONT_CHAR_COUNT) continue;
      const FontGlyph& g = f.glyphs[gi];
      if (g.w > 0 && g.h > 0) {
        sp.size = Vec2((float)g.w, (float)g.h);
        sp.offset = Vec2((float)g.u, (float)g.v);
        sp.position = Vec2(pen + (float)g.xoff * k + ox,
                           top + (float)g.yoff * k + ox);
        engine->renderer.renderer2D.render(sp);
      }
      pen += (float)g.adv * k;
    }
  }
}

// Debug frame profiler (Project > Preferences > Build > Show frame profiler).
// renderScene brackets its three heavy phases with EE COP0-timer reads and
// adds the elapsed ticks here; drawDebugHud averages them over ~1s and prints
// avg ms. The reads live behind `if (DEBUG_SHOW_PROFILER)` (a constexpr), so a
// build with the profiler off contains none of this. This is the same
// COP0/HUD phase-timing harness used to diagnose the usable-highlight cost,
// wired in as a shippable debug option. (COP0 Count runs at 294.912 MHz =
// half the 590 MHz EE clock; /294912 converts ticks to milliseconds.)
u32 g_profScene = 0, g_profHighlight = 0, g_profParticles = 0;
static inline u32 profTicks() {
  u32 v;
  asm volatile("mfc0 %0, $9" : "=r"(v));
  return v;
}

/** Debug-profile HUD (Project > Preferences > Build): FPS, RAM
 * (used/total EE MB) and the per-phase EE-time profiler in the top-left
 * corner. Compiles to nothing in a release build (the DEBUG_SHOW_* constants
 * in terrain_config.hpp fold the calls away). */
void drawDebugHud(Engine* engine) {
  if (!DEBUG_SHOW_FPS && !DEBUG_SHOW_MEM && !DEBUG_SHOW_PROFILER) return;
  static int memRefresh = 0;
  static float memFreeMB = 0.0F;
  char line[40];
  float y = 16.0F;
  if (DEBUG_SHOW_FPS) {
    snprintf(line, sizeof(line), "FPS %d", (int)engine->info.getFps());
    drawHudText(engine, line, 16.0F, y);
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
    drawHudText(engine, line, 16.0F, y);
    y += 20.0F;
  }
  if (DEBUG_SHOW_PROFILER) {
    // Whole-frame time = wall clock between successive HUD calls (this runs
    // once per frame); the phase sums come from renderScene. Averaged over a
    // ~1s window so the numbers hold still enough to read.
    static u32 lastTick = 0, frames = 0, frameAcc = 0;
    static u32 sScene = 0, sHl = 0, sPart = 0;
    static char l1[40] = "PROFILER...", l2[40] = "";
    const u32 now = profTicks();
    if (lastTick) frameAcc += now - lastTick;
    lastTick = now;
    sScene += g_profScene;
    sHl += g_profHighlight;
    sPart += g_profParticles;
    g_profScene = g_profHighlight = g_profParticles = 0;
    if (++frames >= 50) {
      const float k = 1.0F / (294912.0F * (float)frames);  // ticks -> avg ms
      snprintf(l1, sizeof(l1), "FRAME %.2f SCENE %.2f", frameAcc * k,
               sScene * k);
      snprintf(l2, sizeof(l2), "HL %.2f PART %.2f", sHl * k, sPart * k);
      frames = frameAcc = sScene = sHl = sPart = 0;
    }
    drawHudText(engine, l1, 16.0F, y);
    y += 20.0F;
    drawHudText(engine, l2, 16.0F, y);
  }
}

// Runtime scan-mode switch with keep-or-revert confirmation (the Set
// Display Mode flow node). A mode the player's TV can't display would
// strand them on a black screen, so with a confirm window armed the game
// automatically reverts to the previous mode unless X is pressed in time -
// the same safety net PC display settings use.
struct VideoConfirm {
  bool active = false;
  Tyra::DisplayMode prevMode = Tyra::DisplayMode::Interlaced;
  float secondsLeft = 0.0F;
};
VideoConfirm g_videoConfirm;

/** Applies the Set Display Mode / Set Widescreen flow-node requests and
 * ticks the confirm countdown. Must run between frames (before
 * beginFrame): a scan-mode switch rebuilds the VRAM layout. Returns true
 * the frame a scan-mode switch happens - the caller closes any open game
 * menu then, so the player judges the new picture unobstructed and the
 * confirm prompt's X press is not also a menu select. */
bool applyVideoRequests(Engine* engine, ScriptContext& ctx) {
  auto& core = engine->renderer.core;
  bool switched = false;
  if (ctx.widescreen >= 0) {
    core.setDisplayOutput(core.getSettings().getDisplayMode(),
                          ctx.widescreen != 0);
    ctx.widescreen = -1;
  }
  if (ctx.requestDisplayMode >= 0) {
    const auto mode = (Tyra::DisplayMode)ctx.requestDisplayMode;
    const auto prev = core.getSettings().getDisplayMode();
    if (mode != prev) {
      core.setDisplayOutput(mode, core.getSettings().getWidescreen());
      // The vertical refresh may change (PAL 50 <-> DTV 60) - reseed the
      // frame clock so gameplay speed stays wall-clock normalized.
      g_frameRate = engine->renderer.core.getSettings().getRefreshRate();
      switched = true;
      if (ctx.displayConfirmSec > 0.0F) {
        g_videoConfirm.active = true;
        g_videoConfirm.prevMode = prev;
        g_videoConfirm.secondsLeft = ctx.displayConfirmSec;
      } else {
        g_videoConfirm.active = false;  // blind switch - no prompt armed
      }
    }
    ctx.requestDisplayMode = -1;
    ctx.displayConfirmSec = 0.0F;
  }
  if (!g_videoConfirm.active) return switched;
  if (!switched && engine->pad.getClicked().Cross) {
    g_videoConfirm.active = false;  // player kept the new mode
    return switched;
  }
  g_videoConfirm.secondsLeft -= g_frameDt;
  if (g_videoConfirm.secondsLeft <= 0.0F) {
    core.setDisplayOutput(g_videoConfirm.prevMode,
                          core.getSettings().getWidescreen());
    g_frameRate = engine->renderer.core.getSettings().getRefreshRate();
    g_videoConfirm.active = false;
  }
  return switched;
}

/** The keep-or-revert prompt, drawn in the 2D phase (with the other HUD). */
void drawVideoConfirm(Engine* engine) {
  if (!g_videoConfirm.active) return;
  const float w = engine->renderer.core.getSettings().getWidth();
  const float h = engine->renderer.core.getSettings().getHeight();
  const char* ask = "KEEP VIDEO MODE? X = YES";
  drawHudText(engine, ask, (w - hudTextWidth(ask)) * 0.5F, h * 0.5F - 22.0F);
  char line[24];
  snprintf(line, sizeof(line), "BACK IN %d",
           (int)g_videoConfirm.secondsLeft + 1);
  drawHudText(engine, line, (w - hudTextWidth(line)) * 0.5F, h * 0.5F + 2.0F);
}

// --- Loading screens (loading_data.gen.hpp) --------------------------------
// Presents one frame of the loading screen a scene resolves to (background +
// images + baked texts + progress bars) at load progress `fraction` (0..1).
// Called from loadScene while it drains assets/terrain AND from the loop's
// scene-switch hold; the whole point of pumping frames mid-load is that the
// bar advances for real. Sprites and texture links are built once, lazily
// (loadScene(0) at boot can run before init() finishes its own sprite setup),
// then reused - the colored-rect quads share one white 8x8 sprite re-rendered
// per rectangle, exactly like sequences::renderOverlay's black quad.
struct LoadingScreenState {
  bool ready = false;
  bool builtinReady = false;
  Tyra::Sprite builtin;                // hud/loading.png fallback
  Tyra::Sprite white;                  // tinted into every bar quad
  std::vector<Tyra::Sprite> images;    // LS_IMAGE_TOTAL, one per LS_IMAGES
  std::vector<Tyra::Sprite> texts;     // LS_TEXT_TOTAL, one per LS_TEXTS
  std::vector<Tyra::Sprite> segs;      // LS_BAR_TOTAL, linked only for seg bars
  std::vector<Tyra::Sprite> splashes;  // SPLASH_COUNT, one per SPLASHES
};
LoadingScreenState g_loading;

void loadingEnsureReady(Engine* engine) {
  if (g_loading.ready) return;
  g_loading.ready = true;
  auto& repo = engine->renderer.getTextureRepository();
  g_loading.white.mode = SpriteMode::MODE_STRETCH;
  repo.add(FileUtils::fromCwd("hud/loading-white.png"))->addLink(g_loading.white.id);
  g_loading.images.resize(LS_IMAGE_TOTAL > 0 ? LS_IMAGE_TOTAL : 0);
  for (int i = 0; i < LS_IMAGE_TOTAL; ++i) {
    g_loading.images[i].mode = SpriteMode::MODE_STRETCH;
    repo.add(FileUtils::fromCwd(LS_IMAGES[i].path))->addLink(g_loading.images[i].id);
  }
  g_loading.texts.resize(LS_TEXT_TOTAL > 0 ? LS_TEXT_TOTAL : 0);
  for (int i = 0; i < LS_TEXT_TOTAL; ++i) {
    g_loading.texts[i].mode = SpriteMode::MODE_STRETCH;
    repo.add(FileUtils::fromCwd(LS_TEXTS[i].path))->addLink(g_loading.texts[i].id);
  }
  g_loading.segs.resize(LS_BAR_TOTAL > 0 ? LS_BAR_TOTAL : 0);
  for (int i = 0; i < LS_BAR_TOTAL; ++i) {
    if (LS_BARS[i].segPath[0] == '\0') continue;
    g_loading.segs[i].mode = SpriteMode::MODE_STRETCH;
    repo.add(FileUtils::fromCwd(LS_BARS[i].segPath))->addLink(g_loading.segs[i].id);
  }
  g_loading.splashes.resize(SPLASH_COUNT > 0 ? SPLASH_COUNT : 0);
  for (int i = 0; i < SPLASH_COUNT; ++i) {
    g_loading.splashes[i].mode = SpriteMode::MODE_STRETCH;
    repo.add(FileUtils::fromCwd(SPLASHES[i].path))->addLink(g_loading.splashes[i].id);
  }
}

namespace loadingscreen {
// One boot splash frame: the image (i) on its background color. Vsync-paced by
// the caller (the loop's boot sequence), held for SPLASHES[i].seconds.
void renderSplash(Engine* engine, int i) {
  if (i < 0 || i >= SPLASH_COUNT) return;
  loadingEnsureReady(engine);
  const SplashData& s = SPLASHES[i];
  const auto& scr = engine->renderer.core.getSettings();
  const float W = scr.getWidth(), H = scr.getHeight();
  engine->renderer.setClearScreenColor(Color(s.bg[0], s.bg[1], s.bg[2]));
  engine->renderer.beginFrame();
  Sprite& sp = g_loading.splashes[i];
  sp.size = Vec2(s.w, s.h);
  sp.position = Vec2(s.x * W - s.w * 0.5F, s.y * H - s.h * 0.5F);
  engine->renderer.renderer2D.render(sp);
  engine->renderer.endFrame();
}

void renderFrame(Engine* engine, int sceneIdx, float fraction) {
  if (fraction < 0.0F) fraction = 0.0F;
  if (fraction > 1.0F) fraction = 1.0F;

  int screen = -1;
  if (LS_COUNT > 0) {
    if (sceneIdx >= 0 && sceneIdx < SCENE_COUNT)
      screen = SCENE_LOADING_SCREEN[sceneIdx];
    else
      screen = LS_DEFAULT;
    if (screen < 0 || screen >= LS_COUNT) screen = LS_DEFAULT;
  }

  const auto& scr = engine->renderer.core.getSettings();
  const float W = scr.getWidth();
  const float H = scr.getHeight();

  // No authored screen resolves here: the classic built-in (hud/loading.png
  // centered on black), pixel-identical to the pre-editor loading screen.
  if (screen < 0 || screen >= LS_COUNT) {
    if (!g_loading.builtinReady) {
      g_loading.builtin.mode = SpriteMode::MODE_STRETCH;
      g_loading.builtin.size = Vec2(256.0F, 64.0F);
      g_loading.builtin.position = Vec2((W - 256.0F) * 0.5F, (H - 64.0F) * 0.5F);
      engine->renderer.getTextureRepository()
          .add(FileUtils::fromCwd("hud/loading.png"))
          ->addLink(g_loading.builtin.id);
      g_loading.builtinReady = true;
    }
    engine->renderer.setClearScreenColor(Color(0.0F, 0.0F, 0.0F));
    engine->renderer.beginFrame();
    engine->renderer.renderer2D.render(g_loading.builtin);
    engine->renderer.endFrame();
    return;
  }

  loadingEnsureReady(engine);
  const LoadingScreenData& s = LS_SCREENS[screen];
  engine->renderer.setClearScreenColor(Color(s.bg[0], s.bg[1], s.bg[2]));
  engine->renderer.beginFrame();

  // A tinted white quad (GS modulation, tint already in 0..128 range).
  auto quad = [&](float x, float y, float w, float h, const float* c) {
    if (w < 1.0F || h < 1.0F) return;
    g_loading.white.size = Vec2(w, h);
    g_loading.white.position = Vec2(x, y);
    g_loading.white.color = Color(c[0], c[1], c[2], 128.0F);
    engine->renderer.renderer2D.render(g_loading.white);
  };

  for (int i = 0; i < s.imgCount; ++i) {
    const LoadingImageData& im = LS_IMAGES[s.imgFirst + i];
    Sprite& sp = g_loading.images[s.imgFirst + i];
    sp.size = Vec2(im.w, im.h);
    sp.position = Vec2(im.x * W - im.w * 0.5F, im.y * H - im.h * 0.5F);
    engine->renderer.renderer2D.render(sp);
  }
  for (int i = 0; i < s.txtCount; ++i) {
    const LoadingTextData& t = LS_TEXTS[s.txtFirst + i];
    Sprite& sp = g_loading.texts[s.txtFirst + i];
    sp.size = Vec2((float)t.w, (float)t.h);
    sp.position = Vec2(t.x * W - t.w * 0.5F, t.y * H - t.h * 0.5F);
    engine->renderer.renderer2D.render(sp);
  }
  for (int i = 0; i < s.barCount; ++i) {
    const LoadingBarData& b = LS_BARS[s.barFirst + i];
    const float bx = b.x * W - b.w * 0.5F;  // top-left of the bar
    const float by = b.y * H - b.h * 0.5F;
    if (b.kind == 0) {
      // Continuous: track under a left-anchored fill scaled by progress.
      quad(bx, by, b.w, b.h, b.bg);
      quad(bx, by, b.w * fraction, b.h, b.fill);
    } else {
      const int segs = b.segments < 1 ? 1 : b.segments;
      const int lit = (int)(fraction * segs + 0.001F);
      const float segW = (b.w - b.spacing * (segs - 1)) / segs;
      for (int k = 0; k < segs; ++k) {
        const float sx = bx + k * (segW + b.spacing);
        const float* c = (k < lit) ? b.fill : b.bg;
        if (b.segPath[0] != '\0') {
          Sprite& sp = g_loading.segs[s.barFirst + i];  // reused per segment
          sp.size = Vec2(segW, b.h);
          sp.position = Vec2(sx, by);
          sp.color = Color(c[0], c[1], c[2], 128.0F);
          engine->renderer.renderer2D.render(sp);
        } else {
          quad(sx, by, segW, b.h, c);
        }
      }
    }
  }
  engine->renderer.endFrame();
}
}  // namespace loadingscreen

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
  // geometry. Clip 0.15 units in front of the camera instead - just past
  // the real near plane (0.1) and closer than the collision clearance the
  // walkers guarantee (playerRadius 0.35, EYE_CLEARANCE 0.2), so a wall
  // the player presses against can never fall in front of the clip plane
  // and open a see-through hole. (read during setRenderer below)
  PlanesClipAlgorithm::clipMargin =
      -(engine->renderer.core.getSettings().getNear() + 0.15F);

  // Wall-clock normalization: per-frame steps below are tuned for 50 Hz;
  // g_frameScale stretches them so NTSC's 60 Hz plays at the same speed.
  // These are the seeds - updateFrameClock() refreshes both every frame
  // from the measured frame time (frame drops slow the picture, not the
  // game; also what makes the vsync-off build play at the right speed).
  g_frameRate = engine->renderer.core.getSettings().getRefreshRate();
  g_frameDt = 1.0F / g_frameRate;
  g_frameScale = 50.0F / g_frameRate;
  // Seed the analog stick response curves from the project defaults (the
  // Set Stick Curve flow node overrides them at runtime; namespaced constants
  // so they cannot initialize the global-scope g_stick* definitions directly).
  g_stickCurveL = STICK_CURVE_L;
  g_stickCurveR = STICK_CURVE_R;
  g_stickExpL = STICK_EXP_L;
  g_stickExpR = STICK_EXP_R;
  // Experimental (Project > Preferences > Build): skip the vsync wait -
  // continuous frame rate instead of the 50/25 vsync snap, with tearing.
  if (!FRAME_LIMIT) engine->renderer.core.setFrameLimit(false);

  stapip.setRenderer(&engine->renderer.core);
  // Hidden "clipping": "vu1" mode: frustum-crossing packages are clipped by
  // the VU1 clip programs instead of the EE clipper (must follow setRenderer).
  stapip.core.setVU1Clipping(CLIP_VU1);
  engine->renderer.core.postFx.setBloom(POSTFX_BLOOM);
  engine->renderer.core.postFx.setGrain(POSTFX_GRAIN);
  engine->renderer.core.postFx.setDepthOfField(POSTFX_DOF_FOCUS,
                                               POSTFX_DOF_RANGE, POSTFX_DOF);
  // GS hardware distance fog (Scene/Project > Preferences > Fog).
  if (FOG_ENABLED)
    engine->renderer.core.setFog(Color(FOG_R, FOG_G, FOG_B), FOG_START,
                                 FOG_END);
  else
    engine->renderer.core.disableFog();
  // Default color grading look (Tools > Color Grading); no-op when -1.
  // Grading is global - scene switches keep whatever preset is active.
  applySceneGrading(engine, GRADING_DEFAULT);

  engine->renderer.setClearScreenColor(Color(SKY_R, SKY_G, SKY_B));

  // Two-player modes: open pad 2 (connector 2). Optional - no controller
  // there never blocks or asserts; it keeps polling so player 2 can plug in
  // and join mid-game (Start, see the loop).
  if (MULTIPLAYER_MODE != 0) pad2.initOptional(1);

  cameraLookAt = Vec4(0.0F, 0.0F, 0.0F);
  updateCameraOrbit();

  buildScene();

  // scriptCtx wiring + scripts' init() run from bootFirstScene() (loop boot),
  // after the deferred scene load - scripts' onStart must see scene 0's objects.

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

  // "USE" prompt, shown while looking at a usable object. Placement and
  // image come from hud_data.gen.hpp (Tools > UI Editor - the built-in
  // hud/use.png unless a custom sprite replaces it).
  usePromptSprite.mode = SpriteMode::MODE_STRETCH;
  usePromptSprite.size = Vec2(USE_PROMPT_W, USE_PROMPT_H);
  usePromptSprite.position =
      Vec2(USE_PROMPT_X * screen.getWidth() - USE_PROMPT_W * 0.5F,
           USE_PROMPT_Y * screen.getHeight() - USE_PROMPT_H * 0.5F);
  auto* useTexture = engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd(USE_PROMPT_PATH));
  useTexture->addLink(usePromptSprite.id);
  // "PICK UP" variant, shown instead when the looked-at object is pickable.
  // Same placement; its own texture (hud/pickup.png, replace to customize).
  pickPromptSprite.mode = SpriteMode::MODE_STRETCH;
  pickPromptSprite.size = usePromptSprite.size;
  pickPromptSprite.position = usePromptSprite.position;
  auto* pickTexture = engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd(PICK_PROMPT_PATH));
  pickTexture->addLink(pickPromptSprite.id);

  // The loading screen (loading_data.gen.hpp) builds its own sprites lazily on
  // first present (loadingscreen::renderFrame), so nothing to set up here.

  // Sound emitter samples (adpenc output next to the ELF)
  for (int i = 0; i < SND_COUNT; ++i)
    sndSamples.push_back(
        engine->audio.adpcm.load(FileUtils::fromCwd(SND_PATHS[i])));
}

void TerrainGame::loop() {
  updateFrameClock();  // real dt: frame drops slow the picture, not the game
  // The engine pumps pad 1; pad 2 is ours (optional - polls for a hot-join).
  if (MULTIPLAYER_MODE != 0) pad2.update();

  // Boot sequence (the engine holds the Tyra logo ~2s before this):
  //   phase 0 - boot splash images, each shown for its duration (in order),
  //   phase 1 - load scene 0 behind the loading screen (when enabled).
  // Everything runs from the loop, not init(): a frame presented from init()
  // (before the main loop) isn't vsync-paced and flashes by, so the boot
  // visuals were invisible; from the loop they pace normally.
  if (bootPhase < 2) {
    if (bootPhase == 0) {
      if (splashIndex < SPLASH_COUNT) {
        if (splashFrames <= 0)
          splashFrames = everyFrames(SPLASHES[splashIndex].seconds);
        loadingscreen::renderSplash(engine, splashIndex);
        if (--splashFrames <= 0) ++splashIndex;
        return;
      }
      bootPhase = 1;
      if (LOADING_SCREEN) {
        loadingTarget = 0;
        loadingFrames = loadingTotal = everyFrames(0.7F);
      }
    }
    if (bootPhase == 1) {
      if (!LOADING_SCREEN) {
        bootFirstScene();
        bootPhase = 2;
      } else {
        if (loadingFrames > 0) {
          const bool preLoad = loadingFrames > loadingTotal - 5;
          loadingscreen::renderFrame(engine, 0, preLoad ? 0.0F : 1.0F);
          --loadingFrames;
          if (loadingFrames == loadingTotal - 5) bootFirstScene();
          return;
        }
        bootPhase = 2;
      }
    }
  }

  const bool saveMenuActive = updateSaveMenu();
  const bool gameMenuWasOpen = gameMenuIndex >= 0;  // before updateGameMenu()
  const bool gameMenuPausing = updateGameMenu();  // false for overlay menus
  const bool menuActive = saveMenuActive || gameMenuPausing;
  // An open menu owns the pad even when it doesn't pause the world (overlay
  // menus, and the frame X closes a pausing menu): gameplay must not read that
  // same press too, or the X that drives the menu also makes the player jump.
  const bool menuOwnsPad =
      saveMenuActive || gameMenuWasOpen || gameMenuIndex >= 0;
  g_gameplayPaused = menuActive;  // freezes particles + animation playback
  // Option-block menu rows drive their bound engine settings every frame
  // (volume, deadzone, curve, display) - runs regardless of pause so a saved
  // setting keeps applying, and before applyVideoRequests so a display switch
  // it requests lands this frame.
  applyMenuBindings();
  // Portal crossing test: the walker's position before this frame's movement
  const float portalPrevX = players[0].x, portalPrevY = players[0].y,
              portalPrevZ = players[0].z;

  // Player 2 hot-join: Start on pad 2, any time gameplay owns the pads.
  // Leaving goes through a menu "Player count" Toggle (setPlayerTwoActive).
  if (MULTIPLAYER_MODE != 0 && P2_JOIN_ON_START && !menuOwnsPad &&
      !playerTwoActive && pad2.getClicked().Start)
    setPlayerTwoActive(true);
  if (!menuOwnsPad) {
    if (!updatePlayerEntity()) updateCameraOrbit();
    updateUseTarget();
  }

  scriptCtx.playerPosition = cameraPosition;
  scriptCtx.player2Active =
      MULTIPLAYER_MODE != 0 && playerTwoActive && players[1].objIndex >= 0;
  scriptCtx.player2Position =
      scriptCtx.player2Active
          ? Vec4(players[1].x, players[1].y + PP_EYE_HEIGHT(1), players[1].z)
          : scriptCtx.playerPosition;
  {
    // View direction for the scripts (Raycast flow node)
    Vec4 look = cameraLookAt - cameraPosition;
    const float lookLen =
        sqrtf(look.x * look.x + look.y * look.y + look.z * look.z);
    scriptCtx.playerLook = lookLen > 0.0001F
                               ? Vec4(look.x / lookLen, look.y / lookLen,
                                      look.z / lookLen)
                               : Vec4(0.0F, 0.0F, 1.0F);
  }
  if (menuOwnsPad) { scriptCtx.usedObject = -1; useTargetIndex = -1; }
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
      loadingFrames = loadingTotal = everyFrames(0.7F);  // ~0.7s hold
    } else {
      loadScene(target);
    }
  }
  if (loadingFrames > 0) {
    // A few frames at 0% before the (blocking) load, which pumps the bar from
    // 0 to 1 itself, then the remaining frames at 100%.
    const bool preLoad = loadingFrames > loadingTotal - 5;
    loadingscreen::renderFrame(engine, loadingTarget, preLoad ? 0.0F : 1.0F);
    --loadingFrames;
    if (loadingFrames == loadingTotal - 5)
      loadScene(loadingTarget);  // 5 frames shown first
    return;
  }

  // Streaming layers: Load/Unload Layer requests, one asset load per frame,
  // trickle activation of freshly resident layers.
  updateLayerStreaming();

  // Flow graph / script teleport request (needs a Player entity - the orbit
  // camera itself is not teleportable). Teleports P1; an active P2 comes
  // along, dropped a step to the side so the two don't interpenetrate.
  if (scriptCtx.teleport) {
    scriptCtx.teleport = false;
    if (PLAYER_INDEX >= 0) {
      players[0].x = scriptCtx.teleportPos.x;
      players[0].y = scriptCtx.teleportPos.y;
      players[0].z = scriptCtx.teleportPos.z;
      players[0].velY = 0.0F;
      players[0].yaw = scriptCtx.teleportYaw * PI / 180.0F;
      if (playerTwoActive && players[1].objIndex >= 0) {
        players[1].x = players[0].x + 1.2F;
        players[1].z = players[0].z;
        players[1].y = PP_MODE(1) == 1 ? players[0].y
                                       : terrainHeightAt(players[1].x, players[1].z);
        players[1].velY = 0.0F;
        players[1].yaw = players[0].yaw;
      }
    }
  }

  if (!menuActive) updateObjectPhysics();
  // Portal surfaces: carry the player / physics objects that crossed a
  // linked portal through to its target. After the physics step so object
  // crossings see this frame's motion; on a player hop the camera is
  // rebuilt inside, so no frame renders from the departure side.
  if (PORTAL_COUNT > 0 && !menuActive)
    updatePortals(portalPrevX, portalPrevY, portalPrevZ,
                  PLAYER_INDEX >= 0 ? &players[0].x : nullptr, &players[0].y,
                  &players[0].z, &players[0].yaw, &players[0].pitch,
                  &players[0].velY,
                  PLAYER_MODE == 1 ? 0.0F : PLAYER_EYE_HEIGHT);
  // Positioned AFTER the portal step so a teleport frame anchors the carried
  // object to the arrival camera (see the FPP loop note).
  if (!menuOwnsPad) updateCarriedObject();
  updateParticles();
  updateSoundEmitters();

  // Camera flashlight (Player object > Flashlight). The Set Flashlight flow
  // node drives the master (scriptCtx.flashlight: 0 off / 1 on / -1 = leave);
  // the optional toggle button flips the on/off state. The beam shows only
  // while both are set, so the toggle respects the Enabled master.
  if (scriptCtx.flashlight >= 0) {
    g_flashEnabled = scriptCtx.flashlight != 0;
    scriptCtx.flashlight = -1;
  }
  // Runtime graphics switches (Set Fog / Bloom / Grain / Particles flow nodes).
  if (scriptCtx.fog >= 0) {
    if (scriptCtx.fog)
      engine->renderer.core.setFog(Color(FOG_R, FOG_G, FOG_B), FOG_START, FOG_END);
    else
      engine->renderer.core.disableFog();
    scriptCtx.fog = -1;
  }
  if (scriptCtx.bloom >= 0) {
    engine->renderer.core.postFx.setBloom(scriptCtx.bloom);
    scriptCtx.bloom = -1;
  }
  if (scriptCtx.grain >= 0) {
    engine->renderer.core.postFx.setGrain(scriptCtx.grain);
    scriptCtx.grain = -1;
  }
  if (scriptCtx.dof == -2) {
    // Set Depth Of Field, "Scene setting" mode: back to the authored values
    engine->renderer.core.postFx.setDepthOfField(POSTFX_DOF_FOCUS,
                                                 POSTFX_DOF_RANGE, POSTFX_DOF);
    scriptCtx.dof = -1;
  } else if (scriptCtx.dof >= 0) {
    engine->renderer.core.postFx.setDepthOfField(
        scriptCtx.dofFocus, scriptCtx.dofRange, scriptCtx.dof);
    scriptCtx.dof = -1;
  }
  if (scriptCtx.particles >= 0) {
    g_particlesOn = scriptCtx.particles != 0;
    scriptCtx.particles = -1;
  }
  // Analog stick response curves (Set Stick Curve flow node).
  if (scriptCtx.stickCurveL >= 0) {
    g_stickCurveL = scriptCtx.stickCurveL;
    scriptCtx.stickCurveL = -1;
  }
  if (scriptCtx.stickCurveR >= 0) {
    g_stickCurveR = scriptCtx.stickCurveR;
    scriptCtx.stickCurveR = -1;
  }
  if (scriptCtx.stickExpL >= 1.0F) {
    g_stickExpL = scriptCtx.stickExpL;
    scriptCtx.stickExpL = -1.0F;
  }
  if (scriptCtx.stickExpR >= 1.0F) {
    g_stickExpR = scriptCtx.stickExpR;
    scriptCtx.stickExpR = -1.0F;
  }
  // Pad vibration (Vibrate Pad flow node / padVibrate() in scripts): a
  // request drives the DualShock actuators; rumbleSec > 0 arms the auto-stop
  // countdown (0 = vibrate until the next request). The countdown runs even
  // while a menu pauses the scripts, so a timed rumble always ends.
  if (scriptCtx.rumble >= 0) {
    engine->pad.setActuators(scriptCtx.rumbleSmall != 0, (u8)scriptCtx.rumble);
    g_rumbleTimer = (scriptCtx.rumble > 0 || scriptCtx.rumbleSmall != 0)
                        ? scriptCtx.rumbleSec
                        : 0.0F;
    scriptCtx.rumble = -1;
  }
  if (g_rumbleTimer > 0.0F) {
    g_rumbleTimer -= g_frameDt;
    if (g_rumbleTimer <= 0.0F) {
      g_rumbleTimer = 0.0F;
      engine->pad.setActuators(false, 0);
    }
  }
  // Cutscene camera override: a Cutscene Director sequence with a camera track
  // drives the frame camera (Play/Stop Sequence). Applied after scripts so the
  // sequence player (a global Script) has posed the camera for this frame.
  if (scriptCtx.cameraOverride) {
    cameraPosition = scriptCtx.cameraEye;
    cameraLookAt = scriptCtx.cameraAt;
  }
  // Cutscene "Hide player": drop the third-person avatar for this frame
  // (applied after scripts so the sequence player's flag wins).
  if (PLAYER_INDEX >= 0 && PLAYER_MODE == 2)
    runtimeObjects[PLAYER_INDEX].visible = !scriptCtx.hidePlayer;
  if (players[1].objIndex >= 0 && PP_MODE(1) == 2)
    runtimeObjects[players[1].objIndex].visible =
        !scriptCtx.hidePlayer && playerTwoActive;
  // Runtime video output (Set Display Mode / Set Widescreen flow nodes) +
  // the keep-or-revert countdown. Must run before beginFrame - a scan-mode
  // switch rebuilds the VRAM layout between frames. A switch closes any
  // open game menu: the player judges the new picture unobstructed and the
  // confirm prompt's X press cannot double as a menu select.
  if (applyVideoRequests(engine, scriptCtx)) {
    gameMenuIndex = -1;
    gameMenuStackDepth = 0;
  }
  if (!menuOwnsPad && flashlightTogglePressed(engine)) g_flashOn = !g_flashOn;
  if (g_flashEnabled && g_flashOn) {
    Vec4 flashDir = cameraLookAt - cameraPosition;
    engine->renderer.core.setSpotLight(
        Color(FLASHLIGHT_R, FLASHLIGHT_G, FLASHLIGHT_B), cameraPosition,
        flashDir, FLASHLIGHT_RANGE, FLASHLIGHT_ANGLE);
  } else {
    engine->renderer.core.disableSpotLight();
  }
  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt));
  {
    engine->renderer.renderer3D.usePipeline(stapip);
    // Split screen (two players): the scene renders twice, top half from
    // P1's camera and bottom half from P2's (players[1].camPos, written by
    // its walker). A cutscene camera override takes the whole screen. HUD /
    // menus / post fx stay full-screen, drawn after splitView.end().
    const bool splitFrame = MULTIPLAYER_MODE == 2 && playerTwoActive &&
                            players[1].objIndex >= 0 &&
                            !scriptCtx.cameraOverride;
    if (splitFrame) {
      auto& core = engine->renderer.core;
      splitPassActive = true;
      core.splitView.begin(0);
      renderScene();
      // Swap the whole camera state to P2: renderScene reads cameraPosition
      // (sky dome centering, LOD, streaming focus), and the renderer needs
      // the second half's view matrix + frustum planes.
      const Vec4 savedPos = cameraPosition, savedLook = cameraLookAt;
      cameraPosition = players[1].camPos;
      cameraLookAt = players[1].camLook;
      core.renderer3D.update(CameraInfo3D(&cameraPosition, &cameraLookAt));
      core.splitView.begin(1);
      splitSecondPass = true;  // reuse this frame's anim poses/skins
      renderScene();
      splitSecondPass = false;
      core.splitView.end();
      cameraPosition = savedPos;
      cameraLookAt = savedLook;
      core.renderer3D.update(CameraInfo3D(&cameraPosition, &cameraLookAt));
      splitPassActive = false;
    } else {
      renderScene();
    }
    // Depth of field composites right after the 3D scene, BEFORE any 2D:
    // sprites stamp z = max across their whole rect (transparent margins
    // included), which would punch sharp rectangles into a later z-tested
    // DoF pass (a crosshair HUD showed through the blur as a box).
    engine->renderer.core.applyPostFx(Tyra::RendererCorePostFx::PassDof);
    // Full-screen effects can sit inside the HUD stack (Tools > UI Editor):
    // bloom (with color grading) and film grain composite at independent
    // points, so sprites drawn afterwards stay crisp on top of them. -1 = the
    // pass applies at endFrame, over everything (menus included).
    for (int i = 0; i < (int)hudSprites.size(); ++i) {
      if (i == HUD_BLOOM_LAYER)
        engine->renderer.core.applyPostFx(
            Tyra::RendererCorePostFx::PassBloom |
            Tyra::RendererCorePostFx::PassGrading);
      if (i == HUD_GRAIN_LAYER)
        engine->renderer.core.applyPostFx(Tyra::RendererCorePostFx::PassGrain);
{{SCREEN_FX_IN_LOOP}}      if (scriptCtx.hudVisible)
        engine->renderer.renderer2D.render(hudSprites[i]);
    }
    // Custom screen effects placed at the top of the stack (layer -1): drawn
    // over the whole HUD stack, under the USE prompt / texts / pause menus.
{{SCREEN_FX_TOP}}    if (useTargetIndex >= 0)
      engine->renderer.renderer2D.render(
          runtimeObjects[useTargetIndex].data.pickable ? pickPromptSprite
                                                       : usePromptSprite);
    updateAndRenderHudTexts();
    updateAndRenderDynTexts();
    // Cutscene Director widescreen bars + fade-to-black: solid quads over the
    // scene and HUD (texts included), under the pause menus (no-op unless a
    // cutscene draws).
    sequences::renderOverlay(engine, scriptCtx);
    renderGameMenu();
    renderSaveMenu();
    drawDebugHud(engine);
    drawVideoConfirm(engine);
  }
  engine->renderer.endFrame();
}
)";

// Shared scene/terrain mesh building and runtime object management.
static const char* TPL_GAME_CPP_SCENE = R"(
void TerrainGame::buildScene() {
  // Asset tables start empty: loadScene(0) below loads what the first
  // scene's start-resident layers need and nothing else - the rest streams
  // in on demand when a Load Layer node (or a scene switch) asks for it.
  loadedTextures.assign(TEXTURE_COUNT > 0 ? TEXTURE_COUNT : 0, nullptr);
  sceneTexLoaded.assign(TEXTURE_COUNT > 0 ? TEXTURE_COUNT : 0, 0);
  gameModels.assign(MODEL_COUNT > 0 ? MODEL_COUNT : 0, GameModel());
  modelLoaded.assign(MODEL_COUNT > 0 ? MODEL_COUNT : 0, 0);
  gameMaterials.assign(MATERIAL_COUNT > 0 ? MATERIAL_COUNT : 0, GameMaterial());
  materialLoaded.assign(MATERIAL_COUNT > 0 ? MATERIAL_COUNT : 0, 0);
  gameAnimModels.clear();
  gameAnimModels.resize(ANIM_MODEL_COUNT > 0 ? ANIM_MODEL_COUNT : 0);
  animModelLoaded.assign(ANIM_MODEL_COUNT > 0 ? ANIM_MODEL_COUNT : 0, 0);
  // One shared directional-light set for every animated instance; the
  // arrays are member storage, values refresh each frame in the anim pass.
  animDirLights.setLightsManually(animLightColors, animLightDirs);
  g_animGame = this;
  scriptCtx.resolveClip = &animResolveClipThunk;
  scriptCtx.spawnObject = &spawnObjectThunk;
  scriptCtx.despawnObject = &despawnObjectThunk;

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
  // Terrain layer passes: identical settings, but with GS alpha blending on
  // (PRIM ABE) - the per-mesh ALPHA qword defaults to standard alpha-over, so
  // Gouraud vertex alpha = the painted splat weight blends the layer texture
  // over the base pass.
  layerInfoBag = std::make_unique<StaPipInfoBag>();
  layerInfoBag->model = &model;
  layerInfoBag->shadingType = TyraShadingFlat;
  layerInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
  layerInfoBag->fullClipChecks = CLIP_PRECISE;
  layerInfoBag->blendingEnabled = true;
  // The terrain mesh itself is built per chunk by loadScene(0) below
  // (resetTerrainChunks + the synchronous chunk drain).

  // Runtime copies of the scene objects - scripts and physics mutate these.
  skyHorizonR = SKY_R, skyHorizonG = SKY_G, skyHorizonB = SKY_B;
  buildSkyDome();

  // Save system: BIOS mc modules, custom values, menu sprites (hud/save-*.png)
  saveValues.assign(SAVE_VALUE_COUNT > 0 ? SAVE_VALUE_COUNT : 1, 0.0F);
  for (int i = 0; i < SAVE_VALUE_COUNT; ++i) saveValues[i] = SAVE_VALUE_DEFAULTS[i];
  scriptCtx.saveValues = saveValues.data();
  scriptCtx.saveValueCount = SAVE_VALUE_COUNT;
  // Seed the runtime deadzone from the compile-time Preferences defaults (the
  // constants are namespaced, so this cannot happen at the global def). A menu
  // "Deadzone" option block overrides these each frame; the stick response
  // curve globals (g_stickCurve*/g_stickExp*) are seeded separately in init().
  g_deadzoneL = ANALOG_DEADZONE_L;
  g_deadzoneR = ANALOG_DEADZONE_R;
  // The boot display mode IS the project default (main.cpp resolved it,
  // nothing can have switched it yet) - latch it for the menu "DEFAULT"
  // display option before any option lookup below resolves that sentinel.
  g_defaultDispMode = (int)engine->renderer.core.getSettings().getDisplayMode();
  // Seed the display/widescreen option-block trackers from the current saved
  // option so applyMenuBindings does not fire a scan-mode switch (+ confirm
  // prompt) at boot: the game boots in the project's compiled display mode,
  // and a menu row only switches when the player moves it (or loads a save
  // that changed it). With an "apply video mode" row in the project the
  // display row is a staging UI instead - align it to the actual boot mode
  // so a title-screen menu opens showing (and its APPLY row seeing) reality.
  for (int mi = 0; mi < MENU_COUNT; ++mi)
    for (int e = 0; e < MENUS[mi].entryCount; ++e) {
      const MenuEntryData& en = MENUS[mi].entries[e];
      if (en.param < 0 || en.param >= SAVE_VALUE_COUNT) continue;
      if (en.bind == 5) {
        g_menuDispOpt = (int)saveValues[en.param];
        if (MENU_HAS_APPLY_VIDEO) {
          const int live = displayOptionIndexOf(
              en, (int)engine->renderer.core.getSettings().getDisplayMode());
          if (live >= 0) {
            saveValues[en.param] = (float)live;
            g_menuDispOpt = live;
          }
        }
      }
      if (en.bind == 6) g_menuWideOpt = (int)saveValues[en.param];
    }
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
    // Toggle/Choice value strips: a sub-rect sprite per menu (MODE_REPEAT
    // samples [offset, offset+size] texels - the debug glyph atlas trick).
    // renderGameMenu moves offset/position to the active cell each frame.
    menuValueSprites.clear();
    menuValueSprites.reserve(MENU_COUNT);
    for (int i = 0; i < MENU_COUNT; ++i) {
      const MenuData& m = MENUS[i];
      Sprite s;
      s.mode = SpriteMode::MODE_REPEAT;
      s.size = Vec2((float)m.valueCellW, (float)m.valueCellH);
      menuValueSprites.push_back(s);
      if (m.values[0] == '\0') continue;  // no value entries in this menu
      auto* t = engine->renderer.getTextureRepository().add(
          FileUtils::fromCwd(m.values));
      t->addLink(menuValueSprites.back().id);
    }
    setupSprite(menuCursorSprite, "hud/save-cursor.png", 16, 16, 0.0F, 0.0F);
    setupSprite(menuDimSprite, "hud/menu-dim.png", scr.getWidth(),
                scr.getHeight(), 0.0F, 0.0F);

    // On-screen texts (hud_data.gen.hpp): baked sprites toggled by the
    // Show Text / Hide Text flow nodes through scriptCtx (wired here, before
    // the scripts' init runs from the game init).
    hudTextSprites.clear();
    hudTextSprites.reserve(HUD_TEXT_COUNT);
    hudTextReq.assign(HUD_TEXT_COUNT > 0 ? HUD_TEXT_COUNT : 1, -1);
    hudTextDur.assign(HUD_TEXT_COUNT > 0 ? HUD_TEXT_COUNT : 1, 0.0F);
    hudTextOn.assign(HUD_TEXT_COUNT > 0 ? HUD_TEXT_COUNT : 1, 0);
    hudTextTimer.assign(HUD_TEXT_COUNT > 0 ? HUD_TEXT_COUNT : 1, 0.0F);
    for (int i = 0; i < HUD_TEXT_COUNT; ++i) {
      const HudTextData& t = HUD_TEXTS[i];
      Sprite s;
      s.mode = SpriteMode::MODE_STRETCH;
      s.size = Vec2((float)t.w, (float)t.h);
      s.position = Vec2(t.x * scr.getWidth() - t.w * 0.5F,
                        t.y * scr.getHeight() - t.h * 0.5F);
      hudTextSprites.push_back(s);
      auto* tex = engine->renderer.getTextureRepository().add(
          FileUtils::fromCwd(t.path));
      tex->addLink(hudTextSprites.back().id);
      hudTextOn[i] = (unsigned char)t.visible;
    }
    scriptCtx.textRequest = hudTextReq.data();
    scriptCtx.textDuration = hudTextDur.data();
    scriptCtx.textCount = HUD_TEXT_COUNT;

    // Runtime texts (font_data.gen.hpp). Buffers only - no texture is touched
    // here: a font atlas reaches the repository (and VRAM) on the first frame
    // a Display Text using it is actually drawn. See drawFontText.
    dynTextReq.assign(DYN_TEXT_COUNT > 0 ? DYN_TEXT_COUNT : 1, -1);
    dynTextDur.assign(DYN_TEXT_COUNT > 0 ? DYN_TEXT_COUNT : 1, 0.0F);
    dynTextOn.assign(DYN_TEXT_COUNT > 0 ? DYN_TEXT_COUNT : 1, 0);
    dynTextTimer.assign(DYN_TEXT_COUNT > 0 ? DYN_TEXT_COUNT : 1, 0.0F);
    dynTextBuf.assign((DYN_TEXT_COUNT > 0 ? DYN_TEXT_COUNT : 1) * DYN_TEXT_LEN,
                      '\0');
    scriptCtx.dynTextRequest = dynTextReq.data();
    scriptCtx.dynTextDuration = dynTextDur.data();
    scriptCtx.dynTextBuf = dynTextBuf.data();
    scriptCtx.dynTextOn = dynTextOn.data();
    scriptCtx.dynTextCount = DYN_TEXT_COUNT;
    scriptCtx.dynTextLen = DYN_TEXT_LEN;
    if (TITLE_MENU >= 0) {
      gameMenuIndex = TITLE_MENU;
      gameMenuCursor = 0;
      // The pad reconfigures for ~3.5s after boot and reports garbage
      // clicks - one of those must not press a title entry.
      gameMenuGrace = 200;
    }
  }

  // The first scene is loaded from the loop (bootFirstScene), not here, so
  // its load is vsync-paced behind the loading screen after the logo hold.
}

// Loads scene 0 and runs the scripts' init() once - the boot equivalent of a
// scene switch. Deferred out of init()/buildScene() into the loop's boot
// sequence so the load runs at vsync pace (a visible loading-screen progress
// bar) instead of flashing by before the first presented frame.
void TerrainGame::bootFirstScene() {
  loadScene(0);
  scriptCtx.engine = engine;
  scriptCtx.objects = runtimeObjects.data();
  scriptCtx.objectCount = (int)runtimeObjects.size();
  scriptCtx.skyColor = Color(SKY_R, SKY_G, SKY_B);
  scriptCtx.playerPosition = cameraPosition;
  scriptCtx.playerLook = Vec4(0.0F, 0.0F, 1.0F);  // real look set per frame
  for (Script* script : getScripts()) script->init(scriptCtx);
}

// Shared texture cache: one engine texture per path, reference-counted so
// layers/models sharing a PNG neither load it twice nor free it while the
// other still draws with it. A missing file caches a null entry - the part
// degrades to its Kd color instead of an assert.
Texture* TerrainGame::acquireTexture(const std::string& path) {
  auto it = texCache.find(path);
  if (it == texCache.end()) {
    TexEntry e;
    if (assetFileExists(path)) {
      e.tex =
          engine->renderer.getTextureRepository().add(FileUtils::fromCwd(path));
      // Upload to GS VRAM now, OUTSIDE the frame. The pipelines otherwise
      // upload on first use - a PATH3 transfer in the middle of a rendered
      // frame, racing the VU1/GIF work in flight. Boot-time loads got away
      // with it on the first near-empty frames; textures streamed in by
      // Load Layer hit it mid-gameplay repeatedly.
      engine->renderer.core.texture.useTexture(e.tex);
    } else {
      TYRA_WARN("Texture missing: ", path.c_str());
    }
    it = texCache.emplace(path, e).first;
  }
  it->second.refs++;
  return it->second.tex;
}

void TerrainGame::releaseTexture(const std::string& path) {
  auto it = texCache.find(path);
  if (it == texCache.end()) return;
  if (--it->second.refs > 0) return;
  // last reference gone - destruct the texture and free its GS buffer slot
  if (it->second.tex)
    engine->renderer.getTextureRepository().free(it->second.tex);
  texCache.erase(it);
}

// Loads one custom .obj through the engine's LeanObjLoader: geometry split
// per MTL material, map_Kd textures through the texture cache, the real
// mesh AABB for box collision and a CollisionMesh where some scene object
// collides in mesh mode. Called on demand by the layer streaming.
void TerrainGame::loadModelAsset(int i) {
  if (i < 0 || i >= MODEL_COUNT || modelLoaded[i]) return;
  modelLoaded[i] = 1;  // missing/unparseable stays empty but counts as tried
  const std::string overrideMtl = MODEL_MTLS[i];
  auto mesh = LeanObjLoader::load(MODEL_PATHS[i], overrideMtl);
  if (!mesh) return;  // stays empty - objects using it render nothing
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
      part.texture = acquireTexture(path);
      gm.texPaths.push_back(path);
    }
    if (mat.reflTextureName == "@sky") {
      // Dynamic env map: the engine-owned VRAM target, re-rendered from the
      // sky dome every frame (renderScene).
      part.reflTexture = engine->renderer.core.envMap.getTexture();
      part.reflStrength = mat.reflStrength;
      part.reflDynamic = true;
      ++g_dynamicEnvUsers;
    } else if (!mat.reflTextureName.empty()) {
      const std::string path = dir + mat.reflTextureName;
      part.reflTexture = acquireTexture(path);
      part.reflStrength = mat.reflStrength;
      gm.texPaths.push_back(path);
    }
    part.reflRounded = mat.reflRounded;
    gm.parts.push_back(std::move(part));
  }
  if (MODEL_NEEDS_COLLIDER[i]) {
    std::vector<float> all;  // the collider spans every material part
    for (const auto& part : gm.parts)
      all.insert(all.end(), part.verts.begin(), part.verts.end());
    gm.collider.build(all.data(), (u32)(all.size() / 8), 8);
  }
}

// Frees a model's geometry, collider and texture references. Only called
// when no object of a resident layer uses the model - every GeoPart drawing
// it was dropped by deactivateObject() beforehand.
void TerrainGame::freeModelAsset(int i) {
  if (i < 0 || i >= MODEL_COUNT || !modelLoaded[i]) return;
  GameModel& gm = gameModels[i];
  for (const std::string& path : gm.texPaths) releaseTexture(path);
  for (const GameModelPart& part : gm.parts)
    if (part.reflDynamic) --g_dynamicEnvUsers;
  gm = GameModel();
  modelLoaded[i] = 0;
}

// Primitive materials: each MATERIAL_PATHS entry is a .mtl whose FIRST
// material becomes the surface of the primitives it is assigned to
// (Kd color + optional map_Kd texture, resolved relative to the .mtl).
void TerrainGame::loadMaterialAsset(int i) {
  if (i < 0 || i >= MATERIAL_COUNT || materialLoaded[i]) return;
  materialLoaded[i] = 1;
  const auto materials = LeanObjLoader::loadMtl(MATERIAL_PATHS[i]);
  if (materials.empty()) return;  // stays white - plain object color
  const auto& mat = materials.front();
  GameMaterial& gmat = gameMaterials[i];
  gmat.kd[0] = mat.kd[0];
  gmat.kd[1] = mat.kd[1];
  gmat.kd[2] = mat.kd[2];
  std::string dir = MATERIAL_PATHS[i];
  const size_t slash = dir.find_last_of('/');
  dir = slash == std::string::npos ? "" : dir.substr(0, slash + 1);
  if (!mat.textureName.empty()) {
    gmat.texPath = dir + mat.textureName;
    gmat.texture = acquireTexture(gmat.texPath);
  }
  if (mat.reflTextureName == "@sky") {
    // Dynamic env map (see loadModelAsset).
    gmat.reflTexture = engine->renderer.core.envMap.getTexture();
    gmat.reflStrength = mat.reflStrength;
    gmat.reflDynamic = true;
    ++g_dynamicEnvUsers;
  } else if (!mat.reflTextureName.empty()) {
    gmat.reflTexPath = dir + mat.reflTextureName;
    gmat.reflTexture = acquireTexture(gmat.reflTexPath);
    gmat.reflStrength = mat.reflStrength;
  }
  gmat.reflRounded = mat.reflRounded;
}

void TerrainGame::freeMaterialAsset(int i) {
  if (i < 0 || i >= MATERIAL_COUNT || !materialLoaded[i]) return;
  GameMaterial& gmat = gameMaterials[i];
  if (!gmat.texPath.empty()) releaseTexture(gmat.texPath);
  if (!gmat.reflTexPath.empty()) releaseTexture(gmat.reflTexPath);
  if (gmat.reflDynamic) --g_dynamicEnvUsers;
  gmat = GameMaterial();
  materialLoaded[i] = 0;
}

// Animated models: .glb files serialized by the editor into .tskl skeletal
// files (TsklLoader). The model data is shared; every scene object gets a
// SkelInstance (own playback state + skinned output mesh). Textures ship as
// PNGs next to the .tskl and link to each instance's materials by id.
void TerrainGame::loadAnimModelAsset(int i) {
  if (i < 0 || i >= ANIM_MODEL_COUNT || animModelLoaded[i]) return;
  animModelLoaded[i] = 1;
  auto model = TsklLoader::load(ANIM_MODEL_PATHS[i]);
  if (!model) return;  // stays empty - objects using it render nothing
  GameAnimModel& gam = gameAnimModels[i];
  gam.textures.assign(model->parts.size(), nullptr);
  for (size_t m = 0; m < model->parts.size(); ++m) {
    const std::string& path = model->parts[m].texturePath;
    if (path.empty()) continue;
    Texture* t = acquireTexture(path);
    gam.texPaths.push_back(path);
    if (t)
      gam.textures[m] = t;
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

// Frees the shared skeletal model + its textures. Every SkelInstance
// sampling it is already gone - deactivateObject() resets them before the
// residency pass frees assets.
void TerrainGame::freeAnimModelAsset(int i) {
  if (i < 0 || i >= ANIM_MODEL_COUNT || !animModelLoaded[i]) return;
  GameAnimModel& gam = gameAnimModels[i];
  for (const std::string& path : gam.texPaths) releaseTexture(path);
  gam = GameAnimModel();
  animModelLoaded[i] = 0;
}

// Scene (terrain) textures from texture_data.gen.hpp, through the cache.
void TerrainGame::loadSceneTexture(int i) {
  if (i < 0 || i >= TEXTURE_COUNT || sceneTexLoaded[i]) return;
  sceneTexLoaded[i] = 1;
  loadedTextures[i] = acquireTexture(TEXTURE_PATHS[i]);
}

void TerrainGame::freeSceneTexture(int i) {
  if (i < 0 || i >= TEXTURE_COUNT || !sceneTexLoaded[i]) return;
  releaseTexture(TEXTURE_PATHS[i]);
  loadedTextures[i] = nullptr;
  sceneTexLoaded[i] = 0;
}

// Desired residency of an object's layer (-1 = no layer = always resident).
bool TerrainGame::layerOn(int layer) const {
  if (layer < 0) return true;
  if (layer >= (int)layerTarget.size()) return true;
  return layerTarget[layer] != 0;
}

// Recomputes which assets the active scene needs under the current layer
// targets: frees loaded-but-unneeded ones immediately (cheap), rebuilds the
// stream queue with the needed-but-missing ones. loadScene() drains the
// queue synchronously behind the loading screen; updateLayerStreaming()
// drains it one asset per frame during gameplay.
void TerrainGame::applyLayerResidency() {
  std::vector<unsigned char> modelNeed(gameModels.size(), 0);
  std::vector<unsigned char> materialNeed(gameMaterials.size(), 0);
  std::vector<unsigned char> animNeed(gameAnimModels.size(), 0);
  std::vector<unsigned char> texNeed(loadedTextures.size(), 0);
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    const SceneObjectData& d = SCENE_OBJECTS[i];
    if (!layerOn(d.layer)) continue;
    if (d.model >= 0 && d.model < (int)modelNeed.size()) modelNeed[d.model] = 1;
    if (d.material >= 0 && d.material < (int)materialNeed.size())
      materialNeed[d.material] = 1;
    if (d.animModel >= 0 && d.animModel < (int)animNeed.size())
      animNeed[d.animModel] = 1;
  }
  // Active spawn-pool clones keep their template's assets resident even when
  // the template's own layer is out (a clone from a no-layer template must
  // never lose its model mid-frame; clones OF an unloading layer are already
  // deactivated before this runs).
  for (int i = SCENE_OBJECT_COUNT; i < (int)runtimeObjects.size(); ++i) {
    if (!runtimeObjects[i].active) continue;
    const SceneObjectData& d = runtimeObjects[i].data;
    if (d.model >= 0 && d.model < (int)modelNeed.size()) modelNeed[d.model] = 1;
    if (d.material >= 0 && d.material < (int)materialNeed.size())
      materialNeed[d.material] = 1;
    if (d.animModel >= 0 && d.animModel < (int)animNeed.size())
      animNeed[d.animModel] = 1;
  }
  if (TERRAIN_TEXTURE >= 0 && TERRAIN_TEXTURE < (int)texNeed.size())
    texNeed[TERRAIN_TEXTURE] = 1;
  // Painted terrain layers keep their tiled textures resident with the scene.
  for (int l = 0; l < TERRAIN_LAYER_COUNT; ++l) {
    const int t = TERRAIN_LAYER_TEXTURES[g_activeScene][l];
    if (t >= 0 && t < (int)texNeed.size()) texNeed[t] = 1;
  }

  for (int i = 0; i < (int)modelNeed.size(); ++i)
    if (!modelNeed[i] && modelLoaded[i]) freeModelAsset(i);
  for (int i = 0; i < (int)materialNeed.size(); ++i)
    if (!materialNeed[i] && materialLoaded[i]) freeMaterialAsset(i);
  for (int i = 0; i < (int)animNeed.size(); ++i)
    if (!animNeed[i] && animModelLoaded[i]) freeAnimModelAsset(i);
  for (int i = 0; i < (int)texNeed.size(); ++i)
    if (!texNeed[i] && sceneTexLoaded[i]) freeSceneTexture(i);

  streamQueue.clear();
  for (int i = 0; i < (int)texNeed.size(); ++i)  // terrain first - most visible
    if (texNeed[i] && !sceneTexLoaded[i]) streamQueue.push_back((3 << 16) | i);
  for (int i = 0; i < (int)materialNeed.size(); ++i)
    if (materialNeed[i] && !materialLoaded[i]) streamQueue.push_back((1 << 16) | i);
  for (int i = 0; i < (int)modelNeed.size(); ++i)
    if (modelNeed[i] && !modelLoaded[i]) streamQueue.push_back((0 << 16) | i);
  for (int i = 0; i < (int)animNeed.size(); ++i)
    if (animNeed[i] && !animModelLoaded[i]) streamQueue.push_back((2 << 16) | i);
}

void TerrainGame::processOneStreamJob() {
  if (streamQueue.empty()) return;
  const int job = streamQueue.front();
  streamQueue.erase(streamQueue.begin());
  const int kind = job >> 16;
  const int index = job & 0xFFFF;
  if (kind == 0)
    loadModelAsset(index);
  else if (kind == 1)
    loadMaterialAsset(index);
  else if (kind == 2)
    loadAnimModelAsset(index);
  else
    loadSceneTexture(index);

  // A clone spawned before its template's assets were resident built empty
  // geometry - re-arm it now that the asset landed (authored objects go
  // through activateObject after the queue drains instead).
  for (int i = SCENE_OBJECT_COUNT; i < (int)runtimeObjects.size(); ++i) {
    RuntimeObject& o = runtimeObjects[i];
    if (!o.active) continue;
    if ((kind == 0 && o.data.model == index) ||
        (kind == 1 && o.data.material == index) ||
        (kind == 2 && o.data.animModel == index)) {
      o.dirty = true;
      if (kind == 2) setupAnimObject(i);
    }
  }
}

// Brings a streamed-in object back with fresh runtime state from the scene
// data - like a scene load, a re-entered GTA3 interior resets the same way.
// Geometry rebuilds lazily through the dirty flag.
void TerrainGame::activateObject(int i) {
  RuntimeObject& o = runtimeObjects[i];
  o = RuntimeObject();
  o.data = SCENE_OBJECTS[i];
  o.visible = o.data.type != 4 && o.data.type != 6 &&
              !(o.data.type == 7 && !o.data.emitEnabled);
  o.dirty = true;
  setupAnimObject(i);
}

// Streams an object out: everything it owns on the heap (vertex copies,
// bags, skeletal instance, hull) is released. The constexpr scene data
// stays, so activateObject() can rebuild it exactly.
void TerrainGame::deactivateObject(int i) {
  runtimeObjects[i].active = false;
  runtimeObjects[i].visible = false;
  runtimeObjects[i].dirty = false;
  objectGeometry[i] = ObjectGeometry();
}

// --- Dynamic spawning (Spawn Object / Despawn Object flow nodes) ------------
// Clones an authored scene object into a free pool slot past
// SCENE_OBJECT_COUNT and returns its runtimeObjects index (-1 = pool full or
// bad template). The template itself is untouched. The clone keeps the
// template's layer, so unloading that layer despawns it too; its assets are
// pinned by applyLayerResidency while it lives, and a clone spawned before
// its model streamed in re-arms its geometry when the asset lands
// (processOneStreamJob).
int TerrainGame::spawnObjectAt(int templateIndex, float x, float y, float z,
                               float yaw) {
  if (templateIndex < 0 || templateIndex >= SCENE_OBJECT_COUNT) return -1;
  int slot = -1;
  for (int i = SCENE_OBJECT_COUNT; i < (int)runtimeObjects.size(); ++i)
    if (!runtimeObjects[i].active) {
      slot = i;
      break;
    }
  if (slot < 0) return -1;  // MAX_SPAWNED_OBJECTS clones already live
  RuntimeObject& o = runtimeObjects[slot];
  o = RuntimeObject();
  o.data = SCENE_OBJECTS[templateIndex];
  o.data.position[0] = x;
  o.data.position[1] = y;
  o.data.position[2] = z;
  o.data.rotation[1] = yaw;
  o.data.saveState = false;  // clones are never persisted in save slots
  // Same visibility rules as activateObject: markers stay invisible,
  // disabled emitters too.
  o.visible = o.data.type != 4 && o.data.type != 6 &&
              !(o.data.type == 7 && !o.data.emitEnabled);
  o.dirty = true;
  objectGeometry[slot] = ObjectGeometry();
  setupAnimObject(slot);
  applyLayerResidency();  // queue the template's assets if not resident yet
  if (o.data.type == 7) buildParticles();
  return slot;
}

// Despawns a spawned clone immediately (its slot recycles). On an authored
// object it only deactivates - the layer streaming can bring those back.
void TerrainGame::despawnObjectAt(int index) {
  if (index < 0 || index >= (int)runtimeObjects.size()) return;
  if (!runtimeObjects[index].active) return;
  const bool emitter = runtimeObjects[index].data.type == 7;
  deactivateObject(index);
  applyLayerResidency();  // free whatever only this clone still needed
  if (emitter) buildParticles();
}

// Once per frame: applies the scripts' Load/Unload Layer requests, frees
// unloading layers immediately, streams missing assets in ONE per frame and
// then trickle-activates the loaded layer's objects a few per frame - the
// cost is spread out so a corridor walk masks the whole load, GTA3 style.
void TerrainGame::updateLayerStreaming() {
  const int lc = (int)layerTarget.size();
  if (lc == 0) return;

  // Auto-streamed layers (Layers panel > Auto stream): crossing a zone
  // boundary queues the same request a Load/Unload Layer node would, so
  // scripts can still override a zone until the next crossing. The unload
  // edge sits a hysteresis band beyond the radius - pacing along the border
  // doesn't thrash. Focus = cameraLookAt (the player in FPP, the terrain
  // center for orbit showcases) - and player 2's avatar while active, so a
  // zone loads when EITHER player enters and unloads only when both leave.
  if ((int)layerAutoInside.size() == lc) {
    const float px = cameraLookAt.x;
    const float pz = cameraLookAt.z;
    const bool p2 = playerTwoActive && players[1].objIndex >= 0;
    for (int l = 0; l < lc; ++l) {
      const float r = SCENE_LAYER_STREAM_R[l];
      if (r <= 0.0F) continue;
      const float dx = px - SCENE_LAYER_STREAM_X[l];
      const float dz = pz - SCENE_LAYER_STREAM_Z[l];
      float d2 = dx * dx + dz * dz;
      if (p2) {
        const float dx2 = players[1].x - SCENE_LAYER_STREAM_X[l];
        const float dz2 = players[1].z - SCENE_LAYER_STREAM_Z[l];
        const float e2 = dx2 * dx2 + dz2 * dz2;
        if (e2 < d2) d2 = e2;
      }
      const float rOut = r * 1.15F + 8.0F;
      if (!layerAutoInside[l] && d2 < r * r) {
        layerAutoInside[l] = 1;
        layerRequest[l] = 1;
      } else if (layerAutoInside[l] && d2 > rOut * rOut) {
        layerAutoInside[l] = 0;
        layerRequest[l] = 0;
      }
    }
  }

  bool changed = false;
  for (int l = 0; l < lc; ++l) {
    const signed char req = layerRequest[l];
    layerRequest[l] = -1;
    if (req < 0) continue;
    if (req != 0 && layerTarget[l] == 0) {
      layerTarget[l] = 1;
      layerState[l] = 1;  // loading - assets stream in below
      changed = true;
    } else if (req == 0 && layerTarget[l] != 0) {
      layerTarget[l] = 0;
      changed = true;
    }
  }

  if (changed) {
    // Unloads first: the layer's objects drop out this frame, then whatever
    // only they needed is freed (deactivate before free - no bag or particle
    // pool may still point at a freed texture).
    bool anyOut = false;
    for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
      // data.layer (the runtime copy) so spawned clones despawn with the
      // layer their template belongs to - authored objects read the same.
      const int l = runtimeObjects[i].data.layer;
      if (l >= 0 && l < lc && layerTarget[l] == 0 && runtimeObjects[i].active) {
        deactivateObject(i);
        anyOut = true;
      }
    }
    for (int l = 0; l < lc; ++l)
      if (layerTarget[l] == 0) layerState[l] = 0;
    applyLayerResidency();
    if (anyOut) buildParticles();  // drop the streamed-out emitters' pools
  }

  if (!streamQueue.empty()) {
    processOneStreamJob();  // one asset per frame - the streaming budget
    return;                 // activation starts once everything is resident
  }

  // All assets in: activate a few objects per frame until each loading
  // layer is complete (they pop in staggered instead of stalling a frame).
  bool anyLoading = false;
  for (int l = 0; l < lc; ++l) anyLoading |= (layerState[l] == 1);
  if (!anyLoading) return;
  int budget = 4;
  // Authored objects only: spawn-pool slots activate through spawnObjectAt.
  for (int i = 0; i < SCENE_OBJECT_COUNT && budget > 0; ++i) {
    const int l = SCENE_OBJECTS[i].layer;
    if (l < 0 || l >= lc || layerState[l] != 1 || runtimeObjects[i].active)
      continue;
    activateObject(i);
    --budget;
  }
  bool completed = false;
  for (int l = 0; l < lc; ++l) {
    if (layerState[l] != 1) continue;
    bool pending = false;
    for (int i = 0; i < SCENE_OBJECT_COUNT; ++i)
      if (SCENE_OBJECTS[i].layer == l && !runtimeObjects[i].active)
        pending = true;
    if (!pending) {
      layerState[l] = 2;
      completed = true;
    }
  }
  if (completed) buildParticles();  // pools for the streamed-in emitters
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
  g.animLastTick = 0;  // fresh instance skins on its first in-view frame
  // type 5 = animated Model, type 6 = a third-person Player avatar (same path)
  if ((o.data.type != 5 && o.data.type != 6) || o.data.animModel < 0 ||
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
    // material albedo (glTF baseColorFactor). The lit VU1 programs ignore
    // this single color - it rides in the per-part light colors below - but
    // it is kept in sync for any single-color path that may read it.
    const float* base = gam.src->parts[m].color;
    mat->ambient.set(base[0] * 128.0F, base[1] * 128.0F, base[2] * 128.0F,
                     128.0F);
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
    // Fold this part's material albedo into its light colors so the lit VU1
    // program renders the .glb material color (outputColor = albedo * light),
    // not the plain scene light color (gray). Scene light/ambient here mirror
    // updateAndRenderAnimObjects; directions stay shared (animLightDirs).
    {
      const float* base = gam.src->parts[m].color;
      const float amb = 128.0F * SCENE_BRIGHTNESS * SCENE_AMBIENT;
      const float dif = 128.0F * SCENE_BRIGHTNESS * SCENE_DIFFUSE;
      ap.litColors[0].set(dif * SCENE_LIGHT_COL_R * base[0],
                          dif * SCENE_LIGHT_COL_G * base[1],
                          dif * SCENE_LIGHT_COL_B * base[2], 1.0F);
      ap.litColors[1].set(0.0F, 0.0F, 0.0F, 1.0F);
      ap.litColors[2].set(0.0F, 0.0F, 0.0F, 1.0F);
      ap.litColors[3].set(amb * base[0], amb * base[1], amb * base[2], 128.0F);
      ap.animLights = std::make_unique<PipelineDirLightsBag>(true);
      ap.animLights->setLightsManually(ap.litColors, animLightDirs);
      ap.lightBag->dirLights = ap.animLights.get();
    }
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
//    the ambient-prop / enemy-pack case) share one skinned mesh - the
//    nearest one skins, the rest re-point their bags at its arrays;
//  - with ANIM_LOD_DISTANCE set (Preferences > Rendering), far instances
//    refresh their pose every 2nd frame and every 4th beyond twice the
//    distance, staggered per object; playback time is unaffected and a
//    just-(re)appeared instance always skins immediately;
//  - with MESH_LOD_DISTANCE set, far instances render the decimated
//    variants baked into the .tskl (~50% verts, ~25% beyond twice the
//    distance) - less skinning, packing, clipping and VU1 per instance;
//  - the skinned arrays render through the SAME static pipeline as the rest
//    of the scene: one submission per vertex (DynPip uploads every vertex
//    twice for its from/to lerp), no VU1 program swap mid-frame, and the
//    EE clipper handles screen-edge crossers like all other geometry.
// In-view instances draw nearest-first: the front-to-back order lets the GS
// z-reject overdraw and makes each pose group's mesh owner its closest
// on-screen member (the LOD refresh rate follows the closest copy).
// One directional light matches the baked static lighting (point lights are
// baked into static vertex colors and cannot follow animated meshes).
void TerrainGame::updateAndRenderAnimObjects() {
  if (gameAnimModels.empty()) return;
  bool any = false;
  for (int i = 0; i < (int)runtimeObjects.size() && !any; ++i)
    any = runtimeObjects[i].active && runtimeObjects[i].visible &&
          objectGeometry[i].animInst != nullptr;
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

  // pass 1: playback bookkeeping for every instance; collect the in-view
  // ones with their camera distance
  struct VisibleAnim {
    int obj;
    float dist2;
  };
  static std::vector<VisibleAnim> inView;
  inView.clear();

  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    RuntimeObject& o = runtimeObjects[i];
    ObjectGeometry& g = objectGeometry[i];
    SkelInstance* inst = g.animInst.get();
    if (!inst || !o.active || !o.visible) continue;
    const GameAnimModel& gam = gameAnimModels[o.data.animModel];

    if (o.animClip < 0 || o.animClip >= (int)gam.src->clips.size())
      o.animClip = 0;
    // Playback bookkeeping runs once per FRAME, not once per render pass -
    // the split screen's second half re-renders the same instant.
    if (!splitSecondPass) {
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
      const float step =
          (o.animPlaying && !g_gameplayPaused) ? g_frameDt * o.animSpeed : 0.0F;
      o.animFinished = inst->advance(step);
    }

    // draw-distance cut-off (same rule as the static path in renderScene);
    // the distance doubles as the LOD tier and the draw-order key below
    const float dx = o.data.position[0] - cameraPosition.x;
    const float dy = o.data.position[1] - cameraPosition.y;
    const float dz = o.data.position[2] - cameraPosition.z;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    if (o.data.drawDistance > 0.0F &&
        dist2 > o.data.drawDistance * o.data.drawDistance)
      continue;

    // model matrix straight from the object data: T * R(X,Y,Z) * Ryaw * S -
    // the same transform the static path bakes through pushVert()/rotated(),
    // plus the content-forward correction (modelYaw) as a pre-rotation
    // around the model's own Y, so an X-forward-authored mesh renders
    // turned while the LOGIC yaw (walker faceYaw, AI turn-to-face, authored
    // rotation) stays convention-pure.
    V3 sx = {o.data.scale[0], 0.0F, 0.0F};
    V3 sy = {0.0F, o.data.scale[1], 0.0F};
    V3 sz = {0.0F, 0.0F, o.data.scale[2]};
    if (o.data.modelYaw != 0.0F) {
      const float ya = o.data.modelYaw * (PI / 180.0F);
      const float yc = cosf(ya), ys = sinf(ya);
      auto preYaw = [&](const V3& v) {
        return V3{v.x * yc + v.z * ys, v.y, -v.x * ys + v.z * yc};
      };
      sx = preYaw(sx);
      sy = preYaw(sy);
      sz = preYaw(sz);
    }
    const V3 bx = rotated(sx, o.data.rotation);
    const V3 by = rotated(sy, o.data.rotation);
    const V3 bz = rotated(sz, o.data.rotation);
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

    inView.push_back({i, dist2});
  }
  if (inView.empty()) return;
  std::sort(inView.begin(), inView.end(),
            [](const VisibleAnim& a, const VisibleAnim& b) {
              return a.dist2 < b.dist2;
            });
  if (!splitSecondPass) ++animLodTick;  // frame counter, not pass counter

  // pass 2, nearest first: group equal poses per mesh-LOD tier, gate far
  // skins, submit
  struct RenderedAnim {
    int obj;
    int meshOwner;
    u8 meshLod;
  };
  static std::vector<RenderedAnim> rendered;
  rendered.clear();

  for (const VisibleAnim& va : inView) {
    const int i = va.obj;
    RuntimeObject& o = runtimeObjects[i];
    ObjectGeometry& g = objectGeometry[i];
    SkelInstance* inst = g.animInst.get();

    // mesh LOD tier: which baked variant this instance renders (the .tskl
    // clamps per part - a file without chains always renders the full mesh).
    // Per-object override first: -1 = project preference, 0 = never LOD,
    // > 0 = this object's own distance (each Player object - P1 and P2 of a
    // two-player scene - carries its own).
    const float meshLodDist =
        o.data.meshLod < 0.0F ? MESH_LOD_DISTANCE : o.data.meshLod;
    u8 meshLod = 0;
    if (meshLodDist > 0.0F) {
      const float m2 = meshLodDist * meshLodDist;
      meshLod = va.dist2 > m2 * 4.0F ? 2 : va.dist2 > m2 ? 1 : 0;
    }

    // pose sharing: follow an already-rendered instance in the same pose
    // AND the same tier (each tier holds its own skinned buffers)
    int meshOwner = i;
    for (const RenderedAnim& r : rendered) {
      if (r.meshLod != meshLod) continue;
      if (runtimeObjects[r.obj].data.animModel != o.data.animModel) continue;
      if (!inst->poseEquals(*objectGeometry[r.obj].animInst)) continue;
      meshOwner = r.meshOwner;
      break;
    }

    // animation LOD: a far mesh owner refreshes its pose every 2nd frame
    // (every 4th beyond twice the distance), staggered by object index. An
    // instance that just (re)entered the view skins immediately - its held
    // pose could be arbitrarily stale. The split screen's second half never
    // re-skins (same instant, already skinned) - unless this half's camera
    // distance lands the instance in a different mesh-LOD tier, which the
    // tier-switch check below still forces into that tier's buffers.
    bool allowSkin = !splitSecondPass;
    const float animLodDist =
        o.data.animLod < 0.0F ? ANIM_LOD_DISTANCE : o.data.animLod;
    if (allowSkin && animLodDist > 0.0F && meshOwner == i &&
        g.animLastTick != 0 && animLodTick - g.animLastTick <= 4) {
      const float lod2 = animLodDist * animLodDist;
      if (va.dist2 > lod2 * 4.0F)
        allowSkin = ((animLodTick + (u32)i) & 3) == 0;
      else if (va.dist2 > lod2)
        allowSkin = ((animLodTick + (u32)i) & 1) == 0;
    }
    g.animLastTick = animLodTick;

    ObjectGeometry& owner = objectGeometry[meshOwner];
    SkelInstance* ownerInst = owner.animInst.get();
    // a tier switch always re-skins (the other tier's buffers hold an older
    // skin, or the bind pose before their first ever use)
    const bool reskinned =
        meshOwner == i && (allowSkin || inst->currentLod() != meshLod)
            ? inst->ensurePose(meshLod)
            : false;
    for (size_t p = 0; p < g.animParts.size(); ++p) {
      ObjectGeometry::AnimPart& ap = g.animParts[p];
      if (!ap.bag) continue;
      // bags may point at another frame's group leader or tier - re-aim
      const SkelInstance::LodArrays la = ownerInst->lodArrays(p, meshLod);
      ap.bag->vertices = la.vertices;
      ap.bag->count = la.count;
      ap.lightBag->normals = la.normals;
      if (ap.texBag) ap.texBag->coordinates = la.textureCoords;
      if (meshOwner == i) {
        if (reskinned) ap.bag->bboxVersion = ++g_bboxStamp;  // skinned in place
      } else {
        // identical pointer + version = followers reuse the owner's cached
        // frustum boxes instead of recomputing them per instance
        ap.bag->bboxVersion = owner.animParts[p].bag->bboxVersion;
      }
      stapip.core.render(ap.bag.get());
    }
    rendered.push_back({i, meshOwner, meshLod});
  }
}

// Minimum gap kept between the camera eye and any surface overhead. Must
// stay larger than the near clip distance (0.15, see clipMargin in init())
// or looking up at a ceiling the head touches would open a see-through hole.
constexpr float EYE_CLEARANCE = 0.2F;

// Shared player-vs-scene collision (both walkers). Box mode reproduces the
// classic behavior (XZ box + stand-on-top + step up 0.5), with models sized
// by their real mesh AABB instead of the unit scale box. Mesh mode collides
// with the model's triangles in object-local space: a downward ray finds the
// walkable ground (ramps/stairs work) and steep faces push the player out
// like walls. Rotation is honored in mesh mode and ignored in box mode.
// ceiling collects the lowest surface overhead (box undersides, mesh hits of
// an upward ray) so the walkers can clamp jumps below it.
void TerrainGame::collidePlayer(float prevX, float prevZ, float* nextX,
                                float* nextZ, float feetY, float eyeHeight,
                                float* ground, float* ceiling) {
  const float playerRadius = 0.35F;
  for (int oi = 0; oi < (int)runtimeObjects.size(); ++oi) {
    const RuntimeObject& o = runtimeObjects[oi];
    // The carried object rides in front of the face - letting it block its
    // own carrier would wedge the player against thin air.
    if (oi == carryIndex) continue;
    if (!o.active || !o.visible || o.data.type == 4 || o.data.type == 6 ||
        o.data.type == 7 || o.data.type == 8 || o.data.type == 9 ||
        o.data.type == 11 || o.data.type == 13 ||  // 13 = decal (visual only)
        o.data.type == 14)                         // 14 = camera marker
      continue;
    if (o.data.collision == 2) continue;  // none
    // Portal pass-through (updatePortalPass): while the walker stands in a
    // linked portal's opening, objects fully behind that portal's plane
    // stop colliding - the mounting wall becomes a doorway. Exact OBB
    // extent along the plane normal, same math as the view dead zone.
    if (portalPassOn) {
      const V3 pax = rotated({1.0F, 0.0F, 0.0F}, o.data.rotation);
      const V3 pay = rotated({0.0F, 1.0F, 0.0F}, o.data.rotation);
      const V3 paz = rotated({0.0F, 0.0F, 1.0F}, o.data.rotation);
      const float r =
          fabsf(portalPassPlane[0] * pax.x + portalPassPlane[1] * pax.y +
                portalPassPlane[2] * pax.z) *
              0.5F * o.data.scale[0] +
          fabsf(portalPassPlane[0] * pay.x + portalPassPlane[1] * pay.y +
                portalPassPlane[2] * pay.z) *
              0.5F * o.data.scale[1] +
          fabsf(portalPassPlane[0] * paz.x + portalPassPlane[1] * paz.y +
                portalPassPlane[2] * paz.z) *
              0.5F * o.data.scale[2];
      const float sd = portalPassPlane[0] * o.data.position[0] +
                       portalPassPlane[1] * o.data.position[1] +
                       portalPassPlane[2] * o.data.position[2] -
                       portalPassPlane[3];
      if (sd < -r + 0.1F) continue;
    }

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
      // is approximated by the average XZ scale - exact for uniform scales).
      // "Steep" is judged in WORLD space: a tumbled physics model lying on
      // its side has world-walls whose LOCAL normal reads as a walkable
      // floor - so world-up rides into mesh space with the query.
      const V3 c = toLocal(*nextX, feetY + eyeHeight * 0.5F, *nextZ);
      Vec4 center(c.x, c.y, c.z, 1.0F);
      const float sAvg = (sx + sz) * 0.5F;
      const V3 upL = invRotated({0.0F, 1.0F, 0.0F}, o.data.rotation);
      // prev rides along so the push is side-aware: a step longer than the
      // radius that lands past a wall's plane is ejected BACK to the side
      // the player came from - the plain two-sided push pointed inward
      // there and sucked fast walkers inside the mesh.
      const V3 pc = toLocal(prevX, feetY + eyeHeight * 0.5F, prevZ);
      const Vec4 prevLocal(pc.x, pc.y, pc.z, 1.0F);
      if (gm->collider.resolveSphere(&center, playerRadius / sAvg, 0.7F,
                                     Vec4(upL.x, upL.y, upL.z, 0.0F),
                                     &prevLocal)) {
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

      // ceiling: a ray from step height straight up past the head finds the
      // underside of overhead geometry (door lintels, floors jumped against)
      const V3 co = toLocal(*nextX, feetY + 0.5F, *nextZ);
      const V3 cq =
          toLocal(*nextX, feetY + eyeHeight + EYE_CLEARANCE + 1.0F, *nextZ);
      V3 cd = {cq.x - co.x, cq.y - co.y, cq.z - co.z};
      const float cl = sqrtf(cd.x * cd.x + cd.y * cd.y + cd.z * cd.z);
      if (cl > 0.0001F) {
        cd.x /= cl, cd.y /= cl, cd.z /= cl;
        float t;
        if (gm->collider.raycast(Vec4(co.x, co.y, co.z, 1.0F),
                                 Vec4(cd.x, cd.y, cd.z, 0.0F), cl, &t)) {
          const V3 hit =
              toWorld({co.x + cd.x * t, co.y + cd.y * t, co.z + cd.z * t});
          if (hit.y < *ceiling) *ceiling = hit.y;
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
    float ex = 0.5F * o.data.scale[0], ey = 0.5F * o.data.scale[1],
          ez = 0.5F * o.data.scale[2];
    V3 localCenter = {0.0F, 0.0F, 0.0F};  // box center in the object's own frame
    const float* mn = gm ? gm->mn : (anim ? anim->min : nullptr);
    const float* mx = gm ? gm->mx : (anim ? anim->max : nullptr);
    if (mn && mx) {
      localCenter = {0.5F * (mn[0] + mx[0]) * o.data.scale[0],
                     0.5F * (mn[1] + mx[1]) * o.data.scale[1],
                     0.5F * (mn[2] + mx[2]) * o.data.scale[2]};
      ex = 0.5F * (mx[0] - mn[0]) * o.data.scale[0];
      ey = 0.5F * (mx[1] - mn[1]) * o.data.scale[1];
      ez = 0.5F * (mx[2] - mn[2]) * o.data.scale[2];
    }
    // World box center: the model-AABB offset lives in the object's own frame,
    // so it rotates with the object (a primitive's offset is 0, so this is
    // just its position).
    const V3 cWorld = rotated(localCenter, o.data.rotation);
    const float cx = o.data.position[0] + cWorld.x;
    const float cy = o.data.position[1] + cWorld.y;
    const float cz = o.data.position[2] + cWorld.z;
    const float hx = ex + playerRadius;
    const float hz = ez + playerRadius;
    const float top = cy + ey;
    const float bottom = cy - ey;

    // Footprint test in the box's OWN horizontal frame, so a yaw-rotated box
    // blocks along its real (rotated) faces instead of an axis-aligned bound
    // that juts into empty space at the corners. Vertical (top/bottom) stays
    // world-space: a yaw does not tilt the box, and box mode does not model a
    // tilted top - a pitched/rolled box still collides upright, as before
    // (mesh collision is the escape hatch for those). Reduces exactly to the
    // old AABB test when the object is unrotated.
    //
    // The frame is YAW ONLY, never the full 3D rotation: physics bodies
    // tumble (spin writes pitch/roll into rotation), and projecting a pitched
    // frame onto XZ is not an isometry - part of the horizontal offset
    // escapes into local Y and gets dropped, so far-away points read as
    // "inside" and the back-projection contracts the committed position
    // toward the box center (the player teleported into thrown objects and
    // stuck inside them).
    const float yaw = o.data.rotation[1] * PI / 180.0F;
    const float yawC = cosf(yaw), yawS = sinf(yaw);
    auto toLocalXZ = [&](float wx, float wz, float& lx, float& lz) {
      const float dx = wx - cx, dz = wz - cz;
      lx = dx * yawC - dz * yawS;
      lz = dx * yawS + dz * yawC;
    };
    float lnx, lnz, lpx, lpz;
    toLocalXZ(*nextX, *nextZ, lnx, lnz);
    toLocalXZ(prevX, prevZ, lpx, lpz);

    const bool nextInside = lnx > -hx && lnx < hx && lnz > -hz && lnz < hz;
    if (!nextInside) continue;

    const bool wasInsideX = lpx > -hx && lpx < hx;
    const bool wasInsideZ = lpz > -hz && lpz < hz;
    // Re-projects a (possibly axis-cancelled) local target back to world.
    auto commitLocal = [&](float lx, float lz) {
      *nextX = cx + lx * yawC + lz * yawS;
      *nextZ = cz - lx * yawS + lz * yawC;
    };

    if (feetY + 0.5F >= top) {
      // low enough to walk onto - candidate floor
      if (top > *ground) *ground = top;
    } else if (bottom >= feetY + eyeHeight) {
      // box entirely above the head - overhead surface for the jump clamp
      if (bottom < *ceiling) *ceiling = bottom;
      if (bottom < feetY + eyeHeight + EYE_CLEARANCE) {
        // walking under would leave the eye closer than the clip plane -
        // block, unless the player is already under it (let them walk out)
        if (!wasInsideX || !wasInsideZ)
          commitLocal(wasInsideX ? lnx : lpx, wasInsideZ ? lnz : lpz);
      }
    } else if (feetY < top) {
      // vertical overlap: also the lowest surface overhead when the box sank
      // onto the player (moving platforms) - lets the clamp push them out
      if (bottom >= feetY && bottom < *ceiling) *ceiling = bottom;
      // blocked - cancel the local axes that entered the box this frame, so
      // the residual slide runs along the (rotated) wall. Already inside on
      // both axes = a full stop.
      if (wasInsideX && wasInsideZ)
        commitLocal(lpx, lpz);
      else
        commitLocal(wasInsideX ? lnx : lpx, wasInsideZ ? lnz : lpz);
    }
  }
}

// Last volume/pan sent to each emitter channel (16-23). audsrv RPCs are
// synchronous and share one client lock with the music stream, so
// updateSoundEmitters only issues an RPC when the quantized value changes.
static int sndChVol[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static int sndChPan[8] = {-999, -999, -999, -999, -999, -999, -999, -999};

// Switches the runtime state to a scene from scene_data.hpp and settles the
// asset residency for it: everything the scene's start-resident layers need
// loads synchronously here (the switch hides behind the loading screen),
// assets no resident layer uses any more - the previous scene's included -
// are freed. Runtime objects are rebuilt; vectors and per-object bags are
// reused/freed here, nothing leaks.
void TerrainGame::loadScene(int sceneIndex) {
  if (sceneIndex < 0 || sceneIndex >= SCENE_COUNT) return;
  currentScene = sceneIndex;
  g_activeScene = sceneIndex;
  sceneGeneration++;  // scene scripts see this and reset their state
  carryIndex = thrownIndex = -1;  // carried objects stay in their old scene
  carryPortalPi = -1;
  thrownFreeIndex = -1;           // and so does the portal-free latch
  portalLiveFlags.clear(); // stale through-views must not survive the switch
  portalPrevPos.clear();   // crossing history restarts with the new objects
  // Before any mesh baking: terrain chunks and object geometry shade with
  // this scene's point lights (see collectScenePointLights).
  collectScenePointLights();

  // Size the terrain chunk pool for this scene's grid up front (independent
  // of the streamed assets below) so the loading bar's denominator can count
  // the in-view chunks that still have to be built.
  if (infoBag) resetTerrainChunks();
  // Terrain view focus, same source the synchronous drain used to use (the
  // player entity's authored position, else the orbit look-at).
  const float lsFocusX = (PLAYER_INDEX >= 0 && PLAYER_INDEX < SCENE_OBJECT_COUNT)
                             ? SCENE_OBJECTS[PLAYER_INDEX].position[0]
                             : cameraLookAt.x;
  const float lsFocusZ = (PLAYER_INDEX >= 0 && PLAYER_INDEX < SCENE_OBJECT_COUNT)
                             ? SCENE_OBJECTS[PLAYER_INDEX].position[2]
                             : cameraLookAt.z;

  // Loading-screen progress pump. The load is otherwise blocking, so we count
  // the work units (assets to stream + objects to build + terrain chunks) and
  // present a loading-screen frame every ~1/24th of the way through, so the
  // bar reflects real progress. lsStep caps presented frames (rendering every
  // unit would slow chunk-heavy loads). With LOADING_SCREEN off, lsPump is a
  // no-op and the load behaves exactly as before.
  int lsDone = 0, lsMark = 0, lsTotal = 0, lsStep = 1;
  auto lsPump = [&](int n) {
    if (!LOADING_SCREEN) return;
    lsDone += n;
    if (lsDone >= lsMark) {
      lsMark += lsStep;
      loadingscreen::renderFrame(
          engine, sceneIndex, lsTotal > 0 ? (float)lsDone / (float)lsTotal : 1.0F);
    }
  };

  // Streaming layers: desired residency from the scene's authored defaults.
  {
    // The previous scene's runtime objects (spawn-pool clones included) are
    // gone from here on - drop their active flags BEFORE the residency math
    // so no stale clone pins the old scene's assets under new-scene indices.
    for (RuntimeObject& o : runtimeObjects) o.active = false;
    const int lc = SCENE_LAYER_COUNT;
    layerTarget.assign(lc > 0 ? lc : 0, 0);
    layerState.assign(lc > 0 ? lc : 0, 0);
    layerRequest.assign(lc > 0 ? lc : 0, -1);
    layerAutoInside.assign(lc > 0 ? lc : 0, 0);
    // Auto-streamed layers start resident only when the spawn point is
    // inside their zone; everything else follows the authored Start loaded.
    float spawnX = 0.0F, spawnZ = 0.0F;
    if (PLAYER_INDEX >= 0) {
      spawnX = SCENE_OBJECTS[PLAYER_INDEX].position[0];
      spawnZ = SCENE_OBJECTS[PLAYER_INDEX].position[2];
    } else {
      for (int i = 0; i < SCENE_OBJECT_COUNT; ++i)
        if (SCENE_OBJECTS[i].type == 4) {  // spawn point (built-in FPP)
          spawnX = SCENE_OBJECTS[i].position[0];
          spawnZ = SCENE_OBJECTS[i].position[2];
          break;
        }
    }
    for (int l = 0; l < lc; ++l) {
      const float r = SCENE_LAYER_STREAM_R[l];
      if (r > 0.0F) {
        const float dx = spawnX - SCENE_LAYER_STREAM_X[l];
        const float dz = spawnZ - SCENE_LAYER_STREAM_Z[l];
        const bool inside = dx * dx + dz * dz < r * r;
        layerTarget[l] = inside ? 1 : 0;
        layerAutoInside[l] = inside ? 1 : 0;
      } else {
        layerTarget[l] = SCENE_LAYER_START[l] ? 1 : 0;
      }
    }
    applyLayerResidency();
    // Now the work is known: assets queued + objects to build + chunks in view.
    if (LOADING_SCREEN) {
      lsTotal = (int)streamQueue.size() + SCENE_OBJECT_COUNT +
                countPendingChunks(lsFocusX, lsFocusZ);
      lsStep = lsTotal > 0 ? (lsTotal + 23) / 24 : 1;
      loadingscreen::renderFrame(engine, sceneIndex, 0.0F);  // first frame
    }
    while (!streamQueue.empty()) {
      processOneStreamJob();
      lsPump(1);
    }
    for (int l = 0; l < lc; ++l) layerState[l] = layerTarget[l] ? 2 : 0;
  }

  // Terrain, lighting, sky, clipping and post-FX are per scene (Scene >
  // Preferences overrides) - re-apply render settings (the chunk pool was
  // reset at the top; the chunks themselves drain at the end of this
  // function, once the view focus is known).
  if (infoBag) {
    infoBag->fullClipChecks = CLIP_PRECISE;
    skyHorizonR = SKY_R, skyHorizonG = SKY_G, skyHorizonB = SKY_B;
    buildSkyDome();
  }
  // Per-scene clipping override may flip the hidden VU1 clipping mode.
  stapip.core.setVU1Clipping(CLIP_VU1);
  // Per-scene sky color (the loop paints the clear screen from ctx.skyColor)
  // and post effects.
  scriptCtx.skyColor = Color(SKY_R, SKY_G, SKY_B);
  engine->renderer.setClearScreenColor(scriptCtx.skyColor);
  engine->renderer.core.postFx.setBloom(POSTFX_BLOOM);
  engine->renderer.core.postFx.setGrain(POSTFX_GRAIN);
  engine->renderer.core.postFx.setDepthOfField(POSTFX_DOF_FOCUS,
                                               POSTFX_DOF_RANGE, POSTFX_DOF);
  // GS hardware distance fog (Scene/Project > Preferences > Fog).
  if (FOG_ENABLED)
    engine->renderer.core.setFog(Color(FOG_R, FOG_G, FOG_B), FOG_START,
                                 FOG_END);
  else
    engine->renderer.core.disableFog();

  // Camera flashlight is a Player property: reset the runtime master to this
  // scene's player Enabled flag (the flow graph can change it later); the
  // on/off toggle starts on.
  g_flashEnabled = FLASHLIGHT_ENABLED;
  g_flashOn = true;

  // Authored objects + the dynamic spawn pool (Spawn Object flow node).
  runtimeObjects.assign(SCENE_OBJECT_COUNT + MAX_SPAWNED_OBJECTS,
                        RuntimeObject());
  objectGeometry.clear();
  objectGeometry.resize(SCENE_OBJECT_COUNT + MAX_SPAWNED_OBJECTS);
  // Pool slots start empty - data arrives from a template at spawn time.
  for (int i = SCENE_OBJECT_COUNT; i < (int)runtimeObjects.size(); ++i) {
    runtimeObjects[i].active = false;
    runtimeObjects[i].visible = false;
    runtimeObjects[i].dirty = false;
  }
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    runtimeObjects[i].data = SCENE_OBJECTS[i];
    // spawn points and the player are editor markers, not geometry; emitters
    // honor their Enabled flag (Show/Hide Object flips visible at runtime)
    runtimeObjects[i].visible =
        SCENE_OBJECTS[i].type != 4 && SCENE_OBJECTS[i].type != 6 &&
        !(SCENE_OBJECTS[i].type == 7 && !SCENE_OBJECTS[i].emitEnabled);
    runtimeObjects[i].dirty = true;
    // objects of layers that don't start resident wait for Load Layer
    runtimeObjects[i].active = layerOn(SCENE_OBJECTS[i].layer);
    if (!runtimeObjects[i].active) {
      runtimeObjects[i].visible = false;
      runtimeObjects[i].dirty = false;
    }
    lsPump(1);
  }
  // Animated models: fresh per-object mesh instances + playback defaults
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i)
    if (runtimeObjects[i].active) setupAnimObject(i);

  // Static batching: group the batchStatic-flagged objects (material x
  // coarse world cell). The always-resident assets - materials included -
  // streamed in above, so the reflective-material opt-out can decide here;
  // the batches themselves bake lazily on the first renderScene.
  buildStaticBatchList();

  scriptCtx.objects = runtimeObjects.data();
  scriptCtx.objectCount = (int)runtimeObjects.size();
  scriptCtx.scene = currentScene;
  scriptCtx.sceneGeneration = sceneGeneration;
  scriptCtx.layerState = layerState.data();
  scriptCtx.layerRequest = layerRequest.data();
  scriptCtx.layerCount = (int)layerState.size();
  scriptCtx.usedObject = -1;
  useTargetIndex = -1;

  // Player entity start state for this scene. players[0] = the first Player
  // object (P1), players[1] = the second (P2 of the two-player modes; -1
  // when the scene has only one). Player 2 stays active across scene
  // switches as long as the new scene can host it.
  if (playerTwoActive && PLAYER2_INDEX < 0) {
    playerTwoActive = false;
    syncPlayerCountMenuValue();
  }
  sharedBoom = 0.0F;
  for (int pi = 0; pi < 2; ++pi) {
    PlayerCtl& P = players[pi];
    P.objIndex = PP_INDEX(pi) < SCENE_OBJECT_COUNT ? PP_INDEX(pi) : -1;
    if (P.objIndex < 0) continue;
    P.x = SCENE_OBJECTS[P.objIndex].position[0];
    P.z = SCENE_OBJECTS[P.objIndex].position[2];
    P.y = PP_MODE(pi) == 1 ? SCENE_OBJECTS[P.objIndex].position[1]
                           : terrainHeightAt(P.x, P.z);
    P.yaw = SCENE_OBJECTS[P.objIndex].rotation[1] * PI / 180.0F;
    P.velY = 0.0F;
    P.pitch = 0.0F;
    // Third person: the avatar starts facing its authored yaw and its
    // locomotion clip names resolve to the model's clip indices. The Player
    // object is a rendered avatar only in this mode - in FPP/noclip its model
    // (if any) is never built, so its runtime object stays invisible here.
    // The P2 avatar shows only while player 2 is actually in the game.
    P.faceYaw = P.yaw;
    P.boom = PP_CAM_DIST(pi);  // start fully extended, not easing out from 0
    runtimeObjects[P.objIndex].visible =
        PP_MODE(pi) == 2 && (pi == 0 || playerTwoActive);
    if (PP_MODE(pi) == 2) {
      // Idle/walk fall back to the model's first clip when unset; run/jump are
      // optional, so an empty name stays unmapped (-1) instead of clip 0.
      P.idleClip = resolveClipIndex(P.objIndex, PP_IDLE_CLIP(pi));
      P.walkClip = resolveClipIndex(P.objIndex, PP_WALK_CLIP(pi));
      P.runClip =
          PP_RUN_CLIP(pi)[0] ? resolveClipIndex(P.objIndex, PP_RUN_CLIP(pi)) : -1;
      P.jumpClip =
          PP_JUMP_CLIP(pi)[0] ? resolveClipIndex(P.objIndex, PP_JUMP_CLIP(pi)) : -1;
      // Start ON the idle clip so drivePlayerAnim recognizes it as a locomotion
      // pose from frame one. Without this, setupAnimObject's default (clip 0)
      // would look like a scripted one-shot when idle isn't clip 0, and a
      // looping clip 0 would wedge locomotion off (animFinished never fires).
      if (P.idleClip >= 0 && objectGeometry[P.objIndex].animInst) {
        RuntimeObject& body = runtimeObjects[P.objIndex];
        body.animClip = P.idleClip;
        body.animLoop = true;
        body.animPlaying = true;
        body.animRestart = true;
      }
    }
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

  // Terrain: build every chunk in view of the start focus. From here on
  // renderScene streams the ring. With no loading screen this is the old
  // one-shot drain; with one, the chunks build in lsStep-sized batches so the
  // bar can advance between them (breaking if the pool momentarily can't make
  // progress, which the view-rect-sized pool should never hit at load time).
  if (!LOADING_SCREEN) {
    updateTerrainChunks(lsFocusX, lsFocusZ, 0.0F, 0.0F, false, 0x7FFFFFFF);
  } else {
    int pending = countPendingChunks(lsFocusX, lsFocusZ);
    while (pending > 0) {
      updateTerrainChunks(lsFocusX, lsFocusZ, 0.0F, 0.0F, false, lsStep);
      const int now = countPendingChunks(lsFocusX, lsFocusZ);
      if (now >= pending) break;  // no forward progress (pool cap) - bail out
      lsPump(pending - now);
      pending = now;
    }
    loadingscreen::renderFrame(engine, sceneIndex, 1.0F);  // final frame
  }
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
    if (!o.active || !o.visible) {
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
    // Master SFX volume (menu "Sound volume" option block); 100 = unscaled.
    vol = vol * scriptCtx.sfxVolume / 100;
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
// no trig and no allocations in the per-frame path. The EE simulates and
// writes one center + one qword of 2x2 basis weights + one color per
// particle; the VU1 billboard program (engine StaPipBillboardBag) expands
// each center into a camera-facing quad in clip space, so the quad
// building cost never touches the EE. An emitter with a material carrying
// a map_Kd draws its quads with that texture (the color then modulates
// it; corner UVs are fixed on VU1).
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
    if (!runtimeObjects[i].active) continue;  // emitter streamed out - no pool
    ParticleSystem ps;
    ps.objectIndex = i;
    ps.rng = 12345u + (unsigned int)i * 7919u;
    int n = runtimeObjects[i].data.emitCount;  // data copy: spawn slots too
    if (n < 1) n = 1;
    if (n > 256) n = 256;
    ps.pos.assign(n, Vec4(0.0F, 0.0F, 0.0F, 1.0F));
    ps.vel.assign(n, Vec4(0.0F, 0.0F, 0.0F, 0.0F));
    ps.life.assign(n, 0.0F);  // dead -> staggered respawn over the first frames
    ps.maxLife.assign(n, 1.0F);
    ps.params.assign(n, Vec4(0.0F, 0.0F, 0.0F, 0.0F));
    ps.cols.assign(n, Color(0.0F, 0.0F, 0.0F, 0.0F));
    ps.infoBag = std::make_unique<StaPipInfoBag>();
    ps.infoBag->model = &model;
    ps.infoBag->shadingType = TyraShadingGouraud;
    // VU1 billboard bags cull per quad on VU1 - no EE frustum/clip work.
    // None is safe for billboard bags only: the VU1 program ADCs any quad
    // whose corner leaves the GS raster window (ordinary bags must never
    // use None - see the engine skill's wrap pitfall).
    ps.infoBag->frustumCulling = PipelineInfoBagFrustumCulling_None;
    ps.infoBag->fullClipChecks = false;
    ps.colorBag = std::make_unique<StaPipColorBag>();
    ps.colorBag->many = ps.cols.data();
    ps.billboardBag = std::make_unique<StaPipBillboardBag>();
    ps.bag = std::make_unique<StaPipBag>();
    ps.bag->info = ps.infoBag.get();
    ps.bag->color = ps.colorBag.get();
    ps.bag->lighting = nullptr;
    ps.bag->billboard = ps.billboardBag.get();
    ps.bag->vertices = ps.pos.data();
    ps.bag->count = 0;
    // The texture bag is mandatory for billboard bags - its coordinates
    // channel carries the per-particle basis weights. Textured particles:
    // the emitter's material supplies the map (its Kd is ignored - the
    // emitter color is the tint); corner UVs are fixed in the VU1 program.
    ps.texBag = std::make_unique<StaPipTextureBag>();
    ps.texBag->texture = nullptr;
    ps.texBag->coordinates = ps.params.data();
    const int mi = runtimeObjects[i].data.material;  // data copy: spawn slots too
    if (mi >= 0 && mi < (int)gameMaterials.size() && gameMaterials[mi].texture)
      ps.texBag->texture = gameMaterials[mi].texture;
    ps.bag->texture = ps.texBag.get();
    particles.push_back(std::move(ps));
  }
}

void TerrainGame::updateParticles() {
  if (particles.empty() || !g_particlesOn) return;  // Set Particles switch
  // Paused: leave every billboard bag exactly as it was last built - the
  // scene render still draws them, so particles hang frozen behind the menu.
  if (g_gameplayPaused) return;
  const float dt = g_frameDt;

  // camera right/up shared by every billboard basis this frame
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
    if (!o.active || !o.visible) {
      ps.bag->count = 0;  // Hide Object turns the emitter off
      continue;
    }
    const SceneObjectData& d = o.data;
    const int kind = d.emitKind;
    const int n = (int)ps.life.size();

    // Per-emitter billboard basis for the VU1 expansion. Rain streaks hang
    // from world-up (vertical quads); everything else faces the camera
    // plane. A portal pass re-renders these bags with the VIRTUAL camera's
    // basis swapped in - the centers below are view-independent.
    ps.billboardBag->right = Vec4(rx, 0.0F, rz, 0.0F);
    ps.billboardBag->up = kind == 4 ? Vec4(0.0F, 1.0F, 0.0F, 0.0F)
                                    : Vec4(ux, uy, uz, 0.0F);

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
        // density knob: Opacity 0..1 -> peak alpha 0..60 (old look ~= 0.3)
        alpha = d.emitOpacity * 60.0F *
                (t < 0.5F ? t * 2.0F : (1.0F - t) * 2.0F);  // fade in+out
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

      // The quad itself is built on VU1: corner = center +/- (right*m00 +
      // up*m01) +/- (right*m10 + up*m11). Rain streaks hang from world-up
      // (the emitter basis above) with an independent half-height; fog
      // puffs additionally swirl - the billboard slowly rotates in the
      // camera plane, alternating direction per puff (the swirling fog
      // roll) - keep in sync with the viewport preview
      // (drawEmitterPreviews).
      float m00 = size, m01 = 0.0F, m10 = 0.0F, m11 = size;
      if (kind == 2) {
        const float age = ps.maxLife[i] - ps.life[i];
        const float ang = (float)i * 2.4F + (i & 1 ? 0.3F : -0.3F) * age;
        const float ca = cosf(ang), sa = sinf(ang);
        m00 = ca * size;
        m01 = sa * size;
        m10 = -sa * size;
        m11 = ca * size;
      }
      if (sizeUp > 0.0F) m11 = sizeUp;  // rain: thin width, streak height
      ps.params[i] = Vec4(m00, m01, m10, m11);
      ps.cols[i] = Color(cr, cg, cb, alpha);
    }
    ps.bag->count = (u32)n;
  }
}
// Picks the nearest usable object the camera is close to and looking at
// (thresholds in controls.hpp). BTN_USE on it -> scriptCtx.usedObject for
// one frame, which fires the flow graph "On Used" trigger.
void TerrainGame::updateUseTarget() {
  useTargetIndex = -1;
  scriptCtx.usedObject = -1;
  // Hands full: BTN_USE means "drop" (updateCarriedObject), so no use
  // targeting - and no prompt - while carrying.
  if (carryIndex >= 0) return;

  Vec4 dir = cameraLookAt - cameraPosition;
  const float dirLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (dirLen < 0.0001F) return;
  dir.x /= dirLen, dir.y /= dirLen, dir.z /= dirLen;

  float bestDist = 0.0F;
  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    const RuntimeObject& o = runtimeObjects[i];
    if (!o.active || !(o.data.usable || o.data.pickable) || !o.visible) continue;
    if (o.data.type == 4 || o.data.type == 6 || o.data.type == 7 ||
        o.data.type == 8 || o.data.type == 9 || o.data.type == 11 ||
        o.data.type == 14)
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

  if (useTargetIndex >= 0 && engine->pad.getClicked().BTN_USE) {
    // A pickable object can also be usable: it fires On Used AND gets picked
    // up on the same press (a grab sound wired in the graph, for instance).
    if (runtimeObjects[useTargetIndex].data.usable)
      scriptCtx.usedObject = useTargetIndex;
    if (runtimeObjects[useTargetIndex].data.pickable) {
      carryIndex = useTargetIndex;
      carryGrabbed = true;  // don't read this same press as "drop"
      if (thrownIndex == carryIndex) thrownIndex = -1;  // caught mid-flight
      RuntimeObject& g = runtimeObjects[carryIndex];
      // Catching a body kills its momentum and its tumble (a crate caught
      // mid-flight must not keep spinning in your hands).
      g.velocityX = g.velocityY = g.velocityZ = 0.0F;
      g.spin[0] = g.spin[1] = g.spin[2] = 0.0F;
      // Seed the smoothed reach with the object's current distance: a close
      // grab reels out to full reach, a far one snaps in (the sweep rule).
      const float gx = g.data.position[0] - cameraPosition.x;
      const float gy = g.data.position[1] - cameraPosition.y;
      const float gz = g.data.position[2] - cameraPosition.z;
      carryDist = sqrtf(gx * gx + gy * gy + gz * gz);
    }
  }

  // Using a save point (type 10) opens the save menu next frame; the
  // "On Used" trigger still fires for its flow graph this frame.
  if (scriptCtx.usedObject >= 0 &&
      SCENE_OBJECTS[scriptCtx.usedObject].type == 10)
    scriptCtx.openSaveMenu = true;
}

// Largest half extent of an object's collision box (mesh/anim AABB when the
// object has one, else the unit scale box) - the sweep radius that keeps the
// whole object clear of walls while carried or in flight.
float TerrainGame::objectHalfExtent(const RuntimeObject& o) const {
  float ex = 0.5F * o.data.scale[0], ey = 0.5F * o.data.scale[1],
        ez = 0.5F * o.data.scale[2];
  const GameModel* gm = nullptr;
  if (o.data.type == 5 && o.data.model >= 0 &&
      o.data.model < (int)gameModels.size())
    gm = &gameModels[o.data.model];
  const SkelModel* anim = nullptr;
  if (o.data.type == 5 && o.data.animModel >= 0 &&
      o.data.animModel < (int)gameAnimModels.size())
    anim = gameAnimModels[o.data.animModel].src.get();
  const float* mn = gm ? gm->mn : (anim ? anim->min : nullptr);
  const float* mx = gm ? gm->mx : (anim ? anim->max : nullptr);
  if (mn && mx) {
    ex = 0.5F * (mx[0] - mn[0]) * o.data.scale[0];
    ey = 0.5F * (mx[1] - mn[1]) * o.data.scale[1];
    ez = 0.5F * (mx[2] - mn[2]) * o.data.scale[2];
  }
  float r = ex > ey ? ex : ey;
  if (ez > r) r = ez;
  if (r < 0.05F) r = 0.05F;
  return r;
}

// Carry whisker: the third-person spring arm's pre-block, turned around.
// The spring arm pulls the CAMERA in when the boom sweep hits a wall; here
// the same sweep pushes the WALKER back when the carried object no longer
// fits in front of the face - so pressing "face first" against a wall while
// carrying is simply blocked, instead of the object being parked inside the
// wall. Called by every walker after collidePlayer, on the carrying player
// only. The probe is horizontal (yaw only): with the look pitch in it, the
// terrain underfoot would read as a wall whenever the player looks down.
void TerrainGame::applyCarryWhisker(float* nextX, float* nextZ, float probeY,
                                    float yaw, float feetY, float eyeHeight) {
  if (carryIndex < 0) return;
  const RuntimeObject& o = runtimeObjects[carryIndex];
  if (!o.active || !o.visible) return;
  const float r = objectHalfExtent(o);
  const float need = 0.55F + r;  // the object fully outside the carrier
  const float hx = sinf(yaw), hz = cosf(yaw);
  // Walking a carried object INTO a portal opening must not read the
  // mounting wall as a blocker. portalPassOn (the player's body is IN the
  // opening, published by updatePortalPass and still live here) is
  // authoritative and takes precedence: once the player reaches the plane
  // the forward probe below no longer starts in FRONT of it, so on its own
  // the doorway would slam shut exactly at the crossing and the whisker
  // would bounce the player back out (owner: can't walk through a portal
  // while carrying). The forward probe still covers the approach, before
  // the body column enters the opening.
  if (portalPassOn) {
    sweepPassPlane[0] = portalPassPlane[0];
    sweepPassPlane[1] = portalPassPlane[1];
    sweepPassPlane[2] = portalPassPlane[2];
    sweepPassPlane[3] = portalPassPlane[3];
    sweepPassOn = true;
  } else {
    const float reach = need + r + 0.1F;
    const float a0[3] = {*nextX, probeY, *nextZ};
    const float b0[3] = {*nextX + hx * reach, probeY, *nextZ + hz * reach};
    armSweepPass(a0, b0);
  }
  const float d =
      sweepSphere(*nextX, probeY, *nextZ, hx, 0.0F, hz, need, r, carryIndex);
  sweepPassOn = false;
  if (d < need) {
    const float px = *nextX, pz = *nextZ;
    *nextX -= hx * (need - d);
    *nextZ -= hz * (need - d);
    // The pushback is a displacement collidePlayer never saw - unswept it
    // shoves the walker clean through whatever stands at their back (owner
    // repro: carry + reverse into a wall = teleported behind it). Re-run
    // the collision from the pre-push spot; ground/ceiling are discarded.
    float g2 = -1e30F, c2 = 1e30F;
    collidePlayer(px, pz, nextX, nextZ, feetY, eyeHeight, &g2, &c2);
  }
}

// Carried + thrown pickable objects. The carried one rides PICK_CARRY_DIST in
// front of the face, swept against the world each frame so it can neither be
// pushed through a wall nor parked behind one - blocked reach just brings it
// closer. It keeps colliding with the world (the sweep) but not with its
// carrier (collidePlayer/springArm skip it), so it cannot wedge the player.
// BTN_USE drops it in place - already a swept, legal spot - and BTN_THROW
// launches it when the object allows that.
void TerrainGame::updateCarriedObject() {
  // In-flight object: only objects WITHOUT a rigid body reach this path -
  // a thrown physics object is handed straight to updateObjectPhysics
  // (releaseCarried), which already sweeps, bounces, rolls and tumbles it.
  // Integrate under gravity, sweep each step, stop on hit.
  if (thrownIndex >= 0) {
    RuntimeObject& o = runtimeObjects[thrownIndex];
    if (!o.active || !o.visible) {
      thrownIndex = -1;
    } else {
      const float r = objectHalfExtent(o);
      thrownVel[1] -= GRAVITY * g_frameDt * g_frameDt;
      // Terminal fall (30 u/s real time, matching the rigid-body sim): a
      // throw that enters a portal infinite-fall loop keeps flying on this
      // path and would otherwise accelerate without bound.
      const float termFall = 30.0F * g_frameDt;
      if (thrownVel[1] < -termFall) thrownVel[1] = -termFall;
      const float stepLen =
          sqrtf(thrownVel[0] * thrownVel[0] + thrownVel[1] * thrownVel[1] +
                thrownVel[2] * thrownVel[2]);
      bool stop = stepLen < 0.0005F;
      bool hopped = false;
      const float prevT[3] = {o.data.position[0], o.data.position[1],
                              o.data.position[2]};
      if (!stop) {
        const float ix = 1.0F / stepLen;
        // While this frame's intended segment pierces a linked opening,
        // obstacles fully behind that portal's plane stop blocking the
        // sweep - without this the mounting wall stops the center ~r
        // short of the crossing plane and the throw can never cross.
        // Pad the aim segment by the object's extent: the sweep stops the
        // CENTER ~r short of a wall sitting at the plane, so an unpadded
        // center segment never reaches it and the doorway never opens
        // (owner repro: every throw bounced off the mounting wall).
        {
          const float pad = 1.0F + (r + 0.1F) / stepLen;
          const float want[3] = {prevT[0] + thrownVel[0] * pad,
                                 prevT[1] + thrownVel[1] * pad,
                                 prevT[2] + thrownVel[2] * pad};
          armSweepPass(prevT, want);
        }
        const float d = sweepSphere(
            o.data.position[0], o.data.position[1], o.data.position[2],
            thrownVel[0] * ix, thrownVel[1] * ix, thrownVel[2] * ix, stepLen,
            r, thrownIndex);
        sweepPassOn = false;
        o.data.position[0] += thrownVel[0] * ix * d;
        o.data.position[1] += thrownVel[1] * ix * d;
        o.data.position[2] += thrownVel[2] * ix * d;
        stop = d < stepLen;  // hit something on the way
        // Crossed a linked opening: hop through, position and the full
        // velocity vector mapped by the pair isometry; keep flying.
        if (PORTAL_COUNT > 0 &&
            (thrownIndex >= (int)portalHopCool.size() ||
             portalHopCool[thrownIndex] == 0) &&
            portalCarryCrossing(prevT, o.data.position, thrownVel)) {
          stop = false;
          hopped = true;
          if (thrownIndex < (int)portalHopCool.size())
            portalHopCool[thrownIndex] = 6;
        }
      }
      // Ground rest matches updateObjectPhysics, so the handoff is
      // seamless. Skipped on the hop frame (the mapped arrival is legal by
      // continuity) and inside a swallowing floor portal's zone - the
      // terrain over the portal must not catch the throw before its center
      // reaches the plane (the walkers' portalSwallowsPlayer rule).
      if (!hopped) {
        bool swallow = false;
        for (int pi = 0; PORTAL_COUNT > 0 && pi < PORTAL_COUNT; ++pi) {
          const PortalData& p = PORTALS[pi];
          if (p.scene != currentScene || p.object < 0 || p.target < 0)
            continue;
          // player-released flight: any linked floor portal swallows it
          if (p.object >= (int)runtimeObjects.size() ||
              p.target >= (int)runtimeObjects.size())
            continue;
          RuntimeObject& m = runtimeObjects[p.object];
          if (!m.active || !m.visible || !runtimeObjects[p.target].active)
            continue;
          if (portalSwallowSwept(m, 0.5F * m.data.scale[0] + 0.25F,
                                 0.5F * m.data.scale[1] + 0.25F,
                                 Vec4(prevT[0], prevT[1], prevT[2], 1.0F),
                                 Vec4(o.data.position[0], o.data.position[1],
                                      o.data.position[2], 1.0F))) {
            swallow = true;
            break;
          }
        }
        const float floorY =
            terrainHeightAt(o.data.position[0], o.data.position[2]) +
            0.5F * o.data.scale[1];
        if (!swallow && o.data.position[1] <= floorY) {
          o.data.position[1] = floorY;
          stop = true;
        }
      }
      o.dirty = true;
      if (stop) {
        o.velocityX = o.velocityY = o.velocityZ = 0.0F;
        thrownIndex = -1;
      }
    }
  }

  if (carryIndex < 0) {
    carryPortalPi = -1;
    return;
  }
  RuntimeObject& o = runtimeObjects[carryIndex];
  // Despawned or hidden mid-carry (flow graph): the hands just open, and the
  // body wakes so it resumes falling if it is shown again mid-air.
  if (!o.active || !o.visible) {
    releaseCarried(o, 0.0F, 0.0F, 0.0F);
    carryIndex = -1;
    carryPortalPi = -1;
    return;
  }

  Vec4 dir = cameraLookAt - cameraPosition;
  const float dirLen = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (dirLen < 0.0001F) return;
  dir.x /= dirLen, dir.y /= dirLen, dir.z /= dirLen;

  // Carry origin: the eye in first person; the avatar's head (the camera
  // pivot) in third person - "in front of the face" means the avatar's face,
  // not a camera floating meters behind it.
  float ox = cameraPosition.x, oy = cameraPosition.y, oz = cameraPosition.z;
  if (PLAYER_MODE == 2) {
    ox = cameraLookAt.x, oy = cameraLookAt.y, oz = cameraLookAt.z;
  }

  const float r = objectHalfExtent(o);
  // Same policy as the third-person camera boom (springArm): the sweep is
  // the law - a blocked reach pulls the object IN, even past its comfort
  // distance, and NEVER pushes it back out into geometry (the old
  // "never inside the carrier" clamp did exactly that: face against a wall,
  // the object got parked inside the wall). Snap in on a hit, ease back out
  // when the wall clears, so leaving it doesn't pop. PICK_MIN_DIST keeps the
  // object's near face off the near clip plane; the walkers' carry whisker
  // (a springArm-style pre-block) keeps the player from squeezing that far
  // in the first place.
  // Carrying into a portal opening: obstacles fully behind that portal's
  // plane stop blocking the carry sweep - without this the mounting wall
  // pinned the object at the face and the whisker below shoved the walker
  // back, so a wall portal could only be crossed BACKWARDS (owner report).
  // The probe starts BEHIND the eye (backed up along -dir): armSweepPass
  // only arms when its segment starts in FRONT of the plane, and at the
  // portal centre the eye sits right ON the plane - without the back-up the
  // doorway fails exactly there, the sweep catches the mounting wall and
  // yanks the object onto it (owner: cube stops on the wall dead centre).
  {
    const float reach = PICK_CARRY_DIST + r * 2.0F + 0.1F;
    const float bk = 1.2F;
    const float a0[3] = {ox - dir.x * bk, oy - dir.y * bk, oz - dir.z * bk};
    const float b0[3] = {ox + dir.x * reach, oy + dir.y * reach,
                         oz + dir.z * reach};
    armSweepPass(a0, b0);
  }
  float want = sweepSphere(ox, oy, oz, dir.x, dir.y, dir.z, PICK_CARRY_DIST + r,
                           r, carryIndex);
  sweepPassOn = false;
  // Portal-aware carry: the nearest portal opening the carry ray pierces
  // within reach. If that portal DRAWS the object in its through-view
  // (viewAll or the view list), the object flies on THROUGH it - mapped to
  // the far side, in front of the target, where the through-view renders it
  // as if it went through (and it is skipped in the near main pass so it
  // does not also appear as a distant double). This is what lets a carried
  // object cross smoothly instead of stopping dead at the surface (owner:
  // it pinned like a wall). A portal that would NOT render the object
  // (teleport-only) can't show it on the far side, so there the object is
  // clamped to the plane (half-in slice) as the best available.
  int bendPi = -1;
  float bendT = want;
  bool bendShows = false;
  for (int pi = 0; PORTAL_COUNT > 0 && pi < PORTAL_COUNT; ++pi) {
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
    if (p.object >= (int)runtimeObjects.size() ||
        p.target >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& pm = runtimeObjects[p.object];
    if (!pm.active || !pm.visible || !runtimeObjects[p.target].active) continue;
    const V3 pax = rotated({1.0F, 0.0F, 0.0F}, pm.data.rotation);
    const V3 pay = rotated({0.0F, 1.0F, 0.0F}, pm.data.rotation);
    const V3 paz = rotated({0.0F, 0.0F, 1.0F}, pm.data.rotation);
    const float denom = dir.x * paz.x + dir.y * paz.y + dir.z * paz.z;
    if (denom >= -1e-4F) continue;  // ray must head into the front (+Z) face
    const float sd = (pm.data.position[0] - ox) * paz.x +
                     (pm.data.position[1] - oy) * paz.y +
                     (pm.data.position[2] - oz) * paz.z;
    const float t = sd / denom;  // ray param at the plane
    // Allow the plane to sit a little BEHIND the eye (t slightly negative):
    // at the portal centre the eye is right on it, and dropping the crossing
    // there would un-bend the object for a frame and snap it to the near
    // pass behind the surface (owner: cube stops at the centre).
    if (t >= bendT || t < -(r + 0.6F)) continue;
    const float cxp = ox + dir.x * t - pm.data.position[0];
    const float cyp = oy + dir.y * t - pm.data.position[1];
    const float czp = oz + dir.z * t - pm.data.position[2];
    const float lxp = cxp * pax.x + cyp * pax.y + czp * pax.z;
    const float lyp = cxp * pay.x + cyp * pay.y + czp * pay.z;
    const float phx = 0.5F * pm.data.scale[0] + 0.25F;
    const float phy = 0.5F * pm.data.scale[1] + 0.25F;
    if (lxp > -phx && lxp < phx && lyp > -phy && lyp < phy) {
      bendPi = pi;
      bendT = t;
      bendShows = portalShowsObject(pi, carryIndex);
    }
  }
  // Aiming through a portal that will render the object on the far side:
  // Aiming through a portal that will render the object on the far side:
  // let the reach run FULL, overriding whatever the sweep returned. The
  // object is NOT pinned or bent - it rides straight ahead, straddling the
  // surface, and the two-sided render (below + the portal's through-view)
  // draws each half. The mounting wall must not clip `want` short (a wall
  // flush with the plane still trips the sweep, and the whole point is that
  // it opens like a doorway). A portal that will NOT render the object
  // (teleport-only) can't show the far half, so there the object clamps to
  // the surface (half-in slice) as the best available.
  if (bendPi >= 0 && bendShows)
    want = PICK_CARRY_DIST + r;
  else if (bendPi >= 0 && !bendShows && bendT > 0.0F && bendT < want)
    want = bendT;
  const float minD = PICK_MIN_DIST + r;
  if (want < minD) want = minD;
  if (want < carryDist) {
    carryDist = want;  // blocked: snap in, never clip into the wall
  } else {
    float k = 0.2F * g_frameScale;
    if (k > 1.0F) k = 1.0F;
    carryDist += (want - carryDist) * k;
  }
  const float d = carryDist;
  // Real position rides straight ahead - the object straddles the surface
  // naturally as the player approaches. carryPortalPi tells renderScene which
  // portal's through-view should draw the far half (mapped to the exit).
  carryPortalPi = (bendPi >= 0 && bendShows) ? bendPi : -1;
  o.data.position[0] = ox + dir.x * d;
  o.data.position[1] = oy + dir.y * d;
  o.data.position[2] = oz + dir.z * d;
  // In the hands the body has no momentum of its own: zero every component
  // (not just Y - the rigid-body sim reads all three) so a release starts
  // from rest instead of resuming whatever it was doing before the grab.
  o.velocityX = o.velocityY = o.velocityZ = 0.0F;
  o.spin[0] = o.spin[1] = o.spin[2] = 0.0F;
  o.dirty = true;

  const auto& clicked = engine->pad.getClicked();
  if (carryGrabbed) {
    carryGrabbed = false;  // the press that grabbed it is not a drop
  } else if (clicked.BTN_USE) {
    releaseCarried(runtimeObjects[carryIndex], 0.0F, 0.0F, 0.0F);
    carryIndex = -1;
    carryPortalPi = -1;
  } else if (o.data.pickThrow && clicked.BTN_THROW) {
    const int idx = carryIndex;
    carryIndex = -1;
    carryPortalPi = -1;
    const float vx = dir.x * PICK_THROW_SPEED * g_frameDt;
    const float vy = dir.y * PICK_THROW_SPEED * g_frameDt;
    const float vz = dir.z * PICK_THROW_SPEED * g_frameDt;
    if (!releaseCarried(runtimeObjects[idx], vx, vy, vz)) {
      // No rigid body to hand off to: fly the hand-rolled arc instead.
      thrownIndex = idx;
      thrownVel[0] = vx;
      thrownVel[1] = vy;
      thrownVel[2] = vz;
    }
  }
}

// Hands a released object back to whatever moves it. A rigid body (Physics
// on) simply takes the velocity and WAKES: the sim sleeps settled bodies
// (restFrames >= PHYS_SLEEP_FRAMES) and skips them entirely, and an object
// picked up off the ground is asleep by definition - without this it would
// hang in mid-air where it was dropped, and a throw would ignore bounce,
// friction and tumble. Returns false for a non-physics object, which has no
// simulation to hand off to (it stays put on a drop; a throw flies the
// hand-rolled arc in updateCarriedObject).
bool TerrainGame::releaseCarried(RuntimeObject& o, float vx, float vy,
                                 float vz) {
  o.dirty = true;
  if (!o.data.physics) return false;
  o.velocityX = vx;
  o.velocityY = vy;
  o.velocityZ = vz;
  o.restFrames = 0;  // wake - see the RuntimeObject sleep contract
  // Player-released: portal-free (crosses any linked portal, like the
  // player) until it settles - updatePortals clears the latch on sleep.
  thrownFreeIndex = (int)(&o - runtimeObjects.data());
  return true;
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
    d.playerPos[0] = players[0].x;
    d.playerPos[1] = players[0].y;
    d.playerPos[2] = players[0].z;
    d.playerYaw = players[0].yaw * 180.0F / PI;
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
       i < SCENE_OBJECT_COUNT && d.objectCount < SAVE_OBJECT_MAX; ++i) {
    if (!SCENE_OBJECTS[i].saveState) continue;  // spawn clones never persist
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
    o.velocityX = o.velocityY = o.velocityZ = 0.0F;
    o.spin[0] = o.spin[1] = o.spin[2] = 0.0F;
    o.restFrames = 0;  // wake: the restored position may be mid-air
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

  // Toggle/Choice rows: the state is the bound save value (the option
  // index). Cross and dpad right cycle forward, dpad left backward.
  auto cycleValue = [&](const MenuEntryData& e, int dir) {
    if (e.param < 0 || e.param >= SAVE_VALUE_COUNT || e.optionCount <= 0)
      return;
    int v = (int)saveValues[e.param];
    if (v < 0) v = 0;
    if (v >= e.optionCount) v = e.optionCount - 1;
    saveValues[e.param] = (float)((v + dir + e.optionCount) % e.optionCount);
  };
  if (gameMenuCursor >= 0 && gameMenuCursor < m.entryCount) {
    const MenuEntryData& cur = m.entries[gameMenuCursor];
    if (cur.action == 7 || cur.action == 8) {
      if (clicked.DpadLeft) cycleValue(cur, -1);
      if (clicked.DpadRight) cycleValue(cur, 1);
    }
  }

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
      case 7:  // toggle - flip/cycle the bound save value (menu stays open)
      case 8:  // choice
        cycleValue(e, 1);
        break;
      case 9:  // apply video mode: commit the display row's staged selection
        // (a scan-mode switch closes the menu - see applyVideoRequests'
        // caller - so the player judges the new picture unobstructed).
        for (int vmi = 0; vmi < MENU_COUNT; ++vmi) {
          for (int vei = 0; vei < MENUS[vmi].entryCount; ++vei) {
            const MenuEntryData& row = MENUS[vmi].entries[vei];
            if (row.bind != 5 || row.param < 0 ||
                row.param >= SAVE_VALUE_COUNT || row.optionCount <= 0)
              continue;
            int idx = (int)saveValues[row.param];
            if (idx < 0) idx = 0;
            if (idx >= row.optionCount) idx = row.optionCount - 1;
            const int mode = displayOptionMode(row, idx);
            if (mode !=
                (int)engine->renderer.core.getSettings().getDisplayMode()) {
              scriptCtx.requestDisplayMode = mode;
              scriptCtx.displayConfirmSec = 8.0F;  // keep-or-revert net
              g_menuDispOpt = idx;
            }
            vmi = MENU_COUNT;  // first display row wins
            break;
          }
        }
        break;
    }
  }
  return pausing();
}

// Ready-made menu "option blocks" (Menu Editor > Insert option block): a
// Toggle/Choice row bound to a built-in engine setting. Every frame we map the
// row's option index (held in its save value) onto the setting, evenly across
// the row's options - so the same row that persists and previews as a normal
// stateful entry also drives the engine, with no flow graph. Volume / deadzone
// / curve are idempotent (re-applied each frame, cheap). Display mode and
// widescreen rebuild VRAM / arm the confirm prompt, so they fire only when the
// option actually changes, routed through the same scriptCtx video requests
// the Set Display Mode / Set Widescreen flow nodes use. When the project has
// an "apply video mode" row (MENU_HAS_APPLY_VIDEO) the display row defers
// instead: cycling it only stages a selection the APPLY row commits (case 9
// in updateGameMenu), so the player can browse the option list without the
// screen switching under them.
void TerrainGame::applyMenuBindings() {
  for (int mi = 0; mi < MENU_COUNT; ++mi) {
    const MenuData& m = MENUS[mi];
    for (int e = 0; e < m.entryCount; ++e) {
      const MenuEntryData& en = m.entries[e];
      if (en.bind == 0) continue;
      const int cnt = en.optionCount > 0 ? en.optionCount : 1;
      int idx = (en.param >= 0 && en.param < SAVE_VALUE_COUNT)
                    ? (int)saveValues[en.param]
                    : 0;
      if (idx < 0) idx = 0;
      if (idx >= cnt) idx = cnt - 1;
      // t: option index normalized to 0..1 (single-option rows read as full).
      const float t = cnt > 1 ? (float)idx / (float)(cnt - 1) : 1.0F;
      switch (en.bind) {
        case 1:  // music volume 0..100
          engine->audio.song.setVolume((u8)(t * 100.0F + 0.5F));
          break;
        case 2:  // master sfx volume 0..100
          scriptCtx.sfxVolume = (int)(t * 100.0F + 0.5F);
          break;
        case 3:  // deadzone, both sticks 0..0.4
          g_deadzoneL = g_deadzoneR = t * 0.4F;
          break;
        case 4: {  // aim response curve (both sticks): drives the shared
          // g_stickCurve*/g_stickExp* runtime (same globals the Set Stick Curve
          // flow node uses). Option 0 Linear, 1 Smooth (S-curve), 2+ Precise
          // (exponential - finer near center). Uses idx, not the even mapping.
          int curve = 0;
          if (idx == 1) curve = 2;       // S-curve
          else if (idx >= 2) curve = 1;  // exponential
          g_stickCurveL = g_stickCurveR = curve;
          g_stickExpL = g_stickExpR = 2.0F;
          break;
        }
        case 5:  // display / scan mode (option -> mode via displayOptionMode)
          if (MENU_HAS_APPLY_VIDEO) {
            // Deferred: the row only stages a selection while a menu is on
            // screen; the "apply video mode" row commits it. With no menu
            // open, snap the row back to the live mode - a browsed-but-
            // unapplied selection (or a reverted confirm) never lies.
            if (gameMenuIndex < 0 && en.param >= 0 &&
                en.param < SAVE_VALUE_COUNT) {
              const int live = displayOptionIndexOf(
                  en,
                  (int)engine->renderer.core.getSettings().getDisplayMode());
              if (live >= 0 && live != idx) saveValues[en.param] = (float)live;
            }
          } else if (idx != g_menuDispOpt) {
            g_menuDispOpt = idx;
            scriptCtx.requestDisplayMode = displayOptionMode(en, idx);
            scriptCtx.displayConfirmSec = 8.0F;  // keep-or-revert safety net
          }
          break;
        case 6:  // widescreen 4:3 / 16:9
          if (idx != g_menuWideOpt) {
            g_menuWideOpt = idx;
            scriptCtx.widescreen = idx;
          }
          break;
        case 7:  // player count (1P / 2P) - edge-triggered, so the pad-2
          // Start join isn't reverted by the bind on the next frame
          // (setPlayerTwoActive writes the row's save value back in sync).
          if (idx != menuPlayerCountPrev) {
            menuPlayerCountPrev = idx;
            setPlayerTwoActive(idx >= 1);
          }
          break;
      }
    }
  }
}

// Keep a bound "Player count" menu row's save value (and the bind's edge
// detector) in line with the actual player-2 state, so a pad-2 Start join
// shows up in the menu instead of fighting it.
void TerrainGame::syncPlayerCountMenuValue() {
  const int idx = playerTwoActive ? 1 : 0;
  menuPlayerCountPrev = idx;
  for (int mi = 0; mi < MENU_COUNT; ++mi) {
    const MenuData& m = MENUS[mi];
    for (int e = 0; e < m.entryCount; ++e) {
      const MenuEntryData& en = m.entries[e];
      if (en.bind == 7 && en.param >= 0 && en.param < SAVE_VALUE_COUNT)
        saveValues[en.param] = (float)idx;
    }
  }
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
  // Toggle/Choice rows: the current option label, a cell of the baked value
  // strip drawn right-aligned on the row (cell right edge 24px from the
  // panel's right border - the mirror of the 56px label margin).
  if (m.values[0] != '\0' &&
      gameMenuIndex < (int)menuValueSprites.size()) {
    Sprite& vs = menuValueSprites[gameMenuIndex];
    for (int i = 0; i < m.entryCount; ++i) {
      const MenuEntryData& e = m.entries[i];
      if (e.cell < 0 || e.optionCount <= 0) continue;
      int v = (e.param >= 0 && e.param < SAVE_VALUE_COUNT)
                  ? (int)saveValues[e.param]
                  : 0;
      if (v < 0) v = 0;
      if (v >= e.optionCount) v = e.optionCount - 1;
      vs.offset = Vec2(0.0F, (float)((e.cell + v) * m.valuePitch));
      vs.position = Vec2(panel.position.x + m.valueX,
                         panel.position.y + m.row0Y + i * m.rowH);
      engine->renderer.renderer2D.render(vs);
    }
  }
}

// On-screen texts: apply the frame's Show/Hide Text requests, tick the
// auto-hide timers, draw what is visible. Baked sprites - one 2D quad each.
void TerrainGame::updateAndRenderHudTexts() {
  for (int i = 0; i < (int)hudTextSprites.size(); ++i) {
    if (scriptCtx.textRequest && scriptCtx.textRequest[i] >= 0) {
      hudTextOn[i] = scriptCtx.textRequest[i] != 0 ? 1 : 0;
      hudTextTimer[i] = hudTextOn[i] ? scriptCtx.textDuration[i] : 0.0F;
      scriptCtx.textRequest[i] = -1;
    }
    if (hudTextOn[i] && hudTextTimer[i] > 0.0F) {
      hudTextTimer[i] -= g_frameDt;
      if (hudTextTimer[i] <= 0.0F) {
        hudTextOn[i] = 0;
        hudTextTimer[i] = 0.0F;
      }
    }
    if (hudTextOn[i]) engine->renderer.renderer2D.render(hudTextSprites[i]);
  }
}

// Runtime texts: same request/timer protocol as the baked ones, but the string
// lives in dynTextBuf (refreshed every frame by the owning flow-graph script
// while the slot is on) and is drawn glyph by glyph from the font's atlas.
void TerrainGame::updateAndRenderDynTexts() {
  for (int i = 0; i < DYN_TEXT_COUNT; ++i) {
    if (scriptCtx.dynTextRequest[i] >= 0) {
      dynTextOn[i] = scriptCtx.dynTextRequest[i] != 0 ? 1 : 0;
      dynTextTimer[i] = dynTextOn[i] ? scriptCtx.dynTextDuration[i] : 0.0F;
      scriptCtx.dynTextRequest[i] = -1;
    }
    if (dynTextOn[i] && dynTextTimer[i] > 0.0F) {
      dynTextTimer[i] -= g_frameDt;
      if (dynTextTimer[i] <= 0.0F) {
        dynTextOn[i] = 0;
        dynTextTimer[i] = 0.0F;
      }
    }
    if (!dynTextOn[i]) continue;
    const DynTextData& d = DYN_TEXTS[i];
    const char* s = &dynTextBuf[(size_t)i * DYN_TEXT_LEN];
    if (!s[0]) continue;
    const auto& scr = engine->renderer.core.getSettings();
    drawFontText(engine, d.font, s, d.x * scr.getWidth(), d.y * scr.getHeight(),
                 d.size);
  }
}

// Spring arm (third person only): how far the camera may sit down the boom
// before it would end up inside geometry or under the terrain. Casts the boom
// from the pivot (the avatar's head) toward the desired eye and returns the
// first blocked distance, so the caller can pull the camera to the collision
// point instead of letting it punch through walls.
//
// Budget-driven shape - this runs every frame, so:
//  - AABB only, even for mesh-collision objects. Camera collision needs no
//    triangle precision (stopping a few cm early is invisible), and a slab
//    test is a few compares with no sqrt, vs walking a collider's triangles.
//  - The boom is short (PLAYER_CAM_DIST), so a 6-compare broad phase (boom
//    segment AABB vs object AABB) rejects nearly every object before any
//    division happens; only real candidates reach the slab test.
//  - Objects marked collision "none" and the markers/visual-only types are
//    skipped - including the avatar itself (type 6), which must never block
//    its own camera.
//  - The terrain march is a fixed 8 steps + 4 bisections over the distance
//    that SURVIVED the object pass (constant cost, and shorter once an object
//    already pulled the camera in).
constexpr float CAM_RADIUS = 0.3F;    // eye clearance kept off surfaces; must
                                      // exceed the 0.15 near clip
constexpr float CAM_MIN_DIST = 0.6F;  // never pull closer than this to the head
float TerrainGame::springArm(float px, float py, float pz, float dx, float dy,
                             float dz, float maxDist) const {
  // Camera boom: the camera's own radius, ignoring the carried object (it
  // rides right in front of the face and must never shove the camera).
  return sweepSphere(px, py, pz, dx, dy, dz, maxDist, CAM_RADIUS, carryIndex);
}

float TerrainGame::sweepSphere(float px, float py, float pz, float dx,
                               float dy, float dz, float maxDist, float radius,
                               int skipIndex) const {
  const float r = radius;
  float best = maxDist;

  // Broad-phase key: the boom segment's AABB, expanded by the camera radius.
  const float qx = px + dx * maxDist, qy = py + dy * maxDist,
              qz = pz + dz * maxDist;
  const float sminX = (px < qx ? px : qx) - r, smaxX = (px > qx ? px : qx) + r;
  const float sminY = (py < qy ? py : qy) - r, smaxY = (py > qy ? py : qy) + r;
  const float sminZ = (pz < qz ? pz : qz) - r, smaxZ = (pz > qz ? pz : qz) + r;

  for (int oi = 0; oi < (int)runtimeObjects.size(); ++oi) {
    const RuntimeObject& o = runtimeObjects[oi];
    if (oi == skipIndex) continue;  // the swept object itself
    if (!o.active || !o.visible) continue;
    const int ty = o.data.type;
    if (ty == 4 || ty == 6 || ty == 7 || ty == 8 || ty == 9 || ty == 11 ||
        ty == 13 || ty == 14)
      continue;  // markers / emitters / decals / the avatar - not blockers
    if (o.data.collision == 2) continue;  // "none": the sweep passes through
    if (sweepPassOn) {
      // Portal pass-through for a thrown object's sweep: obstacles fully
      // behind the aimed portal's plane open up (exact OBB extent along
      // the plane normal - collidePlayer's doorway rule).
      const V3 pax = rotated({1.0F, 0.0F, 0.0F}, o.data.rotation);
      const V3 pay = rotated({0.0F, 1.0F, 0.0F}, o.data.rotation);
      const V3 paz = rotated({0.0F, 0.0F, 1.0F}, o.data.rotation);
      const float re =
          fabsf(sweepPassPlane[0] * pax.x + sweepPassPlane[1] * pax.y +
                sweepPassPlane[2] * pax.z) *
              0.5F * o.data.scale[0] +
          fabsf(sweepPassPlane[0] * pay.x + sweepPassPlane[1] * pay.y +
                sweepPassPlane[2] * pay.z) *
              0.5F * o.data.scale[1] +
          fabsf(sweepPassPlane[0] * paz.x + sweepPassPlane[1] * paz.y +
                sweepPassPlane[2] * paz.z) *
              0.5F * o.data.scale[2];
      const float sd = sweepPassPlane[0] * o.data.position[0] +
                       sweepPassPlane[1] * o.data.position[1] +
                       sweepPassPlane[2] * o.data.position[2] -
                       sweepPassPlane[3];
      if (sd < -re + 0.1F) continue;
    }

    // Oriented box, sized exactly like box-mode player collision (the real
    // mesh or baked anim AABB when the object has one, else the unit scale
    // box) - and cast in the box's OWN frame, so a yaw-rotated block stops the
    // boom at its real faces instead of leaking through the corners of an
    // axis-aligned stand-in.
    const GameModel* gm = nullptr;
    if (ty == 5 && o.data.model >= 0 && o.data.model < (int)gameModels.size())
      gm = &gameModels[o.data.model];
    const SkelModel* anim = nullptr;
    if (ty == 5 && o.data.animModel >= 0 &&
        o.data.animModel < (int)gameAnimModels.size())
      anim = gameAnimModels[o.data.animModel].src.get();
    float ex = 0.5F * o.data.scale[0], ey = 0.5F * o.data.scale[1],
          ez = 0.5F * o.data.scale[2];
    V3 localCenter = {0.0F, 0.0F, 0.0F};
    const float* mn = gm ? gm->mn : (anim ? anim->min : nullptr);
    const float* mx = gm ? gm->mx : (anim ? anim->max : nullptr);
    if (mn && mx) {
      localCenter = {0.5F * (mn[0] + mx[0]) * o.data.scale[0],
                     0.5F * (mn[1] + mx[1]) * o.data.scale[1],
                     0.5F * (mn[2] + mx[2]) * o.data.scale[2]};
      ex = 0.5F * (mx[0] - mn[0]) * o.data.scale[0];
      ey = 0.5F * (mx[1] - mn[1]) * o.data.scale[1];
      ez = 0.5F * (mx[2] - mn[2]) * o.data.scale[2];
    }
    const V3 cW = rotated(localCenter, o.data.rotation);
    const float cx = o.data.position[0] + cW.x;
    const float cy = o.data.position[1] + cW.y;
    const float cz = o.data.position[2] + cW.z;

    // Broad phase: the OBB's own world AABB (rotated half-extent vectors summed
    // per axis), inflated by r, vs the boom segment AABB. Conservative - it
    // never rejects an object the local slab test could still hit (the old
    // scale-box AABB was too small for a rotated block and leaked candidates).
    const V3 hxv = rotated({ex, 0.0F, 0.0F}, o.data.rotation);
    const V3 hyv = rotated({0.0F, ey, 0.0F}, o.data.rotation);
    const V3 hzv = rotated({0.0F, 0.0F, ez}, o.data.rotation);
    const float wex = fabsf(hxv.x) + fabsf(hyv.x) + fabsf(hzv.x);
    const float wey = fabsf(hxv.y) + fabsf(hyv.y) + fabsf(hzv.y);
    const float wez = fabsf(hxv.z) + fabsf(hyv.z) + fabsf(hzv.z);
    if (cx + wex + r < sminX || cx - wex - r > smaxX ||
        cy + wey + r < sminY || cy - wey - r > smaxY ||
        cz + wez + r < sminZ || cz - wez - r > smaxZ)
      continue;  // broad phase: nowhere near the boom

    // Narrow phase: slab test in the box's local frame. invRotated is
    // orthonormal, so the hit parameter t is still a world-space distance.
    const V3 lo = invRotated({px - cx, py - cy, pz - cz}, o.data.rotation);
    const V3 ld = invRotated({dx, dy, dz}, o.data.rotation);
    float t0 = 0.0F, t1 = best;
    bool miss = false;
    auto slab = [&](float o1, float d1, float lo1, float hi1) {
      if (miss) return;
      if (d1 > 1e-6F || d1 < -1e-6F) {
        const float inv = 1.0F / d1;
        float ta = (lo1 - o1) * inv, tb = (hi1 - o1) * inv;
        if (ta > tb) {
          const float s = ta;
          ta = tb;
          tb = s;
        }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
      } else if (o1 < lo1 || o1 > hi1) {
        miss = true;  // parallel and outside the slab
      }
    };
    slab(lo.x, ld.x, -ex - r, ex + r);
    slab(lo.y, ld.y, -ey - r, ey + r);
    slab(lo.z, ld.z, -ez - r, ez + r);
    if (miss || t0 > t1) continue;
    // t0 < 0 = the pivot is already inside this (inflated) box - e.g. the
    // player brushing a wall. Ignore it rather than collapse the camera onto
    // the head; a box we are standing in cannot usefully block the boom.
    if (t0 < 0.0F) continue;
    if (t0 < best) best = t0;
  }

  // Terrain: march the surviving distance, then bisect to tighten the hit.
  // `- r` keeps the eye a radius clear of the ground, and `lo` (the last
  // known-free sample) is the conservative answer.
  const float step = best * 0.125F;
  if (step > 1e-4F) {
    float prev = 0.0F;
    for (int i = 1; i <= 8; ++i) {
      const float t = step * i;
      if (py + dy * t - r <= terrainHeightAt(px + dx * t, pz + dz * t)) {
        float lo = prev, hi = t;
        for (int k = 0; k < 4; ++k) {
          const float mid = (lo + hi) * 0.5F;
          if (py + dy * mid - r <= terrainHeightAt(px + dx * mid, pz + dz * mid))
            hi = mid;
          else
            lo = mid;
        }
        best = lo;
        break;
      }
      prev = t;
    }
  }
  return best;
}

// Dispatcher: walk P1 (and P2 while active) from their pads, then compose the
// frame camera. Shared screen frames the pair with one camera; split screen
// keeps each player's own view (the render pass reads players[1].camPos).
bool TerrainGame::updatePlayerEntity() {
  if (PLAYER_INDEX < 0) return false;

  updatePlayerWalker(players[0], 0, engine->pad);
  const bool p2 =
      MULTIPLAYER_MODE != 0 && playerTwoActive && players[1].objIndex >= 0;
  if (p2) {
    if (MULTIPLAYER_MODE == 1) {
      // Shared screen: P2 moves relative to the one camera - its movement
      // basis mirrors P1's orbit (the walker skips its right stick).
      players[1].yaw = players[0].yaw;
      players[1].pitch = players[0].pitch;
    }
    updatePlayerWalker(players[1], 1, pad2);
  }

  if (p2 && MULTIPLAYER_MODE == 1) {
    updateSharedCamera();
  } else {
    cameraPosition = players[0].camPos;
    cameraLookAt = players[0].camLook;
  }
  return true;
}

void TerrainGame::updatePlayerWalker(PlayerCtl& P, int pi, Tyra::Pad& pad) {
  const auto& leftJoy = pad.getLeftJoyPad();
  const auto& rightJoy = pad.getRightJoyPad();
  // stickAxis applies the per-stick deadzone (g_deadzoneL/R - Preferences, or a
  // menu "Deadzone" option block) and response curve (g_stickCurve*/g_stickExp*
  // - Preferences > Input / Set Stick Curve node / a menu "Aim curve" block).
  auto axisL = [&](const u8& raw) {
    return stickAxis(raw, g_deadzoneL, g_stickCurveL, g_stickExpL);
  };
  auto axisR = [&](const u8& raw) {
    return stickAxis(raw, g_deadzoneR, g_stickCurveR, g_stickExpR);
  };

  // Right stick: look around (stick right = turn right). A shared-screen P2
  // has no camera of its own (the dispatcher mirrors P1's orbit into it), so
  // only players that own a view read the right stick.
  const bool ownCamera = pi == 0 || MULTIPLAYER_MODE == 2;
  if (ownCamera) {
    P.yaw -= axisR(rightJoy.h) * 0.05F * PP_LOOK_SPEED(pi) * g_frameScale;
    P.pitch -= axisR(rightJoy.v) * 0.035F * PP_LOOK_SPEED(pi) * g_frameScale;
    if (P.pitch > 1.35F) P.pitch = 1.35F;
    if (P.pitch < -1.35F) P.pitch = -1.35F;
  }

  const float fx = sinf(P.yaw);
  const float fz = cosf(P.yaw);
  const float forward = -axisL(leftJoy.v);
  const float strafe = axisL(leftJoy.h);

  if (PP_MODE(pi) == 1) {
    // Noclip: fly where the camera looks; X up, Square down.
    const float cp = cosf(P.pitch);
    const float step = PP_WALK_SPEED(pi) * g_frameScale;
    P.x += (fx * cp * forward - fz * strafe) * step;
    P.z += (fz * cp * forward + fx * strafe) * step;
    P.y += sinf(P.pitch) * forward * step;
    if (pad.getPressed().BTN_FLY_UP) P.y += step;
    if (pad.getPressed().BTN_FLY_DOWN) P.y -= step;

    P.camPos = Vec4(P.x, P.y, P.z);
    P.camLook = Vec4(P.x + fx * cp, P.y + sinf(P.pitch), P.z + fz * cp);
    return;
  }

  if (PP_MODE(pi) == 2) {
    // Third person: the left stick moves the avatar relative to the camera,
    // the avatar turns to face where it walks, and the camera rides a boom
    // behind it. Terrain bounds + object collision + gravity/jump match walk.
    float nextX = P.x + (fx * forward - fz * strafe) * PP_WALK_SPEED(pi) * g_frameScale;
    float nextZ = P.z + (fz * forward + fx * strafe) * PP_WALK_SPEED(pi) * g_frameScale;
    const float limX = TERRAIN_WIDTH * 0.5F - 1.0F;
    const float limZ = TERRAIN_DEPTH * 0.5F - 1.0F;
    if (nextX > limX) nextX = limX;
    if (nextX < -limX) nextX = -limX;
    if (nextZ > limZ) nextZ = limZ;
    if (nextZ < -limZ) nextZ = -limZ;

    float ground = terrainHeightAt(nextX, nextZ);
    // a linked floor portal underfoot swallows the avatar too
    if (PORTAL_COUNT > 0 && portalSwallowsPlayer(nextX, P.y, nextZ))
      ground = -1e30F;
    float ceiling = 1e30F;
    // The avatar shoves physics bodies exactly like the FPP walkers do.
    pushPhysicsBodies(P.x, P.z, nextX, nextZ, P.y, PP_EYE_HEIGHT(pi));
    updatePortalPass(nextX, P.y, nextZ);  // wall doorways open in collision
    collidePlayer(P.x, P.z, &nextX, &nextZ, P.y, PP_EYE_HEIGHT(pi), &ground,
                  &ceiling);
    // The whisker reads portalPassOn to keep the mounting wall open while
    // carrying THROUGH a portal - reset it only after the whisker runs.
    if (pi == 0)  // carrying is pad-1 only (updateUseTarget)
      applyCarryWhisker(&nextX, &nextZ, P.y + PP_CAM_HEIGHT(pi), P.yaw, P.y,
                        PP_EYE_HEIGHT(pi));
    portalPassOn = false;
    const float movedX = nextX - P.x, movedZ = nextZ - P.z;
    P.x = nextX;
    P.z = nextZ;

    P.velY -= GRAVITY * g_frameDt * g_frameDt;
    P.y += P.velY;
    const float maxY = ceiling - PP_EYE_HEIGHT(pi) - EYE_CLEARANCE;
    if (P.y > maxY && maxY >= ground) {
      P.y = maxY;
      if (P.velY > 0.0F) P.velY = 0.0F;
    }
    bool grounded = false;
    if (P.y <= ground) {
      P.y = ground;
      P.velY = 0.0F;
      grounded = true;
      if (PP_CAN_JUMP(pi) && pad.getClicked().BTN_JUMP)
        P.velY = PP_JUMP_SPEED(pi) * g_frameDt;
    }

    // Turn the avatar toward its movement direction (shortest-arc lerp).
    const float movedLen = sqrtf(movedX * movedX + movedZ * movedZ);
    if (movedLen > 0.0005F) {
      float desired = atan2f(movedX, movedZ);
      float d = desired - P.faceYaw;
      while (d > PI) d -= 2.0F * PI;
      while (d < -PI) d += 2.0F * PI;
      float k = PP_TURN_RATE(pi) * g_frameScale;
      if (k > 1.0F) k = 1.0F;
      P.faceYaw += d * k;
    }

    // Camera boom: the eye rides PP_CAM_DIST behind/above the head along
    // the orbit direction. The spring arm shortens the boom to the first thing
    // it hits so the camera never enters geometry or the terrain. Classic
    // spring behavior: pull IN instantly (a late pull-in means a visible clip
    // through a wall) and ease back OUT, so leaving cover doesn't snap.
    const float headY = P.y + PP_CAM_HEIGHT(pi);
    const float cp = cosf(P.pitch);
    const float boomX = sinf(P.yaw) * cp;
    const float boomZ = cosf(P.yaw) * cp;
    const float boomY = sinf(P.pitch);

    // Over-the-shoulder: slide the WHOLE rig - eye and look-at alike - along
    // the camera's right vector, so the avatar sits off-center in frame.
    // (Offsetting only the eye would just angle the camera back at the player
    // and keep them centered - that is not an over-the-shoulder shot.) The
    // right vector matches the walkers' own strafe convention. The offset is
    // itself spring-armed so a shoulder cam cannot slide into a wall the player
    // is hugging - and this second cast costs nothing at all when the offset is
    // 0, which is the default.
    const float rx = -cosf(P.yaw), rz = sinf(P.yaw);
    float shoulder = PP_CAM_SHOULDER(pi);
    if (shoulder > 0.0001F || shoulder < -0.0001F) {
      const float s = shoulder < 0.0F ? -1.0F : 1.0F;
      shoulder = s * springArm(P.x, headY, P.z, rx * s, 0.0F, rz * s,
                               shoulder * s);
    }
    const float pivotX = P.x + rx * shoulder;
    const float pivotZ = P.z + rz * shoulder;

    float want =
        springArm(pivotX, headY, pivotZ, -boomX, -boomY, -boomZ, PP_CAM_DIST(pi));
    if (want < CAM_MIN_DIST) want = CAM_MIN_DIST;

    // Whisker anticipation: two extra casts splayed ~20 deg to either side of
    // the boom spot walls the camera is about to sweep behind and start easing
    // the boom in before the straight ray is blocked. A whisker hit is
    // off-axis - a hint, not the true obstruction - so it only pulls the
    // target partway toward its own distance. The want-clamp below remains the
    // never-clip guarantee; whiskers just mean it usually fires from a boom
    // that is already most of the way in, so the residual jump is small.
    float target = want;
    const float wcos = 0.94F, wsin = 0.342F;
    for (int s = -1; s <= 1; s += 2) {
      const float wx = -boomX * wcos + (-boomZ) * (wsin * s);
      const float wz = boomX * (wsin * s) - boomZ * wcos;
      const float d =
          springArm(pivotX, headY, pivotZ, wx, -boomY, wz, PP_CAM_DIST(pi));
      const float soft = d + (want - d) * 0.4F;
      if (soft < target) target = soft;
    }
    if (target < CAM_MIN_DIST) target = CAM_MIN_DIST;

    // Ease toward the anticipated length: briskly in (a few frames), gently
    // back out (~2 s for a full boom), and never past the straight-ray hit -
    // blocked mid-ease still clamps so the camera cannot enter geometry.
    const float rate = target < P.boom ? 0.30F : 0.06F;
    float k = rate * g_frameScale;
    if (k > 1.0F) k = 1.0F;
    P.boom += (target - P.boom) * k;
    if (P.boom > want) P.boom = want;
    if (P.boom < CAM_MIN_DIST) P.boom = CAM_MIN_DIST;
    float eyeX = pivotX - boomX * P.boom;
    float eyeY = headY - boomY * P.boom;
    float eyeZ = pivotZ - boomZ * P.boom;
    // Safety net: the march samples the heightmap discretely, so a sharp ridge
    // between two samples could still leave the eye underground.
    const float minEyeY = terrainHeightAt(eyeX, eyeZ) + 0.4F;
    if (eyeY < minEyeY) eyeY = minEyeY;
    P.camPos = Vec4(eyeX, eyeY, eyeZ);
    P.camLook = Vec4(pivotX, headY, pivotZ);

    // Drive the avatar object: stand at the feet, face the walk direction,
    // and auto-select its locomotion clip. updateAndRenderAnimObjects draws it.
    if (P.objIndex >= 0 && P.objIndex < (int)runtimeObjects.size()) {
      RuntimeObject& body = runtimeObjects[P.objIndex];
      body.data.position[0] = P.x;
      body.data.position[1] = P.y;
      body.data.position[2] = P.z;
      body.data.rotation[1] = P.faceYaw * 180.0F / PI;
      const float step = PP_WALK_SPEED(pi) * g_frameScale;
      drivePlayerAnim(P, body, step > 1e-4F ? movedLen / step : 0.0F, grounded);
    }
    return;
  }

  // Walk mode: terrain bounds, object collision, gravity + jump.
  float nextX = P.x + (fx * forward - fz * strafe) * PP_WALK_SPEED(pi) * g_frameScale;
  float nextZ = P.z + (fz * forward + fx * strafe) * PP_WALK_SPEED(pi) * g_frameScale;

  const float limX = TERRAIN_WIDTH * 0.5F - 1.0F;
  const float limZ = TERRAIN_DEPTH * 0.5F - 1.0F;
  if (nextX > limX) nextX = limX;
  if (nextX < -limX) nextX = -limX;
  if (nextZ > limZ) nextZ = limZ;
  if (nextZ < -limZ) nextZ = -limZ;

  float ground = terrainHeightAt(nextX, nextZ);
  // a linked floor portal underfoot swallows the walker (see
  // portalSwallowsPlayer) - the terrain stops being the floor there
  if (PORTAL_COUNT > 0 && portalSwallowsPlayer(nextX, P.y, nextZ))
    ground = -1e30F;
  float ceiling = 1e30F;
  // Shove physics bodies with the attempted step first - collidePlayer may
  // cancel the move against the (still solid) body, the push moves it away
  // over the next frames.
  pushPhysicsBodies(P.x, P.z, nextX, nextZ, P.y, PP_EYE_HEIGHT(pi));
  updatePortalPass(nextX, P.y, nextZ);  // wall doorways open in collision
  collidePlayer(P.x, P.z, &nextX, &nextZ, P.y, PP_EYE_HEIGHT(pi), &ground,
                &ceiling);
  // whisker reads portalPassOn (carry through a portal) - reset after it
  if (pi == 0)  // carrying is pad-1 only (updateUseTarget)
    applyCarryWhisker(&nextX, &nextZ, P.y + PP_EYE_HEIGHT(pi), P.yaw, P.y,
                      PP_EYE_HEIGHT(pi));
  portalPassOn = false;
  P.x = nextX;
  P.z = nextZ;

  P.velY -= GRAVITY * g_frameDt * g_frameDt;  // GRAVITY is units/s^2
  P.y += P.velY;
  // Jump clamp: keep the eye EYE_CLEARANCE below overhead geometry so the
  // camera never pokes into it (skipped when the gap is too low to stand in)
  const float maxY = ceiling - PP_EYE_HEIGHT(pi) - EYE_CLEARANCE;
  if (P.y > maxY && maxY >= ground) {
    P.y = maxY;
    if (P.velY > 0.0F) P.velY = 0.0F;
  }
  if (P.y <= ground) {
    P.y = ground;
    P.velY = 0.0F;
    if (PP_CAN_JUMP(pi) && pad.getClicked().BTN_JUMP)
      P.velY = PP_JUMP_SPEED(pi) * g_frameDt;  // units/s
  }

  const float eyeY = P.y + PP_EYE_HEIGHT(pi);
  P.camPos = Vec4(P.x, eyeY, P.z);
  P.camLook = Vec4(P.x + fx * cosf(P.pitch), eyeY + sinf(P.pitch),
                   P.z + fz * cosf(P.pitch));
}

// Shared-screen camera: orbit (P1's right stick, mirrored into both players'
// yaw/pitch by the dispatcher) around the midpoint of the two avatars, with
// the boom stretched by their separation so the pair stays in frame. The
// spring arm and terrain safety net match the single-player boom.
void TerrainGame::updateSharedCamera() {
  PlayerCtl& A = players[0];
  PlayerCtl& B = players[1];
  const float midX = (A.x + B.x) * 0.5F;
  const float midZ = (A.z + B.z) * 0.5F;
  const float midY = (A.y + B.y) * 0.5F + PP_CAM_HEIGHT(0);
  const float dx = A.x - B.x, dy = A.y - B.y, dz = A.z - B.z;
  const float sep = sqrtf(dx * dx + dy * dy + dz * dz);
  const float dist = PP_CAM_DIST(0) + sep * 0.7F;

  const float cp = cosf(A.pitch);
  const float boomX = sinf(A.yaw) * cp;
  const float boomZ = cosf(A.yaw) * cp;
  const float boomY = sinf(A.pitch);

  float want = springArm(midX, midY, midZ, -boomX, -boomY, -boomZ, dist);
  if (want < CAM_MIN_DIST) want = CAM_MIN_DIST;
  if (want < sharedBoom) {
    sharedBoom = want;  // blocked: snap in, never clip
  } else {
    float k = 0.06F * g_frameScale;
    if (k > 1.0F) k = 1.0F;
    sharedBoom += (want - sharedBoom) * k;
  }
  float eyeX = midX - boomX * sharedBoom;
  float eyeY = midY - boomY * sharedBoom;
  float eyeZ = midZ - boomZ * sharedBoom;
  const float minEyeY = terrainHeightAt(eyeX, eyeZ) + 0.4F;
  if (eyeY < minEyeY) eyeY = minEyeY;
  cameraPosition = Vec4(eyeX, eyeY, eyeZ);
  cameraLookAt = Vec4(midX, midY, midZ);
}

// Player 2 join/leave (Start on pad 2, or a menu Toggle bound to "Player
// count"). Guarded: joining needs a two-player mode and a second Player
// object in the scene. Shows/hides the P2 third-person avatar and keeps the
// bound menu row's save value in sync so both entry points agree.
void TerrainGame::setPlayerTwoActive(bool active) {
  if (active && (MULTIPLAYER_MODE == 0 || players[1].objIndex < 0)) return;
  if (playerTwoActive == active) return;
  playerTwoActive = active;
  if (players[1].objIndex >= 0 && PP_MODE(1) == 2 &&
      players[1].objIndex < (int)runtimeObjects.size())
    runtimeObjects[players[1].objIndex].visible = active;
  syncPlayerCountMenuValue();
}

// Locomotion-driven clip selection for the third-person avatar. speedFrac is
// the planar speed as a fraction of full walk speed. The mapping is trivial -
// idle / walk / run by speed, jump while airborne - and the avatar's playback
// speed tracks the real speed so the feet don't slide. The escape hatch: if a
// non-locomotion clip is currently playing (a script/flow "Play Animation"
// one-shot), locomotion holds off until it finishes, then resumes. This is the
// whole "third-person for free" story: no state machine, full override.
void TerrainGame::drivePlayerAnim(PlayerCtl& P, RuntimeObject& body,
                                  float speedFrac, bool grounded) {
  if (P.objIndex < 0 || !objectGeometry[P.objIndex].animInst) return;
  const int pi = &P == &players[1] ? 1 : 0;
  body.animPlaying = true;

  int want;
  if (!grounded && P.jumpClip >= 0)
    want = P.jumpClip;
  else if (speedFrac < 0.12F)
    want = P.idleClip;
  else if (speedFrac < PP_RUN_THRESHOLD(pi) || P.runClip < 0)
    want = P.walkClip;
  else
    want = P.runClip;
  if (want < 0) want = P.idleClip;
  if (want < 0) want = 0;  // no clips mapped: hold the model's first clip

  const bool locomotion =
      body.animClip == P.idleClip || body.animClip == P.walkClip ||
      body.animClip == P.runClip || body.animClip == P.jumpClip;
  if (!locomotion && !body.animFinished) return;  // let a one-shot finish

  if (body.animClip != want) {
    body.animClip = want;
    body.animLoop = true;
    body.animRestart = true;
    body.animFade = 0.18F;  // cross-fade from the outgoing pose
  }
  // Match playback to foot speed on the moving clips (min 0.6x so a slow creep
  // still animates), otherwise the authored speed.
  const float base = body.data.animSpeed;
  if (want == P.walkClip || want == P.runClip)
    body.animSpeed = base * (speedFrac < 0.6F ? 0.6F : speedFrac);
  else
    body.animSpeed = base;
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
    // Zenith-size bias: pow(elevation fraction, SKY_ZENITH_EXP). exp 1 = linear.
    const float t0 = powf((float)st / stacks, SKY_ZENITH_EXP),
                t1 = powf((float)(st + 1) / stacks, SKY_ZENITH_EXP);
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
  // Not the shared identity `model`: renderScene keeps skyMat centered on the
  // camera so the dome follows the player instead of sitting at world origin.
  skyDome.infoBag->model = &skyMat;
  skyDome.infoBag->shadingType = TyraShadingGouraud;
  // Static geometry crossing the screen edges all the time - needs clipping
  skyDome.infoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
  skyDome.infoBag->fullClipChecks = true;
  // The dome sits past the fog end distance - hardware fog would paint it
  // solid fog color, so it opts out (the horizon still fades into the fog
  // because the terrain and objects do get fogged).
  skyDome.infoBag->fogDisabled = true;
  skyDome.colorBag = std::make_unique<StaPipColorBag>();
  skyDome.colorBag->many = skyDome.colors.data();
  skyDome.bag = std::make_unique<StaPipBag>();
  skyDome.bag->info = skyDome.infoBag.get();
  skyDome.bag->color = skyDome.colorBag.get();
  skyDome.bag->vertices = skyDome.vertices.data();
  skyDome.bag->count = static_cast<u32>(skyDome.vertices.size());
  skyDome.bag->texture = nullptr;
  skyDome.bag->lighting = nullptr;
  skyDome.bag->bboxVersion = ++g_bboxStamp;  // dome rebuilds on retint
}

void TerrainGame::rebuildObjectGeometry(int index, bool localSpace) {
  RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];
  o.dirty = false;
  g.matrixMode = localSpace;
  g_bakeLocal = localSpace;
  if (localSpace) updateObjMat(index);
  g.apronVerts.clear();  // position/size changed - the highlight ring follows
  g.hullProxyVerts.clear();  // and the shell proxy re-bakes the transform

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
    part.envNormals.clear();
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
      g_envNormals = src.reflTexture ? &part.envNormals : nullptr;
      for (size_t i = 0; i + 7 < src.verts.size(); i += 8) {
        const float* v = &src.verts[i];
        pushVert(part.vertices, part.colors, part.sts, o.data,
                 {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6], v[7], src.kd,
                 textured);
      }
    }
    g_envNormals = nullptr;
  } else {
    g_primKd = gmat ? gmat->kd : nullptr;
    g_primTextured = gmat && gmat->texture;
    g_envNormals =
        (gmat && gmat->reflTexture) ? &g.parts[0].envNormals : nullptr;
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
      case 12: addPlane(p0.vertices, p0.colors, p0.sts, o.data); break;
      case 13: {
        // Projecting decal: a world-space mesh conforming to the receiver
        // geometry was baked on the host (decalproj) - just upload it (unlit,
        // tinted; alpha comes from the texture like the flat decal). Falls back
        // to the flat quad when this object has no baked mesh. index maps 1:1 to
        // the scene table for authored objects (spawned clones are past the
        // count, so they never match and use the flat quad).
        const BakedDecal* dt =
            (currentScene >= 0 &&
             currentScene < (int)(sizeof(SCENE_DECAL_TABLES) / sizeof(SCENE_DECAL_TABLES[0])))
                ? SCENE_DECAL_TABLES[currentScene]
                : nullptr;
        if (dt && index < SCENE_DECAL_COUNTS[currentScene] && dt[index].vertCount > 0) {
          const float cs = g_primTextured ? 128.0F : 255.0F;
          const Color col(o.data.color[0] * cs, o.data.color[1] * cs,
                          o.data.color[2] * cs, 128.0F);
          const float* bv = dt[index].verts;
          for (int i = 0; i < dt[index].vertCount; ++i) {
            const float* v = &bv[i * 5];
            p0.vertices.push_back(Vec4(v[0], v[1], v[2], 1.0F));
            p0.colors.push_back(col);
            p0.sts.push_back(Vec4(v[3], v[4], 1.0F, 0.0F));
          }
        } else {
          addDecal(p0.vertices, p0.colors, p0.sts, o.data);
        }
        break;
      }
      case 14: break;  // camera - cutscene shot marker, no geometry
      case 15: {
        // mirror: only the glass quad is geometry (the reflected copies are
        // re-submitted per frame by renderMirrors). Reuses the decal quad
        // (+Z face, slight +Z nudge keeps it off a backing wall), then
        // rewrites the vertex alpha with the authored glass opacity - the
        // GS alpha-blends it over the copies drawn just before it.
        addDecal(p0.vertices, p0.colors, p0.sts, o.data);
        float opacity = 0.35F;
        for (int mi = 0; mi < MIRROR_COUNT; ++mi)
          if (MIRRORS[mi].scene == currentScene && MIRRORS[mi].object == index) {
            opacity = MIRRORS[mi].opacity;
            break;
          }
        for (Color& c : p0.colors) c.a = opacity * 128.0F;
        break;
      }
      case 16: {
        // portal: the tinted "energy surface" quad (decal quad, +Z face,
        // same +Z nudge). The live through-view is projected over it by
        // renderPortals; on unlinked/far portals - and on the pair member
        // that lost this frame's single view slot - this tint is what shows.
        addDecal(p0.vertices, p0.colors, p0.sts, o.data);
        for (Color& c : p0.colors) c.a = 70.0F;  // ~55% energy-glass alpha
        break;
      }
      default: addBox(p0.vertices, p0.colors, p0.sts, o.data); break;
    }
    g_primKd = nullptr;
    g_primTextured = false;
    g_envNormals = nullptr;
  }
  g_bakeLocal = false;

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
    part.bag->bboxVersion = ++g_bboxStamp;  // geometry changed - fresh boxes
    // Fast-path bodies render local vertices under objMat; everything else
    // sits in world space under the shared identity. Reset on every rebuild
    // (the bag may have been created under the other mode).
    part.infoBag->model = g.matrixMode ? &g.objMat : &model;

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

    // Reflective material (refl): the additive sphere-map second pass. The
    // env bag reuses this part's vertex array and bboxVersion; all-white
    // "many" colors keep its VU1 program shape identical to a textured base
    // bag, so the frustum-bbox cache entry is shared, not recomputed.
    Texture* envTex = o.data.type == 5 ? gm->parts[pi].reflTexture
                                       : (gmat ? gmat->reflTexture : nullptr);
    const float envStr = o.data.type == 5
                             ? gm->parts[pi].reflStrength
                             : (gmat ? gmat->reflStrength : 0.0F);
    if (envTex && envStr > 0.004F &&
        part.envNormals.size() == part.vertices.size()) {
      part.envColors.assign(part.vertices.size(),
                            Color(128.0F, 128.0F, 128.0F, 128.0F));
      if (!part.envBag) {
        part.envInfoBag = std::make_unique<StaPipInfoBag>();
        part.envInfoBag->model = &model;
        part.envInfoBag->shadingType = TyraShadingFlat;
        part.envInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
        part.envInfoBag->fullClipChecks = true;
        // Coplanar with the base pass: standard GEQUAL test. (TestOnly's
        // alpha-fail FB_ONLY trick corrupted close-up frames - see below.)
        part.envInfoBag->zTestType = PipelineZTest_Standard;
        // GS fog would ADD the fog color through the additive equation
        // (brightening fogged pixels) - the reflection just stays unfogged.
        part.envInfoBag->fogDisabled = true;
        part.envColorBag = std::make_unique<StaPipColorBag>();
        part.envTexBag = std::make_unique<StaPipTextureBag>();
        part.envBag = std::make_unique<StaPipBag>();
        part.envBag->info = part.envInfoBag.get();
        part.envBag->color = part.envColorBag.get();
        part.envBag->texture = part.envTexBag.get();
        part.envBag->lighting = nullptr;
      }
      // Additive equation Cv = Cs*FIX/128 + Cd; FIX 128 = full strength.
      const float fix = envStr * 128.0F + 0.5F;
      part.envInfoBag->additiveBlendFix =
          fix > 255.0F ? 255 : (fix < 1.0F ? 1 : (u8)fix);
      part.envColorBag->many = part.envColors.data();
      part.envTexBag->texture = envTex;
      // "-rounded" materials: overwrite the captured face normals with
      // directions radiating from the part centroid - a flat face then
      // sweeps a gradient of the sphere map instead of showing one uniform
      // sample (the viewport shader mirrors this via uReflRounded).
      const bool envRounded = o.data.type == 5
                                  ? gm->parts[pi].reflRounded
                                  : (gmat && gmat->reflRounded);
      if (envRounded && !part.vertices.empty()) {
        const u32 nv = static_cast<u32>(part.vertices.size());
        float cx = 0.0F, cy = 0.0F, cz = 0.0F;
        for (u32 vi = 0; vi < nv; ++vi) {
          cx += part.vertices[vi].x;
          cy += part.vertices[vi].y;
          cz += part.vertices[vi].z;
        }
        cx /= (float)nv, cy /= (float)nv, cz /= (float)nv;
        for (u32 vi = 0; vi < nv; ++vi) {
          float dx = part.vertices[vi].x - cx;
          float dy = part.vertices[vi].y - cy;
          float dz = part.vertices[vi].z - cz;
          const float l = sqrtf(dx * dx + dy * dy + dz * dz);
          if (l > 0.0001F)
            dx /= l, dy /= l, dz /= l;
          else
            dx = 0.0F, dy = 1.0F, dz = 0.0F;
          part.envNormals[vi].set(dx, dy, dz, 0.0F);
        }
      }
      // The normals ride in the ST slot and the TCE programs (cull/as_is/
      // clip) compute the matcap ST from the per-mesh camera basis - zero
      // EE per-vertex work, no pipeline barriers, in both clipping modes.
      part.envTexBag->coordinates = part.envNormals.data();
      part.envTexBag->coordinatesAreNormals = true;
      part.envBag->vertices = part.vertices.data();
      part.envBag->count = static_cast<u32>(part.vertices.size());
      part.envBag->bboxVersion = part.bag->bboxVersion;
    } else {
      part.envBag.reset();
    }
  }
}

// --- object physics: rigid-body-lite ---------------------------------------
// Every data.physics object is a body: full 3D velocity, restitution bounces
// off the terrain (real slope normals from the heightfield), friction that
// turns falls into slides and slides into stops, ground contact converting
// slide into tumble, momentum exchange between bodies and AABB contacts
// against static solids. Near-rest bodies fall asleep (restFrames) and cost
// one branch per frame until something wakes them, so a settled scene pays
// nothing. The vector work runs on VU0: Tyra::Vec4's operators, innerProduct,
// cross and normalize are VU0 macro-mode assembly.
constexpr float PHYS_REST_SPEED2 = 0.00025F;  // (units/frame)^2 = "not moving"
constexpr float PHYS_REST_SPIN = 0.75F;       // deg/frame = "not spinning"
// A mover this fast treats a SLEEPING body as a body, not a wall: pass 1
// skips the static resolution so the impulse pass sees the overlap, wakes
// the sleeper and trades momentum (~2.5 u/s at 50 fps; rest is ~0.8 u/s).
constexpr float PHYS_WAKE_SPEED2 = 0.0025F;
constexpr float PHYS_FLATTEN_STEP = 3.0F;     // settle-flatten, deg/frame
// Settle-flatten engages while the tumble is still dying (well above the
// rest-spin gate) so the lay-down continues the motion instead of starting
// after a visible dead stop.
constexpr float PHYS_FLATTEN_SPIN = 2.5F;     // deg/frame
constexpr float PHYS_MAX_SPEED = 3.0F;        // units/frame velocity clamp
constexpr float PHYS_PUSH = 0.55F;            // player shove gain (scaled 1/mass)

// World-space AABB half-extents + center offset from data.position of a solid
// object - models use their mesh AABB, exactly like collidePlayer.
void TerrainGame::physExtents(const SceneObjectData& d, const GameModel* gm,
                              const SkelModel* anim, float* cOff, float* ext) {
  cOff[0] = cOff[1] = cOff[2] = 0.0F;
  for (int a = 0; a < 3; ++a) ext[a] = 0.5F * d.scale[a];
  const float* mn = gm ? gm->mn : (anim ? anim->min : nullptr);
  const float* mx = gm ? gm->mx : (anim ? anim->max : nullptr);
  if (mn && mx) {
    for (int a = 0; a < 3; ++a) {
      cOff[a] = 0.5F * (mn[a] + mx[a]) * d.scale[a];
      ext[a] = 0.5F * (mx[a] - mn[a]) * d.scale[a];
      if (ext[a] < 0.01F) ext[a] = 0.01F;
    }
  }
}

// A moving body renders through objMat (local vertices, VU1 applies the
// motion) unless a consumer of its vertex arrays assumes world space: the
// usable-highlight hull/apron and reflective matcap normals both do, and
// animated models already ride their own animMat.
bool TerrainGame::physFastPathEligible(int index) const {
  const RuntimeObject& o = runtimeObjects[index];
  const int t = o.data.type;
  if (t != 0 && t != 1 && t != 2 && t != 3 && t != 5 && t != 12) return false;
  if (o.data.usable) return false;
  if (t == 5) {
    if (o.data.animModel >= 0) return false;
    if (o.data.model < 0 || o.data.model >= (int)gameModels.size())
      return false;
    for (const GameModelPart& mp : gameModels[o.data.model].parts)
      if (mp.reflTexture) return false;
  } else if (o.data.material >= 0 &&
             o.data.material < (int)gameMaterials.size() &&
             gameMaterials[o.data.material].reflTexture) {
    return false;
  }
  return true;
}

// Rotation columns come from the same rotated() the vertex bake uses, so the
// matrix path and the bake path can never disagree on the Euler order. Scale
// is baked into the local vertices - the basis stays unit-length.
void TerrainGame::updateObjMat(int index) {
  const RuntimeObject& o = runtimeObjects[index];
  M4x4& m = objectGeometry[index].objMat;
  const V3 bx = rotated({1.0F, 0.0F, 0.0F}, o.data.rotation);
  const V3 by = rotated({0.0F, 1.0F, 0.0F}, o.data.rotation);
  const V3 bz = rotated({0.0F, 0.0F, 1.0F}, o.data.rotation);
  m.identity();
  m.data[0] = bx.x, m.data[1] = bx.y, m.data[2] = bx.z;
  m.data[4] = by.x, m.data[5] = by.y, m.data[6] = by.z;
  m.data[8] = bz.x, m.data[9] = bz.y, m.data[10] = bz.z;
  m.data[12] = o.data.position[0];
  m.data[13] = o.data.position[1];
  m.data[14] = o.data.position[2];
}

// Solid enough to block/bump a physics body (matches collidePlayer's list).
bool TerrainGame::physObstacle(const SceneObjectData& d) {
  if (d.collision == 2) return false;
  const int t = d.type;
  return t != 4 && t != 6 && t != 7 && t != 8 && t != 9 && t != 11 &&
         t != 13 && t != 14;
}

// ---------------------------------------------------------------------------
// Static batching (STATIC_BATCHING): every StaPip submit costs ~0.7-1.5 ms
// of fixed EE overhead on real hardware regardless of vertex count, so a
// scene of many small primitive objects pays for its object COUNT, not its
// geometry (twice over in split screen). Objects flagged batchStatic at
// build time merge into combined world-space bags instead - grouped by
// material within a coarse world cell so no batch spans the whole map (a
// map-wide bbox would defeat the engine's whole-bag frustum cut, the same
// reason the terrain is chunked).
void TerrainGame::buildStaticBatchList() {
  staticBatches.clear();
  objectBatchOf.assign(SCENE_OBJECT_COUNT, -1);
  if (!STATIC_BATCHING) return;
  if (!batchInfoBag) {
    batchInfoBag = std::make_unique<StaPipInfoBag>();
    batchInfoBag->model = &model;
    batchInfoBag->shadingType = TyraShadingFlat;
    // Same rules as the per-object bags: always classify against the
    // frustum (raw submission wraps the GS raster window) with full clip
    // checks.
    batchInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
    batchInfoBag->fullClipChecks = true;
  }
  // Cell width: a quarter of the map on big maps (a 2048-unit map gets a
  // 4x4 grid), but never below 48 units - on a small map a finer grid (or
  // one straddling the origin) splits a handful of objects into
  // single-member batches and the merge wins nothing. The grid anchors at
  // the map corner (terrain is centered on the origin) for the same reason.
  const float mapW =
      TERRAIN_WIDTH > TERRAIN_DEPTH ? TERRAIN_WIDTH : TERRAIN_DEPTH;
  const float cellW = mapW * 0.25F > 48.0F ? mapW * 0.25F : 48.0F;
  std::vector<int> keyX, keyZ;  // per-batch cell, only needed while grouping
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i) {
    const SceneObjectData& d = SCENE_OBJECTS[i];
    if (!d.batchStatic) continue;
    // Materials of always-resident objects are loaded by now; a reflective
    // one draws a second additive env pass per bag - keep those objects on
    // the solo path, which already handles the env bag.
    if (d.material >= 0 && (d.material >= (int)gameMaterials.size() ||
                            gameMaterials[d.material].reflTexture))
      continue;
    const int cx = (int)floorf((d.position[0] + 0.5F * mapW) / cellW);
    const int cz = (int)floorf((d.position[2] + 0.5F * mapW) / cellW);
    int bi = -1;
    for (int b = 0; b < (int)staticBatches.size(); ++b)
      if (staticBatches[b].material == d.material && keyX[b] == cx &&
          keyZ[b] == cz) {
        bi = b;
        break;
      }
    if (bi < 0) {
      staticBatches.emplace_back();
      staticBatches.back().material = d.material;
      keyX.push_back(cx);
      keyZ.push_back(cz);
      bi = (int)staticBatches.size() - 1;
    }
    staticBatches[bi].members.push_back(i);
    objectBatchOf[i] = (short)bi;
    // The scene-load dirty flag is consumed by membership: the batch itself
    // starts dirty and bakes on the first renderScene. From here on a
    // member turning dirty means a REAL runtime mutation - the demotion
    // check in renderStaticBatches keys on exactly that.
    runtimeObjects[i].dirty = false;
  }
  int batched = 0;
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i)
    if (objectBatchOf[i] >= 0) ++batched;
  TYRA_LOG("Static batching: ", batched, " objects in ",
           (int)staticBatches.size(), " batches");
}

// One batch's bake: re-run the primitive builders for every shown member
// into the combined arrays - world-space vertices with baked lighting,
// byte-identical to what the solo path produces for the same object. The
// heap arrays are reused across rebuilds; the engine's frustum-bbox cache
// is keyed by (pointer, version), so the bboxVersion bump below is what
// keeps reused pointers from resurrecting stale boxes.
void TerrainGame::rebuildStaticBatch(StaticBatch& b) {
  b.dirty = false;
  b.vertices.clear();
  b.colors.clear();
  b.sts.clear();
  b.shown.assign(b.members.size(), 0);
  const GameMaterial* gmat =
      (b.material >= 0 && b.material < (int)gameMaterials.size())
          ? &gameMaterials[b.material]
          : nullptr;
  g_primKd = gmat ? gmat->kd : nullptr;
  g_primTextured = gmat && gmat->texture;
  for (size_t k = 0; k < b.members.size(); ++k) {
    RuntimeObject& o = runtimeObjects[b.members[k]];
    // Members are never dirty here: scene load consumes the flag at
    // grouping time and renderStaticBatches demotes dirtied members before
    // calling this.
    const bool show = o.active && o.visible;
    b.shown[k] = show ? 1 : 0;
    if (!show) continue;
    switch (o.data.type) {
      case 1: addSphere(b.vertices, b.colors, b.sts, o.data); break;
      case 2: addCylinder(b.vertices, b.colors, b.sts, o.data); break;
      case 3: addCone(b.vertices, b.colors, b.sts, o.data); break;
      case 12: addPlane(b.vertices, b.colors, b.sts, o.data); break;
      default: addBox(b.vertices, b.colors, b.sts, o.data); break;
    }
  }
  g_primKd = nullptr;
  g_primTextured = false;
  if (b.vertices.empty()) {
    b.bag.reset();
    return;
  }
  if (!b.bag) {
    b.colorBag = std::make_unique<StaPipColorBag>();
    b.bag = std::make_unique<StaPipBag>();
    b.bag->info = batchInfoBag.get();
    b.bag->color = b.colorBag.get();
    b.bag->texture = nullptr;
    b.bag->lighting = nullptr;
  }
  b.colorBag->many = b.colors.data();
  b.bag->vertices = b.vertices.data();
  b.bag->count = static_cast<u32>(b.vertices.size());
  b.bag->bboxVersion = ++g_bboxStamp;  // geometry changed - fresh boxes
  if (gmat && gmat->texture) {
    if (!b.texBag) b.texBag = std::make_unique<StaPipTextureBag>();
    b.texBag->texture = gmat->texture;
    b.texBag->coordinates = b.sts.data();
    b.bag->texture = b.texBag.get();
  } else {
    b.bag->texture = nullptr;
  }
  // World AABB for the split-screen band cull - the batch analogue of the
  // terrain chunks' build-time boxes.
  b.aabbMin[0] = b.aabbMax[0] = b.vertices[0].x;
  b.aabbMin[1] = b.aabbMax[1] = b.vertices[0].y;
  b.aabbMin[2] = b.aabbMax[2] = b.vertices[0].z;
  for (const Vec4& v : b.vertices) {
    if (v.x < b.aabbMin[0]) b.aabbMin[0] = v.x;
    if (v.x > b.aabbMax[0]) b.aabbMax[0] = v.x;
    if (v.y < b.aabbMin[1]) b.aabbMin[1] = v.y;
    if (v.y > b.aabbMax[1]) b.aabbMax[1] = v.y;
    if (v.z < b.aabbMin[2]) b.aabbMin[2] = v.z;
    if (v.z > b.aabbMax[2]) b.aabbMax[2] = v.z;
  }
}

// Per-frame batch pass: catch changed members, rebuild whatever changed,
// then submit. This is the correctness net for the runtime-only mutation
// channels (Live Link edits, Raycast/custom-node latches fed into object
// actions, global scripts) that build-time eligibility cannot rule out:
// - a DIRTIED member (position/color/... actually mutated) is DEMOTED to
//   the solo path for the rest of the scene - a per-frame-animated member
//   would otherwise re-bake its whole batch every frame, which is slower
//   than the pre-batching status quo. The batch rebuilds once without it
//   and the solo path picks it up this same frame (its dirty stays set).
// - a visibility/residency flip vs the shown snapshot (hide/show can skip
//   the dirty flag) only rebuilds the batch in place - the member may well
//   reappear, and hides are events, not per-frame animation.
void TerrainGame::renderStaticBatches() {
  for (StaticBatch& b : staticBatches) {
    bool stale = b.dirty;
    for (size_t k = 0; k < b.members.size(); ++k) {
      const RuntimeObject& o = runtimeObjects[b.members[k]];
      if (o.dirty) {
        objectBatchOf[b.members[k]] = -1;  // demote: solo from now on
        b.members.erase(b.members.begin() + (long)k);
        b.shown.erase(b.shown.begin() + (long)k);
        --k;
        stale = true;
        continue;
      }
      if (!stale &&
          b.shown[k] != (unsigned char)((o.active && o.visible) ? 1 : 0))
        stale = true;
    }
    if (stale) rebuildStaticBatch(b);
    if (!b.bag || b.bag->count == 0) continue;
    // Split halves: same band early-out the terrain chunks use.
    if (splitBandActive && outsideSplitBand(b.aabbMin, b.aabbMax)) continue;
    stapip.core.render(b.bag.get());
  }
}

void TerrainGame::updateObjectPhysics() {
  // GRAVITY is units/s^2; velocities are per-frame displacements.
  const float gravityPerFrame = GRAVITY * g_frameDt * g_frameDt;
  const float microBounce2 = gravityPerFrame * gravityPerFrame * 6.25F;
  const int count = (int)runtimeObjects.size();

  // Pass 1: integrate each awake body against the world (terrain, bounds,
  // static solids - sleeping bodies included, they are static this frame).
  for (int i = 0; i < count; ++i) {
    RuntimeObject& o = runtimeObjects[i];
    // Carried/thrown objects are driven by updateCarriedObject this frame.
    if (i == carryIndex || i == thrownIndex) continue;
    if (!o.active || !o.data.physics) continue;
    if (o.restFrames >= PHYS_SLEEP_FRAMES) continue;  // asleep

    const GameModel* gm = nullptr;
    const SkelModel* anim = nullptr;
    if (o.data.type == 5) {
      if (o.data.model >= 0 && o.data.model < (int)gameModels.size())
        gm = &gameModels[o.data.model];
      if (o.data.animModel >= 0 &&
          o.data.animModel < (int)gameAnimModels.size())
        anim = gameAnimModels[o.data.animModel].src.get();
    }
    float cOff[3], ext[3];
    physExtents(o.data, gm, anim, cOff, ext);
    // Tumbled bodies contact the world through their ROTATED bound: the
    // support extent of the OBB along each world axis (sum of |basis
    // column| * half extent) and the rotated center offset. The plain
    // axis-aligned ext let a rolled box sink corner-deep into the terrain
    // as if it were a sphere of its half-height. Spheres skip this - they
    // are rotation-invariant and their box corners would overestimate the
    // radius. (Yaw-only rotation leaves the vertical extent unchanged, so
    // authored yaw blocks rest exactly as before.)
    if (o.data.type != 1 &&
        (o.data.rotation[0] != 0.0F || o.data.rotation[1] != 0.0F ||
         o.data.rotation[2] != 0.0F)) {
      const V3 bx = rotated({1.0F, 0.0F, 0.0F}, o.data.rotation);
      const V3 by = rotated({0.0F, 1.0F, 0.0F}, o.data.rotation);
      const V3 bz = rotated({0.0F, 0.0F, 1.0F}, o.data.rotation);
      const V3 rc = rotated({cOff[0], cOff[1], cOff[2]}, o.data.rotation);
      const float lx = ext[0], ly = ext[1], lz = ext[2];
      ext[0] = fabsf(bx.x) * lx + fabsf(by.x) * ly + fabsf(bz.x) * lz;
      ext[1] = fabsf(bx.y) * lx + fabsf(by.y) * ly + fabsf(bz.y) * lz;
      ext[2] = fabsf(bx.z) * lx + fabsf(by.z) * ly + fabsf(bz.z) * lz;
      cOff[0] = rc.x, cOff[1] = rc.y, cOff[2] = rc.z;
    }
    const float radius = ext[0] > ext[2] ? ext[0] : ext[2];
    const float bounce = o.data.physBounce;

    Vec4 vel(o.velocityX, o.velocityY, o.velocityZ, 0.0F);
    vel.y -= gravityPerFrame;
    // Terminal fall velocity, 30 u/s real time (owner-tuned: 50 read as a
    // strobing blur in a door-sized infinite-fall loop, 15 as too floaty;
    // PHYS_MAX_SPEED alone would allow 150).
    const float termFall = 30.0F * g_frameDt;
    if (vel.y < -termFall) vel.y = -termFall;
    const float clampSp2 = vel.innerProduct(vel);
    if (clampSp2 > PHYS_MAX_SPEED * PHYS_MAX_SPEED)
      vel *= PHYS_MAX_SPEED / sqrtf(clampSp2);
    Vec4 pos(o.data.position[0], o.data.position[1], o.data.position[2], 1.0F);
    const Vec4 prevPos = pos;
    pos += vel;

    bool grounded = false;
    Vec4 slideTan(0.0F, 0.0F, 0.0F, 0.0F);

    // Terrain contact. The response uses the real slope normal (central
    // differences on the heightfield) so bodies kick sideways off hills and
    // slide/roll downhill instead of stopping dead inside the slope.
    const float bottomY = pos.y + cOff[1] - ext[1];
    float groundY = terrainHeightAt(pos.x, pos.z);
    // Floor-portal swallowing: a body over a linked, object-teleporting floor
    // portal ignores the terrain (see portalSwallowZone) - otherwise it rests
    // on the ground before its center can reach the plane of a portal lying on
    // (or near) the terrain, and never falls in.
    for (int pi = 0; PORTAL_COUNT > 0 && pi < PORTAL_COUNT; ++pi) {
      const PortalData& p = PORTALS[pi];
      if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
      if (!portalCanCross(p, i)) continue;
      if (p.object >= count || p.target >= count) continue;
      RuntimeObject& m = runtimeObjects[p.object];
      if (!m.active || !m.visible || !runtimeObjects[p.target].active) continue;
      if (&o == &m || &o == &runtimeObjects[p.target]) continue;
      // Swept, not point-sampled: a terminal-velocity faller crosses the
      // whole approach zone between two frames and the endpoint test would
      // let the terrain clamp stop it dead over the portal (a post-#97
      // hitch - the old portal fall code was capped at 1 u/frame and could
      // not tunnel).
      if (portalSwallowSwept(m, 0.5F * m.data.scale[0] + 0.25F,
                             0.5F * m.data.scale[1] + 0.25F, prevPos, pos)) {
        groundY = -1e30F;
        break;
      }
    }
    if (bottomY <= groundY) {
      pos.y += groundY - bottomY;
      const float hs = radius > 0.35F ? radius : 0.35F;
      Vec4 nrm(terrainHeightAt(pos.x - hs, pos.z) -
                   terrainHeightAt(pos.x + hs, pos.z),
               2.0F * hs,
               terrainHeightAt(pos.x, pos.z - hs) -
                   terrainHeightAt(pos.x, pos.z + hs),
               0.0F);
      nrm.normalize();
      const float vn = vel.innerProduct(nrm);
      if (vn < 0.0F) {
        const Vec4 vNorm = nrm * vn;
        Vec4 vTan = vel - vNorm;
        vTan *= 1.0F - o.data.physFriction * 0.18F;
        Vec4 vBounce = vNorm * -bounce;
        // kill micro-bounces so bodies settle instead of buzzing forever
        if (vBounce.innerProduct(vBounce) < microBounce2)
          vBounce = Vec4(0.0F, 0.0F, 0.0F, 0.0F);
        slideTan = vTan;
        vel = vTan + vBounce;
      }
      grounded = true;
    }

    // Terrain edges are walls: reflect instead of clamping dead.
    const float wallX = TERRAIN_WIDTH * 0.5F - 0.5F;
    const float wallZ = TERRAIN_DEPTH * 0.5F - 0.5F;
    if (pos.x > wallX) {
      pos.x = wallX;
      if (vel.x > 0.0F) vel.x = -vel.x * bounce;
    } else if (pos.x < -wallX) {
      pos.x = -wallX;
      if (vel.x < 0.0F) vel.x = -vel.x * bounce;
    }
    if (pos.z > wallZ) {
      pos.z = wallZ;
      if (vel.z > 0.0F) vel.z = -vel.z * bounce;
    } else if (pos.z < -wallZ) {
      pos.z = -wallZ;
      if (vel.z < 0.0F) vel.z = -vel.z * bounce;
    }

    // Static solids (and sleeping bodies): AABB vs AABB, resolved along the
    // axis of least penetration. Crates rest on platforms, balls bounce off
    // walls. Awake-vs-awake pairs are handled by the impulse pass below.
    const float movedX = pos.x - prevPos.x, movedZ = pos.z - prevPos.z;
    const bool movedXZ =
        movedX * movedX + movedZ * movedZ > 1e-10F;
    // Aiming into a linked opening this frame: solids fully behind that
    // portal's plane open up for this body (the walkers' doorway rule) -
    // without it the mounting wall bounces the body back ~r short of the
    // crossing plane and updatePortals never sees the pierce.
    float aimPlane[4] = {0, 0, 0, 0};
    bool aimOn = false;
    if (PORTAL_COUNT > 0) {
      const float a3[3] = {prevPos.x, prevPos.y, prevPos.z};
      // Segment end padded by the body's extent: the AABB resolution stops
      // the CENTER ~r short of a wall at the plane, so an unpadded center
      // segment never pierces and the doorway never opens.
      Vec4 mv = pos - prevPos;
      const float mvLen = sqrtf(mv.innerProduct(mv));
      float bodyR = ext[0];
      if (ext[1] > bodyR) bodyR = ext[1];
      if (ext[2] > bodyR) bodyR = ext[2];
      const float pad = mvLen > 1e-6F ? 1.0F + (bodyR + 0.1F) / mvLen : 1.0F;
      const float b3[3] = {prevPos.x + mv.x * pad, prevPos.y + mv.y * pad,
                           prevPos.z + mv.z * pad};
      const int aim = portalCarryAim(a3, b3, i);
      if (aim >= 0) {
        const RuntimeObject& pm = runtimeObjects[PORTALS[aim].object];
        const V3 pn = rotated({0.0F, 0.0F, 1.0F}, pm.data.rotation);
        aimPlane[0] = pn.x;
        aimPlane[1] = pn.y;
        aimPlane[2] = pn.z;
        aimPlane[3] = pn.x * pm.data.position[0] +
                      pn.y * pm.data.position[1] +
                      pn.z * pm.data.position[2];
        aimOn = true;
      }
    }
    for (int j = 0; j < count; ++j) {
      if (j == i) continue;
      RuntimeObject& s = runtimeObjects[j];
      if (!s.active || !physObstacle(s.data)) continue;
      const bool sSleeping =
          s.data.physics && s.restFrames >= PHYS_SLEEP_FRAMES;
      if (s.data.physics && !sSleeping) continue;  // impulse pass handles it
      // A meaningful hit treats a sleeping body as a body, not a wall: this
      // static resolution would separate the pair, and the impulse pass -
      // the one that wakes the sleeper and trades momentum - would never
      // see the overlap (a thrown crate bounced off a sleeping one without
      // waking it). Skip it and let pass 2 handle the hit; near-rest
      // contacts (resting stacks) keep the wall treatment, so settled
      // stacks stay cheap and stable.
      if (sSleeping && vel.innerProduct(vel) > PHYS_WAKE_SPEED2) continue;
      if (aimOn) {
        const V3 pax = rotated({1.0F, 0.0F, 0.0F}, s.data.rotation);
        const V3 pay = rotated({0.0F, 1.0F, 0.0F}, s.data.rotation);
        const V3 paz = rotated({0.0F, 0.0F, 1.0F}, s.data.rotation);
        const float re =
            fabsf(aimPlane[0] * pax.x + aimPlane[1] * pax.y +
                  aimPlane[2] * pax.z) *
                0.5F * s.data.scale[0] +
            fabsf(aimPlane[0] * pay.x + aimPlane[1] * pay.y +
                  aimPlane[2] * pay.z) *
                0.5F * s.data.scale[1] +
            fabsf(aimPlane[0] * paz.x + aimPlane[1] * paz.y +
                  aimPlane[2] * paz.z) *
                0.5F * s.data.scale[2];
        const float sd = aimPlane[0] * s.data.position[0] +
                         aimPlane[1] * s.data.position[1] +
                         aimPlane[2] * s.data.position[2] - aimPlane[3];
        if (sd < -re + 0.1F) continue;
      }

      const GameModel* sgm = nullptr;
      const SkelModel* sanim = nullptr;
      if (s.data.type == 5) {
        if (s.data.model >= 0 && s.data.model < (int)gameModels.size())
          sgm = &gameModels[s.data.model];
        if (s.data.animModel >= 0 &&
            s.data.animModel < (int)gameAnimModels.size())
          sanim = gameAnimModels[s.data.animModel].src.get();
      }
      float sOff[3], sExt[3];
      physExtents(s.data, sgm, sanim, sOff, sExt);

      const float dx = (s.data.position[0] + sOff[0]) - (pos.x + cOff[0]);
      const float px = sExt[0] + ext[0] - (dx < 0.0F ? -dx : dx);
      if (px <= 0.0F) {
        // A sleeping body riding on this one loses its support when we slide
        // out from under it - wake it so it falls (checked while separated
        // in X; the Z branch below never runs for those).
        if (sSleeping && movedXZ) {
          const float sb = s.data.position[1] + sOff[1] - sExt[1];
          const float myTop = prevPos.y + cOff[1] + ext[1];
          if (sb > myTop - 0.1F && sb < myTop + 0.1F &&
              px > -(radius + 0.2F)) {
            s.restFrames = 0;
            s.dirty = true;
          }
        }
        continue;
      }
      const float dy = (s.data.position[1] + sOff[1]) - (pos.y + cOff[1]);
      const float py = sExt[1] + ext[1] - (dy < 0.0F ? -dy : dy);
      if (py <= 0.0F) continue;
      const float dz = (s.data.position[2] + sOff[2]) - (pos.z + cOff[2]);
      const float pz = sExt[2] + ext[2] - (dz < 0.0F ? -dz : dz);
      if (pz <= 0.0F) continue;

      if (sSleeping && movedXZ) {
        // still overlapping in XZ but sliding: keep the rider awake too
        const float sb = s.data.position[1] + sOff[1] - sExt[1];
        const float myTop = pos.y + cOff[1] + ext[1];
        if (sb > myTop - 0.1F && sb < myTop + 0.1F) s.restFrames = 0;
      }

      if (py <= px && py <= pz) {
        const float dir = dy > 0.0F ? -1.0F : 1.0F;  // push away from s
        pos.y += dir * py;
        if (vel.y * dir < 0.0F) {
          vel.y = -vel.y * bounce;
          if (vel.y * vel.y < microBounce2) vel.y = 0.0F;
          if (dir > 0.0F) {  // landed on top of s
            grounded = true;
            slideTan = Vec4(vel.x, 0.0F, vel.z, 0.0F);
            vel.x *= 1.0F - o.data.physFriction * 0.18F;
            vel.z *= 1.0F - o.data.physFriction * 0.18F;
          }
        }
      } else if (px <= pz) {
        const float dir = dx > 0.0F ? -1.0F : 1.0F;
        pos.x += dir * px;
        if (vel.x * dir < 0.0F) vel.x = -vel.x * bounce;
      } else {
        const float dir = dz > 0.0F ? -1.0F : 1.0F;
        pos.z += dir * pz;
        if (vel.z * dir < 0.0F) vel.z = -vel.z * bounce;
      }
    }

    // Tumble: rolling without slipping (w = v / r) about the horizontal axis
    // perpendicular to the slide direction; friction bleeds it off with the
    // slide itself. Euler-added per axis - visually right, era-appropriate.
    // A latched settle-flatten owns the rotation: re-deriving spin from the
    // residual slide here would fight the ease (overshoot-and-return).
    if (o.data.physTumble && o.flatTgt[0] > 720.0F) {
      if (grounded) {
        const float ts2 = slideTan.innerProduct(slideTan);
        if (ts2 > 1e-8F) {
          const float ts = sqrtf(ts2);
          const float r = radius > 0.05F ? radius : 0.05F;
          const float degPerFrame = ts / r * 57.29578F;
          o.spin[0] = -slideTan.z / ts * degPerFrame;
          o.spin[2] = slideTan.x / ts * degPerFrame;
        } else {
          o.spin[0] *= 0.8F;
          o.spin[1] *= 0.8F;
          o.spin[2] *= 0.8F;
        }
      } else {
        for (int a = 0; a < 3; ++a) o.spin[a] *= 0.995F;  // air drag
      }
    }

    // Rest bookkeeping: near-still on the ground long enough -> sleep.
    const float speed2 = vel.innerProduct(vel);
    const float spinMag =
        fabsf(o.spin[0]) + fabsf(o.spin[1]) + fabsf(o.spin[2]);

    // Settle-flatten: a near-rest tumbled body eases onto a flat face
    // instead of sleeping on an edge or corner - pitch/roll walk to a 90deg
    // step (the crate visibly tips onto its face; the rotated support
    // extent above lowers it with the tilt). It engages while the tumble is
    // still dying (spin under PHYS_FLATTEN_SPIN, well above the rest gate)
    // and picks each target ONCE with a momentum lookahead - a crate still
    // tipping forward finishes its fall onto the NEXT face instead of being
    // yanked back to the nearest one - then takes the residual spin over so
    // the two drivers never fight. The latched targets keep the choice
    // stable while the spin decays. Euler-order trap: rotated() composes
    // Rz*Ry*Rx, and with the roll on an ODD step the pose is only flat when
    // the yaw sits on a step too - so the yaw joins the easing exactly then
    // (a settling crate twisting slightly reads as natural). Spheres skip
    // it: their orientation is invisible and easing would visibly roll the
    // baked shading for nothing. Sleep waits for the easing to finish
    // (flattening resets the countdown).
    bool flattening = false, flatMoved = false;
    if (o.data.physTumble && o.data.type != 1 && grounded &&
        speed2 < PHYS_REST_SPEED2 * 4.0F && spinMag < PHYS_FLATTEN_SPIN) {
      if (o.flatTgt[0] > 720.0F) {  // unlatched - pick the faces once
        auto pick = [&](int axis) {
          // ~20 frames of the dying spin decide whether the tip carries
          // over the balance point onto the next face
          return roundf((o.data.rotation[axis] + o.spin[axis] * 20.0F) /
                        90.0F) *
                 90.0F;
        };
        o.flatTgt[0] = pick(0);
        o.flatTgt[2] = pick(2);
        // roll on an ODD 90 step: the yaw must land on a step too
        o.flatTgt[1] = fabsf(fmodf(o.flatTgt[2], 180.0F)) > 45.0F
                           ? roundf(o.data.rotation[1] / 90.0F) * 90.0F
                           : 1e9F;
        o.spin[0] = o.spin[2] = 0.0F;  // the ease drives from here
      }
      auto ease = [&](int axis) {
        const float target = o.flatTgt[axis];
        if (target > 720.0F) return;  // yaw not eased this settle
        float d = target - o.data.rotation[axis];
        if (fabsf(d) < 0.001F) return;
        if (d > PHYS_FLATTEN_STEP) {
          d = PHYS_FLATTEN_STEP;
          flattening = true;
        } else if (d < -PHYS_FLATTEN_STEP) {
          d = -PHYS_FLATTEN_STEP;
          flattening = true;
        }
        o.data.rotation[axis] += d;
        flatMoved = true;
      };
      ease(0);
      ease(2);
      ease(1);
    } else {
      o.flatTgt[0] = o.flatTgt[1] = o.flatTgt[2] = 1e9F;  // re-pick next settle
    }

    if (grounded && speed2 < PHYS_REST_SPEED2 && spinMag < PHYS_REST_SPIN &&
        !flattening) {
      if (o.restFrames < PHYS_SLEEP_FRAMES) ++o.restFrames;
      if (o.restFrames >= PHYS_SLEEP_FRAMES) {
        vel = Vec4(0.0F, 0.0F, 0.0F, 0.0F);
        o.spin[0] = o.spin[1] = o.spin[2] = 0.0F;
        // Fast-path bodies stay on objMat while asleep - NO settle rebuild.
        // The old world re-bake refreshed the shading for the rest pose, and
        // that discrete jump from the wake-pose shading that rode the tumble
        // read as "the rotation snapped back" the instant a thrown body
        // froze (on a sphere the shade gradient is the only orientation
        // cue). Nothing needs the world-space arrays at rest: every
        // fast-path consumer (mirrors, env pass, split band, use targeting,
        // collision) reads objMat or o.data, and the two vertex-array
        // consumers (usable highlight, matcap normals) are excluded from
        // the fast path by physFastPathEligible. Trade-off: a resting body
        // keeps the shading baked at wake - the same shading it showed all
        // flight. A retint / Live Link edit still re-bakes via dirty.
      }
    } else {
      o.restFrames = 0;
    }

    // Write back; rebuild geometry only when the transform actually changed.
    const float dPos = fabsf(pos.x - o.data.position[0]) +
                       fabsf(pos.y - o.data.position[1]) +
                       fabsf(pos.z - o.data.position[2]);
    o.data.position[0] = pos.x;
    o.data.position[1] = pos.y;
    o.data.position[2] = pos.z;
    o.velocityX = vel.x;
    o.velocityY = vel.y;
    o.velocityZ = vel.z;
    if (spinMag > 0.001F) {
      for (int a = 0; a < 3; ++a) {
        o.data.rotation[a] += o.spin[a];
        if (o.data.rotation[a] > 360.0F) o.data.rotation[a] -= 720.0F;
        if (o.data.rotation[a] < -360.0F) o.data.rotation[a] += 720.0F;
      }
    }
    if (dPos > 1e-5F || spinMag > 0.001F || flatMoved) {
      // Moving: eligible bodies get ONE local-space bake and ride objMat
      // from then on (refreshed in renderScene - no EE re-bake per frame);
      // the rest fall back to the legacy world-space rebuild.
      ObjectGeometry& g = objectGeometry[i];
      if (!g.matrixMode && !o.dirty && physFastPathEligible(i))
        rebuildObjectGeometry(i, true);
      if (!g.matrixMode) o.dirty = true;
    }
  }

  // Pass 2: momentum exchange between bodies. Upright-cylinder contacts (XZ
  // circle + Y interval) resolved along the axis of least penetration,
  // impulses split by mass; hitting a sleeping body wakes it. Pairs where
  // both sleep are skipped, so settled stacks stay free.
  for (int i = 0; i < count; ++i) {
    RuntimeObject& a = runtimeObjects[i];
    if (i == carryIndex || i == thrownIndex) continue;  // driven by the carry
    if (!a.active || !a.data.physics || !physObstacle(a.data)) continue;
    for (int j = i + 1; j < count; ++j) {
      RuntimeObject& b = runtimeObjects[j];
      if (j == carryIndex || j == thrownIndex) continue;
      if (!b.active || !b.data.physics || !physObstacle(b.data)) continue;
      if (a.restFrames >= PHYS_SLEEP_FRAMES &&
          b.restFrames >= PHYS_SLEEP_FRAMES)
        continue;

      float aOff[3], aExt[3], bOff[3], bExt[3];
      const GameModel* gm = nullptr;
      const SkelModel* anim = nullptr;
      if (a.data.type == 5) {
        if (a.data.model >= 0 && a.data.model < (int)gameModels.size())
          gm = &gameModels[a.data.model];
        if (a.data.animModel >= 0 &&
            a.data.animModel < (int)gameAnimModels.size())
          anim = gameAnimModels[a.data.animModel].src.get();
      }
      physExtents(a.data, gm, anim, aOff, aExt);
      gm = nullptr;
      anim = nullptr;
      if (b.data.type == 5) {
        if (b.data.model >= 0 && b.data.model < (int)gameModels.size())
          gm = &gameModels[b.data.model];
        if (b.data.animModel >= 0 &&
            b.data.animModel < (int)gameAnimModels.size())
          anim = gameAnimModels[b.data.animModel].src.get();
      }
      physExtents(b.data, gm, anim, bOff, bExt);

      const float dy = (b.data.position[1] + bOff[1]) -
                       (a.data.position[1] + aOff[1]);
      const float ph = aExt[1] + bExt[1] - (dy < 0.0F ? -dy : dy);
      if (ph <= 0.0F) continue;
      const float rA = aExt[0] > aExt[2] ? aExt[0] : aExt[2];
      const float rB = bExt[0] > bExt[2] ? bExt[0] : bExt[2];
      float dx = b.data.position[0] - a.data.position[0];
      float dz = b.data.position[2] - a.data.position[2];
      const float d2 = dx * dx + dz * dz;
      const float rSum = rA + rB;
      if (d2 >= rSum * rSum) continue;

      const float invMa = 1.0F / (a.data.physMass < 0.05F ? 0.05F : a.data.physMass);
      const float invMb = 1.0F / (b.data.physMass < 0.05F ? 0.05F : b.data.physMass);
      const float invSum = invMa + invMb;
      const float e = a.data.physBounce > b.data.physBounce ? a.data.physBounce
                                                            : b.data.physBounce;
      const float dist = sqrtf(d2 > 1e-8F ? d2 : 1e-8F);
      const float pr = rSum - dist;

      float nx, ny, nz;  // contact normal, a -> b
      float pen;
      if (ph < pr) {  // vertical contact (landing on / popping out from under)
        nx = 0.0F;
        ny = dy >= 0.0F ? 1.0F : -1.0F;
        nz = 0.0F;
        pen = ph;
      } else {  // side contact
        if (d2 > 1e-8F) {
          nx = dx / dist;
          nz = dz / dist;
        } else {  // dead center overlap: split along X deterministically
          nx = 1.0F;
          nz = 0.0F;
        }
        ny = 0.0F;
        pen = pr;
      }

      // separate the pair, then trade momentum along the normal
      const float sepA = pen * (invMa / invSum), sepB = pen * (invMb / invSum);
      a.data.position[0] -= nx * sepA;
      a.data.position[1] -= ny * sepA;
      a.data.position[2] -= nz * sepA;
      b.data.position[0] += nx * sepB;
      b.data.position[1] += ny * sepB;
      b.data.position[2] += nz * sepB;

      const float relN = (b.velocityX - a.velocityX) * nx +
                         (b.velocityY - a.velocityY) * ny +
                         (b.velocityZ - a.velocityZ) * nz;
      if (relN < 0.0F) {  // approaching
        const float jImp = -(1.0F + e) * relN / invSum;
        a.velocityX -= nx * jImp * invMa;
        a.velocityY -= ny * jImp * invMa;
        a.velocityZ -= nz * jImp * invMa;
        b.velocityX += nx * jImp * invMb;
        b.velocityY += ny * jImp * invMb;
        b.velocityZ += nz * jImp * invMb;
      }
      a.restFrames = 0;
      b.restFrames = 0;
      // Fast-path bodies pick the separation up through objMat next render;
      // forcing dirty would throw away their local bake every contact frame.
      if (!objectGeometry[i].matrixMode) a.dirty = true;
      if (!objectGeometry[j].matrixMode) b.dirty = true;
    }
  }
}

void TerrainGame::pushPhysicsBodies(float prevX, float prevZ, float nextX,
                                    float nextZ, float feetY,
                                    float eyeHeight) {
  const float mx = nextX - prevX, mz = nextZ - prevZ;
  const float m2 = mx * mx + mz * mz;
  if (m2 < 1e-10F) return;
  const int count = (int)runtimeObjects.size();
  for (int i = 0; i < count; ++i) {
    RuntimeObject& o = runtimeObjects[i];
    if (!o.active || !o.data.physics || !physObstacle(o.data)) continue;

    const GameModel* gm = nullptr;
    const SkelModel* anim = nullptr;
    if (o.data.type == 5) {
      if (o.data.model >= 0 && o.data.model < (int)gameModels.size())
        gm = &gameModels[o.data.model];
      if (o.data.animModel >= 0 &&
          o.data.animModel < (int)gameAnimModels.size())
        anim = gameAnimModels[o.data.animModel].src.get();
    }
    float cOff[3], ext[3];
    physExtents(o.data, gm, anim, cOff, ext);

    // capsule overlap in Y (a body under our feet is floor, not a shove)
    const float top = o.data.position[1] + cOff[1] + ext[1];
    const float bottom = o.data.position[1] + cOff[1] - ext[1];
    if (bottom > feetY + eyeHeight || top < feetY + 0.15F) continue;

    const float radius = ext[0] > ext[2] ? ext[0] : ext[2];
    const float dx = o.data.position[0] - nextX;
    const float dz = o.data.position[2] - nextZ;
    const float reach = radius + 0.45F;  // player radius + a skin
    const float d2 = dx * dx + dz * dz;
    if (d2 > reach * reach) continue;
    if (dx * mx + dz * mz <= 0.0F) continue;  // walking away from it

    const float inv = 1.0F / sqrtf(d2 > 1e-8F ? d2 : 1e-8F);
    const float mass = o.data.physMass < 0.05F ? 0.05F : o.data.physMass;
    const float push = sqrtf(m2) * PHYS_PUSH / mass;
    o.velocityX += dx * inv * push;
    o.velocityZ += dz * inv * push;
    o.restFrames = 0;  // velocity-only change: the physics pass moves it
  }
}

void TerrainGame::renderScene() {
  // Debug profiler: scene phase = sky + terrain + objects + anim (+ the
  // deferred usable bodies, timed separately below). Folded away entirely
  // when DEBUG_SHOW_PROFILER is false. See drawDebugHud.
  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;
  // Split halves: bound the visible vertical band once per pass; the chunk
  // and static-object submissions below early-out against it.
  splitBandActive = splitPassActive;
  if (splitBandActive) computeSplitBand();
  // Scripts changing ctx.skyColor retint the dome horizon
  if (skyDome.bag && (scriptCtx.skyColor.r != skyHorizonR ||
                      scriptCtx.skyColor.g != skyHorizonG ||
                      scriptCtx.skyColor.b != skyHorizonB)) {
    skyHorizonR = scriptCtx.skyColor.r;
    skyHorizonG = scriptCtx.skyColor.g;
    skyHorizonB = scriptCtx.skyColor.b;
    buildSkyDome();
  }
  // Reflective materials: camera basis for the sphere-map STs. Matcap UVs
  // from the camera-space normal - u along the camera's right, v (image
  // space, 0 = top) against its up. The editor viewport shader and the VU1
  // CalculateTyraEnvStq macro mirror this formula - keep them in sync.
  V3 envFwd = {cameraLookAt.x - cameraPosition.x,
               cameraLookAt.y - cameraPosition.y,
               cameraLookAt.z - cameraPosition.z};
  {
    const float l =
        sqrtf(envFwd.x * envFwd.x + envFwd.y * envFwd.y + envFwd.z * envFwd.z);
    if (l > 0.0001F) envFwd.x /= l, envFwd.y /= l, envFwd.z /= l;
  }
  V3 envRight = {-envFwd.z, 0.0F, envFwd.x};  // cross(fwd, worldUp)
  {
    const float l = sqrtf(envRight.x * envRight.x + envRight.z * envRight.z);
    if (l > 0.0001F)
      envRight.x /= l, envRight.z /= l;
    else
      envRight = {1.0F, 0.0F, 0.0F};  // looking straight up/down
  }
  const V3 envUp = {envRight.y * envFwd.z - envRight.z * envFwd.y,
                    envRight.z * envFwd.x - envRight.x * envFwd.z,
                    envRight.x * envFwd.y - envRight.y * envFwd.x};

  // Dynamic env map ("@sky" materials): render the sky dome into the
  // engine's 128x128 VRAM target from a level wide-FOV view along the
  // camera forward - the GT3 trick, reflections follow the live sky (script
  // retints included). Runs before any main-frame 3D so the raster redirect
  // brackets only the dome submission. Refreshed every SECOND frame (also
  // the GT3 trick): the target persists in VRAM, and a 25/30 Hz update of
  // a blurry 128px reflection is imperceptible while the pass costs a
  // couple of ms per hit on real hardware.
  static bool envMapTick = false;  // first frame MUST render (fresh VRAM)
  envMapTick = !envMapTick;
  // Not inside a split half: the env bracket's end() restores a full-screen
  // raster, which would undo the half's scissor/offset. Reflections keep the
  // last rendered map while split-screen is active.
  if (g_dynamicEnvUsers > 0 && skyDome.bag && envMapTick && !splitPassActive) {
    auto& core = engine->renderer.core;
    skyMat.identity();
    skyMat.data[12] = cameraPosition.x;
    skyMat.data[13] = cameraPosition.y;
    skyMat.data[14] = cameraPosition.z;
    // Level forward: keeps the sphere map's horizon on its center line.
    V3 lvl = {envFwd.x, 0.0F, envFwd.z};
    const float ll = sqrtf(lvl.x * lvl.x + lvl.z * lvl.z);
    if (ll > 0.0001F)
      lvl.x /= ll, lvl.z /= ll;
    else
      lvl = {1.0F, 0.0F, 0.0F};
    Vec4 envLook(cameraPosition.x + lvl.x, cameraPosition.y,
                 cameraPosition.z + lvl.z, 1.0F);
    core.envMap.begin(Color(scriptCtx.skyColor.r, scriptCtx.skyColor.g,
                            scriptCtx.skyColor.b, 128.0F));
    core.renderer3D.pushEnvView(cameraPosition, envLook, 110.0F,
                                (float)Tyra::RendererCoreEnvMap::size);
    const Tyra::PipelineZTest prevZTest = skyDome.infoBag->zTestType;
    skyDome.infoBag->zTestType = PipelineZTest_AllPass;
    stapip.core.render(skyDome.bag.get());
    skyDome.infoBag->zTestType = prevZTest;
    // "Show in reflections" objects render into the map too - base passes
    // only (no env pass inside the env pass), depth-tested against the
    // target's dedicated z-buffer so they occlude each other correctly.
    for (int ri = 0; ri < (int)runtimeObjects.size(); ++ri) {
      RuntimeObject& ro = runtimeObjects[ri];
      if (!ro.active || !ro.visible || !ro.data.reflected) continue;
      // Skip the object the camera is standing at: it would swamp the whole
      // map - typically the very reflective surface being inspected, whose
      // own dark self-reflection reads as ugly patches up close. (Bounding
      // radius approximated from the scale; unit primitives fit a 1x1x1
      // cube, so half-diagonal = 0.87 * max scale.)
      {
        float half = ro.data.scale[0];
        if (ro.data.scale[1] > half) half = ro.data.scale[1];
        if (ro.data.scale[2] > half) half = ro.data.scale[2];
        const float skipR = 0.87F * half * 1.9F;
        const float sdx = ro.data.position[0] - cameraPosition.x;
        const float sdy = ro.data.position[1] - cameraPosition.y;
        const float sdz = ro.data.position[2] - cameraPosition.z;
        if (sdx * sdx + sdy * sdy + sdz * sdz < skipR * skipR) continue;
      }
      if (ro.dirty) rebuildObjectGeometry(ri);
      if (objectGeometry[ri].matrixMode) updateObjMat(ri);
      for (GeoPart& part : objectGeometry[ri].parts)
        if (part.bag) stapip.core.render(part.bag.get());
    }
    core.renderer3D.popEnvView(CameraInfo3D(&cameraPosition, &cameraLookAt));
    core.envMap.end();
  }

  // Portal through-view: rendered IN-PLACE into the real framebuffer at
  // full resolution, every frame (the view must stay in lockstep with the
  // player camera or the surface visibly lags). Must run right after the
  // frame clear, before any main-scene 3D - the z-carved opening survives
  // the main scene drawing around it (see renderPortalView).
  renderPortalView();

  if (skyDome.bag) {
    // Follow the camera: park the dome's centre on the eye so however big the
    // map is, the horizon and zenith always wrap around the player. Only the
    // translation moves; the dome vertices never rebuild for this.
    skyMat.identity();
    skyMat.data[12] = cameraPosition.x;
    skyMat.data[13] = cameraPosition.y;
    skyMat.data[14] = cameraPosition.z;
    stapip.core.render(skyDome.bag.get());
  }
  // Terrain: stream the chunk ring around the view focus (budgeted, so the
  // build cost spreads over frames), then submit the built chunks - the
  // engine drops whole out-of-frustum chunks EE-side (main-bbox classify)
  // before any packaging or clipping work happens.
  // One streaming update per FRAME (the split's second pass reuses it) with
  // player 2's avatar as a second focus while active - each player keeps the
  // terrain around them resident even when the pair walks apart.
  if (!splitSecondPass) {
    const bool p2Focus = playerTwoActive && players[1].objIndex >= 0;
    updateTerrainChunks(cameraLookAt.x, cameraLookAt.z,
                        p2Focus ? players[1].x : 0.0F,
                        p2Focus ? players[1].z : 0.0F, p2Focus, 2);
  }
  renderTerrain();
  // Static batches: one submit per material x cell group of the non-moving
  // primitives (rebuilt first when a member changed). Opaque z-tested
  // geometry, so drawing before the solo objects is order-free.
  renderStaticBatches();
  // Highlighted-in-reach usables get a separate shell pass after the scene.
  // RIM mode (default): the body is deferred out of the main pass and drawn
  // AFTER its shells, erasing the shell wash over the object's own receding
  // faces without a second full draw (the old order needed main-pass draw +
  // repaint - two exact-clip passes per frame). OVERLAY mode: the body draws
  // normally in the main pass and the shells are painted ON it afterwards, so
  // it stays in this list only for the shell pass.
  auto renderEnvPass = [&](GeoPart& part) {
    if (!part.envBag) return;
    // TCE programs compute the matcap ST on VU1 - the EE only refreshes the
    // per-mesh camera basis here.
    part.envTexBag->envRight.set(envRight.x, envRight.y, envRight.z, 0.0F);
    part.envTexBag->envUp.set(envUp.x, envUp.y, envUp.z, 0.0F);
    stapip.core.render(part.envBag.get());
  };
  int hlList[8];
  float hlListD2[8];
  int hlCount = 0;
  const bool hlActive = HIGHLIGHT_USABLE;
  const bool hlOverlay = HIGHLIGHT_OVERLAY;
  for (int i = 0; i < (int)runtimeObjects.size(); ++i) {
    if (!runtimeObjects[i].active) continue;  // streamed out with its layer
    // Batched members render via renderStaticBatches above; their dirty flag
    // is consumed by the batch rebuild, never by the solo path.
    if (i < (int)objectBatchOf.size() && objectBatchOf[i] >= 0) continue;
    if (runtimeObjects[i].dirty) rebuildObjectGeometry(i);
    // Fast-path bodies: this matrix refresh is their whole per-frame render
    // cost - it also folds in pass-2 separations and player pushes applied
    // after the physics integration wrote the matrix.
    if (objectGeometry[i].matrixMode) updateObjMat(i);
    if (!runtimeObjects[i].visible) continue;
    if (beyondDrawDistance(runtimeObjects[i].data, cameraPosition)) continue;
    // Split halves: whole objects above/below the visible band skip here.
    if (splitBandActive && objectOutsideSplitBand(i)) continue;
    // mirrors draw after the scene (copies first, then the blended glass -
    // see renderMirrors); drawing the quad here would z-write the plane and
    // reject the reflected geometry behind it. Portals blend their tinted
    // surface after the scene too (renderPortals), with the live
    // through-view projected over it.
    if (runtimeObjects[i].data.type == 15 || runtimeObjects[i].data.type == 16)
      continue;
    if (hlActive && hlCount < 8 && runtimeObjects[i].data.usable &&
        highlightInReach(i)) {
      const float ddx = runtimeObjects[i].data.position[0] - cameraPosition.x;
      const float ddy = runtimeObjects[i].data.position[1] - cameraPosition.y;
      const float ddz = runtimeObjects[i].data.position[2] - cameraPosition.z;
      hlList[hlCount] = i;
      hlListD2[hlCount++] = ddx * ddx + ddy * ddy + ddz * ddz;
      if (!hlOverlay) continue;  // rim: defer body; overlay: draw it now
    }
    for (GeoPart& part : objectGeometry[i].parts)
      if (part.bag) {
        stapip.core.render(part.bag.get());
        renderEnvPass(part);
      }
  }
  // Animated models: advance playback, then skin + draw the in-view ones
  // through the same static pipeline (see updateAndRenderAnimObjects)
  updateAndRenderAnimObjects();
  // Mirrors after the whole scene (including the skinned avatars their
  // copies re-use): reflected copies first, glass quads blended over them
  renderMirrors();
  // Portals after the mirrors: tinted quads blended over the finished
  // scene, then the live through-view projected onto the winner's surface
  // (z-tested against the scene, so walls still occlude the portal).
  renderPortals();
  if (DEBUG_SHOW_PROFILER) g_profScene += profTicks() - profScene0;
  // Highlight shells after the whole scene so they depth-test against the
  // finished z-buffer and can't be punched through by a later draw. Sorted
  // far-to-near: a nearer object's rim/body correctly covers a farther one.
  for (int a = 0; a < hlCount; ++a)  // insertion sort, farthest first
    for (int b = a + 1; b < hlCount; ++b)
      if (hlListD2[b] > hlListD2[a]) {
        const int ti = hlList[a];
        hlList[a] = hlList[b];
        hlList[b] = ti;
        const float td = hlListD2[a];
        hlListD2[a] = hlListD2[b];
        hlListD2[b] = td;
      }
  for (int a = 0; a < hlCount; ++a) {
    const int i = hlList[a];
    const u32 ph = DEBUG_SHOW_PROFILER ? profTicks() : 0;
    renderHighlightHull(i);  // shells + ground apron = the highlight overhead
    if (DEBUG_SHOW_PROFILER) g_profHighlight += profTicks() - ph;
    if (!hlOverlay) {
      // Rim mode: paint the deferred body over the shells (GEQUAL wins the
      // equal-depth test, erasing the wash). Real geometry - count it as
      // scene, not highlight overhead.
      const u32 pb = DEBUG_SHOW_PROFILER ? profTicks() : 0;
      for (GeoPart& part : objectGeometry[i].parts)
        if (part.bag) {
          stapip.core.render(part.bag.get());
          renderEnvPass(part);
        }
      if (DEBUG_SHOW_PROFILER) g_profScene += profTicks() - pb;
    }
  }
  // particles last - alpha blended over the scene. The second split half
  // re-faces the quads at ITS camera first - billboards built during the
  // simulation face player 1's view.
  const u32 profPart0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;
  // The split's second half re-aims the billboard basis at ITS camera
  // before submitting - centers are view-independent, so two Vec4 writes
  // per system replace any re-simulation (the portal through-view trick).
  if (splitSecondPass && !particles.empty()) {
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
      if (!ps.bag || ps.bag->count == 0) continue;
      const int kind = runtimeObjects[ps.objectIndex].data.emitKind;
      ps.billboardBag->right = Vec4(rx, 0.0F, rz, 0.0F);
      ps.billboardBag->up = kind == 4 ? Vec4(0.0F, 1.0F, 0.0F, 0.0F)
                                      : Vec4(ux, uy, uz, 0.0F);
    }
  }
  for (ParticleSystem& ps : particles)
    if (ps.bag && ps.bag->count > 0) stapip.core.render(ps.bag.get());
  if (DEBUG_SHOW_PROFILER) g_profParticles += profTicks() - profPart0;
}

// Mirror objects (type 15): the PS2-era mirror. Every listed target is
// submitted a SECOND time under a reflection matrix about the glass plane -
// VU1 re-transforms the target's live vertex arrays (the same trick as the
// highlight shells re-submitting under hullMat), so the EE never touches a
// vertex and moving or animated targets reflect their current frame for
// free. The copies are real geometry on the far side of the plane: build
// the mirror into a wall (or give it a backing) so only the glass reveals
// them. Winding flips under a reflection, but the GS draws both faces, so
// no triangle reordering is needed. Draw order: copies first (plain
// z-tested scene geometry), then the tinted glass quad alpha-blends over
// them; highlight shells and particles still sort on top afterwards.
void TerrainGame::renderMirrors() {
  for (int mi = 0; mi < MIRROR_COUNT; ++mi) {
    const MirrorData& mir = MIRRORS[mi];
    if (mir.scene != currentScene || mir.object < 0 ||
        mir.object >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& m = runtimeObjects[mir.object];
    if (!m.active || !m.visible) continue;
    if (beyondDrawDistance(m.data, cameraPosition)) continue;

    // Householder reflection about the glass plane (normal = the mirror's
    // rotated +Z, through its live position): x' = x - 2*((x . n) - d) * n
    V3 n = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
    const float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (nl > 0.0001F) n.x /= nl, n.y /= nl, n.z /= nl;
    const float d = n.x * m.data.position[0] + n.y * m.data.position[1] +
                    n.z * m.data.position[2];
    mirrorMat.identity();
    mirrorMat.data[0] = 1.0F - 2.0F * n.x * n.x;
    mirrorMat.data[1] = -2.0F * n.x * n.y;
    mirrorMat.data[2] = -2.0F * n.x * n.z;
    mirrorMat.data[4] = -2.0F * n.x * n.y;
    mirrorMat.data[5] = 1.0F - 2.0F * n.y * n.y;
    mirrorMat.data[6] = -2.0F * n.y * n.z;
    mirrorMat.data[8] = -2.0F * n.x * n.z;
    mirrorMat.data[9] = -2.0F * n.y * n.z;
    mirrorMat.data[10] = 1.0F - 2.0F * n.z * n.z;
    mirrorMat.data[12] = 2.0F * d * n.x;
    mirrorMat.data[13] = 2.0F * d * n.y;
    mirrorMat.data[14] = 2.0F * d * n.z;

    for (int t = 0; t < mir.targetCount; ++t)
      renderMirroredObject(MIRROR_TARGETS[mir.firstTarget + t]);
    if (mir.reflectPlayer) {
      // only a visible body reflects: the third-person avatar is a normal
      // runtime object (visible only in mode 2), so the FPP player - no
      // body - is skipped by the visibility check inside
      const int pi = PLAYER_INDEXES[currentScene];
      if (pi >= 0) renderMirroredObject(pi);
    }

    // the glass quad itself, alpha-blended over the copies (its vertex
    // alpha carries the opacity - see rebuildObjectGeometry case 15)
    for (GeoPart& part : objectGeometry[mir.object].parts)
      if (part.bag) stapip.core.render(part.bag.get());
  }
}

// One reflected copy: re-submit the target's live bags under mirrorMat.
// Static parts hold world-space vertices (the reflection maps world to
// world); animated parts hold model-space vertices under animMat, so the
// copy composes reflection * animMat. The swapped-in matrix pointer is
// consumed during render() (packets are built synchronously - the highlight
// shells rewrite hullMat between submits on the same fact), so restoring it
// right after the call is safe.
void TerrainGame::renderMirroredObject(int index) {
  if (index < 0 || index >= (int)runtimeObjects.size()) return;
  RuntimeObject& o = runtimeObjects[index];
  // no mirror-in-mirror: a mirror listing another mirror would recurse the
  // illusion with a matrix that is only valid for the outer plane
  if (!o.active || !o.visible || o.data.type == 15) return;
  if (beyondDrawDistance(o.data, cameraPosition)) return;
  ObjectGeometry& g = objectGeometry[index];
  // Fast-path bodies hold local vertices under objMat - the copy composes
  // reflection * objMat, exactly like the animated path right below.
  if (g.matrixMode) mirrorObjMat = mirrorMat * g.objMat;
  for (GeoPart& part : g.parts) {
    if (!part.bag) continue;
    part.infoBag->model = g.matrixMode ? &mirrorObjMat : &mirrorMat;
    stapip.core.render(part.bag.get());
    part.infoBag->model = g.matrixMode ? &g.objMat : &model;
  }
  if (g.animInfoBag && !g.animParts.empty()) {
    // The anim bags point at whatever updateAndRenderAnimObjects last
    // skinned - an on-screen target reflects its exact current pose; a
    // target that skipped this frame's skinning reflects its held pose.
    mirrorAnimMat = mirrorMat * g.animMat;
    g.animInfoBag->model = &mirrorAnimMat;
    for (ObjectGeometry::AnimPart& ap : g.animParts)
      if (ap.bag && ap.bag->count > 0) stapip.core.render(ap.bag.get());
    g.animInfoBag->model = &g.animMat;
  }
}

// Portal objects (type 16): a PS2-honest take on the seamless portal. The
// through-view is a real second render - the player camera mapped through
// the pair (so it is always in sync with the player's), drawn into the
// engine's 128x128 portal VRAM target through the normal VU1 static
// pipeline (transform/clip on VU1, camera math on the VU0-macro Vec4/M4x4
// ops). The surface then samples that target with SCREEN-LOCKED UVs: since
// the virtual camera shares the main camera's fov/aspect, the destination
// appears in the target exactly where the portal quad sits on screen, so
// sampling at the fragment's own screen position yields correct
// parallax - no reprojection per pixel, just one textured fan. Budget: ONE
// portal view per frame (the nearest linked portal the camera faces);
// every other portal shows its tinted quad. The teleport (updatePortals)
// uses the same mapping, so what you see through the surface is exactly
// where you arrive.

// The player camera mapped through the portal pair: world -> source local
// frame -> 180 deg flip about local Y (walk INTO the front, come OUT of the
// target's front) -> target frame -> world.
bool TerrainGame::portalCamera(int pi, Vec4* outEye, Vec4* outAt) {
  const PortalData& p = PORTALS[pi];
  if (p.target < 0 || p.target >= (int)runtimeObjects.size()) return false;
  RuntimeObject& src = runtimeObjects[p.object];
  RuntimeObject& dst = runtimeObjects[p.target];
  if (!dst.active) return false;
  const V3 sxA = rotated({1.0F, 0.0F, 0.0F}, src.data.rotation);
  const V3 syA = rotated({0.0F, 1.0F, 0.0F}, src.data.rotation);
  const V3 szA = rotated({0.0F, 0.0F, 1.0F}, src.data.rotation);
  const V3 dxA = rotated({1.0F, 0.0F, 0.0F}, dst.data.rotation);
  const V3 dyA = rotated({0.0F, 1.0F, 0.0F}, dst.data.rotation);
  const V3 dzA = rotated({0.0F, 0.0F, 1.0F}, dst.data.rotation);
  auto mapPoint = [&](const Vec4& wp, Vec4* out) {
    const float rx = wp.x - src.data.position[0];
    const float ry = wp.y - src.data.position[1];
    const float rz = wp.z - src.data.position[2];
    // R^T: project onto the source axes (they are orthonormal)
    float lx = rx * sxA.x + ry * sxA.y + rz * sxA.z;
    const float ly = rx * syA.x + ry * syA.y + rz * syA.z;
    float lz = rx * szA.x + ry * szA.y + rz * szA.z;
    lx = -lx;  // the 180 deg flip about local Y
    lz = -lz;
    out->set(dst.data.position[0] + dxA.x * lx + dyA.x * ly + dzA.x * lz,
             dst.data.position[1] + dxA.y * lx + dyA.y * ly + dzA.y * lz,
             dst.data.position[2] + dxA.z * lx + dyA.z * ly + dzA.z * lz,
             1.0F);
  };
  mapPoint(cameraPosition, outEye);
  mapPoint(cameraLookAt, outAt);
  return true;
}

// Render the through-view of the best on-screen portal IN-PLACE: full-res
// into the real framebuffer, right after the frame clear and before any
// main-scene 3D. The GS has no stencil, so the shaped opening is carved
// with the z-buffer instead (RendererCore::portalViewBegin/End): the
// destination view renders scissored to the quad's screen bbox, then the
// bbox depths are re-farred, the quad interior is capped at the surface
// depth (walls in front still occlude the view, the wall behind loses) and
// the spilled ring outside the opening is repainted with the clear color.
// The main scene then draws around it and the opening survives - crisp,
// no texture resample, no seam. Content = sky dome + terrain (portal's
// showTerrain flag) plus the explicit view-object list - the Mirror
// philosophy: the second-render cost is always visible to the author.
// Animated targets re-use their last skinned pose (skinning runs later in
// the frame).
void TerrainGame::renderPortalView() {
  if ((int)portalLiveFlags.size() != PORTAL_COUNT)
    portalLiveFlags.assign(PORTAL_COUNT ? PORTAL_COUNT : 1, 0);
  for (int pi = 0; pi < PORTAL_COUNT; ++pi) portalLiveFlags[pi] = 0;
  if (PORTAL_COUNT == 0) return;
  // Collect the qualifying portals (linked, alive, camera on the front
  // side), nearest-first, and render up to four views. Carve order is
  // FARTHEST first: where two openings overlap on screen, the nearer
  // portal's bbox re-clears the farther's work and wins - the same rule
  // real occlusion would give.
  const int kMaxViews = 4;
  int order[8];
  float d2s[8];
  int cnt = 0;
  for (int pi = 0; pi < PORTAL_COUNT; ++pi) {
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
    if (p.object >= (int)runtimeObjects.size()) continue;
    RuntimeObject& m = runtimeObjects[p.object];
    if (!m.active || !m.visible) continue;
    if (beyondDrawDistance(m.data, cameraPosition)) continue;
    if (p.target >= (int)runtimeObjects.size() ||
        !runtimeObjects[p.target].active)
      continue;
    // camera must be on the front (+Z) side - the back face never shows a view
    const V3 n = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
    const float relX = cameraPosition.x - m.data.position[0];
    const float relY = cameraPosition.y - m.data.position[1];
    const float relZ = cameraPosition.z - m.data.position[2];
    if (relX * n.x + relY * n.y + relZ * n.z <= 0.0F) continue;
    const float d2 = relX * relX + relY * relY + relZ * relZ;
    // bounded shortlist (no allocation): keep the 8 nearest candidates
    if (cnt < 8) {
      order[cnt] = pi;
      d2s[cnt] = d2;
      ++cnt;
    } else {
      int worst = 0;
      for (int i = 1; i < 8; ++i)
        if (d2s[i] > d2s[worst]) worst = i;
      if (d2 < d2s[worst]) {
        order[worst] = pi;
        d2s[worst] = d2;
      }
    }
  }
  if (cnt == 0) return;
  for (int a = 0; a < cnt; ++a)  // nearest-first
    for (int b = a + 1; b < cnt; ++b)
      if (d2s[b] < d2s[a]) {
        const float td = d2s[a];
        d2s[a] = d2s[b];
        d2s[b] = td;
        const int ti = order[a];
        order[a] = order[b];
        order[b] = ti;
      }
  const int views = cnt < kMaxViews ? cnt : kMaxViews;
  for (int v = views - 1; v >= 0; --v) {  // farthest of the selected first
    if (renderOnePortalView(order[v])) portalLiveFlags[order[v]] = 1;
  }
}

// One in-place through-view. Returns true when the opening was actually
// carved (the quad survived the frustum clip and its bbox is on screen).
bool TerrainGame::renderOnePortalView(int pi) {
  Vec4 eye, at;
  if (!portalCamera(pi, &eye, &at)) return false;
  const PortalData& p = PORTALS[pi];
  RuntimeObject& m = runtimeObjects[p.object];

  // The quad's screen footprint under the MAIN camera. Corners on the same
  // +Z-nudged plane as the tint quad (addDecal's 0.02 local nudge), clipped
  // in clip space against the near plane and the four screen edges
  // (Sutherland-Hodgman, <=9 verts - a handful of flops on the EE), then
  // projected to GS screen coordinates and reversed-z depths (the same
  // mapping the VU1 path writes: near -> 0xFFFFFF).
  const V3 ax = rotated({1.0F, 0.0F, 0.0F}, m.data.rotation);
  const V3 ay = rotated({0.0F, 1.0F, 0.0F}, m.data.rotation);
  const V3 az = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
  const float hx = 0.5F * m.data.scale[0];
  const float hy = 0.5F * m.data.scale[1];
  const float nz = 0.02F * m.data.scale[2];
  const float cx = m.data.position[0] + az.x * nz;
  const float cy = m.data.position[1] + az.y * nz;
  const float cz = m.data.position[2] + az.z * nz;
  const float sgnX[4] = {-1.0F, 1.0F, 1.0F, -1.0F};
  const float sgnY[4] = {-1.0F, -1.0F, 1.0F, 1.0F};
  const float fbW = engine->renderer.core.getSettings().getWidth();
  const float fbH = engine->renderer.core.getSettings().getRenderHeightF();
  // Heights above are RASTER heights: with field rendering
  // (InterlacedField) the buffer is half height and the projection's
  // raster scale is built at getRenderHeightF - screen-space math here
  // must match it (getHeight would land the mask a field off).

  float xy[24];
  u32 zz[12];
  int n = 0;
  int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;

  // "Looking into the opening from close, roughly head-on" - the gate for
  // the last-resort full-screen crossing mask below (used only on the one
  // frame the eye is on the surface and the fan degenerates; see there).
  bool zone = false;
  {
    const float relX = cameraPosition.x - m.data.position[0];
    const float relY = cameraPosition.y - m.data.position[1];
    const float relZ = cameraPosition.z - m.data.position[2];
    const float lz = relX * az.x + relY * az.y + relZ * az.z;
    const float lx = relX * ax.x + relY * ax.y + relZ * ax.z;
    const float ly = relX * ay.x + relY * ay.y + relZ * ay.z;
    const float thresh =
        engine->renderer.core.getSettings().getNear() * 2.0F + 0.45F;
    Vec4 fwd = cameraLookAt - cameraPosition;
    const float into = -(fwd.x * az.x + fwd.y * az.y + fwd.z * az.z);
    zone = lz > 0.0F && lz < thresh && lx > -hx - 0.3F && lx < hx + 0.3F &&
           ly > -hy - 0.3F && ly < hy + 0.3F && into > 0.0F;
  }

  bool carved = false;
  {
    const M4x4& vp = engine->renderer.core.renderer3D.getViewProj();
    Vec4 poly[12], tmp[12];
    n = 4;
    for (int i = 0; i < 4; ++i) {
      const float wx = cx + ax.x * hx * sgnX[i] + ay.x * hy * sgnY[i];
      const float wy = cy + ax.y * hx * sgnX[i] + ay.y * hy * sgnY[i];
      const float wz = cz + ax.z * hx * sgnX[i] + ay.z * hy * sgnY[i];
      poly[i] = vp * Vec4(wx, wy, wz, 1.0F);
    }
    // Frustum edges sit at |x| = w * screenW/4096 in this projection (the
    // VU1 pipeline's fixed 2048 scale), NOT at |x| = w; small margin - the
    // GS scissor finishes the job.
    const float xl = fbW / 4096.0F * 1.06F;
    const float yl = fbH / 4096.0F * 1.06F;
    const float wMin = engine->renderer.core.getSettings().getNear() * 0.5F;
    for (int plane = 0; plane < 5 && n >= 3; ++plane) {
      auto dist = [&](const Vec4& v) -> float {
        switch (plane) {
          case 0: return v.w - wMin;
          case 1: return xl * v.w - v.x;
          case 2: return xl * v.w + v.x;
          case 3: return yl * v.w - v.y;
          default: return yl * v.w + v.y;
        }
      };
      int outN = 0;
      for (int i = 0; i < n; ++i) {
        const Vec4& a = poly[i];
        const Vec4& b = poly[(i + 1) % n];
        const float da = dist(a);
        const float db = dist(b);
        if (da >= 0.0F) tmp[outN++] = a;
        if ((da >= 0.0F) != (db >= 0.0F)) {
          const float t = da / (da - db);
          tmp[outN++] = Vec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                             a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }
      }
      n = outN;
      for (int i = 0; i < n; ++i) poly[i] = tmp[i];
    }
    if (n >= 3) {
      float minX = 1e30F, minY = 1e30F, maxX = -1e30F, maxY = -1e30F;
      for (int i = 0; i < n; ++i) {
        const float inv = 1.0F / poly[i].w;
        const float sx = fbW * 0.5F + poly[i].x * inv * 2048.0F;
        const float sy = fbH * 0.5F + poly[i].y * inv * 2048.0F;
        xy[i * 2] = sx;
        xy[i * 2 + 1] = sy;
        if (sx < minX) minX = sx;
        if (sx > maxX) maxX = sx;
        if (sy < minY) minY = sy;
        if (sy > maxY) maxY = sy;
        float zf = (poly[i].z * inv + 1.0F) * 8388607.5F;
        if (zf < 0.0F) zf = 0.0F;
        if (zf > 16777215.0F) zf = 16777215.0F;
        zz[i] = (u32)zf;
      }
      bx0 = (int)minX;
      by0 = (int)minY;
      bx1 = (int)maxX + 1;
      by1 = (int)maxY + 1;
      carved = !(bx1 <= 0 || by1 <= 0 || bx0 >= (int)fbW || by0 >= (int)fbH);
    }
  }

  // Full-screen crossing mask: ONLY as a last resort, when the eye is so
  // close to the surface that the clipped fan has degenerated (`!carved`) -
  // there is no valid opening polygon to carve, so the destination fills the
  // screen for that single frame right at the plane (the walker teleports
  // the same instant). Every approach frame keeps a valid fan, so the crisp
  // carved WINDOW is used and the mounting wall stays drawn AROUND it - the
  // full mask paints the whole screen with the destination and erases that
  // wall, breaking the illusion (owner: "the wall disappears"). Gating it on
  // fan-degeneracy alone keeps that to the one unavoidable frame instead of
  // the whole near approach.
  if (zone && !carved) {
    n = 4;
    xy[0] = 0.0F;
    xy[1] = 0.0F;
    xy[2] = fbW;
    xy[3] = 0.0F;
    xy[4] = fbW;
    xy[5] = fbH;
    xy[6] = 0.0F;
    xy[7] = fbH;
    for (int i = 0; i < 4; ++i) zz[i] = 0xFFFFFFu;
    bx0 = 0;
    by0 = 0;
    bx1 = (int)fbW;
    by1 = (int)fbH;
  } else if (!carved) {
    return false;
  }

  // Dead zone: the virtual camera sits BEHIND the exit plane (the
  // isometry puts it there), and the PS2 has no oblique near plane to
  // clip what lies between the two - through a real hole that region is
  // invisible. Terrain chunks (renderTerrain) and view objects fully on
  // the camera side of the target plane are skipped.
  RuntimeObject& tgt = runtimeObjects[p.target];
  const V3 exitN = rotated({0.0F, 0.0F, 1.0F}, tgt.data.rotation);
  const float exitD = exitN.x * tgt.data.position[0] +
                      exitN.y * tgt.data.position[1] +
                      exitN.z * tgt.data.position[2];
  portalExitPlane[0] = exitN.x;
  portalExitPlane[1] = exitN.y;
  portalExitPlane[2] = exitN.z;
  portalExitPlane[3] = exitD;
  portalExitPlaneOn = true;

  auto& core = engine->renderer.core;
  core.portalViewBegin(bx0, by0, bx1, by1);
  // Same projection as the screen, only the view swaps to the virtual
  // camera - the destination lands exactly where the opening is.
  core.renderer3D.pushPortalView(eye, at);
  if (p.showTerrain) {
    if (skyDome.bag) {
      // dome parked on the VIRTUAL eye; the main pass re-centers it after
      skyMat.identity();
      skyMat.data[12] = eye.x;
      skyMat.data[13] = eye.y;
      skyMat.data[14] = eye.z;
      stapip.core.render(skyDome.bag.get());
    }
    // resident chunks only - the streaming ring follows the MAIN camera, so
    // with terrain streaming on, keep the pair inside the streamed radius
    renderTerrain();
  }
  // Billboard basis for THIS view: particles are camera-facing quads
  // whose centers are view-independent, so the same bags redraw for the
  // virtual camera once right/up are swapped (the VU1 billboard program
  // reads them per mesh - see updateParticles). Rain (kind 4) keeps
  // world-up like the main pass. Each emitter's basis is restored to what
  // it held (the main-camera basis updateParticles set) right after its
  // draw, so the frame's final main-pass particle render is unaffected.
  Vec4 pvFwd = at - eye;
  {
    const float l = sqrtf(pvFwd.x * pvFwd.x + pvFwd.y * pvFwd.y +
                          pvFwd.z * pvFwd.z);
    if (l > 0.0001F) pvFwd.x /= l, pvFwd.y /= l, pvFwd.z /= l;
  }
  float pvRx = pvFwd.z, pvRz = -pvFwd.x;
  {
    const float l = sqrtf(pvRx * pvRx + pvRz * pvRz);
    if (l > 0.0001F) pvRx /= l, pvRz /= l;
    else pvRx = 1.0F, pvRz = 0.0F;
  }
  const Vec4 pvRight(pvRx, 0.0F, pvRz, 0.0F);
  const Vec4 pvUp(-pvRz * pvFwd.y, pvRz * pvFwd.x - pvRx * pvFwd.z,
                  pvRx * pvFwd.y, 0.0F);

  // One view object: base bags + (animated) last skinned pose + (emitter)
  // its particle billboards under the virtual basis. No portal-in-portal:
  // the recursion would re-carve the very opening being rendered (mirrors
  // draw only their glass here - the reflected copies are a main-pass
  // trick that would need its own bracket).
  auto renderViewObject = [&](int ti) {
    if (ti < 0 || ti >= (int)runtimeObjects.size()) return;
    RuntimeObject& ro = runtimeObjects[ti];
    if (!ro.active || !ro.visible || ro.data.type == 16) return;
    {
      // Skip objects entirely behind the exit mouth (dead zone above).
      // The extent along the plane normal is the exact OBB projection -
      // a crude max-axis radius made a WIDE thin wall count as "reaching
      // through" its own thickness (a wall the target portal is mounted
      // on filled the whole view with its backside; owner report). The
      // 0.1 slack keeps a flush-mounted wall (portal quad nudged 0.02 in
      // front of it) classified as behind; geometry genuinely poking
      // through the plane still renders.
      const V3 oax = rotated({1.0F, 0.0F, 0.0F}, ro.data.rotation);
      const V3 oay = rotated({0.0F, 1.0F, 0.0F}, ro.data.rotation);
      const V3 oaz = rotated({0.0F, 0.0F, 1.0F}, ro.data.rotation);
      const float r =
          fabsf(exitN.x * oax.x + exitN.y * oax.y + exitN.z * oax.z) * 0.5F *
              ro.data.scale[0] +
          fabsf(exitN.x * oay.x + exitN.y * oay.y + exitN.z * oay.z) * 0.5F *
              ro.data.scale[1] +
          fabsf(exitN.x * oaz.x + exitN.y * oaz.y + exitN.z * oaz.z) * 0.5F *
              ro.data.scale[2];
      const float sd = exitN.x * ro.data.position[0] +
                       exitN.y * ro.data.position[1] +
                       exitN.z * ro.data.position[2] - exitD;
      if (sd < -r + 0.1F) return;
    }
    if (ro.data.type == 7) {
      // Emitter: redraw its live particle billboards from the virtual
      // camera. Centers are view-independent; only right/up change.
      for (ParticleSystem& ps : particles) {
        if (ps.objectIndex != ti || !ps.bag || ps.bag->count == 0) continue;
        const Vec4 savedR = ps.billboardBag->right;
        const Vec4 savedU = ps.billboardBag->up;
        ps.billboardBag->right = pvRight;
        ps.billboardBag->up = ro.data.emitKind == 4 ? Vec4(0.0F, 1.0F, 0.0F, 0.0F)
                                                    : pvUp;
        stapip.core.render(ps.bag.get());
        ps.billboardBag->right = savedR;  // main pass draws these again later
        ps.billboardBag->up = savedU;
      }
      return;  // emitters have no geometry/anim parts
    }
    const bool batched =
        ti < (int)objectBatchOf.size() && objectBatchOf[ti] >= 0;
    if (batched) {
      // Batched member: its geometry lives only in the merged batch bag,
      // and a merged bag cannot skip the members behind the exit plane -
      // submitting whole batches here painted the mounting wall's backside
      // across the through-view (owner report; the batch AABB spans a
      // whole grouping cell, so the plane test never rejected it). Solo
      // bake on first use instead and let the per-object dead zone above
      // do its work. A DIRTY member is left alone: rebuildObjectGeometry
      // consumes the flag renderStaticBatches keys its demotion on (the
      // portal pass runs before it in the frame); demotion rebuilds the
      // solo bag this same frame and the next live view picks it up.
      if (objectGeometry[ti].parts.empty() && !ro.dirty)
        rebuildObjectGeometry(ti);
    } else if (ro.dirty) {
      rebuildObjectGeometry(ti);
    }
    ObjectGeometry& g = objectGeometry[ti];
    for (GeoPart& part : g.parts)
      if (part.bag) stapip.core.render(part.bag.get());
    if (g.animInfoBag)
      for (ObjectGeometry::AnimPart& ap : g.animParts)
        if (ap.bag && ap.bag->count > 0) stapip.core.render(ap.bag.get());
  };
  // Two-sided carry: if the object in the hands is passing through THIS
  // portal, its through-view draws it mapped to the FAR side - the half that
  // has gone through the opening, coming out the exit. The main pass draws
  // the near half at the real position and the z-cap clips whatever lay past
  // the surface, so the two halves meet at the plane and the object reads as
  // physically passing through (renderViewObject's exit-plane dead zone
  // keeps the mapped copy hidden until the object's centre actually reaches
  // the surface, so it appears only as it emerges).
  const bool drawCarryFar = carryPortalPi == pi && carryIndex >= 0 &&
                            carryIndex < (int)runtimeObjects.size();
  float savedCarry[3];
  if (drawCarryFar) {
    RuntimeObject& co = runtimeObjects[carryIndex];
    savedCarry[0] = co.data.position[0];
    savedCarry[1] = co.data.position[1];
    savedCarry[2] = co.data.position[2];
    portalMapPoint(pi, co.data.position[0], co.data.position[1],
                   co.data.position[2]);
    co.dirty = true;  // rebuild at the mapped position for this view
  }
  if (p.viewAll) {
    // Experimental "all objects in view": submit the whole scene a second
    // time. The pushed frustum planes classify every bag against the
    // VIRTUAL camera (off-view geometry drops EE-side before packaging)
    // and draw distances are measured from the virtual eye, so the real
    // cost is what the destination actually sees - still, big scenes pay
    // for this; the authored list stays the shipping-quality default.
    // Batched members are handled inside renderViewObject (solo bake on
    // first use) - NOT by submitting the merged batch bags: a merged bag
    // cannot drop just the members behind the exit plane, and the batch
    // AABB spans its whole grouping cell, so a whole-bag plane test never
    // fires - the wall the target portal is mounted on filled the view
    // with its backside.
    for (int ti = 0; ti < (int)runtimeObjects.size(); ++ti) {
      if (runtimeObjects[ti].data.type == 15) continue;  // glass-only anyway
      if (beyondDrawDistance(runtimeObjects[ti].data, eye)) continue;
      renderViewObject(ti);
    }
  } else {
    // drawCarryFar implies carryIndex is on this list (portalShowsObject),
    // so the loop already renders the mapped far half.
    for (int v = 0; v < p.viewCount; ++v)
      renderViewObject(PORTAL_VIEW_OBJECTS[p.firstView + v]);
  }
  if (drawCarryFar) {
    RuntimeObject& co = runtimeObjects[carryIndex];
    co.data.position[0] = savedCarry[0];
    co.data.position[1] = savedCarry[1];
    co.data.position[2] = savedCarry[2];
    co.dirty = true;  // main pass rebuilds at the real (near) position
  }
  portalExitPlaneOn = false;
  core.renderer3D.popEnvView(CameraInfo3D(&cameraPosition, &cameraLookAt));
  core.portalViewEnd(xy, zz, n, (u8)scriptCtx.skyColor.r,
                     (u8)scriptCtx.skyColor.g, (u8)scriptCtx.skyColor.b);
  return true;
}

// Blend the tinted quads of every portal EXCEPT the live ones over the
// finished scene (a live portal's opening already shows the through-view
// carved by renderPortalView - a tint over it would wash the image).
void TerrainGame::renderPortals() {
  if (PORTAL_COUNT == 0) return;
  for (int pi = 0; pi < PORTAL_COUNT; ++pi) {
    if (pi < (int)portalLiveFlags.size() && portalLiveFlags[pi]) continue;
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 ||
        p.object >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& m = runtimeObjects[p.object];
    if (!m.active || !m.visible) continue;
    if (beyondDrawDistance(m.data, cameraPosition)) continue;
    for (GeoPart& part : objectGeometry[p.object].parts)
      if (part.bag) stapip.core.render(part.bag.get());
  }
}

// Floor-portal swallowing (owner's suggestion): while a body touches a
// linked floor portal (front normal pointing up), it stops colliding with
// the TERRAIN - the ground clamp would otherwise rest it on the terrain
// before it can reach the crossing plane, so a portal lying on the ground
// could never swallow anything. Restricted to floor portals (wall/ceiling
// surfaces never need it) and to the portal's rectangle footprint; the
// zone spans a little above the plane (the approach) and below it (the
// straddle while the crossing probe travels) - the teleport fires long
// before the body leaves the zone downward.
bool TerrainGame::portalSwallowZone(const RuntimeObject& m, float hx, float hy,
                                    float x, float y, float z) {
  const V3 azS = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
  if (azS.y < 0.5F) return false;  // floor portals only
  const V3 axS = rotated({1.0F, 0.0F, 0.0F}, m.data.rotation);
  const V3 ayS = rotated({0.0F, 1.0F, 0.0F}, m.data.rotation);
  const float rx = x - m.data.position[0];
  const float ry = y - m.data.position[1];
  const float rz = z - m.data.position[2];
  const float lx = rx * axS.x + ry * axS.y + rz * axS.z;
  const float ly = rx * ayS.x + ry * ayS.y + rz * ayS.z;
  const float lz = rx * azS.x + ry * azS.y + rz * azS.z;
  return lz > -0.6F && lz < 2.0F && lx > -hx && lx < hx && ly > -hy &&
         ly < hy;
}

// Both endpoints of this frame's motion, plus the point where the segment
// pierces the portal plane - exact for a vertical fall, which is the only
// motion fast enough (PHYS_MAX_SPEED) to tunnel the zone's 2-unit height.
bool TerrainGame::portalSwallowSwept(const RuntimeObject& m, float hx,
                                     float hy, const Vec4& a, const Vec4& b) {
  if (portalSwallowZone(m, hx, hy, b.x, b.y, b.z) ||
      portalSwallowZone(m, hx, hy, a.x, a.y, a.z))
    return true;
  const V3 azS = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
  if (azS.y < 0.5F) return false;  // floor portals only
  const float lza = (a.x - m.data.position[0]) * azS.x +
                    (a.y - m.data.position[1]) * azS.y +
                    (a.z - m.data.position[2]) * azS.z;
  const float lzb = (b.x - m.data.position[0]) * azS.x +
                    (b.y - m.data.position[1]) * azS.y +
                    (b.z - m.data.position[2]) * azS.z;
  if (!(lza > 0.0F && lzb < 0.0F)) return false;  // must pierce downward
  const float t = lza / (lza - lzb);
  return portalSwallowZone(m, hx, hy, a.x + (b.x - a.x) * t,
                           a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}

// The linked, object-teleporting portal whose opening the motion segment
// a->b pierces front-to-back (authored rectangle + the crossing slack), or
// -1. Shared by the thrown-object flight and the physics pass-through.
// The owner's crossing rule: whatever a portal SHOWS can also go through
// it. teleportObjects and viewAll open it to every rigid body, a view-list
// member is eligible by being visible, and the player-released body
// (thrownFreeIndex) crosses like the player regardless.
bool TerrainGame::portalCanCross(const PortalData& p, int oi) {
  if (p.teleportObjects || p.viewAll) return true;
  if (oi >= 0 && oi == thrownFreeIndex) return true;
  for (int v = 0; v < p.viewCount; ++v)
    if (PORTAL_VIEW_OBJECTS[p.firstView + v] == oi) return true;
  return false;
}

bool TerrainGame::portalShowsObject(int pi, int oi) {
  const PortalData& p = PORTALS[pi];
  if (p.viewAll) return true;
  for (int v = 0; v < p.viewCount; ++v)
    if (PORTAL_VIEW_OBJECTS[p.firstView + v] == oi) return true;
  return false;
}

void TerrainGame::portalMapPoint(int pi, float& x, float& y, float& z) {
  const PortalData& p = PORTALS[pi];
  const RuntimeObject& src = runtimeObjects[p.object];
  const RuntimeObject& dst = runtimeObjects[p.target];
  const V3 sxA = rotated({1.0F, 0.0F, 0.0F}, src.data.rotation);
  const V3 syA = rotated({0.0F, 1.0F, 0.0F}, src.data.rotation);
  const V3 szA = rotated({0.0F, 0.0F, 1.0F}, src.data.rotation);
  const V3 dxA = rotated({1.0F, 0.0F, 0.0F}, dst.data.rotation);
  const V3 dyA = rotated({0.0F, 1.0F, 0.0F}, dst.data.rotation);
  const V3 dzA = rotated({0.0F, 0.0F, 1.0F}, dst.data.rotation);
  const float rx = x - src.data.position[0];
  const float ry = y - src.data.position[1];
  const float rz = z - src.data.position[2];
  float lx = rx * sxA.x + ry * sxA.y + rz * sxA.z;
  const float ly = rx * syA.x + ry * syA.y + rz * syA.z;
  float lz = rx * szA.x + ry * szA.y + rz * szA.z;
  lx = -lx;  // the 180 deg flip about local Y
  lz = -lz;
  x = dst.data.position[0] + dxA.x * lx + dyA.x * ly + dzA.x * lz;
  y = dst.data.position[1] + dxA.y * lx + dyA.y * ly + dzA.y * lz;
  z = dst.data.position[2] + dxA.z * lx + dyA.z * ly + dzA.z * lz;
}

bool TerrainGame::armSweepPass(const float* a, const float* b) {
  sweepPassOn = false;
  if (PORTAL_COUNT == 0) return false;
  const int aim = portalCarryAim(a, b, -1);
  if (aim < 0) return false;
  const RuntimeObject& pm = runtimeObjects[PORTALS[aim].object];
  const V3 pn = rotated({0.0F, 0.0F, 1.0F}, pm.data.rotation);
  sweepPassPlane[0] = pn.x;
  sweepPassPlane[1] = pn.y;
  sweepPassPlane[2] = pn.z;
  sweepPassPlane[3] = pn.x * pm.data.position[0] +
                      pn.y * pm.data.position[1] +
                      pn.z * pm.data.position[2];
  sweepPassOn = true;
  return true;
}

int TerrainGame::portalCarryAim(const float* a, const float* b, int forObj) {
  for (int pi = 0; pi < PORTAL_COUNT; ++pi) {
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
    if (forObj >= 0 && !portalCanCross(p, forObj)) continue;
    if (p.object >= (int)runtimeObjects.size() ||
        p.target >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& m = runtimeObjects[p.object];
    if (!m.active || !m.visible || !runtimeObjects[p.target].active) continue;
    const V3 axS = rotated({1.0F, 0.0F, 0.0F}, m.data.rotation);
    const V3 ayS = rotated({0.0F, 1.0F, 0.0F}, m.data.rotation);
    const V3 azS = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
    const float hx = 0.5F * m.data.scale[0] + 0.25F;
    const float hy = 0.5F * m.data.scale[1] + 0.25F;
    const float r0x = a[0] - m.data.position[0];
    const float r0y = a[1] - m.data.position[1];
    const float r0z = a[2] - m.data.position[2];
    const float r1x = b[0] - m.data.position[0];
    const float r1y = b[1] - m.data.position[1];
    const float r1z = b[2] - m.data.position[2];
    const float l0z = r0x * azS.x + r0y * azS.y + r0z * azS.z;
    const float l1z = r1x * azS.x + r1y * azS.y + r1z * azS.z;
    if (!(l0z > 0.0F && l1z <= 0.0F)) continue;  // front-to-back only
    const float tt = l0z / (l0z - l1z);
    const float cxp = r0x + (r1x - r0x) * tt;
    const float cyp = r0y + (r1y - r0y) * tt;
    const float czp = r0z + (r1z - r0z) * tt;
    const float lxp = cxp * axS.x + cyp * axS.y + czp * axS.z;
    const float lyp = cxp * ayS.x + cyp * ayS.y + czp * ayS.z;
    if (lxp > -hx && lxp < hx && lyp > -hy && lyp < hy) return pi;
  }
  return -1;
}

// Hop a thrown object through the pair: position and the FULL velocity
// vector mapped by the same flip-about-local-Y isometry updatePortals
// applies to the player/physics bodies, so a sideways throw exits the
// target with the matching sideways motion and the crossing is continuous.
bool TerrainGame::portalCarryCrossing(const float* a, float* pos, float* vel) {
  // Thrown-arc objects are player-released - any linked portal carries them
  const int pi = portalCarryAim(a, pos, -1);
  if (pi < 0) return false;
  const PortalData& p = PORTALS[pi];
  RuntimeObject& m = runtimeObjects[p.object];
  RuntimeObject& t = runtimeObjects[p.target];
  const V3 sxA = rotated({1.0F, 0.0F, 0.0F}, m.data.rotation);
  const V3 syA = rotated({0.0F, 1.0F, 0.0F}, m.data.rotation);
  const V3 szA = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
  const V3 dxA = rotated({1.0F, 0.0F, 0.0F}, t.data.rotation);
  const V3 dyA = rotated({0.0F, 1.0F, 0.0F}, t.data.rotation);
  const V3 dzA = rotated({0.0F, 0.0F, 1.0F}, t.data.rotation);
  const float rx = pos[0] - m.data.position[0];
  const float ry = pos[1] - m.data.position[1];
  const float rz = pos[2] - m.data.position[2];
  float lx = rx * sxA.x + ry * sxA.y + rz * sxA.z;
  const float ly = rx * syA.x + ry * syA.y + rz * syA.z;
  float lz = rx * szA.x + ry * szA.y + rz * szA.z;
  lx = -lx;
  lz = -lz;
  pos[0] = t.data.position[0] + dxA.x * lx + dyA.x * ly + dzA.x * lz;
  pos[1] = t.data.position[1] + dxA.y * lx + dyA.y * ly + dzA.y * lz;
  pos[2] = t.data.position[2] + dxA.z * lx + dyA.z * ly + dzA.z * lz;
  float lvx = vel[0] * sxA.x + vel[1] * sxA.y + vel[2] * sxA.z;
  const float lvy = vel[0] * syA.x + vel[1] * syA.y + vel[2] * syA.z;
  float lvz = vel[0] * szA.x + vel[1] * szA.y + vel[2] * szA.z;
  lvx = -lvx;
  lvz = -lvz;
  vel[0] = dxA.x * lvx + dyA.x * lvy + dzA.x * lvz;
  vel[1] = dxA.y * lvx + dyA.y * lvy + dzA.y * lvz;
  vel[2] = dxA.z * lvx + dyA.z * lvy + dzA.z * lvz;
  return true;
}

bool TerrainGame::portalSwallowsPlayer(float x, float feetY, float z) {
  if (PORTAL_COUNT == 0) return false;
  for (int pi = 0; pi < PORTAL_COUNT; ++pi) {
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
    if (p.object >= (int)runtimeObjects.size() ||
        p.target >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& m = runtimeObjects[p.object];
    if (!m.active || !m.visible || !runtimeObjects[p.target].active) continue;
    const float hx = 0.5F * m.data.scale[0] + 0.25F;
    const float hy = 0.5F * m.data.scale[1] + 0.25F;
    // feet AND waist: the zone must hold while the body straddles the
    // plane, or the ground clamp snaps the walker back mid-crossing
    if (portalSwallowZone(m, hx, hy, x, feetY, z) ||
        portalSwallowZone(m, hx, hy, x, feetY + 1.0F, z))
      return true;
  }
  return false;
}

// Publishes the plane of the linked portal whose opening the walker's body
// column currently sits in (any orientation - wall doorways included; feet
// and waist probed like the crossing test). collidePlayer then ignores
// objects fully behind that plane: the mounting wall opens up, geometry in
// front of or poking through the surface still collides. Off when the
// column is outside every opening - the same wall blocks normally beside
// its portal.
void TerrainGame::updatePortalPass(float x, float feetY, float z) {
  portalPassOn = false;
  if (PORTAL_COUNT == 0) return;
  for (int pi = 0; pi < PORTAL_COUNT; ++pi) {
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
    if (p.object >= (int)runtimeObjects.size() ||
        p.target >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& m = runtimeObjects[p.object];
    if (!m.active || !m.visible || !runtimeObjects[p.target].active) continue;
    const V3 axS = rotated({1.0F, 0.0F, 0.0F}, m.data.rotation);
    const V3 ayS = rotated({0.0F, 1.0F, 0.0F}, m.data.rotation);
    const V3 azS = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
    const float hx = 0.5F * m.data.scale[0] + 0.25F;
    const float hy = 0.5F * m.data.scale[1] + 0.25F;
    auto inZone = [&](float wy) {
      const float rx = x - m.data.position[0];
      const float ry = wy - m.data.position[1];
      const float rz = z - m.data.position[2];
      const float lx = rx * axS.x + ry * axS.y + rz * axS.z;
      const float ly = rx * ayS.x + ry * ayS.y + rz * ayS.z;
      const float lz = rx * azS.x + ry * azS.y + rz * azS.z;
      return lz > -0.6F && lz < 1.2F && lx > -hx && lx < hx && ly > -hy &&
             ly < hy;
    };
    if (inZone(feetY) || inZone(feetY + 1.0F)) {
      portalPassPlane[0] = azS.x;
      portalPassPlane[1] = azS.y;
      portalPassPlane[2] = azS.z;
      portalPassPlane[3] = azS.x * m.data.position[0] +
                           azS.y * m.data.position[1] +
                           azS.z * m.data.position[2];
      portalPassOn = true;
      return;
    }
  }
}

// Portal crossings. The player probes with TWO segments: a "waist" point
// (feet + up to 1 unit, capped by eyeH - door-sized wall surfaces trigger
// naturally, a noclip camera probes its own position) and the FEET (so
// jumping/dropping into a floor portal triggers even while the waist stays
// above the plane); physics objects test their center. A crossing = the
// probe segment pierced the front (+Z) face inside the rectangle this
// frame. Position, view direction and vertical velocity map through the
// pair exactly like portalCamera, with NO exit offset - the isometry
// carries the overshoot past the target plane, so the hop is continuous
// (an offset read as a one-frame camera pop on hardware). The walkers keep
// no horizontal velocity state, so a tilted pair carries only the vertical
// component (yaw-rotated pairs, the common teleporter, lose nothing).
// Returns true when the player teleported (the frame camera is rebuilt here
// so no frame ever renders from the departure side).
bool TerrainGame::updatePortals(float prevX, float prevY, float prevZ,
                                float* px, float* py, float* pz, float* pyaw,
                                float* ppitch, float* pvelY, float eyeH) {
  if (PORTAL_COUNT == 0) return false;
  const bool freshPrev =
      (int)portalPrevPos.size() != (int)runtimeObjects.size() * 3;
  if (freshPrev) {
    portalPrevPos.resize(runtimeObjects.size() * 3);
    portalHopCool.assign(runtimeObjects.size(), 0);
  }
  // A released body stays portal-free only until it settles to sleep.
  if (thrownFreeIndex >= 0 &&
      (thrownFreeIndex >= (int)runtimeObjects.size() ||
       runtimeObjects[thrownFreeIndex].restFrames >= PHYS_SLEEP_FRAMES))
    thrownFreeIndex = -1;
  bool playerTeleported = false;
  const float probeLift = eyeH > 1.0F ? 1.0F : eyeH;

  for (int pi = 0; pi < PORTAL_COUNT; ++pi) {
    const PortalData& p = PORTALS[pi];
    if (p.scene != currentScene || p.object < 0 || p.target < 0) continue;
    if (p.object >= (int)runtimeObjects.size() ||
        p.target >= (int)runtimeObjects.size())
      continue;
    RuntimeObject& m = runtimeObjects[p.object];
    RuntimeObject& t = runtimeObjects[p.target];
    if (!m.active || !m.visible || !t.active) continue;

    const V3 sxA = rotated({1.0F, 0.0F, 0.0F}, m.data.rotation);
    const V3 syA = rotated({0.0F, 1.0F, 0.0F}, m.data.rotation);
    const V3 szA = rotated({0.0F, 0.0F, 1.0F}, m.data.rotation);
    const V3 dxA = rotated({1.0F, 0.0F, 0.0F}, t.data.rotation);
    const V3 dyA = rotated({0.0F, 1.0F, 0.0F}, t.data.rotation);
    const V3 dzA = rotated({0.0F, 0.0F, 1.0F}, t.data.rotation);
    // a little slack around the authored rectangle - brushing the frame
    // edge should not drop the player onto the wall the portal is built in
    const float hx = 0.5F * m.data.scale[0] + 0.25F;
    const float hy = 0.5F * m.data.scale[1] + 0.25F;
    auto localOf = [&](float wxx, float wyy, float wzz, float* lx, float* ly,
                       float* lz) {
      const float rx = wxx - m.data.position[0];
      const float ry = wyy - m.data.position[1];
      const float rz = wzz - m.data.position[2];
      *lx = rx * sxA.x + ry * sxA.y + rz * sxA.z;
      *ly = rx * syA.x + ry * syA.y + rz * syA.z;
      *lz = rx * szA.x + ry * szA.y + rz * szA.z;
    };
    // segment a->b pierced the front face inside the rectangle?
    auto crossed = [&](float ax_, float ay_, float az_, float bx_, float by_,
                       float bz_) -> bool {
      float l0x, l0y, l0z, l1x, l1y, l1z;
      localOf(ax_, ay_, az_, &l0x, &l0y, &l0z);
      localOf(bx_, by_, bz_, &l1x, &l1y, &l1z);
      if (!(l0z > 0.0F && l1z <= 0.0F)) return false;
      const float tt = l0z / (l0z - l1z);
      const float ix = l0x + (l1x - l0x) * tt;
      const float iy = l0y + (l1y - l0y) * tt;
      return ix > -hx && ix < hx && iy > -hy && iy < hy;
    };
    // world point / direction through the pair (flip about local Y)
    auto mapPoint = [&](float wxx, float wyy, float wzz, float* ox, float* oy,
                        float* oz) {
      float lx, ly, lz;
      localOf(wxx, wyy, wzz, &lx, &ly, &lz);
      lx = -lx;
      lz = -lz;
      *ox = t.data.position[0] + dxA.x * lx + dyA.x * ly + dzA.x * lz;
      *oy = t.data.position[1] + dxA.y * lx + dyA.y * ly + dzA.y * lz;
      *oz = t.data.position[2] + dxA.z * lx + dyA.z * ly + dzA.z * lz;
    };
    auto mapDir = [&](float wxx, float wyy, float wzz, float* ox, float* oy,
                      float* oz) {
      float lx = wxx * sxA.x + wyy * sxA.y + wzz * sxA.z;
      const float ly = wxx * syA.x + wyy * syA.y + wzz * syA.z;
      float lz = wxx * szA.x + wyy * szA.y + wzz * szA.z;
      lx = -lx;
      lz = -lz;
      *ox = dxA.x * lx + dyA.x * ly + dzA.x * lz;
      *oy = dxA.y * lx + dyA.y * ly + dzA.y * lz;
      *oz = dxA.z * lx + dyA.z * ly + dzA.z * lz;
    };

    // --- the player -------------------------------------------------------
    // Two probe segments: the waist (feet + up to 1 unit - door-sized wall
    // portals trigger naturally) and the FEET (dropping/jumping into a
    // floor portal must trigger even while the waist stays above it).
    const bool waistHit =
        px && !playerTeleported &&
        crossed(prevX, prevY + probeLift, prevZ, *px, *py + probeLift, *pz);
    const bool feetHit = px && !playerTeleported && !waistHit &&
                         crossed(prevX, prevY, prevZ, *px, *py, *pz);
    if (waistHit || feetHit) {
      // Map the feet position directly - the pair transform is an isometry,
      // so the overshoot past the plane maps to the same overshoot past the
      // target plane and the crossing is CONTINUOUS. No exit offset: it
      // read as a one-frame camera pop on hardware, and the arrival already
      // sits on the target's exit side moving away (a two-way pair can't
      // re-trigger without genuinely walking back).
      // vertical motion BEFORE the position is overwritten. Like the
      // objects below, carry the ACTUAL motion this frame - the walker's
      // ground clamp can zero velY on the very crossing frame.
      float vy = *pvelY;
      const float stepY = *py - prevY;
      if (fabsf(stepY) > fabsf(vy)) vy = stepY;
      float nx2, ny2, nz2;
      mapPoint(*px, *py, *pz, &nx2, &ny2, &nz2);
      *px = nx2;
      *py = ny2;
      *pz = nz2;
      // view direction through the same mapping (walker convention:
      // forward = (sin yaw * cos pitch, sin pitch, cos yaw * cos pitch))
      float ndx, ndy, ndz;
      mapDir(sinf(*pyaw) * cosf(*ppitch), sinf(*ppitch),
             cosf(*pyaw) * cosf(*ppitch), &ndx, &ndy, &ndz);
      *pyaw = atan2f(ndx, ndz);
      float sp = ndy;
      if (sp > 1.0F) sp = 1.0F;
      if (sp < -1.0F) sp = -1.0F;
      *ppitch = asinf(sp);
      float nvx, nvy, nvz;
      mapDir(0.0F, vy, 0.0F, &nvx, &nvy, &nvz);
      *pvelY = nvy;
      // rebuild this frame's camera from the arrival state
      if (PLAYER_INDEX >= 0 && PLAYER_MODE == 2) {
        // third person: park the camera on the authored boom straight
        // behind the avatar; the spring arm takes over next frame
        const float bx = sinf(*pyaw), bz = cosf(*pyaw);
        cameraPosition = Vec4(*px - bx * PLAYER_CAM_DIST,
                              *py + PLAYER_CAM_HEIGHT + PLAYER_CAM_DIST * 0.25F,
                              *pz - bz * PLAYER_CAM_DIST);
        cameraLookAt = Vec4(*px, *py + PLAYER_CAM_HEIGHT, *pz);
        players[0].boom = PLAYER_CAM_DIST;
        players[0].faceYaw = *pyaw;
      } else {
        const float eyY = *py + eyeH;
        cameraPosition = Vec4(*px, eyY, *pz);
        cameraLookAt = Vec4(*px + sinf(*pyaw) * cosf(*ppitch),
                            eyY + sinf(*ppitch),
                            *pz + cosf(*pyaw) * cosf(*ppitch));
      }
      playerTeleported = true;
    }

    // --- physics objects: portalCanCross decides per body (the flag, the
    // whatever-the-portal-shows rule, or the player-released latch) ------
    if (!freshPrev) {
      for (int oi = 0; oi < (int)runtimeObjects.size(); ++oi) {
        RuntimeObject& ro = runtimeObjects[oi];
        if (!ro.active || !ro.data.physics) continue;
        if (!portalCanCross(p, oi)) continue;
        if (oi == p.object || oi == p.target) continue;
        if (oi == carryIndex) continue;  // in the hands - the carry owns it
        if (PLAYER_INDEX >= 0 && oi == PLAYER_INDEX) continue;
        if (portalHopCool[oi] > 0) continue;  // just hopped - settle first
        const float* pp = &portalPrevPos[oi * 3];
        if (pp[0] == ro.data.position[0] && pp[1] == ro.data.position[1] &&
            pp[2] == ro.data.position[2])
          continue;  // resting - cheap early out
        if (!crossed(pp[0], pp[1], pp[2], ro.data.position[0],
                     ro.data.position[1], ro.data.position[2]))
          continue;
        // Carry the object's ACTUAL motion this frame through the pair -
        // the WHOLE velocity vector, not just Y. The vertical component
        // gets a position-delta fallback: the physics ground clamp may
        // have zeroed velocityY on the very crossing frame (an
        // infinite-fall loop would otherwise hitch at the far end with
        // v = 0), and the step still holds the real fall. The horizontal
        // components MUST map through the pair too - dropping them left a
        // thrown object exiting with world-space X/Z that no longer match
        // the rotated target, so it careened sideways (owner: the thrown
        // sphere "freaks out" between the portals).
        float vy = ro.velocityY;
        const float stepY = ro.data.position[1] - pp[1];
        if (fabsf(stepY) > fabsf(vy)) vy = stepY;
        float nx2, ny2, nz2;
        mapPoint(ro.data.position[0], ro.data.position[1],
                 ro.data.position[2], &nx2, &ny2, &nz2);
        // no exit offset - the mapped overshoot already sits past the
        // target plane (continuity); the prev-pos stamp below stops the
        // reverse link from reading the hop as another crossing
        ro.data.position[0] = nx2;
        ro.data.position[1] = ny2;
        ro.data.position[2] = nz2;
        float nvx, nvy, nvz;
        mapDir(ro.velocityX, vy, ro.velocityZ, &nvx, &nvy, &nvz);
        ro.velocityX = nvx;
        ro.velocityY = nvy;
        ro.velocityZ = nvz;
        ro.dirty = true;  // world-space bags rebuild at the arrival
        portalHopCool[oi] = 6;
        // stamp the arrival as this object's new "previous" so the reverse
        // link of a two-way pair can't see the same hop as a crossing
        portalPrevPos[oi * 3] = ro.data.position[0];
        portalPrevPos[oi * 3 + 1] = ro.data.position[1];
        portalPrevPos[oi * 3 + 2] = ro.data.position[2];
      }
    }
  }

  // refresh the crossing history for the next frame
  for (int oi = 0; oi < (int)runtimeObjects.size(); ++oi) {
    portalPrevPos[oi * 3] = runtimeObjects[oi].data.position[0];
    portalPrevPos[oi * 3 + 1] = runtimeObjects[oi].data.position[1];
    portalPrevPos[oi * 3 + 2] = runtimeObjects[oi].data.position[2];
    if (portalHopCool[oi] > 0) --portalHopCool[oi];
  }
  return playerTeleported;
}

// True when the usable object sits inside the highlight distance (same
// reference point as the USE interaction) and has anything to draw.
bool TerrainGame::highlightInReach(int index) const {
  const RuntimeObject& o = runtimeObjects[index];
  const ObjectGeometry& g = objectGeometry[index];
  bool any = false;  // marker-only objects have no draw parts
  for (const GeoPart& part : g.parts)
    if (part.bag) {
      any = true;
      break;
    }
  if (!any) return false;
  float half = o.data.scale[0];
  if (o.data.scale[1] > half) half = o.data.scale[1];
  if (o.data.scale[2] > half) half = o.data.scale[2];
  half *= 0.5F;
  if (half < 0.01F) half = 0.01F;
  const float dx = o.data.position[0] - cameraPosition.x;
  const float dy = o.data.position[1] - cameraPosition.y;
  const float dz = o.data.position[2] - cameraPosition.z;
  const float reach = HIGHLIGHT_DISTANCE + half;
  return dx * dx + dy * dy + dz * dz <= reach * reach;
}

// Usable-object highlight (Project > Preferences): a soft colored rim around
// the object silhouette. HIGHLIGHT_STEPS concentric copies of the object,
// grown around its center with fading alpha, drawn after the scene with
// z-test but no z-write (PipelineZTest_TestOnly). Each shell is additionally
// pushed away from the camera by a uniform scale around the eye point - that
// keeps its screen silhouette identical but places it behind the object's
// own depth, so the z-buffer rejects the interior and only the rim survives,
// correctly occluded by anything nearer. Proximity uses the same reference
// point as the USE interaction (the camera / player eye).
//
// Both the grow (scale about the object center) and the pushback (scale
// about the eye) are uniform point scales, so each shell collapses into ONE
// scale+translation model matrix over the object's own vertex arrays - VU1
// applies it during transform and the EE never touches a vertex. (The first
// version grew and terrain-clamped every vertex of every shell on the EE
// each frame plus a full bbox recompute; with the default 4 steps a
// few-thousand-vert usable model near the player cost milliseconds of
// EE time - the frame is EE-bound, so it fell to the next vsync divisor.)
void TerrainGame::renderHighlightHull(int index) {
  const RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];

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

  // Shells draw from the low-detail proxy, not the object's own (possibly
  // heavily subdivided) mesh - built lazily, cleared on geometry rebuilds.
  if (g.hullProxyVerts.empty()) buildHighlightProxy(index);
  if (g.hullProxyVerts.empty()) return;  // marker-only object

  const float dist = sqrtf(dist2);
  const float cx = o.data.position[0], cy = o.data.position[1],
              cz = o.data.position[2];
  // How far in front of the object the camera is - the pushback must move
  // the shell past the object's front surface without reaching things
  // right behind it.
  float behind = dist - half;
  if (behind < 0.5F) behind = 0.5F;
  // One pushback for all shells (sized for the widest) - per-shell depths
  // would make the terrain/scene cut each shell on a different line and the
  // rim edge turns into visible steps.
  float growMax = HIGHLIGHT_WIDTH / half;
  if (growMax > 0.6F) growMax = 0.6F;
  // k = scale about the eye. RIM mode (>1) pushes the shell behind the
  // object's depth so the z-buffer keeps only the silhouette rim. OVERLAY
  // mode keeps k = 1 (no pushback): each grown shell sits at the object's own
  // depth, its front faces landing just in front of the surface, so the glow
  // paints ON the object and fades outward into a rim (the body is drawn
  // first, in the main pass, then these shells blend over it).
  const float k = HIGHLIGHT_OVERLAY ? 1.0F
                                    : 1.0F + (growMax * half + 0.15F) / behind;

  if (!hullBag) {
    hullInfoBag = std::make_unique<StaPipInfoBag>();
    hullInfoBag->model = &hullMat;
    hullInfoBag->shadingType = TyraShadingFlat;
    hullInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
    hullInfoBag->fullClipChecks = true;
    hullInfoBag->zTestType = PipelineZTest_TestOnly;
    hullColorBag = std::make_unique<StaPipColorBag>();
    hullBag = std::make_unique<StaPipBag>();
    hullBag->info = hullInfoBag.get();
    hullBag->color = hullColorBag.get();
    hullBag->texture = nullptr;
    hullBag->lighting = nullptr;
  }

  // Shell colors, alpha roughly halving outward - near the silhouette all
  // shells overlap into solid color fading outward. Refreshed every call
  // (scene switches change the prefs; same values otherwise).
  hullShellCols.resize(HIGHLIGHT_STEPS);
  // Strongest (innermost) shell alpha = HIGHLIGHT_OPACITY of full (128 = the
  // PS2 opaque max); the outer shells halve outward. Opacity 1 + steps 1 =
  // a fully solid outline.
  float alpha = HIGHLIGHT_OPACITY * 128.0F;
  for (int s = 0; s < HIGHLIGHT_STEPS; ++s) {
    hullShellCols[s] = Color(HIGHLIGHT_R, HIGHLIGHT_G, HIGHLIGHT_B, alpha);
    alpha *= 0.55F;
  }

  for (int s = 0; s < HIGHLIGHT_STEPS; ++s) {
    // uniform growth - rotated objects stay unskewed
    float grow = (HIGHLIGHT_WIDTH * (s + 1)) / (HIGHLIGHT_STEPS * half);
    if (grow > 0.6F) grow = 0.6F;
    const float f = 1.0F + grow;
    // shell = eye + k*((c + f*(p - c)) - eye) = (k*f)*p + offset
    const float a = k * f;
    hullMat.identity();
    hullMat.data[0] = a;
    hullMat.data[5] = a;
    hullMat.data[10] = a;
    hullMat.data[12] = k * (1.0F - f) * cx + (1.0F - k) * cameraPosition.x;
    hullMat.data[13] = k * (1.0F - f) * cy + (1.0F - k) * cameraPosition.y;
    hullMat.data[14] = k * (1.0F - f) * cz + (1.0F - k) * cameraPosition.z;
    hullColorBag->single = &hullShellCols[s];
    // One submit per shell over the proxy positions; the package boxes live
    // in their own cache slot (keyed by the proxy pointer) and recompute
    // only when the proxy rebuilds - never per frame.
    hullBag->vertices = g.hullProxyVerts.data();
    hullBag->count = static_cast<u32>(g.hullProxyVerts.size());
    hullBag->bboxVersion = g.hullProxyStamp;
    stapip.core.render(hullBag.get());
  }

  // Grounded objects: the shells dip below the terrain and the ground in
  // front z-rejects them - no bottom rim from a low camera. The old code
  // fixed that by terrain-clamping every shell vertex (the expensive part);
  // a small terrain-following annulus around the base gives the same glow
  // apron for a fraction of a percent of the vertices.
  if (cy - 0.5F * o.data.scale[1] <=
      terrainHeightAt(cx, cz) + HIGHLIGHT_WIDTH + 0.25F) {
    if (g.apronVerts.empty()) buildHighlightApron(index, half);
    if (!g.apronVerts.empty()) {
      if (!apronBag) {
        apronInfoBag = std::make_unique<StaPipInfoBag>();
        apronInfoBag->model = &model;  // world-space, camera-independent
        apronInfoBag->shadingType = TyraShadingFlat;
        apronInfoBag->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
        apronInfoBag->fullClipChecks = true;
        apronInfoBag->zTestType = PipelineZTest_TestOnly;
        apronColorBag = std::make_unique<StaPipColorBag>();
        apronBag = std::make_unique<StaPipBag>();
        apronBag->info = apronInfoBag.get();
        apronBag->color = apronColorBag.get();
        apronBag->texture = nullptr;
        apronBag->lighting = nullptr;
      }
      apronColorBag->many = g.apronCols.data();
      apronBag->vertices = g.apronVerts.data();
      apronBag->count = static_cast<u32>(g.apronVerts.size());
      apronBag->bboxVersion = g.apronStamp;
      stapip.core.render(apronBag.get());
    }
  }

  // RIM mode: the pushback clears the object's front face but not its
  // receding side faces - shells still blend over those at glancing angles.
  // The caller paints the deferred body right after this call (GEQUAL wins
  // the equal-depth test), erasing the wash without touching the rim and
  // without the old main-pass-draw + repaint double draw. OVERLAY mode wants
  // exactly that wash on the surface, so the caller draws the body FIRST (in
  // the main pass) and leaves these shells on top.
}

// Builds the shells' low-detail stand-in (world-space positions only).
// Primitives regenerate through their own builders with the subdivision
// forced down - a subdivided box/plane has the same silhouette at detail 1,
// and a curved primitive at 12 segments is within a couple of percent of
// its full-detail silhouette, invisible under the soft rim. Models have no
// cheaper source, so their parts are concatenated as-is (one submit per
// shell instead of one per part).
void TerrainGame::buildHighlightProxy(int index) {
  const RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];
  g.hullProxyVerts.clear();

  if (o.data.type == 5) {
    size_t total = 0;
    for (const GeoPart& part : g.parts) total += part.vertices.size();
    g.hullProxyVerts.reserve(total);
    for (const GeoPart& part : g.parts)
      if (part.bag)
        g.hullProxyVerts.insert(g.hullProxyVerts.end(), part.vertices.begin(),
                                part.vertices.end());
  } else {
    SceneObjectData low = o.data;
    low.primDetail = 1;
    switch (o.data.type) {
      case 1:
      case 2:
      case 3:
        low.primDetail = o.data.primDetail < 12 ? o.data.primDetail : 12;
        break;
      default:
        break;  // boxes, planes, decals: detail 1
    }
    // The builders emit colors/sts too; shells only need positions, the
    // rest is discarded (built once per geometry rebuild).
    std::vector<Color> cols;
    std::vector<Vec4> sts;
    switch (o.data.type) {
      case 1: addSphere(g.hullProxyVerts, cols, sts, low); break;
      case 2: addCylinder(g.hullProxyVerts, cols, sts, low); break;
      case 3: addCone(g.hullProxyVerts, cols, sts, low); break;
      case 12: addPlane(g.hullProxyVerts, cols, sts, low); break;
      case 13: addDecal(g.hullProxyVerts, cols, sts, low); break;
      default:
        if (!g.parts.empty() && g.parts[0].bag)
          addBox(g.hullProxyVerts, cols, sts, low);
        break;  // marker-only types keep the proxy empty
    }
  }
  if (!g.hullProxyVerts.empty()) g.hullProxyStamp = ++g_bboxStamp;
}

// The terrain-hugging glow ring around a grounded usable object's base: one
// annulus band per shell with the shell's growth radius and alpha, following
// the terrain height at every ring point. World-space and camera-independent,
// so it's built once and redrawn from cache until rebuildObjectGeometry
// invalidates it (move/resize) or a scene switch recreates the geometry.
void TerrainGame::buildHighlightApron(int index, float half) {
  const RuntimeObject& o = runtimeObjects[index];
  ObjectGeometry& g = objectGeometry[index];
  const float cx = o.data.position[0], cz = o.data.position[2];
  // Elliptical footprint so stretched objects keep a snug ring
  float rx = 0.5F * o.data.scale[0];
  float rz = 0.5F * o.data.scale[2];
  if (rx < 0.05F) rx = 0.05F;
  if (rz < 0.05F) rz = 0.05F;

  const int SEG = 24;
  Vec4 inner[SEG + 1], outer[SEG + 1];
  for (int j = 0; j <= SEG; ++j) {
    const float t = (2.0F * PI * j) / SEG;
    const float px = cx + rx * cosf(t);
    const float pz = cz + rz * sinf(t);
    inner[j] = Vec4(px, terrainHeightAt(px, pz) + 0.02F, pz, 1.0F);
  }

  g.apronVerts.clear();
  g.apronCols.clear();
  g.apronVerts.reserve((size_t)HIGHLIGHT_STEPS * SEG * 6);
  g.apronCols.reserve((size_t)HIGHLIGHT_STEPS * SEG * 6);

  // Same alpha ramp as the shells (HIGHLIGHT_OPACITY strongest, halving out).
  float alpha = HIGHLIGHT_OPACITY * 128.0F;
  for (int s = 0; s < HIGHLIGHT_STEPS; ++s) {
    float grow = (HIGHLIGHT_WIDTH * (s + 1)) / (HIGHLIGHT_STEPS * half);
    if (grow > 0.6F) grow = 0.6F;
    const float f = 1.0F + grow;
    const Color c(HIGHLIGHT_R, HIGHLIGHT_G, HIGHLIGHT_B, alpha);
    alpha *= 0.55F;
    for (int j = 0; j <= SEG; ++j) {
      const float t = (2.0F * PI * j) / SEG;
      const float px = cx + rx * f * cosf(t);
      const float pz = cz + rz * f * sinf(t);
      outer[j] = Vec4(px, terrainHeightAt(px, pz) + 0.02F, pz, 1.0F);
    }
    for (int j = 0; j < SEG; ++j) {
      g.apronVerts.push_back(inner[j]);
      g.apronVerts.push_back(outer[j]);
      g.apronVerts.push_back(outer[j + 1]);
      g.apronVerts.push_back(inner[j]);
      g.apronVerts.push_back(outer[j + 1]);
      g.apronVerts.push_back(inner[j + 1]);
      for (int v = 0; v < 6; ++v) g.apronCols.push_back(c);
    }
    for (int j = 0; j <= SEG; ++j) inner[j] = outer[j];
  }
  g.apronStamp = ++g_bboxStamp;
}

// --- Terrain chunks ---------------------------------------------------------
// The heightmap grid is cut into TERRAIN_CHUNK_CELLS x TERRAIN_CHUNK_CELLS
// tiles, one StaPip bag each: whole off-screen tiles are rejected EE-side by
// the engine's bag-bbox frustum check, and with TERRAIN_VIEW_DISTANCE > 0
// only the tiles around the view focus are kept in memory at all - mesh RAM
// stays constant no matter how large the map is. Gameplay never depends on
// the mesh (terrainHeightAt samples TERRAIN_HEIGHTS), so a not-yet-streamed
// far chunk is a purely visual gap - pair the view distance with fog.

// Sizes the slot pool for the active scene and marks every chunk unbuilt.
// The pool never reallocates afterwards: chunk bags point into their own
// slot's vectors, so slots must not move while chunks are alive.
void TerrainGame::resetTerrainChunks() {
  const int cellsX = HM_W - 1;
  const int cellsZ = HM_D - 1;
  terrainChunksX = (cellsX + TERRAIN_CHUNK_CELLS - 1) / TERRAIN_CHUNK_CELLS;
  terrainChunksZ = (cellsZ + TERRAIN_CHUNK_CELLS - 1) / TERRAIN_CHUNK_CELLS;
  const int total = terrainChunksX * terrainChunksZ;

  int pool = total;
  if (TERRAIN_VIEW_DISTANCE > 0.0F) {
    // The view rect (focus +- view distance) covers at most ceil(2V/span)+1
    // tiles per axis; +1 more per axis for the eviction hysteresis. Scenes
    // that can host player 2 stream around two foci, so they need room for
    // two disjoint rects.
    const float spanX = TERRAIN_CHUNK_CELLS * ((float)TERRAIN_WIDTH / cellsX);
    const float spanZ = TERRAIN_CHUNK_CELLS * ((float)TERRAIN_DEPTH / cellsZ);
    const int nx = (int)(2.0F * TERRAIN_VIEW_DISTANCE / spanX) + 3;
    const int nz = (int)(2.0F * TERRAIN_VIEW_DISTANCE / spanZ) + 3;
    const int rects = (MULTIPLAYER_MODE != 0 && PLAYER2_INDEX >= 0) ? 2 : 1;
    if (nx * nz * rects < pool) pool = nx * nz * rects;
  }

  terrainChunks.clear();
  terrainChunks.resize(pool);  // sized once per scene - slots stay put
  terrainChunkSlot.assign(total, -1);
}

// Macro ground variation (docs/terrain-painting.md): deterministic value
// noise over WORLD position - two smoothstepped octaves - multiplied into the
// vertex shade below. Large soft patches of lighter/darker ground at zero
// runtime cost, with an infinite period (breaks even the stochastic
// supertile's second-order repetition). Twin of tintNoise2 in the editor
// viewport (viewport.cpp buildTerrainChunkMesh) - keep the formulas in sync.
static float tintHash(int ix, int iz) {
  unsigned h = (unsigned)ix * 73856093u ^ (unsigned)iz * 19349663u;
  h ^= h >> 13;
  h *= 0x85EBCA6Bu;
  h ^= h >> 16;
  return (float)(h & 0xFFFFu) / 65535.0F;
}
static float tintValue(float x, float z, float scale) {
  const float gx = x / scale, gz = z / scale;
  const float fxf = floorf(gx), fzf = floorf(gz);
  const int ix = (int)fxf, iz = (int)fzf;
  float fx = gx - fxf, fz = gz - fzf;
  fx = fx * fx * (3.0F - 2.0F * fx);  // smoothstep = soft round patches
  fz = fz * fz * (3.0F - 2.0F * fz);
  const float a = tintHash(ix, iz), b = tintHash(ix + 1, iz);
  const float c = tintHash(ix, iz + 1), d = tintHash(ix + 1, iz + 1);
  return (a * (1.0F - fx) + b * fx) * (1.0F - fz) +
         (c * (1.0F - fx) + d * fx) * fz;
}
static float tintNoise2(float x, float z, float scale) {
  return tintValue(x, z, scale) * 0.7F +
         tintValue(x + 191.0F, z - 353.0F, scale * 0.37F) * 0.3F;
}

// Fills a pool slot with the mesh of chunk (cx, cz): the same vertex layout,
// checker colors and baked shading the old whole-map build used - a chunk
// streamed in later is pixel-identical to one built at scene load.
void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {
  TerrainChunk& ch = terrainChunks[slot];
  if (ch.cx >= 0)  // recycling: unmap the chunk this slot held
    terrainChunkSlot[ch.cz * terrainChunksX + ch.cx] = -1;
  ch.cx = cx;
  ch.cz = cz;
  terrainChunkSlot[cz * terrainChunksX + cx] = (short)slot;

  const int cellsX = HM_W - 1;
  const int cellsZ = HM_D - 1;
  const float stepX = (float)TERRAIN_WIDTH / cellsX;
  const float stepZ = (float)TERRAIN_DEPTH / cellsZ;
  const float startX = -TERRAIN_WIDTH * 0.5F;
  const float startZ = -TERRAIN_DEPTH * 0.5F;

  auto hAt = [&](int ix, int iz) {
    if (ix < 0) ix = 0;
    if (iz < 0) iz = 0;
    if (ix > HM_W - 1) ix = HM_W - 1;
    if (iz > HM_D - 1) iz = HM_D - 1;
    return TERRAIN_HEIGHTS[iz * HM_W + ix];
  };
  {
    // Height extent over this chunk's vertex grid - the portal dead-zone
    // test in renderTerrain does an exact AABB-vs-plane check with it.
    const int mgx0 = cx * TERRAIN_CHUNK_CELLS;
    const int mgz0 = cz * TERRAIN_CHUNK_CELLS;
    const int mgx1 = mgx0 + TERRAIN_CHUNK_CELLS > cellsX ? cellsX
                                                         : mgx0 + TERRAIN_CHUNK_CELLS;
    const int mgz1 = mgz0 + TERRAIN_CHUNK_CELLS > cellsZ ? cellsZ
                                                         : mgz0 + TERRAIN_CHUNK_CELLS;
    ch.minY = ch.maxY = hAt(mgx0, mgz0);
    for (int iz = mgz0; iz <= mgz1; ++iz)
      for (int ix = mgx0; ix <= mgx1; ++ix) {
        const float h = hAt(ix, iz);
        if (h < ch.minY) ch.minY = h;
        if (h > ch.maxY) ch.maxY = h;
      }
  }
  auto shadeAt = [&](int ix, int iz) -> V3 {
    V3 n = {hAt(ix - 1, iz) - hAt(ix + 1, iz), 2.0F * (stepX < stepZ ? stepX : stepZ),
            hAt(ix, iz - 1) - hAt(ix, iz + 1)};
    const float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 0.00001F) n.x /= len, n.y /= len, n.z /= len;
    V3 s = shadeOf(n);
    const V3 wp = {startX + ix * stepX, hAt(ix, iz), startZ + iz * stepZ};
    const V3 pl = pointLightAt(wp, n);
    s.x += pl.x, s.y += pl.y, s.z += pl.z;
    // Macro ground variation: base and layer passes both shade through here,
    // so a darker patch darkens grass and painted path together.
    if (TERRAIN_TINT_VARIATION > 0.0F) {
      const float tv =
          1.0F + TERRAIN_TINT_VARIATION *
                     (tintNoise2(wp.x, wp.z, TERRAIN_TINT_SCALE) - 0.5F);
      s.x *= tv, s.y *= tv, s.z *= tv;
    }
    if (s.x > 1.0F) s.x = 1.0F;
    if (s.y > 1.0F) s.y = 1.0F;
    if (s.z > 1.0F) s.z = 1.0F;
    return s;
  };

  // No material: two greens in a checker pattern. With a material, the Kd
  // tint colors every cell uniformly - textured terrain modulates the map
  // (PS2 modulation: 128 = 1.0, so Kd*128), flat terrain uses Kd*255.
  const bool textured = TERRAIN_TEXTURE >= 0;
  const bool hasMat = TERRAIN_HAS_MATERIAL;
  const float k = textured ? 128.0F : 255.0F;
  const float baseA[3] = {hasMat ? TERRAIN_TINT_R * k : 96.0F,
                          hasMat ? TERRAIN_TINT_G * k : 160.0F,
                          hasMat ? TERRAIN_TINT_B * k : 72.0F};
  const float baseB[3] = {hasMat ? TERRAIN_TINT_R * k : 74.0F,
                          hasMat ? TERRAIN_TINT_G * k : 128.0F,
                          hasMat ? TERRAIN_TINT_B * k : 56.0F};

  const int gx0 = cx * TERRAIN_CHUNK_CELLS;
  const int gz0 = cz * TERRAIN_CHUNK_CELLS;
  int gx1 = gx0 + TERRAIN_CHUNK_CELLS;
  int gz1 = gz0 + TERRAIN_CHUNK_CELLS;
  if (gx1 > cellsX) gx1 = cellsX;  // edge chunks cover the remainder
  if (gz1 > cellsZ) gz1 = cellsZ;

  ch.vertices.clear();
  ch.colors.clear();
  ch.sts.clear();
  ch.vertices.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6);
  ch.colors.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6);
  if (textured) ch.sts.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6);

  // Painted terrain layers: find which layers have any weight on this chunk's
  // vertices - each gets one extra alpha-blended pass sharing ch.vertices.
  // (Bag pointers into layerPasses elements are re-set after the resize below,
  // every build - the vector may reallocate.)
  const int layerN = TERRAIN_LAYER_COUNT;
  const unsigned char* splatW8 = TERRAIN_SPLAT_WEIGHTS;
  int activeLayers[TERRAIN_MAX_LAYERS];
  int activeN = 0;
  if (layerN > 0 && splatW8) {
    for (int l = 0; l < layerN; ++l) {
      bool any = false;
      for (int z = gz0; z <= gz1 && !any; ++z)
        for (int x = gx0; x <= gx1; ++x)
          if (splatW8[((size_t)z * HM_W + x) * layerN + l]) {
            any = true;
            break;
          }
      if (any) activeLayers[activeN++] = l;
    }
  }
  ch.layerPasses.resize(activeN);
  for (int a = 0; a < activeN; ++a) {
    TerrainChunk::LayerPass& lp = ch.layerPasses[a];
    lp.layer = activeLayers[a];
    lp.colors.clear();
    lp.sts.clear();
    lp.colors.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6);
    lp.sts.reserve((size_t)(gx1 - gx0) * (gz1 - gz0) * 6);
  }
  auto splatAt = [&](int ix, int iz, int l) -> float {
    if (ix < 0) ix = 0;
    if (iz < 0) iz = 0;
    if (ix > HM_W - 1) ix = HM_W - 1;
    if (iz > HM_D - 1) iz = HM_D - 1;
    return splatW8[((size_t)iz * HM_W + ix) * layerN + l] / 255.0F;
  };

  for (int z = gz0; z < gz1; ++z) {
    for (int x = gx0; x < gx1; ++x) {
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
        ch.sts.push_back(
            Vec4(wx * TERRAIN_TILE_U, wz * TERRAIN_TILE_V, 1.0F, 0.0F));
      };

      ch.vertices.push_back(Vec4(x0, h00, z0, 1.0F));
      ch.vertices.push_back(Vec4(x1, h10, z0, 1.0F));
      ch.vertices.push_back(Vec4(x0, h01, z1, 1.0F));
      ch.vertices.push_back(Vec4(x1, h10, z0, 1.0F));
      ch.vertices.push_back(Vec4(x1, h11, z1, 1.0F));
      ch.vertices.push_back(Vec4(x0, h01, z1, 1.0F));

      if (textured) {
        st(x0, z0);
        st(x1, z0);
        st(x0, z1);
        st(x1, z0);
        st(x1, z1);
        st(x0, z1);
      }

      ch.colors.push_back(shaded(s00));
      ch.colors.push_back(shaded(s10));
      ch.colors.push_back(shaded(s01));
      ch.colors.push_back(shaded(s10));
      ch.colors.push_back(shaded(s11));
      ch.colors.push_back(shaded(s01));

      // Layer passes: same triangles, tiled layer STs, shade-lit tint colors
      // whose alpha is the painted weight (128 = fully this layer). Weights sit
      // on the vertices, so the GS Gouraud-interpolates the blend per pixel.
      for (int a = 0; a < activeN; ++a) {
        TerrainChunk::LayerPass& lp = ch.layerPasses[a];
        const int l = lp.layer;
        const float* tint = TERRAIN_LAYER_TINTS[g_activeScene][l];
        const bool ltex = TERRAIN_LAYER_TEXTURES[g_activeScene][l] >= 0;
        const float lk = ltex ? 128.0F : 255.0F;
        const float ltu = TERRAIN_LAYER_TILE_US[g_activeScene][l];
        const float ltv = TERRAIN_LAYER_TILE_VS[g_activeScene][l];
        auto lcol = [&](const V3& s, float w) {
          return Color(tint[0] * lk * s.x, tint[1] * lk * s.y,
                       tint[2] * lk * s.z, w * 128.0F);
        };
        const float w00 = splatAt(x, z, l), w10 = splatAt(x + 1, z, l);
        const float w01 = splatAt(x, z + 1, l), w11 = splatAt(x + 1, z + 1, l);
        lp.colors.push_back(lcol(s00, w00));
        lp.colors.push_back(lcol(s10, w10));
        lp.colors.push_back(lcol(s01, w01));
        lp.colors.push_back(lcol(s10, w10));
        lp.colors.push_back(lcol(s11, w11));
        lp.colors.push_back(lcol(s01, w01));
        auto lst = [&](float wx, float wz) {
          lp.sts.push_back(Vec4(wx * ltu, wz * ltv, 1.0F, 0.0F));
        };
        lst(x0, z0);
        lst(x1, z0);
        lst(x0, z1);
        lst(x1, z0);
        lst(x1, z1);
        lst(x0, z1);
      }
    }
  }

  if (!ch.bag) {
    ch.colorBag = std::make_unique<StaPipColorBag>();
    ch.bag = std::make_unique<StaPipBag>();
    ch.bag->info = infoBag.get();
    ch.bag->lighting = nullptr;
  }
  ch.colorBag->many = ch.colors.data();
  ch.bag->color = ch.colorBag.get();
  ch.bag->vertices = ch.vertices.data();
  ch.bag->count = static_cast<u32>(ch.vertices.size());
  if (textured && loadedTextures[TERRAIN_TEXTURE]) {
    ch.texBag.texture = loadedTextures[TERRAIN_TEXTURE];
    ch.texBag.coordinates = ch.sts.data();
    ch.bag->texture = &ch.texBag;
  } else {
    ch.bag->texture = nullptr;
  }
  // Reused slot = same bag pointer with new vertex content: without the bump
  // the engine's bbox cacher would cull this chunk with the old chunk's boxes.
  ch.bag->bboxVersion = ++g_bboxStamp;

  // Layer-pass bags: blend-enabled info bag, the chunk's own vertices, the
  // pass's tiled STs and weight-alpha colors. Pointers are (re)set every build
  // because resize() may have moved the LayerPass elements.
  for (TerrainChunk::LayerPass& lp : ch.layerPasses) {
    if (!lp.bag) {
      lp.colorBag = std::make_unique<StaPipColorBag>();
      lp.bag = std::make_unique<StaPipBag>();
      lp.bag->lighting = nullptr;
    }
    lp.bag->info = layerInfoBag.get();
    lp.colorBag->many = lp.colors.data();
    lp.bag->color = lp.colorBag.get();
    lp.bag->vertices = ch.vertices.data();
    lp.bag->count = static_cast<u32>(ch.vertices.size());
    const int lti = TERRAIN_LAYER_TEXTURES[g_activeScene][lp.layer];
    if (lti >= 0 && loadedTextures[lti]) {
      lp.texBag.texture = loadedTextures[lti];
      lp.texBag.coordinates = lp.sts.data();
      lp.bag->texture = &lp.texBag;
    } else {
      lp.bag->texture = nullptr;
    }
    lp.bag->bboxVersion = ++g_bboxStamp;
  }

  // World AABB of the built mesh - the split-band cull tests it per half.
  // One pass at build time, nothing per frame.
  if (!ch.vertices.empty()) {
    ch.aabbMin[0] = ch.aabbMax[0] = ch.vertices[0].x;
    ch.aabbMin[1] = ch.aabbMax[1] = ch.vertices[0].y;
    ch.aabbMin[2] = ch.aabbMax[2] = ch.vertices[0].z;
    for (const Vec4& v : ch.vertices) {
      if (v.x < ch.aabbMin[0]) ch.aabbMin[0] = v.x;
      if (v.x > ch.aabbMax[0]) ch.aabbMax[0] = v.x;
      if (v.y < ch.aabbMin[1]) ch.aabbMin[1] = v.y;
      if (v.y > ch.aabbMax[1]) ch.aabbMax[1] = v.y;
      if (v.z < ch.aabbMin[2]) ch.aabbMin[2] = v.z;
      if (v.z > ch.aabbMax[2]) ch.aabbMax[2] = v.z;
    }
  }
}

// Unbuilt chunks in the current view rect (the same rect updateTerrainChunks
// builds into). loadScene uses this as the loading-bar denominator and to
// drive the batched terrain drain to completion.
int TerrainGame::countPendingChunks(float focusX, float focusZ) {
  if (terrainChunksX <= 0 || terrainChunksZ <= 0 || !infoBag) return 0;
  const int cellsX = HM_W - 1;
  const int cellsZ = HM_D - 1;
  const float spanX = TERRAIN_CHUNK_CELLS * ((float)TERRAIN_WIDTH / cellsX);
  const float spanZ = TERRAIN_CHUNK_CELLS * ((float)TERRAIN_DEPTH / cellsZ);
  const float startX = -TERRAIN_WIDTH * 0.5F;
  const float startZ = -TERRAIN_DEPTH * 0.5F;
  int cx0 = 0, cz0 = 0, cx1 = terrainChunksX - 1, cz1 = terrainChunksZ - 1;
  if (TERRAIN_VIEW_DISTANCE > 0.0F) {
    auto clampX = [&](int v) {
      return v < 0 ? 0 : (v > terrainChunksX - 1 ? terrainChunksX - 1 : v);
    };
    auto clampZ = [&](int v) {
      return v < 0 ? 0 : (v > terrainChunksZ - 1 ? terrainChunksZ - 1 : v);
    };
    cx0 = clampX((int)((focusX - TERRAIN_VIEW_DISTANCE - startX) / spanX));
    cx1 = clampX((int)((focusX + TERRAIN_VIEW_DISTANCE - startX) / spanX));
    cz0 = clampZ((int)((focusZ - TERRAIN_VIEW_DISTANCE - startZ) / spanZ));
    cz1 = clampZ((int)((focusZ + TERRAIN_VIEW_DISTANCE - startZ) / spanZ));
  }
  int pending = 0;
  for (int cz = cz0; cz <= cz1; ++cz)
    for (int cx = cx0; cx <= cx1; ++cx)
      if (terrainChunkSlot[cz * terrainChunksX + cx] < 0) ++pending;
  return pending;
}

// Keeps the resident chunk set centered on the view focus. View distance off
// (0) = the whole map stays resident (small maps - matches the old
// behavior). Otherwise chunks outside the focus rect are freed - with one
// tile of hysteresis so walking along a border doesn't rebuild the same ring
// every frame - and missing ones are built nearest-first, `budget` per call
// (loadScene passes INT_MAX to drain behind the loading screen).
void TerrainGame::updateTerrainChunks(float focusX, float focusZ,
                                      float focus2X, float focus2Z,
                                      bool twoFoci, int budget) {
  if (terrainChunksX <= 0 || terrainChunksZ <= 0 || !infoBag) return;
  const int cellsX = HM_W - 1;
  const int cellsZ = HM_D - 1;
  const float spanX = TERRAIN_CHUNK_CELLS * ((float)TERRAIN_WIDTH / cellsX);
  const float spanZ = TERRAIN_CHUNK_CELLS * ((float)TERRAIN_DEPTH / cellsZ);
  const float startX = -TERRAIN_WIDTH * 0.5F;
  const float startZ = -TERRAIN_DEPTH * 0.5F;

  // One view rect per focus. With two players apart, the rects are disjoint;
  // a chunk survives if it sits in (a hysteresis ring around) EITHER rect -
  // evicting on a single rect would make the two split-screen passes throw
  // out each other's terrain and burn the whole build budget on churn.
  struct Rect {
    int cx0, cz0, cx1, cz1;
    float fx, fz;
  };
  Rect rects[2];
  int rectCount = 0;
  auto addRect = [&](float fx, float fz) {
    Rect r = {0, 0, terrainChunksX - 1, terrainChunksZ - 1, fx, fz};
    if (TERRAIN_VIEW_DISTANCE > 0.0F) {
      auto clampX = [&](int v) {
        return v < 0 ? 0 : (v > terrainChunksX - 1 ? terrainChunksX - 1 : v);
      };
      auto clampZ = [&](int v) {
        return v < 0 ? 0 : (v > terrainChunksZ - 1 ? terrainChunksZ - 1 : v);
      };
      r.cx0 = clampX((int)((fx - TERRAIN_VIEW_DISTANCE - startX) / spanX));
      r.cx1 = clampX((int)((fx + TERRAIN_VIEW_DISTANCE - startX) / spanX));
      r.cz0 = clampZ((int)((fz - TERRAIN_VIEW_DISTANCE - startZ) / spanZ));
      r.cz1 = clampZ((int)((fz + TERRAIN_VIEW_DISTANCE - startZ) / spanZ));
    }
    rects[rectCount++] = r;
  };
  addRect(focusX, focusZ);
  if (twoFoci) addRect(focus2X, focus2Z);

  if (TERRAIN_VIEW_DISTANCE > 0.0F) {
    for (TerrainChunk& ch : terrainChunks) {
      if (ch.cx < 0) continue;
      bool keep = false;
      for (int r = 0; r < rectCount && !keep; ++r)
        keep = ch.cx >= rects[r].cx0 - 1 && ch.cx <= rects[r].cx1 + 1 &&
               ch.cz >= rects[r].cz0 - 1 && ch.cz <= rects[r].cz1 + 1;
      if (!keep) {
        terrainChunkSlot[ch.cz * terrainChunksX + ch.cx] = -1;
        ch.cx = ch.cz = -1;  // buffers keep their capacity for the next build
      }
    }
  }

  while (budget > 0) {
    // Nearest unbuilt chunk to its own rect's focus, across both rects. The
    // rects are small (a handful of tiles across), so a per-call linear scan
    // beats maintaining a build queue that would need reordering on every
    // focus move.
    int bestCx = -1, bestCz = -1;
    float bestD = 0.0F;
    for (int r = 0; r < rectCount; ++r)
      for (int cz = rects[r].cz0; cz <= rects[r].cz1; ++cz)
        for (int cx = rects[r].cx0; cx <= rects[r].cx1; ++cx) {
          if (terrainChunkSlot[cz * terrainChunksX + cx] >= 0) continue;
          const float dx = startX + (cx + 0.5F) * spanX - rects[r].fx;
          const float dz = startZ + (cz + 0.5F) * spanZ - rects[r].fz;
          const float d = dx * dx + dz * dz;
          if (bestCx < 0 || d < bestD) {
            bestCx = cx;
            bestCz = cz;
            bestD = d;
          }
        }
    if (bestCx < 0) return;  // everything in view is built

    int slot = -1;
    for (int s = 0; s < (int)terrainChunks.size(); ++s)
      if (terrainChunks[s].cx < 0) {
        slot = s;
        break;
      }
    if (slot < 0) return;  // pool momentarily full - eviction frees one soon

    buildTerrainChunk(slot, bestCx, bestCz);
    --budget;
  }
}

// Two planes bounding the CENTRAL half of the full-height projection - the
// rows a split half can actually show. The raster crop (XYOFFSET + scissor)
// keeps the projection and the engine's frustum planes full-height, so
// without this every half transforms ~2x the geometry it displays; anything
// wholly outside the band skips submission instead. 0.62 instead of the
// exact 0.5 leaves margin for the clipper's guard band - conservative,
// never visibly wrong. Degenerate views (looking straight up/down) disable
// the cull for the pass rather than guess.
void TerrainGame::computeSplitBand() {
  Vec4 f = cameraLookAt - cameraPosition;
  const float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
  if (fl < 0.0001F) {
    splitBandActive = false;
    return;
  }
  f.x /= fl, f.y /= fl, f.z /= fl;
  // Roll-free camera up: world up orthonormalized against the forward.
  float ux = -f.x * f.y, uy = 1.0F - f.y * f.y, uz = -f.z * f.y;
  const float ul = sqrtf(ux * ux + uy * uy + uz * uz);
  if (ul < 0.05F) {
    splitBandActive = false;
    return;
  }
  ux /= ul, uy /= ul, uz /= ul;
  const float t =
      0.62F * tanf(engine->renderer.core.renderer3D.getFov() * (PI / 360.0F));
  const float inv = 1.0F / sqrtf(1.0F + t * t);
  const float sa = t * inv, ca = inv;
  splitBandP[0] = cameraPosition.x;
  splitBandP[1] = cameraPosition.y;
  splitBandP[2] = cameraPosition.z;
  splitBandN[0][0] = f.x * sa - ux * ca;  // top edge: inside = below it
  splitBandN[0][1] = f.y * sa - uy * ca;
  splitBandN[0][2] = f.z * sa - uz * ca;
  splitBandN[1][0] = f.x * sa + ux * ca;  // bottom edge: inside = above it
  splitBandN[1][1] = f.y * sa + uy * ca;
  splitBandN[1][2] = f.z * sa + uz * ca;
}

bool TerrainGame::outsideSplitBand(const float mn[3], const float mx[3]) const {
  const float cx = 0.5F * (mn[0] + mx[0]) - splitBandP[0];
  const float cy = 0.5F * (mn[1] + mx[1]) - splitBandP[1];
  const float cz = 0.5F * (mn[2] + mx[2]) - splitBandP[2];
  const float ex = 0.5F * (mx[0] - mn[0]);
  const float ey = 0.5F * (mx[1] - mn[1]);
  const float ez = 0.5F * (mx[2] - mn[2]);
  for (int p = 0; p < 2; ++p) {
    const float* n = splitBandN[p];
    const float r = ex * fabsf(n[0]) + ey * fabsf(n[1]) + ez * fabsf(n[2]);
    if (cx * n[0] + cy * n[1] + cz * n[2] + r < 0.0F) return true;
  }
  return false;
}

// AABB of a static object, sized like the springArm/box-collision one; a
// rotated object falls back to its bounding-sphere cube so the test can
// under-cull but never over-cull.
bool TerrainGame::objectOutsideSplitBand(int i) const {
  const RuntimeObject& o = runtimeObjects[i];
  const GameModel* gm = nullptr;
  if (o.data.type == 5 && o.data.model >= 0 &&
      o.data.model < (int)gameModels.size())
    gm = &gameModels[o.data.model];
  const SkelModel* anim = nullptr;
  if (o.data.type == 5 && o.data.animModel >= 0 &&
      o.data.animModel < (int)gameAnimModels.size())
    anim = gameAnimModels[o.data.animModel].src.get();
  float cx = o.data.position[0], cy = o.data.position[1],
        cz = o.data.position[2];
  float ex = 0.5F * o.data.scale[0], ey = 0.5F * o.data.scale[1],
        ez = 0.5F * o.data.scale[2];
  float ox = 0.0F, oy = 0.0F, oz = 0.0F;  // local AABB center offset
  const float* mnp = gm ? gm->mn : (anim ? anim->min : nullptr);
  const float* mxp = gm ? gm->mx : (anim ? anim->max : nullptr);
  if (mnp && mxp) {
    ox = 0.5F * (mnp[0] + mxp[0]) * o.data.scale[0];
    oy = 0.5F * (mnp[1] + mxp[1]) * o.data.scale[1];
    oz = 0.5F * (mnp[2] + mxp[2]) * o.data.scale[2];
    ex = 0.5F * (mxp[0] - mnp[0]) * o.data.scale[0];
    ey = 0.5F * (mxp[1] - mnp[1]) * o.data.scale[1];
    ez = 0.5F * (mxp[2] - mnp[2]) * o.data.scale[2];
  }
  const float* rot = o.data.rotation;
  if (rot[0] != 0.0F || rot[1] != 0.0F || rot[2] != 0.0F ||
      o.data.modelYaw != 0.0F) {
    // Rotation moves both the extents and the center offset in world space -
    // bound everything with the diagonal radius around the position.
    const float r = sqrtf(ex * ex + ey * ey + ez * ez) +
                    sqrtf(ox * ox + oy * oy + oz * oz);
    ex = ey = ez = r;
    ox = oy = oz = 0.0F;
  }
  cx += ox, cy += oy, cz += oz;
  const float mn[3] = {cx - ex, cy - ey, cz - ez};
  const float mx[3] = {cx + ex, cy + ey, cz + ez};
  return outsideSplitBand(mn, mx);
}

void TerrainGame::renderTerrain() {
  for (TerrainChunk& ch : terrainChunks) {
    if (ch.cx < 0 || !ch.bag || ch.bag->count == 0) continue;
    if (portalExitPlaneOn) {
      // Portal through-view dead zone: skip chunks fully on the virtual
      // camera's side of the exit plane - through a real hole that region
      // is invisible, and with no oblique near plane on the PS2 it would
      // otherwise render its backside INTO the opening (a floor->ceiling
      // pair puts the virtual eye underground). Exact AABB-vs-plane check:
      // the chunk's rect + its built minY/maxY, tested at the p-vertex
      // (the box corner farthest along the plane normal) - no slope
      // margin to mis-tune. A straddling chunk still renders whole.
      const int cellsX = HM_W - 1, cellsZ = HM_D - 1;
      const float stepX = (float)TERRAIN_WIDTH / cellsX;
      const float stepZ = (float)TERRAIN_DEPTH / cellsZ;
      const int gx0 = ch.cx * TERRAIN_CHUNK_CELLS;
      const int gz0 = ch.cz * TERRAIN_CHUNK_CELLS;
      const int gx1 = gx0 + TERRAIN_CHUNK_CELLS > cellsX
                          ? cellsX
                          : gx0 + TERRAIN_CHUNK_CELLS;
      const int gz1 = gz0 + TERRAIN_CHUNK_CELLS > cellsZ
                          ? cellsZ
                          : gz0 + TERRAIN_CHUNK_CELLS;
      const float x0w = -TERRAIN_WIDTH * 0.5F + gx0 * stepX;
      const float x1w = -TERRAIN_WIDTH * 0.5F + gx1 * stepX;
      const float z0w = -TERRAIN_DEPTH * 0.5F + gz0 * stepZ;
      const float z1w = -TERRAIN_DEPTH * 0.5F + gz1 * stepZ;
      const float pvx = portalExitPlane[0] > 0.0F ? x1w : x0w;
      const float pvy = portalExitPlane[1] > 0.0F ? ch.maxY : ch.minY;
      const float pvz = portalExitPlane[2] > 0.0F ? z1w : z0w;
      if (portalExitPlane[0] * pvx + portalExitPlane[1] * pvy +
              portalExitPlane[2] * pvz - portalExitPlane[3] <
          -0.05F)
        continue;
    }
    // Split halves: skip chunks entirely above/below the visible band before
    // the engine's (full-height) frustum classify sees them.
    if (splitBandActive && outsideSplitBand(ch.aabbMin, ch.aabbMax)) continue;
    stapip.core.render(ch.bag.get());
    // Painted layers: alpha-blend over the base pass right away (same
    // geometry = equal depth passes the GS >= z-test; keeping base + layers
    // adjacent also keeps the texture cache warm per chunk).
    for (TerrainChunk::LayerPass& lp : ch.layerPasses)
      if (lp.bag && lp.bag->count > 0) stapip.core.render(lp.bag.get());
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
  // geometry. Clip 0.15 units in front of the camera instead - just past
  // the real near plane (0.1) and closer than the collision clearance the
  // walkers guarantee (playerRadius 0.35, EYE_CLEARANCE 0.2), so a wall
  // the player presses against can never fall in front of the clip plane
  // and open a see-through hole. (read during setRenderer below)
  PlanesClipAlgorithm::clipMargin =
      -(engine->renderer.core.getSettings().getNear() + 0.15F);

  // Wall-clock normalization: per-frame steps below are tuned for 50 Hz;
  // g_frameScale stretches them so NTSC's 60 Hz plays at the same speed.
  // These are the seeds - updateFrameClock() refreshes both every frame
  // from the measured frame time (frame drops slow the picture, not the
  // game; also what makes the vsync-off build play at the right speed).
  g_frameRate = engine->renderer.core.getSettings().getRefreshRate();
  g_frameDt = 1.0F / g_frameRate;
  g_frameScale = 50.0F / g_frameRate;
  // Seed the analog stick response curves from the project defaults (the
  // Set Stick Curve flow node overrides them at runtime; namespaced constants
  // so they cannot initialize the global-scope g_stick* definitions directly).
  g_stickCurveL = STICK_CURVE_L;
  g_stickCurveR = STICK_CURVE_R;
  g_stickExpL = STICK_EXP_L;
  g_stickExpR = STICK_EXP_R;
  // Experimental (Project > Preferences > Build): skip the vsync wait -
  // continuous frame rate instead of the 50/25 vsync snap, with tearing.
  if (!FRAME_LIMIT) engine->renderer.core.setFrameLimit(false);

  stapip.setRenderer(&engine->renderer.core);
  // Hidden "clipping": "vu1" mode: frustum-crossing packages are clipped by
  // the VU1 clip programs instead of the EE clipper (must follow setRenderer).
  stapip.core.setVU1Clipping(CLIP_VU1);
  engine->renderer.core.postFx.setBloom(POSTFX_BLOOM);
  engine->renderer.core.postFx.setGrain(POSTFX_GRAIN);
  engine->renderer.core.postFx.setDepthOfField(POSTFX_DOF_FOCUS,
                                               POSTFX_DOF_RANGE, POSTFX_DOF);
  // GS hardware distance fog (Scene/Project > Preferences > Fog).
  if (FOG_ENABLED)
    engine->renderer.core.setFog(Color(FOG_R, FOG_G, FOG_B), FOG_START,
                                 FOG_END);
  else
    engine->renderer.core.disableFog();
  // Default color grading look (Tools > Color Grading); no-op when -1.
  // Grading is global - scene switches keep whatever preset is active.
  applySceneGrading(engine, GRADING_DEFAULT);

  engine->renderer.setClearScreenColor(Color(SKY_R, SKY_G, SKY_B));

  // Two-player modes: open pad 2 (connector 2). Optional - no controller
  // there never blocks or asserts; it keeps polling so player 2 can plug in
  // and join mid-game (Start, see the loop).
  if (MULTIPLAYER_MODE != 0) pad2.initOptional(1);

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

  // scriptCtx wiring + scripts' init() run from bootFirstScene() (loop boot),
  // after the deferred scene load - scripts' onStart must see scene 0's objects.

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

  // "USE" prompt, shown while looking at a usable object. Placement and
  // image come from hud_data.gen.hpp (Tools > UI Editor - the built-in
  // hud/use.png unless a custom sprite replaces it).
  usePromptSprite.mode = SpriteMode::MODE_STRETCH;
  usePromptSprite.size = Vec2(USE_PROMPT_W, USE_PROMPT_H);
  usePromptSprite.position =
      Vec2(USE_PROMPT_X * screen.getWidth() - USE_PROMPT_W * 0.5F,
           USE_PROMPT_Y * screen.getHeight() - USE_PROMPT_H * 0.5F);
  auto* useTexture = engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd(USE_PROMPT_PATH));
  useTexture->addLink(usePromptSprite.id);
  // "PICK UP" variant, shown instead when the looked-at object is pickable.
  // Same placement; its own texture (hud/pickup.png, replace to customize).
  pickPromptSprite.mode = SpriteMode::MODE_STRETCH;
  pickPromptSprite.size = usePromptSprite.size;
  pickPromptSprite.position = usePromptSprite.position;
  auto* pickTexture = engine->renderer.getTextureRepository().add(
      FileUtils::fromCwd(PICK_PROMPT_PATH));
  pickTexture->addLink(pickPromptSprite.id);

  // The loading screen (loading_data.gen.hpp) builds its own sprites lazily on
  // first present (loadingscreen::renderFrame), so nothing to set up here.

  // Sound emitter samples (adpenc output next to the ELF)
  for (int i = 0; i < SND_COUNT; ++i)
    sndSamples.push_back(
        engine->audio.adpcm.load(FileUtils::fromCwd(SND_PATHS[i])));
}

void TerrainGame::loop() {
  updateFrameClock();  // real dt: frame drops slow the picture, not the game
  // The engine pumps pad 1; pad 2 is ours (optional - polls for a hot-join).
  if (MULTIPLAYER_MODE != 0) pad2.update();

  // Boot sequence (the engine holds the Tyra logo ~2s before this):
  //   phase 0 - boot splash images, each shown for its duration (in order),
  //   phase 1 - load scene 0 behind the loading screen (when enabled).
  // Everything runs from the loop, not init(): a frame presented from init()
  // (before the main loop) isn't vsync-paced and flashes by, so the boot
  // visuals were invisible; from the loop they pace normally.
  if (bootPhase < 2) {
    if (bootPhase == 0) {
      if (splashIndex < SPLASH_COUNT) {
        if (splashFrames <= 0)
          splashFrames = everyFrames(SPLASHES[splashIndex].seconds);
        loadingscreen::renderSplash(engine, splashIndex);
        if (--splashFrames <= 0) ++splashIndex;
        return;
      }
      bootPhase = 1;
      if (LOADING_SCREEN) {
        loadingTarget = 0;
        loadingFrames = loadingTotal = everyFrames(0.7F);
      }
    }
    if (bootPhase == 1) {
      if (!LOADING_SCREEN) {
        bootFirstScene();
        bootPhase = 2;
      } else {
        if (loadingFrames > 0) {
          const bool preLoad = loadingFrames > loadingTotal - 5;
          loadingscreen::renderFrame(engine, 0, preLoad ? 0.0F : 1.0F);
          --loadingFrames;
          if (loadingFrames == loadingTotal - 5) bootFirstScene();
          return;
        }
        bootPhase = 2;
      }
    }
  }

  const bool saveMenuActive = updateSaveMenu();
  const bool gameMenuWasOpen = gameMenuIndex >= 0;  // before updateGameMenu()
  const bool gameMenuPausing = updateGameMenu();  // false for overlay menus
  const bool menuActive = saveMenuActive || gameMenuPausing;
  // An open menu owns the pad even when it doesn't pause the world (overlay
  // menus, and the frame X closes a pausing menu): gameplay must not read that
  // same press too, or the X that drives the menu also makes the player jump.
  const bool menuOwnsPad =
      saveMenuActive || gameMenuWasOpen || gameMenuIndex >= 0;
  g_gameplayPaused = menuActive;  // freezes particles + animation playback
  // Option-block menu rows drive their bound engine settings every frame
  // (volume, deadzone, curve, display) - runs regardless of pause so a saved
  // setting keeps applying, and before applyVideoRequests so a display switch
  // it requests lands this frame.
  applyMenuBindings();
  // Portal crossing test: the walker's position before this frame's movement
  // (Player entity when the scene has one, the built-in FPP walker otherwise)
  const bool portalEnt = PLAYER_INDEX >= 0;
  const float portalPrevX = portalEnt ? players[0].x : playerX;
  const float portalPrevY = portalEnt ? players[0].y : playerY;
  const float portalPrevZ = portalEnt ? players[0].z : playerZ;
  // Player 2 hot-join: Start on pad 2, any time gameplay owns the pads.
  // Leaving goes through a menu "Player count" Toggle (setPlayerTwoActive).
  if (MULTIPLAYER_MODE != 0 && P2_JOIN_ON_START && !menuOwnsPad &&
      !playerTwoActive && pad2.getClicked().Start)
    setPlayerTwoActive(true);
  if (!menuOwnsPad) {
    if (!updatePlayerEntity()) updatePlayer();
    updateUseTarget();
  }

  scriptCtx.playerPosition = cameraPosition;
  scriptCtx.player2Active =
      MULTIPLAYER_MODE != 0 && playerTwoActive && players[1].objIndex >= 0;
  scriptCtx.player2Position =
      scriptCtx.player2Active
          ? Vec4(players[1].x, players[1].y + PP_EYE_HEIGHT(1), players[1].z)
          : scriptCtx.playerPosition;
  {
    // View direction for the scripts (Raycast flow node)
    Vec4 look = cameraLookAt - cameraPosition;
    const float lookLen =
        sqrtf(look.x * look.x + look.y * look.y + look.z * look.z);
    scriptCtx.playerLook = lookLen > 0.0001F
                               ? Vec4(look.x / lookLen, look.y / lookLen,
                                      look.z / lookLen)
                               : Vec4(0.0F, 0.0F, 1.0F);
  }
  if (menuOwnsPad) { scriptCtx.usedObject = -1; useTargetIndex = -1; }
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
      loadingFrames = loadingTotal = everyFrames(0.7F);  // ~0.7s hold
    } else {
      loadScene(target);
      fppSpawnPending = true;
    }
  }
  if (loadingFrames > 0) {
    // A few frames at 0% before the (blocking) load, which pumps the bar from
    // 0 to 1 itself, then the remaining frames at 100%.
    const bool preLoad = loadingFrames > loadingTotal - 5;
    loadingscreen::renderFrame(engine, loadingTarget, preLoad ? 0.0F : 1.0F);
    --loadingFrames;
    if (loadingFrames == loadingTotal - 5) {  // a few frames shown first
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

  // Streaming layers: Load/Unload Layer requests, one asset load per frame,
  // trickle activation of freshly resident layers.
  updateLayerStreaming();

  // Flow graph / script teleport request: move the Player entity when the
  // scene has one, the built-in FPP player otherwise. Teleports P1; an
  // active P2 comes along, dropped a step to the side.
  if (scriptCtx.teleport) {
    scriptCtx.teleport = false;
    if (PLAYER_INDEX >= 0) {
      players[0].x = scriptCtx.teleportPos.x;
      players[0].y = scriptCtx.teleportPos.y;
      players[0].z = scriptCtx.teleportPos.z;
      players[0].velY = 0.0F;
      players[0].yaw = scriptCtx.teleportYaw * PI / 180.0F;
      if (playerTwoActive && players[1].objIndex >= 0) {
        players[1].x = players[0].x + 1.2F;
        players[1].z = players[0].z;
        players[1].y = PP_MODE(1) == 1 ? players[0].y
                                       : terrainHeightAt(players[1].x, players[1].z);
        players[1].velY = 0.0F;
        players[1].yaw = players[0].yaw;
      }
    } else {
      playerX = scriptCtx.teleportPos.x;
      playerY = scriptCtx.teleportPos.y;
      playerZ = scriptCtx.teleportPos.z;
      playerVelY = 0.0F;
      yaw = scriptCtx.teleportYaw * PI / 180.0F;
    }
  }

  if (!menuActive) updateObjectPhysics();
  // Portal surfaces: carry the player / physics objects that crossed a
  // linked portal through to its target. After the physics step so object
  // crossings see this frame's motion; on a player hop the camera is
  // rebuilt inside, so no frame renders from the departure side.
  if (PORTAL_COUNT > 0 && !menuActive) {
    if (portalEnt)
      updatePortals(portalPrevX, portalPrevY, portalPrevZ, &players[0].x, &players[0].y,
                    &players[0].z, &players[0].yaw, &players[0].pitch, &players[0].velY,
                    PLAYER_MODE == 1 ? 0.0F : PLAYER_EYE_HEIGHT);
    else
      updatePortals(portalPrevX, portalPrevY, portalPrevZ, &playerX,
                    &playerY, &playerZ, &yaw, &pitch, &playerVelY,
                    EYE_HEIGHT);
  }
  // The carried object is positioned AFTER the portal step: on the frame the
  // player teleports, updatePortals rebuilds the camera to the arrival side,
  // so placing the object here anchors it in front of the ARRIVAL camera -
  // otherwise it holds one frame at the departure side and blinks as it
  // crosses (owner).
  if (!menuOwnsPad) updateCarriedObject();
  updateParticles();
  updateSoundEmitters();

  // Camera flashlight (Player object > Flashlight). The Set Flashlight flow
  // node drives the master (scriptCtx.flashlight: 0 off / 1 on / -1 = leave);
  // the optional toggle button flips the on/off state. The beam shows only
  // while both are set, so the toggle respects the Enabled master.
  if (scriptCtx.flashlight >= 0) {
    g_flashEnabled = scriptCtx.flashlight != 0;
    scriptCtx.flashlight = -1;
  }
  // Runtime graphics switches (Set Fog / Bloom / Grain / Particles flow nodes).
  if (scriptCtx.fog >= 0) {
    if (scriptCtx.fog)
      engine->renderer.core.setFog(Color(FOG_R, FOG_G, FOG_B), FOG_START, FOG_END);
    else
      engine->renderer.core.disableFog();
    scriptCtx.fog = -1;
  }
  if (scriptCtx.bloom >= 0) {
    engine->renderer.core.postFx.setBloom(scriptCtx.bloom);
    scriptCtx.bloom = -1;
  }
  if (scriptCtx.grain >= 0) {
    engine->renderer.core.postFx.setGrain(scriptCtx.grain);
    scriptCtx.grain = -1;
  }
  if (scriptCtx.dof == -2) {
    // Set Depth Of Field, "Scene setting" mode: back to the authored values
    engine->renderer.core.postFx.setDepthOfField(POSTFX_DOF_FOCUS,
                                                 POSTFX_DOF_RANGE, POSTFX_DOF);
    scriptCtx.dof = -1;
  } else if (scriptCtx.dof >= 0) {
    engine->renderer.core.postFx.setDepthOfField(
        scriptCtx.dofFocus, scriptCtx.dofRange, scriptCtx.dof);
    scriptCtx.dof = -1;
  }
  if (scriptCtx.particles >= 0) {
    g_particlesOn = scriptCtx.particles != 0;
    scriptCtx.particles = -1;
  }
  // Analog stick response curves (Set Stick Curve flow node).
  if (scriptCtx.stickCurveL >= 0) {
    g_stickCurveL = scriptCtx.stickCurveL;
    scriptCtx.stickCurveL = -1;
  }
  if (scriptCtx.stickCurveR >= 0) {
    g_stickCurveR = scriptCtx.stickCurveR;
    scriptCtx.stickCurveR = -1;
  }
  if (scriptCtx.stickExpL >= 1.0F) {
    g_stickExpL = scriptCtx.stickExpL;
    scriptCtx.stickExpL = -1.0F;
  }
  if (scriptCtx.stickExpR >= 1.0F) {
    g_stickExpR = scriptCtx.stickExpR;
    scriptCtx.stickExpR = -1.0F;
  }
  // Pad vibration (Vibrate Pad flow node / padVibrate() in scripts): a
  // request drives the DualShock actuators; rumbleSec > 0 arms the auto-stop
  // countdown (0 = vibrate until the next request). The countdown runs even
  // while a menu pauses the scripts, so a timed rumble always ends.
  if (scriptCtx.rumble >= 0) {
    engine->pad.setActuators(scriptCtx.rumbleSmall != 0, (u8)scriptCtx.rumble);
    g_rumbleTimer = (scriptCtx.rumble > 0 || scriptCtx.rumbleSmall != 0)
                        ? scriptCtx.rumbleSec
                        : 0.0F;
    scriptCtx.rumble = -1;
  }
  if (g_rumbleTimer > 0.0F) {
    g_rumbleTimer -= g_frameDt;
    if (g_rumbleTimer <= 0.0F) {
      g_rumbleTimer = 0.0F;
      engine->pad.setActuators(false, 0);
    }
  }
  // Cutscene camera override: a Cutscene Director sequence with a camera track
  // drives the frame camera (Play/Stop Sequence). Applied after scripts so the
  // sequence player (a global Script) has posed the camera for this frame.
  if (scriptCtx.cameraOverride) {
    cameraPosition = scriptCtx.cameraEye;
    cameraLookAt = scriptCtx.cameraAt;
  }
  // Cutscene "Hide player": drop the third-person avatar for this frame
  // (applied after scripts so the sequence player's flag wins).
  if (PLAYER_INDEX >= 0 && PLAYER_MODE == 2)
    runtimeObjects[PLAYER_INDEX].visible = !scriptCtx.hidePlayer;
  if (players[1].objIndex >= 0 && PP_MODE(1) == 2)
    runtimeObjects[players[1].objIndex].visible =
        !scriptCtx.hidePlayer && playerTwoActive;
  // Runtime video output (Set Display Mode / Set Widescreen flow nodes) +
  // the keep-or-revert countdown. Must run before beginFrame - a scan-mode
  // switch rebuilds the VRAM layout between frames. A switch closes any
  // open game menu: the player judges the new picture unobstructed and the
  // confirm prompt's X press cannot double as a menu select.
  if (applyVideoRequests(engine, scriptCtx)) {
    gameMenuIndex = -1;
    gameMenuStackDepth = 0;
  }
  if (!menuOwnsPad && flashlightTogglePressed(engine)) g_flashOn = !g_flashOn;
  if (g_flashEnabled && g_flashOn) {
    Vec4 flashDir = cameraLookAt - cameraPosition;
    engine->renderer.core.setSpotLight(
        Color(FLASHLIGHT_R, FLASHLIGHT_G, FLASHLIGHT_B), cameraPosition,
        flashDir, FLASHLIGHT_RANGE, FLASHLIGHT_ANGLE);
  } else {
    engine->renderer.core.disableSpotLight();
  }
  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt));
  {
    engine->renderer.renderer3D.usePipeline(stapip);
    // Split screen (two players): the scene renders twice, top half from
    // P1's camera and bottom half from P2's (players[1].camPos, written by
    // its walker). A cutscene camera override takes the whole screen. HUD /
    // menus / post fx stay full-screen, drawn after splitView.end().
    const bool splitFrame = MULTIPLAYER_MODE == 2 && playerTwoActive &&
                            players[1].objIndex >= 0 &&
                            !scriptCtx.cameraOverride;
    if (splitFrame) {
      auto& core = engine->renderer.core;
      splitPassActive = true;
      core.splitView.begin(0);
      renderScene();
      // Swap the whole camera state to P2: renderScene reads cameraPosition
      // (sky dome centering, LOD, streaming focus), and the renderer needs
      // the second half's view matrix + frustum planes.
      const Vec4 savedPos = cameraPosition, savedLook = cameraLookAt;
      cameraPosition = players[1].camPos;
      cameraLookAt = players[1].camLook;
      core.renderer3D.update(CameraInfo3D(&cameraPosition, &cameraLookAt));
      core.splitView.begin(1);
      splitSecondPass = true;  // reuse this frame's anim poses/skins
      renderScene();
      splitSecondPass = false;
      core.splitView.end();
      cameraPosition = savedPos;
      cameraLookAt = savedLook;
      core.renderer3D.update(CameraInfo3D(&cameraPosition, &cameraLookAt));
      splitPassActive = false;
    } else {
      renderScene();
    }
    // Depth of field composites right after the 3D scene, BEFORE any 2D:
    // sprites stamp z = max across their whole rect (transparent margins
    // included), which would punch sharp rectangles into a later z-tested
    // DoF pass (a crosshair HUD showed through the blur as a box).
    engine->renderer.core.applyPostFx(Tyra::RendererCorePostFx::PassDof);
    // Full-screen effects can sit inside the HUD stack (Tools > UI Editor):
    // bloom (with color grading) and film grain composite at independent
    // points, so sprites drawn afterwards stay crisp on top of them. -1 = the
    // pass applies at endFrame, over everything (menus included).
    for (int i = 0; i < (int)hudSprites.size(); ++i) {
      if (i == HUD_BLOOM_LAYER)
        engine->renderer.core.applyPostFx(
            Tyra::RendererCorePostFx::PassBloom |
            Tyra::RendererCorePostFx::PassGrading);
      if (i == HUD_GRAIN_LAYER)
        engine->renderer.core.applyPostFx(Tyra::RendererCorePostFx::PassGrain);
{{SCREEN_FX_IN_LOOP}}      if (scriptCtx.hudVisible)
        engine->renderer.renderer2D.render(hudSprites[i]);
    }
    // Custom screen effects placed at the top of the stack (layer -1): drawn
    // over the whole HUD stack, under the USE prompt / texts / pause menus.
{{SCREEN_FX_TOP}}    if (useTargetIndex >= 0)
      engine->renderer.renderer2D.render(
          runtimeObjects[useTargetIndex].data.pickable ? pickPromptSprite
                                                       : usePromptSprite);
    updateAndRenderHudTexts();
    updateAndRenderDynTexts();
    // Cutscene Director widescreen bars + fade-to-black: solid quads over the
    // scene and HUD (texts included), under the pause menus (no-op unless a
    // cutscene draws).
    sequences::renderOverlay(engine, scriptCtx);
    renderGameMenu();
    renderSaveMenu();
    drawDebugHud(engine);
    drawVideoConfirm(engine);
  }
  engine->renderer.endFrame();
}
)";

static const char* TPL_GAME_CPP_FPP_TAIL = R"(
void TerrainGame::updatePlayer() {
  const auto& leftJoy = engine->pad.getLeftJoyPad();
  const auto& rightJoy = engine->pad.getRightJoyPad();
  // stickAxis applies the per-stick deadzone (g_deadzoneL/R - Preferences, or a
  // menu "Deadzone" option block) and response curve (g_stickCurve*/g_stickExp*
  // - Preferences > Input / Set Stick Curve node / a menu "Aim curve" block).
  auto axisL = [&](const u8& raw) {
    return stickAxis(raw, g_deadzoneL, g_stickCurveL, g_stickExpL);
  };
  auto axisR = [&](const u8& raw) {
    return stickAxis(raw, g_deadzoneR, g_stickCurveR, g_stickExpR);
  };

  // Right stick: look around (stick right = turn right)
  yaw -= axisR(rightJoy.h) * 0.05F * LOOK_SPEED * g_frameScale;
  pitch -= axisR(rightJoy.v) * 0.035F * LOOK_SPEED * g_frameScale;
  if (pitch > 1.2F) pitch = 1.2F;
  if (pitch < -1.2F) pitch = -1.2F;

  // Left stick: walk. Forward is where the camera looks (flat).
  const float fx = sinf(yaw);
  const float fz = cosf(yaw);
  const float forward = -axisL(leftJoy.v);
  const float strafe = axisL(leftJoy.h);
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
  // a linked floor portal underfoot swallows the walker (see
  // portalSwallowsPlayer) - the terrain stops being the floor there
  if (PORTAL_COUNT > 0 && portalSwallowsPlayer(nextX, playerY, nextZ))
    ground = -1e30F;
  float ceiling = 1e30F;
  // Shove physics bodies with the attempted step first - collidePlayer may
  // cancel the move against the (still solid) body, the push moves it away
  // over the next frames.
  pushPhysicsBodies(playerX, playerZ, nextX, nextZ, playerY, EYE_HEIGHT);
  updatePortalPass(nextX, playerY, nextZ);  // wall doorways open in collision
  collidePlayer(playerX, playerZ, &nextX, &nextZ, playerY, EYE_HEIGHT, &ground,
                &ceiling);
  // whisker reads portalPassOn (carry through a portal) - reset after it
  applyCarryWhisker(&nextX, &nextZ, playerY + EYE_HEIGHT, yaw, playerY,
                    EYE_HEIGHT);
  portalPassOn = false;
  playerX = nextX;
  playerZ = nextZ;

  // Gravity & jumping (X). GRAVITY: units/s^2, JUMP_SPEED: units/s.
  playerVelY -= GRAVITY * g_frameDt * g_frameDt;
  playerY += playerVelY;
  // Jump clamp: keep the eye EYE_CLEARANCE below overhead geometry so the
  // camera never pokes into it (skipped when the gap is too low to stand in)
  const float maxY = ceiling - EYE_HEIGHT - EYE_CLEARANCE;
  if (playerY > maxY && maxY >= ground) {
    playerY = maxY;
    if (playerVelY > 0.0F) playerVelY = 0.0F;
  }
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
    return R"(# simple house: cube walls + pyramid roof (TyraX built-in)
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
    R"(// Generated by TyraX. Delete this line to take ownership of this file.
#pragma once

// Global pad mapping - the single place gameplay buttons are defined.
// Values are Tyra pad button member names (Cross, Circle, Square, Triangle,
// L1, R1, DpadUp, ...), used as engine->pad.getClicked().BTN_USE etc.
#define BTN_USE Square
#define BTN_JUMP Cross
#define BTN_FLY_UP Cross     // noclip: ascend
#define BTN_FLY_DOWN Square  // noclip: descend
#define BTN_THROW Circle     // throw a carried pickable object ("Can throw")

// "Use" interaction (objects marked usable in the editor)
constexpr float USE_DISTANCE = 4.0F;   // max distance to the object surface
constexpr float USE_LOOK_DOT = 0.92F;  // how directly you must look (cos angle)

// Pickable objects (marked pickable in the editor). Defines, not constexpr:
// the game cpp falls back with #ifndef when an owned copy of this file
// predates them.
#define PICK_CARRY_DIST 2.2F    // carry reach in front of the face
#define PICK_THROW_SPEED 14.0F  // launch speed, units/s
#define PICK_MIN_DIST 0.3F      // floor for the carry reach: keeps the
                                // object's near face off the clip plane
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

// 128x32 "PICK UP" prompt, PNG (2026 bytes) - written into res/hud/pickup.png
// of every project when missing; drawn instead of the USE prompt while the
// looked-at object is pickable. Same style as use.png (white, black outline).
const unsigned char* pickPromptPng(size_t& size) {
    static const unsigned char data[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,128,0,0,0,32,8,6,
        0,0,0,218,34,112,37,0,0,7,177,73,68,65,84,120,218,237,154,127,76,84,
        217,21,199,63,204,2,99,116,144,58,165,160,16,212,46,67,208,110,197,18,
        82,39,209,168,153,70,165,9,129,110,164,36,141,162,113,88,84,26,71,77,
        246,15,155,218,214,96,251,7,109,93,99,93,106,90,139,22,109,253,177,
        245,71,157,154,84,196,193,162,66,141,176,34,6,97,75,250,99,172,6,129,
        217,65,104,160,128,48,12,183,127,240,222,115,24,134,55,40,172,29,218,
        247,77,94,114,231,221,115,190,243,222,121,231,222,123,206,185,23,52,
        104,208,160,65,131,6,13,26,52,104,208,160,225,117,17,6,156,0,250,1,17,
        224,26,150,250,58,128,58,224,32,240,5,73,87,15,92,2,6,125,228,189,64,
        53,16,239,247,63,95,5,62,0,26,0,55,48,4,116,74,156,31,0,95,242,147,
        255,16,232,243,123,150,70,96,153,143,204,18,224,99,96,196,79,174,31,
        248,13,16,238,199,169,3,126,13,12,248,200,142,0,247,1,147,143,220,70,
        160,205,143,179,19,40,152,130,173,66,22,153,19,188,140,218,245,47,32,
        27,120,79,69,230,67,137,127,62,112,101,146,188,191,5,140,192,90,21,
        153,107,18,239,219,192,179,32,124,91,252,222,245,27,42,178,231,124,6,
        196,243,9,100,60,83,176,213,180,66,55,141,92,137,0,133,133,133,8,33,
        198,93,94,175,151,129,129,1,218,219,219,169,168,168,96,195,134,13,0,
        209,210,200,95,13,80,84,84,164,200,219,237,118,153,119,33,240,14,80,
        11,188,27,21,21,197,190,125,251,168,174,174,198,237,118,51,52,52,132,
        219,237,166,170,170,138,61,123,246,48,103,206,28,128,60,224,46,240,
        101,128,188,188,60,133,183,169,169,201,151,55,1,168,4,226,45,22,11,3,
        3,3,138,92,71,71,7,38,147,201,119,118,27,247,174,54,155,77,145,175,
        169,169,241,229,5,152,3,24,13,6,195,24,59,72,156,225,83,176,149,37,84,
        103,128,66,64,20,22,22,138,201,194,102,179,201,222,253,28,16,69,69,69,
        74,159,221,110,151,251,254,8,252,3,16,107,214,172,17,46,151,75,149,
        243,241,227,199,98,217,178,101,178,238,67,64,228,229,229,41,253,77,77,
        77,114,223,83,224,19,64,152,205,102,209,219,219,171,200,116,119,119,
        139,212,212,84,89,238,62,16,233,247,174,54,64,216,108,54,69,167,166,
        166,70,150,151,61,193,0,8,131,193,48,230,249,76,38,147,50,170,95,211,
        86,127,3,222,10,197,25,224,149,113,248,240,97,18,19,19,145,166,235,
        137,240,14,240,182,217,108,198,225,112,16,27,27,171,202,185,120,241,
        98,202,203,203,49,26,141,0,203,131,204,88,75,83,83,83,41,47,47,199,96,
        48,0,208,223,223,79,102,102,38,141,141,141,0,127,1,190,46,197,25,132,
        136,173,76,210,51,133,190,3,212,214,214,18,22,22,70,88,88,24,17,17,17,
        196,199,199,179,127,255,126,165,63,50,50,146,188,188,188,160,75,75,
        120,120,56,103,207,158,37,50,114,116,32,118,117,117,177,125,251,118,
        98,99,99,153,61,123,54,233,233,233,92,186,116,73,81,72,72,72,96,215,
        174,93,65,159,47,57,57,153,27,55,110,48,111,222,60,0,134,134,134,216,
        184,113,35,119,239,222,5,120,2,172,151,130,182,207,28,175,104,171,53,
        211,245,191,225,111,202,131,135,135,135,105,111,111,167,184,184,152,
        181,107,215,146,145,145,1,64,82,82,82,80,39,205,201,201,81,228,60,30,
        15,235,214,173,163,161,161,65,17,120,240,224,1,185,185,185,92,190,124,
        25,189,94,207,149,43,87,184,122,245,170,42,233,194,133,11,185,121,243,
        38,113,113,113,0,140,140,140,176,101,203,22,42,42,42,0,92,192,58,41,
        56,124,227,152,132,173,18,102,156,3,76,152,59,134,133,5,79,47,50,51,
        149,182,221,110,151,63,254,63,129,44,192,9,188,15,252,40,39,39,199,95,
        245,49,240,69,255,155,113,113,113,84,86,86,202,83,42,114,64,118,225,
        194,5,164,104,123,3,240,247,144,203,179,95,218,234,197,140,115,128,
        136,136,8,226,226,226,216,182,109,155,28,213,2,208,210,210,18,84,55,
        61,61,93,105,223,185,115,71,110,254,2,144,67,250,98,41,157,251,38,240,
        87,192,46,165,140,105,82,74,168,192,96,48,224,112,56,72,78,78,86,238,
        213,213,213,81,90,90,42,255,124,79,170,19,252,215,48,9,91,181,206,8,7,
        48,155,205,8,33,38,236,239,239,239,231,252,249,243,65,121,98,98,98,
        148,182,203,229,242,29,221,50,188,64,190,116,249,34,205,159,107,209,
        162,69,227,248,87,172,88,65,110,110,46,23,47,94,68,226,248,253,155,
        254,232,175,104,171,242,25,159,5,120,189,94,118,238,220,73,107,107,43,
        82,181,108,98,47,13,127,233,167,58,157,110,218,158,189,167,167,71,105,
        151,148,148,200,153,67,38,176,57,136,170,0,84,63,216,68,24,25,25,153,
        170,173,62,150,106,34,51,203,1,132,16,244,245,245,225,116,58,57,119,
        238,28,102,179,153,51,103,206,0,252,27,248,131,154,238,243,231,207,
        149,246,130,5,11,240,169,224,249,98,31,208,3,60,2,126,8,124,69,141,
        243,244,233,211,172,92,185,18,143,199,163,196,5,71,142,28,145,187,127,
        22,164,244,58,44,7,107,1,156,244,45,223,217,213,215,121,253,117,94,
        211,86,249,161,90,9,84,77,109,116,58,29,6,131,129,164,164,36,54,111,
        222,76,125,125,189,156,106,89,128,79,213,120,164,156,28,128,213,171,
        87,203,77,43,144,2,204,2,118,0,63,1,162,164,234,223,15,24,221,43,248,
        126,32,190,178,178,50,242,243,243,105,110,110,230,232,209,163,202,253,
        173,91,183,202,17,119,140,79,9,58,16,250,0,122,123,123,149,27,209,209,
        209,190,17,250,231,228,61,140,185,115,231,142,155,202,167,104,171,166,
        25,227,0,254,179,159,100,184,39,192,117,224,219,140,110,194,220,15,
        166,120,237,218,53,165,157,149,149,69,90,90,26,64,50,208,194,232,134,
        204,113,128,210,210,82,28,14,7,133,133,133,204,159,63,31,201,65,198,
        192,233,116,82,80,80,160,76,197,7,15,30,164,173,237,229,10,116,252,
        248,113,185,40,244,45,41,203,8,132,79,1,218,219,219,149,27,41,41,41,
        114,96,153,8,116,3,205,242,218,46,195,227,241,208,213,213,165,44,33,
        159,133,173,66,170,20,124,239,222,61,185,124,121,47,136,110,145,74,41,
        120,68,175,215,139,214,214,86,165,175,179,179,83,88,173,86,97,52,26,
        197,172,89,179,196,242,229,203,69,89,89,217,152,210,233,161,67,135,
        148,146,235,4,165,224,102,224,50,32,54,109,218,52,70,183,164,164,68,
        150,105,149,106,240,227,98,73,64,24,141,70,225,241,120,20,189,150,150,
        22,177,126,253,122,17,21,21,37,162,163,163,69,118,118,182,232,232,232,
        80,250,107,107,107,101,94,215,20,108,245,127,231,0,207,0,97,177,88,
        198,24,91,13,46,151,75,196,196,196,4,115,128,38,233,67,246,3,226,214,
        173,91,138,140,215,235,21,171,86,173,146,229,126,53,193,51,55,0,227,
        28,79,13,5,5,5,50,103,101,168,56,128,110,6,56,86,35,208,86,85,85,69,
        86,86,22,221,221,221,170,194,79,159,62,37,35,35,131,206,206,78,38,145,
        207,63,145,106,8,236,222,189,91,9,208,116,58,29,39,78,156,64,175,215,
        195,232,222,125,160,29,184,31,3,236,221,187,151,234,234,234,160,47,
        113,234,212,41,78,158,60,9,163,103,30,170,67,197,184,51,193,1,6,165,
        181,216,117,253,250,117,76,38,19,69,69,69,212,215,215,211,211,211,195,
        139,23,47,112,58,157,56,28,14,172,86,43,75,151,46,229,225,195,135,72,
        241,193,47,39,193,255,83,192,249,232,209,35,142,29,59,166,220,92,178,
        100,9,7,14,28,144,183,130,223,15,160,247,59,224,84,79,79,15,22,139,
        133,29,59,118,112,251,246,109,220,110,55,94,175,151,193,193,65,218,
        218,218,176,219,237,100,103,103,99,181,90,229,180,241,59,210,18,240,
        63,7,181,3,33,23,130,232,78,230,64,200,34,192,49,201,195,19,31,49,249,
        3,33,72,14,166,198,119,85,101,0,21,51,186,91,56,153,3,29,249,211,96,
        171,144,133,14,56,25,224,152,84,67,160,104,220,15,122,41,32,243,63,18,
        246,231,0,71,194,190,38,141,236,79,36,163,122,164,243,4,242,145,48,
        255,252,255,231,1,142,132,53,49,246,72,24,192,247,128,174,0,31,164,65,
        138,192,213,144,34,253,119,29,163,187,135,30,201,14,207,128,63,1,223,
        5,62,63,77,182,210,160,65,131,6,13,26,52,104,208,160,65,131,134,41,
        224,63,218,97,91,227,58,15,90,186,0,0,0,0,73,69,78,68,174,66,96,130,
    };
    size = sizeof(data);
    return data;
}


// 512x16 glyph strip for the in-game HUD text (debug overlays + the video
// mode confirm prompt), rendered from an embedded 8x8 pixel font and
// encoded on first use. 42 glyphs in two rows of 32 cells of 16px - the
// order must match drawHudText's atlas string in the game template; the
// right half of each cell stays transparent so the GS's bilinear filter
// never samples the neighboring glyph.
const std::vector<unsigned char>& debugFontPng() {
    static std::vector<unsigned char> png = [] {
        // Classic CP437-style 8x8 glyphs, one byte per row, MSB = left pixel.
        static const unsigned char rows[42][8] = {
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
            {0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00},  // A
            {0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00},  // C
            {0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00},  // D
            {0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00},  // G
            {0xCC, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0xCC, 0x00},  // H
            {0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00},  // I
            {0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00},  // J
            {0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00},  // K
            {0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00},  // L
            {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00},  // N
            {0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00},  // O
            {0x78, 0xCC, 0xCC, 0xCC, 0xDC, 0x78, 0x1C, 0x00},  // Q
            {0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00},  // R
            {0xFC, 0xB4, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00},  // T
            {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xFC, 0x00},  // U
            {0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x00},  // V
            {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00},  // W
            {0xC6, 0xC6, 0x6C, 0x38, 0x38, 0x6C, 0xC6, 0x00},  // X
            {0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x30, 0x78, 0x00},  // Y
            {0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00},  // Z
            {0x78, 0xCC, 0x0C, 0x18, 0x30, 0x00, 0x30, 0x00},  // ?
            {0x00, 0x00, 0xFC, 0x00, 0x00, 0xFC, 0x00, 0x00},  // =
            {0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00},  // -
            {0x00, 0x30, 0x30, 0x00, 0x00, 0x30, 0x30, 0x00},  // :
        };
        const int w = 512, h = 16;  // PS2 textures need power-of-two sizes
        std::vector<unsigned char> rgba(w * h * 4, 0);
        for (int g = 0; g < 42; ++g)
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    if (!(rows[g][y] & (0x80 >> x))) continue;
                    unsigned char* px =
                        &rgba[(((g / 32) * 8 + y) * w + (g % 32) * 16 + x) * 4];
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

  // Camera-attached flashlight (Scene/Project > Preferences > Flashlight).
  if (FLASHLIGHT_ENABLED) {
    Vec4 flashDir = cameraLookAt - cameraPosition;
    engine->renderer.core.setSpotLight(
        Color(FLASHLIGHT_R, FLASHLIGHT_G, FLASHLIGHT_B), cameraPosition,
        flashDir, FLASHLIGHT_RANGE, FLASHLIGHT_ANGLE);
  } else {
    engine->renderer.core.disableSpotLight();
  }
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
    R"(// Generated by TyraX. Delete this line to take ownership of this file.
#pragma once

#include <tyra>
#include <vector>
#include "scene_data.hpp"

namespace {{NAME_UPPER_NS}} {

/** Frames of near-rest ground contact after which a physics body falls
 * asleep (a sleeping body costs one branch per frame until woken). */
constexpr int PHYS_SLEEP_FRAMES = 24;

/** A scene object at runtime. Mutate `data` (position/rotation/scale/color),
 * `visible` or the velocity fields, then set `dirty = true` so the geometry
 * gets rebuilt on the next frame. */
struct RuntimeObject {
  SceneObjectData data;
  bool visible = true;
  // Object physics state (data.physics). Velocities are per-frame
  // displacements (like the player's), spin is degrees/frame. After writing
  // them from a script set restFrames = 0 too - a body with restFrames >=
  // PHYS_SLEEP_FRAMES is asleep and skips simulation entirely.
  float velocityY = 0.0F;  // vertical velocity (kept first: legacy scripts)
  float velocityX = 0.0F, velocityZ = 0.0F;
  float spin[3] = {0.0F, 0.0F, 0.0F};  // angular velocity, degrees/frame
  // Settle-flatten targets, latched once per settle so the chosen face
  // never flips mid-ease. 1e9 = unlatched; [1] additionally means "yaw
  // stays" when the roll lands on an even 90deg step.
  float flatTgt[3] = {1e9F, 1e9F, 1e9F};
  signed char restFrames = 0;          // sleep counter; write 0 to wake
  bool dirty = true;
  // False while the object's streaming layer is not resident: the object is
  // fully out of the game (no render, collision, sound, USE, physics) and
  // its geometry/assets may be freed. Managed by the game's layer streaming
  // - scripts should use the Load/Unload Layer flow nodes (or
  // ctx.layerRequest) instead of writing this directly.
  bool active = true;

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
  Tyra::Vec4 playerLook;           // normalized view direction this frame
  // Two-player modes (docs/multiplayer.md): player 2's eye position while
  // active; equals playerPosition otherwise, so "nearest player" logic can
  // read it unconditionally.
  Tyra::Vec4 player2Position;
  bool player2Active = false;
  RuntimeObject* objects = nullptr;  // mutable scene objects
  int objectCount = 0;
  Tyra::Color skyColor;  // write to change the clear color

  // Cutscene camera override (a Cutscene Director sequence with a camera
  // track, driven by the Play/Stop Sequence flow nodes). The generated
  // sequence player writes cameraOverride = true + cameraEye/cameraAt every
  // frame such a cutscene is active; the game applies them to the frame camera
  // just before rendering, and the player writes false when the cutscene ends.
  bool cameraOverride = false;
  Tyra::Vec4 cameraEye;
  Tyra::Vec4 cameraAt;

  // Cutscene presentation, also written by the sequence player every frame a
  // cutscene is active (and zeroed when it ends): widescreen mask style
  // (0 none, 1 cinema 2.39:1, 2 wide 16:9, 3 pillarbox, 4 frame) with its
  // slide-in coverage envelope, and a fade-to-black overlay alpha. The game
  // composites them as solid 2D quads over the scene and the HUD, under the
  // pause menus (sequences::renderOverlay in sequences.gen.cpp).
  int barsStyle = 0;
  float barsAmount = 0.0F;  // 0..1 of the style's full coverage
  float fadeAlpha = 0.0F;   // 0..1 black overlay

  // Set by the sequence player while a "Hide player" cutscene is active: the
  // game hides the third-person avatar for the frame (no effect in FPP/noclip,
  // which have no visible body). Cleared when the cutscene ends.
  bool hidePlayer = false;

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

  // On-screen texts (HUD_TEXTS order, hud_data.gen.hpp). Write 1 into
  // textRequest[i] to show a text, 0 to hide it (-1 = leave). When showing,
  // textDuration[i] > 0 auto-hides after that many seconds, 0 = the text
  // stays until hidden. The game applies and resets requests every frame.
  signed char* textRequest = nullptr;
  float* textDuration = nullptr;
  int textCount = 0;

  // Runtime texts (DYN_TEXTS order, font_data.gen.hpp - one slot per Display
  // Text node). Same request protocol as textRequest above, but the string is
  // not baked: write it into dynTextBuf + i * DYN_TEXT_LEN. dynTextOn[i] tells
  // a script whether its slot is currently on screen, so it only pays for the
  // refresh while the text is actually visible.
  signed char* dynTextRequest = nullptr;
  float* dynTextDuration = nullptr;
  char* dynTextBuf = nullptr;
  const unsigned char* dynTextOn = nullptr;
  int dynTextCount = 0;
  int dynTextLen = 0;  // stride of dynTextBuf (DYN_TEXT_LEN)

  // Camera flashlight master switch (the Player object's "Enabled"). Write 1
  // to turn it on, 0 to turn it off, -1 to leave it unchanged; the game
  // applies and resets it. The optional toggle button still gates the beam.
  int flashlight = -1;

  // Runtime graphics switches (Set Fog / Set Bloom / Set Grain / Set Particles
  // flow nodes). fog / particles: -1 = leave, 0 = off, 1 = on. bloom / grain:
  // -1 = leave, else a 0..128 fixed-point amount. The game applies and resets.
  int fog = -1;
  int bloom = -1;
  int grain = -1;
  int particles = -1;

  // Depth of field (Set Depth Of Field flow node). dof: -1 = leave, -2 =
  // restore the scene's authored setting (Tools > UI Editor), else a 0..128
  // blur amount (0 = off). The image blurs progressively from dofFocus to
  // dofFocus + dofRange (world units from the camera). The game applies and
  // resets dof.
  int dof = -1;
  float dofFocus = 0.0F;
  float dofRange = 0.0F;

  // Analog stick response curves (Set Stick Curve flow node). Per stick:
  // curve = -1 leave, else 0 Linear / 1 Exponential / 2 S-Curve; exp = the
  // curve exponent, applied only when >= 1 (< 0 = leave). The game copies
  // both into the runtime g_stickCurve*/g_stickExp* globals and resets them.
  int stickCurveL = -1;
  int stickCurveR = -1;
  float stickExpL = -1.0F;
  float stickExpR = -1.0F;

  // Pad vibration (Vibrate Pad flow node / padVibrate() below). rumble: -1 =
  // leave, else the DualShock big-motor power 0..255 (0 = off); rumbleSmall:
  // the on/off buzz motor. rumbleSec > 0 auto-stops the vibration after that
  // many seconds, 0 = vibrate until the next request. The game applies the
  // request to the pad actuators and resets rumble to -1.
  int rumble = -1;
  int rumbleSmall = 0;
  float rumbleSec = 0.0F;

  // Runtime video output (Set Display Mode / Set Widescreen flow nodes).
  // requestDisplayMode: -1 = leave, else a Tyra::DisplayMode value (0 =
  // interlaced, 1 = progressive 480p, 2 = 1080i, 3 = interlaced field
  // rendering, 4 = full-height PAL 576i). displayConfirmSec > 0
  // arms the keep-or-revert prompt: the game switches, asks the player to
  // confirm with X and reverts to the previous mode automatically when the
  // timer runs out (a mode the TV can't display would otherwise strand the
  // player on a black screen). widescreen: -1 = leave, 0/1 = 4:3 / 16:9.
  // The game applies and resets all three.
  int requestDisplayMode = -1;
  float displayConfirmSec = 0.0F;
  int widescreen = -1;

  // Master sound-effect volume as a percentage (0..100), driven by a menu
  // "Sound volume" option block (applyMenuBindings). 100 = unscaled. Applied
  // as a multiplier on every Play Sound one-shot and every sound-emitter
  // sample, so it rides on top of each source's own volume.
  int sfxVolume = 100;

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

  // Streaming layers (SCENE_LAYER_* tables, per active scene). layerState[i]:
  // 0 = unloaded, 1 = loading (assets streaming in over frames), 2 = loaded.
  // Write 1 into layerRequest[i] to start loading a layer, 0 to unload it
  // (the game applies requests after this frame's scripts; -1 = none).
  // Loading is incremental - GTA3 style: request the next area's layer a
  // corridor early and it pops in without a hitch.
  const unsigned char* layerState = nullptr;
  signed char* layerRequest = nullptr;
  int layerCount = 0;

  // Animated models: clip-name -> clip-index lookup for an object (-1 =
  // unknown clip / not an animated model). Set by the game at startup.
  int (*resolveClip)(int objectIndex, const char* clipName) = nullptr;

  // Dynamic spawning (Spawn Object / Despawn Object flow nodes). spawnObject
  // clones the authored object at templateIndex (the template itself is
  // untouched) into a free runtime slot past the authored objects and
  // returns its index into `objects` (-1 = pool full / bad template). The
  // clone starts at (x, y, z) facing yaw degrees and carries the template's
  // layer, so unloading that layer despawns it. despawnObject frees a
  // spawned slot immediately; on an authored index it only deactivates (the
  // layer streaming can re-activate authored objects). Set by the game.
  int (*spawnObject)(int templateIndex, float x, float y, float z,
                     float yaw) = nullptr;
  void (*despawnObject)(int objectIndex) = nullptr;
};

/** Inputs and outputs of a custom flow-graph node (see flow_nodes.hpp).
 * A `call = fn` custom node runs when an exec link reaches it; the game fills
 * the input fields, calls fn(ctx, io), then latches whatever fn wrote into the
 * output fields so downstream nodes (custom or built-in) can read them. Only
 * the pins the node declared in its .flownode are meaningful; the rest keep
 * their defaults. Object fields are indices into ctx.objects (-1 = none). */
struct FlowNodeIO {
  int self = -1;                 // object that owns the graph
  // --- inputs (resolved fresh for this call) ---
  int object = -1;               // the "target" object input (or self); -1 invalid
  Tyra::Vec4 position;           // wired position input (0,0,0 if none)
  bool boolIn = false;           // OR of wired bool inputs
  const char* text = "";         // first wired text input ("" if none)
  const float* num = nullptr;    // the node's num0..3 params
  const char* str = "";          // the string param (when string = text)
  // --- outputs (write the ones your node declares) ---
  int objectOut = -1;            // an object index, e.g. a raycast/pick result
  Tyra::Vec4 positionOut;
  bool boolOut = false;
  char* textOut = nullptr;       // write up to textOutCap bytes (NUL-terminated)
  int textOutCap = 0;
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

/** Vibrates the DualShock pad: big = heavy-motor strength 0..1, small = the
 * on/off buzz motor. seconds > 0 auto-stops after that long, 0 = vibrate
 * until the next call. padVibrate(ctx, 0.0F) stops immediately. */
inline void padVibrate(ScriptContext& ctx, float big, bool small = false,
                       float seconds = 0.0F) {
  const int power = (int)(big * 255.0F + 0.5F);
  ctx.rumble = power < 0 ? 0 : power > 255 ? 255 : power;
  ctx.rumbleSmall = small ? 1 : 0;
  ctx.rumbleSec = seconds < 0.0F ? 0.0F : seconds;
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
   * rotation/scale/color), self->visible or the velocity/spin fields (write
   * self->restFrames = 0 to wake a sleeping body), then set
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

// Custom-flow-node C++ bodies (inc/scripts/flow_nodes.hpp). Marker-owned:
// regenerated while the marker line is present, so DELETE the first line to
// keep your own functions. Every `call = fn` custom node (flow-nodes/*.flownode)
// resolves to a function here with the signature below. Ships one working
// example (flowExampleNearest) used by the scaffolded example.flownode.
static const char* TPL_FLOW_NODES_HPP =
    R"(// Generated by TyraX. Delete this line to take ownership of this file.
#pragma once

// C++ bodies for custom flow-graph nodes. A .flownode file with `call = fn`
// runs the function `fn` here when an exec link reaches the node. Signature:
//
//     void fn(ScriptContext& ctx, FlowNodeIO& io);
//
// `io` (see FlowNodeIO in script.hpp) carries the node's inputs - io.object
// (the target object index), io.position/io.boolIn/io.text (wired inputs),
// io.num (num0..3 params), io.str (string param) - and its outputs: write
// io.objectOut / io.positionOut / io.boolOut / io.textOut and the game latches
// them for downstream nodes (custom OR built-in - e.g. an object output can
// feed a built-in Hide Object). Declare which pins exist in the .flownode
// (`in = ...`, `out = ...`). Add `#include`s and helper functions freely; this
// header is compiled into the generated flow graph. Keep functions cheap - they
// run every time their node fires.
#include "scripts/script.hpp"

namespace {{NAME_UPPER_NS}} {

// Example: output the scene object nearest the player (a stand-in for a
// look-at raycast). Used by the scaffolded example.flownode; wire its object
// output into a built-in Hide/Move Object, and its exec output onward.
inline void flowExampleNearest(ScriptContext& ctx, FlowNodeIO& io) {
  int best = -1;
  float bestDist = 0.0F;
  for (int i = 0; i < ctx.objectCount; ++i) {
    if (i == io.self || !ctx.objects[i].active) continue;
    const float* p = ctx.objects[i].data.position;
    const float dx = p[0] - ctx.playerPosition.x;
    const float dy = p[1] - ctx.playerPosition.y;
    const float dz = p[2] - ctx.playerPosition.z;
    const float d = dx * dx + dy * dy + dz * dz;
    if (best < 0 || d < bestDist) {
      best = i;
      bestDist = d;
    }
  }
  io.objectOut = best;  // -1 = nothing; downstream object refs handle that
}

}  // namespace {{NAME_UPPER_NS}}
)";

// Example script for FPP projects - user-owned, written only at creation.
// A minimal "hello world": it logs one line at startup (find it in the PCSX2
// log). The commented block in update() is a ready-to-uncomment example that
// tints the sky on X - left off by default so pressing X (also the jump
// button) does not recolor the sky by surprise.
static const char* TPL_EXAMPLE_SCRIPT_FPP =
    R"(// Example TyraX script. This file is yours - it is never regenerated.
// Says hello in the PCSX2 log at startup. Uncomment the block in update()
// for a working example that reacts to the pad.
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace {{NAME_UPPER_NS}} {

class ExampleInteraction : public Script {
 public:
  void init(ScriptContext& ctx) override {
    (void)ctx;
    TYRA_LOG("Hello from TyraX! Edit src/scripts/example_interaction.cpp.");
  }

  void update(ScriptContext& ctx) override {
    (void)ctx;
    // Example: walk up to the first box and press X to toggle the sky color.
    // Commented out so a jump (X) does not recolor the sky - uncomment to try.
    //
    // static bool toggled = false;
    // RuntimeObject* box = nullptr;
    // for (int i = 0; i < ctx.objectCount; ++i)
    //   if (ctx.objects[i].data.type == 0) { box = &ctx.objects[i]; break; }
    // if (!box) return;
    // const float dx = ctx.playerPosition.x - box->data.position[0];
    // const float dz = ctx.playerPosition.z - box->data.position[2];
    // if ((dx * dx + dz * dz) < 8.0F * 8.0F && ctx.engine->pad.getClicked().Cross) {
    //   toggled = !toggled;
    //   TYRA_LOG("Box says hello! Sky toggled: ", (int)toggled);
    //   ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
    //                          : Tyra::Color(SKY_R, SKY_G, SKY_B);
    // }
  }
};

}  // namespace {{NAME_UPPER_NS}}

TYRA_SCRIPT({{NAME_UPPER_NS}}::ExampleInteraction);
)";

// Example script for orbit projects (no player to walk around with).
static const char* TPL_EXAMPLE_SCRIPT_ORBIT =
    R"(// Example TyraX script. This file is yours - it is never regenerated.
// Says hello in the PCSX2 log at startup. Uncomment the block in update()
// for a working example that reacts to the pad.
#include "scripts/script.hpp"
#include "terrain_config.hpp"

namespace {{NAME_UPPER_NS}} {

class ExampleInteraction : public Script {
 public:
  void init(ScriptContext& ctx) override {
    (void)ctx;
    TYRA_LOG("Hello from TyraX! Edit src/scripts/example_interaction.cpp.");
  }

  void update(ScriptContext& ctx) override {
    (void)ctx;
    // Example: press X to toggle the sky color. Commented out so it does not
    // fire by surprise - uncomment to try it.
    //
    // static bool toggled = false;
    // if (ctx.engine->pad.getClicked().Cross) {
    //   toggled = !toggled;
    //   TYRA_LOG("X pressed! Sky toggled: ", (int)toggled);
    //   ctx.skyColor = toggled ? Tyra::Color(230.0F, 120.0F, 60.0F)
    //                          : Tyra::Color(SKY_R, SKY_G, SKY_B);
    // }
  }
};

}  // namespace {{NAME_UPPER_NS}}

TYRA_SCRIPT({{NAME_UPPER_NS}}::ExampleInteraction);
)";

// Stub for "New script..." in the editor: an attachable object script.
static const char* TPL_SCRIPT_STUB =
    R"(// {{SCRIPT_FILE}} - created by TyraX. This file is yours.
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
    //   ctx.playerLook                      - normalized view direction
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

// docker-compose.yml is regenerated on every build (refreshGenerated) and
// carries a machine-specific absolute path to the engine sources plus a hash
// derived from it - never worth committing (it just churns and leaks the
// author's local path).
static const char* TPL_GITIGNORE = R"(obj/
bin/*.elf
*.history
.vscode/
.res-baked/
docker-compose.yml
)";

static const char* TPL_DIR_KEEP = "*\n!.gitignore\n";

// res/ holds AUTHORED assets - they must reach the repo (the split-object
// format exists precisely so a team can share a map through git; a teammate
// without your textures sees "material file missing" on every object). Only
// build-regenerated bakes are ignored: menus/ is fully rebuilt from the
// .tyra at build, and .tskl/.tanm are baked from their .glb source. hud/ is
// NOT ignored - user-imported HUD images land there next to the baked text
// sprites, and losing imports is worse than committing regenerable bakes.
static const char* TPL_RES_GITIGNORE =
    R"(# Authored assets (models, textures, materials, ui, audio, sfx) are
# checked in - a pulled project must render without missing files. Only
# build-regenerated output is ignored.
/menus/

# Baked animated-model output (.glb is the source; .tskl/.tanm are
# regenerated from it on every build).
/models/*.tskl
/models/*.tanm
)";

// Multi-user collaboration hints, written once at project creation (a new game
// map is its own repo, separate from the editor). Object bodies are split one
// file per object so they merge cleanly; the files below cannot be auto-merged
// and are marked lockable so a team can claim them before editing (needs
// `git lfs install`, uses only the lock registry - no LFS storage/server).
static const char* TPL_GITATTRIBUTES = R"(# Multi-user collaboration for this Tyra project.
#
# Object bodies are split one file per object (objects/<id>.json), so two people
# editing DIFFERENT objects touch different files and git merges them with no
# conflict - just edit. The manifest (<name>.tyra) and per-object files are text.
#
# The files below cannot be sanely auto-merged (a heightmap grid, binary assets).
# Lock one before you edit it so nobody else edits the same file at the same
# time. Locking needs Git LFS installed once per clone (`git lfs install`); it
# uses only LFS's lock registry, not LFS storage, so no special remote is needed:
#     git lfs lock   terrain-main.heights     # claim it
#     git lfs unlock terrain-main.heights     # release it
# `git lfs locks` lists who holds what. A lockable file is read-only in your
# working tree until you lock it - that read-only bit is the reminder.

terrain-*.heights lockable -merge
res/models/**     lockable -merge
res/materials/**  lockable -merge
res/textures/**   lockable -merge
res/fonts/**      lockable -merge
res/audio/**      lockable -merge
res/sfx/**        lockable -merge
res/hud/**        lockable -merge
)";

static const char* TPL_COLLABORATION = R"(# Working on this project with others

This project is laid out to keep git merges painless when several people edit it
at once.

## What merges cleanly (just edit)

- **Scene objects** live one per file under `objects/<id>.json`. Each object has
  a stable id, so editing, moving or recoloring different objects never
  conflicts - you each touch a different file.
- **The manifest** (`<name>.tyra`) holds project-wide settings and, per scene, an
  ordered list of object ids. Editing settings or different scenes merges
  line-by-line. The one place two people can still collide is *both adding an
  object to the same scene at the same time* - a one-line conflict in the id
  list, trivial to resolve (keep both ids).

## What to lock first (cannot auto-merge)

Some files are a single indivisible blob a git auto-merge would corrupt.
`.gitattributes` marks them **lockable**; lock one before editing so no one else
edits it concurrently:

- `terrain-*.heights` - a scene's terrain heightmap (one grid; two sculpts can't
  merge).
- everything under `res/` - imported textures, models, audio, fonts.

Locking uses Git LFS's lock registry (no LFS storage / no special server):

    git lfs install                       # once per clone
    git lfs lock   terrain-main.heights   # claim before editing
    git lfs unlock terrain-main.heights   # release when done
    git lfs locks                         # see who holds what

Until you lock a lockable file it is read-only in your working copy - the
reminder to lock it. If your team does not use locking, ignore this: the files
still work as plain git files.

## Not tracked / regenerated

`obj/`, `bin/*.elf`, `.res-baked/`, `docker-compose.yml`, `*.history` and the
`*.gen.*` sources are build output or local state (see `.gitignore`) - never
resolve merge conflicts in generated files; fix the source and rebuild.
)";

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

// inc/decal_data.gen.hpp - baked projected-decal meshes. For every decal with
// "Project onto surfaces", decalproj clips the receiver geometry (terrain +
// overlapping objects) against the decal's projector volume HERE, on the host,
// and emits the resulting world-space triangle list (pos3 + uv2 per vertex).
// The generated game just uploads and draws it (rebuildObjectGeometry case 13),
// so no projection or clipping ever runs on the PS2 EE. One table per scene,
// indexed by object index; the flat-quad decal is used where there is no mesh.
static std::string decalDataHeader(const Project& p) {
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
           "#pragma once\n\n"
           "// Baked projected decals (\"Project onto surfaces\"): world-space\n"
           "// triangle lists (5 floats/vertex: pos3 + uv2) computed on the host,\n"
           "// per scene, indexed by object index. Empty entry = not a projecting\n"
           "// decal (the flat quad is used instead).\n\n"
           "namespace {\n"
           "struct BakedDecal { const float* verts; int vertCount; };\n\n";
    const int sceneCount = (int)p.scenes.size();
    std::vector<bool> hasTable(sceneCount, false);
    for (int si = 0; si < sceneCount; ++si) {
        const auto& objs = p.scenes[si].objects;
        std::vector<std::string> entry(objs.size(), "{nullptr, 0}");
        bool any = false;
        for (size_t oi = 0; oi < objs.size(); ++oi) {
            const SceneObject& o = objs[oi];
            if (o.type != PrimitiveType::Decal || !o.decalProject || o.materialPath.empty())
                continue;
            decalproj::DecalMesh m = decalproj::project(p, p.scenes[si], o);
            if (m.verts.empty()) continue;
            out << "static const float S" << si << "_D" << oi << "[] = {";
            for (size_t k = 0; k < m.verts.size(); ++k)
                out << (k ? "," : "") << floatLit(m.verts[k]);
            out << "};\n";
            entry[oi] = "{S" + std::to_string(si) + "_D" + std::to_string(oi) + ", " +
                        std::to_string((int)(m.verts.size() / 5)) + "}";
            any = true;
        }
        if (!any) continue;
        hasTable[si] = true;
        out << "static const BakedDecal S" << si << "_DECALS[" << objs.size() << "] = {";
        for (size_t oi = 0; oi < objs.size(); ++oi)
            out << (oi ? ", " : "") << entry[oi];
        out << "};\n\n";
    }
    out << "static const BakedDecal* const SCENE_DECAL_TABLES[] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << (hasTable[si] ? ("S" + std::to_string(si) + "_DECALS") : "nullptr");
    out << "};\n"
           "static const int SCENE_DECAL_COUNTS[] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "")
            << (hasTable[si] ? (int)p.scenes[si].objects.size() : 0);
    out << "};\n}\n";
    return out.str();
}

// Static batching eligibility (SceneObjectData::batchStatic). The game may
// merge flagged objects into combined world-space bags at scene load; the
// flag must therefore rule out everything that renders through a special
// path or moves/hides at runtime through a channel the game can predict at
// build time. Runtime-only mutation channels that can hit ANY object (Live
// Link, a Raycast latch or custom-node object output fed into an action,
// global scripts writing ctx.objects) are NOT excludable here - the game
// covers them by rebuilding a batch whenever a member is dirtied.

// Object names referenced anywhere that can move, hide, re-target or
// re-submit an object at runtime: same-scene flow-graph nodes with an
// object-name param (writers and readers alike - over-excluding is safe,
// the cost is one solo bag), mirror target lists, portal view lists, and
// cutscene tracks /
// camera-shot bindings (project-wide, they apply to whatever scene is
// active). Patrol waypoint PREFIXES are deliberately not expanded: the AI
// only reads waypoint positions, it never mutates the waypoint object.
static std::set<std::string> batchBlockedNames(const Project& p,
                                               const SceneData& sc) {
    std::set<std::string> refs;
    for (const SceneObject& o : sc.objects) {
        for (const FlowNode& n : o.flowGraph.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (t && t->strKind == FlowParamKind::ObjectName && !n.str.empty())
                refs.insert(n.str);
        }
        if (o.type == PrimitiveType::Mirror)
            for (const std::string& m : o.mirrorObjects) refs.insert(m);
        // Portal view lists re-submit their objects through renderPortalView's
        // per-object solo bags - a batched member has no solo bag and simply
        // vanishes from the through-view (only particles survived; the missing
        // wall around the target portal then read as seeing through two
        // portals at once).
        if (o.type == PrimitiveType::Portal)
            for (const std::string& m : o.portalObjects) refs.insert(m);
    }
    for (const Sequence& s : p.sequences) {
        for (const SeqTrack& tr : s.tracks) refs.insert(tr.target);
        for (const SeqCameraKey& k : s.cameraKeys) refs.insert(k.camera);
    }
    return refs;
}

static bool staticBatchEligible(const SceneObject& o,
                                const std::set<std::string>& blocked) {
    // Geometry primitives only - models render per-MTL-part with their own
    // textures, decals/mirrors have dedicated draw paths, markers have no
    // geometry.
    const bool shape =
        o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
        o.type == PrimitiveType::Plane;
    if (!shape) return false;
    if (o.physics) return false;      // moves every frame while falling
    if (o.usable) return false;       // highlight defers/re-submits the body
    if (o.pickable) return false;     // carried/thrown - moves at runtime
    if (o.saveState) return false;    // a loaded save repositions it
    if (o.reflected) return false;    // re-submitted into the env map pass
    if (o.drawDistance != 0.0f) return false;  // per-object distance cut-off
    if (!o.layer.empty()) return false;        // streamed in/out with a layer
    // Per-object logic: the graph can move self, attached scripts get a
    // per-frame hook on this object.
    if (!o.flowGraph.nodes.empty() || !o.scripts.empty()) return false;
    return blocked.find(o.name) == blocked.end();
}

// inc/scene_data.hpp - pure data mirror of the .tyra project, regenerated on build
static std::string sceneDataContent(const Project& p, const std::string& ns) {
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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
           "             // 12=plane 13=decal 14=camera (cutscene shot marker)\n"
           "             // 15=mirror (glass quad; reflections via MIRRORS below)\n"
           "             // 16=portal (linked surface; through-view + teleport\n"
           "             //    via PORTALS below)\n"
           "  float position[3];\n"
           "  float rotation[3];  // degrees\n"
           "  float scale[3];\n"
           "  float color[3];  // 0..1\n"
           "  int physics;  // 1 = rigid body: gravity, bounces, tumbles, collides\n"
           "  float physMass;     // relative mass (impulse exchange, player push)\n"
           "  float physBounce;   // restitution 0..1: 0 = thud, 1 = superball\n"
           "  float physFriction; // ground drag 0..1: 0 = ice, 1 = sticky\n"
           "  int physTumble;     // 1 = ground contact converts slide into roll\n"
           "  int model;    // index into MODEL_PATHS / gameModels, -1 = none\n"
           "  int material; // primitives: index into MATERIAL_PATHS, -1 = plain color\n"
           "  int usable;   // 1 = shows the USE prompt up close (see controls.hpp)\n"
           "  int pickable; // 1 = BTN_USE picks it up and carries it in front of\n"
           "                // the camera; BTN_USE again drops it\n"
           "  int pickThrow; // 1 = a carried object launches with BTN_THROW\n"
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
           "  float drawDistance;  // not drawn farther than this from the camera;\n"
           "                       // 0 = unlimited (collision/logic always run)\n"
           "  int reflected;  // 1 = rendered into the dynamic (\"@sky\") env map\n"
           "  int animModel;  // animated models: index into ANIM_MODEL_PATHS, -1 = none\n"
           "  const char* animClip;  // animated models: starting clip (\"\" = first)\n"
           "  int animAutoplay;      // animated models: 1 = play at scene start\n"
           "  int animLoop;          // animated models: 1 = starting clip loops\n"
           "  float animSpeed;       // animated models: playback speed multiplier\n"
           "  float animLod;  // per-object animation-LOD distance override:\n"
           "                  // -1 = project ANIM_LOD_DISTANCE, 0 = off, >0 = custom\n"
           "  float meshLod;  // per-object mesh-LOD distance override (same coding)\n"
           "  float modelYaw; // content-forward correction (deg around model Y),\n"
           "                  // between scale and rotation - X-forward-authored models\n"
           "                  // set +-90; runtime facing (faceYaw/AI) stays pure\n"
           "  int primDetail;        // segments (curved) or box subdivisions/edge\n"
           "  int layer;      // streaming layer (SCENE_LAYER_* tables), -1 = none:\n"
           "                  // always resident, never streamed out\n"
           "  int batchStatic; // 1 = may merge into a combined static batch bag\n"
           "                   // (build-time verdict: non-moving primitive with\n"
           "                   // no physics/logic/graph refs/save-state/layer)\n"
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
            // Placeholder row so the array is never zero-sized. Field order
            // must track SceneObjectData 1:1 (physics params physMass/Bounce/
            // Friction/Tumble after the `physics` flag, pickable/pickThrow
            // after `usable`, `reflected` before the anim block) - an empty
            // scene must still compile.
            out << "    {0, {0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}, 0, "
                   "1.0F, 0.35F, 0.5F, 1, -1, -1, 0, "
                   "0, 0, "
                   "0, 0, 0.0F, 1, 0, 3.0F, 20.0F, 9.8F, 1.0F, 1.5F, 1.0F, 0.6F, 0, "
                   "-1, 0, 15.0F, 0.0F, 0, 1.0F, 8.0F, 0, 0, 0.0F, 0, -1, \"\", 1, 1, "
                   "1.0F, -1.0F, -1.0F, 0.0F, 1, -1, 0},\n";
        } else {
            auto soundIndexOf = [&](const std::string& path) {
                for (size_t i = 0; i < p.sounds.size(); ++i)
                    if (p.sounds[i] == path) return (int)i;
                return -1;
            };
            auto layerIndexIn = [&](const std::string& name) {
                if (name.empty()) return -1;
                const auto& layers = p.scenes[si].layers;
                for (size_t i = 0; i < layers.size(); ++i)
                    if (layers[i].name == name) return (int)i;
                return -1;  // unknown layer name = always resident
            };
            const std::set<std::string> blocked =
                batchBlockedNames(p, p.scenes[si]);
            for (const SceneObject& o : objs) {
                out << "    {" << (int)o.type << ", " << vec3Init(o.position) << ", "
                    << vec3Init(o.rotation) << ", " << vec3Init(o.scale) << ", "
                    << vec3Init(o.color) << ", " << (o.physics ? 1 : 0) << ", "
                    << floatLit(o.physMass) << ", " << floatLit(o.physBounce)
                    << ", " << floatLit(o.physFriction) << ", "
                    << (o.physTumble ? 1 : 0) << ", "
                    << modelIndexOf(p, o) << ", " << materialIndexOf(p, o)
                    << ", "
                    // save points are always usable - USE is how they open
                    << ((o.usable || o.type == PrimitiveType::SavePoint) ? 1 : 0)
                    << ", " << (o.pickable ? 1 : 0) << ", "
                    << (o.pickThrow ? 1 : 0)
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
                    << ", " << o.collisionMode << ", "
                    << floatLit(o.drawDistance) << ", " << (o.reflected ? 1 : 0)
                    << ", " << animModelIndexOf(p, o)
                    << ", \"" << escapeCString(o.animClip) << "\", "
                    << (o.animAutoplay ? 1 : 0) << ", " << (o.animLoop ? 1 : 0)
                    << ", " << floatLit(o.animSpeed) << ", "
                    << floatLit(o.animLodOverride) << ", "
                    << floatLit(o.meshLodOverride) << ", "
                    << floatLit(o.modelYawOffset) << ", "
                    << clampPrimDetail(o.type, o.primDetail) << ", "
                    << layerIndexIn(o.layer) << ", "
                    << (staticBatchEligible(o, blocked) ? 1 : 0) << "},  // "
                    << o.name << "\n";
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

    // Stable per-object identity for Live Link (docs/live-link.md): FNV-1a 64
    // of the editor object id, in authored order. The live_link.gen.cpp poller
    // maps snapshot records onto runtime objects through these, so renames /
    // reorders in the editor keep addressing the right object. Unused (and
    // dropped by the compiler) outside debug+Live-Link builds.
    {
        char hb[19];
        for (int si = 0; si < sceneCount; ++si) {
            const auto& objs = p.scenes[si].objects;
            out << "constexpr unsigned long long SCENE_" << si
                << "_OBJECT_ID_HASHES[" << (objs.empty() ? (size_t)1 : objs.size())
                << "] = {";
            if (objs.empty()) {
                out << "0";
            } else {
                for (size_t i = 0; i < objs.size(); ++i) {
                    std::snprintf(hb, sizeof(hb), "0x%016llx",
                                  (unsigned long long)project::liveLinkIdHash(objs[i]));
                    out << (i ? ", " : "") << hb << "ULL";
                }
            }
            out << "};\n";
        }
        out << "inline const unsigned long long* SCENE_OBJECT_ID_TABLES"
               "[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si)
            out << (si ? ", " : "") << "SCENE_" << si << "_OBJECT_ID_HASHES";
        out << "};\n\n";
    }

    // Streaming layers: per-scene layer count and which layers start resident
    // (rows padded to SCENE_MAX_LAYERS with true). SceneObjectData.layer
    // indexes these; the Load/Unload Layer flow nodes flip residency at
    // runtime and the game streams the assets in/out.
    int maxLayers = 1;
    for (const SceneData& sc : p.scenes)
        if ((int)sc.layers.size() > maxLayers) maxLayers = (int)sc.layers.size();
    out << "constexpr int SCENE_LAYER_COUNTS[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "") << p.scenes[si].layers.size();
    out << "};\n"
        << "constexpr int SCENE_MAX_LAYERS = " << maxLayers << ";\n"
        << "constexpr bool SCENE_LAYER_STARTS[SCENE_COUNT][SCENE_MAX_LAYERS] = {";
    for (int si = 0; si < sceneCount; ++si) {
        out << (si ? ", {" : "{");
        for (int li = 0; li < maxLayers; ++li) {
            const auto& layers = p.scenes[si].layers;
            const bool start = li >= (int)layers.size() || layers[li].startLoaded;
            out << (li ? ", " : "") << (start ? "true" : "false");
        }
        out << "}";
    }
    out << "};\n";

    // Auto-streamed layers: zone center + radius per layer; radius 0 = the
    // layer is script-driven only. The game loads an auto layer while the
    // player is inside the zone and unloads it past radius + hysteresis
    // (edge-triggered - Load/Unload Layer nodes can still override until
    // the next boundary crossing).
    auto layerFloatTable = [&](const char* name, auto get) {
        out << "constexpr float " << name << "[SCENE_COUNT][SCENE_MAX_LAYERS] = {";
        for (int si = 0; si < sceneCount; ++si) {
            out << (si ? ", {" : "{");
            for (int li = 0; li < maxLayers; ++li) {
                const auto& layers = p.scenes[si].layers;
                out << (li ? ", " : "")
                    << floatLit(li < (int)layers.size() ? get(layers[li]) : 0.0f);
            }
            out << "}";
        }
        out << "};\n";
    };
    layerFloatTable("SCENE_LAYER_STREAM_XS",
                    [](const SceneLayer& l) { return l.streamX; });
    layerFloatTable("SCENE_LAYER_STREAM_ZS",
                    [](const SceneLayer& l) { return l.streamZ; });
    layerFloatTable("SCENE_LAYER_STREAM_RADII", [](const SceneLayer& l) {
        return l.autoStream ? l.streamRadius : 0.0f;
    });
    out << "\n";

    // Mirror objects (type 15): a flat side-table keyed by (scene, object)
    // like OBJECT_SCRIPT_ATTACHES, so SceneObjectData stays a fixed POD.
    // Target names resolve to scene-table indices here; a dangling name (the
    // object was deleted) is silently dropped and the mirror just shows less.
    {
        std::ostringstream infos, targets;
        int mirrorCount = 0, targetCount = 0;
        for (int si = 0; si < sceneCount; ++si) {
            const auto& objs = p.scenes[si].objects;
            for (size_t oi = 0; oi < objs.size(); ++oi) {
                const SceneObject& o = objs[oi];
                if (o.type != PrimitiveType::Mirror) continue;
                const int first = targetCount;
                for (const std::string& name : o.mirrorObjects)
                    for (size_t ti = 0; ti < objs.size(); ++ti) {
                        if (ti == oi || objs[ti].name != name) continue;
                        targets << (targetCount ? ", " : "") << ti;
                        ++targetCount;
                        break;
                    }
                infos << (mirrorCount ? ",\n    " : "    ") << "{" << si << ", "
                      << oi << ", " << floatLit(o.mirrorOpacity) << ", "
                      << (o.mirrorReflectPlayer ? 1 : 0) << ", " << first << ", "
                      << (targetCount - first) << "},  // " << o.name;
                ++mirrorCount;
            }
        }
        out << "// Mirrors (type 15): each entry re-draws its target objects\n"
               "// reflected across the mirror plane (renderMirrors in the game\n"
               "// cpp). Targets index the mirror's own scene object table.\n"
               "struct MirrorData {\n"
               "  int scene;          // scene index\n"
               "  int object;         // the mirror's index in its scene table\n"
               "  float opacity;      // glass alpha 0..1 (tint = object color)\n"
               "  int reflectPlayer;  // 1 = also reflect the third-person avatar\n"
               "  int firstTarget;    // first entry in MIRROR_TARGETS\n"
               "  int targetCount;\n"
               "};\n"
            << "constexpr int MIRROR_COUNT = " << mirrorCount << ";\n"
            << "constexpr MirrorData MIRRORS[" << (mirrorCount ? mirrorCount : 1)
            << "] = {\n"
            << (mirrorCount ? infos.str() : "    {0, -1, 0.0F, 0, 0, 0}")
            << "\n};\n"
            << "constexpr int MIRROR_TARGETS["
            << (targetCount ? targetCount : 1) << "] = {"
            << (targetCount ? targets.str() : "-1") << "};\n\n";
    }

    // Portal objects (type 16): same flat side-table pattern as MIRRORS.
    // The target (the linked portal) and the view-object list resolve from
    // names to scene-table indices here; a dangling/empty target leaves the
    // portal inactive (target -1: tinted surface, no view, no teleport).
    {
        std::ostringstream infos, views;
        int portalCount = 0, viewCount = 0;
        for (int si = 0; si < sceneCount; ++si) {
            const auto& objs = p.scenes[si].objects;
            for (size_t oi = 0; oi < objs.size(); ++oi) {
                const SceneObject& o = objs[oi];
                if (o.type != PrimitiveType::Portal) continue;
                int target = -1;
                if (!o.portalTarget.empty())
                    for (size_t ti = 0; ti < objs.size(); ++ti)
                        if (ti != oi && objs[ti].type == PrimitiveType::Portal &&
                            objs[ti].name == o.portalTarget) {
                            target = (int)ti;
                            break;
                        }
                const int first = viewCount;
                for (const std::string& name : o.portalObjects)
                    for (size_t ti = 0; ti < objs.size(); ++ti) {
                        if (ti == oi || objs[ti].name != name) continue;
                        views << (viewCount ? ", " : "") << ti;
                        ++viewCount;
                        break;
                    }
                infos << (portalCount ? ",\n    " : "    ") << "{" << si << ", "
                      << oi << ", " << target << ", "
                      << (o.portalShowTerrain ? 1 : 0) << ", "
                      << (o.portalTeleportObjects ? 1 : 0) << ", "
                      << (o.portalViewAll ? 1 : 0) << ", " << first << ", "
                      << (viewCount - first) << "},  // " << o.name;
                ++portalCount;
            }
        }
        out << "// Portals (type 16): each entry links a surface to its target\n"
               "// portal. The game renders the through-view of the nearest one\n"
               "// into a VRAM target every frame and teleports whatever crosses\n"
               "// the surface (renderPortalView/renderPortals/updatePortals in\n"
               "// the game cpp). Indices index the portal's own scene table.\n"
               "struct PortalData {\n"
               "  int scene;            // scene index\n"
               "  int object;           // the portal's index in its scene table\n"
               "  int target;           // linked portal's index, -1 = inactive\n"
               "  int showTerrain;      // 1 = sky dome + terrain in the view\n"
               "  int teleportObjects;  // 1 = physics objects teleport too\n"
               "  int viewAll;          // 1 = EVERY object in the view\n"
               "                        //     (experimental; list ignored)\n"
               "  int firstView;        // first entry in PORTAL_VIEW_OBJECTS\n"
               "  int viewCount;\n"
               "};\n"
            << "constexpr int PORTAL_COUNT = " << portalCount << ";\n"
            << "constexpr PortalData PORTALS[" << (portalCount ? portalCount : 1)
            << "] = {\n"
            << (portalCount ? infos.str() : "    {0, -1, -1, 0, 0, 0, 0, 0}")
            << "\n};\n"
            << "constexpr int PORTAL_VIEW_OBJECTS["
            << (viewCount ? viewCount : 1) << "] = {"
            << (viewCount ? views.str() : "-1") << "};\n\n";
    }

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

    // Player entities per scene: the first Player object drives the camera
    // (P1); the second one, when present, is player 2 of the two-player modes
    // (docs/multiplayer.md). Both get the same table set, P2's prefixed
    // PLAYER2_; the per-player accessor macros below the scene tables select
    // by player index.
    std::vector<const SceneObject*> players[2] = {
        std::vector<const SceneObject*>(sceneCount, nullptr),
        std::vector<const SceneObject*>(sceneCount, nullptr)};
    std::vector<int> playerIdx[2] = {std::vector<int>(sceneCount, -1),
                                     std::vector<int>(sceneCount, -1)};
    for (int si = 0; si < sceneCount; ++si) {
        int found = 0;
        for (size_t i = 0; i < p.scenes[si].objects.size() && found < 2; ++i)
            if (p.scenes[si].objects[i].type == PrimitiveType::Player) {
                players[found][si] = &p.scenes[si].objects[i];
                playerIdx[found][si] = (int)i;
                ++found;
            }
    }

    for (int pl = 0; pl < 2; ++pl) {
        const std::string pre = pl == 0 ? "PLAYER_" : "PLAYER2_";
        const auto& ps = players[pl];
        out << "constexpr int " << pre << "INDEXES[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si)
            out << (si ? ", " : "") << playerIdx[pl][si];
        out << "};\n"
            << "constexpr int " << pre << "MODES[SCENE_COUNT] = {";  // 0 = walk, 1 = noclip
        for (int si = 0; si < sceneCount; ++si)
            out << (si ? ", " : "") << (ps[si] ? ps[si]->playerMode : 0);
        out << "};\n";

        auto playerFloat = [&](const char* name, auto get, float dflt) {
            out << "constexpr float " << pre << name << "[SCENE_COUNT] = {";
            for (int si = 0; si < sceneCount; ++si)
                out << (si ? ", " : "") << floatLit(ps[si] ? get(*ps[si]) : dflt);
            out << "};\n";
        };
        playerFloat("WALK_SPEEDS", [](const SceneObject& o) { return o.playerWalkSpeed; }, 0.4f);
        playerFloat("LOOK_SPEEDS", [](const SceneObject& o) { return o.playerLookSpeed; }, 1.0f);
        playerFloat("EYE_HEIGHTS", [](const SceneObject& o) { return o.playerEyeHeight; }, 1.8f);
        playerFloat("JUMP_SPEEDS", [](const SceneObject& o) { return o.playerJumpSpeed; }, 4.5f);
        out << "constexpr bool " << pre << "CAN_JUMPS[SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si)
            out << (si ? ", " : "")
                << (!ps[si] || ps[si]->playerCanJump ? "true" : "false");
        out << "};\n";

        // Third-person parameters (playerMode == 2). Clip names resolve to the
        // avatar model's clip indices at scene load (resolveClipIndex).
        playerFloat("RUN_THRESHOLDS", [](const SceneObject& o) { return o.playerRunThreshold; }, 0.55f);
        playerFloat("CAM_DISTS", [](const SceneObject& o) { return o.playerCamDist; }, 6.0f);
        playerFloat("CAM_HEIGHTS", [](const SceneObject& o) { return o.playerCamHeight; }, 1.6f);
        playerFloat("CAM_SHOULDERS", [](const SceneObject& o) { return o.playerCamShoulder; }, 0.0f);
        playerFloat("TURN_RATES", [](const SceneObject& o) { return o.playerTurnRate; }, 0.25f);
        auto playerClip = [&](const char* name, auto get) {
            out << "constexpr const char* " << pre << name << "[SCENE_COUNT] = {";
            for (int si = 0; si < sceneCount; ++si)
                out << (si ? ", " : "") << "\""
                    << (ps[si] ? escapeCString(get(*ps[si])) : std::string()) << "\"";
            out << "};\n";
        };
        playerClip("IDLE_CLIPS", [](const SceneObject& o) { return o.playerIdleClip; });
        playerClip("WALK_CLIPS", [](const SceneObject& o) { return o.playerWalkClip; });
        playerClip("RUN_CLIPS", [](const SceneObject& o) { return o.playerRunClip; });
        playerClip("JUMP_CLIPS", [](const SceneObject& o) { return o.playerJumpClip; });
    }
    out << "\n";

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
    // Hidden third clipping mode ("clipping": "vu1" in project.json, no UI):
    // per-package classification stays (CLIP_PRECISE is true for it), but
    // crossing packages are clipped on VU1 instead of the EE.
    sceneBools("CLIP_VU1S", [&](int si) { return rs[si].clipping == "vu1"; });
    sceneFloats("SKY_RS", [&](int si) { return floatLit(rs[si].skyColor[0] * 255.0f); });
    sceneFloats("SKY_GS", [&](int si) { return floatLit(rs[si].skyColor[1] * 255.0f); });
    sceneFloats("SKY_BS", [&](int si) { return floatLit(rs[si].skyColor[2] * 255.0f); });
    sceneBools("SKY_DOMES", [&](int si) { return rs[si].skyDome; });
    // Zenith-size gradient exponent, precomputed: pow(t, exp), exp=(1-size)/size
    // (0.5 => 1 linear). The dome build raises the elevation fraction to it.
    sceneFloats("SKY_ZENITH_EXPS", [&](int si) {
        float z = rs[si].zenithSize;
        z = z < 0.05f ? 0.05f : (z > 0.95f ? 0.95f : z);
        return floatLit((1.0f - z) / z);
    });
    sceneFloats("SKY_TOP_RS", [&](int si) { return floatLit(rs[si].skyTopColor[0] * 255.0f); });
    sceneFloats("SKY_TOP_GS", [&](int si) { return floatLit(rs[si].skyTopColor[1] * 255.0f); });
    sceneFloats("SKY_TOP_BS", [&](int si) { return floatLit(rs[si].skyTopColor[2] * 255.0f); });
    sceneInts("POSTFX_BLOOMS", [&](int si) { return fx128(rs[si].bloom); });
    sceneInts("POSTFX_GRAINS", [&](int si) { return fx128(rs[si].grain); });
    sceneInts("POSTFX_DOFS", [&](int si) { return fx128(rs[si].dofAmount); });
    sceneFloats("POSTFX_DOF_FOCUSES",
                [&](int si) { return floatLit(rs[si].dofFocus); });
    sceneFloats("POSTFX_DOF_RANGES",
                [&](int si) { return floatLit(rs[si].dofRange); });
    sceneBools("FOG_ENABLEDS", [&](int si) { return rs[si].fogEnabled; });
    sceneFloats("FOG_RS", [&](int si) { return floatLit(rs[si].fogColor[0] * 255.0f); });
    sceneFloats("FOG_GS", [&](int si) { return floatLit(rs[si].fogColor[1] * 255.0f); });
    sceneFloats("FOG_BS", [&](int si) { return floatLit(rs[si].fogColor[2] * 255.0f); });
    sceneFloats("FOG_STARTS", [&](int si) { return floatLit(rs[si].fogStart); });
    sceneFloats("FOG_ENDS", [&](int si) { return floatLit(rs[si].fogEnd); });
    // Camera flashlight is a Player object property (color/range/angle are
    // fixed per scene; the enabled flag is only the runtime master's initial
    // value - the flow graph / toggle button change it live). No player in a
    // scene = no flashlight there.
    sceneBools("FLASHLIGHT_ENABLEDS", [&](int si) {
        return players[0][si] && players[0][si]->flashlightEnabled;
    });
    sceneFloats("FLASHLIGHT_RS", [&](int si) {
        return floatLit((players[0][si] ? players[0][si]->flashlightColor[0] : 0.0f) * 128.0f);
    });
    sceneFloats("FLASHLIGHT_GS", [&](int si) {
        return floatLit((players[0][si] ? players[0][si]->flashlightColor[1] : 0.0f) * 128.0f);
    });
    sceneFloats("FLASHLIGHT_BS", [&](int si) {
        return floatLit((players[0][si] ? players[0][si]->flashlightColor[2] : 0.0f) * 128.0f);
    });
    sceneFloats("FLASHLIGHT_RANGES", [&](int si) {
        return floatLit(players[0][si] ? players[0][si]->flashlightRange : 30.0f);
    });
    sceneFloats("FLASHLIGHT_ANGLES", [&](int si) {
        return floatLit(players[0][si] ? players[0][si]->flashlightAngle : 20.0f);
    });
    sceneBools("HIGHLIGHT_USABLES", [&](int si) { return rs[si].highlightUsable; });
    sceneFloats("HIGHLIGHT_DISTANCES", [&](int si) { return floatLit(rs[si].highlightDistance); });
    sceneFloats("HIGHLIGHT_RS", [&](int si) { return floatLit(rs[si].highlightColor[0] * 255.0f); });
    sceneFloats("HIGHLIGHT_GS", [&](int si) { return floatLit(rs[si].highlightColor[1] * 255.0f); });
    sceneFloats("HIGHLIGHT_BS", [&](int si) { return floatLit(rs[si].highlightColor[2] * 255.0f); });
    sceneFloats("HIGHLIGHT_WIDTHS", [&](int si) { return floatLit(rs[si].highlightWidth); });
    sceneInts("HIGHLIGHT_STEPS_S", [&](int si) { return rs[si].highlightSteps; });
    sceneFloats("HIGHLIGHT_OPACITIES", [&](int si) { return floatLit(rs[si].highlightOpacity); });
    sceneBools("HIGHLIGHT_OVERLAYS", [&](int si) { return rs[si].highlightOverlay; });

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

// Camera flashlight runtime state (a Player object property), defined in the
// game cpp. g_flashEnabled is the master switch (Set Flashlight flow node);
// g_flashOn is the on/off toggle state the player controls with the optional
// toggle button. The beam shows only while both are set.
extern bool g_flashEnabled;
extern bool g_flashOn;
)";
    // Per-scene flashlight toggle button: the pad button the scene's player
    // presses to flip the beam on/off (empty = no toggle). A template so the
    // header stays engine-include-free (instantiated where Engine is complete).
    out << "template <typename TEngine>\n"
           "inline bool flashlightTogglePressed(TEngine* engine) {\n"
           "  (void)engine;\n"
           "  switch (g_activeScene) {\n";
    for (int si = 0; si < sceneCount; ++si)
        if (players[0][si] && !players[0][si]->flashlightToggleButton.empty())
            out << "    case " << si << ": return engine->pad.getClicked()."
                << players[0][si]->flashlightToggleButton << ";\n";
    out << "    default: break;\n"
           "  }\n"
           "  return false;\n"
           "}\n";
    out << R"(// Frames per `seconds` of wall-clock time (>= 1), for frame-counter timers.
// Uses the MEASURED frame time, not the nominal vsync rate: with vsync
// disabled the loop free-runs way past 50 FPS and a nominal-rate count would
// make every timer (Delay, Every N Seconds, splash holds, sound retriggers)
// fire that much too fast - the disableVsync contract is "faster picture,
// same gameplay speed". At vsync the measured dt snaps to nominal, so this
// is bit-identical to the old seconds * g_frameRate there.
inline int everyFrames(float seconds) {
  const int f = (int)(seconds / (g_frameDt > 0.0001F ? g_frameDt : 0.02F));
  return f < 1 ? 1 : f;
}
#define SCENE_OBJECT_COUNT SCENE_OBJECT_COUNTS[g_activeScene]
#define SCENE_OBJECTS SCENE_OBJECT_TABLES[g_activeScene]
#define SCENE_LAYER_COUNT SCENE_LAYER_COUNTS[g_activeScene]
#define SCENE_LAYER_START SCENE_LAYER_STARTS[g_activeScene]
#define SCENE_LAYER_STREAM_X SCENE_LAYER_STREAM_XS[g_activeScene]
#define SCENE_LAYER_STREAM_Z SCENE_LAYER_STREAM_ZS[g_activeScene]
#define SCENE_LAYER_STREAM_R SCENE_LAYER_STREAM_RADII[g_activeScene]
#define PLAYER_INDEX PLAYER_INDEXES[g_activeScene]
#define PLAYER_MODE PLAYER_MODES[g_activeScene]
#define PLAYER_WALK_SPEED PLAYER_WALK_SPEEDS[g_activeScene]
#define PLAYER_LOOK_SPEED PLAYER_LOOK_SPEEDS[g_activeScene]
#define PLAYER_EYE_HEIGHT PLAYER_EYE_HEIGHTS[g_activeScene]
#define PLAYER_JUMP_SPEED PLAYER_JUMP_SPEEDS[g_activeScene]
#define PLAYER_CAN_JUMP PLAYER_CAN_JUMPS[g_activeScene]
#define PLAYER_RUN_THRESHOLD PLAYER_RUN_THRESHOLDS[g_activeScene]
#define PLAYER_CAM_DIST PLAYER_CAM_DISTS[g_activeScene]
#define PLAYER_CAM_HEIGHT PLAYER_CAM_HEIGHTS[g_activeScene]
#define PLAYER_CAM_SHOULDER PLAYER_CAM_SHOULDERS[g_activeScene]
#define PLAYER_TURN_RATE PLAYER_TURN_RATES[g_activeScene]
#define PLAYER_IDLE_CLIP PLAYER_IDLE_CLIPS[g_activeScene]
#define PLAYER_WALK_CLIP PLAYER_WALK_CLIPS[g_activeScene]
#define PLAYER_RUN_CLIP PLAYER_RUN_CLIPS[g_activeScene]
#define PLAYER_JUMP_CLIP PLAYER_JUMP_CLIPS[g_activeScene]
#define PLAYER2_INDEX PLAYER2_INDEXES[g_activeScene]
// Per-player table selection for the shared walker (pi: 0 = P1, 1 = P2).
#define PP_TBL(pi, T) \
  ((pi) == 0 ? PLAYER_##T[g_activeScene] : PLAYER2_##T[g_activeScene])
#define PP_INDEX(pi) PP_TBL(pi, INDEXES)
#define PP_MODE(pi) PP_TBL(pi, MODES)
#define PP_WALK_SPEED(pi) PP_TBL(pi, WALK_SPEEDS)
#define PP_LOOK_SPEED(pi) PP_TBL(pi, LOOK_SPEEDS)
#define PP_EYE_HEIGHT(pi) PP_TBL(pi, EYE_HEIGHTS)
#define PP_JUMP_SPEED(pi) PP_TBL(pi, JUMP_SPEEDS)
#define PP_CAN_JUMP(pi) PP_TBL(pi, CAN_JUMPS)
#define PP_RUN_THRESHOLD(pi) PP_TBL(pi, RUN_THRESHOLDS)
#define PP_CAM_DIST(pi) PP_TBL(pi, CAM_DISTS)
#define PP_CAM_HEIGHT(pi) PP_TBL(pi, CAM_HEIGHTS)
#define PP_CAM_SHOULDER(pi) PP_TBL(pi, CAM_SHOULDERS)
#define PP_TURN_RATE(pi) PP_TBL(pi, TURN_RATES)
#define PP_IDLE_CLIP(pi) PP_TBL(pi, IDLE_CLIPS)
#define PP_WALK_CLIP(pi) PP_TBL(pi, WALK_CLIPS)
#define PP_RUN_CLIP(pi) PP_TBL(pi, RUN_CLIPS)
#define PP_JUMP_CLIP(pi) PP_TBL(pi, JUMP_CLIPS)
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
// Painted terrain layers (two-pass splatting; docs/terrain-painting.md)
#define TERRAIN_LAYER_COUNT TERRAIN_LAYER_COUNTS[g_activeScene]
#define TERRAIN_SPLAT_WEIGHTS TERRAIN_SPLAT_TABLES[g_activeScene]
#define TERRAIN_TILE_U TERRAIN_TILE_US[g_activeScene]
#define TERRAIN_TILE_V TERRAIN_TILE_VS[g_activeScene]
#define TERRAIN_HAS_MATERIAL TERRAIN_HAS_MATERIALS[g_activeScene]
#define TERRAIN_TINT_R TERRAIN_TINTS[g_activeScene][0]
#define TERRAIN_TINT_G TERRAIN_TINTS[g_activeScene][1]
#define TERRAIN_TINT_B TERRAIN_TINTS[g_activeScene][2]
#define TERRAIN_TINT_VARIATION TERRAIN_TINT_VARIATIONS[g_activeScene]
#define TERRAIN_TINT_SCALE TERRAIN_TINT_SCALES[g_activeScene]
// Per-scene sky / clipping / post-FX / usable-highlight (Scene > Preferences)
#define CLIP_PRECISE CLIP_PRECISES[g_activeScene]
#define CLIP_VU1 CLIP_VU1S[g_activeScene]
#define SKY_R SKY_RS[g_activeScene]
#define SKY_G SKY_GS[g_activeScene]
#define SKY_B SKY_BS[g_activeScene]
#define SKY_DOME SKY_DOMES[g_activeScene]
#define SKY_ZENITH_EXP SKY_ZENITH_EXPS[g_activeScene]
#define SKY_TOP_R SKY_TOP_RS[g_activeScene]
#define SKY_TOP_G SKY_TOP_GS[g_activeScene]
#define SKY_TOP_B SKY_TOP_BS[g_activeScene]
#define POSTFX_BLOOM POSTFX_BLOOMS[g_activeScene]
#define POSTFX_GRAIN POSTFX_GRAINS[g_activeScene]
#define POSTFX_DOF POSTFX_DOFS[g_activeScene]
#define POSTFX_DOF_FOCUS POSTFX_DOF_FOCUSES[g_activeScene]
#define POSTFX_DOF_RANGE POSTFX_DOF_RANGES[g_activeScene]
#define FOG_ENABLED FOG_ENABLEDS[g_activeScene]
#define FOG_R FOG_RS[g_activeScene]
#define FOG_G FOG_GS[g_activeScene]
#define FOG_B FOG_BS[g_activeScene]
#define FOG_START FOG_STARTS[g_activeScene]
#define FOG_END FOG_ENDS[g_activeScene]
#define FLASHLIGHT_ENABLED FLASHLIGHT_ENABLEDS[g_activeScene]
#define FLASHLIGHT_R FLASHLIGHT_RS[g_activeScene]
#define FLASHLIGHT_G FLASHLIGHT_GS[g_activeScene]
#define FLASHLIGHT_B FLASHLIGHT_BS[g_activeScene]
#define FLASHLIGHT_RANGE FLASHLIGHT_RANGES[g_activeScene]
#define FLASHLIGHT_ANGLE FLASHLIGHT_ANGLES[g_activeScene]
#define HIGHLIGHT_USABLE HIGHLIGHT_USABLES[g_activeScene]
#define HIGHLIGHT_DISTANCE HIGHLIGHT_DISTANCES[g_activeScene]
#define HIGHLIGHT_R HIGHLIGHT_RS[g_activeScene]
#define HIGHLIGHT_G HIGHLIGHT_GS[g_activeScene]
#define HIGHLIGHT_B HIGHLIGHT_BS[g_activeScene]
#define HIGHLIGHT_WIDTH HIGHLIGHT_WIDTHS[g_activeScene]
#define HIGHLIGHT_STEPS HIGHLIGHT_STEPS_S[g_activeScene]
#define HIGHLIGHT_OPACITY HIGHLIGHT_OPACITIES[g_activeScene]
#define HIGHLIGHT_OVERLAY HIGHLIGHT_OVERLAYS[g_activeScene]
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

// ---------------------------------------------------------------------------
// Custom screen effects -> C++ (screen_fx.gen.hpp/.cpp). Each enabled placement
// whose .screenfx file resolved becomes one build callback; its index here is
// the effect's codegen id (screenFx_<id>). The frame loop calls them at their
// stack slot via RendererCore::applyCustomPostFx. See docs/custom-screen-
// effects.md and src/screenfx.cpp for the file format.

// Enabled placements whose effect file loaded, in stack order.
static std::vector<const ScreenFxPlacement*> enabledScreenFx(const Project& p) {
    std::vector<const ScreenFxPlacement*> v;
    for (const auto& f : p.screenFx)
        if (f.enabled && customScreenFx(f.key)) v.push_back(&f);
    return v;
}

// The applyCustomPostFx() calls injected into the frame loop. inLoop = the
// dispatch inside `for (i ...)` (placements with a concrete layer); otherwise
// the post-loop dispatch for topmost placements (layer -1 / >= hud size).
static std::string screenFxDispatch(const Project& p, bool inLoop) {
    const auto fx = enabledScreenFx(p);
    std::ostringstream out;
    for (size_t n = 0; n < fx.size(); ++n) {
        const int layer = fx[n]->layer;
        const bool top = layer < 0 || layer >= (int)p.hud.size();
        const CustomScreenFx* e = customScreenFx(fx[n]->key);
        if (inLoop) {
            if (top) continue;
            out << "      if (i == " << layer
                << ")  // " << e->title << "\n"
                << "        engine->renderer.core.applyCustomPostFx(&screenFx_"
                << n << ", nullptr);\n";
        } else {
            if (!top) continue;
            out << "    engine->renderer.core.applyCustomPostFx(&screenFx_" << n
                << ", nullptr);  // " << e->title << "\n";
        }
    }
    return out.str();
}

static std::string screenFxHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const auto fx = enabledScreenFx(p);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every "
           "build.\n"
           "#pragma once\n\n"
           "#include <tyra>\n\n"
           "// Custom screen effects (Tools > UI Editor). Each callback appends\n"
           "// the effect's GS blits over the current framebuffer and returns\n"
           "// the advanced packet cursor; run via applyCustomPostFx at the\n"
           "// effect's stack slot. Bodies live in screen_fx.gen.cpp.\n"
           "namespace "
        << ns << " {\n\n";
    for (size_t n = 0; n < fx.size(); ++n) {
        const CustomScreenFx* e = customScreenFx(fx[n]->key);
        out << "qword_t* screenFx_" << n
            << "(Tyra::RendererCorePostFx& fx, qword_t* q, void* user);"
               "  // "
            << e->title << "\n";
    }
    if (fx.empty()) out << "// (no custom screen effects placed)\n";
    out << "\n}  // namespace " << ns << "\n";
    return out.str();
}

static std::string screenFxSource(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const auto fx = enabledScreenFx(p);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every "
           "build.\n"
           "#include \"scripts/screen_fx.gen.hpp\"\n\n"
           "// PS2 GS packing macros the effect bodies use (PACK_GIFTAG,\n"
           "// GS_SET_*, GS_REG_*, GS_PSM_*). No pixel shaders on the PS2 - an\n"
           "// effect is framebuffer blits, like the built-in bloom / grain.\n"
           "#include <gif_tags.h>\n"
           "#include <gs_gp.h>\n"
           "#include <gs_psm.h>\n"
           "#include <draw.h>\n\n"
           "namespace "
        << ns << " {\nusing namespace Tyra;\n";
    for (size_t n = 0; n < fx.size(); ++n) {
        const ScreenFxPlacement* pl = fx[n];
        const CustomScreenFx* e = customScreenFx(pl->key);
        std::string body = e->code;
        for (int i = 0; i < 4; ++i)
            body = replaceAll(body, "{p" + std::to_string(i) + "}",
                              "param[" + std::to_string(i) + "]");
        out << "\n// " << e->title << " (" << pl->key << ")\n"
            << "qword_t* screenFx_" << n
            << "(Tyra::RendererCorePostFx& fx, qword_t* q, void* user) {\n"
            << "  (void)user; (void)fx; (void)q;\n"
            << "  const float param[4] = {" << floatLit(pl->params[0]) << ", "
            << floatLit(pl->params[1]) << ", " << floatLit(pl->params[2]) << ", "
            << floatLit(pl->params[3]) << "};\n"
            << "  (void)param;\n"
            << body;
        if (!body.empty() && body.back() != '\n') out << "\n";
        out << "  return q;\n}\n";
    }
    out << "\n}  // namespace " << ns << "\n";
    return out.str();
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
    s = replaceAll(s, "{{TERRAIN_VIEW_DISTANCE}}", floatLit(st.terrainViewDistance));
    s = replaceAll(s, "{{EYE_HEIGHT}}", floatLit(st.eyeHeight));
    s = replaceAll(s, "{{WALK_SPEED}}", floatLit(st.walkSpeed));
    s = replaceAll(s, "{{LOOK_SPEED}}", floatLit(st.lookSpeed));
    s = replaceAll(s, "{{DEADZONE_L}}", floatLit(st.stickDeadzoneL));
    s = replaceAll(s, "{{DEADZONE_R}}", floatLit(st.stickDeadzoneR));
    s = replaceAll(s, "{{STICK_CURVE_L}}", std::to_string(st.stickCurveL));
    s = replaceAll(s, "{{STICK_CURVE_R}}", std::to_string(st.stickCurveR));
    s = replaceAll(s, "{{STICK_EXP_L}}", floatLit(st.stickExpL));
    s = replaceAll(s, "{{STICK_EXP_R}}", floatLit(st.stickExpR));
    s = replaceAll(s, "{{ORBIT_SPEED}}", floatLit(st.orbitSpeed));
    s = replaceAll(s, "{{MULTIPLAYER_MODE}}", st.multiplayer == "shared" ? "1"
                                              : st.multiplayer == "split" ? "2"
                                                                          : "0");
    s = replaceAll(s, "{{P2_JOIN_ON_START}}",
                   st.p2JoinOnStart ? "true" : "false");
    s = replaceAll(s, "{{GRAVITY}}", floatLit(st.gravity));
    s = replaceAll(s, "{{JUMP_SPEED}}", floatLit(st.jumpSpeed));
    s = replaceAll(s, "{{LOADING_SCREEN}}", st.loadingScreen ? "true" : "false");
    s = replaceAll(s, "{{FRAME_LIMIT}}", st.disableVsync ? "false" : "true");
    s = replaceAll(s, "{{ANIM_LOD_DISTANCE}}", floatLit(st.animLodDistance));
    s = replaceAll(s, "{{MESH_LOD_DISTANCE}}", floatLit(st.meshLodDistance));
    s = replaceAll(s, "{{STATIC_BATCHING}}", st.staticBatching ? "true" : "false");
    s = replaceAll(s, "{{VIDEO_MODE}}", st.videoSystem == "pal"    ? "PAL"
                                        : st.videoSystem == "ntsc" ? "NTSC"
                                                                   : "Auto");
    s = replaceAll(s, "{{DISPLAY_MODE}}",
                   st.displayMode == "1080i"              ? "HiDef1080i"
                   : st.displayMode == "progressive"      ? "Progressive480p"
                   : st.displayMode == "interlaced-field" ? "InterlacedField"
                   : st.displayMode == "pal576"           ? "Pal576i"
                                                          : "Interlaced");
    s = replaceAll(s, "{{PAL_FULL_HEIGHT}}",
                   st.palFullHeight ? "true" : "false");
    s = replaceAll(s, "{{WIDESCREEN}}", st.widescreen ? "true" : "false");
    const bool debugProfile = st.buildProfile == "debug";
    s = replaceAll(s, "{{DEBUG_SHOW_FPS}}",
                   debugProfile && st.showFps ? "true" : "false");
    s = replaceAll(s, "{{DEBUG_SHOW_MEM}}",
                   debugProfile && st.showMemory ? "true" : "false");
    s = replaceAll(s, "{{DEBUG_SHOW_PROFILER}}",
                   debugProfile && st.showProfiler ? "true" : "false");
    s = replaceAll(s, "{{ENGINE_SRC}}", engineSourceDir());
    s = replaceAll(s, "{{ENGINE_HASH}}", engineSourceHash());
    // Custom screen effect dispatch injected into the frame loop (empty when
    // none are placed). Two slots: inside the HUD-sprite loop (per-layer) and
    // after it (topmost, layer -1).
    s = replaceAll(s, "{{SCREEN_FX_IN_LOOP}}", screenFxDispatch(p, true));
    s = replaceAll(s, "{{SCREEN_FX_TOP}}", screenFxDispatch(p, false));
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
// Cutscene Director -> C++ runtime. The header declares the play/stop entry
// points the flow-graph Play/Stop Sequence nodes call; the source compiles the
// keyframe tables and a global Script (the director) that poses objects and
// the camera each frame while a sequence is active. Object names resolve to
// (scene, runtime object index) here - the director only applies a track while
// its scene is the active one.
// ---------------------------------------------------------------------------
std::string sequencesHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
           "#pragma once\n\n"
           "#include \"scripts/script.hpp\"\n\n"
           "namespace "
        << ns
        << " {\n"
           "namespace sequences {\n"
           "// Cutscene Director runtime (see src/scripts/sequences.gen.cpp),\n"
           "// driven by the Play Sequence / Stop Sequence flow nodes.\n"
           "void play(int index);  // start Project::sequences[index] at t=0\n"
           "void stop();           // stop the active sequence, free the camera\n"
           "// Widescreen bars + fade-to-black compositor, called by the game\n"
           "// loop inside beginFrame/endFrame after the HUD (solid 2D quads;\n"
           "// no-op unless the active cutscene draws them).\n"
           "void renderOverlay(Tyra::Engine* engine, const ScriptContext& ctx);\n"
           "}  // namespace sequences\n"
           "}  // namespace "
        << ns << "\n";
    return out.str();
}

std::string sequencesScript(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);

    // First scene (+ index within it) that owns an object with this name.
    auto resolve = [&](const std::string& name) -> std::pair<int, int> {
        if (name.empty()) return {-1, -1};
        for (size_t si = 0; si < p.scenes.size(); ++si) {
            const auto& objs = p.scenes[si].objects;
            for (size_t oi = 0; oi < objs.size(); ++oi)
                if (objs[oi].name == name) return {(int)si, (int)oi};
        }
        return {-1, -1};
    };
    auto v3 = [&](const float* a) {
        return "{" + floatLit(a[0]) + ", " + floatLit(a[1]) + ", " + floatLit(a[2]) + "}";
    };

    std::ostringstream out;
    out << "// Generated by TyraX from the Cutscene Director. Do not edit -\n"
           "// regenerated on every build. Edit the sequences in the editor.\n"
           "#include \"scripts/script.hpp\"\n"
           "#include \"scripts/sequences.gen.hpp\"\n\n"
           "namespace "
        << ns
        << " {\n"
           "namespace {\n\n"
           "// Easing (mirrors src/sequence.hpp seqEase): 0 linear, 1 smoothstep,\n"
           "// 2 step (hold to the next key).\n"
           "static float seqEase(int e, float u) {\n"
           "  if (u <= 0.0F) return 0.0F;\n"
           "  if (u >= 1.0F) return 1.0F;\n"
           "  if (e == 1) return u * u * (3.0F - 2.0F * u);\n"
           "  if (e == 2) return 0.0F;\n"
           "  return u;\n"
           "}\n\n"
           "struct ObjKey { float t; float pos[3]; float rot[3]; float scale[3];\n"
           "                float col[3]; int vis; int ease; };\n"
           "struct Track { int scene; int obj; int chPos; int chRot; int chScale;\n"
           "               int chCol; int chVis; const ObjKey* keys; int keyCount; };\n"
           "// A camera shot. camScene/camObj >= 0 = bound to a Camera entity: the\n"
           "// shot films from that object's CURRENT pose (so an object track can\n"
           "// dolly it); eye/at hold the entity's authored pose as the fallback\n"
           "// when its scene is not the active one. fov is the entity's for bound\n"
           "// shots, the key's own for free ones. shake = handheld amplitude.\n"
           "struct CamKey { float t; float eye[3]; float at[3]; float fov;\n"
           "                float shake; int ease; int camScene; int camObj; };\n"
           "struct Seq { const char* name; float duration; int loop; int camEnabled;\n"
           "             int hidePlayer;  // hide the third-person avatar while playing\n"
           "             int bars; int skippable; float fadeIn; float fadeOut;\n"
           "             float barsSlideIn; float barsSlideOut;  // bars reveal, s\n"
           "             float barTB; float barLR;  // mask coverage per edge\n"
           "             const Track* tracks; int trackCount;\n"
           "             const CamKey* camKeys; int camKeyCount; };\n\n";

    // Per-sequence static keyframe tables.
    for (size_t si = 0; si < p.sequences.size(); ++si) {
        const Sequence& s = p.sequences[si];
        const std::string sp = "kS" + std::to_string(si);
        // Object tracks: one ObjKey[] per track, then the Track[].
        for (size_t ti = 0; ti < s.tracks.size(); ++ti) {
            SeqTrack t = s.tracks[ti];  // copy so we can sort keys by time
            std::sort(t.keys.begin(), t.keys.end(),
                      [](const SeqObjectKey& a, const SeqObjectKey& b) {
                          return a.time < b.time;
                      });
            const auto rr = resolve(t.target);
            out << "static const ObjKey " << sp << "T" << ti << "K[] = {";
            for (size_t ki = 0; ki < t.keys.size(); ++ki) {
                const SeqObjectKey& k = t.keys[ki];
                out << (ki ? ", " : "") << "{" << floatLit(k.time) << ", " << v3(k.position)
                    << ", " << v3(k.rotation) << ", " << v3(k.scale) << ", " << v3(k.color)
                    << ", " << (k.visible ? 1 : 0) << ", " << k.easing << "}";
            }
            out << "};  // \"" << t.target << "\" -> scene " << rr.first << " obj "
                << rr.second << "\n";
        }
        out << "static const Track " << sp << "Tracks[] = {";
        for (size_t ti = 0; ti < s.tracks.size(); ++ti) {
            const SeqTrack& t = s.tracks[ti];
            const auto rr = resolve(t.target);
            out << (ti ? ", " : "") << "{" << rr.first << ", " << rr.second << ", "
                << (t.animPos ? 1 : 0) << ", " << (t.animRot ? 1 : 0) << ", "
                << (t.animScale ? 1 : 0) << ", " << (t.animColor ? 1 : 0) << ", "
                << (t.animVis ? 1 : 0) << ", " << sp << "T" << ti << "K, "
                << t.keys.size() << "}";
        }
        if (s.tracks.empty()) out << "{0, -1, 0, 0, 0, 0, 0, nullptr, 0}";  // non-empty array
        out << "};\n";
        // Camera track. Bound shots resolve their Camera entity to a (scene,
        // object) index here and bake the entity's authored pose + FOV as the
        // eye/at/fov fallback for when the entity is unavailable at runtime.
        out << "static const CamKey " << sp << "Cam[] = {";
        {
            std::vector<SeqCameraKey> ck = s.cameraKeys;
            std::sort(ck.begin(), ck.end(),
                      [](const SeqCameraKey& a, const SeqCameraKey& b) {
                          return a.time < b.time;
                      });
            for (size_t ci = 0; ci < ck.size(); ++ci) {
                const SeqCameraKey& k = ck[ci];
                float eye[3] = {k.eye[0], k.eye[1], k.eye[2]};
                float at[3] = {k.target[0], k.target[1], k.target[2]};
                float fov = k.fov;
                std::pair<int, int> rr = {-1, -1};
                if (!k.camera.empty()) {
                    rr = resolve(k.camera);
                    const SceneObject* cam =
                        rr.first >= 0 ? &p.scenes[rr.first].objects[rr.second] : nullptr;
                    if (cam && cam->type == PrimitiveType::Camera) {
                        float fwd[3];
                        seqCameraForward(cam->rotation, fwd);
                        for (int c = 0; c < 3; ++c) {
                            eye[c] = cam->position[c];
                            at[c] = cam->position[c] + fwd[c];
                        }
                        fov = cam->cameraFov;
                    } else {
                        rr = {-1, -1};  // stale/non-camera binding: free shot
                    }
                }
                out << (ci ? ", " : "") << "{" << floatLit(k.time) << ", " << v3(eye)
                    << ", " << v3(at) << ", " << floatLit(fov) << ", "
                    << floatLit(k.shake) << ", " << k.easing << ", " << rr.first
                    << ", " << rr.second << "}";
                if (!k.camera.empty()) out << " /* \"" << k.camera << "\" */";
            }
            if (ck.empty())
                out << "{0.0F, {0,0,0}, {0,0,0}, 60.0F, 0.0F, 0, -1, -1}";
        }
        out << "};\n\n";
    }

    // The sequence table. The widescreen-mask coverage fractions come from the
    // same seqBarsFractions the editor overlays on the viewport.
    out << "static const Seq kSeqs[] = {";
    for (size_t si = 0; si < p.sequences.size(); ++si) {
        const Sequence& s = p.sequences[si];
        const std::string sp = "kS" + std::to_string(si);
        float bt, bb, bl, br;
        seqBarsFractions(s.bars, bt, bb, bl, br);
        out << (si ? ", " : "") << "\n  {\"" << escapeCString(s.name) << "\", "
            << floatLit(s.duration) << ", " << (s.loop ? 1 : 0) << ", "
            << (s.cameraEnabled ? 1 : 0) << ", " << (s.hidePlayer ? 1 : 0) << ", "
            << s.bars << ", "
            << (s.skippable ? 1 : 0) << ", " << floatLit(s.fadeIn) << ", "
            << floatLit(s.fadeOut) << ", " << floatLit(s.barsSlideIn) << ", "
            << floatLit(s.barsSlideOut) << ", " << floatLit(bt) << ", "
            << floatLit(bl) << ", " << sp << "Tracks, " << s.tracks.size() << ", "
            << sp << "Cam, " << s.cameraKeys.size() << "}";
    }
    if (p.sequences.empty())
        out << "{\"\", 0.0F, 0, 0, 0, 0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, "
               "nullptr, 0, nullptr, 0}";  // non-empty array
    out << "\n};\n"
        << "static const int kSeqCount = " << p.sequences.size() << ";\n\n";

    // Sampler + director.
    out << R"(// Interpolates one component of an object channel (0 pos, 1 rot, 2 scale,
// 3 color) across a track's keys at time t. Holds the ends.
static float sampleObj(const ObjKey* k, int n, float t, int comp, int which) {
  if (n <= 0) return 0.0F;
  auto val = [&](int i) -> float {
    if (which == 0) return k[i].pos[comp];
    if (which == 1) return k[i].rot[comp];
    if (which == 2) return k[i].scale[comp];
    return k[i].col[comp];
  };
  if (t <= k[0].t) return val(0);
  if (t >= k[n - 1].t) return val(n - 1);
  int i = 0;
  while (i < n - 1 && t >= k[i + 1].t) ++i;
  const float span = k[i + 1].t - k[i].t;
  const float u = span > 1e-6F ? (t - k[i].t) / span : 0.0F;
  const float e = seqEase(k[i].ease, u);
  return val(i) + (val(i + 1) - val(i)) * e;
}

// The Cutscene Director: one global Script. While a sequence is active it
// poses every track's object (in the matching scene), resolves and blends the
// camera shots (free or bound to Camera entities), applies the shot FOV to the
// real projection (RendererCore3D::setFov, restored afterwards) and drives the
// widescreen bars / fade the game composites via sequences::renderOverlay.
// Playback advances by the real frame dt so cutscenes run at a fixed
// wall-clock speed on PAL and NTSC alike.
class SequenceDirector : public Script {
  int active_ = -1;
  float time_ = 0.0F;
  bool cleanup_ = false;   // hand everything back on the next update
  float baseFov_ = -1.0F;  // projection FOV before the first override

  // Clamp + apply a shot FOV; the first application snapshots the FOV to
  // restore (recomputes the projection matrix + frustum planes only when the
  // value actually moved).
  void applyFov(ScriptContext& ctx, float fov) {
    if (!ctx.engine) return;
    auto& r3d = ctx.engine->renderer.core.renderer3D;
    if (baseFov_ < 0.0F) baseFov_ = r3d.getFov();
    if (fov < 20.0F) fov = 20.0F;
    if (fov > 110.0F) fov = 110.0F;
    if (fabsf(fov - r3d.getFov()) > 0.05F) r3d.setFov(fov);
  }
  // Ends the takeover: camera back to the game, presentation overlays off,
  // projection FOV restored.
  void release(ScriptContext& ctx) {
    ctx.cameraOverride = false;
    ctx.hidePlayer = false;
    ctx.barsStyle = 0;
    ctx.barsAmount = 0.0F;
    ctx.fadeAlpha = 0.0F;
    if (baseFov_ >= 0.0F && ctx.engine) {
      ctx.engine->renderer.core.renderer3D.setFov(baseFov_);
      baseFov_ = -1.0F;
    }
    cleanup_ = false;
  }

 public:
  void begin(int idx) {
    if (idx < 0 || idx >= kSeqCount) return;
    active_ = idx;
    time_ = 0.0F;
  }
  void end() {
    if (active_ >= 0) cleanup_ = true;
    active_ = -1;
  }
  int activeIndex() const { return active_; }

  void update(ScriptContext& ctx) override {
    if (active_ < 0 || active_ >= kSeqCount) {
      if (cleanup_) release(ctx);
      return;
    }
    const Seq& s = kSeqs[active_];
    // A skippable cutscene ends early on START.
    if (s.skippable && ctx.engine && ctx.engine->pad.getClicked().Start) {
      active_ = -1;
      release(ctx);
      return;
    }
    ctx.hidePlayer = s.hidePlayer != 0;
    for (int i = 0; i < s.trackCount; ++i) {
      const Track& tr = s.tracks[i];
      if (tr.scene != ctx.scene || tr.obj < 0 || tr.obj >= ctx.objectCount) continue;
      if (tr.keyCount <= 0) continue;
      RuntimeObject& o = ctx.objects[tr.obj];
      if (tr.chPos)
        for (int c = 0; c < 3; ++c)
          o.data.position[c] = sampleObj(tr.keys, tr.keyCount, time_, c, 0);
      if (tr.chRot)
        for (int c = 0; c < 3; ++c)
          o.data.rotation[c] = sampleObj(tr.keys, tr.keyCount, time_, c, 1);
      if (tr.chScale)
        for (int c = 0; c < 3; ++c)
          o.data.scale[c] = sampleObj(tr.keys, tr.keyCount, time_, c, 2);
      if (tr.chCol)
        for (int c = 0; c < 3; ++c)
          o.data.color[c] = sampleObj(tr.keys, tr.keyCount, time_, c, 3);
      if (tr.chVis) {
        int j = 0;
        while (j < tr.keyCount - 1 && time_ >= tr.keys[j + 1].t) ++j;
        o.visible = tr.keys[j].vis != 0;  // visibility steps between keys
      }
      o.dirty = true;
    }
    if (s.camEnabled && s.camKeyCount > 0) {
      const CamKey* k = s.camKeys;
      const int n = s.camKeyCount;
      const float t = time_;
      // One shot's eye/at/fov. Bound shots film from the Camera entity's
      // CURRENT pose (object tracks already ran this frame, so a keyframed
      // camera entity gives a dolly/crane move); the +Z lens direction math
      // mirrors seqCameraForward in src/sequence.hpp.
      auto shot = [&](int i, float eye[3], float at[3], float& fov) {
        const CamKey& c = k[i];
        if (c.camObj >= 0 && c.camScene == ctx.scene &&
            c.camObj < ctx.objectCount) {
          const RuntimeObject& o = ctx.objects[c.camObj];
          const float d2r = 3.14159265F / 180.0F;
          const float sx = sinf(o.data.rotation[0] * d2r);
          const float cx = cosf(o.data.rotation[0] * d2r);
          const float sy = sinf(o.data.rotation[1] * d2r);
          const float cy = cosf(o.data.rotation[1] * d2r);
          const float sz = sinf(o.data.rotation[2] * d2r);
          const float cz = cosf(o.data.rotation[2] * d2r);
          const float fwd[3] = {cx * sy * cz + sx * sz,
                                cx * sy * sz - sx * cz, cx * cy};
          for (int j = 0; j < 3; ++j) {
            eye[j] = o.data.position[j];
            at[j] = o.data.position[j] + fwd[j];
          }
        } else {
          for (int j = 0; j < 3; ++j) {
            eye[j] = c.eye[j];
            at[j] = c.at[j];
          }
        }
        fov = c.fov;
      };
      int i = 0;
      while (i < n - 1 && t >= k[i + 1].t) ++i;
      float eye[3], at[3], fov;
      shot(i, eye, at, fov);
      float shake = k[i].shake;
      if (t > k[i].t && i < n - 1) {
        const float span = k[i + 1].t - k[i].t;
        const float u = span > 1e-6F ? (t - k[i].t) / span : 0.0F;
        const float w = seqEase(k[i].ease, u);
        float eye1[3], at1[3], fov1;
        shot(i + 1, eye1, at1, fov1);
        for (int j = 0; j < 3; ++j) {
          eye[j] += (eye1[j] - eye[j]) * w;
          at[j] += (at1[j] - at[j]) * w;
        }
        fov += (fov1 - fov) * w;
        shake += (k[i + 1].shake - shake) * w;
      }
      if (shake > 0.0F) {
        // handheld noise - mirrors seqShakeOffset in src/sequence.hpp
        const float ox =
            shake * (0.6F * sinf(t * 23.7F) + 0.4F * sinf(t * 7.3F + 1.7F));
        const float oy =
            shake * (0.6F * sinf(t * 19.1F + 0.9F) + 0.4F * sinf(t * 9.7F));
        const float oz = shake * 0.3F * sinf(t * 13.9F + 2.3F);
        eye[0] += ox, eye[1] += oy, eye[2] += oz;
        at[0] += ox, at[1] += oy, at[2] += oz;
      }
      ctx.cameraOverride = true;
      ctx.cameraEye.x = eye[0];
      ctx.cameraEye.y = eye[1];
      ctx.cameraEye.z = eye[2];
      ctx.cameraAt.x = at[0];
      ctx.cameraAt.y = at[1];
      ctx.cameraAt.z = at[2];
      applyFov(ctx, fov);
    }
    // Presentation: bars slide in/out over the sequence's reveal times
    // (mirrors seqBarsAmount; 0 = instant), fades ramp from/to black (mirrors
    // seqFadeAlpha).
    if (s.bars > 0) {
      float a = 1.0F;
      if (s.barsSlideIn > 0.0F && time_ < s.barsSlideIn)
        a = time_ / s.barsSlideIn;
      if (s.barsSlideOut > 0.0F) {
        const float left = s.duration - time_;
        if (left < s.barsSlideOut && left / s.barsSlideOut < a)
          a = left / s.barsSlideOut;
      }
      ctx.barsStyle = s.bars;
      ctx.barsAmount = a < 0.0F ? 0.0F : (a > 1.0F ? 1.0F : a);
    } else {
      ctx.barsStyle = 0;
      ctx.barsAmount = 0.0F;
    }
    {
      float fade = 0.0F;
      if (s.fadeIn > 0.0F && time_ < s.fadeIn) fade = 1.0F - time_ / s.fadeIn;
      if (s.fadeOut > 0.0F) {
        const float o = 1.0F - (s.duration - time_) / s.fadeOut;
        if (o > fade) fade = o;
      }
      ctx.fadeAlpha = fade < 0.0F ? 0.0F : (fade > 1.0F ? 1.0F : fade);
    }
    time_ += g_frameDt;
    if (time_ >= s.duration) {
      if (s.loop) {
        time_ -= s.duration;
        if (time_ < 0.0F) time_ = 0.0F;
      } else {
        active_ = -1;
        cleanup_ = true;  // release() on the next update
      }
    }
  }
};

SequenceDirector g_seqDirector;
static const bool g_seqRegistered = []() {
  getScripts().push_back(&g_seqDirector);
  return true;
}();

}  // namespace

namespace sequences {
void play(int index) { g_seqDirector.begin(index); }
void stop() { g_seqDirector.end(); }

// Solid black quads: the widescreen mask edges (coverage from the active
// sequence's style scaled by the slide envelope) and the fade overlay. One
// stretched 8x8 opaque-black sprite (res/hud/seq-black.png) reused for every
// quad; the sprite alpha carries the fade (128 = opaque on the GS).
void renderOverlay(Tyra::Engine* engine, const ScriptContext& ctx) {
  if (ctx.barsAmount <= 0.0F && ctx.fadeAlpha <= 0.0F) return;
  static Tyra::Sprite quad;
  static bool ready = false;
  if (!ready) {
    quad.mode = Tyra::SpriteMode::MODE_STRETCH;
    auto* tex = engine->renderer.getTextureRepository().add(
        Tyra::FileUtils::fromCwd("hud/seq-black.png"));
    tex->addLink(quad.id);
    ready = true;
  }
  const auto& scr = engine->renderer.core.getSettings();
  const float W = scr.getWidth();
  const float H = scr.getHeight();
  auto fill = [&](float x, float y, float w, float h, float alpha) {
    if (w < 1.0F || h < 1.0F || alpha <= 0.0F) return;
    quad.position = Tyra::Vec2(x, y);
    quad.size = Tyra::Vec2(w, h);
    quad.color.a = 128.0F * (alpha > 1.0F ? 1.0F : alpha);
    engine->renderer.renderer2D.render(quad);
  };
  const int idx = g_seqDirector.activeIndex();
  if (ctx.barsAmount > 0.0F && idx >= 0) {
    const float tb = kSeqs[idx].barTB * ctx.barsAmount * H;
    const float lr = kSeqs[idx].barLR * ctx.barsAmount * W;
    fill(0.0F, 0.0F, W, tb, 1.0F);
    fill(0.0F, H - tb, W, tb, 1.0F);
    fill(0.0F, 0.0F, lr, H, 1.0F);
    fill(W - lr, 0.0F, lr, H, 1.0F);
  }
  if (ctx.fadeAlpha > 0.0F) fill(0.0F, 0.0F, W, H, ctx.fadeAlpha);
}
}  // namespace sequences

)";
    out << "}  // namespace " << ns << "\n";
    return out.str();
}

// One Display Text node's runtime slot. The slot index is the node's position
// in a fixed (scene, object, node) walk - both fontDataHeader and
// flowGraphScript derive it from dynTextSlots(), so the generated tables and
// the generated script always agree on who owns which slot.
struct DynTextSlot {
    int scene = 0;
    int ownerIdx = 0;
    int nodeId = 0;
    int fontSlot = 0;  // index into FONTS (not Project::fonts)
    float x = 0.5f, y = 0.5f, size = 16.0f;
};

static std::vector<DynTextSlot> dynTextSlots(const Project& p) {
    const std::vector<int> atlasFonts = p.atlasFontIndices();
    std::vector<DynTextSlot> out;
    for (size_t si = 0; si < p.scenes.size(); ++si) {
        const auto& objs = p.scenes[si].objects;
        for (size_t oi = 0; oi < objs.size(); ++oi)
            for (const FlowNode& n : objs[oi].flowGraph.nodes) {
                if (n.type != "DisplayText") continue;
                DynTextSlot s;
                s.scene = (int)si;
                s.ownerIdx = (int)oi;
                s.nodeId = n.id;
                // n.str names a Project::fonts entry; FONTS only holds the ones
                // that actually got an atlas, so remap through atlasFonts.
                const GameFont* gf = p.findFont(n.str);
                const int projIdx = gf ? (int)(gf - p.fonts.data()) : 0;
                for (size_t k = 0; k < atlasFonts.size(); ++k)
                    if (atlasFonts[k] == projIdx) s.fontSlot = (int)k;
                s.x = n.num[0];
                s.y = n.num[1];
                s.size = n.num[2] > 0.5f ? n.num[2] : 16.0f;
                out.push_back(s);
            }
    }
    return out;
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
    auto ambienceIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.ambiencePresets.size(); ++i)
            if (p.ambiencePresets[i].name == name) return (int)i;
        return -1;
    };
    auto sequenceIndexOf = [&](const std::string& name) {
        for (size_t i = 0; i < p.sequences.size(); ++i)
            if (p.sequences[i].name == name) return (int)i;
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
    auto hudTextIndex = [&](const std::string& name) {
        for (size_t i = 0; i < p.hudTexts.size(); ++i)
            if (p.hudTexts[i].name == name) return (int)i;
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

    // Spawn Object nodes: each gets a global handle slot holding the index
    // of the clone it spawned last (-1 = none). Handles are runtime data -
    // the one object reference that cannot resolve statically - and reset
    // with their script's scene-generation state.
    std::vector<std::string> spawnSlots;  // "si:owner:node" in slot order
    for (size_t si = 0; si < p.scenes.size(); ++si)
        for (size_t oi = 0; oi < p.scenes[si].objects.size(); ++oi)
            for (const FlowNode& n : p.scenes[si].objects[oi].flowGraph.nodes)
                if (n.type == "SpawnObject")
                    spawnSlots.push_back(std::to_string(si) + ":" +
                                         std::to_string(oi) + ":" +
                                         std::to_string(n.id));
    auto spawnSlotOf = [&](size_t si, size_t oi, int nodeId) {
        const std::string key = std::to_string(si) + ":" + std::to_string(oi) +
                                ":" + std::to_string(nodeId);
        for (size_t i = 0; i < spawnSlots.size(); ++i)
            if (spawnSlots[i] == key) return (int)i;
        return -1;
    };

    // Display Text nodes: one runtime slot each, in the same order
    // fontDataHeader lays out DYN_TEXTS.
    const std::vector<DynTextSlot> dynSlots = dynTextSlots(p);
    auto dynTextSlotOf = [&](size_t si, size_t oi, int nodeId) {
        for (size_t i = 0; i < dynSlots.size(); ++i)
            if (dynSlots[i].scene == (int)si && dynSlots[i].ownerIdx == (int)oi &&
                dynSlots[i].nodeId == nodeId)
                return (int)i;
        return -1;
    };

    // Text plane (Log inputs, Convert nodes, save texts) only when used -
    // keeps graphs without text nodes free of the string helpers.
    bool anyTextNode = false;
    bool anyRaycast = false;
    bool anyDynText = false;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects)
            for (const FlowNode& n : o.flowGraph.nodes) {
                if (const FlowNodeType* t = flowNodeType(n.type))
                    anyTextNode |= (t->textIn || t->textOut);
                anyRaycast |= (n.type == "Raycast");
                anyDynText |= (n.type == "DisplayText");
            }
    const bool anyNav = anyNavAiNode(p);

    std::ostringstream out;
    out << "// Generated by TyraX from the per-object Flow Graphs. Do not\n"
           "// edit - regenerated on every build. Edit the graphs in the editor.\n"
           "#include \"scripts/script.hpp\"\n"
           "#include \"scripts/sequences.gen.hpp\"  // Play/Stop Sequence nodes\n"
           "#include \"scripts/flow_nodes.hpp\"  // custom-node C++ bodies\n";
    if (anyRaycast)
        out << "#include \"terrain_heights.gen.hpp\"  // Raycast vs terrain\n";
    if (anyNav)
        out << "#include \"scripts/navigation.gen.hpp\"  // AI nodes "
               "(Patrol/Chase/Flee/On Player Seen)\n";
    out << "\n"
           "#include <math.h>\n"
           "#include <stdio.h>\n\n"
           "#include <string>\n\n"
           "namespace "
        << ns << " {\n";

    if (anyRaycast) {
        out << R"(
// Raycast node: the nearest thing the player's view ray hits within maxDist -
// object bounding spheres (same marker-type skip list as the USE picker) and
// the terrain heightmap. hitObj = the hit object index (-1 = none/terrain);
// hitPos = the hit point (the ray's end when nothing was hit).
static void flowRaycast(ScriptContext& ctx, float maxDist, int* hitObj,
                        float* hitPos) {
  const float ox = ctx.playerPosition.x;
  const float oy = ctx.playerPosition.y;
  const float oz = ctx.playerPosition.z;
  float dx = ctx.playerLook.x, dy = ctx.playerLook.y, dz = ctx.playerLook.z;
  const float dl = sqrtf(dx * dx + dy * dy + dz * dz);
  *hitObj = -1;
  if (dl < 0.0001F) {
    hitPos[0] = ox; hitPos[1] = oy; hitPos[2] = oz;
    return;
  }
  dx /= dl; dy /= dl; dz /= dl;
  float best = maxDist;
  const int player = PLAYER_INDEXES[ctx.scene];
  for (int i = 0; i < ctx.objectCount; ++i) {
    const RuntimeObject& o = ctx.objects[i];
    if (!o.active || !o.visible || i == player) continue;
    const int ty = o.data.type;
    if (ty == 4 || ty == 6 || ty == 7 || ty == 8 || ty == 9 || ty == 11 ||
        ty == 14)
      continue;  // markers/emitters, not geometry
    // bounding sphere: half the largest scale axis (matches the USE picker)
    float half = o.data.scale[0];
    if (o.data.scale[1] > half) half = o.data.scale[1];
    if (o.data.scale[2] > half) half = o.data.scale[2];
    half *= 0.5F;
    const float cx = o.data.position[0] - ox;
    const float cy = o.data.position[1] - oy;
    const float cz = o.data.position[2] - oz;
    const float tca = cx * dx + cy * dy + cz * dz;  // closest approach
    if (tca < 0.0F || tca - half > best) continue;
    const float d2 = cx * cx + cy * cy + cz * cz - tca * tca;
    if (d2 > half * half) continue;
    float t = tca - sqrtf(half * half - d2);
    if (t < 0.0F) t = 0.0F;  // ray starts inside the sphere
    if (t < best) {
      best = t;
      *hitObj = i;
    }
  }
  // Terrain: fixed-step march, then a short bisection to tighten the hit.
  // Only a terrain hit closer than the best object hit wins the ray.
  float prev = 0.0F;
  for (float t = 0.5F; t <= best; t += 0.5F) {
    if (oy + dy * t <=
        terrainHeightAtScene(ctx.scene, ox + dx * t, oz + dz * t)) {
      float lo = prev, hi = t;
      for (int k = 0; k < 8; ++k) {
        const float mid = (lo + hi) * 0.5F;
        if (oy + dy * mid <=
            terrainHeightAtScene(ctx.scene, ox + dx * mid, oz + dz * mid))
          hi = mid;
        else
          lo = mid;
      }
      best = hi;
      *hitObj = -1;  // the terrain stopped the ray first
      break;
    }
    prev = t;
  }
  hitPos[0] = ox + dx * best;
  hitPos[1] = oy + dy * best;
  hitPos[2] = oz + dz * best;
}
)";
    }

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

    if (anyDynText) {
        out << "\n// Display Text: copy a runtime string into its slot's buffer\n"
               "// (silently truncated - the slot is a fixed DYN_TEXT_LEN).\n"
               "static inline void flowSetDynText(ScriptContext& ctx, int slot,\n"
               "                                  const std::string& s) {\n"
               "  if (!ctx.dynTextBuf || slot < 0 || slot >= ctx.dynTextCount) return;\n"
               "  char* dst = ctx.dynTextBuf + slot * ctx.dynTextLen;\n"
               "  int n = (int)s.size();\n"
               "  if (n > ctx.dynTextLen - 1) n = ctx.dynTextLen - 1;\n"
               "  for (int i = 0; i < n; ++i) dst[i] = s[i];\n"
               "  dst[n] = '\\0';\n"
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

    if (!spawnSlots.empty()) {
        out << "\n// Spawn Object handles: runtimeObjects index of each node's\n"
               "// latest clone (-1 = none). Reset with the owning script's\n"
               "// scene-generation state.\n"
               "static int flowSpawned["
            << spawnSlots.size() << "] = {";
        for (size_t i = 0; i < spawnSlots.size(); ++i) out << (i ? ", -1" : "-1");
        out << "};\n";
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
    // Streaming layers are per scene - names resolve within the owning scene
    auto layerIndexOf = [&](const std::string& name) {
        const auto& layers = p.scenes[si].layers;
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i].name == name) return (int)i;
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
        // Runtime-only references (Spawn Object clones, a custom node's object
        // output) are handled by targetExpr below, which callers check first.
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

        // Dynamic counterpart of resolveTarget: when the object-link chain
        // hits a runtime object source - a Spawn Object node's CLONE, or a
        // custom node's object output (a pick / raycast result) - the reference
        // is known only at runtime. Returns the int-lvalue expression (-1 = no
        // object), or "" when the target is static (use resolveTarget then).
        auto targetExpr = [&](const FlowNode& n) -> std::string {
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
                if (src->type == "SpawnObject") {
                    const int k = spawnSlotOf(si, ownerIdx, src->id);
                    if (k >= 0) return "flowSpawned[" + std::to_string(k) + "]";
                    return "";
                }
                // A custom node's (or Raycast's) object output is a runtime
                // latch var, and is authoritative (unrelated to the node's
                // own inputs).
                if (const FlowNodeType* st = flowNodeType(src->type);
                    st && st->idOut &&
                    (flowCustomNode(src->type) || src->type == "Raycast"))
                    return "objOut" + std::to_string(src->id);
                cur = src;
            }
            return "";
        };

        // A Get Position node with its exec input wired runs as a sampling
        // action: it latches the target's position into its posOut member
        // when the exec fires (see the registry comment in flowgraph.hpp).
        // Unwired, it keeps the original pure behavior - consumers read the
        // target's position live.
        auto getPosLatched = [&](const FlowNode& n) {
            if (n.type != "GetPosition") return false;
            for (const FlowLink& l : fg.links)
                if (l.kind == FlowLinkExec && l.toNode == n.id) return true;
            return false;
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
            // A custom node's (or Raycast's / an exec-wired Get Position's)
            // runtime position output (latched member).
            if (const FlowNodeType* t = flowNodeType(n.type);
                t && t->posOut &&
                (flowCustomNode(n.type) || n.type == "Raycast" ||
                 getPosLatched(n))) {
                const std::string base = "posOut" + std::to_string(n.id) + "[";
                return {base + "0]", base + "1]", base + "2]"};
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
            // Runtime target (spawned clone / custom object output): read the
            // position through the handle, guarded per component.
            const std::string dyn = targetExpr(n);
            if (!dyn.empty()) {
                auto comp = [&](int a) {
                    return "(" + dyn + " >= 0 ? ctx.objects[" + dyn +
                           "].data.position[" + std::to_string(a) + "] : 0.0F)";
                };
                return {comp(0), comp(1), comp(2)};
            }
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
            // A custom node's runtime bool output (latched member).
            if (const FlowNodeType* t = flowNodeType(n.type);
                t && t->boolOut && flowCustomNode(n.type))
                return "boolOut" + std::to_string(n.id);
            if (n.type == "IsVisible") {
                const std::string dyn = targetExpr(n);
                if (!dyn.empty())
                    return "(" + dyn + " >= 0 && ctx.objects[" + dyn +
                           "].visible)";
                const int idx = resolveTarget(n);
                if (idx < 0) return "false";
                return "(ctx.objects[" + std::to_string(idx) + "].visible)";
            }
            if (n.type == "OnPlayerSeen") {
                // the live "seen right now" condition (the exec output fires
                // on its rising edge in the trigger scan below)
                const std::string args = ", " + floatLit(n.num[0]) + ", " +
                                         floatLit(n.num[1]) + ", " +
                                         (n.num[2] != 0.0f ? "1" : "0") + ")";
                const std::string dyn = targetExpr(n);
                if (!dyn.empty())
                    return "(" + dyn + " >= 0 && navPlayerSeen(ctx, " + dyn +
                           args + ")";
                const int idx = resolveTarget(n);
                if (idx < 0) return "false";
                return "navPlayerSeen(ctx, " + std::to_string(idx) + args;
            }
            if (n.type == "IsLayerLoaded") {
                const int li = layerIndexOf(n.str);
                if (li < 0) return "false";  // unknown layer name
                return "(ctx.layerState && ctx.layerState[" + std::to_string(li) +
                       "] == 2)";
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
            // A custom node's runtime text output (latched member).
            if (const FlowNodeType* t = flowNodeType(n.type);
                t && t->textOut && flowCustomNode(n.type))
                return "std::string(textOut" + std::to_string(n.id) + ")";
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

        // Display Text's string: the node's own prefix (str2) followed by every
        // wired text input, concatenated. Same shape as Log Message, minus the
        // spacing - "Score: " + a Get Save Value reads as one label.
        auto dynTextExpr = [&](const FlowNode& n) -> std::string {
            const auto es = textInputs(n);
            std::string s;
            if (!n.str2.empty() || es.empty())
                s = "std::string(\"" + escapeCString(n.str2) + "\")";
            for (const std::string& e : es) s += (s.empty() ? "" : " + ") + e;
            return s;
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

        // action node -> inline statements. `pin` is the exec input the link
        // fired (FlowLink::toPin): a merged node (Set Object Visible's
        // show/hide/toggle) switches its body on it, every other node ignores
        // it because it only has pin 0.
        auto actionCode = [&](const FlowNode& n, const std::string& pad,
                              int pin) -> std::string {
            std::ostringstream c;
            // dyn: the target is a runtime handle (Spawn Object clone or a
            // custom node's object output); the whole action is then wrapped in
            // a handle-validity guard below.
            const std::string dyn = targetExpr(n);
            const int idx = dyn.empty() ? resolveTarget(n) : -1;
            const bool needsObject =
                flowNodeType(n.type)->strKind == FlowParamKind::ObjectName;
            if (needsObject && dyn.empty() && idx < 0) {
                c << pad << "// node " << n.id << " (" << n.type << "): unknown object '"
                  << n.str << "'\n";
                return c.str();
            }
            const std::string objIdx = dyn.empty() ? std::to_string(idx) : dyn;
            std::string obj = "ctx.objects[" + objIdx + "]";
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
            } else if (n.type == "SetLayerLoaded") {
                const int li = layerIndexOf(n.str);
                if (li < 0) {
                    c << pad << "// node " << n.id << " (" << n.type
                      << "): unknown layer '" << n.str << "'\n";
                } else {
                    c << pad << "if (ctx.layerRequest && " << li
                      << " < ctx.layerCount) ctx.layerRequest[" << li
                      << "] = " << (pin == 1 ? 0 : 1) << ";  // \""
                      << n.str << "\"\n";
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
            } else if (n.type == "SetAmbience") {
                // Runtime repaint of the sky from a named preset (resolved to a
                // literal here). Lighting/fog are baked per-scene at build, so
                // only the sky changes live - like the Set Sky Color node.
                const int ai = ambienceIndexOf(n.str);
                if (n.str.empty() || ai < 0) {
                    c << pad << "// node " << n.id
                      << " (SetAmbience): unknown preset '" << n.str << "'\n";
                } else {
                    const AmbiencePreset& a = p.ambiencePresets[ai];
                    c << pad << "ctx.skyColor = Tyra::Color("
                      << floatLit(a.skyColor[0] * 255.0f) << ", "
                      << floatLit(a.skyColor[1] * 255.0f) << ", "
                      << floatLit(a.skyColor[2] * 255.0f) << ");  // \"" << n.str
                      << "\"\n";
                }
            } else if (n.type == "PlaySequence") {
                const int si = sequenceIndexOf(n.str);
                if (n.str.empty() || si < 0) {
                    c << pad << "// node " << n.id
                      << " (PlaySequence): unknown sequence '" << n.str << "'\n";
                } else {
                    c << pad << "sequences::play(" << si << ");  // \"" << n.str
                      << "\"\n";
                }
            } else if (n.type == "StopSequence") {
                c << pad << "sequences::stop();\n";
            } else if (n.type == "SetObjectVisible") {
                if (pin == 1)
                    c << pad << obj << ".visible = false;\n";
                else if (pin == 2)
                    c << pad << obj << ".visible = !" << obj << ".visible;\n";
                else
                    c << pad << obj << ".visible = true;\n";
            } else if (n.type == "MoveObjectBy") {
                for (int a = 0; a < 3; ++a)
                    if (n.num[a] != 0.0f)
                        c << pad << obj << ".data.position[" << a
                          << "] += " << floatLit(n.num[a]) << ";\n";
                c << pad << obj << ".restFrames = 0;\n";  // wake physics bodies
                c << pad << obj << ".dirty = true;\n";
            } else if (n.type == "PushObject") {
                // params are units/s; velocities are per-frame displacements.
                // Velocity-only change: waking is enough, the physics pass
                // moves the body (no dirty - that would drop its fast path).
                const char* velField[3] = {".velocityX", ".velocityY",
                                           ".velocityZ"};
                for (int a = 0; a < 3; ++a)
                    if (n.num[a] != 0.0f)
                        c << pad << obj << velField[a]
                          << " += " << floatLit(n.num[a]) << " * g_frameDt;\n";
                c << pad << obj << ".restFrames = 0;\n";
            } else if (n.type == "SetObjectColor") {
                for (int a = 0; a < 3; ++a)
                    c << pad << obj << ".data.color[" << a
                      << "] = " << floatLit(n.num[a]) << ";\n";
                c << pad << obj << ".dirty = true;\n";
            } else if (n.type == "SetPosition") {
                const auto e = posExpr(n);
                for (int a = 0; a < 3; ++a)
                    c << pad << obj << ".data.position[" << a << "] = " << e[a] << ";\n";
                c << pad << obj << ".restFrames = 0;\n";  // wake physics bodies
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
            } else if (n.type == "SetFlashlight") {
                c << pad << "ctx.flashlight = " << (n.num[0] != 0.0f ? "1" : "0")
                  << ";\n";
            } else if (n.type == "SetFog") {
                c << pad << "ctx.fog = " << (n.num[0] != 0.0f ? "1" : "0") << ";\n";
            } else if (n.type == "SetParticles") {
                c << pad << "ctx.particles = " << (n.num[0] != 0.0f ? "1" : "0")
                  << ";\n";
            } else if (n.type == "SetBloom" || n.type == "SetGrain") {
                int v = (int)(n.num[0] * 128.0f + 0.5f);
                if (v < 0) v = 0;
                if (v > 128) v = 128;
                c << pad << "ctx." << (n.type == "SetBloom" ? "bloom" : "grain")
                  << " = " << v << ";\n";
            } else if (n.type == "SetDof") {
                // Mode (num[3]): 0 = set the custom params, 1 = off,
                // 2 = restore the scene's authored setting (-2 request).
                const int mode = (int)n.num[3];
                if (mode == 1) {
                    c << pad << "ctx.dof = 0;\n";
                } else if (mode == 2) {
                    c << pad << "ctx.dof = -2;  // scene setting\n";
                } else {
                    int v = (int)(n.num[2] * 128.0f + 0.5f);
                    if (v < 0) v = 0;
                    if (v > 128) v = 128;
                    // A wired position turns Focus into the live distance
                    // from the player to that point.
                    bool posWired = false;
                    for (const FlowLink& l : fg.links)
                        posWired |= (l.kind == FlowLinkPos && l.toNode == n.id);
                    if (posWired && v > 0) {
                        const auto e = posExpr(n);
                        c << pad << "{\n"
                          << pad << "  const float fdx = " << e[0]
                          << " - ctx.playerPosition.x;\n"
                          << pad << "  const float fdy = " << e[1]
                          << " - ctx.playerPosition.y;\n"
                          << pad << "  const float fdz = " << e[2]
                          << " - ctx.playerPosition.z;\n"
                          << pad
                          << "  ctx.dofFocus = sqrtf(fdx * fdx + fdy * fdy + "
                             "fdz * fdz);\n"
                          << pad << "}\n";
                    } else {
                        c << pad << "ctx.dofFocus = "
                          << floatLit(n.num[0] < 0.0f ? 0.0f : n.num[0])
                          << ";\n";
                    }
                    c << pad << "ctx.dofRange = "
                      << floatLit(n.num[1] < 0.1f ? 0.1f : n.num[1]) << ";\n";
                    c << pad << "ctx.dof = " << v << ";\n";
                }
            } else if (n.type == "Raycast") {
                // Latch the results into this node's runtime members; the
                // "after" exec fires right after (emitExec), so downstream
                // actions read fresh values.
                const float md = n.num[0] > 0.001f ? n.num[0] : 100.0f;
                c << pad << "flowRaycast(ctx, " << floatLit(md) << ", &objOut"
                  << n.id << ", posOut" << n.id << ");\n";
            } else if (n.type == "GetPosition") {
                // Only reached when its exec input is wired (emitExec): latch
                // the target's position now, so downstream consumers keep
                // "where it was when the exec fired" even after the target
                // moves. posExpr hands them the latched member; an unwired
                // Get Position never runs and stays a live data source.
                const std::string id = std::to_string(n.id);
                const std::string dyn = targetExpr(n);
                if (!dyn.empty()) {
                    c << pad << "if (" << dyn << " >= 0) {\n";
                    for (int a = 0; a < 3; ++a)
                        c << pad << "  posOut" << id << "[" << a
                          << "] = ctx.objects[" << dyn << "].data.position[" << a
                          << "];\n";
                    c << pad << "}\n";
                } else {
                    const int idx = resolveTarget(n);
                    if (idx < 0) {
                        c << pad << "// node " << id
                          << " (GetPosition): unknown object '" << n.str << "'\n";
                    } else {
                        for (int a = 0; a < 3; ++a)
                            c << pad << "posOut" << id << "[" << a
                              << "] = ctx.objects[" << idx << "].data.position["
                              << a << "];\n";
                    }
                }
            } else if (n.type == "SetStickCurve") {
                // Stick: 0 left, 1 right, 2 both. Curve: 0 Linear / 1 Exp /
                // 2 S-Curve. Exponent clamped to >= 1 (only shapes curves 1/2).
                int stick = (int)n.num[0];
                stick = stick < 0 ? 0 : stick > 2 ? 2 : stick;
                int curve = (int)n.num[1];
                curve = curve < 0 ? 0 : curve > 2 ? 2 : curve;
                float e = n.num[2] < 1.0f ? 1.0f : n.num[2];
                if (stick != 1) {  // left or both
                    c << pad << "ctx.stickCurveL = " << curve << ";\n";
                    c << pad << "ctx.stickExpL = " << floatLit(e) << ";\n";
                }
                if (stick != 0) {  // right or both
                    c << pad << "ctx.stickCurveR = " << curve << ";\n";
                    c << pad << "ctx.stickExpR = " << floatLit(e) << ";\n";
                }
            } else if (n.type == "VibratePad") {
                // Big: heavy motor 0..1 -> 0..255. Small: on/off buzz motor.
                // Seconds > 0 auto-stops (0 = until the next Vibrate Pad).
                float big = n.num[0] < 0.0f ? 0.0f : n.num[0] > 1.0f ? 1.0f
                                                                     : n.num[0];
                float secs = n.num[2] < 0.0f ? 0.0f : n.num[2];
                c << pad << "ctx.rumble = " << (int)(big * 255.0f + 0.5f)
                  << ";\n";
                c << pad << "ctx.rumbleSmall = "
                  << (n.num[1] != 0.0f ? 1 : 0) << ";\n";
                c << pad << "ctx.rumbleSec = " << floatLit(secs) << ";\n";
            } else if (n.type == "SetDisplayMode") {
                int mode = (int)n.num[0];
                mode = mode < 0 ? 0 : mode > 3 ? 3 : mode;
                float confirm = n.num[1] < 0.0f ? 0.0f : n.num[1];
                c << pad << "ctx.requestDisplayMode = " << mode << ";\n";
                c << pad << "ctx.displayConfirmSec = " << floatLit(confirm)
                  << ";\n";
            } else if (n.type == "SetWidescreen") {
                c << pad << "ctx.widescreen = " << (n.num[0] != 0.0f ? "1" : "0")
                  << ";\n";
            } else if (n.type == "SetHudVisible") {
                if (pin == 1)
                    c << pad << "ctx.hudVisible = false;\n";
                else if (pin == 2)
                    c << pad << "ctx.hudVisible = !ctx.hudVisible;\n";
                else
                    c << pad << "ctx.hudVisible = true;\n";
            } else if (n.type == "SetTextVisible") {
                const int ti = hudTextIndex(n.str);
                if (ti < 0) {
                    // Texts removed from the project just stop being shown.
                    c << pad << "// node " << n.id << " (" << n.type
                      << "): unknown text '" << n.str << "'\n";
                } else if (pin == 1) {
                    c << pad << "ctx.textRequest[" << ti << "] = 0;  // \"" << n.str
                      << "\"\n";
                } else {
                    float secs = n.num[0];
                    if (secs < 0.0f) secs = 0.0f;
                    c << pad << "ctx.textRequest[" << ti << "] = 1;  // \"" << n.str
                      << "\"\n"
                      << pad << "ctx.textDuration[" << ti << "] = " << floatLit(secs)
                      << ";\n";
                }
            } else if (n.type == "DisplayText") {
                const int slot = dynTextSlotOf(si, ownerIdx, n.id);
                if (slot < 0) {
                    c << pad << "// node " << n.id << " (DisplayText): no slot\n";
                } else if (pin == 1) {
                    c << pad << "ctx.dynTextRequest[" << slot << "] = 0;\n";
                } else {
                    float secs = n.num[3];
                    if (secs < 0.0f) secs = 0.0f;
                    c << pad << "ctx.dynTextRequest[" << slot << "] = 1;\n"
                      << pad << "ctx.dynTextDuration[" << slot << "] = "
                      << floatLit(secs) << ";\n"
                      // Fill the buffer now as well as in the per-frame refresh,
                      // so the first frame shows the right string instead of a
                      // stale one.
                      << pad << "flowSetDynText(ctx, " << slot << ", "
                      << dynTextExpr(n) << ");\n";
                }
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
                    c << pad << "  ctx.engine->audio.adpcm.setVolume(" << vol
                      << " * ctx.sfxVolume / 100, ch);\n"
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
            } else if (n.type == "Animation") {
                if (pin == 1) {
                    c << pad << "stopAnimation(ctx, " << objIdx << ");\n";
                } else {
                    // str = clip name ("" = the model's first clip); Loop 1 =
                    // loop, Speed <= 0 = the authored default (1.0); Fade =
                    // crossfade seconds (0 = instant switch)
                    const float speed = n.num[1] > 0.001f ? n.num[1] : 1.0f;
                    const float fade = n.num[2] > 0.0f ? n.num[2] : 0.0f;
                    c << pad << "playAnimation(ctx, " << objIdx << ", \""
                      << escapeCString(n.str) << "\", "
                      << (n.num[0] != 0.0f ? "true" : "false") << ", "
                      << floatLit(speed) << ", " << floatLit(fade) << ");\n";
                }
            } else if (n.type == "SpawnObject") {
                // objIdx = the TEMPLATE (link > name > self); the clone lands
                // in this node's handle slot. A linked position beats the
                // template's own; Yaw is the clone's Y rotation in degrees.
                const int k = spawnSlotOf(si, ownerIdx, n.id);
                const auto e = posExpr(n);
                c << pad << "flowSpawned[" << k
                  << "] = ctx.spawnObject ? ctx.spawnObject(" << objIdx << ", "
                  << e[0] << ", " << e[1] << ", " << e[2] << ", "
                  << floatLit(n.num[0]) << ") : -1;\n";
            } else if (n.type == "DespawnObject") {
                c << pad << "if (ctx.despawnObject) ctx.despawnObject(" << objIdx
                  << ");\n";
                if (!dyn.empty())  // the handle no longer points at a clone
                    c << pad << dyn << " = -1;\n";
            } else if (n.type == "PatrolWaypoints") {
                // Waypoints resolve at codegen: objects named <prefix><n>, in
                // natural order. str = the prefix; the target NPC comes from
                // the object link (or self), like Play Animation.
                const auto wps = waypointIndices(sceneObjs, n.str);
                if (wps.empty()) {
                    c << pad << "// node " << n.id
                      << " (PatrolWaypoints): no objects named '" << n.str
                      << "<n>'\n";
                } else {
                    c << pad << "{\n" << pad << "  static const int navWps"
                      << n.id << "[] = {";
                    for (size_t i = 0; i < wps.size(); ++i) {
                        c << (i ? ", " : "") << wps[i];
                    }
                    c << "};  //";
                    for (int wi : wps) c << " " << sceneObjs[wi].name;
                    c << "\n"
                      << pad << "  navPatrol(ctx, " << objIdx << ", navWps"
                      << n.id << ", " << wps.size() << ", " << floatLit(n.num[0])
                      << ", " << floatLit(n.num[1] > 0.0f ? n.num[1] : 0.0f)
                      << ", " << (n.num[2] != 0.0f ? 1 : 0) << ");\n"
                      << pad << "}\n";
                }
            } else if (n.type == "ChasePlayer") {
                c << pad << "navChase(ctx, " << objIdx << ", "
                  << floatLit(n.num[0]) << ", " << floatLit(n.num[1]) << ", "
                  << floatLit(n.num[2]) << ");\n";
            } else if (n.type == "FleePlayer") {
                c << pad << "navFlee(ctx, " << objIdx << ", "
                  << floatLit(n.num[0]) << ", " << floatLit(n.num[1]) << ");\n";
            } else if (n.type == "StopAi") {
                c << pad << "navStop(ctx, " << objIdx << ");\n";
            } else if (const CustomFlowNode* cn = flowCustomNode(n.type)) {
                const FlowNodeType* t = &cn->type;
                c << pad << "// node " << n.id << " (" << n.type << ")\n";
                if (!cn->callFn.empty()) {
                    // C++-backed node: build a FlowNodeIO from the resolved
                    // inputs, call the user function (flow_nodes.hpp), then
                    // latch its outputs into this node's runtime members so
                    // downstream nodes (custom or built-in) can read them.
                    const std::string p2 = pad + "  ";
                    c << pad << "{\n";
                    c << p2 << "const float num[4] = {" << floatLit(n.num[0]) << ", "
                      << floatLit(n.num[1]) << ", " << floatLit(n.num[2]) << ", "
                      << floatLit(n.num[3]) << "};\n";
                    c << p2 << "FlowNodeIO io;\n";
                    c << p2 << "io.self = " << (int)ownerIdx << ";\n";
                    c << p2 << "io.object = " << objIdx << ";\n";
                    c << p2 << "io.num = num;\n";
                    c << p2 << "io.str = \"" << escapeCString(n.str) << "\";\n";
                    if (t->posIn) {
                        const auto e = posExpr(n);
                        c << p2 << "io.position = Tyra::Vec4(" << e[0] << ", " << e[1]
                          << ", " << e[2] << ");\n";
                    }
                    if (t->boolIn) {
                        std::string b = boolInputsOr(n);
                        if (b.empty()) b = "false";
                        c << p2 << "io.boolIn = " << b << ";\n";
                    }
                    if (t->textIn) {
                        const auto es = textInputs(n);
                        if (!es.empty()) {
                            c << p2 << "std::string textIn = " << es[0] << ";\n";
                            c << p2 << "io.text = textIn.c_str();\n";
                        }
                    }
                    if (t->textOut)
                        c << p2 << "char textOutBuf[64] = {};\n"
                          << p2 << "io.textOut = textOutBuf; io.textOutCap = 64;\n";
                    c << p2 << cn->callFn << "(ctx, io);\n";
                    if (t->idOut) c << p2 << "objOut" << n.id << " = io.objectOut;\n";
                    if (t->boolOut) c << p2 << "boolOut" << n.id << " = io.boolOut;\n";
                    if (t->posOut)
                        c << p2 << "posOut" << n.id << "[0] = io.positionOut.x;\n"
                          << p2 << "posOut" << n.id << "[1] = io.positionOut.y;\n"
                          << p2 << "posOut" << n.id << "[2] = io.positionOut.z;\n";
                    if (t->textOut)
                        c << p2 << "snprintf(textOut" << n.id << ", sizeof(textOut"
                          << n.id << "), \"%s\", io.textOut ? io.textOut : \"\");\n";
                    c << pad << "}\n";
                } else {
                    // Inline snippet: substitute {placeholders} into the body.
                    std::string body = cn->code;
                    body = replaceAll(body, "{self}", std::to_string((int)ownerIdx));
                    body = replaceAll(body, "{obj}", objIdx);
                    body = replaceAll(body, "{str}", "\"" + escapeCString(n.str) + "\"");
                    for (int a = 0; a < 4; ++a) {
                        const std::string ai = std::to_string(a);
                        body = replaceAll(body, "{num" + ai + "}", floatLit(n.num[a]));
                        body = replaceAll(body, "{int" + ai + "}", intLit(n.num[a]));
                    }
                    std::istringstream lines(body);
                    std::string ln;
                    while (std::getline(lines, ln)) c << pad << ln << "\n";
                }
            }
            // Clone targets guard on the handle: no clone spawned yet (or it
            // was despawned / the scene reloaded) skips the action outright.
            if (!dyn.empty() && !c.str().empty())
                return pad + "if (" + dyn + " >= 0 && " + dyn +
                       " < ctx.objectCount) {\n" + c.str() + pad + "}\n";
            return c.str();
        };

        // Emit the actions reached by exec links out of `fromId`. A custom
        // node with exec_out fires its own downstream inline right after it
        // runs (so raycast -> [exec] -> Hide Object sequences the data
        // dependency); `visited` guards against exec cycles. Built-in Delay's
        // exec-out fires from its per-frame countdown, not here, so it does not
        // recurse. Pure data / trigger nodes never "run".
        std::function<std::string(int, const std::string&, std::vector<int>&)> emitExec =
            [&](int fromId, const std::string& pad,
                std::vector<int>& visited) -> std::string {
            std::ostringstream c;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkExec || l.fromNode != fromId) continue;
                const FlowNode* m = nodeById(l.toNode);
                if (!m) continue;
                const FlowNodeType* t = flowNodeType(m->type);
                if (!t || t->trigger || t->pure) continue;
                // Keyed on node AND pin: one trigger may legitimately drive two
                // different branches of the same merged node, which is not a
                // cycle.
                const int key = m->id * kFlowMaxExecIn + l.toPin;
                bool seen = false;
                for (int v : visited) seen |= (v == key);
                if (seen) {
                    c << pad << "// exec cycle at node " << m->id << " skipped\n";
                    continue;
                }
                visited.push_back(key);
                c << actionCode(*m, pad, l.toPin);
                if (t->execThrough &&
                    (flowCustomNode(m->type) || m->type == "Raycast" ||
                     m->type == "GetPosition"))
                    c << emitExec(m->id, pad, visited);
            }
            return c.str();
        };
        // all actions exec-linked to a trigger (pure data nodes never "run")
        auto linkedActions = [&](int triggerId, const std::string& pad) {
            std::vector<int> visited;
            return emitExec(triggerId, pad, visited);
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
            if (n.type == "SpawnObject") {
                // handle slots live globally but reset with this script's
                // scene-generation state (a reload clears the whole pool)
                const int k = spawnSlotOf(si, ownerIdx, n.id);
                if (k >= 0)
                    flagResets << "      flowSpawned[" << k << "] = -1;\n";
            } else if (n.type == "Delay") {
                const std::string var = "delay" + std::to_string(n.id);
                members << "  int " << var << " = 0;\n";
                flagResets << "      " << var << " = 0;\n";
                clsOut << "    if (" << var << " > 0 && --" << var << " == 0) {\n"
                       << linkedActions(n.id, "      ") << "    }\n";
            } else if (n.type == "DisplayText") {
                // The wired text is a live value (a save value, a counter), so
                // re-read it every frame the slot is on - that is what makes
                // this node different from the baked Set Text Visible. Skipped
                // while hidden, so an off-screen text costs nothing.
                const int slot = dynTextSlotOf(si, ownerIdx, n.id);
                if (slot >= 0)
                    clsOut << "    if (ctx.dynTextOn && ctx.dynTextOn[" << slot
                           << "])\n      flowSetDynText(ctx, " << slot << ", "
                           << dynTextExpr(n) << ");\n";
            } else if (n.type == "MoveObjectTo") {
                if (!targetExpr(n).empty()) {
                    clsOut << "    // node " << n.id << " (MoveObjectTo): runtime "
                              "object targets (spawned clone / custom output) are "
                              "not supported here yet\n";
                    continue;
                }
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

        // Runtime output latches for C++-backed custom nodes, the built-in
        // Raycast and exec-wired Get Position nodes: members hold the last
        // value each output pin produced, so downstream nodes read them as
        // plain variables (objOut<id>, boolOut<id>, posOut<id>[3],
        // textOut<id>). Reset to defaults on scene (re)load like the other
        // per-node state.
        for (const FlowNode& n : fg.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (!t || !(flowCustomNode(n.type) || n.type == "Raycast" ||
                        getPosLatched(n)))
                continue;
            const std::string id = std::to_string(n.id);
            // Get Position latches only its position - its object output
            // stays the compile-time resolution.
            if (t->idOut && n.type != "GetPosition") {
                members << "  int objOut" << id << " = -1;\n";
                flagResets << "      objOut" << id << " = -1;\n";
            }
            if (t->boolOut) {
                members << "  bool boolOut" << id << " = false;\n";
                flagResets << "      boolOut" << id << " = false;\n";
            }
            if (t->posOut) {
                members << "  float posOut" << id << "[3] = {};\n";
                flagResets << "      posOut" << id << "[0] = posOut" << id
                           << "[1] = posOut" << id << "[2] = 0.0F;\n";
            }
            if (t->textOut) {
                members << "  char textOut" << id << "[64] = {};\n";
                flagResets << "      textOut" << id << "[0] = 0;\n";
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
                const std::string dyn = targetExpr(n);
                if (!dyn.empty()) {
                    clsOut << "    if (" << dyn << " >= 0 && ctx.usedObject == " << dyn
                           << ") {\n" << body << "    }\n";
                    continue;
                }
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
                const std::string dyn = targetExpr(n);
                std::string idxStr;
                if (dyn.empty()) {
                    const int idx = resolveTarget(n);
                    if (idx < 0) {
                        clsOut << "    // node " << n.id << " (NearObject): unknown object '"
                            << n.str << "'\n";
                        continue;
                    }
                    idxStr = std::to_string(idx);
                } else {
                    idxStr = dyn;
                }
                const std::string flag = "near" + std::to_string(n.id);
                members << "  bool " << flag << " = false;\n";
                flagResets << "      " << flag << " = false;\n";
                const float r = n.num[0] > 0.01f ? n.num[0] : 3.0f;
                clsOut << "    " << (dyn.empty() ? "{" : "if (" + dyn + " >= 0) {")
                    << "\n      const float dx = ctx.playerPosition.x - ctx.objects["
                    << idxStr
                    << "].data.position[0];\n      const float dz = ctx.playerPosition.z - "
                       "ctx.objects["
                    << idxStr
                    << "].data.position[2];\n      const bool isNear = dx * dx + dz * dz < "
                    << floatLit(r * r) << ";\n      if (isNear && !" << flag << ") {\n"
                    << body << "      }\n      " << flag << " = isNear;\n    }\n";
            } else if (n.type == "OnPlayerSeen") {
                // rising edge of the vision condition (like NearObject)
                const std::string dyn = targetExpr(n);
                std::string idxStr;
                if (dyn.empty()) {
                    const int idx = resolveTarget(n);
                    if (idx < 0) {
                        clsOut << "    // node " << n.id
                               << " (OnPlayerSeen): unknown object '" << n.str
                               << "'\n";
                        continue;
                    }
                    idxStr = std::to_string(idx);
                } else {
                    idxStr = dyn;
                }
                const std::string flag = "seen" + std::to_string(n.id);
                members << "  bool " << flag << " = false;\n";
                flagResets << "      " << flag << " = false;\n";
                clsOut << "    "
                       << (dyn.empty() ? "{" : "if (" + dyn + " >= 0) {")
                       << "\n      const bool isSeen = navPlayerSeen(ctx, "
                       << idxStr << ", " << floatLit(n.num[0]) << ", "
                       << floatLit(n.num[1]) << ", "
                       << (n.num[2] != 0.0f ? 1 : 0) << ");\n"
                       << "      if (isSeen && !" << flag << ") {\n"
                       << body << "      }\n      " << flag
                       << " = isSeen;\n    }\n";
            } else if (n.type == "EverySeconds") {
                // Countdown, not `frame % everyFrames(s)`: the divisor now
                // tracks the measured dt, and a modulo against a moving
                // divisor can skip its == 0 frame entirely at uncapped FPS.
                const std::string var = "every" + std::to_string(n.id);
                members << "  int " << var << " = 1;\n";  // first frame fires
                flagResets << "      " << var << " = 1;\n";
                clsOut << "    if (--" << var << " <= 0) {\n      " << var
                       << " = everyFrames(" << floatLit(n.num[0]) << ");\n"
                       << body << "    }\n";
            } else if (n.type == "OnAnimFinished") {
                const std::string dyn = targetExpr(n);
                if (!dyn.empty()) {
                    clsOut << "    if (" << dyn << " >= 0 && ctx.objects[" << dyn
                           << "].animFinished) {\n" << body << "    }\n";
                    continue;
                }
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

// src/scripts/live_link.gen.cpp - the Live Link poller. Debug builds with the
// "Live Link" project preference on: the editor mirrors scene edits into the
// running game by writing livelink.bin next to the ELF, which the game
// already reaches over host: (PCSX2 Host Filesystem or the ps2link file
// server - the same mechanism on both, no extra transport). This generated
// global Script polls that file every few frames; otherwise the file compiles
// to an empty translation unit. Records address objects by the stable id
// hash baked into scene_data.hpp (SCENE_*_OBJECT_ID_HASHES), so a session
// survives renames/reorders, spawns NEWLY ADDED objects through the runtime
// spawn pool (templateIdx names an equal-recipe authored object to clone) and
// hides deleted ones (absence from the snapshot = deleted; restored on undo).
// The editor only streams while bin/livelink.sig (as-built record, stamped by
// the Runner) says the session is representable. Binary layout v2
// (little-endian, both sides are LE):
//   u32 magic 'TXLL', u32 version=2, u32 seq, i32 scene, i32 count, u32 rsvd
//   count records of 64 bytes:
//     u64 idHash, i32 templateIdx (-1 = existed at build), u32 pad,
//     f32 position[3], rotation[3] (deg), scale[3], color[3] (0..1)
//   u32 footer = seq ^ 0x5A5A5A5A   (guards torn/partial reads)
static std::string liveLinkScript(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n";
    if (p.settings.buildProfile != "debug" || !p.settings.liveLink) {
        out << "// Live Link is compiled only into debug builds with the "
               "\"Live Link\"\n// preference on (Project > Preferences > "
               "Build); this build carries no poller.\n";
        return out.str();
    }

    size_t maxObjects = 1;
    for (const SceneData& sc : p.scenes)
        if (sc.objects.size() > maxObjects) maxObjects = sc.objects.size();

    out << "// Live Link (debug builds): applies editor edits to the running "
           "game with no\n"
           "// rebuild. The editor writes livelink.bin next to the ELF; this "
           "script polls\n"
           "// it over host:, patches transforms/colors in place, spawns "
           "newly added\n"
           "// objects from equal-recipe templates and hides deleted ones.\n"
           "#include <tyra>\n"
           "#include <algorithm>\n"
           "#include <cstdio>\n"
           "#include <cstring>\n"
           "#include \"scripts/script.hpp\"\n"
           "\n"
           "namespace " << ns << " {\n"
           "namespace {\n"
           "\n"
           "typedef unsigned long long llu64;\n"
           "constexpr u32 LL_MAGIC = 0x4C4C5854;  // \"TXLL\"\n"
           "constexpr u32 LL_VERSION = 2;\n"
           "constexpr int LL_HEADER = 24;\n"
           "constexpr int LL_STRIDE = 64;  // one record (id + template + 12 "
           "floats)\n"
           "// Largest authored object table across scenes - table bounds.\n"
           "constexpr int LL_MAX_OBJECTS = " << maxObjects << ";\n"
           "// Live-spawned clones tracked at once; matches the game's\n"
           "// MAX_SPAWNED_OBJECTS pool (spawnObject fails past it anyway).\n"
           "constexpr int LL_MAX_SPAWNED = 32;\n"
           "\n"
           "class LiveLink : public Script {\n"
           " public:\n"
           "  void update(ScriptContext& ctx) override {\n"
           "    // Scene (re)load resets runtime objects to their baked state: "
           "rebuild the\n"
           "    // id lookup and re-apply the latest snapshot (lastSeq_ = 0).\n"
           "    if (ctx.sceneGeneration != gen_) rebuildTables(ctx);\n"
           "\n"
           "    // Over ps2link every fopen is a network round-trip - poll "
           "sparsely there;\n"
           "    // PCSX2's Host Filesystem is plain local IO, poll ~10x/s.\n"
           "    if (--cooldown_ > 0) return;\n"
           "    cooldown_ = Tyra::IrxLoader::keepIopResident ? 25 : 6;\n"
           "\n"
           "    static unsigned char buf[LL_HEADER +\n"
           "                             (LL_MAX_OBJECTS + LL_MAX_SPAWNED) * "
           "LL_STRIDE + 4];\n"
           "    FILE* f = fopen(Tyra::FileUtils::fromCwd(\"livelink.bin\")"
           ".c_str(), \"rb\");\n"
           "    if (!f) return;  // no snapshot yet (or shipped without the "
           "editor)\n"
           "    const size_t got = fread(buf, 1, sizeof(buf), f);\n"
           "    fclose(f);\n"
           "    if (got < LL_HEADER + 4) return;\n"
           "\n"
           "    u32 magic, version, seq;\n"
           "    s32 scene, count;\n"
           "    memcpy(&magic, buf + 0, 4);\n"
           "    memcpy(&version, buf + 4, 4);\n"
           "    memcpy(&seq, buf + 8, 4);\n"
           "    memcpy(&scene, buf + 12, 4);\n"
           "    memcpy(&count, buf + 16, 4);\n"
           "    if (magic != LL_MAGIC || version != LL_VERSION) return;\n"
           "    if (seq == lastSeq_) return;  // already applied\n"
           "    if (scene != ctx.scene || count < 0 ||\n"
           "        count > LL_MAX_OBJECTS + LL_MAX_SPAWNED)\n"
           "      return;\n"
           "    // Exact size + footer echo of seq: a torn write fails both.\n"
           "    if (got != (size_t)(LL_HEADER + count * LL_STRIDE + 4)) return;\n"
           "    u32 foot;\n"
           "    memcpy(&foot, buf + LL_HEADER + count * LL_STRIDE, 4);\n"
           "    if (foot != (seq ^ 0x5A5A5A5AU)) return;\n"
           "\n"
           "    bool present[LL_MAX_OBJECTS] = {};\n"
           "    bool presentSpawn[LL_MAX_SPAWNED] = {};\n"
           "    for (int i = 0; i < count; ++i) {\n"
           "      const unsigned char* r = buf + LL_HEADER + i * LL_STRIDE;\n"
           "      llu64 id;\n"
           "      s32 tmpl;\n"
           "      float v[12];\n"
           "      memcpy(&id, r + 0, 8);\n"
           "      memcpy(&tmpl, r + 8, 4);\n"
           "      memcpy(v, r + 16, 48);\n"
           "\n"
           "      const int idx = findAuthored(id);\n"
           "      if (idx >= 0) {\n"
           "        if (idx < LL_MAX_OBJECTS) present[idx] = true;\n"
           "        if (idx >= ctx.objectCount) continue;\n"
           "        RuntimeObject& o = ctx.objects[idx];\n"
           "        bool changed = patch(o, v);\n"
           "        if (hiddenByLL_[idx]) {  // deleted then undone: restore\n"
           "          o.visible = prevVisible_[idx];\n"
           "          hiddenByLL_[idx] = false;\n"
           "          changed = true;\n"
           "        }\n"
           "        if (changed) o.dirty = true;\n"
           "        continue;\n"
           "      }\n"
           "\n"
           "      const int s = findSpawned(id);\n"
           "      if (s >= 0) {\n"
           "        presentSpawn[s] = true;\n"
           "        const int slot = spawnedSlot_[s];\n"
           "        if (slot < ctx.objectCount && ctx.objects[slot].active) {\n"
           "          RuntimeObject& o = ctx.objects[slot];\n"
           "          if (patch(o, v)) o.dirty = true;\n"
           "        }\n"
           "        continue;\n"
           "      }\n"
           "\n"
           "      // Unknown id = an object added in the editor after the "
           "build: clone\n"
           "      // the equal-recipe template the editor picked, then patch "
           "the clone.\n"
           "      if (tmpl >= 0 && ctx.spawnObject && spawnedCount_ < "
           "LL_MAX_SPAWNED) {\n"
           "        const int slot = ctx.spawnObject(tmpl, v[0], v[1], v[2], "
           "v[4]);\n"
           "        if (slot >= 0) {\n"
           "          spawnedId_[spawnedCount_] = id;\n"
           "          spawnedSlot_[spawnedCount_] = slot;\n"
           "          presentSpawn[spawnedCount_] = true;\n"
           "          ++spawnedCount_;\n"
           "          if (slot < ctx.objectCount) {\n"
           "            RuntimeObject& o = ctx.objects[slot];\n"
           "            patch(o, v);\n"
           "            o.dirty = true;\n"
           "          }\n"
           "        }\n"
           "      }\n"
           "    }\n"
           "\n"
           "    // Authored objects missing from the snapshot were deleted in "
           "the editor:\n"
           "    // hide them (geometry stays baked until a rebuild; collision "
           "remains -\n"
           "    // an approximation, exactly like the Hide Object flow node).\n"
           "    const int authored =\n"
           "        SCENE_OBJECT_COUNTS[ctx.scene] < LL_MAX_OBJECTS\n"
           "            ? SCENE_OBJECT_COUNTS[ctx.scene]\n"
           "            : LL_MAX_OBJECTS;\n"
           "    for (int i = 0; i < authored && i < ctx.objectCount; ++i) {\n"
           "      if (present[i] || hiddenByLL_[i]) continue;\n"
           "      prevVisible_[i] = ctx.objects[i].visible;\n"
           "      ctx.objects[i].visible = false;\n"
           "      hiddenByLL_[i] = true;\n"
           "    }\n"
           "    // Live-spawned clones missing from the snapshot: despawn "
           "(frees the slot).\n"
           "    for (int s = spawnedCount_ - 1; s >= 0; --s) {\n"
           "      if (presentSpawn[s]) continue;\n"
           "      if (ctx.despawnObject) ctx.despawnObject(spawnedSlot_[s]);\n"
           "      --spawnedCount_;\n"
           "      spawnedId_[s] = spawnedId_[spawnedCount_];\n"
           "      spawnedSlot_[s] = spawnedSlot_[spawnedCount_];\n"
           "      presentSpawn[s] = presentSpawn[spawnedCount_];\n"
           "    }\n"
           "    lastSeq_ = seq;\n"
           "  }\n"
           "\n"
           " private:\n"
           "  static bool apply3(float* dst, const float* src) {\n"
           "    if (dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2])\n"
           "      return false;\n"
           "    dst[0] = src[0], dst[1] = src[1], dst[2] = src[2];\n"
           "    return true;\n"
           "  }\n"
           "  // dirty only on a real change - it costs a geometry rebuild.\n"
           "  static bool patch(RuntimeObject& o, const float* v) {\n"
           "    bool changed = false;\n"
           "    changed |= apply3(o.data.position, v + 0);\n"
           "    changed |= apply3(o.data.rotation, v + 3);\n"
           "    changed |= apply3(o.data.scale, v + 6);\n"
           "    changed |= apply3(o.data.color, v + 9);\n"
           "    return changed;\n"
           "  }\n"
           "\n"
           "  // Sorted (hash -> authored index) lookup over the baked id "
           "table.\n"
           "  void rebuildTables(const ScriptContext& ctx) {\n"
           "    gen_ = ctx.sceneGeneration;\n"
           "    lastSeq_ = 0;  // re-apply the latest snapshot after a reload\n"
           "    spawnedCount_ = 0;  // scene reload despawned every clone\n"
           "    memset(hiddenByLL_, 0, sizeof(hiddenByLL_));\n"
           "    n_ = SCENE_OBJECT_COUNTS[ctx.scene] < LL_MAX_OBJECTS\n"
           "             ? SCENE_OBJECT_COUNTS[ctx.scene]\n"
           "             : LL_MAX_OBJECTS;\n"
           "    const llu64* ids = SCENE_OBJECT_ID_TABLES[ctx.scene];\n"
           "    for (int i = 0; i < n_; ++i) order_[i] = (short)i;\n"
           "    std::sort(order_, order_ + n_,\n"
           "              [&](short a, short b) { return ids[a] < ids[b]; });\n"
           "    for (int i = 0; i < n_; ++i) sorted_[i] = ids[order_[i]];\n"
           "  }\n"
           "  int findAuthored(llu64 id) const {\n"
           "    int lo = 0, hi = n_;\n"
           "    while (lo < hi) {\n"
           "      const int mid = (lo + hi) / 2;\n"
           "      if (sorted_[mid] < id) lo = mid + 1;\n"
           "      else hi = mid;\n"
           "    }\n"
           "    return (lo < n_ && sorted_[lo] == id) ? order_[lo] : -1;\n"
           "  }\n"
           "  int findSpawned(llu64 id) const {\n"
           "    for (int s = 0; s < spawnedCount_; ++s)\n"
           "      if (spawnedId_[s] == id) return s;\n"
           "    return -1;\n"
           "  }\n"
           "\n"
           "  int cooldown_ = 1;\n"
           "  u32 lastSeq_ = 0;\n"
           "  unsigned int gen_ = 0xFFFFFFFFU;  // forces the first rebuild\n"
           "  int n_ = 0;\n"
           "  llu64 sorted_[LL_MAX_OBJECTS];\n"
           "  short order_[LL_MAX_OBJECTS];\n"
           "  bool hiddenByLL_[LL_MAX_OBJECTS] = {};\n"
           "  bool prevVisible_[LL_MAX_OBJECTS] = {};\n"
           "  llu64 spawnedId_[LL_MAX_SPAWNED];\n"
           "  int spawnedSlot_[LL_MAX_SPAWNED];\n"
           "  int spawnedCount_ = 0;\n"
           "};\n"
           "\n"
           "LiveLink g_liveLink;\n"
           "static const bool g_liveLinkRegistered = []() {\n"
           "  getScripts().push_back(&g_liveLink);\n"
           "  return true;\n"
           "}();\n"
           "\n"
           "}  // namespace\n"
           "}  // namespace " << ns << "\n";
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
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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
    out << "};\n\n";

    // Painted terrain-layer weights (docs/terrain-painting.md), on the SAME
    // vertex grid as the heightmap: layerCount bytes per vertex (0..255,
    // layer-interleaved), drawn as Gouraud vertex alpha by the layer passes.
    // nullptr = unpainted scene. Layer descriptors live in texture_data.gen.hpp.
    for (int si = 0; si < sceneCount; ++si) {
        const SceneData& sc = p.scenes[si];
        const int n = (int)sc.terrainLayers.size();
        if (n == 0) continue;
        out << "constexpr unsigned char SPLAT_" << si << "_WEIGHTS["
            << vws[si] * vds[si] * n << "] = {";
        const bool match = sc.splatW == vws[si] && sc.splatD == vds[si] &&
                           (int)sc.splat.size() == sc.splatW * sc.splatD * n;
        for (int z = 0; z < vds[si]; ++z) {
            for (int x = 0; x < vws[si]; ++x) {
                // Defensive nearest resample: the splat normally already sits
                // on this grid (ensureSplatmap), but a hand-edited project may
                // disagree - never emit a mis-sized table.
                int sx = x, sz = z;
                if (!match && sc.splatW > 1 && sc.splatD > 1) {
                    sx = (int)((float)x / (vws[si] - 1) * (sc.splatW - 1) + 0.5f);
                    sz = (int)((float)z / (vds[si] - 1) * (sc.splatD - 1) + 0.5f);
                }
                for (int l = 0; l < n; ++l) {
                    const size_t i = ((size_t)z * vws[si] + x) * n + l;
                    if (i % 20 == 0) out << "\n    ";
                    const bool ok = match || (sc.splatW > 1 && sc.splatD > 1 &&
                                              (int)sc.splat.size() >=
                                                  (sz * sc.splatW + sx + 1) * n);
                    out << (ok ? (int)sc.splat[((size_t)sz * sc.splatW + sx) * n + l]
                               : 0)
                        << ",";
                }
            }
        }
        out << "\n};\n";
    }
    out << "inline const unsigned char* TERRAIN_SPLAT_TABLES[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si) {
        const bool has = !p.scenes[si].terrainLayers.empty();
        out << (si ? ", " : "");
        if (has)
            out << "SPLAT_" << si << "_WEIGHTS";
        else
            out << "nullptr";
    }
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
// ---------------------------------------------------------------------------
// NavMesh + NPC AI (docs/navigation-ai.md). The walkable-cell grid is baked
// on the host (navmesh.cpp) into inc/nav_data.gen.hpp; the runtime -
// src/scripts/navigation.gen.cpp - runs A* over that bitmap on the EE and
// ticks every AI agent each frame (the Patrol/Chase/Flee flow nodes only set
// the agent's state; one shared state per object, so a Chase interrupts a
// Patrol). Everything below is gated on the AI nodes actually appearing in a
// flow graph: without them both files are stubs and the game carries zero
// nav data or code.

static bool anyNavAiNode(const Project& p) {
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects)
            for (const FlowNode& n : o.flowGraph.nodes)
                if (n.type == "PatrolWaypoints" || n.type == "ChasePlayer" ||
                    n.type == "FleePlayer" || n.type == "StopAi" ||
                    n.type == "OnPlayerSeen")
                    return true;
    return false;
}

// Scene-object indices whose names start with `prefix`, in natural order
// ("wp2" before "wp10"): sorted by the numeric suffix after the prefix, then
// by name. The Patrol Waypoints node resolves its route with this at codegen.
static std::vector<int> waypointIndices(const std::vector<SceneObject>& objs,
                                        const std::string& prefix) {
    std::vector<std::pair<long, int>> found;  // (numeric suffix, object index)
    for (size_t i = 0; i < objs.size(); ++i) {
        const std::string& name = objs[i].name;
        if (prefix.empty() || name.rfind(prefix, 0) != 0) continue;
        const std::string tail = name.substr(prefix.size());
        long num = -1;
        if (!tail.empty()) {
            bool digits = true;
            for (char c : tail) digits &= (c >= '0' && c <= '9');
            if (!digits) continue;  // "wpX" is not a waypoint of prefix "wp"
            num = std::atol(tail.c_str());
        }
        found.push_back({num, (int)i});
    }
    std::stable_sort(found.begin(), found.end(),
                     [&](const auto& a, const auto& b) {
                         if (a.first != b.first) return a.first < b.first;
                         return objs[a.second].name < objs[b.second].name;
                     });
    std::vector<int> out;
    for (const auto& f : found) out.push_back(f.second);
    return out;
}

// inc/nav_data.gen.hpp - the baked walkable grid, one bitmap per scene
static std::string navDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX (navmesh bake). Do not edit - regenerated "
           "on every build.\n"
           "#pragma once\n\nnamespace "
        << ns << " {\n\n";

    if (!anyNavAiNode(p)) {
        out << "// No flow graph uses the AI nodes - no nav data baked.\n"
               "constexpr int NAV_ENABLED = 0;\n\n}  // namespace "
            << ns << "\n";
        return out.str();
    }

    const int sceneCount = (int)p.scenes.size();
    std::vector<navmesh::NavGrid> grids(sceneCount);
    int maxCells = 1;
    for (int si = 0; si < sceneCount; ++si) {
        grids[si] = navmesh::bake(p, p.scenes[si]);
        if (grids[si].w * grids[si].d > maxCells)
            maxCells = grids[si].w * grids[si].d;
    }

    size_t maxObjects = 1;
    for (const SceneData& sc : p.scenes)
        if (sc.objects.size() > maxObjects) maxObjects = sc.objects.size();

    out << "constexpr int NAV_ENABLED = 1;\n"
        << "constexpr int NAV_SCENE_COUNT = " << sceneCount << ";\n"
        << "// A* working arrays are sized to the largest scene grid; agents\n"
        << "// cover the authored objects plus the runtime spawn pool.\n"
        << "constexpr int NAV_MAX_CELLS = " << maxCells << ";\n"
        << "constexpr int NAV_MAX_AGENTS = " << (maxObjects + 32) << ";\n\n";

    auto navIntArr = [&](const char* name, auto get) {
        out << "constexpr int " << name << "[NAV_SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si) out << (si ? ", " : "") << get(si);
        out << "};\n";
    };
    auto navFloatArr = [&](const char* name, auto get) {
        out << "constexpr float " << name << "[NAV_SCENE_COUNT] = {";
        for (int si = 0; si < sceneCount; ++si)
            out << (si ? ", " : "") << floatLit(get(si));
        out << "};\n";
    };
    navIntArr("NAV_WS", [&](int si) { return grids[si].w; });
    navIntArr("NAV_DS", [&](int si) { return grids[si].d; });
    navFloatArr("NAV_ORIGIN_XS", [&](int si) { return grids[si].originX; });
    navFloatArr("NAV_ORIGIN_ZS", [&](int si) { return grids[si].originZ; });
    navFloatArr("NAV_CELL_WS", [&](int si) { return grids[si].cellW; });
    navFloatArr("NAV_CELL_DS", [&](int si) { return grids[si].cellD; });
    out << "\n";

    // Walkable bitmaps, 32 cells per word (cell = z * w + x, LSB first).
    for (int si = 0; si < sceneCount; ++si) {
        const navmesh::NavGrid& g = grids[si];
        const int cells = g.w * g.d;
        const int words = cells > 0 ? (cells + 31) / 32 : 1;
        int walkableCount = 0;
        for (uint8_t c : g.walkable) walkableCount += c;
        out << "// scene \"" << p.scenes[si].name << "\": " << g.w << "x" << g.d
            << " cells, " << walkableCount << " walkable\n"
            << "constexpr unsigned int NAV_" << si << "_CELLS[" << words << "] = {";
        for (int wI = 0; wI < words; ++wI) {
            unsigned int word = 0;
            for (int b = 0; b < 32; ++b) {
                const int i = wI * 32 + b;
                if (i < cells && g.walkable[i]) word |= 1u << b;
            }
            if (wI % 8 == 0) out << "\n    ";
            char hb[12];
            std::snprintf(hb, sizeof(hb), "0x%08x", word);
            out << hb << (wI + 1 < words ? ", " : "");
        }
        out << "\n};\n";
    }
    out << "inline const unsigned int* NAV_CELL_TABLES[NAV_SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si)
        out << (si ? ", " : "") << "NAV_" << si << "_CELLS";
    out << "};\n\n}  // namespace " << ns << "\n";
    return out.str();
}

// inc/scripts/navigation.gen.hpp - the AI API the flow-graph script calls
static std::string navigationHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
           "#pragma once\n\n";
    if (!anyNavAiNode(p)) {
        out << "// No flow graph uses the AI nodes - navigation runtime not "
               "generated.\n";
        return out.str();
    }
    out << "#include \"scripts/script.hpp\"\n\nnamespace " << ns << R"( {

// AI agent commands (the Patrol/Chase/Flee/Stop AI flow nodes). One shared
// agent state per runtime object - starting a behavior replaces the current
// one. Movement runs in the generated NavAiScript every frame: A* over the
// baked nav grid (at most one pathfind per frame, round-robin), terrain
// snapping, turn-to-face. See src/scripts/navigation.gen.cpp.
void navPatrol(ScriptContext& ctx, int obj, const int* waypoints, int count,
               float speed, float pauseSec, int once);
void navChase(ScriptContext& ctx, int obj, float speed, float stopDist,
              float giveUpDist);
void navFlee(ScriptContext& ctx, int obj, float speed, float safeDist);
void navStop(ScriptContext& ctx, int obj);

// True while `obj` sees the player: within range, inside the vision cone of
// fovDeg around the object's facing (+Z rotated by its Y rotation), and with
// needLos != 0 also terrain line-of-sight (hills hide; objects do not).
bool navPlayerSeen(ScriptContext& ctx, int obj, float range, float fovDeg,
                   int needLos);

}  // namespace )" << ns
        << "\n";
    return out.str();
}

// src/scripts/navigation.gen.cpp - A* + the per-frame AI agent tick
static std::string navigationSource(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n";
    if (!anyNavAiNode(p)) {
        out << "// No flow graph uses the AI nodes - navigation runtime not "
               "generated.\n";
        return out.str();
    }
    out << "#include \"scripts/navigation.gen.hpp\"\n"
           "#include \"nav_data.gen.hpp\"\n"
           "#include \"terrain_heights.gen.hpp\"\n\n"
           "#include <math.h>\n"
           "#include <string.h>\n\n"
           "namespace "
        << ns << " {\n";
    out << R"(
namespace {

constexpr float NAV_PI = 3.14159265F;
// Smoothed waypoints an agent carries; longer routes repath on consumption.
constexpr int NAV_PATH_MAX = 24;
// A* expansion cap per call - bounds the worst-case EE time of one pathfind.
constexpr int NAV_MAX_EXPAND = 4096;

inline int navW() { return NAV_WS[g_activeScene]; }
inline int navD() { return NAV_DS[g_activeScene]; }
inline float navCellW() { return NAV_CELL_WS[g_activeScene]; }
inline float navCellD() { return NAV_CELL_DS[g_activeScene]; }

inline bool navWalkableCell(int x, int z) {
  if (x < 0 || x >= navW() || z < 0 || z >= navD()) return false;
  const int i = z * navW() + x;
  return (NAV_CELL_TABLES[g_activeScene][i >> 5] >> (i & 31)) & 1u;
}
inline int navCellX(float wx) {
  return (int)floorf((wx - NAV_ORIGIN_XS[g_activeScene]) / navCellW());
}
inline int navCellZ(float wz) {
  return (int)floorf((wz - NAV_ORIGIN_ZS[g_activeScene]) / navCellD());
}
inline float navCenterX(int cx) {
  return NAV_ORIGIN_XS[g_activeScene] + (cx + 0.5F) * navCellW();
}
inline float navCenterZ(int cz) {
  return NAV_ORIGIN_ZS[g_activeScene] + (cz + 0.5F) * navCellD();
}

// Nearest walkable cell within maxR rings (the goal - the player, a waypoint
// - may sit on a blocked cell; walkers still want to get close).
bool navNearestWalkable(int* cx, int* cz, int maxR) {
  if (navWalkableCell(*cx, *cz)) return true;
  for (int r = 1; r <= maxR; ++r)
    for (int dz = -r; dz <= r; ++dz)
      for (int dx = -r; dx <= r; ++dx) {
        if (dx > -r && dx < r && dz > -r && dz < r) continue;  // ring only
        if (navWalkableCell(*cx + dx, *cz + dz)) {
          *cx += dx;
          *cz += dz;
          return true;
        }
      }
  return false;
}

// Straight-line walkability (half-cell sampling) - path smoothing and the
// skip-ahead both use it. Conservative enough at nav-cell resolution.
bool navLineWalkable(float x0, float z0, float x1, float z1) {
  const float dx = x1 - x0, dz = z1 - z0;
  const float dist = sqrtf(dx * dx + dz * dz);
  const float step = 0.5F * (navCellW() < navCellD() ? navCellW() : navCellD());
  const int n = (int)(dist / step) + 1;
  for (int i = 0; i <= n; ++i) {
    const float t = (float)i / n;
    if (!navWalkableCell(navCellX(x0 + dx * t), navCellZ(z0 + dz * t)))
      return false;
  }
  return true;
}

// --- A* over the walkable bitmap (integer costs 10 straight / 14 diagonal,
// octile heuristic). Static working arrays sized to the largest scene grid;
// the visit stamp avoids clearing them between searches.
unsigned short navG[NAV_MAX_CELLS];
unsigned short navParent[NAV_MAX_CELLS];
unsigned short navSeen[NAV_MAX_CELLS];
unsigned char navClosed[NAV_MAX_CELLS];
unsigned short navHeapCell[NAV_MAX_CELLS];
unsigned short navHeapF[NAV_MAX_CELLS];
int navHeapN = 0;
unsigned short navStampNow = 0;

inline void navHeapPush(unsigned short cell, unsigned short f) {
  if (navHeapN >= NAV_MAX_CELLS) return;
  int i = navHeapN++;
  navHeapCell[i] = cell;
  navHeapF[i] = f;
  while (i > 0) {
    const int up = (i - 1) / 2;
    if (navHeapF[up] <= navHeapF[i]) break;
    unsigned short tc = navHeapCell[i], tf = navHeapF[i];
    navHeapCell[i] = navHeapCell[up]; navHeapF[i] = navHeapF[up];
    navHeapCell[up] = tc; navHeapF[up] = tf;
    i = up;
  }
}
inline unsigned short navHeapPop() {
  const unsigned short top = navHeapCell[0];
  navHeapN--;
  navHeapCell[0] = navHeapCell[navHeapN];
  navHeapF[0] = navHeapF[navHeapN];
  int i = 0;
  for (;;) {
    const int l = i * 2 + 1, r = i * 2 + 2;
    int best = i;
    if (l < navHeapN && navHeapF[l] < navHeapF[best]) best = l;
    if (r < navHeapN && navHeapF[r] < navHeapF[best]) best = r;
    if (best == i) break;
    unsigned short tc = navHeapCell[i], tf = navHeapF[i];
    navHeapCell[i] = navHeapCell[best]; navHeapF[i] = navHeapF[best];
    navHeapCell[best] = tc; navHeapF[best] = tf;
    i = best;
  }
  return top;
}

inline unsigned short navHeuristic(int x0, int z0, int x1, int z1) {
  int dx = x0 > x1 ? x0 - x1 : x1 - x0;
  int dz = z0 > z1 ? z0 - z1 : z1 - z0;
  const int mn = dx < dz ? dx : dz;
  return (unsigned short)(10 * (dx + dz) - 6 * mn);
}

// One AI agent per runtime object. The flow-node commands fill this in; the
// per-frame tick below moves the object along its path.
struct NavAgent {
  unsigned char mode = 0;  // 0 idle, 1 patrol, 2 chase, 3 flee
  unsigned char wantPath = 0;
  unsigned char once = 0;      // patrol: stop after the last waypoint
  unsigned char pathLen = 0, pathPos = 0;
  short wpIndex = 0;           // patrol position in the waypoint list
  int goalCell = -1;           // current A* goal
  const int* wps = nullptr;    // patrol waypoint object indices (static data)
  short wpCount = 0;
  float speed = 2.0F;
  float p0 = 0.0F, p1 = 0.0F;  // chase: stopDist/giveUp; flee: safeDist;
                               // patrol: pause seconds
  float pauseLeft = 0.0F;
  float yOff = 0.0F;           // authored height above the terrain
  float repathLeft = 0.0F;     // seconds until the next repath (chase/flee)
  unsigned short path[NAV_PATH_MAX];
};

NavAgent navAgents[NAV_MAX_AGENTS];
unsigned int navGeneration = 0xFFFFFFFFu;
int navServedLast = 0;  // round-robin cursor of the one-per-frame pathfinder

// Resets every agent once per scene (re)load. Called by BOTH the command
// entry points and the per-frame tick: script order is undefined, so an On
// Start trigger's navPatrol can run before the tick has seen the new scene
// generation - the reset must not wipe that same-frame command.
inline void navSyncGeneration(ScriptContext& ctx) {
  if (ctx.sceneGeneration == navGeneration) return;
  navGeneration = ctx.sceneGeneration;
  for (int i = 0; i < NAV_MAX_AGENTS; ++i) navAgents[i] = NavAgent{};
  navServedLast = 0;
}

// Runs A* from the agent's position to goalCell and fills its (smoothed)
// waypoint list. Unreachable goals path to the closest reachable cell
// instead, so a chase pressed against a wall still closes in.
void navFindPath(NavAgent& a, const RuntimeObject& o) {
  a.pathLen = a.pathPos = 0;
  if (a.goalCell < 0) return;
  int sx = navCellX(o.data.position[0]);
  int sz = navCellZ(o.data.position[2]);
  if (!navNearestWalkable(&sx, &sz, 3)) return;  // agent fully off-mesh
  const int gx = a.goalCell % navW();
  const int gz = a.goalCell / navW();
  const int w = navW();

  if (++navStampNow == 0) {
    memset(navSeen, 0, sizeof(navSeen));
    navStampNow = 1;
  }
  navHeapN = 0;
  const unsigned short start = (unsigned short)(sz * w + sx);
  navSeen[start] = navStampNow;
  navClosed[start] = 0;
  navG[start] = 0;
  navParent[start] = start;
  navHeapPush(start, navHeuristic(sx, sz, gx, gz));

  int bestCell = start;
  unsigned short bestH = navHeuristic(sx, sz, gx, gz);
  int expanded = 0;
  bool reached = false;
  while (navHeapN > 0 && expanded < NAV_MAX_EXPAND) {
    const unsigned short cur = navHeapPop();
    if (navClosed[cur]) continue;  // stale heap duplicate
    navClosed[cur] = 1;
    expanded++;
    const int cx = cur % w, cz = cur / w;
    const unsigned short h = navHeuristic(cx, cz, gx, gz);
    if (h < bestH) { bestH = h; bestCell = cur; }
    if (cx == gx && cz == gz) { reached = true; break; }
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dz == 0) continue;
        const int nx = cx + dx, nz = cz + dz;
        if (!navWalkableCell(nx, nz)) continue;
        // no corner cutting: a diagonal needs both orthogonals open
        if (dx != 0 && dz != 0 &&
            (!navWalkableCell(cx + dx, cz) || !navWalkableCell(cx, cz + dz)))
          continue;
        const unsigned int ng = navG[cur] + ((dx != 0 && dz != 0) ? 14 : 10);
        if (ng > 60000u) continue;  // path absurdly long - give up this way
        const unsigned short n = (unsigned short)(nz * w + nx);
        if (navSeen[n] == navStampNow && navG[n] <= ng) continue;
        navSeen[n] = navStampNow;
        navClosed[n] = 0;
        navG[n] = (unsigned short)ng;
        navParent[n] = cur;
        navHeapPush(n, (unsigned short)(ng + navHeuristic(nx, nz, gx, gz)));
      }
  }
  (void)reached;

  // Reconstruct goal -> start into a raw cell list (bounded), then smooth:
  // emit the farthest cell reachable in a straight line, repeat. Routes
  // longer than the waypoint buffer repath when the buffer runs out.
  static unsigned short raw[512];
  int rawLen = 0;
  for (unsigned short c = (unsigned short)bestCell; rawLen < 512;
       c = navParent[c]) {
    raw[rawLen++] = c;
    if (c == start) break;
  }
  // raw[] is goal..start - walk it backward while string-pulling
  int cur = rawLen - 1;  // start
  float fromX = o.data.position[0], fromZ = o.data.position[2];
  while (cur > 0 && a.pathLen < NAV_PATH_MAX) {
    int far = cur - 1;
    // farthest raw cell with a clear straight line (bounded lookahead)
    for (int j = cur - 1; j >= 0 && j >= cur - 40; --j) {
      const float jx = navCenterX(raw[j] % w), jz = navCenterZ(raw[j] / w);
      if (navLineWalkable(fromX, fromZ, jx, jz)) far = j;
    }
    a.path[a.pathLen++] = raw[far];
    fromX = navCenterX(raw[far] % w);
    fromZ = navCenterZ(raw[far] / w);
    cur = far;
  }
}

// Where the chasers/fleers aim. The Player OBJECT tracks the live player
// only in third-person mode (the avatar is driven every frame); in FPP
// walk / noclip the object keeps its authored spawn position forever, so
// reading it there would leave every NPC watching the spawn point while
// the real player walks free. Everywhere else the camera IS the player.
inline void navPlayerPos(ScriptContext& ctx, float* px, float* py, float* pz) {
  const int pi = PLAYER_INDEXES[ctx.scene];
  if (pi >= 0 && pi < ctx.objectCount && PLAYER_MODES[ctx.scene] == 2) {
    const float* p = ctx.objects[pi].data.position;
    *px = p[0]; *py = p[1]; *pz = p[2];
    return;
  }
  *px = ctx.playerPosition.x;
  *py = ctx.playerPosition.y - 1.5F;  // camera = the eye; approximate the feet
  *pz = ctx.playerPosition.z;
}

inline float navPlanarDist(const RuntimeObject& o, float px, float pz) {
  const float dx = px - o.data.position[0];
  const float dz = pz - o.data.position[2];
  return sqrtf(dx * dx + dz * dz);
}

// Grabs the agent slot for a fresh command: captures the object's authored
// height above the terrain so slopes do not swallow floating NPCs.
NavAgent* navBegin(ScriptContext& ctx, int obj, unsigned char mode,
                   float speed) {
  navSyncGeneration(ctx);
  if (obj < 0 || obj >= ctx.objectCount || obj >= NAV_MAX_AGENTS)
    return nullptr;
  NavAgent& a = navAgents[obj];
  a.mode = mode;
  a.wantPath = 0;
  a.pathLen = a.pathPos = 0;
  a.goalCell = -1;
  a.speed = speed > 0.001F ? speed : 2.0F;
  a.pauseLeft = 0.0F;
  a.repathLeft = 0.0F;
  const float* p = ctx.objects[obj].data.position;
  a.yOff = p[1] - terrainHeightAtScene(ctx.scene, p[0], p[2]);
  return &a;
}

}  // namespace

void navPatrol(ScriptContext& ctx, int obj, const int* waypoints, int count,
               float speed, float pauseSec, int once) {
  NavAgent* a = navBegin(ctx, obj, 1, speed);
  if (!a || count <= 0) return;
  a->wps = waypoints;
  a->wpCount = (short)count;
  a->wpIndex = 0;
  a->once = once ? 1 : 0;
  a->p0 = pauseSec > 0.0F ? pauseSec : 0.0F;
  // the tick aims at the first waypoint and requests the path
}

void navChase(ScriptContext& ctx, int obj, float speed, float stopDist,
              float giveUpDist) {
  NavAgent* a = navBegin(ctx, obj, 2, speed);
  if (!a) return;
  a->p0 = stopDist > 0.01F ? stopDist : 1.5F;
  a->p1 = giveUpDist > 0.01F ? giveUpDist : 0.0F;
  // repathLeft = 0 makes the tick route to the player this frame
}

void navFlee(ScriptContext& ctx, int obj, float speed, float safeDist) {
  NavAgent* a = navBegin(ctx, obj, 3, speed);
  if (!a) return;
  a->p0 = safeDist > 0.01F ? safeDist : 15.0F;
}

void navStop(ScriptContext& ctx, int obj) {
  navSyncGeneration(ctx);
  if (obj < 0 || obj >= ctx.objectCount || obj >= NAV_MAX_AGENTS) return;
  navAgents[obj].mode = 0;
}

bool navPlayerSeen(ScriptContext& ctx, int obj, float range, float fovDeg,
                   int needLos) {
  if (obj < 0 || obj >= ctx.objectCount) return false;
  const RuntimeObject& o = ctx.objects[obj];
  if (!o.active || !o.visible) return false;
  if (range < 0.01F) range = 15.0F;
  if (fovDeg < 1.0F) fovDeg = 90.0F;
  float px, py, pz;
  navPlayerPos(ctx, &px, &py, &pz);
  const float dx = px - o.data.position[0];
  const float dy = py - o.data.position[1];
  const float dz = pz - o.data.position[2];
  if (dx * dx + dy * dy + dz * dz > range * range) return false;
  // vision cone around the facing (+Z rotated by the Y rotation)
  const float planar = sqrtf(dx * dx + dz * dz);
  if (planar > 0.05F) {
    const float yaw = o.data.rotation[1] * NAV_PI / 180.0F;
    const float dot = (sinf(yaw) * dx + cosf(yaw) * dz) / planar;
    if (dot < cosf(fovDeg * 0.5F * NAV_PI / 180.0F)) return false;
  }
  if (needLos) {
    // terrain line-of-sight, eye to eye - a hill hides the player
    const float ex = o.data.position[0];
    const float ey = o.data.position[1] + 1.5F;
    const float ez = o.data.position[2];
    const float ty = py + 1.5F;
    const float dist = sqrtf(dx * dx + dz * dz);
    const int steps = (int)dist + 1;
    for (int i = 1; i < steps; ++i) {
      const float t = (float)i / steps;
      if (terrainHeightAtScene(ctx.scene, ex + dx * t, ez + dz * t) >
          ey + (ty - ey) * t)
        return false;
    }
  }
  return true;
}

namespace {

// Advances one agent a frame: follow the waypoint list at `speed`, snap to
// the terrain (+ the captured height offset), turn to face the motion - the
// same shortest-arc yaw lerp the third-person avatar uses.
void navMoveAgent(ScriptContext& ctx, int idx) {
  NavAgent& a = navAgents[idx];
  RuntimeObject& o = ctx.objects[idx];
  if (a.pathPos >= a.pathLen) return;
  const int w = navW();
  const unsigned short cell = a.path[a.pathPos];
  const float tx = navCenterX(cell % w);
  const float tz = navCenterZ(cell / w);
  float* pos = o.data.position;
  const float dx = tx - pos[0];
  const float dz = tz - pos[2];
  const float dist = sqrtf(dx * dx + dz * dz);
  const float step = a.speed * g_frameDt;
  const float arrive = 0.3F * (navCellW() < navCellD() ? navCellW() : navCellD());
  if (dist <= (step > arrive ? step : arrive)) {
    a.pathPos++;
  }
  if (dist > 0.0005F) {
    const float mv = step < dist ? step : dist;
    pos[0] += dx / dist * mv;
    pos[2] += dz / dist * mv;
    pos[1] = terrainHeightAtScene(ctx.scene, pos[0], pos[2]) + a.yOff;
    // turn toward the motion (degrees, shortest arc)
    float desired = atan2f(dx, dz) * 180.0F / NAV_PI;
    float d = desired - o.data.rotation[1];
    while (d > 180.0F) d -= 360.0F;
    while (d < -180.0F) d += 360.0F;
    float k = 0.25F * g_frameScale;
    if (k > 1.0F) k = 1.0F;
    o.data.rotation[1] += d * k;
    o.dirty = true;
  }
}

}  // namespace

// The per-frame AI tick, registered like any global Script. Runs in every
// scene; scenes without live agents fall straight through the mode == 0
// skips. Menus pause it together with the rest of the scripts.
class NavAiScript : public Script {
 public:
  void update(ScriptContext& ctx) override {
    navSyncGeneration(ctx);
    float px, py, pz;
    navPlayerPos(ctx, &px, &py, &pz);
    const int count =
        ctx.objectCount < NAV_MAX_AGENTS ? ctx.objectCount : NAV_MAX_AGENTS;

    for (int i = 0; i < count; ++i) {
      NavAgent& a = navAgents[i];
      if (a.mode == 0) continue;
      RuntimeObject& o = ctx.objects[i];
      if (!o.active) {  // despawned / streamed out - the behavior ends
        a.mode = 0;
        continue;
      }

      if (a.mode == 2) {  // chase
        const float dist = navPlanarDist(o, px, pz);
        if (a.p1 > 0.0F && dist > a.p1) {
          a.mode = 0;  // gave up
          continue;
        }
        a.repathLeft -= g_frameDt;
        if (a.repathLeft <= 0.0F) {  // timer-gated: at most 2 repaths/s
          int gx = navCellX(px), gz = navCellZ(pz);
          if (navNearestWalkable(&gx, &gz, 3)) {
            a.goalCell = gz * navW() + gx;
            a.wantPath = 1;
          }
          a.repathLeft = 0.5F;
        }
        if (dist > a.p0) {
          navMoveAgent(ctx, i);
        } else {
          // in reach: stand and face the player
          const float ddx = px - o.data.position[0];
          const float ddz = pz - o.data.position[2];
          if (ddx * ddx + ddz * ddz > 0.0001F) {
            float desired = atan2f(ddx, ddz) * 180.0F / NAV_PI;
            float d = desired - o.data.rotation[1];
            while (d > 180.0F) d -= 360.0F;
            while (d < -180.0F) d += 360.0F;
            float k = 0.25F * g_frameScale;
            if (k > 1.0F) k = 1.0F;
            o.data.rotation[1] += d * k;
            o.dirty = true;
          }
        }
      } else if (a.mode == 3) {  // flee
        const float dist = navPlanarDist(o, px, pz);
        if (dist >= a.p0) {
          a.mode = 0;  // safe
          continue;
        }
        a.repathLeft -= g_frameDt;
        if (a.repathLeft <= 0.0F) {  // timer-gated, like the chase
          // aim a grid-bounded chunk away from the player, fanning out to
          // the sides when the straight-away cell is blocked
          float ax = o.data.position[0] - px, az = o.data.position[2] - pz;
          const float al = sqrtf(ax * ax + az * az);
          if (al < 0.01F) { ax = 1.0F; az = 0.0F; }
          else { ax /= al; az /= al; }
          const float reach = a.p0 - dist + 2.0F;
          const float baseAng = atan2f(ax, az);
          for (int c = 0; c < 7; ++c) {
            static const float fan[7] = {0.0F, 0.6F, -0.6F, 1.2F, -1.2F,
                                         1.8F, -1.8F};
            const float ang = baseAng + fan[c];
            int gx = navCellX(o.data.position[0] + sinf(ang) * reach);
            int gz = navCellZ(o.data.position[2] + cosf(ang) * reach);
            if (navNearestWalkable(&gx, &gz, 2)) {
              a.goalCell = gz * navW() + gx;
              a.wantPath = 1;
              break;
            }
          }
          a.repathLeft = 1.0F;
        }
        navMoveAgent(ctx, i);
      } else if (a.mode == 1) {  // patrol
        if (!a.wps || a.wpCount <= 0) { a.mode = 0; continue; }
        if (a.pauseLeft > 0.0F) {
          a.pauseLeft -= g_frameDt;
          continue;
        }
        if (a.pathPos >= a.pathLen && !a.wantPath) {
          // arrived (or not yet routed): aim at the current waypoint, or
          // advance to the next one when already there
          const int wpObj = a.wps[a.wpIndex];
          float wx = o.data.position[0], wz = o.data.position[2];
          if (wpObj >= 0 && wpObj < ctx.objectCount) {
            wx = ctx.objects[wpObj].data.position[0];
            wz = ctx.objects[wpObj].data.position[2];
          }
          const float ddx = wx - o.data.position[0];
          const float ddz = wz - o.data.position[2];
          // "Arrived" must out-tolerance the grid: a path ends at a cell
          // CENTER, and a waypoint can sit on a cell corner - up to
          // ~(0.707 + 0.3) cells away. Anything tighter deadlocks the
          // patrol against the raster.
          const float arrive =
              1.1F * (navCellW() > navCellD() ? navCellW() : navCellD());
          if (ddx * ddx + ddz * ddz <= arrive * arrive) {
            a.pauseLeft = a.p0;
            if (a.wpIndex + 1 >= a.wpCount) {
              if (a.once) { a.mode = 0; continue; }
              a.wpIndex = 0;
            } else {
              a.wpIndex++;
            }
          } else {
            int gx = navCellX(wx), gz = navCellZ(wz);
            if (navNearestWalkable(&gx, &gz, 3)) {
              a.goalCell = gz * navW() + gx;
              a.wantPath = 1;
            } else {
              a.mode = 0;  // waypoint unreachable from any nearby cell
            }
          }
        }
        navMoveAgent(ctx, i);
      }
    }

    // Serve at most ONE pathfind per frame (round-robin) - A* on a 128x128
    // grid is cheap, but not "every agent, every frame" cheap on the EE.
    for (int k = 1; k <= count; ++k) {
      const int i = (navServedLast + k) % count;
      if (!navAgents[i].wantPath || navAgents[i].mode == 0) continue;
      if (!ctx.objects[i].active) { navAgents[i].wantPath = 0; continue; }
      navFindPath(navAgents[i], ctx.objects[i]);
      navAgents[i].wantPath = 0;
      navServedLast = i;
      break;
    }
  }
};

}  // namespace )" << ns
        << "\n\nTYRA_SCRIPT(" << ns << "::NavAiScript);\n";
    return out.str();
}

// inc/texture_data.gen.hpp - PNG textures used by the scene
static std::string textureDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const auto paths = collectTexturePaths(p);

    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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
    // Per scene: the terrain material's map_Kd texture index (-1 = none), the
    // tiling scale, whether a material is assigned at all, and its Kd tint.
    std::vector<project::TerrainMaterial> terrain(p.scenes.size());
    std::vector<float> baseStochF(p.scenes.size(), 1.0f);
    for (size_t si = 0; si < p.scenes.size(); ++si) {
        terrain[si] = project::resolveTerrainMaterial(
            p, project::resolvedSettings(p, p.scenes[si]).terrainMaterial);
        baseStochF[si] = terrainStochFactor(p, terrain[si].texture,
                                            p.scenes[si].terrainBaseStochastic);
    }
    out << "};\n\n"
        << "constexpr int TERRAIN_TEXTURES[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "")
            << textureIndexOf(p, terrainTexKey(terrain[si].texture,
                                               p.scenes[si].terrainBaseStochastic));
    out << "};\n"
        // Texture tiling from the material's map_Kd "-s" option: UV repeats per
        // world unit, per axis (u across X, v across Z). Divided by the
        // stochastic factor so the baked supertile keeps the source at its size.
        << "constexpr float TERRAIN_TILE_US[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "") << floatLit(terrain[si].tile[0] / baseStochF[si]);
    out << "};\n"
        << "constexpr float TERRAIN_TILE_VS[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "") << floatLit(terrain[si].tile[1] / baseStochF[si]);
    out << "};\n"
        << "constexpr bool TERRAIN_HAS_MATERIALS[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "") << (terrain[si].present ? "true" : "false");
    out << "};\n"
        << "constexpr float TERRAIN_TINTS[" << p.scenes.size() << "][3] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "") << "{" << floatLit(terrain[si].kd[0]) << ", "
            << floatLit(terrain[si].kd[1]) << ", " << floatLit(terrain[si].kd[2]) << "}";
    out << "};\n"
        // Macro ground variation (docs/terrain-painting.md): value-noise tint
        // multiplied into the vertex shade while chunks bake. 0 = off.
        << "constexpr float TERRAIN_TINT_VARIATIONS[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "") << floatLit(p.scenes[si].terrainTintVariation);
    out << "};\n"
        << "constexpr float TERRAIN_TINT_SCALES[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "")
            << floatLit(p.scenes[si].terrainTintScale > 1.0f
                            ? p.scenes[si].terrainTintScale
                            : 1.0f);
    out << "};\n";

    // Painted terrain layers (docs/terrain-painting.md): per scene, each
    // layer's texture index / tiling (the material's map_Kd "-s" divided by the
    // layer Size - bigger Size = larger pattern) / Kd tint / has-material flag.
    // The per-vertex blend weights live in terrain_heights.gen.hpp (same grid
    // as the heightmap); the game draws one extra alpha-blended pass per layer
    // present in a chunk.
    size_t maxLayers = 0;
    for (const SceneData& sc : p.scenes)
        maxLayers = sc.terrainLayers.size() > maxLayers ? sc.terrainLayers.size()
                                                        : maxLayers;
    out << "\nconstexpr int TERRAIN_LAYER_COUNTS[" << p.scenes.size() << "] = {";
    for (size_t si = 0; si < p.scenes.size(); ++si)
        out << (si ? ", " : "") << p.scenes[si].terrainLayers.size();
    out << "};\n"
        << "constexpr int TERRAIN_MAX_LAYERS = " << (maxLayers > 0 ? maxLayers : 1)
        << ";\n";
    auto layerMat = [&](const SceneData& sc, size_t li) {
        return project::resolveTerrainMaterial(p, sc.terrainLayers[li].material);
    };
    auto perLayer = [&](const char* name, auto emit) {
        out << name << "[" << p.scenes.size() << "][TERRAIN_MAX_LAYERS] = {";
        for (size_t si = 0; si < p.scenes.size(); ++si) {
            out << (si ? ", " : "") << "{";
            for (size_t li = 0; li < maxLayers; ++li) {
                out << (li ? ", " : "");
                if (li < p.scenes[si].terrainLayers.size())
                    emit(p.scenes[si], li);
                else
                    out << "0";
            }
            out << "}";
        }
        out << "};\n";
    };
    if (maxLayers == 0) {
        // Keep the arrays compilable for the loops that index them.
        out << "constexpr int TERRAIN_LAYER_TEXTURES[" << p.scenes.size()
            << "][1] = {};\n"
            << "constexpr float TERRAIN_LAYER_TILE_US[" << p.scenes.size()
            << "][1] = {};\n"
            << "constexpr float TERRAIN_LAYER_TILE_VS[" << p.scenes.size()
            << "][1] = {};\n"
            << "constexpr float TERRAIN_LAYER_TINTS[" << p.scenes.size()
            << "][1][3] = {};\n";
    } else {
        auto layerStoch = [&](const SceneData& sc, size_t li) {
            return terrainStochFactor(p, layerMat(sc, li).texture,
                                      sc.terrainLayers[li].stochastic);
        };
        perLayer("constexpr int TERRAIN_LAYER_TEXTURES", [&](const SceneData& sc,
                                                             size_t li) {
            out << textureIndexOf(
                p, terrainTexKey(layerMat(sc, li).texture,
                                 sc.terrainLayers[li].stochastic));
        });
        perLayer("constexpr float TERRAIN_LAYER_TILE_US",
                 [&](const SceneData& sc, size_t li) {
                     const float s = sc.terrainLayers[li].scale > 0.0f
                                         ? sc.terrainLayers[li].scale
                                         : 1.0f;
                     out << floatLit(layerMat(sc, li).tile[0] /
                                     (s * layerStoch(sc, li)));
                 });
        perLayer("constexpr float TERRAIN_LAYER_TILE_VS",
                 [&](const SceneData& sc, size_t li) {
                     const float s = sc.terrainLayers[li].scale > 0.0f
                                         ? sc.terrainLayers[li].scale
                                         : 1.0f;
                     out << floatLit(layerMat(sc, li).tile[1] /
                                     (s * layerStoch(sc, li)));
                 });
        out << "constexpr float TERRAIN_LAYER_TINTS[" << p.scenes.size()
            << "][TERRAIN_MAX_LAYERS][3] = {";
        for (size_t si = 0; si < p.scenes.size(); ++si) {
            out << (si ? ", " : "") << "{";
            for (size_t li = 0; li < maxLayers; ++li) {
                out << (li ? ", " : "") << "{";
                if (li < p.scenes[si].terrainLayers.size()) {
                    const project::TerrainMaterial m = layerMat(p.scenes[si], li);
                    // No material assigned = the neutral gray the editor shows.
                    const float g = m.present ? 1.0f : 0.6f;
                    out << floatLit(m.present ? m.kd[0] : g) << ", "
                        << floatLit(m.present ? m.kd[1] : g) << ", "
                        << floatLit(m.present ? m.kd[2] : g);
                } else {
                    out << "0, 0, 0";
                }
                out << "}";
            }
            out << "}";
        }
        out << "};\n";
    }
    out << "\n}  // namespace " << ns << "\n";
    return out.str();
}

// inc/font_data.gen.hpp - glyph atlases for the fonts a Display Text node
// draws with, plus one runtime slot per such node. Only fonts reachable from a
// Display Text node appear here: static text is already pixels by build time,
// so its font costs the game nothing.
static std::string fontDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    const std::vector<int> atlasFonts = p.atlasFontIndices();
    const std::vector<DynTextSlot> slots = dynTextSlots(p);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns
        << " {\n\n"
           "// A glyph's cell in the atlas and how to lay it down. Mirrors\n"
           "// menubake::AtlasGlyph - the metrics here and the baked pixels come\n"
           "// from the same atlasLayout() call, so they cannot drift.\n"
           "struct FontGlyph {\n"
           "  short u, v;        // top-left texel in the atlas\n"
           "  short w, h;        // glyph size (0 = nothing to draw)\n"
           "  short xoff, yoff;  // pen/line-top -> glyph top-left, at baseSize\n"
           "  short adv;         // pen step to the next glyph, at baseSize\n"
           "};\n\n"
           "struct FontData {\n"
           "  const char* atlas;  // relative to the game binary\n"
           "  short texW, texH;\n"
           "  short lineH;        // baseline pitch at baseSize\n"
           "  short baseSize;     // size the metrics were baked at\n"
           "  unsigned char r, g, b;  // tint (glyphs bake white)\n"
           "  unsigned char shadow;   // 1 = draw a dark 1px offset pass first\n"
           "  const FontGlyph* glyphs;\n"
           "};\n\n"
        << "constexpr int FONT_FIRST_CHAR = " << menubake::kAtlasFirstChar << ";\n"
        << "constexpr int FONT_CHAR_COUNT = " << menubake::kAtlasCharCount << ";\n"
        << "constexpr int FONT_COUNT = " << atlasFonts.size() << ";\n\n";

    auto byteLit = [](float v) {
        int b = (int)(v * 255.0f + 0.5f);
        return b < 0 ? 0 : b > 255 ? 255 : b;
    };

    for (size_t k = 0; k < atlasFonts.size(); ++k) {
        const GameFont& gf = p.fonts[atlasFonts[k]];
        menubake::AtlasLayout lay;
        const bool ok = menubake::atlasLayout(gf, p, lay);
        out << "inline const FontGlyph FONT_GLYPHS_" << k << "[FONT_CHAR_COUNT] = {\n";
        for (int i = 0; i < menubake::kAtlasCharCount; ++i) {
            const menubake::AtlasGlyph g =
                ok ? lay.glyphs[i] : menubake::AtlasGlyph{};
            out << "    {" << g.u << ", " << g.v << ", " << g.w << ", " << g.h
                << ", " << g.xoff << ", " << g.yoff << ", " << g.advance << "},\n";
        }
        out << "};\n";
    }
    out << "\n";

    out << "inline const FontData FONTS[FONT_COUNT > 0 ? FONT_COUNT : 1] = {\n";
    if (atlasFonts.empty()) {
        out << "    {\"\", 0, 0, 0, 0, 255, 255, 255, 0, nullptr},\n";
    } else {
        for (size_t k = 0; k < atlasFonts.size(); ++k) {
            const GameFont& gf = p.fonts[atlasFonts[k]];
            menubake::AtlasLayout lay;
            const bool ok = menubake::atlasLayout(gf, p, lay);
            out << "    {\"fonts/" << menubake::atlasFileName(gf.name) << "\", "
                << (ok ? lay.texW : 8) << ", " << (ok ? lay.texH : 8) << ", "
                << (ok ? lay.lineH : 8) << ", " << (ok ? lay.baseSize : 8) << ", "
                << byteLit(gf.color[0]) << ", " << byteLit(gf.color[1]) << ", "
                << byteLit(gf.color[2]) << ", " << (gf.shadow ? 1 : 0)
                << ", FONT_GLYPHS_" << k << "},  // " << gf.name << "\n";
        }
    }
    out << "};\n\n";

    out << "// One slot per Display Text node (see DynTextSlot in templates.cpp).\n"
           "struct DynTextData {\n"
           "  short font;   // index into FONTS\n"
           "  float x, y;   // normalized screen position, center anchor\n"
           "  float size;   // glyph height in pixels\n"
           "};\n\n"
        << "constexpr int DYN_TEXT_COUNT = " << slots.size() << ";\n"
        << "constexpr int DYN_TEXT_LEN = 64;  // per-slot string buffer\n"
        << "inline const DynTextData DYN_TEXTS[DYN_TEXT_COUNT > 0 ? DYN_TEXT_COUNT "
           ": 1] = {\n";
    if (slots.empty()) {
        out << "    {0, 0, 0, 0},\n";
    } else {
        for (const DynTextSlot& s : slots)
            out << "    {" << s.fontSlot << ", " << floatLit(s.x) << ", "
                << floatLit(s.y) << ", " << floatLit(s.size)
                << "},  // scene " << s.scene << ", object " << s.ownerIdx
                << ", node " << s.nodeId << "\n";
    }
    out << "};\n\n}  // namespace " << ns << "\n";
    return out.str();
}

// inc/hud_data.gen.hpp - HUD image sprites (PNG paths relative to bin/)
static std::string hudDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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
    out << "};\n\n"
        << "// Screen-stack positions of the full-screen effects (Tools > UI\n"
           "// Editor). The effect applies right before the HUD sprite at this\n"
           "// index, so lower-index sprites get it and higher ones draw crisp on\n"
           "// top. -1 = at end of frame, over everything including menus. Bloom\n"
           "// carries color grading; film grain is placed independently.\n"
        << "constexpr int HUD_BLOOM_LAYER = " << p.hudBloomLayer << ";\n"
        << "constexpr int HUD_GRAIN_LAYER = " << p.hudGrainLayer << ";\n";

    // The USE prompt (Tools > UI Editor): the built-in hud/use.png unless a
    // custom image replaces it; placement is normalized, center anchor.
    std::string usePath = p.usePrompt.imagePath;
    if (usePath.rfind("res/", 0) == 0) usePath = usePath.substr(4);
    if (usePath.empty()) usePath = "hud/use.png";
    out << "\n// The USE prompt sprite (shown while looking at a usable object)\n"
        << "constexpr const char* USE_PROMPT_PATH = \"" << usePath << "\";\n"
        << "constexpr float USE_PROMPT_X = " << floatLit(p.usePrompt.pos[0])
        << ";  // normalized, center anchor\n"
        << "constexpr float USE_PROMPT_Y = " << floatLit(p.usePrompt.pos[1]) << ";\n"
        << "constexpr float USE_PROMPT_W = " << floatLit(p.usePrompt.size[0])
        << ";  // on-screen pixels\n"
        << "constexpr float USE_PROMPT_H = " << floatLit(p.usePrompt.size[1]) << ";\n"
        << "// The \"PICK UP\" variant, shown instead for pickable objects\n"
           "// (same placement; replace res/hud/pickup.png to customize)\n"
           "constexpr const char* PICK_PROMPT_PATH = \"hud/pickup.png\";\n";

    // On-screen texts, baked to res/hud/text-*.png sprites by the editor
    // (menubake). Shown/hidden by the Show Text / Hide Text flow nodes.
    out << "\nstruct HudTextData {\n"
           "  const char* path;  // baked text sprite, relative to the ELF\n"
           "  float x, y;        // normalized screen position, center anchor\n"
           "  int w, h;          // texture size (pow2; content centered)\n"
           "  int visible;       // 1 = shown when the game starts\n"
           "};\n\n"
        << "constexpr int HUD_TEXT_COUNT = " << p.hudTexts.size() << ";\n"
        << "inline const HudTextData HUD_TEXTS[HUD_TEXT_COUNT > 0 ? "
           "HUD_TEXT_COUNT : 1] = {\n";
    if (p.hudTexts.empty()) {
        out << "    {\"\", 0, 0, 0, 0, 0},\n";
    } else {
        for (const HudText& t : p.hudTexts) {
            int tw = 8, th = 8;  // fallback if no usable font (bake errors out)
            menubake::textLayout(t, p, tw, th);
            out << "    {\"hud/" << menubake::textFileName(t.name) << "\", "
                << floatLit(t.pos[0]) << ", " << floatLit(t.pos[1]) << ", " << tw
                << ", " << th << ", " << (t.visibleAtStart ? 1 : 0) << "},  // "
                << t.name << "\n";
        }
    }
    out << "};\n"
        << "\n}  // namespace " << ns << "\n";
    return out.str();
}

// inc/loading_data.gen.hpp - loading screens (Tools > Loading Screens):
// per-screen element tables (images, baked texts, progress bars) plus the
// scene -> screen assignment. Colors are emitted in their final runtime
// ranges: screen background 0..255 (setClearScreenColor), bar tints 0..128
// (GS sprite modulation, 128 = 1.0).
static std::string loadingDataHeader(const Project& p) {
    const std::string ns = sanitizeNamespace(p.name);
    std::ostringstream out;
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns
        << " {\n\n"
           "struct LoadingImageData {\n"
           "  const char* path;  // relative to the game binary (res/ is copied there)\n"
           "  float x, y;        // normalized screen position, center anchor\n"
           "  float w, h;        // size in pixels\n"
           "};\n\n"
           "struct LoadingTextData {\n"
           "  const char* path;  // baked text sprite, relative to the ELF\n"
           "  float x, y;        // normalized screen position, center anchor\n"
           "  int w, h;          // texture size (pow2; content centered)\n"
           "};\n\n"
           "struct LoadingBarData {\n"
           "  int kind;              // 0 = continuous fill, 1 = quantized segments\n"
           "  float x, y;            // normalized screen position, center anchor\n"
           "  float w, h;            // total on-screen size in pixels\n"
           "  float bg[3], fill[3];  // sprite tints, GS range (128 = 1.0)\n"
           "  int segments;          // quantized only\n"
           "  float spacing;         // quantized: gap between segments, px\n"
           "  const char* segPath;   // segment sprite; \"\" = colored rects\n"
           "};\n\n"
           "struct LoadingScreenData {\n"
           "  float bg[3];  // clear color, 0..255\n"
           "  int imgFirst, imgCount;  // slices into LS_IMAGES/LS_TEXTS/LS_BARS\n"
           "  int txtFirst, txtCount;\n"
           "  int barFirst, barCount;\n"
           "};\n\n";

    auto binPath = [](std::string s) {
        if (s.rfind("res/", 0) == 0) s = s.substr(4);
        return s;
    };
    std::ostringstream imgs, txts, bars, screens;
    int nImg = 0, nTxt = 0, nBar = 0;
    for (size_t si = 0; si < p.loadingScreens.size(); ++si) {
        const LoadingScreenDef& ls = p.loadingScreens[si];
        screens << "    {{" << floatLit(ls.bgColor[0] * 255.0f) << ", "
                << floatLit(ls.bgColor[1] * 255.0f) << ", "
                << floatLit(ls.bgColor[2] * 255.0f) << "}, " << nImg << ", "
                << ls.images.size() << ", " << nTxt << ", " << ls.texts.size()
                << ", " << nBar << ", " << ls.bars.size() << "},  // "
                << ls.name << "\n";
        for (const HudImage& h : ls.images) {
            imgs << "    {\"" << binPath(h.imagePath) << "\", "
                 << floatLit(h.pos[0]) << ", " << floatLit(h.pos[1]) << ", "
                 << floatLit(h.size[0]) << ", " << floatLit(h.size[1])
                 << "},  // " << ls.name << ": " << h.name << "\n";
            ++nImg;
        }
        for (const HudText& t : ls.texts) {
            // Baked under the screen-index-mangled name; must match the
            // save-time bake in project.cpp (writeGeneratedAssets).
            HudText copy = t;
            copy.name = "ls-" + std::to_string(si) + "-" + t.name;
            int tw = 8, th = 8;  // fallback if no usable font
            menubake::textLayout(copy, p, tw, th);
            txts << "    {\"hud/" << menubake::textFileName(copy.name) << "\", "
                 << floatLit(t.pos[0]) << ", " << floatLit(t.pos[1]) << ", "
                 << tw << ", " << th << "},  // " << ls.name << ": " << t.name
                 << "\n";
            ++nTxt;
        }
        for (const LoadingBar& b : ls.bars) {
            bars << "    {" << b.kind << ", " << floatLit(b.pos[0]) << ", "
                 << floatLit(b.pos[1]) << ", " << floatLit(b.size[0]) << ", "
                 << floatLit(b.size[1]) << ", {"
                 << floatLit(b.bgColor[0] * 128.0f) << ", "
                 << floatLit(b.bgColor[1] * 128.0f) << ", "
                 << floatLit(b.bgColor[2] * 128.0f) << "}, {"
                 << floatLit(b.fillColor[0] * 128.0f) << ", "
                 << floatLit(b.fillColor[1] * 128.0f) << ", "
                 << floatLit(b.fillColor[2] * 128.0f) << "}, " << b.segments
                 << ", " << floatLit(b.spacing) << ", \""
                 << binPath(b.segImage.imagePath) << "\"},  // " << ls.name
                 << ": " << b.name << "\n";
            ++nBar;
        }
    }
    const int n = (int)p.loadingScreens.size();
    out << "constexpr int LS_COUNT = " << n << ";\n"
        << "constexpr int LS_IMAGE_TOTAL = " << nImg << ";\n"
        << "constexpr int LS_TEXT_TOTAL = " << nTxt << ";\n"
        << "constexpr int LS_BAR_TOTAL = " << nBar << ";\n"
        << "inline const LoadingScreenData LS_SCREENS[LS_COUNT > 0 ? LS_COUNT : 1] = {\n"
        << (n ? screens.str() : std::string("    {{0, 0, 0}, 0, 0, 0, 0, 0, 0},\n"))
        << "};\n\n"
        << "inline const LoadingImageData LS_IMAGES[" << (nImg > 0 ? nImg : 1)
        << "] = {\n"
        << (nImg ? imgs.str() : std::string("    {\"\", 0, 0, 0, 0},\n")) << "};\n\n"
        << "inline const LoadingTextData LS_TEXTS[" << (nTxt > 0 ? nTxt : 1)
        << "] = {\n"
        << (nTxt ? txts.str() : std::string("    {\"\", 0, 0, 0, 0},\n")) << "};\n\n"
        << "inline const LoadingBarData LS_BARS[" << (nBar > 0 ? nBar : 1)
        << "] = {\n"
        << (nBar ? bars.str()
                 : std::string("    {0, 0, 0, 0, 0, {0, 0, 0}, {0, 0, 0}, 0, 0, \"\"},\n"))
        << "};\n\n";

    // Scene -> screen assignment, resolved at codegen exactly like the editor
    // resolves it (name, else project default, else -1 = the built-in
    // hud/loading.png-on-black fallback).
    int def = p.defaultLoadingScreen;
    if (def < -1 || def >= n) def = -1;
    out << "constexpr int LS_DEFAULT = " << def << ";  // -1 = built-in fallback\n"
        << "inline const int SCENE_LOADING_SCREEN[" << p.scenes.size() << "] = {";
    for (size_t i = 0; i < p.scenes.size(); ++i)
        out << (i ? ", " : "") << project::loadingScreenIndexFor(p, p.scenes[i]);
    out << "};\n\n";

    // Boot splash screens: full-screen(ish) images shown in order at startup,
    // after the Tyra logo, each for `seconds`. Images only for now.
    out << "struct SplashData {\n"
           "  const char* path;      // relative to the game binary\n"
           "  float x, y, w, h;      // normalized center anchor + pixel size\n"
           "  float bg[3];           // clear color behind the image, 0..255\n"
           "  float seconds;         // time on screen\n"
           "};\n\n"
        << "constexpr int SPLASH_COUNT = " << p.splashScreens.size() << ";\n"
        << "inline const SplashData SPLASHES[SPLASH_COUNT > 0 ? SPLASH_COUNT : 1] = {\n";
    if (p.splashScreens.empty()) {
        out << "    {\"\", 0, 0, 0, 0, {0, 0, 0}, 0},\n";
    } else {
        for (const SplashScreen& s : p.splashScreens) {
            const HudImage& h = s.image;
            out << "    {\"" << binPath(h.imagePath) << "\", " << floatLit(h.pos[0])
                << ", " << floatLit(h.pos[1]) << ", " << floatLit(h.size[0]) << ", "
                << floatLit(h.size[1]) << ", {" << floatLit(s.bgColor[0] * 255.0f)
                << ", " << floatLit(s.bgColor[1] * 255.0f) << ", "
                << floatLit(s.bgColor[2] * 255.0f) << "}, " << floatLit(s.duration)
                << "},  // " << s.name << "\n";
        }
    }
    out << "};\n"
        << "\n}  // namespace " << ns << "\n";
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
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
           "#pragma once\n\nnamespace "
        << ns
        << " {\n\n"
           "// Menu entry actions: 0 close, 1 switch scene, 2 open save menu,\n"
           "// 3 open menu (submenu), 4 set save value, 5 add to save value,\n"
           "// 6 fire flow event, 7 toggle, 8 choice (7/8: param = the save\n"
           "// value holding the option index), 9 apply video mode (commits\n"
           "// the display-mode row's staged selection). param = resolved\n"
           "// index, -1 = unknown target.\n"
           "struct MenuEntryData {\n"
           "  int action;\n"
           "  int param;\n"
           "  float amount;\n"
           "  int optionCount;  // toggle/choice: how many options cycle\n"
           "  int cell;         // first cell in the value strip (-1 = none)\n"
           "  int bind;         // option-block binding (applyMenuBindings):\n"
           "                    // 0 none, 1 music vol, 2 sfx vol, 3 deadzone,\n"
           "                    // 4 stick curve, 5 display mode, 6 widescreen\n"
           "  // bind 5 only: the Tyra::DisplayMode each option drives\n"
           "  // (optionCount ints; -1 = the project-default boot mode).\n"
           "  // Null = the option index itself.\n"
           "  const int* optModes;\n"
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
           "  // Toggle/Choice value strip (\"\" = this menu has none): cell\n"
           "  // geometry mirrors menubake::valueStripLayout; valueX is the\n"
           "  // cell's left edge relative to the panel's left edge.\n"
           "  const char* values;\n"
           "  int valueCellW, valueCellH, valuePitch, valueX;\n"
           "};\n\n"
        << "constexpr int MENU_COUNT = " << p.menus.size() << ";\n\n";

    for (size_t mi = 0; mi < p.menus.size(); ++mi) {
        const GameMenu& m = p.menus[mi];
        const int entries = (int)m.entries.size() > menubake::kMaxEntries
                                ? menubake::kMaxEntries
                                : (int)m.entries.size();
        const menubake::ValueStripLayout vl = menubake::valueStripLayout(m);
        out << "// menu \"" << m.name << "\"\n";
        // Explicit option->mode tables for display-mode rows (see
        // MenuEntryData::optModes); rows without one keep the positional map.
        for (int e = 0; e < entries; ++e) {
            const MenuEntry& en = m.entries[e];
            const bool stateful = en.action == MenuEntry::Toggle ||
                                  en.action == MenuEntry::Choice;
            if (!stateful || en.settingBind != MenuEntry::BindDisplayMode ||
                en.optionModes.empty())
                continue;
            const int optionCount = (int)menubake::entryOptionLabels(en).size();
            if (optionCount <= 0) continue;
            out << "constexpr int MENU_" << mi << "_E" << e << "_MODES["
                << optionCount << "] = {";
            for (int o = 0; o < optionCount; ++o) {
                int mode = o < (int)en.optionModes.size() ? en.optionModes[o]
                                                          : (o < 4 ? o : 4);
                // Tyra::DisplayMode range; -1 = the project-default option
                if (mode < -1) mode = -1;
                if (mode > 4) mode = 4;
                out << (o ? ", " : "") << mode;
            }
            out << "};\n";
        }
        out << "constexpr MenuEntryData MENU_" << mi << "_ENTRIES["
            << (entries > 0 ? entries : 1) << "] = {\n";
        if (entries == 0) {
            out << "    {0, -1, 0.0F, 0, -1, 0, nullptr},\n";
        } else {
            for (int e = 0; e < entries; ++e) {
                const MenuEntry& en = m.entries[e];
                int param = -1;
                switch (en.action) {
                    case MenuEntry::SwitchScene: param = sceneIndexOf(en.param); break;
                    case MenuEntry::OpenMenu: param = menuIndexOf(en.param); break;
                    case MenuEntry::SetValue:
                    case MenuEntry::AddValue:
                    case MenuEntry::Toggle:
                    case MenuEntry::Choice: param = valueIndexOf(en.param); break;
                    case MenuEntry::FlowEvent: param = eventIndexOf(en.param); break;
                    default: break;
                }
                const int optionCount =
                    (int)menubake::entryOptionLabels(en).size();
                const int cell = e < (int)vl.firstCell.size() ? vl.firstCell[e] : -1;
                // Bindings only make sense on stateful (Toggle/Choice) rows.
                const int bind = (en.action == MenuEntry::Toggle ||
                                  en.action == MenuEntry::Choice)
                                     ? en.settingBind
                                     : 0;
                const bool hasModes = bind == MenuEntry::BindDisplayMode &&
                                      !en.optionModes.empty() && optionCount > 0;
                out << "    {" << en.action << ", " << param << ", "
                    << floatLit(en.amount) << ", " << optionCount << ", " << cell
                    << ", " << bind << ", ";
                if (hasModes)
                    out << "MENU_" << mi << "_E" << e << "_MODES";
                else
                    out << "nullptr";
                out << "},  // " << en.label << "\n";
            }
        }
        out << "};\n";
    }
    if (p.menus.empty())
        out << "constexpr MenuEntryData MENU_0_ENTRIES[1] = {{0, -1, 0.0F, 0, -1, 0, nullptr}};\n";

    int titleMenu = -1;
    for (size_t mi = 0; mi < p.menus.size(); ++mi)
        if (p.menus[mi].titleScreen && titleMenu < 0) titleMenu = (int)mi;

    int pauseMenu = -1;
    for (size_t mi = 0; mi < p.menus.size(); ++mi)
        if (p.menus[mi].pauseMenu && pauseMenu < 0) pauseMenu = (int)mi;

    out << "\ninline const MenuData MENUS[MENU_COUNT > 0 ? MENU_COUNT : 1] = {\n";
    if (p.menus.empty()) {
        out << "    {\"\", 0, 0, 0, 0, 0, 0, MENU_0_ENTRIES, 0, 0, 0.5F, 0.45F, "
               "\"\", 0, 0, 0, 0},\n";
        // (unreachable - MENU_COUNT is 0; the dummy keeps the array valid)
    } else {
        for (size_t mi = 0; mi < p.menus.size(); ++mi) {
            const GameMenu& m = p.menus[mi];
            const int entries = (int)m.entries.size() > menubake::kMaxEntries
                                    ? menubake::kMaxEntries
                                    : (int)m.entries.size();
            // Layout depends on the custom images (flow blocks push the
            // cursor rows down) - the baker is the single source of truth.
            const menubake::PanelLayout l = menubake::panelLayout(m, p);
            const menubake::ValueStripLayout vl = menubake::valueStripLayout(m);
            const bool hasValues = menubake::menuHasValueEntries(m);
            out << "    {\"menus/" << menubake::panelFileName(m.name) << "\", "
                << l.panelW << ", " << l.canvasH << ", " << l.contentH << ", "
                << l.row0Y << ", " << l.rowH << ", " << entries
                << ", MENU_" << mi << "_ENTRIES, " << (m.titleScreen ? 1 : 0)
                << ", " << (m.pauseGame ? 1 : 0) << ", " << floatLit(m.screenPos[0])
                << ", " << floatLit(m.screenPos[1]) << ", ";
            if (hasValues)
                out << "\"menus/" << menubake::valueStripFileName(m.name) << "\", "
                    << vl.cellW << ", " << vl.cellH << ", " << vl.pitch << ", "
                    << (l.panelW - 24 - vl.cellW);
            else
                out << "\"\", 0, 0, 0, 0";
            out << "},  // " << m.name << "\n";
        }
    }
    bool hasApplyVideo = false;
    for (const GameMenu& m : p.menus) {
        const int entries = (int)m.entries.size() > menubake::kMaxEntries
                                ? menubake::kMaxEntries
                                : (int)m.entries.size();
        for (int e = 0; e < entries; ++e)
            hasApplyVideo |= m.entries[e].action == MenuEntry::ApplyVideo;
    }

    out << "};\n\n"
        << "constexpr int TITLE_MENU = " << titleMenu << ";\n"
        << "// The Start button opens/closes this menu in-game (-1 = none)\n"
        << "constexpr int PAUSE_MENU = " << pauseMenu << ";\n"
        << "// True when any menu carries an \"apply video mode\" row (action\n"
        << "// 9): display-mode rows then only stage a selection and that row\n"
        << "// commits it; without one they switch on change (the classic\n"
        << "// behavior).\n"
        << "constexpr bool MENU_HAS_APPLY_VIDEO = "
        << (hasApplyVideo ? "true" : "false") << ";\n\n"
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
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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
    out << "// Generated by TyraX. Do not edit - regenerated on every build.\n"
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

// .vscode/extensions.json - recommends the TyraX VS Code extension, which adds
// syntax highlighting / validation for the project's .flownode and .screenfx
// files (see tools/vscode-tyrax). Static and machine-independent, so unlike
// c_cpp_properties.json it is written only when missing (refreshGenerated),
// preserving any recommendations the user adds. The id matches the extension's
// publisher.name so VS Code doesn't re-prompt once it is installed.
static std::string vscodeExtensionsJson() {
    return "{\n"
           "  \"recommendations\": [\n"
           "    \"tyrax.tyrax-flownode\"\n"
           "  ]\n"
           "}\n";
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
        if (!animimport::parseSkel(full, skel, error)) {
            warn(relPath + ": " + error);
            continue;
        }
        for (const std::string& w : skel.warnings) warn(relPath + ": " + w);
        // Distance LODs ride in the .tskl only when something uses them -
        // the engine keeps every loaded LOD (plus per-instance skinning
        // buffers) in the PS2's 32 MB, so an unused chain is pure waste.
        // "Uses" = the project preference, or any object referencing this
        // model with a per-object mesh-LOD override > 0.
        bool lodWanted = p.settings.meshLodDistance > 0.0f;
        for (const SceneData& sc : p.scenes)
            for (const SceneObject& obj : sc.objects)
                if (obj.meshLodOverride > 0.0f && obj.modelPath == relPath)
                    lodWanted = true;
        if (lodWanted) glbparser::generateSkelLods(skel);

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
    out << "// Generated by TyraX from the per-object script attachments. Do\n"
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
        {"inc\\font_data.gen.hpp", fontDataHeader(p)},
        {"inc\\loading_data.gen.hpp", loadingDataHeader(p)},
        {"inc\\terrain_heights.gen.hpp", terrainHeightsHeader(p)},
        {"inc\\nav_data.gen.hpp", navDataHeader(p)},
        {"inc\\scripts\\navigation.gen.hpp", navigationHeader(p)},
        {"src\\scripts\\navigation.gen.cpp", navigationSource(p)},
        {"inc\\texture_data.gen.hpp", textureDataHeader(p)},
        {"inc\\decal_data.gen.hpp", decalDataHeader(p)},
        {"inc\\save_system.gen.hpp", saveSystemHeader(p)},
        {"src\\save_system.gen.cpp", saveSystemSource(p)},
        {"inc\\menu_data.gen.hpp", menuDataHeader(p)},
        {"inc\\scripts\\script.hpp", fill(TPL_SCRIPT_HPP)},
        {"inc\\scripts\\sequences.gen.hpp", sequencesHeader(p)},
        {"src\\scripts\\sequences.gen.cpp", sequencesScript(p)},
        {"inc\\scripts\\flow_nodes.hpp", fill(TPL_FLOW_NODES_HPP)},
        {"src\\scripts\\flow_graph.gen.cpp", flowGraphScript(p)},
        {"src\\scripts\\live_link.gen.cpp", liveLinkScript(p)},
        {"inc\\scripts\\screen_fx.gen.hpp", screenFxHeader(p)},
        {"src\\scripts\\screen_fx.gen.cpp", screenFxSource(p)},
        {"src\\scripts\\object_scripts.gen.cpp", objectScriptsSource(p)},
        {"src\\scripts\\example_interaction.cpp",
         fill(fpp ? TPL_EXAMPLE_SCRIPT_FPP : TPL_EXAMPLE_SCRIPT_ORBIT)},
        {".vscode\\c_cpp_properties.json", vscodeCppProperties()},
        {".vscode\\extensions.json", vscodeExtensionsJson()},
        {"run.ps1", fill(TPL_RUN_PS1)},
        {"windows-pcsx2.ps1", fill(TPL_PCSX2_PS1)},
        {".gitignore", fill(TPL_GITIGNORE)},
        {".gitattributes", TPL_GITATTRIBUTES},
        {"COLLABORATION.md", TPL_COLLABORATION},
        {"res\\.gitignore", TPL_RES_GITIGNORE},
        {"bin\\.gitignore", TPL_DIR_KEEP},
        {"obj\\.gitignore", TPL_DIR_KEEP},
    };
}

}  // namespace templates
