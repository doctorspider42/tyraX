// Lit street lamps and wet roads (docs/weather.md). See roadlight.hpp.
#include "roadlight.hpp"

#include <math.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>

#include "livelogic.hpp"  // the --vehicle-check Live Logic block only
#include "project.hpp"    // the --vehicle-check codegen block only
#include "roaddetail.hpp"
#include "templates.hpp"
#include "weather_core_gen.hpp"

namespace roadlight {

// The core, compiled for the host. The game gets the same text inside its
// namespace weather (weatherHeaderSource below).
namespace weather {
#include "weather_core.inl"
}

namespace {

std::string lit(float v, int digits = 7) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.*g", digits, (double)v);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s + "F";
}

std::string replaceOnce(std::string s, const std::string& from, const std::string& to,
                        std::vector<std::string>* misses, const char* what) {
    const size_t at = s.find(from);
    if (at == std::string::npos) {
        if (misses) misses->push_back(what);
        return s;
    }
    s.replace(at, from.size(), to);
    return s;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos) {
        s.replace(at, from.size(), to);
        at += to.size();
    }
    return s;
}

// The built-in lamp's lens colour (roadfurniture.cpp kLens), unshaded - so a
// vertex of exactly this colour IS the lens.
constexpr float kLensRgb[3] = {1.0f, 0.93f, 0.72f};

}  // namespace

// --- lamps ---------------------------------------------------------------------

std::vector<Lamp> lampsOf(const roadfurn::Result& furniture) {
    std::vector<Lamp> out;
    for (size_t ii = 0; ii < furniture.instances.size(); ++ii) {
        const roadfurn::Instance& inst = furniture.instances[ii];
        if (inst.kind != roadfurn::kLamp || inst.vertexCount <= 0) continue;
        Lamp L;
        L.road = inst.road;
        L.instance = (int)ii;
        float sx = 0, sy = 0, sz = 0;
        int lens = 0;
        float maxH = 0.0f;
        for (int k = 0; k < inst.vertexCount; ++k) {
            const roadfurn::Vertex& v = furniture.tris[(size_t)(inst.firstVertex + k)];
            maxH = std::max(maxH, v.y - inst.y);
            if (std::fabs(v.r - kLensRgb[0]) < 1e-4f && std::fabs(v.g - kLensRgb[1]) < 1e-4f &&
                std::fabs(v.b - kLensRgb[2]) < 1e-4f) {
                sx += v.x, sy += v.y, sz += v.z;
                ++lens;
            }
        }
        if (lens > 0) {
            L.hx = sx / (float)lens, L.hy = sy / (float)lens, L.hz = sz / (float)lens;
        } else {
            // An .obj lamp: no known lens. Its head is its highest quarter's
            // furthest reach along the facing (the arm over the road).
            float reach = 0.0f;
            for (int k = 0; k < inst.vertexCount; ++k) {
                const roadfurn::Vertex& v = furniture.tris[(size_t)(inst.firstVertex + k)];
                if (v.y - inst.y < 0.75f * maxH) continue;
                reach = std::max(reach, (v.x - inst.x) * inst.fx + (v.z - inst.z) * inst.fz);
            }
            L.hx = inst.x + inst.fx * reach * 0.85f;
            L.hz = inst.z + inst.fz * reach * 0.85f;
            L.hy = inst.y + 0.92f * maxH;
        }
        out.push_back(L);
    }
    return out;
}

// --- pools ---------------------------------------------------------------------

namespace {

struct PoolBuilder {
    const roadgen::HeightFn& top;
    float cx, cz, r;
    std::vector<roadgen::Vertex>& out;

    // The surface's highest point within 0.6: on open asphalt that
    // is the surface itself; beside a kerb it lifts the vertex onto the kerb
    // top, so the triangle that spans the step floats over the road edge
    // rather than diving under the pavement - hidden light is a dark band
    // along every kerb, a little float is invisible.
    float h(float x, float z) const {
        float y = top(x, z);
        const float rr = std::min(2.0f * r / (float)(1 << kPoolDepth), 0.6f);
        y = std::max({y, top(x + rr, z), top(x - rr, z), top(x, z + rr), top(x, z - rr)});
        return y + kPoolLift;
    }
    roadgen::Vertex vert(float x, float y, float z) const {
        roadgen::Vertex v;
        v.x = x, v.y = y, v.z = z;
        v.u = (x - cx) / (2.0f * r) + 0.5f;
        v.v = (z - cz) / (2.0f * r) + 0.5f;
        return v;
    }
    void quad(float x0, float z0, float x1, float z1, int depth) {
        // Wholly outside the pool's lit disc: the corona texture is all but
        // black past kPoolCrop of its radius.
        const float qx = std::clamp(cx, x0, x1), qz = std::clamp(cz, z0, z1);
        const float lit = r * kPoolCrop;
        if ((qx - cx) * (qx - cx) + (qz - cz) * (qz - cz) >= lit * lit) return;
        float ya = h(x0, z0), yb = h(x1, z0), yc = h(x1, z1), yd = h(x0, z1);
        // The two triangles (a b c, a c d) against the surface at a 5 x 5
        // lattice. The budget is ASYMMETRIC: a pool may float a little over
        // the street (kPoolFloat - the light lands a hair off in screen
        // space, nobody sees it) but may never dip by more than kPoolSink,
        // which keeps it over the drawn surface (hidden light is a hole in
        // the pool). That is what keeps a lamp on a gently rolling street at
        // one or a few quads.
        float sink = 0.0f;
        bool floats = false;
        for (int j = 0; j <= 4; ++j)
            for (int i = 0; i <= 4; ++i) {
                const float s = (float)i / 4.0f, t = (float)j / 4.0f;
                const float y = s >= t ? ya + s * (yb - ya) + t * (yc - yb)
                                       : ya + t * (yd - ya) + s * (yc - yd);
                const float want = h(x0 + s * (x1 - x0), z0 + t * (z1 - z0));
                sink = std::max(sink, want - y);
                floats = floats || y - want > kPoolFloat;
            }
        if (depth < kPoolDepth && (sink > kPoolSink || floats)) {
            const float mx = 0.5f * (x0 + x1), mz = 0.5f * (z0 + z1);
            quad(x0, z0, mx, mz, depth + 1);
            quad(mx, z0, x1, mz, depth + 1);
            quad(x0, mz, mx, z1, depth + 1);
            quad(mx, mz, x1, z1, depth + 1);
            return;
        }
        // At the depth cap a quad that still dips (a kerb or a pavement edge
        // runs through it) is RAISED by its worst dip: it floats over the low
        // side of the step instead of hiding under the high one. Its edges
        // then step against a neighbour's - a crack of light nobody sees,
        // where the alternative was a dark band along every kerb.
        if (sink > kPoolSink) {
            const float up = sink - kPoolSink;
            ya += up, yb += up, yc += up, yd += up;
        }
        const roadgen::Vertex a = vert(x0, ya, z0), b = vert(x1, yb, z0);
        const roadgen::Vertex c = vert(x1, yc, z1), d = vert(x0, yd, z1);
        out.insert(out.end(), {a, b, c, a, c, d});
    }
};

}  // namespace

Pools bakePools(std::vector<Lamp>& lamps, const roadgen::HeightFn& top) {
    Pools res;
    // Per lamp: the surface under the head, its slope, the radius, the mesh.
    std::vector<std::vector<roadgen::Vertex>> meshes(lamps.size());
    for (size_t i = 0; i < lamps.size(); ++i) {
        Lamp& L = lamps[i];
        L.gx = L.hx, L.gz = L.hz;
        L.gy = top(L.gx, L.gz);
        L.radius = std::clamp((L.hy - L.gy) * kPoolRadiusPerHeight, kPoolMinRadius, kPoolMaxRadius);
        // Least-squares slope over a 5 x 5 lattice within 0.6 of the radius:
        // the wet streak lies on this plane, the console asks no height query.
        float sxy = 0, sxx = 0, szy = 0, szz = 0;
        for (int j = -2; j <= 2; ++j)
            for (int k = -2; k <= 2; ++k) {
                const float dx = 0.3f * L.radius * (float)k, dz = 0.3f * L.radius * (float)j;
                const float y = top(L.gx + dx, L.gz + dz) - L.gy;
                sxy += dx * y, sxx += dx * dx, szy += dz * y, szz += dz * dz;
            }
        L.sx = sxx > 0.0f ? sxy / sxx : 0.0f;
        L.sz = szz > 0.0f ? szy / szz : 0.0f;
        PoolBuilder b{top, L.gx, L.gz, L.radius, meshes[i]};
        b.quad(L.gx - L.radius, L.gz - L.radius, L.gx + L.radius, L.gz + L.radius, 0);
    }
    // Chunks: whole pools per kPoolCell cell (the cull and streaming
    // granularity), within the furniture's vertex budget.
    res.first.assign(lamps.size(), -1);
    res.count.assign(lamps.size(), 0);
    std::map<std::pair<int, int>, std::vector<size_t>> cells;
    for (size_t i = 0; i < lamps.size(); ++i)
        cells[{(int)std::floor(lamps[i].gx / kPoolCell),
               (int)std::floor(lamps[i].gz / kPoolCell)}]
            .push_back(i);
    for (const auto& [cell, members] : cells) {
        (void)cell;
        int chunk = 0;
        for (size_t i : members) {
            const int n = (int)meshes[i].size();
            if (n == 0) continue;
            if (chunk > 0 && chunk + n > roadfurn::kChunkBudget) {
                res.chunkSizes.push_back(chunk);
                chunk = 0;
            }
            res.first[i] = (int)res.tris.size();
            res.count[i] = n;
            res.tris.insert(res.tris.end(), meshes[i].begin(), meshes[i].end());
            chunk += n;
            ++res.lamps;
        }
        if (chunk > 0) res.chunkSizes.push_back(chunk);
    }
    return res;
}

uint32_t packUv(float u, float v) {
    const auto q = [](float t) {
        return (uint32_t)std::clamp((int)std::lround(t * 4095.0f), 0, 4095);
    };
    return q(u) << 12 | q(v);
}

void unpackUv(uint32_t w, float* u, float* v) {
    *u = (float)((w >> 12) & 4095u) * (1.0f / 4095.0f);
    *v = (float)(w & 4095u) * (1.0f / 4095.0f);
}

std::string lampTableSource(const std::vector<std::pair<int, Lamp>>& lamps) {
    std::ostringstream out;
    out << "// Lit street lamps (docs/weather.md): scene, head xyz, the surface under it\n"
           "// (xyz), that surface's slope (dy/dx, dy/dz) and the pool radius - what the\n"
           "// coronas and the wet streaks are built from every frame.\n"
        << "constexpr int ROAD_LAMP_COUNT = " << lamps.size() << ";\n"
        << "constexpr float ROAD_LAMP_DRAW_DISTANCE = " << lit(kPoolDrawDistance) << ";\n";
    if (lamps.empty()) {
        out << "constexpr float ROAD_LAMPS[10] = {};\n";
        return out.str();
    }
    out << "constexpr float ROAD_LAMPS[" << lamps.size() * 10 << "] = {\n";
    for (const auto& [scene, L] : lamps)
        out << "    " << scene << ".0F, " << lit(L.hx) << ", " << lit(L.hy) << ", " << lit(L.hz)
            << ", " << lit(L.gx) << ", " << lit(L.gy) << ", " << lit(L.gz) << ", " << lit(L.sx, 5)
            << ", " << lit(L.sz, 5) << ", " << lit(L.radius, 5) << ",\n";
    out << "};\n";
    return out.str();
}

