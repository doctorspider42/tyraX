// Road streaming (docs/roads.md "Road streaming"). See roadstream.hpp for the
// contract: this file is the codegen that turns the non-streaming road
// source into the streaming runtime, the template patches, and the
// --vehicle-check block that exercises the core and the codegen.
#include "roadstream.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <sstream>

#include "project.hpp"
#include "roadgen.hpp"
#include "templates.hpp"

#include "roadstream_core_gen.hpp"

namespace roadstream {

// The core, compiled for the host. The game gets the same text inside its
// class (coreSource below).
#include "roadstream_core.inl"

namespace {

std::string lit(float v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.7g", (double)v);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s + "F";
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

// The cutter: every piece of the streaming runtime that is also part of the
// non-streaming build is CUT from that build's text, never retyped. A miss is
// recorded and the piece comes back as an #error line, so a template edit
// that moves an anchor fails the game build instead of shipping a stale copy.
struct Cutter {
    std::vector<std::string>& errors;
    std::string missing(const char* what) {
        errors.push_back(what);
        return std::string("\n#error road streaming: template anchor missing (") + what +
               ") - see src/roadstream.cpp\n";
    }
    // [from, to) - or [from, to] with `inclusive`.
    std::string between(const std::string& s, const std::string& from, const std::string& to,
                        bool inclusive, const char* what) {
        const size_t a = s.find(from);
        if (a == std::string::npos) return missing(what);
        if (s.find(from, a + 1) != std::string::npos) return missing(what);
        const size_t b = s.find(to, a + from.size());
        if (b == std::string::npos) return missing(what);
        return s.substr(a, b - a + (inclusive ? to.size() : 0));
    }
    // Exactly one occurrence of `from`, replaced.
    std::string once(const std::string& s, const std::string& from, const std::string& to,
                     const char* what) {
        const size_t a = s.find(from);
        if (a == std::string::npos || s.find(from, a + 1) != std::string::npos)
            return s + missing(what);
        std::string out = s;
        out.replace(a, from.size(), to);
        return out;
    }
};

// ---------------------------------------------------------------------------
// The class members. The core first (its structs are used by what follows).
// ---------------------------------------------------------------------------
const char* kMembers = R"RSMEM(  // --- road streaming (docs/roads.md "Road streaming") ---
  // The project streams its roads (ProjectSettings::roadStreamRadius): every
  // chunk buildRoads would make, and every baked row, is an ITEM with an XZ
  // box; only the items near the view focus are built. The structs below are
  // src/roadstream_core.inl, pasted verbatim (the editor compiles the same
  // file for --vehicle-check).
{{CORE}}
  struct RsItem {
    unsigned char kind = 0;  // RS_STRIP .. RS_FURN_BOX
    int a = 0;               // the road (strip) or the table row
    int b = 0;               // strip: resume station (seg << 16 | k); a piece: its first vertex
    float arc = 0.0F, px = 0.0F, pz = 0.0F;  // strip: the arc state before that station
    int verts = 0;           // vertices the item makes (planned)
    int chunk = -1;          // its procChunks index while resident
  };
  mutable std::vector<RsItem> rsItems_;
  std::vector<RsBox> rsBoxes_;
  std::vector<unsigned char> rsOn_;       // resident, per item
  std::vector<RsLocalIndex> rsIndex_;     // per item: its height index while resident
  std::vector<int> rsResident_;           // the resident items
  mutable std::vector<int> rsFree_;       // free procChunks slots (owner -8)
  std::vector<float> rsSpillY_;           // every spill vertex's height, lifted once
  std::vector<int> rsSpillAt_;            // per ROAD_SPILLS row: its first rsSpillY_ entry
  RsGrid rsGrid_;
  RsRing rsRing_;
  std::vector<int> rsLoad_;
  std::vector<float> rsKeys_;
  ProcChunk rsScratch_;                   // the plan pass's one chunk
  mutable size_t rsChunkCount_ = 0;       // procChunks.size() when the slots were last mapped
  int rsTarget_ = -1;                     // the slot a strip replay writes into
  bool rsPending_ = true;
  float rsLastFocus_[4] = {0.0F, 0.0F, 0.0F, 0.0F};
  int rsLastFoci_ = 0;
  int rsPlanVerts_ = 0, rsPlanChunks_ = 0, rsPlanPackages_ = 0, rsPlanTriangles_ = 0;
  int rsStatBuilt_ = 0, rsStatDropped_ = 0, rsStatFrames_ = 0, rsMismatch_ = 0;
  unsigned int rsStatTicks_ = 0, rsStatWorst_ = 0;
  void roadStreamSetup(int scene);
  void roadStreamUpdate(float fx, float fz, float f2x, float f2z, bool two, int budget);
  void roadStreamBuild(int item);
  void roadStreamDrop(int item);
  void roadStreamTessellate(int ri, int item);
  bool roadStreamOpen(ProcChunk*& c, int item, int ri, int seg, int k, float arc, float x,
                      float z);
  void roadStreamPlanClose();
  void roadStreamAddItem(int kind, int a, int b, int verts, const float* v, int stride,
                         int zAt, int count);
  void roadStreamLiftSpills(const float* v, int stride, size_t count, int stripRun,
                            const RsBox& box);
  void roadStreamFinish(ProcChunk& c);
  int roadStreamSlot();
  void roadStreamRebindAll();
  void roadStreamRemap() const;
  Tyra::Texture* roadStreamTexture(int tex);
  bool roadStreamReady(float x, float z) const;
)RSMEM";

// ---------------------------------------------------------------------------
// The functions. {{...}} are pieces cut from the non-streaming build.
// ---------------------------------------------------------------------------
const char* kImpl = R"RSIMPL(
// ---------------------------------------------------------------------------
// Road streaming (docs/roads.md "Road streaming"). This project streams its
// roads, so buildRoads is never called: roadStreamSetup PLANS the scene into
// items - every strip chunk buildRoads would cut, every baked row - and
// roadStreamUpdate keeps the items within ROAD_STREAM_RADIUS of the view
// focus built, nearest first, at most ROAD_STREAM_BUDGET vertex units a
// frame, dropping what has moved beyond ROAD_STREAM_KEEP. Every piece below
// that BUILDS geometry is cut from the non-streaming build's own source by
// the codegen (src/roadstream.cpp), so a streamed chunk is the chunk
// buildRoads makes.
// ---------------------------------------------------------------------------
constexpr float ROAD_STREAM_RADIUS = {{RADIUS}};
constexpr float ROAD_STREAM_KEEP = {{KEEP}};
constexpr float ROAD_STREAM_CELL = {{CELL}};
constexpr int ROAD_STREAM_BUDGET = {{BUDGET}};
enum : unsigned char {
  RS_STRIP = 0, RS_JUNCTION = 1, RS_SPILL = 2, RS_EDGE = 3, RS_KERB = 4,  // the surface
  RS_BRIDGE = 5, RS_DETAIL = 6, RS_FURN = 7,                             // drawn only
  RS_BRIDGE_BOX = 8, RS_FURN_BOX = 9                                     // collision only
};

Tyra::Texture* TerrainGame::roadStreamTexture(int tex) {
  if (tex < 0 || tex >= ROAD_TEXTURE_COUNT) return nullptr;
  if (!roadTextures_[tex]) roadTextures_[tex] = acquireTexture(ROAD_TEXTURE_PATHS[tex]);
  return roadTextures_[tex];
}

// One road's strip chunks: buildRoads' own tessellator. item < 0 PLANS the
// whole road (every chunk into rsScratch_, recorded as an item with its
// resume state); item >= 0 REPLAYS that one chunk into procChunks[rsTarget_]
// and stops where buildRoads would have started the next one.
void TerrainGame::roadStreamTessellate(int ri, int item) {
  const int scene = ROAD_DEFS[ri].scene;
  (void)scene;
{{PROLOGUE}}{{BODY}}  (void)any;
}

bool TerrainGame::roadStreamOpen(ProcChunk*& c, int item, int ri, int seg, int k, float arc,
                                 float x, float z) {
  if (item >= 0) {
    if (c) return false;  // the replayed chunk is complete
    c = &procChunks[(size_t)rsTarget_];
    return true;
  }
  if (c) roadStreamPlanClose();
  RsItem it;
  it.kind = RS_STRIP;
  it.a = ri;
  it.b = (seg << 16) | (k & 0xFFFF);
  it.arc = arc;
  it.px = x;
  it.pz = z;
  rsItems_.push_back(it);
  rsBoxes_.push_back(RsBox{0.0F, 0.0F, 0.0F, 0.0F});
  rsScratch_.vertices.clear();
  rsScratch_.colors.clear();
  rsScratch_.sts.clear();
  c = &rsScratch_;
  return true;
}

// A planned strip chunk is complete in rsScratch_: its box, its size, the
// statistics buildRoads logs, and the spill heights it lifts.
void TerrainGame::roadStreamPlanClose() {
  RsItem& it = rsItems_.back();
  const ProcChunk& c = rsScratch_;
  it.verts = (int)c.vertices.size();
  RsBox b = {0.0F, 0.0F, 0.0F, 0.0F};
  if (!c.vertices.empty()) {
    b = RsBox{c.vertices[0].x, c.vertices[0].z, c.vertices[0].x, c.vertices[0].z};
    for (size_t i = 1; i < c.vertices.size(); ++i) {
      const float x = c.vertices[i].x, z = c.vertices[i].z;
      if (x < b.x0) b.x0 = x;
      if (x > b.x1) b.x1 = x;
      if (z < b.z0) b.z0 = z;
      if (z > b.z1) b.z1 = z;
    }
  }
  rsBoxes_.back() = b;
  roadStreamLiftSpills(reinterpret_cast<const float*>(c.vertices.data()), 4, c.vertices.size(),
                       c.stripRun, b);
  ++rsPlanChunks_;
  rsPlanVerts_ += it.verts;
  const size_t run = c.stripRun > 0 ? (size_t)c.stripRun : (size_t)75;
  rsPlanPackages_ += (int)((c.vertices.size() + run - 1) / run);
  if (c.stripRun <= 0) {
    rsPlanTriangles_ += (int)(c.vertices.size() / 3);
  } else {
    for (size_t at = 0; at < c.vertices.size(); at += run) {
      const size_t left = c.vertices.size() - at;
      const size_t len = left < run ? left : run;
      for (size_t k = 0; k + 2 < len; ++k) {
        const Tyra::Vec4& a = c.vertices[at + k];
        const Tyra::Vec4& bb = c.vertices[at + k + 1];
        const Tyra::Vec4& d = c.vertices[at + k + 2];
        const bool degenerate = (a.x == bb.x && a.y == bb.y && a.z == bb.z) ||
                                (bb.x == d.x && bb.y == d.y && bb.z == d.z) ||
                                (a.x == d.x && a.y == d.y && a.z == d.z);
        if (!degenerate) ++rsPlanTriangles_;
      }
    }
  }
}

// SPILLS: buildRoads lifts each spill vertex onto roadSurfaceAt over the
// COMPLETE road set (every strip and junction row of the scene, no spill).
// A streamed scene never holds that set, so the lift happens once, here, as
// the plan pass walks every surface chunk - the same triangle test, the same
// maximum - and rsSpillY_ keeps the answers (4 bytes a spill vertex).
void TerrainGame::roadStreamLiftSpills(const float* v, int stride, size_t count, int stripRun,
                                       const RsBox& box) {
  if (rsSpillY_.empty() || count < 3) return;
  for (int si = 0; si < ROAD_SPILL_COUNT; ++si) {
    if (rsSpillAt_[(size_t)si] < 0) continue;
    const RoadSpillRt& sp = ROAD_SPILLS[si];
    float* ys = &rsSpillY_[(size_t)rsSpillAt_[(size_t)si]];
    for (int k = 0; k < sp.count; ++k) {
      const float* sv = &ROAD_SPILL_VERTS[(size_t)(sp.first + k) * 5];
      if (sv[0] < box.x0 || sv[0] > box.x1 || sv[1] < box.z0 || sv[1] > box.z1) continue;
      const float y = RsTri::highest(v, stride, count, stripRun, sv[0], sv[1]);
      if (y > ys[k]) ys[k] = y;
    }
  }
}

void TerrainGame::roadStreamAddItem(int kind, int a, int b, int verts, const float* v,
                                    int stride, int zAt, int count) {
  RsItem it;
  it.kind = (unsigned char)kind;
  it.a = a;
  it.b = b;
  it.verts = verts;
  rsItems_.push_back(it);
  RsBox box = {v[0], v[zAt], v[0], v[zAt]};
  for (int i = 1; i < count; ++i) {
    const float x = v[(size_t)i * (size_t)stride], z = v[(size_t)i * (size_t)stride + (size_t)zAt];
    if (x < box.x0) box.x0 = x;
    if (x > box.x1) box.x1 = x;
    if (z < box.z0) box.z0 = z;
    if (z > box.z1) box.z1 = z;
  }
  rsBoxes_.push_back(box);
}

void TerrainGame::roadStreamSetup(int scene) {
{{PROLOGUE}}  (void)any;
  (void)grey;
  const u32 rsT0 = profTicks();
  // Whatever an earlier scene left (loadScene clears procChunks anyway).
  bool erased = false;
  for (size_t i = procChunks.size(); i > 0; --i)
    if (procChunks[i - 1].owner <= -3 && procChunks[i - 1].owner >= -8) {
      procChunks.erase(procChunks.begin() + (i - 1));
      erased = true;
    }
  for (size_t i = procColliders.size(); i > 0; --i)
    if ((procColliders[i - 1].owner == -5 || procColliders[i - 1].owner == -7) &&
        procColliders[i - 1].instance <= -2)
      procColliders.erase(procColliders.begin() + (i - 1));
  if (erased) roadStreamRebindAll();
  rsItems_.clear();
  rsBoxes_.clear();
  rsOn_.clear();
  rsIndex_.clear();
  rsResident_.clear();
  rsFree_.clear();
  rsSpillY_.clear();
  rsSpillAt_.assign(ROAD_SPILL_COUNT > 0 ? (size_t)ROAD_SPILL_COUNT : (size_t)1, -1);
  rsPlanVerts_ = rsPlanChunks_ = rsPlanPackages_ = rsPlanTriangles_ = 0;
  rsPending_ = true;
  rsLastFoci_ = 0;
  rsChunkCount_ = procChunks.size();
  for (int si = 0; si < ROAD_SPILL_COUNT; ++si) {
    const RoadSpillRt& sp = ROAD_SPILLS[si];
    if (sp.scene != scene) continue;
    rsSpillAt_[(size_t)si] = (int)rsSpillY_.size();
    rsSpillY_.insert(rsSpillY_.end(), (size_t)sp.count, -1.0e30F);
  }
  // 1. Strip chunks: every road walked once, chunk by chunk, exactly as
  //    buildRoads cuts it - nothing kept but each chunk's resume state and box.
  for (int ri = 0; ri < ROAD_COUNT; ++ri) {
    const RoadDefRt& rd = ROAD_DEFS[ri];
    if (rd.scene != scene || rd.pointCount < 2) continue;
    roadStreamTessellate(ri, -1);
  }
  rsScratch_ = ProcChunk();  // the plan's buffers go back to the heap
  // 2. Junction rows (nodes, paint, bridge decks), in buildRoads' 1800 pieces.
  for (int ji = 0; ji < ROAD_JUNCTION_COUNT; ++ji) {
    const RoadJunctionRt& j = ROAD_JUNCTIONS[ji];
    if (j.scene != scene) continue;
    for (int first = 0; first < j.count; first += 1800) {
      const int count = std::min(1800, j.count - first);
      const float* v = &ROAD_JUNCTION_VERTS[(size_t)(j.first + first) * 5];
      roadStreamAddItem(RS_JUNCTION, ji, first, count, v, 5, 2, count);
      roadStreamLiftSpills(v, 5, (size_t)count, 0, rsBoxes_.back());
      ++rsPlanChunks_;
      rsPlanVerts_ += count;
      rsPlanPackages_ += (count + 74) / 75;
      rsPlanTriangles_ += count / 3;
    }
  }
  // 3. Spills: every surface under them has now been seen.
  for (int si = 0; si < ROAD_SPILL_COUNT; ++si) {
    const RoadSpillRt& sp = ROAD_SPILLS[si];
    if (sp.scene != scene || sp.count < 1) continue;
    float* ys = &rsSpillY_[(size_t)rsSpillAt_[(size_t)si]];
    for (int k = 0; k < sp.count; ++k) {
      const float* sv = &ROAD_SPILL_VERTS[(size_t)(sp.first + k) * 5];
      float y = ys[k];
      if (y < -1.0e29F) y = terrainHeightAt(sv[0], sv[1]) + 0.12F;
      ys[k] = y + 0.02F + sp.lift;
    }
    roadStreamAddItem(RS_SPILL, si, 0, sp.count, &ROAD_SPILL_VERTS[(size_t)sp.first * 5], 5, 1,
                      sp.count);
    ++rsPlanChunks_;
    rsPlanVerts_ += sp.count;
    rsPlanPackages_ += (sp.count + 74) / 75;
    rsPlanTriangles_ += sp.count / 3;
  }
  // 4. Soft edges, 1800 at a time.
  for (int ei = 0; ei < ROAD_EDGE_COUNT; ++ei) {
    const RoadSpillRt& ed = ROAD_EDGES[ei];
    if (ed.scene != scene) continue;
    for (int at = 0; at < ed.count; at += 1800) {
      const int n = ed.count - at < 1800 ? ed.count - at : 1800;
      roadStreamAddItem(RS_EDGE, ei, at, n, &ROAD_EDGE_VERTS[(size_t)(ed.first + at) * 5], 5, 1,
                        n);
      ++rsPlanChunks_;
      rsPlanVerts_ += n;
      rsPlanPackages_ += (n + 74) / 75;
      rsPlanTriangles_ += n / 3;
    }
  }
{{SETUP_ROWS}}  if (rsItems_.size() > 32000) {
    // The grid stores item ids in 16 bits and the collider tag in a short.
    TYRA_LOG("ROADSTREAM too many items (", (int)rsItems_.size(), "), the rest are dropped");
    rsItems_.resize(32000);
    rsBoxes_.resize(32000);
  }
  rsOn_.assign(rsItems_.size(), 0);
  rsIndex_.resize(rsItems_.size());
  rsGrid_.build(rsBoxes_.data(), (int)rsBoxes_.size(), ROAD_STREAM_CELL);
  TYRA_LOG("ROADS scene ", scene, " chunks ", rsPlanChunks_, " vertices ", rsPlanVerts_,
           " (streamed)");
  TYRA_LOG("ROADSTRIP scene ", scene, " strips ", useStrips ? 1 : 0, " packages ",
           rsPlanPackages_, " triangles ", rsPlanTriangles_);
  TYRA_LOG("ROADSTREAM plan scene ", scene, " items ", (int)rsItems_.size(), " cells ",
           rsGrid_.nx, "x", rsGrid_.nz, " radius ", (int)ROAD_STREAM_RADIUS, " keep ",
           (int)ROAD_STREAM_KEEP, " spill vertices ", (int)rsSpillY_.size(), " ms ",
           (int)((profTicks() - rsT0) / 294912U));
}

// Every bag's pointers into its chunk's arrays - after procChunks moved its
// elements (a reallocation, an erase), they point at the old addresses.
void TerrainGame::roadStreamRebindAll() {
  for (ProcChunk& c : procChunks) {
    if (!c.bag || !c.colorBag) continue;
    c.colors.bind(c.colorBag);
    c.vertices.bind(c.bag);
    if (c.bag->texture && c.texBag) c.sts.bind(c.texBag);
  }
}

// Which procChunks slot holds which item, read back from the chunks' tags -
// after anything else erased or inserted procChunks (a prefab despawn, a
// regenerated volume), the indices the items kept are stale.
void TerrainGame::roadStreamRemap() const {
  TerrainGame* self = const_cast<TerrainGame*>(this);
  for (RsItem& it : rsItems_)
    if (it.kind < RS_BRIDGE_BOX) it.chunk = -1;
  rsFree_.clear();
  for (size_t ci = 0; ci < procChunks.size(); ++ci) {
    const ProcChunk& c = procChunks[ci];
    if (c.owner == -8) {
      rsFree_.push_back((int)ci);
      continue;
    }
    if (c.owner > -3 || c.owner < -7 || c.instance > -2) continue;
    const int item = -2 - c.instance;
    if (item < (int)rsItems_.size()) rsItems_[(size_t)item].chunk = (int)ci;
  }
  self->roadStreamRebindAll();
  rsChunkCount_ = procChunks.size();
}

int TerrainGame::roadStreamSlot() {
  if (rsChunkCount_ != procChunks.size()) roadStreamRemap();
  while (!rsFree_.empty()) {
    const int slot = rsFree_.back();
    rsFree_.pop_back();
    if (slot >= 0 && slot < (int)procChunks.size() && procChunks[(size_t)slot].owner == -8)
      return slot;
  }
  const bool grows = procChunks.size() == procChunks.capacity();
  procChunks.emplace_back();
  procChunks.back().owner = -8;
  if (grows) roadStreamRebindAll();  // every chunk just moved
  rsChunkCount_ = procChunks.size();
  return (int)procChunks.size() - 1;
}

// procFinishChunks for ONE chunk - its loop body, cut from the template, so
// a streamed chunk gets exactly the bags, the box and the info bag the full
// pass would give it, without the full pass's walk over every chunk.
void TerrainGame::roadStreamFinish(ProcChunk& c) {
{{BATCH_INFO}}  do {
{{FINISH_BODY}}  } while (false);
}

void TerrainGame::roadStreamBuild(int item) {
  if (item < 0 || item >= (int)rsItems_.size() || rsOn_[(size_t)item]) return;
  RsItem& it = rsItems_[(size_t)item];
  const short tag = (short)(-2 - item);
{{PROLOGUE}}  (void)any;
  (void)grey;
  (void)tag;
  if (it.kind == RS_BRIDGE_BOX || it.kind == RS_FURN_BOX) {
{{BUILD_BOXES}}    rsOn_[(size_t)item] = 1;
    rsResident_.push_back(item);
    return;
  }
  const int slot = roadStreamSlot();
  procChunks[(size_t)slot] = ProcChunk();
  ProcChunk& c = procChunks[(size_t)slot];
  c.instance = -2 - item;
  it.chunk = slot;
  if (it.verts > 0) {
    c.vertices.reserve((size_t)it.verts);
    c.colors.reserve((size_t)it.verts);
    if (it.kind != RS_KERB && it.kind != RS_BRIDGE && it.kind != RS_FURN)
      c.sts.reserve((size_t)it.verts);
  }
  switch (it.kind) {
    case RS_STRIP:
      rsTarget_ = slot;
      roadStreamTessellate(it.a, item);
      if ((int)c.vertices.size() != it.verts && ++rsMismatch_ <= 4)
        TYRA_LOG("ROADSTREAM replay mismatch item ", item, " road ", it.a, " planned ", it.verts,
                 " built ", (int)c.vertices.size());
      break;
    case RS_JUNCTION: {
      const RoadJunctionRt& j = ROAD_JUNCTIONS[it.a];
      Tyra::Texture* tex = roadStreamTexture(j.tex);
      const int first = it.b;
{{JUNCTION_FILL}}      break;
    }
    case RS_SPILL: {
      const RoadSpillRt& sp = ROAD_SPILLS[it.a];
      const RoadDefRt& rd = ROAD_DEFS[sp.road];
      roadStreamTexture(rd.tex);
      const float* spillY = &rsSpillY_[(size_t)rsSpillAt_[(size_t)it.a]];
      size_t yi = 0;
{{SPILL_FILL}}      break;
    }
    case RS_EDGE: {
      const RoadSpillRt& ed = ROAD_EDGES[it.a];
      const RoadDefRt& rd = ROAD_DEFS[ed.road];
      Tyra::Texture* tex = roadStreamTexture(rd.tex);
      const int at = it.b;
      const int n = ed.count - at < 1800 ? ed.count - at : 1800;
{{EDGE_FILL}}      break;
    }
{{BUILD_ROWS}}    default:
      break;
  }
  roadStreamFinish(c);
  if (it.kind <= RS_KERB &&
      !rsIndex_[(size_t)item].build(reinterpret_cast<const float*>(c.vertices.data()),
                                    c.vertices.size(), c.stripRun))
    TYRA_LOG("ROADSTREAM item ", item, " too large for its height index");
  rsOn_[(size_t)item] = 1;
  rsResident_.push_back(item);
}

// Frees one item's geometry (or its collision box). The caller takes it off
// rsResident_.
void TerrainGame::roadStreamDrop(int item) {
  RsItem& it = rsItems_[(size_t)item];
  if (!rsOn_[(size_t)item]) return;
  rsOn_[(size_t)item] = 0;
  if (it.kind == RS_BRIDGE_BOX || it.kind == RS_FURN_BOX) {
    const short tag = (short)(-2 - item);
    for (size_t i = procColliders.size(); i > 0; --i)
      if (procColliders[i - 1].instance == tag &&
          (procColliders[i - 1].owner == -5 || procColliders[i - 1].owner == -7)) {
        procColliders.erase(procColliders.begin() + (i - 1));
        break;
      }
    return;
  }
  if (rsChunkCount_ != procChunks.size() || it.chunk < 0 ||
      it.chunk >= (int)procChunks.size() ||
      procChunks[(size_t)it.chunk].instance != -2 - item)
    roadStreamRemap();
  if (it.chunk >= 0) {
    procChunks[(size_t)it.chunk] = ProcChunk();  // arrays and bags back to the heap
    procChunks[(size_t)it.chunk].owner = -8;
    rsFree_.push_back(it.chunk);
  }
  it.chunk = -1;
  rsIndex_[(size_t)item] = RsLocalIndex();
}

// One streaming step: drop what is beyond the keep radius of every focus,
// then build the nearest missing items until the budget is spent (always at
// least one). budget < 0 = ROAD_STREAM_BUDGET; loadScene passes "all".
void TerrainGame::roadStreamUpdate(float fx, float fz, float f2x, float f2z, bool two,
                                   int budget) {
  if (rsItems_.empty()) return;
  if (budget < 0) budget = ROAD_STREAM_BUDGET;
  const float foci[4] = {fx, fz, f2x, f2z};
  const int nf = two ? 2 : 1;
  // Nothing to do until a focus has moved a little or a build is pending.
  if (!rsPending_ && nf == rsLastFoci_) {
    bool moved = false;
    for (int f = 0; f < nf && !moved; ++f) {
      const float dx = foci[f * 2] - rsLastFocus_[f * 2];
      const float dz = foci[f * 2 + 1] - rsLastFocus_[f * 2 + 1];
      moved = dx * dx + dz * dz > 4.0F;
    }
    if (!moved) return;
  }
  for (int f = 0; f < 4; ++f) rsLastFocus_[f] = foci[f];
  rsLastFoci_ = nf;
  const u32 t0 = profTicks();
  for (size_t r = 0; r < rsResident_.size();) {
    const int item = rsResident_[r];
    if (RsRing::keepItem(rsBoxes_[(size_t)item], foci, nf, ROAD_STREAM_KEEP)) {
      ++r;
      continue;
    }
    roadStreamDrop(item);
    rsResident_[r] = rsResident_.back();
    rsResident_.pop_back();
    ++rsStatDropped_;
  }
  rsRing_.wanted(rsGrid_, rsBoxes_.data(), rsOn_.data(), (int)rsItems_.size(), foci, nf,
                 ROAD_STREAM_RADIUS, rsLoad_, rsKeys_);
  int spent = 0;
  size_t built = 0;
  for (; built < rsLoad_.size() && spent < budget; ++built) {
    const RsItem& it = rsItems_[(size_t)rsLoad_[built]];
    spent += it.verts * (it.kind == RS_STRIP ? {{STRIP_WEIGHT}} : 1) + {{ITEM_COST}};
    roadStreamBuild(rsLoad_[built]);
    ++rsStatBuilt_;
  }
  rsPending_ = built < rsLoad_.size();
  const u32 ticks = profTicks() - t0;
  int verts = 0;
  size_t indexBytes = 0;
  auto residentTotals = [&]() {
    verts = 0;
    indexBytes = 0;
    for (const int item : rsResident_) {
      verts += rsItems_[(size_t)item].verts;
      indexBytes += rsIndex_[(size_t)item].bytes();
    }
  };
  if (budget > 0x1000000) {
    // loadScene's drain: one line, and it does not count as a frame.
    residentTotals();
    TYRA_LOG("ROADSTREAM load resident ", (int)rsResident_.size(), "/", (int)rsItems_.size(),
             " vertices ", verts, " index KB ", (int)(indexBytes / 1024), " ms ",
             (int)(ticks / 294912U));
    rsStatBuilt_ = rsStatDropped_ = 0;
    return;
  }
  rsStatTicks_ += ticks;
  if (ticks > rsStatWorst_) rsStatWorst_ = ticks;
  // The streaming log: a line a few seconds apart while the ring is moving -
  // what is resident, what moved, and the worst frame's streaming cost.
  if (++rsStatFrames_ >= 150 && (rsStatBuilt_ > 0 || rsStatDropped_ > 0)) {
    residentTotals();
    TYRA_LOG("ROADSTREAM resident ", (int)rsResident_.size(), "/", (int)rsItems_.size(),
             " vertices ", verts, " index KB ", (int)(indexBytes / 1024), " built ",
             rsStatBuilt_, " dropped ", rsStatDropped_, " busy frames ", rsStatFrames_,
             " total us ", (int)(rsStatTicks_ / 295U), " worst us ",
             (int)(rsStatWorst_ / 295U));
    rsStatFrames_ = 0;
    rsStatBuilt_ = rsStatDropped_ = 0;
    rsStatTicks_ = rsStatWorst_ = 0;
  }
}

// Is every surface item touching (x, z)'s cell resident? A car that is not
// being driven only simulates where this holds (docs/roads.md "Road
// streaming": far cars freeze rather than drop through an unbuilt road).
bool TerrainGame::roadStreamReady(float x, float z) const {
  const int cell = rsGrid_.cellAt(x, z);
  if (cell < 0) return true;  // no road anywhere near: the terrain is the ground
  for (unsigned int e = rsGrid_.start[(size_t)cell]; e < rsGrid_.start[(size_t)cell + 1]; ++e) {
    const int item = rsGrid_.items[e];
    if (rsItems_[(size_t)item].kind <= RS_KERB && !rsOn_[(size_t)item]) return false;
  }
  return true;
}
)RSIMPL";

// ---------------------------------------------------------------------------
// TABLES ON DISK (docs/roads.md "Tables on disk", src/roadfile.hpp). With
// Params::tablesOnDisk the baked rows are not in the ELF: the pieces below
// are spliced into the text above (at anchors this file owns, so a miss is
// the same #error as a template anchor), and nothing else changes - the rows
// a build expands are the ones the embedded build expands, read from
// bin/roadfile/roads.bin instead of .rodata.
// ---------------------------------------------------------------------------
const char* kDiskMembers = R"RSDMEM(  // --- tables on disk (docs/roads.md "Tables on disk") ---
  // The baked rows are in bin/roadfile/roads.bin; ROAD_FILE_ITEMS (scene_data.hpp)
  // says where each item's are. A reader thread reads the items the ring
  // will want next into a slot; the frame collects finished slots into a
  // capped cache, builds from it and frees the bytes.
  struct RsIoSlot {
    volatile int state = 0;  // 0 free, 1 queued, 2 reading, 3 read, 4 read failed, 5 bad checksum
    int item = -1;
    unsigned int at = 0, bytes = 0, sum = 0, ticket = 0, ticks = 0;
    unsigned char* buf = nullptr;
  };
  RsIoSlot rsIo_[{{SLOTS}}];
  unsigned int rsIoTicket_ = 0;
  int rsIoSema_ = -1, rsIoThread_ = -1;
  int rsFd_ = -1;
  int rsFileState_ = 0;  // 0 not opened yet, 1 good, -1 missing, -2 stale, -3 unreadable
  int rsReadErrors_ = 0;
  char rsFileMsg_[48] = {};
  std::vector<unsigned char*> rsBytes_;  // per item: its rows, read and not yet built
  std::vector<unsigned char> rsQueued_;  // per item: a reader slot holds it
  std::vector<unsigned char> rsBad_;     // per item: failed reads (3 = given up)
  RsCache rsCache_;                      // which items hold bytes, LRU
  unsigned int rsUseStamp_ = 0;
  std::vector<float> rsSpillXZ_;         // the plan only: every spill vertex's x, z
  RsBox rsSpillBox_ = {0.0F, 0.0F, 0.0F, 0.0F};
  int rsStatReads_ = 0, rsStatLate_ = 0, rsNowReads_ = 0;
  unsigned int rsStatReadBytes_ = 0, rsStatReadTicks_ = 0, rsStatReadWorst_ = 0;
  unsigned int rsNowBytes_ = 0, rsNowTicks_ = 0;
  static void roadStreamIoMain(void* self);
  bool roadStreamOpenFile();
  void roadStreamFileError(int state, const char* hud, const char* detail);
  unsigned char* roadStreamReadNow(int fi);
  bool roadStreamIoRequest(int item);
  void roadStreamIoCollect();
  void roadStreamIoIdle();
  void roadStreamFreeBytes(int item);
  void roadStreamAddFileItem(int fi, int verts);
  void roadStreamDrawError();
)RSDMEM";

