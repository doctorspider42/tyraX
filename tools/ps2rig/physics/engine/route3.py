"""engine/route3.py - RIG ENGINE ONLY (engine/setup.py), after engine/route2.py:
- probeRoute[r].sn / st: sendPacket calls + ticks inside that route's bags;
- probeRoute[r].q[0..4]: prepare split - packager/planes, texture+wrap,
  program, light pick, fog/info/stamp-arm, (q[5]) sendObjectData+MVP;
- probeBag[64]: census of partial-replay (route 5) bags by vertex pointer.
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
    ('  struct ProbeRoute { u32 n, v, t, s, b, p, d, f; u32 e[8]; u32 c[3]; };\n',
     '  struct ProbeRoute { u32 n, v, t, s, b, p, d, f; u32 e[8]; u32 c[3]; u32 sn, st; u32 q[6]; };\n'
     '  static inline u32 probeSendN = 0, probeSendT = 0;\n'
     '  struct ProbeBag { u32 ptr, count, n, drop, cull, clip, stripped, tex, mx, my, mz; };\n'
     '  static inline ProbeBag probeBag[64] = {};\n'
     '  static inline const void* probeCurBag = nullptr;\n'
     '  static inline s32 probeCurModel[3] = {};\n'
     '  static inline u32 probeCurFlags = 0;\n'),
    ('    u32 m[7] = {}; u32 c[3] = {};\n',
     '    u32 m[7] = {}; u32 c[3] = {}; u32 sn0 = probeSendN, st0 = probeSendT; u32 pq[5] = {};\n'),
    ('      r.c[0] += c[0]; r.c[1] += c[1]; r.c[2] += c[2];\n',
     '      r.c[0] += c[0]; r.c[1] += c[1]; r.c[2] += c[2];\n'
     '      r.sn += probeSendN - sn0; r.st += probeSendT - st0;\n'
     '      if (tP && pq[4]) { u32 prev = tB; for (int k = 0; k < 5; ++k) { if (pq[k]) { r.q[k] += pq[k] - prev; prev = pq[k]; } } r.q[5] += tP - prev; }\n'
     '      if (route == 5 && probeCurBag) {\n'
     '        const u32 key = reinterpret_cast<u32>(probeCurBag);\n'
     '        for (int i = 0; i < 64; ++i) {\n'
     '          ProbeBag& b = probeBag[i];\n'
     '          if (b.ptr == 0) { b.ptr = key; b.count = verts; b.stripped = probeCurFlags & 1; b.tex = probeCurFlags >> 1;\n'
     '            b.mx = probeCurModel[0]; b.my = probeCurModel[1]; b.mz = probeCurModel[2]; }\n'
     '          if (b.ptr == key) { ++b.n; b.drop += c[0]; b.cull += c[1]; b.clip += c[2]; break; }\n'
     '        }\n'
     '      }\n'),
])
C = 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(C, [
    ('  probeRl_.tS = probeTicks();\n',
     '  probeRl_.tS = probeTicks();\n'
     '  probeCurBag = bag->vertices; probeCurFlags = (bag->stripped ? 1u : 0u) | (bag->texture ? 2u : 0u);\n'
     '  probeCurModel[0] = (s32)bag->info->model->data[12]; probeCurModel[1] = (s32)bag->info->model->data[13]; probeCurModel[2] = (s32)bag->info->model->data[14];\n'),
    ('  TYRA_ATTRIB_ADD(prepPackagerTicks, attribPackagerStart);\n',
     '  TYRA_ATTRIB_ADD(prepPackagerTicks, attribPackagerStart);\n  probeRl_.pq[0] = probeTicks();\n'),
    ('  TYRA_ATTRIB_ADD(prepTextureTicks, attribTextureStart);\n',
     '  TYRA_ATTRIB_ADD(prepTextureTicks, attribTextureStart);\n  probeRl_.pq[1] = probeTicks();\n'),
    ('  TYRA_ATTRIB_ADD(prepProgramTicks, attribProgramStart);\n',
     '  TYRA_ATTRIB_ADD(prepProgramTicks, attribProgramStart);\n  probeRl_.pq[2] = probeTicks();\n'),
    ('  TYRA_ATTRIB_ADD(prepLightTicks, attribLightStart);\n',
     '  TYRA_ATTRIB_ADD(prepLightTicks, attribLightStart);\n  probeRl_.pq[3] = probeTicks();\n'),
    ('  qbufferRenderer.setClipBandTight(bag->info->tightClipBand);\n  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);\n',
     '  qbufferRenderer.setClipBandTight(bag->info->tightClipBand);\n  probeRl_.pq[4] = probeTicks();\n  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);\n'),
])
