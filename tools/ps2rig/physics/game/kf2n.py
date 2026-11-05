"""game/kf2n.py ARM - on top of game/kf.py: the crash frames taken apart.
Silences the debug log lines a crash prints (VEHDMG x5 sites, VEHPAINT,
VEHLOD - each ~0.6 ms over ps2link in a debug build, compiled out in
release) and records per frame: time in vehicleDamageCapture / DentApply /
PiecesCollapse / DetachPiece, number of pieces detached, time in
renderObjectBag for the three cars, and the engine's full re-bakes, patch
rebakes and payload refreshes."""
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


edit('inc/game_runtime.gen.hpp', [
    ("struct Rec { unsigned short v[11]; };\n",
     "struct Rec { unsigned short v[20]; };\n"
     "inline u32 cap = 0, dentA = 0, coll = 0, det = 0, nDet = 0, carR = 0;\n"
     "inline int carObj[3] = {-2, -2, -2};\n"
     "inline u32 reb0 = 0, pat0 = 0, ref0 = 0;\n"
     "struct T { u32& a; u32 t; explicit T(u32& x) : a(x), t(Tyra::FrameProfile::ticks()) {}\n"
     "  ~T() { a += Tyra::FrameProfile::ticks() - t; } };\n", 1),
])

V = 'src/gen/game_vehicles.gen.cpp'
edit(V, [
    ("bool TerrainGame::vehicleDamageCapture(int vi) {\n",
     "bool TerrainGame::vehicleDamageCapture(int vi) {\n  kf::T kft_(kf::cap);\n", 1),
    ("void TerrainGame::vehiclePiecesCollapse(int vi) {\n",
     "void TerrainGame::vehiclePiecesCollapse(int vi) {\n  kf::T kft_(kf::coll);\n", 1),
    ("void TerrainGame::vehicleDetachPiece(int vi, int row, const float* dent) {\n",
     "void TerrainGame::vehicleDetachPiece(int vi, int row, const float* dent) {\n  kf::T kft_(kf::det); ++kf::nDet;\n", 1),
    ("int TerrainGame::vehicleDentApply(int vi, const float* dent) {\n",
     "int TerrainGame::vehicleDentApply(int vi, const float* dent) {\n  kf::T kft_(kf::dentA);\n", 1),
    ('TYRA_LOG("VEHDMG ', 'if (false) TYRA_LOG("VEHDMG ', 3),
    ('TYRA_LOG("VEHLOD car ', 'if (false) TYRA_LOG("VEHLOD car ', 1),
])
edit('inc/game_runtime.gen.hpp', [
    ('TYRA_LOG("VEHPAINT normals ', 'if (false) TYRA_LOG("VEHPAINT normals ', 1)])
edit('src/gen/game_scene.gen.cpp', [
    ("void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n",
     "void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n"
     "  u32 kfDummy_ = 0;\n"
     "  kf::T kft_((owner == kf::carObj[0] || owner == kf::carObj[1] || owner == kf::carObj[2]) ? kf::carR : kfDummy_);\n", 1)])

edit('src/terrain_game.cpp', [
    ("      r.v[10] = (unsigned short)f_;\n",
     "      r.v[10] = (unsigned short)f_;\n"
     "      r.v[11] = q(kf::cap); r.v[12] = q(kf::dentA); r.v[13] = q(kf::coll); r.v[14] = q(kf::det);\n"
     "      r.v[15] = (unsigned short)kf::nDet; r.v[16] = q(kf::carR);\n"
     "      r.v[17] = (unsigned short)(Tyra::StaPipBakedStreams::probeRebuild - kf::reb0);\n"
     "      r.v[18] = (unsigned short)(Tyra::StaPipBakedStreams::probePatch - kf::pat0);\n"
     "      r.v[19] = (unsigned short)(Tyra::StaPipBakedStreams::probeRefresh - kf::ref0);\n", 1),
    ("    kf::phys = kf::veh = kf::dmg = kf::fx = kf::scene = 0;\n",
     "    kf::phys = kf::veh = kf::dmg = kf::fx = kf::scene = 0;\n"
     "    kf::cap = kf::dentA = kf::coll = kf::det = kf::nDet = kf::carR = 0;\n"
     "    kf::reb0 = Tyra::StaPipBakedStreams::probeRebuild; kf::pat0 = Tyra::StaPipBakedStreams::probePatch;\n"
     "    kf::ref0 = Tyra::StaPipBakedStreams::probeRefresh;\n"
     "    for (int c_ = 0; c_ < 3; ++c_) kf::carObj[c_] = c_ < vehicleCount_ ? vehicles_[c_].object : -2;\n", 1),
    ("r.v[10], r.v[0], r.v[1], r.v[2], r.v[3], r.v[4], r.v[5], r.v[6], r.v[7], r.v[8], r.v[9]);",
     "r.v[10], r.v[0], r.v[1], r.v[2], r.v[3], r.v[4], r.v[5], r.v[6], r.v[7], r.v[8], r.v[9]);\n"
     "          o_ += snprintf(l_ + o_, sizeof(l_) - o_, \",%u,%u,%u,%u,%u,%u,%u,%u,%u\", r.v[11], r.v[12], r.v[13], r.v[14], r.v[15], r.v[16], r.v[17], r.v[18], r.v[19]);", 1),
    ("for (int b = 0; b < 1000; b += 20) {\n        char l_[900];",
     "for (int b = 0; b < 1000; b += 10) {\n        char l_[900];", 1),
    ("for (int j = b; j < b + 20; ++j) {", "for (int j = b; j < b + 10; ++j) {", 1),
])
print('kf2 probe ok')