// After the RS_ enum: the read-ahead constants and the one read primitive.
const char* kDiskDefs = R"RSDDEF(// Tables on disk: read this much further out than the build radius (less
// than the keep band, so an item dropped behind is not read again), keep at
// most ROAD_STREAM_CACHE bytes read ahead, and read with ROAD_IO_SLOTS items
// in flight.
constexpr float ROAD_STREAM_PREFETCH = {{PREFETCH}};
constexpr size_t ROAD_STREAM_CACHE = {{CACHE}};
constexpr int ROAD_IO_SLOTS = {{SLOTS}};
alignas(16) static unsigned char rsIoStack[16 * 1024];

// One absolute seek, then reads of at most 16 KB - the engine's proven
// pattern on the PS2 host filesystem (audio_song.cpp), and a short chunk
// keeps a main-thread file call (a log line, a texture) from waiting behind a
// whole item. A short read is a failure, never a partial item.
static bool rsReadRange(int fd, unsigned int at, unsigned int bytes, unsigned char* buf) {
  if (fd < 0) return false;
  if (lseek(fd, (off_t)at, SEEK_SET) != (off_t)at) return false;
  unsigned int done = 0;
  while (done < bytes) {
    unsigned int n = bytes - done;
    if (n > 16384U) n = 16384U;
    const int got = (int)read(fd, buf + done, n);
    if (got <= 0) return false;
    done += (unsigned int)got;
  }
  return true;
}
)RSDDEF";

