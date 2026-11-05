"""engine/stamplap.py ENGINE - COP0 laps inside a SUCCESSFUL tryStamp:
probeStampLap[k] += time between consecutive marks, probeStampLapN = hits.
Marks: 0 entry, 1 found+sweep, 2 producer facts, 3 renderer facts+prim,
4 visibility, 5 texture, 6 light pick+spot, 7 batch+wrap, 8 texture use+start,
9 copy+fixups, 10 end."""
import sys, os
root = sys.argv[1]  # the engine directory


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    if 'probeStampLap' in s:
        return
    for a, b in pairs:
        assert s.count(a) == 1, (rel, a[:70], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


edit('inc/renderer/3d/pipeline/static/core/stapip_core.hpp', [
    ('  static inline u32 probeWarm = 0, probeInWarm = 0;\n',
     '  static inline u32 probeWarm = 0, probeInWarm = 0;\n'
     '  static inline u32 probeStampLap[11] = {}, probeStampLapN = 0, probeSL_[11] = {};\n'),
])
f = 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
M = lambda k: '  StaPipCore::probeSL_[%d] = StaPipCore::probeTicks();\n' % k
edit(f, [
    ('  if (!stampEnabled || stamps == nullptr) return false;\n  sweepStamps();\n  StampEntry* s = findStamp(bag, false);\n',
     M(0) + '  if (!stampEnabled || stamps == nullptr) return false;\n  sweepStamps();\n  StampEntry* s = findStamp(bag, false);\n' + M(1)),
    ('    STAMP_REJECT(2);\n  // A moved model is not a different submission',
     '    STAMP_REJECT(2);\n' + M(2) + '  // A moved model is not a different submission'),
    ('  if (stampPrimKey(bag) != s->primKey) STAMP_REJECT(4);\n',
     '  if (stampPrimKey(bag) != s->primKey) STAMP_REJECT(4);\n' + M(3)),
    ('      STAMP_REJECT(5);\n  }\n  // Texture: resident',
     '      STAMP_REJECT(5);\n  }\n' + M(4) + '  // Texture: resident'),
    ('    STAMP_REJECT(6);\n  // The dynamic light:',
     '    STAMP_REJECT(6);\n' + M(5) + '  // The dynamic light:'),
    ('  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) STAMP_REJECT(7);\n',
     '  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) STAMP_REJECT(7);\n' + M(6)),
    ('#undef STAMP_REJECT\n\n  // ---- committed',
     '#undef STAMP_REJECT\n' + M(7) + '\n  // ---- committed'),
    ('  qword_t* dst = packet->next;\n  for (u32 i = 0; i < s->qw; ++i) dst[i] = s->bytes[i];\n',
     M(8) + '  qword_t* dst = packet->next;\n  for (u32 i = 0; i < s->qw; ++i) dst[i] = s->bytes[i];\n'),
    ('  packet->next = dst + s->qw;\n  if (s->wrapBlock) {',
     '  packet->next = dst + s->qw;\n' + M(9) + '  if (s->wrapBlock) {'),
    ('    flushPendingPacket();\n  ++stampHits;\n  return true;\n',
     '    flushPendingPacket();\n  ++stampHits;\n'
     '  if (!StaPipCore::probeWarm) {\n'
     '    const u32 e_ = StaPipCore::probeTicks();\n'
     '    for (int k_ = 1; k_ < 10; ++k_) StaPipCore::probeStampLap[k_] += StaPipCore::probeSL_[k_] - StaPipCore::probeSL_[k_ - 1];\n'
     '    StaPipCore::probeStampLap[10] += e_ - StaPipCore::probeSL_[9];\n'
     '    ++StaPipCore::probeStampLapN;\n'
     '  }\n'
     '  return true;\n'),
])
print('stamplap engine ok')
