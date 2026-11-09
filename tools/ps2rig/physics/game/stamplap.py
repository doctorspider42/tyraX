"""game/stamplap.py ARM - (after game/site.py) zeroes probeStampLap at frame
300, logs 'KFRSLAP n l1..l10' (ticks, frames 300..1300) at 1300."""
import sys, os
root = sys.argv[1]
p = os.path.join(root, 'src/terrain_game.cpp')
s = open(p, encoding='utf-8').read()
a = '      if (f_ == 300) for (int i_ = 0; i_ < 128; ++i_) Tyra::StaPipCore::probeSiteTab[i_] = {};\n'
assert s.count(a) == 1 and 'KFRSLAP' not in s
s = s.replace(a, a + '      if (f_ == 300) { for (int k_ = 0; k_ < 11; ++k_) Tyra::StaPipCore::probeStampLap[k_] = 0; Tyra::StaPipCore::probeStampLapN = 0; }\n')
b = '        TYRA_LOG("KFRCEN ", c_[0], " ", c_[1], " ", c_[2], " ", c_[3], " ", c_[4], " ", c_[5]);\n'
assert s.count(b) == 1
s = s.replace(b, b + '        { char l_[300]; int o_ = snprintf(l_, sizeof(l_), "KFRSLAP %u", Tyra::StaPipCore::probeStampLapN); for (int k_ = 1; k_ < 11; ++k_) o_ += snprintf(l_ + o_, sizeof(l_) - o_, " %u", Tyra::StaPipCore::probeStampLap[k_]); TYRA_LOG(l_); }\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('stamplap game ok')
