# Imported by make_arm.py for --submitsegs: Cld scopes on every pass the VIF1
# submission makes over a producer's chain (preflight walk, chain copy, REF
# fixup, native sizing, native emission), next to the existing q.submit scope.
import re

def apply(eng, edit):
    h = eng / 'inc/renderer/core/paths/path1/frame_chain_arena.hpp'
    t = h.read_text(encoding='utf-8-sig')
    if 'cld_probe.hpp' not in t:
        t = t.replace('#pragma once\n', '#pragma once\n#include "debug/cld_probe.hpp"\n', 1)
    n = 0
    for name in ('SnapshotPreflight', 'SnapshotChainCopy', 'SnapshotFixup'):
        pat = '{ HardwareTrace::Scope trace("%s", HardwareTrace::Kind::Span, true);' % name
        c = t.count(pat)
        n += c
        t = t.replace(pat, pat + ' Cld::S cldSeg("s.%s");' % name[8:].lower())
    if n == 0:
        raise SystemExit('submitsegs: no snapshot scopes found')
    h.write_bytes(t.encode())
    p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
    for name, short in (('NativeSizing', 'n.sizing'), ('NativeEmission', 'n.emit')):
        pat = 'HardwareTrace::Scope trace%s("%s", HardwareTrace::Kind::Span, true);' % (
            'Emit' if name == 'NativeEmission' else '', name)
        if name == 'NativeSizing':
            pat = '{ HardwareTrace::Scope trace("NativeSizing", HardwareTrace::Kind::Span, true);'
        edit(p, pat, pat + ' Cld::S cldSeg("%s");' % short)
    print('submitsegs: snapshot scopes', n)
