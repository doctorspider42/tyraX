// Road traffic (docs/traffic.md): the codegen half - the ambient cars
// appended to the scenes, the console tables, the TerrainGame members and
// functions, and the hooks spliced into the vehicle runtime.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "ambience.hpp"
#include "project.hpp"
#include "roadfurniture.hpp"
#include "roadlanes.hpp"
#include "templates.hpp"

#include "traffic_core_gen.hpp"

namespace roadlanes {
namespace {

std::string lit(float v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.7g", (double)v);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s + "F";
}

std::string replaceOnce(const std::string& s, const std::string& from, const std::string& to) {
    const size_t a = s.find(from);
    if (a == std::string::npos) return s;
    std::string out = s;
    out.replace(a, from.size(), to);
    return out;
}

// The definitions the cars are drawn from: the setting's list (by name), or
// every definition with a model.
std::vector<std::string> trafficDefs(const Project& p) {
    std::vector<std::string> out;
    for (const VehicleDef& v : p.vehicles) {
        if (v.modelPath.empty() || v.id.empty()) continue;
        if (!p.settings.traffic.vehicles.empty() &&
            std::find(p.settings.traffic.vehicles.begin(), p.settings.traffic.vehicles.end(),
                      v.name) == p.settings.traffic.vehicles.end())
            continue;
        out.push_back(v.name);
    }
    return out;
}

bool sceneHasLanes(const SceneData& sc) {
    for (const SceneObject& o : sc.objects)
        if (o.type == PrimitiveType::Road && o.roadKind != 1 && o.roadPoints.size() >= 4) return true;
    return false;
}

}  // namespace

bool projectHasTraffic(const Project& p) {
    if (p.settings.traffic.cars <= 0 || trafficDefs(p).empty()) return false;
    for (const SceneData& sc : p.scenes)
        if (sceneHasLanes(sc)) return true;
    return false;
}

bool withTrafficCars(const Project& p, Project& out) {
    if (!projectHasTraffic(p)) return false;
    out = p;
    const std::vector<std::string> defs = trafficDefs(p);
    for (size_t si = 0; si < out.scenes.size(); ++si) {
        SceneData& sc = out.scenes[si];
        if (!sceneHasLanes(sc)) continue;
        // Parked on the first road's first point until the runtime places
        // them: a real spot, so no table that bounds the scene by its objects
        // grows; hidden from the first frame.
        float px = 0.0f, pz = 0.0f;
        for (const SceneObject& o : sc.objects)
            if (o.type == PrimitiveType::Road && o.roadKind != 1 && o.roadPoints.size() >= 4) {
                px = o.roadPoints[0];
                pz = o.roadPoints[1];
                break;
            }
        for (int k = 0; k < p.settings.traffic.cars; ++k) {
            SceneObject o;
            o.type = PrimitiveType::Vehicle;
            o.name = std::string(kCarPrefix) + std::to_string(k);
            o.id = std::string(kCarPrefix) + std::to_string(si) + "-" + std::to_string(k);
            o.vehicleDef = defs[(size_t)k % defs.size()];
            o.vehicleDriveable = false;
            o.position[0] = px;
            o.position[1] = 0.0f;
            o.position[2] = pz;
            sc.objects.push_back(std::move(o));
        }
    }
    return true;
}

void Tables::addScene(int scene, const Graph& g, const Options& opt,
                      const std::vector<roadfurn::Instance>& furniture) {
    while ((int)scenes.size() <= scene) scenes.push_back({0, 0, 0, 0, 0, 0});
    TfGraph tg;
    toSim(g, opt, 1.0f, 1.0f, 1.0f, tg);
    std::array<int, 6>& row = scenes[(size_t)scene];
    row[0] = (int)segs.size() / 13;
    row[1] = (int)tg.segs.size();
    row[2] = (int)nodeSignal.size();
    row[3] = (int)tg.nodes.size();
    row[4] = (int)lamps.size() / 8;
    for (const TfSeg& s : tg.segs) {
        segs.insert(segs.end(), {(int)(pts.size() / 3) + 0, s.count, s.kind, s.node,
                                 (int)next.size(), s.nextCount, s.group, s.rank, s.turn,
                                 (int)conf.size(), s.confCount, s.inner, s.outer});
        for (int k = 0; k < s.count * 3; ++k) pts.push_back(tg.pts[(size_t)s.first * 3 + (size_t)k]);
        for (int k = 0; k < s.nextCount; ++k) next.push_back(tg.next[(size_t)(s.nextFirst + k)]);
        for (int k = 0; k < s.confCount; ++k) conf.push_back(tg.conf[(size_t)(s.confFirst + k)]);
    }
    // Per node its phase count (0 = no lights): 2 at a four-way node, one per
    // arm elsewhere.
    for (size_t ni = 0; ni < g.nodes.size(); ++ni)
        nodeSignal.push_back(g.nodes[ni].signalled ? g.nodes[ni].phases : 0);
    for (const roadfurn::Instance& in : furniture) {
        if (in.kind != roadfurn::kSignal || in.node < 0 || in.arm < 0) continue;
        if ((size_t)in.node >= g.nodes.size() || !g.nodes[(size_t)in.node].signalled) continue;
        // The head shows the phase of the approach it faces.
        const int phase = roadfurn::signalPhase(g.nodes[(size_t)in.node].arms, in.arm);
        lamps.insert(lamps.end(), {(float)in.node, (float)phase, in.x, in.y, in.z, in.fx, in.fz,
                                   in.scale});
    }
    row[5] = (int)lamps.size() / 8 - row[4];
    lanes += (int)g.lanes.size();
    conns += (int)g.conns.size();
    warnings += (int)g.warnings.size();
    std::ostringstream n;
    n << "// scene " << scene << ": " << g.lanes.size() << " lanes, " << g.conns.size()
      << " connections, " << row[5] << " signal heads, " << g.deadEnds << " dead end(s)\n";
    notes += n.str();
}

std::string Tables::source(const Project& p, int sceneCount) const {
    const TrafficSettings& t = p.settings.traffic;
    float radius = t.radius;
    // Never spawn where the roads may not be built (docs/roads.md "Road
    // streaming"): the ring keeps everything within its radius.
    if (p.settings.roadStreamRadius > 0.0f)
        radius = std::min(radius, std::max(30.0f, p.settings.roadStreamRadius - 20.0f));
    std::ostringstream o;
    o << "// Road traffic (docs/traffic.md): the lane graph, host-baked. Per scene a\n"
         "// contiguous run of segments - lanes, then the connections through the\n"
         "// nodes - with their points; NEXT and CONF hold scene-local segment\n"
         "// indices. Copied into the traffic core (TfGraph) at scene load.\n"
      << notes << "constexpr int TRAFFIC_CARS = " << t.cars << ";\n"
      << "constexpr float TRAFFIC_RADIUS = " << lit(radius) << ";\n"
      << "constexpr float TRAFFIC_DENSITY = " << lit(t.density) << ";\n"
      << "constexpr float TRAFFIC_SPEED = " << lit(t.speed) << ";\n"
      << "constexpr float TRAFFIC_GREEN = " << lit(t.green) << ";\n"
      << "constexpr float TRAFFIC_AMBER = " << lit(t.amber) << ";\n"
      << "constexpr float TRAFFIC_ALL_RED = " << lit(t.allRed) << ";\n"
      << "constexpr int TRAFFIC_HEADLIGHTS = " << (t.headlights ? 1 : 0) << ";\n"
      << "constexpr int TRAFFIC_LANE_CHANGES = " << (t.laneChanges ? 1 : 0) << ";\n"
      << "constexpr float TRAFFIC_LENS_Y[3] = {" << lit(roadfurn::kSignalLensY[0]) << ", "
      << lit(roadfurn::kSignalLensY[1]) << ", " << lit(roadfurn::kSignalLensY[2]) << "};\n"
      << "constexpr float TRAFFIC_LENS_Z = " << lit(roadfurn::kSignalLensZ) << ";\n"
      << "constexpr float TRAFFIC_LENS_HALF = " << lit(roadfurn::kSignalLensHalf) << ";\n"
      << "struct TrafficSegData { int first, count, kind, node, nextFirst, nextCount, group, rank,"
         " turn, confFirst, confCount, inner, outer; };\n"
      << "struct TrafficLampData { int node, group; float x, y, z, fx, fz, scale; };\n";
    o << "constexpr int TRAFFIC_SCENES[SCENE_COUNT][6] = {";
    for (int si = 0; si < sceneCount; ++si) {
        const std::array<int, 6> r = (size_t)si < scenes.size() ? scenes[(size_t)si]
                                                                : std::array<int, 6>{0, 0, 0, 0, 0, 0};
        o << (si ? ", " : "") << "{" << r[0] << ", " << r[1] << ", " << r[2] << ", " << r[3] << ", "
          << r[4] << ", " << r[5] << "}";
    }
    o << "};\n";
    // Headlights (docs/traffic.md): night in a scene whose day/night cycle
    // does not run, at the hour it is baked at - the runtime's own rule (the
    // sun under 2 degrees). A running cycle is asked live; no cycle is day.
    o << "constexpr int TRAFFIC_NIGHT[SCENE_COUNT] = {";
    for (int si = 0; si < sceneCount; ++si) {
        int night = 0;
        if ((size_t)si < p.scenes.size())
            if (const DayCycle* c = templates::sceneDayCycle(p, p.scenes[(size_t)si]))
                if (!c->runtime)
                    night = ambience::evaluate(*c, ambience::bakedHour(*c)).sunDir[1] < 0.0349f ? 1 : 0;
        o << (si ? ", " : "") << night;
    }
    o << "};\n";
    o << "constexpr TrafficSegData TRAFFIC_SEGS[" << std::max<size_t>(1, segs.size() / 13) << "] = {\n";
    if (segs.empty()) o << "    {0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, -1, -1}\n";
    for (size_t i = 0; i < segs.size(); i += 13) {
        o << "    {";
        for (int k = 0; k < 13; ++k) o << (k ? ", " : "") << segs[i + (size_t)k];
        o << "},\n";
    }
    o << "};\n";
    o << "constexpr float TRAFFIC_PTS[" << std::max<size_t>(3, pts.size()) << "] = {\n";
    if (pts.empty()) o << "    0.0F, 0.0F, 0.0F\n";
    for (size_t i = 0; i < pts.size(); i += 3)
        o << "    " << lit(pts[i]) << ", " << lit(pts[i + 1]) << ", " << lit(pts[i + 2]) << ",\n";
    o << "};\n";
    auto ints = [&](const char* name, const std::vector<int>& v) {
        // Scene-local segment indices: a short holds a city (7 020); past that, int.
        o << "constexpr " << (segs.size() / 13 > 32000 ? "int " : "short ") << name << "["
          << std::max<size_t>(1, v.size()) << "] = {";
        if (v.empty()) o << "0";
        for (size_t i = 0; i < v.size(); ++i) o << (i ? (i % 24 ? ", " : ",\n    ") : "") << v[i];
        o << "};\n";
    };
    ints("TRAFFIC_NEXT", next);
    ints("TRAFFIC_CONF", conf);
    ints("TRAFFIC_NODE_SIGNAL", nodeSignal);
    o << "constexpr TrafficLampData TRAFFIC_LAMPS[" << std::max<size_t>(1, lamps.size() / 8) << "] = {\n";
    if (lamps.empty()) o << "    {0, 0, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F}\n";
    for (size_t i = 0; i < lamps.size(); i += 8)
        o << "    {" << (int)lamps[i] << ", " << (int)lamps[i + 1] << ", " << lit(lamps[i + 2]) << ", "
          << lit(lamps[i + 3]) << ", " << lit(lamps[i + 4]) << ", " << lit(lamps[i + 5]) << ", "
          << lit(lamps[i + 6]) << ", " << lit(lamps[i + 7]) << "},\n";
    o << "};\n";
    return o.str();
}

std::string coreSource() {
    std::string s(reinterpret_cast<const char*>(trafficsrc::kCore), trafficsrc::kCoreSize);
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    std::string out;
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

std::string membersSource() {
    return std::string("  // --- road traffic (docs/traffic.md) ---\n"
                       "  // The traffic core (src/traffic_core.inl, pasted verbatim - the editor\n"
                       "  // compiles the same file for --vehicle-check's host simulation).\n") +
           coreSource() + R"TFMEM(
  TfSim tf_;
  std::vector<int> tfVehicle_;  // per traffic car: its vehicles_ slot
  std::vector<int> tfCarOf_;    // per vehicles_ slot: its traffic car, -1
  int tfScene_ = -1;
  float tfLenT_ = 0.0F, tfLaneNear_ = 0.0F, tfLogT_ = 0.0F;
  unsigned int tfRng_ = 0x9E3779B9U;
  int tfSpawned_ = 0, tfRecycled_ = 0, tfRedRuns_ = 0, tfTarget_ = 0;
  // Headlights at night (docs/traffic.md "Headlights"): the state every
  // traffic car was last switched to (-1 = not yet decided this scene).
  int tfLights_ = -1;
  // EE cost (profTicks, 295 a microsecond): the traffic core's own share and
  // the whole vehicle step it runs inside, summed over the log period.
  u32 tfCoreTicks_ = 0U, tfStepTicks_ = 0U, tfStepT0_ = 0U;
  int tfFrames_ = 0;
  struct TfLamp { int node, group; float x, y, z, fx, fz, scale; };
  std::vector<TfLamp> tfLamps_;
  // The stop lines at signalled nodes (the player's red-light check): the
  // lane, its node and its phase.
  struct TfLine { int lane, node, group; };
  std::vector<TfLine> tfLines_;
  int tfRunLine_ = -1;
  float tfRunAlong_ = 0.0F;
  enum { kTfLampMax = 48 };
  BagArray<Tyra::Vec4> tfLampVerts_;
  BagArray<Tyra::Color> tfLampCols_;
  std::unique_ptr<Tyra::StaPipBag> tfLampBag_;
  std::unique_ptr<Tyra::StaPipInfoBag> tfLampInfo_;
  std::unique_ptr<Tyra::StaPipColorBag> tfLampColorBag_;
  int tfLampCount_ = 0;
  unsigned int tfLampSig_ = 0U;
  void trafficSetup(int scene);
  void trafficFrame(float dt);
  void trafficDrive(int vi, float& throttle, float& brake, float& steer);
  bool trafficKinematic(int vi, float dt, float throttle, float brake);
  int tfKinematic_ = 0;  // cars on the cheap far path this frame (telemetry)
  void trafficPlace(int car, int seg, float s);
  void trafficRemove(int car);
  bool trafficGroundReady(float x, float z) const;
  int trafficNight() const;
  void trafficRedLight();
  void renderTrafficLamps();
)TFMEM";
}

std::string implSource(bool streamed) {
    std::string s = R"TFIMPL(
// --- road traffic (docs/traffic.md) -----------------------------------------

// Defined further down the vehicle runtime (the vehiclesim::bodyRotation twin).
static void vehBodyRotation(float pitch, float yaw, float roll, float out[3]);

// Where the roads are not built (road streaming), a car is never spawned.
bool TerrainGame::trafficGroundReady(float x, float z) const {
  {{TF_READY}}
}

// Night for the traffic's headlights: the scene's day/night cycle with the
// sun under 2 degrees, else the hour a non-running cycle is baked at
// (TRAFFIC_NIGHT, host-computed with the same rule).
int TerrainGame::trafficNight() const {
  if (!TRAFFIC_HEADLIGHTS) return 0;  // Preferences > Traffic > Headlights at night off
  const int sc = currentScene >= 0 && currentScene < SCENE_COUNT ? currentScene : 0;
  if (daynight::active(sc)) return daynight::g_sun[1] < 0.0349F ? 1 : 0;
  return TRAFFIC_NIGHT[sc];
}

void TerrainGame::trafficSetup(int scene) {
  tf_ = TfSim();
  tfVehicle_.clear();
  tfLamps_.clear();
  tfLines_.clear();
  tfCarOf_.assign(VEHICLE_COUNT > 0 ? VEHICLE_COUNT : 1, -1);
  tfScene_ = scene;
  tfLenT_ = tfLogT_ = tfLaneNear_ = 0.0F;
  tfSpawned_ = tfRecycled_ = 0;
  tfRunLine_ = -1;
  tfLights_ = -1;
  tfLampSig_ = 0U;
  tfLampCount_ = 0;
  tfLampVerts_.resize(kTfLampMax * 12);
  tfLampCols_.resize(kTfLampMax * 12);
  const int* sc = TRAFFIC_SCENES[scene];
  TfGraph& g = tf_.g;
  g.green = TRAFFIC_GREEN;
  g.amber = TRAFFIC_AMBER;
  g.allRed = TRAFFIC_ALL_RED;
  g.speed = TRAFFIC_SPEED;
  // The scene's points are one contiguous run of TRAFFIC_PTS: point at it.
  const int base = sc[1] > 0 ? TRAFFIC_SEGS[sc[0]].first : 0;
  g.pts = &TRAFFIC_PTS[(size_t)base * 3];
  g.pointCount = 0;
  for (int k = 0; k < sc[1]; ++k) {
    const TrafficSegData& d = TRAFFIC_SEGS[sc[0] + k];
    TfSeg s;
    s.first = d.first - base;
    s.count = d.count;
    g.pointCount = s.first + s.count > g.pointCount ? s.first + s.count : g.pointCount;
    s.kind = d.kind;
    s.node = d.node;
    s.group = d.group;
    s.rank = d.rank;
    s.turn = d.turn;
    s.nextFirst = (int)g.next.size();
    s.nextCount = d.nextCount;
    for (int i = 0; i < d.nextCount; ++i) g.next.push_back(TRAFFIC_NEXT[d.nextFirst + i]);
    s.confFirst = (int)g.conf.size();
    s.confCount = d.confCount;
    for (int i = 0; i < d.confCount; ++i) g.conf.push_back(TRAFFIC_CONF[d.confFirst + i]);
    s.inner = d.inner;
    s.outer = d.outer;
    g.segs.push_back(s);
  }
  // TRAFFIC_NODE_SIGNAL is each node's phase count (0 = no lights); each
  // node's cycle is one slot per phase (the host toSim's twin).
  for (int k = 0; k < sc[3]; ++k) {
    TfNode n;
    const int ph = TRAFFIC_NODE_SIGNAL[sc[2] + k];
    n.signal = ph > 0 ? 1 : 0;
    n.phases = ph > 1 ? ph : 2;
    const float cyc = (float)n.phases * (TRAFFIC_GREEN + TRAFFIC_AMBER + TRAFFIC_ALL_RED);
    n.offset = fmodf((float)k * 7.31F, cyc);
    g.nodes.push_back(n);
  }
  g.finish(3.0F);
  for (int k = 0; k < sc[5]; ++k) {
    const TrafficLampData& d = TRAFFIC_LAMPS[sc[4] + k];
    tfLamps_.push_back({d.node, d.group, d.x, d.y, d.z, d.fx, d.fz, d.scale});
  }
  for (int si = 0; si < (int)g.segs.size(); ++si) {
    const TfSeg& G = g.segs[(size_t)si];
    if (G.kind != 0 || G.nextCount <= 0) continue;
    const TfSeg& C = g.segs[(size_t)g.next[(size_t)G.nextFirst]];
    if (C.kind == 1 && C.group >= 0 && C.node >= 0 && g.nodes[(size_t)C.node].signal)
      tfLines_.push_back({si, C.node, C.group});
  }
  // The appended cars (VEHICLES rows with wpFirst -2): parked out of the
  // world until the ring places them.
  for (int vi = 0; vi < vehicleCount_; ++vi) {
    VehicleRt& v = vehicles_[vi];
    if (v.wpFirst != -2) continue;
    tfCarOf_[(size_t)vi] = (int)tfVehicle_.size();
    tfVehicle_.push_back(vi);
    v.active = 0;
    if (v.object >= 0 && v.object < (int)runtimeObjects.size())
      runtimeObjects[v.object].visible = false;
  }
  tf_.reset((int)tfVehicle_.size());
  tf_.changing = TRAFFIC_LANE_CHANGES;
  int conns = 0, sig = 0;
  for (const TfSeg& G : g.segs) conns += G.kind == 1;
  for (const TfNode& n : g.nodes) sig += n.signal;
  TYRA_LOG("TRAFFIC scene ", scene, " lanes ", (int)g.lanes.size(), " connections ", conns,
           " points ", g.pointCount, " signals ", sig, " lamps ",
           (int)tfLamps_.size(), " cars ", (int)tfVehicle_.size());
}

void TerrainGame::trafficPlace(int car, int seg, float s) {
  const int vi = tfVehicle_[(size_t)car];
  VehicleRt& v = vehicles_[vi];
  if (v.damage > 0.0F || v.dentCount > 0 || v.piecesGone != 0U) repairVehicle(vi);
  const int obj = v.object, def = v.def, drv = v.driveable;
  const float scale = v.scale;
  v = VehicleRt();
  v.object = obj;
  v.def = def;
  v.driveable = drv;
  v.scale = scale;
  v.wpFirst = -2;
  v.active = 1;
  v.lightsOn = tfLights_ > 0 ? 2 : 0;  // lamps without the pool (see trafficFrame)
  float p[5];
  tf_.g.at(seg, s, p);
  v.pos[0] = p[0];
  v.pos[1] = p[1] + VEHICLE_DEFS[def].rideHeight * scale;
  v.pos[2] = p[2];
  v.yaw = atan2f(p[3], p[4]) * 57.29578F;
  v.speed = TRAFFIC_SPEED * 0.6F;
  v.aiPrevX = p[0];
  v.aiPrevZ = p[2];
  tf_.place(car, seg, s);
  TfCar& c = tf_.cars[(size_t)car];
  c.x = p[0];
  c.z = p[2];
  c.yaw = v.yaw;
  c.speed = v.speed;
  c.passed = 0;
  if (obj >= 0 && obj < (int)runtimeObjects.size()) runtimeObjects[obj].visible = true;
  ++tfSpawned_;
}

void TerrainGame::trafficRemove(int car) {
  const int vi = tfVehicle_[(size_t)car];
  VehicleRt& v = vehicles_[vi];
  tf_.remove(car);
  v.active = 0;
  if (v.object >= 0 && v.object < (int)runtimeObjects.size())
    runtimeObjects[v.object].visible = false;
  ++tfRecycled_;
}

// Once a frame, before the vehicle sub-steps: the signal clock, who is in the
// way, recycling the cars that fell behind and placing new ones out of view.
void TerrainGame::trafficFrame(float dt) {
  if (tfScene_ < 0 || tf_.g.segs.empty() || tfVehicle_.empty()) return;
  const u32 tfT0 = profTicks();
  ++tfFrames_;
  tf_.clock += dt;
  float fx = cameraLookAt.x, fz = cameraLookAt.z;
  if (vehicleDriver_ >= 0 && vehicleDriver_ < vehicleCount_) {
    fx = vehicles_[vehicleDriver_].pos[0];
    fz = vehicles_[vehicleDriver_].pos[2];
  } else if (PLAYER_INDEX >= 0) {
    fx = players[0].x;
    fz = players[0].z;
  }
  float cfx = cameraLookAt.x - cameraPosition.x, cfz = cameraLookAt.z - cameraPosition.z;
  {
    const float l = sqrtf(cfx * cfx + cfz * cfz);
    if (l > 1e-3F) {
      cfx /= l;
      cfz /= l;
    } else {
      cfx = 0.0F;
      cfz = 1.0F;
    }
  }
  // Everything that is not traffic is in the way: other cars, and the player
  // on foot.
  tf_.obst.clear();
  for (int vi = 0; vi < vehicleCount_; ++vi) {
    const VehicleRt& v = vehicles_[vi];
    if (!v.active || v.def < 0 || tfCarOf_[(size_t)vi] >= 0) continue;
    const VehicleDefData& s = VEHICLE_DEFS[v.def];
    tf_.obst.push_back({v.pos[0], v.pos[2], v.yaw, (0.5F * s.wheelBase + s.bodyOverhang) * v.scale,
                        v.speed});
  }
  if (vehicleDriver_ < 0 && PLAYER_INDEX >= 0)
    tf_.obst.push_back({players[0].x, players[0].z, players[0].yaw * 57.29578F, 0.4F, 0.0F});
  // Headlights: every traffic car switches them with the night (a cheap
  // test once a frame, the cars rewritten only when it flips).
  {
    const int night = trafficNight();
    if (night != tfLights_) {
      tfLights_ = night;
      // 2 = lamps and coronas without the projected pool (the hook in
      // renderVehicleGlow): a pool per moving car is what lights cost.
      for (int ci = 0; ci < (int)tfVehicle_.size(); ++ci)
        vehicles_[tfVehicle_[(size_t)ci]].lightsOn = night ? 2 : 0;
    }
  }
  const float R = TRAFFIC_RADIUS;
  int on = 0, atLine = 0;
  for (int ci = 0; ci < (int)tf_.cars.size(); ++ci) {
    TfCar& c = tf_.cars[(size_t)ci];
    if (!c.on) continue;
    const VehicleRt& v = vehicles_[tfVehicle_[(size_t)ci]];
    c.x = v.pos[0];
    c.z = v.pos[2];
    c.yaw = v.yaw;
    c.speed = v.speed;
    c.stopT = (v.speed > 0.3F || v.speed < -0.3F) ? 0.0F : c.stopT + dt;
    atLine += c.atLine;
    const float dx = v.pos[0] - fx, dz = v.pos[2] - fz;
    const float d = sqrtf(dx * dx + dz * dz);
    const float vx = v.pos[0] - cameraPosition.x, vz = v.pos[2] - cameraPosition.z;
    const float vd = sqrtf(vx * vx + vz * vz);
    const bool seen = vd < 160.0F && (vd < 6.0F || (vx * cfx + vz * cfz) > 0.4F * vd);
    const bool wrecked = VEHICLE_DEFS[v.def].damageMechanical > 0.5F && v.damage >= 0.999F;
    if (d > R * 1.25F || (!seen && (c.stopT > 45.0F || wrecked))) {
      trafficRemove(ci);
      continue;
    }
    ++on;
  }
  tfLenT_ -= dt;
  if (tfLenT_ <= 0.0F) {
    tfLenT_ = 1.0F;
    tfLaneNear_ = tf_.laneLengthNear(fx, fz, R);
  }
  int target = (int)(TRAFFIC_DENSITY * tfLaneNear_ / 100.0F + 0.5F);
  if (target > (int)tf_.cars.size()) target = (int)tf_.cars.size();
  tfTarget_ = target;
  if (on < target) {
    float s = 0.0F;
    const int seg = tf_.spawnPick(tfRng_, fx, fz, cfx, cfz, 0.45F, R * 0.4F, R, 14.0F, &s);
    if (seg >= 0) {
      float p[5];
      tf_.g.at(seg, s, p);
      if (trafficGroundReady(p[0], p[2]))
        for (int ci = 0; ci < (int)tf_.cars.size(); ++ci)
          if (!tf_.cars[(size_t)ci].on) {
            trafficPlace(ci, seg, s);
            ++on;
            break;
          }
    }
  }
  trafficRedLight();
  tfCoreTicks_ += profTicks() - tfT0;
  tfLogT_ += dt;
  if (tfLogT_ >= 5.0F) {
    tfLogT_ = 0.0F;
    const int n = tfFrames_ > 0 ? tfFrames_ : 1;
    TYRA_LOG("TRAFFIC cars ", on, "/", target, " lane ", (int)tfLaneNear_, " spawned ", tfSpawned_,
             " recycled ", tfRecycled_, " at a line ", atLine, " red runs ", tfRedRuns_,
             " lane changes ", tf_.laneChanges, " overtakes ", tf_.overtakes,
             " lights ", tfLights_ > 0 ? 1 : 0,
             " clock ", (int)tf_.clock, " far ", tfKinematic_ / n,
             " core us/frame ", (int)(tfCoreTicks_ / 295U / (u32)n),
             " vehicles us/frame ", (int)(tfStepTicks_ / 295U / (u32)n));
    tfSpawned_ = tfRecycled_ = 0;
    tf_.laneChanges = tf_.overtakes = 0;
    tfCoreTicks_ = tfStepTicks_ = 0U;
    tfFrames_ = 0;
    tfKinematic_ = 0;
  }
}

// The player's car crossing a stop line on red: a log line, and the count,
// node and speed in ScriptContext that the On Red Light Run flow node fires
// on (a graph cannot call the game; it watches the count move).
void TerrainGame::trafficRedLight() {
  if (vehicleDriver_ < 0 || vehicleDriver_ >= vehicleCount_ || tfLines_.empty()) {
    tfRunLine_ = -1;
    return;
  }
  const VehicleRt& v = vehicles_[vehicleDriver_];
  const float hx = sinf(v.yaw * 0.017453293F), hz = cosf(v.yaw * 0.017453293F);
  // The nearest line across (a car straddling the centre line, or between two
  // lanes of its direction, still crosses the line of the lane nearest it).
  int best = -1;
  float bestAlong = 0.0F, bestLat = 1e9F;
  for (int k = 0; k < (int)tfLines_.size(); ++k) {
    const TfSeg& G = tf_.g.segs[(size_t)tfLines_[(size_t)k].lane];
    const float* e = &tf_.g.pts[(size_t)(G.first + G.count - 1) * 3];
    const float* q = &tf_.g.pts[(size_t)(G.first + G.count - 2) * 3];
    float tx = e[0] - q[0], tz = e[2] - q[2];
    const float tl = sqrtf(tx * tx + tz * tz);
    if (tl < 1e-4F) continue;
    tx /= tl;
    tz /= tl;
    const float rx = v.pos[0] - e[0], rz = v.pos[2] - e[2];
    const float along = rx * tx + rz * tz, lat = rx * tz - rz * tx;
    const float al = lat < 0.0F ? -lat : lat;
    if (along > -8.0F && along < 4.0F && al < 4.5F && al < bestLat && hx * tx + hz * tz > 0.6F) {
      best = k;
      bestAlong = along;
      bestLat = al;
    }
  }
  if (best >= 0 && best == tfRunLine_ && tfRunAlong_ < 0.0F && bestAlong >= 0.0F) {
    const TfLine& l = tfLines_[(size_t)best];
    const int lt = tf_.g.light(l.node, l.group, tf_.clock);
    TYRA_LOG("TRAFFIC player crossed the line at node ", l.node, " on ",
             lt == 0 ? "green" : (lt == 1 ? "amber" : "red"));
    if (lt == 2) {
      ++tfRedRuns_;
      ++scriptCtx.redLightRuns;
      scriptCtx.redLightNode = l.node;
      scriptCtx.redLightSpeed = v.speed > 0.0F ? v.speed : -v.speed;
      TYRA_LOG("TRAFFIC red light run ", tfRedRuns_, " at node ", l.node, " speed ",
               (int)(scriptCtx.redLightSpeed * 10.0F) / 10.0F);
    }
  }
  tfRunLine_ = best;
  tfRunAlong_ = bestAlong;
}

// A traffic car's pedals and wheel, from the core - in place of the pad's or
// the waypoint AI's, so the sim, the walls and the damage are untouched.
void TerrainGame::trafficDrive(int vi, float& throttle, float& brake, float& steer) {
  const int ci = tfCarOf_[(size_t)vi];
  TfCar& c = tf_.cars[(size_t)ci];
  const VehicleRt& v = vehicles_[vi];
  const VehicleDefData& s = VEHICLE_DEFS[v.def];
  c.x = v.pos[0];
  c.z = v.pos[2];
  c.yaw = v.yaw;
  c.speed = v.speed;
  c.half = (0.5F * s.wheelBase + s.bodyOverhang) * v.scale;
  c.halfW = (0.5F * s.track + 0.35F) * v.scale;
  c.brake = s.brakeDecel;
  if (s.damageMechanical > 0.5F && v.damage >= 0.999F) {
    throttle = 0.0F;
    brake = 1.0F;
    steer = 0.0F;
    return;
  }
  const u32 t0 = profTicks();
  tf_.drive(ci, &throttle, &brake, &steer);
  tfCoreTicks_ += profTicks() - t0;
}

// FAR CARS (docs/traffic.md "What it costs"): a traffic car this far from
// the camera is drawn by its far tier, a few pixels tall - so it skips the
// vehicle sim (tyres, suspension, walls) and slides along its lane at the
// speed the core's pedals ask for, glued to the lane's own height. The full
// sim takes over again as it comes near; the lane keeps the two consistent.
bool TerrainGame::trafficKinematic(int vi, float dt, float throttle, float brake) {
  VehicleRt& v = vehicles_[vi];
  const VehicleDefData& s = VEHICLE_DEFS[v.def];
  const float dx = v.pos[0] - cameraPosition.x, dz = v.pos[2] - cameraPosition.z;
  const float nearD = s.trafficDistance + 30.0F > 45.0F ? s.trafficDistance + 30.0F : 45.0F;
  if (dx * dx + dz * dz < nearD * nearD) return false;
  TfCar& c = tf_.cars[(size_t)tfCarOf_[(size_t)vi]];
  float spd = v.speed > 0.0F ? v.speed : 0.0F;
  if (brake > 0.01F) spd -= s.brakeDecel * 0.6F * brake * dt;
  else if (throttle > 0.01F) spd += s.accel * throttle * dt;
  if (spd < 0.0F) spd = 0.0F;
  float p[5];
  tf_.pose(c, spd * dt, p);  // across a lane change in flight, blended
  const float SC = v.scale;
  v.pos[0] = p[0];
  v.pos[1] = p[1] + s.rideHeight * SC;
  v.pos[2] = p[2];
  if (spd > 0.05F) v.yaw = atan2f(p[3], p[4]) * 57.29578F;
  v.speed = spd;
  v.lateral = 0.0F;
  v.velY = 0.0F;
  v.pitch = v.roll = v.leanPitch = v.leanRoll = 0.0F;
  v.pitchVel = v.rollVel = 0.0F;
  v.grounded = 1;
  for (int k = 0; k < 4; ++k) v.wheelY[k] = p[1];
  ++tfKinematic_;
  if (v.object >= 0 && v.object < (int)runtimeObjects.size()) {
    RuntimeObject& o = runtimeObjects[v.object];
    o.data.position[0] = v.pos[0];
    o.data.position[1] = v.pos[1];
    o.data.position[2] = v.pos[2];
    vehBodyRotation(0.0F, v.yaw, 0.0F, o.data.rotation);
    if (vehSubStepMore_) {
      v.objMatPending = true;
    } else if (o.onMatrixPath) {
      updateObjMat(v.object);
      v.objMatPending = false;
    } else {
      o.dirty = true;
      v.objMatPending = false;
    }
  }
  return true;
}

// The lit lens of every signal head in reach: one quad (both windings) per
// head over its baked, unlit lens. Rewritten only when a light changes or a
// head comes into reach - otherwise the bag replays its stream.
void TerrainGame::renderTrafficLamps() {
  if (tfLamps_.empty()) return;
  unsigned int sig = 2166136261U;
  int count = 0;
  int pick[kTfLampMax];
  int lens[kTfLampMax];
  for (int k = 0; k < (int)tfLamps_.size() && count < kTfLampMax; ++k) {
    const TfLamp& l = tfLamps_[(size_t)k];
    const float dx = l.x - cameraPosition.x, dz = l.z - cameraPosition.z;
    if (dx * dx + dz * dz > 95.0F * 95.0F) continue;
    const int st = tf_.g.light(l.node, l.group, tf_.clock);
    pick[count] = k;
    lens[count] = st == 2 ? 0 : (st == 1 ? 1 : 2);
    sig = (sig ^ (unsigned int)(k * 4 + lens[count])) * 16777619U;
    ++count;
  }
  if (count <= 0) return;
  if (sig != tfLampSig_ || count != tfLampCount_) {
    tfLampSig_ = sig;
    tfLampCount_ = count;
    for (int i = 0; i < count; ++i) {
      const TfLamp& l = tfLamps_[(size_t)pick[i]];
      const int k = lens[i];
      const float sc = l.scale;
      const float h = (TRAFFIC_LENS_HALF + 0.01F) * sc;
      const float cz = (TRAFFIC_LENS_Z + 0.012F) * sc;
      const float cx = l.x + l.fx * cz, cy = l.y + TRAFFIC_LENS_Y[k] * sc, czz = l.z + l.fz * cz;
      const float rx = l.fz * h, rz = -l.fx * h;
      auto g = tfLampVerts_.span((size_t)i * 12, 12);
      auto c = tfLampCols_.span((size_t)i * 12, 12);
      g[0].set(cx - rx, cy - h, czz - rz, 1.0F);
      g[1].set(cx + rx, cy - h, czz + rz, 1.0F);
      g[2].set(cx + rx, cy + h, czz + rz, 1.0F);
      g[3] = g[0];
      g[4] = g[2];
      g[5].set(cx - rx, cy + h, czz - rz, 1.0F);
      for (int j = 0; j < 6; ++j) g[6 + j] = g[5 - j];
      const Tyra::Color col = k == 0 ? Tyra::Color(255.0F, 40.0F, 28.0F, 128.0F)
                            : (k == 1 ? Tyra::Color(255.0F, 175.0F, 30.0F, 128.0F)
                                      : Tyra::Color(70.0F, 255.0F, 120.0F, 128.0F));
      for (int j = 0; j < 12; ++j) c[j] = col;
    }
    if (tfLampBag_) tfLampBag_->bboxVersion = ++g_bboxStamp;
  }
  if (!tfLampBag_) {
    tfLampInfo_ = std::make_unique<StaPipInfoBag>();
    tfLampInfo_->model = &model;
    tfLampInfo_->shadingType = TyraShadingGouraud;
    tfLampInfo_->fullClipChecks = true;
    tfLampInfo_->frustumCulling = PipelineInfoBagFrustumCulling_Precise;
    tfLampInfo_->zTestType = PipelineZTest_Standard;
    tfLampColorBag_ = std::make_unique<StaPipColorBag>();
    tfLampCols_.bind(tfLampColorBag_);
    tfLampBag_ = std::make_unique<StaPipBag>();
    tfLampBag_->info = tfLampInfo_.get();
    tfLampBag_->color = tfLampColorBag_.get();
    tfLampBag_->lighting = nullptr;
    tfLampBag_->texture = nullptr;
    tfLampVerts_.bind(tfLampBag_);
    tfLampBag_->bboxVersion = ++g_bboxStamp;
  }
  tfLampBag_->count = (u32)(tfLampCount_ * 12);
  stapip.core.render(tfLampBag_.get());
}
)TFIMPL";
    s = replaceOnce(s, "{{TF_READY}}",
                    streamed ? "return roadStreamReady(x, z);" : "(void)x;\n  (void)z;\n  return true;");
    return s;
}

