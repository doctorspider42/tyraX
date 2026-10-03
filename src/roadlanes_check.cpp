// --vehicle-check "road traffic" (docs/traffic.md): the lane graph's
// properties and a host simulation of traffic through a signalised crossing,
// driven by the SAME core the console runs (src/traffic_core.inl) and the same
// vehicle sim (vehiclesim::step).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "project.hpp"
#include "roadfurniture.hpp"
#include "templates.hpp"
#include "roadlanes.hpp"
#include "vehiclesim.hpp"

namespace roadlanes {
namespace {

roadgen::CrossingRoad road(const char* id, std::vector<float> pts, float width) {
    roadgen::CrossingRoad r;
    r.id = id;
    r.points = std::move(pts);
    r.width = width;
    r.intersection = "res/materials/x.mtl";
    return r;
}

bool inPolygon(const std::vector<float>& ring, float x, float z, float tol) {
    const size_t n = ring.size() / 2;
    bool in = false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = ring[i * 2], zi = ring[i * 2 + 1], xj = ring[j * 2], zj = ring[j * 2 + 1];
        if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi) in = !in;
    }
    if (in) return true;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const float ax = ring[j * 2], az = ring[j * 2 + 1], bx = ring[i * 2], bz = ring[i * 2 + 1];
        const float dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
        float f = l2 > 0 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0.0f;
        f = std::clamp(f, 0.0f, 1.0f);
        if (std::hypot(ax + dx * f - x, az + dz * f - z) <= tol) return true;
    }
    return false;
}

struct Built {
    std::vector<roadgen::CrossingRoad> roads;
    roadgen::CrossingPlan plan;
    Graph g;
};
Built make(std::vector<roadgen::CrossingRoad> roads, bool signals, bool leftHand = false) {
    Built b;
    b.roads = std::move(roads);
    b.plan = roadgen::planCrossings(b.roads, {});
    std::vector<roadfurn::Settings> fs(b.roads.size());
    for (roadfurn::Settings& s : fs) s.signals = signals;
    std::vector<bool> sig(b.plan.crossings.size());
    for (size_t ci = 0; ci < sig.size(); ++ci)
        sig[ci] = roadfurn::nodeSignalled(b.plan.crossings[ci], b.roads, fs);
    Options o;
    o.leftHand = leftHand;
    b.g = build(b.roads, b.plan, sig, [](float, float) { return 0.0f; }, o);
    return b;
}

