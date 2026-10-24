# Imported by make_arm.py for --streamab: arm 1 = the old slice / strip-subrange
# clip routes, other arms = whole-package streamed clipping (same ELF).
def apply(proj, edit, include):
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n'
         '  stapip.core.setStreamClip(Cld::arm != 1);\n')