// The hooks in the vehicle runtime. Each anchor is unique in that template;
// --vehicle-check "road traffic" generates a traffic project and proves every
// hook landed.
std::string patchTemplate(std::string s) {
    // 1. After setupVehicles has made every car (the appended ones included).
    s = replaceOnce(s,
                    "        warm(&vehTutPanel_);\n"
                    "      }\n"
                    "    }\n"
                    "  }\n"
                    "}\n",
                    "        warm(&vehTutPanel_);\n"
                    "      }\n"
                    "    }\n"
                    "  }\n"
                    "  trafficSetup(scene);  // road traffic (docs/traffic.md)\n"
                    "}\n");
    // 2. Once a frame, before the sub-steps.
    s = replaceOnce(s, "  vehFrameDt_ = dt;\n",
                    "  vehFrameDt_ = dt;\n"
                    "  tfStepT0_ = profTicks();\n"
                    "  trafficFrame(dt);  // road traffic (docs/traffic.md)\n");
    s = replaceOnce(s, "  updateVehicleTutorial(dt);\n}\n",
                    "  updateVehicleTutorial(dt);\n"
                    "  tfStepTicks_ += profTicks() - tfStepT0_;\n"
                    "}\n");
    // 3. A traffic car takes its inputs from the traffic core.
    s = replaceOnce(s,
                    "    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {\n",
                    "    } else if (v.wpFirst == -2 && vi < (int)tfCarOf_.size() && tfCarOf_[(size_t)vi] >= 0) {\n"
                    "      // ROAD TRAFFIC (docs/traffic.md): the lane graph's driver fills the\n"
                    "      // same four numbers.\n"
                    "      trafficDrive(vi, inThrottle, inBrake, inSteer);\n"
                    "    } else if (v.wpCount > 0 && (s.damageMechanical <= 0.5F || v.damage < 0.999F)) {\n");
    // 5. A far traffic car takes the cheap path (trafficKinematic) once its
    //    pedals are known and before the sim proper.
    s = replaceOnce(s,
                    "      continue;\n"
                    "    }\n"
                    "\n"
                    "    // Steering, with the lock shrinking toward top speed",
                    "      continue;\n"
                    "    }\n"
                    "    if (v.wpFirst == -2 && tfCarOf_[(size_t)vi] >= 0 &&\n"
                    "        trafficKinematic(vi, dt, inThrottle, inBrake))\n"
                    "      continue;  // road traffic, far from the camera (docs/traffic.md)\n"
                    "\n"
                    "    // Steering, with the lock shrinking toward top speed");
    // 4. A traffic car waiting at a red light must not fall asleep (a
    //    sleeping car skips its step, and it would never pull away).
    s = replaceOnce(s, "        vi != vehicleDriver_ && v.wpCount <= 0 && v.grounded &&\n",
                    "        vi != vehicleDriver_ && v.wpCount <= 0 && v.wpFirst != -2 && v.grounded &&\n");
    // 6. Headlights: lightsOn 2 is a traffic car at night - lamps and coronas
    //    lit, but no projected headlight pool (measured: six pools cost
    //    1.1-1.6 ms of render in PCSX2, docs/traffic.md "What it costs").
    s = replaceOnce(s, "    if (v.lightsOn > 0 && !(v.lampBroken & 1) && flashGoboTex &&\n",
                    "    if (v.lightsOn == 1 && !(v.lampBroken & 1) && flashGoboTex &&  // 2 = traffic: no pool\n");
    return s;
}

const char* const kHookMarks[6] = {"    if (v.lightsOn == 1 && !(v.lampBroken & 1) && flashGoboTex &&",
                                   "        trafficKinematic(vi, dt, inThrottle, inBrake))",
                                   "  trafficSetup(scene);  // road traffic",
                                   "  trafficFrame(dt);  // road traffic",
                                   "      trafficDrive(vi, inThrottle, inBrake, inSteer);",
                                   "v.wpCount <= 0 && v.wpFirst != -2 && v.grounded"};

}  // namespace roadlanes
