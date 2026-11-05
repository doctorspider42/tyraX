"""game/kf3.py ARM - on top of game/kf.py + game/kf2.py: renderScene split by its
own cost labels, every frame. costStart always reads the clock; costEnd adds
the row's time to a per-frame slot per label (object rows of the three cars
go to 'Object_car', other object rows to 'Object_other'). Up to 24 labels x
1000 frames; dumped after frame 1300 as KFL lines."""
import sys, os
root = sys.argv[1]


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    for a, b, c in pairs:
        n = s.count(a)
        assert n == c, (rel, a[:60], n)
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


edit('inc/game_runtime.gen.hpp', [
    ("inline int carObj[3] = {-2, -2, -2};\n",
     "inline int carObj[3] = {-2, -2, -2};\n"
     "inline const char* labName[48]; inline int labN = 0; inline u32 churn0 = 0, rqw0 = 0, rtk0 = 0;\n"
     "inline u32 labNow[48];\n"
     "inline unsigned short labRec[1000][48];\n"
     "inline void lab(const char* label, int object, u32 start, u32 now) {\n"
     "  if (object >= 0 && label[0] == 'O' && label[1] == 'b' && label[6] == 0)\n"
     "    label = (object == carObj[0] || object == carObj[1] || object == carObj[2]) ? \"Object_car\" : \"Object_other\";\n"
     "  int k = 0;\n"
     "  while (k < labN && labName[k] != label) ++k;\n"
     "  if (k == labN) { if (labN >= 44) return; labName[labN++] = label; labNow[k] = 0; }\n"
     "  labNow[k] += now - start;\n"
     "}\n", 1),
])
edit('src/gen/game_physics.gen.cpp', [
    ("  auto costStart = [&]() -> u32 { return (costSeq || (Tyra::HardwareTrace::active && Tyra::HardwareTrace::detailed)) ? profTicks() : 0; };",
     "  auto costStart = [&]() -> u32 { return profTicks(); };", 1),
    ("  auto costEnd = [&](const char* label, int object, u32 start) {\n",
     "  auto costEnd = [&](const char* label, int object, u32 start) {\n"
     "    kf::lab(label, object, start, profTicks());\n", 1),
])
edit('src/terrain_game.cpp', [
    ("      r.v[10] = (unsigned short)f_;\n",
     "      r.v[10] = (unsigned short)f_;\n"
     "      for (int k_ = 0; k_ < 48; ++k_) kf::labRec[f_ - 200][k_] = k_ < kf::labN ? q(kf::labNow[k_]) : 0;\n"
     "      kf::labRec[f_ - 200][44] = (unsigned short)((Tyra::StaPipBakedStreams::probeRefreshQw - kf::rqw0) / 100);\n"
     "      kf::labRec[f_ - 200][45] = q(Tyra::StaPipBakedStreams::probeRefreshTicks - kf::rtk0);\n"
     "      kf::labRec[f_ - 200][46] = (unsigned short)(100 * (Tyra::StaPipBakedStreams::probeChurn - kf::churn0));\n"
     "      kf::labRec[f_ - 200][47] = (unsigned short)(100 * (Tyra::StaPipBakedStreams::probeRebuild - kf::reb0));\n", 1),
    ("    kf::cap = kf::dentA",
     "    for (int k_ = 0; k_ < 48; ++k_) kf::labNow[k_] = 0;\n"
     "    kf::churn0 = Tyra::StaPipBakedStreams::probeChurn;\n"
     "    kf::rqw0 = Tyra::StaPipBakedStreams::probeRefreshQw; kf::rtk0 = Tyra::StaPipBakedStreams::probeRefreshTicks;\n"
     "    kf::cap = kf::dentA", 1),
    ("      TYRA_LOG(\"KF fields: f period pre work stall phys veh dmg fx scene speed10 (0.01 ms)\");\n",
     "      TYRA_LOG(\"KF fields: f period pre work stall phys veh dmg fx scene speed10 (0.01 ms)\");\n"
     "      for (int k_ = 0; k_ < kf::labN; ++k_) TYRA_LOG(\"KFLAB \", k_, \" \", kf::labName[k_]);\n"
     "      TYRA_LOG(\"KFLAB 44 #refresh_kqw\"); TYRA_LOG(\"KFLAB 45 refresh_copy_ms\"); TYRA_LOG(\"KFLAB 46 #churn_skips\"); TYRA_LOG(\"KFLAB 47 #full_rebakes\");\n"
     "      for (int b = 0; b < 1000; b += 10) {\n"
     "        char l_[2600]; int o_ = snprintf(l_, sizeof(l_), \"KFL\");\n"
     "        for (int j = b; j < b + 10; ++j) {\n"
     "          o_ += snprintf(l_ + o_, sizeof(l_) - o_, \" %d:\", j + 200);\n"
     "          for (int k_ = 0; k_ < 48; ++k_) o_ += snprintf(l_ + o_, sizeof(l_) - o_, \"%u,\", kf::labRec[j][k_]);\n"
     "        }\n"
     "        TYRA_LOG(l_);\n"
     "      }\n", 1),
])
print('kf3 probe ok')
