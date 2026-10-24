# Imported by make_arm.py for --singlepassab: runtime A/B of Vif1Queue's
# single-pass recorder. Arm 1 takes the four-pass snapshot route; arms 0 and 2
# the single pass. Counts accepted/fallback single-pass submits.
def apply(eng, edit):
    p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
    t = p.read_text(encoding='utf-8-sig')
    if 'cld_probe.hpp' not in t:
        p.write_bytes(('#include "debug/cld_probe.hpp"\n' + t).encode())
    edit(p, '  if ((TYRA_NATIVE_VIF_RECORD || frameRecording) && sourceCopied) {\n    prepareArena();\n',
         '  if ((TYRA_NATIVE_VIF_RECORD || frameRecording) && sourceCopied && Cld::arm != 1) {\n    prepareArena();\n')
    nl = chr(92) + 'n'
    edit(p, '      const SinglePassResult r = recordSinglePass(chain, qwords);\n',
         '      const SinglePassResult r = recordSinglePass(chain, qwords);\n'
         '      { static unsigned spOk = 0, spFail = 0; if (r.ok) ++spOk; else ++spFail;\n'
         '        if ((spOk + spFail) % 20000 == 0) printf("SINGLEPASS ok=%u fail=%u' + nl + '", spOk, spFail); }\n')