std::string emptyLampTableSource() { return lampTableSource({}); }

// --- the generated runtime -----------------------------------------------------

namespace {

// Class members (after `std::vector<ProcChunk> procChunks;`).
const char* kMembers = R"RLMEM(  // --- street lamps and weather (docs/weather.md) ---
  // The wet roads' tint: every plain asphalt chunk (owner -3, one grey
  // colour) points its colour bag here, so wetness is ONE colour per frame,
  // never a per-vertex rewrite. 128 grey = dry = exactly the old picture.
  Tyra::Color roadWetTint_ = Tyra::Color(128.0F, 128.0F, 128.0F, 128.0F);
  // The lamp pools' colour: lamp colour x night level x the grade's
  // compensation. The pool chunks (ProcChunk::lampLight) point here.
  Tyra::Color roadLampPoolColor_ = Tyra::Color(0.0F, 0.0F, 0.0F, 128.0F);
  // Puddles (docs/weather.md "Puddles"): every puddle chunk (owner -6,
  // ProcChunk::puddle) points its colour bag here - dark water plus the sky,
  // alpha the wetness. Alpha 0 = dry: renderRoadChunks skips them.
  Tyra::Color roadPuddleColor_ = Tyra::Color(0.0F, 0.0F, 0.0F, 0.0F);
  float roadLampLevel_ = 0.0F;
  float roadLampComp_[3] = {1.0F, 1.0F, 1.0F};
  int roadLampScene_ = -1, roadLampFirst_ = 0, roadLampEnd_ = 0;
  std::unique_ptr<Tyra::StaPipInfoBag> roadLampInfoBag_;
  // Coronas + wet streaks: one additive bag rebuilt per frame.
  BagArray<Tyra::Vec4> roadLampSprVerts_, roadLampSprSts_;
  BagArray<Tyra::Color> roadLampSprCols_;
  std::unique_ptr<Tyra::StaPipInfoBag> roadLampSprInfo_;
  std::unique_ptr<Tyra::StaPipColorBag> roadLampSprColorBag_;
  std::unique_ptr<Tyra::StaPipTextureBag> roadLampSprTexBag_;
  std::unique_ptr<Tyra::StaPipBag> roadLampSprBag_;
  // Rain: drops wrapped round the camera, drawn by the particles' VU1
  // billboard program (one bag, the rain emitter's streak shape).
  BagArray<Tyra::Vec4> rainPos_, rainParams_;
  BagArray<Tyra::Color> rainCols_;
  std::unique_ptr<Tyra::StaPipInfoBag> rainInfo_;
  std::unique_ptr<Tyra::StaPipColorBag> rainColorBag_;
  std::unique_ptr<Tyra::StaPipTextureBag> rainTexBag_;
  std::unique_ptr<Tyra::StaPipBillboardBag> rainBillboard_;
  std::unique_ptr<Tyra::StaPipBag> rainBag_;
  unsigned int rainRng_ = 0x9E3779B9u;
  void updateWeather();
  void updateRain(float dt);
  void renderRoadLamps();
  void renderRain();
)RLMEM";

// The functions (before renderRoadChunks). No C block comments: this text
// also lands inside the collision part's commented-out copy of the roads.
const char* kImpl = R"RLIMPL(// ---------------------------------------------------------------------------
// Street lamps and weather (docs/weather.md). The lamps' pools are host-baked
// furniture rows (ProcChunk::lampLight); the coronas and the wet streaks are
// rebuilt here from ROAD_LAMPS for the lamps near the camera; the rain is a
// billboard bag wrapped round the camera. All of it is gated on two numbers
// updateWeather computes once a frame: the lamps' level and the wetness.
// ---------------------------------------------------------------------------
constexpr int kRoadLampCoronas = 64;      // per frame, nearest-first is not needed: few are near
constexpr int kRoadLampStreaks = 32;
constexpr float kRoadLampCoronaFar = 160.0F;
constexpr float kRoadLampCoronaFade = 110.0F;
constexpr float kRoadLampStreakFar = 55.0F;
constexpr int kRoadCarStreaks = 0;        // car-light streaks a frame (0: not vehicles + weather)
constexpr int kRainDrops = 240;
constexpr float kRainBox = 11.0F;         // half width of the box round the camera
constexpr float kRainBelow = 4.0F, kRainHeight = 14.0F;

void TerrainGame::updateWeather() {
  const float dt = g_gameplayPaused ? 0.0F : g_frameDt;
  weather::g_state.tick(dt);
  if (roadLampScene_ != currentScene) {
    // This scene's run of ROAD_LAMPS (the table is in scene order).
    roadLampScene_ = currentScene;
    roadLampFirst_ = roadLampEnd_ = 0;
    for (int i = 0; i < ROAD_LAMP_COUNT; ++i) {
      const int s = (int)ROAD_LAMPS[(size_t)i * 10];
      if (s == currentScene) {
        if (roadLampEnd_ == roadLampFirst_) roadLampFirst_ = i;
        roadLampEnd_ = i + 1;
      }
    }
  }
  // The lamps' level: the scene's mode, the live sun, or the baked hour.
  float lamp = 0.0F;
  const int sc = currentScene >= 0 && currentScene < SCENE_COUNT ? currentScene : 0;
  const int mode = weather::SCENE_LAMP_MODES[sc];
  if (mode == 1)
    lamp = 1.0F;
  else if (mode == 0)
    lamp = daynight::active(sc) ? weather::weatherLampLevel(daynight::g_sun[1])
                                : weather::SCENE_LAMP_STATIC[sc];
  roadLampLevel_ = lamp;
  // Emissive light must not be darkened by the night's full-screen grade a
  // second time: the sky bodies' compensation (daynight::g_comp) applies.
  for (int a = 0; a < 3; ++a)
    roadLampComp_[a] = daynight::gradeOn(sc) ? daynight::g_comp[a] : 1.0F;
  auto c255 = [](float v) { return v > 255.0F ? 255.0F : v; };
  roadLampPoolColor_ = Tyra::Color(c255(112.0F * lamp * roadLampComp_[0]),
                                   c255(84.0F * lamp * roadLampComp_[1]),
                                   c255(48.0F * lamp * roadLampComp_[2]), 128.0F);
  // Wet asphalt: darker and a little cooler. 128 = dry = the texture as is.
  const float w = weather::g_state.wet;
  roadWetTint_ = Tyra::Color(128.0F - w * 58.0F, 128.0F - w * 55.0F, 128.0F - w * 46.0F, 128.0F);
  // Puddles: dark water mirroring the sky, filling as the road soaks. The sky
  // colour is the one the frame clears with, which the night grade's
  // compensation has already brightened - take that back out, the puddle is
  // graded like the road it lies on.
  {
    float pc[4];
    weather::weatherPuddleColor(w, scriptCtx.skyColor.r / roadLampComp_[0],
                                scriptCtx.skyColor.g / roadLampComp_[1],
                                scriptCtx.skyColor.b / roadLampComp_[2], roadLampLevel_, pc);
    roadPuddleColor_ = Tyra::Color(pc[0], pc[1], pc[2], pc[3]);
  }
  updateRain(dt);
}

void TerrainGame::updateRain(float dt) {
  const float rain = weather::g_state.rain;
  if (rain < 0.01F && rainPos_.empty()) return;
  if (!rainBag_) {
    rainPos_.assign(kRainDrops, Tyra::Vec4(0.0F, 0.0F, 0.0F, 1.0F));
    rainParams_.assign(kRainDrops, Tyra::Vec4(0.0F, 0.0F, 0.0F, 0.0F));
    rainCols_.assign(kRainDrops, Tyra::Color(170.0F, 178.0F, 196.0F, 84.0F));
    for (int i = 0; i < kRainDrops; ++i) {
      rainPos_[(size_t)i] = Tyra::Vec4(
          cameraPosition.x + (prand(rainRng_) - 0.5F) * 2.0F * kRainBox,
          cameraPosition.y - kRainBelow + prand(rainRng_) * kRainHeight,
          cameraPosition.z + (prand(rainRng_) - 0.5F) * 2.0F * kRainBox, 1.0F);
      // A streak: 3.2 cm wide, 0.75-1.2 long, hanging from world-up (the rain
      // emitter's basis - its weights are not negated, it has no up side).
      const float len = 0.38F + 0.22F * prand(rainRng_);
      rainParams_[(size_t)i] = Tyra::Vec4(0.016F, 0.0F, 0.0F, len);
    }
    rainInfo_ = std::make_unique<Tyra::StaPipInfoBag>();
    rainInfo_->model = &model;
    rainInfo_->shadingType = Tyra::TyraShadingGouraud;
    rainInfo_->frustumCulling = Tyra::PipelineInfoBagFrustumCulling_None;  // billboard: VU1 culls
    rainInfo_->fullClipChecks = false;
    rainInfo_->zTestType = Tyra::PipelineZTest_TestOnly;
    rainColorBag_ = std::make_unique<Tyra::StaPipColorBag>();
    rainTexBag_ = std::make_unique<Tyra::StaPipTextureBag>();
    rainTexBag_->texture = nullptr;
    rainBillboard_ = std::make_unique<Tyra::StaPipBillboardBag>();
    rainBag_ = std::make_unique<Tyra::StaPipBag>();
    rainBag_->info = rainInfo_.get();
    rainBag_->color = rainColorBag_.get();
    rainBag_->texture = rainTexBag_.get();
    rainBag_->lighting = nullptr;
    rainBag_->billboard = rainBillboard_.get();
    rainCols_.bind(rainColorBag_);
    rainParams_.bind(rainTexBag_);
  }
  // Fall, drift, and wrap round the camera: the box moves with the view, so a
  // car at 26 units/s drives through rain rather than out of it.
  const float fall = 17.0F * dt, drift = 1.2F * dt;
  const float cx = cameraPosition.x, cy = cameraPosition.y, cz = cameraPosition.z;
  for (int i = 0; i < kRainDrops; ++i) {
    Tyra::Vec4& p = rainPos_[(size_t)i];
    p.y -= fall;
    p.x += drift;
    if (p.x - cx > kRainBox) p.x -= 2.0F * kRainBox;
    else if (p.x - cx < -kRainBox) p.x += 2.0F * kRainBox;
    if (p.z - cz > kRainBox) p.z -= 2.0F * kRainBox;
    else if (p.z - cz < -kRainBox) p.z += 2.0F * kRainBox;
    if (p.y - cy < -kRainBelow) p.y += kRainHeight;
    else if (p.y - cy > kRainHeight - kRainBelow) p.y -= kRainHeight;
  }
  rainPos_.bind(rainBag_);
  rainBag_->count = (u32)((float)kRainDrops * (rain > 1.0F ? 1.0F : rain));
}

