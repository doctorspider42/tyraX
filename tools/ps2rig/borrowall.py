# Imported by make_arm.py for --borrowall: TIMING UPPER BOUND ONLY. Arm 1 treats
# every REF target outside the qbuffer copy pools as immutable (borrowed, not
# copied into the frame arena). Unsafe for arrays refilled per frame - pictures
# from arm 1 mean nothing.
def apply(eng, proj, edit, include):
    p = eng / 'src/renderer/core/paths/path1/vif1_queue.cpp'
    include(p)
    edit(p, 'bool isImmutableSpan(uint32_t address, uint32_t qwords) {\n',
         'bool isImmutableSpan(uint32_t address, uint32_t qwords) {\n'
         '  if (Cld::arm == 1) return true;\n')
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n  Cld::isPool = &Tyra::StaPipQBuffer::isPoolAddress;\n')
