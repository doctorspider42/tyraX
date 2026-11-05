"""engine/warm.py ENGINE - (after engine/lean.py) two probes for the per-bag
cost follow-up:
  arm bit 32: every renderNow runs twice in a row; the second (warm) lap goes
    to probeRouteW[route] instead of probeRoute (not reset per frame).
  census: route-5 (partial replay) bags with no clip package (c[2] == 0),
    and of those with no dropped package either (all drawn as-is). Counted
    only on the create() path (arm bit 4 off), cold laps only."""
import sys, os
root = sys.argv[1]  # the engine directory


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    if 'probeRouteW' in s or 'probeInWarm' in s:
        return
    for a, b in pairs:
        assert s.count(a) == 1, (rel, a[:60], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


hdr = 'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(hdr, [
    ('  static inline ProbeRoute probeRoute[12] = {};\n',
     '  static inline ProbeRoute probeRoute[12] = {};\n'
     '  static inline ProbeRoute probeRouteW[12] = {};\n'
     '  static inline u32 probeWarm = 0, probeInWarm = 0;\n'
     '  static inline u32 probeCen[6] = {};  // partN partT noclipN noclipT allinN allinT\n'),
    ('      ProbeRoute& r = probeRoute[route];\n',
     '      ProbeRoute& r = probeWarm ? probeRouteW[route] : probeRoute[route];\n'
     '      if (route == 5 && !probeWarm && (c[0] + c[1] + c[2]) != 0) {\n'
     '        probeCen[0]++; probeCen[1] += t1 - t0;\n'
     '        if (c[2] == 0) { probeCen[2]++; probeCen[3] += t1 - t0; if (c[0] == 0) { probeCen[4]++; probeCen[5] += t1 - t0; } }\n'
     '      }\n'),
])

src = 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(src, [
    ('void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n'
     '  ProbeRouteLap probeRl_(',
     'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n'
     '  if ((probeArm & 32) && !probeInWarm) {\n'
     '    probeInWarm = 1;\n'
     '    renderNow(bag, reception);\n'
     '    probeWarm = 1;\n'
     '    renderNow(bag, reception);\n'
     '    probeWarm = 0;\n'
     '    probeInWarm = 0;\n'
     '    return;\n'
     '  }\n'
     '  ProbeRouteLap probeRl_('),
])
print('warm engine ok')