// Before the strips are walked: the spill lift needs every spill vertex's XZ.
const char* kDiskSpillRead = R"RSDSP(  // TABLES ON DISK: open bin/roadfile/roads.bin (a missing or stale file is reported
  // here, loudly, once), then read the scene's spill rows - the lift below
  // needs every spill vertex's x and z before the strips are walked. Spills
  // are small (one per crossing decal); only x and z are kept, for the plan.
  roadStreamOpenFile();
  rsSpillXZ_.assign(rsSpillY_.size() * 2, 0.0F);
  rsSpillBox_ = RsBox{1.0e30F, 1.0e30F, -1.0e30F, -1.0e30F};
  if (rsFileState_ != 1) rsSpillXZ_.clear();
  for (int fi = 0; fi < ROAD_FILE_ITEM_COUNT && !rsSpillXZ_.empty(); ++fi) {
    const RoadFileItem& d = ROAD_FILE_ITEMS[fi];
    if ((int)d.scene != scene || d.kind != RS_SPILL) continue;
    unsigned char* rows = roadStreamReadNow(fi);
    if (!rows) {
      rsSpillXZ_.clear();  // no lift: the spills are not built (reported)
      break;
    }
    const float* v = reinterpret_cast<const float*>(rows);
    float* xz = &rsSpillXZ_[(size_t)rsSpillAt_[(size_t)d.row] * 2];
    for (int k = 0; k < d.count; ++k) {
      xz[k * 2] = v[k * 5];
      xz[k * 2 + 1] = v[k * 5 + 1];
    }
    free(rows);
    if (d.x0 < rsSpillBox_.x0) rsSpillBox_.x0 = d.x0;
    if (d.z0 < rsSpillBox_.z0) rsSpillBox_.z0 = d.z0;
    if (d.x1 > rsSpillBox_.x1) rsSpillBox_.x1 = d.x1;
    if (d.z1 > rsSpillBox_.z1) rsSpillBox_.z1 = d.z1;
  }
)RSDSP";

