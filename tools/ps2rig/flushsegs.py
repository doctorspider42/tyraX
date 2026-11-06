# Imported by make_arm.py for --flushsegs: where flushBuffers' time goes (lean ticks).
# F.build = addBuffersDataToPacket at the bag's end, F.send = the send that follows
# (if the bag does not stay pending), F.half = half flushes inside cull()/clip().
def apply(eng, edit, include):
    p = eng / 'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    include(p)
    edit(p, '    addBuffersDataToPacket(offset, offset + size, !keepPending);\n    if (!keepPending) {\n      sendPacket();\n',
         '    const unsigned cldF0 = Cld::ticks();\n'
         '    addBuffersDataToPacket(offset, offset + size, !keepPending);\n'
         '    const unsigned cldF1 = Cld::ticks();\n'
         '    Cld::scope("F.build", cldF1 - cldF0);\n'
         '    if (!keepPending) {\n      sendPacket();\n'
         '      Cld::scope("F.send", Cld::ticks() - cldF1);\n')
    edit(p, '    addBuffersDataToPacket(from, to);\n    sendPacket();\n  }\n}\n\nvoid StaPipQBufferRenderer::clip(',
         '    const unsigned cldH0 = Cld::ticks();\n    addBuffersDataToPacket(from, to);\n    sendPacket();\n'
         '    Cld::scope("F.halfCull", Cld::ticks() - cldH0);\n  }\n}\n\nvoid StaPipQBufferRenderer::clip(')
