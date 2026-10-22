# Imported by make_arm.py for --headsegs (needs --routetime): where a non-stamp
# partial/strip bag's time goes before and after its package loop. Lean ticks.
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
    edit(p, '#if TYRA_VU1_EXP_PAINT\n', '  const unsigned cldHA = Cld::ticks();\n#if TYRA_VU1_EXP_PAINT\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribPackagerStart);\n', '  const unsigned cldHB = Cld::ticks();\n  TYRA_ATTRIB_MARK(attribPackagerStart);\n')
    edit(p, '  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);\n',
         '  const unsigned cldHC = Cld::ticks();\n  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);\n  const unsigned cldHD = Cld::ticks();\n')
    edit(p, '  bool replayedWhole = false;\n', '  const unsigned cldHE = Cld::ticks();\n  bool replayedWhole = false;\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribRetainStart);\n', '  const unsigned cldHF = Cld::ticks();\n  TYRA_ATTRIB_MARK(attribRetainStart);\n')
    edit(p, '  TYRA_ATTRIB_MARK(attribFlushStart);\n', '  const unsigned cldHG = Cld::ticks();\n  TYRA_ATTRIB_MARK(attribFlushStart);\n')
    edit(p, '  Cld::scope(cldRn, Cld::ticks() - cldR0);\n',
         '  Cld::scope(cldRn, Cld::ticks() - cldR0);\n'
         '  if (cldRn[2] == \'p\' || cldRn[2] == \'s\') {\n'
         '    const unsigned cldHZ = Cld::ticks();\n'
         '    Cld::scope("H.miss", cldHA - cldR0);\n'
         '    Cld::scope("H.bounds", cldHB - cldHA);\n'
         '    Cld::scope("H.prep", cldHC - cldHB);\n'
         '    Cld::scope("H.objdata", cldHD - cldHC);\n'
         '    Cld::scope("H.mid", cldHE - cldHD);\n'
         '    Cld::scope("H.bake", cldHF - cldHE);\n'
         '    Cld::scope("H.route", cldHG - cldHF);\n'
         '    Cld::scope("H.flush", cldHZ - cldHG);\n'
         '  }\n')
