# Imported by make_arm.py for --clipab: runtime A/B of VU1 clipping. Arm 1
# clips crossing packages on the EE (VU1 then runs only cull programs); arms 0
# and 2 keep VU1 clipping. Pair with --gputl to price the clip programs on VU1.
def apply(proj, edit):
    p = proj / 'src/terrain_game.cpp'
    edit(p, 'void TerrainGame::loop() {\n',
         'void TerrainGame::loop() {\n'
         '  { static int cldClipArm = -1; if (Cld::arm != cldClipArm) { cldClipArm = Cld::arm;\n'
         '      stapip.core.flushDeferred(); stapip.core.setVU1Clipping(CLIP_VU1 && Cld::arm != 1);\n'
         '      printf("CLIPAB arm=%d vu1clip=%d\\n", Cld::arm, (int)(CLIP_VU1 && Cld::arm != 1)); } }\n')