// Sections 2-8 of the plan, from the directory.
const char* kDiskSetup = R"RSDSET(  // 2-8. TABLES ON DISK: every baked row is an item of the directory the
  //    codegen wrote (ROAD_FILE_ITEMS), its box with it, so the plan reads
  //    no row. The junction rows that overlap a spill are read here once,
  //    for the spill lift - the only use of their vertices before a build.
  for (int fi = 0; fi < ROAD_FILE_ITEM_COUNT; ++fi) {
    const RoadFileItem& d = ROAD_FILE_ITEMS[fi];
    if ((int)d.scene != scene || d.kind != RS_JUNCTION) continue;
    roadStreamAddFileItem(fi, d.count);
    if (!rsSpillXZ_.empty() && d.x0 <= rsSpillBox_.x1 && d.x1 >= rsSpillBox_.x0 &&
        d.z0 <= rsSpillBox_.z1 && d.z1 >= rsSpillBox_.z0) {
      unsigned char* rows = roadStreamReadNow(fi);
      if (rows) {
        roadStreamLiftSpills(reinterpret_cast<const float*>(rows), 5, (size_t)d.count, 0,
                             rsBoxes_.back());
        free(rows);
      }
    }
    ++rsPlanChunks_;
    rsPlanVerts_ += d.count;
    rsPlanPackages_ += (d.count + 74) / 75;
    rsPlanTriangles_ += d.count / 3;
  }
  // 3. Spills: every surface under them has now been seen.
  for (int si = 0; si < ROAD_SPILL_COUNT; ++si) {
    const RoadSpillRt& sp = ROAD_SPILLS[si];
    if (sp.scene != scene || sp.count < 1 || rsSpillXZ_.empty()) continue;
    float* ys = &rsSpillY_[(size_t)rsSpillAt_[(size_t)si]];
    for (int k = 0; k < sp.count; ++k) {
      const float* sv = &rsSpillXZ_[(size_t)(rsSpillAt_[(size_t)si] + k) * 2];
      float y = ys[k];
      if (y < -1.0e29F) y = terrainHeightAt(sv[0], sv[1]) + 0.12F;
      ys[k] = y + 0.02F + sp.lift;
    }
  }
  std::vector<float>().swap(rsSpillXZ_);
  // 4-8. Spills, soft edges, kerbs and rails, bridge structure, details and
  //    furniture: one item per directory row.
  for (int fi = 0; fi < ROAD_FILE_ITEM_COUNT; ++fi) {
    const RoadFileItem& d = ROAD_FILE_ITEMS[fi];
    if ((int)d.scene != scene || d.kind == RS_JUNCTION) continue;
    roadStreamAddFileItem(fi, d.kind == RS_KERB && !useStrips ? d.count * 3 : d.count);
    if (d.kind == RS_SPILL || d.kind == RS_EDGE) {
      ++rsPlanChunks_;
      rsPlanVerts_ += d.count;
      rsPlanPackages_ += (d.count + 74) / 75;
      rsPlanTriangles_ += d.count / 3;
    }
  }
)RSDSET";

// The streaming step, reading ahead. Its build and drop halves are the
// embedded step's; what is new is the read band and the cache.
const char* kDiskUpdate = R"RSDUPD(// One streaming step (tables on disk): finished reads join the cache; drop
// what is beyond the keep radius of every focus; then walk the items within
// radius + ROAD_STREAM_PREFETCH, nearest first - an item whose rows are not
// read yet is handed to the reader (and, inside the radius, waited for:
// the frame never blocks on it), an item inside the radius with its rows is
// built until the budget is spent (always at least one). budget < 0 =
// ROAD_STREAM_BUDGET; loadScene passes "all", and then the rows inside the
// radius are read right here, with the reader idle.
void TerrainGame::roadStreamUpdate(float fx, float fz, float f2x, float f2z, bool two,
                                   int budget) {
  if (rsItems_.empty()) return;
  if (budget < 0) budget = ROAD_STREAM_BUDGET;
  const bool drain = budget > 0x1000000;
  const float foci[4] = {fx, fz, f2x, f2z};
  const int nf = two ? 2 : 1;
  roadStreamIoCollect();
  // Nothing to do until a focus has moved a little or a build/read is pending.
  if (!rsPending_ && nf == rsLastFoci_) {
    bool moved = false;
    for (int f = 0; f < nf && !moved; ++f) {
      const float dx = foci[f * 2] - rsLastFocus_[f * 2];
      const float dz = foci[f * 2 + 1] - rsLastFocus_[f * 2 + 1];
      moved = dx * dx + dz * dz > 4.0F;
    }
    if (!moved) return;
  }
  for (int f = 0; f < 4; ++f) rsLastFocus_[f] = foci[f];
  rsLastFoci_ = nf;
  const u32 t0 = profTicks();
  ++rsUseStamp_;
  for (size_t r = 0; r < rsResident_.size();) {
    const int item = rsResident_[r];
    if (RsRing::keepItem(rsBoxes_[(size_t)item], foci, nf, ROAD_STREAM_KEEP)) {
      ++r;
      continue;
    }
    roadStreamDrop(item);
    rsResident_[r] = rsResident_.back();
    rsResident_.pop_back();
    ++rsStatDropped_;
  }
  // Rows read ahead for an item the ring has since left behind are not kept.
  for (size_t c = rsCache_.items.size(); c > 0; --c) {
    const int item = rsCache_.items[c - 1];
    if (!RsRing::keepItem(rsBoxes_[(size_t)item], foci, nf, ROAD_STREAM_KEEP))
      roadStreamFreeBytes(item);
  }
  rsRing_.wanted(rsGrid_, rsBoxes_.data(), rsOn_.data(), (int)rsItems_.size(), foci, nf,
                 ROAD_STREAM_RADIUS + ROAD_STREAM_PREFETCH, rsLoad_, rsKeys_);
  const float r2 = ROAD_STREAM_RADIUS * ROAD_STREAM_RADIUS;
  int spent = 0;
  bool waiting = false;
  for (size_t w = 0; w < rsLoad_.size(); ++w) {
    const int item = rsLoad_[w];
    const RsItem& it = rsItems_[(size_t)item];
    const bool inside = rsKeys_[w] <= r2;
    if (it.file >= 0) {
      if (rsFileState_ != 1 || rsBad_[(size_t)item] >= 3) continue;  // reported, never built
      if (rsBytes_[(size_t)item]) {
        rsCache_.touch(item, rsUseStamp_);
      } else if (drain && inside) {
        unsigned char* rows = roadStreamReadNow(it.file);
        if (!rows) {
          rsBad_[(size_t)item] = 3;
          continue;
        }
        rsBytes_[(size_t)item] = rows;
        rsCache_.add(item, ROAD_FILE_ITEMS[it.file].bytes, rsUseStamp_);
      } else {
        if (!drain && !rsQueued_[(size_t)item]) roadStreamIoRequest(item);
        if (inside) {
          waiting = true;  // its rows are on the way; the frame does not wait
          ++rsStatLate_;
        }
        continue;
      }
    }
    if (!inside) continue;
    if (spent >= budget) {
      waiting = true;
      continue;  // keep walking: the reads further out are still issued
    }
    spent += it.verts * (it.kind == RS_STRIP ? {{STRIP_WEIGHT}} : 1) + {{ITEM_COST}};
    roadStreamBuild(item);
    ++rsStatBuilt_;
  }
  // The cap: past it, the least recently wanted read-ahead goes.
  while (rsCache_.total > ROAD_STREAM_CACHE) {
    const int victim = rsCache_.oldest();
    if (victim < 0) break;
    roadStreamFreeBytes(victim);
  }
  bool busy = false;
  for (const RsIoSlot& s : rsIo_) busy = busy || s.state != 0;
  rsPending_ = waiting || busy;
  const u32 ticks = profTicks() - t0;
  int verts = 0;
  size_t indexBytes = 0;
  auto residentTotals = [&]() {
    verts = 0;
    indexBytes = 0;
    for (const int item : rsResident_) {
      verts += rsItems_[(size_t)item].verts;
      indexBytes += rsIndex_[(size_t)item].bytes();
    }
  };
  if (drain) {
    // loadScene's drain: one line, and it does not count as a frame. The
    // ROADFILE line is the synchronous read throughput (the plan's reads too).
    residentTotals();
    TYRA_LOG("ROADSTREAM load resident ", (int)rsResident_.size(), "/", (int)rsItems_.size(),
             " vertices ", verts, " index KB ", (int)(indexBytes / 1024), " ms ",
             (int)(ticks / 294912U));
    TYRA_LOG("ROADFILE load reads ", rsNowReads_, " KB ", (int)(rsNowBytes_ / 1024U), " read ms ",
             (int)(rsNowTicks_ / 294912U), " KB/s ",
             rsNowTicks_ > 0 ? (int)((double)rsNowBytes_ / 1024.0 /
                                     ((double)rsNowTicks_ / 294912000.0))
                             : 0,
             " errors ", rsReadErrors_);
    rsNowReads_ = 0;
    rsNowBytes_ = rsNowTicks_ = 0;
    rsStatBuilt_ = rsStatDropped_ = 0;
    rsStatLate_ = 0;
    return;
  }
  rsStatTicks_ += ticks;
  if (ticks > rsStatWorst_) rsStatWorst_ = ticks;
  // The streaming log: a line a few seconds apart while the ring is moving -
  // what is resident, what moved, and the worst frame's streaming cost; and
  // what the reader read, how long it took (its own wall time, off the
  // frame), how often an item inside the radius had to wait for its rows
  // (late, in item-frames) and what sits read ahead.
  if (++rsStatFrames_ >= 150 && (rsStatBuilt_ > 0 || rsStatDropped_ > 0)) {
    residentTotals();
    TYRA_LOG("ROADSTREAM resident ", (int)rsResident_.size(), "/", (int)rsItems_.size(),
             " vertices ", verts, " index KB ", (int)(indexBytes / 1024), " built ",
             rsStatBuilt_, " dropped ", rsStatDropped_, " busy frames ", rsStatFrames_,
             " total us ", (int)(rsStatTicks_ / 295U), " worst us ",
             (int)(rsStatWorst_ / 295U));
    TYRA_LOG("ROADFILE reads ", rsStatReads_, " KB ", (int)(rsStatReadBytes_ / 1024U),
             " reader ms ", (int)(rsStatReadTicks_ / 294912U), " worst us ",
             (int)(rsStatReadWorst_ / 295U), " late ", rsStatLate_, " cached KB ",
             (int)(rsCache_.total / 1024U), " errors ", rsReadErrors_);
    rsStatFrames_ = 0;
    rsStatBuilt_ = rsStatDropped_ = 0;
    rsStatTicks_ = rsStatWorst_ = 0;
    rsStatReads_ = rsStatLate_ = 0;
    rsStatReadBytes_ = rsStatReadTicks_ = rsStatReadWorst_ = 0;
  }
}

)RSDUPD";

// The reader, the file checks and the cache - appended to the runtime.
const char* kDiskImpl = R"RSDIMP(
// ---------------------------------------------------------------------------
// Tables on disk (docs/roads.md "Tables on disk"): bin/roadfile/roads.bin, its reader
// thread and the read-ahead cache.
// ---------------------------------------------------------------------------
void TerrainGame::roadStreamFileError(int state, const char* hud, const char* detail) {
  rsFileState_ = state;
  snprintf(rsFileMsg_, sizeof(rsFileMsg_), "%s", hud);
  TYRA_LOG("ROADFILE ERROR ", detail,
           " - the baked road rows (junctions, pavements, kerbs, rails, bridges, details,"
           " furniture) will NOT be built. Rebuild the project: bin/",
           ROAD_FILE_NAME, " must come from the same build as the ELF.");
}

// Opens bin/roadfile/roads.bin and checks its header against this ELF's directory,
// once per boot; starts the reader. A failure is final for the boot (and
// says so, in the log and on screen) - the strips still stream, the baked
// rows are never built. Never fatal.
bool TerrainGame::roadStreamOpenFile() {
  if (rsFileState_ == 1) return true;
  if (rsFileState_ < 0) return false;
  const std::string path = Tyra::FileUtils::fromCwd(ROAD_FILE_NAME);
  char detail[200];
  rsFd_ = open(path.c_str(), O_RDONLY);
  if (rsFd_ < 0) {
    snprintf(detail, sizeof(detail), "cannot open %s", path.c_str());
    roadStreamFileError(-1, "ROAD DATA MISSING - SEE LOG", detail);
    return false;
  }
  unsigned char h[RsFile::kHeader];
  unsigned int items = 0, payload = 0, hash = 0;
  if (!rsReadRange(rsFd_, 0, RsFile::kHeader, h)) {
    snprintf(detail, sizeof(detail), "cannot read the header of %s", path.c_str());
    roadStreamFileError(-3, "ROAD DATA UNREADABLE - SEE LOG", detail);
    close(rsFd_);
    rsFd_ = -1;
    return false;
  }
  if (!RsFile::header(h, &items, &payload, &hash) || items != (unsigned int)ROAD_FILE_ITEM_COUNT ||
      payload != ROAD_FILE_BYTES || hash != ROAD_FILE_HASH) {
    snprintf(detail, sizeof(detail),
             "%s is stale: items %u bytes %u hash %08X, this ELF wants items %d bytes %u"
             " hash %08X",
             path.c_str(), items, payload, hash, ROAD_FILE_ITEM_COUNT, ROAD_FILE_BYTES,
             ROAD_FILE_HASH);
    roadStreamFileError(-2, "ROAD DATA STALE - REBUILD - SEE LOG", detail);
    close(rsFd_);
    rsFd_ = -1;
    return false;
  }
  // The reader thread, above the game's priority (0x40) and below the audio
  // threads (0x5, 0x6): it spends its life blocked on the IOP, and wakes only
  // to hand a finished read over - so a read is never stuck behind a frame.
  ee_sema_t sema;
  sema.count = 0;
  sema.init_count = 0;
  sema.max_count = 0x10000;
  sema.attr = 0;
  sema.option = 0;
  sema.wait_threads = 0;
  rsIoSema_ = CreateSema(&sema);
  void* gp = nullptr;
  asm volatile("move %0, $gp" : "=r"(gp));
  ee_thread_t th;
  memset(&th, 0, sizeof(th));
  th.func = reinterpret_cast<void*>(&TerrainGame::roadStreamIoMain);
  th.stack = rsIoStack;
  th.stack_size = (int)sizeof(rsIoStack);
  th.gp_reg = gp;
  th.initial_priority = 0x30;
  rsIoThread_ = rsIoSema_ >= 0 ? CreateThread(&th) : -1;
  if (rsIoThread_ < 0 || StartThread(rsIoThread_, this) < 0) {
    snprintf(detail, sizeof(detail), "cannot start the road reader thread (sema %d thread %d)",
             rsIoSema_, rsIoThread_);
    roadStreamFileError(-3, "ROAD DATA UNREADABLE - SEE LOG", detail);
    return false;
  }
  rsFileState_ = 1;
  TYRA_LOG("ROADFILE open ", path.c_str(), " items ", (int)items, " KB ", (int)(payload / 1024U));
  return true;
}

