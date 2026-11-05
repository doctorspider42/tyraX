"""game/bakeev2.py ARM - the first bake-event probe with a window from env BEV_F0..BEV_F1
(default 480..510) and terrain-chunk owners (chunk / chunkL / chunkAO /
chunkGS / chunkEM cx,cz) besides objN.pK and batchN."""
import os, sys
F0 = os.environ.get('BEV_F0', '480')
F1 = os.environ.get('BEV_F1', '510')
p = sys.argv[1] + '/src/terrain_game.cpp'
s = open(p, encoding='utf-8').read()
a = 'void TerrainGame::loop() {\n'
assert s.count(a) == 1
s = s.replace(a, a + '  Tyra::StaPipBakedStreams::probeFrame = g_frameStamp;\n'
                     '  Tyra::StaPipBakedStreams::probeArm = (g_frameStamp >= %s && g_frameStamp < %s) ? 1 : 0;\n' % (F0, F1))
a = '      kf::dumped = true;\n'
assert s.count(a) == 1
L = [
    '      for (u32 e_ = 0; e_ < Tyra::StaPipBakedStreams::probeEvN; ++e_) {',
    '        const auto& ev = Tyra::StaPipBakedStreams::probeEv[e_];',
    '        char who[64]; snprintf(who, sizeof(who), "?%x", ev.ptr & 0xFFFFFF);',
    '        for (size_t oi = 0; oi < objectGeometry.size(); ++oi)',
    '          for (size_t pi = 0; pi < objectGeometry[oi].parts.size(); ++pi)',
    '            if (reinterpret_cast<u32>(objectGeometry[oi].parts[pi].vertices.data()) == ev.ptr)',
    '              snprintf(who, sizeof(who), "obj%d.p%d%s", (int)oi, (int)pi, objectGeometry[oi].matrixMode ? "m" : "w");',
    '        for (size_t ci_ = 0; ci_ < terrainChunks.size(); ++ci_) {',
    '          const TerrainChunk& tc_ = terrainChunks[ci_];',
    '          auto is_ = [&](const Tyra::StaPipBag* b_) { return b_ && reinterpret_cast<u32>(b_->vertices) == ev.ptr; };',
    '          if (is_(tc_.bag.get())) snprintf(who, sizeof(who), "chunk%d,%d", tc_.cx, tc_.cz);',
    '          if (is_(tc_.aoBag.get())) snprintf(who, sizeof(who), "chunkAO%d,%d", tc_.cx, tc_.cz);',
    '          if (is_(tc_.gsBag.get())) snprintf(who, sizeof(who), "chunkGS%d,%d", tc_.cx, tc_.cz);',
    '          if (is_(tc_.emisBag.get())) snprintf(who, sizeof(who), "chunkEM%d,%d", tc_.cx, tc_.cz);',
    '          for (const auto& lp_ : tc_.layerPasses) if (is_(lp_.bag.get())) snprintf(who, sizeof(who), "chunkL%d,%d", tc_.cx, tc_.cz);',
    '        }',
    '        for (size_t bi_ = 0; bi_ < staticBatches.size(); ++bi_)',
    '          if (staticBatches[bi_].bag && reinterpret_cast<u32>(staticBatches[bi_].bag->vertices) == ev.ptr)',
    '            snprintf(who, sizeof(who), "batch%d", (int)bi_);',
    '        TYRA_LOG("KFEV f ", (int)ev.frame, " kind ", (int)ev.kind, " count ", (int)ev.count, " pk ", (int)(ev.qw & 0xFFFF), " prog ", (int)(ev.ticks & 0xFFFFFF), " ", who);',
    '      }',
]
s = s.replace(a, a + '\n'.join(L) + '\n')
open(p, 'w', encoding='utf-8', newline='\n').write(s)
print('bakeev2 ok')
