"""engine/route.py - RIG ENGINE ONLY (engine/setup.py): per-route cost of
StaPipCore::renderNow, COP0 ticks. StaPipCore::probeRoute[r]:
 n bags, v vertices, t total, s stamp-try (head up to the stamp verdict),
 b bounds, p prepare (texture/light/objectData), d dispatch, f flush+end.
Routes: 0 empty, 1 stamp hit, 2 culled, 3 whole baked replay, 4 direct pkgs,
5 partial replay, 6 partial strip, 7 partial pkgs, 8 partial slices/sub,
9 renderInstances (whole call, nests its renderNow), 10 endSubmissionBatch
(packet send, after the deferred flush). The game zeroes them per frame
(game/route.py). Idempotent."""
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
    ('class StaPipCore {\n public:\n',
     'class StaPipCore {\n public:\n'
     '  // ROUTEPROBE (rig only)\n'
     '  struct ProbeRoute { u32 n, v, t, s, b, p, d, f; };\n'
     '  static inline ProbeRoute probeRoute[12] = {};\n'
     '  static u32 probeTicks() { u32 t_; asm volatile("mfc0 %0, $9" : "=r"(t_)); return t_; }\n'
     '  struct ProbeRouteLap {\n'
     '    u32 t0, tS = 0, tB = 0, tP = 0, tD = 0, route = 0, verts;\n'
     '    explicit ProbeRouteLap(u32 v_) : t0(probeTicks()), verts(v_) {}\n'
     '    ~ProbeRouteLap() {\n'
     '      const u32 t1 = probeTicks();\n'
     '      ProbeRoute& r = probeRoute[route];\n'
     '      ++r.n; r.v += verts; r.t += t1 - t0;\n'
     '      const u32 a = tS ? tS : t0; if (tS) r.s += tS - t0;\n'
     '      if (tB) r.b += tB - a;\n'
     '      if (tP) r.p += tP - tB;\n'
     '      if (tD) { r.d += tD - tP; r.f += t1 - tD; }\n'
     '    }\n'
     '  };\n'),
    ('  void endSubmissionBatch() {\n    flushDeferred();\n    qbufferRenderer.endSubmissionBatch();\n  }\n',
     '  void endSubmissionBatch() {\n    flushDeferred();\n'
     '    const u32 t0_ = probeTicks();\n'
     '    qbufferRenderer.endSubmissionBatch();\n'
     '    probeRoute[10].t += probeTicks() - t0_; ++probeRoute[10].n;\n  }\n'),
])

C = 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(C, [
    ('void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n',
     'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n'
     '  ProbeRouteLap probeRl_(bag->count > 0 ? (u32)bag->count : 0u);\n'),
    ('            &kNoSpotLight))\n      return;\n  }\n#endif\n',
     '            &kNoSpotLight)) {\n      probeRl_.route = 1;\n      return;\n    }\n  }\n#endif\n'
     '  probeRl_.tS = probeTicks();\n'),
    ('      recordOutsideBag(bag);\n      return;\n',
     '      recordOutsideBag(bag);\n      probeRl_.route = 2;\n      return;\n'),
    ('  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;\n',
     '  probeRl_.tB = probeTicks();\n'
     '  const u32 prepareStart = telemetryEnabled ? readCoreTelemetryTicks() : 0;\n'),
    ('  qbufferRenderer.setClipperMVP(&mvp);\n\n  TYRA_ATTRIB_ADD(prepObjectDataTicks',
     '  qbufferRenderer.setClipperMVP(&mvp);\n  probeRl_.tP = probeTicks();\n\n  TYRA_ATTRIB_ADD(prepObjectDataTicks'),
    ('    if (replayedPartial) {\n      // done\n',
     '    if (replayedPartial) {\n      probeRl_.route = 5;\n'),
    ('      Verbose("Material - partial, stripped. Packages: ", packagesCount);\n',
     '      Verbose("Material - partial, stripped. Packages: ", packagesCount);\n      probeRl_.route = 6;\n'),
    ('      renderClipSlices(bag, 0, bag->count, 0x3F);\n',
     '      renderClipSlices(bag, 0, bag->count, 0x3F);\n      probeRl_.route = 8;\n'),
    ('      renderSubpkgs(subpkgs, packagesCount);\n',
     '      renderSubpkgs(subpkgs, packagesCount);\n      probeRl_.route = 8;\n'),
    ('  TYRA_ATTRIB_MARK(attribFlushStart);\n  qbufferRenderer.flushBuffers();\n',
     '  if (probeRl_.route == 0) probeRl_.route = replayedWhole ? 3 : directRoute ? 4 : 7;\n'
     '  probeRl_.tD = probeTicks();\n'
     '  TYRA_ATTRIB_MARK(attribFlushStart);\n  qbufferRenderer.flushBuffers();\n'),
    ('u32 StaPipCore::renderInstances(StaPipBag* bag, const M4x4* models,\n                                u32 count) {\n  if (count == 0 || bag->count == 0) return 0;\n',
     'u32 StaPipCore::renderInstances(StaPipBag* bag, const M4x4* models,\n                                u32 count) {\n  if (count == 0 || bag->count == 0) return 0;\n'
     '  ProbeRouteLap probeRl_(count);\n  probeRl_.route = 9;\n'),
])
print('route engine ok')