// The reader thread: the oldest queued slot, one seek and its reads, the
// checksum, done. Everything else (which item, the cache, the build) is the
// frame's.
void TerrainGame::roadStreamIoMain(void* self) {
  TerrainGame* g = static_cast<TerrainGame*>(self);
  for (;;) {
    WaitSema(g->rsIoSema_);
    RsIoSlot* s = nullptr;
    for (int i = 0; i < ROAD_IO_SLOTS; ++i) {
      RsIoSlot& c = g->rsIo_[i];
      if (c.state == 1 && (!s || (int)(c.ticket - s->ticket) < 0)) s = &c;
    }
    if (!s) continue;
    s->state = 2;
    const u32 t0 = profTicks();
    const bool read = rsReadRange(g->rsFd_, s->at, s->bytes, s->buf);
    const bool good = read && RsFile::sum(s->buf, s->bytes, RsFile::kSeed) == s->sum;
    s->ticks = profTicks() - t0;
    s->state = !read ? 4 : (good ? 3 : 5);
  }
}

// A synchronous read on the frame's thread - the plan and loadScene's drain
// only, when the reader is idle (roadStreamIoIdle ran). Null on failure.
unsigned char* TerrainGame::roadStreamReadNow(int fi) {
  if (rsFileState_ != 1 || fi < 0 || fi >= ROAD_FILE_ITEM_COUNT) return nullptr;
  const RoadFileItem& d = ROAD_FILE_ITEMS[fi];
  unsigned char* buf = static_cast<unsigned char*>(memalign(64, d.bytes > 0 ? d.bytes : 64U));
  if (!buf) {
    TYRA_LOG("ROADFILE out of memory reading item ", fi, " (", (int)d.bytes, " bytes)");
    return nullptr;
  }
  const u32 t0 = profTicks();
  const bool read = rsReadRange(rsFd_, d.at, d.bytes, buf);
  const bool good = read && RsFile::sum(buf, d.bytes, RsFile::kSeed) == d.sum;
  rsNowTicks_ += profTicks() - t0;
  ++rsNowReads_;
  rsNowBytes_ += d.bytes;
  if (!good) {
    if (++rsReadErrors_ <= 8)
      TYRA_LOG("ROADFILE ERROR item ", fi, " kind ", (int)d.kind, " row ", d.row, " at ",
               (int)d.at, " bytes ", (int)d.bytes, read ? ": checksum mismatch" : ": read failed");
    snprintf(rsFileMsg_, sizeof(rsFileMsg_), "%s", "ROAD DATA READ ERROR - SEE LOG");
    free(buf);
    return nullptr;
  }
  return buf;
}

// Hands one item to the reader. False = every slot is busy (the ring asks
// again next frame) or no memory for its rows.
bool TerrainGame::roadStreamIoRequest(int item) {
  const RsItem& it = rsItems_[(size_t)item];
  if (it.file < 0 || rsFileState_ != 1) return false;
  RsIoSlot* s = nullptr;
  for (RsIoSlot& c : rsIo_)
    if (c.state == 0) {
      s = &c;
      break;
    }
  if (!s) return false;
  const RoadFileItem& d = ROAD_FILE_ITEMS[it.file];
  unsigned char* buf = static_cast<unsigned char*>(memalign(64, d.bytes > 0 ? d.bytes : 64U));
  if (!buf) return false;
  s->item = item;
  s->at = d.at;
  s->bytes = d.bytes;
  s->sum = d.sum;
  s->ticket = ++rsIoTicket_;
  s->buf = buf;
  s->state = 1;
  rsQueued_[(size_t)item] = 1;
  SignalSema(rsIoSema_);
  return true;
}

// Every finished slot: good rows join the cache (unless their item no longer
// wants them), a failure is counted, logged and shown, and retried at most
// twice (a ps2link hiccup) before the item is given up.
void TerrainGame::roadStreamIoCollect() {
  for (RsIoSlot& s : rsIo_) {
    const int state = s.state;
    if (state < 3) continue;
    const int item = s.item;
    const bool valid = item >= 0 && item < (int)rsItems_.size();
    bool kept = false;
    if (state == 3) {
      ++rsStatReads_;
      rsStatReadBytes_ += s.bytes;
      rsStatReadTicks_ += s.ticks;
      if (s.ticks > rsStatReadWorst_) rsStatReadWorst_ = s.ticks;
      if (valid && !rsOn_[(size_t)item] && !rsBytes_[(size_t)item]) {
        rsBytes_[(size_t)item] = s.buf;
        rsCache_.add(item, s.bytes, rsUseStamp_);
        kept = true;
      }
    } else {
      if (valid && rsBad_[(size_t)item] < 3) ++rsBad_[(size_t)item];
      if (++rsReadErrors_ <= 8)
        TYRA_LOG("ROADFILE ERROR item ", item, " at ", (int)s.at, " bytes ", (int)s.bytes,
                 state == 4 ? ": read failed" : ": checksum mismatch",
                 valid && rsBad_[(size_t)item] >= 3 ? " (given up)" : " (will retry)");
      snprintf(rsFileMsg_, sizeof(rsFileMsg_), "%s", "ROAD DATA READ ERROR - SEE LOG");
    }
    if (!kept) free(s.buf);
    if (valid) rsQueued_[(size_t)item] = 0;
    s.buf = nullptr;
    s.item = -1;
    s.state = 0;
  }
}

// Waits out the reader's current read and drops everything queued or read
// ahead: a new scene's items are numbered afresh.
void TerrainGame::roadStreamIoIdle() {
  for (RsIoSlot& s : rsIo_) {
    // Queued and not picked: the reader runs above this thread, so it is
    // never between "found it queued" and "marked it reading" while we look.
    if (s.state == 1) s.state = 0;
    while (s.state == 2) Tyra::Threading::sleep(1);
    if (s.buf) free(s.buf);
    s.buf = nullptr;
    s.item = -1;
    s.state = 0;
  }
  for (const int item : rsCache_.items)
    if (item >= 0 && item < (int)rsBytes_.size() && rsBytes_[(size_t)item]) {
      free(rsBytes_[(size_t)item]);
      rsBytes_[(size_t)item] = nullptr;
    }
  rsCache_.clear();
}

void TerrainGame::roadStreamFreeBytes(int item) {
  if (item < 0 || item >= (int)rsBytes_.size() || !rsBytes_[(size_t)item]) return;
  free(rsBytes_[(size_t)item]);
  rsBytes_[(size_t)item] = nullptr;
  rsCache_.remove(item);
}

void TerrainGame::roadStreamAddFileItem(int fi, int verts) {
  const RoadFileItem& d = ROAD_FILE_ITEMS[fi];
  RsItem it;
  it.kind = (unsigned char)d.kind;
  it.a = d.row;
  it.b = d.first;
  it.verts = verts;
  it.file = fi;
  rsItems_.push_back(it);
  rsBoxes_.push_back(RsBox{d.x0, d.z0, d.x1, d.z1});
}

// The loud half of a bad road file: an out-of-memory load is a black screen,
// this must not be one. Drawn every frame over the scene, in the debug font
// every build ships.
void TerrainGame::roadStreamDrawError() {
  if (rsFileState_ >= 0 && rsReadErrors_ == 0) return;
  const float h = (float)engine->renderer.core.getSettings().getHeight();
  drawHudText(engine, rsFileMsg_, 16.0F, h * 0.5F);
}
)RSDIMP";

}  // namespace

std::string coreSource() {
    std::string s(reinterpret_cast<const char*>(roadstreamsrc::kCore), roadstreamsrc::kCoreSize);
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    // Indent into the class body (cosmetic: the generated header reads as one).
    std::string out;
    out.reserve(s.size() + s.size() / 16);
    size_t at = 0;
    while (at < s.size()) {
        size_t nl = s.find('\n', at);
        if (nl == std::string::npos) nl = s.size();
        const std::string line = s.substr(at, nl - at);
        if (!line.empty()) out += "  " + line;
        out += "\n";
        at = nl + 1;
    }
    return out;
}

float suggestedRadius(float fogEnd, float terrainViewDistance) {
    const float base = std::max(fogEnd, terrainViewDistance);
    return std::max(120.0f, std::ceil((base + kCell * 0.5f) / 10.0f) * 10.0f);
}

