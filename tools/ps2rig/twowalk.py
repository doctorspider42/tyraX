# Imported by make_engine_probe.py for --twowalk: the two-players example in
# gameplay instead of its title screen. The title menu is closed after frame
# 60 and every player walks forward in a slow circle (no pad needed), so the
# windows sample split-screen gameplay with both characters animating.
def apply(proj, edit):
    p = proj / 'src/terrain_game.cpp'
    edit(p, '  const bool gameMenuWasOpen = gameMenuIndex >= 0;  // before updateGameMenu()\n',
         '  const bool gameMenuWasOpen = gameMenuIndex >= 0;  // before updateGameMenu()\n'
         '  if (gameMenuIndex >= 0 && g_frameStamp > 60) gameMenuIndex = -1;  // rig: no title menu\n')
    p = proj / 'src/gen/game_collision.gen.cpp'
    edit(p, '  const float fx = sinf(P.yaw);\n  const float fz = cosf(P.yaw);\n'
            '  const float forward = -axisL(leftJoy.v);\n',
         '  P.yaw += 0.02F;  // rig: walk in circles\n'
         '  const float fx = sinf(P.yaw);\n  const float fz = cosf(P.yaw);\n'
         '  const float forward = 0.7F; (void)leftJoy;  // rig\n')
    print('two-players walk')