// Oriented boxes overlap (separating axis), cars as length x width boxes.
bool overlap(const float* a, const float* b) {
    // a/b: x, z, yaw (deg), half length, half width
    float ax[2][2], bx[2][2];
    auto axes = [](const float* c, float out[2][2]) {
        const float y = c[2] * 0.017453293f;
        out[0][0] = std::sin(y), out[0][1] = std::cos(y);
        out[1][0] = std::cos(y), out[1][1] = -std::sin(y);
    };
    axes(a, ax);
    axes(b, bx);
    const float dx = b[0] - a[0], dz = b[1] - a[1];
    const float (*all[4]) = {ax[0], ax[1], bx[0], bx[1]};
    for (const float* n : all) {
        auto rad = [&](const float* c, float (*e)[2]) {
            return c[3] * std::fabs(e[0][0] * n[0] + e[0][1] * n[1]) +
                   c[4] * std::fabs(e[1][0] * n[0] + e[1][1] * n[1]);
        };
        if (std::fabs(dx * n[0] + dz * n[1]) > rad(a, ax) + rad(b, bx)) return false;
    }
    return true;
}

}  // namespace

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- road traffic --\n");
    // 1. A straight two-way street: lanes keep right (left under left-hand
    // traffic), one per direction at 8 wide, two at 14.
    {
        Built b = make({road("st", {0, -50, 0, 50}, 8)}, false);
        int right = 0, wrong = 0;
        for (const Lane& l : b.g.lanes) {
            const float x = l.pts[3 * (l.pts.size() / 6)];
            ((l.dir > 0) == (x > 0.0f) ? right : wrong)++;
        }
        Built lh = make({road("st", {0, -50, 0, 50}, 8)}, false, true);
        int lright = 0;
        for (const Lane& l : lh.g.lanes)
            lright += (l.dir > 0) == (l.pts[3 * (l.pts.size() / 6)] < 0.0f);
        Built wide = make({road("st", {0, -50, 0, 50}, 14)}, false);
        std::printf("  straight: %zu lanes (%d on their side), left-hand %d/%zu, 14 wide %zu lanes, "
                    "dead ends %d\n",
                    b.g.lanes.size(), right, lright, lh.g.lanes.size(), wide.g.lanes.size(),
                    b.g.deadEnds);
        verdict(b.g.lanes.size() == 2 && wrong == 0 && right == 2,
                "a two-way street carries a lane each way, each on its own side");
        verdict(lright == 2, "left-hand traffic mirrors the lanes");
        verdict(wide.g.lanes.size() == 4, "a 14-unit street carries two lanes each way");
        verdict(b.g.deadEnds == 2 && b.g.conns.empty(),
                "both open ends are dead ends: their lanes stop there (the runtime recycles the car)");
    }
    // 2. A T: continuity, the stop line's side, priority = stop lines, curves
    // inside the patch.
    {
        Built b = make({road("main", {-60, 0, 60, 0}, 8), road("stem", {0, 0, 0, 60}, 8)}, false);
        roadgen::Surface s;
        for (const roadgen::CrossingRoad& r : b.roads) {
            std::vector<roadgen::Vertex> tris;
            roadgen::tessellate(r.points, r.width, [](float, float) { return 0.0f; }, tris);
            s.add(tris, r.grip);
        }
        roadgen::addCrossingsToSurface(s, b.roads, b.plan, [](float, float) { return 0.0f; });
        s.build();
        std::vector<roadgen::Vertex> paint;
        roadgen::bakeMarkings(b.plan, b.roads, s, paint);
        const roadgen::Crossing& c = b.plan.crossings[0];
        // Which arms carry a painted stop line: paint inside the line's box.
        std::vector<int> painted(c.armList.size(), 0);
        for (size_t ai = 0; ai < c.armList.size(); ++ai) {
            const roadgen::NodeArm& a = c.armList[ai];
            // A painted triangle whose centre lies where the stop line goes
            // (an edge line runs past the box's outer side).
            for (size_t t = 0; t + 2 < paint.size(); t += 3) {
                const float cx = (paint[t].x + paint[t + 1].x + paint[t + 2].x) / 3.0f;
                const float cz = (paint[t].z + paint[t + 1].z + paint[t + 2].z) / 3.0f;
                const float rx = cx - a.capX, rz = cz - a.capZ;
                const float al = rx * a.tx + rz * a.tz, la = rx * -a.tz + rz * a.tx;
                if (al >= -0.75f && al <= -0.2f && la >= 0.3f && la <= a.h - 0.8f) painted[ai] = 1;
            }
        }
        int noExit = 0, conns = 0, mismatch = 0, outside = 0, sideOk = 0, sideBad = 0;
        for (size_t li = 0; li < b.g.lanes.size(); ++li) {
            const Lane& l = b.g.lanes[li];
            if (l.toNode >= 0 && b.g.laneOut[li].empty()) ++noExit;
            // The lane reaching a stop line ends on the painted side.
            if (l.toNode >= 0 && painted[(size_t)l.toArm]) {
                const roadgen::NodeArm& a = c.armList[(size_t)l.toArm];
                const float rx = l.pts[l.pts.size() - 3] - a.capX, rz = l.pts[l.pts.size() - 1] - a.capZ;
                ((rx * -a.tz + rz * a.tx) > 0.0f ? sideOk : sideBad)++;
            }
        }
        for (const Connection& cn : b.g.conns) {
            if (cn.node < 0) continue;
            ++conns;
            if (cn.givesWay != (painted[(size_t)cn.arm] != 0)) ++mismatch;
            for (size_t k = 0; k < cn.pts.size(); k += 3)
                if (!inPolygon(c.shape.outline, cn.pts[k], cn.pts[k + 2], 0.35f)) {
                    ++outside;
                    break;
                }
        }
        std::printf("  T: %zu lanes, %d node connections, stop lines on %d arm(s), lanes at a line on "
                    "its side %d/%d, priority mismatches %d, curves leaving the patch %d\n",
                    b.g.lanes.size(), conns, painted[0] + painted[1] + painted[2], sideOk,
                    sideOk + sideBad, mismatch, outside);
        verdict(noExit == 0 && conns == 6, "every lane into the T has a legal exit (6 movements)");
        verdict(sideOk > 0 && sideBad == 0, "the lane that stops is the lane the stop line is painted on");
        verdict(mismatch == 0, "exactly the movements from a stop-lined arm give way");
        verdict(outside == 0, "every turn curve stays inside the node's patch");
    }
    // 3. A four-way crossing with lights: continuity, curves, the cycle.
    Built x = make({road("ew", {-70, 0, 70, 0}, 8), road("ns", {0, -70, 0, 70}, 8)}, true);
    {
        const roadgen::Crossing& c = x.plan.crossings[0];
        int noExit = 0, conns = 0, outside = 0;
        for (size_t li = 0; li < x.g.lanes.size(); ++li)
            if (x.g.lanes[li].toNode >= 0 && x.g.laneOut[li].empty()) ++noExit;
        for (const Connection& cn : x.g.conns) {
            if (cn.node < 0) continue;
            ++conns;
            for (size_t k = 0; k < cn.pts.size(); k += 3)
                if (!inPolygon(c.shape.outline, cn.pts[k], cn.pts[k + 2], 0.35f)) {
                    ++outside;
                    break;
                }
        }
        verdict(x.g.nodes[0].signalled && noExit == 0 && conns == 12 && outside == 0,
                "a signalled crossing: 12 movements, every lane has an exit, curves stay inside");
        TfGraph tg;
        toSim(x.g, Options(), 12.0f, 3.0f, 2.0f, tg);
        // No conflicting greens: the two phases are never both non-red, and two
        // crossing movements allowed at once always have a priority between them.
        int bothGo = 0, equalConflict = 0;
        const int L = (int)x.g.lanes.size();
        for (float t = 0.0f; t < 2.0f * tg.cycle(); t += 0.25f) {
            if (tg.light(0, 0, t) != 2 && tg.light(0, 1, t) != 2) ++bothGo;
            for (size_t i = 0; i < x.g.conns.size(); ++i) {
                const Connection& a = x.g.conns[i];
                if (a.node < 0 || tg.light(0, a.group, t) == 2) continue;
                for (int j : a.conflicts) {
                    const Connection& b = x.g.conns[(size_t)j];
                    if (tg.light(0, b.group, t) != 2 && b.rank == a.rank) ++equalConflict;
                }
            }
        }
        (void)L;
        std::printf("  signals: cycle %.0f s, phases both moving %d of %d samples, crossing movements "
                    "of equal priority allowed together %d\n",
                    tg.cycle(), bothGo, (int)(2.0f * tg.cycle() / 0.25f), equalConflict);
        verdict(bothGo == 0, "the two signal phases are never green or amber together");
        verdict(equalConflict == 0,
                "movements that cross under the same green always have one that gives way");
    }
    // 4. The host simulation: N cars through the signalised crossing, each
    // turning back at the street ends, on vehiclesim with the console's core.
    {
        TfSim sim;
        toSim(x.g, Options(), 8.0f, 3.0f, 2.0f, sim.g);
        const int N = 10;
        sim.reset(N);
        vehiclesim::DriveSpec spec;
        std::vector<vehiclesim::DriveState> st((size_t)N);
        // Spread the cars over the lanes, a car length apart at least.
        for (int i = 0; i < N; ++i) {
            const int seg = sim.g.lanes[(size_t)i % sim.g.lanes.size()];
            const float s = 6.0f + 18.0f * (float)(i / (int)sim.g.lanes.size());
            sim.place(i, seg, s);
            float p[5];
            sim.g.at(seg, s, p);
            st[(size_t)i].pos[0] = p[0];
            st[(size_t)i].pos[1] = spec.rideHeight;
            st[(size_t)i].pos[2] = p[2];
            st[(size_t)i].yaw = std::atan2(p[3], p[4]) * 57.29578f;
            st[(size_t)i].grounded = true;
        }
        const float half = 0.5f * spec.wheelBase + spec.bodyOverhang;
        const float hw = 0.5f * spec.track + 0.35f;
        int collisions = 0, redEntries = 0, maxStop = 0, recycled = 0;
        float worstStop = 0.0f;
        std::vector<int> lastHold((size_t)N, -1);
        const float dt = 1.0f / 50.0f;
        const int steps = (int)(300.0f / dt);
        for (int k = 0; k < steps; ++k) {
            sim.clock += dt;
            for (int i = 0; i < N; ++i) {
                TfCar& c = sim.cars[(size_t)i];
                vehiclesim::DriveState& d = st[(size_t)i];
                c.x = d.pos[0], c.z = d.pos[2], c.yaw = d.yaw, c.speed = d.speed;
                c.half = half;
                c.brake = spec.brakeDecel;
                float th, br, sr;
                sim.drive(i, &th, &br, &sr);
                if (c.hold != lastHold[(size_t)i] && c.hold >= 0) {
                    const TfSeg& C = sim.g.segs[(size_t)c.hold];
                    if (sim.g.light(C.node, C.group, sim.clock) == 2) ++redEntries;
                }
                lastHold[(size_t)i] = c.hold;
                vehiclesim::DriveInput in;
                in.throttle = th;
                in.brake = br;
                in.steer = -sr;  // the core turns toward +yaw; DriveInput's right is -yaw
                vehiclesim::step(spec, in, dt, [](float, float) { return 0.0f; }, d);
                if (std::fabs(d.speed) < 0.3f) c.stopT += dt;
                else c.stopT = 0.0f;
                // At a dead end: recycled onto the start of a lane toward the
                // node, the runtime's out-of-view recycling.
                if (c.nxt < 0 && c.stopT > 1.0f) {
                    for (int tries = 0; tries < 64; ++tries) {
                        const int seg = sim.g.lanes[(size_t)(sim.rnd(c) % sim.g.lanes.size())];
                        if (sim.g.segs[(size_t)seg].nextCount <= 0) continue;
                        float p[5];
                        sim.g.at(seg, 3.0f, p);
                        bool clear = true;
                        for (int j = 0; j < N; ++j)
                            if (j != i && std::hypot(st[(size_t)j].pos[0] - p[0], st[(size_t)j].pos[2] - p[2]) < 12.0f)
                                clear = false;
                        if (!clear) continue;
                        sim.place(i, seg, 3.0f);
                        d = vehiclesim::DriveState();
                        d.pos[0] = p[0], d.pos[1] = spec.rideHeight, d.pos[2] = p[2];
                        d.yaw = std::atan2(p[3], p[4]) * 57.29578f;
                        d.grounded = true;
                        c.x = p[0], c.z = p[2], c.yaw = d.yaw, c.speed = 0.0f;
                        lastHold[(size_t)i] = -1;
                        ++recycled;
                        break;
                    }
                }
                if (c.nxt >= 0) worstStop = std::max(worstStop, c.stopT);
                if (std::getenv("TF_DEBUG") && c.stopT > 40.0f && c.stopT < 40.0f + dt * 1.5f) {
                    const TfSeg& S = sim.g.segs[(size_t)c.seg];
                    std::printf("    stuck t %.1f car %d seg %d kind %d s %.1f/%.1f nxt %d hold %d atLine %d gap %.1f light %d occ:",
                                k * dt, i, c.seg, S.kind, c.s, S.len, c.nxt, c.hold, c.atLine,
                                sim.leaderGap(i), c.nxt >= 0 ? sim.g.light(sim.g.segs[(size_t)c.nxt].node, sim.g.segs[(size_t)c.nxt].group, sim.clock) : -1);
                    if (c.nxt >= 0) {
                        const TfSeg& C = sim.g.segs[(size_t)c.nxt];
                        for (int q = 0; q < C.confCount; ++q) std::printf(" %d", sim.occ[(size_t)sim.g.conf[(size_t)(C.confFirst + q)]]);
                    }
                    std::printf("\n");
                    for (int j = 0; j < N; ++j)
                        std::printf("      car %d seg %d s %.1f nxt %d hold %d v %.1f pos %.1f,%.1f\n", j, sim.cars[(size_t)j].seg, sim.cars[(size_t)j].s,
                                    sim.cars[(size_t)j].nxt, sim.cars[(size_t)j].hold, st[(size_t)j].speed, st[(size_t)j].pos[0], st[(size_t)j].pos[2]);
                }
            }
            for (int i = 0; i < N; ++i)
                for (int j = i + 1; j < N; ++j) {
                    const float a[5] = {st[(size_t)i].pos[0], st[(size_t)i].pos[2], st[(size_t)i].yaw, half, hw};
                    const float b[5] = {st[(size_t)j].pos[0], st[(size_t)j].pos[2], st[(size_t)j].yaw, half, hw};
                    if (overlap(a, b)) {
                        if (collisions < 3 && std::getenv("TF_DEBUG"))
                            std::printf("    overlap t %.2f cars %d (seg %d s %.1f %.1f,%.1f yaw %.0f v %.1f) %d (seg %d s %.1f %.1f,%.1f yaw %.0f v %.1f)\n",
                                        k * dt, i, sim.cars[(size_t)i].seg, sim.cars[(size_t)i].s, a[0], a[1], a[2], st[(size_t)i].speed,
                                        j, sim.cars[(size_t)j].seg, sim.cars[(size_t)j].s, b[0], b[1], b[2], st[(size_t)j].speed);
                        ++collisions;
                    }
                }
        }
        int minPassed = 1 << 30, totalPassed = 0;
        float worstOff = 0.0f;
        for (int i = 0; i < N; ++i) {
            minPassed = std::min(minPassed, sim.cars[(size_t)i].passed);
            totalPassed += sim.cars[(size_t)i].passed;
            float p[5];
            sim.g.at(sim.cars[(size_t)i].seg, sim.cars[(size_t)i].s, p);
            worstOff = std::max(worstOff, std::hypot(p[0] - st[(size_t)i].pos[0], p[2] - st[(size_t)i].pos[2]));
        }
        (void)maxStop;
        std::printf("  simulation: %d cars, 300 s: %d movements through the node "
                    "(fewest per car %d), colliding steps %d, entries on red %d, longest standstill "
                    "%.1f s, worst distance off the lane %.2f, recycled at dead ends %d\n",
                    N, totalPassed, minPassed, collisions, redEntries, worstStop, worstOff, recycled);
        verdict(collisions == 0, "no two cars ever overlap");
        verdict(redEntries == 0, "no car takes a movement whose light is red");
        verdict(minPassed >= 4 && worstStop < 60.0f,
                "no deadlock: every car keeps moving through the node");
        verdict(worstOff < 1.5f, "cars follow their lanes");
    }
    // 5. The codegen: a traffic project gets the tables, the core, the cars
    // and all four hooks; with traffic off not one byte of it.
    {
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "tyrax-traffic-check";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        Project p;
        p.name = "tfcheck";
        p.dir = dir.string();
        p.scenes.emplace_back();
        SceneData& sc = p.scenes.back();
        sc.name = "main";
        for (int k = 0; k < 2; ++k) {
            SceneObject o;
            o.type = PrimitiveType::Road;
            o.id = k ? "ns" : "ew";
            o.name = o.id;
            o.roadPoints = k ? std::vector<float>{0, -60, 0, 60} : std::vector<float>{-60, 0, 60, 0};
            o.roadWidth = 8.0f;
            o.roadTexture = "res/materials/roads/road-2lane.mtl";
            o.roadIntersectionTexture = "res/materials/roads/road-junction.mtl";
            o.roadFurniture.signals = true;
            sc.objects.push_back(o);
        }
        VehicleDef v;
        v.id = "car000000001";
        v.name = "Car";
        v.modelPath = "res/models/car.glb";
        p.vehicles.push_back(v);
        auto gen = [&](int cars) {
            p.settings.traffic.cars = cars;
            std::string all;
            for (const auto& f : templates::generate(p)) all += f.content;
            return all;
        };
        const std::string on = gen(4);
        int hooks = 0;
        for (const char* m : kHookMarks) hooks += on.find(m) != std::string::npos;
        const bool tables = on.find("constexpr TrafficSegData TRAFFIC_SEGS[") != std::string::npos &&
                            on.find("constexpr TrafficLampData TRAFFIC_LAMPS[") != std::string::npos;
        const bool core = on.find("struct TfSim {") != std::string::npos &&
                          on.find("void TerrainGame::renderTrafficLamps()") != std::string::npos &&
                          on.find("renderTrafficLamps(); costEnd") != std::string::npos;
        std::printf("  codegen: hooks %d/5, tables %s, core %s\n", hooks, tables ? "yes" : "no",
                    core ? "yes" : "no");
        verdict(hooks == 5 && tables && core,
                "a traffic project gets the lane tables, the core and all five vehicle hooks");
        // Streamed roads (tables on disk by default): buildRoads is not
        // compiled there, and the traffic functions must still be.
        p.settings.roadStreamRadius = 150.0f;
        const std::string streamed = gen(4);
        p.settings.roadStreamRadius = 0.0f;
        verdict(streamed.find("void TerrainGame::trafficSetup(int scene)") != std::string::npos &&
                    streamed.find("return roadStreamReady(x, z);") != std::string::npos,
                "a streamed project gets the traffic functions, spawning only where roads are built");
        const std::string off = gen(0);
        verdict(off.find("TRAFFIC_") == std::string::npos && off.find("TfSim") == std::string::npos &&
                    off.find("trafficSetup") == std::string::npos,
                "traffic off generates not one byte of it");
        std::filesystem::remove_all(dir, ec);
    }
}

}  // namespace roadlanes
