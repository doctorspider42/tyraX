"""engine/arm.py - RIG ENGINE ONLY (engine/setup.py): StaPipCore::probeArm (boot arm mask set by
the game, game/arm.py) + StaPipBagPackager::probeClsT/N (ticks/calls of
checkFrustum inside create, to split classification from record fill).
Idempotent."""
import sys
root = sys.argv[1].rstrip('/') + '/'


def edit(rel, pairs):
    p = root + rel
    s = open(p, encoding='utf-8', newline='').read()
    assert '\r' not in s
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (rel, a[:70], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('inc/renderer/3d/pipeline/static/core/stapip_core.hpp', [
    ('  static inline u32 probeSendN = 0, probeSendT = 0;\n',
     '  static inline u32 probeSendN = 0, probeSendT = 0;\n  static inline u32 probeArm = 0;\n')])

P = 'inc/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.hpp'
s = open(root + P, encoding='utf-8').read()
i = s.index('class StaPipBagPackager {')
j = s.index('public:', i) + len('public:')
if 'probeClsT' not in s:
    s = s[:j] + '\n  static inline u32 probeClsT = 0, probeClsN = 0, probeCreateT = 0;\n' + s[j:]
    open(root + P, 'w', encoding='utf-8', newline='').write(s)

edit('src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp', [
    ('    result[i].isInFrustum = coarse && coarseRoute != PARTIALLY_IN_FRUSTUM\n'
     '        ? coarseRoute : checkFrustum(\n'
     '        result[i], capturePlaneMasks ? &result[i].clipPlaneMask : nullptr,\n'
     '        &result[i].guardBandOnly);\n',
     '    { u32 c0_; asm volatile("mfc0 %0, $9" : "=r"(c0_));\n'
     '    result[i].isInFrustum = coarse && coarseRoute != PARTIALLY_IN_FRUSTUM\n'
     '        ? coarseRoute : checkFrustum(\n'
     '        result[i], capturePlaneMasks ? &result[i].clipPlaneMask : nullptr,\n'
     '        &result[i].guardBandOnly);\n'
     '    u32 c1_; asm volatile("mfc0 %0, $9" : "=r"(c1_)); probeClsT += c1_ - c0_; ++probeClsN; }\n'),
])
print('arm engine ok')