Emitted emit(const Sources& src, const Params& prm) {
    Emitted e;
    Cutter cut{e.errors};
    const std::string& R = src.roads;

    // The tessellator's set-up lines (the strip run, the strip switch, the
    // grey every road vertex carries, `any`).
    const std::string prologue = cut.between(R, "  const unsigned int stripRun = 75u;\n",
                                             "  bool any = false;\n", true, "buildRoads prologue");
    // One road's tessellation: the body of buildRoads' per-road loop.
    std::string body = cut.between(R, "    const RoadDefRt& rd = ROAD_DEFS[ri];\n",
                                   "    // This road's last chunk still has an open run.\n"
                                   "    closeChunk();\n",
                                   true, "buildRoads per-road loop");
    body = cut.once(body,
                    "    if (rd.scene != scene || rd.pointCount < 2) continue;\n",
                    "    if (rd.pointCount < 2) return;\n", "road scene test");
    body = cut.once(body,
                    "    float arc = 0.0F, prevX = 0.0F, prevZ = 0.0F;\n"
                    "    sampleAt(0, 0.0F, &prevX, &prevZ);\n"
                    "    for (int seg = 0; seg < n - 1; ++seg) {\n",
                    "    float arc = 0.0F, prevX = 0.0F, prevZ = 0.0F;\n"
                    "    sampleAt(0, 0.0F, &prevX, &prevZ);\n"
                    "    // ROAD STREAMING: a replay resumes at the station BEFORE its chunk's\n"
                    "    // first span, with the arc state that station started from - the\n"
                    "    // state the plan pass recorded there. That station emits no span\n"
                    "    // (havePrev is still false), it only rebuilds the row the span needs.\n"
                    "    int rsSeg0 = 0, rsK0 = 0;\n"
                    "    if (item >= 0) {\n"
                    "      const RsItem& rsIt = rsItems_[(size_t)item];\n"
                    "      rsSeg0 = rsIt.b >> 16;\n"
                    "      rsK0 = rsIt.b & 0xFFFF;\n"
                    "      arc = rsIt.arc;\n"
                    "      prevX = rsIt.px;\n"
                    "      prevZ = rsIt.pz;\n"
                    "    }\n"
                    "    int rsLastSeg = 0, rsLastK = 0;\n"
                    "    float rsLastArc = 0.0F, rsLastX = prevX, rsLastZ = prevZ;\n"
                    "    for (int seg = rsSeg0; seg < n - 1; ++seg) {\n",
                    "station loop");
    body = cut.once(body,
                    "      for (int k = (seg == 0 ? 0 : 1); k <= steps; ++k) {\n",
                    "      for (int k = (seg == rsSeg0 ? rsK0 : (seg == 0 ? 0 : 1)); k <= steps; ++k) {\n"
                    "        const int rsSeg = seg, rsK = k;\n"
                    "        const float rsArc = arc, rsX = prevX, rsZ = prevZ;\n",
                    "station step");
    body = cut.once(body,
                    "            closeChunk();\n"
                    "            procChunks.push_back(ProcChunk());\n"
                    "            c = &procChunks.back();\n",
                    "            closeChunk();\n"
                    "            if (!roadStreamOpen(c, item, ri, rsLastSeg, rsLastK, rsLastArc,\n"
                    "                                rsLastX, rsLastZ))\n"
                    "              return;\n",
                    "chunk open");
    body = cut.once(body, "        px0.swap(nx);\n",
                    "        rsLastSeg = rsSeg;\n"
                    "        rsLastK = rsK;\n"
                    "        rsLastArc = rsArc;\n"
                    "        rsLastX = rsX;\n"
                    "        rsLastZ = rsZ;\n"
                    "        px0.swap(nx);\n",
                    "station end");
    body += "    if (item < 0 && c) roadStreamPlanClose();\n";

    // The row uploads' per-chunk bodies.
    const std::string junction =
        cut.between(R, "      c.owner = -3;\n      c.roadTex = tex;\n      c.roadGrip = j.grip;\n",
                    "    }\n  }\n  // SPILLS", false, "junction row upload");
    const std::string spill = cut.between(
        R, "      c.owner = -3;\n      c.roadTex = (rd.tex >= 0 && rd.tex < ROAD_TEXTURE_COUNT)\n",
        "    }\n  }\n  // SOFT EDGES", false, "spill upload");
    const std::string edge = cut.between(
        R, "      c.owner = -3;\n      c.roadTex = tex;\n      c.roadGrip = rd.grip;\n      c.roadEdge = true;\n",
        "    }\n  }\n", false, "edge upload");

    std::string setupRows, buildRows, buildBoxes;
    // Tables on disk: the plan's row loops are the directory's (kDiskSetup),
    // only the collision boxes keep their loops (setupBoxes); and a build
    // reads its row from the item's bytes, through a local of the table's
    // own name, so the cut text indexes it unchanged.
    const bool disk = prm.tablesOnDisk;
    std::string setupBoxes;
    auto rowRef = [&](const char* type, const char* var, const char* table,
                      const char* verts) {
        if (!disk) return std::string("      const ") + type + "& " + var + " = " + table + "[it.a];\n";
        return std::string("      ") + type + " " + var + " = " + table + "[it.a];\n" + "      " + var +
               ".first = 0;  // tables on disk: the bytes are this row\n" +
               "      const float* " + verts + " = reinterpret_cast<const float*>(bytes);\n";
    };
    if (!src.kerbs.empty()) {
        const std::string same = cut.between(src.kerbs, "    auto same = [](const float* a, const float* b) {\n",
                                             "    };\n", true, "kerb same()");
        const std::string fill = cut.between(src.kerbs, "      c.owner = -4;\n", "      ++kerbChunks;\n",
                                             false, "kerb upload");
        setupRows +=
            "  // 5. Kerbs and rails: one item per baked row.\n"
            "  for (int ki = 0; ki < ROAD_KERB_COUNT; ++ki) {\n"
            "    const RoadKerbRt& kr = ROAD_KERBS[ki];\n"
            "    if (kr.scene != scene || kr.count < 3) continue;\n"
            "    roadStreamAddItem(RS_KERB, ki, 0, useStrips ? kr.count : kr.count * 3,\n"
            "                      &ROAD_KERB_VERTS[(size_t)kr.first * 4], 4, 2, kr.count);\n"
            "  }\n";
        buildRows += "    case RS_KERB: {\n" +
                     rowRef("RoadKerbRt", "kr", "ROAD_KERBS", "ROAD_KERB_VERTS") +
                     "      int kerbTriangles = 0;\n" +
                     same + fill +
                     "      (void)kerbTriangles;\n"
                     "      break;\n"
                     "    }\n";
    }
    if (!src.bridges.empty()) {
        const std::string fill = cut.between(src.bridges, "      c.owner = -5;\n", "      ++bridgeChunks;\n",
                                             false, "bridge upload");
        const std::string box = cut.between(src.bridges, "      StaticBox sb;\n", "      ++bridgeBoxes;\n",
                                            false, "bridge walls");
        const char* bridgeWalls =
            "  for (int bi = 0; bi < ROAD_BRIDGE_BOX_COUNT; ++bi) {\n"
            "    const float* b = &ROAD_BRIDGE_BOXES[(size_t)bi * 11];\n"
            "    if ((int)b[0] != scene) continue;\n"
            "    const float corners[6] = {b[1], b[2], b[3], b[4], b[5], b[6]};\n"
            "    roadStreamAddItem(RS_BRIDGE_BOX, bi, 0, 0, corners, 3, 2, 2);\n"
            "  }\n";
        setupRows += std::string(
            "  // 6. Bridge structure, and its walls one box at a time.\n"
            "  for (int bi = 0; bi < ROAD_BRIDGE_COUNT; ++bi) {\n"
            "    const RoadBridgeRt& br = ROAD_BRIDGES[bi];\n"
            "    if (br.scene != scene || br.count < 3) continue;\n"
            "    roadStreamAddItem(RS_BRIDGE, bi, 0, br.count, &ROAD_BRIDGE_VERTS[(size_t)br.first * 4],\n"
            "                      4, 2, br.count);\n"
            "  }\n") + bridgeWalls;
        setupBoxes += std::string("  // Bridge walls, one box at a time.\n") + bridgeWalls;
        buildRows += "    case RS_BRIDGE: {\n" +
                     rowRef("RoadBridgeRt", "br", "ROAD_BRIDGES", "ROAD_BRIDGE_VERTS") +
                     fill +
                     "      break;\n"
                     "    }\n";
        buildBoxes += "    if (it.kind == RS_BRIDGE_BOX) {\n"
                      "      const float* b = &ROAD_BRIDGE_BOXES[(size_t)it.a * 11];\n" +
                      box +
                      "      procColliders.back().instance = tag;\n"
                      "    }\n";
    }
    if (!src.details.empty()) {
        const std::string tex = cut.between(src.details, "    Tyra::Texture* detailTex = nullptr;\n",
                                            "      detailTex = roadTextures_[ROAD_DETAIL_TEX];\n    }\n",
                                            true, "detail texture");
        const std::string fill = cut.between(src.details, "      c.owner = -6;\n", "      ++detailChunks;\n",
                                             false, "detail upload");
        setupRows +=
            "  // 7. Road details.\n"
            "  for (int di = 0; di < ROAD_DETAIL_COUNT; ++di) {\n"
            "    const RoadDetailRt& dr = ROAD_DETAILS[di];\n"
            "    if (dr.scene != scene || dr.count < 3) continue;\n"
            "    roadStreamAddItem(RS_DETAIL, di, 0, dr.count, &ROAD_DETAIL_VERTS[(size_t)dr.first * 5],\n"
            "                      5, 2, dr.count);\n"
            "  }\n";
        buildRows += "    case RS_DETAIL: {\n" +
                     rowRef("RoadDetailRt", "dr", "ROAD_DETAILS", "ROAD_DETAIL_VERTS") +
                     tex +
                     "      if (!detailTex) break;\n" + fill +
                     "      break;\n"
                     "    }\n";
    }
    if (!src.furniture.empty()) {
        const std::string k = cut.between(src.furniture, "    const float k = ", ";\n", true,
                                          "furniture colour scale");
        const std::string fill = cut.between(src.furniture, "      c.owner = -7;\n", "      ++furnChunks;\n",
                                             false, "furniture upload");
        const std::string box = cut.between(src.furniture, "      StaticBox sb;\n", "      ++furnBoxes;\n",
                                            false, "furniture boxes");
        const char* furnBoxes =
            "  for (int bi = 0; bi < ROAD_FURN_BOX_COUNT; ++bi) {\n"
            "    const float* b = &ROAD_FURN_BOXES[(size_t)bi * 7];\n"
            "    if ((int)b[0] != scene) continue;\n"
            "    roadStreamAddItem(RS_FURN_BOX, bi, 0, 0, b + 1, 3, 2, 2);\n"
            "  }\n";
        setupRows += std::string(
            "  // 8. Street furniture, and its poles and trunks one box at a time.\n"
            "  for (int fi = 0; fi < ROAD_FURN_COUNT; ++fi) {\n"
            "    const RoadFurnRt& fr = ROAD_FURN[fi];\n"
            "    if (fr.scene != scene || fr.count < 3) continue;\n"
            "    roadStreamAddItem(RS_FURN, fi, 0, fr.count, &ROAD_FURN_VERTS[(size_t)fr.first * 3], 3,\n"
            "                      2, fr.count);\n"
            "  }\n") + furnBoxes;
        setupBoxes += std::string("  // Furniture poles and trunks, one box at a time.\n") + furnBoxes;
        buildRows += "    case RS_FURN: {\n" +
                     rowRef("RoadFurnRt", "fr", "ROAD_FURN", "ROAD_FURN_VERTS") +
                     (disk ? "      const unsigned int* ROAD_FURN_RGB =\n"
                             "          reinterpret_cast<const unsigned int*>(bytes + (size_t)fr.count * 12);\n"
                           : "") +
                     k + fill +
                     "      break;\n"
                     "    }\n";
        buildBoxes += "    if (it.kind == RS_FURN_BOX) {\n"
                      "      const float* b = &ROAD_FURN_BOXES[(size_t)it.a * 7];\n" +
                      box +
                      "      procColliders.back().instance = tag;\n"
                      "    }\n";
    }

    // procFinishChunks: its info-bag set-up and its per-chunk body.
    const std::string batchInfo = cut.between(src.finish, "  if (!batchInfoBag) {\n",
                                              "  // Its Gouraud twin", false, "procFinishChunks info bag");
    const std::string finishBody =
        cut.between(src.finish, "    if (c.vertices.empty()) {\n      c.bag.reset();\n      continue;\n    }\n",
                    "      c.centre[a] = 0.5F * (c.aabbMin[a] + c.aabbMax[a]);\n", true,
                    "procFinishChunks body");

    std::string impl = kImpl;
    std::string members = kMembers;
    if (disk) {
        // Tables on disk: splice the reader into the runtime above, at anchors
        // of this file's own text (a miss is the same loud #error).
        auto region = [&](const std::string& from, const std::string& to, const std::string& text,
                          const char* what) {
            const size_t a = impl.find(from);
            const size_t b = a == std::string::npos ? a : impl.find(to, a);
            if (a == std::string::npos || b == std::string::npos ||
                impl.find(from, a + 1) != std::string::npos) {
                impl += cut.missing(what);
                return;
            }
            impl.replace(a, b - a, text);
        };
        // The plan's rows come from the directory; the spill rows are read
        // before the strips (their lift); the update reads ahead.
        region("  // 2. Junction rows (nodes, paint, bridge decks), in buildRoads' 1800 pieces.\n",
               "{{SETUP_ROWS}}", std::string(kDiskSetup) + setupBoxes, "disk: plan rows");
        setupRows.clear();
        region("// One streaming step: drop what is beyond the keep radius of every focus,\n",
               "// Is every surface item touching", kDiskUpdate, "disk: update");
        impl = cut.once(impl, "  // 1. Strip chunks: every road walked once",
                        std::string(kDiskSpillRead) + "  // 1. Strip chunks: every road walked once",
                        "disk: spill read");
        impl = cut.once(impl, "  rsPending_ = true;\n  rsLastFoci_ = 0;\n",
                        "  rsPending_ = true;\n  rsLastFoci_ = 0;\n"
                        "  roadStreamIoIdle();  // the old scene's reads and read-ahead go\n",
                        "disk: setup reset");
        impl = cut.once(impl, "  rsIndex_.resize(rsItems_.size());\n",
                        "  rsIndex_.resize(rsItems_.size());\n"
                        "  rsBytes_.assign(rsItems_.size(), nullptr);\n"
                        "  rsQueued_.assign(rsItems_.size(), 0);\n"
                        "  rsBad_.assign(rsItems_.size(), 0);\n"
                        "  rsUseStamp_ = 0;\n",
                        "disk: per-item state");
        impl = cut.once(impl, "  if (rsSpillY_.empty() || count < 3) return;\n",
                        "  if (rsSpillY_.empty() || rsSpillXZ_.empty() || count < 3) return;\n",
                        "disk: lift guard");
        impl = cut.once(impl, "      const float* sv = &ROAD_SPILL_VERTS[(size_t)(sp.first + k) * 5];\n",
                        "      const float* sv = &rsSpillXZ_[(size_t)(rsSpillAt_[(size_t)si] + k) * 2];\n",
                        "disk: lift xz");
        // A build reads its rows from the item's bytes, and frees them.
        impl = cut.once(impl, "  (void)tag;\n  if (it.kind == RS_BRIDGE_BOX",
                        "  (void)tag;\n"
                        "  // Tables on disk: a baked item's rows, read ahead by the reader.\n"
                        "  const unsigned char* bytes = it.file >= 0 ? rsBytes_[(size_t)item] : nullptr;\n"
                        "  if (it.file >= 0 && !bytes) return;  // not read yet: the ring asks again\n"
                        "  if (it.kind == RS_BRIDGE_BOX",
                        "disk: build bytes");
        impl = cut.once(impl, "  roadStreamFinish(c);\n",
                        "  roadStreamFinish(c);\n"
                        "  roadStreamFreeBytes(item);  // expanded: the rows go back to the heap\n",
                        "disk: build frees");
        impl = cut.once(impl,
                        "      const RoadJunctionRt& j = ROAD_JUNCTIONS[it.a];\n"
                        "      Tyra::Texture* tex = roadStreamTexture(j.tex);\n"
                        "      const int first = it.b;\n",
                        "      RoadJunctionRt j = ROAD_JUNCTIONS[it.a];\n"
                        "      Tyra::Texture* tex = roadStreamTexture(j.tex);\n"
                        "      const int first = it.b;\n"
                        "      // Tables on disk: the bytes are this piece; the cut below reads\n"
                        "      // ROAD_JUNCTION_VERTS[(j.first + first + k) * 5], so j.first = -first.\n"
                        "      j.first = -first;\n"
                        "      const float* ROAD_JUNCTION_VERTS = reinterpret_cast<const float*>(bytes);\n",
                        "disk: junction rows");
        impl = cut.once(impl,
                        "      const RoadSpillRt& sp = ROAD_SPILLS[it.a];\n"
                        "      const RoadDefRt& rd = ROAD_DEFS[sp.road];\n",
                        "      RoadSpillRt sp = ROAD_SPILLS[it.a];\n"
                        "      sp.first = 0;  // tables on disk: the bytes are this row\n"
                        "      const float* ROAD_SPILL_VERTS = reinterpret_cast<const float*>(bytes);\n"
                        "      const RoadDefRt& rd = ROAD_DEFS[sp.road];\n",
                        "disk: spill rows");
        impl = cut.once(impl,
                        "      const RoadSpillRt& ed = ROAD_EDGES[it.a];\n"
                        "      const RoadDefRt& rd = ROAD_DEFS[ed.road];\n"
                        "      Tyra::Texture* tex = roadStreamTexture(rd.tex);\n"
                        "      const int at = it.b;\n",
                        "      RoadSpillRt ed = ROAD_EDGES[it.a];\n"
                        "      const RoadDefRt& rd = ROAD_DEFS[ed.road];\n"
                        "      Tyra::Texture* tex = roadStreamTexture(rd.tex);\n"
                        "      const int at = it.b;\n"
                        "      ed.first = -at;  // tables on disk: the bytes are this piece\n"
                        "      const float* ROAD_EDGE_VERTS = reinterpret_cast<const float*>(bytes);\n",
                        "disk: edge rows");
        impl = cut.once(impl,
                        "  RS_BRIDGE_BOX = 8, RS_FURN_BOX = 9                                     // collision only\n};\n",
                        std::string("  RS_BRIDGE_BOX = 8, RS_FURN_BOX = 9                                     // collision only\n};\n") +
                            kDiskDefs,
                        "disk: constants");
        impl += kDiskImpl;
        members = cut.once(members,
                           "    int chunk = -1;          // its procChunks index while resident\n",
                           "    int chunk = -1;          // its procChunks index while resident\n"
                           "    int file = -1;           // tables on disk: its ROAD_FILE_ITEMS row\n",
                           "disk: item file");
        members += kDiskMembers;
        impl = replaceAll(impl, "{{PREFETCH}}", lit(kPrefetch));
        impl = replaceAll(impl, "{{CACHE}}", std::to_string(kCacheBytes));
        impl = replaceAll(impl, "{{SLOTS}}", std::to_string(kIoSlots));
        members = replaceAll(members, "{{SLOTS}}", std::to_string(kIoSlots));
    }
    impl = replaceAll(impl, "{{PROLOGUE}}", prologue);
    impl = replaceAll(impl, "{{BODY}}", body);
    impl = replaceAll(impl, "{{JUNCTION_FILL}}", junction);
    impl = replaceAll(impl, "{{SPILL_FILL}}", spill);
    impl = replaceAll(impl, "{{EDGE_FILL}}", edge);
    impl = replaceAll(impl, "{{SETUP_ROWS}}", setupRows);
    impl = replaceAll(impl, "{{BUILD_ROWS}}", buildRows);
    impl = replaceAll(impl, "{{BUILD_BOXES}}", buildBoxes);
    impl = replaceAll(impl, "{{BATCH_INFO}}", batchInfo);
    impl = replaceAll(impl, "{{FINISH_BODY}}", finishBody);
    impl = replaceAll(impl, "{{RADIUS}}", lit(prm.radius));
    impl = replaceAll(impl, "{{KEEP}}", lit(prm.radius + kHysteresis));
    impl = replaceAll(impl, "{{CELL}}", lit(kCell));
    impl = replaceAll(impl, "{{BUDGET}}", std::to_string(kBudget));
    impl = replaceAll(impl, "{{STRIP_WEIGHT}}", std::to_string(kStripWeight));
    impl = replaceAll(impl, "{{ITEM_COST}}", std::to_string(kItemCost));
    e.impl = impl;
    e.members = replaceAll(members, "{{CORE}}", coreSource());
    e.setup =
        "  // Road streaming (docs/roads.md \"Road streaming\"): plan the scene's roads,\n"
        "  // then build everything within the radius of the start focus.\n"
        "  roadStreamSetup(sceneIndex);\n"
        "  roadStreamUpdate(lsFocusX, lsFocusZ, 0.0F, 0.0F, false, 0x7FFFFFFF);\n";
    return e;
}