void TerrainGame::renderRain() {
  if (!rainBag_ || rainBag_->count == 0) return;
  Vec4 fwd = cameraLookAt - cameraPosition;
  float rx = fwd.z, rz = -fwd.x;
  const float rl = sqrtf(rx * rx + rz * rz);
  if (rl > 0.0001F) rx /= rl, rz /= rl;
  else rx = 1.0F, rz = 0.0F;
  rainBillboard_->right = Vec4(rx, 0.0F, rz, 0.0F);
  rainBillboard_->up = Vec4(0.0F, 1.0F, 0.0F, 0.0F);
  stapip.core.render(rainBag_.get());
}

void TerrainGame::renderRoadLamps() {
  const float lv = roadLampLevel_;
  if (!beamCoronaTex) return;
  const float wet = weather::g_state.wet;
  const bool lit = lv >= 0.004F;
  // Car lights on a wet road (docs/weather.md): any car whose lamps are on,
  // night or day - so the lamps' level does not gate this half.
  const bool carLights = kRoadCarStreaks > 0 && wet > 0.02F;
  if (!lit && !carLights) return;
  // 1. The pools: host-baked additive decals, coloured by roadLampPoolColor_.
  //    Drawn here, after the whole scene, so the asphalt under them is
  //    already in the frame whatever order the interleaved passes chose.
  for (ProcChunk& c : procChunks) {
    if (!lit) break;
    if (!c.lampLight || !c.bag || c.bag->count == 0) continue;
    const Tyra::Vec4 mn(c.aabbMin[0], c.aabbMin[1], c.aabbMin[2], 1.0F);
    const Tyra::Vec4 mx(c.aabbMax[0], c.aabbMax[1], c.aabbMax[2], 1.0F);
    if (Tyra::CoreBBox::frustumCheckAABB(
            engine->renderer.core.renderer3D.frustumPlanes.getAll(), mn, mx) ==
        Tyra::CoreBBoxFrustum::OUTSIDE_FRUSTUM)
      continue;
    if (c.drawDist > 0.0F) {
      const float dx = c.centre[0] - cameraPosition.x;
      const float dy = c.centre[1] - cameraPosition.y;
      const float dz = c.centre[2] - cameraPosition.z;
      if (dx * dx + dy * dy + dz * dz > c.drawDist * c.drawDist) continue;
    }
    if (splitBandActive && outsideSplitBand(c.aabbMin, c.aabbMax)) continue;
    stapip.core.render(c.bag.get());
  }
  // 2. Coronas round the lamp heads and, on a wet road, each lamp's
  //    reflection: a streak lying on the road from under the lamp toward the
  //    viewer, centred where a mirror would show it. One additive bag.
  //    And 3. below, every car's lamps mirrored in the wet road, in the same
  //    bag.
  const bool lampSprites = lit && roadLampEnd_ > roadLampFirst_;
  if (!lampSprites && !carLights) return;
  if (!roadLampSprBag_) {
    roadLampSprVerts_.reserve((kRoadLampCoronas + kRoadLampStreaks + kRoadCarStreaks) * 6);
    roadLampSprSts_.reserve((kRoadLampCoronas + kRoadLampStreaks + kRoadCarStreaks) * 6);
    roadLampSprCols_.reserve((kRoadLampCoronas + kRoadLampStreaks + kRoadCarStreaks) * 6);
    roadLampSprInfo_ = std::make_unique<Tyra::StaPipInfoBag>();
    roadLampSprInfo_->model = &model;
    roadLampSprInfo_->shadingType = Tyra::TyraShadingGouraud;
    roadLampSprInfo_->frustumCulling = Tyra::PipelineInfoBagFrustumCulling_Precise;
    roadLampSprInfo_->zTestType = Tyra::PipelineZTest_TestOnly;  // occluded, never writes z
    roadLampSprInfo_->fullClipChecks = true;
    roadLampSprInfo_->additiveBlendFix = 128;
    roadLampSprInfo_->fogDisabled = true;  // additive: fog would ADD its colour
    roadLampSprInfo_->dynLightPick = false;
    roadLampSprInfo_->spotLit = false;
    roadLampSprColorBag_ = std::make_unique<Tyra::StaPipColorBag>();
    roadLampSprTexBag_ = std::make_unique<Tyra::StaPipTextureBag>();
    roadLampSprTexBag_->texture = beamCoronaTex;
    roadLampSprBag_ = std::make_unique<Tyra::StaPipBag>();
    roadLampSprBag_->info = roadLampSprInfo_.get();
    roadLampSprBag_->color = roadLampSprColorBag_.get();
    roadLampSprBag_->texture = roadLampSprTexBag_.get();
    roadLampSprBag_->lighting = nullptr;
  }
  roadLampSprVerts_.clear();
  roadLampSprSts_.clear();
  roadLampSprCols_.clear();
  const float ex = cameraPosition.x, ey = cameraPosition.y, ez = cameraPosition.z;
  float fx = cameraLookAt.x - ex, fy = cameraLookAt.y - ey, fz = cameraLookAt.z - ez;
  const float fl = sqrtf(fx * fx + fy * fy + fz * fz);
  if (fl < 1e-4F) return;
  fx /= fl, fy /= fl, fz /= fl;
  float rx = fz, rz = -fx;
  const float rl = sqrtf(rx * rx + rz * rz);
  if (rl > 1e-4F) rx /= rl, rz /= rl;
  else rx = 1.0F, rz = 0.0F;
  const float ux = -rz * fy, uy = rz * fx - rx * fz, uz = rx * fy;
  int coronas = 0, streaks = 0;
  auto put = [&](float x, float y, float z, float u, float v, const Tyra::Color& col) {
    roadLampSprVerts_.push_back(Tyra::Vec4(x, y, z, 1.0F));
    roadLampSprSts_.push_back(Tyra::Vec4(u, v, 1.0F, 0.0F));
    roadLampSprCols_.push_back(col);
  };
  for (int li = roadLampFirst_; lampSprites && li < roadLampEnd_; ++li) {
    const float* L = &ROAD_LAMPS[(size_t)li * 10];
    const float dx = L[1] - ex, dy = L[2] - ey, dz = L[3] - ez;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > kRoadLampCoronaFar * kRoadLampCoronaFar) continue;
    if (dx * fx + dy * fy + dz * fz < -2.0F) continue;  // behind the camera
    const float d = sqrtf(d2);
    if (coronas < kRoadLampCoronas) {
      const float fade = d > kRoadLampCoronaFade
                             ? (kRoadLampCoronaFar - d) / (kRoadLampCoronaFar - kRoadLampCoronaFade)
                             : 1.0F;
      // Half size grows a little with distance, so a far lamp is still a
      // point of light rather than a sub-pixel speck; pulled toward the
      // camera so it is not cut by its own lamp head.
      const float s = 0.55F + 0.011F * d;
      const float pull = d > 1.0F ? (d * 0.5F < 0.7F ? d * 0.5F : 0.7F) / d : 0.0F;
      const float cx = L[1] - dx * pull, cy = L[2] - dy * pull, cz = L[3] - dz * pull;
      const float k = lv * fade;
      const Tyra::Color col(118.0F * k * roadLampComp_[0], 100.0F * k * roadLampComp_[1],
                            74.0F * k * roadLampComp_[2], 128.0F);
      // Corners c -/+ right*s -/+ up*s.
      const float px = rx * s, pz = rz * s;
      const float qx = ux * s, qy = uy * s, qz = uz * s;
      put(cx - px - qx, cy - qy, cz - pz - qz, 0.0F, 0.0F, col);
      put(cx + px - qx, cy - qy, cz + pz - qz, 1.0F, 0.0F, col);
      put(cx + px + qx, cy + qy, cz + pz + qz, 1.0F, 1.0F, col);
      put(cx - px - qx, cy - qy, cz - pz - qz, 0.0F, 0.0F, col);
      put(cx + px + qx, cy + qy, cz + pz + qz, 1.0F, 1.0F, col);
      put(cx - px + qx, cy + qy, cz - pz + qz, 0.0F, 1.0F, col);
      ++coronas;
    }
    if (wet > 0.02F && d < kRoadLampStreakFar && streaks < kRoadLampStreaks) {
      float tx = ex - L[4], tz = ez - L[6];
      const float hd = sqrtf(tx * tx + tz * tz);
      if (hd < 0.5F) continue;
      tx /= hd, tz /= hd;
      const float hgt = L[2] - L[5] > 0.5F ? L[2] - L[5] : 0.5F;
      const float eyeH = ey - L[5] > 0.3F ? ey - L[5] : 0.3F;
      const float m = hd * hgt / (hgt + eyeH);  // the mirror point, from the foot
      float half = 0.5F * m + 1.2F;
      if (half > L[9] * 1.6F) half = L[9] * 1.6F;
      const float mx = L[4] + tx * m, mz = L[6] + tz * m;
      const float wd = 0.30F + 0.006F * d;
      const float nx = -tz * wd, nz = tx * wd;
      const float fade = d > kRoadLampStreakFar * 0.7F
                             ? (kRoadLampStreakFar - d) / (kRoadLampStreakFar * 0.3F)
                             : 1.0F;
      const float k = lv * wet * fade;
      const Tyra::Color col(96.0F * k * roadLampComp_[0], 80.0F * k * roadLampComp_[1],
                            58.0F * k * roadLampComp_[2], 128.0F);
      auto yAt = [&](float x, float z) {
        return L[5] + L[7] * (x - L[4]) + L[8] * (z - L[6]) + 0.07F;
      };
      const float x0 = mx - tx * half, z0 = mz - tz * half;  // the lamp's end
      const float x1 = mx + tx * half, z1 = mz + tz * half;  // the viewer's end
      put(x0 - nx, yAt(x0 - nx, z0 - nz), z0 - nz, 0.0F, 0.0F, col);
      put(x0 + nx, yAt(x0 + nx, z0 + nz), z0 + nz, 1.0F, 0.0F, col);
      put(x1 + nx, yAt(x1 + nx, z1 + nz), z1 + nz, 1.0F, 1.0F, col);
      put(x0 - nx, yAt(x0 - nx, z0 - nz), z0 - nz, 0.0F, 0.0F, col);
      put(x1 + nx, yAt(x1 + nx, z1 + nz), z1 + nz, 1.0F, 1.0F, col);
      put(x1 - nx, yAt(x1 - nx, z1 - nz), z1 - nz, 0.0F, 1.0F, col);
      ++streaks;
    }
  }
  (void)streaks;
  // {{ROAD_CAR_STREAKS}}
  if (roadLampSprVerts_.empty()) return;
  roadLampSprVerts_.bind(roadLampSprBag_);
  roadLampSprSts_.bind(roadLampSprTexBag_);
  roadLampSprCols_.bind(roadLampSprColorBag_);
  roadLampSprColorBag_->single = nullptr;
  roadLampSprBag_->bboxVersion = ++g_bboxStamp;
  stapip.core.render(roadLampSprBag_.get());
}



)RLIMPL";

