// Breakable street furniture (docs/roads.md "Breakable furniture"). See
// roadfurnbreak.hpp for the contract: the generated runtime as a template
// patch, the host twins of its two rules, and --vehicle-check "breakable
// furniture".
#include "roadfurnbreak.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

#include "json.hpp"
#include "project.hpp"  // the --vehicle-check codegen block only
#include "roadfurniture.hpp"
#include "roadgen.hpp"
#include "roadlight.hpp"
#include "templates.hpp"
#include "vehiclesim.hpp"

namespace roadfurnbreak {
namespace {

std::string replaceOnce(std::string s, const std::string& from, const std::string& to,
                        std::vector<std::string>& misses, const char* what) {
    const size_t at = s.find(from);
    if (at == std::string::npos) {
        misses.push_back(what);
        return s;
    }
    s.replace(at, from.size(), to);
    return s;
}

// Every occurrence (the FPP and orbit headers each hold one class; a patch
// that finds none is a miss).
std::string replaceEvery(std::string s, const std::string& from, const std::string& to,
                         std::vector<std::string>& misses, const char* what) {
    size_t at = 0;
    int n = 0;
    while ((at = s.find(from, at)) != std::string::npos) {
        s.replace(at, from.size(), to);
        at += to.size();
        ++n;
    }
    if (n == 0) misses.push_back(what);
    return s;
}

// Class members, after `std::vector<StaticBox> procColliders;`.
const char* const kMembers = R"FBRK(  // --- breakable street furniture (docs/roads.md "Breakable furniture") ---
  // One flag per ROAD_FURN_PIECES row (= per furniture box), cleared on a
  // scene load: a prop that was knocked over stays down while its cell
  // streams out and back in, and stands again on the next load.
  std::vector<unsigned char> furnBroken_;
  std::vector<unsigned char> furnLampDark_;  // per ROAD_LAMPS row (lit lamps)
  int furnBrokenCount_ = 0;
  int furnBreaks_ = 0;
  void furnBreakReset();
  bool furnBreakable(int piece, float spd) const;
  bool furnBrokenAt(int piece) const;
  // A broken piece's box keeps its slot (the sub-step gather cache and the
  // streaming tags hold indices into procColliders) and moves out of reach.
  static void furnBoxInert(StaticBox& b) {
    b.mn[0] = b.mx[0] = 1.0e9F;
    b.mn[2] = b.mx[2] = 1.0e9F;
    b.mn[1] = b.mx[1] = -1.0e9F;
    b.lhx = b.lhz = 0.0F;
  }
  ProcChunk* furnChunkOf(int row);
  void furnCollapse(ProcChunk& c, int first, int count);
  void furnBreakApplyChunk(ProcChunk& c);
  void furnBreakContacts(int vi, const int* pieces, int n, float prevX, float prevZ);
  void furnBreak(int piece, int vi, float spd);
)FBRK";

// The functions, before renderRoadChunks.
const char* const kImpl = R"FBRK(// --- Breakable street furniture (docs/roads.md "Breakable furniture") -----
// A car at or above a piece's threshold does not take its box as a wall
// (considerProc sets it aside); after the wall pass, a set-aside box the car
// body touches BREAKS: the box goes inert, the piece's vertices in its merged
// chunk collapse to one point (one write of its own range - nothing is
// rebuilt), a copy of them flies off as a vehicle debris piece, the car keeps
// ROAD_FURN_PIECES[].keep of its speed, dust and sparks puff out of the car's
// smoke pool and the hit sound plays. A lamp's pool and halo and a signal's
// lit lens go out with it.
static const int kFurnDebrisReserve = 192;   // vertices per debris slot (a lamp is 162)
static const int kFurnDebrisMaxVerts = 1800; // bigger props vanish in dust only

void TerrainGame::furnBreakReset() {
  furnBroken_.assign(ROAD_FURN_PIECE_COUNT > 0 ? (size_t)ROAD_FURN_PIECE_COUNT : (size_t)1, 0);
// {{FURN_LAMP_RESET}}
  furnBrokenCount_ = 0;
  // The debris slots keep this capacity: a hit copies into them and does not
  // allocate (a car's lost panel re-creates its slot, so it may grow again).
  for (VehDebris& d : vehDebris_) {
    d.local.reserve((size_t)kFurnDebrisReserve);
    d.cols.reserve((size_t)kFurnDebrisReserve);
    d.sts.reserve((size_t)kFurnDebrisReserve);
  }
  bool untextured = false;
  for (auto& b : vehDebrisBatches_) untextured = untextured || b->tex == nullptr;
  if (!untextured) vehDebrisBatches_.push_back(std::make_unique<VehDebrisBatch>());
}

bool TerrainGame::furnBreakable(int piece, float spd) const {
  if (piece < 0 || piece >= ROAD_FURN_PIECE_COUNT || (size_t)piece >= furnBroken_.size())
    return false;
  const float t = ROAD_FURN_PIECES[piece].speed;
  return t > 0.0F && spd >= t && !furnBroken_[(size_t)piece];
}

bool TerrainGame::furnBrokenAt(int piece) const {
  return piece >= 0 && (size_t)piece < furnBroken_.size() && furnBroken_[(size_t)piece];
}

TerrainGame::ProcChunk* TerrainGame::furnChunkOf(int row) {
  if (row < 0) return nullptr;
  for (ProcChunk& c : procChunks)
    if (c.owner == -7 && c.furnRow == row) return &c;
  return nullptr;  // streamed out: its build collapses it (furnBreakApplyChunk)
}

// One piece's run of a merged chunk, every vertex moved onto its first: the
// triangles have no area, the GS draws nothing, the chunk is not rebuilt.
void TerrainGame::furnCollapse(ProcChunk& c, int first, int count) {
  if (first < 0 || count <= 1 || (size_t)(first + count) > c.vertices.size()) return;
  auto P = c.vertices.span((size_t)first, (size_t)count);
  const Tyra::Vec4 a = P[0];
  for (int k = 1; k < count; ++k) P[(size_t)k] = a;
  if (c.bag) c.bag->bboxVersion = ++g_bboxStamp;
}

// A furniture chunk (or a lamp pool chunk) the road stream just built: the
// pieces broken before it streamed out stay broken.
void TerrainGame::furnBreakApplyChunk(ProcChunk& c) {
  if (furnBrokenCount_ <= 0 || c.furnRow < 0) return;
  int n = 0;
  for (int i = 0; i < ROAD_FURN_PIECE_COUNT && (size_t)i < furnBroken_.size(); ++i) {
    if (!furnBroken_[(size_t)i]) continue;
    const RoadFurnPieceRt& p = ROAD_FURN_PIECES[i];
    if (p.row == c.furnRow) furnCollapse(c, p.first, p.count), ++n;
    if (p.poolRow == c.furnRow) furnCollapse(c, p.poolFirst, p.poolCount), ++n;
  }
  if (n > 0) TYRA_LOG("FURN restreamed row ", c.furnRow, " kept ", n, " broken piece(s) down");
}

// After the wall pass: the set-aside boxes the car body touches now, where
// it was, or half way (the host twin is roadfurnbreak::touches).
void TerrainGame::furnBreakContacts(int vi, const int* pieces, int n, float prevX,
                                    float prevZ) {
  VehicleRt& v = vehicles_[vi];
  const VehicleDefData& s = VEHICLE_DEFS[v.def];
  const float SC = v.scale;
  const float hx = 0.5F * s.track * SC;
  const float hz = 0.5F * s.wheelBase * SC + (s.bodyOverhang > 0.0F ? s.bodyOverhang * SC : 0.0F);
  const float kDeg = 3.14159265F / 180.0F;
  const float c = cosf(v.yaw * kDeg), sn = sinf(v.yaw * kDeg);
  // The speed the car arrived with (before this frame's collisions).
  const float spd = sqrtf(v.dmgPreV[0] * v.dmgPreV[0] + v.dmgPreV[1] * v.dmgPreV[1]);
  for (int k = 0; k < n; ++k) {
    const int piece = pieces[k];
    if (furnBrokenAt(piece) || piece < 0 || piece >= ROAD_FURN_BOX_COUNT) continue;
    const float* b = &ROAD_FURN_BOXES[(size_t)piece * 7];
    const float bx = 0.5F * (b[1] + b[4]), bz = 0.5F * (b[3] + b[6]);
    const float ex = 0.5F * (b[4] - b[1]), ez = 0.5F * (b[6] - b[3]);
    const float r = ex > ez ? ex : ez;
    bool hit = false;
    for (int q = 0; q < 3 && !hit; ++q) {
      const float f = q == 0 ? 1.0F : (q == 1 ? 0.5F : 0.0F);
      const float px = prevX + (v.pos[0] - prevX) * f, pz = prevZ + (v.pos[2] - prevZ) * f;
      const float dx = bx - px, dz = bz - pz;
      const float lx = dx * c - dz * sn;
      const float lz = dx * sn + dz * c;
      hit = (lx < 0.0F ? -lx : lx) < hx + r && (lz < 0.0F ? -lz : lz) < hz + r;
    }
    if (hit) furnBreak(piece, vi, spd);
  }
}

void TerrainGame::furnBreak(int piece, int vi, float spd) {
  if (piece < 0 || piece >= ROAD_FURN_PIECE_COUNT || furnBrokenAt(piece)) return;
  const u32 t0 = profTicks();
  furnBroken_[(size_t)piece] = 1;
  ++furnBrokenCount_;
  ++furnBreaks_;
  const RoadFurnPieceRt& p = ROAD_FURN_PIECES[piece];
  VehicleRt& v = vehicles_[vi];
  // 1. Its box: inert, in place (indices into procColliders stay valid).
  for (StaticBox& sb : procColliders)
    if (sb.owner == -7 && sb.furn == piece) furnBoxInert(sb);
  const float* bx = &ROAD_FURN_BOXES[(size_t)piece * 7];
  const float cx = 0.5F * (bx[1] + bx[4]), cz = 0.5F * (bx[3] + bx[6]);
  // The way the car was going (the box's way when it was standing still).
  float dx = v.dmgPreV[0], dz = v.dmgPreV[1];
  float dl = sqrtf(dx * dx + dz * dz);
  if (dl < 1e-3F) dx = cx - v.pos[0], dz = cz - v.pos[2], dl = sqrtf(dx * dx + dz * dz);
  if (dl < 1e-4F) dx = 0.0F, dz = 1.0F, dl = 1.0F;
  dx /= dl, dz /= dl;
  // 2. Its vertices: a debris copy, then the merged chunk's range collapsed.
  int debris = 0;
  if (ProcChunk* ch = furnChunkOf(p.row)) {
    if (p.count >= 3 && p.count <= kFurnDebrisMaxVerts &&
        (size_t)(p.first + p.count) <= ch->vertices.size()) {
      VehDebris& d = vehDebris_[vehDebrisNext_];
      vehDebrisNext_ = (vehDebrisNext_ + 1) % kVehDebrisMax;
      if (d.active)  // the oldest piece gives its slot up
        for (auto& bp : vehDebrisBatches_)
          if (bp->tex == d.tex) bp->dirty = 1;
      d.active = 1;
      d.rest = 0;
      d.tex = nullptr;
      d.local.clear();
      d.cols.clear();
      d.sts.clear();
      const BagArray<Tyra::Vec4>& cv = ch->vertices;
      const BagArray<Tyra::Color>& cc = ch->colors;
      float mn[3] = {1e30F, 1e30F, 1e30F}, mx[3] = {-1e30F, -1e30F, -1e30F};
      for (int k = 0; k < p.count; ++k) {
        const Tyra::Vec4& q = cv[(size_t)(p.first + k)];
        const float qq[3] = {q.x, q.y, q.z};
        for (int a = 0; a < 3; ++a) {
          if (qq[a] < mn[a]) mn[a] = qq[a];
          if (qq[a] > mx[a]) mx[a] = qq[a];
        }
      }
      float wc[3];
      for (int a = 0; a < 3; ++a) wc[a] = 0.5F * (mn[a] + mx[a]);
      for (int k = 0; k < p.count; ++k) {
        const size_t at = (size_t)(p.first + k);
        const Tyra::Vec4& q = cv[at];
        d.local.push_back(Tyra::Vec4(q.x - wc[0], q.y - wc[1], q.z - wc[2], 1.0F));
        d.cols.push_back(at < cc.size() ? cc[at] : Tyra::Color(128.0F, 128.0F, 128.0F, 128.0F));
        d.sts.push_back(Tyra::Vec4(0.0F, 0.0F, 1.0F, 0.0F));
      }
      for (int a = 0; a < 3; ++a) {
        d.pos[a] = wc[a];
        d.vel[a] = 0.0F;
        d.spin[a] = 0.0F;
      }
      for (int a = 0; a < 9; ++a) d.rot[a] = (a % 4 == 0) ? 1.0F : 0.0F;
      d.low = wc[1] - mn[1];
      d.thin = 0;
      for (int a = 1; a < 3; ++a)
        if (mx[a] - mn[a] < mx[d.thin] - mn[d.thin]) d.thin = a;
      // Thrown along the car's way, up a little, tipping over forward (the
      // axis up x forward) - the debris pass kicks it on out of the car and
      // lays it down flat.
      d.vel[0] = dx * (0.55F * spd + 1.0F);
      d.vel[1] = 2.5F + 0.12F * spd;
      d.vel[2] = dz * (0.55F * spd + 1.0F);
      const float tip = 2.5F + 0.18F * spd;
      d.spin[0] = dz * tip;
      d.spin[1] = (piece & 1) ? 0.7F : -0.7F;
      d.spin[2] = -dx * tip;
      for (auto& bp : vehDebrisBatches_)
        if (bp->tex == nullptr) bp->dirty = 1;
      debris = p.count;
    }
    furnCollapse(*ch, p.first, p.count);
  }
// {{FURN_LAMP_OFF}}
// {{FURN_SIGNAL_OFF}}
  // 3. The car keeps going, a little slower.
  v.speed *= p.keep;
  v.lateral *= p.keep;
  // 4. Dust at the foot of the pole, sparks off metal (not a tree), from the
  //    car's own smoke pool - every vehicle project has one.
  if (VehFx* fx = vehFxFor(v.def)) {
    const float edx = cx - cameraPosition.x, edz = cz - cameraPosition.z;
    if (edx * edx + edz * edz < 70.0F * 70.0F) {
      const float y0 = bx[2] + 0.3F;
      for (int b = 0; b < 8; ++b) {
        const int sl = fx->takeSmoke();
        const float a = (float)b * 2.39996F;
        fx->smokePos[sl].set(cx, y0, cz, 1.0F);
        fx->smokeVel[sl].set(cosf(a) * 1.4F + dx * 0.15F * spd, 0.7F + 0.1F * (float)(b % 3),
                             sinf(a) * 1.4F + dz * 0.15F * spd, 0.0F);
        fx->smokeMaxLife[sl] = 0.8F + 0.05F * (float)(b % 4);
        fx->smokeLife[sl] = fx->smokeMaxLife[sl];
        fx->smokeShade[sl] = 1.05F;
        fx->smokeScale[sl] = 1.3F;
      }
      if (p.kind != 1)
        for (int b = 0; b < 10; ++b) {
          const int sl = fx->takeSmoke();
          const float a = (float)b * 2.39996F;
          fx->smokePos[sl].set(cx, y0 + 0.5F, cz, 1.0F);
          fx->smokeVel[sl].set(cosf(a) * 3.0F + dx * 0.3F * spd, 2.0F + 0.4F * (float)(b % 3),
                               sinf(a) * 3.0F + dz * 0.3F * spd, 0.0F);
          fx->smokeMaxLife[sl] = 0.22F + 0.03F * (float)(b % 4);
          fx->smokeLife[sl] = fx->smokeMaxLife[sl];
          fx->smokeShade[sl] = 1.9F;
          fx->smokeScale[sl] = 0.3F;
        }
    }
  }
  // 5. The hit, on the drive's reserved one-shot voice (base+20, the gear
  //    shift's - a crash outranks a shift blip), quieter with distance.
  if (p.snd >= 0 && p.snd < (int)sndSamples.size() && sndSamples[p.snd]) {
    const float ddx = cx - cameraPosition.x, ddz = cz - cameraPosition.z;
    const float far = sqrtf(ddx * ddx + ddz * ddz);
    const float att = far < 60.0F ? 1.0F - far / 60.0F : 0.0F;
    if (att > 0.02F) {
      const s8 ch = (s8)(scriptCtx.reverbBusBase + 20);
      engine->audio.adpcm.setVolume((u8)(90.0F * att * (float)scriptCtx.sfxVolume / 100.0F), ch);
      engine->audio.adpcm.forcePlay(sndSamples[p.snd], ch);
    }
  }
  // 6. The player's car scores it (the On Prop Broken flow node).
  if (vi == vehicleDriver_) {
    ++scriptCtx.propBreaks;
    scriptCtx.propBreakKind = p.kind;
    scriptCtx.propBreakSpeed = spd;
  }
  static const char* const kKind[5] = {"lamp", "tree", "bollard", "sign", "signal"};
  TYRA_LOG("FURN break kind ", kKind[p.kind >= 0 && p.kind < 5 ? p.kind : 0], " speed ",
           (int)(spd * 10.0F) / 10.0F, " piece ", piece, " car ", vi, " keep ",
           (int)(p.keep * 100.0F + 0.5F), "% debris ", debris, " lamp ", p.lamp, " pool ",
           p.poolCount, " broken ", furnBrokenCount_, " us ", (int)((profTicks() - t0) / 295U));
}

)FBRK";

}  // namespace

