"""patch_kf.py ARM - crash fixture with a per-frame timing ring.
Scripted drive (frames 250..750: full throttle + nitrous, straight - the CR
crash into the parked car). Every frame 200..1199 records the previous
frame's FrameProfile terms (period, pre, work, stall) plus this loop's own
scopes (physics, stepVehicles, damage, debris+smoke+skids, renderScene) and
speed. Nothing is logged until frame 1300; then KF lines, 20 frames each, in
units of 0.01 ms."""
import sys, os
root = sys.argv[1]


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    for a, b, c in pairs:
        n = s.count(a)
        assert n == c, (rel, a[:60], n)
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


edit('inc/game_runtime.gen.hpp', [(
    "namespace ftrig {\n",
    "namespace kf {\n"
    "struct Rec { unsigned short v[11]; };\n"
    "inline Rec rec[1000];\n"
    "inline u32 phys = 0, veh = 0, dmg = 0, fx = 0, scene = 0;\n"
    "inline bool dumped = false;\n"
    "}  // namespace kf\n"
    "namespace ftrig {\n", 1)])

edit('src/gen/game_vehicles.gen.cpp', [
    ("    updateVehicleDamage(dt);\n",
     "    { const u32 d0_ = Tyra::FrameProfile::ticks(); updateVehicleDamage(dt); kf::dmg += Tyra::FrameProfile::ticks() - d0_; }\n", 1),
    ("      if (!vehSubStepRepeat_ && engine->pad.getClicked().DpadUp)",
     "      if (g_frameStamp > 250 && g_frameStamp < 750) { inThrottle = 1.0F; inBrake = 0.0F; inNos = 1; }\n"
     "      if (!vehSubStepRepeat_ && engine->pad.getClicked().DpadUp)", 1),
])

edit('src/terrain_game.cpp', [
    ("void TerrainGame::loop() {\n",
     "void TerrainGame::loop() {\n"
     "  {\n"
     "    const u32 f_ = g_frameStamp;\n"
     "    auto q = [](u32 t) -> unsigned short { const u32 v = t / 2949U; return v > 65535U ? 65535 : (unsigned short)v; };\n"
     "    if (f_ >= 200 && f_ < 1200) {\n"
     "      kf::Rec& r = kf::rec[f_ - 200];\n"
     "      r.v[0] = q(Tyra::FrameProfile::tPeriod); r.v[1] = q(Tyra::FrameProfile::tPre);\n"
     "      r.v[2] = q(Tyra::FrameProfile::tFrameWork); r.v[3] = q(Tyra::FrameProfile::tStall);\n"
     "      r.v[4] = q(kf::phys); r.v[5] = q(kf::veh); r.v[6] = q(kf::dmg); r.v[7] = q(kf::fx); r.v[8] = q(kf::scene);\n"
     "      const float spd = vehicleDriver_ >= 0 ? vehicles_[vehicleDriver_].speed : 0.0F;\n"
     "      r.v[9] = (unsigned short)(spd < 0.0F ? 0 : (spd > 600.0F ? 6000 : (int)(spd * 10.0F)));\n"
     "      r.v[10] = (unsigned short)f_;\n"
     "    }\n"
     "    kf::phys = kf::veh = kf::dmg = kf::fx = kf::scene = 0;\n"
     "    if (f_ >= 1300 && !kf::dumped) {\n"
     "      kf::dumped = true;\n"
     "      TYRA_LOG(\"KF fields: f period pre work stall phys veh dmg fx scene speed10 (0.01 ms)\");\n"
     "      for (int b = 0; b < 1000; b += 20) {\n"
     "        char l_[900]; int o_ = snprintf(l_, sizeof(l_), \"KF\");\n"
     "        for (int j = b; j < b + 20; ++j) {\n"
     "          const kf::Rec& r = kf::rec[j];\n"
     "          o_ += snprintf(l_ + o_, sizeof(l_) - o_, \" %u:%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\", r.v[10], r.v[0], r.v[1], r.v[2], r.v[3], r.v[4], r.v[5], r.v[6], r.v[7], r.v[8], r.v[9]);\n"
     "        }\n"
     "        TYRA_LOG(l_);\n"
     "      }\n"
     "    }\n"
     "  }\n", 1),
    ("    if (!menuActive) updateObjectPhysics();\n    ftrig::physOnly",
     "    if (!menuActive) updateObjectPhysics();\n    kf::phys += Tyra::FrameProfile::ticks() - phT;\n    ftrig::physOnly", 1),
    ("{ Tyra::HardwareTrace::Scope trace(\"Vehicles_update\"); stepVehicles(g_frameScale * (1.0F / 50.0F)); }",
     "{ Tyra::HardwareTrace::Scope trace(\"Vehicles_update\"); const u32 v0_ = Tyra::FrameProfile::ticks(); stepVehicles(g_frameScale * (1.0F / 50.0F)); kf::veh += Tyra::FrameProfile::ticks() - v0_; } const u32 fx0_ = Tyra::FrameProfile::ticks();", 1),
    ("updateVehicleDebris(g_frameScale * (1.0F / 50.0F)); }",
     "updateVehicleDebris(g_frameScale * (1.0F / 50.0F)); kf::fx += Tyra::FrameProfile::ticks() - fx0_; }", 1),
    ("      renderScene();\n",
     "      { const u32 s0_ = Tyra::FrameProfile::ticks(); renderScene(); kf::scene += Tyra::FrameProfile::ticks() - s0_; }\n", 3),
])
print('kf probe ok')
