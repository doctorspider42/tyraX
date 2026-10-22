# Imported by make_arm.py for --stamplight: what the light section of a stamp
# hit costs (t.light = pick + spot build + no-influence test).
def apply(eng, edit):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    t = p.read_text(encoding='utf-8-sig')
    if 'cld_probe.hpp' not in t:
        p.write_bytes(('#include "debug/cld_probe.hpp"\n' + t).encode())
    edit(p, '  const RendererCoreSpotLight* light = nullptr;\n  const bool fastLight',
         '  const Cld::M cldL0 = Cld::mark();\n'
         '  const RendererCoreSpotLight* light = nullptr;\n  const bool fastLight')
    edit(p, '  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) STAMP_REJECT(7);\n',
         '  if ((meshSpot.enabled ? 1 : 0) != s->spotActive) STAMP_REJECT(7);\n'
         '  Cld::span("t.light", cldL0, Cld::mark());\n')
