"""game/reblap.py ARM - rebuildStaticBatch taken apart (apply AFTER
game/settlelap.py; overwrites its particle slots 36-39 and slot 43):
36 head (retire + copy of the members that stay), 37 emit (bake of appended /
all members), 38 tail (bind, pinPackageSize, aabb), 39 members emitted
(x0.01 = count), 43 batch vertices / 10 (x0.01). Settle 40/41/42 as before."""
import sys
root = sys.argv[1]
T = 'Tyra::FrameProfile::ticks()'
p = root + '/src/gen/game_collision.gen.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('void TerrainGame::rebuildStaticBatch(StaticBatch& b) {\n',
    'void TerrainGame::rebuildStaticBatch(StaticBatch& b) {\n  u32 rl_ = %s;\n' % T)
rep('  batchUsesStrips[(size_t)bi] = useStrips ? 1 : 0;\n',
    '  { const u32 n_ = %s; kf::labNow[36] += n_ - rl_; rl_ = n_; }\n'
    '  batchUsesStrips[(size_t)bi] = useStrips ? 1 : 0;\n' % T)
rep('  const size_t emitCount = patching ? patch.append.size() : b.members.size();\n',
    '  const size_t emitCount = patching ? patch.append.size() : b.members.size();\n'
    '  kf::labNow[39] += 2949u * (u32)emitCount;\n'
    '  { const u32 n_ = %s; kf::labNow[38] += n_ - rl_; rl_ = n_; }\n' % T)
rep('  g_primKd = nullptr;\n  g_primKe = nullptr;\n  g_primTextured = false;\n  g_primUvRect = nullptr;\n  g_aoOff = false;\n  g_prelitTex = false;\n  g_giProbeShade = false;\n  g_lampBakeHere = false;\n  if (b.vertices.empty()) {\n',
    '  { const u32 n_ = %s; kf::labNow[37] += n_ - rl_; rl_ = n_; }\n'
    '  g_primKd = nullptr;\n  g_primKe = nullptr;\n  g_primTextured = false;\n  g_primUvRect = nullptr;\n  g_aoOff = false;\n  g_prelitTex = false;\n  g_giProbeShade = false;\n  g_lampBakeHere = false;\n  if (b.vertices.empty()) {\n' % T)
rep('    if (v.z > b.aabbMax[2]) b.aabbMax[2] = v.z;\n  }\n}\n',
    '    if (v.z > b.aabbMax[2]) b.aabbMax[2] = v.z;\n  }\n'
    '  kf::labNow[38] += %s - rl_;\n  kf::labNow[43] += 2949u * (u32)(b.vertices.size() / 10);\n}\n' % T)
# settlelap counted refused prebakes in 43: drop that, 43 is ours now
rep('kf::labNow[ok_ ? 42 : 43] += 2949; }', 'if (ok_) kf::labNow[42] += 2949; }')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()
s = s.replace('kf::labNow[36] +=', '0u +').replace('kf::labNow[37] +=', '0u +') \
     .replace('kf::labNow[38] +=', '0u +').replace('kf::labNow[39] +=', '0u +')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('reblap ok')
