"""game/phys2.py ARM - on top of game/physlaps.py: physics sub-laps.
terr (terrain samples), solveSetup, euler (rbMatToEuler), objMat (updateObjMat
calls), bpRef (broadPhaseRefresh time + calls), bpQ (world box queries)."""
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
    "namespace kf {\n",
    "namespace omlap { inline u64 t = 0, n = 0, bp = 0, bpn = 0, bq = 0; }\n"
    "struct OmT { u64& a; u64 t0; explicit OmT(u64& x) : a(x), t0(Tyra::FrameProfile::ticks()) {}\n"
    "  ~OmT() { a += Tyra::FrameProfile::ticks() - t0; } };\n"
    "namespace kf {\n", 1)])

edit('src/gen/game_collision.gen.cpp', [
    ("void TerrainGame::updateObjMat(int index) {\n",
     "void TerrainGame::updateObjMat(int index) {\n  OmT omt_(omlap::t); ++omlap::n;\n", 1),
    ("void TerrainGame::broadPhaseRefresh() const {\n",
     "void TerrainGame::broadPhaseRefresh() const {\n  OmT bpt_(omlap::bp); ++omlap::bpn;\n", 1),
    ("  worldBroadPhase.queryBox(area, out);\n}\n",
     "  { OmT bqt_(omlap::bq); worldBroadPhase.queryBox(area, out); }\n}\n", 1),
])

edit('src/gen/game_physics.gen.cpp', [
    ("namespace physlap { inline u64 t[6];", "namespace physlap { inline u64 terr, setup, euler; inline u64 t[6];", 1),
    ("    const float hC = terrainHeightAt(x1.x, x1.z);\n",
     "    const u64 tT_ = Tyra::FrameProfile::ticks();\n    const float hC = terrainHeightAt(x1.x, x1.z);\n", 1),
    ("      if (n) emit(si, -1, cand, n);\n    }\n\n    // Aiming into a linked opening",
     "      if (n) emit(si, -1, cand, n);\n    }\n    physlap::terr += Tyra::FrameProfile::ticks() - tT_;\n\n    // Aiming into a linked opening", 1),
    ("  // The passes run on packed copies of the bodies",
     "  physlap::setup += Tyra::FrameProfile::ticks() - lap_;\n  // The passes run on packed copies of the bodies", 1),
    ("    rbMatToEuler(R, rot);\n",
     "    { const u64 tE_ = Tyra::FrameProfile::ticks(); rbMatToEuler(R, rot); physlap::euler += Tyra::FrameProfile::ticks() - tE_; }\n", 1),
    ("write=%.3f awake", "write=%.3f terr=%.3f solveSetup=%.3f euler=%.3f objMat=%.3f objMatN=%.1f bpRefresh=%.3f bpRefreshN=%.1f bpQuery=%.3f awake", 1),
    ("physlap::t[3]*k, physlap::t[4]*k,",
     "physlap::t[3]*k, physlap::t[4]*k, physlap::terr*k, physlap::setup*k, physlap::euler*k, omlap::t*k, omlap::n/50.0, omlap::bp*k, omlap::bpn/50.0, omlap::bq*k,", 1),
    ("    char line[200];\n", "    char line[480];\n", 1),
    ("    for (auto& v : physlap::t) v = 0;\n",
     "    for (auto& v : physlap::t) v = 0;\n    physlap::terr = physlap::setup = physlap::euler = 0;\n"
     "    omlap::t = omlap::n = omlap::bp = omlap::bpn = omlap::bq = 0;\n", 1),
])
print('phys2 probe ok')
