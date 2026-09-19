"""Compile the static-batch twins and diff their grouping member for member.

Run with Python 3 and g++ on PATH. No emulator, no third-party Python modules.

WHY THIS EXISTS. The static-batch grouping has two implementations and cannot
have one. `TerrainGame::buildStaticBatchList` is generated code that runs on
the EE at scene load and reads things that only exist there - the loaded
gameModels/gameMaterials, the live g_dynLights list, and the engine's own
Texture* pointers - so it cannot move host-side without baking a batch table
into inc/scene_data.hpp, i.e. changing the generated output of every project
that exists. src/staticbatch.cpp is therefore a host TWIN, and this is its
oracle: the runtime function is lifted VERBATIM out of templates.cpp (never
copied into this file - a copy is the drift it exists to catch), compiled
against small stubs, and run beside the twin on the same fixtures.

This is verify-road-twins.py's arrangement, for the same reason and after the
same accident: the repository has already paid once for a preview that
disagreed with the console, because a preview that is believed is worse than
no preview at all.

WHAT IT CHECKS, per fixture: the same number of batches, the same membership
(as a set of (object, part) pairs per batch, order-independent since neither
side promises an order), and the same per-object verdict of batched vs solo.

THE MISSING-TEXTURE FIXTURE IS NOT FILLER. acquireTexture caches by path and
hands back a NULL pointer for a file that is not on disk, so in the running
game every missing texture groups with every untextured primitive. It looks
like a bug, it is the behaviour, and a twin that "fixed" it would make the
Static Batches panel confidently wrong. Fixture E pins it.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[3]
templates = (root / 'src/templates.cpp').read_text(encoding='utf-8', errors='replace')

# --- lift the runtime half, verbatim ---------------------------------------
START = 'void TerrainGame::buildStaticBatchList() {'
try:
    start = templates.index(START)
except ValueError:
    sys.exit('FAIL: buildStaticBatchList not found in src/templates.cpp - '
             'the oracle cannot lift what it cannot find (did the function '
             'get renamed?)')
end = templates.index('\n}\n', start) + 3
runtime = templates[start:end]

# The lifted body must still contain the decisions this oracle is about. If a
# key silently leaves the grouping, a diff of two agreeing implementations
# proves nothing - so assert the shape of what was lifted, the way
# verify-road-twins.py pins the greedy span extension by text.
for needle, what in [
    ('stapip.core.render', 'nothing - sanity'),
    ('cellFor', 'the draw-distance cell cap'),
    ('lampOf', 'the reaching-lamp key'),
    ('stripRun', 'the strip-run key'),
    ('members.size() < 2', 'the singleton drop'),
]:
    if needle == 'stapip.core.render':
        continue
    if needle not in runtime:
        sys.exit(f'FAIL: the lifted buildStaticBatchList no longer mentions '
                 f'{needle!r} ({what}). Either the grouping changed shape or '
                 f'the lift is truncated; fix the oracle deliberately.')

STUB = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#define TYRA_LOG(...) ((void)0)

// Only what buildStaticBatchList touches. Storage and rendering are stubbed;
// nothing that decides a grouping is.
namespace Tyra {
struct Texture {};
enum ShadingType { TyraShadingFlat, TyraShadingGouraud };
enum FrustumCulling { PipelineInfoBagFrustumCulling_Precise };
struct StaPipInfoBag {
  void* model = nullptr;
  int shadingType = 0;
  int frustumCulling = 0;
  bool fullClipChecks = false;
};
}
using Tyra::Texture;
using Tyra::StaPipInfoBag;
static const int TyraShadingGouraud = 1;
static const int PipelineInfoBagFrustumCulling_Precise = 1;

struct SceneObjectData {
  int type = 0;
  float position[3] = {0, 0, 0};
  float scale[3] = {1, 1, 1};
  float drawDistance = 0.0f;
  int model = -1;
  int material = -1;
  int batchStatic = 0;
  float lightRadius = 0.0f;
};

struct GameModelPart { Texture* texture = nullptr; Texture* reflTexture = nullptr;
                       unsigned int stripRun = 0; };
struct GameModel { float mn[3] = {0,0,0}, mx[3] = {0,0,0};
                   std::vector<GameModelPart> parts; };
struct GameMaterial { Texture* texture = nullptr; Texture* reflTexture = nullptr; };

struct StaticBatchMember { int object; int part; };
struct StaticBatch {
  Texture* texture = nullptr;
  float drawDistance = 0.0f;
  unsigned int stripRun = 0;
  std::vector<StaticBatchMember> members;
  bool dirty = true;
  std::unique_ptr<int> bag;
};
struct RuntimeObjectStub { bool dirty = false; };
struct DynLightRt { int objIndex = 0; };

// The scene under test, written by the fixture builder below.
static int SCENE_OBJECT_COUNT = 0;
static std::vector<SceneObjectData> g_objects;
#define SCENE_OBJECTS g_objects
static int STATIC_BATCHING = 1;
static float TERRAIN_WIDTH = 100.0f, TERRAIN_DEPTH = 100.0f;
static const char* TEXTURE_ATLAS_INFO = "";
static std::vector<DynLightRt> g_dynLights;

struct TerrainGame {
  std::vector<StaticBatch> staticBatches;
  std::vector<short> objectBatchOf;
  std::vector<RuntimeObjectStub> runtimeObjects;
  std::vector<GameModel> gameModels;
  std::vector<GameMaterial> gameMaterials;
  std::unique_ptr<StaPipInfoBag> batchInfoBag;
  int model = 0;
  void buildStaticBatchList();
};
'''

# --- the host twin, and the three project:: helpers it calls ----------------
# blockedNames is stage ONE (eligibility), which already has its own oracle -
# the batchStatic column of any generated inc/scene_data.hpp. This harness is
# about stage TWO, the grouping, so those three are stubbed rather than
# linking the whole of project.cpp. The fixtures set batchStatic directly on
# the runtime side and batchExclude/type on the host side, so nothing here
# depends on what they would have returned.
HOST_STUBS = r'''
#include "staticbatch.hpp"
namespace project {
std::set<std::string> runtimeRefNames(const Project&,
                                      const std::vector<SceneObject>&) {
  return {};
}
std::vector<int> areaCaughtObjects(const std::vector<SceneObject>&,
                                   const std::string&, int) { return {}; }
std::vector<int> areaLiveCandidates(const std::vector<SceneObject>&, int,
                                    const std::set<std::string>&) { return {}; }
}
'''

MAIN = r'''
#include <map>
#include <set>
#include <sstream>

// ---------------------------------------------------------------------------
// A fixture, described once and handed to BOTH sides.
// ---------------------------------------------------------------------------
struct FxPart { std::string texture; bool reflective = false;
                unsigned stripRun = 0; unsigned verts = 75; };
struct FxModel { std::string path; float mn[3]; float mx[3];
                 std::vector<FxPart> parts; };
struct FxObject {
  int type = 0;                 // 0 box, 5 model, 9 point light
  float pos[3] = {0, 0, 0};
  float scale[3] = {1, 1, 1};
  float drawDistance = 0.0f;
  std::string modelPath;        // type 5
  std::string materialPath;     // primitives
  bool eligible = true;         // what stage 1 decided
  bool dynamicLight = false;    // type 9
  float lightRadius = 0.0f;
};
struct FxMaterial { std::string path; std::string texture; bool reflective = false;
                    bool known = true; };
struct Fixture {
  std::string name;
  float terrainW = 100.0f, terrainD = 100.0f;
  bool batching = true;
  std::vector<FxModel> models;
  std::vector<FxMaterial> materials;
  std::vector<FxObject> objects;
};

// A canonical, order-independent picture of a grouping: for each batch, the
// sorted set of its (object, part) members; the batches themselves sorted so
// neither side's insertion order can make an equal grouping look different.
using Picture = std::vector<std::vector<std::pair<int,int>>>;
static std::string show(const Picture& p) {
  std::ostringstream o;
  for (const auto& b : p) {
    o << "  [";
    for (size_t i = 0; i < b.size(); ++i)
      o << (i ? " " : "") << b[i].first << ':' << b[i].second;
    o << "]\n";
  }
  return o.str();
}
static Picture canon(Picture p) {
  for (auto& b : p) std::sort(b.begin(), b.end());
  std::sort(p.begin(), p.end());
  return p;
}

// --- run the lifted runtime -------------------------------------------------
static Picture runRuntime(const Fixture& fx, std::vector<char>& batchedOut) {
  // Texture identity is a POINTER on this side, exactly as in the game. One
  // pointer per distinct texture NAME, and - the case that matters - a single
  // shared nullptr for "untextured or missing", which is what acquireTexture
  // returns for a file that is not on disk.
  std::map<std::string, Texture*> tex;
  std::vector<std::unique_ptr<Texture>> owned;
  auto texFor = [&](const std::string& name) -> Texture* {
    if (name.empty()) return nullptr;          // untextured / missing
    auto it = tex.find(name);
    if (it != tex.end()) return it->second;
    owned.push_back(std::make_unique<Texture>());
    return tex[name] = owned.back().get();
  };

  TerrainGame g;
  TERRAIN_WIDTH = fx.terrainW;
  TERRAIN_DEPTH = fx.terrainD;
  STATIC_BATCHING = fx.batching ? 1 : 0;

  std::map<std::string, int> modelIndex;
  for (const FxModel& m : fx.models) {
    GameModel gm;
    for (int a = 0; a < 3; ++a) { gm.mn[a] = m.mn[a]; gm.mx[a] = m.mx[a]; }
    for (const FxPart& p : m.parts) {
      GameModelPart gp;
      gp.texture = texFor(p.texture);
      gp.reflTexture = p.reflective ? texFor(m.path + "@refl") : nullptr;
      gp.stripRun = p.stripRun;
      gm.parts.push_back(gp);
    }
    modelIndex[m.path] = (int)g.gameModels.size();
    g.gameModels.push_back(gm);
  }
  std::map<std::string, int> matIndex;
  for (const FxMaterial& m : fx.materials) {
    GameMaterial gm;
    gm.texture = texFor(m.texture);
    gm.reflTexture = m.reflective ? texFor(m.path + "@refl") : nullptr;
    matIndex[m.path] = (int)g.gameMaterials.size();
    g.gameMaterials.push_back(gm);
  }

  g_objects.clear();
  g_dynLights.clear();
  for (const FxObject& o : fx.objects) {
    SceneObjectData d;
    d.type = o.type;
    for (int a = 0; a < 3; ++a) { d.position[a] = o.pos[a]; d.scale[a] = o.scale[a]; }
    d.drawDistance = o.drawDistance;
    d.model = o.modelPath.empty() ? -1 : modelIndex[o.modelPath];
    d.material = o.materialPath.empty() ? -1 : matIndex[o.materialPath];
    d.batchStatic = o.eligible ? 1 : 0;
    d.lightRadius = o.lightRadius;
    g_objects.push_back(d);
  }
  SCENE_OBJECT_COUNT = (int)g_objects.size();
  // collectScenePointLights' own rule: type 9 + dynamic, scene order, capped.
  for (int i = 0; i < SCENE_OBJECT_COUNT; ++i)
    if (fx.objects[i].type == 9 && fx.objects[i].dynamicLight &&
        g_dynLights.size() < 8) {
      DynLightRt d; d.objIndex = i; g_dynLights.push_back(d);
    }
  g.runtimeObjects.assign(g_objects.size(), RuntimeObjectStub());

  g.buildStaticBatchList();

  Picture pic;
  for (const StaticBatch& b : g.staticBatches) {
    std::vector<std::pair<int,int>> m;
    for (const StaticBatchMember& mm : b.members) m.push_back({mm.object, mm.part});
    pic.push_back(m);
  }
  batchedOut.assign(g_objects.size(), 0);
  for (size_t i = 0; i < g.objectBatchOf.size(); ++i)
    batchedOut[i] = g.objectBatchOf[i] != -1 ? 1 : 0;
  return canon(pic);
}

// --- run the host twin ------------------------------------------------------
static Picture runHost(const Fixture& fx, std::vector<char>& batchedOut) {
  Project p;
  p.settings.staticBatching = fx.batching;
  SceneData sc;
  sc.terrain.width = (int)fx.terrainW;
  sc.terrain.depth = (int)fx.terrainD;
  for (size_t i = 0; i < fx.objects.size(); ++i) {
    const FxObject& o = fx.objects[i];
    SceneObject so;
    so.name = "o" + std::to_string(i);
    so.type = (PrimitiveType)o.type;
    for (int a = 0; a < 3; ++a) { so.position[a] = o.pos[a]; so.scale[a] = o.scale[a]; }
    so.drawDistance = o.drawDistance;
    so.modelPath = o.modelPath;
    so.materialPath = o.materialPath;
    so.lightDynamic = o.dynamicLight;
    so.lightRadius = o.lightRadius;
    // Stage 1 is oracled separately (the batchStatic column of a generated
    // scene_data.hpp), so an ineligible fixture object is expressed with the
    // one lever that is a pure stage-1 refusal and nothing else.
    so.batchExclude = !o.eligible;
    sc.objects.push_back(so);
  }

  std::map<std::string, const FxModel*> models;
  for (const FxModel& m : fx.models) models[m.path] = &m;
  std::map<std::string, const FxMaterial*> mats;
  for (const FxMaterial& m : fx.materials) mats[m.path] = &m;

  staticbatch::Inputs in;
  in.model = [&](const std::string& path) {
    staticbatch::ModelInfo mi;
    auto it = models.find(path);
    if (it == models.end()) return mi;
    mi.baked = true;
    for (int a = 0; a < 3; ++a) { mi.min[a] = it->second->mn[a]; mi.max[a] = it->second->mx[a]; }
    for (const FxPart& p : it->second->parts) {
      staticbatch::ModelPart mp;
      mp.texture = p.texture;
      mp.reflective = p.reflective;
      mp.stripRun = p.stripRun;
      mp.vertexCount = p.verts;
      mp.stripVertexCount = p.stripRun ? p.verts : 0u;
      mi.parts.push_back(mp);
    }
    return mi;
  };
  in.material = [&](const std::string& path) {
    staticbatch::MaterialInfo mi;
    auto it = mats.find(path);
    if (it == mats.end()) return mi;
    mi.known = it->second->known;
    mi.texture = it->second->texture;
    mi.reflective = it->second->reflective;
    return mi;
  };
  in.primVertices = [](const SceneObject&) { return 36u; };

  staticbatch::Result r = staticbatch::compute(p, sc, in, {});
  Picture pic;
  for (const auto& b : r.batches) {
    std::vector<std::pair<int,int>> m;
    for (const auto& mm : b.members) m.push_back({mm.object, mm.part});
    pic.push_back(m);
  }
  batchedOut.assign(sc.objects.size(), 0);
  for (size_t i = 0; i < r.objects.size(); ++i)
    batchedOut[i] = r.objects[i].reason == staticbatch::Reason::Batched ? 1 : 0;
  return canon(pic);
}

static int failures = 0;
static void check(const Fixture& fx) {
  std::vector<char> rb, hb;
  Picture r = runRuntime(fx, rb);
  Picture h = runHost(fx, hb);
  bool ok = (r == h) && (rb == hb);
  std::printf("%-34s %s  (%d batches)\n", fx.name.c_str(),
              ok ? "AGREE" : "*** DISAGREE ***", (int)r.size());
  if (!ok) {
    ++failures;
    std::printf("  runtime:\n%s  host:\n%s", show(r).c_str(), show(h).c_str());
    if (rb != hb) {
      std::printf("  batched flags differ:\n   runtime:");
      for (char c : rb) std::printf(" %d", c);
      std::printf("\n   host:   ");
      for (char c : hb) std::printf(" %d", c);
      std::printf("\n");
    }
  }
}

static FxObject box(float x, float z, const std::string& mat,
                    float dd = 0.0f, float s = 1.0f) {
  FxObject o; o.type = 0; o.pos[0] = x; o.pos[2] = z; o.materialPath = mat;
  o.drawDistance = dd; o.scale[0] = o.scale[1] = o.scale[2] = s; return o;
}
static FxObject mdl(float x, float z, const std::string& path,
                    float dd = 0.0f, float s = 1.0f) {
  FxObject o; o.type = 5; o.pos[0] = x; o.pos[2] = z; o.modelPath = path;
  o.drawDistance = dd; o.scale[0] = o.scale[1] = o.scale[2] = s; return o;
}

int main() {
  // A: the plain merge - three boxes, one texture, one cell.
  {
    Fixture fx; fx.name = "A plain merge";
    fx.materials = {{"m/stone.mtl", "stone.png"}};
    fx.objects = {box(0,0,"m/stone.mtl"), box(3,0,"m/stone.mtl"),
                  box(0,3,"m/stone.mtl")};
    check(fx);
  }
  // B: two textures never merge, and a singleton is dropped.
  {
    Fixture fx; fx.name = "B texture splits, singleton drops";
    fx.materials = {{"m/a.mtl","a.png"},{"m/b.mtl","b.png"}};
    fx.objects = {box(0,0,"m/a.mtl"), box(2,0,"m/a.mtl"), box(4,0,"m/b.mtl")};
    check(fx);
  }
  // C: the draw-distance cell cap. A 2048 map gives a 512 base cell; a
  // 60-unit cut-off caps these to 60, so objects 200 apart split.
  {
    Fixture fx; fx.name = "C draw-distance caps the cell";
    fx.terrainW = fx.terrainD = 2048.0f;
    fx.materials = {{"m/c.mtl","c.png"}};
    fx.objects = {box(-900,0,"m/c.mtl",60.0f), box(-880,0,"m/c.mtl",60.0f),
                  box(-400,0,"m/c.mtl",60.0f), box(-380,0,"m/c.mtl",60.0f)};
    check(fx);
  }
  // D: the reaching lamp is part of the key - same texture, same cell, one
  // lit and one not.
  {
    Fixture fx; fx.name = "D reaching lamp splits the key";
    fx.materials = {{"m/d.mtl","d.png"}};
    FxObject lamp; lamp.type = 9; lamp.dynamicLight = true;
    lamp.lightRadius = 6.0f; lamp.pos[0] = 0; lamp.pos[2] = 0;
    lamp.eligible = false;
    fx.objects = {lamp, box(1,0,"m/d.mtl"), box(2,0,"m/d.mtl"),
                  box(20,0,"m/d.mtl"), box(21,0,"m/d.mtl")};
    check(fx);
  }
  // E: THE MISSING TEXTURE. `known=false` is a material whose texture file is
  // not on disk; the game's acquireTexture returns nullptr for it, which is
  // the SAME pointer an untextured primitive binds - so the two merge. A twin
  // that treated "missing" as its own group would split this into singletons
  // and drop both, and the panel would report no batch where the game builds
  // one.
  {
    Fixture fx; fx.name = "E missing texture merges with untextured";
    fx.materials = {{"m/gone.mtl", "", false, false}};
    FxObject untextured = box(0,0,"");       // no material at all
    fx.objects = {untextured, box(2,0,"m/gone.mtl"), box(4,0,"m/gone.mtl")};
    check(fx);
  }
  // F: a model too big for its cell stays solo, while its small neighbours
  // merge. Half-cell guard on a 100-unit map (base cell 48, half 24).
  {
    Fixture fx; fx.name = "F oversized model stays solo";
    FxModel small{"small.obj", {-1,-1,-1}, {1,1,1}, {{"t.png", false, 0, 90}}};
    FxModel big{"big.obj", {-30,-2,-30}, {30,2,30}, {{"t.png", false, 0, 900}}};
    fx.models = {small, big};
    fx.objects = {mdl(0,0,"small.obj"), mdl(3,0,"small.obj"),
                  mdl(6,0,"big.obj")};
    check(fx);
  }
  // G: a reflective model is refused whole, even though its parts share a
  // texture with batchable neighbours.
  {
    Fixture fx; fx.name = "G reflective model refused whole";
    FxModel plain{"p.obj", {-1,-1,-1}, {1,1,1}, {{"t.png", false, 0, 90}}};
    FxModel shiny{"s.obj", {-1,-1,-1}, {1,1,1}, {{"t.png", true, 0, 90}}};
    fx.models = {plain, shiny};
    fx.objects = {mdl(0,0,"p.obj"), mdl(2,0,"p.obj"), mdl(4,0,"s.obj")};
    check(fx);
  }
  // H: the strip run is part of the key - two models sharing a texture and a
  // cell but baked with different run lengths must not share a bag, because
  // one array carries one topology.
  {
    Fixture fx; fx.name = "H strip run splits the key";
    FxModel a{"a.obj", {-1,-1,-1}, {1,1,1}, {{"t.png", false, 75, 150}}};
    FxModel b{"b.obj", {-1,-1,-1}, {1,1,1}, {{"t.png", false, 0, 150}}};
    fx.models = {a, b};
    fx.objects = {mdl(0,0,"a.obj"), mdl(2,0,"a.obj"),
                  mdl(4,0,"b.obj"), mdl(6,0,"b.obj")};
    check(fx);
  }
  // I: a multi-part model - one part merges with a neighbour, the other is
  // alone and drops. The OBJECT is still batched (objectBatchOf != -1).
  {
    Fixture fx; fx.name = "I multi-part model spans batches";
    FxModel two{"two.obj", {-1,-1,-1}, {1,1,1},
                {{"shared.png", false, 0, 90}, {"lonely.png", false, 0, 90}}};
    FxModel one{"one.obj", {-1,-1,-1}, {1,1,1}, {{"shared.png", false, 0, 90}}};
    fx.models = {two, one};
    fx.objects = {mdl(0,0,"two.obj"), mdl(2,0,"one.obj")};
    check(fx);
  }
  // J: batching off project-wide builds nothing at all.
  {
    Fixture fx; fx.name = "J batching off";
    fx.batching = false;
    fx.materials = {{"m/j.mtl","j.png"}};
    fx.objects = {box(0,0,"m/j.mtl"), box(2,0,"m/j.mtl")};
    check(fx);
  }
  // K: an ineligible object never reaches the grouping, and its absence must
  // not silently rescue a batch from the singleton rule.
  {
    Fixture fx; fx.name = "K ineligible drops its group to one";
    fx.materials = {{"m/k.mtl","k.png"}};
    FxObject out = box(2,0,"m/k.mtl"); out.eligible = false;
    fx.objects = {box(0,0,"m/k.mtl"), out};
    check(fx);
  }

  if (failures) {
    std::printf("\n%d fixture(s) DISAGREE - the twins have drifted.\n", failures);
    return 1;
  }
  std::printf("\nAll fixtures agree.\n");
  return 0;
}
'''

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    (td / 'runtime.cpp').write_text(STUB + runtime + HOST_STUBS + MAIN, encoding='utf-8')
    exe = td / ('twins.exe' if sys.platform == 'win32' else 'twins')
    cmd = ['g++', '-std=c++20', '-O0', '-I', str(root / 'src'),
           str(td / 'runtime.cpp'), str(root / 'src/staticbatch.cpp'),
           '-o', str(exe)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode != 0:
        print(build.stdout)
        print(build.stderr)
        sys.exit('FAIL: the twin harness did not compile')
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    print(run.stdout, end='')
    if run.stderr:
        print(run.stderr, end='')
    sys.exit(run.returncode)