// The car-light streaks (docs/weather.md "Car lights on a wet road"), spliced
// into renderRoadLamps where `// {{ROAD_CAR_STREAKS}}` stands - only in a
// project with vehicles, the one place VehicleRt exists. Generic on purpose:
// EVERY vehicle whose lamps are on (the player's toggle, a definition that
// starts lit, traffic switched on at night) draws, whoever turned them on.
const char* kCarStreaks = R"RLCAR(  // 3. Car lights on a wet road: every car whose lamps are on mirrors them
  //    as streaks toward the viewer - the core's weatherCarStreaks, the same
  //    function --vehicle-check counts. Only the faces turned to the camera
  //    draw: two quads a car, four at most, kRoadCarStreaks a frame, the
  //    driver's car first.
  if (carLights) {
    int carQuads = 0;
    // Dimmer by day: a lamp mirrored in a wet road under daylight is faint.
    const float day = 0.45F + 0.55F * (lv > 1.0F ? 1.0F : lv);
    for (int pass = 0; pass < 2; ++pass)
      for (int vi = 0; vi < vehicleCount_ && carQuads < kRoadCarStreaks; ++vi) {
        if ((pass == 0) != (vi == vehicleDriver_)) continue;
        const VehicleRt& v = vehicles_[vi];
        if (!v.active || v.def < 0 || v.object < 0 || v.object >= (int)objectGeometry.size())
          continue;
        if (!runtimeObjects[v.object].visible) continue;
        const float cdx = v.pos[0] - ex, cdz = v.pos[2] - ez;
        const float far = weather::kWeatherCarStreakFar + 6.0F;
        if (cdx * cdx + cdz * cdz > far * far) continue;
        const VehicleDefData& s = VEHICLE_DEFS[v.def];
        // The road under each axle: the wheels' own contact heights (a car
        // that never ran its physics yet stands on its origin).
        float gf = 0.5F * (v.wheelY[0] + v.wheelY[1]), gr = 0.5F * (v.wheelY[2] + v.wheelY[3]);
        if (!(fabsf(gf - v.pos[1]) < 1.5F)) gf = v.pos[1];
        if (!(fabsf(gr - v.pos[1]) < 1.5F)) gr = v.pos[1];
        float q[48], k[4];
        int rearOf[4];
        const int n = weather::weatherCarStreaks(v.pos, v.yaw, v.scale, s.lampFront, s.lampRear,
                                                 s.track, s.wheelBase, s.bodyOverhang, gf, gr,
                                                 v.lightsOn, v.brakeOn, v.lampBroken, ex, ey, ez,
                                                 wet, q, k, rearOf);
        for (int j = 0; j < n && carQuads < kRoadCarStreaks; ++j) {
          const float b = k[j] * day;
          const float br = rearOf[j] ? (v.brakeOn ? 1.0F : 0.55F) : 1.0F;
          const Tyra::Color col =
              rearOf[j] ? Tyra::Color(150.0F * b * br * roadLampComp_[0], 22.0F * b * br * roadLampComp_[1],
                                      16.0F * b * br * roadLampComp_[2], 128.0F)
                        : Tyra::Color(120.0F * b * roadLampComp_[0], 112.0F * b * roadLampComp_[1],
                                      92.0F * b * roadLampComp_[2], 128.0F);
          const float* c = &q[j * 12];
          put(c[0], c[1], c[2], 0.0F, 0.0F, col);
          put(c[3], c[4], c[5], 1.0F, 0.0F, col);
          put(c[6], c[7], c[8], 1.0F, 1.0F, col);
          put(c[0], c[1], c[2], 0.0F, 0.0F, col);
          put(c[6], c[7], c[8], 1.0F, 1.0F, col);
          put(c[9], c[10], c[11], 0.0F, 1.0F, col);
          ++carQuads;
        }
      }
  }
)RLCAR";

}  // namespace

std::string patchTemplate(std::string s, const Gates& g) {
    if (!g.lamps && !g.weather) return s;
    // 1. ProcChunk gains its lamp flag; the class gains the members.
    s = replaceAll(s,
                   "    bool smooth = false;\n  };\n  std::vector<ProcChunk> procChunks;\n",
                   "    bool smooth = false;\n"
                   "    // A street lamp's POOL of light (docs/weather.md): an additive\n"
                   "    // furniture chunk drawn by renderRoadLamps at night, never by\n"
                   "    // renderProcChunks.\n"
                   "    int lampLight = 0;\n" +
                       std::string(g.weather
                                       ? "    // A road-detail PUDDLE (docs/weather.md \"Puddles\"): its colour\n"
                                         "    // bag is roadPuddleColor_, and it is not drawn while dry.\n"
                                         "    int puddle = 0;\n"
                                       : "") +
                       "  };\n  std::vector<ProcChunk> procChunks;\n" + std::string(kMembers));
    // 2. procFinishChunks (and the streaming copy cut from it): the pool
    //    chunks' additive bag and night colour, the asphalt's wet tint.
    std::string finish =
        "      c.bag->info = roadBlendInfoBag.get();\n"
        "    }\n"
        "    // Street lamps and weather (docs/weather.md): a lamp pool is additive,\n"
        "    // z-tested, unfogged and coloured by the night level; a plain asphalt\n"
        "    // chunk (one grey colour) takes the wet tint - ONE colour a frame.\n"
        "    c.colorBag->single = nullptr;\n"
        "    if (c.lampLight) {\n"
        "      if (!roadLampInfoBag_) {\n"
        "        roadLampInfoBag_ = std::make_unique<StaPipInfoBag>();\n"
        "        roadLampInfoBag_->model = &model;\n"
        "        roadLampInfoBag_->shadingType = TyraShadingFlat;\n"
        "        roadLampInfoBag_->frustumCulling = PipelineInfoBagFrustumCulling_Precise;\n"
        "        roadLampInfoBag_->fullClipChecks = true;\n"
        "        roadLampInfoBag_->zTestType = PipelineZTest_TestOnly;\n"
        "        roadLampInfoBag_->additiveBlendFix = 128;\n"
        "        roadLampInfoBag_->fogDisabled = true;\n"
        "        roadLampInfoBag_->dynLightPick = false;\n"
        "        roadLampInfoBag_->spotLit = false;\n"
        "      }\n"
        "      c.bag->info = roadLampInfoBag_.get();\n"
        "      c.colorBag->single = &roadLampPoolColor_;\n"
        "    }\n";
    if (g.weather && g.roads)
        finish +=
            "    if (c.owner == -3 && c.roadTex && !c.roadBlend && !c.roadEdge) {\n"
            "      const BagArray<Tyra::Color>& cc = c.colors;\n"
            "      bool grey = cc.size() > 0;\n"
            "      for (size_t k = 0; k < cc.size() && grey; ++k)\n"
            "        grey = cc[k].r == 128.0F && cc[k].g == 128.0F && cc[k].b == 128.0F &&\n"
            "               cc[k].a == 128.0F;\n"
            "      if (grey) c.colorBag->single = &roadWetTint_;\n"
            "    }\n";
    if (g.puddles)
        finish +=
            "    if (c.puddle) c.colorBag->single = &roadPuddleColor_;  // puddles: the wetness\n";
    s = replaceAll(s, "      c.bag->info = roadBlendInfoBag.get();\n    }\n", finish);
    // 2b. renderRoadChunks: a puddle chunk is not even submitted while dry
    //     (its alpha is 0 and every texel would fail the alpha test anyway).
    if (g.puddles)
        s = replaceAll(s, "      continue;  // -6: road details, blended after the asphalt\n",
                       "      continue;  // -6: road details, blended after the asphalt\n"
                       "    if (c.puddle && roadPuddleColor_.a < 1.0F) continue;  // dry: no puddles\n");
    // 3. renderProcChunks leaves the pools to renderRoadLamps.
    s = replaceAll(s,
                   "  for (ProcChunk& c : procChunks) {\n"
                   "    // Roads have their own phase and profiler row.",
                   "  for (ProcChunk& c : procChunks) {\n"
                   "    if (c.lampLight) continue;  // street lamp pools: renderRoadLamps\n"
                   "    // Roads have their own phase and profiler row.");
    // 4. The pools and coronas draw through the corona sprite.
    if (g.lamps && !(g.weather && g.vehicles))
        s = replaceAll(s, "    if (BEAMS_USED || STAR_COUNT > 0)\n      beamCoronaTex",
                       "    if (BEAMS_USED || STAR_COUNT > 0 || ROAD_LAMP_COUNT > 0)  // + lit street lamps\n"
                       "      beamCoronaTex");
    else if (g.weather && g.vehicles)
        s = replaceAll(s, "    if (BEAMS_USED || STAR_COUNT > 0)\n      beamCoronaTex",
                       "    if (BEAMS_USED || STAR_COUNT > 0 || ROAD_LAMP_COUNT > 0 ||\n"
                       "        VEHICLE_DEF_COUNT > 0)  // + lit street lamps, car lights on a wet road\n"
                       "      beamCoronaTex");
    // 5. Once a frame, in the game loop; the authored weather on a scene load.
    s = replaceAll(s, "  updateParticles();\n",
                   "  updateParticles();\n  updateWeather();  // docs/weather.md\n");
    s = replaceAll(s, "    daynight::reset(sceneIndex);\n",
                   "    daynight::reset(sceneIndex);\n    weather::reset(sceneIndex);  // docs/weather.md\n");
    // 6. The draws: lamps after the light beams (additive, z-tested against the
    //    finished scene), rain after the particles.
    s = replaceAll(s,
                   "  { const u32 ct=costStart(); updateAndRenderLightBeams(); costEnd(\"Light_beams\",-1,ct); }\n",
                   "  { const u32 ct=costStart(); updateAndRenderLightBeams(); costEnd(\"Light_beams\",-1,ct); }\n"
                   "  { const u32 ct=costStart(); renderRoadLamps(); costEnd(\"Road_lamps\",-1,ct); }\n");
    s = replaceAll(s, "  costEnd(\"Particles\",-1,costParticleStart);\n",
                   "  costEnd(\"Particles\",-1,costParticleStart);\n"
                   "  { const u32 ct=costStart(); renderRain(); costEnd(\"Rain\",-1,ct); }\n");
    // 7. The functions - with, in a project with vehicles and weather, the
    //    car-light streaks in renderRoadLamps and their budget in its bag.
    std::string impl = kImpl;
    if (g.weather && g.vehicles) {
        impl = replaceAll(impl, "constexpr int kRoadCarStreaks = 0;        // car-light streaks a frame (0: not vehicles + weather)\n",
                          "constexpr int kRoadCarStreaks = weather::kWeatherCarStreakMax;  // car-light streaks a frame\n");
        impl = replaceAll(impl, "  // {{ROAD_CAR_STREAKS}}\n", kCarStreaks);
    } else {
        impl = replaceAll(impl, "  // {{ROAD_CAR_STREAKS}}\n", "");
    }
    s = replaceAll(s, "void TerrainGame::renderRoadChunks() {", impl + "void TerrainGame::renderRoadChunks() {");
    return s;
}

