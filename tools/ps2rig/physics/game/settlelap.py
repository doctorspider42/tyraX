"""game/settlelap.py ARM - KFL slots: 40 settle rebuild ms, 41 settle prebake
ms, 42 prebake ok (count, 0.01 = 1), 43 prebake refused (count), plus
36 particle systems ms, 37 glass ms, 38 skids ms, 39 smoke ms (do not combine
with game/batchlap.py)."""
import sys
root = sys.argv[1]
T = 'Tyra::FrameProfile::ticks()'
p = root + '/src/gen/game_collision.gen.cpp'
s = open(p, encoding='utf-8').read()
a = ('    rebuildStaticBatch(b);\n'
     '    if (b.bag && b.bag->count > 0) stapip.core.prebakeBag(b.bag.get());\n')
assert s.count(a) == 1
s = s.replace(a,
    '    { const u32 t0_ = %s; rebuildStaticBatch(b); kf::labNow[40] += %s - t0_; }\n'
    '    if (b.bag && b.bag->count > 0) { const u32 t1_ = %s;\n'
    '      const bool ok_ = stapip.core.prebakeBag(b.bag.get());\n'
    '      kf::labNow[41] += %s - t1_; kf::labNow[ok_ ? 42 : 43] += 2949; }\n' % (T, T, T, T))
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/gen/game_physics.gen.cpp'
s = open(p, encoding='utf-8').read()
for a, k in [('    if (ps.bag && ps.bag->count > 0) renderWorldBag(ps.bag.get());\n', 36),
             ('  renderVehicleGlass();\n', 37), ('  renderVehicleSkids();\n', 38),
             ('  renderVehicleSmoke();\n', 39)]:
    assert s.count(a) == 1, a
    ind = a[:len(a) - len(a.lstrip())]
    s = s.replace(a, '%s{ const u32 tq_ = %s;\n%s%s  kf::labNow[%d] += %s - tq_; }\n' % (ind, T, a, ind, k, T))
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('settlelap ok')
