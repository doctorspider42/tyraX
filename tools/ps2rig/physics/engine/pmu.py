"""engine/pmu.py - RIG ENGINE ONLY (engine/setup.py), after engine/route3.py: EE PMU per route.
PCR0 = event 6 (I-cache miss), PCR1 = event 6 (D-cache miss), counting in
kernel+supervisor+user (not EXL). probeRoute[r].im / dm. The game calls
StaPipCore::probePmuFrame() once a frame (zeroes both counters, so a 31-bit
counter never overflows - an overflow is an unmaskable exception).
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


H = 'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(H, [
    ('u32 sn, st; u32 q[6]; };\n',
     'u32 sn, st; u32 q[6]; u32 im, dm; };\n'
     '  static u32 probePc0() { u32 v_; asm volatile("mfpc %0, 0" : "=r"(v_)); return v_; }\n'
     '  static u32 probePc1() { u32 v_; asm volatile("mfpc %0, 1" : "=r"(v_)); return v_; }\n'
     '  static void probePmuFrame() {\n'
     '    const u32 off_ = 0, on_ = (1u << 31) | (6u << 15) | (7u << 12) | (6u << 5) | (7u << 2);\n'
     '    asm volatile("mtps %0, 0\\n sync.p\\n mtpc $0, 0\\n mtpc $0, 1\\n sync.p\\n mtps %1, 0\\n sync.p" :: "r"(off_), "r"(on_));\n'
     '  }\n'),
    ('    u32 m[7] = {}; u32 c[3] = {}; u32 sn0 = probeSendN, st0 = probeSendT; u32 pq[5] = {};\n',
     '    u32 m[7] = {}; u32 c[3] = {}; u32 sn0 = probeSendN, st0 = probeSendT; u32 pq[5] = {};\n'
     '    u32 i0 = probePc0(), d0 = probePc1();\n'),
    ('      r.sn += probeSendN - sn0; r.st += probeSendT - st0;\n',
     '      r.sn += probeSendN - sn0; r.st += probeSendT - st0;\n'
     '      r.im += probePc0() - i0; r.dm += probePc1() - d0;\n'),
])
print('pmu engine ok')
