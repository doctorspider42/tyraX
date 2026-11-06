# Imported by make_arm.py for --stripsegs: lean COP0 ticks inside renderStrippedPkgs.
# Splits a partial strip bag's package loop into: subrange acquire (+ rebuild count),
# block classification, emits, and the per-package remainder.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '  for (u16 i = 0; i < count; i++) {\n    const bool guardBandOnly = doClip && isGuardBandOnly(packages[i]);\n    const bool cull = (doClip && packages[i].isInFrustum == IN_FRUSTUM) ||\n                      !doClip || guardBandOnly;\n',
         '  for (u16 i = 0; i < count; i++) {\n    const unsigned cldS0 = Cld::ticks();\n    struct CldEnd { unsigned t; ~CldEnd() { Cld::scope("SP.pkg", Cld::ticks() - t); } } cldEnd{cldS0};\n    const bool guardBandOnly = doClip && isGuardBandOnly(packages[i]);\n    const bool cull = (doClip && packages[i].isInFrustum == IN_FRUSTUM) ||\n                      !doClip || guardBandOnly;\n')
    edit(p, '        bool built = false;\n        const auto* entry = stripSubranges ? stripSubranges->acquire(\n            pkg.vertices, pkg.size, trisPerChunk, pkg.bag->bboxVersion,\n            pkg.bag->contentVersion ? *pkg.bag->contentVersion : 0, 0, built) : nullptr;\n',
         '        bool built = false;\n        const unsigned cldA0 = Cld::ticks();\n        const auto* entry = stripSubranges ? stripSubranges->acquire(\n            pkg.vertices, pkg.size, trisPerChunk, pkg.bag->bboxVersion,\n            pkg.bag->contentVersion ? *pkg.bag->contentVersion : 0, 0, built) : nullptr;\n        Cld::scope(built ? "SP.acqBuilt" : "SP.acqHit", Cld::ticks() - cldA0);\n')
    edit(p, '          const u32 blocks = (triangles + trisPerChunk - 1) / trisPerChunk;\n          for (u32 b = 0; b < blocks; ++b) {\n',
         '          const u32 blocks = (triangles + trisPerChunk - 1) / trisPerChunk;\n          const unsigned cldC0 = Cld::ticks();\n          for (u32 b = 0; b < blocks; ++b) {\n')
    edit(p, '          // Keep primitive order. Only contiguous safe list ranges can merge;\n',
         '          Cld::scope("SP.classify", Cld::ticks() - cldC0);\n          { unsigned cldN[3] = {0,0,0}; for (u32 b = 0; b < blocks; ++b) ++cldN[routes[b]]; Cld::scope("SP.bClip", cldN[0]*295u); Cld::scope("SP.bCull", cldN[1]*295u); Cld::scope("SP.bOut", cldN[2]*295u); }\n          const unsigned cldE0 = Cld::ticks();\n          // Keep primitive order. Only contiguous safe list ranges can merge;\n')
    edit(p, '            if (route == StaPipStripSubranges::Cull) qbufferRenderer.cull(buffer);\n            else qbufferRenderer.clip(buffer);\n          }\n          continue;\n',
         '            if (route == StaPipStripSubranges::Cull) qbufferRenderer.cull(buffer);\n            else qbufferRenderer.clip(buffer);\n          }\n          Cld::scope("SP.emit", Cld::ticks() - cldE0);\n          continue;\n')
    # the cull branch of the strip loop
    edit(p, '      if (telemetryEnabled) ++telemetry.packagesStrip;\n      auto buffer = qbufferRenderer.getBuffer();\n',
         '      if (telemetryEnabled) ++telemetry.packagesStrip;\n      Cld::scope("SP.cullPkgN", 295u);\n      auto buffer = qbufferRenderer.getBuffer();\n')