bool breaks(float speed, float threshold) {
    const float s = speed < 0.0f ? -speed : speed;
    return threshold > 0.0f && s >= threshold;
}

bool touches(float px, float pz, float yawDeg, float hx, float hz, float bx, float bz, float r) {
    const float kDeg = 3.14159265f / 180.0f;
    const float c = std::cos(yawDeg * kDeg), sn = std::sin(yawDeg * kDeg);
    const float dx = bx - px, dz = bz - pz;
    const float lx = dx * c - dz * sn;
    const float lz = dx * sn + dz * c;
    return std::fabs(lx) < hx + r && std::fabs(lz) < hz + r;
}

std::string patchTemplate(std::string s, const Gates& g) {
    std::vector<std::string> misses;
    // 1. The chunk knows its ROAD_FURN row, the box its piece; the class
    //    gains the members.
    s = replaceEvery(s, "  };\n  std::vector<ProcChunk> procChunks;\n",
                     "    // Breakable furniture (docs/roads.md): this chunk's ROAD_FURN row.\n"
                     "    int furnRow = -1;\n"
                     "  };\n  std::vector<ProcChunk> procChunks;\n",
                     misses, "ProcChunk");
    s = replaceEvery(s,
                     "    float lhx = 0.0F, lhz = 0.0F, yc = 1.0F, ys = 0.0F;\n  };\n"
                     "  std::vector<StaticBox> procColliders;\n",
                     "    float lhx = 0.0F, lhz = 0.0F, yc = 1.0F, ys = 0.0F;\n"
                     "    // Breakable furniture (docs/roads.md): this box's ROAD_FURN_PIECES row.\n"
                     "    int furn = -1;\n"
                     "  };\n  std::vector<StaticBox> procColliders;\n" +
                         std::string(kMembers),
                     misses, "StaticBox");
    // 2. The upload (and the streaming copies cut from it): each chunk keeps
    //    its row, each box its piece - a broken piece's box comes back inert.
    s = replaceEvery(s, "      c.owner = -7;\n", "      c.owner = -7;\n      c.furnRow = fi;\n", misses,
                     "furniture chunk row");
    s = replaceEvery(s, "      sb.owner = -7;\n",
                     "      sb.owner = -7;\n"
                     "      sb.furn = bi;\n"
                     "      if (furnBrokenAt(bi)) furnBoxInert(sb);  // knocked over earlier\n",
                     misses, "furniture box piece");
    if (g.streamed) {
        s = replaceOnce(s, "    case RS_FURN: {\n", "    case RS_FURN: {\n      const int fi = it.a;\n", misses,
                        "stream furniture row");
        s = replaceOnce(s, "      const float* b = &ROAD_FURN_BOXES[(size_t)it.a * 7];\n",
                        "      const int bi = it.a;\n"
                        "      const float* b = &ROAD_FURN_BOXES[(size_t)it.a * 7];\n",
                        misses, "stream furniture box");
        s = replaceOnce(s, "  roadStreamFinish(c);\n",
                        "  roadStreamFinish(c);\n"
                        "  furnBreakApplyChunk(c);  // breakable furniture: still broken\n",
                        misses, "stream chunk rebuild");
    }
    // 3. The vehicle update: a fast car sets a breakable box aside instead of
    //    walling against it, and breaks the ones its body touches after the
    //    wall pass.
    s = replaceOnce(s, "    int pushIdx[8];\n    int pushN = 0;\n",
                    "    int pushIdx[8];\n    int pushN = 0;\n"
                    "    // Breakable furniture (docs/roads.md): boxes this car is fast enough\n"
                    "    // to knock over - not walls; furnBreakContacts below.\n"
                    "    int furnBrk[4];\n    int furnBrkN = 0;\n",
                    misses, "vehicle gather lists");
    s = replaceOnce(s,
                    "        if (ddx * ddx + ddz * ddz >= rr * rr || wallBoxN >= 12) return;\n"
                    "        wallBox[wallBoxN++] = {wx, wz, bhx, bhz, b.yc, b.ys};\n",
                    "        if (ddx * ddx + ddz * ddz >= rr * rr || wallBoxN >= 12) return;\n"
                    "        if (b.furn >= 0 && furnBrkN < 4 && furnBreakable(b.furn, spd)) {\n"
                    "          furnBrk[furnBrkN++] = b.furn;  // breakable furniture: not a wall\n"
                    "          return;\n"
                    "        }\n"
                    "        wallBox[wallBoxN++] = {wx, wz, bhx, bhz, b.yc, b.ys};\n",
                    misses, "vehicle considerProc");
    s = replaceOnce(s, "    VEH_LAP(3);\n",
                    "    VEH_LAP(3);\n"
                    "    if (furnBrkN > 0) furnBreakContacts(vi, furnBrk, furnBrkN, prevX, prevZ);\n",
                    misses, "vehicle after walls");
    // 4. Lamps: a knocked-down lamp's halo and wet streak are skipped, its
    //    pool collapses with it.
    std::string impl = kImpl;
    if (g.lamps) {
        s = replaceOnce(s, "    const float* L = &ROAD_LAMPS[(size_t)li * 10];\n",
                        "    const float* L = &ROAD_LAMPS[(size_t)li * 10];\n"
                        "    if (furnLampDark_[(size_t)li]) continue;  // knocked over (breakable furniture)\n",
                        misses, "lamp coronas");
        impl = replaceEvery(impl, "// {{FURN_LAMP_RESET}}\n",
                            "  furnLampDark_.assign(ROAD_LAMP_COUNT > 0 ? (size_t)ROAD_LAMP_COUNT : (size_t)1, 0);\n",
                            misses, "impl lamp reset");
        impl = replaceEvery(impl, "// {{FURN_LAMP_OFF}}\n",
                            "  // The lamp's light: its halo and streak (renderRoadLamps skips it) and\n"
                            "  // its pool, collapsed in its own chunk like the post.\n"
                            "  if (p.lamp >= 0 && (size_t)p.lamp < furnLampDark_.size()) furnLampDark_[(size_t)p.lamp] = 1;\n"
                            "  if (p.poolCount > 0)\n"
                            "    if (ProcChunk* pc = furnChunkOf(p.poolRow)) furnCollapse(*pc, p.poolFirst, p.poolCount);\n",
                            misses, "impl lamp off");
    } else {
        impl = replaceEvery(impl, "// {{FURN_LAMP_RESET}}\n", "", misses, "impl lamp reset");
        impl = replaceEvery(impl, "// {{FURN_LAMP_OFF}}\n", "", misses, "impl lamp off");
    }
    // 5. Traffic: a knocked-down signal's lit lens (renderTrafficLamps) goes
    //    out - its head is moved out of reach and the lens bag rewritten.
    impl = replaceEvery(impl, "// {{FURN_SIGNAL_OFF}}\n",
                        g.traffic ? "  if (p.kind == 4) {\n"
                                    "    for (TfLamp& l : tfLamps_)\n"
                                    "      if (fabsf(l.x - cx) < 0.05F && fabsf(l.z - cz) < 0.05F) l.x = l.z = 1.0e9F;\n"
                                    "    tfLampSig_ = 0;\n"
                                    "  }\n"
                                  : "",
                        misses, "impl signal off");
    s = replaceOnce(s, "void TerrainGame::renderRoadChunks() {", impl + "void TerrainGame::renderRoadChunks() {",
                    misses, "impl");
    // fillTemplate runs once per generated FILE and each anchor lives in one
    // of them, so a miss here is normal; kHookMarks is what --vehicle-check
    // counts in the whole generated project instead.
    (void)misses;
    return s;
}

