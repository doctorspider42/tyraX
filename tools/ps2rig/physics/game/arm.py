"""game/arm.py ARM - boot-time arm switch for same-ELF A/B: frame 1 reads
host:arm.txt (one digit, bit mask) into kfr::arm and logs 'ARMSEL n'.
Arms (game side):
  bit0 (1): static batches + roads + procedural chunks drawn inside one
            StaPip submission batch scope (they were outside any scope when
            the interleave is off).
Engine-side arms read Tyra::StaPipCore::probeArm (set from kfr::arm too).
Run after game/route.py."""
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
    "namespace kfr {\n",
    "namespace kfr {\ninline int arm = 0;\n")])

edit('src/terrain_game.cpp', [(
    'void TerrainGame::loop() {\n',
    'void TerrainGame::loop() {\n'
    '  if (g_frameStamp == 1) {\n'
    '    FILE* af_ = fopen("host:arm.txt", "r");\n'
    '    if (af_) { int v_ = 0; if (fscanf(af_, "%d", &v_) == 1) kfr::arm = v_; fclose(af_); }\n'
    '    Tyra::g_spanBases = (kfr::arm & 8) != 0;\n'
    '    Tyra::StaPipCore::probeArm = (u32)kfr::arm;\n'
    '    TYRA_LOG("ARMSEL ", kfr::arm);\n'
    '  }\n'),
    ('    if (f_ == 300) for (int i_ = 0; i_ < 64; ++i_) Tyra::StaPipCore::probeBag[i_] = {};\n',
     '    if (f_ == 300) for (int i_ = 0; i_ < 64; ++i_) Tyra::StaPipCore::probeBag[i_] = {};\n'
     '    if (f_ == 300) { Tyra::StaPipBagPackager::probeClsT = Tyra::StaPipBagPackager::probeClsN = 0; for (int w_ = 0; w_ < 12; ++w_) Tyra::StaPipCore::probeWhy[w_] = 0; Tyra::StaPipCore::probeSendI = Tyra::StaPipCore::probeSendD = Tyra::StaPipCore::probeSendN = Tyra::StaPipCore::probeSendT = 0; }\n'
     '    if (f_ == 1300) TYRA_LOG("KFRCLS ", Tyra::StaPipBagPackager::probeClsT, " ", Tyra::StaPipBagPackager::probeClsN, " send ", Tyra::StaPipCore::probeSendN, " ", Tyra::StaPipCore::probeSendT, " ", Tyra::StaPipCore::probeSendI, " ", Tyra::StaPipCore::probeSendD, " lean ", Tyra::StaPipCore::probeLeanChecked, " bad ", Tyra::StaPipCore::probeLeanBad, " why ", Tyra::StaPipCore::probeWhy[1], " ", Tyra::StaPipCore::probeWhy[2], " ", Tyra::StaPipCore::probeWhy[3], " ", Tyra::StaPipCore::probeWhy[4], " ", Tyra::StaPipCore::probeWhy[5], " ", Tyra::StaPipCore::probeWhy[6], " ", Tyra::StaPipCore::probeWhy[7], " ", Tyra::StaPipCore::probeWhy[8], " ", Tyra::StaPipCore::probeWhy[9]);\n'),
])

edit('src/gen/game_physics.gen.cpp', [
    ('  { const u32 ct=costStart(); renderStaticBatches(); costEnd("Static_batches",-1,ct); }\n',
     '  if (kfr::arm & 1) stapip.core.beginSubmissionBatch();\n'
     '  { const u32 ct=costStart(); renderStaticBatches(); costEnd("Static_batches",-1,ct); }\n'),
    ('  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n',
     '  { const u32 ct=costStart(); renderProcChunks(); costEnd("Procedural",-1,ct); }\n'
     '  if (kfr::arm & 1) stapip.core.endSubmissionBatch();\n'),
])
print('arm ok')
