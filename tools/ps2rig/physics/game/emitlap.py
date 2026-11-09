"""game/emitlap.py ARM - the terrain strip emitter (emitAt) taken apart, KFL
(0.01 ms unless noted): 36 shadeAtF, 37 hAtE, 38 the pushes (vertices,
colors, sts, emisCols, layers), 39 whole strip loop, 40 vertices emitted
(x0.01 = count/10), 41 builds (x0.01), 42 repeatAt calls (x0.01 = /10).
Not with SLAP/GW/REB/TLAP/CLAP/CID."""
import sys
root = sys.argv[1]
T = 'Tyra::FrameProfile::ticks()'
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('    auto emitAt = [&](int ix, int iz) {\n'
    '      const float wx = startX + ix * stepX;\n'
    '      const float wz = startZ + iz * stepZ;\n'
    '      const V3 s = shadeAtF(ix, iz);\n'
    '      ch.vertices.push_back(Vec4(wx, hAtE(ix, iz), wz, 1.0F));\n',
    '    auto emitAt = [&](int ix, int iz) {\n'
    '      const float wx = startX + ix * stepX;\n'
    '      const float wz = startZ + iz * stepZ;\n'
    '      const u32 e0_ = %s;\n'
    '      const V3 s = shadeAtF(ix, iz);\n'
    '      const u32 e1_ = %s;\n'
    '      const float hy_ = hAtE(ix, iz);\n'
    '      const u32 e2_ = %s;\n'
    '      kf::labNow[36] += e1_ - e0_; kf::labNow[37] += e2_ - e1_; kf::labNow[40] += 295u;\n'
    '      ch.vertices.push_back(Vec4(wx, hy_, wz, 1.0F));\n' % (T, T, T))
# end of emitAt: the layer loop's closing brace is followed by the closing of the lambda
rep('        lp.sts.push_back(Vec4(wx * c.tu - c.offU, wz * c.tv - c.offV, 1.0F, 0.0F));\n'
    '      }\n'
    '    };\n',
    '        lp.sts.push_back(Vec4(wx * c.tu - c.offU, wz * c.tv - c.offV, 1.0F, 0.0F));\n'
    '      }\n'
    '      kf::labNow[38] += %s - e2_;\n'
    '    };\n' % T)
rep('    auto repeatAt = [&](size_t idx) {\n',
    '    auto repeatAt = [&](size_t idx) {\n      kf::labNow[42] += 295u;\n')
rep('    for (int z = gz0; z < gz1; z += lod) {\n'
    '      const int zN = z + lod > gz1 ? gz1 : z + lod;\n'
    '      // Each quad ROW is one strip;',
    '    const u32 sl0_ = %s; kf::labNow[41] += 2949u;\n'
    '    for (int z = gz0; z < gz1; z += lod) {\n'
    '      const int zN = z + lod > gz1 ? gz1 : z + lod;\n'
    '      // Each quad ROW is one strip;' % T)
rep('    while (!ch.vertices.empty() && runLen() % 3 != 0)\n'
    '      repeatAt(ch.vertices.size() - 1);\n',
    '    while (!ch.vertices.empty() && runLen() % 3 != 0)\n'
    '      repeatAt(ch.vertices.size() - 1);\n'
    '    kf::labNow[39] += TICKS - sl0_;\n'.replace('TICKS', T))
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('emitlap ok')
