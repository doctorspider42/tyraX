"""game/chunklap.py ARM - buildTerrainChunk in phases, KFL slots (0.01 ms):
36 setup (minY, emitters), 37 shadow fold, 38 reserve + layer setup,
39 vertex emission, 40 binds + AO/emissive passes, 41 ground-shadow pass,
42 package pin + tail, 43 builds (x0.01 = count). Not with SLAP/GW/REB/TLAP."""
import sys
root = sys.argv[1]
T = 'Tyra::FrameProfile::ticks()'
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {\n',
    'void TerrainGame::buildTerrainChunk(int slot, int cx, int cz) {\n'
    '  u32 cl_ = %s; kf::labNow[43] += 2949u;\n' % T)
marks = [
    ('  const bool foldOn = groundShadowFoldWanted(currentScene);\n', 36),
    ('  // Quads this chunk really emits', 37),
    ('  // Triangle strips (docs/model-pipeline.md', 38),
    ("  // Terrain lightmap STs: the chunk's world XZ", 39),
    ('  // Ground shadow map pass (docs/shadows.md', 40),
    ('  // Every pass above draws ch.vertices', 41),
    ('  // World AABB of the built mesh', 42),
]
for a, k in marks:
    rep(a, '  { const u32 n_ = %s; kf::labNow[%d] += n_ - cl_; cl_ = n_; }\n' % (T, k) + a)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('chunklap ok')
