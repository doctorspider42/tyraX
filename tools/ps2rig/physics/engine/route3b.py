"""engine/route3b.py - RIG ENGINE ONLY (engine/setup.py): times every StaPipQBufferRenderer::sendPacket
into StaPipCore::probeSendN/probeSendT (route3 attributes them per route)."""
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


Q = 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(Q, [
    ('#include "renderer/core/paths/path1/frame_submission.hpp"\n',
     '#include "renderer/core/paths/path1/frame_submission.hpp"\n'
     '#include "renderer/3d/pipeline/static/core/stapip_core.hpp"\n'),
    ('void StaPipQBufferRenderer::sendPacket() {\n',
     'void StaPipQBufferRenderer::sendPacket() {\n'
     '  struct ProbeSend_ { u32 t0 = StaPipCore::probeTicks(); ~ProbeSend_() {'
     ' StaPipCore::probeSendT += StaPipCore::probeTicks() - t0; ++StaPipCore::probeSendN; } } probeSend_;\n'),
])
print('route3b engine ok')
