# Imported by make_arm.py for --firstdraw: what a rebuilt chunk's first draw pays (lean-safe ticks).
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    include(p)
    edit(p, '''    bbox = cacher.getBBoxes(bag->vertices, bag->count,
                            reinterpret_cast<u32>(bag->vertices),
                            bag->bboxVersion, maxVertCount);
''', '''    { const unsigned cf0 = Cld::ticks();
    bbox = cacher.getBBoxes(bag->vertices, bag->count,
                            reinterpret_cast<u32>(bag->vertices),
                            bag->bboxVersion, maxVertCount);
    Cld::scope("F.bbox", Cld::ticks() - cf0); }
''')
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    t = p.read_text(encoding='utf-8-sig')
    a = '''bool StaPipQBufferRenderer::bakeBlock(packet2_t* packet, u32 fromQw, u32 toQw,
                                      StaPipBakedEntry* entry, u32 index) {
'''
    assert t.count(a) == 1
    t = t.replace(a, a + '  struct CldT { unsigned t0 = Cld::ticks(); ~CldT() { Cld::scope("F.bake", Cld::ticks() - t0); } } cldT;\n')
    b = '''void StaPipQBufferRenderer::endBakedBag() {
  StaPipBakedEntry* entry = bakedCurrent;
'''
    assert t.count(b) == 1
    t = t.replace(b, '''void StaPipQBufferRenderer::endBakedBag() {
  struct CldT { unsigned t0 = Cld::ticks(); ~CldT() { Cld::scope("F.endBake", Cld::ticks() - t0); } } cldT;
  StaPipBakedEntry* entry = bakedCurrent;
''')
    p.write_bytes(t.encode())
