# Imported by make_arm.py for --lightab: arm 1 runs the old light pick (full
# entries, per-frame sphere + inverse); arms 0 and 2 the fast one.
def apply(proj, edit):
    p = proj / 'src/terrain_game.cpp'
    edit(p, 'void TerrainGame::loop() {\n',
         'void TerrainGame::loop() {\n'
         '  Tyra::RendererCore::fastLightPick = Cld::arm != 1;\n')