std::string weatherHeaderSource(const std::vector<SceneWeather>& scenes) {
    std::ostringstream out;
    out << "\n// Weather and street lamps (docs/weather.md): each scene's authored weather\n"
           "// and lamp mode, the core both the editor and this game compile\n"
           "// (src/weather_core.inl), and the one live state Set Weather drives.\n"
           "namespace weather {\n\n";
    auto row = [&](const char* type, const char* name, auto value) {
        out << "constexpr " << type << " " << name << "[SCENE_COUNT] = {";
        for (size_t i = 0; i < scenes.size(); ++i) out << (i ? ", " : "") << value(scenes[i]);
        out << "};\n";
    };
    row("int", "SCENE_WEATHERS", [](const SceneWeather& w) { return std::to_string(w.weather); });
    row("float", "SCENE_WEATHER_INTENSITIES", [](const SceneWeather& w) { return lit(w.intensity, 5); });
    // 0 auto (the sun, or the baked hour below), 1 always on, 2 off.
    row("int", "SCENE_LAMP_MODES", [](const SceneWeather& w) { return std::to_string(w.lamps); });
    row("float", "SCENE_LAMP_STATIC", [](const SceneWeather& w) { return lit(w.staticLevel, 5); });
    std::string core(reinterpret_cast<const char*>(weathersrc::kCore), weathersrc::kCoreSize);
    core.erase(std::remove(core.begin(), core.end(), '\r'), core.end());
    out << "\n" << core
        << "\ninline WeatherState g_state;\n"
           "// A scene load: its authored weather, roads already wet.\n"
           "inline void reset(int scene) {\n"
           "  if (scene < 0 || scene >= SCENE_COUNT) return;\n"
           "  g_state.reset(SCENE_WEATHERS[scene] == kWeatherRain ? SCENE_WEATHER_INTENSITIES[scene]\n"
           "                                                      : 0.0F);\n"
           "}\n"
           "// The Set Weather flow node.\n"
           "inline void request(int kind, float intensity, float seconds) {\n"
           "  g_state.request(kind, intensity, seconds);\n"
           "}\n\n"
           "}  // namespace weather\n";
    return out.str();
}

namespace {
// WeatherSim mirrors the core's fields, so the header need not include it.
weather::WeatherState toCore(const WeatherSim& w) {
    weather::WeatherState s;
    s.rain = w.rain, s.wet = w.wet, s.from = w.from, s.to = w.to, s.time = w.time, s.span = w.span;
    return s;
}
void fromCore(const weather::WeatherState& s, WeatherSim& w) {
    w.rain = s.rain, w.wet = s.wet, w.from = s.from, w.to = s.to, w.time = s.time, w.span = s.span;
}
}  // namespace

void WeatherSim::reset(float intensity) {
    weather::WeatherState s = toCore(*this);
    s.reset(intensity);
    fromCore(s, *this);
}

void WeatherSim::request(int kind, float intensity, float seconds) {
    weather::WeatherState s = toCore(*this);
    s.request(kind, intensity, seconds);
    fromCore(s, *this);
}

void WeatherSim::tick(float dt) {
    weather::WeatherState s = toCore(*this);
    s.tick(dt);
    fromCore(s, *this);
}

float lampLevelFromSun(float sunY) { return weather::weatherLampLevel(sunY); }

float puddleLevel(float wet) { return weather::weatherPuddleLevel(wet); }

void puddleColor(float wet, float skyR, float skyG, float skyB, float lamps, float out[4]) {
    weather::weatherPuddleColor(wet, skyR, skyG, skyB, lamps, out);
}

int carStreaks(const float pos[3], float yawDeg, float scale, const float lampFront[4],
               const float lampRear[4], float track, float wheelBase, float overhang,
               float groundFront, float groundRear, int lightsOn, int brakeOn, int broken,
               float ex, float ey, float ez, float wet, float quads[48], float ks[4],
               int rear[4]) {
    return weather::weatherCarStreaks(pos, yawDeg, scale, lampFront, lampRear, track, wheelBase,
                                      overhang, groundFront, groundRear, lightsOn, brakeOn, broken,
                                      ex, ey, ez, wet, quads, ks, rear);
}

