# Imported by make_arm.py for --partscopes: COP0 scopes inside the partial route.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '      auto packages1By3 =\n          packager.create(&subpkgsSize, packages[i], clipPackageSize());\n',
         '      const Cld::M cldSc0 = Cld::mark();\n      auto packages1By3 =\n          packager.create(&subpkgsSize, packages[i], clipPackageSize());\n      Cld::span("p.subcreate", cldSc0, Cld::mark());\n')
    edit(p, '      renderSubpkgs(packages1By3, subpkgsSize);\n    } else {\n      recordPackage(packages[i], OUTSIDE_FRUSTUM);\n',
         '      { Cld::S cldSr("p.subrender"); renderSubpkgs(packages1By3, subpkgsSize); }\n    } else {\n      recordPackage(packages[i], OUTSIDE_FRUSTUM);\n')
    edit(p, '            if (!(TYRA_STAPIP_EXPANSION_CACHE &&\n                  qbufferRenderer.fillCachedStrip(buffer, pkg, first, n)))\n              buffer->fillByStripExpand(pkg, first, n);\n',
         '            { Cld::S cldEx("s.expand");\n            if (!(TYRA_STAPIP_EXPANSION_CACHE &&\n                  qbufferRenderer.fillCachedStrip(buffer, pkg, first, n)))\n              buffer->fillByStripExpand(pkg, first, n); }\n')
    edit(p, '        if (!(TYRA_STAPIP_EXPANSION_CACHE &&\n              qbufferRenderer.fillCachedStrip(buffer, packages[i], t, n)))\n          buffer->fillByStripExpand(packages[i], t, n);\n',
         '        { Cld::S cldEx2("s.expand2");\n        if (!(TYRA_STAPIP_EXPANSION_CACHE &&\n              qbufferRenderer.fillCachedStrip(buffer, packages[i], t, n)))\n          buffer->fillByStripExpand(packages[i], t, n); }\n')
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    edit(p, 'void StaPipQBufferRenderer::addBuffersDataToPacket(const u32& from,\n                                                   const u32& to,\n                                                   const bool& finalize) {\n',
         'void StaPipQBufferRenderer::addBuffersDataToPacket(const u32& from,\n                                                   const u32& to,\n                                                   const bool& finalize) {\n  Cld::S cldAb("q.addBuffers");\n')
    edit(p, 'void StaPipQBufferRenderer::sendPacket() {\n', 'void StaPipQBufferRenderer::sendPacket() {\n  Cld::S cldSp("q.sendPacket");\n')
