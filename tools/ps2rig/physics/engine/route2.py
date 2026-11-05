"""engine/route2.py - RIG ENGINE ONLY (engine/setup.py), after engine/route.py: dispatch sub-laps
(probeRoute[r].e[k], ticks) and partial package census (c[0..2] = packages
routed drop / cull / clip). e1 beginBakedBag, e2 replayWhole try, e3 retain +
beginPartialReplay, e4 packager.create, e5 routes loop, e6 emitPartialRuns,
e7 rest of dispatch after it. Idempotent."""
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
    ('  struct ProbeRoute { u32 n, v, t, s, b, p, d, f; };\n',
     '  struct ProbeRoute { u32 n, v, t, s, b, p, d, f; u32 e[8]; u32 c[3]; };\n'),
    ('    u32 t0, tS = 0, tB = 0, tP = 0, tD = 0, route = 0, verts;\n',
     '    u32 t0, tS = 0, tB = 0, tP = 0, tD = 0, route = 0, verts;\n'
     '    u32 m[7] = {}; u32 c[3] = {};\n'),
    ('      if (tD) { r.d += tD - tP; r.f += t1 - tD; }\n',
     '      if (tD) { r.d += tD - tP; r.f += t1 - tD; }\n'
     '      m[0] = tP;\n'
     '      for (int k = 1; k < 7; ++k) if (m[k] && m[k - 1]) r.e[k] += m[k] - m[k - 1];\n'
     '      if (m[6] && tD) r.e[7] += tD - m[6];\n'
     '      r.c[0] += c[0]; r.c[1] += c[1]; r.c[2] += c[2];\n'),
])
C = 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(C, [
    ('    bakeBag = qbufferRenderer.beginBakedBag(bag, maxVertCount, !directRoute);\n',
     '    bakeBag = qbufferRenderer.beginBakedBag(bag, maxVertCount, !directRoute);\n'
     '    probeRl_.m[1] = probeTicks();\n'),
    ('    replayedWhole = directRoute && bakeBag && qbufferRenderer.replayWholeBakedBag();\n',
     '    replayedWhole = directRoute && bakeBag && qbufferRenderer.replayWholeBakedBag();\n'
     '    probeRl_.m[2] = probeTicks();\n'),
    ('      {\n        static u8 routes[512];\n',
     '      probeRl_.m[3] = probeTicks();\n      {\n        static u8 routes[512];\n'),
    ('        auto packages = packager.create(&packagesCount, bag, maxVertCount);\n        if (packagesCount <= 512) {\n',
     '        auto packages = packager.create(&packagesCount, bag, maxVertCount);\n'
     '        probeRl_.m[4] = probeTicks();\n        if (packagesCount <= 512) {\n'),
    ('            routes[i] = route;\n',
     '            routes[i] = route;\n            ++probeRl_.c[route];\n'),
    ('          replayedPartial = qbufferRenderer.emitPartialRuns(\n',
     '          probeRl_.m[5] = probeTicks();\n'
     '          replayedPartial = qbufferRenderer.emitPartialRuns(\n'),
    ('              routes, packagesCount, partialReplay == 4 ? masks : nullptr);\n',
     '              routes, packagesCount, partialReplay == 4 ? masks : nullptr);\n'
     '          probeRl_.m[6] = probeTicks();\n'),
])
print('route2 engine ok')
