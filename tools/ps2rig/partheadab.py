# Imported by make_arm.py for --partheadab (3 arms, same ELF):
#   arm 1 = partly batched models OFF (all-or-nothing, as before 1.175.0);
#   arm 2 = crossing-bag head stamps OFF;  arm 0 = both on.
# Partial batching is decided at scene load, which the rig's reset runs BEFORE
# the new arm is chosen - so it keys off the cycle that is about to start.
def apply(proj, edit, include):
    import glob, pathlib
    hit = None
    for f in glob.glob(str(proj / 'src/gen/*.cpp')):
        t = pathlib.Path(f).read_text(encoding='utf-8-sig')
        if 'if (SCENE_OBJECTS[i].type == 5 && expected <= 32u) continue;  // partly' in t:
            hit = pathlib.Path(f)
    assert hit is not None, 'partial batching anchor not found'
    include(hit)
    edit(hit, 'if (SCENE_OBJECTS[i].type == 5 && expected <= 32u) continue;  // partly',
         'if ((Cld::cycle % CLD_ARMS) != 1 && SCENE_OBJECTS[i].type == 5 && expected <= 32u) continue;  // partly')
    p = proj / 'src/gen/game_physics.gen.cpp'
    edit(p, '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n',
         '  const u32 profScene0 = DEBUG_SHOW_PROFILER ? profTicks() : 0;\n'
         '  stapip.core.setHeadStampEnabled(Cld::arm != 2);\n')
