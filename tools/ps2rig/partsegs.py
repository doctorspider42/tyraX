# Imported by make_arm.py for --partsegs: lean COP0 ticks inside the partial routes.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    def wrap(old, name, count=1):
        edit(p, old, '{ const unsigned cldP0 = Cld::ticks();\n' + old + 'Cld::scope("' + name + '", Cld::ticks() - cldP0); }\n', count)
    # packager.create in the partial branch (stripped + list) - classification lives here
    a = '      auto packages = packager.create(&packagesCount, bag, maxVertCount);\n      TYRA_ATTRIB_ADD(dsCreateTicks, attribCreateStart);\n      Verbose("Material - partial, stripped. Packages: ", packagesCount);\n'
    edit(p, a, '      const unsigned cldPc = Cld::ticks();\n' + a + '      Cld::scope("P.createS", Cld::ticks() - cldPc);\n')
    # whole stripped render / list render
    wrap('      renderStrippedPkgs(packages, doClip, packagesCount);\n', 'P.renderS')
    wrap('      renderPkgs(packages, doClip, packagesCount);\n', 'P.renderL')
    # inside the package loops: the cull emits and the clip/expand work
    edit(p, '      if (retainCurrentBag) buffer->retainIndex = static_cast<int>(i);\n      buffer->bakeIndex = static_cast<int>(i);\n      qbufferRenderer.cull(buffer);\n',
         '      if (retainCurrentBag) buffer->retainIndex = static_cast<int>(i);\n      buffer->bakeIndex = static_cast<int>(i);\n      { const unsigned cldC0 = Cld::ticks(); qbufferRenderer.cull(buffer); Cld::scope("P.cullPkg", Cld::ticks() - cldC0); }\n', 2)
    edit(p, '            auto* buffer = qbufferRenderer.getBuffer();\n            if (!(TYRA_STAPIP_EXPANSION_CACHE &&\n                  qbufferRenderer.fillCachedStrip(buffer, pkg, first, n)))\n              buffer->fillByStripExpand(pkg, first, n);\n            if (route == StaPipStripSubranges::Cull) qbufferRenderer.cull(buffer);\n            else qbufferRenderer.clip(buffer);\n',
         '            const unsigned cldE0 = Cld::ticks();\n            auto* buffer = qbufferRenderer.getBuffer();\n            if (!(TYRA_STAPIP_EXPANSION_CACHE &&\n                  qbufferRenderer.fillCachedStrip(buffer, pkg, first, n)))\n              buffer->fillByStripExpand(pkg, first, n);\n            const unsigned cldE1 = Cld::ticks();\n            Cld::scope("P.expand", cldE1 - cldE0);\n            if (route == StaPipStripSubranges::Cull) qbufferRenderer.cull(buffer);\n            else qbufferRenderer.clip(buffer);\n            Cld::scope(route == StaPipStripSubranges::Cull ? "P.expCull" : "P.expClip", Cld::ticks() - cldE1);\n')
    edit(p, '    buffer->clipPlaneMask = planeMask;\n    qbufferRenderer.clip(buffer);\n',
         '    buffer->clipPlaneMask = planeMask;\n    { const unsigned cldK0 = Cld::ticks(); qbufferRenderer.clip(buffer); Cld::scope("P.slice", Cld::ticks() - cldK0); }\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribFlushStart);\n  qbufferRenderer.flushBuffers();\n',
         '  TYRA_ATTRIB_MARK(attribFlushStart);\n  { const unsigned cldF0 = Cld::ticks(); qbufferRenderer.flushBuffers(); if (!directRoute) Cld::scope("P.flush", Cld::ticks() - cldF0); }\n')
