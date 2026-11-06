# Imported by make_arm.py for --shot: one VRAM frame grab per cycle near the
# end of the parked window (cld_shot.hpp), after endFrame in terrain_game.cpp.
import shutil


def apply(proj, edit, rig):
    shutil.copy(rig / 'cld_shot.hpp', proj / 'inc/cld_shot.hpp')
    p = proj / 'src/terrain_game.cpp'
    t = p.read_text(encoding='utf-8-sig')
    if 'cld_shot.hpp' not in t:
        p.write_bytes(('#include "cld_shot.hpp"\n' + t).encode())
    edit(p, '  { Cld::S cldEnd("G.endFrame");\n  engine->renderer.endFrame(); }\n',
         '  { Cld::S cldEnd("G.endFrame");\n  engine->renderer.endFrame(); }\n'
         '  CldShot::maybe(engine);\n')
