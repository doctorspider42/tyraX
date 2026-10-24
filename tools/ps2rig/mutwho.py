# Imported by make_arm.py for --mutwho: who owns the arena's mutable copies.
# arm 0: nothing extra; arm 1: every bag drawn through renderObjectBag opts in
# to immutableStreams; arm 2: renderWorldBag bags too (registrations persist,
# so arm 2 = objects + world). TIMING/BYTES ONLY - dynamic bags borrowed.
def apply(eng, proj, edit, include):
    p = proj / 'src/gen/game_scene.gen.cpp'
    include(p)
    edit(p, 'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n',
         'void TerrainGame::renderObjectBag(int owner, Tyra::StaPipBag* bag) {\n'
         '  if (Cld::arm >= 1) bag->immutableStreams = true;\n')
    edit(p, 'void TerrainGame::renderWorldBag(Tyra::StaPipBag* bag) {\n',
         'void TerrainGame::renderWorldBag(Tyra::StaPipBag* bag) {\n'
         '  if (Cld::arm >= 2) bag->immutableStreams = true;\n')
