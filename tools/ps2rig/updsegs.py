# Imported by make_arm.py for --updsegs: split G.update into scopes U.l0..U.l9
# using the loop's own UPD_LAP marks (the TYRA_FRAME_PROFILE=0 branch is
# replaced) plus three extra marks inside lap 9: camera collision, dyn lights,
# dyn-lit objects.
def apply(proj, edit):
    p = proj / 'src/terrain_game.cpp'
    edit(p, '#define UPD_LAP(i) ((void)0)\n#endif\n',
         '#endif\n#undef UPD_LAP\n'
         '  static const char* const cldLapN[] = {"U.l0","U.l1","U.l2","U.l3","U.l4","U.l5","U.l6","U.l7","U.l8","U.l9","U.cam","U.dynL","U.dynLit"};\n'
         '  unsigned cldLapT = Cld::ticks();\n'
         '#define UPD_LAP(i) do { const unsigned cldNow = Cld::ticks(); Cld::scope(cldLapN[i], cldNow - cldLapT); cldLapT = cldNow; } while (0)\n')
    edit(p, '  collideVehicleCamera();\n', '  UPD_LAP(8);\n  collideVehicleCamera();\n  UPD_LAP(10);\n')
    edit(p, '  updateDynLights(engine, scriptCtx);\n  updateDynLitObjects();\n',
         '  UPD_LAP(9);\n  updateDynLights(engine, scriptCtx);\n  UPD_LAP(11);\n  updateDynLitObjects();\n  UPD_LAP(12);\n')
