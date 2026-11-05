"""game/stampcen.py ARM - (after game/warm.py) zeroes probeStampBag at frame
300 and logs 'KFRSTAMP ptr count n light info at x,y,z' at frame 1300
(counts cover frames 300..1300)."""
import sys, os
root = sys.argv[1]
p = os.path.join(root, 'src/terrain_game.cpp')
s = open(p, encoding='utf-8').read()
a = '        TYRA_LOG("KFRCEN ", c_[0], " ", c_[1], " ", c_[2], " ", c_[3], " ", c_[4], " ", c_[5]);\n'
assert s.count(a) == 1 and 'KFRSTAMP' not in s
b = (a +
     '        for (int i_ = 0; i_ < 64; ++i_) { const auto& b_ = Tyra::StaPipCore::probeStampBag[i_]; if (!b_.ptr) break;'
     ' TYRA_LOG("KFRSTAMP ", b_.ptr, " count ", b_.count, " n ", b_.n, " light ", b_.light, " info ", b_.info, " at ", (int)b_.mx, ",", (int)b_.my, ",", (int)b_.mz); }\n')
s = s.replace(a, b)
a2 = '      if (f_ == 300) { for (int r_ = 0; r_ < 12; ++r_) Tyra::StaPipCore::probeRouteW[r_] = {};'
assert s.count(a2) == 1
s = s.replace(a2, '      if (f_ == 300) for (int i_ = 0; i_ < 64; ++i_) Tyra::StaPipCore::probeStampBag[i_] = {};\n' + a2)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('stampcen game ok')
