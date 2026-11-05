"""engine/site.py ENGINE - (after engine/stampcen.py) per call-site census.
The game sets StaPipCore::probeSite before every render() (game/site.py);
deferred stamps carry their site through the queue. Cold laps only:
probeSiteTab[128] = {site, stampN, stampT, otherN, otherT, verts}."""
import sys, os
root = sys.argv[1]  # the engine directory


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    if 'probeSiteTab' in s:
        return
    for a, b in pairs:
        assert s.count(a) == 1, (rel, a[:60], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


edit('inc/renderer/3d/pipeline/static/core/stapip_core.hpp', [
    ('  static inline u32 probeWarm = 0, probeInWarm = 0;\n',
     '  static inline u32 probeWarm = 0, probeInWarm = 0;\n'
     '  static inline u32 probeSite = 0, probeCurSite = 0, probeDeferSite[64] = {};\n'
     '  struct ProbeSite { u32 site, sn, st, on, ot, v; };\n'
     '  static inline ProbeSite probeSiteTab[128] = {};\n'
     '  static void probeSiteAdd(u32 route, u32 t, u32 v) {\n'
     '    for (int i = 0; i < 128; ++i) {\n'
     '      ProbeSite& s = probeSiteTab[i];\n'
     '      if (s.site == 0 && s.sn == 0 && s.on == 0) s.site = probeCurSite;\n'
     '      if (s.site == probeCurSite) { if (route == 1) { ++s.sn; s.st += t; } else { ++s.on; s.ot += t; } s.v += v; return; }\n'
     '    }\n'
     '  }\n'),
    ('      ProbeRoute& r = probeWarm ? probeRouteW[route] : probeRoute[route];\n',
     '      ProbeRoute& r = probeWarm ? probeRouteW[route] : probeRoute[route];\n'
     '      if (!probeWarm && route != 10) probeSiteAdd(route, t1 - t0, verts);\n'),
])
edit('src/renderer/3d/pipeline/static/core/stapip_core.cpp', [
    ('      deferred[deferredCount++] = {bag, reception};\n',
     '      probeDeferSite[deferredCount] = probeSite;\n'
     '      deferred[deferredCount++] = {bag, reception};\n'),
    ('#endif\n  renderNow(bag, reception);\n}\n',
     '#endif\n  probeCurSite = probeSite;\n  renderNow(bag, reception);\n}\n'),
    ('    renderNow(deferred[i].bag, deferred[i].reception);\n',
     '    probeCurSite = probeDeferSite[i];\n'
     '    renderNow(deferred[i].bag, deferred[i].reception);\n'),
])
print('site engine ok')
