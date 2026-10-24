# --firstdraw2: expansion cache + strip subranges + the whole stripped partial route (lean-safe ticks).
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    t = p.read_text(encoding='utf-8-sig')
    a = 'bool StaPipQBufferRenderer::fillCachedStrip(StaPipQBuffer* buffer,\n    const StaPipBagPackage& pkg, u32 first, u32 triangles) {\n'
    assert t.count(a) == 1
    t = t.replace(a, a + '  struct CldT { unsigned t0 = Cld::ticks(); ~CldT() { Cld::scope("F.expand", Cld::ticks() - t0); } } cldT;\n')
    p.write_bytes(t.encode())
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    include(p)
    edit(p, '      renderStrippedPkgs(packages, doClip, packagesCount);\n',
         '      { const unsigned cs0 = Cld::ticks(); renderStrippedPkgs(packages, doClip, packagesCount); Cld::scope("F.stripRoute", Cld::ticks() - cs0); }\n')
