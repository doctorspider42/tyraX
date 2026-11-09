"""game/gpuwait.py ARM - KFL slots 36 ring-full spin, 37 waitFor, 38 drain, 39 GS tail wait
(0.01 ms, engine/gpuwait.py totals for the previous frame). Use with SLAP off."""
import sys
p = sys.argv[1] + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, a + '\n      for (int g_ = 0; g_ < 4; ++g_) kf::labRec[f_ - 200][36 + g_] = q(Tyra::FrameProfile::probeGpuWait[g_]);')
a = '    kf::phys = kf::veh = kf::dmg = kf::fx = kf::scene = 0;\n'
assert s.count(a) == 1
s = s.replace(a, a + '    for (int g_ = 0; g_ < 4; ++g_) Tyra::FrameProfile::probeGpuWait[g_] = 0;\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('gpuwait ok')