std::string patchTemplate(std::string s, const Params& prm) {
    // 1. roadSurfaceAt asks the resident chunks' own indexes instead of the
    //    global ROADINDEX grid (which would be rebuilt on every load/unload).
    s = replaceAll(
        s,
        "  if (roadIdxDirty || roadIdxChunks != procChunks.size())\n"
        "    buildRoadHeightIndex();\n"
        "  if (roadIdxN <= 0) return best;\n"
        "  if (x < roadIdxMinX || z < roadIdxMinZ) return best;\n"
        "  const int ix = (int)((x - roadIdxMinX) * roadIdxInv);\n"
        "  const int iz = (int)((z - roadIdxMinZ) * roadIdxInv);\n"
        "  if (ix < 0 || iz < 0 || ix >= roadIdxN || iz >= roadIdxN) return best;\n"
        "  const size_t k = (size_t)iz * (size_t)roadIdxN + (size_t)ix;\n"
        "  for (unsigned int e = roadIdxStart[k]; e < roadIdxStart[k + 1]; ++e) {\n"
        "    const unsigned int item = roadIdxItems[e];\n"
        "    const ProcChunk& c = procChunks[(size_t)(item >> 19)];\n"
        "    const size_t i = (size_t)(item & 0x7FFFFU);\n",
        "  // Road streaming (docs/roads.md \"Road streaming\"): the coarse cell names the\n"
        "  // items touching it, and each resident surface chunk answers from its own\n"
        "  // index - the ROADINDEX grid, one per chunk, built and freed with it.\n"
        "  const int rsCell = rsGrid_.cellAt(x, z);\n"
        "  if (rsCell < 0) return best;\n"
        "  if (rsChunkCount_ != procChunks.size()) roadStreamRemap();\n"
        "  for (unsigned int rsE = rsGrid_.start[(size_t)rsCell];\n"
        "       rsE < rsGrid_.start[(size_t)rsCell + 1]; ++rsE) {\n"
        "    const int rsIt = rsGrid_.items[rsE];\n"
        "    if (rsItems_[(size_t)rsIt].kind > RS_KERB || !rsOn_[(size_t)rsIt]) continue;\n"
        "    if (rsItems_[(size_t)rsIt].chunk < 0 ||\n"
        "        procChunks[(size_t)rsItems_[(size_t)rsIt].chunk].instance != -2 - rsIt)\n"
        "      roadStreamRemap();\n"
        "    const int rsChunk = rsItems_[(size_t)rsIt].chunk;\n"
        "    if (rsChunk < 0) continue;\n"
        "    const unsigned short* rsB = nullptr;\n"
        "    const unsigned short* rsEnd = nullptr;\n"
        "    if (!rsIndex_[(size_t)rsIt].cellRange(x, z, &rsB, &rsEnd)) continue;\n"
        "    const ProcChunk& c = procChunks[(size_t)rsChunk];\n"
        "    for (; rsB < rsEnd; ++rsB) {\n"
        "    const size_t i = (size_t)*rsB;\n");
    s = replaceAll(s,
                   "                 ca, cb, cc);\n"
                   "  }\n"
                   "#if TYRA_ROAD_INDEX_VERIFY\n",
                   "                 ca, cb, cc);\n"
                   "  }\n"
                   "  }\n"
                   "#if TYRA_ROAD_INDEX_VERIFY\n");
    // 2. One streaming step per frame, after the terrain's own ring and in its
    //    own profiler row, with player 2 as a second focus.
    s = replaceAll(s, "  costEnd(\"Terrain\",-1,costTerrainStart);\n",
                   "  costEnd(\"Terrain\",-1,costTerrainStart);\n"
                   "  if (!splitSecondPass) {\n"
                   "    // Road streaming (docs/roads.md \"Road streaming\").\n"
                   "    const u32 ct = costStart();\n"
                   "    const bool p2Road = playerTwoActive && players[1].objIndex >= 0;\n"
                   "    roadStreamUpdate(cameraLookAt.x, cameraLookAt.z,\n"
                   "                     p2Road ? players[1].x : 0.0F,\n"
                   "                     p2Road ? players[1].z : 0.0F, p2Road, -1);\n"
                   "    costEnd(\"Road_stream\",-1,ct);\n"
                   "  }\n");
    // Tables on disk: the reader's system headers (open/lseek/read, memalign,
    // the kernel's threads), and the on-screen error over every frame.
    if (prm.tablesOnDisk) {
        s = replaceAll(s, "#include <time.h>\n#include <algorithm>\n",
                       "#include <time.h>\n"
                       "#include <fcntl.h>   // road tables on disk (docs/roads.md)\n"
                       "#include <kernel.h>\n"
                       "#include <malloc.h>\n"
                       "#include <stdlib.h>\n"
                       "#include <unistd.h>\n"
                       "#include <algorithm>\n");
        s = replaceAll(s, "    drawDebugHud(engine, cameraPosition, cameraLookAt);\n",
                       "    roadStreamDrawError();  // a missing or stale bin/roadfile/roads.bin, loudly\n"
                       "    drawDebugHud(engine, cameraPosition, cameraLookAt);\n");
    }
    // 3. A car nobody drives does not simulate where its roads are not built:
    //    it would drop through to the terrain (or under a bridge deck) and be
    //    found there when the ring came back. It sleeps instead, in place.
    if (prm.vehicles)
        s = replaceAll(s,
                       "    if (v.sleepFrames >= 25) {\n"
                       "      Tyra::HardwareTrace::Scope trace(\"Vehicle_sleep\");\n",
                       "    if (v.sleepFrames >= 25 ||\n"
                       "        (vi != vehicleDriver_ && !roadStreamReady(v.pos[0], v.pos[2]))) {\n"
                       "      Tyra::HardwareTrace::Scope trace(\"Vehicle_sleep\");\n");
    return s;
}

