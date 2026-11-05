"""game/site.py ARM - (after game/stampcen.py) every renderWorldBag /
renderObjectBag call sets StaPipCore::probeSite = line | file << 16 (file ids
below); zeroed at frame 300, logged at 1300 as
'KFRSITE site stampN stampT otherN otherT verts'."""
import sys, os, re
root = sys.argv[1]
files = ['game_collision.gen.cpp', 'game_lighting.gen.cpp', 'game_physics.gen.cpp',
         'game_scene.gen.cpp', 'game_vehicles.gen.cpp']
for fid, name in enumerate(files, 1):
    p = os.path.join(root, 'src/gen', name)
    s = open(p, encoding='utf-8').read()
    if 'probeSite =' in s:
        continue
    macro = ('#define renderWorldBag(x) (Tyra::StaPipCore::probeSite = (__LINE__ | (%d << 16)), renderWorldBag(x))\n'
             '#define renderObjectBag(o, x) (Tyra::StaPipCore::probeSite = (__LINE__ | (%d << 16)), renderObjectBag(o, x))\n') % (fid, fid)
    if name == 'game_scene.gen.cpp':
        # after both definitions
        anchor = 'void TerrainGame::renderWorldBag(Tyra::StaPipBag* bag) {'
        i = s.index(anchor)
        j = s.index('\n}\n', i) + 3
        s = s[:j] + macro + s[j:]
    else:
        incs = list(re.finditer(r'^#include .*\n', s, re.M))
        j = incs[-1].end()
        s = s[:j] + macro + s[j:]
    open(p, 'w', encoding='utf-8', newline='\n').write(s)

p = os.path.join(root, 'src/terrain_game.cpp')
s = open(p, encoding='utf-8').read()
a = '        TYRA_LOG("KFRCEN ", c_[0], " ", c_[1], " ", c_[2], " ", c_[3], " ", c_[4], " ", c_[5]);\n'
assert s.count(a) == 1 and 'KFRSITE' not in s
s = s.replace(a, a +
     '        for (int i_ = 0; i_ < 128; ++i_) { const auto& b_ = Tyra::StaPipCore::probeSiteTab[i_]; if (!b_.sn && !b_.on) break;'
     ' TYRA_LOG("KFRSITE ", b_.site, " ", b_.sn, " ", b_.st, " ", b_.on, " ", b_.ot, " ", b_.v); }\n')
a2 = '      if (f_ == 300) for (int i_ = 0; i_ < 64; ++i_) Tyra::StaPipCore::probeStampBag[i_] = {};\n'
assert s.count(a2) == 1
s = s.replace(a2, a2 + '      if (f_ == 300) for (int i_ = 0; i_ < 128; ++i_) Tyra::StaPipCore::probeSiteTab[i_] = {};\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('site game ok')
