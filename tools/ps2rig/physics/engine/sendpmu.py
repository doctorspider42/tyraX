"""engine/sendpmu.py - RIG ENGINE ONLY (engine/setup.py), after engine_route3b/engine_pmu: PMU
misses inside StaPipQBufferRenderer::sendPacket (probeSendI/D), whole run."""
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
    ('  static inline u32 probeArm = 0;\n',
     '  static inline u32 probeArm = 0;\n  static inline u32 probeSendI = 0, probeSendD = 0;\n')])
edit('src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp', [
    ('  struct ProbeSend_ { u32 t0 = StaPipCore::probeTicks(); ~ProbeSend_() {',
     '  struct ProbeSend_ { u32 t0 = StaPipCore::probeTicks(), i0 = StaPipCore::probePc0(), d0 = StaPipCore::probePc1(); ~ProbeSend_() {'
     ' StaPipCore::probeSendI += StaPipCore::probePc0() - i0; StaPipCore::probeSendD += StaPipCore::probePc1() - d0;')])
print('sendpmu engine ok')
