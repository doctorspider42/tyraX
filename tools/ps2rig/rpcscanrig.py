# Imported by make_arm.py for --rpcscan: check SIF RPC client structs every
# frame (cld_rpcscan.hpp). Write bin/rpcscan.txt after the build with
# rpcscan_table.sh <projdir>.
import shutil


def apply(proj, rig, edit):
    shutil.copy(rig / 'cld_rpcscan.hpp', proj / 'inc/cld_rpcscan.hpp')
    p = proj / 'src/terrain_game.cpp'
    t = p.read_text(encoding='utf-8-sig')
    if 'cld_rpcscan.hpp' not in t:
        p.write_bytes(('#include "cld_rpcscan.hpp"\n' + t).encode())
    edit(p, 'void TerrainGame::loop() {\n',
         'void TerrainGame::loop() {\n  CldRpcScan::tick("loopStart");\n')
    edit(p, '  { Cld::S cldEnd("G.endFrame");\n  engine->renderer.endFrame(); }\n',
         '  CldRpcScan::tick("beforeEndFrame");\n'
         '  { Cld::S cldEnd("G.endFrame");\n  engine->renderer.endFrame(); }\n'
         '  CldRpcScan::tick("afterEndFrame");\n')
