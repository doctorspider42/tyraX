"""game/batchlap.py ARM - KFL slots: 36 rebuildStaticBatch time, 37 batch
submitHeavy time, 38 rebuild count (x0.01), 39 appended members (x0.01)."""
import sys
root = sys.argv[1]
p = root + '/src/gen/game_collision.gen.cpp'
s = open(p, encoding='utf-8').read()
a = '    if (stale) rebuildStaticBatch(b);\n'
assert s.count(a) == 1
s = s.replace(a, '    if (stale) { const u32 t0_ = Tyra::FrameProfile::ticks(); rebuildStaticBatch(b); kf::labNow[36] += Tyra::FrameProfile::ticks() - t0_; kf::labNow[38] += 29; }\n')
a = '    submitHeavy(b.bag.get());\n  }\n}\n'
assert s.count(a) == 1, s.count(a)
s = s.replace(a, '    { const u32 t1_ = Tyra::FrameProfile::ticks(); submitHeavy(b.bag.get()); kf::labNow[37] += Tyra::FrameProfile::ticks() - t1_; }\n  }\n}\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('batchlap ok')
