"""engine/stampx.py - RIG ENGINE ONLY (engine/setup.py): tryStamp reject reasons into
StaPipCore::probeWhy[12] (1 none, 2 producer, 3 renderer, 4 prim, 5 view,
6 tex, 7 spot, 8 wrap, 9 early view) and arm bit1 (probeArm & 2): the
visibility test first, when the model matrix is unchanged since capture.
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


edit('inc/renderer/3d/pipeline/static/core/stapip_core.hpp', [
    ('  static inline u32 probeArm = 0;\n',
     '  static inline u32 probeArm = 0;\n  static inline u32 probeWhy[12] = {};\n')])
Q = 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(Q, [
    ('#define STAMP_REJECT(n) return false\n',
     '#define STAMP_REJECT(n) do { ++StaPipCore::probeWhy[n]; return false; } while (0)\n'),
    ('  StampEntry* s = findStamp(bag, false);\n  if (s == nullptr) STAMP_REJECT(1);\n',
     '  StampEntry* s = findStamp(bag, false);\n  if (s == nullptr) STAMP_REJECT(1);\n'
     '  if ((StaPipCore::probeArm & 2) && s->vertices == bag->vertices &&\n'
     '      s->bboxVersion == bag->bboxVersion &&\n'
     '      mmi::equal64(s->model.data, bag->info->model->data)) {\n'
     '    const CoreBBoxFrustum f0 = CoreBBox::frustumCheckAABB(planes, s->wlo, s->whi);\n'
     '    if (f0 == OUTSIDE_FRUSTUM ||\n'
     '        (f0 != IN_FRUSTUM &&\n'
     '         (guardBandPlanes == nullptr ||\n'
     '          CoreBBox::activePlaneMaskAABB(guardBandPlanes, s->wlo, s->whi, 8) != 0)))\n'
     '      STAMP_REJECT(9);\n'
     '  }\n'),
])
print('stampx engine ok')
