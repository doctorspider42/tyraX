"""game/route.py ARM - per-frame StaPip route costs (needs engine/route.py in
the rig engine and game/kf.py first). Frames 200..1199: KFR ring with
(n, t in 0.01 ms) for routes 0..10; window 300..1199 sums of every field.
Dumped with the KF lines (frame 1300): 'KFRSUM r n v t s b p d f' (ticks) and
'KFR f:n0,t0,n1,t1,...'."""
import sys, os
root = sys.argv[1]


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    for a, b in pairs:
        assert s.count(a) == 1, (rel, a[:60], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


edit('inc/game_runtime.gen.hpp', [(
    "namespace kf {\n",
    "namespace kfr {\n"
    "inline unsigned short rec[1000][22];\n"
    "inline u32 sum[11][29];\n"
    "inline u32 pmuI = 0, pmuD = 0, pmuF = 0;\n"
    "}  // namespace kfr\n"
    "namespace kf {\n")])

a = '      r.v[10] = (unsigned short)f_;\n'
edit('src/terrain_game.cpp', [
    (a, a +
     '      for (int r_ = 0; r_ < 11; ++r_) {\n'
     '        const auto& pr_ = Tyra::StaPipCore::probeRoute[r_];\n'
     '        kfr::rec[f_ - 200][2 * r_] = (unsigned short)(pr_.n > 65535 ? 65535 : pr_.n);\n'
     '        kfr::rec[f_ - 200][2 * r_ + 1] = q(pr_.t);\n'
     '        if (f_ >= 300) { const u32* w_ = &pr_.n; for (int k_ = 0; k_ < 29; ++k_) kfr::sum[r_][k_] += w_[k_]; }\n'
     '      }\n'),
    ('    kf::phys = kf::veh = kf::dmg = kf::fx = kf::scene = 0;\n',
     '    kf::phys = kf::veh = kf::dmg = kf::fx = kf::scene = 0;\n'
     '    if (f_ >= 300 && f_ < 1200) { kfr::pmuI += Tyra::StaPipCore::probePc0(); kfr::pmuD += Tyra::StaPipCore::probePc1(); ++kfr::pmuF; }\n'
     '    if (f_ == 1300) TYRA_LOG("KFRPMU ", kfr::pmuF, " ", kfr::pmuI, " ", kfr::pmuD);\n'
     '    for (int r_ = 0; r_ < 12; ++r_) Tyra::StaPipCore::probeRoute[r_] = {};\n'
     + ('    Tyra::StaPipCore::probePmuFrame();\n' if os.environ.get('PMU') else '') +
     '    if (f_ == 300) for (int i_ = 0; i_ < 64; ++i_) Tyra::StaPipCore::probeBag[i_] = {};\n'),
    ('      TYRA_LOG("KF fields: f period pre work stall phys veh dmg fx scene speed10 (0.01 ms)");\n',
     '      TYRA_LOG("KF fields: f period pre work stall phys veh dmg fx scene speed10 (0.01 ms)");\n'
     '      for (int r_ = 0; r_ < 11; ++r_) { char l_[400]; int o_ = snprintf(l_, sizeof(l_), "KFRSUM %d", r_); for (int k_ = 0; k_ < 29; ++k_) o_ += snprintf(l_ + o_, sizeof(l_) - o_, " %u", kfr::sum[r_][k_]); TYRA_LOG(l_); }\n'
     '      for (int i_ = 0; i_ < 64; ++i_) { const auto& b_ = Tyra::StaPipCore::probeBag[i_]; if (!b_.ptr) break;'
     ' TYRA_LOG("KFRBAG ", b_.ptr, " count ", b_.count, " n ", b_.n, " drop ", b_.drop, " cull ", b_.cull, " clip ", b_.clip,'
     ' " strip ", b_.stripped, " tex ", b_.tex, " at ", (int)b_.mx, ",", (int)b_.my, ",", (int)b_.mz); }\n'
     '      for (int b = 0; b < 1000; b += 10) {\n'
     '        char l_[1400]; int o_ = snprintf(l_, sizeof(l_), "KFR");\n'
     '        for (int j = b; j < b + 10; ++j) {\n'
     '          o_ += snprintf(l_ + o_, sizeof(l_) - o_, " %d:", j + 200);\n'
     '          for (int k_ = 0; k_ < 22; ++k_) o_ += snprintf(l_ + o_, sizeof(l_) - o_, "%u,", kfr::rec[j][k_]);\n'
     '        }\n'
     '        TYRA_LOG(l_);\n'
     '      }\n'),
])
print('route game ok')
