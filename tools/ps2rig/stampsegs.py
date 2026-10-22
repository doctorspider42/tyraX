# Imported by make_arm.py for --stampsegs: per-segment ticks (+ PMU misses when not
# --lean) on the tryStamp HIT path. Updated 2026-10-14 for the any-size batch rule.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    def mark(anchor, name):
        edit(p, anchor, '  const Cld::M ' + name + ' = Cld::mark();\n' + anchor)
    edit(p, '  if (!stampEnabled || stamps == nullptr) return false;\n  sweepStamps();\n',
         '  if (!stampEnabled || stamps == nullptr) return false;\n  const Cld::M cm0 = Cld::mark();\n  sweepStamps();\n')
    mark('  if (memcmp(&s->model, bag->info->model, sizeof(M4x4)) != 0) {\n    s->model = *bag->info->model;\n    stampWorldBox(*s);\n  }\n', 'cm1')
    mark('  {\n    const CoreBBoxFrustum f = CoreBBox::frustumCheckAABB(planes, s->wlo, s->whi);\n', 'cm2')
    mark('  const Texture* tex = static_cast<const Texture*>(s->texture);\n  if (tex != nullptr && !rendererCore->texture.isResident(tex)) STAMP_REJECT(6);\n', 'cm3')
    mark('  const RendererCoreSpotLight* light = nullptr;\n  if (dynamicLightReceive && bag->info->dynLightPick) {\n    const M4x4& m = s->model;\n', 'cm4')
    mark('  {\n    const bool repeatNow =\n', 'cm5')
    mark('  clipper.setSpot(meshSpot);\n  ensureProgramSet(false);\n', 'cm6')
    mark('  auto* packet = packets[context];\n  if (!objectDataPending) {\n    packet2_reset(packet, false);\n    packetWrapSet = false;\n  }\n  qword_t* dst = packet->next;\n', 'cm7')
    mark('  // The same bookkeeping replayWholeBakedBag does after its REF.\n', 'cm8')
    edit(p, '    flushPendingPacket();\n  ++stampHits;\n  return true;\n',
         '    flushPendingPacket();\n  { const Cld::M cm9 = Cld::mark();\n'
         '    Cld::span("s.find", cm0, cm1); Cld::span("s.facts", cm1, cm2); Cld::span("s.frustum", cm2, cm3);\n'
         '    Cld::span("s.tex", cm3, cm4); Cld::span("s.light", cm4, cm5); Cld::span("s.batch", cm5, cm6);\n'
         '    Cld::span("s.progtex", cm6, cm7); Cld::span("s.copy", cm7, cm8); Cld::span("s.flush", cm8, cm9);\n'
         '    Cld::span("s.total", cm0, cm9);\n'
         '    static unsigned cldLastEnd = 0;\n'
         '    Cld::span(cm0.t - cldLastEnd < 3000u ? "s.totalHot" : "s.totalCold", cm0, cm9);\n'
         '    cldLastEnd = cm9.t; }\n'
         '  ++stampHits;\n  return true;\n')
