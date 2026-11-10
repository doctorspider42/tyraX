"""engine/stampcen.py ENGINE - census of the bags that take the stamp route:
probeStampBag[64] keyed by bag->vertices (shared mesh => shared key), with
hits, vertex count, dynamic-light-receive flag and a sample model position."""
import sys, os
root = sys.argv[1]  # the engine directory


def edit(rel, pairs):
    p = os.path.join(root, rel)
    s = open(p, encoding='utf-8').read()
    if 'probeStampBag' in s:
        return
    for a, b in pairs:
        assert s.count(a) == 1, (rel, a[:60], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='\n').write(s)


edit('inc/renderer/3d/pipeline/static/core/stapip_core.hpp', [
    ('  static inline u32 probeWarm = 0, probeInWarm = 0;\n',
     '  static inline u32 probeWarm = 0, probeInWarm = 0;\n'
     '  struct ProbeStampBag { u32 ptr, n, count, light, info; s32 mx, my, mz; };\n'
     '  static inline ProbeStampBag probeStampBag[64] = {};\n'
     '  static void probeStamp(const void* v, u32 count, u32 light, const void* info, const float* m) {\n'
     '    const u32 key = reinterpret_cast<u32>(v);\n'
     '    for (int i = 0; i < 64; ++i) {\n'
     '      ProbeStampBag& b = probeStampBag[i];\n'
     '      if (b.ptr == 0) { b.ptr = key; b.count = count; b.light = light; b.info = reinterpret_cast<u32>(info); b.mx = (s32)m[12]; b.my = (s32)m[13]; b.mz = (s32)m[14]; }\n'
     '      if (b.ptr == key) { ++b.n; return; }\n'
     '    }\n'
     '  }\n'),
])
edit('src/renderer/3d/pipeline/static/core/stapip_core.cpp', [
    ('      probeRl_.route = 1;\n      return;\n',
     '      probeRl_.route = 1;\n'
     '      if (!probeWarm) probeStamp(bag->vertices, (u32)bag->count, bag->info->dynamicLightReceive ? 1u : 0u, bag->info, bag->info->model->data);\n'
     '      return;\n'),
])
print('stampcen engine ok')
