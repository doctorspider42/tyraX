# Imported by make_arm.py for --bagcensus: at rig frames 3000 (parked) and
# 3880 (driving) print every StaPip bag the frame submits: bag, vertex stream,
# count, texture, info, model pointer + translation, and whether it stamped.
# Counts only (PCSX2): the point is WHICH bags share a mesh.
def apply(proj, eng, edit):
    h = eng / 'inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp'
    edit(h, '  u32 takeStampHits() { const u32 v = stampHits; stampHits = 0; return v; }',
         '  u32 takeStampHits() { const u32 v = stampHits; stampHits = 0; return v; }\n'
         '  u32 peekStampHits() const { return stampHits; }')
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, 'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n',
         'struct CldBagCensus {\n'
         '  StaPipBag* bag; StaPipQBufferRenderer* q; u32 h0; bool on;\n'
         '  ~CldBagCensus() {\n'
         '    if (!on) return;\n'
         '    const M4x4* m = bag->info ? bag->info->model : nullptr;\n'
         '    printf("BAG f=%u bag=%p v=%p n=%d tex=%p info=%p model=%p t=%d,%d,%d stamp=%u strip=%d\\n", Cld::frame,\n'
         '      (void*)bag, (void*)bag->vertices, bag->count,\n'
         '      (void*)(bag->texture ? bag->texture->texture : nullptr), (void*)bag->info, (void*)m,\n'
         '      m ? (int)m->data[12] : 0, m ? (int)m->data[13] : 0, m ? (int)m->data[14] : 0,\n'
         '      q->peekStampHits() - h0, bag->stripped ? 1 : 0);\n'
         '  }\n'
         '};\n'
         'void StaPipCore::renderNow(StaPipBag* bag, StaPipLightReception reception) {\n'
         '  CldBagCensus cldCen{bag, &qbufferRenderer, qbufferRenderer.peekStampHits(),\n'
         '                      Cld::frame == 3000 || Cld::frame == 3880};\n')
