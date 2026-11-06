# Imported by make_arm.py for --deferab: arm 1 = stamps submitted at their call,
# other arms = stamp deferral (queued runs). Same ELF.
def apply(proj, edit, include):
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n'
         '  stapip.core.setStampDeferral(Cld::arm != 1);\n')