// ---------------------------------------------------------------------------
// --vehicle-check "road streaming"
// ---------------------------------------------------------------------------
namespace {

// A road chunk set built by the HOST twin of the tessellator: the strips the
// console's buildRoads makes (vertex for vertex, verify-road-twins.py), with
// buildRoads' chunk cut.
struct HostChunk {
    std::vector<float> v;  // x, y, z, w
    int stripRun = 0;
};

std::vector<HostChunk> hostRoadChunks(const std::vector<float>& points, float width,
                                      const roadgen::HeightFn& h, float y0) {
    std::vector<roadgen::Vertex> strip;
    std::vector<int> sizes;
    roadgen::tessellateStrips(points, width, h, strip, &sizes, 1.0f);
    std::vector<HostChunk> out;
    size_t at = 0;
    for (int n : sizes) {
        HostChunk c;
        c.stripRun = roadgen::kStripRun;
        for (int k = 0; k < n; ++k) {
            const roadgen::Vertex& v = strip[at + (size_t)k];
            c.v.insert(c.v.end(), {v.x, v.y + y0, v.z, 1.0f});
        }
        at += (size_t)n;
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- road streaming --\n");
    // A small district: a grid of streets on rolling ground, plus a couple of
    // flat triangle-list "junction" patches - the surface chunks a streamed
    // scene's height index has to answer for.
    const roadgen::HeightFn ground = [](float x, float z) {
        return 0.6f * std::sin(x * 0.05f) + 0.4f * std::cos(z * 0.07f);
    };
    std::vector<HostChunk> chunks;
    for (int i = 0; i < 4; ++i) {
        const float c = -150.0f + 100.0f * (float)i;
        for (auto& ch : hostRoadChunks({c, -200.0f, c + 5.0f, 0.0f, c, 200.0f}, 10.0f, ground, 0.0f))
            chunks.push_back(std::move(ch));
        for (auto& ch : hostRoadChunks({-200.0f, c, 0.0f, c + 4.0f, 200.0f, c}, 8.0f, ground, 0.02f))
            chunks.push_back(std::move(ch));
    }
    for (int p = 0; p < 3; ++p) {  // list patches over the crossings
        HostChunk c;
        const float cx = -150.0f + 100.0f * (float)p, cz = -50.0f;
        const float q[4][2] = {{cx - 7, cz - 7}, {cx + 7, cz - 7}, {cx + 7, cz + 7}, {cx - 7, cz + 7}};
        const int tri[6] = {0, 1, 2, 0, 2, 3};
        for (int t : tri)
            c.v.insert(c.v.end(), {q[t][0], ground(q[t][0], q[t][1]) + 0.3f, q[t][1], 1.0f});
        chunks.push_back(std::move(c));
    }
    const int n = (int)chunks.size();
    std::vector<RsBox> boxes((size_t)n);
    size_t totalVerts = 0;
    for (int i = 0; i < n; ++i) {
        const auto& v = chunks[(size_t)i].v;
        RsBox b{v[0], v[2], v[0], v[2]};
        for (size_t k = 0; k < v.size(); k += 4) {
            b.x0 = std::min(b.x0, v[k]);
            b.x1 = std::max(b.x1, v[k]);
            b.z0 = std::min(b.z0, v[k + 2]);
            b.z1 = std::max(b.z1, v[k + 2]);
        }
        boxes[(size_t)i] = b;
        totalVerts += v.size() / 4;
    }

    // 1. The cut: every chunk has exactly one HOME cell (the cell of its box
    //    centre), so every road vertex belongs to exactly one streamed unit;
    //    the grid lists each chunk in every cell its box touches and nowhere
    //    else; and the union of the units is the whole build.
    RsGrid grid;
    grid.build(boxes.data(), n, kCell);
    {
        std::vector<int> homes((size_t)n, 0), listed((size_t)n, 0);
        bool exact = true;
        for (int iz = 0; iz < grid.nz; ++iz)
            for (int ix = 0; ix < grid.nx; ++ix) {
                const size_t k = (size_t)iz * (size_t)grid.nx + (size_t)ix;
                const float cx0 = grid.x0 + (float)ix * grid.cell, cz0 = grid.z0 + (float)iz * grid.cell;
                for (int i = 0; i < n; ++i) {
                    const RsBox& b = boxes[(size_t)i];
                    const bool touches = grid.cellX(b.x0) <= ix && ix <= grid.cellX(b.x1) &&
                                         grid.cellZ(b.z0) <= iz && iz <= grid.cellZ(b.z1);
                    bool inList = false;
                    for (unsigned int e = grid.start[k]; e < grid.start[k + 1]; ++e)
                        inList = inList || grid.items[e] == i;
                    if (touches != inList) exact = false;
                    if (inList) ++listed[(size_t)i];
                    const float mx = 0.5f * (b.x0 + b.x1), mz = 0.5f * (b.z0 + b.z1);
                    if (mx >= cx0 && mx < cx0 + grid.cell && mz >= cz0 && mz < cz0 + grid.cell)
                        ++homes[(size_t)i];
                }
            }
        bool oneHome = true, everyListed = true;
        size_t unionVerts = 0;
        for (int i = 0; i < n; ++i) {
            oneHome = oneHome && homes[(size_t)i] == 1;
            everyListed = everyListed && listed[(size_t)i] >= 1;
            unionVerts += chunks[(size_t)i].v.size() / 4;
        }
        verdict(oneHome, "every streamed chunk has exactly one home cell");
        verdict(exact && everyListed, "the cell grid lists a chunk in exactly the cells its box touches");
        verdict(unionVerts == totalVerts, "the streamed units add up to the whole build");
        std::printf("  %d chunks, %zu vertices, grid %dx%d of %.0f units\n", n, totalVerts, grid.nx,
                    grid.nz, (double)grid.cell);
    }

    // 2. The height index. Reference: roadSurfaceAt's walk over EVERY
    //    resident triangle (what TYRA_ROAD_INDEX_VERIFY's scan oracle does).
    //    Streamed: the coarse cell, then each resident chunk's own index.
    std::vector<RsLocalIndex> index((size_t)n);
    std::vector<unsigned char> on((size_t)n, 0);
    auto scan = [&](float x, float z) {
        float best = -1.0e30f;
        for (int i = 0; i < n; ++i) {
            if (!on[(size_t)i]) continue;
            const HostChunk& c = chunks[(size_t)i];
            best = std::max(best, RsTri::highest(c.v.data(), 4, c.v.size() / 4, c.stripRun, x, z));
        }
        return best;
    };
    auto streamed = [&](float x, float z) {
        float best = -1.0e30f;
        const int cell = grid.cellAt(x, z);
        if (cell < 0) return best;
        for (unsigned int e = grid.start[(size_t)cell]; e < grid.start[(size_t)cell + 1]; ++e) {
            const int it = grid.items[e];
            if (!on[(size_t)it]) continue;
            const unsigned short *b = nullptr, *en = nullptr;
            if (!index[(size_t)it].cellRange(x, z, &b, &en)) continue;
            const float* v = chunks[(size_t)it].v.data();
            for (; b < en; ++b) {
                const size_t i = *b;
                best = std::max(best, RsTri::heightAt(v + (i - 2) * 4, v + (i - 1) * 4, v + i * 4, x, z));
            }
        }
        return best;
    };
    auto load = [&](int i) {
        if (on[(size_t)i]) return;
        on[(size_t)i] = 1;
        index[(size_t)i].build(chunks[(size_t)i].v.data(), chunks[(size_t)i].v.size() / 4,
                               chunks[(size_t)i].stripRun);
    };
    auto drop = [&](int i) {
        on[(size_t)i] = 0;
        index[(size_t)i] = RsLocalIndex();
    };
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> coord(-210.0f, 210.0f);
    auto compare = [&](int samples, int* onRoad) {
        int bad = 0;
        for (int s = 0; s < samples; ++s) {
            const float x = coord(rng), z = coord(rng);
            const float a = scan(x, z), b = streamed(x, z);
            if (a > -1.0e29f) ++*onRoad;
            if (a != b) ++bad;
        }
        // And ON the roads, where an off-road sample agrees trivially.
        for (int i = 0; i < n; ++i) {
            const auto& v = chunks[(size_t)i].v;
            for (size_t k = 0; k + 8 < v.size(); k += 28) {
                const float x = (v[k] + v[k + 4] + v[k + 8]) / 3.0f;
                const float z = (v[k + 2] + v[k + 6] + v[k + 10]) / 3.0f;
                const float a = scan(x, z), b = streamed(x, z);
                if (a > -1.0e29f) ++*onRoad;
                if (a != b) ++bad;
            }
        }
        return bad;
    };
    for (int i = 0; i < n; ++i) load(i);
    int onRoad = 0;
    const int badAll = compare(4000, &onRoad);
    verdict(badAll == 0 && onRoad > 1000, "streamed height index = full scan, everything resident");
    // A load/unload sequence: the ring walks across the district, chunks come
    // and go, and after every step the per-chunk indexes must answer exactly
    // what a full rebuild (every resident chunk re-indexed from scratch) and
    // the scan answer.
    RsRing ring;
    std::vector<int> want;
    std::vector<float> keys;
    int badSeq = 0, steps = 0, loads = 0, drops = 0;
    bool hysteresisHolds = true, nearestFirst = true;
    const float radius = 90.0f, keep = radius + kHysteresis;
    for (int i = 0; i < n; ++i) drop(i);
    for (float t = 0.0f; t <= 1.0f; t += 0.05f, ++steps) {
        const float foci[2] = {-190.0f + 380.0f * t, -120.0f + 200.0f * t * t};
        for (int i = 0; i < n; ++i)
            if (on[(size_t)i] && !RsRing::keepItem(boxes[(size_t)i], foci, 1, keep)) {
                drop(i);
                ++drops;
            }
        ring.wanted(grid, boxes.data(), on.data(), n, foci, 1, radius, want, keys);
        for (size_t w = 1; w < keys.size(); ++w) nearestFirst = nearestFirst && keys[w - 1] <= keys[w];
        for (int i : want) {
            load(i);
            ++loads;
        }
        for (int i = 0; i < n; ++i) {
            const float d2 = boxes[(size_t)i].dist2(foci[0], foci[1]);
            if (d2 <= radius * radius && !on[(size_t)i]) hysteresisHolds = false;  // missing
            if (d2 > keep * keep && on[(size_t)i]) hysteresisHolds = false;       // kept too long
        }
        int dummy = 0;
        badSeq += compare(300, &dummy);
        // The full rebuild: every resident chunk's index from scratch.
        std::vector<RsLocalIndex> rebuilt((size_t)n);
        for (int i = 0; i < n; ++i)
            if (on[(size_t)i])
                rebuilt[(size_t)i].build(chunks[(size_t)i].v.data(), chunks[(size_t)i].v.size() / 4,
                                         chunks[(size_t)i].stripRun);
        for (int i = 0; i < n; ++i)
            if (rebuilt[(size_t)i].start != index[(size_t)i].start ||
                rebuilt[(size_t)i].items != index[(size_t)i].items)
                ++badSeq;
    }
    verdict(badSeq == 0, "height index after a load/unload sequence = a full rebuild = the scan");
    verdict(hysteresisHolds, "the ring keeps everything within the radius and nothing past the keep band");
    verdict(nearestFirst, "the ring builds nearest first");
    verdict(loads > 0 && drops > 0, "the walk both loads and drops chunks");
    std::printf("  index: %d samples on road with everything resident; %d steps, %d loads, %d drops\n",
                onRoad, steps, loads, drops);

    // 3. The spill lift sees roadSurfaceAt's arithmetic: a point on a list
    //    patch and on a strip agrees with the scan, and a point off every
    //    triangle is "no surface".
    {
        for (int i = 0; i < n; ++i) load(i);
        const HostChunk& patch = chunks.back();
        const float y = RsTri::highest(patch.v.data(), 4, patch.v.size() / 4, 0, 50.0f, -50.0f);
        verdict(y > -1.0e29f && std::fabs(y - scan(50.0f, -50.0f)) < 1e-6f,
                "the spill lift reads the highest surface, as roadSurfaceAt does");
        verdict(RsTri::highest(patch.v.data(), 4, patch.v.size() / 4, 0, 500.0f, 500.0f) < -1.0e29f,
                "the spill lift finds nothing off the roads");
    }

    // 4. The codegen: a streaming project generates every piece, every
    //    anchor still matches, and a project with streaming off generates no
    //    streaming code at all.
    {
        // A scratch directory: the build seeds files (the road paint) into the
        // project it generates.
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "tyrax-roadstream-check";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Project p;
        p.name = "rscheck";
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
            o.roadDetails = kerb ? 0.5f : 0.0f;
            o.roadTexture = "res/materials/roads/road-2lane.mtl";
            o.roadIntersectionTexture = "res/materials/roads/road-junction.mtl";
            sc.objects.push_back(o);
            return &sc.objects.back();
        };
        road("a", {-60, 0, 0, 0, 60, 0}, true)->roadFurniture.lamps.spacing = 15.0f;
        road("b", {0, -60, 0, 0, 0, 60}, false);
        SceneObject* bridge = road("c", {-60, 80, 0, 80, 60, 80}, false);
        bridge->roadBridge = true;
        bridge->roadHeights = {0.0f, 6.0f, 0.0f};
        auto gen = [&](float radius) {
            p.settings.roadStreamRadius = radius;
            std::string all;
            for (const auto& f : templates::generate(p)) all += f.content;
            return all;
        };
        const std::string on = gen(150.0f);
        verdict(on.find("roadStreamSetup(sceneIndex)") != std::string::npos &&
                    on.find("void TerrainGame::roadStreamTessellate") != std::string::npos &&
                    on.find("rsGrid_.cellAt(x, z)") != std::string::npos &&
                    on.find("roadStreamUpdate(cameraLookAt.x") != std::string::npos &&
                    on.find("case RS_KERB:") != std::string::npos &&
                    on.find("case RS_DETAIL:") != std::string::npos &&
                    on.find("case RS_BRIDGE:") != std::string::npos &&
                    on.find("case RS_FURN:") != std::string::npos,
                "a streaming project generates the streaming runtime and its three hooks");
        verdict(on.find("road streaming: template anchor missing") == std::string::npos,
                "every streaming anchor still matches the non-streaming source");
        const std::string off = gen(0.0f);
        verdict(off.find("roadStream") == std::string::npos && off.find("RsGrid") == std::string::npos,
                "streaming off generates no streaming code");
        std::filesystem::remove_all(dir, ec);
    }
}

}  // namespace roadstream
