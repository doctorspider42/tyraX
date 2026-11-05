"""game/terrlap.py ARM - renderTerrain per chunk, KFL slots (0.01 ms unless
noted): 36 chunks submitted (x0.01 = count), 37 slowest chunk's whole draw
(base + layers + AO + GS + emis), 38 that chunk's id cx*100+cz (x0.01),
39 time in the GS passes, 40 camera x+1000 (x0.01 = units), 41 camera z+1000,
42 camera y+1000, 43 view yaw in degrees+360 (x0.01). Not with SLAP/GW/REB."""
import sys
root = sys.argv[1]
T = 'Tyra::FrameProfile::ticks()'
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('    renderWorldBag(ch.bag.get());\n',
    '    const u32 tc0_ = %s; kf::labNow[36] += 2949u;\n'
    '    renderWorldBag(ch.bag.get());\n' % T)
rep('    if (ch.gsBag && ch.gsBag->count > 0 &&\n',
    '    const u32 tg0_ = %s;\n'
    '    if (ch.gsBag && ch.gsBag->count > 0 &&\n' % T)
rep('    if (ch.emisBag && ch.emisBag->count > 0)\n      renderWorldBag(ch.emisBag.get());\n  }\n',
    '    const u32 tg1_ = %s; kf::labNow[39] += tg1_ - tg0_;\n'
    '    if (ch.emisBag && ch.emisBag->count > 0)\n      renderWorldBag(ch.emisBag.get());\n'
    '    { const u32 d_ = %s - tc0_; if (d_ > kf::labNow[37]) { kf::labNow[37] = d_;\n'
    '        kf::labNow[38] = 2949u * (u32)(ch.cx * 100 + ch.cz); } }\n'
    '  }\n'
    '  kf::labNow[40] = 2949u * (u32)(cameraPosition.x + 1000.0F);\n'
    '  kf::labNow[41] = 2949u * (u32)(cameraPosition.z + 1000.0F);\n'
    '  kf::labNow[42] = 2949u * (u32)(cameraPosition.y + 1000.0F);\n'
    '  kf::labNow[43] = 2949u * (u32)(atan2f(cameraLookAt.x - cameraPosition.x, cameraLookAt.z - cameraPosition.z) * 57.29578F + 360.0F);\n' % (T, T))
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('terrlap ok')
