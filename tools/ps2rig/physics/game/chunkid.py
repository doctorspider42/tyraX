"""game/chunkid.py ARM - which terrain chunk buildTerrainChunk rebuilt, KFL:
36 chunk id cx*100+cz+1, 37 lod*1000+lodW*100+lodE*10+lodN, 38 lodS,
39 build time (0.01 ms), 40 builds this frame (x0.01), 41 focus x+1000,
42 focus z+1000 (x0.01 = units). Not with SLAP/GW/REB/TLAP/CLAP."""
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
    '  const u32 cid0_ = %s; kf::labNow[40] += 2949u;\n' % T)
rep('  // World AABB of the built mesh',
    '  kf::labNow[36] = 2949u * (u32)(cx * 100 + cz + 1);\n'
    '  kf::labNow[37] = 2949u * (u32)(ch.lod * 1000 + ch.lodW * 100 + ch.lodE * 10 + ch.lodN);\n'
    '  kf::labNow[38] = 2949u * (u32)ch.lodS;\n'
    '  kf::labNow[39] += %s - cid0_;\n'
    '  kf::labNow[41] = 2949u * (u32)(terrainLodFocus[0][0] + 1000.0F);\n'
    '  kf::labNow[42] = 2949u * (u32)(terrainLodFocus[0][1] + 1000.0F);\n'
    '  // World AABB of the built mesh' % T)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('chunkid ok')