// --- --vehicle-check "wet roads and lamps" ---------------------------------------

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- wet roads and lamps --\n");
    char msg[256];
    // A gentle hill and a kerbed street with a pavement: the surface the
    // pools must lie on.
    const roadgen::HeightFn ground = [](float x, float z) {
        return 0.6f * std::sin(x * 0.05f) + 0.4f * std::cos(z * 0.07f);
    };
    roadgen::CrossingRoad a;
    a.id = "a";
    a.points = {-80, 0, 0, 0, 80, 0};
    a.width = 9.0f;
    a.kerb = true;
    roadgen::CrossingRoad b;
    b.id = "b";
    b.points = {0, -60, 0, 60};
    b.width = 7.0f;
    b.intersection = a.intersection = "res/materials/x.mtl";
    std::vector<roadgen::CrossingRoad> roads = {a, b};
    std::vector<roadfurn::Settings> fs(2);
    fs[0].lamps.spacing = 14.0f;
    fs[0].lamps.side = roadfurn::kAlternate;
    fs[1].lamps.spacing = 18.0f;
    fs[1].lamps.side = roadfurn::kBoth;
    const roadgen::CrossingPlan plan = roadgen::planCrossings(roads, {});
    const roadgen::TerrainGrid grid{};
    roadfurn::SceneInput in = roadfurn::prepare(roads, fs, plan, ground, grid, "");
    const roadfurn::Result furn = roadfurn::build(in);
    // The drawn surface: roads (with their lift), patches, pavements.
    roadgen::Surface surf;
    for (const roadgen::CrossingRoad& r : roads) {
        std::vector<roadgen::Vertex> mesh;
        roadgen::tessellate(r.points, r.width,
                            [&](float x, float z) { return ground(x, z) + roadgen::rankLift(r.rank); },
                            mesh, {}, r.sampleStep);
        surf.add(mesh);
    }
    surf.add(in.patches);
    roadgen::addPavementsToSurface(surf, in.pavements);
    surf.build();
    const roadgen::HeightFn top = [&](float x, float z) {
        const float s = surf.at(x, z), g = ground(x, z);
        return s != roadgen::Surface::kNone && s > g ? s : g;
    };
    std::vector<Lamp> lamps = lampsOf(furn);
    verdict(!lamps.empty() && (int)lamps.size() == furn.perKind[roadfurn::kLamp],
            "every furniture lamp is a light");
    const Pools pools = bakePools(lamps, top);
    {
        // The head is the lens: over the road, 5.2 up, never on the pole.
        bool ok = true;
        for (const Lamp& L : lamps) {
            const float above = L.hy - L.gy;
            ok &= above > 4.5f && above < 5.8f;
        }
        verdict(ok, "each lamp's light is its lens, about 5.2 above the surface under it");
    }
    {
        // Every pool vertex lies kPoolLift over the drawn surface, and the
        // pool's samples follow it: a lattice inside each triangle is never
        // further than the flatness budget off (kerb faces excepted by the
        // depth cap: count those, they must be few).
        float below = 0.0f, above = 0.0f;
        for (const roadgen::Vertex& v : pools.tris) {
            const float d = v.y - (top(v.x, v.z) + kPoolLift);
            below = std::min(below, d), above = std::max(above, d);
        }
        std::snprintf(msg, sizeof(msg),
                      "every pool vertex lies %.2f over the drawn surface, never under it "
                      "(worst under %.5f, over %.3f at a kerb)",
                      (double)kPoolLift, (double)-below, (double)above);
        verdict(below > -1e-4f && above < 0.5f, msg);
        int samples = 0, off = 0;
        for (size_t t = 0; t + 2 < pools.tris.size(); t += 3)
            for (float s = 0.2f; s < 0.7f; s += 0.2f)
                for (float r = 0.2f; r + s < 0.95f; r += 0.2f) {
                    const roadgen::Vertex& p0 = pools.tris[t];
                    const roadgen::Vertex& p1 = pools.tris[t + 1];
                    const roadgen::Vertex& p2 = pools.tris[t + 2];
                    const float x = p0.x + s * (p1.x - p0.x) + r * (p2.x - p0.x);
                    const float z = p0.z + s * (p1.z - p0.z) + r * (p2.z - p0.z);
                    const float y = p0.y + s * (p1.y - p0.y) + r * (p2.y - p0.y);
                    ++samples;
                    // Hidden: under the drawn surface (the light would be lost).
                    if (y < top(x, z) + kPoolLift - kPoolSink - 0.005f) ++off;
                }
        std::snprintf(msg, sizeof(msg),
                      "no pool sinks more than %.2f under its lift, so none hides under the surface (%d of %d samples)",
                      (double)kPoolSink, off, samples);
        verdict(samples > 0 && off * 50 < samples, msg);
    }
    {
        // Each pool is centred under its own lamp: the UV centre (0.5, 0.5)
        // is the point under the head, and every vertex is within the radius
        // box of SOME lamp.
        bool centred = true;
        for (const Lamp& L : lamps)
            centred &= std::fabs(L.gx - L.hx) < 1e-6f && std::fabs(L.gz - L.hz) < 1e-6f &&
                       L.radius >= kPoolMinRadius && L.radius <= kPoolMaxRadius;
        bool inside = true;
        for (const roadgen::Vertex& v : pools.tris) {
            bool any = false;
            for (const Lamp& L : lamps) {
                const float u = (v.x - L.gx) / (2.0f * L.radius) + 0.5f;
                const float w = (v.z - L.gz) / (2.0f * L.radius) + 0.5f;
                any |= std::fabs(u - v.u) < 1e-4f && std::fabs(w - v.v) < 1e-4f && u >= -1e-4f &&
                       u <= 1.0001f && w >= -1e-4f && w <= 1.0001f;
            }
            inside &= any;
        }
        verdict(centred && inside, "every pool is centred under its lamp, its UV spans the pool");
        // A pool on flat ground is one quad.
        std::vector<Lamp> flat(1);
        flat[0].hx = 3.0f, flat[0].hy = 5.3f, flat[0].hz = 4.0f;
        const Pools fp = bakePools(flat, [](float, float) { return 0.0f; });
        verdict(fp.tris.size() == 6 && fp.chunkSizes.size() == 1,
                "a lamp over flat ground costs one quad (6 vertices)");
    }
    {
        // Chunks: whole pools, within budget, one pool cell each - the
        // granularity the road streaming items and the roadfile pages use.
        int sum = 0;
        bool budget = true;
        for (int c : pools.chunkSizes) sum += c, budget &= c > 0 && c <= roadfurn::kChunkBudget && c % 3 == 0;
        bool oneCell = true;
        size_t at = 0;
        for (int c : pools.chunkSizes) {
            float mnx = 1e30f, mxx = -1e30f, mnz = 1e30f, mxz = -1e30f;
            for (int k = 0; k < c; ++k) {
                const roadgen::Vertex& v = pools.tris[at + (size_t)k];
                mnx = std::min(mnx, v.x), mxx = std::max(mxx, v.x);
                mnz = std::min(mnz, v.z), mxz = std::max(mxz, v.z);
            }
            // The cell's own 48 units plus a pool radius either side.
            oneCell &= mxx - mnx <= kPoolCell + 2.0f * kPoolMaxRadius &&
                       mxz - mnz <= kPoolCell + 2.0f * kPoolMaxRadius;
            at += (size_t)c;
        }
        verdict(sum == (int)pools.tris.size() && budget && oneCell,
                "pool chunks hold whole pools, within budget, one 96-unit cell each");
        std::snprintf(msg, sizeof(msg), "%d lamps, %zu pool vertices in %zu chunks (%.1f a lamp)",
                      (int)lamps.size(), pools.tris.size(), pools.chunkSizes.size(),
                      lamps.empty() ? 0.0 : (double)pools.tris.size() / (double)lamps.size());
        verdict(pools.tris.size() < lamps.size() * 200, msg);
    }
    {
        // Determinism: the same roads bake the same pools, bit for bit.
        std::vector<Lamp> again = lampsOf(roadfurn::build(roadfurn::prepare(roads, fs, plan, ground, grid, "")));
        const Pools p2 = bakePools(again, top);
        bool same = p2.tris.size() == pools.tris.size() && p2.chunkSizes == pools.chunkSizes;
        for (size_t i = 0; same && i < p2.tris.size(); ++i)
            same = std::memcmp(&p2.tris[i], &pools.tris[i], sizeof(roadgen::Vertex)) == 0;
        verdict(same, "the same roads bake the same lamps and pools, bit for bit");
    }
    {
        // The packed texture coordinate round-trips to 1/4095.
        float worst = 0.0f;
        for (const roadgen::Vertex& v : pools.tris) {
            float u, w;
            unpackUv(packUv(v.u, v.v), &u, &w);
            worst = std::max({worst, std::fabs(u - std::clamp(v.u, 0.0f, 1.0f)),
                              std::fabs(w - std::clamp(v.v, 0.0f, 1.0f))});
        }
        verdict(worst <= 0.5f / 4095.0f + 1e-6f, "a pool vertex's UV survives the colour word");
    }
    {
        // The weather state machine.
        WeatherSim s;
        s.reset(0.0f);
        s.request(1, 0.8f, 4.0f);
        for (int i = 0; i < 100; ++i) s.tick(0.02f);  // 2 s
        const bool ramp = std::fabs(s.rain - 0.4f) < 1e-3f && s.wet > 0.0f && s.wet <= s.rain;
        for (int i = 0; i < 100; ++i) s.tick(0.02f);  // 4 s
        const bool reached = std::fabs(s.rain - 0.8f) < 1e-4f;
        for (int i = 0; i < 400; ++i) s.tick(0.02f);  // soaked
        const bool soaked = std::fabs(s.wet - 0.8f) < 1e-4f;
        verdict(ramp && reached && soaked,
                "rain ramps linearly over its transition and the roads soak up to it");
        s.request(0, 1.0f, 1.0f);
        for (int i = 0; i < 100; ++i) s.tick(0.02f);  // 2 s
        const bool stopped = s.rain == 0.0f && s.wet > 0.7f;
        for (int i = 0; i < 1000; ++i) s.tick(0.05f);  // 50 s
        verdict(stopped && s.wet == 0.0f,
                "rain stops in its transition, the roads dry slowly after it");
        s.request(1, 1.0f, 0.0f);
        const bool instant = s.rain == 1.0f && s.wet == 1.0f;
        s.tick(0.0f);
        verdict(instant && s.rain == 1.0f, "a 0-second Set Weather switches at once, roads wet too");
        s.request(1, 7.0f, 0.0f);
        verdict(s.rain == 1.0f, "the intensity is clamped to 0..1");
        WeatherSim paused;
        paused.reset(0.0f);
        paused.request(1, 1.0f, 2.0f);
        for (int i = 0; i < 50; ++i) paused.tick(0.0f);
        verdict(paused.rain == 0.0f, "a paused game (dt 0) holds the weather");
    }
    {
        // The lamps' level from the sun: off at noon, on at midnight, smooth
        // through the dusk.
        const float noon = lampLevelFromSun(0.9f), midnight = lampLevelFromSun(-0.7f);
        const float dusk = lampLevelFromSun(std::sin(1.5f * 3.14159265f / 180.0f));
        bool mono = true;
        float prev = 1.0f;
        for (int e = -20; e <= 20; ++e) {
            const float l = lampLevelFromSun(std::sin((float)e * 3.14159265f / 180.0f));
            mono &= l <= prev + 1e-6f;
            prev = l;
        }
        verdict(noon == 0.0f && midnight == 1.0f && dusk > 0.1f && dusk < 0.9f && mono,
                "lamps: off by day, on at night, fading through the dusk");
    }
    // Puddles (docs/weather.md "Puddles"): the road-details fixture - a kerbed
    // T with zebras and a kerbless cross street on rolling ground.
    {
        const roadgen::HeightFn hills = [](float x, float z) {
            return 0.8f * std::sin(x * 0.11f) + 0.5f * std::cos(z * 0.07f) +
                   0.3f * std::fabs(std::sin(x * 0.05f + z * 0.03f));
        };
        auto mk = [](const char* id, std::vector<float> pts, float width, bool kerb) {
            roadgen::CrossingRoad r;
            r.id = id;
            r.points = std::move(pts);
            r.width = width;
            r.intersection = "res/materials/x.mtl";
            r.kerb = kerb;
            r.markings = roadgen::kMarkCrossings;
            r.details = 0.9f;
            return r;
        };
        const std::vector<roadgen::CrossingRoad> pr = {
            mk("main", {-120, 0, 0, 2, 120, 0}, 12, true), mk("stem", {0, 0, 0, 90}, 9, true),
            mk("cross", {60, -80, 62, 80}, 8, false)};
        const roadgen::CrossingPlan pplan = roadgen::planCrossings(pr, {});
        std::vector<roadgen::Vertex> roadTris, patches, paint;
        std::vector<std::vector<roadgen::Vertex>> perRoad(pr.size());
        for (size_t i = 0; i < pr.size(); ++i) {
            roadgen::tessellate(pr[i].points, pr[i].width,
                                [&](float x, float z) { return hills(x, z) + roadgen::rankLift(pr[i].rank); },
                                perRoad[i], {}, pr[i].sampleStep);
            roadTris.insert(roadTris.end(), perRoad[i].begin(), perRoad[i].end());
        }
        for (const roadgen::Crossing& c : pplan.crossings) {
            if (c.kind != roadgen::kCrossPatch || c.patchDuplicate) continue;
            std::vector<roadgen::Vertex> mesh;
            roadgen::tessellateJunctionSurface(c.shape, roadTris, hills, c.lift, mesh);
            patches.insert(patches.end(), mesh.begin(), mesh.end());
        }
        roadgen::Surface drawn, patchOnly, paintOnly;
        drawn.add(roadTris);
        drawn.add(patches);
        drawn.build();
        roadgen::bakeMarkings(pplan, pr, drawn, paint);
        patchOnly.add(patches);
        patchOnly.build();
        paintOnly.add(paint);
        paintOnly.build();
        std::vector<roadgen::Surface> own(pr.size());
        for (size_t i = 0; i < pr.size(); ++i) {
            own[i].add(perRoad[i]);
            own[i].build();
        }
        auto bake = [&](bool puddles) {
            roaddetail::SceneInput di;
            di.roads = &pr;
            di.plan = &pplan;
            di.ground = hills;
            di.patches = patches;
            di.paint = paint;
            di.puddles = puddles;
            return roaddetail::build(di);
        };
        const roaddetail::Result dry = bake(false), wetA = bake(true), wetB = bake(true);
        std::snprintf(msg, sizeof(msg),
                      "%zu puddles (%d tried, %d on a crest), %zu vertices in %zu chunks",
                      wetA.puddles.size(), wetA.puddleCandidates, wetA.puddlesOnCrest,
                      wetA.puddleTris.size(), wetA.puddleChunkSizes.size());
        verdict(wetA.puddles.size() >= 6 && dry.puddles.empty() && dry.puddleTris.empty(), msg);
        // Placed LAST: a project that gains weather keeps every other decal.
        bool same = dry.tris.size() == wetA.tris.size() && dry.chunkSizes == wetA.chunkSizes &&
                    dry.decals.size() == wetA.decals.size();
        for (size_t i = 0; same && i < dry.tris.size(); ++i)
            same = std::memcmp(&dry.tris[i], &wetA.tris[i], sizeof(roadgen::Vertex)) == 0;
        verdict(same, "puddles never move, add or drop a decal of the other kinds");
        bool det = wetA.puddleTris.size() == wetB.puddleTris.size() &&
                   wetA.puddleChunkSizes == wetB.puddleChunkSizes;
        for (size_t i = 0; det && i < wetA.puddleTris.size(); ++i)
            det = std::memcmp(&wetA.puddleTris[i], &wetB.puddleTris[i], sizeof(roadgen::Vertex)) == 0;
        verdict(det, "the same roads bake the same puddles, bit for bit");
        // Every sample of every puddle triangle: on its own road, kDetailLift
        // over the drawn surface, and on no node patch and no paint.
        int samples = 0, offRoad = 0, onNode = 0, onPaint = 0, offLift = 0;
        for (size_t t = 0; t + 2 < wetA.puddleTris.size(); t += 3)
            for (float s = 0.1f; s < 0.85f; s += 0.2f)
                for (float r = 0.1f; r + s < 0.95f; r += 0.2f) {
                    const roadgen::Vertex& p0 = wetA.puddleTris[t];
                    const roadgen::Vertex& p1 = wetA.puddleTris[t + 1];
                    const roadgen::Vertex& p2 = wetA.puddleTris[t + 2];
                    const float x = p0.x + s * (p1.x - p0.x) + r * (p2.x - p0.x);
                    const float z = p0.z + s * (p1.z - p0.z) + r * (p2.z - p0.z);
                    const float y = p0.y + s * (p1.y - p0.y) + r * (p2.y - p0.y);
                    ++samples;
                    bool onAny = false;
                    float top = roadgen::Surface::kNone;
                    for (const roadgen::Surface& o : own) {
                        const float h = o.at(x, z);
                        if (h != roadgen::Surface::kNone) onAny = true, top = std::max(top, h);
                    }
                    if (!onAny) ++offRoad;
                    else if (std::fabs(y - top - roaddetail::kDetailLift) > 0.01f) ++offLift;
                    if (patchOnly.at(x, z) != roadgen::Surface::kNone) ++onNode;
                    if (paintOnly.at(x, z) != roadgen::Surface::kNone) ++onPaint;
                }
        std::snprintf(msg, sizeof(msg),
                      "every puddle sample lies on its road, %.2f over it, off every node patch "
                      "and its paint (%d samples: %d off, %d off the lift, %d on a node, %d on paint)",
                      (double)roaddetail::kDetailLift, samples, offRoad, offLift, onNode, onPaint);
        verdict(samples > 0 && offRoad == 0 && offLift == 0 && onNode == 0 && onPaint == 0, msg);
        // Low spots: no puddle sits on a crest of its road (higher than the
        // road 5 units either way along it), and the kerb puddles hug an edge.
        int crest = 0, atEdge = 0;
        for (const roaddetail::Decal& d : wetA.puddles) {
            const roadgen::CrossingRoad& r = pr[(size_t)d.road];
            const roadgen::Surface& o = own[(size_t)d.road];
            const float y0 = o.at(d.x, d.z);
            const float ya = o.at(d.x - d.ax * 5.0f, d.z - d.az * 5.0f);
            const float yb = o.at(d.x + d.ax * 5.0f, d.z + d.az * 5.0f);
            if (y0 != roadgen::Surface::kNone && ya != roadgen::Surface::kNone &&
                yb != roadgen::Surface::kNone && y0 > 0.5f * (ya + yb) + 0.05f)
                ++crest;
            // Distance from the edge: the farthest the road reaches across.
            const float lx = -d.az, lz = d.ax;
            float edgeDist = 1e9f;
            for (int sd = -1; sd <= 1; sd += 2)
                for (float k = 0.0f; k < r.width; k += 0.1f)
                    if (o.at(d.x + lx * k * (float)sd, d.z + lz * k * (float)sd) ==
                        roadgen::Surface::kNone) {
                        edgeDist = std::min(edgeDist, k);
                        break;
                    }
            atEdge += edgeDist < d.hv + 1.0f ? 1 : 0;
        }
        std::snprintf(msg, sizeof(msg),
                      "puddles collect in low spots: none on a crest, %d of %zu hug the kerb or edge",
                      atEdge, wetA.puddles.size());
        verdict(crest == 0 && atEdge * 2 >= (int)wetA.puddles.size(), msg);
        // Beside the gullies: on the kerbed roads, a share of the puddles
        // stand right next to a gully (the drain they collect at).
        int byGully = 0, kerbed = 0;
        for (const roaddetail::Decal& d : wetA.puddles) {
            if (!pr[(size_t)d.road].kerb) continue;
            ++kerbed;
            bool nearG = false;
            for (const roaddetail::Decal& gd : wetA.decals)
                nearG |= gd.kind == roaddetail::kGully && gd.road == d.road &&
                         std::hypot(gd.x - d.x, gd.z - d.z) < d.hu + gd.hu + 0.6f;
            byGully += nearG ? 1 : 0;
        }
        std::snprintf(msg, sizeof(msg), "%d of %d puddles on kerbed roads lie beside a gully",
                      byGully, kerbed);
        verdict(kerbed > 0 && byGully * 4 >= kerbed, msg);
        int sum = 0;
        bool budget = true;
        for (int c : wetA.puddleChunkSizes)
            sum += c, budget &= c > 0 && c % 3 == 0 && c <= roaddetail::kChunkBudget;
        bool uv = true;
        for (const roadgen::Vertex& v : wetA.puddleTris) uv &= v.u > 0.0f && v.u < 1.0f && v.v > 0.0f && v.v < 1.0f;
        verdict(sum == (int)wetA.puddleTris.size() && budget && uv,
                "puddle chunks hold whole puddles within budget, UVs inside the puddle texture");
        // The texture: 16 RGBA entries, so a 4-bit bake keeps it as drawn.
        const std::vector<unsigned char> px = roaddetail::generatePuddles();
        std::vector<uint32_t> entries;
        int opaque = 0;
        for (size_t i = 0; i + 3 < px.size(); i += 4) {
            const uint32_t e = (uint32_t)px[i] << 24 | (uint32_t)px[i + 1] << 16 |
                               (uint32_t)px[i + 2] << 8 | px[i + 3];
            if (std::find(entries.begin(), entries.end(), e) == entries.end()) entries.push_back(e);
            opaque += px[i + 3] == 255 ? 1 : 0;
        }
        verdict(entries.size() <= 16 && opaque > 400 && px == roaddetail::generatePuddles(),
                "the puddle texture is 16 RGBA entries at most, deterministic");
        // Visible only when wet: the level and the colour's alpha follow the
        // wetness, 0 while the road is merely damp.
        bool mono = true;
        float prev = 0.0f;
        for (int i = 0; i <= 100; ++i) {
            const float l = puddleLevel((float)i / 100.0f);
            mono &= l + 1e-6f >= prev;
            prev = l;
        }
        float c0[4], c1[4], cNight[4];
        puddleColor(0.0f, 120.0f, 140.0f, 170.0f, 0.0f, c0);
        puddleColor(1.0f, 120.0f, 140.0f, 170.0f, 0.0f, c1);
        puddleColor(1.0f, 8.0f, 9.0f, 14.0f, 0.0f, cNight);
        verdict(mono && puddleLevel(0.3f) == 0.0f && puddleLevel(0.9f) == 1.0f && c0[3] == 0.0f &&
                    c1[3] > 90.0f && c1[3] <= 128.0f && c1[2] > cNight[2],
                "puddles only show on a wet road: none below 0.35 wetness, full above 0.85, "
                "lighter under a day sky than at night");
    }
    // Car lights on a wet road (docs/weather.md): the core function the
    // console calls, car by car.
    {
        const float pos[3] = {0.0f, 0.0f, 0.0f};
        const float lf[4] = {0.62f, 0.66f, 2.05f, 0.14f}, lr[4] = {0.64f, 0.78f, -2.1f, 0.12f};
        const float none4[4] = {0, 0, 0, 0};
        float q[48], k[4];
        int rear[4];
        auto count = [&](int lights, int brake, int broken, float ex, float ez, float wet,
                         const float* f = nullptr, const float* r = nullptr) {
            return carStreaks(pos, 0.0f, 1.0f, f ? f : lf, r ? r : lr, 1.6f, 2.7f, 0.9f, 0.0f, 0.0f,
                              lights, brake, broken, ex, 2.2f, ez, wet, q, k, rear);
        };
        // Heading 0 faces +z: a camera ahead sees the headlights, one behind
        // the tail lamps.
        const int ahead = count(1, 0, 0, 0.5f, 18.0f, 1.0f);
        const bool aheadFront = ahead == 2 && rear[0] == 0 && rear[1] == 0;
        bool onRoad = true, between = true;
        for (int j = 0; j < ahead; ++j)
            for (int c = 0; c < 4; ++c) {
                onRoad &= std::fabs(q[j * 12 + c * 3 + 1] - 0.06f) < 1e-4f;
                const float z = q[j * 12 + c * 3 + 2];
                between &= z > 2.0f && z < 18.0f && k[j] > 0.0f && k[j] <= 1.0f;
            }
        const int behind = count(1, 0, 0, -0.5f, -18.0f, 1.0f);
        const bool behindRear = behind == 2 && rear[0] == 1 && rear[1] == 1;
        const int dry = count(1, 1, 0, 0.5f, 18.0f, 0.0f);
        const int off = count(0, 0, 0, 0.5f, 18.0f, 1.0f) + count(0, 0, 0, 0.5f, -18.0f, 1.0f);
        const int braking = count(0, 1, 0, 0.5f, -18.0f, 1.0f) + count(0, 1, 0, 0.5f, 18.0f, 1.0f);
        const int broken = count(1, 0, 1, 0.5f, 18.0f, 1.0f);
        const int far = count(1, 0, 0, 0.5f, 80.0f, 1.0f);
        const int side = count(1, 0, 0, 0.5f, 18.0f, 1.0f, none4, none4);  // unmeasured model
        std::snprintf(msg, sizeof(msg),
                      "car streaks: 2 ahead (headlights), 2 behind (tail lamps), dry %d, lamps off %d, "
                      "braking %d, broken headlights %d, 80 away %d, unmeasured %d",
                      dry, off, braking, broken, far, side);
        verdict(aheadFront && behindRear && onRoad && between && dry == 0 && off == 0 &&
                    braking == 2 && broken == 0 && far == 0 && side == 2,
                msg);
        // N cars x at most 4 quads, capped per frame.
        int total = 0;
        for (int car = 0; car < 20; ++car) total += std::min(4, count(1, 0, 0, 0.5f, 18.0f, 1.0f));
        std::snprintf(msg, sizeof(msg),
                      "20 lit cars in view ask for %d streak quads, the frame draws at most %d",
                      total, weather::kWeatherCarStreakMax);
        verdict(total == 40 && weather::kWeatherCarStreakMax == 24, msg);
        // The splice: a vehicle project gets the block, any other none.
        const std::string tpl =
            "  updateParticles();\n"
            "void TerrainGame::renderRoadChunks() {\n";
        Gates gv;
        gv.weather = true;
        gv.vehicles = true;
        const std::string withCars = patchTemplate(tpl, gv);
        gv.vehicles = false;
        const std::string noCars = patchTemplate(tpl, gv);
        verdict(withCars.find("weather::weatherCarStreaks(") != std::string::npos &&
                    withCars.find("kRoadCarStreaks = weather::kWeatherCarStreakMax") != std::string::npos &&
                    noCars.find("weatherCarStreaks") == std::string::npos &&
                    noCars.find("kRoadCarStreaks = 0") != std::string::npos &&
                    withCars.find("{{ROAD_CAR_STREAKS}}") == std::string::npos &&
                    noCars.find("{{ROAD_CAR_STREAKS}}") == std::string::npos,
                "a project with vehicles and weather splices the car streaks into the lamp bag, "
                "any other none");
    }
    // The codegen: lamps and weather generate their runtime, the pools ride in
    // the streaming furniture items (embedded and on disk), and a project
    // with neither generates none of it.
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "tyrax-roadlight-check";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Project p;
        p.name = "rlcheck";
        p.dir = dir.string();
        p.scenes.emplace_back();
        SceneData& sc = p.scenes.back();
        sc.name = "main";
        auto road = [&](const std::string& id, std::vector<float> pts, bool kerb) {
            SceneObject o;
            o.type = PrimitiveType::Road;
            o.id = id;
            o.name = id;
            o.roadPoints = std::move(pts);
            o.roadWidth = 8.0f;
            o.roadKerb = kerb;
            o.roadTexture = "res/materials/roads/road-2lane.mtl";
            o.roadIntersectionTexture = "res/materials/roads/road-junction.mtl";
            sc.objects.push_back(o);
            return &sc.objects.back();
        };
        road("a", {-60, 0, 0, 0, 60, 0}, true)->roadFurniture.lamps.spacing = 15.0f;
        road("b", {0, -60, 0, 0, 0, 60}, false);
        int genNo = 0;
        auto gen = [&]() {
            std::string all;
            for (const auto& f : templates::generate(p)) all += f.content;
            if (const char* d = std::getenv("TYRAX_ROADLIGHT_DUMP")) {
                std::FILE* fp = std::fopen((std::string(d) + "/gen" + std::to_string(genNo) + ".txt").c_str(), "wb");
                if (fp) std::fwrite(all.data(), 1, all.size(), fp), std::fclose(fp);
            }
            ++genNo;
            return all;
        };
        auto has = [](const std::string& s, const char* what) {
            return s.find(what) != std::string::npos;
        };
        const std::string lit = gen();
        verdict(has(lit, "struct RoadFurnRt { int scene; int first; int count; int light; }") &&
                    has(lit, "ROAD_LAMPS[") && has(lit, "c.lampLight = fr.light;") &&
                    has(lit, "void TerrainGame::renderRoadLamps()") &&
                    has(lit, "renderRoadLamps(); costEnd(\"Road_lamps\"") &&
                    has(lit, "c.colorBag->single = &roadLampPoolColor_;") &&
                    has(lit, "|| ROAD_LAMP_COUNT > 0)  // + lit street lamps") &&
                    has(lit, "weather::reset(sceneIndex);") && !has(lit, "roadWetTint_;\n    }"),
                "a project with lamps generates the pools, the lamp table and the lamp runtime");
        p.settings.roadStreamRadius = 150.0f;
        p.settings.roadStreamEmbedTables = true;
        const std::string streamed = gen();
        // The streamed build of a furniture item is cut from the upload
        // block, so it carries the light column with it.
        const size_t at = streamed.find("    case RS_FURN: {");
        const size_t end = at == std::string::npos ? at : streamed.find("      break;\n", at);
        verdict(at != std::string::npos && end != std::string::npos &&
                    streamed.substr(at, end - at).find("c.lampLight = fr.light;") != std::string::npos &&
                    !has(streamed, "template anchor missing"),
                "a streamed project builds each pool with its furniture item (every anchor holds)");
        p.settings.roadStreamEmbedTables = false;
        const std::string disk = gen();
        int furnItems = 0, furnRows = 0;
        for (size_t k = disk.find("RoadFurnRt ROAD_FURN["); k != std::string::npos && k < disk.size();) {
            const size_t e = disk.find("};", k);
            for (size_t q = disk.find("    {", k); q != std::string::npos && q < e; q = disk.find("    {", q + 1))
                ++furnRows;
            break;
        }
        if (const size_t d = disk.find("RoadFileItem ROAD_FILE_ITEMS["); d != std::string::npos) {
            const size_t e = disk.find("};", d);
            for (size_t k = disk.find("    {7, ", d); k != std::string::npos && k < e;
                 k = disk.find("    {7, ", k + 1))
                ++furnItems;
        }
        std::snprintf(msg, sizeof(msg),
                      "tables on disk: every furniture row, pools included, is a roads.bin item "
                      "(%d rows, %d items)", furnRows, furnItems);
        verdict(furnRows > 0 && furnRows == furnItems && has(disk, "c.lampLight = fr.light;"), msg);
        p.settings.roadStreamRadius = 0.0f;
        p.settings.roadStreamEmbedTables = false;
        // Weather: a raining scene gets the wet tint on its asphalt and the rain.
        sc.weather = 1;
        sc.weatherIntensity = 0.6f;
        const std::string rain = gen();
        // This scene is the project's last (a new Project carries its own).
        std::string wantKinds = "SCENE_WEATHERS[SCENE_COUNT] = {", wantIn = "SCENE_WEATHER_INTENSITIES[SCENE_COUNT] = {";
        for (size_t i = 0; i + 1 < p.scenes.size(); ++i) wantKinds += "0, ", wantIn += "1.0F, ";
        wantKinds += "1}", wantIn += "0.6F}";
        verdict(has(rain, "if (grey) c.colorBag->single = &roadWetTint_;") &&
                    has(rain, "renderRain(); costEnd(\"Rain\"") && has(rain, wantKinds.c_str()) &&
                    has(rain, wantIn.c_str()),
                "a raining scene generates the wet tint, the rain and its authored weather");
        // Rain + road details: the puddle rows (a `wet` column), their
        // texture, the shared colour and the dry skip - embedded, streamed
        // and on disk.
        sc.objects[0].roadDetails = 0.8f;
        const std::string wetDetails = gen();
        verdict(has(wetDetails, "struct RoadDetailRt { int scene; int first; int count; int wet; };") &&
                    has(wetDetails, "ROAD_PUDDLE_TEX = ") && has(wetDetails, "c.puddle = dr.wet;") &&
                    has(wetDetails, "\"materials/roads/road-puddles.png\"") &&
                    has(wetDetails, "if (c.puddle) c.colorBag->single = &roadPuddleColor_;") &&
                    has(wetDetails, "if (c.puddle && roadPuddleColor_.a < 1.0F) continue;") &&
                    has(wetDetails, "weather::weatherPuddleColor(w,") && has(wetDetails, " puddles ("),
                "rain on a road with details generates the puddle rows, texture, colour and dry skip");
        p.settings.roadStreamRadius = 150.0f;
        p.settings.roadStreamEmbedTables = true;
        const std::string wetStream = gen();
        {
            const size_t at = wetStream.find("    case RS_DETAIL: {");
            const size_t end = at == std::string::npos ? at : wetStream.find("      break;\n    }\n", at);
            const std::string body = at == std::string::npos || end == std::string::npos
                                         ? std::string()
                                         : wetStream.substr(at, end - at);
            verdict(body.find("c.puddle = dr.wet;") != std::string::npos &&
                        body.find("puddleTex = roadTextures_[ROAD_PUDDLE_TEX];") != std::string::npos &&
                        !has(wetStream, "template anchor missing"),
                    "a streamed project builds each puddle chunk with its detail item (every anchor holds)");
        }
        p.settings.roadStreamEmbedTables = false;
        const std::string wetDisk = gen();
        {
            int rows = 0, items = 0, wetRows = 0;
            if (const size_t k = wetDisk.find("RoadDetailRt ROAD_DETAILS["); k != std::string::npos) {
                const size_t e = wetDisk.find("};", k);
                for (size_t q = wetDisk.find("    {", k); q != std::string::npos && q < e;
                     q = wetDisk.find("    {", q + 1)) {
                    ++rows;
                    const size_t close = wetDisk.find("},", q);
                    wetRows += wetDisk.compare(close - 3, 3, ", 1") == 0 ? 1 : 0;
                }
            }
            if (const size_t d = wetDisk.find("RoadFileItem ROAD_FILE_ITEMS["); d != std::string::npos) {
                const size_t e = wetDisk.find("};", d);
                for (size_t k = wetDisk.find("    {6, ", d); k != std::string::npos && k < e;
                     k = wetDisk.find("    {6, ", k + 1))
                    ++items;
            }
            std::snprintf(msg, sizeof(msg),
                          "tables on disk: every detail row, puddles included, is a roads.bin item "
                          "(%d rows, %d of them puddles, %d items)", rows, wetRows, items);
            verdict(rows > 0 && wetRows > 0 && rows == items, msg);
        }
        p.settings.roadStreamRadius = 0.0f;
        sc.objects[0].roadDetails = 0.0f;
        // Set Weather compiles to a request on the weather state.
        sc.weather = 0;
        {
            FlowGraph& fg = sc.objects[0].flowGraph;
            FlowNode on;
            on.id = 1;
            on.type = "OnStart";
            FlowNode set;
            set.id = 2;
            set.type = "SetWeather";
            set.num[0] = 1.0f, set.num[1] = 0.5f, set.num[2] = 3.0f;
            fg.nodes = {on, set};
            FlowLink l;
            l.id = 3, l.fromNode = 1, l.toNode = 2;
            fg.links = {l};
            fg.nextId = 4;
        }
        const std::string node = gen();
        verdict(has(node, "weather::request(1, 0.5F, 3.0F);") &&
                    has(node, "#include \"daynight.gen.hpp\"  // Set Weather"),
                "Set Weather compiles to a request on the weather state");
        // Live Logic (docs/live-logic.md): a build with weather says so in
        // its built list, and Set Weather compiles to its opcode.
        {
            const std::string built = livelogic::builtListText(p);
            const std::filesystem::path bf = dir / "livelogic.built";
            {
                std::FILE* fp = std::fopen(bf.string().c_str(), "wb");
                if (fp) std::fwrite(built.data(), 1, built.size(), fp), std::fclose(fp);
            }
            livelogic::BuiltList list;
            const bool loaded = livelogic::loadBuiltList(bf.string(), list);
            const int si = (int)p.scenes.size() - 1;
            const bool patchable = livelogic::capability(p, sc, sc.objects[0].flowGraph).patchable;
            livelogic::Program prog;
            const bool compiled = livelogic::compile(p, si, 0, prog);
            bool op = false;
            for (const livelogic::Instr& in : prog.instrs)
                op |= in.op == livelogic::OP_SetWeather && in.num[0] == 1.0f && in.num[1] == 0.5f &&
                      in.num[2] == 3.0f;
            verdict(loaded && list.weather && patchable && compiled && op,
                    "Live Logic: a build with weather lists it, Set Weather compiles to OP_SetWeather");
        }
        // Neither: no lamp and no weather code at all.
        sc.objects[0].flowGraph = FlowGraph{};
        sc.objects[0].roadFurniture.lamps.spacing = 0.0f;
        sc.objects[0].roadFurniture.trees.spacing = 12.0f;
        sc.objects[0].roadDetails = 0.8f;  // details without weather: no puddles
        const std::string none = gen();
        verdict(!has(none, "lampLight") && !has(none, "weather::") && !has(none, "ROAD_LAMP") &&
                    !has(none, "roadWetTint_") && has(none, "struct RoadFurnRt { int scene; int first; int count; };") &&
                    !has(none, "puddle") && !has(none, "PUDDLE") &&
                    has(none, "struct RoadDetailRt { int scene; int first; int count; };") &&
                    livelogic::builtListText(p).find("\nweather\n") == std::string::npos,
                "a project with no lamps and no weather generates none of it (details without puddles)");
        sc.objects[0].roadDetails = 0.0f;
        // Lamps Off in the only scene: no pools, no runtime.
        sc.objects[0].roadFurniture.lamps.spacing = 15.0f;
        sc.streetLamps = 2;
        const std::string off = gen();
        verdict(!has(off, "lampLight") && !has(off, "ROAD_LAMP"),
                "a scene whose lamps are Off bakes no pools");
        std::filesystem::remove_all(dir, ec);
    }
    {
        // The weather header pastes the core the editor compiled.
        const std::string h = weatherHeaderSource({SceneWeather{1, 0.5f, 0, 0.0f}});
        verdict(h.find("struct WeatherState") != std::string::npos &&
                    h.find("SCENE_WEATHERS[SCENE_COUNT] = {1}") != std::string::npos &&
                    h.find("/*") == std::string::npos,
                "the game's weather header carries the editor's core, no block comments");
    }
}

}  // namespace roadlight
