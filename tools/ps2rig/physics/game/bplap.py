"""game/bplap.py ARM - broadPhaseRefresh split, KFL (0.01 ms): 36 the walk
over every object, 37 the rebuild (begin/set/commit) when something changed,
38 updateMoving, 39 refreshes this frame (x0.01), 40 refreshes that rebuilt
(x0.01), 41 objects walked (x0.01 = /100). Not with SLAP/GW/REB/TLAP/CLAP/CID/OLAP."""
import sys
root = sys.argv[1]
T = 'Tyra::FrameProfile::ticks()'
p = root + '/src/gen/game_collision.gen.cpp'
s = open(p, encoding='utf-8').read()


def rep(a, b):
    global s
    assert s.count(a) == 1, (a[:70], s.count(a))
    s = s.replace(a, b)


rep('  movedIdx.clear();\n  movedBox.clear();\n',
    '  movedIdx.clear();\n  movedBox.clear();\n'
    '  const u32 bw0_ = TICKS; kf::labNow[39] += 2949u; kf::labNow[41] += 29u * (u32)n;\n'.replace('TICKS', T))
rep('  if (!changed) {\n'
    '    if (!movedIdx.empty())\n'
    '      broadPhase.updateMoving(movedIdx.data(), movedBox.data(), (int)movedIdx.size());\n'
    '    return;\n'
    '  }\n'
    '  broadPhase.begin(n);\n',
    '  const u32 bw1_ = TICKS; kf::labNow[36] += bw1_ - bw0_;\n'
    '  if (!changed) {\n'
    '    if (!movedIdx.empty())\n'
    '      broadPhase.updateMoving(movedIdx.data(), movedBox.data(), (int)movedIdx.size());\n'
    '    kf::labNow[38] += TICKS - bw1_;\n'
    '    return;\n'
    '  }\n'
    '  struct BpEnd_ { u32 t0; ~BpEnd_() { kf::labNow[37] += TICKS - t0; kf::labNow[40] += 2949u; } } bpEnd_{bw1_};\n'
    '  broadPhase.begin(n);\n'.replace('TICKS', T))
open(p, 'w', encoding='utf-8', newline='\n').write(s)
p = root + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;'
assert s.count(a) == 1
s = s.replace(a, 'kf::labRec[f_ - 200][k_] = (k_ < kf::labN || (k_ >= 36 && k_ < 44)) ? q(kf::labNow[k_]) : 0;')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('bplap ok')
