"""game/warm.py ARM - (after game/route.py) zeroes probeRouteW/probeCen at
frame 300, snapshots them at 1200 (same 900-frame window as KFRSUM) and logs
'KFRWARM r <29 fields>' + 'KFRCEN partN partT noclipN noclipT allinN allinT'
at frame 1300."""
import sys, os
root = sys.argv[1]
p = os.path.join(root, 'src/terrain_game.cpp')
s = open(p, encoding='utf-8').read()
a = '    if (f_ == 1300) TYRA_LOG("KFRPMU ", kfr::pmuF, " ", kfr::pmuI, " ", kfr::pmuD);\n'
assert s.count(a) == 1 and 'KFRWARM' not in s
b = (a +
     '    {\n'
     '      static Tyra::StaPipCore::ProbeRoute w_[12]; static u32 c_[6];\n'
     '      if (f_ == 300) { for (int r_ = 0; r_ < 12; ++r_) Tyra::StaPipCore::probeRouteW[r_] = {}; for (int k_ = 0; k_ < 6; ++k_) Tyra::StaPipCore::probeCen[k_] = 0; }\n'
     '      if (f_ == 1200) { for (int r_ = 0; r_ < 12; ++r_) w_[r_] = Tyra::StaPipCore::probeRouteW[r_]; for (int k_ = 0; k_ < 6; ++k_) c_[k_] = Tyra::StaPipCore::probeCen[k_]; }\n'
     '      if (f_ == 1300) {\n'
     '        for (int r_ = 0; r_ < 11; ++r_) { char l_[400]; int o_ = snprintf(l_, sizeof(l_), "KFRWARM %d", r_); const u32* x_ = &w_[r_].n; for (int k_ = 0; k_ < 29; ++k_) o_ += snprintf(l_ + o_, sizeof(l_) - o_, " %u", x_[k_]); TYRA_LOG(l_); }\n'
     '        TYRA_LOG("KFRCEN ", c_[0], " ", c_[1], " ", c_[2], " ", c_[3], " ", c_[4], " ", c_[5]);\n'
     '      }\n'
     '    }\n')
s = s.replace(a, b)
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('warm game ok')