const char* const kHookMarks[kHookCount] = {
    "    int furnRow = -1;\n",
    "    int furn = -1;\n",
    "  std::vector<unsigned char> furnBroken_;",
    "constexpr RoadFurnPieceRt ROAD_FURN_PIECES[",
    "  furnBreakReset();  // breakable furniture",
    "      c.owner = -7;\n      c.furnRow = fi;\n",
    "      sb.furn = bi;\n",
    "    int furnBrk[4];\n",
    "          furnBrk[furnBrkN++] = b.furn;",
    "    if (furnBrkN > 0) furnBreakContacts(vi, furnBrk, furnBrkN, prevX, prevZ);",
    "void TerrainGame::furnBreak(int piece, int vi, float spd) {",
};

// --- --vehicle-check "breakable furniture" -------------------------------------

namespace {

struct Bake {
    std::vector<roadgen::CrossingRoad> roads;
    std::vector<roadfurn::Settings> sets;
    roadgen::CrossingPlan plan;
    roadfurn::Result res;
    roadfurn::Tables tables;
    std::vector<roadlight::Lamp> lamps;
    roadlight::Pools pools;
};

// A lone kerbless street along x at z = 0 with lamps, bollards, trees (all
// breakable at their defaults: trees stay solid) and signs nowhere.
void bake(Bake& b) {
    roadgen::CrossingRoad r;
    r.id = "lone";
    r.points = {-60, 0, 0, 0, 60, 0};
    r.width = 8.0f;
    r.intersection = "res/materials/x.mtl";
    r.markings = roadgen::kMarkCrossings;
    b.roads = {r};
    b.sets.assign(1, roadfurn::Settings{});
    roadfurn::Settings& s = b.sets[0];
    // Lamps and trees on the north side only (+z, the left of a road whose
    // points run +x), so the car from the south meets nothing else on its way.
    s.lamps.spacing = 15.0f;
    s.lamps.side = roadfurn::kLeft;
    s.trees.spacing = 15.0f;
    s.trees.side = roadfurn::kLeft;
    s.trees.phase = 5.0f;
    s.trees.offset = 3.0f;
    s.bollards.spacing = 15.0f;
    s.bollards.phase = 10.0f;
    s.bollards.offset = 0.3f;
    s.breakable = true;
    b.plan = roadgen::planCrossings(b.roads, {});
    roadfurn::SceneInput in;
    in.roads = &b.roads;
    in.settings = &b.sets;
    in.plan = &b.plan;
    in.ground = [](float, float) { return 0.0f; };
    b.res = roadfurn::build(in);
    b.tables = roadfurn::Tables{};
    b.tables.breakable = true;
    b.tables.lit = true;
    b.tables.add(0, b.res, {3});
    b.lamps = roadlight::lampsOf(b.res);
    b.pools = roadlight::bakePools(b.lamps, [](float, float) { return 0.0f; });
    std::vector<float> xyz;
    std::vector<uint32_t> uv;
    for (const roadgen::Vertex& v : b.pools.tris) {
        xyz.insert(xyz.end(), {v.x, v.y, v.z});
        uv.push_back(roadlight::packUv(v.u, v.v));
    }
    std::vector<int> inst;
    for (const roadlight::Lamp& L : b.lamps) inst.push_back(L.instance);
    b.tables.addLight(0, xyz, uv, b.pools.chunkSizes, "", inst, b.pools.first, b.pools.count, 0);
}

// One drive at a breakable lamp, the runtime's rule on the host sim: the
// boxes the car is fast enough to break are not solid, and one its body
// touches after the step breaks - its box goes, the car keeps `keep` of
// its speed, the piece's triangles become a debris copy.
struct Drive {
    int breakStep = -1;
    float before = 0, after = 0;  // the car's speed at the break, before / after the loss
    float endSpeed = 0, endZ = 0;
    int debrisVerts = 0;
    bool debrisExact = false;
    float debrisVelDot = 0;       // launch velocity . travel direction
    bool boxGone = false;
    vehiclesim::DriveState st;
};

Drive drive(const Bake& b, int piece, float cruise) {
    using namespace vehiclesim;
    const roadfurn::Instance& L = b.res.instances[(size_t)piece];
    const roadfurn::Box& box0 = b.res.boxes[(size_t)piece];
    std::vector<unsigned char> broken(b.res.boxes.size(), 0);
    DriveSpec spec;
    Drive out;
    DriveState& st = out.st;
    st.pos[0] = L.x;
    st.pos[2] = L.z - 25.0f;  // facing +z (yaw 0) straight at the pole
    st.pos[1] = spec.rideHeight;
    std::vector<unsigned char> aside(b.res.boxes.size(), 0);
    const SolidFn solid = [&](float x, float z, float feetY) {
        for (size_t i = 0; i < b.res.boxes.size(); ++i) {
            if (broken[i] || aside[i]) continue;
            const roadfurn::Box& bx = b.res.boxes[i];
            if (bx.mx[1] <= feetY + 0.5f || bx.mn[1] >= feetY + 0.9f) continue;
            if (x > bx.mn[0] - 0.35f && x < bx.mx[0] + 0.35f && z > bx.mn[2] - 0.35f &&
                z < bx.mx[2] + 0.35f)
                return true;
        }
        return false;
    };
    const HeightFn flat = [](float, float) { return 0.0f; };
    const float hx = 0.5f * spec.track, hz = 0.5f * spec.wheelBase + spec.bodyOverhang;
    for (int i = 0; i < 400; ++i) {
        DriveInput in;
        in.throttle = cruise <= 0.0f || st.speed < cruise ? 1.0f : 0.0f;
        for (size_t k = 0; k < aside.size(); ++k)
            aside[k] = !broken[k] && breaks(st.speed, b.tables.pieces[k].speed);
        const float px = st.pos[0], pz = st.pos[2];
        step(spec, in, 1.0f / 50.0f, flat, st, solid);
        for (size_t k = 0; k < aside.size(); ++k) {
            if (!aside[k] || broken[k]) continue;
            const roadfurn::Box& bx = b.res.boxes[k];
            const float cx = 0.5f * (bx.mn[0] + bx.mx[0]), cz = 0.5f * (bx.mn[2] + bx.mx[2]);
            const float r = 0.5f * std::max(bx.mx[0] - bx.mn[0], bx.mx[2] - bx.mn[2]);
            bool hit = false;
            for (int q = 0; q < 3 && !hit; ++q) {
                const float f = q == 0 ? 1.0f : (q == 1 ? 0.5f : 0.0f);
                hit = touches(px + (st.pos[0] - px) * f, pz + (st.pos[2] - pz) * f, st.yaw, hx, hz,
                              cx, cz, r);
            }
            if (!hit) continue;
            broken[k] = 1;
            if ((int)k != piece) continue;
            out.breakStep = i;
            out.before = st.speed;
            const float keep = b.tables.pieces[k].keep;
            st.speed *= keep;
            st.lateral *= keep;
            out.after = st.speed;
            // The debris copy, read where the runtime reads it: the piece's
            // run of its ROAD_FURN row - which must be exactly the instance's
            // own baked triangles - launched the runtime's way (along the
            // travel at 0.55 x speed + 1, up 2.5 + 0.12 x speed).
            const roadfurn::Instance& in2 = b.res.instances[k];
            const roadfurn::Tables::Piece& pc = b.tables.pieces[k];
            out.debrisVerts = pc.count;
            out.debrisExact = pc.row >= 0 && pc.count == in2.vertexCount && pc.count > 0;
            for (int v = 0; out.debrisExact && v < pc.count; ++v) {
                const roadfurn::Vertex& a = b.res.tris[(size_t)(in2.firstVertex + v)];
                const size_t g = (size_t)(b.tables.rows[(size_t)pc.row].first + pc.first + v);
                out.debrisExact = b.tables.verts[g * 3] == a.x && b.tables.verts[g * 3 + 1] == a.y &&
                                  b.tables.verts[g * 3 + 2] == a.z;
            }
            const float vx = st.pos[0] - px, vz = st.pos[2] - pz;
            const float vl = std::sqrt(vx * vx + vz * vz);
            const float spd = out.before;
            const float lx = vl > 1e-5f ? vx / vl : 0.0f, lz = vl > 1e-5f ? vz / vl : 1.0f;
            // The launch along the travel, against the pole's offset from the
            // car: positive = thrown away from the car, not back into it.
            const float ox = in2.x - px, oz = in2.z - pz;
            out.debrisVelDot = (lx * ox + lz * oz) * (0.55f * spd + 1.0f);
        }
    }
    out.boxGone = broken[(size_t)piece] != 0;
    out.endSpeed = st.speed;
    out.endZ = st.pos[2];
    (void)box0;
    return out;
}

}  // namespace

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- breakable furniture --\n");
    Bake a, b2;
    bake(a);
    bake(b2);
    const roadfurn::Result& r = a.res;
    int lamp = -1, tree = -1;
    for (size_t i = 0; i < r.instances.size(); ++i) {
        const roadfurn::Instance& in = r.instances[i];
        if (in.z < 0.0f) continue;  // the far side: the car starts south of the road
        if (lamp < 0 && in.kind == roadfurn::kLamp && in.x > -30.0f && in.x < 30.0f) lamp = (int)i;
        if (tree < 0 && in.kind == roadfurn::kTree && in.x > -30.0f && in.x < 30.0f) tree = (int)i;
    }
    std::printf("  %zu pieces (%d lamps, %d trees, %d bollards), lamp %d, tree %d\n",
                a.tables.pieces.size(), r.perKind[roadfurn::kLamp], r.perKind[roadfurn::kTree],
                r.perKind[roadfurn::kBollard], lamp, tree);
    // The settings: the threshold and the keep reach every instance from its
    // road's rule, trees off by default.
    {
        bool ok = !r.instances.empty();
        for (const roadfurn::Instance& in : r.instances) {
            const roadfurn::Break d = roadfurn::defaultBreak(in.kind);
            if (in.kind == roadfurn::kTree)
                ok = ok && in.breakSpeed == 0.0f && in.breakKeep == 1.0f;
            else
                ok = ok && in.breakSpeed == d.speed && std::fabs(in.breakKeep - (1.0f - d.loss)) < 1e-6f;
        }
        verdict(ok && lamp >= 0 && tree >= 0,
                "lamps and bollards break at their kind's speed, trees stay solid by default");
    }
    // The piece table: every instance's run of the merged rows, exactly.
    {
        const roadfurn::Tables& t = a.tables;
        bool ok = t.pieces.size() == r.instances.size() && t.pieces.size() == r.boxes.size();
        int lampsWithPool = 0, lampRows = 0;
        for (size_t i = 0; ok && i < t.pieces.size(); ++i) {
            const roadfurn::Tables::Piece& pc = t.pieces[i];
            const roadfurn::Instance& in = r.instances[i];
            ok = pc.row >= 0 && t.rows[(size_t)pc.row].first + pc.first == in.firstVertex &&
                 pc.count == in.vertexCount && pc.first + pc.count <= t.rows[(size_t)pc.row].count &&
                 !t.rows[(size_t)pc.row].light && pc.kind == in.kind && pc.snd == 3;
            if (in.kind == roadfurn::kLamp) {
                ++lampRows;
                if (pc.lamp >= 0 && pc.poolRow >= 0 && t.rows[(size_t)pc.poolRow].light &&
                    pc.poolFirst + pc.poolCount <= t.rows[(size_t)pc.poolRow].count && pc.poolCount > 0 &&
                    a.lamps[(size_t)pc.lamp].instance == (int)i)
                    ++lampsWithPool;
            } else {
                ok = ok && pc.lamp < 0 && pc.poolRow < 0;
            }
        }
        // Collapsing one piece leaves every other vertex of its row exactly
        // where it was, and its own triangles without area.
        bool collapseOk = lamp >= 0;
        if (lamp >= 0) {
            const roadfurn::Tables::Piece& pc = t.pieces[(size_t)lamp];
            const roadfurn::Tables::Row& row = t.rows[(size_t)pc.row];
            std::vector<float> v(t.verts.begin() + (size_t)row.first * 3,
                                 t.verts.begin() + (size_t)(row.first + row.count) * 3);
            const std::vector<float> orig = v;
            for (int k = 1; k < pc.count; ++k)
                for (int c = 0; c < 3; ++c)
                    v[(size_t)(pc.first + k) * 3 + (size_t)c] = v[(size_t)pc.first * 3 + (size_t)c];
            for (int k = 0; k < row.count; ++k) {
                const bool inside = k >= pc.first && k < pc.first + pc.count;
                if (inside) continue;
                collapseOk = collapseOk && std::memcmp(&v[(size_t)k * 3], &orig[(size_t)k * 3],
                                                       3 * sizeof(float)) == 0;
            }
            for (int k = pc.first; k + 2 < pc.first + pc.count; k += 3) {
                const float* p0 = &v[(size_t)k * 3];
                const float* p1 = &v[(size_t)(k + 1) * 3];
                const float* p2 = &v[(size_t)(k + 2) * 3];
                const float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
                const float bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
                const float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
                collapseOk = collapseOk && cx * cx + cy * cy + cz * cz == 0.0f;
            }
            collapseOk = collapseOk && pc.first % 3 == 0 && pc.count % 3 == 0;
        }
        std::printf("  piece table: %s, %d of %d lamps know their ROAD_LAMPS row and pool, "
                    "collapse %s\n", ok ? "exact" : "WRONG", lampsWithPool, lampRows,
                    collapseOk ? "clean" : "WRONG");
        verdict(ok, "every piece is its instance's exact vertex run of its ROAD_FURN row");
        verdict(lampRows > 0 && lampsWithPool == lampRows,
                "every lamp piece names its ROAD_LAMPS row and its pool's run");
        verdict(collapseOk, "collapsing a piece moves nothing else and leaves it no area");
        const std::string src = t.source(true);
        verdict(src.find("constexpr RoadFurnPieceRt ROAD_FURN_PIECES[" +
                         std::to_string(t.pieces.size()) + "]") != std::string::npos,
                "the console gets one ROAD_FURN_PIECES row per box");
    }
    // The drive: fast into the lamp breaks it, the car loses its share and
    // goes on; slow it stops at the pole, as before; a tree stops a fast car.
    if (lamp >= 0) {
        const Drive fast = drive(a, lamp, 0.0f);
        const float keep = a.tables.pieces[(size_t)lamp].keep;
        std::printf("  fast: broke at step %d at %.2f u/s -> %.2f (keep %.2f), %d debris vertices, "
                    "end speed %.2f at z %.1f (pole at %.1f)\n",
                    fast.breakStep, fast.before, fast.after, keep, fast.debrisVerts,
                    fast.endSpeed, fast.endZ, r.instances[(size_t)lamp].z);
        verdict(fast.breakStep >= 0 && fast.boxGone && fast.before >= 9.0f &&
                    std::fabs(fast.after - fast.before * keep) < 1e-4f,
                "a car into a lamp at speed breaks it and keeps exactly its share of the speed");
        verdict(fast.endSpeed > 0.5f * fast.after && fast.endZ > r.instances[(size_t)lamp].z + 5.0f,
                "after the hit the car drives on past where the pole stood");
        verdict(fast.debrisVerts == r.instances[(size_t)lamp].vertexCount && fast.debrisExact &&
                    fast.debrisVelDot > 0.0f,
                "the broken lamp becomes one debris piece of its own triangles, thrown forward");
        const Drive slow = drive(a, lamp, 4.0f);
        const float poleZ = r.instances[(size_t)lamp].z;
        std::printf("  slow: break step %d, end speed %.2f, nose at %.2f (pole face %.2f)\n",
                    slow.breakStep, slow.endSpeed, slow.endZ + 1.3f,
                    poleZ - r.instances[(size_t)lamp].radius);
        verdict(slow.breakStep < 0 && !slow.boxGone && std::fabs(slow.endSpeed) < 0.5f &&
                    slow.endZ < poleZ,
                "a slow bump does not break it: the car stops at the pole");
        const Drive again = drive(b2, lamp, 0.0f);
        verdict(again.breakStep == fast.breakStep &&
                    std::memcmp(&again.st, &fast.st, sizeof(vehiclesim::DriveState)) == 0,
                "the same drive breaks the same pole on the same step, bit for bit");
    }
    if (tree >= 0) {
        const Drive t = drive(a, tree, 0.0f);
        std::printf("  tree: break step %d, end speed %.2f\n", t.breakStep, t.endSpeed);
        verdict(t.breakStep < 0 && !t.boxGone && std::fabs(t.endSpeed) < 2.0f,
                "a tree (not breakable by default) stops a fast car");
    }
    // The settings round-trip, and a road that only sets breakable saves it.
    {
        roadfurn::Settings s;
        s.lamps.spacing = 12.0f;
        s.breakable = true;
        s.brk[roadfurn::kTree].on = true;
        s.brk[roadfurn::kLamp].speed = 11.0f;
        s.brk[roadfurn::kSign].loss = 0.3f;
        s.breakSound = "res/sfx/crash.wav";
        json::Value v;
        const std::string text = roadfurn::toJson(s);
        roadfurn::Settings back;
        const bool parsed = json::parse(text, v);
        if (parsed) roadfurn::fromJson(v, back);
        roadfurn::Settings plain;
        plain.lamps.spacing = 12.0f;
        const std::string plainText = roadfurn::toJson(plain);
        std::printf("  json: %s\n", text.c_str());
        verdict(parsed && back == s && plainText.find("breakable") == std::string::npos &&
                    roadfurn::signature(s) != roadfurn::signature(plain),
                "the breakable settings round-trip, save nothing at the defaults, and sign the bake");
    }
    // The codegen: a vehicle project with breakable furniture gets the piece
    // table and every hook (embedded, lit, streamed, with traffic); without
    // a car, or with nothing breakable, none of it.
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "tyrax-furnbreak-check";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Project p;
        p.name = "fbcheck";
        p.dir = dir.string();
        p.scenes.emplace_back();
        SceneData& sc = p.scenes.back();
        sc.name = "main";
        auto road = [&](const std::string& id, std::vector<float> pts) {
            SceneObject o;
            o.type = PrimitiveType::Road;
            o.id = id;
            o.name = id;
            o.roadPoints = std::move(pts);
            o.roadWidth = 8.0f;
            o.roadTexture = "res/materials/roads/road-2lane.mtl";
            o.roadIntersectionTexture = "res/materials/roads/road-junction.mtl";
            sc.objects.push_back(o);
            return sc.objects.size() - 1;
        };
        const size_t ra = road("a", {-60, 0, 0, 0, 60, 0});
        const size_t rb = road("b", {0, -60, 0, 0, 0, 60});
        sc.objects[ra].roadFurniture.lamps.spacing = 15.0f;
        sc.objects[ra].roadFurniture.signals = true;
        sc.objects[ra].roadFurniture.breakable = true;
        sc.objects[rb].roadFurniture.signals = true;
        int genNo = 0;
        auto gen = [&]() {
            std::string all;
            for (const auto& f : templates::generate(p)) all += f.content;
            // TYRAX_FURNBREAK_DUMP=<dir>: every generated source as gen<N>.txt.
            if (const char* d = std::getenv("TYRAX_FURNBREAK_DUMP")) {
                const std::string path = std::string(d) + "/gen" + std::to_string(genNo) + ".txt";
                if (std::FILE* fp = std::fopen(path.c_str(), "wb")) {
                    std::fwrite(all.data(), 1, all.size(), fp);
                    std::fclose(fp);
                }
            }
            ++genNo;
            return all;
        };
        auto has = [](const std::string& s, const char* what) { return s.find(what) != std::string::npos; };
        const std::string noCar = gen();
        VehicleDef vd;
        vd.id = "car000000001";
        vd.name = "Car";
        vd.modelPath = "res/models/car.glb";
        p.vehicles.push_back(vd);
        {
            SceneObject car;
            car.type = PrimitiveType::Vehicle;
            car.id = "carobj00000001";
            car.name = "car";
            car.vehicleDef = "Car";
            car.position[2] = -30.0f;
            sc.objects.push_back(car);
        }
        const std::string on = gen();
        int hooks = 0;
        for (const char* m : kHookMarks) hooks += has(on, m);
        const bool lampHook = has(on, "    if (furnLampDark_[(size_t)li]) continue;") &&
                              has(on, "furnLampDark_[(size_t)p.lamp] = 1;");
        std::printf("  codegen: %d/%d hooks, lamp hook %s\n", hooks, kHookCount, lampHook ? "yes" : "no");
        verdict(hooks == kHookCount && lampHook && !has(noCar, "furnBroken_") &&
                    !has(noCar, "ROAD_FURN_PIECES"),
                "a vehicle project with breakable furniture gets the table and every hook (lamps "
                "included), one without a car none");
        p.settings.traffic.cars = 2;
        p.settings.traffic.vehicles = {"Car"};
        const std::string traffic = gen();
        int tHooks = 0;
        for (const char* m : kHookMarks) tHooks += has(traffic, m);
        verdict(has(traffic, "for (TfLamp& l : tfLamps_)") && tHooks == kHookCount,
                "with road traffic a broken signal puts its live lens out");
        p.settings.traffic = TrafficSettings();
        p.settings.roadStreamRadius = 150.0f;
        for (const bool embed : {true, false}) {
            p.settings.roadStreamEmbedTables = embed;
            const std::string st = gen();
            int sHooks = 0;
            for (const char* m : kHookMarks) sHooks += has(st, m);
            verdict(has(st, "  furnBreakApplyChunk(c);  // breakable furniture: still broken") &&
                        has(st, "    case RS_FURN: {\n      const int fi = it.a;") &&
                        has(st, "      const int bi = it.a;") && sHooks == kHookCount &&
                        !has(st, "road streaming: template anchor missing"),
                    embed ? "a streamed project re-collapses a broken piece when its chunk comes back"
                          : "...and so does one with its tables on disk");
        }
        p.settings.roadStreamRadius = 0.0f;
        p.settings.roadStreamEmbedTables = false;
        sc.objects[ra].roadFurniture.breakable = false;
        const std::string off = gen();
        verdict(!has(off, "furnBroken_") && !has(off, "ROAD_FURN_PIECES") && has(off, "ROAD_FURN_BOXES"),
                "furniture with nothing breakable generates none of it");
        std::filesystem::remove_all(dir, ec);
    }
}

}  // namespace roadfurnbreak
